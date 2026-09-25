// Tests for render/track_state: the instant timeline the highway draws from
// (a port of Onyx's CommonState/DrumState maps), built from a PreviewScene.

#include "doctest.h"

#include <stdexcept>
#include <vector>

#include "app/preview_view.h"
#include "render/track_state.h"

using namespace hydra;
using namespace hydra::app;
using namespace hydra::render;

namespace {

PreviewNote note(double ms, PreviewLane lane, bool cymbal = false, bool ghost = false,
                 bool accent = false) {
    PreviewNote n;
    n.ms = ms;
    n.tick = static_cast<int64_t>(ms);  // 1 tick per ms keeps ticks distinct
    n.lane = lane;
    n.cymbal = cymbal;
    n.ghost = ghost;
    n.accent = accent;
    return n;
}

PreviewSpan span(double start_ms, double end_ms) {
    PreviewSpan s;
    s.start_ms = start_ms;
    s.end_ms = end_ms;
    s.start_tick = static_cast<int64_t>(start_ms);
    s.end_tick = static_cast<int64_t>(end_ms);
    return s;
}

PreviewFill fill(PreviewSpan s, PreviewFillState state) {
    PreviewFill f;
    f.span = s;
    f.state = state;
    return f;
}

// One tick per millisecond (60 BPM at 1000 ticks per beat), matching note()
// and span() above, so half a tick is 0.5 ms and the edges these cases expect
// (1.0005, 0.2505, ...) are the same as before spans moved to ticks.
PreviewScene timed_scene() {
    PreviewScene s;
    s.timing = SongTiming(1000, {{0, 4000}}, {{0, 60.0}});
    s.tick_resolution = 1000;
    return s;
}

const TrackInstant* find(const std::vector<TrackInstant>& v, double t) {
    for (const TrackInstant& i : v)
        if (i.t == doctest::Approx(t)) return &i;
    return nullptr;
}

}  // namespace

TEST_CASE("toggle_at: Onyx makeToggle truth table") {
    std::vector<std::pair<double, double>> ivs{{1.0, 2.0}, {2.0, 3.0}};
    CHECK(toggle_at(ivs, 0.5) == Toggle::Empty);
    CHECK(toggle_at(ivs, 1.0) == Toggle::Start);
    CHECK(toggle_at(ivs, 1.5) == Toggle::On);
    CHECK(toggle_at(ivs, 2.0) == Toggle::Restart);  // one ends, the next starts
    CHECK(toggle_at(ivs, 3.0) == Toggle::End);
    CHECK(toggle_at(ivs, 3.5) == Toggle::Empty);
}

TEST_CASE("build_track_state: gems, pro-off, and the phrase end note reads inside") {
    PreviewScene scene = timed_scene();
    scene.notes = {note(0.0, PreviewLane::Red), note(500.0, PreviewLane::Yellow, true, true),
                   note(500.0, PreviewLane::Kick), note(1000.0, PreviewLane::Green, true, false, true)};
    scene.sp_phrases = {span(500.0, 1000.0)};

    TrackState st = build_track_state(scene, TrackStateOptions{true});
    const auto& inst = st.instants();
    // Instants: 0.0, 0.5, 1.0, 1.0005 (phrase end + half a tick).
    REQUIRE(inst.size() == 4);
    CHECK(inst[0].t == doctest::Approx(0.0));
    CHECK(inst[1].t == doctest::Approx(0.5));
    CHECK(inst[2].t == doctest::Approx(1.0));
    CHECK(inst[3].t == doctest::Approx(1.0005));

    // Two gems at 0.5 s: a yellow ghost cymbal and a kick.
    REQUIRE(inst[1].notes.size() == 2);
    CHECK(inst[1].notes[0].pad == Pad::Yellow);
    CHECK(inst[1].notes[0].cymbal);
    CHECK(inst[1].notes[0].velocity == Velocity::Ghost);
    CHECK(inst[1].notes[1].kick);
    CHECK(inst[2].notes[0].velocity == Velocity::Accent);

    // The phrase starts at 0.5, is still on at its last note (1.0), ends after.
    CHECK(inst[0].overdrive == Toggle::Empty);
    CHECK(inst[1].overdrive == Toggle::Start);
    CHECK(inst[2].overdrive == Toggle::On);
    CHECK(inst[3].overdrive == Toggle::End);

    // Pro off: no cymbals anywhere.
    TrackState flat = build_track_state(scene, TrackStateOptions{false});
    for (const TrackInstant& i : flat.instants())
        for (const TrackGem& g : i.notes) CHECK_FALSE(g.cymbal);
}

TEST_CASE("build_track_state: active SP window ends exactly at the deact node") {
    PreviewScene scene;
    scene.notes = {note(0.0, PreviewLane::Red), note(4000.0, PreviewLane::Red)};
    PreviewActivation a;
    a.tick = 1000;
    a.ms = 1000.0;
    a.has_sp_end = true;
    a.sp_end_tick = 3000;
    a.sp_end_ms = 3000.0;
    scene.activations = {a};

    TrackState st = build_track_state(scene, TrackStateOptions{});
    const TrackInstant* start = find(st.instants(), 1.0);
    const TrackInstant* end = find(st.instants(), 3.0);
    REQUIRE(start);
    REQUIRE(end);
    CHECK(start->sp_active == Toggle::Start);
    CHECK(end->sp_active == Toggle::End);
    CHECK(find(st.instants(), 0.0)->sp_active == Toggle::Empty);
    CHECK(find(st.instants(), 4.0)->sp_active == Toggle::Empty);
    // No instant at 3.0005: the window is exact, not epsilon-extended.
    CHECK(find(st.instants(), 3.0005) == nullptr);
}

TEST_CASE("build_track_state: taken, offered and hidden fills toggle different spans") {
    PreviewScene scene = timed_scene();
    scene.notes = {note(0.0, PreviewLane::Red), note(2000.0, PreviewLane::Green),
                   note(5000.0, PreviewLane::Green), note(8000.0, PreviewLane::Green)};
    scene.fills = {fill(span(1000.0, 2000.0), PreviewFillState::Taken),
                   fill(span(4000.0, 5000.0), PreviewFillState::Offered),
                   fill(span(7000.0, 8000.0), PreviewFillState::Hidden)};
    PreviewActivation a;
    a.tick = 2000;
    a.ms = 2000.0;
    a.has_lane = true;
    a.lane = PreviewLane::Green;
    scene.activations = {a};

    TrackState st = build_track_state(scene, TrackStateOptions{});
    const TrackInstant* taken = find(st.instants(), 1.0);
    const TrackInstant* offered = find(st.instants(), 4.0);
    REQUIRE(taken);
    REQUIRE(offered);

    // The taken fill drives `fill_taken` and the lit lane, not `fill`.
    CHECK(taken->fill_taken == Toggle::Start);
    CHECK(taken->fill == Toggle::Empty);
    CHECK(taken->fill_lane == Toggle::Start);
    REQUIRE(taken->fill_lane_pad.has_value());
    CHECK(*taken->fill_lane_pad == Pad::Green);

    // The offered fill drives `fill` alone.
    CHECK(offered->fill == Toggle::Start);
    CHECK(offered->fill_taken == Toggle::Empty);
    CHECK(offered->fill_lane == Toggle::Empty);
    CHECK_FALSE(offered->fill_lane_pad.has_value());

    // The hidden fill produces no intervals at all: nothing toggles at 7.0.
    const TrackInstant* hidden = find(st.instants(), 7.0);
    CHECK(hidden == nullptr);
    const TrackInstant* hidden_end = find(st.instants(), 8.0);  // the note is there
    REQUIRE(hidden_end);
    CHECK(hidden_end->fill == Toggle::Empty);
    CHECK(hidden_end->fill_taken == Toggle::Empty);
    CHECK(hidden_end->fill_lane == Toggle::Empty);

    // The activation note itself (2.0) is inside both the taken fill and the lane.
    const TrackInstant* act = find(st.instants(), 2.0);
    REQUIRE(act);
    CHECK(act->fill_taken == Toggle::On);
    CHECK(act->fill_lane == Toggle::On);
    CHECK(act->fill == Toggle::Empty);
}

TEST_CASE("build_track_state: beats land on instants; solo toggles") {
    PreviewScene scene = timed_scene();
    scene.notes = {note(250.0, PreviewLane::Red)};
    scene.beats = {{0, 0.0, PreviewBeatKind::Bar}, {0, 250.0, PreviewBeatKind::Half},
                   {0, 500.0, PreviewBeatKind::Beat}};
    scene.solos = {span(250.0, 250.0)};  // a one-note solo

    TrackState st = build_track_state(scene, TrackStateOptions{});
    REQUIRE(find(st.instants(), 0.0));
    CHECK(*find(st.instants(), 0.0)->beat == PreviewBeatKind::Bar);
    CHECK(*find(st.instants(), 0.25)->beat == PreviewBeatKind::Half);
    CHECK(find(st.instants(), 0.25)->notes.size() == 1);
    CHECK(find(st.instants(), 0.25)->solo == Toggle::Start);
    CHECK(find(st.instants(), 0.2505)->solo == Toggle::End);
}

TEST_CASE("window: strict bounds, and a synthesized instant when empty") {
    PreviewScene scene = timed_scene();
    scene.notes = {note(1000.0, PreviewLane::Red), note(2000.0, PreviewLane::Red),
                   note(3000.0, PreviewLane::Red)};
    scene.solos = {span(900.0, 3100.0)};
    TrackState st = build_track_state(scene, TrackStateOptions{});

    std::vector<TrackInstant> w = st.window(1.0, 3.0);  // excludes both ends
    REQUIRE(w.size() == 1);
    CHECK(w[0].t == doctest::Approx(2.0));
    CHECK(w[0].solo == Toggle::On);

    // Nothing between 2.1 and 2.9: the synthesized midpoint instant carries
    // the ongoing solo.
    std::vector<TrackInstant> empty = st.window(2.1, 2.9);
    REQUIRE(empty.size() == 1);
    CHECK(empty[0].t == doctest::Approx(2.5));
    CHECK(empty[0].solo == Toggle::On);
    CHECK(empty[0].notes.empty());
    CHECK_FALSE(empty[0].beat.has_value());

    // Outside every span: synthesized Empty.
    std::vector<TrackInstant> before = st.window(0.1, 0.5);
    REQUIRE(before.size() == 1);
    CHECK(before[0].solo == Toggle::Empty);
}

TEST_CASE("make_toggle_bounds: covers [near, far], merges equal neighbours") {
    PreviewScene scene = timed_scene();
    scene.notes = {note(0.0, PreviewLane::Red), note(10000.0, PreviewLane::Red)};
    // Touching solos: with the half-tick end they overlap, so the second's start
    // and the first's end both read as On and the span never breaks.
    scene.solos = {span(1000.0, 2000.0), span(2000.0, 3000.0)};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    CHECK(find(st.instants(), 2.0)->solo == Toggle::On);
    CHECK(find(st.instants(), 2.0005)->solo == Toggle::On);

    std::vector<TrackInstant> w = st.window(0.5, 4.0);
    std::vector<ToggleSpan> spans =
        st.make_toggle_bounds(w, 0.5, 4.0, &TrackInstant::solo);
    // off [0.5,1.0), on [1.0, 3.0005), off to 4.0
    REQUIRE(spans.size() == 3);
    CHECK(spans[0].t1 == doctest::Approx(0.5));
    CHECK(spans[0].t2 == doctest::Approx(1.0));
    CHECK_FALSE(spans[0].on);
    CHECK(spans[1].t1 == doctest::Approx(1.0));
    CHECK(spans[1].t2 == doctest::Approx(3.0005));
    CHECK(spans[1].on);
    CHECK(spans[2].t2 == doctest::Approx(4.0));
    CHECK_FALSE(spans[2].on);

    // A window opening mid-span starts "on".
    std::vector<TrackInstant> mid = st.window(1.5, 2.5);
    std::vector<ToggleSpan> mid_spans =
        st.make_toggle_bounds(mid, 1.5, 2.5, &TrackInstant::solo);
    REQUIRE(mid_spans.size() == 1);
    CHECK(mid_spans[0].on);
    CHECK(mid_spans[0].t1 == doctest::Approx(1.5));
    CHECK(mid_spans[0].t2 == doctest::Approx(2.5));
}

TEST_CASE("build_track_state: a chord one tick after a phrase ends is not SP (480 res, 300 BPM)") {
    // One tick here is 0.417 ms, shorter than the old half-millisecond margin.
    SongTiming timing(480, {{0, 1920}}, {{0, 300.0}});
    auto at_tick = [&](int64_t tick, PreviewLane lane) {
        PreviewNote n;
        n.tick = tick;
        n.ms = timing.ms_index().at(tick);
        n.lane = lane;
        return n;
    };
    PreviewScene scene;
    scene.timing = timing;
    scene.tick_resolution = 480;
    scene.notes = {at_tick(0, PreviewLane::Red), at_tick(480, PreviewLane::Yellow),
                   at_tick(481, PreviewLane::Blue)};
    PreviewSpan phrase;
    phrase.start_tick = 0;
    phrase.end_tick = 480;
    phrase.start_ms = timing.ms_index().at(0);
    phrase.end_ms = timing.ms_index().at(480);
    scene.sp_phrases = {phrase};

    TrackState st = build_track_state(scene, TrackStateOptions{});
    const TrackInstant* last_in = find(st.instants(), timing.ms_index().at(480) / 1000.0);
    const TrackInstant* next = find(st.instants(), timing.ms_index().at(481) / 1000.0);
    REQUIRE(last_in);
    REQUIRE(next);
    CHECK(last_in->overdrive == Toggle::On);
    CHECK(next->overdrive == Toggle::Empty);
}

TEST_CASE("build_track_state: spans need the song timing") {
    PreviewScene scene;  // no timing
    scene.notes = {note(1000.0, PreviewLane::Red)};
    scene.sp_phrases = {span(1000.0, 1000.0)};
    CHECK_THROWS_AS(build_track_state(scene, TrackStateOptions{}), std::invalid_argument);

    // A scene with no spans still builds without timing.
    PreviewScene plain;
    plain.notes = {note(1000.0, PreviewLane::Red)};
    CHECK(build_track_state(plain, TrackStateOptions{}).instants().size() == 1);
}
