#include "ui/library_view.h"

#include "app/edition.h"
#include "imgui.h"
#include "store/record_store.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/win32_dialogs.h"

#include <algorithm>
#include <cstdio>

namespace hydra::ui {

namespace {

HWND main_hwnd() {
    return static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
}

void render_chart_folders(AppState& app) {
    if (ImGui::TreeNode("Song folders:")) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kFolderListBg);
        ImGui::BeginChild("songfolders", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() *
                                                        (float)std::max<size_t>(
                                                            app.settings.chartfolders.size(), 1)),
                          ImGuiChildFlags_Borders);
        if (app.settings.chartfolders.empty()) {
            ImGui::TextDisabled("(None.)");
        } else {
            std::optional<size_t> to_remove;
            for (size_t i = 0; i < app.settings.chartfolders.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::PushStyleColor(ImGuiCol_Button, kDeleteButtonColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kDeleteButtonHoveredColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, kDeleteButtonActiveColor);
                if (ImGui::Button("X")) to_remove = i;
                ImGui::PopStyleColor(3);
                ImGui::SameLine();
                ImGui::TextUnformatted(app.settings.chartfolders[i].c_str());
                ImGui::PopID();
            }
            if (to_remove) {
                app.settings.chartfolders.erase(app.settings.chartfolders.begin() +
                                                (long)*to_remove);
                app.settings.is_rescan = false;
                app.settings.save();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::TreePop();
    }

    if (ImGui::Button("Add folder...")) {
        if (auto folder = browse_for_folder(main_hwnd())) {
            bool already = false;
            for (const std::string& f : app.settings.chartfolders)
                if (f == *folder) already = true;
            if (!already) app.settings.chartfolders.push_back(*folder);
            app.settings.is_rescan = false;
            app.settings.save();
        }
    }
    ImGui::SameLine();

    bool can_scan = !app.settings.chartfolders.empty();
    begin_disabled_button(!can_scan);
    if (ImGui::Button(app.settings.is_rescan ? "Refresh scan" : "Scan charts")) {
        app.start_scan();
        ImGui::OpenPopup("Scanning charts");
    }
    end_disabled_button(!can_scan);
    ImGui::SameLine();

    bool searching = !app.search.empty();
    char label[96];
    if (searching)
        std::snprintf(label, sizeof(label), "Analyze search (%lld)",
                      (long long)app.current_page.total_count);
    else
        std::snprintf(label, sizeof(label), "Analyze library");
    if (ImGui::Button(label)) {
        app.start_batch(app.batch_redo);
        ImGui::OpenPopup("Analyzing");
    }
    ImGui::SameLine();
    ImGui::Checkbox("redo existing", &app.batch_redo);
}

void render_view_controls(AppState& app) {
    ImGui::TextUnformatted("View:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::BeginDisabled(true);  // only "Expert" exists end-to-end today
    int idx = 0;
    const char* items[] = {"Expert"};
    ImGui::Combo("##difficulty", &idx, items, 1);
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Checkbox("Pro Drums", &app.settings.view_prodrums)) {
        app.settings.save();
        app.table_viewpage = 0;
        app.refresh_page();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("2x Bass", &app.settings.view_bass2x)) {
        app.settings.save();
        app.table_viewpage = 0;
        app.refresh_page();
    }
}

void render_search_box(AppState& app) {
    static char buf[256] = "";
    static bool synced = false;
    if (!synced) {
        std::snprintf(buf, sizeof(buf), "%s", app.search.c_str());
        synced = true;
    }
    ImGui::TextUnformatted("Search:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(282);
    if (ImGui::InputText("##search", buf, sizeof(buf))) {
        app.search = buf;
        app.table_viewpage = 0;
        app.refresh_page();
    }
}

const char* summary_label(AppState& app, const store::ChartLibraryEntry& row,
                          ImVec4* out_color) {
    static thread_local std::string buf;
    auto summary = app.store->get_summary(row.md5, app.settings.chartmode_key());
    if (!summary) {
        *out_color = kNewSongColor;
        return "(New...)";
    }
    const auto& [hyversion, bestpath] = *summary;
    if (hyversion == store::current_record_version(hydra::kUncapped)) {
        *out_color = kBestPathColor;
        buf = bestpath;
        return buf.c_str();
    }
    *out_color = kWarningColor;
    return "(Update...)";
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
        if (ImGui::Selectable(row.title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
            app.select(row);

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(row.artist.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(row.charter.c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(row.rootfolder.c_str());

        ImGui::TableSetColumnIndex(4);
        ImVec4 color = kDefaultTextColor;
        const char* label = summary_label(app, row, &color);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::PushFont(g_mono_font, 0.0f);  // matches hydra_app.py binding MonoFont here
        ImGui::TextUnformatted(label);
        ImGui::PopFont();
        ImGui::PopStyleColor();

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

    int64_t last_page = app.current_page.total_count / app.rows_per_page;
    ImGui::Text("%d/%lld", app.table_viewpage + 1, (long long)last_page + 1);
    ImGui::SameLine();

    bool at_last_page = app.table_viewpage >= last_page;
    begin_disabled_button(at_last_page);
    if (ImGui::ArrowButton("##pageright", ImGuiDir_Right)) {
        app.table_viewpage = std::min((int)last_page, app.table_viewpage + 1);
        app.refresh_page();
    }
    end_disabled_button(at_last_page);
}

void render_scan_modal(AppState& app) {
    if (!ImGui::BeginPopupModal("Scanning charts", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ScanProgress p = app.scan_job->snapshot();
    if (p.phase == ScanProgress::Phase::CountingFolders) {
        ImGui::Text("Discovering folders... (%d found)", p.folders_seen);
    } else {
        ImGui::Text("Discovering folders... (%d found)", p.folders_total);
        float frac = p.folders_total > 0 ? (float)p.folders_seen / (float)p.folders_total : 0.0f;
        char overlay[32];
        std::snprintf(overlay, sizeof(overlay), "%d/%d", p.folders_seen, p.folders_total);
        ImGui::ProgressBar(frac, ImVec2(-1, 0), overlay);
    }

    if (p.phase == ScanProgress::Phase::Writing)
        ImGui::TextUnformatted("Writing library...");

    if (p.phase == ScanProgress::Phase::Done) {
        ImGui::Text("Done! %d chart(s) found.", p.charts_found);
        if (!p.errors.empty()) {
            ImGui::TextColored(kWarningColor, "Skipped %d folder(s) with errors:",
                               (int)p.errors.size());
            ImGui::BeginChild("scanerrors", ImVec2(-1, 100), ImGuiChildFlags_Borders);
            for (const std::string& e : p.errors) ImGui::TextUnformatted(e.c_str());
            ImGui::EndChild();
        }
        if (ImGui::Button("Continue")) {
            app.settings.is_rescan = true;
            app.settings.save();
            app.table_viewpage = 0;
            app.refresh_page();
            app.scan_job.reset();
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndPopup();
}

void render_batch_modal(AppState& app) {
    if (!ImGui::BeginPopupModal("Analyzing", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    BatchJob::Snapshot s = app.batch_job->snapshot();
    ImGui::Text(app.batch_job->is_cancelled() ? "Cancelling..."
                                              : s.finished ? "Finished." : "Analyzing...");
    if (!s.current_title.empty()) ImGui::TextUnformatted(s.current_title.c_str());

    float frac = s.total > 0 ? (float)s.completed / (float)s.total : 1.0f;
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%d/%d", s.completed, s.total);
    ImGui::ProgressBar(frac, ImVec2(-1, 0), overlay);
    ImGui::Text("%d analyzed, %d already stored, %d failed.", s.completed - s.failed, s.skipped,
               s.failed);

    if (s.finished) {
        if (!s.failures.empty()) {
            ImGui::TextColored(kWarningColor, "Failed:");
            ImGui::BeginChild("batchfailures", ImVec2(-1, 120), ImGuiChildFlags_Borders);
            for (const std::string& f : s.failures) ImGui::TextUnformatted(f.c_str());
            ImGui::EndChild();
        }
        if (ImGui::Button("Continue")) {
            app.batch_job.reset();
            app.refresh_page();
            ImGui::CloseCurrentPopup();
        }
    } else {
        if (ImGui::Button("Cancel")) app.batch_job->cancel();
    }

    ImGui::EndPopup();
}

}  // namespace

void render_main_window(AppState& app) {
    // The primary window: fixed to the full viewport, no title bar/resize/
    // move/collapse of its own -- mirrors hydra_app.py's
    // dpg.set_primary_window("mainwindow", True), a single window that IS the
    // app rather than a panel floating inside it.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Hydra", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    ImGui::SeparatorText("Settings");
    render_chart_folders(app);

    // Total library size, not the current search's match count -- matches
    // hydra_app.py's refresh_librarytitle, which is driven by
    // appstate.librarysize (cached at scan time), independent of the search box.
    char libtitle[64];
    std::snprintf(libtitle, sizeof(libtitle), "Library (%lld chart%s)",
                 (long long)app.library_total, app.library_total == 1 ? "" : "s");
    ImGui::SeparatorText(libtitle);
    render_view_controls(app);
    render_search_box(app);
    ImGui::Spacing();

    // Fill whatever vertical space is left (minus room for the pagination
    // row) with as many library rows as fit, instead of a fixed row count
    // that leaves blank space on a tall window or clips on a short one.
    float footer_h = ImGui::GetFrameHeightWithSpacing();
    float header_h = ImGui::GetFrameHeightWithSpacing();
    float row_h = ImGui::GetTextLineHeightWithSpacing();
    float avail = ImGui::GetContentRegionAvail().y - footer_h - header_h;
    int visible_rows = std::clamp((int)(avail / row_h), 5, 200);
    app.set_rows_per_page(visible_rows);

    if (app.current_page.total_count > 0) {
        render_library_table(app, visible_rows);
    } else {
        ImGui::TextUnformatted(
            "No songs scanned. Set a folder and scan songs to get started!");
    }

    if (app.scan_job) render_scan_modal(app);
    if (app.batch_job) render_batch_modal(app);

    ImGui::End();
}

}  // namespace hydra::ui
