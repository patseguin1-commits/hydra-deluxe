// The Preview's layout and look as numbers: a struct mirroring Onyx's
// 3d-config.yml (shipped as assets/preview/3d-config.json), with Onyx's values
// as the defaults so a missing key changes nothing. Every camera, highway,
// light and timing constant the renderer uses comes from here — never from a
// literal in the draw code — so the port stays checkable against Onyx's file.
//
// Colours follow Onyx's `stackColor`: "#rrggbb" -> (r/255, g/255, b/255, 1).

#ifndef HYDRA_RENDER_PREVIEW_CONFIG_H
#define HYDRA_RENDER_PREVIEW_CONFIG_H

#include <string>
#include <string_view>

namespace hydra::render {

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct LightConfig {
    Vec3 position;
    Color ambient{0.2f, 0.2f, 0.2f, 1};  // #333333
    Color diffuse{1, 1, 1, 1};
    Color specular{1, 1, 1, 1};
};

struct PreviewConfig {
    struct View {
        Color background{0x1c / 255.0f, 0x1d / 255.0f, 0x2b / 255.0f, 1};
        float track_fade_bottom = 0.7734724292101341f;
        float track_fade_top = 0.8315946348733234f;
        float height_width_ratio = 1.1666666666f;
        Vec3 camera_position{0, 1.4f, 3};
        float camera_rotate = 25;  // degrees down, around X
        float camera_fov = 45;     // degrees
        float camera_near = 0.1f;
        float camera_far = 100;
    } view;

    struct Track {
        float y = -1;  // the floor
        float x_left = -1, x_right = 1;  // the note area
        float z_past = 2, z_now = 0, z_future = -12;
        float secs_future = 1.35f;  // events this far ahead sit at z_future
        Color color_normal{0.2f, 0.2f, 0.2f, 1};          // #333333
        Color color_solo{0.2f, 0.2f, 0.6f, 1};            // #333399
        Color railing_color{0x73 / 255.0f, 0x73 / 255.0f, 0x73 / 255.0f, 1};
        float railing_x_width = 0.09f;
        float railing_y_top = -0.85f, railing_y_bottom = -1.1f;
        float beats_z_past = 0.05f, beats_z_future = -0.05f;
        float targets_z_past = 0.13f, targets_z_future = -0.13f;
        float targets_secs_light = 0.1666666f;
        LightConfig light{{0, -0.5f, 0.5f}};
    } track;

    struct Gems {
        Color color_hit{1, 1, 1, 1};
        float secs_fade = 0.1f;
        LightConfig light{{0, 1, 0.2f}};  // relative to the gem's bottom centre
    } gems;

    // Onyx's text.time_box. Hydra draws the box in its own monospace font,
    // so the file's `font` key is not read; size and margin are.
    struct Text {
        float time_box_size = 15;
        float time_box_margin = 10;
    } text;

    // Hydra-only (not in Onyx's file).
    struct Hydra {
        int msaa = 4;  // mirrors Onyx's prefMSAA default
        Color sp_active_color{0x6c / 255.0f, 0xf7 / 255.0f, 0xc6 / 255.0f, 1};
        float sp_active_darken = 0.2f;
        // How dim a fill the path passed over draws, next to the taken one.
        float fill_offered_alpha = 0.35f;
    } hydra;
};

// "#rrggbb" or "#rrggbbaa" -> Color; anything else returns opaque magenta.
Color parse_hex_color(std::string_view text);

// Parse the JSON form of 3d-config.yml. Missing keys keep their defaults.
// Throws std::runtime_error on malformed JSON.
PreviewConfig load_preview_config(const std::string& json_text);

}  // namespace hydra::render

#endif  // HYDRA_RENDER_PREVIEW_CONFIG_H
