#include "core/squeeze_rating.h"

#include "core/backend_value.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hydra {

std::optional<TransferScale> transfer_scale_between(int64_t act_tick,
                                                    int64_t end_tick,
                                                    const SongTiming& timing) {
    TransferScale scale;
    double front_late = timing.ms_per_measure_at(act_tick);
    double front_early = timing.ms_per_measure_at(act_tick - 1);
    if (front_late <= 0.0 || front_early <= 0.0) return std::nullopt;
    scale.late = timing.ms_per_measure_at(end_tick) / front_late;
    scale.early = timing.ms_per_measure_at(end_tick - 1) / front_early;
    return scale;
}

std::optional<int64_t> activation_deact_tick(const Activation& act) {
    return act.deact_tick;
}

std::optional<ActTransferScales> frontend_transfer_scales(const Activation& act,
                                                          const SongTiming& timing) {
    if (!act.timecode || !act.sp_meter || !act.deact_tick) return std::nullopt;

    int64_t act_tick = act.timecode->ticks();
    bool has_sqin = false;
    for (const SPSqueeze& sq : act.sqinouts) {
        if (sq.kind == SqueezeKind::SqIn) {
            has_sqin = true;
            break;
        }
    }

    // The SP end the search recorded, straight off the record.
    int64_t post_tick = *act.deact_tick;
    // The SqIn phrase is judged against the end as it stood before that
    // phrase extended SP: one 2-measure step down from D. With several
    // SqIns, or a plain collection after the last one, this is exact only
    // for the last extension -- one `pre` per activation is all the data
    // model (and the blob) carries.
    int64_t pre_tick =
        has_sqin ? timing.plusmeasure(timing.timecode(post_tick), -2).ticks()
                 : post_tick;

    std::optional<TransferScale> post =
        transfer_scale_between(act_tick, post_tick, timing);
    if (!post) return std::nullopt;

    ActTransferScales scales{*post, *post};
    if (pre_tick != post_tick) {
        if (std::optional<TransferScale> pre =
                transfer_scale_between(act_tick, pre_tick, timing))
            scales.pre = *pre;
    }
    return scales;
}

double effective_backend_ms(double offset_ms, double transfer_r) {
    return std::abs(offset_ms) * 2.0 / (1.0 + transfer_r);
}

double squeeze_budget_ms(double transfer_r, double hit_window_ms) {
    return hit_window_ms * (1.0 + transfer_r);
}

bool transfer_is_material(double gap_ms, double transfer_r,
                          double hit_window_ms) {
    double gap = std::abs(gap_ms);
    return std::abs(effective_backend_ms(gap, transfer_r) - gap) >
               kTransferImpactMs ||
           gap > squeeze_budget_ms(transfer_r, hit_window_ms);
}

ActivationRating rate_activation(const Activation& act,
                                 const SongTiming* timing,
                                 double hit_window_ms,
                                 double backend_leeway_ms) {
    ActivationRating out;

    std::optional<ActTransferScales> scales;
    if (timing) scales = frontend_transfer_scales(act, *timing);
    // No timing at hand (no songmeta row): the record stores the scales the
    // search computed (1.0 on old blobs).
    if (!scales) scales = ActTransferScales{act.transfer_pre, act.transfer_post};
    out.scales = *scales;

    // The scale that governs each row follows the sign of its offset, not the
    // kind of squeeze. A sqout row still inside SP (offset < 0) has to be
    // achieved by an early frontend hit, so it reads the early scale; a sqout
    // row already past the SP end (offset > 0) is free, and the only thing
    // that can destroy it is a late frontend hit dragging the end over it, so
    // it reads the late scale. Plain positive rows want a late frontend hit.
    // All of them live at the (possibly SqIn-extended) SP end, so they read
    // `post`. effective_ms maps the row's raw ms onto the nominal 2*W budget
    // the ratings assume (the real combined budget is W*(1+r)); it engages
    // only when the scale actually moves the number (an over-budget row still
    // warns at x1.00 but reads at face value).
    std::vector<BackendSqueeze> backends = act.display_backends();
    out.backends.reserve(backends.size());
    for (const BackendSqueeze& bsq : backends) {
        BackendRating row;
        row.row = bsq;
        row.squeezed_out = act.is_sqout_backend(bsq);
        if (bsq.offset_ms) {
            bool applies = false;
            if (row.squeezed_out && *bsq.offset_ms > 0.0) {
                row.scale = scales->post.late;
                applies = transfer_is_material(*bsq.offset_ms, row.scale,
                                               hit_window_ms);
                out.late_backend_warns |= applies;
            } else if (row.squeezed_out) {
                row.scale = scales->post.early;
                applies = transfer_is_material(*bsq.offset_ms, row.scale,
                                               hit_window_ms);
                out.early_backend_warns |= applies;
            } else if (!core::counted_without_squeeze(*bsq.offset_ms,
                                                      backend_leeway_ms)) {
                row.scale = scales->post.late;
                applies = transfer_is_material(*bsq.offset_ms, row.scale,
                                               hit_window_ms);
                out.late_backend_warns |= applies;
            }
            if (applies) {
                double eff = effective_backend_ms(*bsq.offset_ms, row.scale);
                if (std::abs(eff - std::abs(*bsq.offset_ms)) > kTransferImpactMs)
                    row.effective_ms = eff;
            }
        }
        row.budget_ms = squeeze_budget_ms(row.scale, hit_window_ms);
        out.backends.push_back(std::move(row));
    }

    // The SqIn/SqOut phrase notes are judged at the pre-extension end, in the
    // direction that decides them. A squeeze you still have to earn
    // (difficulty > 0) is decided by the hit that achieves it: early (-) for a
    // SqOut, late (+) for a SqIn. A free one (difficulty <= 0) is already
    // yours, so the direction that matters is the opposite one -- the frontend
    // error that would move the SP end far enough to take it away. They have
    // no display row of their own, so they only feed the warning line.
    for (const SPSqueeze& sq : act.sqinouts) {
        bool achieved_early = (sq.kind == SqueezeKind::SqOut);
        // At difficulty 0 the gap is 0 and nothing can be material, so the
        // achievement direction stands.
        bool early = sq.difficulty() >= 0.0 ? achieved_early : !achieved_early;
        if (early)
            out.early_note_warns |= transfer_is_material(
                sq.difficulty(), scales->pre.early, hit_window_ms);
        else
            out.late_note_warns |= transfer_is_material(
                sq.difficulty(), scales->pre.late, hit_window_ms);
    }

    out.late_warns = out.late_backend_warns || out.late_note_warns;
    out.early_warns = out.early_backend_warns || out.early_note_warns;

    // The cap-clamped flag fires when the activation has a clamp_tick AND at
    // least one squeeze the frontend decides: any SqIn/SqOut, or any backend
    // row that was squeezed out or that the engine does not count (at or past
    // the leeway).
    if (act.clamp_tick.has_value()) {
        bool has_frontend_squeeze = !act.sqinouts.empty();
        if (!has_frontend_squeeze) {
            for (const BackendRating& br : out.backends) {
                if (br.squeezed_out ||
                    (br.row.offset_ms &&
                     !core::counted_without_squeeze(*br.row.offset_ms,
                                                    backend_leeway_ms))) {
                    has_frontend_squeeze = true;
                    break;
                }
            }
        }
        out.cap_clamped = has_frontend_squeeze;
    }

    return out;
}

std::vector<TimingTier> timing_tiers(double hit_window_ms) {
    const double w = hit_window_ms;
    return {
        {"Normal", "t0", kDifficultMs}, {"Hard", "t1", w / 2},
        {"Extreme", "t2", w},           {"Insane", "t3", 3 * w / 2},
        {"Insane+", "t4", 2 * w},       {"Beyond", "t5", std::nullopt},
        {"None", "tn", std::nullopt},
    };
}

namespace {

// Bisection ceiling: displacements past this are far outside anything a
// player can execute, so a target unreachable within it reports +infinity.
constexpr double kSolverMaxMs = 8000.0;
constexpr double kSolverToleranceMs = 1e-3;

// Solves f(d) >= target for the smallest d in [0, kSolverMaxMs], where f is
// monotone non-decreasing with f(0) == 0.
template <typename F>
double bisect_min(F f, double target) {
    if (target <= 0.0) return 0.0;
    double lo = 0.0, hi = kSolverMaxMs;
    if (f(hi) < target) return std::numeric_limits<double>::infinity();
    while (hi - lo > kSolverToleranceMs) {
        double mid = (lo + hi) / 2.0;
        if (f(mid) < target)
            lo = mid;
        else
            hi = mid;
    }
    return hi;
}

}  // namespace

// Prices the plain 2*B-measure SP end only: it does not model the +2-measure
// extension per SP phrase collected mid-activation the way
// frontend_transfer_scales does (no production caller needs that yet).
double sp_end_shift_ms(double displaced_ms, SqueezeKind kind,
                       const Activation& act, const SongTiming& timing) {
    if (!act.timecode || !act.sp_meter) return 0.0;
    const double h = act.timecode->ms();
    const int64_t end_measures = 2 * static_cast<int64_t>(*act.sp_meter);
    const double base = timing.sp_end_ms(h, end_measures);
    if (kind == SqueezeKind::SqOut)
        return base - timing.sp_end_ms(h - displaced_ms, end_measures);
    return timing.sp_end_ms(h + displaced_ms, end_measures) - base;
}

double required_frontend_ms(double gap_ms, double backend_ms, SqueezeKind kind,
                            const Activation& act, const SongTiming& timing) {
    return bisect_min(
        [&](double d) { return sp_end_shift_ms(d, kind, act, timing); },
        gap_ms - backend_ms);
}

double exact_even_split_ms(double gap_ms, SqueezeKind kind,
                           const Activation& act, const SongTiming& timing) {
    return bisect_min(
        [&](double d) { return d + sp_end_shift_ms(d, kind, act, timing); },
        gap_ms);
}

}  // namespace hydra
