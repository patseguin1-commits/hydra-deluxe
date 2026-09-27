#include "ui/details_parts.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/preview_view.h"  // path_overlay_key
#include "core/model.h"        // kCloneHeroSpCap
#include "imgui.h"
#include "imgui_internal.h"  // SetKeyOwner, owner-aware IsKeyPressed
#include "render/overlay_layout.h"
#include "ui/fonts.h"
#include "ui/preview_controller.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <string>

namespace hydra::ui::detail {

// The Preview tab: a transport row over the 3D note highway. Reached only while
// the tab is shown, so the controller (and its decode + GPU work) spins up lazily
// on first view, per the "render only while active" gating.
void render_preview_panel(AppState& app, const Path* selected_path) {
    PreviewController* pc = app.preview_controller();
    if (pc == nullptr) {
        ImGui::TextUnformatted("Preview is unavailable (no render device).");
        return;
    }
    if (!app.selected) {
        ImGui::TextUnformatted("Select a song to preview.");
        return;
    }

    // Open (or keep open) for the current selection; a no-op once running for
    // this chart. This is where the async decode starts.
    pc->set_volume(app.settings.preview_volume);  // before the audio exists too
    // The meter's ceiling is the cap the viewed record was analyzed at; a
    // chart with no record yet previews at the Clone Hero cap.
    const int sp_cap = app.viewed.status == store::RecordStatus::Ready
                           ? app.viewed.record->sp_cap.value_or(kCloneHeroSpCap)
                           : kCloneHeroSpCap;
    // The overlay key is the path's verbose string: rebuilt when the
    // selection or the record changes, not every frame.
    DetailsViewState& ui = app.details_ui;
    if (selected_path != ui.overlay_key_path ||
        app.record_generation.n != ui.overlay_key_generation) {
        ui.overlay_key = hydra::app::path_overlay_key(selected_path);
        ui.overlay_key_path = selected_path;
        ui.overlay_key_generation = app.record_generation.n;
    }
    pc->open(*app.selected, app.settings.view_prodrums, app.settings.effective_bass2x(),
             app.settings.difficulty(), selected_path, ui.overlay_key, sp_cap,
             app.settings.rules);
    pc->poll();

    if (pc->has_error()) {
        ImGui::TextColored(kWarningColor, "Preview failed: %s", pc->error().c_str());
        return;
    }
    if (pc->loading()) {
        // A big chart decodes for several seconds; the step label and bar
        // are what tell the user it is still moving.
        PreviewController::LoadProgress lp = pc->load_progress();
        ImGui::Text("Loading preview: %s", lp.label.c_str());
        char overlay[16];
        std::snprintf(overlay, sizeof(overlay), "%.0f%%", lp.fraction * 100.0f);
        ImGui::ProgressBar(lp.fraction, ImVec2(-1.0f, 0.0f), overlay);
        return;
    }

    // No audio output device: the chart previews muted. One line says so and
    // the highway below draws as usual; the device's own message is a hover away.
    if (pc->has_audio_warning()) {
        ImGui::TextColored(kWarningColor, "No audio device found; the preview is muted.");
        hint(pc->audio_warning().c_str());
    }

    // Transport row: back 5 s, back 5 ticks, play/pause, forward 5 ticks,
    // forward 5 s, a scrubber, and the time readout. Every piece whose text
    // changes while playing sits in a fixed slot (see widgets.h): otherwise
    // the Vol slider walked under a held mouse as the readout's digits
    // changed width. The four step buttons have fixed labels, so they need
    // no slot.
    // The tick buttons and comma/period step this many chart ticks.
    constexpr int kTickStep = 5;
    if (ImGui::Button("-5s")) pc->jump_ms(-5000.0);
    hint("Back 5 seconds (Left arrow)");
    ImGui::SameLine();
    if (ImGui::Button("< 5 Ticks")) pc->step_ticks(-kTickStep);
    hint("Back 5 ticks (Comma)");
    ImGui::SameLine();
    const float play_w = std::max(button_slot_width("Play"), button_slot_width("Pause"));
    if (button_in_slot(pc->playing() ? "Pause" : "Play", play_w)) pc->toggle();
    hint("Play or pause (Space)");
    ImGui::SameLine();
    if (ImGui::Button("5 Ticks >")) pc->step_ticks(kTickStep);
    hint("Forward 5 ticks (Period)");
    ImGui::SameLine();
    if (ImGui::Button("+5s")) pc->jump_ms(5000.0);
    hint("Forward 5 seconds (Right arrow)");
    ImGui::SameLine();

    // The clock drives the scrubber, so a chart with no audio still scrubs.
    double len_ms = pc->length_ms();
    float pos_s = static_cast<float>(pc->position_ms() / 1000.0);
    float len_s = static_cast<float>(len_ms / 1000.0);
    std::string len_digits = widest_digits(digit_count((long long)len_s));
    std::string readout_sample = len_digits + "." + len_digits.substr(0, 1) + " / " +
                                 len_digits + "." + len_digits.substr(0, 1) + " s";
    const float readout_w = text_slot_width(readout_sample.c_str());
    const float volume_w = px(110.0f);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(
        px(120.0f), ImGui::GetContentRegionAvail().x - readout_w - text_slot_width("Vol") -
                        volume_w - 3.0f * spacing));
    if (ImGui::SliderFloat("##scrub", &pos_s, 0.0f, len_s > 0.0f ? len_s : 1.0f, "%.1fs"))
        pc->seek_ms(static_cast<double>(pos_s) * 1000.0);
    // Holding the scrubber pauses playback (Onyx's rule); release resumes.
    pc->set_scrubbing(ImGui::IsItemActive());
    ImGui::SameLine();
    char readout[48];
    std::snprintf(readout, sizeof(readout), "%.1f / %.1f s", pos_s, len_s);
    text_in_slot(readout, readout_w);

    // Volume: applied live and remembered in the settings file.
    ImGui::TextUnformatted("Vol");
    ImGui::SameLine();
    int volume = app.settings.preview_volume;
    ImGui::SetNextItemWidth(volume_w);
    if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d%%")) {
        app.settings.preview_volume = volume;
        pc->set_volume(volume);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) app.commit_settings();

    // Keys: Space plays or pauses, Left/Right jump 5 s, comma/period step
    // 5 ticks; a held arrow or comma/period repeats. Not while a text field
    // has the keyboard. Keyboard navigation (on in app_shell.cpp) reads the
    // arrows and Space only when nobody owns them, so the Preview claims them
    // every frame it shows; a claim made this frame still holds during next
    // frame's navigation update, so even the first press lands here rather
    // than moving focus, nudging the scrubber, or pressing whichever button
    // was clicked last.
    if (!ImGui::GetIO().WantTextInput) {
        const ImGuiID keys_owner = ImGui::GetID("##preview_keys");
        ImGui::SetKeyOwner(ImGuiKey_LeftArrow, keys_owner);
        ImGui::SetKeyOwner(ImGuiKey_RightArrow, keys_owner);
        ImGui::SetKeyOwner(ImGuiKey_Space, keys_owner);
        if (ImGui::IsKeyPressed(ImGuiKey_Space, ImGuiInputFlags_None, keys_owner)) pc->toggle();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat, keys_owner))
            pc->jump_ms(-5000.0);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, ImGuiInputFlags_Repeat, keys_owner))
            pc->jump_ms(5000.0);
        if (ImGui::IsKeyPressed(ImGuiKey_Comma, true)) pc->step_ticks(-kTickStep);
        if (ImGui::IsKeyPressed(ImGuiKey_Period, true)) pc->step_ticks(kTickStep);
    }

    // Highway viewport: size the offscreen target to the remaining region.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = static_cast<int>(avail.x);
    int h = static_cast<int>(avail.y);
    ID3D11ShaderResourceView* srv = pc->render(w, h);
    if (srv != nullptr && w > 0 && h > 0) {
        ImGui::Image((ImTextureID)(intptr_t)srv,
                     ImVec2(static_cast<float>(w), static_cast<float>(h)));

        // The text overlays: the time box and score box top-left, the SP drain
        // box top-right beside the gauge. They share one scale, fitted by
        // render::overlay_scale so they sit beside the highway: their
        // configured size whenever there is room, smaller in a narrow window,
        // never below kOverlayMinScale (under that they overlap rather than
        // become unreadable). Everything is measured at scale 1 first, then
        // drawn at the fitted scale. The gauge keeps its size.
        ImFont* font = g_mono_font ? g_mono_font : ImGui::GetFont();
        const render::PreviewConfig& pcfg = pc->preview_config();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetItemRectMin();
        const ImVec2 img_max = ImGui::GetItemRectMax();
        auto text_width = [font](float sz, const char* s) {
            return font->CalcTextSizeA(sz, FLT_MAX, 0.0f, s).x;
        };

        // The time box's lines, the way Onyx draws its own (top-left,
        // monospace, on a translucent dark panel): time / length,
        // [measure:beat:tick] for both, BPM, the time signature, and the
        // practice section (absent on charts that have none).
        hydra::app::PreviewTimeBox box = pc->time_box();
        const char* lines[5];
        int line_count = 0;
        lines[line_count++] = box.timestamp.c_str();
        lines[line_count++] = box.measure_beat.c_str();
        lines[line_count++] = box.bpm.c_str();
        lines[line_count++] = box.time_sig.c_str();
        if (!box.section.empty()) lines[line_count++] = box.section.c_str();
        hydra::app::PreviewScoreBox score = pc->score_box();
        const bool has_gauge = pc->sp_meter_has_curve();
        hydra::app::PreviewDrainBox drain = pc->drain_box();
        const bool drain_drawn = has_gauge && drain.shown;
        const char* d_lines[3] = {drain.header.c_str(), drain.rate.c_str(),
                                  drain.detail.c_str()};

        // The gauge's geometry, which is not scaled.
        const float bar_w = px(14.0f);
        const float inset = px(10.0f);
        const float v_margin = px(10.0f);
        const float d_gap = px(6.0f);  // between the drain box and the gauge
        const float gauge_left = img_max.x - inset - bar_w;

        // Scale-1 sizes, and the extents the fit needs (image pixels).
        const float size1 = px(pcfg.text.time_box_size);
        const float margin1 = px(pcfg.text.time_box_margin);
        const float pad1 = px(8.0f);
        const float gap1 = px(6.0f);
        const float line_h1 = size1 * 1.25f;
        float time_text_w1 = 0.0f;
        for (int i = 0; i < line_count; ++i)
            time_text_w1 = std::max(time_text_w1, text_width(size1, lines[i]));
        render::OverlayBoxes fit;
        fit.left_w = margin1 + time_text_w1 + pad1 * 2.0f;
        fit.left_h = margin1 + line_h1 * static_cast<float>(line_count) + pad1;
        if (score.shown) {
            const float score_size1 = score.available ? size1 * 1.8f : size1;
            float score_w1 = text_width(score_size1, score.score.c_str());
            if (!score.detail.empty())
                score_w1 = std::max(score_w1, text_width(size1, score.detail.c_str()));
            const float score_lines_h1 =
                score_size1 * 1.2f + (score.detail.empty() ? 0.0f : line_h1);
            fit.left_w = std::max(fit.left_w, margin1 + score_w1 + pad1 * 2.0f);
            fit.left_h += gap1 + pad1 + score_lines_h1 + pad1;
        }
        if (drain_drawn) {
            float drain_w1 = 0.0f;
            for (const char* l : d_lines) drain_w1 = std::max(drain_w1, text_width(size1, l));
            fit.right_w = drain_w1 + pad1 * 2.0f;
            fit.right_h = line_h1 * 3.0f + pad1 * 2.0f;
            fit.right_edge = gauge_left - d_gap - origin.x;
            fit.right_top = v_margin;
        }
        fit.gap = gap1;
        const float scale = render::overlay_scale(pcfg, w, h, fit);
        pc->set_overlay_scale(scale);

        const float size = size1 * scale;
        const float margin = margin1 * scale;
        const float pad = pad1 * scale;
        const float gap = gap1 * scale;
        const float line_h = size * 1.25f;
        const float corner = px(6.0f) * scale;

        // The time box.
        float text_w = 0.0f;
        for (int i = 0; i < line_count; ++i) text_w = std::max(text_w, text_width(size, lines[i]));
        ImVec2 box_min(origin.x, origin.y);
        ImVec2 box_max(origin.x + margin + text_w + pad * 2.0f,
                       origin.y + margin + line_h * static_cast<float>(line_count) + pad);
        dl->AddRectFilled(box_min, box_max, IM_COL32(0, 0, 0, 128), corner,
                          ImDrawFlags_RoundCornersBottomRight);
        for (int i = 0; i < line_count; ++i)
            dl->AddText(font, size, ImVec2(origin.x + margin, origin.y + margin + line_h * i),
                        IM_COL32(255, 255, 255, 255), lines[i]);

        // The score box, under the time box in the same panel style: the
        // running score in large type, then "x<mult> · combo <n>" in light
        // grey. Absent until the chart is analyzed; "Score unavailable" (at
        // the time box's size) when the path can't be replayed to its stored
        // score. Right corners rounded, since it sits against the left edge.
        if (score.shown) {
            const float score_size = score.available ? size * 1.8f : size;
            const float score_h = score_size * 1.2f;
            const float score_w = text_width(score_size, score.score.c_str());
            const float detail_w =
                score.detail.empty() ? 0.0f : text_width(size, score.detail.c_str());
            const float lines_h = score_h + (score.detail.empty() ? 0.0f : line_h);
            ImVec2 s_min(origin.x, box_max.y + gap);
            ImVec2 s_max(origin.x + margin + std::max(score_w, detail_w) + pad * 2.0f,
                         s_min.y + pad + lines_h + pad);
            dl->AddRectFilled(s_min, s_max, IM_COL32(0, 0, 0, 128), corner,
                              ImDrawFlags_RoundCornersRight);
            dl->AddText(font, score_size, ImVec2(origin.x + margin, s_min.y + pad),
                        IM_COL32(255, 255, 255, 255), score.score.c_str());
            if (!score.detail.empty())
                dl->AddText(font, size, ImVec2(origin.x + margin, s_min.y + pad + score_h),
                            IM_COL32(200, 200, 200, 255), score.detail.c_str());
        }

        // The Star Power meter: a gauge down the image's right edge, filling
        // bottom-up as phrases are collected and draining while SP is active.
        // Hydra's own overlay, like the time box above -- not part of the Onyx
        // render. The value is the view-model's curve read at the playhead, so
        // it is anchored to the same engine truth the path overlay is.
        if (has_gauge) {
            ImVec2 gauge_min(gauge_left, origin.y + v_margin);
            ImVec2 gauge_max(img_max.x - inset, img_max.y - v_margin);
            if (gauge_max.y > gauge_min.y) {
                dl->AddRectFilled(gauge_min, gauge_max, IM_COL32(0, 0, 0, 128), px(4.0f));

                const int cap = std::max(1, pc->sp_meter_cap());
                float fill = static_cast<float>(pc->sp_meter_bars()) / static_cast<float>(cap);
                fill = fill < 0.0f ? 0.0f : (fill > 1.0f ? 1.0f : fill);

                const float fill_pad = px(2.0f);
                ImVec2 in_min(gauge_min.x + fill_pad, gauge_min.y + fill_pad);
                ImVec2 in_max(gauge_max.x - fill_pad, gauge_max.y - fill_pad);
                const float in_h = in_max.y - in_min.y;
                if (in_h > 0.0f && fill > 0.0f)
                    dl->AddRectFilled(ImVec2(in_min.x, in_max.y - in_h * fill), in_max,
                                      IM_COL32(255, 204, 51, 230));  // Star Power gold
                // One line per whole-bar boundary, over the fill, so a glance
                // reads how many bars are banked and not just how full it is.
                for (int b = 1; b < cap; ++b) {
                    const float y = in_max.y - in_h * (static_cast<float>(b) /
                                                       static_cast<float>(cap));
                    dl->AddLine(ImVec2(in_min.x, y), ImVec2(in_max.x, y),
                                IM_COL32(0, 0, 0, 160), px(1.0f));
                }
            }
        }

        // The SP drain box, top-right just left of the gauge and level with
        // its top, where the highway is narrowest; right-aligned in the time
        // box's panel style. How long a bar of SP lasts at the playhead, then
        // "empties in" (gold, SP running on the path) or "full meter" (grey,
        // if activated here). Every number is build_drain_box's.
        if (drain_drawn) {
            float d_w = 0.0f;
            for (const char* l : d_lines) d_w = std::max(d_w, text_width(size, l));
            ImVec2 d_min(gauge_left - d_gap - d_w - pad * 2.0f, origin.y + v_margin);
            ImVec2 d_max(gauge_left - d_gap, d_min.y + line_h * 3.0f + pad * 2.0f);
            dl->AddRectFilled(d_min, d_max, IM_COL32(0, 0, 0, 128), corner);
            const ImU32 accent = drain.active ? IM_COL32(255, 204, 51, 255)  // SP gold
                                              : IM_COL32(200, 200, 200, 255);
            const ImU32 colors[3] = {accent, IM_COL32(255, 255, 255, 255), accent};
            for (int i = 0; i < 3; ++i) {
                const float lw = text_width(size, d_lines[i]);
                dl->AddText(font, size,
                            ImVec2(d_max.x - pad - lw, d_min.y + pad + line_h * static_cast<float>(i)),
                            colors[i], d_lines[i]);
            }
        }
    }
}

}  // namespace hydra::ui::detail
