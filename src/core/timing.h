// Timing math: converting between ticks, milliseconds and measures.
//
// A song measures time in ticks; the app also needs milliseconds (from the
// tempo map) and measure/beat/tick and decimal-measure positions (from the
// meter map). Timecode carries all of these for one tick. MsIndex and
// MeasureIndex are the binary-search accelerators the Python side caches on its
// TempoMaps; here they are built once per song into SongTiming and reused.
//
// The arithmetic keeps a fixed operation order and integer floor semantics:
// stored records and pathstrings depend on these ms/measure values
// bit-for-bit, so any change here silently invalidates stored results.

#ifndef HYDRA_CORE_TIMING_H
#define HYDRA_CORE_TIMING_H

#include <cstdint>
#include <map>
#include <vector>

namespace hydra {

// combo -> score multiplier: x1 below 10, x2 below 20, x3 below 30, else x4.
int to_multiplier(int combo);

// Where each tempo section starts and the time elapsed by then.
// Built from a bpm map (tick -> BPM) and the tick resolution.
class MsIndex {
public:
    MsIndex(const std::map<int64_t, double>& bpm_map, int64_t tick_r);

    // Milliseconds at an absolute tick (defined for negative ticks too: they
    // read back at the opening tempo, as the original walk did).
    double at(int64_t ticks) const;

    // Ticks-per-second of the tempo section containing `ticks` (a tick exactly
    // on a tempo change reads the new tempo, same rule as at()).
    double tps_at(int64_t ticks) const;

    // Continuous (sub-tick) variants for the display-layer exact squeeze
    // solver: piecewise-linear, monotone, mutually inverse, and agreeing with
    // at() on integer ticks. NOT part of the bit-for-bit scoring surface (see
    // the header comment) -- never use them in the graph or the engine's
    // scoring path. (The engine's rebuild step does call tick_at_ms through
    // frontend_transfer_scales, but only to fill the display-only transfer
    // fields after all scoring is done.)
    double ms_at_tick_f(double ticks) const;
    double tick_at_ms(double ms) const;

private:
    std::vector<int64_t> keys_;
    std::vector<double> tps_;
    std::vector<double> elapsed_;
};

// Where each meter section starts, in ticks and in whole measures.
// Built from a tpm map (tick -> ticks-per-measure).
class MeasureIndex {
public:
    MeasureIndex(const std::map<int64_t, int64_t>& tpm_map, int64_t tick_r);

    // Which section a tick is measured in (a tick exactly on a boundary is
    // measured with the section before it).
    int section_at(int64_t ticks) const;

    int64_t keys_at(int i) const { return keys_[i]; }
    int64_t tpm_at(int i) const { return tpm_[i]; }
    int64_t starts_at(int i) const { return starts_[i]; }
    int64_t measures_at(int i) const { return measures_[i]; }
    int count() const { return static_cast<int>(keys_.size()); }

private:
    std::vector<int64_t> keys_;
    std::vector<int64_t> tpm_;
    std::vector<int64_t> starts_;
    std::vector<int64_t> measures_;
};

// A point in time in a song, in multiple representations. Compares/hashes on
// ticks alone.
class Timecode {
public:
    Timecode() = default;
    // Build from prebuilt indexes (the common path — indexes are shared).
    Timecode(int64_t ticks, int64_t tick_r,
             const MeasureIndex& mbt, const MsIndex& ms);

    // Raw ticks only, mbt/ms left at their defaults. This is the state a
    // decoded Activation's timecode is in until restore_timecodes
    // (store/serialize.h) resolves it against the song's tempo map — a stored
    // record is deserialized without a SongTiming at hand, so ticks are all
    // that's known until the caller restores them (see restore_timecodes).
    static Timecode raw(int64_t ticks) {
        Timecode tc;
        tc.ticks_ = ticks;
        return tc;
    }

    int64_t ticks() const { return ticks_; }
    // {measure, beat, tick}. Meaningful for ticks >= 0.
    const int64_t* measure_beats_ticks() const { return mbt_; }
    double measures_decimal() const { return measures_decimal_; }
    double ms() const { return ms_; }

    bool operator<(const Timecode& o) const { return ticks_ < o.ticks_; }
    bool operator<=(const Timecode& o) const { return ticks_ <= o.ticks_; }
    bool operator>(const Timecode& o) const { return ticks_ > o.ticks_; }
    bool operator>=(const Timecode& o) const { return ticks_ >= o.ticks_; }
    bool operator==(const Timecode& o) const { return ticks_ == o.ticks_; }
    bool operator!=(const Timecode& o) const { return ticks_ != o.ticks_; }

private:
    int64_t ticks_ = 0;
    int64_t mbt_[3] = {0, 0, 0};
    double measures_decimal_ = 0.0;
    double ms_ = 0.0;
};

// One bar of Star Power lasts two measures. Every "how long does this much SP
// run" call to plusmeasure goes through this, so the rule has one home.
inline constexpr int64_t kMeasuresPerSpBar = 2;
constexpr int64_t sp_bars_to_measures(int64_t bars) { return kMeasuresPerSpBar * bars; }

// A song's timing context: resolution plus the two indexes, built once and
// reused to make Timecodes. The maps must contain a tick-0 entry (as every
// real chart does), or construction throws std::out_of_range — matching the
// KeyError(0) the Python indexes raise.
class SongTiming {
public:
    SongTiming(int64_t tick_r,
               const std::map<int64_t, int64_t>& tpm_map,
               const std::map<int64_t, double>& bpm_map);

    int64_t tick_resolution() const { return tick_r_; }
    const MeasureIndex& measure_index() const { return mbt_; }
    const MsIndex& ms_index() const { return ms_; }

    Timecode timecode(int64_t ticks) const {
        return Timecode(ticks, tick_r_, mbt_, ms_);
    }

    // A Timecode offset from `tc` by whole/partial measures (partial
    // measures scale by percentage of the target section's meter). Used by activation
    // auto-fill placement and, later, the score graph. Not cached here —
    // the caller caches if the call volume warrants it.
    Timecode plusmeasure(const Timecode& tc, int64_t add_measures) const;

    // Local measure duration in ms for time just AFTER `ticks`: the meter
    // section's ticks-per-measure over the tempo section's ticks-per-second.
    // A tick exactly on a meter or tempo change reads the NEW section; pass
    // `ticks - 1` for the duration just before the tick. Display-layer helper
    // (frontend->SP-end squeeze scaling); not part of the scoring surface.
    double ms_per_measure_at(int64_t ticks) const;

    // Continuous measure position <-> tick, and the exact (sub-tick,
    // piecewise-composed) SP end for an activation hit at `act_hit_ms`
    // holding SP for `end_measures` measures. All display-layer only, like
    // ms_per_measure_at: they interpolate in doubles with no plusmeasure tick
    // rounding, so they must never feed the graph/engine/stored records.
    // Right-continuous at section boundaries; exact inverses of each other
    // when meter changes land on barlines (as real charts do -- a mid-measure
    // meter change makes the measure position jump, and these follow the
    // section arrays through it).
    double measures_at_tick_f(double ticks) const;
    double tick_at_measures_f(double measures) const;
    double sp_end_ms(double act_hit_ms, int64_t end_measures) const;

private:
    int64_t tick_r_;
    MeasureIndex mbt_;
    MsIndex ms_;
};

}  // namespace hydra

#endif  // HYDRA_CORE_TIMING_H
