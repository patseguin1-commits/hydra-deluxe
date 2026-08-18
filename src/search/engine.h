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

// Run the BFS over the graph and return finished, best-score-first,
// variant-prepared Paths. depth_mode: 0 = scores, 1 = points.
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
std::vector<Path> run_search(const ScoreGraph& graph, int depth_mode,
                             int depth_value, std::optional<double> ms_filter,
                             bool no_skips = false,
                             bool hard_ms_filter = false,
                             const std::function<void(float)>& on_progress = {});

}  // namespace hydra

#endif  // HYDRA_SEARCH_ENGINE_H
