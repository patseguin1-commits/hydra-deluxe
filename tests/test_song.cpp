// Smoke test for parse/song: every chart in the corpus parses without
// throwing, and the resulting timestamp sequence holds the structural
// invariants the search depends on (monotone ticks, decodable chords,
// consistent activation-fill placement). Covers .mid, .chart, and .sng.

#include "doctest.h"

#include <string>

#include "core/model.h"
#include "corpus_util.h"
#include "multidiff_chart.h"
#include "parse/song.h"

using namespace hydra;

namespace {

// The structural invariants the search depends on, checked on one parsed song.
// Returns false (with a message naming `what`) on the first violation.
void check_invariants(const Song& song, const std::string& what) {
    bool ticks_ok = true, codes_ok = true, fills_ok = true;
    int64_t prev = -1;
    for (const SongTimestamp& ts : song.sequence) {
        // Strictly increasing: two chords never share a tick.
        if (ts.timecode.ticks() <= prev) ticks_ok = false;
        prev = ts.timecode.ticks();

        // Every chord code decodes back to the same chord.
        const std::string code = ts.chord.code();
        if (code.empty() || Chord::from_code(code).code() != code) codes_ok = false;

        // An activation fill length is always positive.
        if (ts.activation_length.has_value() && *ts.activation_length <= 0)
            fills_ok = false;
    }
    CHECK_MESSAGE(ticks_ok, what << ": ticks not strictly increasing");
    CHECK_MESSAGE(codes_ok, what << ": chord code round-trip");
    CHECK_MESSAGE(fills_ok, what << ": non-positive activation fill");
}

bool ends_with(const std::string& s, const char* suffix) {
    std::string suf(suffix);
    return s.size() >= suf.size() &&
           s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

}  // namespace

TEST_CASE("song parse holds its invariants over the corpus") {
    int charts = 0, nonempty = 0;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        ++charts;
        if (song.is_empty()) continue;
        ++nonempty;
        check_invariants(song, path);
    }

    CHECK(nonempty > 0);
    MESSAGE("checked " << charts << " charts (" << nonempty << " non-empty)");
}

TEST_CASE("song parse holds its invariants at Hard too") {
    // Same sweep at Hard. Not every chart has Hard charting (an empty song is
    // the honest answer there), but plenty do, and whatever parses must hold
    // exactly the same structure Expert does.
    int charts = 0, nonempty = 0, mids = 0, mids_nonempty = 0;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true, Difficulty::Hard);
        ++charts;
        const bool is_mid = ends_with(path, ".mid");
        if (is_mid) ++mids;
        if (song.is_empty()) continue;
        ++nonempty;
        if (is_mid) ++mids_nonempty;
        check_invariants(song, path + " [Hard]");
    }

    // The corpus has 18 .chart files with a [HardDrums] section, so this is a
    // real sweep and not a silently-empty one.
    CHECK(nonempty > 0);
    // .mid charts keep every difficulty in the one "PART DRUMS" track, so a
    // corpus .mid with Hard notes proves the pitch base works end to end.
    CHECK(mids_nonempty > 0);
    MESSAGE("checked " << charts << " charts at Hard (" << nonempty << " non-empty, "
                       << mids_nonempty << "/" << mids << " .mid)");
}

TEST_CASE(".chart: each difficulty reads its own section") {
    const std::vector<uint8_t> data = multidiff::chart_bytes();

    Song expert = load_songbytes_chart(data, true, true);
    Song hard = load_songbytes_chart(data, true, true, Difficulty::Hard);
    Song medium = load_songbytes_chart(data, true, true, Difficulty::Medium);
    Song easy = load_songbytes_chart(data, true, true, Difficulty::Easy);

    CHECK(expert.sequence.size() == multidiff::kExpertChords);
    CHECK(hard.sequence.size() == multidiff::kHardChords);
    CHECK(easy.sequence.size() == multidiff::kEasyChords);
    // No [MediumDrums] section at all: a missing difficulty parses empty
    // rather than throwing or falling back to another difficulty.
    CHECK(medium.is_empty());
    CHECK(medium.sequence.size() == multidiff::kMediumChords);

    // The sections really are different charting, not the same notes read
    // four times.
    REQUIRE(!expert.sequence.empty());
    REQUIRE(!hard.sequence.empty());
    REQUIRE(!easy.sequence.empty());
    CHECK(expert.sequence[0].chord.code() != hard.sequence[0].chord.code());
    CHECK(easy.sequence[0].chord.code() != hard.sequence[0].chord.code());

    // Everything inside the section follows it: the ghost, the pro cymbal, the
    // SP phrase and the activation fill are all in [HardDrums] alone.
    CHECK(hard.sequence[0].flag_sp);
    bool any_activation = false;
    for (const SongTimestamp& ts : hard.sequence)
        if (ts.has_activation()) any_activation = true;
    CHECK(any_activation);
    // The fill was charted, so no fills had to be synthesized.
    CHECK(hard.features.empty());
    // Pro cymbals are still a pro-drums-only reading at Hard.
    Song hard_nonpro = load_songbytes_chart(data, false, true, Difficulty::Hard);
    CHECK(hard_nonpro.sequence[1].chord.code() != hard.sequence[1].chord.code());

    check_invariants(hard, "multidiff [Hard]");
    check_invariants(easy, "multidiff [Easy]");
}

TEST_CASE(".mid: each difficulty reads its own pitch base") {
    // Corpus .mid files that carry Hard notes parse to a different sequence
    // than their Expert one — the pitch base is what selects the difficulty.
    int compared = 0, differed = 0;
    for (const std::string& path : corpus::chart_paths()) {
        if (!ends_with(path, ".mid")) continue;
        Song expert = load_songpath_mid(path, true, true);
        Song hard = load_songpath_mid(path, true, true, Difficulty::Hard);
        if (hard.is_empty()) continue;
        ++compared;
        if (hard.sequence.size() != expert.sequence.size()) ++differed;
        check_invariants(hard, path + " [Hard]");
        // Medium and Easy sit at their own bases and must not throw either.
        check_invariants(load_songpath_mid(path, true, true, Difficulty::Medium),
                         path + " [Medium]");
        check_invariants(load_songpath_mid(path, true, true, Difficulty::Easy),
                         path + " [Easy]");
        if (compared >= 5) break;  // a handful is enough; the sweep covers the rest
    }
    CHECK(compared > 0);
    // Hard is a reduction of Expert, so the two streams are not the same one
    // read twice. (Per-chart the counts could coincide; across the sample they
    // cannot all coincide.)
    CHECK(differed > 0);
}
