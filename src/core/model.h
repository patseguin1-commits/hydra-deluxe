// Domain model: charts, chords, squeezes, activations and paths.
//
// The note/chord/path/record types the parser fills and the search produces.
// The *string* forms here (Path::pathstring/pathstring_verbose,
// Activation::notationstr) are user-visible and pinned by the tests, so they must
// match Python byte-for-byte: comma-grouped scores, int() truncation on ms, the
// exact +/- squeeze symbols and [KRYBG] slot layout.
//
// Records are stored in the binary format of store/serialize.h. Stored paths
// carry each chord as its Chord::code, read back by Chord::from_code.

#ifndef HYDRA_CORE_MODEL_H
#define HYDRA_CORE_MODEL_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/rules.h"
#include "core/timing.h"

namespace hydra {

// Clone Hero's Star Power meter holds this many bars and no more. It is the
// default SP cap; records analyzed at a different cap answer what-if questions
// whose scores are not achievable in game.
inline constexpr int kCloneHeroSpCap = 4;

// A chart file that does not work. Chord and
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
std::string color_str(NoteColor c);         // "Kick"/"Red"/...
std::string dynamic_str(NoteDynamicType t); // "none"/"ghost"/"accent"
std::string color_notationstr(NoteColor c); // "K"/"R"/"Y"/"B"/"G"

// ---- squeeze thresholds -------------------------------------------------
// One home for the ms thresholds that define squeeze semantics. Each used to
// be a repeated literal; the value is the interface, so a change here is a
// deliberate rule change, not a stray edit.

// A squeeze (or calibration fill) tighter than this many ms counts as
// difficult: it turns on warning colors, and it is the "Normal" floor of the
// report's timing tiers.
constexpr double kDifficultMs = 2.0;

// The backend leeway edge is a user rule now: core::Rules::backend_leeway_ms
// (hydra_rules.ini), read by the engine and BackendSqueeze::summarystr.

// The default per-side hit window (the registrable Clone Hero Pro Drums
// window). The *setting* app::Settings::hit_window_ms starts from this; the
// display-layer defaults below use it so all entry points agree.
constexpr double kDefaultHitWindowMs = 85.0;

// The squeeze horizon in ms. The search graph only looks this far from the SP
// end for a reachable squeeze, and backends within it of the deactivation are
// stored and shown, so nothing the engine collects is trimmed. The Backend
// limit setting narrows the display from here. hydra_batch prints it.
constexpr double kSqueezeWindowMs = 500.0;

// Calibration-fill (E) timing window, applied to e_offset in both directions:
// an activation with e_offset < -window is illegal (the fill can't be
// summoned), and one with e_offset < +window is E-critical. 60 since 2026-09
// (was 85 from 1.5.0, +/-70 before that): across 18,773 analyzed charts the
// hardest E0 on any best path was 57.7 ms, so nothing past 60 earns its
// place. Search-load-bearing, so it is a constant, never the hit_window_ms
// setting.
constexpr double kCalibrationFillWindowMs = 60.0;

// ---- note value ----------------------------------------------------------
// What one note is worth before any multiplier. ChordNote::basescore and
// category_scores both read these, so the price has one home.
inline constexpr int kNoteBasePoints = 50;
inline constexpr int kCymbalBonusPoints = 15;
// Solo bonus: this many points per note hit inside a solo section.
inline constexpr int kSoloBonusPerNote = 100;

// ---- ChordNote ----------------------------------------------------------

struct ChordNote {
    NoteColor colortype;
    NoteDynamicType dynamictype = NoteDynamicType::Normal;
    NoteCymbalType cymbaltype = NoteCymbalType::Normal;
    bool is2x = false;

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

    // The chord spelled one character per lane, in KRYBG order: "." for an
    // empty lane, otherwise n/g/a for a normal/ghost/accent note, upper case
    // for a cymbal or a 2x kick. Red + yellow cymbal + green cymbal is
    // ".nN.N". Every chord a chart can express has one; nothing is looked up.
    std::string code() const;
    // The reverse of code(). Throws std::out_of_range on a malformed code.
    static Chord from_code(const std::string& code);

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

// How hard a squeeze is, in ms: a SqIn's offset, or a SqOut's offset negated.
// "-x + 0.0" turns -0.0 into +0.0 so a dead-on SqOut prints "0.0". The engine's
// act_difficulty and SPSqueeze::difficulty both call this.
inline double squeeze_difficulty(bool is_sqin, double offset_ms) {
    return is_sqin ? offset_ms : (-offset_ms + 0.0);
}

// E0: the calibration fill lands inside its window and nothing was skipped.
inline bool is_e0(double e_offset, int skips) {
    return e_offset < kCalibrationFillWindowMs && skips == 0;
}

// How hard an E0 activation's calibration fill is, in ms.
inline double calibration_fill_difficulty(double e_offset) { return -e_offset + 0.0; }

// A SqIn (+) or SqOut (-): which way the note is squeezed across the SP end,
// and by how many ms.
struct SPSqueeze {
    SqueezeKind kind;
    double offset_ms = 0.0;

    double offset() const { return offset_ms; }
    double timing() const { return -offset_ms + 0.0; }
    double difficulty() const { return squeeze_difficulty(kind == SqueezeKind::SqIn, offset_ms); }
    const char* symbol() const {
        return kind == SqueezeKind::SqIn ? "+" : "-";
    }
    bool is_difficult() const { return difficulty() > kDifficultMs; }
    const char* type_name() const {
        return kind == SqueezeKind::SqIn ? "SqIn" : "SqOut";
    }
    std::string description() const;
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
    // leeway_ms: the backend leeway edge (Rules::backend_leeway_ms); a
    // non-SP row under it rates "Standard".
    std::string summarystr(double hit_window_ms = kDefaultHitWindowMs,
                           double leeway_ms = core::default_rules().backend_leeway_ms) const;
};

// Multiplier squeeze. Construction validates the chord+combo and throws
// std::invalid_argument when it is not a squeezable situation (mirroring the
// ValueError that ScoreGraph.store_multsqueeze catches).
class MultSqueeze {
public:
    MultSqueeze(Chord chord, int combo);
    // Whether a chord hit at this combo is a multiplier squeeze: exactly the
    // cases the constructor accepts, answered without throwing. The graph
    // asks this of every chord, and "no" is the usual answer.
    static bool applies(const Chord& chord, int combo);

    int multiplier() const;     // to_multiplier(combo) + 1
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
    double early = 1.0;  // r- : early (-) hits — difficult SqOuts, free SqIns
    double late = 1.0;   // r+ : late (+) hits — difficult SqIns, free SqOuts,
                         //      backend squeezes
};

struct Activation {
    // The search sets these six on every activation it makes, so they are
    // plain values (record format v7, docs/adr/0017).
    int skips = 0;
    Timecode timecode;
    Chord chord;
    int sp_meter = 0;
    int frontend_points = 0;
    std::vector<BackendSqueeze> backends;
    std::vector<SPSqueeze> sqinouts;
    double e_offset = 0.0;

    // The deactivation node D: the chart tick where this activation's Star
    // Power ends, extensions from phrases collected mid-SP included. Stamped
    // by the search at copy-out (blob v4). Unset only on a record written
    // before v4; nothing in the codebase re-derives it.
    std::optional<int64_t> deact_tick;

    // The collecting note the SP cap pinned this window's end to: when the
    // meter was full and a phrase extended the window, the end sat a fixed
    // distance from that phrase's note, not from the activation. Stamped by
    // the search (blob v5). Unset when the window never hit the cap or on
    // an older record. Nothing re-derives it.
    std::optional<int64_t> clamp_tick;

    // The chart tick of the SP phrase chord this activation squeezed out:
    // the deact edge's sqinout_time when the path took the SqOut branch.
    // Stamped by the search at copy-out (blob v6). Unset when the activation
    // did not squeeze out, or on an older record. Nothing re-derives it.
    std::optional<int64_t> sqout_tick;

    // The ticks of the SP phrase-end chords this activation collected while
    // active, in chart order: every phrase the gauge received, a late-SqIn
    // phrase and a cap-clamped phrase included. A squeezed-out phrase is not
    // among them. The search records each one as its path crosses the phrase
    // (blob v6). Empty when none was collected, or on an older record.
    // Nothing re-derives it.
    std::vector<int64_t> collected_phrase_ticks;

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

    // Is this backend the note squeezed out of SP? Compares against the
    // sqout_tick the engine stored (record format v6), so no display re-derives it.
    bool is_sqout_backend(const BackendSqueeze& bsq) const;

    // Backends worth keeping: those near the deactivation, plus whatever note
    // is being squeezed out of SP however far out it lands. The details view
    // shows exactly these, and the path codec stores only these, so a stored
    // record shows the same rows as a fresh one.
    std::vector<BackendSqueeze> display_backends() const;
};

// deact_tick and the scales above are stored data only. Everything that
// derives or judges them — transfer_scale_between, frontend_transfer_scales,
// and the display-layer rating built on them — lives in
// core/squeeze_rating.h. activation_deact_tick lives there too, but it
// derives nothing: it just hands back the stored deact_tick.

// A read-only walk over a path's activations: its own, then the variant tail
// it shares with its parent -- the order all_activations() copies them in.
// It holds pointers into the Path and copies nothing, so it is valid only
// while that Path is alive and unchanged.
class ActivationWalk {
public:
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Activation;
        using difference_type = std::ptrdiff_t;
        using pointer = const Activation*;
        using reference = const Activation&;

        iterator(const std::vector<Activation>* own, const std::vector<Activation>* tail,
                 size_t i)
            : own_(own), tail_(tail), i_(i) {}
        reference operator*() const {
            return i_ < own_->size() ? (*own_)[i_] : (*tail_)[i_ - own_->size()];
        }
        pointer operator->() const { return &**this; }
        iterator& operator++() {
            ++i_;
            return *this;
        }
        iterator operator++(int) {
            iterator old = *this;
            ++i_;
            return old;
        }
        bool operator==(const iterator& o) const { return i_ == o.i_; }
        bool operator!=(const iterator& o) const { return i_ != o.i_; }

    private:
        const std::vector<Activation>* own_;
        const std::vector<Activation>* tail_;
        size_t i_;
    };

    ActivationWalk(const std::vector<Activation>& own, const std::vector<Activation>& tail)
        : own_(&own), tail_(&tail) {}

    size_t size() const { return own_->size() + tail_->size(); }
    bool empty() const { return size() == 0; }
    const Activation& operator[](size_t i) const {
        return i < own_->size() ? (*own_)[i] : (*tail_)[i - own_->size()];
    }
    const Activation& front() const { return (*this)[0]; }
    const Activation& back() const { return (*this)[size() - 1]; }
    iterator begin() const { return iterator(own_, tail_, 0); }
    iterator end() const { return iterator(own_, tail_, size()); }

private:
    const std::vector<Activation>* own_;
    const std::vector<Activation>* tail_;
};

// ---- Path ---------------------------------------------------------------

struct Path {
    std::vector<Activation> activations;
    int notecount = 0;
    int leftover_sp = 0;

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
    // The same activations, read in place. Prefer this unless the caller
    // really needs its own copy.
    ActivationWalk walk_activations() const { return ActivationWalk(activations, variant_tail); }
    bool has_activations() const;

    int64_t totalscore() const;
    std::string pathstring() const;
    // The Ctrl+C string: the chart's multiplier squeezes (from the record,
    // HydraRecord::multsqueezes), the verbose activations, the score.
    std::string pathstring_verbose(const std::vector<MultSqueeze>& multsqueezes) const;

    int tied_pathcount() const { return tied_count; }
    int recount_tied_paths();
    void prepare_variants();

    std::optional<double> difficulty() const;
    // True when the hardest squeeze or E0 fill is past kDifficultMs: the
    // warning color's rule, asked of the path instead of re-derived by callers.
    bool is_difficult() const;

    // An "all-0" path: it has activations and every one of them records
    // skips == 0. False for a path with no activations.
    bool is_allzero() const;

    // Points per scored note, on average. 0.0 for a path with no scoring
    // notes, instead of dividing by zero.
    double avg_mult() const;
};

// ---- HydraRecord --------------------------------------------------------

struct HydraRecord {
    std::optional<double> ms_limit;
    std::optional<int> sp_cap;
    bool sp_cap_converged = true;
    // The fingerprint of the rules the search ran under (blob v6, path
    // structure v4): Rules::fingerprint() for a fixed-cap run,
    // Rules::auto_fingerprint() for an Auto run. A record built in memory
    // starts with the default rules' fixed-cap fingerprint, computed once
    // (core::default_stamp), not once per record decoded; analyze_chart
    // stamps the real one. An older blob reads back core::kNoRulesFingerprint,
    // which matches no rules, so it can never pass as current.
    uint64_t rules_fingerprint = core::default_stamp().fixed;
    std::vector<Path> paths;

    // The chart's multiplier squeezes, in chart order. They depend on the
    // combo alone, never on the path, so a record holds one list rather than
    // one per path (docs/adr/0017).
    std::vector<MultSqueeze> multsqueezes;

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
    // variants), each path before its variants. Pointers into
    // `paths`; valid until the record is modified or moved.
    std::vector<const Path*> all_paths() const;

    // The same traversal over allzero_paths.
    std::vector<const Path*> all_allzero_paths() const;
};

// Format an integer with thousands separators, matching Python's `{:,}`.
std::string group_thousands(int64_t n);

}  // namespace hydra

#endif  // HYDRA_CORE_MODEL_H
