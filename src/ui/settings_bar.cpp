// The "Analysis settings" bar under the toolbar: every setting an analysis
// runs with, for every song. Locked while anything analyzes, because a result
// is filed under the settings it ran with -- changing SP cap mid-run used to
// hide the result it had just made.

#include "imgui.h"
#include "imgui_internal.h"  // SeparatorEx (vertical)
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

void bar_separator() {
    ImGui::SameLine(0.0f, px(14.0f));
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, px(14.0f));
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

    // Two stacked caption lines; the controls sit centred on them.
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Analysis settings");
    ImGui::TextDisabled(locked ? "locked" : "for every song");
    ImGui::EndGroup();
    const float caption_h = ImGui::GetItemRectSize().y;
    float line_end = ImGui::GetItemRectMax().x;

    // The four groups are blocks. A block stays on the current line when it
    // fits, separator included, and otherwise starts a new line with no
    // separator before it. A block's width is known only once it is drawn, so
    // each frame places the blocks by the widths they had on the last one
    // (0 on the very first frame: one line, corrected a frame later).
    using Block = void (*)(AppState&, bool);
    static constexpr Block kBlocks[] = {render_difficulty, render_sp_cap, render_score_range,
                                        render_path_limit};
    static_assert(std::size(kBlocks) ==
                  std::extent_v<decltype(LibraryViewState::settings_block_w)>);
    const float separator_w = px(14.0f) * 2.0f + 1.0f;  // gap, 1 px rule, gap
    float* block_w = app.library_ui.settings_block_w;
    for (size_t i = 0; i < std::size(kBlocks); ++i) {
        if (line_end + separator_w + block_w[i] <= right_edge) {
            bar_separator();
            if (i == 0)
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                                     (caption_h - ImGui::GetFrameHeight()) * 0.5f);
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
