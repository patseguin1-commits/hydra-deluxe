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
#include <optional>
#include <string>
#include <vector>

namespace hydra::ui::detail {

namespace {

// The line under the highway naming the Preview's keys.
const char* const kPreviewKeysHint =
    "Space play/pause \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 5 s \xC2\xB7 , . 5 ticks "
    "\xC2\xB7 [ ] previous/next activation";

// "Showing" and the ##previewpath list: the same paths, in the same order and
// from the same cache, as the Paths tab's buttons. A pick sets the one
// selection both tabs read (DetailsViewState::selected_path). Drawn only when
// the song has a Ready record with paths.
void render_path_picker(AppState& app) {
    if (app.viewed.status != store::RecordStatus::Ready || !app.viewed.record ||
        app.viewed.record->paths.empty())
        return;
    const hydra::app::PathButtonsView& list = app.details_ui.paths_tab.buttons(
        *app.viewed.record, app.record_generation.n, app.settings.depth_mode,
        app.settings.depth_value);
    const Path*& selected = app.details_ui.selected_path;
    std::string current;
    for (const hydra::app::PathButtonView& b : list.buttons)
        if (b.path == selected) current = hydra::app::preview_path_label(b);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Showing");
    ImGui::SameLine();
    ImGui::PushFont(g_mono_font, 0.0f);
    // On its own line, as wide as the longest path in the list (or the
    // line). A path longer than the line (40 activations and "  (optimal)"
    // run to about 100 characters) ends in "…" rather than vanish under the
    // arrow, with the whole path a hover away; the open list shows it whole.
    float widest = 0.0f;
    for (const hydra::app::PathButtonView& b : list.buttons)
        widest = std::max(widest, ImGui::CalcTextSize(hydra::app::preview_path_label(b).c_str()).x);
    const float chrome = ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
    const float box_w = std::min(widest + chrome, ImGui::GetContentRegionAvail().x);
    const std::string shown = render::ellipsize(
        current, box_w - chrome, [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; });
    ImGui::SetNextItemWidth(box_w);
    const bool open = ImGui::BeginCombo("##previewpath", shown.c_str());
    if (!open && shown != current) overflow_tooltip(current.c_str());
    if (open) {
        for (size_t i = 0; i < list.buttons.size(); ++i) {
            const hydra::app::PathButtonView& b = list.buttons[i];
            // "##<i>" keeps two paths with the same notation apart.
            const std::string item = hydra::app::preview_path_label(b) + "##" + std::to_string(i);
            const bool is_selected = b.path == selected;
            if (ImGui::Selectable(item.c_str(), is_selected)) selected = b.path;
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopFont();
}

// Gold ticks over the scrubber just drawn, one per activation, where the
// grab's centre sits for that time. ImGui keeps 2 px of padding and half a
// grab at each end of a float slider, so the ticks do too.
void draw_scrub_marks(const std::vector<double>& marks) {
    if (marks.empty()) return;
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const float grab = ImGui::GetStyle().GrabMinSize;
    const float pad = 2.0f;  // ImGui's slider grab_padding, not scaled
    const float x0 = mn.x + pad + grab * 0.5f;
    const float span = (mx.x - mn.x) - 2.0f * pad - grab;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (double f : marks) {
        const float x = x0 + static_cast<float>(f) * span;
        dl->AddRectFilled(ImVec2(x - px(1.5f), mn.y + px(2.0f)), ImVec2(x + px(1.5f), mx.y - px(2.0f)),
                          ImGui::GetColorU32(kBestPathColor));
    }
}

}  // namespace

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
        // Wrapped: an error naming a file path runs far past the panel's edge.
        WarnColor warn;
        ImGui::TextWrapped("Preview failed: %s", pc->error().c_str());
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

    // "Show in Preview" on the Paths tab: once the overlay for the selected
    // path is in, move the playhead to that activation.
    std::optional<size_t>& jump = app.details_ui.paths_tab.ui().preview_jump;
    if (jump && pc->overlay_path_key().rfind(ui.overlay_key, 0) == 0) {
        pc->seek_activation(*jump);
        jump.reset();
    }

    // Row 1: which path the overlay draws. Row 2: the activation jumps, the
    // transport buttons and the volume. Row 3: the scrubber, a gold mark per
    // activation, and the clock. The clock sits in a fixed slot (see
    // widgets.h), so nothing walks under a held mouse as its digits change.
    // The tick buttons and comma/period step this many chart ticks.
    constexpr int kTickStep = 5;
    render_path_picker(app);
    const bool no_acts = pc->scrub_marks().empty();
    begin_disabled_button(no_acts);
    if (ImGui::Button("< Act##prevact")) pc->jump_activation(-1);
    ImGui::SameLine();
    if (ImGui::Button("Act >##nextact")) pc->jump_activation(+1);
    end_disabled_button(no_acts);
    hint("Previous or next activation ([ and ])");
    ImGui::SameLine(0.0f, px(16.0f));
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
    ImGui::SameLine(0.0f, px(16.0f));

    // Volume: applied live and remembered in the settings file.
    ImGui::TextUnformatted("Vol");
    ImGui::SameLine();
    int volume = app.settings.preview_volume;
    ImGui::SetNextItemWidth(px(110.0f));
    if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d%%")) {
        app.settings.preview_volume = volume;
        pc->set_volume(volume);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) app.commit_settings();

    // The clock drives the scrubber, so a chart with no audio still scrubs.
    const hydra::app::PreviewTimeBox box = pc->time_box();
    float pos_s = static_cast<float>(pc->position_ms() / 1000.0);
    const float len_s = static_cast<float>(pc->length_ms() / 1000.0);
    // The clock's slot fits its widest form: every digit drawn as the widest one.
    std::string readout_sample = box.timestamp;
    const char widest = widest_digits(1)[0];
    for (char& c : readout_sample)
        if (c >= '0' && c <= '9') c = widest;
    ImGui::PushFont(g_mono_font, 0.0f);
    const float readout_w = text_slot_width(readout_sample.c_str());
    ImGui::PopFont();
    ImGui::SetNextItemWidth(std::max(px(120.0f), ImGui::GetContentRegionAvail().x - readout_w -
                                                     ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::SliderFloat("##scrub", &pos_s, 0.0f, len_s > 0.0f ? len_s : 1.0f, ""))
        pc->seek_ms(static_cast<double>(pos_s) * 1000.0);
    // Holding the scrubber pauses playback (Onyx's rule); release resumes.
    pc->set_scrubbing(ImGui::IsItemActive());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Gold marks are this path's activations.");
    draw_scrub_marks(pc->scrub_marks());
    ImGui::SameLine();
    ImGui::PushFont(g_mono_font, 0.0f);
    text_in_slot(box.timestamp.c_str(), readout_w);
    ImGui::PopFont();
    ImGui::NewLine();  // text_in_slot leaves the cursor on its line; the highway goes below

    // Keys: Space plays or pauses, Left/Right jump 5 s, comma/period step
    // 5 ticks, [ and ] jump between activations; a held arrow or comma/period repeats. Not while a text field
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
        // [ and ] jump between the drawn path's activations; no repeat.
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) pc->jump_activation(-1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) pc->jump_activation(+1);
    }

    // Highway viewport: the remaining region, less one line for the key hint.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = static_cast<int>(avail.x);
    int h = static_cast<int>(avail.y - ImGui::GetTextLineHeightWithSpacing());
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
        // monospace, on a translucent dark panel): the playhead's measure and
        // the song's last, the tempo and signature in force, and the practice
        // section (absent on charts that have none). The clock is beside the
        // scrubber now.
        const std::string where = box.position + "  of " + box.length;
        const char* lines[3];
        int line_count = 0;
        lines[line_count++] = where.c_str();
        lines[line_count++] = box.tempo.c_str();
        if (!box.section_line.empty()) lines[line_count++] = box.section_line.c_str();
        hydra::app::PreviewNextActBox next = pc->next_act_box();
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
        // The next-activation box stands on the image's bottom edge, where
        // the highway is widest, and wraps its lines to the room there. So
        // the scale needs room only for its widest word, taken over every box
        // this path shows: the same all through playback, whichever
        // activation is next, and after the last one.
        auto width_at = [&](float sz) {
            return [&text_width, sz](const std::string& s) { return text_width(sz, s.c_str()); };
        };
        // The header's count ("2 of 3") stays on one line.
        constexpr size_t kHeaderCountWords = 3;
        float next_word_w1 = 0.0f;
        for (const hydra::app::PreviewNextActBox& b : pc->next_act_boxes())
            next_word_w1 = std::max({next_word_w1,
                                     render::widest_word(b.header, width_at(size1), kHeaderCountWords),
                                     render::widest_word(b.detail, width_at(size1))});
        if (next_word_w1 > 0.0f) fit.bottom_left_w = margin1 + next_word_w1 + pad1 * 2.0f;
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

        // The next activation, bottom-left in the same panel style: its number
        // in gold, then where it is and its chord. Hidden past the last one.
        // A line too wide for the room beside the highway at the bottom
        // wraps at its spaces, so the box grows up rather than over the lane.
        if (next.shown) {
            const float text_room =
                render::bottom_left_room(pcfg, w, h, gap1) - margin - pad * 2.0f;
            const std::vector<std::string> head =
                render::wrap_words(next.header, text_room, width_at(size), kHeaderCountWords);
            const std::vector<std::string> body =
                render::wrap_words(next.detail, text_room, width_at(size));
            float nw = 0.0f;
            for (const std::string& l : head) nw = std::max(nw, text_width(size, l.c_str()));
            for (const std::string& l : body) nw = std::max(nw, text_width(size, l.c_str()));
            const float n_lines = static_cast<float>(head.size() + body.size());
            const ImVec2 n_min(origin.x, img_max.y - (pad * 2.0f + line_h * n_lines));
            const ImVec2 n_max(origin.x + margin + nw + pad * 2.0f, img_max.y);
            dl->AddRectFilled(n_min, n_max, IM_COL32(0, 0, 0, 128), corner,
                              ImDrawFlags_RoundCornersTopRight);
            float y = n_min.y + pad;
            for (const std::string& l : head) {
                dl->AddText(font, size, ImVec2(origin.x + margin, y), IM_COL32(255, 204, 51, 255),
                            l.c_str());
                y += line_h;
            }
            for (const std::string& l : body) {
                dl->AddText(font, size, ImVec2(origin.x + margin, y), IM_COL32(255, 255, 255, 255),
                            l.c_str());
                y += line_h;
            }
        }

        // The Star Power meter: a gauge down the image's right edge, filling
        // bottom-up as phrases are collected and draining while SP is active.
        // Hydra's own overlay, like the time box above -- not part of the Onyx
        // render. The value is the view-model's curve read at the playhead, so
        // it is anchored to the same engine truth the path overlay is.
        if (has_gauge) {
            // "SP" above the gauge in gold, the banked bars under it.
            const float label_size = px(14.0f);
            const float label_h = label_size * 1.25f;
            const float centre_x = gauge_left + bar_w * 0.5f;
            dl->AddText(font, label_size,
                        ImVec2(centre_x - text_width(label_size, "SP") * 0.5f, origin.y + v_margin),
                        IM_COL32(255, 204, 51, 255), "SP");
            const std::string readout = pc->sp_meter_readout();
            const float rw = text_width(label_size, readout.c_str());
            dl->AddText(font, label_size,
                        ImVec2(std::min(centre_x - rw * 0.5f, img_max.x - px(2.0f) - rw),
                               img_max.y - v_margin - label_size),
                        IM_COL32(255, 255, 255, 255), readout.c_str());
            ImVec2 gauge_min(gauge_left, origin.y + v_margin + label_h);
            ImVec2 gauge_max(img_max.x - inset, img_max.y - v_margin - label_h);
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

    // The keys, under the highway.
    ImGui::TextDisabled("%s", kPreviewKeysHint);
}

}  // namespace hydra::ui::detail
