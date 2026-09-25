// Unit tests for core/squeeze_rating: the deactivation node, the frontend
// transfer scales computed from it, and the display-layer judgement built on
// top — materiality, per-row effective ms, and the exact SP-end solver.

#include "doctest.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/squeeze_rating.h"
#include "core/rules.h"

using namespace hydra;

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
    // The node the search would have stamped: no SqIn yet, so the plain
    // act + 2*B measures (this is what the old activation_deact_tick fallback
    // computed for a no-backend-rows, no-SqIn activation).
    act.deact_tick = st.plusmeasure(*act.timecode, 16).ticks();

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
    // With a SqIn present, the old fallback's node was act + 2*B + 2 measures;
    // re-stamp deact_tick to match (frontend_transfer_scales now reads it
    // straight off the record and steps `pre` back down itself).
    act.deact_tick = st.plusmeasure(*act.timecode, 18).ticks();
    auto sqin_scales = frontend_transfer_scales(act, st);
    REQUIRE(sqin_scales.has_value());
    CHECK(sqin_scales->pre.late == doctest::Approx(scales->pre.late));
    CHECK(sqin_scales->post.late == doctest::Approx(scales->pre.late));

    // A SqOut does not move either end.
    act.sqinouts.clear();
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -5.0});
    // No SqIn any more (a SqOut doesn't count): back to act + 2*B measures.
    act.deact_tick = st.plusmeasure(*act.timecode, 16).ticks();
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

    // Has both timecode and sp_meter, but no deact_tick: a pre-v4 record
    // cannot say where SP ended, and nothing guesses any more.
    Activation partial;
    partial.timecode = st.timecode(0);
    partial.sp_meter = 2;
    CHECK(!frontend_transfer_scales(partial, st).has_value());
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
    // The node the old fallback derived (act + 2*B + 2 measures, the SqIn
    // present): 6 measures -> tick 11520. A 0-offset backend row placed at
    // the same tick below recovers the identical node, so this one value
    // covers both calls in this test.
    act.deact_tick = st.plusmeasure(*act.timecode, 6).ticks();

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
    // No SqIn, no backend rows: the old fallback's node, act + 2*B measures.
    act.deact_tick = st.plusmeasure(*act.timecode, 6).ticks();

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
    // No SqIn, no backend rows: the old fallback's node, act + 2*B measures.
    act2.deact_tick = st2.plusmeasure(*act2.timecode, 4).ticks();

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
    // No SqIn, no backend rows: the old fallback's node, act + 2*B measures.
    act3.deact_tick = flat.plusmeasure(*act3.timecode, 4).ticks();
    auto flat_scale = frontend_transfer_scales(act3, flat);
    REQUIRE(flat_scale.has_value());
    CHECK(flat_scale->pre.early == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(flat_scale->pre.late == doctest::Approx(1.0).epsilon(1e-12));
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
    // No SqIn, no backend rows yet: the old fallback's node, act + 2*B
    // measures -- this activation collects no phrase mid-SP, so it is also
    // exactly the node a 0.0-offset backend row would name (see below).
    act.deact_tick = st.plusmeasure(*act.timecode, 6).ticks();
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

TEST_CASE("activation_deact_tick: the stored node, read back") {
    // activation_deact_tick derives nothing any more: it just hands back
    // act.deact_tick. No SongTiming is needed to test that.

    // Set: returns exactly what was stored.
    Activation act;
    act.deact_tick = 46080;
    CHECK(activation_deact_tick(act) == 46080);

    // A timecode and sp_meter at hand but no deact_tick: nullopt. This is the
    // point of the change -- there is no measure-count fallback any more.
    Activation stale;
    stale.timecode = Timecode::raw(3840);
    stale.sp_meter = 2;
    CHECK(!activation_deact_tick(stale).has_value());

    // A bare, default-constructed activation: nullopt.
    Activation bare;
    CHECK(!activation_deact_tick(bare).has_value());
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
    // The node the search actually stamped: the mid-SP collection pushes it
    // to act + 6 measures (tick 46080), matching the 0.0-offset row above --
    // this is the whole point of the fixture (act + 2*B alone would be
    // 42240).
    act.deact_tick = 46080;

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

TEST_CASE("rate_activation: SqIns warn late, SqOuts early, with no backend rows") {
    // A SqIn-only activation must flag the late direction even with no
    // backend rows at all (the old code only set it via backends): the SqIn
    // itself needs a late (+) frontend hit. Stored scales of 0.5 make a
    // 50 ms phrase gap read 66.7 ms, well past the 1 ms materiality floor.
    Activation act;
    act.transfer_pre = TransferScale{0.5, 0.5};
    act.transfer_post = act.transfer_pre;
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 50.0});

    ActivationRating rate = rate_activation(act, nullptr, 85.0);
    CHECK(rate.late_warns);
    CHECK_FALSE(rate.early_warns);
    CHECK(rate.late_note_warns);
    CHECK_FALSE(rate.late_backend_warns);
    CHECK(rate.backends.empty());

    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -50.0});
    rate = rate_activation(act, nullptr, 85.0);
    CHECK(rate.late_warns);
    CHECK(rate.early_warns);
}

TEST_CASE("transfer_is_material: impact or budget, not |r - 1|") {
    // r == 1: the effective ms equals the raw gap, so only a gap past the
    // combined budget (2*W) makes the identity scale material.
    CHECK_FALSE(transfer_is_material(50.0, 1.0, 85.0));
    CHECK(transfer_is_material(200.0, 1.0, 85.0));

    // r == 0.5: a 30 ms gap reads effectively 40 ms — a 10 ms impact.
    CHECK(effective_backend_ms(30.0, 0.5) == doctest::Approx(40.0));
    CHECK(transfer_is_material(30.0, 0.5, 85.0));

    // A near-1 ratio on a tiny gap moves it under the 1 ms impact floor.
    CHECK_FALSE(transfer_is_material(2.0, 0.99, 85.0));
}

TEST_CASE("rate_activation: stored scales, materiality-gated warns and rows") {
    // No timing at hand: the stored (blob v3) scales govern. The flat 1.0
    // defaults never warn for in-budget gaps.
    Activation flat;
    flat.skips = 0;
    flat.e_offset = 300.0;
    BackendSqueeze late_row;
    late_row.offset_ms = 50.0;  // inside the display window, past the leeway floor
    flat.backends.push_back(late_row);

    ActivationRating r = rate_activation(flat, nullptr, 85.0);
    CHECK_FALSE(r.late_warns);
    CHECK_FALSE(r.early_warns);
    REQUIRE(r.backends.size() == 1);
    CHECK_FALSE(r.backends[0].squeezed_out);
    CHECK(r.backends[0].scale == doctest::Approx(1.0));
    CHECK_FALSE(r.backends[0].effective_ms.has_value());

    // A stored post.late of 0.5 makes the same +50 ms row material:
    // effectively 66.7 ms on the nominal scale.
    Activation scaled = flat;
    scaled.transfer_post.late = 0.5;
    r = rate_activation(scaled, nullptr, 85.0);
    CHECK(r.late_warns);
    CHECK(r.late_backend_warns);
    CHECK_FALSE(r.late_note_warns);
    CHECK_FALSE(r.early_warns);
    REQUIRE(r.backends.size() == 1);
    CHECK(r.backends[0].scale == doctest::Approx(0.5));
    REQUIRE(r.backends[0].effective_ms.has_value());
    CHECK(*r.backends[0].effective_ms ==
          doctest::Approx(effective_backend_ms(50.0, 0.5)));

    // A row inside the backend leeway never engages the late scale.
    Activation leeway = scaled;
    leeway.backends.clear();
    BackendSqueeze leeway_row;
    leeway_row.offset_ms = 1.5;
    leeway.backends.push_back(leeway_row);
    r = rate_activation(leeway, nullptr, 85.0);
    CHECK_FALSE(r.late_warns);
    CHECK_FALSE(r.backends[0].effective_ms.has_value());

    // An over-budget gap at the flat 1.0 scale still warns (it exceeds the
    // 170 ms combined budget) but reads at face value: an eff. figure would
    // just repeat the raw ms, so it stays unset.
    Activation overbudget = flat;
    overbudget.backends.clear();
    BackendSqueeze over_row;
    over_row.offset_ms = 222.2;
    overbudget.backends.push_back(over_row);
    r = rate_activation(overbudget, nullptr, 85.0);
    CHECK(r.late_backend_warns);
    REQUIRE(r.backends.size() == 1);
    CHECK(r.backends[0].scale == doctest::Approx(1.0));
    CHECK_FALSE(r.backends[0].effective_ms.has_value());

    // A SqOut phrase note reads the pre-end early scale.
    Activation sqout = flat;
    sqout.backends.clear();
    sqout.transfer_pre.early = 0.5;
    sqout.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -50.0});
    r = rate_activation(sqout, nullptr, 85.0);
    CHECK(r.early_warns);
    CHECK(r.early_note_warns);
    CHECK_FALSE(r.early_backend_warns);
    CHECK_FALSE(r.late_warns);
}

TEST_CASE("rate_activation: free squeezes read the opposite scale direction") {
    // The governing direction follows the sign of the difficulty, not the
    // squeeze kind. A squeeze you already have (difficulty <= 0) is not
    // achieved by a frontend hit — it is destroyed by one, in the opposite
    // direction, so that is the scale that decides whether it survives.

    // A free sqout: the note sits +300 ms past the SP end already, so a LATE
    // frontend hit is what could drag the end over it. With late 4.45 the
    // 300 ms of margin is worth only 110 ms of the nominal budget.
    Activation freeout;
    freeout.skips = 0;
    freeout.transfer_post = TransferScale{1.0, 4.45};
    freeout.transfer_pre = freeout.transfer_post;
    freeout.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 300.0});
    BackendSqueeze out_row;
    out_row.timecode = Timecode::raw(3187);
    out_row.offset_ms = 300.0;
    freeout.backends.push_back(out_row);
    freeout.sqout_tick = 3187;  // this row is the squeezed-out chord

    ActivationRating r = rate_activation(freeout, nullptr, 85.0);
    REQUIRE(r.backends.size() == 1);
    CHECK(r.backends[0].squeezed_out);
    CHECK(r.backends[0].scale == doctest::Approx(4.45));
    REQUIRE(r.backends[0].effective_ms.has_value());
    CHECK(*r.backends[0].effective_ms ==
          doctest::Approx(effective_backend_ms(300.0, 4.45)));
    CHECK(r.late_backend_warns);
    CHECK_FALSE(r.early_backend_warns);
    // The phrase note flips with the row: same squeeze, same direction.
    CHECK(r.late_note_warns);
    CHECK_FALSE(r.early_note_warns);

    // A difficult sqout keeps the achievement direction: the note is inside
    // SP, so only an early frontend hit pulls the end back before it.
    Activation hardout;
    hardout.skips = 0;
    hardout.transfer_post = TransferScale{0.5, 1.0};
    hardout.transfer_pre = hardout.transfer_post;
    hardout.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -50.0});
    BackendSqueeze hard_row;
    hard_row.timecode = Timecode::raw(3053);
    hard_row.offset_ms = -50.0;
    hardout.backends.push_back(hard_row);
    hardout.sqout_tick = 3053;

    r = rate_activation(hardout, nullptr, 85.0);
    REQUIRE(r.backends.size() == 1);
    CHECK(r.backends[0].squeezed_out);
    CHECK(r.backends[0].scale == doctest::Approx(0.5));
    REQUIRE(r.backends[0].effective_ms.has_value());
    CHECK(*r.backends[0].effective_ms ==
          doctest::Approx(effective_backend_ms(50.0, 0.5)));
    CHECK(r.early_backend_warns);
    CHECK_FALSE(r.late_backend_warns);
    CHECK(r.early_note_warns);

    // A free SqIn (the note sits comfortably inside SP) is threatened by an
    // early frontend hit, so it warns on the early scale.
    Activation freein;
    freein.skips = 0;
    freein.transfer_pre = TransferScale{0.5, 1.0};
    freein.transfer_post = freein.transfer_pre;
    freein.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, -50.0});

    r = rate_activation(freein, nullptr, 85.0);
    CHECK(r.early_note_warns);
    CHECK_FALSE(r.late_note_warns);
    CHECK(r.early_warns);
    CHECK_FALSE(r.late_warns);
}

TEST_CASE("rate_activation: a live timing overrides the stored scales") {
    // Flat 4/4 at one tempo: the live recompute yields identity scales even
    // though the record stored 0.5s — the stored values are only a fallback.
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    SongTiming st(480, tpm, bpm);

    Activation act;
    act.timecode = st.timecode(0);
    act.sp_meter = 2;
    act.transfer_pre = TransferScale{0.5, 0.5};
    act.transfer_post = TransferScale{0.5, 0.5};
    // No SqIn, no offset-bearing row yet at this point: the old fallback's
    // node, act + 2*B measures (tick 7680) -- the row below sits 50 ms past
    // it, recovering the same node the other way.
    act.deact_tick = st.plusmeasure(*act.timecode, 4).ticks();

    BackendSqueeze row;
    row.timecode = st.timecode(7728);  // 50 ms past the 4-measure SP end
    row.offset_ms = 50.0;
    act.backends.push_back(row);

    ActivationRating r = rate_activation(act, &st, 85.0);
    CHECK(r.scales.post.late == doctest::Approx(1.0));
    CHECK_FALSE(r.late_warns);
    CHECK_FALSE(r.backends[0].effective_ms.has_value());

    // A stale activation (no timecode) falls back to the stored scales even
    // when a timing is at hand.
    Activation stale;
    stale.transfer_post.late = 0.5;
    stale.backends.push_back(row);
    r = rate_activation(stale, &st, 85.0);
    CHECK(r.scales.post.late == doctest::Approx(0.5));
    CHECK(r.late_warns);
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
    // No SqIn, no backend rows: the old fallback's node, act + 2*B measures
    // (tick 9600, matching the "ending at tick 9600" comment above). None of
    // the exact-solver functions below actually read this field (they price
    // the plain 2*B-measure end directly off timecode/sp_meter), but it is
    // set for consistency with the rest of this activation's shape.
    act.deact_tick = st.plusmeasure(*act.timecode, 4).ticks();

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

TEST_CASE("timing_tiers: the ladder at W=85 and at W=70") {
    // The default 85 ms window -> a 170 ms two-hit budget: the 2 ms "Normal"
    // floor, then quarters of the budget.
    std::vector<TimingTier> t85 = timing_tiers();
    REQUIRE(t85.size() == 7);

    CHECK(std::string(t85[0].name) == "Normal");
    CHECK(std::string(t85[0].tok) == "t0");
    REQUIRE(t85[0].cutoff.has_value());
    CHECK(*t85[0].cutoff == doctest::Approx(2.0));

    CHECK(std::string(t85[1].name) == "Hard");
    CHECK(std::string(t85[1].tok) == "t1");
    REQUIRE(t85[1].cutoff.has_value());
    CHECK(*t85[1].cutoff == doctest::Approx(42.5));

    CHECK(std::string(t85[2].name) == "Extreme");
    CHECK(std::string(t85[2].tok) == "t2");
    REQUIRE(t85[2].cutoff.has_value());
    CHECK(*t85[2].cutoff == doctest::Approx(85.0));

    CHECK(std::string(t85[3].name) == "Insane");
    CHECK(std::string(t85[3].tok) == "t3");
    REQUIRE(t85[3].cutoff.has_value());
    CHECK(*t85[3].cutoff == doctest::Approx(127.5));

    CHECK(std::string(t85[4].name) == "Insane+");
    CHECK(std::string(t85[4].tok) == "t4");
    REQUIRE(t85[4].cutoff.has_value());
    CHECK(*t85[4].cutoff == doctest::Approx(170.0));

    // The two open bands carry no cutoff, and "None" is last.
    CHECK(std::string(t85[5].name) == "Beyond");
    CHECK(std::string(t85[5].tok) == "t5");
    CHECK_FALSE(t85[5].cutoff.has_value());
    CHECK(std::string(t85[6].name) == "None");
    CHECK(std::string(t85[6].tok) == "tn");
    CHECK_FALSE(t85[6].cutoff.has_value());

    // The historical 70 ms window reproduces the original 2/35/70/105/140
    // ladder exactly; only the cutoffs move.
    std::vector<TimingTier> t70 = timing_tiers(70.0);
    REQUIRE(t70.size() == 7);
    const double want70[5] = {2.0, 35.0, 70.0, 105.0, 140.0};
    for (size_t i = 0; i < 5; ++i) {
        CHECK(std::string(t70[i].name) == std::string(t85[i].name));
        CHECK(std::string(t70[i].tok) == std::string(t85[i].tok));
        REQUIRE(t70[i].cutoff.has_value());
        CHECK(*t70[i].cutoff == doctest::Approx(want70[i]));
    }
    CHECK_FALSE(t70[5].cutoff.has_value());
    CHECK_FALSE(t70[6].cutoff.has_value());
}

TEST_CASE("display_backends: 500 ms window keeps everything the 500 ms search graph collects") {
    // kBackendDisplayWindowMs moved from 170 to 500 to match the search
    // graph's own collection window, so nothing it gathers gets trimmed at
    // store/display time. 180 and -300 are the regression: both used to fall
    // outside the old +-170 window and get dropped.
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    SongTiming st(480, tpm, bpm);

    Activation act;
    act.timecode = st.timecode(0);
    act.sp_meter = 2;

    const double offsets[] = {0.0, 180.0, -300.0, 499.0, 520.0};
    for (double off : offsets) {
        BackendSqueeze row;
        row.timecode = st.timecode(0);
        row.offset_ms = off;
        act.backends.push_back(row);
    }

    std::vector<BackendSqueeze> kept = act.display_backends();
    REQUIRE(kept.size() == 4);
    CHECK(kept[0].offset_ms == doctest::Approx(0.0));
    CHECK(kept[1].offset_ms == doctest::Approx(180.0));
    CHECK(kept[2].offset_ms == doctest::Approx(-300.0));
    CHECK(kept[3].offset_ms == doctest::Approx(499.0));
}

TEST_CASE("rate_activation: cap_clamped flag") {
    // cap_clamped is only meaningful when both halves are true: the window
    // was cap-clamped (clamp_tick is set) AND the activation actually lists a
    // squeeze the frontend decides (a SqIn/SqOut, or a backend row that's
    // squeezed out or at or past the backend leeway). Either half missing means
    // there's no frontend-decided squeeze for the overfill warning to attach
    // to, so the flag stays false.

    // clamp_tick set, plus a SqOut -- both halves true, so this is the case
    // the overfill warning is for.
    Activation clamped_with_squeeze;
    clamped_with_squeeze.clamp_tick = 3072;
    clamped_with_squeeze.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -50.0});
    ActivationRating r1 = rate_activation(clamped_with_squeeze, nullptr, 85.0);
    CHECK(r1.cap_clamped);

    // clamp_tick set, but the only backend row is at -30 ms -- inside the
    // SP window, so the engine counts it and it isn't a squeeze the frontend
    // decides. No SqIn/SqOut either, so cap_clamped stays false.
    Activation clamped_no_squeeze;
    clamped_no_squeeze.clamp_tick = 3072;
    BackendSqueeze mild_row;
    mild_row.offset_ms = -30.0;
    clamped_no_squeeze.backends.push_back(mild_row);
    ActivationRating r2 = rate_activation(clamped_no_squeeze, nullptr, 85.0);
    CHECK_FALSE(r2.cap_clamped);

    // A SqOut with no clamp_tick at all -- the window was never cap-clamped,
    // so there's nothing to warn about regardless of the squeeze.
    Activation unclamped_with_squeeze;
    unclamped_with_squeeze.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -50.0});
    ActivationRating r3 = rate_activation(unclamped_with_squeeze, nullptr, 85.0);
    CHECK_FALSE(r3.cap_clamped);
}

// The engine counts a plain row less than the leeway past the SP end, so it
// is not a squeeze the frontend decides. The rating, the late-row warn and
// the overfill flag all use that same edge (user decision 7).
TEST_CASE("rate_activation: a plain row inside the leeway is not a frontend squeeze") {
    Activation act;
    act.skips = 0;
    act.e_offset = 300.0;  // not e-critical
    act.clamp_tick = 3072;
    // A late scale this small would make a row treated as late material:
    // 2.5 ms reads as 4.2 ms, past the 1 ms impact floor.
    act.transfer_post.late = 0.2;
    BackendSqueeze row;
    row.timecode = Timecode::raw(3080);
    row.offset_ms = 2.5;
    act.backends.push_back(row);

    ActivationRating r = rate_activation(act, nullptr, 85.0);
    CHECK_FALSE(r.cap_clamped);
    CHECK_FALSE(r.late_backend_warns);
    REQUIRE(r.backends.size() == 1);
    CHECK_FALSE(r.backends[0].effective_ms.has_value());
    CHECK(act.backends[0].summarystr(85.0) == "Standard");

    // At the leeway edge the row is uncounted, so the frontend decides it.
    act.backends[0].offset_ms = 3.0;
    r = rate_activation(act, nullptr, 85.0);
    CHECK(r.cap_clamped);
    CHECK(act.backends[0].summarystr(85.0) == "Hard (uncounted)");

    // The edge is the user's rule: a 2 ms leeway makes 2.5 ms uncounted.
    const double narrow = 2.0;
    REQUIRE(narrow != core::default_rules().backend_leeway_ms);
    act.backends[0].offset_ms = 2.5;
    r = rate_activation(act, nullptr, 85.0, narrow);
    CHECK(r.cap_clamped);
    CHECK(act.backends[0].summarystr(85.0, narrow) == "Hard (uncounted)");
}

TEST_CASE("squeeze_budget_ms: identity scale is twice the hit window") {
    // The backend tooltip's "not %.0fms" figure is this call, not 2.0 * W.
    CHECK(squeeze_budget_ms(1.0, 85.0) == 170.0);
    CHECK(squeeze_budget_ms(1.0, 40.0) == 80.0);
}
