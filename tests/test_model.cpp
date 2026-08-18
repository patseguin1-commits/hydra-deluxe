// Parity + unit tests for core/model (the hydata.py port).
//
// The load-bearing check is that Chord::hash() reproduces CPython's tuple hash
// exactly: every entry in the golden encode table is rebuilt from its code and
// must hash back to the same key and re-encode to the same string. The unit
// half pins the string/number forms the golden analysis diff depends on.

#include "doctest.h"

#include <cstdint>
#include <map>
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

TEST_CASE("frontend_transfer_scale: measure-rate ratio, both directions") {
    // The Tom Sawyer (Onyxite) shape: ten 7/8 measures at 87.35 BPM, then
    // 7/16 at 85.1 -- no boundary at the query points, so early == late.
    std::map<int64_t, int64_t> tpm{{0, 1680}, {16800, 840}};
    std::map<int64_t, double> bpm{{0, 87.35}, {16800, 85.1}};
    SongTiming st(480, tpm, bpm);

    Activation act;
    act.timecode = st.timecode(0);
    act.sp_meter = 8;  // 16 measures: 10 of 7/8 + 6 of 7/16 -> tick 21840

    // End reconstruction matches plusmeasure.
    CHECK(st.plusmeasure(*act.timecode, 16).ticks() == 21840);

    auto scale = frontend_transfer_scale(act, st);
    REQUIRE(scale.has_value());
    // (1.75 * 87.35) / (3.5 * 85.1) = 0.51322...
    CHECK(scale->late == doctest::Approx(0.5132197).epsilon(1e-6));
    CHECK(scale->early == doctest::Approx(scale->late));

    // A SqIn extends the end by one +2-measure step, no matter how many SqIns;
    // the end stays inside the 7/16 section, so the ratio is unchanged.
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 5.0});
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 6.0});
    auto sqin_scale = frontend_transfer_scale(act, st);
    REQUIRE(sqin_scale.has_value());
    CHECK(sqin_scale->late == doctest::Approx(scale->late));

    // A SqOut does not move the end.
    act.sqinouts.clear();
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -5.0});
    auto sqout_scale = frontend_transfer_scale(act, st);
    REQUIRE(sqout_scale.has_value());
    CHECK(sqout_scale->late == doctest::Approx(scale->late));

    // Missing timecode or sp_meter (stale record): no scale.
    Activation bare;
    bare.sp_meter = 2;
    CHECK(!frontend_transfer_scale(bare, st).has_value());
    bare.timecode = st.timecode(0);
    bare.sp_meter.reset();
    CHECK(!frontend_transfer_scale(bare, st).has_value());
}

TEST_CASE("frontend_transfer_scale: direction-dependent at boundaries") {
    // Synthetic boundary-on-both-ends shape (like As I Am's m81 activation):
    // the activation sits exactly on a 6/4 -> 7/8 change and its SP end
    // exactly on a 7/8 -> 9/8 change, with a tempo change on the end tick
    // too. Early (-) hits move into the long 6/4 measure; late (+) hits into
    // the 7/8 one -- two different ratios.
    std::map<int64_t, int64_t> tpm{{0, 2880}, {2880, 1680}, {12960, 2160}};
    std::map<int64_t, double> bpm{{0, 130.0}, {11280, 133.0}, {12960, 129.0}};
    SongTiming st(480, tpm, bpm);

    Activation act;
    act.timecode = st.timecode(2880);
    act.sp_meter = 3;  // 6 measures of 7/8 -> end tick 12960

    CHECK(st.plusmeasure(*act.timecode, 6).ticks() == 12960);

    auto scale = frontend_transfer_scale(act, st);
    REQUIRE(scale.has_value());
    // early: (3.5 * 130) / (6 * 133) = 0.570175...
    CHECK(scale->early == doctest::Approx(0.5701754).epsilon(1e-6));
    // late: (4.5 * 130) / (3.5 * 129) = 1.295681...
    CHECK(scale->late == doctest::Approx(1.2956811).epsilon(1e-6));

    // The Dumpweed shape: constant 4/4, tempo changes exactly on both the
    // activation tick and the SP end tick.
    std::map<int64_t, int64_t> tpm44{{0, 1920}};
    std::map<int64_t, double> bpm2{{0, 98.0},   {1920, 97.5},
                                   {8640, 110.0}, {9600, 102.0}};
    SongTiming st2(480, tpm44, bpm2);

    Activation act2;
    act2.timecode = st2.timecode(1920);
    act2.sp_meter = 2;  // 4 measures -> end tick 9600

    auto scale2 = frontend_transfer_scale(act2, st2);
    REQUIRE(scale2.has_value());
    CHECK(scale2->early == doctest::Approx(98.0 / 110.0).epsilon(1e-9));
    CHECK(scale2->late == doctest::Approx(97.5 / 102.0).epsilon(1e-9));

    // Uniform map: exactly 1.0 both ways.
    std::map<int64_t, double> bpm120{{0, 120.0}};
    SongTiming flat(480, tpm44, bpm120);
    Activation act3;
    act3.timecode = flat.timecode(0);
    act3.sp_meter = 2;
    auto flat_scale = frontend_transfer_scale(act3, flat);
    REQUIRE(flat_scale.has_value());
    CHECK(flat_scale->early == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(flat_scale->late == doctest::Approx(1.0).epsilon(1e-12));
}
