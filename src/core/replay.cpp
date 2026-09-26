#include "core/replay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "core/backend_value.h"
#include "core/scoring.h"
#include "core/squeeze_rating.h"
#include "core/timing.h"
#include "core/model.h"  // kSqueezeWindowMs, the engine's squeeze horizon

namespace hydra {

namespace {

struct Window {
    int64_t act_tick = 0;
    int64_t deact_tick = 0;
    double deact_ms = 0.0;
    std::optional<int64_t> sqout_tick;
};

// The phrase chords the graph could squeeze out at deactivation node D: the
// ones strictly within kSqueezeWindowMs of D, in chart order. The graph takes
// the first of them (graph.cpp add_deact_edge for chords up to D, then
// store_new_backend for chords after it).
std::vector<const SongTimestamp*> sqout_candidates(const Song& song,
                                                   int64_t deact_tick) {
    const double d_ms = song.timing().timecode(deact_tick).ms();
    std::vector<const SongTimestamp*> out;
    for (const SongTimestamp& ts : song.sequence)
        if (ts.flag_sp && std::fabs(ts.timecode.ms() - d_ms) < kSqueezeWindowMs)
            out.push_back(&ts);
    return out;
}

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
        if (w.sqout_offset_ms && !w.sqout_tick)
            throw std::invalid_argument(
                "window " + std::to_string(w.act_tick) + ":" +
                std::to_string(w.deact_tick) +
                " has a SqOut offset but no SqOut chord; resolve it with "
                "resolve_sqout_note first");
        win.sqout_tick = w.sqout_tick;
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
        row.multiplier = sg.multiplier;  // what category_scores applied
        row.multiplier_after = sg.multiplier_after;

        const std::vector<ChordNote> ordering = ts.chord.notes(true);
        row.notes.reserve(ordering.size());
        for (size_t k = 0; k < ordering.size(); ++k) {
            ReplayNote note;
            note.color = ordering[k].colortype;
            note.cymbal = ordering[k].is_cymbal();
            note.sp_points = k < per_note.size() ? per_note[k].sp : 0;
            note.multiplier = k < per_note.size() ? per_note[k].multiplier : 1;
            note.dynamics_bonus =
                k < per_note.size() ? per_note[k].dynamics_bonus : 0;
            note.dynamic = ordering[k].dynamictype;
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
            const core::SqOutPosition pos =
                core::sqout_position(row.tick, w.sqout_tick);
            if (pos == core::SqOutPosition::After ||
                !core::counted_without_squeeze(offset, rules.backend_leeway_ms))
                continue;
            ++sp_claims;
            sp_points += core::backend_row_value(
                offset, sg.sp, sg.sqout_sp(), pos,
                rules.backend_leeway_ms);
        }
        row.in_sp = sp_claims > 0;
        row.multiplier_shown = shown_multiplier(row.multiplier_after, row.in_sp);

        row.points.base = sg.base;
        row.points.combo = sg.combo;
        row.points.sp = sp_points;
        row.points.solo =
            ts.flag_solo ? static_cast<int64_t>(kSoloBonusPerNote) * ts.chord.count() : 0;
        row.points.accent = sg.accent;
        row.points.ghost = sg.ghost;

        combo += ts.chord.count();
        row.combo_after = combo;

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
    for (const Activation& act : path.walk_activations()) {
        if (!act.timecode || !act.deact_tick) continue;

        ReplayWindow w;
        w.act_tick = act.timecode->ticks();
        w.deact_tick = *act.deact_tick;
        for (const SPSqueeze& sq : act.sqinouts)
            if (sq.kind == SqueezeKind::SqOut) w.sqout_offset_ms = sq.offset();
        // A squeeze-out with no stored chord tick is a record from before v6.
        // It is skipped like one with no deact node: never guessed at.
        if (w.sqout_offset_ms && !act.sqout_tick) continue;
        w.sqout_tick = act.sqout_tick;
        out.push_back(w);
    }
    return out;
}

PathReplay replay_stored_path(const Song& song, const Path& path,
                              const core::Rules& rules) {
    PathReplay out;
    out.windows = windows_for_path(path, song);
    out.result = replay_path(song, out.windows, rules);
    out.stored = score_of(path);
    out.activations = path.walk_activations().size();
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

        // -1 (or no key, from a dump written before v6) means "not stamped".
        // The caller resolves a bare offset with resolve_sqout_note.
        if (act.contains("sqout_tick") && act["sqout_tick"].is_number() &&
            act["sqout_tick"].get<int64_t>() >= 0)
            w.sqout_tick = act["sqout_tick"].get<int64_t>();

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

nlohmann::json score_json(const ReplayScore& s) {
    nlohmann::json j = nlohmann::json::object();
    for (const ReplayScoreField& f : kReplayScoreFields) j[f.name] = s.*(f.member);
    return j;
}

// The path list `dump` and `target` both print. One shape, so anything that
// reads dump's JSON reads target's too.
nlohmann::json paths_json(const std::vector<const Path*>& all, const SongTiming& timing) {
    nlohmann::json paths = nlohmann::json::array();
    int index = 0;
    for (const Path* p : all) {
        nlohmann::json acts = nlohmann::json::array();
        for (const Activation& act : p->walk_activations()) {
            nlohmann::json sq = nlohmann::json::array();
            for (const SPSqueeze& s2 : act.sqinouts)
                sq.push_back(nlohmann::json{{"kind", s2.type_name()},
                                            {"offset_ms", s2.offset()}});

            const int64_t act_tick = act.timecode ? act.timecode->ticks() : -1;
            const std::optional<int64_t>& d = act.deact_tick;
            int64_t nominal = -1;
            if (act.timecode && act.sp_meter)
                nominal = timing
                              .plusmeasure(*act.timecode, sp_bars_to_measures(*act.sp_meter))
                              .ticks();

            acts.push_back(nlohmann::json{
                {"act_tick", act_tick},
                {"deact_tick", d ? *d : -1},
                {"sqout_tick", act.sqout_tick ? *act.sqout_tick : -1},
                {"nominal_deact_tick", nominal},
                {"sp_meter", act.sp_meter ? *act.sp_meter : -1},
                {"skips", act.skips ? *act.skips : -1},
                {"chord_code", act.chord ? act.chord->code() : std::string()},
                {"sqinouts", sq},
            });
        }
        paths.push_back(nlohmann::json{
            {"index", index++},
            {"pathstring", p->pathstring()},
            {"total", p->totalscore()},
            {"score", score_json(score_of(*p))},
            {"activations", acts},
        });
    }
    return paths;
}

SqOutNote resolve_sqout_note(const Song& song, const ReplayWindow& w) {
    const std::string where = "window " + std::to_string(w.act_tick) + ":" +
                              std::to_string(w.deact_tick);
    if (!w.sqout_offset_ms)
        throw std::runtime_error(where + " has no SqOut offset to resolve");
    const double d_ms = song.timing().timecode(w.deact_tick).ms();
    const double want_ms = d_ms + *w.sqout_offset_ms;

    const std::vector<const SongTimestamp*> cands =
        sqout_candidates(song, w.deact_tick);
    const SongTimestamp* best = nullptr;
    for (const SongTimestamp* ts : cands)
        if (!best || std::fabs(ts->timecode.ms() - want_ms) <
                         std::fabs(best->timecode.ms() - want_ms))
            best = ts;

    char buf[512];
    if (!best) {
        std::snprintf(buf, sizeof(buf),
                      "%s has a SqOut offset of %.2f ms but no Star Power "
                      "phrase chord within %.0f ms of its SP end",
                      where.c_str(), *w.sqout_offset_ms, kSqueezeWindowMs);
        throw std::runtime_error(buf);
    }
    const SqOutNote typed{best->timecode.ticks(), best->timecode.ms() - d_ms};

    // The engine squeezes out only the first candidate. Anything else is a
    // squeeze-out the search can never produce: refuse, never price it.
    const SongTimestamp* engine = cands.front();
    if (best != engine) {
        std::snprintf(
            buf, sizeof(buf),
            "%s: the SqOut offset %.2f ms lands on the phrase chord at tick "
            "%lld (%.2f ms from the SP end), which the engine never squeezes "
            "out. The only chord it can squeeze out here is the first phrase "
            "chord within %.0f ms of the SP end, at tick %lld (%.2f ms). "
            "Not priced.",
            where.c_str(), *w.sqout_offset_ms, (long long)typed.tick,
            typed.offset_ms, kSqueezeWindowMs,
            (long long)engine->timecode.ticks(), engine->timecode.ms() - d_ms);
        throw std::runtime_error(buf);
    }
    return typed;
}

std::vector<std::string> ambiguous_window_warnings(
    const Song& song, const ReplayResult& result,
    const std::vector<ReplayWindow>& windows) {
    const SongTiming& timing = song.timing();
    std::vector<std::string> out;

    for (const ReplayWindow& w : windows) {
        // A squeeze-out offset or chord settles the question.
        if (w.sqout_offset_ms || w.sqout_tick) continue;

        // The one chord the graph could squeeze out at this D.
        const std::vector<const SongTimestamp*> cands =
            sqout_candidates(song, w.deact_tick);
        if (cands.empty()) continue;
        const int64_t tick = cands.front()->timecode.ticks();

        // Only a chord the window paid can make the score too high: one at or
        // before D, or inside the leeway after it.
        const ReplayChord* chord = nullptr;
        for (const ReplayChord& c : result.chords)
            if (c.tick == tick) chord = &c;
        if (!chord || !chord->in_sp) continue;

        const double deact_ms = timing.timecode(w.deact_tick).ms();
        std::string where = "on the Star Power phrase note at tick " +
                            std::to_string(tick);
        char gap[32];
        if (tick < w.deact_tick) {
            std::snprintf(gap, sizeof(gap), "%.2f", deact_ms - chord->ms);
            where = "just after the Star Power phrase note at tick " +
                    std::to_string(tick) + " (" + gap + " ms earlier)";
        } else if (tick > w.deact_tick) {
            std::snprintf(gap, sizeof(gap), "%.2f", chord->ms - deact_ms);
            where = "just before the Star Power phrase note at tick " +
                    std::to_string(tick) + " (" + gap + " ms later)";
        }

        out.push_back("window " + std::to_string(w.act_tick) + ":" +
                      std::to_string(w.deact_tick) + " ends " + where +
                      " with no squeeze-out offset; if the player squeezed it "
                      "out, this score is high by that note's first-hit share");
    }
    return out;
}

}  // namespace hydra
