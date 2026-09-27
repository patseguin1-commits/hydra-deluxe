#include "ui/library_parts.h"

#include "imgui.h"
#include "store/record_store.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace hydra::ui {

namespace {

const char* summary_label(const LibraryPage::RowSummary& summary, ImVec4* out_color) {
    switch (summary.state) {
        case store::RecordStatus::Ready:
            *out_color = kBestPathColor;
            return summary.bestpath.c_str();
        case store::RecordStatus::Stale:
            *out_color = kWarningColor;
            return "(Stale)";
        default:
            *out_color = kNewSongColor;
            return "(New...)";
    }
}

}  // namespace

namespace detail {

void render_search_box(AppState& app) {
    char (&buf)[256] = app.library_ui.search_buf;
    bool& synced = app.library_ui.search_synced;
    double& edited_at = app.library_ui.search_edited_at;
    if (!synced) {
        std::snprintf(buf, sizeof(buf), "%s", app.search.c_str());
        synced = true;
    }
    ImGui::TextUnformatted("Search:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(282));
    // Ctrl+F jumps to the search box (only while no modal is up -- focusing
    // a control behind an open modal would fight its focus).
    if (!app.show_details && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
        ImGui::SetKeyboardFocusHere();
    // The hint spells out what the search actually matches (it silently
    // covered only title/artist before charter was added to the query).
    if (ImGui::InputTextWithHint("##search", "title, artist, or charter", buf, sizeof(buf)))
        edited_at = ImGui::GetTime();

    // Debounced: re-running two COUNT(*)s plus a leading-wildcard LIKE on
    // every keystroke stuttered on large libraries.
    if (edited_at >= 0.0 && ImGui::GetTime() - edited_at > 0.25) {
        edited_at = -1.0;
        if (app.search != buf) {
            app.search = buf;
            app.table_viewpage = 0;
            app.refresh_page();
        }
    }
}

void render_library_table(AppState& app, int visible_rows) {
    // Reserve room below the table for the pagination row so the table's
    // own auto height doesn't push it off the bottom of the window.
    float footer_h = ImGui::GetFrameHeightWithSpacing();
    ImVec2 outer_size(0, ImGui::GetContentRegionAvail().y - footer_h);
    if (!ImGui::BeginTable("library", 5,
                           ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable |
                               ImGuiTableFlags_SizingStretchProp,
                           outer_size)) {
        return;
    }
    ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Artist", ImGuiTableColumnFlags_WidthStretch, 0.75f);
    ImGui::TableSetupColumn("Charter", ImGuiTableColumnFlags_WidthStretch, 0.5f);
    ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 0.75f);
    ImGui::TableSetupColumn("Best Path", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableHeadersRow();

    for (int r = 0; r < visible_rows; ++r) {
        ImGui::TableNextRow();
        if (r >= (int)app.current_page.rows.size()) {
            for (int c = 0; c < 5; ++c) {
                ImGui::TableSetColumnIndex(c);
                ImGui::TextDisabled("-----");
            }
            continue;
        }

        const store::ChartLibraryEntry& row = app.current_page.rows[(size_t)r];
        ImGui::PushID(r);

        // The row-selectable must be the first item placed in the row, with
        // ImGuiSelectableFlags_SpanAllColumns, for its clickable hit-rect to
        // actually cover the whole row rather than just whichever column it
        // was drawn in -- placing it in the last column (as this used to)
        // left rows unclickable outside that column, which got worse as
        // "Best Path" narrowed (e.g. after paging changed its content).
        ImGui::TableSetColumnIndex(0);
        // A long title hard-clips at the column edge; row_selectable offers
        // the full text on hover while actually over the Title column.
        if (row_selectable(row.title.c_str(), false)) app.select(row);

        ImGui::TableSetColumnIndex(1);
        text_ellipsized(row.artist.c_str());
        ImGui::TableSetColumnIndex(2);
        text_ellipsized(row.charter.c_str());
        ImGui::TableSetColumnIndex(3);
        text_ellipsized(row.rootfolder.c_str());

        ImGui::TableSetColumnIndex(4);
        ImVec4 color = kDefaultTextColor;
        LibraryPage::RowSummary fallback;
        const LibraryPage::RowSummary& summary =
            (size_t)r < app.current_page.summaries.size() ? app.current_page.summaries[(size_t)r]
                                                          : fallback;
        const char* label = summary_label(summary, &color);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::PushFont(g_mono_font, 0.0f);  // matches hydra_app.py binding MonoFont here
        // Ellipsized: a long path in a narrowed column trails off visibly and
        // shows in full on hover (via text_ellipsized's own tooltip).
        text_ellipsized(label);
        ImGui::PopFont();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            if (summary.state == store::RecordStatus::NotAnalyzed)
                ImGui::SetTooltip("Not analyzed yet. Open the song and press \"Analyze "
                                  "paths!\", or use Analyze library.");
            else if (summary.state == store::RecordStatus::Stale)
                ImGui::SetTooltip("Analyzed by an older Hydra version. "
                                  "Re-analyze to refresh it.");
        }

        ImGui::PopID();
    }
    ImGui::EndTable();

    bool at_first_page = app.table_viewpage <= 0;
    begin_disabled_button(at_first_page);
    if (ImGui::ArrowButton("##pageleft", ImGuiDir_Left)) {
        app.table_viewpage = std::max(0, app.table_viewpage - 1);
        app.refresh_page();
    }
    end_disabled_button(at_first_page);
    ImGui::SameLine();

    // (total - 1) / rows: an exact multiple of rows_per_page must not mint a
    // trailing empty page (30 charts / 15 rows is 2 pages, not 3).
    int64_t last_page =
        std::max<int64_t>(0, (app.current_page.total_count - 1) / app.rows_per_page);
    // "1/12" .. "12/12" in a slot as wide as the last page's digits twice, so
    // the right arrow doesn't move when the page number grows a digit.
    char page_text[32];
    std::snprintf(page_text, sizeof(page_text), "%d/%lld", app.table_viewpage + 1,
                  (long long)last_page + 1);
    std::string page_digits = widest_digits(digit_count((long long)last_page + 1));
    text_in_slot(page_text, text_slot_width((page_digits + "/" + page_digits).c_str()));

    bool at_last_page = app.table_viewpage >= last_page;
    begin_disabled_button(at_last_page);
    if (ImGui::ArrowButton("##pageright", ImGuiDir_Right)) {
        app.table_viewpage = std::min((int)last_page, app.table_viewpage + 1);
        app.refresh_page();
    }
    end_disabled_button(at_last_page);
}

}  // namespace detail

}  // namespace hydra::ui
