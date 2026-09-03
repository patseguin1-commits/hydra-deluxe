#include "core/replay.h"

#include <algorithm>
#include <cmath>

#include "core/scoring.h"
#include "core/squeeze_rating.h"
#include "core/timing.h"

namespace hydra {

namespace {

// Two chart positions count as the same note when their ms agree this
// closely. The engine compares ticks; a replay only has the SqOut's offset in
// ms, and distinct chord ticks are never this close in real charts.
constexpr double kSameNoteMs = 0.01;

struct Window {
    int64_t act_tick = 0;
    int64_t deact_tick = 0;
    double deact_ms = 0.0;
    bool has_sqout = false;
    double sqout_ms = 0.0;
};

}  // namespace

ReplayResult replay_path(const Song& song, std::vector<ReplayWindow> windows) {
    const SongTiming& timing = song.timing();

    std::vector<Window> wins;
    wins.reserve(windows.size());
    for (const ReplayWindow& w : windows) {
        Window win;
        win.act_tick = w.act_tick;
        win.deact_tick = w.deact_tick;
        win.deact_ms = timing.timecode(w.deact_tick).ms();
        if (w.sqout_offset_ms) {
            win.has_sqout = true;
            win.sqout_ms = win.deact_ms + *w.sqout_offset_ms;
        }
        wins.push_back(win);
    }
    std::sort(wins.begin(), wins.end(), [](const Window& a, const Window& b) {
        return a.act_tick < b.act_tick;
    });

    ReplayResult out;
    out.chords.reserve(song.sequence.size());

    int combo = 0;
    ReplayScore cum;
    int64_t solo_pending = 0;

    const size_t n = song.sequence.size();
    std::vector<CategoryScores> per_note;

    for (size_t i = 0; i < n; ++i) {
        const SongTimestamp& ts = song.sequence[i];
        const CategoryScores sg = category_scores(ts.chord, combo, &per_note);

        ReplayChord row;
        row.index = static_cast<int>(i);
        row.tick = ts.timecode.ticks();
        row.ms = ts.timecode.ms();
        const int64_t* mbt = ts.timecode.measure_beats_ticks();
        row.measure = mbt[0];
        row.beat = mbt[1];
        row.measure_tick = mbt[2];
        row.measures_decimal = ts.timecode.measures_decimal();
        row.chord_code = ts.chord.code();
        row.is_fill = ts.has_activation();
        row.is_solo = ts.flag_solo;
        row.is_sp_phrase_end = ts.flag_sp;
        row.combo_before = combo;
        row.multiplier = to_multiplier(combo);

        const std::vector<ChordNote> ordering = ts.chord.notes(true);
        row.notes.reserve(ordering.size());
        for (size_t k = 0; k < ordering.size(); ++k) {
            ReplayNote note;
            note.color = ordering[k].colortype;
            note.cymbal = ordering[k].is_cymbal();
            note.sp_points = k < per_note.size() ? per_note[k].sp : 0;
            row.notes.push_back(note);
        }

        // How many activations pay this chord's doubling. Normally 0 or 1;
        // summed rather than flagged because the engine sums too (a chord in
        // one activation's leeway that is also the next activation's frontend
        // is paid by both).
        int64_t sp_points = 0;
        int sp_claims = 0;
        for (const Window& w : wins) {
            if (row.tick < w.act_tick) continue;
            const bool inclusive = row.tick <= w.deact_tick;
            const bool leeway = row.ms > w.deact_ms &&
                                row.ms < w.deact_ms + kBackendLeewayMs;
            if (!inclusive && !leeway) continue;

            if (w.has_sqout) {
                // Nothing past the squeezed-out note is under Star Power any
                // more; the note itself keeps all but its first hit's share.
                if (row.ms > w.sqout_ms + kSameNoteMs) continue;
                ++sp_claims;
                if (std::fabs(row.ms - w.sqout_ms) < kSameNoteMs) {
                    sp_points += sg.sp - sg.sqout_reduction;
                    continue;
                }
                sp_points += sg.sp;
                continue;
            }
            ++sp_claims;
            sp_points += sg.sp;
        }
        row.in_sp = sp_claims > 0;

        row.points.base = sg.base;
        row.points.combo = sg.combo;
        row.points.sp = sp_points;
        row.points.solo = ts.flag_solo ? 100LL * ts.chord.count() : 0;
        row.points.accent = sg.accent;
        row.points.ghost = sg.ghost;

        combo += ts.chord.count();

        cum.add(row.points);
        row.cum = cum;

        if (ts.flag_solo) {
            solo_pending += row.points.solo;
            const bool last_of_run =
                i + 1 >= n || !song.sequence[i + 1].flag_solo;
            if (last_of_run) solo_pending = 0;
        }
        row.cum_onscreen_total = cum.total() - solo_pending;

        out.chords.push_back(std::move(row));
    }

    out.final = cum;
    return out;
}


ReplayScore score_of(const Path& path) {
    ReplayScore s;
    s.base = path.score_base;
    s.combo = path.score_combo;
    s.sp = path.score_sp;
    s.solo = path.score_solo;
    s.accent = path.score_accents;
    s.ghost = path.score_ghosts;
    return s;
}

// The song is no longer consulted: the deact node comes off the record, so
// there is nothing left to rebuild from the chart. The parameter stays so
// callers read the same, and so a future window rule can use it.
std::vector<ReplayWindow> windows_for_path(const Path& path, const Song&) {
    std::vector<ReplayWindow> out;
    for (const Activation& act : path.all_activations()) {
        if (!act.timecode || !act.deact_tick) continue;

        ReplayWindow w;
        w.act_tick = act.timecode->ticks();
        w.deact_tick = *act.deact_tick;
        for (const SPSqueeze& sq : act.sqinouts)
            if (sq.kind == SqueezeKind::SqOut) w.sqout_offset_ms = sq.offset();
        out.push_back(w);
    }
    return out;
}

}  // namespace hydra
