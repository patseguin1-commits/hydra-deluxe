// Domain model — the C++ port of hydra/hydata.py.
//
// The note/chord/path/record types the parser fills and the search produces.
// The *string* forms here (Chord::code, Path::pathstring/pathstring_verbose,
// Activation::notationstr) are what the golden parity diff checks, so they must
// match Python byte-for-byte: comma-grouped scores, int() truncation on ms, the
// exact +/- squeeze symbols and [KRYBG] slot layout.
//
// The JSON save/load path from hydata.py is intentionally not ported — Phase 4
// replaces it with a binary format — but Chord::code / Chord::from_code (which
// that path used) are ported because the parser and the golden song block need
// them.

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
    int ghost_count() const;
    int accent_count() const;

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
    std::string summarystr() const;
    std::string ratingstr() const;
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

struct Activation {
    std::optional<int> skips;
    std::optional<Timecode> timecode;
    std::optional<Chord> chord;
    std::optional<int> sp_meter;
    std::optional<int> frontend_points;
    std::vector<BackendSqueeze> backends;
    std::vector<SPSqueeze> sqinouts;
    std::optional<double> e_offset;

    std::string notationstr() const;
    std::string notationstr_verbose() const;
    bool is_e_critical() const;  // e_offset < 70
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

// hymisc.BACKEND_DISPLAY_WINDOW_MS: backends within this window of the
// deactivation are worth showing/storing.
constexpr double kBackendDisplayWindowMs = 140.0;

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

    const Path& best_path() const { return paths.at(0); }
    Path& best_path() { return paths.at(0); }

    // Depth-first traversal of the path tree (root paths + their nested
    // variants), mirroring hydata.HydraRecord.all_paths(). Pointers into
    // `paths`; valid until the record is modified or moved.
    std::vector<const Path*> all_paths() const;
};

// Format an integer with thousands separators, matching Python's `{:,}`.
std::string group_thousands(int64_t n);

}  // namespace hydra

#endif  // HYDRA_CORE_MODEL_H
