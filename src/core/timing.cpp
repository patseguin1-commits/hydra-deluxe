#include "core/timing.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hydra {

int to_multiplier(int combo) {
    if (combo < 10) return 1;
    if (combo < 20) return 2;
    if (combo < 30) return 3;
    return 4;
}

// ---- MsIndex ------------------------------------------------------------

MsIndex::MsIndex(const std::map<int64_t, double>& bpm_map, int64_t tick_r) {
    // A song's opening tempo is the mark at tick 0; without it the original
    // walk was a KeyError, so this is too.
    if (bpm_map.find(0) == bpm_map.end())
        throw std::out_of_range("bpm map has no tick-0 entry");

    // std::map already iterates in sorted key order.
    keys_.reserve(bpm_map.size());
    tps_.reserve(bpm_map.size());
    for (const auto& kv : bpm_map) {
        keys_.push_back(kv.first);
        // tps = bpm * tick_r / 60, same order as hymisc.
        tps_.push_back(kv.second * static_cast<double>(tick_r) / 60.0);
    }

    // Accumulated section by section, with the same arithmetic as the walk it
    // replaces so the two agree bit-for-bit.
    elapsed_.assign(keys_.size(), 0.0);
    for (size_t i = 1; i < keys_.size(); ++i) {
        elapsed_[i] = elapsed_[i - 1] +
            static_cast<double>(keys_[i] - keys_[i - 1]) / tps_[i - 1] * 1000.0;
    }
}

double MsIndex::at(int64_t ticks) const {
    // bisect_right(keys, ticks) - 1: index of the last key <= ticks.
    auto it = std::upper_bound(keys_.begin(), keys_.end(), ticks);
    int i = static_cast<int>(it - keys_.begin()) - 1;
    if (i < 0) i = 0;  // Before the first mark: read at the opening tempo.
    return elapsed_[i] +
        static_cast<double>(ticks - keys_[i]) / tps_[i] * 1000.0;
}

// ---- MeasureIndex -------------------------------------------------------

MeasureIndex::MeasureIndex(const std::map<int64_t, int64_t>& tpm_map,
                           int64_t tick_r) {
    (void)tick_r;  // The meter index does not use resolution; kept for parity.
    if (tpm_map.find(0) == tpm_map.end())
        throw std::out_of_range("tpm map has no tick-0 entry");

    keys_.reserve(tpm_map.size());
    tpm_.reserve(tpm_map.size());
    for (const auto& kv : tpm_map) {
        keys_.push_back(kv.first);
        tpm_.push_back(kv.second);
    }

    // State on entering each section: the last barline at or before its first
    // tick, and how many whole measures have been counted by then.
    starts_.assign(keys_.size(), 0);
    measures_.assign(keys_.size(), 0);
    for (size_t i = 1; i < keys_.size(); ++i) {
        // Non-negative operands, so integer truncation equals Python's floor.
        int64_t whole = (keys_[i] - starts_[i - 1]) / tpm_[i - 1];
        starts_[i] = starts_[i - 1] + whole * tpm_[i - 1];
        measures_[i] = measures_[i - 1] + whole;
    }
}

int MeasureIndex::section_at(int64_t ticks) const {
    // bisect_left(keys, ticks) - 1, clamped at 0.
    auto it = std::lower_bound(keys_.begin(), keys_.end(), ticks);
    int i = static_cast<int>(it - keys_.begin()) - 1;
    return i > 0 ? i : 0;
}

// ---- Timecode -----------------------------------------------------------

Timecode::Timecode(int64_t ticks, int64_t tick_r,
                   const MeasureIndex& mbt, const MsIndex& ms)
    : ticks_(ticks) {
    // Measure/beat/tick from the meter map.
    int i = mbt.section_at(ticks_);
    int64_t tpm = mbt.tpm_at(i);

    int64_t remaining = ticks_ - mbt.starts_at(i);
    int64_t whole_measures = remaining / tpm;
    remaining -= whole_measures * tpm;

    int64_t measures = mbt.measures_at(i) + whole_measures;

    mbt_[0] = measures;
    mbt_[1] = remaining / tick_r;
    mbt_[2] = remaining % tick_r;

    measures_decimal_ =
        static_cast<double>(measures) +
        static_cast<double>(remaining) / static_cast<double>(tpm);

    // Milliseconds from the tempo map.
    ms_ = ms.at(ticks_);
}

// ---- SongTiming ---------------------------------------------------------

SongTiming::SongTiming(int64_t tick_r,
                       const std::map<int64_t, int64_t>& tpm_map,
                       const std::map<int64_t, double>& bpm_map)
    : tick_r_(tick_r), mbt_(tpm_map, tick_r), ms_(bpm_map, tick_r) {}

namespace {
// bisect.bisect_left(a, x, lo): leftmost insert position >= lo.
int bisect_left_lo(const MeasureIndex& idx, int64_t x, int lo) {
    int hi = idx.count();
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (idx.measures_at(mid) < x)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}
}  // namespace

Timecode SongTiming::plusmeasure(const Timecode& tc, int64_t add_measures) const {
    // Work in measures, exactly as hymisc does. int() truncates toward zero
    // (C++ double->int64 does the same); Python's `% 1` is the fractional part
    // measured from the floor, so use x - floor(x).
    double m_decimal =
        tc.measures_decimal() + static_cast<double>(add_measures);
    int64_t target_m = static_cast<int64_t>(m_decimal);
    double targetpartial = m_decimal - std::floor(m_decimal);

    const MeasureIndex& idx = mbt_;
    int len = idx.count();

    // First section whose measure count reaches the target barline.
    int j = bisect_left_lo(idx, target_m, 1);
    int i = (j < len ? j : len) - 1;
    int64_t handled_ticks =
        idx.starts_at(i) + (target_m - idx.measures_at(i)) * idx.tpm_at(i);

    int64_t current_tpm;
    if (j < len && handled_ticks == idx.keys_at(j)) {
        // The target barline is the next section's first tick, so the partial
        // measure is measured in that section's meter.
        current_tpm = idx.tpm_at(j);
    } else {
        current_tpm = idx.tpm_at(i);
    }

    int64_t partial =
        static_cast<int64_t>(targetpartial * static_cast<double>(current_tpm));
    return timecode(handled_ticks + partial);
}

}  // namespace hydra
