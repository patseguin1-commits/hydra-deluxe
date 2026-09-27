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

    // A finished report job is reaped by AppState::update_background_jobs.

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
    detail::render_batch_strip(app);
    detail::render_batch_done(app);

    // While searching, show the match count against the whole library; the
    // full total alone above three search results read as a wrong count.
    char libtitle[96];
    if (!app.search.empty())
        std::snprintf(libtitle, sizeof(libtitle), "Library (%lld of %lld chart%s)",
                     (long long)app.current_page.total_count, (long long)app.library_total,
                     app.library_total == 1 ? "" : "s");
    else
        std::snprintf(libtitle, sizeof(libtitle), "Library (%lld chart%s)",
                     (long long)app.library_total, app.library_total == 1 ? "" : "s");
    ImGui::SeparatorText(libtitle);
    detail::render_view_controls(app);
    detail::render_search_box(app);
    // "Open path report" sits at the toolbar's right end (render_actions_row).
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
        detail::render_library_table(app, visible_rows);
    } else if (app.library_total > 0) {
        // The library has charts; the search just matched none of them —
        // the "no songs scanned" onboarding text here was misleading.
        ImGui::TextUnformatted("No charts match your search.");
    } else {
        ImGui::TextUnformatted(detail::empty_library_message(app.settings));
    }

    if (app.scan_job) detail::render_scan_modal(app);
    detail::render_batch_confirm(app);
    if (app.dm_picker_open) detail::render_dm_picker_modal(app);

    ImGui::End();
}

}  // namespace hydra::ui
