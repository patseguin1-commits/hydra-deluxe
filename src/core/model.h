// Domain model — the C++ port of hydra/hydata.py.
//
// The note/chord/path/record types the parser fills and the search produces.
// The *string* forms here (Chord::code, Path::pathstring/pathstring_verbose,
// Activation::notationstr) are user-visible and pinned by the tests, so they must
// match Python byte-for-byte: comma-grouped scores, int() truncation on ms, the
// exact +/- squeeze symbols and [KRYBG] slot layout.
//
// The JSON save/load path from hydata.py is intentionally not ported — Phase 4
// replaces it with a binary format — but Chord::code / Chord::from_code (which
// that path used) survive because the parser and the chord tables need them.

#ifndef HYDRA_CORE_MODEL_H
#define HYDRA_CORE_MODEL_H

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/timing.h"

namespace hydra {

// Clone Hero's Star Power meter holds this many bars and no more. It is the
// default SP cap; records analyzed at a different cap answer what-if questions
// whose scores are not achievable in game.
inline constexpr int kCloneHeroSpCap = 4;

// A chart file that does not work, mirroring hymisc.ChartFileError. Chord and
// the parsers raise it; the parsers swallow it per-op exactly as Python does.
class ChartFileError : public std::runtime_error {
public:
    explicit ChartFileError(const std::string& what)
        : std::runtime_error(what) {}
};

// ---- enums --------------------------------------------------------------

enum class NoteColor { Kick = 1, Red = 2, Yellow = 3, Blue = 4, Green = 5 };
enum class NoteDynamicType { Normal = 1, Ghost = 2, Accent = 3 };
enum class NoteCymbalType { Normal = 1, Cymbal = 2 };

bool allows_cymbals(NoteColor c);
bool allows_dynamics(NoteColor c);
std::string color_str(NoteColor c);         // "Kick"/"Red"/...
std::string color_notationstr(NoteColor c); // "K"/"R"/"Y"/"B"/"G"
NoteCymbalType cymbal_flip(NoteCymbalType t);

// ---- squeeze thresholds -------------------------------------------------
// One home for the ms thresholds that define squeeze semantics. Each used to
// be a repeated literal; the value is the interface, so a change here is a
// deliberate rule change, not a stray edit.

// A squeeze (or calibration fill) tighter than this many ms counts as
// difficult: it turns on warning colors, and it is the "Normal" floor of the
// report's timing tiers.
constexpr double kDifficultMs = 2.0;

// Backend leeway edge: a backend note this close after the SP end still
// scores under SP without a deliberate squeeze. Shared by the engine's
// squeeze pricing and the "Standard" edge of BackendSqueeze::summarystr, so
// the price and the label cannot drift apart.
constexpr double kBackendLeewayMs = 3.0;

// The default per-side hit window (the registrable Clone Hero Pro Drums
// window). The *setting* app::Settings::hit_window_ms starts from this; the
// display-layer defaults below use it so all entry points agree.
constexpr double kDefaultHitWindowMs = 85.0;

// Backends within this window of the deactivation are worth showing/storing
// (was hymisc.BACKEND_DISPLAY_WINDOW_MS = 140). 2x the 85 ms hit window, so
// the trim covers the full nominal squeeze budget; raising the hit_window_ms
// *setting* above 85 does not widen this analysis-time trim.
constexpr double kBackendDisplayWindowMs = 170.0;

// Calibration-fill (E) timing window, applied to e_offset in both directions:
// an activation with e_offset < -window is illegal (the fill can't be
// summoned), and one with e_offset < +window is E-critical. 85 since 1.5.0
// (the +/-70 hit-window figure was outdated); search-load-bearing, so it is
// a constant, never the hit_window_ms setting.
constexpr double kCalibrationFillWindowMs = 85.0;

// ---- ChordNote ----------------------------------------------------------

struct ChordNote {
    NoteColor colortype;
    NoteDynamicType dynamictype = NoteDynamicType::Normal;
    NoteCymbalType cymbaltype = NoteCymbalType::Normal;
    bool is2x = false;

    // 1000*color + 100*dyn + 10*cym + is2x, matching ChordNote.__hash__.
    int64_t hash() const;
    bool operator==(const ChordNote& o) const;
    bool operator!=(const ChordNote& o) const { return !(*this == o); }
    std::string str() const;
    int basescore() const;
    bool is_dynamic() const { return dynamictype != NoteDynamicType::Normal; }
    bool is_accent() const { return dynamictype == NoteDynamicType::Accent; }
    bool is_ghost() const { return dynamictype == NoteDynamicType::Ghost; }
    bool is_cymbal() const { return cymbaltype == NoteCymbalType::Cymbal; }
};

// ---- Chord --------------------------------------------------------------

class Chord {
public:
    Chord() = default;

    // hash(tuple(h)) of the five KRYBG slots, reproducing CPython's tuple hash
    // exactly (absent slots contribute hash(-1) == -2). The chord encode table
    // is keyed on this value.
    int64_t hash() const;

    static Chord from_code(const std::string& code);
    std::string code() const;

    bool operator==(const Chord& o) const;
    bool operator!=(const Chord& o) const { return !(*this == o); }

    // notemap access (KRYBG order), mirroring __getitem__/__setitem__.
    std::optional<ChordNote>& at(NoteColor c);
    const std::optional<ChordNote>& at(NoteColor c) const;

    // Non-empty notes. basesorted uses a *stable* sort on basescore (Python's
    // list.sort is stable), which the scoring tie-break depends on.
    std::vector<ChordNote> notes(bool basesorted = false) const;

    int count() const;
    int hands_count() const;

    std::string rowstr() const;
    std::string notationstr() const;

    void apply_disco_flip();
    void apply_flam_conversion();

    // Adds a fresh normal note of this color; raises ChartFileError if the
    // color is already present. Returns a reference so the caller can set its
    // dynamics/cymbal/2x, as MidiParser.op_note does.
    ChordNote& add_note(NoteColor color);
    void insert_note(const ChordNote& note);
    void add_2x();
    void apply_cymbal(NoteColor color);
    void apply_ghost(NoteColor color);
    void apply_accent(NoteColor color);

    // Highest-priority note (Green→Blue→Yellow→Red→Kick); throws if empty.
    const ChordNote& activation_note() const;

private:
    // Index = color value - 1 (Kick..Green), preserving KRYBG iteration order.
    std::array<std::optional<ChordNote>, 5> notemap_{};
};

// ---- squeezes -----------------------------------------------------------

enum class SqueezeKind { SqIn, SqOut };

// A SqIn (+) or SqOut (-), mirroring hydata.SqIn/SqOut. Equality compares the
// offset only, exactly like SPSqueeze.__eq__.
struct SPSqueeze {
    SqueezeKind kind;
    double offset_ms = 0.0;

    double offset() const { return offset_ms; }
    double timing() const { return -offset_ms + 0.0; }
    double difficulty() const {
        return kind == SqueezeKind::SqIn ? offset_ms : (-offset_ms + 0.0);
    }
    const char* symbol() const {
        return kind == SqueezeKind::SqIn ? "+" : "-";
    }
    bool is_difficult() const { return difficulty() > kDifficultMs; }
    const char* type_name() const {
        return kind == SqueezeKind::SqIn ? "SqIn" : "SqOut";
    }
    std::string description() const;

    bool operator==(const SPSqueeze& o) const { return offset_ms == o.offset_ms; }
    bool operator!=(const SPSqueeze& o) const { return !(*this == o); }
};

struct FrontendSqueeze {
    Chord chord;
    int points = 0;
    bool operator==(const FrontendSqueeze& o) const {
        return chord == o.chord && points == o.points;
    }
};

struct BackendSqueeze {
    Timecode timecode;
    Chord chord;
    int points = 0;
    int sqout_points = 0;
    bool is_sp = false;
    std::optional<double> offset_ms;

    bool operator==(const BackendSqueeze& o) const;
    // Rating label. The outer +/-W edges come from the hit window; the inner
    // -10/3/10 edges are absolute (they encode leeway/near-deact semantics,
    // not the window).
    std::string summarystr(double hit_window_ms = kDefaultHitWindowMs) const;
};

// Multiplier squeeze. Construction validates the chord+combo and throws
// std::invalid_argument when it is not a squeezable situation (mirroring the
// ValueError that ScoreGraph.store_multsqueeze catches).
class MultSqueeze {
public:
    MultSqueeze(Chord chord, int combo);

    int multiplier() const;      // to_multiplier(combo) + 1
    std::string direction() const;
    int points() const;
    std::string notationstr() const;
    // "Hit X or Y last/first." guidance text, mirroring MultSqueeze.howto.
    std::string howto() const;

    const Chord& chord() const { return chord_; }
    int combo() const { return combo_; }

    bool operator==(const MultSqueeze& o) const {
        return chord_ == o.chord_ && combo_ == o.combo_;
    }

private:
    void validate() const;
    Chord chord_;
    int combo_;
};

// ---- Activation ---------------------------------------------------------

// How frontend (activation-hit) timing error transfers to the SP end. SP
// length is measure-based, so hitting the frontend d ms off moves the SP end
// by r*d ms, where r = ms-per-measure at the SP end / ms-per-measure at the
// frontend. The two directions differ when the activation or SP end sits
// exactly on a meter/tempo change: an early (-) hit moves into the section
// before the tick, a late (+) hit into the section at/after it.
struct TransferScale {
    double early = 1.0;  // r- : early (-) frontend hits (SqOut direction)
    double late = 1.0;   // r+ : late (+) frontend hits (SqIns, backend squeezes)
};

struct Activation {
    std::optional<int> skips;
    std::optional<Timecode> timecode;
    std::optional<Chord> chord;
    std::optional<int> sp_meter;
    std::optional<int> frontend_points;
    std::vector<BackendSqueeze> backends;
    std::vector<SPSqueeze> sqinouts;
    std::optional<double> e_offset;

    // Frontend transfer scales, computed by the search and stored with the
    // record (blob v3; older blobs default to 1.0 = the flat-tempo identity)
    // so the details display keeps its ratios when no SongTiming is at hand.
    // Display-only: difficulty() and everything the search/filter/report
    // derive stay raw gap ms. Both scales anchor on the search's actual
    // deactivation node D — which sits +2 measures past the plain
    // 2*B-measure end for every SP phrase collected mid-activation. `post`
    // is measured at D and governs the backend rows; `pre` steps one
    // 2-measure SqIn extension down from D and governs the SqIn/SqOut lines,
    // equalling `post` when the activation has no SqIn.
    TransferScale transfer_pre;
    TransferScale transfer_post;

    std::string notationstr() const;
    std::string notationstr_verbose() const;
    bool is_e_critical() const;  // e_offset < kCalibrationFillWindowMs
    bool is_E0() const;
    std::optional<double> e_difficulty(bool verbose = false) const;
    std::optional<double> difficulty() const;
    bool is_difficult() const;

    // Is this backend the note being squeezed out of SP? Mirrors
    // hydata.Activation.is_sqout_backend.
    bool is_sqout_backend(const BackendSqueeze& bsq) const;

    // Backends worth keeping: those near the deactivation, plus whatever note
    // is being squeezed out of SP however far out it lands. Mirrors
    // hydata.Activation.display_backends; used both by the details view and to
    // trim a record's backends before storing it.
    std::vector<BackendSqueeze> display_backends() const;
};

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
// front measure duration is non-positive. Shared by the display layer and
// the search graph so the two can't drift.
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

// The display-layer judgement of these scales — which directions matter, when
// a scale is material, effective ms, the exact SP-end solver — lives in
// core/squeeze_rating.h.

// ---- Path ---------------------------------------------------------------

struct Path {
    std::vector<MultSqueeze> multsqueezes;
    std::vector<Activation> activations;   // hydata's _activations
    int notecount = 0;
    int leftover_sp = 0;
    int skipped_ghosts = 0;
    int skipped_accents = 0;

    int64_t score_base = 0;
    int64_t score_combo = 0;
    int64_t score_sp = 0;
    int64_t score_solo = 0;
    int64_t score_accents = 0;
    int64_t score_ghosts = 0;

    std::vector<Path> variants;
    int tied_count = 1;
    std::optional<int> var_point;
    std::vector<Activation> variant_tail;

    // _activations then _variant_tail, as all_activations() yields.
    std::vector<Activation> all_activations() const;
    bool has_activations() const;

    int64_t totalscore() const;
    std::string pathstring() const;
    std::string pathstring_verbose() const;

    int tied_pathcount() const { return tied_count; }
    int recount_tied_paths();
    void prepare_variants();

    std::optional<double> difficulty() const;

    // An "all-0" path: it has activations and every one of them records
    // skips == 0. False for a path with no activations, and for a stale record
    // whose activations carry no skip count.
    bool is_allzero() const;

    // Points-per-note-scored average, matching hydata.Path.avg_mult. 0.0 for a
    // path with no scoring notes (avoids the ZeroDivisionError guard).
    double avg_mult() const;
};

// ---- HydraRecord --------------------------------------------------------

struct HydraRecord {
    std::optional<double> ms_limit;
    std::optional<int> sp_cap;
    bool sp_cap_converged = true;
    std::vector<Path> paths;

    // The best all-0 path: the highest-scoring path whose activations all
    // record skips == 0, found under a 0 ms timing limit, plus the tied
    // variations a calibration fill or a squeeze in/out produces. The main
    // search keeps paths by score band, not by shape, so this path is usually
    // below the band and absent from `paths`. Empty when the search did not run
    // or found nothing. Deliberately NOT part of all_paths(): the reports and
    // the summary columns must keep counting generated paths only.
    std::vector<Path> allzero_paths;

    const Path& best_path() const { return paths.at(0); }
    Path& best_path() { return paths.at(0); }

    // Depth-first traversal of the path tree (root paths + their nested
    // variants), mirroring hydata.HydraRecord.all_paths(). Pointers into
    // `paths`; valid until the record is modified or moved.
    std::vector<const Path*> all_paths() const;

    // The same traversal over allzero_paths.
    std::vector<const Path*> all_allzero_paths() const;
};

// Format an integer with thousands separators, matching Python's `{:,}`.
std::string group_thousands(int64_t n);

}  // namespace hydra

#endif  // HYDRA_CORE_MODEL_H
