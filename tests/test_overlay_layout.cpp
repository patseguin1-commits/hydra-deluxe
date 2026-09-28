// Tests for render/overlay_layout: where the highway lands on screen, and the
// scale that keeps the Preview's text boxes beside it. Device-free.

#include "doctest.h"

#include "render/highway_draw.h"
#include "render/overlay_layout.h"
#include "render/preview_config.h"

using namespace hydra::render;

TEST_CASE("track_height: the width ratio, capped by the image height") {
    PreviewConfig cfg;  // height_width_ratio 1.1666...
    CHECK(track_height(cfg, 1000, 300) == 300);  // height-limited: the usual case
    CHECK(track_height(cfg, 300, 1000) == 350);  // 300 * 1.1666 = 350
    CHECK(track_height(cfg, 0, 0) == 1);
}

TEST_CASE("highway_span_at: passes through the projected railing corners") {
    PreviewConfig cfg;
    const PreviewConfig::Track& T = cfg.track;
    const int w = 1200, h = 400;
    const float xr = T.x_right + T.railing_x_width;
    const float xl = T.x_left - T.railing_x_width;
    // The strike line lies between the railing's two ends, so both edges run
    // through its corners at their rows (or outside them, the wider line winning).
    const ImagePoint r = project_to_image(cfg, w, h, {xr, T.railing_y_top, T.z_now});
    const ImagePoint l = project_to_image(cfg, w, h, {xl, T.railing_y_top, T.z_now});
    CHECK(highway_span_at(cfg, w, h, r.y).right >= r.x - 0.01f);
    CHECK(highway_span_at(cfg, w, h, l.y).left <= l.x + 0.01f);
    // A point on a lane lies inside the span at its own row.
    const ImagePoint lane = project_to_image(cfg, w, h, {0.9f, T.y, -6.0f});
    const HighwaySpan s = highway_span_at(cfg, w, h, lane.y);
    CHECK(lane.x < s.right);
    CHECK(lane.x > s.left);
}

TEST_CASE("highway_span_at: symmetric, and wider nearer the camera") {
    PreviewConfig cfg;
    const int w = 1200, h = 400;
    const HighwaySpan top = highway_span_at(cfg, w, h, 60.0f);
    const HighwaySpan bottom = highway_span_at(cfg, w, h, 390.0f);
    CHECK(top.left + top.right == doctest::Approx(static_cast<double>(w)).epsilon(0.001));
    CHECK(bottom.left + bottom.right == doctest::Approx(static_cast<double>(w)).epsilon(0.001));
    CHECK(bottom.right - bottom.left > top.right - top.left);
}

TEST_CASE("highway_span_at: a narrower image leaves the highway's size alone") {
    // The track height follows the image height here, so the highway is the
    // same size; a narrower image only trims the empty sides.
    PreviewConfig cfg;
    const HighwaySpan wide = highway_span_at(cfg, 1200, 400, 390.0f);
    const HighwaySpan narrow = highway_span_at(cfg, 600, 400, 390.0f);
    CHECK(narrow.right - narrow.left ==
          doctest::Approx(static_cast<double>(wide.right - wide.left)).epsilon(0.01));
}

TEST_CASE("overlay_scale: full size with room, floored when narrow, clear in between") {
    PreviewConfig cfg;
    const int h = 390;
    auto boxes = [](int w) {
        OverlayBoxes b;
        b.left_w = 265.0f;
        b.left_h = 150.0f;
        b.right_w = 200.0f;
        b.right_h = 70.0f;
        b.right_top = 10.0f;
        b.right_edge = static_cast<float>(w) - 30.0f;
        b.gap = 6.0f;
        return b;
    };
    CHECK(overlay_scale(cfg, 1200, h, boxes(1200)) == 1.0f);
    CHECK(overlay_scale(cfg, 150, h, boxes(150)) == doctest::Approx(kOverlayMinScale));

    float prev = 0.0f;
    bool saw_partial = false;
    for (int w = 150; w <= 1200; w += 10) {
        const OverlayBoxes b = boxes(w);
        const float s = overlay_scale(cfg, w, h, b);
        CHECK(s >= prev - 1e-6f);  // never falls as the image widens
        prev = s;
        if (s > kOverlayMinScale && s < 1.0f) {
            saw_partial = true;
            const float left_room = highway_span_at(cfg, w, h, b.left_h).left - b.gap;
            const float right_room =
                b.right_edge - highway_span_at(cfg, w, h, b.right_top + b.right_h).right - b.gap;
            CHECK(b.left_w * s <= left_room + 0.01f);
            CHECK(b.right_w * s <= right_room + 0.01f);
        }
    }
    CHECK(saw_partial);
    // No boxes: nothing to fit.
    CHECK(overlay_scale(cfg, 300, h, OverlayBoxes{}) == 1.0f);
}

namespace {
// 10 px per character (not per byte), "…" included: a fixed-pitch stand-in
// for the monospace font the path picker draws with.
float ten_per_char(const std::string& s) {
    float w = 0.0f;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) w += 10.0f;
    return w;
}
}  // namespace

TEST_CASE("ellipsize: whole when it fits, cut and ended in an ellipsis when not") {
    const std::string label = "3- 1 2  (optimal)";  // 17 characters, 170 px
    CHECK(ellipsize(label, 170.0f, ten_per_char) == label);
    CHECK(ellipsize(label, 500.0f, ten_per_char) == label);
    // One px short: the end goes, an ellipsis takes its place, and the result fits.
    const std::string cut = ellipsize(label, 169.0f, ten_per_char);
    CHECK(cut == "3- 1 2  (optima\xE2\x80\xA6");
    CHECK(ten_per_char(cut) <= 169.0f);
    // A cut that lands on the two spaces drops them rather than end in "  …".
    CHECK(ellipsize(label, 80.0f, ten_per_char) == "3- 1 2\xE2\x80\xA6");
    // Not even one character and the ellipsis: the ellipsis alone.
    CHECK(ellipsize(label, 15.0f, ten_per_char) == "\xE2\x80\xA6");
    CHECK(ellipsize("", 0.0f, ten_per_char).empty());
}

TEST_CASE("ellipsize: a 40-activation path fits its line, and a cut never splits a character") {
    // The longest real paths: 40 activations and "  (optimal)", about 100 characters.
    std::string label;
    for (int i = 0; i < 40; ++i) label += (i % 3 == 0 ? "2- " : "1 ");
    label += " (optimal)";
    for (float w : {600.0f, 420.0f, 333.0f, 95.0f}) {
        const std::string shown = ellipsize(label, w, ten_per_char);
        CHECK(ten_per_char(shown) <= w);
        CHECK(shown.size() >= 3);
        CHECK(shown.substr(shown.size() - 3) == "\xE2\x80\xA6");
        CHECK(label.rfind(shown.substr(0, shown.size() - 3), 0) == 0);  // a prefix of it
    }
    // A multi-byte character is kept whole or dropped whole.
    const std::string accented = "Caf\xC3\xA9 \xC3\xA9t\xC3\xA9";  // "Café été", 8 characters
    CHECK(ellipsize(accented, 50.0f, ten_per_char) == "Caf\xC3\xA9\xE2\x80\xA6");
    CHECK(ellipsize(accented, 40.0f, ten_per_char) == "Caf\xE2\x80\xA6");
}
