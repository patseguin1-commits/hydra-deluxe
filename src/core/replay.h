// Replay — score an arbitrary Star Power path chord by chord, the way the
// engine scores it.
//
// The search never walks a chart note by note; it walks a graph whose edges
// carry pre-summed scores, so nothing in it can answer "what was this one
// chord worth on this path?". This module answers exactly that, by replaying
// the chart against a list of Star Power windows and applying the same three
// rules the graph applies:
//
//   1. One combo counter, never touched by Star Power. So base, combo,
//      accent and ghost are fixed per chord, and the only path-dependent
//      term is whether the chord's doubling (CategoryScores::sp) is paid.
//   2. The Star Power window is inclusive at both ends: the activation chord
//      earns its doubling, and so does the chord sitting on the deactivation
//      node. A chord landing within kBackendLeewayMs after the deactivation
//      earns it too (search/engine.cpp create_deactivated_path).
//   3. A solo pays 100 per note on both tracks and is never doubled.
//
// Display and tooling only, like core/squeeze_rating.h: nothing in the
// search, the store, or a stored record reads any of this. It exists so the
// hydra_replay CLI and its self-check can price a user's own path and prove
// the pricing against the engine's own numbers.

#ifndef HYDRA_CORE_REPLAY_H
#define HYDRA_CORE_REPLAY_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/model.h"
#include "parse/song.h"

namespace hydra {

// One activation's Star Power window, in ticks. `deact_tick` is the
// deactivation node D — for a stored activation, the tick the search
// stamped onto it; it need not be a chord tick.
struct ReplayWindow {
    int64_t act_tick = 0;
    int64_t deact_tick = 0;

    // Set when the activation ends on a squeeze-out: the SqOut phrase note's
    // offset from D, in ms (the value stored on the activation's SPSqueeze).
    // The note is hit after Star Power has ended, so it and everything after
    // it inside the window lose their doubling, and the note itself keeps
    // only what its non-first hits are worth (CategoryScores::sqout_reduction
    // is the first hit's share). Unset for a plain deactivation.
    std::optional<double> sqout_offset_ms;
};

// The six score categories a Path stores, in the same order.
struct ReplayScore {
    int64_t base = 0;
    int64_t combo = 0;
    int64_t sp = 0;
    int64_t solo = 0;
    int64_t accent = 0;
    int64_t ghost = 0;

    int64_t total() const { return base + combo + sp + solo + accent + ghost; }
    void add(const ReplayScore& o) {
        base += o.base;
        combo += o.combo;
        sp += o.sp;
        solo += o.solo;
        accent += o.accent;
        ghost += o.ghost;
    }
    bool operator==(const ReplayScore& o) const {
        return base == o.base && combo == o.combo && sp == o.sp &&
               solo == o.solo && accent == o.accent && ghost == o.ghost;
    }
};

// One note of one chord, in the base-sorted order category_scores reads
// (Chord::notes(true)). `sp_points` is what this note contributes to the
// chord's doubling; it is reported whether or not the chord is under Star
// Power, so a caller can see what a window would be worth.
struct ReplayNote {
    NoteColor color = NoteColor::Kick;
    bool cymbal = false;
    int sp_points = 0;
};

// One scored chord.
struct ReplayChord {
    int index = 0;
    int64_t tick = 0;
    double ms = 0.0;
    int64_t measure = 0;
    int64_t beat = 0;
    int64_t measure_tick = 0;
    double measures_decimal = 0.0;
    std::string chord_code;
    std::vector<ReplayNote> notes;

    bool is_fill = false;           // the chord carries an activation fill
    bool is_solo = false;
    bool is_sp_phrase_end = false;  // the chord ends an SP phrase

    int combo_before = 0;
    int multiplier = 1;
    bool in_sp = false;

    ReplayScore points;
    ReplayScore cum;

    // The running total as the game's counter shows it: a solo's bonus is
    // withheld until the last chord of that solo run, then paid in full.
    int64_t cum_onscreen_total = 0;
};

struct ReplayResult {
    std::vector<ReplayChord> chords;
    ReplayScore final;
};

// Score `song` under `windows` (any order; they are sorted here). An empty
// list scores the chart with no Star Power anywhere.
ReplayResult replay_path(const Song& song, std::vector<ReplayWindow> windows);

// The six score categories off a stored Path, in ReplayScore's shape, so a
// caller can compare it against a ReplayResult::final with operator==
// instead of listing all six fields at each comparison site.
ReplayScore score_of(const Path& path);

// The Star Power windows a stored path describes: one per activation, with
// its deactivation node read straight off the record (Activation::deact_tick,
// stamped by the search) and the SqOut offset copied across when the
// activation ends on one. An activation with no stored deact node — only a
// record written before blob v4 — is skipped, so a path that yields fewer
// windows than it has activations cannot be replayed faithfully; check the
// counts before trusting the score.
std::vector<ReplayWindow> windows_for_path(const Path& path, const Song& song);

// Not offered: a simulated SP meter and skip count. A straightforward
// simulation (one bar per phrase completed outside Star Power, capped at 4,
// two bars to activate, a passed fill with two bars banked is a skip) was
// measured against every corpus path and does not reproduce the engine: 37
// of 1486 activations came out with too few bars, and 224 with the wrong
// skip count, 201 of them too many. The meter misses bars a squeeze-out
// leaves behind, and the skip count over-counts fills the engine knew could
// not actually be summoned in time. Read both off the record instead
// (Activation::sp_meter / ::skips), which is what `hydra_replay dump` does.

}  // namespace hydra

#endif  // HYDRA_CORE_REPLAY_H
