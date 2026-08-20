// Unit tests for core/model (chord hashing/encoding, squeezes, transfer
// scale). The chord-table round-trip lives in test_chord_tables.cpp; the
// cases here pin the string/number forms and hash shapes directly.

#include "doctest.h"

#include <cmath>
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
    // The activations here carry no backend rows, so this case pins the
    // measure-count fallback path (the deact-node derivation is covered by
    // the Dumpweed regression case below).
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

    // With a 0.0-offset backend row marking the deact node at tick 11520,
    // the D-anchored build-down (pre = D - 2 measures = 7680) reproduces
    // exactly the same split.
    BackendSqueeze d0;
    d0.timecode = st.timecode(11520);
    d0.offset_ms = 0.0;
    act.backends.push_back(d0);
    auto anchored = frontend_transfer_scales(act, st);
    REQUIRE(anchored.has_value());
    CHECK(anchored->pre.late == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(anchored->pre.early == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(anchored->post.late == doctest::Approx(0.8).epsilon(1e-9));
    CHECK(anchored->post.early == doctest::Approx(0.8).epsilon(1e-9));
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
    // activation tick and the SP end tick. No backend rows here, so this is
    // the plain act + 2*B-measure fallback; the REAL Dumpweed activation
    // collects a phrase mid-SP and lands its deact node 2 measures later --
    // that shape is pinned by the regression case below.
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

    // This activation collects no phrase mid-SP (its deact node IS the plain
    // 6-measure end), so the D-anchored derivation from a 0.0-offset backend
    // row agrees with the fallback exactly -- field-verified cross-check.
    Activation with_backend = act;
    BackendSqueeze d0;
    d0.timecode = st.timecode(69120);
    d0.offset_ms = 0.0;
    with_backend.backends.push_back(d0);
    auto anchored = frontend_transfer_scales(with_backend, st);
    REQUIRE(anchored.has_value());
    CHECK(anchored->pre.early == doctest::Approx(scales->pre.early).epsilon(1e-12));
    CHECK(anchored->pre.late == doctest::Approx(scales->pre.late).epsilon(1e-12));

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

    // The description keeps the legacy single-hit line.
    CHECK(sqout.description() == "SqOut: Note timing must be later than 191.1ms.");
    SPSqueeze easy{SqueezeKind::SqOut, 5.0};
    CHECK(easy.description() == "SqOut: Note timing must be later than -5.0ms.");

    // Stored transfer scales are display-only: difficulty stays the raw gap.
    Activation stamped = act;
    stamped.skips = 1;
    stamped.e_offset = 300.0;  // not e-critical
    stamped.transfer_pre = TransferScale{r, 1.0};
    stamped.transfer_post = stamped.transfer_pre;
    stamped.sqinouts.push_back(sqout);
    REQUIRE(stamped.difficulty().has_value());
    CHECK(*stamped.difficulty() == doctest::Approx(191.0825).epsilon(1e-5));
    CHECK(stamped.is_difficult());
}

TEST_CASE("field fixture: Dumpweed SqOut end anchored on the deact node") {
    // Field-verified: blink-182 - Dumpweed (Hoph2o), Expert Pro Drums 2x,
    // activation "1-" at m19.1.0 (tick 34560), sp_meter 2. The player
    // collects ONE SP phrase mid-SP, so the search's deact node sits at tick
    // 46080 (act + 6 measures), not the plain act + 4 (42240) -- and a tempo
    // change on each tick makes the difference visible: the old short-end
    // reconstruction said early x0.890911, the true end gives x0.935114.
    // An FC video shows frontend -74.1 ms + SqOut backend +75.1 ms landing
    // the squeeze on the 143.129 ms gap: 74.1*0.935114 + 75.1 = 144.4 >=
    // 143.129, while the old scale predicts a miss (141.1). DragonDelgar's
    // published even split of +-74 = 143.129 / (1 + 0.935114) matches the
    // true end too.
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{{0, 98.0003},
                                  {34560, 97.4999},
                                  {45120, 104.8004},
                                  {46080, 101.0002}};
    SongTiming st(480, tpm, bpm);

    Activation act;
    act.timecode = st.timecode(34560);
    act.sp_meter = 2;
    BackendSqueeze d0;  // the 0.0-offset row at the deact node
    d0.timecode = st.timecode(46080);
    d0.offset_ms = 0.0;
    act.backends.push_back(d0);

    auto scales = frontend_transfer_scales(act, st);
    REQUIRE(scales.has_value());
    CHECK(scales->post.early ==
          doctest::Approx(98.0003 / 104.8004).epsilon(1e-9));  // 0.935114
    CHECK(scales->post.late ==
          doctest::Approx(97.4999 / 101.0002).epsilon(1e-9));  // 0.965344
    // No SqIn: the two ends coincide.
    CHECK(scales->pre.early == doctest::Approx(scales->post.early));
    CHECK(scales->pre.late == doctest::Approx(scales->post.late));

    // The SqOut gap and its displayed numbers.
    double gap = st.timecode(46080).ms() - st.timecode(45960).ms();
    CHECK(gap == doctest::Approx(143.129).epsilon(1e-4));
    const double r = scales->post.early;
    CHECK(effective_backend_ms(gap, r) ==
          doctest::Approx(2.0 * gap / (1.0 + r)).epsilon(1e-12));  // ~147.9
    CHECK(effective_backend_ms(gap, r) == doctest::Approx(147.94).epsilon(1e-3));
    CHECK(gap / (1.0 + r) == doctest::Approx(73.96).epsilon(1e-3));
    CHECK(74.1 * r + 75.1 > gap);         // the video's successful split
    CHECK(74.1 * 0.890911 + 75.1 < gap);  // the old scale called it a miss

    // The same D recovered through a nonzero-offset row (the ms -> tick
    // rounding path): the SqOut phrase note itself, 143.129 ms before D.
    Activation act2 = act;
    act2.backends.clear();
    BackendSqueeze dq;
    dq.timecode = st.timecode(45960);
    dq.offset_ms = st.timecode(45960).ms() - st.timecode(46080).ms();
    dq.is_sp = true;
    act2.backends.push_back(dq);
    auto scales2 = frontend_transfer_scales(act2, st);
    REQUIRE(scales2.has_value());
    CHECK(scales2->post.early == doctest::Approx(scales->post.early).epsilon(1e-12));
    CHECK(scales2->post.late == doctest::Approx(scales->post.late).epsilon(1e-12));

    // A SqIn builds pre DOWN from D: 46080 - 2 measures = 42240, inside the
    // 97.4999 section -> pre = {early 98.0003/97.4999, late 1.0}.
    Activation act3 = act;
    act3.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 5.0});
    auto scales3 = frontend_transfer_scales(act3, st);
    REQUIRE(scales3.has_value());
    CHECK(scales3->post.early == doctest::Approx(scales->post.early));
    CHECK(scales3->post.late == doctest::Approx(scales->post.late));
    CHECK(scales3->pre.early == doctest::Approx(98.0003 / 97.4999).epsilon(1e-9));
    CHECK(scales3->pre.late == doctest::Approx(1.0).epsilon(1e-12));
}

TEST_CASE("exact solver prices displacements across a tempo boundary") {
    // 4/4 throughout; 60 BPM until tick 960, then 120. The activation at tick
    // 1920 (ms 3000) holds 2 bars = 4 measures, ending at tick 9600 (ms
    // 11000). An early hit up to 1000 ms stays in the 120 section (shift ==
    // displacement); past that it crosses into 60 BPM, where a chart ms is
    // worth half a measure-fraction -- the exact solve diverges from the
    // boundary-sampled linearization (r == 1 here).
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{{0, 60.0}, {960, 120.0}};
    SongTiming st(480, tpm, bpm);

    Activation act;
    act.timecode = st.timecode(1920);
    act.sp_meter = 2;

    // Inside the section: exact == linear.
    CHECK(sp_end_shift_ms(500.0, SqueezeKind::SqOut, act, st) ==
          doctest::Approx(500.0).epsilon(1e-9));
    CHECK(required_frontend_ms(400.0, 100.0, SqueezeKind::SqOut, act, st) ==
          doctest::Approx(300.0).epsilon(1e-4));

    // Across the boundary: covering a 1250 ms gap takes 1500 ms of early
    // displacement (1000 at 1:1, then 500 at 1:0.5), not the 1250 the
    // linearized ratio predicts.
    CHECK(sp_end_shift_ms(1500.0, SqueezeKind::SqOut, act, st) ==
          doctest::Approx(1250.0).epsilon(1e-9));
    CHECK(required_frontend_ms(1250.0, 0.0, SqueezeKind::SqOut, act, st) ==
          doctest::Approx(1500.0).epsilon(1e-4));

    // Exact even split of a 2400 ms gap: x + shift(x) = 2400 with the second
    // arm kinked at 1000 -> x = 1266.67, not the linearized 1200.
    CHECK(exact_even_split_ms(2400.0, SqueezeKind::SqOut, act, st) ==
          doctest::Approx(2400.0 / 1.5 - 500.0 / 1.5 + 0.0)
              .epsilon(1e-4));  // (2400 - 500) / 1.5 = 1266.666...

    // A gap no displacement can cover reports +infinity.
    CHECK(std::isinf(
        required_frontend_ms(5000.0, 0.0, SqueezeKind::SqOut, act, st)));

    // Stale activations (no timecode / meter) shift nothing.
    Activation bare;
    CHECK(sp_end_shift_ms(100.0, SqueezeKind::SqOut, bare, st) == 0.0);
}

TEST_CASE("difficulty is the raw gap, untouched by stored transfer scales") {
    Activation act;
    act.skips = 0;
    act.e_offset = 300.0;  // not e-critical
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -12.0});
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 7.0});

    REQUIRE(act.difficulty().has_value());
    CHECK(*act.difficulty() == doctest::Approx(12.0));

    // The scales are display-only; the metric must not move with them.
    act.transfer_pre = TransferScale{0.5, 3.0};
    act.transfer_post = act.transfer_pre;
    CHECK(*act.difficulty() == doctest::Approx(12.0));
}
