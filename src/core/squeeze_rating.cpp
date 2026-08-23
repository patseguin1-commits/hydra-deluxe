#include "core/squeeze_rating.h"

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

namespace {

// The true SP end is the deactivation node D, which sits one +2-measure
// step past the plain 2*B-measure end for every SP phrase collected
// during the activation. Ordinary mid-SP collections leave no trace on
// the Activation itself, but every backend row encodes D exactly: its
// offset_ms was measured against D (graph.cpp add_deact_edge), so
// D = row.ms - offset. Prefer the smallest-|offset| row; the frequent
// 0.0-offset row is the deact node itself. nullopt when no row carries an
// offset (the activation never deactivates, or an old trimmed record).
std::optional<int64_t> deact_tick_from_rows(const Activation& act,
                                            const SongTiming& timing) {
    const BackendSqueeze* d_row = nullptr;
    for (const BackendSqueeze& bsq : act.backends) {
        if (!bsq.offset_ms) continue;
        if (!d_row || std::abs(*bsq.offset_ms) < std::abs(*d_row->offset_ms))
            d_row = &bsq;
    }
    if (!d_row) return std::nullopt;
    if (*d_row->offset_ms == 0.0) return d_row->timecode.ticks();
    // tick_at_ms is display-layer math (never in the scoring path); its fp
    // error is far below half a tick, so llround recovers the deact node's
    // integer tick exactly.
    return static_cast<int64_t>(std::llround(
        timing.ms_index().tick_at_ms(d_row->timecode.ms() - *d_row->offset_ms)));
}

}  // namespace

std::optional<int64_t> activation_deact_tick(const Activation& act,
                                             const SongTiming& timing) {
    if (!act.timecode || !act.sp_meter) return std::nullopt;
    if (std::optional<int64_t> d = deact_tick_from_rows(act, timing)) return d;
    // Fallback: the plain reconstruction, 2 measures per SP bar, plus the one
    // +2-measure extension a SqIn records (same rule as frontend_transfer_scales).
    bool has_sqin = false;
    for (const SPSqueeze& sq : act.sqinouts)
        if (sq.kind == SqueezeKind::SqIn) has_sqin = true;
    int64_t end_measures = 2 * static_cast<int64_t>(*act.sp_meter) + (has_sqin ? 2 : 0);
    return timing.plusmeasure(*act.timecode, end_measures).ticks();
}

std::optional<ActTransferScales> frontend_transfer_scales(const Activation& act,
                                                          const SongTiming& timing) {
    if (!act.timecode || !act.sp_meter) return std::nullopt;

    int64_t act_tick = act.timecode->ticks();
    bool has_sqin = false;
    for (const SPSqueeze& sq : act.sqinouts) {
        if (sq.kind == SqueezeKind::SqIn) {
            has_sqin = true;
            break;
        }
    }

    std::optional<int64_t> d_tick = deact_tick_from_rows(act, timing);

    int64_t pre_tick, post_tick;
    if (d_tick) {
        post_tick = *d_tick;
        // The SqIn phrase is judged against the end as it stood before that
        // phrase extended SP: one 2-measure step down from D. With several
        // SqIns, or a plain collection after the last one, this is exact only
        // for the last extension -- one `pre` per activation is all the data
        // model (and blob v3) carries.
        pre_tick = has_sqin
                       ? timing.plusmeasure(timing.timecode(post_tick), -2).ticks()
                       : post_tick;
    } else {
        // No backend row carries an offset (the activation never deactivates,
        // or an old trimmed record): fall back to the plain reconstruction,
        // which cannot see mid-SP collections. 2 measures per SP bar.
        int64_t end_measures = 2 * static_cast<int64_t>(*act.sp_meter);
        pre_tick = timing.plusmeasure(*act.timecode, end_measures).ticks();
        post_tick = has_sqin
                        ? timing.plusmeasure(*act.timecode, end_measures + 2).ticks()
                        : pre_tick;
    }

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
                                 double hit_window_ms) {
    ActivationRating out;

    std::optional<ActTransferScales> scales;
    if (timing) scales = frontend_transfer_scales(act, *timing);
    // No timing at hand (no songmeta row): the record stores the scales the
    // search computed (1.0 on old blobs).
    if (!scales) scales = ActTransferScales{act.transfer_pre, act.transfer_post};
    out.scales = *scales;

    // The scale that governs each row: sqout rows need an early frontend,
    // positive rows a late one. Both live at the (possibly SqIn-extended) SP
    // end, so they read `post`. effective_ms maps the row's raw ms onto the
    // nominal 2*W budget the ratings assume (the real combined budget is
    // W*(1+r)); it engages only when the scale is material to the row, and a
    // row whose scale is material is exactly what makes its direction warn.
    std::vector<BackendSqueeze> backends = act.display_backends();
    out.backends.reserve(backends.size());
    for (const BackendSqueeze& bsq : backends) {
        BackendRating row;
        row.row = bsq;
        row.squeezed_out = act.is_sqout_backend(bsq);
        if (bsq.offset_ms) {
            bool applies = false;
            if (row.squeezed_out) {
                row.scale = scales->post.early;
                applies = transfer_is_material(*bsq.offset_ms, row.scale,
                                               hit_window_ms);
                out.early_warns |= applies;
            } else if (*bsq.offset_ms > kDifficultMs) {
                row.scale = scales->post.late;
                applies = transfer_is_material(*bsq.offset_ms, row.scale,
                                               hit_window_ms);
                out.late_warns |= applies;
            }
            if (applies)
                row.effective_ms = effective_backend_ms(*bsq.offset_ms, row.scale);
        }
        row.budget_ms = squeeze_budget_ms(row.scale, hit_window_ms);
        out.backends.push_back(std::move(row));
    }

    // The SqIn/SqOut phrase notes are judged at the pre-extension end: a
    // SqOut wants an early (-) frontend hit, a SqIn a late (+) one. They have
    // no display row of their own, so they only feed the warning line.
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
