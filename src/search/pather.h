// Analysis orchestration — the C++ port of the pieces of hydra/hyutil.py the
// golden analysis block exercises: one run at a fixed cap, the uncapped SP-cap
// ladder, and the edition dispatch. The heavier discovery/thread-pool half of
// hyutil is Phase 4; this is only what produces a record for one chart.

#ifndef HYDRA_SEARCH_PATHER_H
#define HYDRA_SEARCH_PATHER_H

#include <functional>
#include <optional>
#include <vector>

#include "core/model.h"
#include "parse/song.h"

namespace hydra {

// One pathing run with a given SP meter ceiling. build_cap, when set, is the
// ceiling the graph is actually built at (the record still reports sp_cap).
// Mirrors hyutil._analyze_at_cap.
HydraRecord analyze_at_cap(const Song& song, int sp_cap, int depth_mode,
                           int depth_value, std::optional<double> ms_filter,
                           std::optional<int> build_cap,
                           const std::function<void(float)>& on_progress = {});

// The uncapped edition: raise the ceiling up SP_CAP_LADDER until the score
// settles. Mirrors hyutil._analyze_uncapped with SP_CAP_TIME_BUDGET disabled
// (as the golden generator runs it), so no rung is ever abandoned.
// time_budget_s, if set, abandons a ladder rung that overruns it (the first rung
// always finishes), keeping the best rung so far and flagging it unsettled --
// hyutil's SP_CAP_TIME_BUDGET. nullopt runs every rung to completion (what the
// golden generator does), so it must stay unset in the parity tests.
HydraRecord analyze_uncapped(const Song& song, int depth_mode, int depth_value,
                             std::optional<double> ms_filter,
                             const std::function<void(float)>& on_progress = {},
                             std::optional<double> time_budget_s = std::nullopt);

// Full analysis for one edition. capped -> a single run at 4 bars; uncapped ->
// the auto-settling ladder, unless sp_cap is given, in which case a single run
// at that ceiling (any bar count -- the uncapped edition's manual SP-cap
// option). sp_cap is ignored in the capped edition. Throws hydra::ChartFileError
// when the song has no notes, matching hyutil._analyze.
HydraRecord analyze_chart(const Song& song, bool capped, int depth_mode,
                          int depth_value, std::optional<double> ms_filter,
                          std::optional<int> sp_cap = std::nullopt,
                          const std::function<void(float)>& on_progress = {},
                          std::optional<double> time_budget_s = std::nullopt);

}  // namespace hydra

#endif  // HYDRA_SEARCH_PATHER_H
