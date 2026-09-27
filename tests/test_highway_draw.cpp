// Tests for render/highway_draw: the numbers and order of Onyx's drawDrumPlay,
// checked without a GPU.

#include "doctest.h"

#include <cmath>
#include <vector>

#include "app/preview_view.h"
#include "render/highway_draw.h"
#include "render/track_state.h"

using namespace DirectX;
using namespace hydra;
using namespace hydra::app;
using namespace hydra::render;

namespace {

PreviewNote note(double ms, PreviewLane lane, bool cymbal = false, bool ghost = false,
                 bool accent = false) {
    PreviewNote n;
    n.ms = ms;
    n.tick = static_cast<int64_t>(ms);
    n.lane = lane;
    n.cymbal = cymbal;
    n.ghost = ghost;
    n.accent = accent;
    return n;
}

PreviewSpan span(double a, double b) {
    PreviewSpan s;
    s.start_ms = a;
    s.end_ms = b;
    s.start_tick = static_cast<int64_t>(a);
    s.end_tick = static_cast<int64_t>(b);
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

XMFLOAT3 transform(const XMFLOAT4X4& m, float x, float y, float z) {
    XMVECTOR v = XMVector3TransformCoord(XMVectorSet(x, y, z, 1.0f), XMLoadFloat4x4(&m));
    XMFLOAT3 out;
    XMStoreFloat3(&out, v);
    return out;
}

std::vector<const DrawCommand*> of_mesh(const std::vector<DrawCommand>& cmds, MeshId id) {
    std::vector<const DrawCommand*> out;
    for (const DrawCommand& c : cmds)
        if (c.mesh == id) out.push_back(&c);
    return out;
}

}  // namespace

TEST_CASE("time_to_z: Onyx's linear time->depth map") {
    PreviewConfig cfg;
    CHECK(time_to_z(cfg, 10.0, 10.0, 1.0) == doctest::Approx(0.0));
    CHECK(time_to_z(cfg, 10.0, 11.35, 1.0) == doctest::Approx(-12.0));
    CHECK(time_to_z(cfg, 10.0, 10.0 - 0.225, 1.0) == doctest::Approx(2.0));
    CHECK(z_to_time(cfg, 10.0, 2.0, 1.0) == doctest::Approx(9.775));
    CHECK(z_to_time(cfg, 10.0, -12.0, 1.0) == doctest::Approx(11.35));
    // Speed stretches the far time.
    CHECK(time_to_z(cfg, 10.0, 12.7, 2.0) == doctest::Approx(-12.0));
}

TEST_CASE("pad_x: the note area split into four lanes, left to right") {
    PreviewConfig cfg;
    float x1, x2;
    pad_x(cfg, Pad::Red, x1, x2);
    CHECK(x1 == doctest::Approx(-1.0f));
    CHECK(x2 == doctest::Approx(-0.5f));
    pad_x(cfg, Pad::Yellow, x1, x2);
    CHECK(x1 == doctest::Approx(-0.5f));
    CHECK(x2 == doctest::Approx(0.0f));
    pad_x(cfg, Pad::Blue, x1, x2);
    CHECK(x1 == doctest::Approx(0.0f));
    CHECK(x2 == doctest::Approx(0.5f));
    pad_x(cfg, Pad::Green, x1, x2);
    CHECK(x1 == doctest::Approx(0.5f));
    CHECK(x2 == doctest::Approx(1.0f));
}

TEST_CASE("make_camera: Onyx's tilted view and right-handed projection") {
    PreviewConfig cfg;
    HighwayCamera cam = make_camera(cfg, 1.0f);
    CHECK(cam.view_pos.y == doctest::Approx(1.4f));
    // World (0,-1,0) -> view (0, -0.907, -3.733): below centre, in front.
    XMFLOAT3 p = transform(cam.view, 0.0f, -1.0f, 0.0f);
    CHECK(p.x == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(p.y == doctest::Approx(-0.907f).epsilon(0.01));
    CHECK(p.z == doctest::Approx(-3.733f).epsilon(0.01));
    // The far end of the highway sits above the near end on screen.
    XMFLOAT3 far = transform(cam.view, 0.0f, -1.0f, -12.0f);
    CHECK(far.y > p.y);
    CHECK(far.z < p.z);  // farther in front of the camera (RH: -Z)
    // Both project inside the clip volume with positive w.
    XMMATRIX vp = XMLoadFloat4x4(&cam.view) * XMLoadFloat4x4(&cam.proj);
    XMVECTOR clip = XMVector4Transform(XMVectorSet(0.0f, -1.0f, -12.0f, 1.0f), vp);
    CHECK(XMVectorGetW(clip) > 0.0f);
    CHECK(std::fabs(XMVectorGetY(clip) / XMVectorGetW(clip)) < 1.0f);
    XMVECTOR clip_near = XMVector4Transform(XMVectorSet(0.0f, -1.0f, 0.0f, 1.0f), vp);
    CHECK(XMVectorGetY(clip_near) / XMVectorGetW(clip_near) <
          XMVectorGetY(clip) / XMVectorGetW(clip));
}

TEST_CASE("stretch_matrix and light_for") {
    DrawCommand c;
    c.lo[0] = -1; c.lo[1] = -1.25f; c.lo[2] = -0.25f;
    c.hi[0] = -0.5f; c.hi[1] = -0.75f; c.hi[2] = 0.25f;
    XMMATRIX m = stretch_matrix(c);
    // The unit cube's (0.5, 0.5, 0.5) corner lands on the box's hi corner.
    XMVECTOR v = XMVector3TransformCoord(XMVectorSet(0.5f, 0.5f, 0.5f, 1.0f), m);
    CHECK(XMVectorGetX(v) == doctest::Approx(-0.5f));
    CHECK(XMVectorGetY(v) == doctest::Approx(-0.75f));
    CHECK(XMVectorGetZ(v) == doctest::Approx(0.25f));

    PreviewConfig cfg;
    c.light = LightKind::GemOffset;
    LightConfig l = light_for(cfg, c);
    CHECK(l.position.x == doctest::Approx(-0.75f));
    CHECK(l.position.y == doctest::Approx(-0.75f + 1.0f));
    CHECK(l.position.z == doctest::Approx(0.0f + 0.2f));
    c.light = LightKind::Global;
    CHECK(light_for(cfg, c).position.y == doctest::Approx(-0.5f));

    // A Flat keeps Y scale 1.
    DrawCommand f;
    f.lo[0] = -1; f.lo[1] = -1; f.lo[2] = 0.13f;
    f.hi[0] = 1;  f.hi[1] = -1; f.hi[2] = -0.13f;
    XMVECTOR top = XMVector3TransformCoord(XMVectorSet(0.0f, 0.5f, 0.0f, 1.0f), stretch_matrix(f));
    CHECK(XMVectorGetY(top) == doctest::Approx(-0.5f));
}

TEST_CASE("build_highway_draws: order and geometry for a frame") {
    PreviewConfig cfg;
    PreviewScene scene = timed_scene();
    scene.notes = {note(1000.0, PreviewLane::Red), note(1500.0, PreviewLane::Yellow, true),
                   note(1500.0, PreviewLane::Kick)};
    scene.solos = {span(900.0, 1200.0)};
    scene.fills = {fill(span(1300.0, 1500.0), PreviewFillState::Offered)};
    scene.beats = {{0, 1000.0, PreviewBeatKind::Bar}, {0, 1250.0, PreviewBeatKind::Half}};
    TrackState st = build_track_state(scene, TrackStateOptions{});

    const double now = 1.0;  // the Red note is exactly at the strike line
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, now, 1.0);
    REQUIRE(!cmds.empty());

    // Order: floor flats, two railing boxes, beat lines, lane strips, four
    // targets, gems. Find the first index of each kind.
    size_t first_box = cmds.size(), first_always = cmds.size(), first_gem = cmds.size();
    for (size_t i = 0; i < cmds.size(); ++i) {
        if (cmds[i].mesh == MeshId::Box && first_box == cmds.size()) first_box = i;
        if (cmds[i].depth == DepthMode::Always && first_always == cmds.size()) first_always = i;
        if ((cmds[i].mesh == MeshId::Tom || cmds[i].mesh == MeshId::Cymbal ||
             cmds[i].mesh == MeshId::Kick) && first_gem == cmds.size())
            first_gem = i;
    }
    CHECK(first_box < first_always);
    CHECK(first_always < first_gem);
    CHECK(cmds[0].mesh == MeshId::Flat);
    CHECK(cmds[0].depth == DepthMode::Less);
    CHECK(cmds[0].material.kind == MaterialKind::Color);

    // Floor covers near..far: first flat starts at z_past (2), the last floor
    // flat ends at z_future (-12); the solo stretch is tinted #333399.
    CHECK(cmds[0].lo[2] == doctest::Approx(2.0f));
    bool saw_solo = false;
    float floor_far = 0.0f;
    for (size_t i = 0; i < first_box; ++i) {
        if (cmds[i].material.color.b > 0.5f) saw_solo = true;
        floor_far = cmds[i].hi[2];
    }
    CHECK(saw_solo);
    CHECK(floor_far == doctest::Approx(-12.0f));

    // Railings.
    CHECK(cmds[first_box].lo[0] == doctest::Approx(-1.09f));
    CHECK(cmds[first_box].hi[0] == doctest::Approx(-1.0f));
    CHECK(cmds[first_box].lo[1] == doctest::Approx(-0.85f));
    CHECK(cmds[first_box].hi[1] == doctest::Approx(-1.1f));
    CHECK(cmds[first_box + 1].hi[0] == doctest::Approx(1.09f));

    // Beat lines: a Bar at now (z 0 +-0.05, line-1) and a Half at +0.25 s.
    int bars = 0, halves = 0;
    for (const DrawCommand& c : cmds) {
        if (c.material.texture == TextureId::Line1) {
            ++bars;
            CHECK(c.lo[2] == doctest::Approx(0.05f));
            CHECK(c.hi[2] == doctest::Approx(-0.05f));
            CHECK(c.depth == DepthMode::Always);
        }
        if (c.material.texture == TextureId::Line3) ++halves;
    }
    // The Bar sits exactly at now, well inside the (near, far) window.
    CHECK(bars == 1);
    CHECK(halves == 1);

    // Fill: four lane strips from 1.3 s to 1.5005 s.
    int lanes = 0;
    for (const DrawCommand& c : cmds) {
        if (c.material.texture >= TextureId::LaneRed && c.material.texture <= TextureId::LaneGreen) {
            ++lanes;
            CHECK(c.lo[2] == doctest::Approx(time_to_z(cfg, now, 1.3, 1.0)));
            CHECK(c.hi[2] == doctest::Approx(time_to_z(cfg, now, 1.5005, 1.0)));
        }
    }
    CHECK(lanes == 4);

    // Four targets at z [+0.13, -0.13] with depth off.
    int targets = 0;
    for (const DrawCommand& c : cmds)
        if (c.material.texture >= TextureId::TargetRed && c.material.texture <= TextureId::TargetGreen) {
            ++targets;
            CHECK(c.lo[2] == doctest::Approx(0.13f));
            CHECK(c.hi[2] == doctest::Approx(-0.13f));
        }
    CHECK(targets == 4);

    // Gems: the Red exactly at now is a normal gem at the strike line (not a
    // flash — it is not past yet); the Yellow cymbal and the kick at 1.5 s
    // are drawn farther up.
    std::vector<const DrawCommand*> toms = of_mesh(cmds, MeshId::Tom);
    std::vector<const DrawCommand*> cymbals = of_mesh(cmds, MeshId::Cymbal);
    std::vector<const DrawCommand*> kicks = of_mesh(cmds, MeshId::Kick);
    REQUIRE(toms.size() == 1);
    CHECK(toms[0]->material.kind == MaterialKind::Texture);
    CHECK((toms[0]->lo[2] + toms[0]->hi[2]) * 0.5f == doctest::Approx(0.0f));
    REQUIRE(cymbals.size() == 1);
    REQUIRE(kicks.size() == 1);
    const float z15 = static_cast<float>(time_to_z(cfg, now, 1.5, 1.0));
    CHECK(cymbals[0]->lo[0] == doctest::Approx(-0.5f));
    CHECK(cymbals[0]->hi[0] == doctest::Approx(0.0f));
    CHECK(cymbals[0]->lo[1] == doctest::Approx(-1.25f));
    CHECK(cymbals[0]->hi[1] == doctest::Approx(-0.75f));
    CHECK(cymbals[0]->lo[2] == doctest::Approx(z15 - 0.25f));
    CHECK(cymbals[0]->hi[2] == doctest::Approx(z15 + 0.25f));
    CHECK(cymbals[0]->material.texture == TextureId::CymbalYellow);
    CHECK(cymbals[0]->light == LightKind::GemOffset);
    CHECK(kicks[0]->lo[0] == doctest::Approx(-1.0f));
    CHECK(kicks[0]->hi[0] == doctest::Approx(1.0f));
    CHECK(kicks[0]->lo[1] == doctest::Approx(-2.0f));
    CHECK(kicks[0]->hi[1] == doctest::Approx(0.0f));
    CHECK(kicks[0]->lo[2] == doctest::Approx(z15 - 1.0f));
    CHECK(kicks[0]->material.texture == TextureId::LongKick);
}

TEST_CASE("build_highway_draws: a Red gem just ahead fills Onyx's box") {
    PreviewConfig cfg;
    PreviewScene scene;
    scene.notes = {note(1001.0, PreviewLane::Red)};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 1.0, 1.0);
    std::vector<const DrawCommand*> toms = of_mesh(cmds, MeshId::Tom);
    REQUIRE(toms.size() == 1);
    const float z = static_cast<float>(time_to_z(cfg, 1.0, 1.001, 1.0));
    CHECK(toms[0]->lo[0] == doctest::Approx(-1.0f));
    CHECK(toms[0]->hi[0] == doctest::Approx(-0.5f));
    CHECK(toms[0]->lo[1] == doctest::Approx(-1.25f));
    CHECK(toms[0]->hi[1] == doctest::Approx(-0.75f));
    CHECK(toms[0]->lo[2] == doctest::Approx(z - 0.25f));
    CHECK(toms[0]->hi[2] == doctest::Approx(z + 0.25f));
    CHECK(toms[0]->material.kind == MaterialKind::Texture);
    CHECK(toms[0]->material.texture == TextureId::BoxRed);
}

TEST_CASE("build_highway_draws: ghost shrinks 70% and overlays; accent overlays") {
    PreviewConfig cfg;
    PreviewScene scene;
    scene.notes = {note(1100.0, PreviewLane::Blue, false, true),
                   note(1200.0, PreviewLane::Green, true, false, true)};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 1.0, 1.0);
    std::vector<const DrawCommand*> toms = of_mesh(cmds, MeshId::Tom);
    std::vector<const DrawCommand*> cyms = of_mesh(cmds, MeshId::Cymbal);
    REQUIRE(toms.size() == 1);
    REQUIRE(cyms.size() == 1);
    // Blue lane is [0, 0.5]; centre 0.25; 70% -> [0.075, 0.425].
    CHECK(toms[0]->lo[0] == doctest::Approx(0.075f));
    CHECK(toms[0]->hi[0] == doctest::Approx(0.425f));
    CHECK(toms[0]->material.kind == MaterialKind::TextureOverlay);
    CHECK(toms[0]->material.texture == TextureId::BoxBlue);
    CHECK(toms[0]->material.overlay == TextureId::OverlayGhost);
    CHECK(cyms[0]->material.overlay == TextureId::OverlayAccent);
    CHECK(cyms[0]->material.texture == TextureId::CymbalGreen);
    CHECK(cyms[0]->lo[0] == doctest::Approx(0.5f));
}

TEST_CASE("build_highway_draws: a ghost kick keeps full width and overlays") {
    // Hydra departs from Onyx here: Onyx shrinks every ghost to 70%, but a
    // shrunken kick reads as a bar that stops short of the highway edge.
    PreviewConfig cfg;
    PreviewScene scene;
    scene.notes = {note(1100.0, PreviewLane::Kick, false, true)};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 1.0, 1.0);
    std::vector<const DrawCommand*> kicks = of_mesh(cmds, MeshId::Kick);
    REQUIRE(kicks.size() == 1);
    CHECK(kicks[0]->lo[0] == doctest::Approx(-1.0f));
    CHECK(kicks[0]->hi[0] == doctest::Approx(1.0f));
    CHECK(kicks[0]->lo[1] == doctest::Approx(-2.0f));
    CHECK(kicks[0]->hi[1] == doctest::Approx(0.0f));
    CHECK(kicks[0]->material.kind == MaterialKind::TextureOverlay);
    CHECK(kicks[0]->material.texture == TextureId::LongKick);
    CHECK(kicks[0]->material.overlay == TextureId::OverlayGhost);
}

TEST_CASE("build_highway_draws: hit flash and target glow after a note passes") {
    PreviewConfig cfg;
    PreviewScene scene;
    scene.notes = {note(1000.0, PreviewLane::Green), note(1000.0, PreviewLane::Kick),
                   note(5000.0, PreviewLane::Red)};
    TrackState st = build_track_state(scene, TrackStateOptions{});

    // 50 ms after the hit: the gems flash white at the strike line at alpha
    // 0.5, and only the Green target glows (kicks light nothing).
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 1.05, 1.0);
    std::vector<const DrawCommand*> toms = of_mesh(cmds, MeshId::Tom);
    std::vector<const DrawCommand*> kicks = of_mesh(cmds, MeshId::Kick);
    REQUIRE(toms.size() == 1);
    REQUIRE(kicks.size() == 1);
    CHECK(toms[0]->material.kind == MaterialKind::Color);
    CHECK(toms[0]->material.color.r == doctest::Approx(1.0f));
    CHECK(toms[0]->alpha == doctest::Approx(0.5f));
    CHECK((toms[0]->lo[2] + toms[0]->hi[2]) * 0.5f == doctest::Approx(0.0f));
    CHECK(kicks[0]->alpha == doctest::Approx(0.5f));
    int glows = 0;
    for (const DrawCommand& c : cmds)
        if (c.material.texture == TextureId::TargetGreenLight) {
            ++glows;
            CHECK(c.alpha == doctest::Approx(1.0f - 0.05f / 0.1666666f));
        }
    CHECK(glows == 1);
    for (const DrawCommand& c : cmds) CHECK(c.material.texture != TextureId::TargetRedLight);

    // 150 ms after: the flash is gone, the glow is dimmer.
    cmds = build_highway_draws(st, cfg, 1.15, 1.0);
    CHECK(of_mesh(cmds, MeshId::Tom).empty());
    glows = 0;
    for (const DrawCommand& c : cmds)
        if (c.material.texture == TextureId::TargetGreenLight) {
            ++glows;
            CHECK(c.alpha == doctest::Approx(1.0f - 0.15f / 0.1666666f));
        }
    CHECK(glows == 1);

    // 200 ms after: no glow either.
    cmds = build_highway_draws(st, cfg, 1.2, 1.0);
    for (const DrawCommand& c : cmds) CHECK(c.material.texture != TextureId::TargetGreenLight);
}

TEST_CASE("build_highway_draws: energy gems inside an SP phrase, tinted floor in an active window") {
    PreviewConfig cfg;
    PreviewScene scene = timed_scene();
    scene.notes = {note(1100.0, PreviewLane::Red), note(1200.0, PreviewLane::Yellow, true),
                   note(1200.0, PreviewLane::Kick), note(1300.0, PreviewLane::Blue)};
    scene.sp_phrases = {span(1100.0, 1200.0)};  // ends on the yellow/kick chord
    PreviewActivation a;
    a.tick = 1300;
    a.ms = 1300.0;
    a.has_sp_end = true;
    a.sp_end_ms = 2000.0;
    scene.activations = {a};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 1.0, 1.0);

    int energy = 0, plain = 0;
    for (const DrawCommand& c : cmds) {
        if (c.material.texture == TextureId::BoxEnergy || c.material.texture == TextureId::CymbalEnergy ||
            c.material.texture == TextureId::LongEnergy)
            ++energy;
        if (c.material.texture == TextureId::BoxBlue) ++plain;
    }
    CHECK(energy == 3);  // red tom, yellow cymbal, kick — the end chord included
    CHECK(plain == 1);   // the blue after the phrase

    // The floor between 1.3 s and the far edge is the darkened energy colour.
    bool tinted = false;
    for (const DrawCommand& c : cmds) {
        if (c.mesh != MeshId::Flat || c.material.kind != MaterialKind::Color) continue;
        if (c.lo[2] <= time_to_z(cfg, 1.0, 1.3, 1.0) + 1e-4 && c.material.color.g > c.material.color.r)
            tinted = true;
    }
    CHECK(tinted);
}

TEST_CASE("build_highway_draws: the taken fill lights its lane with the lit target") {
    PreviewConfig cfg;
    PreviewScene scene = timed_scene();
    scene.notes = {note(1000.0, PreviewLane::Red), note(1500.0, PreviewLane::Green)};
    scene.fills = {fill(span(1300.0, 1500.0), PreviewFillState::Taken)};
    PreviewActivation a;
    a.tick = 1500;
    a.ms = 1500.0;
    a.has_lane = true;
    a.lane = PreviewLane::Green;
    scene.activations = {a};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 1.0, 1.0);
    int lanes = 0, lit_green = 0;
    for (const DrawCommand& c : cmds) {
        if (c.material.texture >= TextureId::LaneRed && c.material.texture <= TextureId::LaneGreen) {
            ++lanes;
            CHECK(c.alpha == doctest::Approx(1.0f));  // taken fills are full strength
        }
        if (c.material.texture == TextureId::TargetGreenLight) {
            ++lit_green;
            // The extra pass covers the fill's stretch, not the strike line.
            CHECK(c.lo[2] == doctest::Approx(time_to_z(cfg, 1.0, 1.3, 1.0)));
            CHECK(c.hi[2] == doctest::Approx(time_to_z(cfg, 1.0, 1.5005, 1.0)));
            CHECK(c.lo[0] == doctest::Approx(0.5f));  // the green lane's x span
            CHECK(c.hi[0] == doctest::Approx(1.0f));
        }
    }
    CHECK(lanes == 4);      // all four lanes light, Onyx's BRE look
    CHECK(lit_green == 1);  // the activation lane, drawn once more on top
}

TEST_CASE("build_highway_draws: an offered fill's strips are dimmed") {
    PreviewConfig cfg;
    PreviewScene scene = timed_scene();
    scene.notes = {note(1000.0, PreviewLane::Red), note(1500.0, PreviewLane::Green)};
    scene.fills = {fill(span(1300.0, 1500.0), PreviewFillState::Offered)};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 1.0, 1.0);
    int lanes = 0, lit = 0;
    for (const DrawCommand& c : cmds) {
        if (c.material.texture >= TextureId::LaneRed && c.material.texture <= TextureId::LaneGreen) {
            ++lanes;
            CHECK(c.alpha == doctest::Approx(cfg.hydra.fill_offered_alpha));
        }
        if (c.material.texture == TextureId::TargetGreenLight) ++lit;
    }
    CHECK(lanes == 4);
    CHECK(lit == 0);  // nothing was activated here
}

TEST_CASE("build_highway_draws: a hidden fill draws no lane strips") {
    PreviewConfig cfg;
    PreviewScene scene;
    scene.notes = {note(1000.0, PreviewLane::Red), note(1500.0, PreviewLane::Green)};
    scene.fills = {fill(span(1300.0, 1500.0), PreviewFillState::Hidden)};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 1.0, 1.0);
    int lanes = 0;
    for (const DrawCommand& c : cmds)
        if (c.material.texture >= TextureId::LaneRed && c.material.texture <= TextureId::LaneGreen)
            ++lanes;
    CHECK(lanes == 0);
}

TEST_CASE("build_highway_draws: an empty window still draws floor, railings and targets") {
    PreviewConfig cfg;
    PreviewScene scene;
    scene.notes = {note(10000.0, PreviewLane::Red)};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 1.0, 1.0);
    // One floor flat (no solo/SP cuts), two railings, four targets.
    CHECK(cmds.size() == 1 + 2 + 4);
    CHECK(cmds[0].lo[2] == doctest::Approx(2.0f));
    CHECK(cmds[0].hi[2] == doctest::Approx(-12.0f));
}

TEST_CASE("texture_file names every texture") {
    for (int i = 1; i < static_cast<int>(TextureId::Count); ++i)
        CHECK(texture_file(static_cast<TextureId>(i))[0] != '\0');
    CHECK(std::string(texture_file(TextureId::LongKick)) == "long-kick.jpg");
}

TEST_CASE("build_highway_draws: a chord one tick after a phrase ends draws plain") {
    PreviewConfig cfg;
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
    scene.notes = {at_tick(480, PreviewLane::Yellow), at_tick(481, PreviewLane::Blue)};
    PreviewSpan phrase;
    phrase.start_tick = 0;
    phrase.end_tick = 480;
    phrase.start_ms = 0.0;
    phrase.end_ms = timing.ms_index().at(480);
    scene.sp_phrases = {phrase};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 0.0, 1.0);

    int energy = 0, plain_blue = 0;
    for (const DrawCommand& c : cmds) {
        if (c.material.texture == TextureId::BoxEnergy) ++energy;
        if (c.material.texture == TextureId::BoxBlue) ++plain_blue;
    }
    CHECK(energy == 1);      // the yellow on the phrase's last tick
    CHECK(plain_blue == 1);  // the blue one tick later
}
