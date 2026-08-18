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
    REQUIRE(table.size() > 0);

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
