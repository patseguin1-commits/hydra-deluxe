// Unit tests for core/stars: Clone Hero's star cutoffs, as the game computes
// them (float multiply, then round up), and where their inputs come from.

#include "doctest.h"

#include <array>
#include <cstdint>
#include <string>

#include "app/analysis.h"
#include "core/model.h"
#include "core/stars.h"
#include "corpus_util.h"

using namespace hydra;

namespace {

std::array<int64_t, kMaxStars> cutoffs_for(int64_t base) {
    std::array<int64_t, kMaxStars> out{};
    for (int stars = 1; stars <= kMaxStars; ++stars) out[stars - 1] = star_cutoff(base, stars);
    return out;
}

}  // namespace

TEST_CASE("stars: the table is the game's first seven multipliers") {
    CHECK(kMaxStars == 7);
    const std::array<float, 7> expected{0.1f, 0.5f, 1.0f, 2.0f, 2.8f, 3.6f, 4.4f};
    CHECK(kStarMultipliers == expected);
}

TEST_CASE("stars: whole-number products come out exact") {
    const std::array<int64_t, 7> expected{10000, 50000, 100000, 200000, 280000, 360000, 440000};
    CHECK(cutoffs_for(100000) == expected);
}

TEST_CASE("stars: fractional products round up") {
    // 12345.6 -> 12346, 345676.8 -> 345677, 444441.6 -> 444442, 543206.4 -> 543207.
    const std::array<int64_t, 7> expected{12346, 61728, 123456, 246912, 345677, 444442, 543207};
    CHECK(cutoffs_for(123456) == expected);
}

TEST_CASE("stars: the multiply is 32-bit float, as in the game") {
    // Exact: 786437 * 3.6 = 2831173.2, which rounds up to 2831174. The game
    // multiplies in float, where the product lands on 2831173.0, so its
    // cutoff is 2831173. The other six agree with exact math.
    const std::array<int64_t, 7> expected{78644, 393219, 786437, 1572874, 2202024, 2831173, 3460323};
    CHECK(cutoffs_for(786437) == expected);
}

TEST_CASE("stars: a zero base gives zero cutoffs") {
    const std::array<int64_t, 7> expected{};
    CHECK(cutoffs_for(0) == expected);
}

TEST_CASE("stars: star_cutoffs reads the path's base score and solo bonus") {
    Path path;
    path.score_base = 1000;
    path.score_ghosts = 100;
    path.score_accents = 200;
    path.score_solo = 800;
    path.score_combo = 5000;  // the multiplier's share: never part of the base
    path.score_sp = 3000;

    CHECK(path.chart_base_score() == 1300);

    const StarCutoffs sc = star_cutoffs(path);
    CHECK(sc.base == 1300);
    CHECK(sc.solo_bonus == 800);
    const std::array<int64_t, 7> expected{130, 650, 1300, 2600, 3640, 4680, 5720};
    CHECK(sc.cutoffs == expected);
}

TEST_CASE("stars: avg_mult divides by the same base score") {
    Path path;
    path.score_base = 1000;
    path.score_ghosts = 100;
    path.score_accents = 200;
    path.score_combo = 1300;
    path.score_solo = 800;
    // total 3400, minus the 800 solo bonus = 2600, over a base of 1300.
    CHECK(path.avg_mult() == doctest::Approx(2.0));
}

TEST_CASE("stars: the base score is the sum of every note's basescore, on every path") {
    // "87" by Polyphia: cymbals and a drum solo. The engine adds the base
    // score up by category (base + ghosts + accents); ChordNote::basescore
    // prices one note. This ties the two together on a real chart, so they
    // can't drift apart, and checks the Stars tab's premise that every path
    // of a record has the same base score and solo bonus.
    const std::string chart =
        corpus::root() + "/common/Summer Blast _25 Setlist/Tier 6/Polyphia - 87/notes.chart";
    app::AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 10;
    settings.ms_filter = 10.0;

    const Song& song = corpus::song(chart, settings.prodrums, settings.bass2x,
                                    settings.difficulty, settings.rules);
    const HydraRecord& record = corpus::analyzed(chart, settings);
    REQUIRE(!song.is_empty());
    REQUIRE(!record.paths.empty());

    int64_t note_sum = 0;
    int special_notes = 0;
    for (const SongTimestamp& ts : song.sequence) {
        for (const ChordNote& note : ts.chord.notes()) {
            note_sum += note.basescore();
            if (note.is_cymbal() || note.is_dynamic()) ++special_notes;
        }
    }
    // The chart must exercise more than plain 50-point gems, or the sum
    // proves little.
    REQUIRE(special_notes > 0);

    const Path& best = record.best_path();
    MESSAGE("87: base score " << best.chart_base_score() << ", note sum " << note_sum
                              << ", solo bonus " << best.score_solo);
    CHECK(best.chart_base_score() == note_sum);
    CHECK(best.score_solo > 0);

    int checked = 0;
    for (const Path* p : record.all_paths()) {
        CHECK(p->chart_base_score() == best.chart_base_score());
        CHECK(p->score_solo == best.score_solo);
        ++checked;
    }
    for (const Path* p : record.all_allzero_paths()) {
        CHECK(p->chart_base_score() == best.chart_base_score());
        CHECK(p->score_solo == best.score_solo);
        ++checked;
    }
    MESSAGE("87: paths checked (variants and all-0 paths included): " << checked);
    CHECK(checked > 1);
}

TEST_CASE("stars: path_stars counts cutoffs reached without the solo bonus") {
    // Base 1300, so the cutoffs are 130, 650, 1300, 2600, 3640, 4680, 5720
    // (the star_cutoffs case above).
    Path path;
    path.score_base = 1000;
    path.score_ghosts = 100;
    path.score_accents = 200;
    const StarCutoffs sc = star_cutoffs(path);

    CHECK(stars_for_score(sc, 0) == 0);
    CHECK(stars_for_score(sc, 129) == 0);
    CHECK(stars_for_score(sc, 130) == 1);    // a cutoff counts when reached
    CHECK(stars_for_score(sc, 4679) == 5);
    CHECK(stars_for_score(sc, 4680) == 6);
    CHECK(stars_for_score(sc, 5720) == 7);
    CHECK(stars_for_score(sc, 1000000) == 7);  // the game stops at 7

    // 1300 base + 3380 combo = 4680 without the solo bonus: 6 stars. A solo
    // bonus big enough to pass the 7-star cutoff doesn't count.
    path.score_combo = 3380;
    path.score_solo = 2000;
    CHECK(path.totalscore() == 6680);
    CHECK(path_stars(path) == 6);
    path.score_combo = 3379;
    CHECK(path_stars(path) == 5);
}

TEST_CASE("stars: Burnout's optimal path earns 7 stars") {
    // Green Day - Burnout at Expert, Pro Drums, 2x Bass, cap 4: optimal
    // 378,315 against a 7-star cutoff of 335,500 (the plan's reference data).
    const std::string chart = corpus::root() +
        "/common/Summer Blast _25 Setlist/Tier 4/Green Day - Burnout/notes.mid";
    app::AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 2;
    settings.ms_filter = 10.0;
    const HydraRecord& record = corpus::analyzed(chart, settings);
    REQUIRE(!record.paths.empty());
    const Path& best = record.best_path();
    CHECK(best.totalscore() == 378315);
    CHECK(star_cutoffs(best).cutoffs[kMaxStars - 1] == 335500);
    CHECK(path_stars(best) == 7);
}
