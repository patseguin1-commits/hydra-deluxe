// Highway draw list — the pure port of Onyx's `drawDrumPlay`: given the track
// state, the config and the current time, produce the ordered list of draw
// commands for one frame. Every Onyx number (camera, time->depth, lane
// extents, gem boxes, fade timings) is applied here, device-free and
// unit-tested; the renderer only executes commands.
//
// Coordinates are Onyx's: right-handed, +X right, +Y up, the floor at
// track.y, the strike line at z = track.z_now, the future toward -Z.
//
// Hydra vocabulary in identifiers; Onyx names in comments where they differ
// (Onyx "targets" = the strike line, "overdrive" = SP phrase, "BRE" lanes =
// the fill look).

#ifndef HYDRA_RENDER_HIGHWAY_DRAW_H
#define HYDRA_RENDER_HIGHWAY_DRAW_H

#include <vector>

#include <DirectXMath.h>

#include "render/preview_config.h"
#include "render/track_state.h"

namespace hydra::render {

enum class MeshId { Tom, Cymbal, Kick, Flat, Box };

enum class TextureId {
    None,
    BoxRed, BoxYellow, BoxBlue, BoxGreen, BoxEnergy,
    CymbalYellow, CymbalBlue, CymbalGreen, CymbalEnergy,
    LongKick, LongEnergy,
    OverlayGhost, OverlayAccent,
    Line1, Line2, Line3,
    TargetRed, TargetYellow, TargetBlue, TargetGreen,
    TargetRedLight, TargetYellowLight, TargetBlueLight, TargetGreenLight,
    LaneRed, LaneYellow, LaneBlue, LaneGreen,
    Count
};

// The asset file name under assets/preview/textures for a texture.
const char* texture_file(TextureId id);

enum class MaterialKind { Color, Texture, TextureOverlay };

struct Material {
    MaterialKind kind = MaterialKind::Color;
    Color color;                       // Color
    TextureId texture = TextureId::None;  // Texture / TextureOverlay base
    TextureId overlay = TextureId::None;  // TextureOverlay second layer
};

// Which light a draw uses: the highway's fixed light, or the per-gem light
// offset from the gem's top centre (Onyx LightOffset).
enum class LightKind { Global, GemOffset };

enum class DepthMode { Less, Always };

// Onyx's ObjectStretch: the unit mesh scaled and moved to fill the box lo..hi
// (a Flat keeps Y scale 1 since its box is flat).
struct DrawCommand {
    MeshId mesh = MeshId::Flat;
    float lo[3] = {0, 0, 0};
    float hi[3] = {0, 0, 0};
    Material material;
    float alpha = 1.0f;
    LightKind light = LightKind::Global;
    DepthMode depth = DepthMode::Less;
};

struct HighwayCamera {
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 proj;
    Vec3 view_pos;
};

// Onyx setUpTrackView: view = translate(-camera) then rotate down by
// `rotate` degrees about X; projection = perspective(fov, aspect, near, far),
// right-handed. `aspect` is the track rectangle's width / height.
HighwayCamera make_camera(const PreviewConfig& cfg, float aspect);

// Onyx timeToZ: z_now at `now`, z_future at now + secs_future * speed, linear.
double time_to_z(const PreviewConfig& cfg, double now_s, double t_s, double speed);
// Its inverse: the time at a depth (used for the window's near edge).
double z_to_time(const PreviewConfig& cfg, double now_s, double z, double speed);

// The X extent of a pad's lane: the note area split into four, left to right.
void pad_x(const PreviewConfig& cfg, Pad pad, float& x1, float& x2);

// The model matrix for a DrawCommand's box (row-major, DirectXMath row vectors).
DirectX::XMMATRIX stretch_matrix(const DrawCommand& cmd);

// The per-draw light: the highway light, or the gem light offset from the
// box's top centre.
LightConfig light_for(const PreviewConfig& cfg, const DrawCommand& cmd);

// One frame's draw list in Onyx's order: floor spans, railings, beat lines,
// lane strips, strike-line targets and their glows, then gems (far first).
std::vector<DrawCommand> build_highway_draws(const TrackState& state,
                                             const PreviewConfig& cfg,
                                             double now_s, double speed);

}  // namespace hydra::render

#endif  // HYDRA_RENDER_HIGHWAY_DRAW_H
