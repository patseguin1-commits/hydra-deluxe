// Path search over a ScoreGraph: a breadth-first sweep along the two tracks,
// grouping and reducing tied paths each iteration, emitting finished paths
// best-score-first with their variants prepared.

#ifndef HYDRA_SEARCH_ENGINE_H
#define HYDRA_SEARCH_ENGINE_H

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "core/model.h"
#include "search/graph.h"

namespace hydra {

// How the search decides which losing paths to keep: Scores keeps the top
// depth_value + 1 distinct scores, Points everything within depth_value points.
enum class DepthMode { Scores, Points };

// Everything one run_search call can be asked to do, in one value, so no two
// flags can be swapped at a call site. The defaults are a plain best-path
// search: score depth 0, no timing limit, no constraints.
struct EngineOptions {
    // Which losing paths to keep (see DepthMode) and how many.
    DepthMode depth_mode = DepthMode::Scores;
    int depth_value = 0;
    // The ms limit; nullopt is off.
    std::optional<double> ms_filter;
    // Only paths whose activations all record skips == 0: the "all-0" path a
    // player hits by activating at every first opportunity. It removes all
    // activation branching, so such a search is far cheaper.
    bool no_skips = false;
    // Make ms_filter a requirement. By default an over-limit path still
    // survives while nothing outscores it, so the best path can need more
    // timing than the limit allows; with this set it is dropped outright.
    bool hard_ms_filter = false;
    // When set, the node ticks the search must activate at, ascending, and
    // nowhere else. It replaces no_skips's rule for the same branch point, so
    // the search returns exactly one path (the caller's) with all its squeeze
    // variants, priced the engine's own way. An unrealizable set (an
    // activation with SP under 2 bars, a fill the engine cannot spawn in time,
    // a tick that is not a fill node) empties the frontier, which surfaces as
    // the usual std::runtime_error.
    std::optional<std::vector<int64_t>> target_act_ticks;
};

// Run the BFS over the graph and return finished, best-score-first,
// variant-prepared Paths.
// Throws std::runtime_error if the search reaches a broken state.
// on_progress, if set, receives a monotonic 0..1 fraction as the BFS frontier
// sweeps the chart, so the UI can show a real progress bar.
std::vector<Path> run_search(const ScoreGraph& graph, const EngineOptions& options,
                             const std::function<void(float)>& on_progress = {});

}  // namespace hydra

#endif  // HYDRA_SEARCH_ENGINE_H
