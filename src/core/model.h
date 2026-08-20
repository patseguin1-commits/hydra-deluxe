// Domain model — the C++ port of hydra/hydata.py.
//
// The note/chord/path/record types the parser fills and the search produces.
// The *string* forms here (Chord::code, Path::pathstring,
// Activation::notationstr) are user-visible and pinned by the tests: comma-
// grouped scores, int() truncation on ms, the exact +/- squeeze symbols and
// [KRYBG] slot layout. Python byte-parity is deliberately broken for the
// *verbose* forms since 1.5.0: notationstr_verbose/pathstring_verbose print
// per-hit (transfer-scaled) squeeze ms, which hydata.py never computed. Do
// not "fix" them back to raw offsets.
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
    bool is_difficult() const { return difficulty() > 2.0; }
    const char* type_name() const {
        return kind == SqueezeKind::SqIn ? "SqIn" : "SqOut";
    }
    // The squeeze as the joint two-hit constraint it really is:
    // r*frontend + note > gap, with the even split, the combined budget at
    // the given hit window, the minimum song speed when the gap exceeds the
    // budget, and (at speed_pct != 100) the per-hit requirement in real ms.
    // `transfer_r` is the frontend transfer scale in this squeeze's direction
    // (early for SqOut, late for SqIn), from the *pre-extension* SP end.
    std::string description(double transfer_r, double hit_window_ms,
                            int speed_pct) const;
    // Fallback when no timing/scales are available: r = 1, W = 85, 100%.
    std::string description() const { return description(1.0, 85.0, 100); }

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
    std::string summarystr(double hit_window_ms = 85.0) const;
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
    // so difficulty stays computable without a SongTiming in hand. `pre` is
    // measured at the plain 2*B-measure SP end and governs SqIn/SqOut
    // feasibility; `post` at the SqIn-extended (+2 measures) end governs the
    // backend rows, and equals `pre` when the activation has no SqIn.
    TransferScale transfer_pre;
    TransferScale transfer_post;

    std::string notationstr() const;
    std::string notationstr_verbose() const;
    bool is_e_critical() const;  // e_offset < kCalibrationFillWindowMs
    bool is_E0() const;
    std::optional<double> e_difficulty(bool verbose = false) const;

    // One squeeze's difficulty as ms of timing error per hit: the squeeze is
    // the joint constraint r*frontend + note > gap, so the even split
    // gap/(1+r) is the smallest per-hit displacement that satisfies it. r is
    // read from transfer_pre in the squeeze's direction. A W-free quantity.
    double squeeze_difficulty(const SPSqueeze& sq) const;
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
// `pre` is measured at the plain 2*B-measure end and governs the SqIn/SqOut
// feasibility (the phrase note must land inside SP as it stands *before* the
// phrase is collected); `post` is measured at the +2-measure-extended end a
// SqIn produces and governs the backend rows, which live at the extended end.
// Without a SqIn the two are identical.
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

// The activation's transfer scales, from its timecode, SP meter, and (for the
// SqIn +2-measure extension) its sqinouts. Display-only; nullopt when the
// activation has no timecode or sp_meter (stale record).
std::optional<ActTransferScales> frontend_transfer_scales(const Activation& act,
                                                          const SongTiming& timing);

// Which directions of the transfer scale actually matter for this activation:
// `late` when some positive backend squeeze or a SqIn wants a late (+)
// frontend hit, `early` when a note is squeezed out of SP (a sqout backend,
// or any SqOut in sqinouts) and so wants an early (-) one. `backends` is the
// caller's act.display_backends(), passed in so it isn't rebuilt. Display-only.
struct TransferRelevance {
    bool late = false;
    bool early = false;
};
TransferRelevance transfer_scale_relevance(const Activation& act,
                                           const std::vector<BackendSqueeze>& backends);

// A backend squeeze's raw ms mapped onto the nominal 2*W scale the ratings
// assume. With frontend timing scaling by r at the SP end, the real combined
// squeeze budget is squeeze_budget_ms(r, W) = W*(1+r) rather than 2*W, so a
// raw |offset| counts for |offset| * 2 / (1+r) of the nominal budget (a
// W-free quantity).
double effective_backend_ms(double offset_ms, double transfer_r);
double squeeze_budget_ms(double transfer_r, double hit_window_ms = 85.0);

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
