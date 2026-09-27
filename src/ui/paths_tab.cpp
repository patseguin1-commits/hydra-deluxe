// The Paths tab: the path list on the left, the chosen path's activations on
// the right, the multiplier squeeze and score breakdown folds, Copy path and
// the backend-row limit. Every string comes from app::PathsTabCache
// (app/path_view.h); this file only lays it out. What is unfolded lives in
// PathsTabCache::ui(), on AppState, never in a static here.
//
// Text is drawn with ImGui's Text calls, not the draw list, so hydra_uitest's
// visible_text sees it. Rows are a full-width Selectable with the text laid
// over it; only decoration (arrows, circles, the timeline) uses the draw list.

#include "ui/details_parts.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "app/path_view.h"
#include "core/model.h"
#include "imgui.h"
#include "imgui_internal.h"  // RenderArrow
#include "ui/app_state.h"
#include "ui/details_view.h"
#include "ui/fonts.h"
#include "ui/label_layout.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace hydra::ui::detail {

namespace {

ImVec4 text_color() { return ImGui::GetStyleColorVec4(ImGuiCol_Text); }
ImVec4 dim_color() { return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled); }

// Draw `text` at `pos` over an item already submitted. `font` null keeps the
// current font; `wrap_w` > 0 wraps the text at that width.
void text_at(const ImVec2& pos, const ImVec4& color, const char* text,
             ImFont* font = nullptr, float wrap_w = 0.0f) {
    ImGui::SetCursorScreenPos(pos);
    if (font) ImGui::PushFont(font, 0.0f);
    if (wrap_w > 0.0f) ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_w);
    ImGui::TextColored(color, "%s", text);
    if (wrap_w > 0.0f) ImGui::PopTextWrapPos();
    if (font) ImGui::PopFont();
}

// The height of `text` wrapped at `wrap_w`, in `font` (null: the current one).
float wrapped_height(const char* text, float wrap_w, ImFont* font = nullptr) {
    if (font) ImGui::PushFont(font, 0.0f);
    const float h = ImGui::CalcTextSize(text, nullptr, false, wrap_w).y;
    if (font) ImGui::PopFont();
    return h;
}

// After text laid over a block that starts at `top` and is `h` tall: put the
// cursor under the block and submit an empty item there, so the layout goes
// on from a real item (a bare SetCursorScreenPos asserts at the window's end).
void end_overlay(const ImVec2& top, float h) {
    ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + h));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// True when an item `w` wide still fits after the last item, `spacing` apart.
bool fits_on_line(float w, float spacing) {
    return ImGui::GetItemRectMax().x + spacing + w <= ImGui::GetCurrentWindow()->WorkRect.Max.x;
}

// Keep the next item, `w` wide, on the last item's line when it fits there;
// otherwise it starts the next line. A row of variable-length pieces wraps
// instead of running past the column's edge.
void flow_next(float w, float spacing) {
    if (fits_on_line(w, spacing)) ImGui::SameLine(0.0f, spacing);
}

// Put the next item flush right, `w` wide: on the current line when it fits,
// else on the next.
void align_right(float w) {
    if (fits_on_line(w, ImGui::GetStyle().ItemSpacing.x)) ImGui::SameLine();
    const float room = ImGui::GetContentRegionAvail().x - w;
    if (room > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room);
}

float button_width(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

// ---- the path list ----------------------------------------------------------

// One path: a full-width Selectable with the id ##path<i>, the title over it
// (gold for an optimal path) and the detail line under the title. A title
// longer than the list is wide wraps onto more lines, and the button grows to
// hold them, so the whole path is always on screen.
bool path_button(size_t i, const app::PathButtonView& b, bool selected) {
    const float pad = px(6.0f);
    const float gap = px(2.0f);
    const float wrap_w = std::max(1.0f, ImGui::GetContentRegionAvail().x - pad * 2.0f);
    const float title_h = wrapped_height(b.title.c_str(), wrap_w, g_mono_font);
    const float detail_h = b.detail.empty() ? 0.0f : wrapped_height(b.detail.c_str(), wrap_w);
    const float h = pad * 2.0f + title_h + (b.detail.empty() ? 0.0f : gap + detail_h);
    char id[32];
    std::snprintf(id, sizeof(id), "##path%zu", i);
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable(id, selected, ImGuiSelectableFlags_None, ImVec2(0.0f, h));
    const bool optimal = b.group == app::PathButtonView::Group::Optimal;
    text_at(ImVec2(top.x + pad, top.y + pad), optimal ? kBestPathColor : text_color(),
            b.title.c_str(), g_mono_font, wrap_w);
    if (!b.detail.empty())
        text_at(ImVec2(top.x + pad, top.y + pad + title_h + gap),
                b.detail_warn ? kWarningColor : dim_color(), b.detail.c_str(), nullptr, wrap_w);
    end_overlay(top, h);
    return clicked;
}

// The buttons under their headings: Optimal, the "Within N" group, and
// "Best all-0 path". A heading is drawn where the group changes.
void render_path_list(const app::PathButtonsView& list, const Path*& selected_path) {
    using Group = app::PathButtonView::Group;
    for (size_t i = 0; i < list.buttons.size(); ++i) {
        const app::PathButtonView& b = list.buttons[i];
        if (i == 0 || b.group != list.buttons[i - 1].group) {
            if (i > 0) ImGui::Spacing();
            const char* heading = b.group == Group::Optimal  ? "Optimal"
                                  : b.group == Group::Within ? list.within_label.c_str()
                                                             : "Best all-0 path";
            ImGui::TextDisabled("%s", heading);
            if (b.group == Group::AllZero)
                hint("The best path that activates at the first chance every time "
                     "(no skips). It needs no squeeze timing.");
        }
        if (path_button(i, b, b.path == selected_path)) selected_path = b.path;
    }
}

// ---- the activations --------------------------------------------------------

// The strip above the rows: a thin bar the column's width, a gold mark per
// activation at its song_fraction with its number above, and the first and
// last measure under the ends. Not drawn unless every row has a fraction.
// Every mark is drawn; a number is drawn only where it has room beside the
// last one (label_layout.h), and hovering the strip names the activations
// under the mouse, so a crowded chart's hidden numbers can still be read.
void render_timeline(const app::ActivationsView& view) {
    for (const app::ActivationRowView& a : view.acts)
        if (!a.song_fraction) return;
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = px(40.0f);
    const ImVec2 top = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float bar_y = top.y + px(26.0f);
    dl->AddRectFilled(ImVec2(top.x, bar_y), ImVec2(top.x + w, bar_y + px(4.0f)),
                      IM_COL32(58, 58, 62, 255), px(2.0f));
    const float num_size = ImGui::GetFontSize() * 0.7f;
    std::vector<float> centres, widths;
    for (const app::ActivationRowView& a : view.acts) {
        const float x = top.x + static_cast<float>(*a.song_fraction) * w;
        const ImVec2 m_min(x - px(2.0f), top.y + px(20.0f));
        const ImVec2 m_max(x + px(2.0f), top.y + px(36.0f));
        dl->AddRectFilled(m_min, m_max, ImGui::GetColorU32(kBestPathColor), px(1.0f));
        // A mark whose activation needs a squeeze gets the badge's outline.
        if (!a.badge.empty())
            dl->AddRect(ImVec2(m_min.x - px(2.0f), m_min.y - px(2.0f)),
                        ImVec2(m_max.x + px(2.0f), m_max.y + px(2.0f)),
                        ImGui::GetColorU32(kWarningColor), px(1.0f), 0, px(1.5f));
        const std::string num = std::to_string(a.number);
        centres.push_back(x);
        widths.push_back(ImGui::GetFont()->CalcTextSizeA(num_size, FLT_MAX, 0.0f, num.c_str()).x);
    }
    const std::vector<std::optional<float>> lefts =
        spaced_labels(centres, widths, top.x, top.x + w, px(3.0f));
    for (size_t i = 0; i < view.acts.size(); ++i) {
        if (!lefts[i]) continue;
        const app::ActivationRowView& a = view.acts[i];
        dl->AddText(ImGui::GetFont(), num_size, ImVec2(*lefts[i], top.y + px(2.0f)),
                    ImGui::GetColorU32(a.badge.empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text),
                    std::to_string(a.number).c_str());
    }
    if (hovered) {
        // Every activation whose mark is within a few pixels of the mouse,
        // else the nearest one.
        const float mx = ImGui::GetIO().MousePos.x;
        std::string tip;
        size_t nearest = 0;
        for (size_t i = 0; i < centres.size(); ++i) {
            if (std::fabs(centres[i] - mx) < std::fabs(centres[nearest] - mx)) nearest = i;
            if (std::fabs(centres[i] - mx) <= px(6.0f)) {
                const app::ActivationRowView& a = view.acts[i];
                if (!tip.empty()) tip += "\n";
                tip += "Activation " + std::to_string(a.number) + "  " + a.measure;
                if (!a.badge.empty()) tip += "  " + a.badge;
            }
        }
        if (tip.empty() && !centres.empty()) {
            const app::ActivationRowView& a = view.acts[nearest];
            tip = "Activation " + std::to_string(a.number) + "  " + a.measure;
            if (!a.badge.empty()) tip += "  " + a.badge;
        }
        if (!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
    }
    ImGui::PushFont(g_mono_font, 0.0f);
    ImGui::TextDisabled("m1");
    align_right(ImGui::CalcTextSize(view.timeline_end.c_str()).x);
    ImGui::TextDisabled("%s", view.timeline_end.c_str());
    ImGui::PopFont();
}

// One activation's line: a full-width Selectable (##act<number>) with the fold
// arrow, the number in a circle, the notation, the measure, the bars and the
// badge laid over it. A click opens it alone, or closes it.
void render_activation_row(size_t i, const app::ActivationRowView& a, app::PathsTabUi& ui) {
    const bool open = i < ui.act_open.size() && ui.act_open[i];
    const float h = px(34.0f);
    char id[32];
    std::snprintf(id, sizeof(id), "##act%d", a.number);
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    if (ImGui::Selectable(id, open, ImGuiSelectableFlags_None, ImVec2(0.0f, h))) ui.click_row(i);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const float text_y = top.y + (h - line) * 0.5f;
    ImGui::RenderArrow(dl, ImVec2(top.x + px(4.0f), text_y + line * 0.15f),
                       ImGui::GetColorU32(ImGuiCol_TextDisabled),
                       open ? ImGuiDir_Down : ImGuiDir_Right, 0.7f);
    const ImVec2 dot(top.x + px(32.0f), top.y + h * 0.5f);
    dl->AddCircleFilled(dot, px(10.0f), IM_COL32(58, 58, 62, 255));
    const std::string num = std::to_string(a.number);
    text_at(ImVec2(dot.x - ImGui::CalcTextSize(num.c_str()).x * 0.5f, text_y), text_color(),
            num.c_str());
    text_at(ImVec2(top.x + px(52.0f), text_y), kBestPathColor, a.notation.c_str(), g_mono_font);
    text_at(ImVec2(top.x + px(104.0f), text_y), text_color(), a.measure.c_str(), g_mono_font);
    text_at(ImVec2(top.x + px(200.0f), text_y), dim_color(), a.bars.c_str());
    if (!a.badge.empty()) {
        const ImVec2 sz = ImGui::CalcTextSize(a.badge.c_str());
        const float bx = top.x + width - sz.x - px(14.0f);
        const ImVec2 b_min(bx - px(8.0f), text_y - px(2.0f));
        const ImVec2 b_max(bx + sz.x + px(8.0f), text_y + sz.y + px(2.0f));
        dl->AddRectFilled(b_min, b_max, IM_COL32(51, 38, 26, 255), px(9.0f));
        dl->AddRect(b_min, b_max, IM_COL32(106, 69, 32, 255), px(9.0f));
        text_at(ImVec2(bx, text_y), a.difficult ? kWarningColor : dim_color(), a.badge.c_str());
    }
    end_overlay(top, h);
}

// One squeeze sentence in its warm box; the border turns warning-coloured
// when the squeeze is difficult.
void squeeze_box(int act_number, size_t k, const app::TextLine& s) {
    char id[32];
    std::snprintf(id, sizeof(id), "##sq%d_%zu", act_number, k);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(51 / 255.0f, 38 / 255.0f, 26 / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          s.warn ? kWarningColor
                                 : ImVec4(106 / 255.0f, 69 / 255.0f, 32 / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(232 / 255.0f, 214 / 255.0f, 196 / 255.0f, 1.0f));
    ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(s.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor(3);
}

// The backend rows of one activation, in the columns the old table had.
void render_backend_table(const app::ActivationRowView& a) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", app::kBackendTimingsLead);
    ImGui::PopTextWrapPos();
    if (a.backends.empty()) {
        ImGui::TextDisabled("None.");
        return;
    }
    char id[32];
    std::snprintf(id, sizeof(id), "##backends%d", a.number);
    ImGui::PushFont(g_mono_font, 0.0f);
    if (ImGui::BeginTable(id, 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Timing", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("Chord", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("Rating", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const app::BackendRowView& row : a.backends) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.timing.c_str());
            if (!row.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", row.tooltip.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.chord.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(row.points.c_str());
            ImGui::TableSetColumnIndex(3);
            if (row.warn) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
            ImGui::TextUnformatted(row.rating.c_str());
            if (row.warn) ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    ImGui::PopFont();
}

// What an opened row shows: the chord and "Show in Preview", the calibration
// line, the squeeze sentences, the scale and overfill notes with their hover
// hints, and the folded backend table.
void render_activation_body(size_t i, const app::ActivationRowView& a, app::PathsTabUi& ui) {
    ImGui::Indent(px(48.0f));
    ImGui::TextDisabled("Chord");
    ImGui::SameLine();
    ImGui::PushFont(g_mono_font, 0.0f);
    ImGui::TextUnformatted(a.chord.c_str());
    ImGui::PopFont();
    char show[48];
    std::snprintf(show, sizeof(show), "Show in Preview >##showact%d", a.number);
    align_right(button_width("Show in Preview >"));
    if (ImGui::SmallButton(show)) ui.preview_jump = i;

    if (!a.calibration.empty()) ImGui::TextUnformatted(a.calibration.c_str());
    for (size_t k = 0; k < a.squeeze_sentences.size(); ++k)
        squeeze_box(a.number, k, a.squeeze_sentences[k]);
    if (!a.scale_warning.empty()) {
        {
            WarnColor warn;
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(a.scale_warning.c_str());
            ImGui::PopTextWrapPos();
        }
        hint(app::kTransferScaleHint);
    }
    if (!a.overfill_warning.empty()) {
        {
            WarnColor warn;
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(a.overfill_warning.c_str());
            ImGui::PopTextWrapPos();
        }
        hint(app::kOverfillHint);
    }

    char label[48];
    std::snprintf(label, sizeof(label), "Backend timings##act%d", a.number);
    const bool tracked = i < ui.backends_open.size();
    if (tracked) ImGui::SetNextItemOpen(ui.backends_open[i] != 0, ImGuiCond_Always);
    // NoAutoOpenOnLog: ImGui opens every tree node while it logs text (the
    // GUI tests read the screen that way), and the result is written back to
    // backends_open below, so without it a text capture would unfold the table.
    const bool backends = ImGui::TreeNodeEx(
        label, ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_NoAutoOpenOnLog);
    if (tracked) ui.backends_open[i] = backends ? 1 : 0;
    ImGui::SameLine();
    ImGui::TextDisabled("\xC2\xB7 %s", a.backends_label.c_str());
    if (backends) render_backend_table(a);
    ImGui::Unindent(px(48.0f));
    ImGui::Spacing();
}

// The right-hand column's top half: the heading and its summary, Expand all,
// the timeline, and every row with its body when open.
void render_activations(const app::ActivationsView& view, app::PathsTabUi& ui) {
    ImGui::TextUnformatted("Activations");
    if (!view.summary.empty()) {
        flow_next(ImGui::CalcTextSize(view.summary.c_str()).x, ImGui::GetStyle().ItemSpacing.x);
        ImGui::TextDisabled("%s", view.summary.c_str());
    }
    if (view.acts.empty()) {
        ImGui::TextDisabled("None.");
        return;
    }
    const bool all = ui.all_open();
    const char* toggle = all ? "Collapse all" : "Expand all";
    align_right(button_width(toggle));
    if (ImGui::SmallButton(toggle)) ui.set_all(!all);

    render_timeline(view);
    ImGui::Spacing();
    for (size_t i = 0; i < view.acts.size(); ++i) {
        const app::ActivationRowView& a = view.acts[i];
        render_activation_row(i, a, ui);
        if (i < ui.act_open.size() && ui.act_open[i]) render_activation_body(i, a, ui);
    }
}

// ---- the folds, Copy path and the backend limit -----------------------------

// A fold toggle drawn as a button that looks pressed while open.
void fold_button(const char* label, bool& open) {
    const bool was_open = open;
    if (was_open) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(label)) open = !open;
    if (was_open) ImGui::PopStyleColor();
}

void render_path_footer(AppState& app, const app::PathsTabCache::Details& d,
                        app::PathsTabUi& ui) {
    // One row that wraps where the next piece would run past the edge: the
    // summary's length changes from chart to chart.
    ImGui::Spacing();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    fold_button("Multiplier squeeze##mult", ui.mult_open);
    const std::string summary = app::multsqueeze_summary(d.squeezes);
    flow_next(ImGui::CalcTextSize(summary.c_str()).x, spacing);
    ImGui::TextDisabled("%s", summary.c_str());
    flow_next(button_width("Score breakdown##breakdown"), px(16.0f));
    fold_button("Score breakdown##breakdown", ui.breakdown_open);
    flow_next(button_width("Copy path"), px(24.0f));
    if (ImGui::Button("Copy path")) copy_selected_path(app);
    hint("Ctrl+C also copies the selected path");
    const double copied_at = app.details_ui.copied_at;
    if (copied_at >= 0.0 && ImGui::GetTime() - copied_at < 2.0) {
        flow_next(ImGui::CalcTextSize("Copied!").x, spacing);
        ImGui::TextDisabled("Copied!");
    }

    if (ui.mult_open) {
        ImGui::PushFont(g_mono_font, 0.0f);
        if (d.squeezes.empty()) ImGui::TextDisabled("None.");
        for (const app::MultSqueezeView& msq : d.squeezes) {
            ImGui::TextUnformatted(msq.label.c_str());
            ImGui::Indent(px(24.0f));
            ImGui::TextUnformatted(msq.howto.c_str());
            ImGui::Unindent(px(24.0f));
        }
        ImGui::PopFont();
    }
    if (ui.breakdown_open) {
        ImGui::PushFont(g_mono_font, 0.0f);
        for (const std::string& line : d.breakdown) ImGui::TextUnformatted(line.c_str());
        ImGui::PopFont();
    }

    // The backend tables' display window. Purely a filter on what the tables
    // draw: it never reaches the search, so it never re-keys a stored record.
    ImGui::Spacing();
    if (ImGui::Checkbox("Hide backend rows beyond##backendlimit", &app.settings.backendlimit_enabled))
        app.commit_settings();
    ImGui::SameLine();
    const bool limit_off = !app.settings.backendlimit_enabled;
    begin_disabled_input(limit_off);
    ImGui::SetNextItemWidth(px(100.0f));
    if (ImGui::InputInt("##backendlimitvalue", &app.settings.backendlimit_value)) {
        app.settings.backendlimit_value = std::clamp(app.settings.backendlimit_value, 0, 500);
        app.commit_settings();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("ms");
    end_disabled_input(limit_off);
    hint("Hides backend rows beyond +/- this many ms; squeezed-out notes always "
         "show. Display only: changing it never re-analyzes.");
}

}  // namespace

void copy_selected_path(AppState& app) {
    const Path* path = app.details_ui.selected_path;
    if (path == nullptr || !app.viewed.record) return;
    ImGui::SetClipboardText(path->pathstring_verbose(app.viewed.record->multsqueezes).c_str());
    app.details_ui.copied_at = ImGui::GetTime();
}

void render_path_panel(AppState& app, const Path*& selected_path) {
    app::PathsTabCache& cache = app.details_ui.paths_tab;
    const HydraRecord& record = *app.viewed.record;
    const int generation = app.record_generation.n;
    const app::PathButtonsView& list =
        cache.buttons(record, generation, app.settings.depth_mode, app.settings.depth_value);

    // The list is as wide as its longest line, up to kMaxPathListShare of the
    // tab and never taking the details below their minimum; a longer path
    // wraps (path_button). Never under 240 px.
    float widest = 0.0f;
    ImGui::PushFont(g_mono_font, 0.0f);
    for (const app::PathButtonView& b : list.buttons)
        widest = std::max(widest, ImGui::CalcTextSize(b.title.c_str()).x);
    ImGui::PopFont();
    for (const app::PathButtonView& b : list.buttons)
        widest = std::max(widest, ImGui::CalcTextSize(b.detail.c_str()).x);
    const float list_fit = widest + px(6.0f) * 2.0f + ImGui::GetStyle().ScrollbarSize;
    const float tab_w = ImGui::GetContentRegionAvail().x;
    const float list_max =
        std::min(tab_w * kMaxPathListShare, tab_w - px(24.0f) - px(kMinPathDetailsW));
    const float list_w = std::max(px(kMinPathListW), std::min(list_fit, list_max));
    ImGui::BeginChild("##pathlist", ImVec2(list_w, 0.0f));
    render_path_list(list, selected_path);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, px(24.0f));
    ImGui::BeginChild("##pathdetails", ImVec2(0.0f, 0.0f));
    if (selected_path) {
        // details() first: a new path resets ui() before anything reads it.
        const app::PathsTabCache::Details& d = cache.details(
            *selected_path, record, generation,
            app.viewed.timing ? &*app.viewed.timing : nullptr,
            static_cast<double>(app.settings.hit_window_ms), app.settings.backend_limit(),
            app.settings.rules, app.viewed.song_length_ms);
        app::PathsTabUi& ui = cache.ui();
        render_activations(d.activations, ui);
        render_path_footer(app, d, ui);
    }
    ImGui::EndChild();
}

}  // namespace hydra::ui::detail
