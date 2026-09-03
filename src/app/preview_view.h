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
#include <optional>
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
    // The deact node: where this activation's SP runs out, read off the
    // record (the search stamps it). The active SP window the highway tints
    // runs from `ms` to `sp_end_ms`. has_sp_end is false only for a record
    // written before blob v4, which does not carry the node.
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

// A practice section, for the time box's section readout.
struct PreviewSection {
    int64_t tick = 0;
    double ms = 0.0;
    std::string name;
};

// One meter section: where it begins, its ticks per measure, and the barline
// its bars count from — the beat grid's own rules, flattened for the parts of
// the Preview that draw or drain by measures.
struct PreviewMeter {
    int64_t tick = 0;
    int64_t tpm = 0;
    int64_t first_bar = 0;
};

// One straight stretch of the Star Power meter: the banked bars run linearly
// in ms from start_bars at start_ms to end_bars at end_ms.
//
// SP drains at one bar per two measures — linear in MEASURES, not in ms. Inside
// a single tempo section crossed with a single meter section, measures are
// linear in ms, so cutting the curve at every tempo change, meter change,
// phrase collection, activation and deact node makes it exactly piecewise
// linear in ms. Nothing here is an approximation.
struct SpMeterSegment {
    double start_ms = 0.0;
    double end_ms = 0.0;
    double start_bars = 0.0;
    double end_bars = 0.0;
};

// The meter over the whole chart: contiguous segments in time order, each
// one's end_ms the next one's start_ms. The meter's jumps — a phrase
// collecting, an activation spending the bank — are discontinuities BETWEEN
// two adjacent segments, not slopes inside one.
struct SpMeterCurve {
    std::vector<SpMeterSegment> segments;
    int cap = 4;  // the ceiling in bars: 4 in Clone Hero
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
    std::vector<PreviewSection> sections;  // practice sections, tick order
    std::vector<PreviewMeter> meters;      // meter sections, tick order
    // Banked SP over time, for the meter gauge. Empty when the chart has
    // neither SP phrases nor activations. Without a path there is nothing to
    // drain it, so it fills and then pins at the cap — deliberate: that is the
    // chart's own truth, and an unanalyzed chart has no activations to spend
    // the bank on. Phrases collected mid-SP are counted from the deact node
    // rather than from where they sit on the highway, so a squeezed-out phrase
    // steps the gauge the moment SP ends, not during the drain.
    SpMeterCurve sp_meter;
    // The song's own timing. The time box asks it for ms->tick and for
    // measure:beat:tick rather than re-deriving either from the flattened
    // tempo/meter lists above. Empty only on a default-built PreviewScene (no
    // song to read); build_preview_scene always fills it.
    std::optional<SongTiming> timing;
    int64_t tick_resolution = 0;       // ticks per quarter note
    double song_length_ms = 0.0;  // last note onset; the scrubber's right edge
    bool has_notes = false;
};

// The beat grid from the song's timing alone (no parser change): a Bar at
// every measure start, a Beat every quarter note inside the measure, and a
// Half one half-beat before each of those (never before tick 0). Covers ticks
// [0, last_tick]. ms from the timing's own ms index.
std::vector<PreviewBeat> build_beat_events(const SongTiming& timing, int64_t last_tick);

// The Preview's time box at `now_ms`, in Moonscraper's layout: the playhead
// time and the song length as "m:ss.mmm / m:ss.mmm", the same two points as
// 1-based "[measure:beat:tick]", the BPM in force, and the practice section in
// force. `section` is empty when the chart has none at or before the playhead;
// the box is three lines tall then.
struct PreviewTimeBox {
    std::string timestamp;
    std::string measure_beat;
    std::string bpm;
    std::string section;
};

PreviewTimeBox build_time_box(const PreviewScene& scene, double now_ms,
                              double length_ms);

// Bars banked at `ms`: 0 before the curve begins, its final value after the
// curve ends, and interpolated inside a segment. On a boundary shared by two
// segments the LATER one wins — that is how a collection step or an
// activation's snap reads as an instant jump rather than a ramp.
double sp_meter_bars_at(const SpMeterCurve& curve, double ms);

// Build the scene from a parsed song. `path` may be null (the chart is not
// analyzed yet): then `activations` is empty, every candidate fill reads
// Offered, and everything else is present, so the Preview works for any
// selected chart. When `path` is given, its activations (those carrying a
// timecode) become the overlay, their ms resolved against the song's own
// timing so they line up with the notes exactly, and each candidate fill is
// classified Hidden / Offered / Taken from the activations' skip counts.
//
// `sp_cap` is the SP meter's ceiling in bars — the viewed record's own sp_cap,
// which is 4 for any normal Clone Hero run and differs only on a what-if
// record analyzed at another cap. It scales the meter curve and nothing else;
// no note, span or fill in the scene depends on it.
PreviewScene build_preview_scene(const Song& song, const Path* path, int sp_cap = 4);

// Identity of the path an overlay was built from. Path has no operator==, so
// callers that must notice a changed selection compare these keys instead. A
// null path (no overlay) gives an empty key.
std::string path_overlay_key(const Path* path);

}  // namespace hydra::app

#endif  // HYDRA_APP_PREVIEW_VIEW_H
