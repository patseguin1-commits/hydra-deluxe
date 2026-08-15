// Parity + unit tests for core/model (the hydata.py port).
//
// The load-bearing check is that Chord::hash() reproduces CPython's tuple hash
// exactly: every entry in the golden encode table is rebuilt from its code and
// must hash back to the same key and re-encode to the same string. The unit
// half pins the string/number forms the golden analysis diff depends on.

#include "doctest.h"

#include <cstdint>
#include <string>

#include "core/model.h"
#include "golden_util.h"

using namespace hydra;

TEST_CASE("Chord hash + code reproduce CPython over the whole encode table") {
    const golden::json enc = golden::chord_encode();

    int checked = 0, hash_wrong = 0, code_wrong = 0;
    for (auto it = enc.begin(); it != enc.end(); ++it) {
        const int64_t want_hash = std::stoll(it.key());
        const std::string want_code = it.value().get<std::string>();

        Chord chord = Chord::from_code(want_code);
        ++checked;

        if (chord.hash() != want_hash) {
            if (++hash_wrong <= 5)
                CHECK_MESSAGE(chord.hash() == want_hash,
                              "code '" << want_code << "' hashed to "
                                       << chord.hash() << " expected "
                                       << want_hash);
        }
        if (chord.code() != want_code) {
            if (++code_wrong <= 5)
                CHECK_MESSAGE(chord.code() == want_code,
                              "hash " << want_hash << " -> '" << chord.code()
                                      << "' expected '" << want_code << "'");
        }
    }

    CHECK(hash_wrong == 0);
    CHECK(code_wrong == 0);
    MESSAGE("checked " << checked << " chords against the encode table");
}

TEST_CASE("ChordNote hash matches ChordNote.__hash__") {
    // 1000*color + 100*dyn + 10*cym + is2x.
    ChordNote red{NoteColor::Red};  // color 2, dyn 1, cym 1, 2x 0
    CHECK(red.hash() == 2110);
    ChordNote green_cym_accent{NoteColor::Green, NoteDynamicType::Accent,
                               NoteCymbalType::Cymbal, false};
    CHECK(green_cym_accent.hash() == 5 * 1000 + 3 * 100 + 2 * 10);
    ChordNote kick2x{NoteColor::Kick, NoteDynamicType::Normal,
                     NoteCymbalType::Normal, true};
    CHECK(kick2x.hash() == 1111);
}

TEST_CASE("basescore matches ChordNote.basescore") {
    CHECK(ChordNote{NoteColor::Red}.basescore() == 50);
    CHECK(ChordNote{NoteColor::Yellow, NoteDynamicType::Normal,
                    NoteCymbalType::Cymbal, false}
              .basescore() == 65);
    CHECK(ChordNote{NoteColor::Red, NoteDynamicType::Accent}.basescore() == 100);
    CHECK(ChordNote{NoteColor::Yellow, NoteDynamicType::Ghost,
                    NoteCymbalType::Cymbal, false}
              .basescore() == 130);
}

TEST_CASE("group_thousands matches Python {:,}") {
    CHECK(group_thousands(0) == "0");
    CHECK(group_thousands(999) == "999");
    CHECK(group_thousands(1000) == "1,000");
    CHECK(group_thousands(228710) == "228,710");
    CHECK(group_thousands(1234567) == "1,234,567");
    CHECK(group_thousands(-1234567) == "-1,234,567");
}

TEST_CASE("squeeze symbols, timing, difficulty") {
    SPSqueeze sqin{SqueezeKind::SqIn, -5.0};
    SPSqueeze sqout{SqueezeKind::SqOut, 3.0};
    CHECK(std::string(sqin.symbol()) == "+");
    CHECK(std::string(sqout.symbol()) == "-");
    CHECK(sqin.difficulty() == -5.0);
    CHECK(sqout.difficulty() == -3.0);
    CHECK(sqin.timing() == 5.0);
    CHECK(std::string(sqin.type_name()) == "SqIn");
    CHECK(std::string(sqout.type_name()) == "SqOut");
}

TEST_CASE("Activation notationstr: E prefix, skips, symbols") {
    Activation a;
    a.skips = 2;
    a.e_offset = 100.0;  // not e-critical (>= 70)
    CHECK(a.notationstr() == "2");

    a.e_offset = 50.0;  // e-critical
    CHECK(a.notationstr() == "E2");

    a.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, -1.0});
    a.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 1.0});
    CHECK(a.notationstr() == "E2+-");
}

TEST_CASE("Path pathstring and pathstring_verbose") {
    Path p;
    Activation a;
    a.skips = 1;
    a.e_offset = 200.0;  // not e-critical, no sqinouts -> verbose == notationstr
    p.activations.push_back(a);
    p.score_base = 100000;  // totalscore == 100000

    CHECK(p.pathstring() == "1");
    CHECK(p.pathstring_verbose() ==
          "(No mult squeezes.) | 1 | Score: 100,000");

    Path empty;
    CHECK(empty.pathstring() == "(No activations.)");
    CHECK(empty.pathstring_verbose() ==
          "(No mult squeezes.) | (No activations.) | Score: 0");
}

TEST_CASE("Chord rowstr / notationstr / disco flip") {
    Chord c;
    c.add_note(NoteColor::Red);
    c.add_note(NoteColor::Green);
    CHECK(c.notationstr() == "[ R  G]");
    // Green is cymbal-capable, so a normal green renders as "GreenTom"; red is
    // not, so it is just "Red".
    CHECK(c.rowstr() == "[Red - GreenTom]");

    // Disco flip swaps red<->yellow (red becomes a yellow cymbal).
    Chord d;
    d.add_note(NoteColor::Red);
    d.apply_disco_flip();
    REQUIRE(d.at(NoteColor::Yellow).has_value());
    CHECK(d.at(NoteColor::Yellow)->cymbaltype == NoteCymbalType::Cymbal);
    CHECK_FALSE(d.at(NoteColor::Red).has_value());
}
