// Analysis orchestration: one run at a fixed cap, the Auto SP-cap ladder,
// and the dispatch between them. Discovery and the batch thread pool live
// in app/analysis; this is only what produces a record for one chart.

#ifndef HYDRA_SEARCH_PATHER_H
#define HYDRA_SEARCH_PATHER_H

#include <functional>
#include <optional>
#include <vector>

#include "core/model.h"
#include "core/rules.h"
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
    std::optional<int> sp_cap = kCloneHeroSpCap;
    // Auto only: seconds before a too-slow ladder rung is abandoned
    // (hymisc.SP_CAP_TIME_BUDGET). nullopt runs every rung to completion.
    std::optional<double> time_budget_s;
    // Score fills by Clone Hero 1.0's spawn deadline instead of 1.1's flat 4
    // beats (FillDeadlineRule in search/graph.h). CLI-only: it is not part of
    // a record's identity, so a legacy run needs its own db (docs/adr/0010).
    bool legacy_fill_deadline = false;
    // The user's rule choices (hydra_rules.ini). Defaults are today's rules.
    core::Rules rules = core::default_rules();
};

// How tall to build the search graph: the SP cap, but never more levels than
// the song has phrases to bank, and never fewer than one.
int graph_build_cap(int sp_cap, int sp_phrase_count);

// The best all-0 path over an already-built graph: the highest-scoring path
// whose activations all record skips == 0, under a fixed 0 ms timing limit,
// with its tied variations as variants. Empty when the chart offers no such
// path. This is a second, constrained search because the main search keeps
// paths by score band and drops the all-0 path when it scores below the band.
// It has no activation branching, so it is far cheaper than the main search.
std::vector<Path> search_allzero(const ScoreGraph& graph,
                                 const std::function<void(float)>& on_progress = {});

// The engine's own pricing of one specific path: activate at exactly
// `act_ticks` (node ticks of the activation fills, ascending) and at no other
// fill. Every squeeze variant of that path comes back, best score first, with
// deact_tick / sp_meter / skips / sqinouts stamped by the engine exactly as a
// normal search would stamp them. Empty when the engine cannot realize the
// set (an activation with under 2 bars, a fill it cannot spawn in time, a tick
// that is not a fill node). settings.sp_cap must be a fixed cap (Auto is
// rejected with std::invalid_argument). settings.depth_* and ms_filter are
// ignored: the search keeps everything and applies no timing filter, because
// the caller asked for this path, not the best one.
std::vector<Path> search_target(const Song& song, const SearchSettings& settings,
                                const std::vector<int64_t>& act_ticks);

// Full analysis for one chart. settings.sp_cap is the SP meter ceiling in
// bars: 4 is Clone Hero's rule and runs exactly the classic single pass; any
// other number runs a single pass at that ceiling; nullopt is Auto, the
// self-settling ladder (settings.time_budget_s applies only there). Throws
// hydra::ChartFileError when the song has no notes, matching hyutil._analyze.
HydraRecord analyze_chart(const Song& song, const SearchSettings& settings,
                          const std::function<void(float)>& on_progress = {});

}  // namespace hydra

#endif  // HYDRA_SEARCH_PATHER_H
