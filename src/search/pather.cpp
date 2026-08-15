#include "search/pather.h"

#include <algorithm>
#include <chrono>

#include "search/engine.h"
#include "search/graph.h"

namespace hydra {

namespace {

using bench_clock = std::chrono::steady_clock;

// Ceilings the uncapped edition tries, in order. Mirrors hymisc.SP_CAP_LADDER.
const int kSpCapLadder[] = {16, 32, 64, 128, 256, 512};

// Thrown out of the progress callback to abandon a ladder rung that has blown
// the time budget, mirroring hyutil._CapBudgetExceeded / _deadline_callback.
struct CapBudgetExceeded {};

// How many SP phrases the chart offers. No run can bank more bars than this, so
// it's the natural clamp on how tall a graph is worth building.
int count_sp_phrases(const Song& song) {
    int n = 0;
    for (const SongTimestamp& ts : song.sequence)
        if (ts.flag_sp) ++n;
    return n;
}

HydraRecord read(const ScoreGraph& graph, int depth_mode, int depth_value,
                 std::optional<double> ms_filter, bool use_dp,
                 const std::function<void(float)>& on_progress) {
    HydraRecord record;
    record.ms_limit = ms_filter;
    record.paths = run_search(graph, depth_mode, depth_value, ms_filter, use_dp, on_progress);
    return record;
}

}  // namespace

HydraRecord analyze_at_cap(const Song& song, int sp_cap, int depth_mode,
                           int depth_value, std::optional<double> ms_filter,
                           std::optional<int> build_cap,
                           const std::function<void(float)>& on_progress) {
    std::optional<int> cap = build_cap.has_value() ? build_cap
                                                   : std::optional<int>(sp_cap);
    ScoreGraph graph(song, cap);
    HydraRecord record = read(graph, depth_mode, depth_value, ms_filter, false, on_progress);
    record.sp_cap = sp_cap;
    return record;
}

HydraRecord analyze_uncapped(const Song& song, int depth_mode, int depth_value,
                             std::optional<double> ms_filter,
                             const std::function<void(float)>& on_progress,
                             std::optional<double> time_budget_s) {
    int sp_phrases = count_sp_phrases(song);
    const int ladder_n = static_cast<int>(std::size(kSpCapLadder));

    std::optional<HydraRecord> record;
    std::optional<int64_t> previous_score;
    // The clock only starts once the first rung has finished, so there is always
    // a result to report -- exactly like hyutil._analyze_uncapped.
    std::optional<bench_clock::time_point> deadline;

    int rung = 0;
    for (int sp_cap : kSpCapLadder) {
        // Each rung's 0..1 sweep occupies its slice of the overall bar, so the
        // ladder reads as one monotonic progress even though it re-runs the
        // search per ceiling. Early convergence just finishes below 100%. The
        // same per-iteration callback is where a rung overrunning the budget is
        // interrupted (hyutil._deadline_callback), rather than only checking
        // between rungs -- one big-cap rung can dwarf the whole budget.
        auto wrapped = [&](float f) {
            if (deadline && bench_clock::now() > *deadline) throw CapBudgetExceeded{};
            if (on_progress) on_progress((static_cast<float>(rung) + f) /
                                         static_cast<float>(ladder_n));
        };
        int build_cap = std::min(sp_cap, std::max(sp_phrases, 1));
        HydraRecord candidate;
        try {
            candidate = analyze_at_cap(song, sp_cap, depth_mode, depth_value,
                                       ms_filter, build_cap, wrapped);
        } catch (const CapBudgetExceeded&) {
            // Out of time partway up. Keep the best rung that finished; the
            // abandoned one is discarded and the result reads as unsettled.
            break;
        }

        std::optional<int64_t> score;
        if (!candidate.paths.empty())
            score = candidate.best_path().totalscore();

        record = std::move(candidate);

        // Settled: no higher ceiling could bank more than the song offers.
        if (sp_cap >= sp_phrases) {
            record->sp_cap_converged = true;
            return std::move(*record);
        }
        if (previous_score.has_value() && score == previous_score) {
            record->sp_cap_converged = true;
            return std::move(*record);
        }
        previous_score = score;
        ++rung;
        if (!deadline && time_budget_s)
            deadline = bench_clock::now() +
                       std::chrono::duration_cast<bench_clock::duration>(
                           std::chrono::duration<double>(*time_budget_s));
    }

    if (record.has_value()) record->sp_cap_converged = false;
    return std::move(*record);
}

HydraRecord analyze_chart(const Song& song, bool capped, int depth_mode,
                          int depth_value, std::optional<double> ms_filter,
                          std::optional<int> sp_cap,
                          const std::function<void(float)>& on_progress,
                          std::optional<double> time_budget_s) {
    if (song.is_empty())
        throw ChartFileError("No Expert pro drums notes in this chart.");

    if (capped)
        return analyze_at_cap(song, 4, depth_mode, depth_value, ms_filter,
                              std::nullopt, on_progress);
    // Uncapped edition. A manual SP cap (any bar count) runs a single pass at
    // that ceiling instead of the auto-settling ladder. The graph is still only
    // built as tall as the song has phrases to bank -- no run can exceed that --
    // so a huge cap on a short song stays cheap and exact. A forced cap is the
    // user's explicit choice, so the ladder's time budget doesn't apply to it.
    if (sp_cap.has_value()) {
        int build_cap = std::min(*sp_cap, std::max(count_sp_phrases(song), 1));
        return analyze_at_cap(song, *sp_cap, depth_mode, depth_value, ms_filter,
                              build_cap, on_progress);
    }
    return analyze_uncapped(song, depth_mode, depth_value, ms_filter, on_progress,
                            time_budget_s);
}

}  // namespace hydra
