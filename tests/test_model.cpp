// Unit tests for core/model (chord hashing/encoding, squeezes, transfer
// scale). The chord-table round-trip lives in test_chord_tables.cpp; the
// cases here pin the string/number forms and hash shapes directly.

#include "doctest.h"

#include <cstdint>
#include <map>
#include <string>

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

TEST_CASE("frontend_transfer_scales: measure-rate ratio, both directions") {
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

    auto scales = frontend_transfer_scales(act, st);
    REQUIRE(scales.has_value());
    // (1.75 * 87.35) / (3.5 * 85.1) = 0.51322...
    CHECK(scales->pre.late == doctest::Approx(0.5132197).epsilon(1e-6));
    CHECK(scales->pre.early == doctest::Approx(scales->pre.late));
    // No SqIn: the two ends coincide.
    CHECK(scales->post.late == doctest::Approx(scales->pre.late));
    CHECK(scales->post.early == doctest::Approx(scales->pre.early));

    // A SqIn extends the *post* end by one +2-measure step, no matter how
    // many SqIns; here the extended end stays inside the 7/16 section, so
    // both ratios are unchanged. The pre end never moves.
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 5.0});
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 6.0});
    auto sqin_scales = frontend_transfer_scales(act, st);
    REQUIRE(sqin_scales.has_value());
    CHECK(sqin_scales->pre.late == doctest::Approx(scales->pre.late));
    CHECK(sqin_scales->post.late == doctest::Approx(scales->pre.late));

    // A SqOut does not move either end.
    act.sqinouts.clear();
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -5.0});
    auto sqout_scales = frontend_transfer_scales(act, st);
    REQUIRE(sqout_scales.has_value());
    CHECK(sqout_scales->pre.late == doctest::Approx(scales->pre.late));
    CHECK(sqout_scales->post.late == doctest::Approx(scales->pre.late));

    // Missing timecode or sp_meter (stale record): no scale.
    Activation bare;
    bare.sp_meter = 2;
    CHECK(!frontend_transfer_scales(bare, st).has_value());
    bare.timecode = st.timecode(0);
    bare.sp_meter.reset();
    CHECK(!frontend_transfer_scales(bare, st).has_value());
}

TEST_CASE("frontend_transfer_scales: a SqIn splits the two ends") {
    // Flat 4/4, tempo change between the pre-extension end (4 measures,
    // tick 7680) and the SqIn-extended end (6 measures, tick 11520): the
    // SqIn feasibility keeps r = 1 while the backends read 120/150 = 0.8.
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{{0, 120.0}, {9600, 150.0}};
    SongTiming st(480, tpm, bpm);

    Activation act;
    act.timecode = st.timecode(0);
    act.sp_meter = 2;
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 5.0});

    auto scales = frontend_transfer_scales(act, st);
    REQUIRE(scales.has_value());
    CHECK(scales->pre.late == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(scales->pre.early == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(scales->post.late == doctest::Approx(0.8).epsilon(1e-9));
    CHECK(scales->post.early == doctest::Approx(0.8).epsilon(1e-9));
}

TEST_CASE("frontend_transfer_scales: direction-dependent at boundaries") {
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

    auto scale = frontend_transfer_scales(act, st);
    REQUIRE(scale.has_value());
    // early: (3.5 * 130) / (6 * 133) = 0.570175...
    CHECK(scale->pre.early == doctest::Approx(0.5701754).epsilon(1e-6));
    // late: (4.5 * 130) / (3.5 * 129) = 1.295681...
    CHECK(scale->pre.late == doctest::Approx(1.2956811).epsilon(1e-6));

    // The Dumpweed shape: constant 4/4, tempo changes exactly on both the
    // activation tick and the SP end tick.
    std::map<int64_t, int64_t> tpm44{{0, 1920}};
    std::map<int64_t, double> bpm2{{0, 98.0},   {1920, 97.5},
                                   {8640, 110.0}, {9600, 102.0}};
    SongTiming st2(480, tpm44, bpm2);

    Activation act2;
    act2.timecode = st2.timecode(1920);
    act2.sp_meter = 2;  // 4 measures -> end tick 9600

    auto scale2 = frontend_transfer_scales(act2, st2);
    REQUIRE(scale2.has_value());
    CHECK(scale2->pre.early == doctest::Approx(98.0 / 110.0).epsilon(1e-9));
    CHECK(scale2->pre.late == doctest::Approx(97.5 / 102.0).epsilon(1e-9));

    // Uniform map: exactly 1.0 both ways.
    std::map<int64_t, double> bpm120{{0, 120.0}};
    SongTiming flat(480, tpm44, bpm120);
    Activation act3;
    act3.timecode = flat.timecode(0);
    act3.sp_meter = 2;
    auto flat_scale = frontend_transfer_scales(act3, flat);
    REQUIRE(flat_scale.has_value());
    CHECK(flat_scale->pre.early == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(flat_scale->pre.late == doctest::Approx(1.0).epsilon(1e-12));
}

TEST_CASE("transfer_scale_relevance: SqIns want a late frontend") {
    // A SqIn-only activation must flag the late direction even with no
    // backend rows in range (the old code only set it via backends).
    Activation act;
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 1.5});
    TransferRelevance rel = transfer_scale_relevance(act, {});
    CHECK(rel.late);
    CHECK_FALSE(rel.early);

    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -3.0});
    rel = transfer_scale_relevance(act, {});
    CHECK(rel.late);
    CHECK(rel.early);
}

TEST_CASE("field fixture: What's My Age Again? (Sync Chart) SqOut") {
    // Field-verified: Hoph2o's Sync Chart, Expert Pro Drums 2x, path
    // "1- 1 0". The activation at m31.1.0 (tick 57600) sits exactly on a
    // 155 -> 160 BPM change; the 3-bar SP end at m37.1.0 (tick 69120) sits
    // exactly on a 157 -> 160 change. The SqOut note is the last note of the
    // SP phrase, 240 ticks before the end, inside the 157 section. A player
    // at 130% speed hit the frontend 75.4 real ms early and the note 72.3
    // real ms late and the squeeze still failed -- by the ~0.25 ms the
    // frontend transfer scale (x0.9873) removes from the early arm.
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{
        {0, 155.0}, {57600, 160.0}, {67200, 157.0}, {69120, 160.0}};
    SongTiming st(480, tpm, bpm);

    Activation act;
    act.timecode = st.timecode(57600);
    act.sp_meter = 3;  // 6 measures -> tick 69120
    CHECK(st.plusmeasure(*act.timecode, 6).ticks() == 69120);

    auto scales = frontend_transfer_scales(act, st);
    REQUIRE(scales.has_value());
    CHECK(scales->pre.early == doctest::Approx(0.987263).epsilon(1e-6));
    CHECK(scales->pre.late == doctest::Approx(1.0).epsilon(1e-12));

    // The gap: 240 ticks at 157 BPM.
    double gap = st.timecode(69120).ms() - st.timecode(68880).ms();
    CHECK(gap == doctest::Approx(191.0825).epsilon(1e-5));

    SPSqueeze sqout{SqueezeKind::SqOut, -gap};
    const double r = scales->pre.early;
    CHECK(sqout.difficulty() == doctest::Approx(191.0825).epsilon(1e-5));
    CHECK(sqout.difficulty() / (1.0 + r) == doctest::Approx(96.15).epsilon(1e-3));
    CHECK(squeeze_budget_ms(r, 85.0) == doctest::Approx(168.92).epsilon(1e-4));
    CHECK(squeeze_budget_ms(r, 70.0) == doctest::Approx(139.11).epsilon(1e-4));

    // The failed 130% attempt: real-ms displacements scale by 1.3 into chart
    // ms; the early arm is then discounted by r. 75.4/72.3 misses the gap by
    // a quarter of a chart ms; 78/75 covers it.
    double failed = 75.4 * 1.3 * r + 72.3 * 1.3;
    double landed = 78.0 * 1.3 * r + 75.0 * 1.3;
    CHECK(failed < gap);
    CHECK(failed == doctest::Approx(190.76).epsilon(1e-3));
    CHECK(landed > gap);

    // The description pins the joint-constraint text (W = 85, 130% speed).
    CHECK(sqout.description(r, 85.0, 130) ==
          "SqOut: needs frontend(early)x0.987 + note(late) > 191.1 ms\n"
          "  even split: 96.2 ms each; budget 168.9 ms @1x (W=85)"
          " -> needs >=115% speed\n"
          "  at 130%: 74.0 ms real per hit");
    // At the outdated 70 ms window the artifact's numbers reproduce.
    CHECK(sqout.description(r, 70.0, 100) ==
          "SqOut: needs frontend(early)x0.987 + note(late) > 191.1 ms\n"
          "  even split: 96.2 ms each; budget 139.1 ms @1x (W=70)"
          " -> needs >=140% speed");
    // Leeway squeezes keep the legacy single-hit line.
    SPSqueeze easy{SqueezeKind::SqOut, 5.0};
    CHECK(easy.description() == "SqOut: Note timing must be later than -5.0ms.");
}
