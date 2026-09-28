// See overlay_layout.h.

#include "render/overlay_layout.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <DirectXMath.h>

#include "render/highway_draw.h"

using namespace DirectX;

namespace hydra::render {

ImagePoint project_to_image(const PreviewConfig& cfg, int width, int height, const Vec3& p) {
    const int w = std::max(1, width);
    const int h = std::max(1, height);
    const int th = track_height(cfg, w, h);
    const HighwayCamera cam = make_camera(cfg, static_cast<float>(w) / static_cast<float>(th));
    const XMMATRIX view_proj = XMLoadFloat4x4(&cam.view) * XMLoadFloat4x4(&cam.proj);
    const XMVECTOR ndc = XMVector3TransformCoord(XMVectorSet(p.x, p.y, p.z, 1.0f), view_proj);
    ImagePoint out;
    out.x = (XMVectorGetX(ndc) + 1.0f) * 0.5f * static_cast<float>(w);
    // The track rectangle hugs the bottom of the image.
    out.y = static_cast<float>(h - th) + (1.0f - XMVectorGetY(ndc)) * 0.5f * static_cast<float>(th);
    return out;
}

HighwaySpan highway_span_at(const PreviewConfig& cfg, int width, int height, float y) {
    const PreviewConfig::Track& T = cfg.track;
    // x at row y on the screen line through a railing edge's two ends.
    auto x_at = [&](float wx, float wy) {
        const ImagePoint a = project_to_image(cfg, width, height, {wx, wy, T.z_past});
        const ImagePoint b = project_to_image(cfg, width, height, {wx, wy, T.z_future});
        if (std::fabs(a.y - b.y) < 1e-3f) return a.x;
        const float row = std::max(y, b.y);  // above the far end: the far end's edge
        return a.x + (row - a.y) * (b.x - a.x) / (b.y - a.y);
    };
    const float xl = T.x_left - T.railing_x_width;
    const float xr = T.x_right + T.railing_x_width;
    HighwaySpan s;
    s.left = std::min(x_at(xl, T.railing_y_top), x_at(xl, T.railing_y_bottom));
    s.right = std::max(x_at(xr, T.railing_y_top), x_at(xr, T.railing_y_bottom));
    return s;
}

float overlay_scale(const PreviewConfig& cfg, int width, int height, const OverlayBoxes& boxes,
                    float min_scale) {
    float scale = 1.0f;
    if (boxes.left_w > 0.0f) {
        const float room = highway_span_at(cfg, width, height, boxes.left_h).left - boxes.gap;
        scale = std::min(scale, room / boxes.left_w);
    }
    if (boxes.right_w > 0.0f) {
        const float bottom = boxes.right_top + boxes.right_h;
        const float room =
            boxes.right_edge - highway_span_at(cfg, width, height, bottom).right - boxes.gap;
        scale = std::min(scale, room / boxes.right_w);
    }
    return std::clamp(scale, min_scale, 1.0f);
}

std::string ellipsize(const std::string& text, float max_w,
                      const std::function<float(const std::string&)>& width_of) {
    if (width_of(text) <= max_w) return text;
    static const std::string kEllipsis = "\xE2\x80\xA6";
    // The places the text may be cut: every character's start, past the
    // first character. A UTF-8 continuation byte (10xxxxxx) starts none.
    std::vector<size_t> cuts;
    for (size_t i = 1; i < text.size(); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) cuts.push_back(i);
    auto shown = [&](size_t cut) {
        size_t end = cut;
        while (end > 0 && text[end - 1] == ' ') --end;
        return text.substr(0, end) + kEllipsis;
    };
    // The longest cut that fits: a longer prefix is never narrower.
    size_t lo = 0, hi = cuts.size();  // cuts[0..lo) fit; cuts[hi..) do not
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (width_of(shown(cuts[mid])) <= max_w)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo == 0 ? kEllipsis : shown(cuts[lo - 1]);
}

}  // namespace hydra::render
