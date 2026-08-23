// Squeeze rating — where an activation's Star Power ends, how frontend
// timing error carries to that end, and how the resulting squeezes are
// judged for display.
//
// One home for all of it: the deactivation node, the transfer scales, which
// scale directions matter, when a scale is material enough to warn about,
// what a backend row's effective ms is on the nominal two-hit scale, and the
// exact (non-linearized) SP-end solver. The search never reads the judgement
// side; difficulty and the ms filter stay raw gap ms (see core/model.h).
//
// Three functions are the module's entries:
//   rate_activation()          -- the details display's only interface.
//   activation_deact_tick()    -- the Preview's, for the active SP window.
//   frontend_transfer_scales() -- the engine's copy-out stamp, so a record's
//                                 stored ratios and a live recompute agree.
// Everything else below is an internal piece, declared only so its own tests
// can reach it directly. No production caller should use them.

#ifndef HYDRA_CORE_SQUEEZE_RATING_H
#define HYDRA_CORE_SQUEEZE_RATING_H

#include <optional>
#include <vector>

#include "core/model.h"
#include "core/timing.h"

namespace hydra {

// ---- transfer scales ------------------------------------------------------

// Two SP ends coexist in one activation, so two transfer scales do too:
// `post` is measured at the deactivation node D (the end the backend rows'
// offsets are measured against, mid-SP phrase extensions included); `pre`
// is measured one 2-measure step before D and governs the SqIn feasibility
// (the phrase note must land inside SP as it stands *before* the phrase is
// collected). Without a SqIn the two are identical. With several SqIns, or
// a plain collection after the last one, `pre` is exact only for the last
// extension — one pair per activation is all this carries.
struct ActTransferScales {
    TransferScale pre;
    TransferScale post;
};

// The transfer scale between two ticks: mspm(end)/mspm(act), probed at the
// tick (late direction) and tick-1 (early direction). nullopt when either
// front measure duration is non-positive.
std::optional<TransferScale> transfer_scale_between(int64_t act_tick,
                                                    int64_t end_tick,
                                                    const SongTiming& timing);

// The activation's deactivation node D (where its Star Power runs out), in
// ticks: recovered from the backend rows when one carries an offset (their
// offsets are measured against D exactly, mid-SP phrase collections
// included), else the plain act + 2*B measures (+2 with a SqIn). The same
// derivation frontend_transfer_scales uses, shared so the Preview's active
// SP window and the squeeze display can't disagree. Display-only; nullopt
// when the activation has no timecode or sp_meter.
std::optional<int64_t> activation_deact_tick(const Activation& act,
                                             const SongTiming& timing);

// The activation's transfer scales. The SP end is recovered from the backend
// rows (their offsets encode the deactivation node exactly), so mid-SP phrase
// collections are priced in; an activation with no offset-bearing backend row
// falls back to the plain act + 2*B-measure reconstruction. The engine stamps
// the stored transfer_pre/post through this same function at copy-out, so a
// live recompute can't drift from the record. Display-only; nullopt when the
// activation has no timecode or sp_meter (stale record).
std::optional<ActTransferScales> frontend_transfer_scales(const Activation& act,
                                                          const SongTiming& timing);

// ---- rating pieces --------------------------------------------------------

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

// ---- rate_activation ------------------------------------------------------

// One backend table row, resolved: the display row it judges, whether that
// row is the squeezed-out note, which scale governs it, the combined squeeze
// budget that scale buys, and the row's effective ms when the scale
// materially changes it (unset when the row reads at face value).
struct BackendRating {
    BackendSqueeze row;
    bool squeezed_out = false;
    double scale = 1.0;
    double budget_ms = 0.0;
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
    // One entry per act.display_backends() row, in that order.
    std::vector<BackendRating> backends;
};

// The display's whole view of an activation's squeezes: it builds the backend
// rows itself (act.display_backends()), so the caller renders and nothing
// more. `timing` may be null (no songmeta row): the stored scales are used
// then. Backend rows are judged at the post (deact-node) end, SqIn/SqOut
// phrase notes at the pre (pre-extension) end.
ActivationRating rate_activation(const Activation& act,
                                 const SongTiming* timing,
                                 double hit_window_ms = kDefaultHitWindowMs);

// ---- timing tiers ---------------------------------------------------------

// The report's timing tiers: raw squeeze ms banded against the two-hit
// budget 2*W, quarters of the budget after the kDifficultMs "Normal" floor
// (at the historical W = 70 this is the 2/35/70/105/140 ladder). In payload
// order; `cutoff` is the band's exclusive upper edge, unset for the open
// "Beyond" band and the "None" (no squeeze) entry.
struct TimingTier { const char* name; const char* tok; std::optional<double> cutoff; };
std::vector<TimingTier> timing_tiers(double hit_window_ms = kDefaultHitWindowMs);

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
