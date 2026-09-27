// Clone Hero's star cutoffs, copied from the game (v1.1.0.6142, decoded from
// GameAssembly.dll on 2026-09-27).
//
// A star's cutoff is the chart's base score times a fixed multiplier,
// rounded up, with the multiply done in 32-bit float as the game does. You
// get a star when your score *without the solo bonus* is at or above its
// cutoff. The game stops counting at 7 stars.

#ifndef HYDRA_CORE_STARS_H
#define HYDRA_CORE_STARS_H

#include <array>
#include <cstdint>

#include "core/model.h"

namespace hydra {

inline constexpr int kMaxStars = 7;

// 1 through 7 stars. The game's table has two more entries (5.2 and 6.0),
// but its star counter stops at 7, so they are never reached.
inline constexpr std::array<float, kMaxStars> kStarMultipliers{
    0.1f, 0.5f, 1.0f, 2.0f, 2.8f, 3.6f, 4.4f};

// The score needed for `stars` (1..kMaxStars) on a chart with this base score.
int64_t star_cutoff(int64_t base, int stars);

// Everything the Stars tab shows, for one path of a record.
struct StarCutoffs {
    int64_t base = 0;        // Path::chart_base_score()
    int64_t solo_bonus = 0;  // Path::score_solo; not counted toward stars
    std::array<int64_t, kMaxStars> cutoffs{};  // [0] is 1 star
};

StarCutoffs star_cutoffs(const Path& path);

}  // namespace hydra

#endif  // HYDRA_CORE_STARS_H
