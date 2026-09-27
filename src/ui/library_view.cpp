#include "ui/library_view.h"
#include "ui/library_parts.h"

#include "imgui.h"
#include "ui/details_view.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>

namespace hydra::ui {

namespace {

// The library pane: heading, search box, chips and table (library_table.cpp),
// or the empty-library message when nothing is scanned yet.
void render_library_pane(AppState& app) {
    detail::render_library(app);
    if (app.library_total == 0)
        ImGui::TextUnformatted(detail::empty_library_message(app.settings));
}

// The library and, when a song is open, the song panel beside it. The
// library's right edge drags (ImGuiChildFlags_ResizeX, width saved in
// hydra_ui.ini); with no song open the library takes the full width.
void render_library_and_panel(AppState& app) {
    LibraryViewState& ui = app.library_ui;
    const bool panel = app.details_open();
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float min_library = px(320.0f);
    const float min_panel = px(480.0f);
    if (panel) {
        const float max_library = std::max(min_library, avail_w - min_panel);
        ImGui::SetNextWindowSizeConstraints(ImVec2(min_library, 0.0f),
                                            ImVec2(max_library, FLT_MAX));
        // Reopening: the full-width frames while closed replaced the live
        // width, so put back the one the user left.
        if (!ui.panel_was_open && ui.library_w > 0.0f)
            ImGui::SetNextWindowSize(
                ImVec2(std::clamp(ui.library_w, min_library, max_library), 0.0f),
                ImGuiCond_Always);
        ImGui::BeginChild("##library", ImVec2(avail_w * 0.4f, 0.0f), ImGuiChildFlags_ResizeX);
        ui.library_w = ImGui::GetWindowWidth();
    } else {
        // No resize flag: ImGui marks this frame's size NoSavedSettings, so
        // the saved split survives a session that ends with the panel shut.
        ImGui::BeginChild("##library", ImVec2(0.0f, 0.0f));
    }
    ui.panel_was_open = panel;
    render_library_pane(app);
    ImGui::EndChild();

    if (panel) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelBg);
        ImGui::BeginChild("##songpanel", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleColor();
        render_song_panel(app);
        ImGui::EndChild();
    }
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
    // Not NoSavedSettings: the library table's resizable column widths save
    // through the window's settings (tables inherit the flag), and losing
    // them every launch made resizing columns pointless. Position/size are
    // forced every frame anyway, so nothing else can drift.
    ImGui::Begin("Hydra", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // A finished report job is reaped by AppState::update_background_jobs.

    // Reap dmleaderboards jobs once the picker is closed: a finished report
    // job (or a fetch left running when the modal was dismissed) has nothing
    // left to show, and its thread should be joined.
    if (!app.dm_picker_open) {
        if (app.dm_report_job && app.dm_report_job->finished()) app.dm_report_job.reset();
        if (app.dm_fetch_job && app.dm_fetch_job->finished()) app.dm_fetch_job.reset();
    }

    // The song panel's "Rescan library" remedy lands here: the scan modal
    // belongs to this window, so the scan has to start from its frame.
    if (app.request_scan) {
        app.request_scan = false;
        if (!app.settings.chartfolders.empty() && !app.scan_job) {
            app.start_scan();
            ImGui::OpenPopup("Scanning charts");
        }
    }

    detail::render_actions_row(app);
    // The batch strips sit between the toolbar and the settings bar (the
    // Batch mockup).
    detail::render_batch_strip(app);
    detail::render_batch_done(app);
    detail::render_settings_bar(app);
    render_library_and_panel(app);

    if (app.scan_job) detail::render_scan_modal(app);
    detail::render_batch_confirm(app);
    if (app.dm_picker_open) detail::render_dm_picker_modal(app);

    ImGui::End();
}

}  // namespace hydra::ui
