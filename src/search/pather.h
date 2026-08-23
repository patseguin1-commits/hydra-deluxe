// Analysis orchestration: one run at a fixed cap, the Auto SP-cap ladder,
// and the dispatch between them. Discovery and the batch thread pool live
// in app/analysis; this is only what produces a record for one chart.

#ifndef HYDRA_SEARCH_PATHER_H
#define HYDRA_SEARCH_PATHER_H

#include <functional>
#include <optional>
#include <vector>

#include "core/model.h"
#include "parse/song.h"
#include "search/engine.h"
#include "search/graph.h"

namespace hydra {

// Everything the search needs to know about one run. Mirrors the user's
// settings, but holds only what reaches the search (the chart-mode flags
// are parse-time and live on app::AnalysisSettings).
struct SearchSettings {
    DepthMode depth_mode = DepthMode::Scores;
    int depth_value = 4;
    std::optional<double> ms_filter;
    // The SP meter ceiling in bars (4 = Clone Hero's rule). nullopt is Auto:
    // the ladder that raises the ceiling until the score settles.
    std::optional<int> sp_cap = 4;
    // Auto only: seconds before a too-slow ladder rung is abandoned
    // (hymisc.SP_CAP_TIME_BUDGET). nullopt runs every rung to completion.
    std::optional<double> time_budget_s;
};

// The best all-0 path over an already-built graph: the highest-scoring path
// whose activations all record skips == 0, under a fixed 0 ms timing limit,
// with its tied variations as variants. Empty when the chart offers no such
// path. This is a second, constrained search because the main search keeps
// paths by score band and drops the all-0 path when it scores below the band.
// It has no activation branching, so it is far cheaper than the main search.
std::vector<Path> search_allzero(const ScoreGraph& graph,
                                 const std::function<void(float)>& on_progress = {});

// Full analysis for one chart. settings.sp_cap is the SP meter ceiling in
// bars: 4 is Clone Hero's rule and runs exactly the classic single pass; any
// other number runs a single pass at that ceiling; nullopt is Auto, the
// self-settling ladder (settings.time_budget_s applies only there). Throws
// hydra::ChartFileError when the song has no notes, matching hyutil._analyze.
HydraRecord analyze_chart(const Song& song, const SearchSettings& settings,
                          const std::function<void(float)>& on_progress = {});

}  // namespace hydra

#endif  // HYDRA_SEARCH_PATHER_H
