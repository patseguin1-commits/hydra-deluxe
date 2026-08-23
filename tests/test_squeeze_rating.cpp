// Unit tests for core/squeeze_rating: the display-layer judgement of an
// activation's squeezes — relevance, materiality, per-row effective ms, and
// the exact SP-end solver. The transfer-scale *computation* itself
// (frontend_transfer_scales) is pinned in test_model.cpp.

#include "doctest.h"

#include <cmath>
#include <cstdint>
#include <map>

#include "core/squeeze_rating.h"

using namespace hydra;

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
    late_row.offset_ms = 50.0;
    std::vector<BackendSqueeze> backends{late_row};

    ActivationRating r = rate_activation(flat, backends, nullptr, 85.0);
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
    r = rate_activation(scaled, backends, nullptr, 85.0);
    CHECK(r.late_warns);
    CHECK_FALSE(r.early_warns);
    REQUIRE(r.backends.size() == 1);
    CHECK(r.backends[0].scale == doctest::Approx(0.5));
    REQUIRE(r.backends[0].effective_ms.has_value());
    CHECK(*r.backends[0].effective_ms ==
          doctest::Approx(effective_backend_ms(50.0, 0.5)));

    // A row at or under the difficult floor never engages the late scale.
    BackendSqueeze leeway_row;
    leeway_row.offset_ms = 1.5;
    r = rate_activation(scaled, {leeway_row}, nullptr, 85.0);
    CHECK_FALSE(r.late_warns);
    CHECK_FALSE(r.backends[0].effective_ms.has_value());

    // A SqOut phrase note reads the pre-end early scale.
    Activation sqout = flat;
    sqout.transfer_pre.early = 0.5;
    sqout.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -50.0});
    r = rate_activation(sqout, {}, nullptr, 85.0);
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

    BackendSqueeze row;
    row.offset_ms = 50.0;
    ActivationRating r = rate_activation(act, {row}, &st, 85.0);
    CHECK(r.scales.post.late == doctest::Approx(1.0));
    CHECK_FALSE(r.late_warns);
    CHECK_FALSE(r.backends[0].effective_ms.has_value());

    // A stale activation (no timecode) falls back to the stored scales even
    // when a timing is at hand.
    Activation stale;
    stale.transfer_post.late = 0.5;
    r = rate_activation(stale, {row}, &st, 85.0);
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
