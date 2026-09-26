// tests/corpus_util.h caches corpus parses and analyses for the whole run.
// These checks pin what the corpus loops rely on: one answer per chart and
// settings, the same answer a direct call gives, and a failure that repeats
// instead of turning into a silent empty result.

#include "doctest.h"

#include <string>

#include "core/model.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/pather.h"

using namespace hydra;

TEST_CASE("corpus cache: one parse and one analysis per chart and settings") {
    std::string path;
    for (const std::string& p : corpus::chart_paths())
        if (!corpus::song(p, true, true).is_empty()) { path = p; break; }
    REQUIRE(!path.empty());

    const Song& a = corpus::song(path, true, true);
    CHECK(&corpus::song(path, true, true) == &a);
    CHECK(&corpus::song(path, true, true, Difficulty::Hard) != &a);

    SearchSettings cfg;
    cfg.sp_cap = 4;
    cfg.depth_value = 0;
    const HydraRecord& r = corpus::analyzed(path, cfg);
    CHECK(&corpus::analyzed(path, cfg) == &r);

    // The cached record is the one a direct call produces.
    const HydraRecord direct = analyze_chart(load_songpath(path, true, true), cfg);
    REQUIRE(r.paths.size() == direct.paths.size());
    REQUIRE(!r.paths.empty());
    CHECK(r.best_path().pathstring() == direct.best_path().pathstring());
    CHECK(r.best_path().totalscore() == direct.best_path().totalscore());

    // Plain SearchSettings and the matching AnalysisSettings share one entry.
    app::AnalysisSettings same;
    static_cast<SearchSettings&>(same) = cfg;
    CHECK(&corpus::analyzed(path, same) == &r);

    // Different settings are a different entry.
    cfg.depth_value = 1;
    CHECK(&corpus::analyzed(path, cfg) != &r);
}

TEST_CASE("corpus cache: a failure is thrown again on every call") {
    // A chart with no Hard charting parses to an empty song, and analyzing an
    // empty song throws ChartFileError. The second call must throw too.
    std::string no_hard;
    for (const std::string& p : corpus::chart_paths())
        if (corpus::song(p, true, true, Difficulty::Hard).is_empty()) { no_hard = p; break; }
    REQUIRE(!no_hard.empty());

    app::AnalysisSettings hard;
    hard.difficulty = Difficulty::Hard;
    CHECK_THROWS_AS(corpus::analyzed(no_hard, hard), ChartFileError);
    CHECK_THROWS_AS(corpus::analyzed(no_hard, hard), ChartFileError);
}
