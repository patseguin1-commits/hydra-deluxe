// Squeeze rating — how an activation's squeezes are judged for display.
//
// One home for the display-layer squeeze math: which transfer-scale
// directions matter, when a scale is material enough to warn about, what a
// backend row's effective ms is on the nominal two-hit scale, and the exact
// (non-linearized) SP-end solver. The search never reads any of this;
// difficulty and the ms filter stay raw gap ms (see core/model.h).
//
// rate_activation() is the interface the details display renders from; the
// free functions below it are the pieces, exposed because the reports and the
// tests use them individually.

#ifndef HYDRA_CORE_SQUEEZE_RATING_H
#define HYDRA_CORE_SQUEEZE_RATING_H

#include <optional>
#include <vector>

#include "core/model.h"
#include "core/timing.h"

namespace hydra {

// A backend squeeze's raw ms mapped onto the nominal 2*W scale the ratings
// assume. With frontend timing scaling by r at the SP end, the real combined
// squeeze budget is squeeze_budget_ms(r, W) = W*(1+r) rather than 2*W, so a
// raw |offset| counts for |offset| * 2 / (1+r) of the nominal budget (a
// W-free quantity).
double effective_backend_ms(double offset_ms, double transfer_r);
double squeeze_budget_ms(double transfer_r, double hit_window_ms = kDefaultHitWindowMs);

// The transfer scale is shown when it is material to a listed squeeze: the
// gap's effective size moves by more than this many ms, or the gap exceeds
// the combined budget outright. Gating on impact (not on |r - 1|) keeps a
// near-1 ratio visible when a large gap makes even a fraction of a percent
// decide success.
constexpr double kTransferImpactMs = 1.0;
bool transfer_is_material(double gap_ms, double transfer_r,
                          double hit_window_ms = kDefaultHitWindowMs);

// Which directions of the transfer scale actually matter for this activation:
// `late` when some positive backend squeeze or a SqIn wants a late (+)
// frontend hit, `early` when a note is squeezed out of SP (a sqout backend,
// or any SqOut in sqinouts) and so wants an early (-) one. `backends` is the
// caller's act.display_backends(), passed in so it isn't rebuilt. Ungated:
// rate_activation() applies the materiality gate on top of this rule.
struct TransferRelevance {
    bool late = false;
    bool early = false;
};
TransferRelevance transfer_scale_relevance(const Activation& act,
                                           const std::vector<BackendSqueeze>& backends);

// ---- rate_activation ------------------------------------------------------

// One backend table row, resolved: is it the squeezed-out note, which scale
// governs it, and its effective ms when the scale materially changes it
// (unset when the row reads at face value).
struct BackendRating {
    bool squeezed_out = false;
    double scale = 1.0;
    std::optional<double> effective_ms;
};

// The full transfer-scale story for one activation, as the details display
// tells it.
struct ActivationRating {
    // Resolved scales: recomputed live from `timing` when one is at hand,
    // else the record's stored (blob v3) values — 1.0 flat-tempo identities
    // on older blobs.
    ActTransferScales scales;
    // The materially affected directions: drive the scale-warning line.
    bool late_warns = false;
    bool early_warns = false;
    // Parallel to the `backends` argument.
    std::vector<BackendRating> backends;
};

// `backends` is the caller's act.display_backends(), passed in so it isn't
// rebuilt. `timing` may be null (no songmeta row): the stored scales are used
// then. Backend rows are judged at the post (deact-node) end, SqIn/SqOut
// phrase notes at the pre (pre-extension) end.
ActivationRating rate_activation(const Activation& act,
                                 const std::vector<BackendSqueeze>& backends,
                                 const SongTiming* timing,
                                 double hit_window_ms = kDefaultHitWindowMs);

// ---- exact squeeze solver -------------------------------------------------
// The transfer scale linearizes the SP-end map E(h) at one point; these
// evaluate it exactly through SongTiming::sp_end_ms, so a displacement that
// crosses a tempo/meter section boundary is priced correctly. All three
// return quantities in chart ms and are display-only. They read the
// activation's timecode ms, so call them only on restored records.

// How far the SP end moves when the frontend is displaced `displaced_ms` (a
// positive magnitude) in the squeeze's direction: early for SqOut, late for
// SqIn. Judged at the pre-extension (2*B measures) end, like the feasibility
// itself. Returns 0 when the activation lacks a timecode or SP meter.
double sp_end_shift_ms(double displaced_ms, SqueezeKind kind,
                       const Activation& act, const SongTiming& timing);

// The smallest frontend displacement whose exact SP-end shift, plus the
// note's own displacement `backend_ms`, covers `gap_ms`. Monotone, solved by
// bisection to ~1e-3 ms over [0, 8000]; +infinity when even 8000 ms cannot
// cover it (fall back to the linearized figure).
double required_frontend_ms(double gap_ms, double backend_ms, SqueezeKind kind,
                            const Activation& act, const SongTiming& timing);

// The exact even split: the smallest x with x + sp_end_shift_ms(x) >= gap_ms
// -- the solved counterpart of the linearized gap/(1+r). Same bisection and
// +infinity convention as required_frontend_ms.
double exact_even_split_ms(double gap_ms, SqueezeKind kind,
                           const Activation& act, const SongTiming& timing);

}  // namespace hydra

#endif  // HYDRA_CORE_SQUEEZE_RATING_H
