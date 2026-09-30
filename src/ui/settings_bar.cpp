// The "Analysis settings" bar under the toolbar: every setting an analysis
// runs with, for every song. Locked while anything analyzes, because a result
// is filed under the settings it ran with -- changing SP cap mid-run used to
// hide the result it had just made.

#include "imgui.h"
#include "ui/app_state.h"
#include "ui/fonts.h"
#include "ui/library_parts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <type_traits>

namespace hydra::ui::detail {

namespace {

// The gap each side of a divider, and the divider's width.
float separator_gap() { return px(14.0f); }
constexpr float kSeparatorW = 1.0f;

// A 1 px divider at screen x, from top to bottom, drawn straight onto the
// window: as an item (SeparatorEx) it would take the line's height, and the
// dividers on the caption's line should span both caption lines.
void draw_divider(float x, float top, float bottom) {
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, top), ImVec2(x + kSeparatorW, bottom),
                                              ImGui::GetColorU32(ImGuiCol_Separator));
}

void render_difficulty(AppState& app, bool locked) {
    ImGui::TextUnformatted("Difficulty");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(90));
    const char* names[std::size(kAllDifficulties)];
    for (size_t i = 0; i < std::size(kAllDifficulties); ++i)
        names[i] = difficulty_name(kAllDifficulties[i]);
    int idx = static_cast<int>(app.settings.difficulty());
    begin_disabled_input(locked);
    if (ImGui::Combo("##difficulty", &idx, names, IM_ARRAYSIZE(names))) {
        app.settings.view_difficulty = names[idx];
        // A different difficulty is a different chartmode: commit_settings
        // re-reads the library and rewrites the INI.
        app.commit_settings();
    }
    end_disabled_input(locked);
    help_marker("Which charted difficulty to analyze, path and preview");

    ImGui::SameLine();
    begin_disabled_checkbox(locked);
    if (ImGui::Checkbox("Pro Drums", &app.settings.view_prodrums)) app.commit_settings();
    end_disabled_checkbox(locked);

    // A second kick pedal only exists in Expert charting, so off Expert the
    // box reads unchecked and is disabled; the stored view_bass2x is left
    // alone, so returning to Expert brings the user's own setting back.
    ImGui::SameLine();
    const bool expert = app.settings.difficulty() == Difficulty::Expert;
    bool bass2x_shown = app.settings.effective_bass2x();
    begin_disabled_checkbox(!expert || locked);
    if (ImGui::Checkbox("2x Bass", &bass2x_shown)) {
        app.settings.view_bass2x = bass2x_shown;
        app.commit_settings();
    }
    end_disabled_checkbox(!expert || locked);
    if (!expert && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                        ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("2x Bass is an Expert-only charting concept.");
}

void render_sp_cap(AppState& app, bool locked) {
    ImGui::TextUnformatted("SP cap");
    help_marker((std::to_string(kCloneHeroSpCap) +
                 " bars is Clone Hero's rule. Higher caps are what-ifs; their scores "
                 "are not achievable in game.").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(90));
    begin_disabled_input(locked);
    // Number boxes apply every step live but save the INI once the edit ends
    // (AppState::edit_settings, flushed by run_frame).
    int cap = app.settings.sp_cap;
    if (ImGui::InputInt("##spcap", &cap)) {
        app.settings.sp_cap = std::max(1, cap);
        app.edit_settings();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("bars");
    end_disabled_input(locked);

    // The other Clone Hero rule a result is keyed by: when fills spawn. It
    // sits with the SP cap because both are "which game's rules".
    ImGui::SameLine();
    begin_disabled_checkbox(locked);
    if (ImGui::Checkbox("1.0 fills", &app.settings.legacy_fills)) app.commit_settings();
    end_disabled_checkbox(locked);
    help_marker("Spawn drum fills by Clone Hero 1.0's rule instead of 1.1's. A fill only "
                "appears if your Star Power was ready in time: 1.1 wants it 4 beats "
                "before the fill, 1.0 about one fill-length before. For runs played on "
                "1.0; current Clone Hero plays by 1.1.");
}

void render_score_range(AppState& app, bool locked) {
    ImGui::TextUnformatted("Score range");
    help_marker("How many extra paths below optimal to keep: a number of scores, or of "
                "points. More paths take longer to analyze.");
    ImGui::SameLine();
    // Room for six digits beside the two step buttons (each a frame-height
    // square after an inner gap), never less than the old 90 px.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float six_digits = ImGui::CalcTextSize("000000").x + style.FramePadding.x * 2.0f +
                             (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x) * 2.0f;
    ImGui::SetNextItemWidth(std::max(px(90), six_digits));
    begin_disabled_input(locked);
    if (ImGui::InputInt("##depthvalue", &app.settings.depth_value)) {
        if (app.settings.depth_value < 0) app.settings.depth_value = 0;
        app.edit_settings();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(80));
    int mode_idx = app.settings.depth_mode;
    const char* modes[] = {"scores", "points"};
    if (ImGui::Combo("##depthmode", &mode_idx, modes, 2)) {
        app.settings.depth_mode = mode_idx;
        app.commit_settings();
    }
    end_disabled_input(locked);
}

void render_path_limit(AppState& app, bool locked) {
    begin_disabled_checkbox(locked);
    if (ImGui::Checkbox("Path limit##mslimit", &app.settings.mslimit_enabled))
        app.commit_settings();
    end_disabled_checkbox(locked);
    help_marker("Keep extra paths only when their hardest squeeze is within this many "
                "ms. Lower or negative values demand more slack.");
    ImGui::SameLine();
    const bool off = locked || !app.settings.mslimit_enabled;
    begin_disabled_input(off);
    ImGui::SetNextItemWidth(px(100));
    if (ImGui::InputInt("##mslimitvalue", &app.settings.mslimit_value)) {
        app.settings.mslimit_value = std::clamp(app.settings.mslimit_value, -500, 500);
        app.edit_settings();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("ms");
    end_disabled_input(off);
}

}  // namespace

void render_settings_bar(AppState& app) {
    const bool locked = app.settings_locked();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kSettingsBarBg);
    ImGui::BeginChild("##settingsbar", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();

    // The bar's right edge, in screen space: a block that would run past it
    // starts a new line instead.
    const float right_edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

    // Two stacked caption lines; the controls on their line sit centred on
    // them.
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Analysis settings");
    ImGui::TextDisabled(locked ? "locked" : "for every song");
    ImGui::EndGroup();
    const float caption_top = ImGui::GetItemRectMin().y;
    const float caption_bottom = ImGui::GetItemRectMax().y;
    const float frame_h = ImGui::GetFrameHeight();
    float line_end = ImGui::GetItemRectMax().x;
    // The line the blocks are on, for the dividers' height: the caption's
    // until a block wraps.
    float line_top = caption_top, line_bottom = caption_bottom;

    // The four groups are blocks. A block stays on the current line when it
    // fits, divider included, and otherwise starts a new line with no divider
    // before it. A block's width is known only once it is drawn, so each
    // frame places the blocks by the widths they had on the last one (0 on
    // the very first frame: one line, corrected a frame later).
    //
    // The first block on the caption's line starts a fresh line of its own at
    // the centred height, rather than SameLine after the caption: an item
    // that continues a line is placed from the line's top whatever the
    // cursor says (ImGui's ItemSize), so a nudge there moved only the first
    // label and left the rest of the row at the top.
    using Block = void (*)(AppState&, bool);
    static constexpr Block kBlocks[] = {render_difficulty, render_sp_cap, render_score_range,
                                        render_path_limit};
    static_assert(std::size(kBlocks) ==
                  std::extent_v<decltype(LibraryViewState::settings_block_w)>);
    const float gap = separator_gap();
    const float separator_w = gap * 2.0f + kSeparatorW;
    float* block_w = app.library_ui.settings_block_w;
    for (size_t i = 0; i < std::size(kBlocks); ++i) {
        if (line_end + separator_w + block_w[i] <= right_edge) {
            const float divider_x = line_end + gap;
            draw_divider(divider_x, line_top, line_bottom);
            const float x = divider_x + kSeparatorW + gap;
            if (i == 0) {
                // A fresh line at the centred height (the caption group
                // already ended its own line).
                ImGui::SetCursorScreenPos(
                    ImVec2(x, caption_top + (caption_bottom - caption_top - frame_h) * 0.5f));
            } else {
                ImGui::SameLine();  // keeps this line's top
                ImGui::SetCursorScreenPos(ImVec2(x, ImGui::GetCursorScreenPos().y));
            }
        } else {
            // A new line, below the caption when it would reach up beside it.
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float below = caption_bottom + ImGui::GetStyle().ItemSpacing.y;
            if (at.y < below) ImGui::SetCursorScreenPos(ImVec2(at.x, below));
            line_top = ImGui::GetCursorScreenPos().y;
            line_bottom = line_top + frame_h;
        }
        ImGui::AlignTextToFramePadding();
        ImGui::BeginGroup();
        kBlocks[i](app, locked);
        ImGui::EndGroup();
        block_w[i] = ImGui::GetItemRectSize().x;
        line_end = ImGui::GetItemRectMax().x;
    }

    // The lock message is a fifth block, right-aligned on whichever line it
    // lands on.
    if (locked) {
        const char* why = app.batch_running() ? "Stop the batch to change these."
                                              : "Settings are locked while this song analyzes.";
        const float w = ImGui::CalcTextSize(why).x;
        if (line_end + ImGui::GetStyle().ItemSpacing.x + w <= right_edge) ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        const float right = ImGui::GetContentRegionMax().x - w;
        if (right > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right);
        ImGui::PushStyleColor(ImGuiCol_Text, kSubtleTextColor);
        ImGui::TextUnformatted(why);
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

}  // namespace hydra::ui::detail
