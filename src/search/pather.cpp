#include "search/pather.h"

#include <algorithm>
#include <chrono>

#include "search/engine.h"
#include "search/graph.h"

namespace hydra {

namespace {

using bench_clock = std::chrono::steady_clock;

// Ceilings Auto tries, in order. Mirrors hymisc.SP_CAP_LADDER.
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

HydraRecord read(const ScoreGraph& graph, DepthMode depth_mode, int depth_value,
                 std::optional<double> ms_filter,
                 const std::function<void(float)>& on_progress) {
    HydraRecord record;
    record.ms_limit = ms_filter;
    record.paths = run_search(graph, depth_mode, depth_value, ms_filter,
                              /*no_skips=*/false, /*hard_ms_filter=*/false,
                              on_progress);
    return record;
}

// The share of the progress bar the main search owns. The all-0 pass has no
// activation branching, so it finishes in a small fraction of the time; it gets
// the tail so the bar still moves while it runs and Cancel still has a tick to
// unwind from.
constexpr float kMainProgressShare = 0.9f;

std::function<void(float)> scaled_progress(const std::function<void(float)>& cb,
                                           float lo, float hi) {
    if (!cb) return {};
    return [cb, lo, hi](float f) { cb(lo + f * (hi - lo)); };
}

// Run the all-0 pass over an already-built graph and hang the result on the
// record.
void attach_allzero(const ScoreGraph& graph, HydraRecord& record,
                    const std::function<void(float)>& on_progress) {
    // Nothing to add when the optimal path is itself an all-0 path that needs
    // no squeeze timing: it already answers the question, and it is already at
    // the top of the list.
    if (!record.paths.empty()) {
        const Path& best = record.best_path();
        if (best.is_allzero() && best.difficulty().value_or(0.0) <= 0.0) return;
    }
    try {
        record.allzero_paths = search_allzero(graph, on_progress);
    } catch (const std::exception&) {
        // A broken search state is worth losing the section over, not the whole
        // analysis. Cancel and the ladder's time budget unwind through
        // AnalysisCancelled / CapBudgetExceeded, neither of which derives from
        // std::exception, so both still propagate.
        record.allzero_paths.clear();
    }
}

}  // namespace

std::vector<Path> search_allzero(const ScoreGraph& graph,
                                 const std::function<void(float)>& on_progress) {
    // depth_value 0 keeps only the top score; its tied peers still merge into
    // variants (up to MAX_TIED_PATHS), which is where the E / + / - variations
    // of one all-0 path come from. The 0 ms limit is the point of the feature,
    // so it is fixed here and ignores the user's "Path limit" setting --
    // and it is applied hard. The default soft filter only prefers paths inside
    // the limit and still reports an over-limit one while nothing outscores it,
    // which in a no-skips search (a tiny candidate set, usually one path per
    // group) meant the section routinely showed a path needing hundreds of ms.
    std::vector<Path> paths;
    try {
        paths = run_search(graph, DepthMode::Scores, /*depth_value=*/0,
                           /*ms_filter=*/0.0,
                           /*no_skips=*/true, /*hard_ms_filter=*/true,
                           on_progress);
    } catch (const std::runtime_error&) {
        // The hard filter can empty the frontier: this chart offers no all-0
        // path inside 0 ms. run() reports that the same way it reports a broken
        // state, so both end here as "no all-0 path". Cancel and the ladder's
        // time budget unwind through their own non-std::exception types and
        // still propagate.
        return {};
    }

    // A chart can also refuse every activation opportunity (the calibration
    // fill can never be summoned in time). The search then returns a single
    // path with no activations, whose pathstring is empty.
    if (paths.size() == 1 && !paths[0].has_activations()) paths.clear();
    return paths;
}

namespace {

// One pathing run with a given SP meter ceiling. build_cap, when set, is the
// ceiling the graph is actually built at (the record still reports sp_cap).
// want_allzero also runs search_allzero over the same graph and stores it in
// the record's allzero_paths.
// Mirrors hyutil._analyze_at_cap.
HydraRecord analyze_at_cap(const Song& song, int sp_cap, DepthMode depth_mode,
                           int depth_value, std::optional<double> ms_filter,
                           std::optional<int> build_cap, bool legacy_fills,
                           bool want_allzero = false,
                           const std::function<void(float)>& on_progress = {}) {
    std::optional<int> cap = build_cap.has_value() ? build_cap
                                                   : std::optional<int>(sp_cap);
    ScoreGraph graph(song, cap,
                     legacy_fills ? FillDeadlineRule::Ch10
                                  : FillDeadlineRule::Ch11);
    const bool split = want_allzero && static_cast<bool>(on_progress);
    HydraRecord record = read(
        graph, depth_mode, depth_value, ms_filter,
        split ? scaled_progress(on_progress, 0.0f, kMainProgressShare) : on_progress);
    record.sp_cap = sp_cap;
    // The graph is still alive here, so the all-0 pass reuses it instead of
    // paying for a second build.
    if (want_allzero)
        attach_allzero(graph, record,
                       split ? scaled_progress(on_progress, kMainProgressShare, 1.0f)
                             : on_progress);
    return record;
}

// Auto cap: raise the ceiling up the SP-cap ladder until the score settles,
// approximating "no ceiling at all".
// time_budget_s, if set, abandons a ladder rung that overruns it (the first
// rung always finishes), keeping the best rung so far and flagging it
// unsettled. nullopt runs every rung to completion — what the tests use, so
// their results stay deterministic.
// want_allzero runs the all-0 pass once, after the ladder settles, at the
// settled ceiling -- never per rung.
HydraRecord analyze_auto_cap(const Song& song, DepthMode depth_mode, int depth_value,
                             std::optional<double> ms_filter, bool legacy_fills,
                             bool want_allzero = false,
                             const std::function<void(float)>& on_progress = {},
                             std::optional<double> time_budget_s = std::nullopt) {
    int sp_phrases = count_sp_phrases(song);
    const int ladder_n = static_cast<int>(std::size(kSpCapLadder));

    std::optional<HydraRecord> record;
    std::optional<int64_t> previous_score;
    // The clock only starts once the first rung has finished, so there is always
    // a result to report -- exactly like hyutil._analyze_uncapped (the
    // Python-era name for this ladder).
    std::optional<bench_clock::time_point> deadline;

    // The ladder re-runs the search per ceiling, so no rung runs the all-0 pass:
    // only the settled rung's answer is worth keeping. The tail below runs it
    // once, which costs one extra graph build and one cheap search.
    const bool split = want_allzero && static_cast<bool>(on_progress);
    std::function<void(float)> main_cb =
        split ? scaled_progress(on_progress, 0.0f, kMainProgressShare) : on_progress;

    int rung = 0;
    bool converged = false;
    int settled_build_cap = 1;
    for (int sp_cap : kSpCapLadder) {
        // Each rung's 0..1 sweep occupies its slice of the overall bar, so the
        // ladder reads as one monotonic progress even though it re-runs the
        // search per ceiling. Early convergence just finishes below 100%. The
        // same per-iteration callback is where a rung overrunning the budget is
        // interrupted (hyutil._deadline_callback), rather than only checking
        // between rungs -- one big-cap rung can dwarf the whole budget.
        auto wrapped = [&](float f) {
            if (deadline && bench_clock::now() > *deadline) throw CapBudgetExceeded{};
            if (main_cb) main_cb((static_cast<float>(rung) + f) /
                                 static_cast<float>(ladder_n));
        };
        int build_cap = std::min(sp_cap, std::max(sp_phrases, 1));
        HydraRecord candidate;
        try {
            candidate = analyze_at_cap(song, sp_cap, depth_mode, depth_value,
                                       ms_filter, build_cap, legacy_fills,
                                       /*want_allzero=*/false, wrapped);
        } catch (const CapBudgetExceeded&) {
            // Out of time partway up. Keep the best rung that finished; the
            // abandoned one is discarded and the result reads as unsettled.
            break;
        }

        std::optional<int64_t> score;
        if (!candidate.paths.empty())
            score = candidate.best_path().totalscore();

        record = std::move(candidate);
        settled_build_cap = build_cap;

        // Settled: no higher ceiling could bank more than the song offers.
        if (sp_cap >= sp_phrases) {
            converged = true;
            break;
        }
        if (previous_score.has_value() && score == previous_score) {
            converged = true;
            break;
        }
        previous_score = score;
        ++rung;
        if (!deadline && time_budget_s)
            deadline = bench_clock::now() +
                       std::chrono::duration_cast<bench_clock::duration>(
                           std::chrono::duration<double>(*time_budget_s));
    }

    if (record.has_value()) {
        record->sp_cap_converged = converged;
        if (want_allzero) {
            ScoreGraph graph(song, settled_build_cap,
                             legacy_fills ? FillDeadlineRule::Ch10
                                          : FillDeadlineRule::Ch11);
            attach_allzero(graph, *record,
                           split ? scaled_progress(on_progress, kMainProgressShare, 1.0f)
                                 : on_progress);
        }
    }
    return std::move(*record);
}

}  // namespace

HydraRecord analyze_chart(const Song& song, const SearchSettings& settings,
                          const std::function<void(float)>& on_progress) {
    if (song.is_empty())
        throw ChartFileError("No drum notes in this chart.");

    const std::optional<int> sp_cap = settings.sp_cap;
    const DepthMode depth_mode = settings.depth_mode;
    const int depth_value = settings.depth_value;
    const std::optional<double> ms_filter = settings.ms_filter;

    // Clone Hero's 4-bar rule is the classic single pass, kept exactly as it
    // always was so a fresh 4-bar record matches every stored one.
    if (sp_cap == kCloneHeroSpCap)
        return analyze_at_cap(song, kCloneHeroSpCap, depth_mode, depth_value, ms_filter,
                              std::nullopt, settings.legacy_fill_deadline,
                              /*want_allzero=*/true, on_progress);
    // Any other fixed cap runs a single pass at that ceiling. The graph is
    // still only built as tall as the song has phrases to bank -- no run can
    // exceed that -- so a huge cap on a short song stays cheap and exact. A
    // fixed cap is the user's explicit choice, so Auto's time budget doesn't
    // apply to it.
    if (sp_cap.has_value()) {
        int build_cap = std::min(*sp_cap, std::max(count_sp_phrases(song), 1));
        return analyze_at_cap(song, *sp_cap, depth_mode, depth_value, ms_filter,
                              build_cap, settings.legacy_fill_deadline,
                              /*want_allzero=*/true, on_progress);
    }
    return analyze_auto_cap(song, depth_mode, depth_value, ms_filter,
                            settings.legacy_fill_deadline,
                            /*want_allzero=*/true, on_progress,
                            settings.time_budget_s);
}

}  // namespace hydra
