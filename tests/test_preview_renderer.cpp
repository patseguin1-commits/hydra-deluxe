// Tests for render/preview_renderer: the D3D11 executor of the Onyx port, run
// on a headless WARP device with the real assets (HYDRA_ASSET_DIR). Each case
// renders, reads the pixels back, and checks what Onyx's layout implies: the
// background colour outside the highway, a lit floor at the strike line, the
// horizon fade across the top of the track, gems adding pixels, MSAA, resize.

#include "doctest.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "app/preview_view.h"
#include "render/preview_renderer.h"
#include "warp_util.h"

#ifndef HYDRA_ASSET_DIR
#error "HYDRA_ASSET_DIR must be defined (see CMakeLists.txt)"
#endif

using namespace hydra::render;
using hydra::app::PreviewLane;
using hydra::app::PreviewNote;
using hydra::app::PreviewScene;
using Microsoft::WRL::ComPtr;

namespace {

const std::string kAssets = HYDRA_ASSET_DIR;

// Onyx's background #1c1d2b.
bool is_background(const uint8_t* p) {
    return std::abs(p[0] - 0x1c) <= 2 && std::abs(p[1] - 0x1d) <= 2 && std::abs(p[2] - 0x2b) <= 2;
}

int count_non_background(const std::vector<uint8_t>& img) {
    int n = 0;
    for (size_t i = 0; i < img.size(); i += 4)
        if (!is_background(&img[i])) ++n;
    return n;
}

PreviewNote note_at(double ms, PreviewLane lane, bool cymbal = false) {
    PreviewNote n;
    n.ms = ms;
    n.tick = static_cast<int64_t>(ms);
    n.lane = lane;
    n.cymbal = cymbal;
    return n;
}

}  // namespace

TEST_CASE("PreviewRenderer: bare highway — background outside, lit floor, horizon fade (WARP)") {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    REQUIRE(warp::make_device(dev, ctx));

    // Wider than tall: the track rect is the full target (h < w * 1.1666).
    const int W = 160, H = 120;
    PreviewRenderer r(dev.Get(), ctx.Get(), kAssets);
    r.resize(W, H);
    REQUIRE(r.texture_srv() != nullptr);
    CHECK(r.config().hydra.msaa == 4);

    PreviewScene empty;
    r.set_scene(empty);
    r.render(1000.0);
    std::vector<uint8_t> img = warp::read_pixels(dev.Get(), ctx.Get(), r.texture_srv(), W, H);

    // Every pixel is opaque (ImGui composites by alpha).
    for (size_t i = 3; i < img.size(); i += 4) REQUIRE(img[i] == 255);

    // Top corners: past the horizon fade, pure background.
    CHECK(is_background(warp::pixel(img, W, 1, 1)));
    CHECK(is_background(warp::pixel(img, W, W - 2, 1)));
    // The whole top 16% of the track is faded to background (fade ends at
    // 83.16% from the bottom).
    for (int y = 0; y < H * 16 / 100; ++y)
        CHECK(is_background(warp::pixel(img, W, W / 2, y)));

    // Just above the strike line, bottom centre: the lit grey floor.
    const uint8_t* floor = warp::pixel(img, W, W / 2, H - 8);
    CHECK_FALSE(is_background(floor));
    CHECK(floor[0] > 0x20);
    CHECK(std::abs(floor[0] - floor[1]) < 12);  // grey, not tinted
    CHECK(std::abs(floor[1] - floor[2]) < 12);

    // A fair amount of the frame is highway.
    CHECK(count_non_background(img) > W * H / 4);
}

TEST_CASE("PreviewRenderer: a gem at the strike line adds drawn pixels (WARP)") {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    REQUIRE(warp::make_device(dev, ctx));

    const int W = 160, H = 120;
    PreviewRenderer r(dev.Get(), ctx.Get(), kAssets);
    r.resize(W, H);

    PreviewScene empty;
    r.set_scene(empty);
    r.render(1000.0);
    std::vector<uint8_t> bare = warp::read_pixels(dev.Get(), ctx.Get(), r.texture_srv(), W, H);

    PreviewScene with_notes;
    with_notes.has_notes = true;
    with_notes.notes.push_back(note_at(1100.0, PreviewLane::Kick));
    with_notes.notes.push_back(note_at(1100.0, PreviewLane::Red));
    with_notes.notes.push_back(note_at(1300.0, PreviewLane::Yellow, true));
    with_notes.song_length_ms = 1300.0;
    r.set_scene(with_notes);
    r.render(1000.0);
    std::vector<uint8_t> gems = warp::read_pixels(dev.Get(), ctx.Get(), r.texture_srv(), W, H);

    int differing = 0;
    for (size_t i = 0; i < gems.size(); i += 4)
        if (gems[i] != bare[i] || gems[i + 1] != bare[i + 1] || gems[i + 2] != bare[i + 2])
            ++differing;
    CHECK(differing > 40);
}

TEST_CASE("PreviewRenderer: SP phrase energy gems and active SP floor change pixels (WARP)") {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    REQUIRE(warp::make_device(dev, ctx));

    const int W = 160, H = 120;
    PreviewRenderer r(dev.Get(), ctx.Get(), kAssets);
    r.resize(W, H);

    PreviewScene plain;
    plain.has_notes = true;
    plain.notes.push_back(note_at(1200.0, PreviewLane::Green));
    r.set_scene(plain);
    r.render(1000.0);
    std::vector<uint8_t> a = warp::read_pixels(dev.Get(), ctx.Get(), r.texture_srv(), W, H);

    PreviewScene lit = plain;
    // Spans end half a tick past their last note through the song's timing,
    // so a scene with a phrase carries one: one tick per ms (60 BPM at 1000
    // ticks per beat), matching note_at().
    lit.timing = hydra::SongTiming(1000, {{0, 4000}}, {{0, 60.0}});
    lit.tick_resolution = 1000;
    hydra::app::PreviewSpan phrase;
    phrase.start_ms = 1100.0;
    phrase.end_ms = 1200.0;
    phrase.start_tick = 1100;
    phrase.end_tick = 1200;
    lit.sp_phrases.push_back(phrase);
    hydra::app::PreviewActivation act;
    act.ms = 900.0;
    act.has_sp_end = true;
    act.sp_end_ms = 2500.0;
    lit.activations.push_back(act);
    r.set_scene(lit);
    r.render(1000.0);
    std::vector<uint8_t> b = warp::read_pixels(dev.Get(), ctx.Get(), r.texture_srv(), W, H);

    int differing = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ++differing;
    CHECK(differing > 200);
    // The floor at the strike line is now greener than red (energy tint).
    const uint8_t* floor = warp::pixel(b, W, W / 2, H - 8);
    CHECK(floor[1] > floor[0]);
}

TEST_CASE("PreviewRenderer: resize and a tall target keep the track at the bottom (WARP)") {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    REQUIRE(warp::make_device(dev, ctx));

    PreviewRenderer r(dev.Get(), ctx.Get(), kAssets);
    r.resize(128, 128);
    r.resize(100, 300);  // taller than 100 * 1.1666 = 117: the top is background
    CHECK(r.width() == 100);
    CHECK(r.height() == 300);
    PreviewScene scene;
    r.set_scene(scene);
    r.render(0.0);
    std::vector<uint8_t> img = warp::read_pixels(dev.Get(), ctx.Get(), r.texture_srv(), 100, 300);
    CHECK(img.size() == static_cast<size_t>(100) * 300 * 4);
    for (int y = 0; y < 300 - 117 - 2; y += 10) CHECK(is_background(warp::pixel(img, 100, 50, y)));
    CHECK_FALSE(is_background(warp::pixel(img, 100, 50, 292)));
}

TEST_CASE("PreviewRenderer: a missing asset dir is a clear error") {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    REQUIRE(warp::make_device(dev, ctx));
    CHECK_THROWS_AS(PreviewRenderer(dev.Get(), ctx.Get(), "C:\\no\\such\\dir"), std::runtime_error);
}
