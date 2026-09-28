// The library pane: the heading, the search box with its hint and errors,
// the four status chips, and the sortable, scrolling table of every chart.
// What it shows comes from AppState::library (ui/library_model.h); this file
// draws it and hands clicks back. It replaced the paged table in the 2026-09
// interface redesign (Task 12).

#include "ui/library_parts.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "app/library_query.h"
#include "core/model.h"  // group_thousands
#include "imgui.h"
#include "imgui_internal.h"  // ImGuiSelectableFlags_SpanAvailWidth
#include "ui/app_state.h"
#include "ui/fonts.h"
#include "ui/library_model.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace hydra::ui::detail {

namespace {

// Typing is applied at most this often, so a burst of keys on a big library
// filters a few times rather than once per key.
constexpr double kSearchThrottleSeconds = 0.15;

// The table's columns, by index. Each column's user ID is its LibrarySort.
constexpr int kColumnTitle = 0;
constexpr int kColumnArtist = 1;
constexpr int kColumnCharter = 2;
constexpr int kColumnFolder = 3;
constexpr int kColumnBestPath = 4;

// The chips' look, from the approved mockup: the selected chip is filled
// teal with an accent border, the others are outlined only. White on the
// selected fill is about 7:1.
const ImVec4 kChipOnColor{0 / 255.0f, 102 / 255.0f, 102 / 255.0f, 1.0f};
const ImVec4 kChipOnHoveredColor{0 / 255.0f, 122 / 255.0f, 122 / 255.0f, 1.0f};
const ImVec4 kChipOffColor{0.0f, 0.0f, 0.0f, 0.0f};
const ImVec4 kChipOffHoveredColor{60 / 255.0f, 60 / 255.0f, 64 / 255.0f, 1.0f};
const ImVec4 kChipOffBorderColor{74 / 255.0f, 74 / 255.0f, 80 / 255.0f, 1.0f};

// Matched text: a dark gold box behind it and light gold letters (the
// mockup's highlight).
const ImU32 kMatchBgColor = IM_COL32(0x4d, 0x42, 0x00, 0xff);
const ImU32 kMatchTextColor = IM_COL32(0xff, 0xe6, 0x80, 0xff);

// "1 chart", "97 charts", "12,345 charts".
std::string charts_text(size_t n) {
    return group_thousands(static_cast<int64_t>(n)) + (n == 1 ? " chart" : " charts");
}

void clear_search(AppState& app) {
    app.library_ui.search_buf[0] = '\0';
    app.library_ui.search_pending = false;
    app.library_ui.search_applied_at = ImGui::GetTime();
    app.set_search("");
}

// "Library   5 of 97 charts".
void render_heading(AppState& app) {
    ImGui::TextUnformatted("Library");
    ImGui::SameLine();
    const size_t shown = app.library_shown_count();
    const size_t total = app.library.rows().size();
    const std::string count =
        shown == total ? charts_text(total)
                       : group_thousands(static_cast<int64_t>(shown)) + " of " + charts_text(total);
    ImGui::TextColored(kNewSongColor, "%s", count.c_str());
}

void render_search_box(AppState& app) {
    LibraryViewState& ui = app.library_ui;
    char (&buf)[256] = ui.search_buf;
    if (!ui.search_synced) {
        std::snprintf(buf, sizeof(buf), "%s", app.search.c_str());
        ui.search_synced = true;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float clear_w = ImGui::CalcTextSize("X").x + style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(clear_w + style.ItemSpacing.x));

    // Ctrl+F jumps here from anywhere except behind a dialog: focusing a
    // control behind an open popup would fight its focus.
    const bool any_popup =
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!any_popup && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
        ImGui::SetKeyboardFocusHere();

    // EscapeClearsAll: the first Escape empties the box, the next leaves it.
    ImGui::PushFont(g_mono_font, 0.0f);
    const bool edited =
        ImGui::InputTextWithHint("##search", "Search title, artist, charter or folder", buf,
                                 sizeof(buf), ImGuiInputTextFlags_EscapeClearsAll);
    ImGui::PopFont();
    hint("Ctrl+F jumps here. Escape clears it.");
    if (edited) ui.search_pending = true;

    // An emptied box applies at once; typing applies at most every 150 ms.
    const double now = ImGui::GetTime();
    if (ui.search_pending &&
        (buf[0] == '\0' || now - ui.search_applied_at >= kSearchThrottleSeconds)) {
        ui.search_pending = false;
        ui.search_applied_at = now;
        app.set_search(buf);
    }

    ImGui::SameLine();
    if (ImGui::Button("X##clearsearch")) clear_search(app);
    hint("Clear search (Esc)");

    // What the box understands, and what it couldn't.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kNewSongColor,
                       "Quotes match an exact phrase. Narrow with artist: charter: folder: "
                       "stars:7 squeeze<=20");
    for (const std::string& error : app.library.query().errors)
        ImGui::TextColored(kWarningColor, "%s", error.c_str());
    ImGui::PopTextWrapPos();
}

bool chip_button(const char* label, bool on, bool disabled) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, on ? kChipOnColor : kChipOffColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? kChipOnHoveredColor : kChipOffHoveredColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? kChipOnHoveredColor : kChipOffHoveredColor);
    ImGui::PushStyleColor(ImGuiCol_Border, on ? kAccentColor : kChipOffBorderColor);
    begin_disabled_button(disabled);
    const bool clicked = ImGui::Button(label);
    end_disabled_button(disabled);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return clicked;
}

// All (N), Not analyzed (N), Stale (N), Analyzed (N): counts over what the
// search matches. A group with nothing in it can't be picked.
void render_chips(AppState& app) {
    struct Chip {
        StatusChip chip;
        const char* name;
        const char* id;
    };
    static constexpr Chip kChips[] = {
        {StatusChip::All, "All", "chipall"},
        {StatusChip::NotAnalyzed, "Not analyzed", "chipnew"},
        {StatusChip::Stale, "Stale", "chipstale"},
        {StatusChip::Analyzed, "Analyzed", "chipdone"},
    };
    const ChipCounts& counts = app.library.counts();
    // A chip that doesn't fit after the last one starts a new line. A
    // button's width is its label plus the frame padding, known before it is
    // drawn.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float right_edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (size_t i = 0; i < std::size(kChips); ++i) {
        const Chip& c = kChips[i];
        const size_t n = counts.of(c.chip);
        const std::string label = std::string(c.name) + " (" +
                                  group_thousands(static_cast<int64_t>(n)) + ")##" + c.id;
        if (i > 0) {
            const float w =
                ImGui::CalcTextSize(label.c_str(), nullptr, true).x + style.FramePadding.x * 2.0f;
            if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + w <= right_edge)
                ImGui::SameLine();
        }
        const bool on = app.library.chip() == c.chip;
        if (chip_button(label.c_str(), on, !on && n == 0)) app.library.set_chip(c.chip);
        if (c.chip == StatusChip::Stale)
            hint("Analyzed by another Hydra version, or under different rules in "
                 "hydra_rules.ini. Re-analyze to refresh.");
    }
}

// Draws the matched parts of text already drawn at `pos` again, in the
// highlight colours, clipped at max_x.
void overlay_matches(ImVec2 pos, const std::string& text, const std::vector<app::MatchSpan>& spans,
                     float max_x) {
    if (spans.empty()) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float h = ImGui::GetTextLineHeight();
    draw->PushClipRect(pos, ImVec2(max_x, pos.y + h), true);
    for (const app::MatchSpan& s : spans) {
        const float x0 = pos.x + ImGui::CalcTextSize(text.data(), text.data() + s.begin).x;
        const float x1 = pos.x + ImGui::CalcTextSize(text.data(), text.data() + s.end).x;
        if (x0 >= max_x) break;
        draw->AddRectFilled(ImVec2(x0, pos.y), ImVec2(x1, pos.y + h), kMatchBgColor, 2.0f);
        draw->AddText(ImVec2(x0, pos.y), kMatchTextColor, text.data() + s.begin,
                      text.data() + s.end);
    }
    draw->PopClipRect();
}

// A cell's text, ellipsized, with the query's matches highlighted.
void cell_text(const std::string& text, const std::vector<app::MatchSpan>& spans) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float max_x = pos.x + ImGui::GetContentRegionAvail().x;
    text_ellipsized(text.c_str());
    overlay_matches(pos, text, spans, max_x);
}

// Draws a row's title at `pos` cut to end in "..." within `max_w`, and returns
// how wide the kept part is, so the search highlight stops before the "...".
// It goes straight to the draw list: the row's Selectable already gave the
// text log the full title.
float draw_title_ellipsized(ImVec2 pos, float max_w, const std::string& title) {
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float ellipsis_w = ImGui::GetFontBaked()->GetCharAdvance(font->EllipsisChar);
    const char* kept_end = title.data();
    const float kept_w =
        font->CalcTextSizeA(size, std::max(max_w - ellipsis_w, 1.0f), 0.0f, title.data(),
                            title.data() + title.size(), &kept_end)
            .x;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddText(font, size, pos, col, title.data(), kept_end);
    font->RenderChar(draw, size, ImVec2(IM_TRUNC(pos.x + kept_w), pos.y), col, font->EllipsisChar);
    return kept_w;
}

// Which hidden columns this frame's second lines named, for the footer.
struct SecondLineUse {
    bool folder = false;
    bool charter = false;
};

// The second line under a title while a search is on: where the row matched
// when that place isn't a visible column. The folder by default.
struct SecondLine {
    std::string text;
    std::vector<app::MatchSpan> spans;
    bool is_charter = false;
};

SecondLine second_line(const app::LibraryQuery& q, const LibraryRow& row, bool folder_shown,
                       bool charter_shown) {
    SecondLine line;
    line.text = row.entry.rootfolder;
    if (!folder_shown) line.spans = app::match_spans(q, app::QueryField::Folder, line.text);
    if (line.spans.empty() && !charter_shown) {
        std::vector<app::MatchSpan> spans = app::match_spans(q, app::QueryField::Charter, row.charter);
        if (!spans.empty()) {
            const std::string prefix = "charted by ";
            line.text = prefix + row.charter;
            for (app::MatchSpan& s : spans) {
                s.begin += prefix.size();
                s.end += prefix.size();
            }
            line.spans = std::move(spans);
            line.is_charter = true;
        }
    }
    return line;
}

SecondLineUse render_table(AppState& app, ImVec2 size) {
    SecondLineUse used;
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##librarytable", 5, flags, size)) return used;

    ImGui::TableSetupScrollFreeze(0, 1);  // the header row stays on screen
    ImGui::TableSetupColumn("Title",
                            ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort |
                                ImGuiTableColumnFlags_NoHide,
                            1.0f, static_cast<ImGuiID>(LibrarySort::Title));
    ImGui::TableSetupColumn("Artist", ImGuiTableColumnFlags_WidthStretch, 0.75f,
                            static_cast<ImGuiID>(LibrarySort::Artist));
    ImGui::TableSetupColumn("Charter", ImGuiTableColumnFlags_WidthStretch, 0.5f,
                            static_cast<ImGuiID>(LibrarySort::Charter));
    ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 0.75f,
                            static_cast<ImGuiID>(LibrarySort::Folder));
    // Highest score first on the first click.
    ImGui::TableSetupColumn("Best path",
                            ImGuiTableColumnFlags_WidthStretch |
                                ImGuiTableColumnFlags_PreferSortDescending,
                            1.0f, static_cast<ImGuiID>(LibrarySort::BestPath));

    // Charter and Folder make way for the song panel: hidden when it opens,
    // shown when it closes. In between, the header's right-click menu shows
    // or hides any column but Title.
    // T9: read app.details_open() here once it exists.
    const bool panel_open = app.details_open();
    if (app.library_ui.columns_for_panel != panel_open) {
        ImGui::TableSetColumnEnabled(kColumnCharter, !panel_open);
        ImGui::TableSetColumnEnabled(kColumnFolder, !panel_open);
        app.library_ui.columns_for_panel = panel_open;
    }
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if ((specs->SpecsDirty || !app.library_ui.sort_synced) && specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
            app.library.set_sort(static_cast<LibrarySort>(s.ColumnUserID),
                                 s.SortDirection != ImGuiSortDirection_Descending);
        }
        specs->SpecsDirty = false;
        app.library_ui.sort_synced = true;
    }

    const app::LibraryQuery& q = app.library.query();
    const bool folder_shown =
        (ImGui::TableGetColumnFlags(kColumnFolder) & ImGuiTableColumnFlags_IsEnabled) != 0;
    const bool charter_shown =
        (ImGui::TableGetColumnFlags(kColumnCharter) & ImGuiTableColumnFlags_IsEnabled) != 0;
    const bool searching_words = !q.terms.empty();
    // Every row gets the same height (the clipper needs that): two lines
    // while a word search is on and a column is hidden, one otherwise.
    const bool two_lines = searching_words && (!folder_shown || !charter_shown);
    const float line_h = ImGui::GetTextLineHeight();
    const float line_gap = ImGui::GetStyle().ItemSpacing.y;
    const float row_h = two_lines ? line_h * 2.0f + line_gap : line_h;

    const std::vector<size_t>& order = app.library_view_order();
    const std::vector<LibraryRow>& rows = app.library.rows();
    const std::string selected_path = app.selected ? app.selected->notespath : std::string();

    // A new selection (a click, or the panel's Previous/Next) is scrolled
    // into view once.
    std::optional<size_t> scroll_to;
    if (selected_path != app.library_ui.scrolled_to) {
        app.library_ui.scrolled_to = selected_path;
        for (size_t k = 0; k < order.size(); ++k)
            if (rows[order[k]].entry.notespath == selected_path) {
                scroll_to = k;
                break;
            }
    }

    // Only the rows on screen are drawn.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(order.size()));
    if (scroll_to) clipper.IncludeItemByIndex(static_cast<int>(*scroll_to));
    while (clipper.Step()) {
        for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
            const size_t index = order[static_cast<size_t>(k)];
            const LibraryRow& row = rows[index];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, row_h);
            ImGui::PushID(static_cast<int>(index));

            // The row's Selectable goes first, spanning every column, so the
            // whole row is one click target. Its label is the title, which is
            // also how GUI tests click a row ("**/<title>").
            ImGui::TableSetColumnIndex(kColumnTitle);
            const ImVec2 title_pos = ImGui::GetCursorScreenPos();
            const float title_w = ImGui::GetContentRegionAvail().x;
            const bool selected = !selected_path.empty() && row.entry.notespath == selected_path;
            // A title too long for its cell ends in "..." like the other
            // columns: the Selectable keeps its label (its ID, and the text
            // log) but draws it invisibly and lays out only the cell's width,
            // and the cut title is drawn over it.
            const bool title_cut = ImGui::CalcTextSize(row.title.c_str()).x > title_w;
            if (title_cut) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGuiSelectableFlags row_flags = ImGuiSelectableFlags_SpanAllColumns;
            if (title_cut) row_flags |= ImGuiSelectableFlags_SpanAvailWidth;
            const bool clicked = ImGui::Selectable(row.title.c_str(), selected, row_flags,
                                                   ImVec2(title_cut ? title_w : 0.0f, row_h));
            if (title_cut) ImGui::PopStyleColor();
            if (clicked) app.select(row.entry);
            if (ImGui::TableGetHoveredColumn() == kColumnTitle && title_cut)
                overflow_tooltip(row.title.c_str());
            const float title_text_w =
                title_cut ? draw_title_ellipsized(title_pos, title_w, row.title) : title_w;
            if (scroll_to && *scroll_to == static_cast<size_t>(k)) ImGui::SetScrollHereY(0.5f);
            if (searching_words)
                overlay_matches(title_pos, row.title,
                                app::match_spans(q, app::QueryField::Title, row.title),
                                title_pos.x + title_text_w);
            if (two_lines) {
                const SecondLine line = second_line(q, row, folder_shown, charter_shown);
                ImGui::SetCursorScreenPos(ImVec2(title_pos.x, title_pos.y + line_h + line_gap));
                ImGui::PushStyleColor(ImGuiCol_Text, kNewSongColor);
                const ImVec2 line_pos = ImGui::GetCursorScreenPos();
                text_ellipsized(line.text.c_str());
                ImGui::PopStyleColor();
                overlay_matches(line_pos, line.text, line.spans, title_pos.x + title_w);
                if (!line.spans.empty()) (line.is_charter ? used.charter : used.folder) = true;
            }

            // Every column but Title can be hidden from the header menu, and a
            // hidden column's TableSetColumnIndex returns false.
            if (ImGui::TableSetColumnIndex(kColumnArtist))
                cell_text(row.artist,
                          searching_words ? app::match_spans(q, app::QueryField::Artist, row.artist)
                                          : std::vector<app::MatchSpan>{});
            if (ImGui::TableSetColumnIndex(kColumnCharter))
                cell_text(row.charter,
                          searching_words ? app::match_spans(q, app::QueryField::Charter, row.charter)
                                          : std::vector<app::MatchSpan>{});
            if (ImGui::TableSetColumnIndex(kColumnFolder))
                cell_text(row.entry.rootfolder,
                          searching_words
                              ? app::match_spans(q, app::QueryField::Folder, row.entry.rootfolder)
                              : std::vector<app::MatchSpan>{});

            if (!ImGui::TableSetColumnIndex(kColumnBestPath)) {
                ImGui::PopID();
                continue;
            }
            const ImVec4 color = row.status == store::RecordStatus::Ready   ? kBestPathColor
                                 : row.status == store::RecordStatus::Stale ? kWarningColor
                                                                            : kNewSongColor;
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushFont(g_mono_font, 0.0f);
            text_ellipsized(row.best_label.c_str());
            ImGui::PopFont();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                if (row.status == store::RecordStatus::NotAnalyzed)
                    ImGui::SetTooltip("Not analyzed yet. Open the song and press \"Analyze this "
                                      "song\", or use \"Analyze library...\".");
                else if (row.status == store::RecordStatus::Stale)
                    ImGui::SetTooltip("Analyzed by another Hydra version, or under different "
                                      "rules in hydra_rules.ini. Re-analyze to refresh it.");
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    return used;
}

// Under the table while a search is on: why the rows on screen matched when
// the reason is in a hidden column, and a way out.
void render_footer(AppState& app, const SecondLineUse& used) {
    const char* why = used.folder && used.charter ? "Matched on folder and charter."
                      : used.folder               ? "Matched on folder."
                      : used.charter              ? "Matched on charter."
                                                  : nullptr;
    if (why) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kNewSongColor, "%s", why);
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Clear search")) clear_search(app);
}

}  // namespace

void render_library(AppState& app) {
    // Job-driven refreshes first, every frame, whatever is drawn below.
    app.tick_library(ImGui::GetTime());
    // An empty library shows the main window's "no songs" message instead.
    if (app.library_total == 0) return;

    render_heading(app);
    render_search_box(app);
    render_chips(app);
    ImGui::Spacing();

    if (app.library_shown_count() == 0) {
        // The chips fall back to All when their group empties, so nothing
        // shown means the search matched nothing.
        ImGui::TextUnformatted("No charts match your search.");
        if (ImGui::Button("Clear search")) clear_search(app);
        return;
    }

    const bool searching = !app.library.query().empty();
    const float footer_h = searching ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
    const float table_h = std::max(ImGui::GetContentRegionAvail().y - footer_h,
                                   ImGui::GetTextLineHeightWithSpacing() * 4.0f);
    const SecondLineUse used = render_table(app, ImVec2(0.0f, table_h));
    if (searching) render_footer(app, used);
}

}  // namespace hydra::ui::detail
