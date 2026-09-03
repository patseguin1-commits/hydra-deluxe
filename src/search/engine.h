// Path search over a ScoreGraph: a breadth-first sweep along the two tracks,
// grouping and reducing tied paths each iteration, emitting finished paths
// best-score-first with their variants prepared.

#ifndef HYDRA_SEARCH_ENGINE_H
#define HYDRA_SEARCH_ENGINE_H

#include <functional>
#include <optional>
#include <vector>

#include "core/model.h"
#include "search/graph.h"

namespace hydra {

// How the search decides which losing paths to keep: Scores keeps the top
// depth_value + 1 distinct scores, Points everything within depth_value points.
enum class DepthMode { Scores, Points };

// Run the BFS over the graph and return finished, best-score-first,
// variant-prepared Paths.
// Throws std::runtime_error if the search reaches a broken state.
// on_progress, if set, receives a monotonic 0..1 fraction as the BFS frontier
// sweeps the chart. Lets the UI show a real progress bar for a heavy chart
// instead of an indeterminate spinner.
// no_skips constrains the search to paths whose activations all record
// skips == 0 -- the "all-0" path a player hits by activating at every first
// opportunity. It removes all activation branching, so such a search is far
// cheaper than an unconstrained one.
// hard_ms_filter turns ms_filter from a preference into a requirement. By
// default an over-limit path still survives while nothing outscores it, so the
// best path a search reports can need more timing than the limit allows; with
// this set, an over-limit path is dropped outright.
// target_act_ticks, when non-null, pins the activation set: a sorted list of
// node ticks where the search MUST activate, and nowhere else. Every other
// activation opportunity is declined. It replaces no_skips's rule for the same
// branch point, so the search returns exactly one path -- the caller's -- with
// all its squeeze variants, priced the engine's own way. An unrealizable set
// (an activation with SP under 2 bars, a fill the engine cannot spawn in time,
// a tick that is not a fill node) empties the frontier, which surfaces as the
// usual std::runtime_error.
std::vector<Path> run_search(const ScoreGraph& graph, DepthMode depth_mode,
                             int depth_value, std::optional<double> ms_filter,
                             bool no_skips = false,
                             bool hard_ms_filter = false,
                             const std::function<void(float)>& on_progress = {},
                             const std::vector<int64_t>* target_act_ticks = nullptr);

}  // namespace hydra

#endif  // HYDRA_SEARCH_ENGINE_H
