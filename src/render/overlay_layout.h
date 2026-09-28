// Where the highway lands on the Preview image, and the scale that keeps the
// Preview's text boxes (the time and score boxes top-left, the SP drain box
// top-right) beside it rather than on it. Device-free: the same camera and
// track height the renderer draws with, projected on the CPU.
//
// The highway's size follows the track height, not the image width, so a
// narrower window leaves less room at its sides. The boxes shrink into that
// room, never past kOverlayMinScale (below it the text stops being readable,
// so they overlap instead) and never above 1 (their configured size).
//
// Also the "…" cut the Preview's path picker uses for a label too long for
// its line (ellipsize), measured by the caller's font so it stays device-free.

#ifndef HYDRA_RENDER_OVERLAY_LAYOUT_H
#define HYDRA_RENDER_OVERLAY_LAYOUT_H

#include <functional>
#include <string>

#include "render/preview_config.h"

namespace hydra::render {

// A point on the image, in pixels from its top-left corner (x right, y down).
struct ImagePoint {
    float x = 0.0f;
    float y = 0.0f;
};

// Where world point `p` lands in a width x height Preview image, through the
// renderer's own camera, with the track rectangle anchored at the bottom.
ImagePoint project_to_image(const PreviewConfig& cfg, int width, int height, const Vec3& p);

// The highway's outer edges (the railings' outsides) at image row `y`. Each
// railing edge is a straight line on screen, read off its two projected ends;
// the railing's top and bottom give two lines and the wider one is returned.
// Above the far end the far end's edges are used: nothing is drawn there.
struct HighwaySpan {
    float left = 0.0f;
    float right = 0.0f;
};
HighwaySpan highway_span_at(const PreviewConfig& cfg, int width, int height, float y);

// The boxes to fit, at scale 1, in image pixels. A width of 0 means absent.
struct OverlayBoxes {
    float left_w = 0.0f;      // the top-left column (time box, score box under it)
    float left_h = 0.0f;      //   from the image's top edge
    float right_w = 0.0f;     // the SP drain box
    float right_h = 0.0f;
    float right_edge = 0.0f;  // x of the drain box's right edge, left of the gauge
    float right_top = 0.0f;   // y of the drain box's top edge
    float gap = 0.0f;         // clearance kept from the highway's edge
};

inline constexpr float kOverlayMinScale = 0.6f;

// One scale for all the boxes: the largest in [min_scale, 1] at which each box
// clears the highway at its lowest row (where the highway is widest within
// it). Rows are measured at scale 1, so a shrunken box is only more clear.
float overlay_scale(const PreviewConfig& cfg, int width, int height, const OverlayBoxes& boxes,
                    float min_scale = kOverlayMinScale);

// `text` as a box `max_w` wide shows it: whole when it fits, otherwise cut
// short and ended in "…" so the result fits. The cut falls between UTF-8
// characters, never inside one, and drops the spaces it would leave before the
// "…". When not even one character fits, the result is "…" alone. `width_of`
// measures a string in the font the box draws with. The Preview's path picker
// shows a path too long for its line this way.
std::string ellipsize(const std::string& text, float max_w,
                      const std::function<float(const std::string&)>& width_of);

}  // namespace hydra::render

#endif  // HYDRA_RENDER_OVERLAY_LAYOUT_H
