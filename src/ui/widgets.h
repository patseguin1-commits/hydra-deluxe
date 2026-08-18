// Text-overflow helpers shared by the views.
//
// Dear ImGui's plain Text() hard-clips against the column/child edge with no
// visual indicator, so a long title just vanished mid-word with no way to
// read the rest. These render an ellipsis when the text doesn't fit and put
// the full text in a hover tooltip — the same affordance the HTML report
// gives truncated cells via title= attributes.

#ifndef HYDRA_UI_WIDGETS_H
#define HYDRA_UI_WIDGETS_H

#include "imgui.h"
#include "imgui_internal.h"  // RenderTextEllipsis

namespace hydra::ui {

// Shows `text` in a (wrapped) tooltip when the last item is hovered. Callers
// use this directly for items that render their own text (Selectable rows);
// pair with a "does it actually overflow" check so short text stays quiet.
inline void overflow_tooltip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                              ImGuiHoveredFlags_AllowWhenDisabled))
        return;
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// TextUnformatted that ellipsizes at the available width instead of clipping
// mid-glyph, with the full text in a tooltip when it didn't fit. Uses the
// current font and text color, so callers can Push either around it.
inline void text_ellipsized(const char* text) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    float avail = ImGui::GetContentRegionAvail().x;
    ImVec2 text_size = ImGui::CalcTextSize(text);

    if (text_size.x <= avail) {
        ImGui::TextUnformatted(text);
        return;
    }

    ImVec2 pos = window->DC.CursorPos;
    float max_x = pos.x + avail;
    ImGui::RenderTextEllipsis(window->DrawList, pos, ImVec2(max_x, pos.y + text_size.y),
                              max_x, text, nullptr, &text_size);
    // An item exactly as wide as the space the text was given, so layout
    // advances normally and the tooltip has a hover rect.
    ImGui::Dummy(ImVec2(avail, text_size.y));
    overflow_tooltip(text);
}

}  // namespace hydra::ui

#endif  // HYDRA_UI_WIDGETS_H
