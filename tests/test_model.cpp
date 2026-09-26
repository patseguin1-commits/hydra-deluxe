// Unit tests for core/model (chord encoding, squeezes). The chord-code
// round-trip lives in test_chord_code.cpp and the transfer scales in
// test_squeeze_rating.cpp; the cases here pin the string/number forms
// directly.

#include "doctest.h"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/scoring.h"
#include "core/timing.h"
#include "core/backend_value.h"
#include "core/rules.h"
#include "parse/song.h"

using namespace hydra;

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

TEST_CASE("note value: one owner for base, cymbal and dynamic points") {
    CHECK(kNoteBasePoints == 50);
    CHECK(kCymbalBonusPoints == 15);
    CHECK(kSoloBonusPerNote == 100);
    const ChordNote accent_cymbal{NoteColor::Yellow, NoteDynamicType::Accent,
                                  NoteCymbalType::Cymbal, false};
    CHECK(accent_cymbal.basescore() == (kNoteBasePoints + kCymbalBonusPoints) * 2);
    CHECK(ChordNote{NoteColor::Red}.basescore() == kNoteBasePoints);
}

TEST_CASE("category_scores: the squeeze-out cut is basescore at each note's multiplier") {
    Chord c;
    c.at(NoteColor::Red) = ChordNote{NoteColor::Red};
    c.at(NoteColor::Yellow) = ChordNote{NoteColor::Yellow, NoteDynamicType::Accent,
                                        NoteCymbalType::Cymbal, false};
    const std::vector<ChordNote> notes = c.notes(true);
    REQUIRE(notes.size() == 2);
    for (int combo : {0, 8, 9, 29, 45}) {
        CAPTURE(combo);
        // first_note (the default): only note 0 loses its SP doubling.
        std::vector<CategoryScores> per_note;
        const CategoryScores cs = category_scores(c, combo, &per_note);
        const int first_cut = notes[0].basescore() * to_multiplier(combo + 1);
        CHECK(cs.sqout_reduction == first_cut);
        REQUIRE(per_note.size() == 2);
        CHECK(per_note[0].sqout_reduction == first_cut);
        CHECK(per_note[1].sqout_reduction == 0);
        CHECK(cs.sqout_sp() == cs.sp - cs.sqout_reduction);

        // whole_chord: every note loses it, each at its own multiplier.
        std::vector<CategoryScores> whole_per_note;
        const CategoryScores whole =
            category_scores(c, combo, &whole_per_note, core::SqOutRule::WholeChord);
        int whole_cut = 0;
        for (size_t i = 0; i < notes.size(); ++i) {
            const int note_cut =
                notes[i].basescore() * to_multiplier(combo + 1 + static_cast<int>(i));
            REQUIRE(whole_per_note.size() == 2);
            CHECK(whole_per_note[i].sqout_reduction == note_cut);
            whole_cut += note_cut;
        }
        CHECK(whole.sqout_reduction == whole_cut);
        CHECK(whole.sqout_sp() == whole.sp - whole.sqout_reduction);
    }
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

TEST_CASE("squeeze_difficulty and is_e0: one owner for the engine and the model") {
    CHECK(squeeze_difficulty(/*is_sqin=*/true, 3.5) == 3.5);
    CHECK(squeeze_difficulty(/*is_sqin=*/false, -3.5) == 3.5);
    // A SqOut at exactly 0 is +0.0, never -0.0: the "-x + 0.0" idiom stays.
    CHECK_FALSE(std::signbit(squeeze_difficulty(false, 0.0)));
    CHECK(SPSqueeze{SqueezeKind::SqOut, -3.5}.difficulty() == squeeze_difficulty(false, -3.5));

    CHECK(is_e0(kCalibrationFillWindowMs - 0.1, 0));
    CHECK_FALSE(is_e0(kCalibrationFillWindowMs, 0));
    CHECK_FALSE(is_e0(10.0, 1));
    CHECK(calibration_fill_difficulty(-4.0) == 4.0);
    CHECK_FALSE(std::signbit(calibration_fill_difficulty(0.0)));

    Activation a;
    a.e_offset = 10.0;
    a.skips = 0;
    CHECK(a.is_E0() == is_e0(10.0, 0));
    REQUIRE(a.e_difficulty().has_value());
    CHECK(*a.e_difficulty() == calibration_fill_difficulty(10.0));
}

TEST_CASE("Path::is_difficult: past the difficult floor, not at it") {
    Path empty;
    CHECK_FALSE(empty.is_difficult());

    Activation a;
    a.skips = 0;
    a.e_offset = 300.0;  // not e-critical
    a.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -(kDifficultMs + 0.5)});
    Path hard;
    hard.activations.push_back(a);
    CHECK(hard.is_difficult());

    Path edge = hard;
    edge.activations[0].sqinouts[0].offset_ms = -kDifficultMs;
    CHECK_FALSE(edge.is_difficult());  // exactly at the floor is not past it
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
    int64_t tick = 100;  // chart order, one row per 100 ticks
    for (double off : offsets) {
        BackendSqueeze bsq;
        bsq.timecode = Timecode::raw(tick);
        tick += 100;
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
    a.sqout_tick = 200;  // the -184.0 row
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

TEST_CASE("Chord::code spells a ghost/accent kick in the kick lane") {
    Chord normal;
    normal.add_note(NoteColor::Kick);
    normal.add_note(NoteColor::Red);
    CHECK(normal.code() == "nn...");

    Chord ghost;
    ghost.add_note(NoteColor::Kick).dynamictype = NoteDynamicType::Ghost;
    ghost.add_note(NoteColor::Red);
    CHECK(ghost.code() == "gn...");
    CHECK(Chord::from_code("gn...") == ghost);

    Chord accent;
    accent.add_note(NoteColor::Kick).dynamictype = NoteDynamicType::Accent;
    accent.add_note(NoteColor::Red);
    CHECK(accent.code() == "an...");
    CHECK(Chord::from_code("an...") == accent);

    CHECK(ghost.rowstr() == "[Kick (Ghost) - Red]");
}

// The engine's three cases, as values. The deactivation edge in
// create_deactivated_path adds `value - already_paid`, where rows at or
// before the SP end were already paid in full by the SP walk.
TEST_CASE("backend_row_value: every engine case") {
    const double lw = core::default_rules().backend_leeway_ms;  // 3 ms
    using P = core::SqOutPosition;
    using core::backend_row_value;

    // No squeeze-out: full value inside SP or inside the leeway, else 0.
    CHECK(backend_row_value(-50.0, 460, 260, P::NoSqOut, lw) == 460);
    CHECK(backend_row_value(0.0, 460, 260, P::NoSqOut, lw) == 460);
    CHECK(backend_row_value(2.999, 460, 260, P::NoSqOut, lw) == 460);
    CHECK(backend_row_value(3.0, 460, 260, P::NoSqOut, lw) == 0);

    // Before the squeezed-out chord: same as no squeeze-out.
    CHECK(backend_row_value(-50.0, 460, 260, P::Before, lw) == 460);
    CHECK(backend_row_value(1.5, 460, 260, P::Before, lw) == 460);
    CHECK(backend_row_value(10.0, 460, 260, P::Before, lw) == 0);

    // The squeezed-out chord itself keeps only its reduced value, and only
    // where it would have been counted at all.
    CHECK(backend_row_value(-5.0, 460, 260, P::Exact, lw) == 260);
    CHECK(backend_row_value(1.5, 460, 260, P::Exact, lw) == 260);
    // Round and Round (Ratt), second activation: R+Y squeezed out
    // 479.999 ms past the SP end. The engine counts it as 0.
    CHECK(backend_row_value(479.999, 460, 260, P::Exact, lw) == 0);

    // After the squeezed-out chord nothing is under Star Power.
    CHECK(backend_row_value(-50.0, 460, 260, P::After, lw) == 0);
    CHECK(backend_row_value(1.5, 460, 260, P::After, lw) == 0);

    // The leeway is the user's rule (hydra_rules.ini), not a constant.
    CHECK(backend_row_value(5.0, 460, 260, P::NoSqOut, 10.0) == 460);
    CHECK(backend_row_value(5.0, 460, 260, P::NoSqOut, lw) == 0);

    // Position comes from ticks, the way the engine compares them.
    CHECK(core::sqout_position(100, std::nullopt) == P::NoSqOut);
    CHECK(core::sqout_position(99, 100) == P::Before);
    CHECK(core::sqout_position(100, 100) == P::Exact);
    CHECK(core::sqout_position(101, 100) == P::After);

    // Only rows at or before the SP end were paid by the SP walk.
    CHECK(core::paid_by_sp_walk(0.0));
    CHECK(core::paid_by_sp_walk(-0.5));
    CHECK_FALSE(core::paid_by_sp_walk(0.5));
}

TEST_CASE("difficulty names: one list, one spelling") {
    // The dropdown indexes this list by the enum's value, so order matters.
    REQUIRE(std::size(kAllDifficulties) == 4);
    for (size_t i = 0; i < std::size(kAllDifficulties); ++i)
        CHECK(static_cast<size_t>(kAllDifficulties[i]) == i);
    CHECK(std::string(difficulty_name(kAllDifficulties[0])) == "Expert");
    CHECK(std::string(difficulty_name(kAllDifficulties[3])) == "Easy");
}

TEST_CASE("difficulty_from_name: any case, nothing else") {
    CHECK(difficulty_from_name("Hard") == Difficulty::Hard);
    CHECK(difficulty_from_name("medium") == Difficulty::Medium);
    CHECK(difficulty_from_name("EASY") == Difficulty::Easy);
    CHECK(difficulty_from_name("eXpErT") == Difficulty::Expert);
    CHECK_FALSE(difficulty_from_name("Legendary").has_value());
    CHECK_FALSE(difficulty_from_name("Har").has_value());
    CHECK_FALSE(difficulty_from_name("").has_value());
}

TEST_CASE("no_notes_message names the difficulty and the drum mode") {
    CHECK(no_notes_message(Difficulty::Hard, true) == "No Hard Pro Drums notes in this chart.");
    CHECK(no_notes_message(Difficulty::Expert, false) == "No Expert Drums notes in this chart.");
}

TEST_CASE("title_or_unknown: one fallback for a song with no usable name") {
    CHECK(std::string(kUnknownTitle) == "(unknown)");
    CHECK(title_or_unknown("") == "(unknown)");
    // What the metadata readers wrote before this fallback existed.
    CHECK(title_or_unknown("<unknown title>") == "(unknown)");
    CHECK(title_or_unknown("Some Song") == "Some Song");
}

// A multiplier squeeze is a chord whose notes straddle a to_multiplier step.
// For 2- and 3-note chords, MultSqueeze accepts exactly the straddling
// combos. For 4-note chords it accepts only 7, 17 and 27; this pins that
// as-is (see the comment on MultSqueeze::validate).
TEST_CASE("MultSqueeze accepts exactly the 2- and 3-note chords that straddle a multiplier step") {
    const NoteColor order[] = {NoteColor::Red, NoteColor::Yellow, NoteColor::Kick,
                               NoteColor::Blue, NoteColor::Green};
    auto chord_of = [&](int n) {
        Chord c;
        for (int i = 0; i < n; ++i) c.add_note(order[i]);
        c.apply_cymbal(NoteColor::Yellow);  // a cymbal among pads: something to squeeze
        return c;
    };
    auto accepted = [](const Chord& c, int combo) {
        try {
            MultSqueeze ms(c, combo);
            return true;
        } catch (const std::invalid_argument&) {
            return false;
        }
    };
    for (int n = 2; n <= 3; ++n)
        for (int combo = 0; combo < 40; ++combo) {
            const bool straddles = to_multiplier(combo + 1) < to_multiplier(combo + n);
            CHECK_MESSAGE(accepted(chord_of(n), combo) == straddles,
                          "n=" << n << " combo=" << combo);
        }
    for (int combo = 0; combo < 40; ++combo)
        CHECK_MESSAGE(accepted(chord_of(4), combo) == (combo == 7 || combo == 17 || combo == 27),
                      "n=4 combo=" << combo);
}
