// Small UI idioms shared by the views.
//
// The text-overflow helpers exist because Dear ImGui's plain Text() hard-clips
// against the column/child edge with no visual indicator, so a long title just
// vanished mid-word with no way to read the rest. These render an ellipsis
// when the text doesn't fit and put the full text in a hover tooltip — the
// same affordance the HTML report gives truncated cells via title= attributes.
// The rest are the hover-hint / warning-color / progress-overlay patterns that
// used to be hand-rolled at every call site.

#ifndef HYDRA_UI_WIDGETS_H
#define HYDRA_UI_WIDGETS_H

#include <cstdio>

#include "imgui.h"
#include "imgui_internal.h"  // RenderTextEllipsis
#include "ui/theme.h"

namespace hydra::ui {

// Delayed tooltip on the last item — the "explain this control on hover"
// idiom. Sites that need other hover flags, format arguments, or extra
// conditions still call IsItemHovered/SetTooltip directly.
inline void hint(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", text);
}

// Warning-colored text for the current scope — RAII so the Pop can't drift
// away from its Push as lines get added between them.
struct WarnColor {
    WarnColor() { ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor); }
    ~WarnColor() { ImGui::PopStyleColor(); }
    WarnColor(const WarnColor&) = delete;
    WarnColor& operator=(const WarnColor&) = delete;
};

// Full-width progress bar with a "done/total" overlay. total == 0 renders as
// full rather than dividing by zero (an empty batch is finished, not stuck).
inline void progress_bar_counted(int done, int total) {
    float frac = total > 0 ? (float)done / (float)total : 1.0f;
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%d/%d", done, total);
    ImGui::ProgressBar(frac, ImVec2(-1, 0), overlay);
}

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

// A table row's Selectable spanning all columns, with the full text offered
// on hover when it overflows column 0 (only while actually over that column
// — the Selectable's hover rect spans the whole row). The width must be
// captured before the Selectable claims it. Returns the clicked bool.
inline bool row_selectable(const char* text, bool selected) {
    float avail = ImGui::GetContentRegionAvail().x;
    bool clicked =
        ImGui::Selectable(text, selected, ImGuiSelectableFlags_SpanAllColumns);
    if (ImGui::TableGetHoveredColumn() == 0 &&
        ImGui::CalcTextSize(text).x > avail)
        overflow_tooltip(text);
    return clicked;
}

}  // namespace hydra::ui

#endif  // HYDRA_UI_WIDGETS_H
