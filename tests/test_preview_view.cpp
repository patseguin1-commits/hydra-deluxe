// Tests for app/preview_view: the note-highway view-model the Preview tab
// renders. A hand-built song pins the note/lane/attribute mapping and the
// shaded spans exactly; the corpus cases confirm the SP-phrase-start the parser
// now keeps, and that an analyzed chart's overlay lines up with its path.

#include "doctest.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "app/preview_view.h"
#include "core/squeeze_rating.h"
#include "core/timing.h"  // sp_bars_to_measures
#include "corpus_util.h"
#include "parse/song.h"
#include "search/engine.h"
#include "search/graph.h"

using namespace hydra;
using namespace hydra::app;

namespace {

// A tiny 4/4, 120 BPM song built by hand: 480 ticks/quarter, so tick t is at
// t*500/480 ms and a measure is 1920 ticks. Four timestamps exercise every
// note attribute and every shaded-span source.
Song make_hand_song() {
    Song song(480);
    song.bpm_changes[0] = 120.0;  // tpm_changes[0] defaults to 1920 (4/4)
    song.build_timing();

    auto push = [&](int64_t tick, Chord chord) -> SongTimestamp& {
        SongTimestamp ts;
        ts.timecode = song.timecode(tick);
        ts.chord = std::move(chord);
        song.sequence.push_back(std::move(ts));
        return song.sequence.back();
    };

    // tick 0: a plain Red.
    {
        Chord c;
        c.add_note(NoteColor::Red);
        push(0, c);
    }
    // tick 240: a Yellow cymbal, ghost; start of a solo.
    {
        Chord c;
        c.add_note(NoteColor::Yellow);
        c.apply_cymbal(NoteColor::Yellow);
        c.apply_ghost(NoteColor::Yellow);
        push(240, c).flag_solo = true;
    }
    // tick 480: a Blue accent; still in the solo.
    {
        Chord c;
        c.add_note(NoteColor::Blue);
        c.apply_accent(NoteColor::Blue);
        push(480, c).flag_solo = true;
    }
    // tick 720: a 2x-kick + Green chord; ends an SP phrase that began at tick
    // 240, and sits at the end of an activation fill 480 ticks long.
    {
        Chord c;
        c.add_2x();  // adds the Kick note itself, with is2x set
        c.add_note(NoteColor::Green);
        SongTimestamp& ts = push(720, c);
        ts.flag_sp = true;
        ts.sp_phrase_start = 240;
        ts.activation_length = 480;
    }
    return song;
}

// A song with four candidate activation fills, each 240 ticks long, ending on
// the notes at ticks 480, 960, 1440 and 1920.
Song make_fill_song() {
    Song song(480);
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (int i = 1; i <= 4; ++i) {
        Chord c;
        c.add_note(NoteColor::Green);
        SongTimestamp ts;
        ts.timecode = song.timecode(480 * i);
        ts.chord = std::move(c);
        ts.activation_length = 240;
        song.sequence.push_back(std::move(ts));
    }
    return song;
}

Activation act_at(const Song& song, int64_t tick, int skips) {
    Activation a;
    a.timecode = song.timecode(tick);
    a.skips = skips;
    return a;
}

// ---- SP meter fixtures --------------------------------------------------
//
// Same 4/4, 120 BPM grid as make_hand_song: a note every `step` ticks out to
// `last_tick`, with an SP phrase ending on each tick in `phrase_ends`. One
// measure is 1920 ticks = 2000 ms, so one SP bar (two measures) burns 4000 ms
// at this tempo. `extra_bpm` adds tempo changes before the timing is built.
// A phrase can only end on a note, so `step` is there for a fixture that needs
// a phrase off the quarter-note grid.
Song make_sp_song(const std::vector<int64_t>& phrase_ends, int64_t last_tick,
                  const std::map<int64_t, double>& extra_bpm = {},
                  int64_t step = 480) {
    Song song(480);
    song.bpm_changes[0] = 120.0;
    for (const auto& kv : extra_bpm) song.bpm_changes[kv.first] = kv.second;
    song.build_timing();

    for (int64_t t = 0; t <= last_tick; t += step) {
        Chord c;
        c.add_note(NoteColor::Red);
        SongTimestamp ts;
        ts.timecode = song.timecode(t);
        ts.chord = std::move(c);
        if (std::find(phrase_ends.begin(), phrase_ends.end(), t) != phrase_ends.end()) {
            ts.flag_sp = true;
            ts.sp_phrase_start = t >= step ? t - step : 0;
        }
        song.sequence.push_back(std::move(ts));
    }
    return song;
}

// An activation the engine could have recorded: a timecode, the bars it
// spends, and the deactivation node the search stamped on it. Nothing derives
// that node any more, so the fixture has to state it. The default is the plain
// act + 2*sp_meter measures — an activation that collects no phrase mid-SP.
// A fixture that collects one overwrites `deact_tick` and sets `collected_phrase_ticks` itself.
Activation sp_act_at(const Song& song, int64_t tick, int sp_meter) {
    Activation a;
    a.timecode = song.timecode(tick);
    a.sp_meter = sp_meter;
    a.skips = 0;
    a.deact_tick =
        song.timing().plusmeasure(*a.timecode, sp_bars_to_measures(sp_meter)).ticks();
    return a;
}

// The curve must tile the timeline with no gap and no overlap, and never leave
// the 0..cap band (every fixture here spends no more than the cap).
void check_curve_well_formed(const SpMeterCurve& curve) {
    for (size_t i = 1; i < curve.segments.size(); ++i)
        CHECK(curve.segments[i].start_ms == doctest::Approx(curve.segments[i - 1].end_ms));
    for (const SpMeterSegment& s : curve.segments) {
        CHECK(s.end_ms >= s.start_ms);
        CHECK(s.start_bars >= 0.0);
        CHECK(s.end_bars >= 0.0);
        CHECK(s.start_bars <= static_cast<double>(curve.cap) + 1e-9);
        CHECK(s.end_bars <= static_cast<double>(curve.cap) + 1e-9);
    }
}

// The engine fixture from test_search.cpp ("SP cap overfill: a second clamp
// in the same window replaces clamp_tick"), rebuilt with each phrase's start
// tick set so the Preview draws the phrases. 192 ticks per beat, 4/4, 120
// BPM: a measure is 768 ticks and 2000 ms.
Song make_overfill_song() {
    struct N { int64_t tick; bool phrase; bool fill; };
    const std::vector<N> notes = {{0, true, false},    {768, true, false},
                                  {2304, false, true}, {3072, true, false},
                                  {3840, true, false}, {4608, false, false},
                                  {5376, false, false}, {6000, false, false},
                                  {6768, false, false}, {7500, false, false}};
    Song song(192);
    song.tpm_changes[0] = 768;
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (const N& n : notes) {
        SongTimestamp ts;
        ts.timecode = song.timecode(n.tick);
        ts.chord.add_note(NoteColor::Red);
        ts.flag_sp = n.phrase;
        if (n.phrase) ts.sp_phrase_start = n.tick;
        if (n.fill) ts.activation_length = 384;
        song.sequence.push_back(ts);
    }
    return song;
}

// One analyzed corpus chart (the first that yields paths), shared across cases.
const AnalysisResult& analyzed() {
    static const AnalysisResult result = [] {
        AnalysisSettings settings;
        settings.depth_mode = DepthMode::Scores;
        settings.depth_value = 10;
        settings.ms_filter = 10.0;
        for (const std::string& path : corpus::chart_paths()) {
            try {
                AnalysisResult r = analyze_chart_file(path, settings);
                if (!r.song.is_empty() && !r.record.paths.empty()) return r;
            } catch (const std::exception&) {
                continue;
            }
        }
        throw std::runtime_error("no analyzable corpus chart");
    }();
    return result;
}

}  // namespace

TEST_CASE("build_preview_scene: notes carry lane and drum attributes") {
    Song song = make_hand_song();
    PreviewScene scene = build_preview_scene(song, nullptr);

    REQUIRE(scene.has_notes);
    REQUIRE(scene.notes.size() == 5);  // 1 + 1 + 1 + 2 (Kick+Green)

    const PreviewNote& red = scene.notes[0];
    CHECK(red.lane == PreviewLane::Red);
    CHECK(red.tick == 0);
    CHECK(red.ms == doctest::Approx(0.0));
    CHECK_FALSE(red.solo);

    const PreviewNote& yellow = scene.notes[1];
    CHECK(yellow.lane == PreviewLane::Yellow);
    CHECK(yellow.cymbal);
    CHECK(yellow.ghost);
    CHECK(yellow.solo);
    CHECK(yellow.ms == doctest::Approx(250.0));

    const PreviewNote& blue = scene.notes[2];
    CHECK(blue.lane == PreviewLane::Blue);
    CHECK(blue.accent);
    CHECK(blue.solo);

    // The chord at tick 720 expands to Kick (2x) then Green, in KRYBG order.
    CHECK(scene.notes[3].lane == PreviewLane::Kick);
    CHECK(scene.notes[3].double_kick);
    CHECK(scene.notes[4].lane == PreviewLane::Green);

    CHECK(scene.song_length_ms == doctest::Approx(750.0));
}

TEST_CASE("build_preview_scene: SP phrase, solo, and fill spans") {
    Song song = make_hand_song();
    PreviewScene scene = build_preview_scene(song, nullptr);

    REQUIRE(scene.sp_phrases.size() == 1);
    CHECK(scene.sp_phrases[0].start_tick == 240);
    CHECK(scene.sp_phrases[0].end_tick == 720);
    CHECK(scene.sp_phrases[0].start_ms == doctest::Approx(250.0));
    CHECK(scene.sp_phrases[0].end_ms == doctest::Approx(750.0));

    REQUIRE(scene.solos.size() == 1);
    CHECK(scene.solos[0].start_tick == 240);
    CHECK(scene.solos[0].end_tick == 480);

    REQUIRE(scene.fills.size() == 1);
    CHECK(scene.fills[0].span.start_tick == 240);  // 720 - 480
    CHECK(scene.fills[0].span.end_tick == 720);
}

TEST_CASE("build_preview_scene: an unanalyzed chart offers every candidate fill") {
    Song song = make_fill_song();
    PreviewScene scene = build_preview_scene(song, nullptr);
    REQUIRE(scene.fills.size() == 4);
    for (const PreviewFill& f : scene.fills) CHECK(f.state == PreviewFillState::Offered);
}

TEST_CASE("build_preview_scene: skips say which fills the path was offered") {
    Song song = make_fill_song();
    Path path;
    // The path activates on the third fill after passing over one before it.
    path.activations = {act_at(song, 1440, 1)};

    PreviewScene scene = build_preview_scene(song, &path);
    REQUIRE(scene.fills.size() == 4);
    CHECK(scene.fills[0].state == PreviewFillState::Hidden);   // not enough SP
    CHECK(scene.fills[1].state == PreviewFillState::Offered);  // the one skip
    CHECK(scene.fills[2].state == PreviewFillState::Taken);
    CHECK(scene.fills[3].state == PreviewFillState::Hidden);   // past the last act
    CHECK(scene.fills[2].span.end_tick == 1440);
}

TEST_CASE("build_preview_scene: a second activation with skips 0 hides what lies between") {
    Song song = make_fill_song();
    Path path;
    path.activations = {act_at(song, 960, 0), act_at(song, 1920, 0)};

    PreviewScene scene = build_preview_scene(song, &path);
    REQUIRE(scene.fills.size() == 4);
    CHECK(scene.fills[0].state == PreviewFillState::Hidden);
    CHECK(scene.fills[1].state == PreviewFillState::Taken);
    CHECK(scene.fills[2].state == PreviewFillState::Hidden);  // SP was still active
    CHECK(scene.fills[3].state == PreviewFillState::Taken);

    // With one skip charged to the second activation, the fill between them is
    // offered instead.
    Path skipped;
    skipped.activations = {act_at(song, 960, 0), act_at(song, 1920, 1)};
    PreviewScene s2 = build_preview_scene(song, &skipped);
    CHECK(s2.fills[1].state == PreviewFillState::Taken);
    CHECK(s2.fills[2].state == PreviewFillState::Offered);
    CHECK(s2.fills[3].state == PreviewFillState::Taken);
}

TEST_CASE("build_beat_events: bars, beats, and half-beats from the timing alone") {
    // 4/4 at 480 ticks/quarter: bars every 1920, beats every 480.
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    SongTiming st(480, tpm, bpm);

    std::vector<PreviewBeat> beats = build_beat_events(st, 1920);
    // ticks: 0 Bar, 240 Half, 480 Beat, 720 Half, 960 Beat, 1200 Half,
    // 1440 Beat, 1680 Half, 1920 Bar  (no Half before tick 0)
    REQUIRE(beats.size() == 9);
    CHECK(beats[0].tick == 0);
    CHECK(beats[0].kind == PreviewBeatKind::Bar);
    CHECK(beats[1].tick == 240);
    CHECK(beats[1].kind == PreviewBeatKind::Half);
    CHECK(beats[2].tick == 480);
    CHECK(beats[2].kind == PreviewBeatKind::Beat);
    CHECK(beats[6].tick == 1440);
    CHECK(beats[6].kind == PreviewBeatKind::Beat);
    CHECK(beats[8].tick == 1920);
    CHECK(beats[8].kind == PreviewBeatKind::Bar);
    CHECK(beats[8].ms == doctest::Approx(2000.0));
    for (size_t i = 1; i < beats.size(); ++i) CHECK(beats[i].tick > beats[i - 1].tick);
}

TEST_CASE("build_beat_events: a 3/4 section changes the beat count per bar") {
    // One bar of 4/4, then 3/4 (1440 ticks per measure).
    std::map<int64_t, int64_t> tpm{{0, 1920}, {1920, 1440}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    SongTiming st(480, tpm, bpm);

    std::vector<PreviewBeat> beats = build_beat_events(st, 1920 + 1440 * 2);
    int bars = 0, quarter_beats = 0;
    for (const PreviewBeat& b : beats) {
        if (b.kind == PreviewBeatKind::Bar) ++bars;
        if (b.kind == PreviewBeatKind::Beat) ++quarter_beats;
    }
    CHECK(bars == 4);              // ticks 0, 1920, 3360, 4800
    CHECK(quarter_beats == 3 + 2 + 2);  // 3 in the 4/4 bar, 2 in each 3/4 bar
    // Every Beat line lies strictly inside its bar.
    bool saw_3360 = false;
    for (const PreviewBeat& b : beats)
        if (b.tick == 3360) saw_3360 = b.kind == PreviewBeatKind::Bar;
    CHECK(saw_3360);
}

TEST_CASE("build_preview_scene fills beats, tempos and resolution") {
    Song song = make_hand_song();
    PreviewScene scene = build_preview_scene(song, nullptr);
    CHECK(scene.tick_resolution == 480);
    REQUIRE(scene.tempos.size() == 1);
    CHECK(scene.tempos[0].bpm == doctest::Approx(120.0));
    REQUIRE(!scene.beats.empty());
    CHECK(scene.beats.front().tick == 0);
    // Extends two measures past the last note (tick 720 -> through 4560; the
    // last line at or before that is the beat at 4320).
    CHECK(scene.beats.back().tick == 4320);
}

TEST_CASE("build_time_box: timestamp, measure:beat:tick, BPM") {
    Song song = make_hand_song();
    PreviewScene scene = build_preview_scene(song, nullptr);

    // 1300 ms is tick 1248: bar 1, third beat, 288 ticks into it. 5000 ms is
    // tick 4800: the third bar's third beat, exactly on the line.
    PreviewTimeBox box = build_time_box(scene, 1300.0, 5000.0);
    CHECK(box.timestamp == "0:01.300 / 0:05.000");
    CHECK(box.measure_beat == "[1:3:288] / [3:3:000]");
    CHECK(box.bpm == "BPM: 120.000");
    CHECK(box.section.empty());

    CHECK(build_time_box(scene, 0.0, 5000.0).measure_beat == "[1:1:000] / [3:3:000]");

    PreviewTimeBox later = build_time_box(scene, 64000.0, 64000.0);
    CHECK(later.timestamp == "1:04.000 / 1:04.000");

    // The playhead is clamped to the length.
    CHECK(build_time_box(scene, 9999.0, 5000.0).timestamp == "0:05.000 / 0:05.000");
}

TEST_CASE("build_time_box: the end bracket runs past the last beat line") {
    Song song = make_hand_song();
    PreviewScene scene = build_preview_scene(song, nullptr);
    // The beat grid stops at tick 4320, but the last tempo and meter hold
    // forever: 30 s is tick 28800, which is bar 16 on the nose.
    REQUIRE(scene.beats.back().tick == 4320);
    PreviewTimeBox box = build_time_box(scene, 30000.0, 30000.0);
    CHECK(box.measure_beat == "[16:1:000] / [16:1:000]");
    CHECK(box.timestamp == "0:30.000 / 0:30.000");
}

TEST_CASE("build_time_box: the practice section in force") {
    Song song = make_hand_song();
    song.practice_sections.push_back({480, "Verse 1"});
    song.practice_sections.push_back({2400, "Chorus"});
    PreviewScene scene = build_preview_scene(song, nullptr);

    REQUIRE(scene.sections.size() == 2);
    CHECK(scene.sections[0].tick == 480);
    CHECK(scene.sections[0].ms == doctest::Approx(500.0));
    CHECK(scene.sections[0].name == "Verse 1");
    // Past the last note (tick 720), where the ms index has no entry.
    CHECK(scene.sections[1].ms == doctest::Approx(2500.0));

    const double len = 10000.0;
    CHECK(build_time_box(scene, 0.0, len).section.empty());
    CHECK(build_time_box(scene, 400.0, len).section.empty());
    CHECK(build_time_box(scene, 500.0, len).section == "Verse 1");
    CHECK(build_time_box(scene, 2000.0, len).section == "Verse 1");
    CHECK(build_time_box(scene, 2500.0, len).section == "Chorus");
    CHECK(build_time_box(scene, 9000.0, len).section == "Chorus");
}

TEST_CASE("build_time_box: a mid-measure meter change follows the engine") {
    // 4/4 from tick 0, 3/4 from tick 2880 — beat 3 of the second bar, not a
    // barline. A section's bars count from the last barline at or before its
    // first tick (tick 1920 here), so the bar lines run 0, 1920, 3360, 4800 and
    // the second bar is cut short. The box must name ticks the way the engine
    // does rather than by a count of its own.
    Song song(480);
    song.bpm_changes[0] = 120.0;
    song.tpm_changes[2880] = 1440;
    song.build_timing();
    {
        Chord c;
        c.add_note(NoteColor::Red);
        SongTimestamp ts;
        ts.timecode = song.timecode(3600);
        ts.chord = std::move(c);
        song.sequence.push_back(std::move(ts));
    }
    PreviewScene scene = build_preview_scene(song, nullptr);

    // 120 BPM at 480 ticks/quarter: tick 3600 is 3750 ms, past the change.
    const Timecode tc = song.timing().timecode(3600);
    const int64_t* mbt = tc.measure_beats_ticks();
    CHECK(mbt[0] + 1 == 3);  // the engine's measure, 1-based for display
    CHECK(mbt[1] + 1 == 1);
    CHECK(mbt[2] == 240);

    PreviewTimeBox box = build_time_box(scene, 3750.0, 3750.0);
    CHECK(box.measure_beat == "[3:1:240] / [3:1:240]");

    // ...and that measure is the one the drawn bar lines put tick 3600 in: the
    // third line is at 3360 and the fourth at 4800.
    std::vector<int64_t> bars;
    for (const PreviewBeat& b : scene.beats)
        if (b.kind == PreviewBeatKind::Bar) bars.push_back(b.tick);
    REQUIRE(bars.size() >= 4);
    CHECK(bars[1] == 1920);
    CHECK(bars[2] == 3360);
    CHECK(bars[3] == 4800);
}

TEST_CASE("build_preview_scene: no path means no overlay") {
    Song song = make_hand_song();
    PreviewScene scene = build_preview_scene(song, nullptr);
    CHECK(scene.activations.empty());
}

TEST_CASE("parser keeps the SP-phrase start on the flagged note") {
    // Find the first corpus chart with an SP phrase and confirm every note the
    // parser flags as an SP-phrase end now also carries its start tick.
    bool checked_a_chart = false;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, /*pro=*/true, /*bass2x=*/true);
        int sp_notes = 0;
        for (const SongTimestamp& ts : song.sequence) {
            if (!ts.flag_sp) continue;
            ++sp_notes;
            REQUIRE(ts.sp_phrase_start.has_value());
            CHECK(*ts.sp_phrase_start <= ts.timecode.ticks());
        }
        if (sp_notes > 0) {
            checked_a_chart = true;
            break;
        }
    }
    REQUIRE(checked_a_chart);  // the corpus must contain an SP-bearing chart
}

TEST_CASE("build_preview_scene: an analyzed chart's overlay matches its path") {
    const AnalysisResult& r = analyzed();
    const Path& best = r.record.best_path();
    PreviewScene scene = build_preview_scene(r.song, &best);

    REQUIRE(scene.has_notes);
    REQUIRE_FALSE(scene.notes.empty());
    CHECK(scene.song_length_ms > 0.0);

    // Notes are in non-decreasing tick order.
    for (size_t i = 1; i < scene.notes.size(); ++i)
        CHECK(scene.notes[i].tick >= scene.notes[i - 1].tick);

    // The scrubber's right edge is the last note's onset.
    CHECK(scene.song_length_ms == doctest::Approx(scene.notes.back().ms));

    // Every activation the path takes (those with a resolved timecode) appears
    // in the overlay, inside the song, in ms that share the notes' timing.
    size_t expected = 0;
    for (const Activation& a : best.all_activations())
        if (a.timecode.has_value()) ++expected;
    CHECK(scene.activations.size() == expected);
    size_t i = 0;
    for (const Activation& a : best.all_activations()) {
        if (!a.timecode.has_value()) continue;
        const PreviewActivation& pa = scene.activations[i++];
        CHECK(pa.tick >= 0);
        CHECK(pa.ms >= 0.0);
        CHECK(pa.ms <= scene.song_length_ms + 1.0);
        // The active SP window ends exactly at the deact node the record
        // carries, in the song's own ms. The engine stamps that node on every
        // activation it produces, so it is always there on a fresh record.
        std::optional<int64_t> d = activation_deact_tick(a);
        CHECK(d.has_value());
        CHECK(pa.has_sp_end == d.has_value());
        if (d) {
            CHECK(pa.sp_end_tick == *d);
            CHECK(pa.sp_end_ms == doctest::Approx(r.song.timing().ms_index().at(*d)));
            CHECK(pa.sp_end_ms > pa.ms);
        }
        // The activation note's lane is the chord's highest-priority note.
        if (a.chord.has_value() && !a.chord->notes().empty()) {
            CHECK(pa.has_lane);
            CHECK(pa.lane == lane_of(a.chord->activation_note().colortype));
        }
    }

    // Same song, no path: identical notes, empty overlay.
    PreviewScene bare = build_preview_scene(r.song, nullptr);
    CHECK(bare.notes.size() == scene.notes.size());
    CHECK(bare.activations.empty());
}

TEST_CASE("sp meter curve: each phrase's last note banks one bar") {
    // Phrases ending at ticks 960 and 2880, i.e. 1000 ms and 3000 ms.
    Song song = make_sp_song({960, 2880}, /*last_tick=*/5760);
    PreviewScene scene = build_preview_scene(song, nullptr);
    const SpMeterCurve& c = scene.sp_meter;

    REQUIRE_FALSE(c.segments.empty());
    check_curve_well_formed(c);
    CHECK(c.cap == 4);

    CHECK(sp_meter_bars_at(c, 0.0) == doctest::Approx(0.0));
    CHECK(sp_meter_bars_at(c, 999.0) == doctest::Approx(0.0));
    // The step lands ON the phrase's last note: the later segment owns the
    // shared boundary, so the bar is already banked at exactly 1000 ms.
    CHECK(sp_meter_bars_at(c, 1000.0) == doctest::Approx(1.0));
    CHECK(sp_meter_bars_at(c, 1000.001) == doctest::Approx(1.0));
    CHECK(sp_meter_bars_at(c, 2999.0) == doctest::Approx(1.0));
    CHECK(sp_meter_bars_at(c, 3000.0) == doctest::Approx(2.0));
    CHECK(sp_meter_bars_at(c, 6000.0) == doctest::Approx(2.0));
}

TEST_CASE("sp meter curve: an activation snaps to the recorded bars, then drains") {
    // One phrase banked (1000 ms), but the record says the activation at tick
    // 3840 (4000 ms) spent two bars. The record wins.
    Song song = make_sp_song({960}, /*last_tick=*/13440);
    Path path;
    path.activations = {sp_act_at(song, 3840, /*sp_meter=*/2)};

    PreviewScene scene = build_preview_scene(song, &path);
    const SpMeterCurve& c = scene.sp_meter;
    check_curve_well_formed(c);

    // Two bars is four measures: 8000 ms at 120 BPM 4/4, ending at tick 11520.
    REQUIRE(scene.activations.size() == 1);
    CHECK(scene.activations[0].has_sp_end);
    CHECK(scene.activations[0].sp_end_tick == 11520);
    CHECK(scene.activations[0].sp_end_ms == doctest::Approx(12000.0));

    CHECK(sp_meter_bars_at(c, 3999.0) == doctest::Approx(1.0));  // the phrase count
    CHECK(sp_meter_bars_at(c, 4000.0) == doctest::Approx(2.0));  // the record's bars
    // One bar's worth of drain is two measures = 4000 ms.
    CHECK(sp_meter_bars_at(c, 8000.0) == doctest::Approx(1.0));
    CHECK(sp_meter_bars_at(c, 10000.0) == doctest::Approx(0.5));
    CHECK(sp_meter_bars_at(c, 12000.0) == doctest::Approx(0.0));
    CHECK(sp_meter_bars_at(c, 14000.0) == doctest::Approx(0.0));
}

TEST_CASE("sp meter curve: a phrase collected mid-activation jumps the meter a bar") {
    // Phrases at 1000 ms and 6000 ms; the second lands inside the activation's
    // window. The record's deact node is what says it was collected during SP:
    // it sits two measures past the plain end, so the window runs six measures
    // instead of four and the meter steps up a bar at the phrase.
    Song song = make_sp_song({960, 5760}, /*last_tick=*/17280);
    Path path;
    Activation act = sp_act_at(song, 3840, /*sp_meter=*/2);
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 0.0});
    act.deact_tick = 3840 + 6 * 1920;  // 4 measures banked, 2 for the collection
    act.collected_phrase_ticks = {5760};  // the engine's record of that collection
    path.activations = {act};

    PreviewScene scene = build_preview_scene(song, &path);
    const SpMeterCurve& c = scene.sp_meter;
    check_curve_well_formed(c);

    REQUIRE(scene.activations.size() == 1);
    std::optional<int64_t> deact = activation_deact_tick(path.activations[0]);
    REQUIRE(deact.has_value());
    CHECK(*deact == 15360);  // 3840 + 6 measures
    CHECK(scene.activations[0].sp_end_ms == doctest::Approx(16000.0));

    // One bar per two measures = 0.25 bars per second here, before the jump...
    CHECK(sp_meter_bars_at(c, 4000.0) == doctest::Approx(2.0));
    CHECK(sp_meter_bars_at(c, 5000.0) == doctest::Approx(1.75));
    // Exactly one bar more at the phrase's last note than just before it.
    CHECK(sp_meter_bars_at(c, 6000.0 - 1e-6) == doctest::Approx(1.5));
    CHECK(sp_meter_bars_at(c, 6000.0) == doctest::Approx(2.5));
    // ...and the same rate after it, all the way to the extended deact node.
    CHECK(sp_meter_bars_at(c, 8000.0) == doctest::Approx(2.0));
    CHECK(sp_meter_bars_at(c, 12000.0) == doctest::Approx(1.0));
    CHECK(sp_meter_bars_at(c, 16000.0 - 1e-6) == doctest::Approx(0.0));
    CHECK(sp_meter_bars_at(c, 16000.0) == doctest::Approx(0.0));
}

TEST_CASE("sp meter curve: a squeezed-out phrase does not bank mid-drain") {
    // The phrase at tick 9600 (10000 ms) has its last note inside the
    // activation's window, but the deact node sits at the plain act + 4
    // measures: the engine records no extension, so nothing was collected
    // during SP. That phrase is the squeezed-out one -- hit late, just after
    // SP ends, and banked for the next activation. The drain must stay on its
    // plain line across it, and the bar must arrive when the window closes.
    Song song = make_sp_song({960, 9600}, /*last_tick=*/15360);
    Path path;
    path.activations = {sp_act_at(song, 3840, /*sp_meter=*/2)};

    PreviewScene scene = build_preview_scene(song, &path);
    const SpMeterCurve& c = scene.sp_meter;
    check_curve_well_formed(c);

    REQUIRE(scene.activations.size() == 1);
    CHECK(scene.activations[0].sp_end_tick == 11520);
    CHECK(scene.activations[0].sp_end_ms == doctest::Approx(12000.0));

    CHECK(sp_meter_bars_at(c, 4000.0) == doctest::Approx(2.0));
    // Straight through the phrase at one bar per two measures: no step.
    CHECK(sp_meter_bars_at(c, 10000.0 - 1e-6) == doctest::Approx(0.5));
    CHECK(sp_meter_bars_at(c, 10000.0) == doctest::Approx(0.5));
    CHECK(sp_meter_bars_at(c, 11000.0) == doctest::Approx(0.25));
    // Empty at the deact node, then the squeezed-out phrase's bar lands.
    CHECK(sp_meter_bars_at(c, 12000.0 - 1e-6) == doctest::Approx(0.0));
    CHECK(sp_meter_bars_at(c, 12000.0) == doctest::Approx(1.0));
    CHECK(sp_meter_bars_at(c, 13000.0) == doctest::Approx(1.0));
}

TEST_CASE("sp meter curve: a full bank that collects a phrase and stores no row") {
    // Regression, from a gameplay video the user checked against the Preview.
    //
    // A four-bar activation collects one SP phrase while Star Power is
    // running, and no note lands within the engine's 500 ms squeeze window
    // after the deactivation node. So the record stores no backend row at all,
    // and the old code had nothing to read the node out of: it fell back to
    // "two measures per banked bar", which cannot see the collection. The
    // meter emptied two measures early and then showed a bar it had never
    // banked.
    //
    // Now the search stamps the node and the Preview just reads it. Eight
    // measures for the four banked bars plus two for the collection: ten.
    //
    // The phrase's last note sits 3 and 15/16 measures into the window, off
    // the quarter-note grid, so the drain is caught mid-measure on both sides
    // of the step.
    const int64_t act_tick = 3840;                 // 4000 ms
    const int64_t phrase_tick = act_tick + 7560;   // 11400 -> 11875 ms
    const int64_t deact = act_tick + 10 * 1920;    // 23040 -> 24000 ms
    Song song = make_sp_song({phrase_tick}, /*last_tick=*/28800, /*extra_bpm=*/{},
                             /*step=*/120);
    Path path;
    Activation act = sp_act_at(song, act_tick, /*sp_meter=*/4);
    act.deact_tick = deact;
    act.collected_phrase_ticks = {phrase_tick};
    REQUIRE(act.backends.empty());
    path.activations = {act};

    PreviewScene scene = build_preview_scene(song, &path);
    const SpMeterCurve& c = scene.sp_meter;
    check_curve_well_formed(c);
    CHECK(c.cap == 4);

    REQUIRE(scene.activations.size() == 1);
    CHECK(scene.activations[0].has_sp_end);
    CHECK(scene.activations[0].sp_end_tick == deact);
    CHECK(scene.activations[0].sp_end_ms == doctest::Approx(24000.0));

    // Four bars at the activation, then eight measures of drain to burn them.
    CHECK(sp_meter_bars_at(c, 4000.0) == doctest::Approx(4.0));
    // Just before the phrase's last note: 3.9375 measures gone of eight.
    CHECK(sp_meter_bars_at(c, 11875.0 - 1e-6) == doctest::Approx(2.03125));
    // The collection hands back a whole bar on the spot.
    CHECK(sp_meter_bars_at(c, 11875.0) == doctest::Approx(3.03125));
    // The remaining 6.0625 bars' worth of measures runs out exactly at D.
    CHECK(sp_meter_bars_at(c, 24000.0 - 1e-6) == doctest::Approx(0.0));
    CHECK(sp_meter_bars_at(c, 24000.0) == doctest::Approx(0.0));
    // Still 0 after D. The phrase was collected during SP, not squeezed out,
    // so there is no bar waiting at the window's close.
    CHECK(sp_meter_bars_at(c, 26000.0) == doctest::Approx(0.0));
}

TEST_CASE("sp meter curve: two clamped collections refill twice and empty at the deact node") {
    // Cap 2. Two phrases bank 2 bars; the activation at tick 2304 (6000 ms)
    // spends them. The phrases at 3072 (8000 ms) and 3840 (10000 ms) are both
    // collected during SP, and each one clamps at the cap. The engine puts
    // the deact node at 6912 (18000 ms). The old gauge counted the
    // collections off the deact node: (9 - 3) / 2 - 2 = 1, so it drew one
    // refill and then showed a bar after SP ended that was never banked.
    Song song = make_overfill_song();
    ScoreGraph graph(song, 2);
    std::vector<Path> paths = run_search(graph, DepthMode::Scores, 0, std::nullopt);
    REQUIRE(!paths.empty());
    REQUIRE(paths.front().activations.size() == 1);
    const Activation& act = paths.front().activations.front();
    REQUIRE(act.timecode.has_value());
    REQUIRE(act.timecode->ticks() == 2304);
    REQUIRE(activation_deact_tick(act) == std::optional<int64_t>(6912));
    // Extra parentheses: the braced list's comma would split the macro.
    REQUIRE((act.collected_phrase_ticks == std::vector<int64_t>{3072, 3840}));

    PreviewScene scene = build_preview_scene(song, &paths.front(), /*sp_cap=*/2);
    const SpMeterCurve& c = scene.sp_meter;
    check_curve_well_formed(c);
    CHECK(c.cap == 2);

    // The activation snaps to the 2 bars the engine recorded.
    CHECK(sp_meter_bars_at(c, 6000.0) == doctest::Approx(2.0));
    // One measure of drain, then the first collection tops back up to the cap.
    CHECK(sp_meter_bars_at(c, 8000.0 - 1e-6) == doctest::Approx(1.5));
    CHECK(sp_meter_bars_at(c, 8000.0) == doctest::Approx(2.0));
    // Another measure of drain, then the second collection does the same.
    CHECK(sp_meter_bars_at(c, 10000.0 - 1e-6) == doctest::Approx(1.5));
    CHECK(sp_meter_bars_at(c, 10000.0) == doctest::Approx(2.0));
    // Four measures from 10000 ms burn the 2 bars exactly at the deact node.
    CHECK(sp_meter_bars_at(c, 14000.0) == doctest::Approx(1.0));
    CHECK(sp_meter_bars_at(c, 18000.0) == doctest::Approx(0.0));
    // Both window phrases were collected, so nothing banks when SP ends.
    CHECK(sp_meter_bars_at(c, 19000.0) == doctest::Approx(0.0));
}

TEST_CASE("sp meter curve: a phrase ending on the activation note is not counted twice") {
    // The SP phrase's last note sits exactly on the activation tick. That bar
    // is already inside the engine's recorded sp_meter, so it must not also
    // be counted as a mid-SP collection -- the snap should read 2.0, not 3.0.
    Song song = make_sp_song({3840}, /*last_tick=*/13440);
    Path path;
    path.activations = {sp_act_at(song, 3840, /*sp_meter=*/2)};

    PreviewScene scene = build_preview_scene(song, &path);
    const SpMeterCurve& c = scene.sp_meter;
    check_curve_well_formed(c);

    // Two bars is four measures: 8000 ms at 120 BPM 4/4, ending at tick 11520.
    REQUIRE(scene.activations.size() == 1);
    CHECK(scene.activations[0].has_sp_end);
    CHECK(scene.activations[0].sp_end_tick == 11520);
    CHECK(scene.activations[0].sp_end_ms == doctest::Approx(12000.0));

    CHECK(sp_meter_bars_at(c, 4000.0) == doctest::Approx(2.0));   // the snap, no extra bar
    CHECK(sp_meter_bars_at(c, 8000.0) == doctest::Approx(1.0));   // one bar's drain later
    CHECK(sp_meter_bars_at(c, 12000.0) == doctest::Approx(0.0));  // the deact node
    CHECK(sp_meter_bars_at(c, 14000.0) == doctest::Approx(0.0));  // still 0: no double-step
}

TEST_CASE("sp meter curve: the drain is linear in measures across a tempo change") {
    // 120 BPM until tick 5760 (6000 ms), then 60 BPM. A measure costs 2000 ms
    // before the change and 4000 ms after, so the ms slope halves there.
    Song song = make_sp_song({960}, /*last_tick=*/13440, {{5760, 60.0}});
    Path path;
    path.activations = {sp_act_at(song, 3840, /*sp_meter=*/2)};

    PreviewScene scene = build_preview_scene(song, &path);
    const SpMeterCurve& c = scene.sp_meter;
    check_curve_well_formed(c);

    REQUIRE(scene.tempos.size() == 2);
    CHECK(scene.tempos[1].tick == 5760);
    CHECK(scene.tempos[1].ms == doctest::Approx(6000.0));
    // Four measures of SP still, but they now stretch to tick 11520 = 18000 ms.
    CHECK(scene.activations[0].sp_end_tick == 11520);
    CHECK(scene.activations[0].sp_end_ms == doctest::Approx(18000.0));

    CHECK(sp_meter_bars_at(c, 4000.0) == doctest::Approx(2.0));
    CHECK(sp_meter_bars_at(c, 5000.0) == doctest::Approx(1.75));  // 0.25 bars per second
    CHECK(sp_meter_bars_at(c, 6000.0) == doctest::Approx(1.5));   // the tempo change
    CHECK(sp_meter_bars_at(c, 7000.0) == doctest::Approx(1.375)); // 0.125 bars per second
    CHECK(sp_meter_bars_at(c, 12000.0) == doctest::Approx(0.75));
    CHECK(sp_meter_bars_at(c, 18000.0) == doctest::Approx(0.0));
}

TEST_CASE("sp meter curve: an activation with no recorded bars empties the meter at once") {
    // A stale record: the activation has a timecode but no sp_meter, so there
    // is no deact node and no drain to draw. The bank still goes.
    Song song = make_sp_song({960, 1920, 5760}, /*last_tick=*/9600);
    Path path;
    path.activations = {act_at(song, 3840, /*skips=*/0)};

    PreviewScene scene = build_preview_scene(song, &path);
    const SpMeterCurve& c = scene.sp_meter;
    check_curve_well_formed(c);

    REQUIRE(scene.activations.size() == 1);
    CHECK_FALSE(scene.activations[0].has_sp_end);

    CHECK(sp_meter_bars_at(c, 3999.0) == doctest::Approx(2.0));
    CHECK(sp_meter_bars_at(c, 4000.0) == doctest::Approx(0.0));
    CHECK(sp_meter_bars_at(c, 5999.0) == doctest::Approx(0.0));
    CHECK(sp_meter_bars_at(c, 6000.0) == doctest::Approx(1.0));  // banking resumes
}

TEST_CASE("sp meter curve: the bank stops at the cap") {
    // Six phrases, at 1000 ms through 6000 ms.
    Song song = make_sp_song({960, 1920, 2880, 3840, 4800, 5760}, /*last_tick=*/7680);

    PreviewScene scene = build_preview_scene(song, nullptr);
    CHECK(scene.sp_meter.cap == 4);
    CHECK(sp_meter_bars_at(scene.sp_meter, 3000.0) == doctest::Approx(3.0));
    CHECK(sp_meter_bars_at(scene.sp_meter, 4000.0) == doctest::Approx(4.0));
    CHECK(sp_meter_bars_at(scene.sp_meter, 5000.0) == doctest::Approx(4.0));
    CHECK(sp_meter_bars_at(scene.sp_meter, 8000.0) == doctest::Approx(4.0));

    // The same chart on a record analyzed at a cap of 2.
    PreviewScene capped = build_preview_scene(song, nullptr, /*sp_cap=*/2);
    CHECK(capped.sp_meter.cap == 2);
    check_curve_well_formed(capped.sp_meter);
    CHECK(sp_meter_bars_at(capped.sp_meter, 1000.0) == doctest::Approx(1.0));
    CHECK(sp_meter_bars_at(capped.sp_meter, 2000.0) == doctest::Approx(2.0));
    CHECK(sp_meter_bars_at(capped.sp_meter, 6000.0) == doctest::Approx(2.0));
    CHECK(sp_meter_bars_at(capped.sp_meter, 8000.0) == doctest::Approx(2.0));
}

TEST_CASE("sp meter curve: without a path the meter fills and never drains") {
    Song song = make_sp_song({960, 1920, 2880, 3840, 4800}, /*last_tick=*/7680);
    PreviewScene scene = build_preview_scene(song, nullptr);
    REQUIRE(scene.activations.empty());
    check_curve_well_formed(scene.sp_meter);

    // Never decreasing anywhere: nothing spends the bank.
    for (const SpMeterSegment& s : scene.sp_meter.segments) CHECK(s.end_bars >= s.start_bars);
    double prev = 0.0;
    for (double ms = 0.0; ms <= 9000.0; ms += 250.0) {
        const double v = sp_meter_bars_at(scene.sp_meter, ms);
        CHECK(v >= prev - 1e-9);
        prev = v;
    }
    CHECK(sp_meter_bars_at(scene.sp_meter, 9000.0) == doctest::Approx(4.0));
}

TEST_CASE("sp meter curve: a chart with no SP and no path has no curve at all") {
    Song song = make_fill_song();  // fills but no SP phrases
    PreviewScene scene = build_preview_scene(song, nullptr);
    CHECK(scene.sp_meter.segments.empty());
    CHECK(sp_meter_bars_at(scene.sp_meter, 1000.0) == doctest::Approx(0.0));
}

TEST_CASE("sp_meter_bars_at: before the curve, after it, and on a shared boundary") {
    CHECK(sp_meter_bars_at(SpMeterCurve{}, 123.0) == doctest::Approx(0.0));

    SpMeterCurve c;
    c.segments = {{100.0, 200.0, 0.0, 0.0}, {200.0, 400.0, 1.0, 0.0}};

    CHECK(sp_meter_bars_at(c, 50.0) == doctest::Approx(0.0));   // before the first
    CHECK(sp_meter_bars_at(c, 100.0) == doctest::Approx(0.0));
    CHECK(sp_meter_bars_at(c, 150.0) == doctest::Approx(0.0));
    // 200 ms is the boundary: the later segment's 1.0, not the earlier's 0.0.
    CHECK(sp_meter_bars_at(c, 200.0) == doctest::Approx(1.0));
    CHECK(sp_meter_bars_at(c, 300.0) == doctest::Approx(0.5));
    CHECK(sp_meter_bars_at(c, 400.0) == doctest::Approx(0.0));
    CHECK(sp_meter_bars_at(c, 99999.0) == doctest::Approx(0.0));  // holds the last value

    // A zero-width final segment is how a curve carries its end value when the
    // last step lands exactly at the end of the chart.
    SpMeterCurve z;
    z.segments = {{0.0, 100.0, 0.0, 1.0}, {100.0, 100.0, 3.0, 3.0}};
    CHECK(sp_meter_bars_at(z, 50.0) == doctest::Approx(0.5));
    CHECK(sp_meter_bars_at(z, 100.0) == doctest::Approx(3.0));
    CHECK(sp_meter_bars_at(z, 500.0) == doctest::Approx(3.0));
}

TEST_CASE("path_overlay_key: no overlay, the same path, and a changed path") {
    // The key is how the Preview notices the Paths tab picked a different
    // path; Path itself has no operator==.
    CHECK(path_overlay_key(nullptr).empty());

    const AnalysisResult& r = analyzed();
    std::vector<const Path*> paths = r.record.all_paths();
    REQUIRE_FALSE(paths.empty());

    const Path& first = *paths.front();
    Path copy = first;
    CHECK_FALSE(path_overlay_key(&first).empty());
    CHECK(path_overlay_key(&first) == path_overlay_key(&copy));

    // Same activations, a different score: still a different overlay.
    Path rescored = first;
    rescored.score_base += 1;
    CHECK(path_overlay_key(&first) != path_overlay_key(&rescored));

    // Different activations: different key.
    Path trimmed = first;
    if (!trimmed.activations.empty()) {
        trimmed.activations.pop_back();
        CHECK(path_overlay_key(&first) != path_overlay_key(&trimmed));
    }
}

TEST_CASE("step_tick_ms: one tick from the tick the time box shows") {
    // 120 BPM, 480 ticks per quarter: a tick is 500/480 ms.
    Song song = make_hand_song();
    PreviewScene scene = build_preview_scene(song, nullptr);
    const MsIndex& ms = song.timing().ms_index();

    // 1300 ms is the time box's own example: tick 1248, "[1:3:288]".
    const double fwd = step_tick_ms(scene, 1300.0, 1);
    const double back = step_tick_ms(scene, 1300.0, -1);
    CHECK(fwd == doctest::Approx(ms.at(1249)));
    CHECK(back == doctest::Approx(ms.at(1247)));
    CHECK(build_time_box(scene, fwd, 5000.0).measure_beat == "[1:3:289] / [3:3:000]");
    CHECK(build_time_box(scene, back, 5000.0).measure_beat == "[1:3:287] / [3:3:000]");

    // Between ticks it steps from the rounded tick: 1300.6 ms rounds to 1249.
    CHECK(step_tick_ms(scene, 1300.6, 1) == doctest::Approx(ms.at(1250)));

    // Never before tick 0.
    CHECK(step_tick_ms(scene, 0.0, -1) == doctest::Approx(0.0));
    CHECK(step_tick_ms(scene, 0.5, -3) == doctest::Approx(0.0));
}

TEST_CASE("step_tick_ms: a tempo change moves the tick length with it") {
    // 120 BPM until tick 1920 (2000 ms), then 240 BPM: a tick shrinks from
    // 500/480 ms to 250/480 ms.
    Song song = make_sp_song({}, 3840, {{1920, 240.0}});
    PreviewScene scene = build_preview_scene(song, nullptr);
    const MsIndex& ms = song.timing().ms_index();
    REQUIRE(ms.at(1920) == doctest::Approx(2000.0));

    CHECK(step_tick_ms(scene, 2000.0, 1) == doctest::Approx(ms.at(1921)));
    CHECK(step_tick_ms(scene, 2000.0, -1) == doctest::Approx(ms.at(1919)));
    CHECK(step_tick_ms(scene, 2000.0, 1) - 2000.0 == doctest::Approx(250.0 / 480.0));
    CHECK(2000.0 - step_tick_ms(scene, 2000.0, -1) == doctest::Approx(500.0 / 480.0));
}

TEST_CASE("step_tick_ms: a scene with no song leaves the time alone") {
    PreviewScene empty;
    CHECK(step_tick_ms(empty, 1234.5, 1) == doctest::Approx(1234.5));
}
