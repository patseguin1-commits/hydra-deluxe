// Preview view-model — the note highway as plain data, built here so the
// derivation is testable without a Direct3D frame or an audio device.
//
// The Preview tab renders a chart's notes — at the difficulty and drum mode
// the library's View row selects — as a scrolling 3D highway (see
// docs/adr/0005). This module turns a parsed Song (and, when the
// chart has been analyzed, the selected Path) into a PreviewScene: notes with
// their lane and drum attributes at resolved ms/measure positions, plus the
// spans the highway shades — SP phrases, solos, and activation fills — and the
// path overlay's activation moments. The renderer and the transport UI consume
// this; they compute no timing of their own.
//
// Everything here derives from one SongTiming (the song's own), so every ms in
// a scene shares the engine's timing truth and cannot drift between notes and
// overlay. Follows the path_view.h pattern: resolve the facts once, render dumb.

#ifndef HYDRA_APP_PREVIEW_VIEW_H
#define HYDRA_APP_PREVIEW_VIEW_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/timing.h"
#include "parse/song.h"

namespace hydra::app {

// The five drum lanes, in KRYBG order, matching core NoteColor. A dedicated
// enum keeps the renderer off the parser's NoteColor (whose values start at 1).
enum class PreviewLane { Kick = 0, Red = 1, Yellow = 2, Blue = 3, Green = 4 };

PreviewLane lane_of(NoteColor color);

// One drawn note. A chord at a tick expands to one PreviewNote per struck lane.
struct PreviewNote {
    int64_t tick = 0;
    double ms = 0.0;       // onset, from the song's ms index
    double measure = 0.0;  // decimal measure position (for barlines/labels)
    PreviewLane lane = PreviewLane::Kick;
    bool cymbal = false;       // pro-drums cymbal (Yellow/Blue/Green only)
    bool ghost = false;        // dynamics: quiet
    bool accent = false;       // dynamics: loud
    bool double_kick = false;  // the 2x-kick note
    bool solo = false;         // falls inside a solo section
};

// A shaded time span on the highway: SP phrase, solo, or activation fill. Both
// tick and ms endpoints are carried so the renderer can place it by whichever
// axis it scrolls on.
struct PreviewSpan {
    int64_t start_tick = 0;
    int64_t end_tick = 0;
    double start_ms = 0.0;
    double end_ms = 0.0;
};

// What the game would have done with one candidate activation fill, read off
// the path's own skip counts (see build_preview_scene):
//   Hidden  — never shown: not enough SP banked, or SP was already running.
//   Offered — shown and passed over: the path could have activated here.
//   Taken   — the fill the path activates on.
enum class PreviewFillState { Hidden, Offered, Taken };

// One candidate activation fill and what the path did with it.
struct PreviewFill {
    PreviewSpan span;
    PreviewFillState state = PreviewFillState::Hidden;
};

// One activation the selected path takes: where the player deploys banked SP.
struct PreviewActivation {
    int64_t tick = 0;
    double ms = 0.0;
    int sp_meter = 0;  // bars of SP spent, 0 when the record did not record it
    int skips = 0;     // fills passed over before this activation, 0 if unknown
    // The deact node: where this activation's SP runs out. The active SP
    // window the highway tints runs from `ms` to `sp_end_ms`. has_sp_end is
    // false when the record cannot say (no timecode/sp_meter).
    bool has_sp_end = false;
    int64_t sp_end_tick = 0;
    double sp_end_ms = 0.0;
    // The activation note's lane (the chord's highest-priority note, Green
    // first): the lane the activated fill lights. Kick when the record has no
    // chord.
    PreviewLane lane = PreviewLane::Kick;
    bool has_lane = false;
};

// A beat line on the highway: a bar line, a beat line, or the fainter
// half-beat line drawn half a beat before each of those (Onyx line-1/2/3).
enum class PreviewBeatKind { Bar, Beat, Half };

struct PreviewBeat {
    int64_t tick = 0;
    double ms = 0.0;
    PreviewBeatKind kind = PreviewBeatKind::Beat;
};

// A tempo change, for the time box's BPM readout.
struct PreviewTempo {
    int64_t tick = 0;
    double ms = 0.0;
    double bpm = 0.0;
};

// The whole chart as the Preview draws it. Notes are in tick order. Spans are
// in start order and do not overlap within their own list.
struct PreviewScene {
    std::vector<PreviewNote> notes;
    std::vector<PreviewSpan> sp_phrases;    // from the song's SP-phrase flags
    std::vector<PreviewSpan> solos;         // from per-note solo flags
    std::vector<PreviewFill> fills;         // candidate activation-fill windows
    std::vector<PreviewActivation> activations;  // overlay: the path's activations
    std::vector<PreviewBeat> beats;    // bar/beat/half-beat lines, tick order
    std::vector<PreviewTempo> tempos;  // tempo changes, tick order
    int64_t tick_resolution = 0;       // ticks per quarter note
    double song_length_ms = 0.0;  // last note onset; the scrubber's right edge
    bool has_notes = false;
};

// The beat grid from the song's timing alone (no parser change): a Bar at
// every measure start, a Beat every quarter note inside the measure, and a
// Half one half-beat before each of those (never before tick 0). Covers ticks
// [0, last_tick]. ms from the timing's own ms index.
std::vector<PreviewBeat> build_beat_events(const SongTiming& timing, int64_t last_tick);

// The three lines of the Preview's time box at `now_ms`: "m:ss.t", the
// 1-based "measure:beat" from the beat grid, and the BPM in force.
struct PreviewTimeBox {
    std::string timestamp;
    std::string measure_beat;
    std::string bpm;
};

PreviewTimeBox build_time_box(const PreviewScene& scene, double now_ms);

// Build the scene from a parsed song. `path` may be null (the chart is not
// analyzed yet): then `activations` is empty, every candidate fill reads
// Offered, and everything else is present, so the Preview works for any
// selected chart. When `path` is given, its activations (those carrying a
// timecode) become the overlay, their ms resolved against the song's own
// timing so they line up with the notes exactly, and each candidate fill is
// classified Hidden / Offered / Taken from the activations' skip counts.
PreviewScene build_preview_scene(const Song& song, const Path* path);

// Identity of the path an overlay was built from. Path has no operator==, so
// callers that must notice a changed selection compare these keys instead. A
// null path (no overlay) gives an empty key.
std::string path_overlay_key(const Path* path);

}  // namespace hydra::app

#endif  // HYDRA_APP_PREVIEW_VIEW_H
