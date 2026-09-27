#include "render/highway_draw.h"

#include <algorithm>
#include <cmath>
#include <optional>

using namespace DirectX;

namespace hydra::render {

const char* texture_file(TextureId id) {
    switch (id) {
        case TextureId::BoxRed:            return "box-red.png";
        case TextureId::BoxYellow:         return "box-yellow.png";
        case TextureId::BoxBlue:           return "box-blue.png";
        case TextureId::BoxGreen:          return "box-green.png";
        case TextureId::BoxEnergy:         return "box-energy.png";
        case TextureId::CymbalYellow:      return "cymbal-yellow.png";
        case TextureId::CymbalBlue:        return "cymbal-blue.png";
        case TextureId::CymbalGreen:       return "cymbal-green.png";
        case TextureId::CymbalEnergy:      return "cymbal-energy.png";
        case TextureId::LongKick:          return "long-kick.jpg";
        case TextureId::LongEnergy:        return "long-energy.jpg";
        case TextureId::OverlayGhost:      return "overlay-ghost.png";
        case TextureId::OverlayAccent:     return "overlay-accent.png";
        case TextureId::Line1:             return "line-1.png";
        case TextureId::Line2:             return "line-2.png";
        case TextureId::Line3:             return "line-3.png";
        case TextureId::TargetRed:         return "target-red.png";
        case TextureId::TargetYellow:      return "target-yellow.png";
        case TextureId::TargetBlue:        return "target-blue.png";
        case TextureId::TargetGreen:       return "target-green.png";
        case TextureId::TargetRedLight:    return "target-red-light.png";
        case TextureId::TargetYellowLight: return "target-yellow-light.png";
        case TextureId::TargetBlueLight:   return "target-blue-light.png";
        case TextureId::TargetGreenLight:  return "target-green-light.png";
        case TextureId::LaneRed:           return "lane-red.png";
        case TextureId::LaneYellow:        return "lane-yellow.png";
        case TextureId::LaneBlue:          return "lane-blue.png";
        case TextureId::LaneGreen:         return "lane-green.png";
        case TextureId::None:
        case TextureId::Count:             break;
    }
    return "";
}

HighwayCamera make_camera(const PreviewConfig& cfg, float aspect) {
    HighwayCamera cam;
    const Vec3 p = cfg.view.camera_position;
    // Onyx: view = rotateX(tilt) . translate(-pos) on column vectors, i.e.
    // translate first, then rotate — the same order with row vectors.
    XMMATRIX view = XMMatrixTranslation(-p.x, -p.y, -p.z) *
                    XMMatrixRotationX(XMConvertToRadians(cfg.view.camera_rotate));
    XMMATRIX proj = XMMatrixPerspectiveFovRH(XMConvertToRadians(cfg.view.camera_fov),
                                             aspect, cfg.view.camera_near,
                                             cfg.view.camera_far);
    XMStoreFloat4x4(&cam.view, view);
    XMStoreFloat4x4(&cam.proj, proj);
    cam.view_pos = p;
    return cam;
}

int track_height(const PreviewConfig& cfg, int width, int height) {
    const int w = std::max(1, width);
    const int h = std::max(1, height);
    const int t = std::min(h, static_cast<int>(std::lround(w * cfg.view.height_width_ratio)));
    return std::max(1, t);
}

double time_to_z(const PreviewConfig& cfg, double now_s, double t_s, double speed) {
    const double far_time = now_s + speed * cfg.track.secs_future;
    return cfg.track.z_now +
           (cfg.track.z_future - cfg.track.z_now) * ((t_s - now_s) / (far_time - now_s));
}

double z_to_time(const PreviewConfig& cfg, double now_s, double z, double speed) {
    const double far_time = now_s + speed * cfg.track.secs_future;
    return now_s + (far_time - now_s) * ((z - cfg.track.z_now) /
                                         (cfg.track.z_future - cfg.track.z_now));
}

void pad_x(const PreviewConfig& cfg, Pad pad, float& x1, float& x2) {
    const float w = (cfg.track.x_right - cfg.track.x_left) / 4.0f;
    x1 = cfg.track.x_left + static_cast<int>(pad) * w;
    x2 = x1 + w;
}

XMMATRIX stretch_matrix(const DrawCommand& cmd) {
    float sx = std::fabs(cmd.hi[0] - cmd.lo[0]);
    float sy = std::fabs(cmd.hi[1] - cmd.lo[1]);
    float sz = std::fabs(cmd.hi[2] - cmd.lo[2]);
    if (sy == 0.0f) sy = 1.0f;  // Onyx Flat: yScale 1
    const float cx = (cmd.lo[0] + cmd.hi[0]) * 0.5f;
    const float cy = (cmd.lo[1] + cmd.hi[1]) * 0.5f;
    const float cz = (cmd.lo[2] + cmd.hi[2]) * 0.5f;
    return XMMatrixScaling(sx, sy, sz) * XMMatrixTranslation(cx, cy, cz);
}

LightConfig light_for(const PreviewConfig& cfg, const DrawCommand& cmd) {
    if (cmd.light == LightKind::Global) return cfg.track.light;
    // Onyx LightOffset: relative to the box's top centre.
    LightConfig l = cfg.gems.light;
    l.position.x += (cmd.lo[0] + cmd.hi[0]) * 0.5f;
    l.position.y += std::max(cmd.lo[1], cmd.hi[1]);
    l.position.z += (cmd.lo[2] + cmd.hi[2]) * 0.5f;
    return l;
}

namespace {

DrawCommand flat(float x1, float y, float z1, float x2, float z2, Material mat,
                 float alpha = 1.0f, DepthMode depth = DepthMode::Less) {
    DrawCommand c;
    c.mesh = MeshId::Flat;
    c.lo[0] = x1; c.lo[1] = y; c.lo[2] = z1;
    c.hi[0] = x2; c.hi[1] = y; c.hi[2] = z2;
    c.material = mat;
    c.alpha = alpha;
    c.light = LightKind::Global;
    c.depth = depth;
    return c;
}

Material color_mat(Color c) {
    Material m;
    m.kind = MaterialKind::Color;
    m.color = c;
    return m;
}

Material tex_mat(TextureId t) {
    Material m;
    m.kind = MaterialKind::Texture;
    m.texture = t;
    return m;
}

Material overlay_mat(TextureId base, TextureId overlay) {
    Material m;
    m.kind = MaterialKind::TextureOverlay;
    m.texture = base;
    m.overlay = overlay;
    return m;
}

bool toggle_on(Toggle t) { return t != Toggle::Empty && t != Toggle::End; }

// Gem sizes from Onyx's drawDrumPlay / drawGem (mtolly/onyx,
// haskell/packages/onyx-lib-game/src/Onyx/Game/Graphics.hs, lines 660-668 at
// commit 84d5e51). They are literals in Onyx's code, not keys in its
// 3d-config.yml, so they live here rather than in PreviewConfig.
constexpr float kGhostWidthScale = 0.7f;        // Onyx: (v - xCenter) * 0.7
constexpr float kPadGemHalfSize = 0.5f / 2.0f;  // Onyx: reference = 0.5 / 2

TextureId lane_tex(Pad p) {
    switch (p) {
        case Pad::Red:    return TextureId::LaneRed;
        case Pad::Yellow: return TextureId::LaneYellow;
        case Pad::Blue:   return TextureId::LaneBlue;
        case Pad::Green:  return TextureId::LaneGreen;
    }
    return TextureId::LaneRed;
}

TextureId target_tex(Pad p, bool light) {
    switch (p) {
        case Pad::Red:    return light ? TextureId::TargetRedLight : TextureId::TargetRed;
        case Pad::Yellow: return light ? TextureId::TargetYellowLight : TextureId::TargetYellow;
        case Pad::Blue:   return light ? TextureId::TargetBlueLight : TextureId::TargetBlue;
        case Pad::Green:  return light ? TextureId::TargetGreenLight : TextureId::TargetGreen;
    }
    return TextureId::TargetRed;
}

// Onyx drawGem's texture table (no lefty flip).
TextureId gem_tex(const TrackGem& g, bool od) {
    if (g.kick) return od ? TextureId::LongEnergy : TextureId::LongKick;
    if (g.cymbal) {
        if (od) return TextureId::CymbalEnergy;
        switch (g.pad) {
            case Pad::Yellow: return TextureId::CymbalYellow;
            case Pad::Blue:   return TextureId::CymbalBlue;
            case Pad::Green:  return TextureId::CymbalGreen;
            case Pad::Red:    break;  // never a cymbal
        }
    }
    if (od) return TextureId::BoxEnergy;
    switch (g.pad) {
        case Pad::Red:    return TextureId::BoxRed;
        case Pad::Yellow: return TextureId::BoxYellow;
        case Pad::Blue:   return TextureId::BoxBlue;
        case Pad::Green:  return TextureId::BoxGreen;
    }
    return TextureId::BoxRed;
}

Color blend(const Color& a, const Color& b) {
    return Color{(a.r + b.r) * 0.5f, (a.g + b.g) * 0.5f, (a.b + b.b) * 0.5f, 1.0f};
}

}  // namespace

std::vector<DrawCommand> build_highway_draws(const TrackState& state, const PreviewConfig& cfg,
                                             double now_s, double speed) {
    std::vector<DrawCommand> out;
    const PreviewConfig::Track& T = cfg.track;
    const double far_t = now_s + speed * T.secs_future;
    const double near_t = z_to_time(cfg, now_s, T.z_past, speed);
    auto z_of = [&](double t) { return static_cast<float>(time_to_z(cfg, now_s, t, speed)); };

    const TrackWindow win = state.window(near_t, far_t);

    // 1. Floor: one flat per stretch of (solo, active SP) state. Onyx tints
    //    the floor for solos only; the active SP window is Hydra's addition,
    //    drawn the same way in the energy colour darkened to floor brightness.
    {
        std::vector<ToggleSpan> solos = state.make_toggle_bounds(win, near_t, far_t, &TrackInstant::solo);
        std::vector<ToggleSpan> sps = state.make_toggle_bounds(win, near_t, far_t, &TrackInstant::sp_active);
        std::vector<double> cuts{near_t, far_t};
        for (const ToggleSpan& s : solos) { cuts.push_back(s.t1); cuts.push_back(s.t2); }
        for (const ToggleSpan& s : sps) { cuts.push_back(s.t1); cuts.push_back(s.t2); }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        auto state_at = [](const std::vector<ToggleSpan>& spans, double t) {
            for (const ToggleSpan& s : spans)
                if (s.t1 <= t && t < s.t2) return s.on;
            return false;
        };
        const Color sp_col{cfg.hydra.sp_active_color.r * cfg.hydra.sp_active_darken,
                           cfg.hydra.sp_active_color.g * cfg.hydra.sp_active_darken,
                           cfg.hydra.sp_active_color.b * cfg.hydra.sp_active_darken, 1.0f};
        for (size_t i = 0; i + 1 < cuts.size(); ++i) {
            const double t1 = cuts[i], t2 = cuts[i + 1];
            if (t2 <= t1) continue;
            const double mid = (t1 + t2) * 0.5;
            const bool solo = state_at(solos, mid), sp = state_at(sps, mid);
            Color col = solo ? T.color_solo : T.color_normal;
            if (sp) col = solo ? blend(T.color_solo, sp_col) : sp_col;
            out.push_back(flat(T.x_left, T.y, z_of(t1), T.x_right, z_of(t2), color_mat(col)));
        }
    }

    // 2. Railings: two boxes along the whole visible depth.
    {
        DrawCommand l;
        l.mesh = MeshId::Box;
        l.lo[0] = T.x_left - T.railing_x_width; l.lo[1] = T.railing_y_top;    l.lo[2] = T.z_past;
        l.hi[0] = T.x_left;                     l.hi[1] = T.railing_y_bottom; l.hi[2] = T.z_future;
        l.material = color_mat(T.railing_color);
        DrawCommand r = l;
        r.lo[0] = T.x_right;
        r.hi[0] = T.x_right + T.railing_x_width;
        out.push_back(l);
        out.push_back(r);
    }

    // 3. Beat lines (depth test off): bar / beat / half-beat flats.
    for (const TrackInstant& inst : win) {
        if (!inst.beat) continue;
        TextureId tex = *inst.beat == app::PreviewBeatKind::Bar    ? TextureId::Line1
                        : *inst.beat == app::PreviewBeatKind::Beat ? TextureId::Line2
                                                                   : TextureId::Line3;
        const float z = z_of(inst.t);
        out.push_back(flat(T.x_left, T.y, z + T.beats_z_past, T.x_right, z + T.beats_z_future,
                           tex_mat(tex), 1.0f, DepthMode::Always));
    }

    // 4. Lane strips (depth off): a fill window lights all four lanes (Onyx's
    //    BRE look), dimmed to fill_offered_alpha for a fill the path passed
    //    over and at full strength for the one it activates on. That fill's
    //    activation lane then draws once more in the lit target texture, so
    //    the lane to hit stands out from the other three.
    {
        auto strip_tex = [&](Pad pad, double t1, double t2, TextureId tex, float alpha) {
            float x1, x2;
            pad_x(cfg, pad, x1, x2);
            out.push_back(flat(x1, T.y, z_of(t1), x2, z_of(t2), tex_mat(tex), alpha,
                               DepthMode::Always));
        };
        auto strip = [&](Pad pad, double t1, double t2, float alpha) {
            strip_tex(pad, t1, t2, lane_tex(pad), alpha);
        };
        for (const ToggleSpan& s : state.make_toggle_bounds(win, near_t, far_t, &TrackInstant::fill)) {
            if (!s.on) continue;
            for (Pad p : {Pad::Red, Pad::Yellow, Pad::Blue, Pad::Green})
                strip(p, s.t1, s.t2, cfg.hydra.fill_offered_alpha);
        }
        for (const ToggleSpan& s :
             state.make_toggle_bounds(win, near_t, far_t, &TrackInstant::fill_taken)) {
            if (!s.on) continue;
            for (Pad p : {Pad::Red, Pad::Yellow, Pad::Blue, Pad::Green}) strip(p, s.t1, s.t2, 1.0f);
        }
        for (const ToggleSpan& s : state.make_toggle_bounds(win, near_t, far_t, &TrackInstant::fill_lane)) {
            if (!s.on) continue;
            std::optional<Pad> pad;
            for (const TrackInstant& inst : win)
                if (inst.t >= s.t1 && inst.t <= s.t2 && inst.fill_lane_pad) { pad = inst.fill_lane_pad; break; }
            if (!pad)
                for (const TrackInstant& inst : win)
                    if (inst.fill_lane_pad) { pad = inst.fill_lane_pad; break; }
            if (pad) strip_tex(*pad, s.t1, s.t2, target_tex(*pad, true), 1.0f);
        }
    }

    // 5. Strike line (Onyx targets) and the glow after a hit, depth off.
    for (Pad p : {Pad::Red, Pad::Yellow, Pad::Blue, Pad::Green}) {
        float x1, x2;
        pad_x(cfg, p, x1, x2);
        out.push_back(flat(x1, T.y, T.z_now + T.targets_z_past, x2, T.z_now + T.targets_z_future,
                           tex_mat(target_tex(p, false)), 1.0f, DepthMode::Always));
    }
    {
        // Each lane glows from its most recent hit only; a kick lights nothing.
        bool lit[4] = {false, false, false, false};
        for (auto it = win.rbegin(); it != win.rend(); ++it) {
            if (it->t >= now_s || it->t <= near_t) continue;
            const float alpha = static_cast<float>(1.0 - (now_s - it->t) / T.targets_secs_light);
            if (alpha <= 0.0f) continue;
            for (const TrackGem& g : it->notes) {
                if (g.kick || lit[static_cast<int>(g.pad)]) continue;
                lit[static_cast<int>(g.pad)] = true;
                float x1, x2;
                pad_x(cfg, g.pad, x1, x2);
                out.push_back(flat(x1, T.y, T.z_now + T.targets_z_past, x2,
                                   T.z_now + T.targets_z_future, tex_mat(target_tex(g.pad, true)),
                                   alpha, DepthMode::Always));
            }
        }
    }

    // 6. Gems, latest (farthest) first. A note already past the strike line
    //    flashes white there and fades over secs_fade.
    for (auto it = win.rbegin(); it != win.rend(); ++it) {
        const bool od = toggle_on(it->overdrive);
        std::optional<float> fade;
        if (it->t < now_s) {
            const double age = now_s - it->t;
            if (age >= cfg.gems.secs_fade) continue;
            fade = static_cast<float>(1.0 - age / cfg.gems.secs_fade);
        }
        const float z = fade ? z_of(now_s) : z_of(it->t);
        for (const TrackGem& g : it->notes) {
            float x1, x2;
            if (g.kick) {
                x1 = T.x_left;
                x2 = T.x_right;
            } else {
                pad_x(cfg, g.pad, x1, x2);
            }
            // Hydra departs from Onyx here: Onyx shrinks every ghost, kicks
            // included, but a shrunken kick reads as a bar that stops short
            // of the highway edge. A ghost kick shows only the overlay.
            if (g.velocity == Velocity::Ghost && !g.kick) {
                const float cx = x1 + (x2 - x1) * 0.5f;
                x1 = cx + (x1 - cx) * kGhostWidthScale;
                x2 = cx + (x2 - cx) * kGhostWidthScale;
            }
            // A kick's box is as deep and tall as half its width (Onyx:
            // (x2' - x1') / 2); a pad gem's is fixed.
            const float ref = g.kick ? (x2 - x1) * 0.5f : kPadGemHalfSize;
            DrawCommand c;
            c.mesh = g.kick ? MeshId::Kick : g.cymbal ? MeshId::Cymbal : MeshId::Tom;
            c.lo[0] = x1; c.lo[1] = T.y - ref; c.lo[2] = z - ref;
            c.hi[0] = x2; c.hi[1] = T.y + ref; c.hi[2] = z + ref;
            if (fade) {
                c.material = color_mat(cfg.gems.color_hit);
                c.alpha = *fade;
            } else {
                TextureId base = gem_tex(g, od);
                c.material = g.velocity == Velocity::Ghost    ? overlay_mat(base, TextureId::OverlayGhost)
                             : g.velocity == Velocity::Accent ? overlay_mat(base, TextureId::OverlayAccent)
                                                              : tex_mat(base);
                c.alpha = 1.0f;
            }
            c.light = LightKind::GemOffset;
            c.depth = DepthMode::Less;
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace hydra::render
