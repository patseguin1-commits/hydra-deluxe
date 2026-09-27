#include "ui/library_parts.h"

#include "app/report_files.h"
#include "imgui.h"
#include "ui/fonts.h"
#include "ui/generation.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace hydra::ui::detail {

// Transient feedback for actions that used to fail silently (duplicate
// folder, unwritable INI, folder picker not opening). Fades out a few
// seconds after the message changes. same_line appends it to the current
// row (the main action bar); the folder manager renders it on its own line.
void render_status_line(AppState& app, bool same_line) {
    // Lives on the AppState (LibraryViewState); the watcher starts at "seen"
    // for this app's counter, so startup does not start a fade.
    GenerationWatcher& generation = app.library_ui.status_watcher;
    double& shown_at = app.library_ui.status_shown_at;
    if (generation.changed(app.status_generation)) shown_at = ImGui::GetTime();
    if (shown_at < 0.0 || app.status_message.empty()) return;
    if (ImGui::GetTime() - shown_at > 6.0) return;
    if (same_line) ImGui::SameLine();
    ImGui::TextColored(kWarningColor, "%s", app.status_message.c_str());
}

// The single row of library-wide actions at the top of the main screen.
void render_actions_row(AppState& app) {
    // The buttons here carry live counts and swap labels, so each takes the
    // width of its widest label (widgets.h): the row must not reflow under
    // the mouse when a count changes.
    int folder_count = (int)app.settings.chartfolders.size();
    char manage_label[64];
    std::snprintf(manage_label, sizeof(manage_label), "Manage folders... (%d)", folder_count);
    std::string manage_widest =
        "Manage folders... (" + widest_digits(digit_count(folder_count)) + ")";
    if (button_in_slot(manage_label, button_slot_width(manage_widest.c_str())))
        ImGui::OpenPopup("Song folders");
    hint("Add or remove the folders Hydra scans for charts");
    ImGui::SameLine();

    bool can_scan = !app.settings.chartfolders.empty();
    begin_disabled_button(!can_scan);
    if (ImGui::Button("Scan library")) {
        app.start_scan();
        ImGui::OpenPopup("Scanning charts");
    }
    end_disabled_button(!can_scan);
    ImGui::SameLine();

    bool searching = !app.search.empty();
    char label[96];
    if (searching)
        std::snprintf(label, sizeof(label), "Analyze search (%lld)",
                      (long long)static_cast<int64_t>(app.library_match_count()));
    else
        std::snprintf(label, sizeof(label), "Analyze library");
    // Nothing to analyze -> disabled, like "Scan library" with no folders
    // (running a batch over 0 charts just failed the report afterwards).
    int64_t analyzable =
        searching ? static_cast<int64_t>(app.library_match_count()) : app.library_total;
    // A search count can never exceed the library, so this slot fits both labels.
    std::string analyze_widest =
        "Analyze search (" + widest_digits(digit_count(app.library_total)) + ")";
    const float analyze_w =
        std::max(button_slot_width("Analyze library"), button_slot_width(analyze_widest.c_str()));
    // Also off while hydra_rules.ini is bad: no analysis on rules the user
    // did not choose.
    const bool analyze_off = analyzable == 0 || app.analysis_blocked();
    begin_disabled_button(analyze_off);
    if (button_in_slot(label, analyze_w)) {
        // Confirm before starting: a library batch can be hours of all-core
        // CPU, which shouldn't fire irrevocably from one click.
        app.batch_confirm_pending = true;
        ImGui::OpenPopup("Analyzing");
    }
    end_disabled_button(analyze_off);
    ImGui::SameLine();
    ImGui::Checkbox("redo existing", &app.batch_redo);
    hint("Also re-analyze charts that already have a stored result");

    ImGui::SameLine();
    if (ImGui::Button("Compare dmleaderboards user...")) {
        // The leaderboard plays by Clone Hero's rules, so the comparison only
        // means anything against 4-bar records.
        if (app.settings.sp_cap != kCloneHeroSpCap) {
            app.set_status("Leaderboard comparison needs SP cap 4 (Clone Hero's rule). "
                           "Set it in a song's details.");
        } else if (app.settings.difficulty() != Difficulty::Expert) {
            // The ladder only carries Expert scores, so a Hard/Medium/Easy
            // library has nothing to compare against.
            app.set_status("Leaderboard comparison needs Expert difficulty. "
                           "Switch the View difficulty back to Expert.");
        } else {
            // Fresh picker: drop any finished report from a previous run, and
            // only refetch the ladder if we don't already have it this session
            // (the render.com backend cold-starts, so a cached list saves a
            // long wait).
            app.dm_report_job.reset();
            app.dm_picker_open = true;
            if (app.dm_users.empty()) app.start_dm_fetch();
            ImGui::OpenPopup("Compare dmleaderboards user");
        }
    }
    hint("Compare a dmleaderboards.com player's scores against your library");

    // A way back into the last batch's HTML report. While a report job is
    // still building, the slot shows a greyed "Building path report..." button.
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

    render_status_line(app, /*same_line=*/true);
    // A bad rules file is not a passing message: it stays on screen, under
    // the action row, for as long as analysis is off.
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
