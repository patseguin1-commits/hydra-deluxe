// Analysis orchestration: one run at a fixed cap, the Auto SP-cap ladder,
// and the dispatch between them. Discovery and the batch thread pool live
// in app/analysis; this is only what produces a record for one chart.

#ifndef HYDRA_SEARCH_PATHER_H
#define HYDRA_SEARCH_PATHER_H

#include <functional>
#include <optional>
#include <type_traits>
#include <vector>

#include "core/model.h"
#include "parse/song.h"
#include "search/graph.h"

namespace hydra {

// The best all-0 path over an already-built graph: the highest-scoring path
// whose activations all record skips == 0, under a fixed 0 ms timing limit,
// with its tied variations as variants. Empty when the chart offers no such
// path. This is a second, constrained search because the main search keeps
// paths by score band and drops the all-0 path when it scores below the band.
// It has no activation branching, so it is far cheaper than the main search.
std::vector<Path> search_allzero(const ScoreGraph& graph,
                                 const std::function<void(float)>& on_progress = {});

// One pathing run with a given SP meter ceiling. build_cap, when set, is the
// ceiling the graph is actually built at (the record still reports sp_cap).
// want_allzero also runs search_allzero over the same graph and stores it in
// the record's allzero_paths.
// Mirrors hyutil._analyze_at_cap.
HydraRecord analyze_at_cap(const Song& song, int sp_cap, int depth_mode,
                           int depth_value, std::optional<double> ms_filter,
                           std::optional<int> build_cap, bool want_allzero = false,
                           const std::function<void(float)>& on_progress = {});

// Auto cap: raise the ceiling up the SP-cap ladder until the score settles,
// approximating "no ceiling at all".
// time_budget_s, if set, abandons a ladder rung that overruns it (the first
// rung always finishes), keeping the best rung so far and flagging it
// unsettled. nullopt runs every rung to completion — what the tests use, so
// their results stay deterministic.
// want_allzero runs the all-0 pass once, after the ladder settles, at the
// settled ceiling -- never per rung.
HydraRecord analyze_auto_cap(const Song& song, int depth_mode, int depth_value,
                             std::optional<double> ms_filter,
                             bool want_allzero = false,
                             const std::function<void(float)>& on_progress = {},
                             std::optional<double> time_budget_s = std::nullopt);

// Full analysis for one chart. sp_cap is the SP meter ceiling in bars: 4 is
// Clone Hero's rule and runs exactly the classic single pass; any other
// number runs a single pass at that ceiling; nullopt is Auto, the
// self-settling ladder (time_budget_s applies only there). Throws
// hydra::ChartFileError when the song has no notes, matching hyutil._analyze.
HydraRecord analyze_chart(const Song& song, std::optional<int> sp_cap, int depth_mode,
                          int depth_value, std::optional<double> ms_filter,
                          const std::function<void(float)>& on_progress = {},
                          std::optional<double> time_budget_s = std::nullopt);
// The pre-1.6 signature took a `capped` bool here. A bool would now silently
// become a 1-bar cap, so refuse exactly that at compile time (a template so
// an int literal still picks the real overload above).
template <class Bool, std::enable_if_t<std::is_same_v<Bool, bool>, int> = 0>
HydraRecord analyze_chart(const Song&, Bool, int, int, std::optional<double>,
                          const std::function<void(float)>& = {},
                          std::optional<double> = std::nullopt) = delete;

}  // namespace hydra

#endif  // HYDRA_SEARCH_PATHER_H
