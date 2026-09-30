#include "ui/library_parts.h"

#include "app/report_files.h"
#include "core/model.h"
#include "imgui.h"
#include "ui/fonts.h"
#include "ui/generation.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace hydra::ui::detail {

// The status line, on its own wrapping line under the toolbar. News is
// neutral and fades after a few seconds; a problem is orange and stays until
// its X is clicked.
void render_status_line(AppState& app) {
    // The watcher starts at "seen" for this app's counter, so startup does
    // not start a fade.
    GenerationWatcher& generation = app.library_ui.status_watcher;
    double& shown_at = app.library_ui.status_shown_at;
    if (generation.changed(app.status_generation)) shown_at = ImGui::GetTime();
    if (shown_at < 0.0 || app.status_message.empty()) return;
    if (!app.status_is_problem && ImGui::GetTime() - shown_at > 6.0) return;
    if (app.status_is_problem) {
        if (ImGui::SmallButton("X##dismissstatus")) {
            app.dismiss_status();
            return;
        }
        hint("Dismiss");
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, app.status_is_problem ? kWarningColor : kDefaultTextColor);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(app.status_message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// The toolbar: library-wide actions on the left, the last report on the right.
void render_actions_row(AppState& app) {
    const bool batch_busy = app.batch_job && !app.batch_job->snapshot().finished;
    auto busy_tooltip = [&] {
        if (batch_busy && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                               ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Busy: a batch is running.");
    };

    // Buttons with live counts take the width of their widest label, so the
    // row doesn't reflow under the mouse when a count changes.
    int folder_count = (int)app.settings.chartfolders.size();
    char manage_label[64];
    std::snprintf(manage_label, sizeof(manage_label), "Manage folders... (%d)", folder_count);
    std::string manage_widest =
        "Manage folders... (" + widest_digits(digit_count(folder_count)) + ")";
    if (button_in_slot(manage_label, button_slot_width(manage_widest.c_str())))
        ImGui::OpenPopup("Song folders");
    hint("Add or remove the folders Hydra scans for charts");
    ImGui::SameLine();

    const bool can_scan = !app.settings.chartfolders.empty() && !batch_busy && !app.scan_job;
    begin_disabled_button(!can_scan);
    if (ImGui::Button("Scan library")) {
        app.start_scan();
        ImGui::OpenPopup("Scanning charts");
    }
    end_disabled_button(!can_scan);
    busy_tooltip();
    ImGui::SameLine();

    const bool searching = !app.search.empty();
    const int64_t analyzable = searching ? static_cast<int64_t>(app.library_match_count()) : app.library_total;
    const std::string label = searching
                                  ? "Analyze search (" + group_thousands(analyzable) + ")..."
                                  : std::string("Analyze library...");
    // The slot fits the widest count this library can show, commas included.
    std::string sample = group_thousands(app.library_total);
    const char widest = widest_digits(1)[0];
    for (char& c : sample)
        if (c >= '0' && c <= '9') c = widest;
    const float analyze_w = std::max(button_slot_width("Analyze library..."),
                                     button_slot_width(("Analyze search (" + sample + ")...").c_str()));
    // Off with nothing to analyze, under a bad hydra_rules.ini, or mid-batch.
    const bool analyze_off = analyzable == 0 || app.analysis_blocked() || batch_busy;
    begin_disabled_button(analyze_off);
    if (button_in_slot(label.c_str(), analyze_w)) app.open_batch_confirm();
    end_disabled_button(analyze_off);
    busy_tooltip();
    ImGui::SameLine();

    // The leaderboard plays by Clone Hero's rules at Expert, so the comparison
    // only means anything there. Disabled elsewhere, with the reason on hover.
    const bool expert = app.settings.difficulty() == Difficulty::Expert;
    const bool ch_cap = app.settings.sp_cap == kCloneHeroSpCap;
    const bool ch11_fills = !app.settings.legacy_fills;
    const bool compare_off = !expert || !ch_cap || !ch11_fills;
    begin_disabled_button(compare_off);
    if (ImGui::Button("Compare with dmleaderboards...")) {
        // Fresh picker: drop a finished report (a running one is parked, not
        // joined), and refetch the ladder only when this session has none.
        app.cancel_dm_report();
        app.dm_picker_open = true;
        if (app.dm_users.empty()) app.start_dm_fetch();
        ImGui::OpenPopup("Compare dmleaderboards user");
    }
    end_disabled_button(compare_off);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (!expert)
            ImGui::SetTooltip("Needs Expert: the leaderboard only has Expert scores.");
        else if (!ch_cap)
            ImGui::SetTooltip("Needs SP cap %d, Clone Hero's rule: the leaderboard's scores "
                              "were played under it.",
                              kCloneHeroSpCap);
        else if (!ch11_fills)
            ImGui::SetTooltip("Needs Clone Hero 1.1 fills: untick \"1.0 fills\". The "
                              "leaderboard is played on current Clone Hero.");
        else
            ImGui::SetTooltip("Compare a dmleaderboards.com player's scores against your library");
    }

    // The last report, at the right. While one builds, a greyed button says so.
    const bool building = app.report_job && !app.report_job->finished();
    if (building || app.report_file_shown(ImGui::GetTime())) {
        const float report_w = std::max(button_slot_width("Open path report"),
                                        button_slot_width("Building path report..."));
        ImGui::SameLine();
        const float right = ImGui::GetContentRegionMax().x - report_w;
        if (right > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right);
        if (building) {
            begin_disabled_button(true);
            ImGui::Button("Building path report...", ImVec2(report_w, 0.0f));
            end_disabled_button(true);
        } else if (ImGui::Button("Open path report", ImVec2(report_w, 0.0f)) &&
                   !app::open_report_in_browser()) {
            app.set_problem("Windows couldn't open the path report in your browser.");
        }
    }

    render_status_line(app);
    // A bad rules file is not a passing message: it stays under the toolbar
    // for as long as analysis is off.
    if (app.analysis_blocked()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarningColor,
                           "hydra_rules.ini has an error, so analysis is off until the "
                           "file is fixed and Hydra is restarted.");
        ImGui::TextColored(kWarningColor, "%s", app.rules_error.c_str());
        ImGui::PopTextWrapPos();
    }
    render_folder_manager(app);
}

}  // namespace hydra::ui::detail
