// Self-consistency for the generated chord encode table: every entry must
// round-trip through Chord::from_code back to the same hash key and code
// string. Catches a stale or hand-edited chord_tables.cpp.

#include "doctest.h"

#include <cstdint>
#include <string>

#include "core/chord_tables.h"
#include "core/model.h"

TEST_CASE("chord_encode table round-trips through Chord") {
    const auto& table = hydra::chord_encode();
    // 551 chords with no kick or a normal kick (the frozen legacy codes), plus
    // 736 with a ghost or accent kick.
    REQUIRE(table.size() == 1287);

    int hash_wrong = 0, code_wrong = 0, lookup_wrong = 0;
    for (const auto& [hash, code] : table) {
        hydra::Chord chord = hydra::Chord::from_code(code);

        if (chord.hash() != hash && ++hash_wrong <= 5)
            CHECK_MESSAGE(chord.hash() == hash,
                          "code '" << code << "' hashed to " << chord.hash()
                                   << " expected " << hash);
        if (chord.code() != code && ++code_wrong <= 5)
            CHECK_MESSAGE(chord.code() == code,
                          "hash " << hash << " -> '" << chord.code()
                                  << "' expected '" << code << "'");

        const std::string* got = hydra::encode_chord(hash);
        if ((!got || *got != code) && ++lookup_wrong <= 5)
            CHECK_MESSAGE(false, "encode_chord(" << hash << ") mismatch");
    }
    CHECK(hash_wrong == 0);
    CHECK(code_wrong == 0);
    CHECK(lookup_wrong == 0);
    MESSAGE("round-tripped " << table.size() << " chords");
}

TEST_CASE("a ghost or accent kick takes the normal kick's code, prefixed") {
    // The 736 new chords are the old ones with the kick's dynamic swapped, so
    // their codes are "g"/"a" + the legacy code. Nothing legacy moved.
    const auto& table = hydra::chord_encode();
    int checked = 0;
    for (const auto& [hash, code] : table) {
        // The filter is the kick's dynamic, not the code's first letter: "g"
        // and "a" are themselves legacy codes for normal-kick chords.
        hydra::Chord normal = hydra::Chord::from_code(code);
        const auto& kick = normal.at(hydra::NoteColor::Kick);
        if (!kick.has_value()) continue;
        if (kick->dynamictype != hydra::NoteDynamicType::Normal) continue;

        hydra::Chord ghost = hydra::Chord::from_code("g" + code);
        REQUIRE(ghost.at(hydra::NoteColor::Kick).has_value());
        CHECK(ghost.at(hydra::NoteColor::Kick)->dynamictype ==
              hydra::NoteDynamicType::Ghost);
        CHECK(ghost.at(hydra::NoteColor::Kick)->is2x == kick->is2x);
        CHECK(ghost.code() == "g" + code);

        hydra::Chord accent = hydra::Chord::from_code("a" + code);
        REQUIRE(accent.at(hydra::NoteColor::Kick).has_value());
        CHECK(accent.at(hydra::NoteColor::Kick)->dynamictype ==
              hydra::NoteDynamicType::Accent);
        CHECK(accent.at(hydra::NoteColor::Kick)->is2x == kick->is2x);
        CHECK(accent.code() == "a" + code);

        // The pads are untouched by the prefix.
        for (hydra::NoteColor c : {hydra::NoteColor::Red, hydra::NoteColor::Yellow,
                                   hydra::NoteColor::Blue, hydra::NoteColor::Green}) {
            CHECK(ghost.at(c).has_value() == normal.at(c).has_value());
            if (normal.at(c).has_value()) CHECK(*ghost.at(c) == *normal.at(c));
        }
        ++checked;
    }
    // 368 chords carry a normal kick; each yields a ghost and an accent twin.
    CHECK(checked == 368);
    MESSAGE("prefixed " << checked << " normal-kick chords");
}
