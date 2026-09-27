#include "ui/library_view.h"
#include "ui/library_parts.h"

#include "app/report_files.h"
#include "imgui.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cstdio>

namespace hydra::ui {

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

    // A finished report job has nothing left to show once the batch modal is
    // gone (its status lines live there); reclaim the thread.
    if (!app.batch_job && app.report_job && app.report_job->finished()) {
        app.library_ui.report_checked_at = -1.0;  // a new report: look at once
        // You clicked Continue while the report was still building, so the
        // modal never showed how it ended. Say so here instead. A cancelled
        // job says nothing -- you asked for it to stop.
        if (!app.report_outcome_shown && !app.report_job->is_cancelled()) {
            if (app.report_job->ok()) app.set_status("Path report ready.");
            else app.set_status("Path report failed: " + app.report_job->error());
        }
        app.report_job.reset();
    }

    // A cancelled single-chart analysis has nothing to report; reap it here
    // so it doesn't linger after the details modal closed mid-run.
    if (app.analyze_job && app.analyze_job->finished() && app.analyze_job->is_cancelled())
        app.analyze_job.reset();

    // Reap dmleaderboards jobs once the picker is closed: a finished report
    // job (or a fetch left running when the modal was dismissed) has nothing
    // left to show, and its thread should be joined.
    if (!app.dm_picker_open) {
        if (app.dm_report_job && app.dm_report_job->finished()) app.dm_report_job.reset();
        if (app.dm_fetch_job && app.dm_fetch_job->finished()) app.dm_fetch_job.reset();
    }

    // The details modal's "Rescan library" remedy lands here: the scan modal
    // belongs to this window, so the scan has to start from its frame.
    if (app.request_scan) {
        app.request_scan = false;
        if (!app.settings.chartfolders.empty() && !app.scan_job) {
            app.start_scan();
            ImGui::OpenPopup("Scanning charts");
        }
    }

    detail::render_actions_row(app);

    // Interim (Task 12): the library pane's own heading ("Library  5 of 97
    // charts") replaced the separator title that read the old paged count.
    // The view controls and the report button stay on their own row until
    // T9's layout replaces this block at merge.
    ImGui::Separator();

    // A way back into the last batch's HTML report (it used to exist only as
    // an unrequested browser launch right after a batch). While a report job
    // is still building, this slot shows a greyed "Building path report..."
    // button instead, so the window says the report isn't ready yet.
    detail::render_view_controls(app);
    if (app.report_job && !app.report_job->finished()) {
        ImGui::SameLine();
        begin_disabled_button(true);
        ImGui::Button("Building path report...");
        end_disabled_button(true);
    } else if (app.report_file_shown(ImGui::GetTime())) {
        ImGui::SameLine();
        if (ImGui::Button("Open path report") && !app::open_report_in_browser())
            app.set_status("The path report could not be opened.");
    }
    detail::render_library(app);
    if (app.library_total == 0)
        ImGui::TextUnformatted(
            "No songs scanned. Click \"Manage folders...\" to add your song folder, "
            "then \"Scan charts\" to get started!");

    if (app.scan_job) detail::render_scan_modal(app);
    if (app.batch_job || app.batch_confirm_pending) detail::render_batch_modal(app);
    if (app.dm_picker_open) detail::render_dm_picker_modal(app);

    ImGui::End();
}

}  // namespace hydra::ui
