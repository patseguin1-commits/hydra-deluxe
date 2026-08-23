// See preview_view.h. Everything here reads the song's own SongTiming, so the
// notes and the path overlay share one ms truth and never drift apart.

#include "app/preview_view.h"

#include <cstdio>
#include <optional>

#include "core/squeeze_rating.h"

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

}  // namespace

PreviewScene build_preview_scene(const Song& song, const Path* path) {
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
            scene.fills.push_back(span_from_ticks(song, end - *ts.activation_length, end));
        }
    }
    if (in_solo)
        scene.solos.push_back(span_from_ticks(song, solo_start, solo_last));

    scene.has_notes = !scene.notes.empty();
    if (scene.has_notes) scene.song_length_ms = scene.notes.back().ms;

    // The beat grid runs two measures past the last note so lines keep
    // scrolling through the look-ahead after the chart ends.
    const SongTiming& timing = song.timing();
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

    // Overlay: the path's activations, ms resolved against the song's timing.
    if (path != nullptr) {
        for (const Activation& a : path->all_activations()) {
            if (!a.timecode.has_value()) continue;
            PreviewActivation pa;
            pa.tick = a.timecode->ticks();
            pa.ms = song.timecode(pa.tick).ms();
            pa.sp_meter = a.sp_meter.value_or(0);
            pa.skips = a.skips.value_or(0);
            if (std::optional<int64_t> d = activation_deact_tick(a, timing)) {
                pa.has_sp_end = true;
                pa.sp_end_tick = *d;
                pa.sp_end_ms = timing.ms_index().at(*d);
            }
            if (a.chord.has_value() && !a.chord->notes().empty()) {
                pa.has_lane = true;
                pa.lane = lane_of(a.chord->activation_note().colortype);
            }
            scene.activations.push_back(pa);
        }
    }
    return scene;
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

PreviewTimeBox build_time_box(const PreviewScene& scene, double now_ms) {
    PreviewTimeBox box;
    char buf[64];

    double secs = now_ms < 0.0 ? 0.0 : now_ms / 1000.0;
    int minutes = static_cast<int>(secs / 60.0);
    double rem = secs - minutes * 60.0;
    std::snprintf(buf, sizeof buf, "%d:%04.1f", minutes, rem);
    box.timestamp = buf;

    // Count bars and the beats since the last bar up to now.
    int measure = 0, beat = 0;
    for (const PreviewBeat& b : scene.beats) {
        if (b.ms > now_ms) break;
        if (b.kind == PreviewBeatKind::Bar) {
            ++measure;
            beat = 1;
        } else if (b.kind == PreviewBeatKind::Beat) {
            ++beat;
        }
    }
    if (measure == 0) {
        measure = 1;
        beat = 1;
    }
    std::snprintf(buf, sizeof buf, "%d:%d", measure, beat);
    box.measure_beat = buf;

    // The tempo in force: the last change at or before now (the opening tempo
    // before any change).
    double bpm = scene.tempos.empty() ? 0.0 : scene.tempos.front().bpm;
    for (const PreviewTempo& t : scene.tempos) {
        if (t.ms > now_ms) break;
        bpm = t.bpm;
    }
    std::snprintf(buf, sizeof buf, "%.2f", bpm);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    box.bpm = s + " BPM";
    return box;
}

}  // namespace hydra::app
