// Invariant tests for search/ (ScoreGraph + engine + pather).
//
// The engine's best score must be stable across the config knobs that are not
// supposed to change it (search depth, the ms filter), every reported path
// must be internally consistent, and the all-0 pass must honor its contract.

#include "doctest.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/model.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/engine.h"
#include "search/graph.h"
#include "search/pather.h"

using namespace hydra;

TEST_CASE("search invariants hold across the corpus and config knobs") {
    int charts = 0, mismatches = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, "Expert", true, true);
        if (song.is_empty()) continue;
        ++charts;

        std::string d;
        try {
            // Depth asks for more alternate paths and must not move the
            // optimum; the ms filter is a constraint, so its best can only
            // be at or below the unconstrained best.
            HydraRecord shallow = analyze_chart(song, true, 0, 0, std::nullopt);
            HydraRecord deep = analyze_chart(song, true, 0, 200, std::nullopt);
            HydraRecord filtered = analyze_chart(song, true, 0, 200, 20.0);

            if (shallow.paths.empty()) {
                d = "no paths";
            } else {
                const int64_t best = shallow.paths.front().totalscore();
                if (deep.paths.front().totalscore() != best)
                    d = "depth changed the best score";
                else if (!filtered.paths.empty() &&
                         filtered.paths.front().totalscore() > best)
                    d = "ms filter scored above the unconstrained best";
                else if (deep.paths.size() < shallow.paths.size())
                    d = "deeper search returned fewer paths";

                // Paths come out best-first, and every path in a record
                // reports the same chart notecount.
                int64_t prev = INT64_MAX;
                const int notecount = deep.paths.front().notecount;
                for (const Path& p : deep.paths) {
                    if (p.totalscore() > prev) {
                        d = "paths not sorted by score";
                        break;
                    }
                    prev = p.totalscore();
                    if (p.notecount != notecount) {
                        d = "notecount varies between paths";
                        break;
                    }
                    if (p.tied_pathcount() < 1) {
                        d = "tied_pathcount below 1";
                        break;
                    }
                }
            }
        } catch (const ChartFileError&) {
            continue;  // charts the engine rejects are covered elsewhere
        }

        if (!d.empty() && ++mismatches <= 8)
            CHECK_MESSAGE(false, path << " " << d);
    }

    CHECK(mismatches == 0);
    REQUIRE(charts > 0);
    MESSAGE("checked " << charts << " charts");
}

// The all-0 pass is a second, constrained search. Its whole contract is that
// every activation it reports records skips == 0, that the 0 ms limit is a
// requirement rather than a preference, and that it never scores above the
// unconstrained optimum.
TEST_CASE("search_allzero returns only all-0 paths inside the 0 ms limit") {
    int checks = 0, mismatches = 0, found = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, "Expert", true, true);
        if (song.is_empty()) continue;

        ScoreGraph graph(song, 4);
        std::vector<Path> allzero = search_allzero(graph);
        ++checks;
        if (allzero.empty()) continue;
        ++found;

        HydraRecord holder;
        holder.allzero_paths = allzero;
        const int64_t optimum =
            run_search(graph, 0, 0, std::nullopt, false).front().totalscore();

        std::string d;
        for (const Path* p : holder.all_allzero_paths()) {
            if (!p->is_allzero()) {
                d = "not all-0: " + p->pathstring();
                break;
            }
            if (p->difficulty().value_or(0.0) > 0.0) {
                d = "over the 0 ms limit: " + p->pathstring() + " needs " +
                    std::to_string(*p->difficulty()) + " ms";
                break;
            }
            if (p->totalscore() > optimum) {
                d = "scores above the optimum: " + p->pathstring();
                break;
            }
        }
        if (!d.empty() && ++mismatches <= 8)
            CHECK_MESSAGE(false, path << " " << d);
    }

    CHECK(mismatches == 0);
    CHECK(found > 0);
    MESSAGE("checked " << checks << " charts, " << found << " with an all-0 path");
}
