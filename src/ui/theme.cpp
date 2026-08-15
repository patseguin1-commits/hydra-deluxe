#include "ui/theme.h"

namespace hydra::ui {

void apply_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // ---- DPG's own "Default Theme" baseline, for slots hydra_app.py never
    // overrides. Values read directly from dpg.show_style_editor(), not
    // Dear ImGui's StyleColorsDark() (which is meaningfully bluer/darker).
    ImVec4 dpg_surface(51 / 255.0f, 51 / 255.0f, 55 / 255.0f, 1.0f);
    ImVec4 dpg_window(37 / 255.0f, 37 / 255.0f, 38 / 255.0f, 1.0f);
    ImVec4 dpg_border(78 / 255.0f, 78 / 255.0f, 78 / 255.0f, 1.0f);
    colors[ImGuiCol_WindowBg] = dpg_window;
    colors[ImGuiCol_ChildBg] = dpg_window;
    colors[ImGuiCol_PopupBg] = dpg_window;
    colors[ImGuiCol_FrameBg] = dpg_surface;
    colors[ImGuiCol_Header] = dpg_surface;
    colors[ImGuiCol_Border] = dpg_border;
    colors[ImGuiCol_Separator] = dpg_border;
    colors[ImGuiCol_ScrollbarBg] = dpg_surface;
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(82 / 255.0f, 82 / 255.0f, 85 / 255.0f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(90 / 255.0f, 90 / 255.0f, 95 / 255.0f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(90 / 255.0f, 90 / 255.0f, 95 / 255.0f, 1.0f);

    // ---- hydra_app.py's default_theme overrides, applied on top. Full
    // opacity throughout -- DPG's RGB-tuple add_theme_color calls default to
    // alpha=255; a few of these were previously (and wrongly) given reduced
    // alpha here.
    colors[ImGuiCol_Button] = kButtonColor;
    colors[ImGuiCol_ButtonHovered] = kAccentColor;
    colors[ImGuiCol_ButtonActive] = ImVec4(0, 200 / 255.0f, 200 / 255.0f, 1.0f);
    colors[ImGuiCol_CheckMark] = kAccentColor;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0, 100 / 255.0f, 100 / 255.0f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0, 150 / 255.0f, 150 / 255.0f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0, 100 / 255.0f, 100 / 255.0f, 1.0f);
    colors[ImGuiCol_HeaderActive] = kAccentColor;
    colors[ImGuiCol_TitleBgActive] = ImVec4(100 / 255.0f, 0, 0, 1.0f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0, 200 / 255.0f, 200 / 255.0f, 1.0f);
    colors[ImGuiCol_Text] = kDefaultTextColor;

    // DPG's disabled state is a flat opaque gray (verified: a disabled
    // button samples as exactly (100,100,100), no fade). ImGui's
    // BeginDisabled() instead multiplies style.Alpha by DisabledAlpha
    // (0.60 default), which would wash out the explicit colors
    // begin_disabled_button()/begin_disabled_input() push below -- so
    // disable that multiply entirely and rely on the explicit push instead.
    style.DisabledAlpha = 1.0f;
}

void begin_disabled_button(bool disabled) {
    ImGui::BeginDisabled(disabled);
    if (disabled) {
        ImGui::PushStyleColor(ImGuiCol_Button, kDisabledButtonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kDisabledButtonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kDisabledButtonColor);
        ImGui::PushStyleColor(ImGuiCol_Text, kDisabledButtonTextColor);
    }
}

void end_disabled_button(bool disabled) {
    if (disabled) ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
}

void begin_disabled_input(bool disabled) {
    ImGui::BeginDisabled(disabled);
    if (disabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, kDisabledInputTextColor);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kDisabledInputBgColor);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kDisabledInputBgColor);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kDisabledInputBgColor);
        // InputInt's +/- step buttons use Button colors, not FrameBg; Python's
        // mvInputInt(enabled_state=False) component covers this explicitly.
        ImGui::PushStyleColor(ImGuiCol_Button, kDisabledInputBgColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kDisabledInputBgColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kDisabledInputBgColor);
    }
}

void end_disabled_input(bool disabled) {
    if (disabled) ImGui::PopStyleColor(7);
    ImGui::EndDisabled();
}

}  // namespace hydra::ui
