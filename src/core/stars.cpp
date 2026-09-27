#include "core/stars.h"

#include <cmath>

namespace hydra {

int64_t star_cutoff(int64_t base, int stars) {
    // Stored as a float first: the game rounds the product to 32 bits before
    // rounding up, and that can move a large cutoff by one point.
    const float product = static_cast<float>(base) * kStarMultipliers.at(stars - 1);
    return static_cast<int64_t>(std::ceil(static_cast<double>(product)));
}

StarCutoffs star_cutoffs(const Path& path) {
    StarCutoffs out;
    out.base = path.chart_base_score();
    out.solo_bonus = path.score_solo;
    for (int stars = 1; stars <= kMaxStars; ++stars)
        out.cutoffs[stars - 1] = star_cutoff(out.base, stars);
    return out;
}

int stars_for_score(const StarCutoffs& cutoffs, int64_t score_without_solo) {
    // The game's loop: add a star while the score reaches the next cutoff,
    // and stop at 7.
    int stars = 0;
    while (stars < kMaxStars && score_without_solo >= cutoffs.cutoffs[stars]) ++stars;
    return stars;
}

int path_stars(const Path& path) {
    return stars_for_score(star_cutoffs(path), path.totalscore() - path.score_solo);
}

}  // namespace hydra
