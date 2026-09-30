#include "search/pather.h"

#include <algorithm>
#include <stdexcept>

#include "search/engine.h"
#include "search/graph.h"

namespace hydra {

namespace {

HydraRecord read(const ScoreGraph& graph, DepthMode depth_mode, int depth_value,
                 std::optional<double> ms_filter,
                 const std::function<void(float)>& on_progress) {
    HydraRecord record;
    record.ms_limit = ms_filter;
    record.multsqueezes = graph.multsqueezes();
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
        // analysis. Cancel unwinds through AnalysisCancelled, which does not
        // derive from std::exception, so it still propagates.
        record.allzero_paths.clear();
    }
}

}  // namespace

std::vector<Path> search_allzero(const ScoreGraph& graph,
                                 const std::function<void(float)>& on_progress) {
    // depth_value 0 keeps only the top score; its tied peers still merge into
    // variants (up to Rules::max_tied_paths), which is where the E / + / - variations
    // of one all-0 path come from. The 0 ms limit keeps the path free of
    // squeeze timing, so it is fixed here and ignores the user's "Path limit"
    // setting --
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
        // state, so both end here as "no all-0 path". Cancel unwinds through
        // its own non-std::exception type and still propagates.
        return {};
    }

    // A chart can also refuse every activation opportunity (the early
    // fill can never be summoned in time). The search then returns a single
    // path with no activations, whose pathstring is empty.
    if (paths.size() == 1 && !paths[0].has_activations()) paths.clear();
    return paths;
}

std::vector<Path> search_target(const Song& song, const SearchSettings& settings,
                                const std::vector<int64_t>& act_ticks) {
    std::vector<int64_t> ticks = act_ticks;
    std::sort(ticks.begin(), ticks.end());
    ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());

    ScoreGraph graph(song, std::optional<int>(settings.sp_cap),
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
        const ActivationWalk acts = p.walk_activations();
        if (acts.size() != ticks.size()) return {};
        for (size_t i = 0; i < acts.size(); ++i) {
            if (acts[i].timecode.ticks() != ticks[i])
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

}  // namespace

HydraRecord analyze_chart(const Song& song, const SearchSettings& settings,
                          const std::function<void(float)>& on_progress) {
    if (song.is_empty())
        throw ChartFileError("No drum notes in this chart.");

    // One pass at the chosen ceiling, Clone Hero's 4 bars included. The graph
    // is only built as tall as the song has phrases to bank -- no run can
    // exceed that -- so a huge cap on a short song stays cheap, and a 4-bar
    // graph built lower stores the same bytes (test "a 4-bar graph built at
    // the song's phrase count stores the same paths").
    const int build_cap = graph_build_cap(settings.sp_cap, song.sp_phrase_count());
    HydraRecord record =
        analyze_at_cap(song, settings.sp_cap, settings.depth_mode, settings.depth_value,
                       settings.ms_filter, build_cap, settings.legacy_fill_deadline,
                       settings.rules, /*want_allzero=*/true, on_progress);
    record.rules_fingerprint = settings.rules.fingerprint();
    record.legacy_fills = settings.legacy_fill_deadline;
    return record;
}

}  // namespace hydra
