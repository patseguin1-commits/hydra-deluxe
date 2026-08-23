// The golden-image test: Hydra's Preview is a faithful port of Onyx's 3D drum
// previewer, so a Hydra frame of a chart must match a screenshot of Onyx
// showing the same chart at the same time.
//
// Fixture (testdata/preview/golden.json, captured by hand from Onyx — see
// docs/adr/0008 for the procedure):
//   { "chart": "notes.mid", "time_ms": 32000, "width": 900, "height": 800,
//     "pro": true, "bass2x": true, "tolerance": 20,
//     "mask": [[x, y, w, h], ...] }   // rectangles to ignore (Onyx's text)
// plus golden_onyx.png next to it. Without the fixture the test reports that
// it skipped, so the suite passes before the capture exists.
//
// Dev aid: set HYDRA_PREVIEW_DUMP="<chart path>|<time ms>|<out.bmp>" to
// render any chart at any time to a BMP (the failing golden comparison also
// writes build-cpp/preview_actual.bmp).

#include "doctest.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "app/preview_view.h"
#include "image/decode.h"
#include "json.hpp"
#include "parse/song.h"
#include "render/file_util.h"
#include "render/preview_renderer.h"
#include "warp_util.h"

#ifndef HYDRA_ASSET_DIR
#error "HYDRA_ASSET_DIR must be defined (see CMakeLists.txt)"
#endif
#ifndef HYDRA_TESTDATA_DIR
#error "HYDRA_TESTDATA_DIR must be defined (see CMakeLists.txt)"
#endif

using namespace hydra;
using namespace hydra::render;
using Microsoft::WRL::ComPtr;

namespace {

const std::string kFixtureDir = std::string(HYDRA_TESTDATA_DIR) + "/preview";

// Write tightly-packed RGBA (top row first) as a 24-bit BMP.
void write_bmp(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
    const int row = (w * 3 + 3) & ~3;
    const uint32_t data = static_cast<uint32_t>(row) * h;
    const uint32_t size = 54 + data;
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) hdr[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, static_cast<uint32_t>(w));
    put32(22, static_cast<uint32_t>(h)); hdr[26] = 1; hdr[28] = 24; put32(34, data);
    f.write(reinterpret_cast<const char*>(hdr), 54);
    std::vector<uint8_t> line(static_cast<size_t>(row), 0);
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
            line[static_cast<size_t>(x) * 3] = p[2];
            line[static_cast<size_t>(x) * 3 + 1] = p[1];
            line[static_cast<size_t>(x) * 3 + 2] = p[0];
        }
        f.write(reinterpret_cast<const char*>(line.data()), row);
    }
}

// Render `chart` at `time_ms` into W x H with the real assets on WARP.
std::vector<uint8_t> render_chart(const std::string& chart, double time_ms, int w, int h,
                                  bool pro, bool bass2x) {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    REQUIRE(warp::make_device(dev, ctx));

    // Analyze so the path overlay (active SP windows, fills) is present.
    app::AnalysisSettings settings;
    settings.depth_mode = 0;
    settings.depth_value = 10;
    settings.ms_filter = 10.0;
    settings.prodrums = pro;
    settings.bass2x = bass2x;
    app::PreviewScene scene;
    try {
        app::AnalysisResult r = app::analyze_chart_file(chart, settings);
        const Path* best = r.record.paths.empty() ? nullptr : &r.record.best_path();
        scene = app::build_preview_scene(r.song, best);
    } catch (const std::exception&) {
        scene = app::build_preview_scene(load_songpath(chart, pro, bass2x), nullptr);
    }

    PreviewRenderer r(dev.Get(), ctx.Get(), HYDRA_ASSET_DIR);
    r.resize(w, h);
    TrackStateOptions opts;
    opts.pro = pro;
    r.set_scene(scene, opts);
    r.render(time_ms, RenderParams{});
    return warp::read_pixels(dev.Get(), ctx.Get(), r.texture_srv(), w, h);
}

// Box-filter downsample by 2 (RGB only).
std::vector<float> half_res(const std::vector<uint8_t>& rgba, int w, int h, int& ow, int& oh) {
    ow = w / 2;
    oh = h / 2;
    std::vector<float> out(static_cast<size_t>(ow) * oh * 3);
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x)
            for (int c = 0; c < 3; ++c) {
                float s = 0.0f;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx)
                        s += rgba[((static_cast<size_t>(y) * 2 + dy) * w + (x * 2 + dx)) * 4 + c];
                out[(static_cast<size_t>(y) * ow + x) * 3 + c] = s / 4.0f;
            }
    return out;
}

}  // namespace

TEST_CASE("preview golden: a Hydra frame matches the Onyx screenshot") {
    const std::string spec_path = kFixtureDir + "/golden.json";
    std::string spec_text = read_file_text(spec_path);
    if (spec_text.empty()) {
        MESSAGE("golden fixture absent (" << spec_path << "): skipped");
        return;
    }
    nlohmann::json spec = nlohmann::json::parse(spec_text);
    const std::string chart = kFixtureDir + "/" + spec.value("chart", "notes.mid");
    double time_ms = spec.value("time_ms", 0.0);
    if (const char* t = std::getenv("HYDRA_PREVIEW_GOLDEN_TIME")) time_ms = std::atof(t);  // dev aid
    const int w = spec.value("width", 0), h = spec.value("height", 0);
    const bool pro = spec.value("pro", true), bass2x = spec.value("bass2x", true);
    const double tolerance = spec.value("tolerance", 20.0);
    REQUIRE(w > 0);
    REQUIRE(h > 0);

    std::vector<uint8_t> golden_bytes = read_file_bytes(kFixtureDir + "/golden_onyx.png");
    REQUIRE(!golden_bytes.empty());
    image::DecodedImage golden = image::decode_image(golden_bytes);
    // The screenshot may be the whole Onyx window; "crop": [x, y, w, h]
    // names the highway area inside it (the part Hydra renders).
    if (spec.contains("crop")) {
        std::vector<int> c = spec["crop"].get<std::vector<int>>();
        REQUIRE(c.size() == 4);
        REQUIRE(c[2] == w);
        REQUIRE(c[3] == h);
        REQUIRE(c[0] + w <= golden.width);
        REQUIRE(c[1] + h <= golden.height);
        std::vector<uint8_t> cropped(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
            std::memcpy(&cropped[static_cast<size_t>(y) * w * 4],
                        &golden.rgba[((static_cast<size_t>(c[1]) + y) * golden.width + c[0]) * 4],
                        static_cast<size_t>(w) * 4);
        golden.rgba.swap(cropped);
        golden.width = w;
        golden.height = h;
    }
    REQUIRE(golden.width == w);
    REQUIRE(golden.height == h);

    std::vector<uint8_t> actual = render_chart(chart, time_ms, w, h, pro, bass2x);
    if (std::getenv("HYDRA_PREVIEW_GOLDEN_DUMP")) {
        write_bmp("preview_actual.bmp", actual, w, h);
        write_bmp("preview_golden.bmp", golden.rgba, w, h);
    }

    // Masks (in full-res pixels) cover Onyx's on-screen text.
    std::vector<std::vector<int>> masks;
    if (spec.contains("mask"))
        for (const auto& m : spec["mask"]) masks.push_back(m.get<std::vector<int>>());
    auto masked = [&](int x, int y) {
        for (const auto& m : masks)
            if (m.size() == 4 && x >= m[0] && x < m[0] + m[2] && y >= m[1] && y < m[1] + m[3]) return true;
        return false;
    };

    int ow, oh;
    std::vector<float> a = half_res(actual, w, h, ow, oh);
    std::vector<float> g = half_res(golden.rgba, w, h, ow, oh);
    double err = 0.0;
    size_t n = 0;
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            if (masked(x * 2, y * 2)) continue;
            for (int c = 0; c < 3; ++c) {
                err += std::fabs(a[(static_cast<size_t>(y) * ow + x) * 3 + c] -
                                 g[(static_cast<size_t>(y) * ow + x) * 3 + c]);
                ++n;
            }
        }
    const double mae = n ? err / static_cast<double>(n) : 0.0;
    MESSAGE("golden mean abs error: " << mae << " / 255 (tolerance " << tolerance << ")");
    if (mae >= tolerance) write_bmp("preview_actual.bmp", actual, w, h);
    CHECK(mae < tolerance);
}

TEST_CASE("preview dump (dev aid, HYDRA_PREVIEW_DUMP)") {
    const char* env = std::getenv("HYDRA_PREVIEW_DUMP");
    if (!env) return;
    std::string s = env;
    size_t a = s.find('|'), b = s.rfind('|');
    REQUIRE(a != std::string::npos);
    REQUIRE(b != a);
    const std::string chart = s.substr(0, a);
    const double time_ms = std::atof(s.substr(a + 1, b - a - 1).c_str());
    const std::string out = s.substr(b + 1);
    const int w = 900, h = 800;
    const bool bass2x = std::getenv("HYDRA_PREVIEW_DUMP_NO2X") == nullptr;
    std::vector<uint8_t> px = render_chart(chart, time_ms, w, h, true, bass2x);
    write_bmp(out, px, w, h);
    MESSAGE("wrote " << out);
    // Where Hydra thinks that time is, for lining up with Onyx's time box.
    app::PreviewScene scene = app::build_preview_scene(load_songpath(chart, true, bass2x), nullptr);
    app::PreviewTimeBox box = app::build_time_box(scene, time_ms);
    MESSAGE("time box: " << box.timestamp << " | " << box.measure_beat << " | " << box.bpm);
    for (size_t i = 0; i < scene.tempos.size() && i < 6; ++i)
        MESSAGE("tempo[" << i << "] tick " << scene.tempos[i].tick << " ms " << scene.tempos[i].ms
                         << " bpm " << scene.tempos[i].bpm);
    MESSAGE("first note ms " << (scene.notes.empty() ? -1.0 : scene.notes.front().ms)
                             << " tick " << (scene.notes.empty() ? -1 : scene.notes.front().tick));
    // The notes in the visible window, for lining up against an Onyx frame.
    for (const app::PreviewNote& n : scene.notes) {
        if (n.ms < time_ms - 500.0 || n.ms > time_ms + 1500.0) continue;
        bool in_sp = false;
        for (const app::PreviewSpan& s : scene.sp_phrases)
            if (s.start_ms <= n.ms && n.ms <= s.end_ms) in_sp = true;
        std::string flags;
        if (n.cymbal) flags += " cymbal";
        if (n.ghost) flags += " ghost";
        if (n.accent) flags += " accent";
        if (in_sp) flags += " SP";
        MESSAGE("note +" << (n.ms - time_ms) << " ms tick " << n.tick << " lane "
                         << static_cast<int>(n.lane) << flags);
    }
    for (const app::PreviewSpan& s : scene.sp_phrases)
        if (s.end_ms >= time_ms - 2000.0 && s.start_ms <= time_ms + 3000.0)
            MESSAGE("sp phrase " << s.start_ms << " .. " << s.end_ms << " ms");
    std::ofstream tcsv(out + ".tempos.csv");
    for (const app::PreviewTempo& t : scene.tempos) tcsv << t.ms << "," << t.tick << "," << t.bpm << "\n";
    // Every note as CSV (ms,tick,lane,cymbal,sp) beside the image.
    std::ofstream csv(out + ".csv");
    for (const app::PreviewNote& n : scene.notes) {
        bool in_sp = false;
        for (const app::PreviewSpan& s : scene.sp_phrases)
            if (s.start_ms <= n.ms && n.ms <= s.end_ms) in_sp = true;
        csv << n.ms << "," << n.tick << "," << static_cast<int>(n.lane) << "," << (n.cymbal ? 1 : 0)
            << "," << (in_sp ? 1 : 0) << "\n";
    }
}
