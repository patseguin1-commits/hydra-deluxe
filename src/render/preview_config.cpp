#include "render/preview_config.h"

#include <cstdlib>
#include <stdexcept>

#include "json.hpp"

namespace hydra::render {

using json = nlohmann::json;

Color parse_hex_color(std::string_view text) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (text.empty() || text[0] != '#' || (text.size() != 7 && text.size() != 9))
        return Color{1, 0, 1, 1};
    float out[4] = {0, 0, 0, 1};
    for (size_t i = 0; i + 1 < text.size(); i += 2) {
        int hi = hex(text[1 + i]), lo = hex(text[2 + i]);
        if (hi < 0 || lo < 0) return Color{1, 0, 1, 1};
        out[i / 2] = static_cast<float>(hi * 16 + lo) / 255.0f;
    }
    return Color{out[0], out[1], out[2], out[3]};
}

namespace {

// Helpers that keep the default when a key is missing.
void get_f(const json& j, const char* key, float& out) {
    if (j.contains(key) && j[key].is_number()) out = j[key].get<float>();
}
void get_i(const json& j, const char* key, int& out) {
    if (j.contains(key) && j[key].is_number()) out = j[key].get<int>();
}
void get_color(const json& j, const char* key, Color& out) {
    if (j.contains(key) && j[key].is_string())
        out = parse_hex_color(j[key].get<std::string>());
}
void get_vec3(const json& j, const char* key, Vec3& out) {
    if (!j.contains(key) || !j[key].is_object()) return;
    const json& v = j[key];
    get_f(v, "x", out.x);
    get_f(v, "y", out.y);
    get_f(v, "z", out.z);
}
void get_light(const json& j, const char* key, LightConfig& out) {
    if (!j.contains(key) || !j[key].is_object()) return;
    const json& l = j[key];
    get_vec3(l, "position", out.position);
    get_color(l, "ambient", out.ambient);
    get_color(l, "diffuse", out.diffuse);
    get_color(l, "specular", out.specular);
}
const json& sub(const json& j, const char* key) {
    static const json empty = json::object();
    if (j.contains(key) && j[key].is_object()) return j[key];
    return empty;
}

}  // namespace

PreviewConfig load_preview_config(const std::string& json_text) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("3d-config.json: ") + e.what());
    }
    PreviewConfig c;

    const json& view = sub(root, "view");
    get_color(view, "background", c.view.background);
    get_f(sub(view, "track_fade"), "bottom", c.view.track_fade_bottom);
    get_f(sub(view, "track_fade"), "top", c.view.track_fade_top);
    get_f(view, "height_width_ratio", c.view.height_width_ratio);
    const json& cam = sub(view, "camera");
    get_vec3(cam, "position", c.view.camera_position);
    get_f(cam, "rotate", c.view.camera_rotate);
    get_f(cam, "fov", c.view.camera_fov);
    get_f(cam, "near", c.view.camera_near);
    get_f(cam, "far", c.view.camera_far);

    const json& track = sub(root, "track");
    get_f(track, "y", c.track.y);
    get_f(sub(track, "note_area"), "x_left", c.track.x_left);
    get_f(sub(track, "note_area"), "x_right", c.track.x_right);
    const json& time = sub(track, "time");
    get_f(time, "z_past", c.track.z_past);
    get_f(time, "z_now", c.track.z_now);
    get_f(time, "z_future", c.track.z_future);
    get_f(time, "secs_future", c.track.secs_future);
    get_color(sub(track, "color"), "normal", c.track.color_normal);
    get_color(sub(track, "color"), "solo", c.track.color_solo);
    const json& rail = sub(track, "railings");
    get_color(rail, "color", c.track.railing_color);
    get_f(rail, "x_width", c.track.railing_x_width);
    get_f(rail, "y_top", c.track.railing_y_top);
    get_f(rail, "y_bottom", c.track.railing_y_bottom);
    get_f(sub(track, "beats"), "z_past", c.track.beats_z_past);
    get_f(sub(track, "beats"), "z_future", c.track.beats_z_future);
    const json& targets = sub(track, "targets");
    get_f(targets, "z_past", c.track.targets_z_past);
    get_f(targets, "z_future", c.track.targets_z_future);
    get_f(targets, "secs_light", c.track.targets_secs_light);
    get_light(track, "light", c.track.light);

    const json& gems = sub(sub(root, "objects"), "gems");
    get_color(gems, "color_hit", c.gems.color_hit);
    get_f(gems, "secs_fade", c.gems.secs_fade);
    get_light(gems, "light", c.gems.light);

    const json& hy = sub(root, "hydra");
    get_i(hy, "msaa", c.hydra.msaa);
    get_color(hy, "sp_active_color", c.hydra.sp_active_color);
    get_f(hy, "sp_active_darken", c.hydra.sp_active_darken);
    get_f(hy, "fill_offered_alpha", c.hydra.fill_offered_alpha);

    return c;
}

}  // namespace hydra::render
