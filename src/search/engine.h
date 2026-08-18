// Native-object path search — the C++ port of native/hydra_search.cpp adapted to
// read a ScoreGraph's node/edge objects directly instead of the flattened
// hy_search_in arrays. This is Phase 3 step (b): the flat marshalling boundary
// dissolved. The engine algorithm (BFS + activation DP) is a faithful copy of
// the proven native engine; only the data-access layer changed from indexing
// POD arrays to reading the graph objects (through an index enumeration the
// index-based search still needs for addressing).
//
// native/hydra_search.cpp is left untouched — it remains the compiled core of
// the frozen Python DLL until the Phase 6 cutover.

#ifndef HYDRA_SEARCH_ENGINE_H
#define HYDRA_SEARCH_ENGINE_H

#include <functional>
#include <optional>
#include <vector>

#include "core/model.h"
#include "search/graph.h"

namespace hydra {

// Run the BFS (use_dp = false) or the activation DP (use_dp = true) over the
// graph and return finished, best-score-first, variant-prepared Paths in the
// state GraphPather.read would leave them. depth_mode: 0 = scores, 1 = points.
// Throws std::runtime_error if the search reaches a state the Python would have
// raised from.
// on_progress, if set, receives a monotonic 0..1 fraction as the BFS frontier
// sweeps the chart (BFS only; ignored by the DP). Lets the UI show a real
// progress bar for a heavy chart instead of an indeterminate spinner.
// no_skips constrains the search to paths whose activations all record
// skips == 0 -- the "all-0" path a player hits by activating at every first
// opportunity. It removes all activation branching, so such a search is far
// cheaper than an unconstrained one.
// hard_ms_filter turns ms_filter from a preference into a requirement. By
// default an over-limit path still survives while nothing outscores it, so the
// best path a search reports can need more timing than the limit allows; with
// this set, an over-limit path is dropped outright.
// Both are BFS only: run_search throws when either is set with use_dp.
std::vector<Path> run_search(const ScoreGraph& graph, int depth_mode,
                             int depth_value, std::optional<double> ms_filter,
                             bool use_dp, bool no_skips = false,
                             bool hard_ms_filter = false,
                             const std::function<void(float)>& on_progress = {});

}  // namespace hydra

#endif  // HYDRA_SEARCH_ENGINE_H
