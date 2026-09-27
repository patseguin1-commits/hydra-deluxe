// See preview_view.h. Everything here reads the song's own SongTiming, so the
// notes and the path overlay share one ms truth and never drift apart.

#include "app/preview_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <optional>

#include "core/squeeze_rating.h"
#include "core/replay.h"

namespace hydra::app {

PreviewLane lane_of(NoteColor color) {
    switch (color) {
        case NoteColor::Kick:   return PreviewLane::Kick;
        case NoteColor::Red:    return PreviewLane::Red;
        case NoteColor::Yellow: return PreviewLane::Yellow;
        case NoteColor::Blue:   return PreviewLane::Blue;
        case NoteColor::Green:  return PreviewLane::Green;
    }
    return PreviewLane::Kick;  // unreachable; NoteColor is a closed set
}

namespace {

PreviewNote note_from(const SongTimestamp& ts, const ChordNote& n) {
    PreviewNote pn;
    pn.tick = ts.timecode.ticks();
    pn.ms = ts.timecode.ms();
    pn.measure = ts.timecode.measures_decimal();
    pn.lane = lane_of(n.colortype);
    pn.cymbal = n.is_cymbal();
    pn.ghost = n.is_ghost();
    pn.accent = n.is_accent();
    pn.double_kick = n.is2x;
    pn.solo = ts.flag_solo;
    return pn;
}

PreviewSpan span_from_ticks(const Song& song, int64_t start, int64_t end) {
    PreviewSpan s;
    s.start_tick = start;
    s.end_tick = end;
    s.start_ms = song.timecode(start).ms();
    s.end_ms = song.timecode(end).ms();
    return s;
}

// One point where the SP meter's slope or value can change during an
// activation: a tempo change, a meter change, a phrase collected mid-SP, or an
// endpoint of the active window.
struct DrainSplit {
    int64_t tick = 0;
    double ms = 0.0;
    bool collection = false;  // a phrase lands here: the bank gets a bar back
};

void push_segment(SpMeterCurve& curve, double start_ms, double end_ms,
                  double start_bars, double end_bars) {
    if (end_ms <= start_ms) return;  // a zero-width stretch draws nothing
    curve.segments.push_back({start_ms, end_ms, start_bars, end_bars});
}

// The SP meter over the whole chart (see SpMeterCurve). Walks time forward with
// a bank in bars: flat outside SP, stepping up one bar at each phrase's last
// note, and sloping down through each activation's active window.
SpMeterCurve build_sp_meter_curve(const PreviewScene& scene, const SongTiming& timing,
                                  int sp_cap) {
    SpMeterCurve curve;
    curve.cap = sp_cap < 1 ? 1 : sp_cap;
    if (scene.sp_phrases.empty() && scene.activations.empty()) return curve;

    const double cap = static_cast<double>(curve.cap);
    // A phrase is banked on its last note. Every phrase is assumed hit: the
    // path itself is only scored under that same full-combo premise.
    const std::vector<PreviewSpan>& phrases = scene.sp_phrases;
    size_t next_phrase = 0;
    double bank = 0.0;
    double cursor_ms = 0.0;

    // Flat run up to `until_ms`, stepping the bank at every phrase that lands
    // strictly before it.
    auto run_flat_to = [&](double until_ms) {
        while (next_phrase < phrases.size() && phrases[next_phrase].end_ms < until_ms) {
            push_segment(curve, cursor_ms, phrases[next_phrase].end_ms, bank, bank);
            cursor_ms = phrases[next_phrase].end_ms;
            bank = std::min(bank + 1.0, cap);
            ++next_phrase;
        }
        push_segment(curve, cursor_ms, until_ms, bank, bank);
        if (until_ms > cursor_ms) cursor_ms = until_ms;
    };

    for (const PreviewActivation& act : scene.activations) {
        run_flat_to(act.ms);

        // Snap to what the engine recorded rather than to the phrases counted
        // above: if a squeezed boundary phrase ever makes the two disagree, the
        // record is the truth (see "derive display from engine truth").
        bank = static_cast<double>(act.sp_meter);

        // A stale record (no sp_meter, or no deact node) cannot say how long
        // the SP lasted, so there is no drain to draw — the bank just goes.
        if (act.sp_meter <= 0 || !act.has_sp_end) {
            bank = 0.0;
            continue;
        }

        // The phrases whose last note falls inside the window. A phrase ending
        // on the activation note itself is already inside the engine's recorded
        // bank (the sp_meter snap above); it is neither collected here nor
        // banked later. Ticks compare exactly where ms would not.
        std::vector<const PreviewSpan*> window;
        for (size_t i = next_phrase; i < phrases.size(); ++i) {
            if (phrases[i].end_ms >= act.sp_end_ms) break;
            if (phrases[i].end_tick > act.tick) window.push_back(&phrases[i]);
            ++next_phrase;  // consumed here; the flat walk must not step it again
        }

        // Which phrases SP collects is engine truth: the record lists them,
        // late-SqIn and cap-clamped ones included. A phrase ending on the
        // activation note is already inside sp_meter, so only ticks strictly
        // inside the window step the drain (the same bounds the tempo and
        // meter splits below use). A window phrase the list does not name was
        // squeezed out: it is hit just after SP ends, so it takes no step here
        // and banks the moment the window closes, for the next activation.
        std::vector<DrainSplit> splits;
        for (int64_t t : act.collected_phrase_ticks)
            if (t > act.tick && t < act.sp_end_tick)
                splits.push_back({t, timing.ms_index().at(t), true});
        int64_t squeezed_out = 0;
        for (const PreviewSpan* p : window)
            if (std::find(act.collected_phrase_ticks.begin(),
                          act.collected_phrase_ticks.end(),
                          p->end_tick) == act.collected_phrase_ticks.end())
                ++squeezed_out;
        for (const PreviewTempo& t : scene.tempos)
            if (t.tick > act.tick && t.tick < act.sp_end_tick)
                splits.push_back({t.tick, t.ms, false});
        // Meters carry only ticks; their ms comes from the same index every
        // other time in the scene does.
        for (const PreviewMeter& m : scene.meters)
            if (m.tick > act.tick && m.tick < act.sp_end_tick)
                splits.push_back({m.tick, timing.ms_index().at(m.tick), false});
        std::stable_sort(splits.begin(), splits.end(),
                         [](const DrainSplit& a, const DrainSplit& b) { return a.tick < b.tick; });

        // SP burns two measures per bar, so track the drain in measures and
        // halve it only when writing a segment's endpoints.
        double remaining = static_cast<double>(sp_bars_to_measures(act.sp_meter));
        int64_t prev_tick = act.tick;
        double prev_ms = act.ms;
        double prev_measures = timing.measures_at_tick_f(static_cast<double>(prev_tick));
        for (const DrainSplit& s : splits) {
            const double measures = timing.measures_at_tick_f(static_cast<double>(s.tick));
            const double elapsed = measures - prev_measures;
            const double left = std::max(0.0, remaining - elapsed);
            push_segment(curve, prev_ms, s.ms,
                         remaining / static_cast<double>(kMeasuresPerSpBar),
                         left / static_cast<double>(kMeasuresPerSpBar));
            remaining = left;
            if (s.collection)
                remaining = std::min(remaining + static_cast<double>(kMeasuresPerSpBar),
                                     static_cast<double>(sp_bars_to_measures(curve.cap)));
            prev_tick = s.tick;
            prev_ms = s.ms;
            prev_measures = measures;
        }
        // The deact node is engine truth: the meter is empty exactly there, so
        // the last stretch is forced to zero and absorbs any residue left in
        // `remaining`.
        push_segment(curve, prev_ms, act.sp_end_ms,
                     remaining / static_cast<double>(kMeasuresPerSpBar), 0.0);
        // The window phrases SP did not collect are the squeezed-out ones: the
        // player hits them just past the deact node, so they are banked as the
        // window closes. The next segment starts at that value, which is what
        // makes it read as a step at the boundary.
        bank = std::min(static_cast<double>(squeezed_out), cap);
        cursor_ms = std::max(cursor_ms, act.sp_end_ms);
    }

    // Phrases after the last activation still bank, and the meter holds its
    // final value to the end of the chart.
    while (next_phrase < phrases.size()) {
        push_segment(curve, cursor_ms, phrases[next_phrase].end_ms, bank, bank);
        cursor_ms = std::max(cursor_ms, phrases[next_phrase].end_ms);
        bank = std::min(bank + 1.0, cap);
        ++next_phrase;
    }
    // Always emitted, even at zero width: it is what sp_meter_bars_at reads
    // back as the value after the curve ends.
    const double end_ms = std::max(cursor_ms, scene.song_length_ms);
    curve.segments.push_back({cursor_ms, end_ms, bank, bank});
    return curve;
}

// The running score, from the same replay hydra_replay uses. Trusted only
// when replay_stored_path says the replay stands for the path.
PreviewScore build_score(const Song& song, const Path* path, const core::Rules& rules) {
    PreviewScore score;
    if (path == nullptr) return score;  // None
    score.state = PreviewScore::State::Unavailable;
    try {
        const PathReplay pr = replay_stored_path(song, *path, rules);
        if (!pr.faithful()) return score;
        score.steps.reserve(pr.result.chords.size());
        for (const ReplayChord& c : pr.result.chords)
            score.steps.push_back({c.ms, c.cum_onscreen_total, c.multiplier_shown, c.combo_after});
    } catch (const std::exception&) {
        score.steps.clear();
        return score;  // Unavailable
    }
    score.state = PreviewScore::State::Ready;
    return score;
}

}  // namespace

PreviewScene build_preview_scene(const Song& song, const Path* path, int sp_cap,
                                 const core::Rules& rules) {
    PreviewScene scene;
    if (song.is_empty()) return scene;

    bool in_solo = false;
    int64_t solo_start = 0;
    int64_t solo_last = 0;

    for (const SongTimestamp& ts : song.sequence) {
        for (const ChordNote& n : ts.chord.notes())
            scene.notes.push_back(note_from(ts, n));

        // Solo: coalesce a run of flagged timestamps into one span.
        if (ts.flag_solo) {
            if (!in_solo) {
                in_solo = true;
                solo_start = ts.timecode.ticks();
            }
            solo_last = ts.timecode.ticks();
        } else if (in_solo) {
            scene.solos.push_back(span_from_ticks(song, solo_start, solo_last));
            in_solo = false;
        }

        // SP phrase: the end note carries the start tick the parser kept.
        if (ts.flag_sp && ts.sp_phrase_start.has_value())
            scene.sp_phrases.push_back(
                span_from_ticks(song, *ts.sp_phrase_start, ts.timecode.ticks()));

        // Activation fill: the window ends at this note, `activation_length`
        // ticks wide.
        if (ts.activation_length.has_value()) {
            int64_t end = ts.timecode.ticks();
            PreviewFill f;
            f.span = span_from_ticks(song, end - *ts.activation_length, end);
            scene.fills.push_back(f);  // state is filled in from the path below
        }
    }
    if (in_solo)
        scene.solos.push_back(span_from_ticks(song, solo_start, solo_last));

    scene.has_notes = !scene.notes.empty();
    if (scene.has_notes) scene.song_length_ms = scene.notes.back().ms;

    // The beat grid runs two measures past the last note so lines keep
    // scrolling through the look-ahead after the chart ends.
    const SongTiming& timing = song.timing();
    scene.timing = timing;  // the time box names ticks with the engine's math
    scene.tick_resolution = timing.tick_resolution();
    if (scene.has_notes) {
        int64_t last_tick = scene.notes.back().tick;
        const MeasureIndex& mi = timing.measure_index();
        int64_t tpm = mi.tpm_at(mi.section_at(last_tick));
        scene.beats = build_beat_events(timing, last_tick + 2 * tpm);
    }
    for (const auto& kv : song.bpm_changes) {
        PreviewTempo t;
        t.tick = kv.first;
        t.ms = timing.ms_index().at(kv.first);
        t.bpm = kv.second;
        scene.tempos.push_back(t);
    }
    {
        const MeasureIndex& mi = timing.measure_index();
        for (int i = 0; i < mi.count(); ++i) {
            if (mi.tpm_at(i) <= 0) continue;
            scene.meters.push_back({mi.keys_at(i), mi.tpm_at(i), mi.starts_at(i)});
        }
    }
    for (const auto& [tick, sig] : song.timesig_changes)
        scene.time_sigs.push_back({tick, sig.first, sig.second});
    // A section marker can sit past the last note, where the ms index does not
    // reach; the timing's own timecode extrapolates instead.
    for (const SongSection& s : song.practice_sections)
        scene.sections.push_back({s.tick, song.timecode(s.tick).ms(), s.name});

    // Overlay: the path's activations, ms resolved against the song's timing.
    if (path != nullptr) {
        for (const Activation& a : path->walk_activations()) {
            PreviewActivation pa;
            pa.tick = a.timecode.ticks();
            pa.ms = song.timecode(pa.tick).ms();
            pa.sp_meter = a.sp_meter;
            pa.skips = a.skips;
            pa.collected_phrase_ticks = a.collected_phrase_ticks;
            if (std::optional<int64_t> d = activation_deact_tick(a)) {
                pa.has_sp_end = true;
                pa.sp_end_tick = *d;
                pa.sp_end_ms = timing.ms_index().at(*d);
            }
            if (a.chord.count() > 0) {
                pa.has_lane = true;
                pa.lane = lane_of(a.chord.activation_note().colortype);
            }
            scene.activations.push_back(pa);
        }
    }

    // Which fills the game would have shown. Without a path we cannot know, so
    // every candidate is drawn as offered.
    if (path == nullptr) {
        for (PreviewFill& f : scene.fills) f.state = PreviewFillState::Offered;
    } else {
        // `skips` counts the real opportunities the path passed over right
        // before an activation: the search charges a skip only when the player
        // had enough SP and the deadline still allowed it, and resets the count
        // at each activation. So for an activation with skips == n, the n
        // candidates nearest before it (and after the previous activation) were
        // offered, its own candidate was taken, and every other candidate was
        // hidden — the game would not have shown it. Candidates after the last
        // activation stay hidden: the engine records nothing about them.
        int64_t prev_tick = std::numeric_limits<int64_t>::min();
        for (const PreviewActivation& a : scene.activations) {
            for (PreviewFill& f : scene.fills)
                if (f.span.end_tick == a.tick) {
                    f.state = PreviewFillState::Taken;
                    break;
                }
            int left = a.skips;
            for (auto it = scene.fills.rbegin(); it != scene.fills.rend() && left > 0; ++it) {
                if (it->span.end_tick >= a.tick || it->span.end_tick <= prev_tick) continue;
                it->state = PreviewFillState::Offered;
                --left;
            }
            prev_tick = a.tick;
        }
    }

    // The SP meter, built last: it reads the phrases, activations, tempos and
    // meters gathered above.
    scene.score = build_score(song, path, rules);
    scene.sp_meter = build_sp_meter_curve(scene, timing, sp_cap);
    return scene;
}

double sp_meter_bars_at(const SpMeterCurve& curve, double ms) {
    if (curve.segments.empty()) return 0.0;
    if (ms < curve.segments.front().start_ms) return 0.0;
    if (ms >= curve.segments.back().end_ms) return curve.segments.back().end_bars;
    // The last segment that has started: on a boundary two segments share, that
    // is the later one, which is what makes a step read as a step.
    auto it = std::upper_bound(curve.segments.begin(), curve.segments.end(), ms,
                               [](double v, const SpMeterSegment& s) { return v < s.start_ms; });
    const SpMeterSegment& s = *(it - 1);
    const double span = s.end_ms - s.start_ms;
    if (span <= 0.0) return s.end_bars;
    const double t = (ms - s.start_ms) / span;
    return s.start_bars + (s.end_bars - s.start_bars) * t;
}

std::vector<PreviewBeat> build_beat_events(const SongTiming& timing, int64_t last_tick) {
    std::vector<PreviewBeat> lines;
    const MeasureIndex& mi = timing.measure_index();
    const int64_t tick_r = timing.tick_resolution();
    for (int i = 0; i < mi.count(); ++i) {
        const int64_t section_start = mi.keys_at(i);
        const int64_t tpm = mi.tpm_at(i);
        if (tpm <= 0) continue;
        // The section's bars count from its last barline at or before its
        // first tick (a mid-measure meter change restarts the measure there).
        // Bars before the section's own start belong to the section before.
        const bool last_section = i + 1 >= mi.count();
        const int64_t section_end = last_section ? last_tick : mi.keys_at(i + 1);
        for (int64_t bar = mi.starts_at(i);; bar += tpm) {
            if (last_section ? bar > section_end : bar >= section_end) break;
            if (bar >= section_start) lines.push_back({bar, 0.0, PreviewBeatKind::Bar});
            for (int64_t beat = bar + tick_r; beat < bar + tpm; beat += tick_r) {
                if (last_section ? beat > section_end : beat >= section_end) break;
                if (beat >= section_start) lines.push_back({beat, 0.0, PreviewBeatKind::Beat});
            }
        }
    }
    // Onyx draws a fainter half-beat line half a beat before every bar/beat
    // line (all our lines are a full beat apart, so every one qualifies);
    // nothing is drawn before tick 0.
    std::vector<PreviewBeat> out;
    out.reserve(lines.size() * 2);
    const int64_t half = tick_r / 2;
    for (const PreviewBeat& l : lines) {
        if (l.tick - half >= 0) out.push_back({l.tick - half, 0.0, PreviewBeatKind::Half});
        out.push_back(l);
    }
    for (PreviewBeat& b : out) b.ms = timing.ms_index().at(b.tick);
    return out;
}

namespace {

std::string clock_str(double ms) {
    double secs = ms / 1000.0;
    int minutes = static_cast<int>(secs / 60.0);
    double rem = secs - minutes * 60.0;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%d:%06.3f", minutes, rem);
    return buf;
}

// The tick at `ms`, from the song's own ms index. The last tempo section's
// slope holds forever after it, so this keeps counting past the end of the
// chart. Rounded to the nearest tick and never negative.
int64_t tick_at(const SongTiming& timing, double ms) {
    const int64_t tick = std::llround(timing.ms_index().tick_at_ms(ms));
    return tick < 0 ? 0 : tick;
}

// Moonscraper's "[measure:beat:tick]". Timecode counts measures and beats from
// zero; the box shows both 1-based, with the tick still counted from the beat
// line, so the caller adds the two offsets.
std::string bracket_str(int64_t measure, int64_t beat, int64_t tick_in_beat) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "[%lld:%lld:%03lld]",
                  static_cast<long long>(measure),
                  static_cast<long long>(beat),
                  static_cast<long long>(tick_in_beat));
    return buf;
}

// The song length the time box shows: never negative.
double shown_length(double length_ms) { return length_ms < 0.0 ? 0.0 : length_ms; }

// The moment the time box shows: the playhead held inside [0, length].
// step_tick_ms starts from the same moment, so a step moves the displayed
// tick by exactly its size.
double shown_ms(double now_ms, double length_ms) {
    const double len = shown_length(length_ms);
    return now_ms < 0.0 ? 0.0 : (now_ms > len ? len : now_ms);
}

}  // namespace

PreviewTimeBox build_time_box(const PreviewScene& scene, double now_ms,
                              double length_ms) {
    PreviewTimeBox box;

    const double len = shown_length(length_ms);
    const double now = shown_ms(now_ms, length_ms);

    box.timestamp = clock_str(now) + " / " + clock_str(len);

    // A default-built scene carries no song and so no timing: it reads as tick
    // 0 at measure 1, beat 1, exactly as it always has.
    const int64_t now_tick = scene.timing ? tick_at(*scene.timing, now) : 0;
    const int64_t end_tick = scene.timing ? tick_at(*scene.timing, len) : 0;
    auto mbt_str = [&scene](int64_t tick) {
        if (!scene.timing) return bracket_str(1, 1, 0);
        const Timecode tc = scene.timing->timecode(tick);
        const int64_t* mbt = tc.measure_beats_ticks();
        return bracket_str(mbt[0] + 1, mbt[1] + 1, mbt[2]);
    };
    box.measure_beat = mbt_str(now_tick) + " / " + mbt_str(end_tick);

    // The tempo in force: the last change at or before now (the opening tempo
    // before any change).
    double bpm = scene.tempos.empty() ? 0.0 : scene.tempos.front().bpm;
    for (const PreviewTempo& t : scene.tempos) {
        if (t.ms > now) break;
        bpm = t.bpm;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "BPM: %.3f", bpm);
    box.bpm = buf;
    // The time signature in force at the playhead's tick, as the chart wrote
    // it; 4/4 before any, the chart default.
    int ts_num = 4, ts_den = 4;
    for (const PreviewTimeSig& t : scene.time_sigs) {
        if (t.tick > now_tick) break;
        ts_num = t.numerator;
        ts_den = t.denominator;
    }
    std::snprintf(buf, sizeof buf, "Time signature: %d/%d", ts_num, ts_den);
    box.time_sig = buf;

    for (const PreviewSection& s : scene.sections) {
        if (s.tick > now_tick) break;
        box.section = s.name;
    }
    return box;
}

PreviewScoreBox build_score_box(const PreviewScene& scene, double now_ms) {
    PreviewScoreBox box;
    if (scene.score.state == PreviewScore::State::None) return box;
    box.shown = true;
    if (scene.score.state == PreviewScore::State::Unavailable) {
        box.score = "Score unavailable";
        return box;
    }
    box.available = true;

    // The last chord at or before the playhead. Before the first chord
    // nothing is hit yet: 0, x1, combo 0.
    const std::vector<PreviewScoreStep>& steps = scene.score.steps;
    auto it = std::upper_bound(steps.begin(), steps.end(), now_ms,
                               [](double v, const PreviewScoreStep& s) { return v < s.ms; });
    PreviewScoreStep at;
    if (it != steps.begin()) at = *(it - 1);

    box.score = group_thousands(at.total);
    char buf[64];
    std::snprintf(buf, sizeof buf, "x%d \xC2\xB7 combo %d", at.multiplier, at.combo);
    box.detail = buf;
    return box;
}

PreviewDrainBox build_drain_box(const PreviewScene& scene, double now_ms) {
    PreviewDrainBox box;
    if (!scene.timing || scene.sp_meter.segments.empty()) return box;
    box.shown = true;

    const SongTiming& timing = *scene.timing;
    const double now = now_ms < 0.0 ? 0.0 : now_ms;
    const int64_t now_tick = tick_at(timing, now);

    // The rule's own constant at the local measure length: on a tempo or
    // meter change the new section is read, as the time box's BPM line does.
    char buf[64];
    const double bar_ms =
        static_cast<double>(kMeasuresPerSpBar) * timing.ms_per_measure_at(now_tick);
    std::snprintf(buf, sizeof buf, "1 bar / %.1f s", bar_ms / 1000.0);
    box.rate = buf;

    // SP is running when the playhead sits in an activation's stored window.
    // A record with no deact node cannot say, so it reads idle.
    const PreviewActivation* running = nullptr;
    for (const PreviewActivation& a : scene.activations) {
        if (a.has_sp_end && a.ms <= now && now < a.sp_end_ms) {
            running = &a;
            break;
        }
    }

    if (running != nullptr) {
        box.active = true;
        box.header = "SP drain";
        std::snprintf(buf, sizeof buf, "empties in %.1f s",
                      (running->sp_end_ms - now) / 1000.0);
    } else {
        box.header = "SP drain (if activated)";
        // The cap at the bar time in force here, so it jumps exactly when the
        // rate line does rather than blending in the sections ahead.
        std::snprintf(buf, sizeof buf, "full meter %.1f s",
                      bar_ms * static_cast<double>(scene.sp_meter.cap) / 1000.0);
    }
    box.detail = buf;
    return box;
}

double step_tick_ms(const PreviewScene& scene, double now_ms, double length_ms,
                    int delta_ticks) {
    if (!scene.timing) return now_ms;
    int64_t target = tick_at(*scene.timing, shown_ms(now_ms, length_ms)) + delta_ticks;
    if (target < 0) target = 0;
    return scene.timing->ms_index().at(target);
}

std::string path_overlay_key(const Path* path) {
    if (path == nullptr) return {};
    // The key only has to tell paths of one chart apart (the controller
    // compares it after matching the chart), and a chart's multiplier
    // squeezes are the same for every path, so they are left out.
    return path->pathstring_verbose({}) + "|" + std::to_string(path->totalscore());
}

}  // namespace hydra::app
