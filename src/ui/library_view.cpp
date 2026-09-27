#include "ui/library_view.h"
#include "ui/library_parts.h"

#include "imgui.h"
#include "ui/details_view.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "ui/app_shell.h"

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
// library's right edge drags (ImGuiChildFlags_ResizeX); with no song open the
// library takes the full width.
//
// The split is kept as the library's share of the width, in hydra_ui.ini's
// [Hydra][Layout] section, and only a drag changes it. The child itself
// saves nothing (NoSavedSettings): ImGui would save the full width of the
// frames with no song open, clamp it to the largest split on the next open,
// and so squeeze every later panel down to its minimum.
void render_library_and_panel(AppState& app) {
    LibraryViewState& ui = app.library_ui;
    const bool panel = app.details_open();
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float min_library = px(320.0f);
    if (panel) {
        const float max_library = std::max(min_library, avail_w - px(kMinSongPanelW));
        ImGui::SetNextWindowSizeConstraints(ImVec2(min_library, 0.0f),
                                            ImVec2(max_library, FLT_MAX));
        // Set the width from the share when the panel opens or the room
        // changes. Setting it turns the drag off for that one frame (ImGui
        // drops ResizeX under a SetNextWindowSize), so only then.
        // A new UI scale moves the minimums, so it counts as a change too.
        const bool set_width = !ui.panel_was_open || ui.split_set_for_w != avail_w ||
                               ui.split_set_for_px != px(1.0f);
        if (set_width) {
            const float want = std::clamp(library_share() * avail_w, min_library, max_library);
            ImGui::SetNextWindowSize(ImVec2(want, 0.0f), ImGuiCond_Always);
            ui.split_set_for_w = avail_w;
            ui.split_set_for_px = px(1.0f);
        }
        ImGui::BeginChild("##library", ImVec2(avail_w * kDefaultLibraryShare, 0.0f),
                          ImGuiChildFlags_ResizeX, ImGuiWindowFlags_NoSavedSettings);
        const float w = ImGui::GetWindowWidth();
        // Any other change of width is the user dragging the edge.
        if (!set_width && std::fabs(w - ui.library_w) > 0.5f && avail_w > 0.0f)
            remember_library_share(w / avail_w);
        ui.library_w = w;
    } else {
        ImGui::BeginChild("##library", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoSavedSettings);
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
