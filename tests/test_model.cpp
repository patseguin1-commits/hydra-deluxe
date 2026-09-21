// Unit tests for core/model (chord hashing/encoding, squeezes). The
// chord-table round-trip lives in test_chord_tables.cpp and the transfer
// scales in test_squeeze_rating.cpp; the cases here pin the string/number
// forms and hash shapes directly.

#include "doctest.h"

#include <string>
#include <vector>

#include "core/model.h"

using namespace hydra;

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
    a.e_offset = 300.0;  // not e-critical (>= kCalibrationFillWindowMs)
    CHECK(a.notationstr() == "2");

    a.e_offset = kCalibrationFillWindowMs;  // boundary: not e-critical
    CHECK(a.notationstr() == "2");

    a.e_offset = 50.0;  // e-critical
    CHECK(a.notationstr() == "E2");

    a.e_offset = kCalibrationFillWindowMs - 0.1;  // boundary: e-critical
    CHECK(a.notationstr() == "E2");

    a.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, -1.0});
    a.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 1.0});
    CHECK(a.notationstr() == "E2+-");
}

TEST_CASE("Path pathstring and pathstring_verbose") {
    Path p;
    Activation a;
    a.skips = 1;
    a.e_offset = 400.0;  // not e-critical, no sqinouts -> verbose == notationstr
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

// Backend rows past a squeezed-out note are impossible in game: the sqout note
// is hit after SP ends, so every note after it is hit outside SP too. The
// engine trims them at record build; this pins the display-layer guard that
// keeps records stored before that fix from showing them.
TEST_CASE("display_backends drops rows beyond a squeeze out") {
    Activation a;
    const std::vector<double> offsets = {-368.1, -184.0, 0.0, 184.0, 368.1};
    for (double off : offsets) {
        BackendSqueeze bsq;
        bsq.points = 50;
        bsq.offset_ms = off;
        a.backends.push_back(bsq);
    }

    // No squeeze out: every row is inside the +/-500 ms display window.
    REQUIRE(a.display_backends().size() == offsets.size());
    for (size_t i = 0; i < offsets.size(); ++i)
        CHECK(a.display_backends()[i].offset_ms.value() == offsets[i]);

    // Squeezing out at -184.0 keeps that row and the one before it, and drops
    // the three that land after it.
    a.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -184.0});
    std::vector<BackendSqueeze> shown = a.display_backends();
    REQUIRE(shown.size() == 2);
    CHECK(shown[0].offset_ms.value() == -368.1);
    CHECK(shown[1].offset_ms.value() == -184.0);
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

TEST_CASE("a ghost or accent kick scores double, like a pad") {
    CHECK(ChordNote{NoteColor::Kick}.basescore() == 50);
    CHECK(ChordNote{NoteColor::Kick, NoteDynamicType::Ghost}.basescore() == 100);
    CHECK(ChordNote{NoteColor::Kick, NoteDynamicType::Accent}.basescore() == 100);
    // A 2x kick is still a kick: the foot, not the value, is what changes.
    CHECK(ChordNote{NoteColor::Kick, NoteDynamicType::Ghost,
                    NoteCymbalType::Normal, true}
              .basescore() == 100);
    CHECK(allows_dynamics(NoteColor::Kick));
}

TEST_CASE("ChordNote::str shows the kick's dynamic and its 2x flag") {
    auto kick = [](NoteDynamicType dyn, bool is2x) {
        return ChordNote{NoteColor::Kick, dyn, NoteCymbalType::Normal, is2x}.str();
    };
    CHECK(kick(NoteDynamicType::Normal, false) == "Kick");
    CHECK(kick(NoteDynamicType::Ghost, false) == "Kick (Ghost)");
    CHECK(kick(NoteDynamicType::Accent, false) == "Kick (Accent)");
    CHECK(kick(NoteDynamicType::Normal, true) == "Kick (2x)");
    CHECK(kick(NoteDynamicType::Ghost, true) == "Kick (Ghost, 2x)");
    CHECK(kick(NoteDynamicType::Accent, true) == "Kick (Accent, 2x)");

    // Pads read exactly as they always did.
    CHECK(ChordNote{NoteColor::Red}.str() == "Red");
    CHECK(ChordNote{NoteColor::Red, NoteDynamicType::Ghost}.str() ==
          "Red (Ghost)");
    CHECK(ChordNote{NoteColor::Yellow, NoteDynamicType::Accent,
                    NoteCymbalType::Cymbal, false}
              .str() == "YellowCym (Accent)");
}

TEST_CASE("Chord::code prefixes a ghost/accent kick chord with g/a") {
    Chord normal;
    normal.add_note(NoteColor::Kick);
    normal.add_note(NoteColor::Red);
    const std::string base = normal.code();

    Chord ghost;
    ghost.add_note(NoteColor::Kick).dynamictype = NoteDynamicType::Ghost;
    ghost.add_note(NoteColor::Red);
    CHECK(ghost.code() == "g" + base);
    CHECK(Chord::from_code("g" + base) == ghost);

    Chord accent;
    accent.add_note(NoteColor::Kick).dynamictype = NoteDynamicType::Accent;
    accent.add_note(NoteColor::Red);
    CHECK(accent.code() == "a" + base);
    CHECK(Chord::from_code("a" + base) == accent);

    CHECK(ghost.rowstr() == "[Kick (Ghost) - Red]");
}
