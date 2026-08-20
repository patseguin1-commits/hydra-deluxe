#include "core/model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "core/chord_tables.h"

namespace hydra {

// ---- enums --------------------------------------------------------------

bool allows_cymbals(NoteColor c) {
    return c == NoteColor::Yellow || c == NoteColor::Blue ||
           c == NoteColor::Green;
}

bool allows_dynamics(NoteColor c) {
    return c == NoteColor::Red || c == NoteColor::Yellow ||
           c == NoteColor::Blue || c == NoteColor::Green;
}

std::string color_str(NoteColor c) {
    switch (c) {
        case NoteColor::Kick: return "Kick";
        case NoteColor::Red: return "Red";
        case NoteColor::Yellow: return "Yellow";
        case NoteColor::Blue: return "Blue";
        case NoteColor::Green: return "Green";
    }
    return "";
}

std::string color_notationstr(NoteColor c) {
    switch (c) {
        case NoteColor::Kick: return "K";
        case NoteColor::Red: return "R";
        case NoteColor::Yellow: return "Y";
        case NoteColor::Blue: return "B";
        case NoteColor::Green: return "G";
    }
    return "";
}

NoteCymbalType cymbal_flip(NoteCymbalType t) {
    return t == NoteCymbalType::Cymbal ? NoteCymbalType::Normal
                                       : NoteCymbalType::Cymbal;
}

// ---- ChordNote ----------------------------------------------------------

int64_t ChordNote::hash() const {
    return 1000 * static_cast<int64_t>(colortype) +
           100 * static_cast<int64_t>(dynamictype) +
           10 * static_cast<int64_t>(cymbaltype) + (is2x ? 1 : 0);
}

bool ChordNote::operator==(const ChordNote& o) const {
    return colortype == o.colortype && dynamictype == o.dynamictype &&
           cymbaltype == o.cymbaltype && is2x == o.is2x;
}

std::string ChordNote::str() const {
    std::string cym;
    if (allows_cymbals(colortype))
        cym = cymbaltype == NoteCymbalType::Cymbal ? "Cym" : "Tom";

    std::string mod;
    if (allows_dynamics(colortype)) {
        switch (dynamictype) {
            case NoteDynamicType::Normal: mod = ""; break;
            case NoteDynamicType::Ghost: mod = " (Ghost)"; break;
            case NoteDynamicType::Accent: mod = " (Accent)"; break;
        }
    } else if (is2x) {
        mod = " (2x)";
    }

    return color_str(colortype) + cym + mod;
}

int ChordNote::basescore() const {
    int points = is_cymbal() ? 65 : 50;
    if (is_dynamic()) points *= 2;
    return points;
}

// ---- Chord: CPython tuple hash ------------------------------------------

namespace {

// One tuple element's hash. Each of the five slots is a Python int: -1 for an
// absent note, or the ChordNote hash (a small positive int) for a present one.
// PyObject_Hash(int) is the int itself, except hash(-1) == -2 — the only fold
// that can occur here.
uint64_t py_int_lane(int64_t slot) {
    if (slot == -1) return static_cast<uint64_t>(static_cast<int64_t>(-2));
    return static_cast<uint64_t>(slot);
}

// CPython's tuplehash (Objects/tupleobject.c, xxHash-based, unseeded) for a
// fixed 5-tuple. Deterministic across runs, which is why the chord encode
// table can be keyed on it.
int64_t cpython_tuple_hash5(const int64_t slots[5]) {
    const uint64_t P1 = 11400714785074694791ULL;
    const uint64_t P2 = 14029467366897019727ULL;
    const uint64_t P5 = 2870177450012600261ULL;

    uint64_t acc = P5;
    for (int i = 0; i < 5; ++i) {
        uint64_t lane = py_int_lane(slots[i]);
        acc += lane * P2;
        acc = (acc << 31) | (acc >> 33);  // rotate left 31
        acc *= P1;
    }
    acc += 5ULL ^ (P5 ^ 3527539ULL);  // len ^ (P5 ^ 3527539)
    if (acc == static_cast<uint64_t>(-1)) acc = 1546275796ULL;
    return static_cast<int64_t>(acc);
}

}  // namespace

int64_t Chord::hash() const {
    int64_t slots[5];
    for (int i = 0; i < 5; ++i)
        slots[i] = notemap_[i].has_value() ? notemap_[i]->hash() : -1;
    return cpython_tuple_hash5(slots);
}

std::string Chord::code() const {
    const std::string* s = encode_chord(hash());
    if (s == nullptr)
        throw std::out_of_range("chord has no encode-table entry");
    return *s;
}

Chord Chord::from_code(const std::string& code) {
    const std::vector<ChordNoteFields>* notes = decode_chord(code);
    if (notes == nullptr)
        throw std::out_of_range("unknown chord code: " + code);

    Chord chord;
    for (const ChordNoteFields& nf : *notes) {
        ChordNote note{static_cast<NoteColor>(nf.color),
                       static_cast<NoteDynamicType>(nf.dyn),
                       static_cast<NoteCymbalType>(nf.cym), nf.is2x != 0};
        chord.insert_note(note);
    }
    return chord;
}

// ---- Chord: rest --------------------------------------------------------

bool Chord::operator==(const Chord& o) const {
    for (int i = 0; i < 5; ++i) {
        if (notemap_[i].has_value() != o.notemap_[i].has_value()) return false;
        if (notemap_[i].has_value() && !(*notemap_[i] == *o.notemap_[i]))
            return false;
    }
    return true;
}

std::optional<ChordNote>& Chord::at(NoteColor c) {
    return notemap_[static_cast<int>(c) - 1];
}

const std::optional<ChordNote>& Chord::at(NoteColor c) const {
    return notemap_[static_cast<int>(c) - 1];
}

std::vector<ChordNote> Chord::notes(bool basesorted) const {
    std::vector<ChordNote> out;
    for (const auto& slot : notemap_)
        if (slot.has_value()) out.push_back(*slot);
    if (basesorted) {
        std::stable_sort(out.begin(), out.end(),
                         [](const ChordNote& a, const ChordNote& b) {
                             return a.basescore() < b.basescore();
                         });
    }
    return out;
}

int Chord::count() const {
    int n = 0;
    for (const auto& slot : notemap_)
        if (slot.has_value()) ++n;
    return n;
}

int Chord::hands_count() const {
    return at(NoteColor::Kick).has_value() ? count() - 1 : count();
}

std::string Chord::rowstr() const {
    std::vector<ChordNote> ns = notes();
    std::string inner;
    for (size_t i = 0; i < ns.size(); ++i) {
        if (i) inner += " - ";
        inner += ns[i].str();
    }
    return "[" + inner + "]";
}

std::string Chord::notationstr() const {
    std::string krybg = "[";
    const NoteColor order[5] = {NoteColor::Kick, NoteColor::Red,
                                NoteColor::Yellow, NoteColor::Blue,
                                NoteColor::Green};
    for (NoteColor c : order)
        krybg += at(c).has_value() ? color_notationstr(c) : " ";
    return krybg + "]";
}

void Chord::apply_disco_flip() {
    std::optional<ChordNote> red = at(NoteColor::Red);
    std::optional<ChordNote> yellow = at(NoteColor::Yellow);

    if (red) {
        red->cymbaltype = NoteCymbalType::Cymbal;
        red->colortype = NoteColor::Yellow;
    }
    if (yellow) {
        yellow->cymbaltype = NoteCymbalType::Normal;
        yellow->colortype = NoteColor::Red;
    }

    at(NoteColor::Red) = yellow;
    at(NoteColor::Yellow) = red;
}

void Chord::apply_flam_conversion() {
    if (hands_count() != 1) return;
    ChordNote flam = activation_note();
    switch (flam.colortype) {
        case NoteColor::Red: flam.colortype = NoteColor::Yellow; break;
        case NoteColor::Yellow: flam.colortype = NoteColor::Blue; break;
        case NoteColor::Blue: flam.colortype = NoteColor::Green; break;
        case NoteColor::Green: flam.colortype = NoteColor::Blue; break;
        default: break;  // Kick: no conversion, as the Python match has no case.
    }
    insert_note(flam);
}

ChordNote& Chord::add_note(NoteColor color) {
    std::optional<ChordNote>& slot = at(color);
    if (slot.has_value()) throw ChartFileError("Duplicate note.");
    slot = ChordNote{color};
    return *slot;
}

void Chord::insert_note(const ChordNote& note) { at(note.colortype) = note; }

void Chord::add_2x() {
    add_note(NoteColor::Kick);
    at(NoteColor::Kick)->is2x = true;
}

void Chord::apply_cymbal(NoteColor color) {
    // Python asserts the note is present; a modifier without its base note is
    // a malformed chart. Throw rather than deref an empty slot.
    if (!at(color)) throw std::logic_error("apply_cymbal: note not present");
    at(color)->cymbaltype = NoteCymbalType::Cymbal;
}

void Chord::apply_ghost(NoteColor color) {
    if (!at(color)) throw std::logic_error("apply_ghost: note not present");
    at(color)->dynamictype = NoteDynamicType::Ghost;
}

void Chord::apply_accent(NoteColor color) {
    if (!at(color)) throw std::logic_error("apply_accent: note not present");
    at(color)->dynamictype = NoteDynamicType::Accent;
}

const ChordNote& Chord::activation_note() const {
    const NoteColor order[5] = {NoteColor::Green, NoteColor::Blue,
                                NoteColor::Yellow, NoteColor::Red,
                                NoteColor::Kick};
    for (NoteColor c : order) {
        const std::optional<ChordNote>& slot = at(c);
        if (slot.has_value()) return *slot;
    }
    throw std::runtime_error("activation_note on empty chord");
}

// ---- SPSqueeze ----------------------------------------------------------

std::string SPSqueeze::description(double transfer_r, double hit_window_ms,
                                   int speed_pct) const {
    char buf[192];
    const double gap = difficulty();

    // No gap to cover: the note has leeway. Keep the legacy single-hit line.
    if (gap <= 0.0) {
        if (kind == SqueezeKind::SqIn)
            std::snprintf(buf, sizeof(buf),
                          "SqIn: Note timing must be earlier than %.1fms.",
                          timing());
        else
            std::snprintf(buf, sizeof(buf),
                          "SqOut: Note timing must be later than %.1fms.",
                          timing());
        return buf;
    }

    // The joint constraint: r*frontend + note > gap. The even split is the
    // smallest per-hit displacement that satisfies it; the budget is the
    // largest coverable gap at 100% speed with the given hit window.
    const bool sqin = kind == SqueezeKind::SqIn;
    const double per_hit = gap / (1.0 + transfer_r);
    const double budget = squeeze_budget_ms(transfer_r, hit_window_ms);

    std::string out;
    std::snprintf(buf, sizeof(buf),
                  "%s: needs frontend(%s)x%.3f + note(%s) > %.1f ms\n",
                  sqin ? "SqIn" : "SqOut", sqin ? "late" : "early", transfer_r,
                  sqin ? "early" : "late", gap);
    out += buf;

    std::snprintf(buf, sizeof(buf),
                  "  even split: %.1f ms each; budget %.1f ms @1x (W=%.0f)",
                  per_hit, budget, hit_window_ms);
    out += buf;

    if (gap > budget) {
        // Clone Hero song speeds move in 5% steps; round the exact minimum up.
        double pct = gap / budget * 100.0;
        int need = static_cast<int>(std::ceil(pct / 5.0)) * 5;
        std::snprintf(buf, sizeof(buf), " -> needs >=%d%% speed", need);
        out += buf;
    }

    if (speed_pct != 100 && speed_pct > 0) {
        std::snprintf(buf, sizeof(buf), "\n  at %d%%: %.1f ms real per hit",
                      speed_pct, per_hit / (speed_pct / 100.0));
        out += buf;
    }
    return out;
}

// ---- BackendSqueeze -----------------------------------------------------

bool BackendSqueeze::operator==(const BackendSqueeze& o) const {
    return timecode == o.timecode && chord == o.chord && points == o.points &&
           sqout_points == o.sqout_points && is_sp == o.is_sp &&
           offset_ms == o.offset_ms;
}

std::string BackendSqueeze::summarystr(double hit_window_ms) const {
    double off = offset_ms.value_or(0.0);
    const double w = hit_window_ms;
    if (is_sp) {
        if (off < -w) return "Insane SqOut";
        if (off < -10) return "Hard SqOut";
        if (off < 10) return "Standard SqOut";
        if (off < w) return "Easy SqOut";
        return "Free SqOut";
    }
    if (off < -w) return "Free";
    if (off < -10) return "Easy";
    if (off < 3) return "Standard";
    if (off < w) return "Hard (uncounted)";
    return "Insane (uncounted)";
}

// ---- MultSqueeze --------------------------------------------------------

MultSqueeze::MultSqueeze(Chord chord, int combo)
    : chord_(std::move(chord)), combo_(combo) {
    validate();
}

void MultSqueeze::validate() const {
    switch (combo_) {
        case 7: case 8: case 17: case 18: case 27: case 28: break;
        default:
            throw std::invalid_argument("Invalid MultSqueeze combo");
    }
    int mod = (chord_.count() + combo_) % 10;
    if (mod != 0 && mod != 1)
        throw std::invalid_argument("Invalid MultSqueeze chord length");

    std::vector<ChordNote> notes = chord_.notes();
    bool all_same = true;
    for (const ChordNote& n : notes)
        if (n.basescore() != notes[0].basescore()) {
            all_same = false;
            break;
        }
    if (all_same)
        throw std::invalid_argument("MultSqueeze chord has no squeezable notes");
}

int MultSqueeze::multiplier() const { return to_multiplier(combo_) + 1; }

std::string MultSqueeze::direction() const {
    return (combo_ % 10 == 7) ? "high" : "low";
}

int MultSqueeze::points() const {
    std::vector<ChordNote> order = chord_.notes(true);
    return order.back().basescore() - order.front().basescore();
}

std::string MultSqueeze::notationstr() const {
    return std::to_string(multiplier()) + "x";
}

std::string MultSqueeze::howto() const {
    // Ports MultSqueeze.guide_chords + .howto: every "edge" note (the note(s)
    // tied for the highest/lowest basescore, per direction()) is a single-note
    // chord that alone accomplishes the squeeze when hit last/first.
    std::vector<ChordNote> ordered = chord_.notes(/*basesorted=*/true);
    bool high = direction() == "high";
    int edge_score = high ? ordered.back().basescore() : ordered.front().basescore();

    std::string joined;
    for (const ChordNote& note : ordered) {
        if (note.basescore() != edge_score) continue;
        Chord edge;
        edge.insert_note(note);
        if (!joined.empty()) joined += " or ";
        joined += edge.rowstr();
    }
    return "Hit " + joined + (high ? " last." : " first.");
}

// ---- Activation ---------------------------------------------------------

bool Activation::is_e_critical() const {
    return *e_offset < kCalibrationFillWindowMs;
}

bool Activation::is_E0() const { return is_e_critical() && *skips == 0; }

std::optional<double> Activation::e_difficulty(bool verbose) const {
    if (is_E0() || verbose) return -*e_offset + 0.0;
    return std::nullopt;
}

double Activation::squeeze_difficulty(const SPSqueeze& sq) const {
    const double r = sq.kind == SqueezeKind::SqIn ? transfer_pre.late
                                                  : transfer_pre.early;
    return sq.difficulty() / (1.0 + r);
}

std::optional<double> Activation::difficulty() const {
    // Per-hit ms across the board: squeezes are joint two-hit constraints
    // split evenly; the E fill is a single hit, so its raw offset already is
    // per-hit.
    std::vector<double> diffs;
    for (const SPSqueeze& sq : sqinouts) diffs.push_back(squeeze_difficulty(sq));
    if (auto e = e_difficulty()) diffs.push_back(*e);
    if (diffs.empty()) return std::nullopt;
    return *std::max_element(diffs.begin(), diffs.end());
}

bool Activation::is_difficult() const {
    if (auto e = e_difficulty(); e && *e > 2.0) return true;
    for (const SPSqueeze& sq : sqinouts)
        if (squeeze_difficulty(sq) > 2.0) return true;
    return false;
}

std::string Activation::notationstr() const {
    std::string e = is_e_critical() ? "E" : "";
    std::string syms;
    for (const SPSqueeze& sq : sqinouts) syms += sq.symbol();
    return e + std::to_string(*skips) + syms;
}

std::string Activation::notationstr_verbose() const {
    std::vector<std::string> timings;
    if (is_e_critical())
        timings.push_back(
            std::to_string(static_cast<long long>(*e_difficulty(true))) + " ms");
    for (const SPSqueeze& sq : sqinouts)
        timings.push_back(
            std::to_string(static_cast<long long>(squeeze_difficulty(sq))) +
            " ms");

    if (timings.empty()) return notationstr();

    std::string joined;
    for (size_t i = 0; i < timings.size(); ++i) {
        if (i) joined += "/";
        joined += timings[i];
    }
    return notationstr() + " (" + joined + ")";
}

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

std::optional<ActTransferScales> frontend_transfer_scales(const Activation& act,
                                                          const SongTiming& timing) {
    if (!act.timecode || !act.sp_meter) return std::nullopt;

    // 2 measures per SP bar. The SqIn/SqOut phrase note is judged against
    // this pre-extension end; a SqIn then extends the end for the backends by
    // one +2-measure step no matter how many SqIns the activation carries
    // (graph.cpp applies the extension once, on the first SP backend).
    int64_t act_tick = act.timecode->ticks();
    int64_t end_measures = 2 * static_cast<int64_t>(*act.sp_meter);
    int64_t pre_tick = timing.plusmeasure(*act.timecode, end_measures).ticks();

    std::optional<TransferScale> pre =
        transfer_scale_between(act_tick, pre_tick, timing);
    if (!pre) return std::nullopt;

    ActTransferScales scales{*pre, *pre};
    for (const SPSqueeze& sq : act.sqinouts) {
        if (sq.kind != SqueezeKind::SqIn) continue;
        int64_t post_tick =
            timing.plusmeasure(*act.timecode, end_measures + 2).ticks();
        std::optional<TransferScale> post =
            transfer_scale_between(act_tick, post_tick, timing);
        if (post) scales.post = *post;
        break;
    }
    return scales;
}

TransferRelevance transfer_scale_relevance(const Activation& act,
                                           const std::vector<BackendSqueeze>& backends) {
    TransferRelevance rel;
    for (const BackendSqueeze& bsq : backends) {
        if (!bsq.offset_ms) continue;
        if (act.is_sqout_backend(bsq)) rel.early = true;
        else if (*bsq.offset_ms > 2.0) rel.late = true;
    }
    for (const SPSqueeze& sq : act.sqinouts) {
        if (sq.kind == SqueezeKind::SqOut) rel.early = true;
        else rel.late = true;  // a SqIn needs a late (+) frontend hit
    }
    return rel;
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

double effective_backend_ms(double offset_ms, double transfer_r) {
    return std::abs(offset_ms) * 2.0 / (1.0 + transfer_r);
}

double squeeze_budget_ms(double transfer_r, double hit_window_ms) {
    return hit_window_ms * (1.0 + transfer_r);
}

// ---- Path ---------------------------------------------------------------

std::vector<Activation> Path::all_activations() const {
    std::vector<Activation> out;
    out.reserve(activations.size() + variant_tail.size());
    out.insert(out.end(), activations.begin(), activations.end());
    out.insert(out.end(), variant_tail.begin(), variant_tail.end());
    return out;
}

bool Path::has_activations() const {
    return !activations.empty() || !variant_tail.empty();
}

int64_t Path::totalscore() const {
    return score_base + score_combo + score_sp + score_solo + score_accents +
           score_ghosts;
}

std::string Path::pathstring() const {
    if (!has_activations()) return "(No activations.)";
    std::vector<Activation> acts = all_activations();
    std::string out;
    for (size_t i = 0; i < acts.size(); ++i) {
        if (i) out += " ";
        out += acts[i].notationstr();
    }
    return out;
}

std::string Path::pathstring_verbose() const {
    std::vector<std::string> sections;

    if (!multsqueezes.empty()) {
        std::string s;
        for (size_t i = 0; i < multsqueezes.size(); ++i) {
            if (i) s += ", ";
            s += std::to_string(multsqueezes[i].multiplier()) + "x";
        }
        sections.push_back(s);
    } else {
        sections.push_back("(No mult squeezes.)");
    }

    if (has_activations()) {
        std::vector<Activation> acts = all_activations();
        std::string s;
        for (size_t i = 0; i < acts.size(); ++i) {
            if (i) s += " ";
            s += acts[i].notationstr_verbose();
        }
        sections.push_back(s);
    } else {
        sections.push_back("(No activations.)");
    }

    sections.push_back("Score: " + group_thousands(totalscore()));

    std::string out;
    for (size_t i = 0; i < sections.size(); ++i) {
        if (i) out += " | ";
        out += sections[i];
    }
    return out;
}

int Path::recount_tied_paths() {
    tied_count = 1;
    for (Path& v : variants) {
        v.recount_tied_paths();
        tied_count += v.tied_count;
    }
    return tied_count;
}

void Path::prepare_variants() {
    std::vector<Activation> mine = all_activations();
    for (Path& v : variants) {
        int vp = v.var_point.value_or(0);
        v.variant_tail.assign(mine.begin() + vp, mine.end());
        v.score_base = score_base;
        v.score_combo = score_combo;
        v.score_sp = score_sp;
        v.score_solo = score_solo;
        v.score_accents = score_accents;
        v.score_ghosts = score_ghosts;
        v.notecount = notecount;
        v.leftover_sp = leftover_sp;
        v.prepare_variants();
    }
}

std::optional<double> Path::difficulty() const {
    std::optional<double> best;
    for (const Activation& act : all_activations()) {
        if (auto d = act.difficulty()) {
            if (!best || *d > *best) best = d;
        }
    }
    return best;
}

bool Activation::is_sqout_backend(const BackendSqueeze& bsq) const {
    for (const SPSqueeze& sq : sqinouts) {
        if (sq.kind == SqueezeKind::SqOut &&
            std::fabs(bsq.offset_ms.value_or(0.0) - sq.offset()) < 0.01)
            return true;
    }
    return false;
}

std::vector<BackendSqueeze> Activation::display_backends() const {
    std::vector<BackendSqueeze> out;
    for (const BackendSqueeze& bsq : backends) {
        if (std::fabs(bsq.offset_ms.value_or(0.0)) < kBackendDisplayWindowMs ||
            is_sqout_backend(bsq))
            out.push_back(bsq);
    }
    return out;
}

bool Path::is_allzero() const {
    std::vector<Activation> acts = all_activations();
    if (acts.empty()) return false;
    for (const Activation& act : acts)
        if (act.skips.value_or(-1) != 0) return false;
    return true;
}

double Path::avg_mult() const {
    int64_t multscore = totalscore() - score_solo;
    int64_t basescore = score_base + score_ghosts + score_accents;
    if (basescore == 0) return 0.0;
    return static_cast<double>(multscore) / static_cast<double>(basescore);
}

// ---- HydraRecord ---------------------------------------------------------

namespace {

// Depth-first walk over a root list and its nested variants, mirroring
// hydata.HydraRecord.all_paths(). Shared by all_paths() and all_allzero_paths()
// so both traversals stay identical.
std::vector<const Path*> flatten_paths(const std::vector<Path>& roots) {
    std::vector<const Path*> out;
    std::vector<const Path*> queue(roots.size());
    for (size_t i = 0; i < roots.size(); ++i) queue[i] = &roots[i];

    while (!queue.empty()) {
        const Path* p = queue.front();
        queue.erase(queue.begin());
        std::vector<const Path*> variants(p->variants.size());
        for (size_t i = 0; i < p->variants.size(); ++i) variants[i] = &p->variants[i];
        queue.insert(queue.begin(), variants.begin(), variants.end());
        out.push_back(p);
    }
    return out;
}

}  // namespace

std::vector<const Path*> HydraRecord::all_paths() const {
    return flatten_paths(paths);
}

std::vector<const Path*> HydraRecord::all_allzero_paths() const {
    return flatten_paths(allzero_paths);
}

// ---- helpers ------------------------------------------------------------

std::string group_thousands(int64_t n) {
    bool neg = n < 0;
    // Build the magnitude without overflowing on INT64_MIN.
    uint64_t v = neg ? (~static_cast<uint64_t>(n) + 1ULL)
                     : static_cast<uint64_t>(n);
    std::string digits = std::to_string(v);

    std::string out;
    int cnt = 0;
    for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
        out.push_back(digits[static_cast<size_t>(i)]);
        if (++cnt % 3 == 0 && i != 0) out.push_back(',');
    }
    std::reverse(out.begin(), out.end());
    if (neg) out = "-" + out;
    return out;
}

}  // namespace hydra
