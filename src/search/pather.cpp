#include "search/pather.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "search/engine.h"
#include "search/graph.h"

namespace hydra {

namespace {

using bench_clock = std::chrono::steady_clock;

// Thrown out of the progress callback to abandon a ladder rung that has blown
// the time budget (auto_budget_s in hydra_rules.ini).
struct CapBudgetExceeded {};

HydraRecord read(const ScoreGraph& graph, DepthMode depth_mode, int depth_value,
                 std::optional<double> ms_filter,
                 const std::function<void(float)>& on_progress) {
    HydraRecord record;
    record.ms_limit = ms_filter;
    EngineOptions options;
    options.depth_mode = depth_mode;
    options.depth_value = depth_value;
    options.ms_filter = ms_filter;
    record.paths = run_search(graph, options, on_progress);
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
    // variants (up to Rules::max_tied_paths), which is where the E / + / - variations
    // of one all-0 path come from. The 0 ms limit is the point of the feature,
    // so it is fixed here and ignores the user's "Path limit" setting --
    // and it is applied hard. The default soft filter only prefers paths inside
    // the limit and still reports an over-limit one while nothing outscores it,
    // which in a no-skips search (a tiny candidate set, usually one path per
    // group) meant the section routinely showed a path needing hundreds of ms.
    std::vector<Path> paths;
    try {
        EngineOptions options;  // score depth 0: only the top score, plus its ties
        options.ms_filter = 0.0;
        options.no_skips = true;
        options.hard_ms_filter = true;
        paths = run_search(graph, options, on_progress);
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

std::vector<Path> search_target(const Song& song, const SearchSettings& settings,
                                const std::vector<int64_t>& act_ticks) {
    if (!settings.sp_cap)
        throw std::invalid_argument(
            "search_target needs a fixed SP cap; Auto has no single graph to "
            "price the path against");

    std::vector<int64_t> ticks = act_ticks;
    std::sort(ticks.begin(), ticks.end());
    ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());

    ScoreGraph graph(song, std::optional<int>(*settings.sp_cap),
                     settings.legacy_fill_deadline ? FillDeadlineRule::Ch10
                                                   : FillDeadlineRule::Ch11,
                     settings.rules);

    // The caller named the path, so nothing may prune it: the widest possible
    // points band keeps every survivor, and no timing filter is applied. The
    // band is compared as `score + depth_value < best` in int64 arithmetic, so
    // a billion cannot overflow.
    std::vector<Path> paths;
    try {
        EngineOptions options;
        options.depth_mode = DepthMode::Points;
        options.depth_value = 1'000'000'000;
        options.target_act_ticks = ticks;
        paths = run_search(graph, options);
    } catch (const std::runtime_error&) {
        // The frontier emptied: this activation set is not realizable on this
        // chart. That is the normal failure for a targeted search, not a bug.
        return {};
    }

    // A tick the search never met as an activation opportunity -- not a fill
    // node at all, or one the path was already under Star Power for -- does not
    // empty the frontier: the path just quietly comes back with fewer
    // activations than asked for. That is still an unrealizable set, so it
    // reports as one.
    for (const Path& p : paths) {
        const std::vector<Activation> acts = p.all_activations();
        if (acts.size() != ticks.size()) return {};
        for (size_t i = 0; i < acts.size(); ++i) {
            if (!acts[i].timecode || acts[i].timecode->ticks() != ticks[i])
                return {};
        }
    }
    return paths;
}

int graph_build_cap(int sp_cap, int sp_phrase_count) {
    return std::min(sp_cap, std::max(sp_phrase_count, 1));
}

namespace {

// One pathing run with a given SP meter ceiling. build_cap, when set, is the
// ceiling the graph is actually built at (the record still reports sp_cap).
// want_allzero also runs search_allzero over the same graph and stores it in
// the record's allzero_paths.
HydraRecord analyze_at_cap(const Song& song, int sp_cap, DepthMode depth_mode,
                           int depth_value, std::optional<double> ms_filter,
                           std::optional<int> build_cap, bool legacy_fills,
                           const core::Rules& rules,
                           bool want_allzero = false,
                           const std::function<void(float)>& on_progress = {}) {
    std::optional<int> cap = build_cap.has_value() ? build_cap
                                                   : std::optional<int>(sp_cap);
    ScoreGraph graph(song, cap,
                     legacy_fills ? FillDeadlineRule::Ch10
                                  : FillDeadlineRule::Ch11,
                     rules);
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
                             const core::Rules& rules,
                             bool want_allzero = false,
                             const std::function<void(float)>& on_progress = {},
                             std::optional<double> time_budget_s = std::nullopt) {
    int sp_phrases = song.sp_phrase_count();
    const int ladder_n = static_cast<int>(rules.auto_cap_ladder.size());

    std::optional<HydraRecord> record;
    std::optional<int64_t> previous_score;
    // The clock only starts once the first rung has finished, so there is always
    // a result to report.
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
    for (int sp_cap : rules.auto_cap_ladder) {
        // Each rung's 0..1 sweep occupies its slice of the overall bar, so the
        // ladder reads as one monotonic progress even though it re-runs the
        // search per ceiling. Early convergence just finishes below 100%. The
        // same per-iteration callback is where a rung overrunning the budget is
        // interrupted, rather than only checking
        // between rungs -- one big-cap rung can dwarf the whole budget.
        auto wrapped = [&](float f) {
            if (deadline && bench_clock::now() > *deadline) throw CapBudgetExceeded{};
            if (main_cb) main_cb((static_cast<float>(rung) + f) /
                                 static_cast<float>(ladder_n));
        };
        int build_cap = graph_build_cap(sp_cap, sp_phrases);
        HydraRecord candidate;
        try {
            candidate = analyze_at_cap(song, sp_cap, depth_mode, depth_value,
                                       ms_filter, build_cap, legacy_fills, rules,
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
                                          : FillDeadlineRule::Ch11,
                             rules);
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
    if (sp_cap == kCloneHeroSpCap) {
        HydraRecord record =
            analyze_at_cap(song, kCloneHeroSpCap, depth_mode, depth_value, ms_filter,
                           std::nullopt, settings.legacy_fill_deadline, settings.rules,
                           /*want_allzero=*/true, on_progress);
        record.rules_fingerprint = settings.rules.fingerprint();
        return record;
    }
    // Any other fixed cap runs a single pass at that ceiling. The graph is
    // still only built as tall as the song has phrases to bank -- no run can
    // exceed that -- so a huge cap on a short song stays cheap and exact. A
    // fixed cap is the user's explicit choice, so Auto's time budget doesn't
    // apply to it.
    if (sp_cap.has_value()) {
        int build_cap = graph_build_cap(*sp_cap, song.sp_phrase_count());
        HydraRecord record =
            analyze_at_cap(song, *sp_cap, depth_mode, depth_value, ms_filter,
                           build_cap, settings.legacy_fill_deadline, settings.rules,
                           /*want_allzero=*/true, on_progress);
        record.rules_fingerprint = settings.rules.fingerprint();
        return record;
    }
    HydraRecord record = analyze_auto_cap(song, depth_mode, depth_value, ms_filter,
                                          settings.legacy_fill_deadline, settings.rules,
                                          /*want_allzero=*/true, on_progress,
                                          settings.time_budget_s);
    record.rules_fingerprint = settings.rules.fingerprint();
    return record;
}

}  // namespace hydra
