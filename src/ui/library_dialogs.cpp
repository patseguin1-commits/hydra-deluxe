#include "ui/library_parts.h"

#include "app/report_files.h"
#include "core/strutil.h"
#include "imgui.h"
#include "store/record_store.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/win32_dialogs.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

namespace hydra::ui {

namespace {

HWND main_hwnd() {
    return static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
}

}  // namespace

namespace detail {

// The "Song folders" modal: the folder list with add/remove, opened from the
// "Manage folders..." button. Folder management moved off the main screen so
// the library table owns it — setup is a once-in-a-while task, but its list
// used to sit above the table on every launch.
void render_folder_manager(AppState& app) {
    // Folder pending removal, confirmed through the nested popup below —
    // deleting a library source was previously a single un-undoable click.
    std::optional<size_t>& confirm_remove = app.library_ui.confirm_remove;

    if (!ImGui::BeginPopupModal("Song folders", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Hydra scans these folders (and their subfolders) for charts:");
    float row_h = ImGui::GetTextLineHeightWithSpacing();
    float list_rows =
        std::clamp((float)app.settings.chartfolders.size(), 1.0f, 12.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kFolderListBg);
    ImGui::BeginChild("songfolders", ImVec2(px(620), row_h * list_rows + px(8)),
                      ImGuiChildFlags_Borders);
    if (app.settings.chartfolders.empty()) {
        ImGui::TextDisabled("(None.)");
    } else {
        for (size_t i = 0; i < app.settings.chartfolders.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Button, kDeleteButtonColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kDeleteButtonHoveredColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kDeleteButtonActiveColor);
            if (ImGui::Button("X")) confirm_remove = i;
            ImGui::PopStyleColor(3);
            hint("Remove this folder from the library");
            ImGui::SameLine();
            text_ellipsized(app.settings.chartfolders[i].c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (ImGui::Button("Add folder...")) {
        bool dialog_failed = false;
        if (auto folder = browse_for_folder(main_hwnd(), &dialog_failed)) {
            bool already = false;
            for (const std::string& f : app.settings.chartfolders)
                if (f == *folder) already = true;
            if (already) {
                app.set_status("That folder is already in the list.");
            } else {
                app.settings.chartfolders.push_back(*folder);
                app.settings.is_rescan = false;
                app.commit_settings();
            }
        } else if (dialog_failed) {
            app.set_status("The folder picker could not be opened.");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    render_status_line(app, /*same_line=*/false);

    // Nested confirm, stacked on top of this modal.
    if (confirm_remove && !ImGui::IsPopupOpen("Remove folder?"))
        ImGui::OpenPopup("Remove folder?");
    if (ImGui::BeginPopupModal("Remove folder?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!confirm_remove || *confirm_remove >= app.settings.chartfolders.size()) {
            confirm_remove.reset();
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextUnformatted("Remove this folder from the library?");
            ImGui::TextDisabled("%s", app.settings.chartfolders[*confirm_remove].c_str());
            ImGui::TextUnformatted("Its charts disappear from the list on the next scan.");
            ImGui::Spacing();
            if (ImGui::Button("Remove")) {
                app.settings.chartfolders.erase(app.settings.chartfolders.begin() +
                                                (long)*confirm_remove);
                app.settings.is_rescan = false;
                app.commit_settings();
                confirm_remove.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                confirm_remove.reset();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::EndPopup();
}

void render_scan_modal(AppState& app) {
    pin_next_modal_width(px(460.0f));  // live counts must not resize it
    if (!ImGui::BeginPopupModal("Scanning charts", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ScanProgress p = app.scan_job->snapshot();
    if (p.phase == ScanProgress::Phase::Enumerating) {
        ImGui::Text("Discovering folders... (%d found)", p.folders_seen);
    } else {
        ImGui::Text("Discovering folders... (%d found)", p.folders_seen);
        ImGui::TextUnformatted("Reading charts...");
        progress_bar_counted(p.charts_done, p.charts_total);
        if (p.charts_cached > 0)
            ImGui::Text("%d unchanged since last scan, reused.", p.charts_cached);
    }

    if (p.phase == ScanProgress::Phase::Writing)
        ImGui::TextUnformatted("Writing library...");

    if (p.phase == ScanProgress::Phase::Done) {
        if (p.cancelled) {
            ImGui::TextUnformatted("Scan cancelled. The library was left unchanged.");
            if (ImGui::Button("Continue")) {
                app.scan_job.reset();
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::Text("Done! %d chart(s) found.", p.charts_found);
            if (!p.errors.empty()) {
                // "problem(s)", not "skipped item(s)": the list can also carry
                // a failed library write, which is not a skipped chart.
                ImGui::TextColored(kWarningColor, "%d problem(s) during the scan:",
                                   (int)p.errors.size());
                ImGui::BeginChild("scanerrors", ImVec2(-1, px(100)), ImGuiChildFlags_Borders);
                for (const std::string& e : p.errors) ImGui::TextUnformatted(e.c_str());
                ImGui::EndChild();
            }
            if (ImGui::Button("Continue")) {
                app.settings.is_rescan = true;
                app.commit_settings();
                app.table_viewpage = 0;
                app.refresh_page();
                app.scan_job.reset();
                ImGui::CloseCurrentPopup();
            }
        }
    } else {
        if (ImGui::Button("Cancel")) app.scan_job->cancel();
    }

    ImGui::EndPopup();
}

void render_batch_modal(AppState& app) {
    // The title and counts change every chart; the modal keeps one width and
    // the title line is always there, so the Cancel button never moves.
    pin_next_modal_width(px(520.0f));
    if (!ImGui::BeginPopupModal("Analyzing", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // The popup can close without our buttons running; don't leave the
        // confirm stage armed with no modal on screen.
        app.batch_confirm_pending = false;
        return;
    }

    // Confirm stage: no work has started yet. A library batch can be hours of
    // all-core CPU; say what's about to happen and let the user back out.
    if (!app.batch_job) {
        bool searching = !app.search.empty();
        int64_t count = searching ? app.current_page.total_count : app.library_total;
        ImGui::Text("Analyze %lld chart%s as \"%s\"?", (long long)count,
                    count == 1 ? "" : "s", app.settings.chartmode_key().c_str());
        ImGui::TextDisabled(app.batch_redo
                                ? "Charts with a stored result will be re-analyzed."
                                : "Charts that already have a result will be skipped.");
        ImGui::TextDisabled("This can take a while on a large library. It can be "
                            "cancelled at any time.");
        ImGui::Spacing();
        if (ImGui::Button("Start")) {
            app.batch_confirm_pending = false;
            app.start_batch(app.batch_redo);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            app.batch_confirm_pending = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    BatchJob::Snapshot s = app.batch_job->snapshot();

    if (s.preparing) {
        ImGui::TextUnformatted("Preparing chart list...");
        if (ImGui::Button("Cancel")) app.batch_job->cancel();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text(app.batch_job->is_cancelled() ? "Cancelling..."
                                              : s.finished ? "Finished." : "Analyzing...");
    if (!s.current_title.empty())
        text_ellipsized(s.current_title.c_str());
    else
        ImGui::TextUnformatted(" ");  // keep the line so the layout below holds still

    progress_bar_counted(s.completed, s.total);
    ImGui::Text("%d analyzed, %d already stored, %d failed.", s.completed - s.failed, s.skipped,
               s.failed);

    if (s.finished) {
        // One path report per run, built when the batch lands — skipped for a
        // cancelled run. It only auto-opens the browser if the user opted in;
        // otherwise the "Open path report" button below is the way in.
        if (!app.report_started && !app.batch_job->is_cancelled()) {
            app.report_started = true;
            app.report_job = std::make_unique<ReportJob>(*app.store, app.settings.cap_query(),
                                                         app.settings.lens(),
                                                         app.settings.auto_open_report,
                                                         app.settings.hit_window_ms);
            app.report_job->start();
        }
        if (app.report_job && !app.report_job->finished()) {
            ImGui::TextDisabled("Building path report...");
        } else if (app.report_job && !app.report_job->ok()) {
            app.report_outcome_shown = true;
            ImGui::TextColored(kWarningColor, "Path report failed: %s",
                               app.report_job->error().c_str());
        } else if (app.report_job) {
            app.report_outcome_shown = true;
            if (ImGui::Button("Open path report")) app::open_report_in_browser();
            ImGui::SameLine();
            if (ImGui::Checkbox("Open automatically", &app.settings.auto_open_report))
                app.commit_settings();
            hint("Open the report in the browser whenever a batch finishes");
        }

        if (!s.failures.empty()) {
            ImGui::TextColored(kWarningColor, "Failed:");
            ImGui::BeginChild("batchfailures", ImVec2(-1, px(120)), ImGuiChildFlags_Borders);
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

// The "Compare dmleaderboards user" modal: fetch the ladder, let the user pick
// a player (filter-as-you-type), then fetch that player's scores and build the
// HTML comparison. Both network steps can cold-start the render.com backend, so
// each shows a "still waking up" note and a Cancel that abandons the job.
void render_dm_picker_modal(AppState& app) {
    if (!ImGui::BeginPopupModal("Compare dmleaderboards user", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    // Stage 2: a report job is running or done — it owns the modal until the
    // user backs out of it.
    if (app.dm_report_job) {
        if (!app.dm_report_job->finished()) {
            ImGui::TextUnformatted("Fetching scores and building the report...");
            ImGui::TextDisabled("The leaderboard server can take a moment to wake up.");
            if (ImGui::Button("Cancel")) app.dm_report_job->cancel();
        } else if (!app.dm_report_job->ok()) {
            ImGui::TextColored(kWarningColor, "Could not build the report:");
            ImGui::TextWrapped("%s", app.dm_report_job->error().c_str());
            ImGui::Spacing();
            if (ImGui::Button("Back to list")) app.dm_report_job.reset();
        } else {
            ImGui::Text("Done — %d matched, %d above optimal, %d not in your library.",
                        app.dm_report_job->matched(), app.dm_report_job->above(),
                        app.dm_report_job->unmatched());
            ImGui::TextDisabled(app.settings.auto_open_report
                                    ? "The report opened in your browser."
                                    : "The report is ready.");
            ImGui::Spacing();
            if (ImGui::Button("Open report again")) app::open_dm_report_in_browser();
            ImGui::SameLine();
            if (ImGui::Button("Compare another")) app.dm_report_job.reset();
            ImGui::SameLine();
            if (ImGui::Button("Close")) {
                app.dm_report_job.reset();
                app.dm_picker_open = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
        return;
    }

    // Stage 0: still loading the user list.
    if (app.dm_fetch_job && !app.dm_fetch_job->finished()) {
        ImGui::TextUnformatted("Loading the dmleaderboards user list...");
        ImGui::TextDisabled("The leaderboard server can take a moment to wake up.");
        if (ImGui::Button("Cancel")) {
            app.dm_fetch_job.reset();  // destructor cancels + joins
            app.dm_picker_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }
    if (app.dm_fetch_job && app.dm_fetch_job->finished()) {
        if (!app.dm_fetch_job->ok()) {
            ImGui::TextColored(kWarningColor, "Could not load the user list:");
            ImGui::TextWrapped("%s", app.dm_fetch_job->error().c_str());
            ImGui::Spacing();
            if (ImGui::Button("Retry")) app.start_dm_fetch();
            ImGui::SameLine();
            if (ImGui::Button("Close")) {
                app.dm_fetch_job.reset();
                app.dm_picker_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }
        // Take ownership of the fetched list once, then let the job go.
        if (app.dm_users.empty() && !app.dm_fetch_job->users().empty())
            app.dm_users = std::move(app.dm_fetch_job->users());
        app.dm_fetch_job.reset();
    }

    // Stage 1: choose a player.
    ImGui::TextUnformatted("Pick a player to compare against your library:");
    char (&filter)[128] = app.library_ui.dm_filter;
    ImGui::SetNextItemWidth(px(380));
    ImGui::InputTextWithHint("##dmfilter", "filter by name", filter, sizeof(filter));

    const std::string needle = to_lower_ascii(filter);

    ImGui::BeginChild("dmusers", ImVec2(px(480), px(320)), ImGuiChildFlags_Borders);
    if (app.dm_users.empty()) {
        ImGui::TextDisabled("(No users found.)");
    } else {
        for (const net::DmUser& u : app.dm_users) {
            if (!needle.empty() &&
                to_lower_ascii(u.username).find(needle) == std::string::npos)
                continue;
            char label[256];
            if (u.elo)
                std::snprintf(label, sizeof(label), "%s  (%d scores, elo %d)###%s",
                              u.username.c_str(), u.total_scores, *u.elo, u.id.c_str());
            else
                std::snprintf(label, sizeof(label), "%s  (%d scores)###%s", u.username.c_str(),
                              u.total_scores, u.id.c_str());
            bool is_last = u.id == app.settings.dm_last_user;
            if (ImGui::Selectable(label, is_last)) app.start_dm_report(u.id, u.username);
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Close")) {
        app.dm_picker_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace detail

}  // namespace hydra::ui
