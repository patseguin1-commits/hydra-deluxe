// PreviewRenderer — executes the highway draw list on Direct3D 11 into an
// offscreen texture the GUI shows with ImGui::Image (docs/adr/0005).
//
// This is the device half of the Onyx port (docs/adr/0008): the pure half
// (track_state + highway_draw) decides what to draw and where; this class
// loads the verbatim Onyx assets from `asset_dir` (models, textures, the
// config JSON, the HLSL shader files), owns the anti-aliased scene target,
// draws each command with the ported Phong shader, resolves, and composites
// the highway onto the background colour with Onyx's horizon fade.
//
// The track rectangle is anchored at the bottom of the target and is
// min(height, width * height_width_ratio) tall, as Onyx lays out one highway.

#ifndef HYDRA_RENDER_PREVIEW_RENDERER_H
#define HYDRA_RENDER_PREVIEW_RENDERER_H

#include <d3d11.h>

#include <string>

#include "app/preview_view.h"
#include "render/preview_config.h"
#include "render/track_state.h"

namespace hydra::render {

class PreviewRenderer {
public:
    // Loads every asset from `asset_dir` (assets/preview) and builds the
    // pipeline on the shared device. Throws std::runtime_error naming the
    // missing or broken asset / D3D object.
    PreviewRenderer(ID3D11Device* device, ID3D11DeviceContext* context,
                    const std::string& asset_dir);
    ~PreviewRenderer();

    PreviewRenderer(const PreviewRenderer&) = delete;
    PreviewRenderer& operator=(const PreviewRenderer&) = delete;

    // (Re)create the size-dependent targets.
    void resize(int width, int height);

    // Replace the chart being drawn (builds the track state once).
    void set_scene(const hydra::app::PreviewScene& scene,
                   const TrackStateOptions& opts = TrackStateOptions{});

    // Draw the frame at `now_ms` into the offscreen target.
    void render(double now_ms);

    // The final colour texture's shader-resource view (null before resize).
    ID3D11ShaderResourceView* texture_srv() const;

    int width() const;
    int height() const;
    const PreviewConfig& config() const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace hydra::render

#endif  // HYDRA_RENDER_PREVIEW_RENDERER_H
