// Smoke test for parse/song: every chart in the corpus parses without
// throwing, and the resulting timestamp sequence holds the structural
// invariants the search depends on (monotone ticks, decodable chords,
// consistent activation-fill placement). Covers .mid, .chart, and .sng.

#include "doctest.h"

#include <string>

#include "core/model.h"
#include "corpus_util.h"
#include "parse/song.h"

using namespace hydra;

TEST_CASE("song parse holds its invariants over the corpus") {
    int charts = 0, nonempty = 0;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        ++charts;
        if (song.is_empty()) continue;
        ++nonempty;

        bool ticks_ok = true, codes_ok = true, fills_ok = true;
        int64_t prev = -1;
        for (const SongTimestamp& ts : song.sequence) {
            // Strictly increasing: two chords never share a tick.
            if (ts.timecode.ticks() <= prev) ticks_ok = false;
            prev = ts.timecode.ticks();

            // Every chord code decodes back to the same chord.
            const std::string code = ts.chord.code();
            if (code.empty() || Chord::from_code(code).code() != code)
                codes_ok = false;

            // An activation fill length is always positive.
            if (ts.activation_length.has_value() && *ts.activation_length <= 0)
                fills_ok = false;
        }
        CHECK_MESSAGE(ticks_ok, path << ": ticks not strictly increasing");
        CHECK_MESSAGE(codes_ok, path << ": chord code round-trip");
        CHECK_MESSAGE(fills_ok, path << ": non-positive activation fill");
    }

    CHECK(nonempty > 0);
    MESSAGE("checked " << charts << " charts (" << nonempty << " non-empty)");
}
