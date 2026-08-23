#include "core/squeeze_rating.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hydra {

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

TransferRelevance transfer_scale_relevance(const Activation& act,
                                           const std::vector<BackendSqueeze>& backends) {
    TransferRelevance rel;
    for (const BackendSqueeze& bsq : backends) {
        if (!bsq.offset_ms) continue;
        if (act.is_sqout_backend(bsq)) rel.early = true;
        else if (*bsq.offset_ms > kDifficultMs) rel.late = true;
    }
    for (const SPSqueeze& sq : act.sqinouts) {
        if (sq.kind == SqueezeKind::SqOut) rel.early = true;
        else rel.late = true;  // a SqIn needs a late (+) frontend hit
    }
    return rel;
}

ActivationRating rate_activation(const Activation& act,
                                 const std::vector<BackendSqueeze>& backends,
                                 const SongTiming* timing,
                                 double hit_window_ms) {
    ActivationRating out;

    std::optional<ActTransferScales> scales;
    if (timing) scales = frontend_transfer_scales(act, *timing);
    // No timing at hand (no songmeta row): the record stores the scales the
    // search computed (1.0 on old blobs).
    if (!scales) scales = ActTransferScales{act.transfer_pre, act.transfer_post};
    out.scales = *scales;

    // Which directions matter, materiality-gated: backend rows read the
    // (possibly SqIn-extended) end, the SqIn/SqOut phrase notes the
    // pre-extension one — the same relevance rule as
    // transfer_scale_relevance, but a direction only warns when the scale is
    // material to that squeeze.
    for (const BackendSqueeze& bsq : backends) {
        if (!bsq.offset_ms) continue;
        if (act.is_sqout_backend(bsq))
            out.early_warns |= transfer_is_material(*bsq.offset_ms,
                                                    scales->post.early,
                                                    hit_window_ms);
        else if (*bsq.offset_ms > kDifficultMs)
            out.late_warns |= transfer_is_material(*bsq.offset_ms,
                                                   scales->post.late,
                                                   hit_window_ms);
    }
    for (const SPSqueeze& sq : act.sqinouts) {
        if (sq.kind == SqueezeKind::SqOut)
            out.early_warns |= transfer_is_material(sq.difficulty(),
                                                    scales->pre.early,
                                                    hit_window_ms);
        else
            out.late_warns |= transfer_is_material(sq.difficulty(),
                                                   scales->pre.late,
                                                   hit_window_ms);
    }

    // The scale that governs each row: sqout rows need an early frontend,
    // positive rows a late one. Both live at the (possibly SqIn-extended) SP
    // end, so they read `post`. effective_ms maps the row's raw ms onto the
    // nominal 2*W budget the ratings assume (the real combined budget is
    // W*(1+r)); it engages only when the scale is material to the row.
    out.backends.reserve(backends.size());
    for (const BackendSqueeze& bsq : backends) {
        BackendRating row;
        row.squeezed_out = act.is_sqout_backend(bsq);
        if (bsq.offset_ms) {
            bool applies = false;
            if (row.squeezed_out) {
                row.scale = scales->post.early;
                applies = transfer_is_material(*bsq.offset_ms, row.scale,
                                               hit_window_ms);
            } else if (*bsq.offset_ms > kDifficultMs) {
                row.scale = scales->post.late;
                applies = transfer_is_material(*bsq.offset_ms, row.scale,
                                               hit_window_ms);
            }
            if (applies)
                row.effective_ms = effective_backend_ms(*bsq.offset_ms, row.scale);
        }
        out.backends.push_back(row);
    }
    return out;
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
