#include "core/replay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "core/backend_value.h"
#include "core/scoring.h"
#include "core/squeeze_rating.h"
#include "core/timing.h"
// kSqueezeWindowMs: how far from the Star Power end the engine will even look
// for a reachable squeeze. The warning below uses the engine's own horizon
// rather than a number picked here.
#include "search/graph.h"

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

ReplayResult replay_path(const Song& song, std::vector<ReplayWindow> windows,
                         const core::Rules& rules) {
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
        const CategoryScores sg = category_scores(ts.chord, combo, &per_note, rules.sqout_rule);

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
            // The row's offset from the SP end, as the graph measures it. A
            // chord on or before the deactivation node is inside the window
            // whatever its ms says.
            const double offset = row.tick <= w.deact_tick
                                      ? std::min(row.ms - w.deact_ms, 0.0)
                                      : row.ms - w.deact_ms;
            core::SqOutPosition pos = core::SqOutPosition::NoSqOut;
            if (w.has_sqout) {
                if (row.ms > w.sqout_ms + kSameNoteMs)
                    pos = core::SqOutPosition::After;
                else if (std::fabs(row.ms - w.sqout_ms) < kSameNoteMs)
                    pos = core::SqOutPosition::Exact;
                else
                    pos = core::SqOutPosition::Before;
            }
            if (pos == core::SqOutPosition::After ||
                !core::counted_without_squeeze(offset, rules.backend_leeway_ms))
                continue;
            ++sp_claims;
            sp_points += core::backend_row_value(
                offset, sg.sp, sg.sp - sg.sqout_reduction, pos,
                rules.backend_leeway_ms);
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

std::vector<ReplayWindow> windows_from_json(const nlohmann::json& path) {
    if (!path.is_object() || !path.contains("activations") ||
        !path["activations"].is_array())
        throw std::runtime_error("this path has no \"activations\" array");

    std::vector<ReplayWindow> out;
    int index = 0;
    for (const nlohmann::json& act : path["activations"]) {
        const std::string where = "activation " + std::to_string(index++);
        if (!act.is_object() || !act.contains("act_tick") ||
            !act.contains("deact_tick") || !act["act_tick"].is_number() ||
            !act["deact_tick"].is_number())
            throw std::runtime_error(where +
                                     " has no act_tick/deact_tick number");

        ReplayWindow w;
        w.act_tick = act["act_tick"].get<int64_t>();
        w.deact_tick = act["deact_tick"].get<int64_t>();
        // -1 is how the dump writes a missing value. A window with no
        // deactivation node cannot be replayed, and guessing one would print a
        // wrong score with no hint why.
        if (w.act_tick < 0)
            throw std::runtime_error(where + " has no activation tick");
        if (w.deact_tick < 0)
            throw std::runtime_error(
                where +
                " has no deactivation node; the record it came from predates "
                "the field, so this path cannot be replayed");
        if (w.deact_tick < w.act_tick)
            throw std::runtime_error(where +
                                     " deactivates before it activates");

        if (act.contains("sqinouts") && act["sqinouts"].is_array()) {
            for (const nlohmann::json& sq : act["sqinouts"]) {
                if (!sq.is_object()) continue;
                if (sq.value("kind", std::string()) != "SqOut") continue;
                if (!sq.contains("offset_ms") || !sq["offset_ms"].is_number())
                    throw std::runtime_error(where +
                                             " has a SqOut with no offset_ms");
                w.sqout_offset_ms = sq["offset_ms"].get<double>();
            }
        }
        out.push_back(w);
    }
    return out;
}

std::vector<std::string> ambiguous_window_warnings(
    const Song& song, const ReplayResult& result,
    const std::vector<ReplayWindow>& windows) {
    const SongTiming& timing = song.timing();
    std::vector<std::string> out;

    for (const ReplayWindow& w : windows) {
        if (w.sqout_offset_ms) continue;  // the offset settles the question

        // The last Star Power phrase note inside the window. That is the only
        // note a squeeze-out can be about: the player delays it until Star
        // Power has run out, so it is not doubled and its phrase is banked
        // afterwards.
        const ReplayChord* phrase_note = nullptr;
        for (const ReplayChord& c : result.chords) {
            if (c.tick < w.act_tick) continue;
            if (c.tick > w.deact_tick) break;
            if (c.is_sp_phrase_end) phrase_note = &c;
        }
        if (!phrase_note) continue;

        // How far before the deactivation node that note sits. Beyond the
        // engine's own squeeze horizon the phrase was collected well inside
        // Star Power and no squeeze-out was ever reachable, so there is
        // nothing to warn about. On Hail The Sun - Wake this separates the
        // six real squeeze-outs (0 to 94 ms) from every other window on the
        // path (2.2 seconds and up) with room to spare.
        const double deact_ms = timing.timecode(w.deact_tick).ms();
        const double gap_ms = deact_ms - phrase_note->ms;
        if (gap_ms > kSqueezeWindowMs) continue;

        std::string where = "on the Star Power phrase note at tick " +
                            std::to_string(phrase_note->tick);
        if (gap_ms > kSameNoteMs) {
            char gap[32];
            std::snprintf(gap, sizeof(gap), "%.2f", gap_ms);
            where = "just after the Star Power phrase note at tick " +
                    std::to_string(phrase_note->tick) + " (" + gap +
                    " ms earlier)";
        }

        out.push_back("window " + std::to_string(w.act_tick) + ":" +
                      std::to_string(w.deact_tick) + " ends " + where +
                      " with no squeeze-out offset; if the player squeezed it "
                      "out, this score is high by that note's first-hit share");
    }
    return out;
}

}  // namespace hydra
