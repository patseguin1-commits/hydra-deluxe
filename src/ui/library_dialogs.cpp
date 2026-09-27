#include "ui/library_parts.h"

#include "app/report_files.h"
#include "core/model.h"
#include "core/strutil.h"
#include "imgui.h"
#include "store/record_store.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/win32_dialogs.h"

#include <algorithm>
#include <cstdint>
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

namespace {

// The strips' colours. Each text colour was measured on its strip:
// (250,250,250) is 11.97:1 on the running strip, 12.94:1 on the done strip
// and 13.73:1 on the problem strip; the secondary lines are 9.47:1, 9.27:1
// and 9.38:1; orange on the problem strip is 5.65:1; the teal bar on its
// track is 6.4:1.
const ImVec4 kStripBg{22 / 255.0f, 57 / 255.0f, 58 / 255.0f, 1.0f};
const ImVec4 kStripText2{200 / 255.0f, 230 / 255.0f, 230 / 255.0f, 1.0f};
const ImVec4 kStripTrack{11 / 255.0f, 35 / 255.0f, 36 / 255.0f, 1.0f};
const ImVec4 kDoneBg{31 / 255.0f, 51 / 255.0f, 34 / 255.0f, 1.0f};
const ImVec4 kDoneText2{196 / 255.0f, 220 / 255.0f, 199 / 255.0f, 1.0f};
const ImVec4 kProblemBg{58 / 255.0f, 38 / 255.0f, 18 / 255.0f, 1.0f};
const ImVec4 kProblemText2{230 / 255.0f, 205 / 255.0f, 180 / 255.0f, 1.0f};

// Enter or keypad Enter this frame.
bool enter_pressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
           ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
}

// "21 analyzed · 0 failed · 1 skipped (already had a result)".
std::string batch_counts(const BatchJob::Snapshot& s) {
    return count_label(s.completed - s.failed, "analyzed", "analyzed") + " \xC2\xB7 " +
           count_label(s.failed, "failed", "failed") + " \xC2\xB7 " +
           count_label(s.skipped, "skipped", "skipped") + " (already had a result)";
}

}  // namespace

std::string count_label(int64_t n, const char* one, const char* many) {
    return group_thousands(n) + " " + (n == 1 ? one : many);
}

std::string format_duration(double seconds) {
    const long long total = seconds <= 0.0 ? 0 : static_cast<long long>(seconds + 0.5);
    const long long h = total / 3600, m = (total / 60) % 60, s = total % 60;
    char buf[32];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", h, m, s);
    else
        std::snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    return buf;
}

BatchSettingsSummary batch_settings_summary(const app::Settings& s) {
    BatchSettingsSummary out;
    out.difficulty = difficulty_name(s.difficulty());
    if (s.view_prodrums) out.difficulty += " \xC2\xB7 Pro Drums";
    if (s.effective_bass2x()) out.difficulty += " \xC2\xB7 2x Bass";
    out.sp_cap = count_label(s.sp_cap, "bar", "bars") +
                 (s.sp_cap == kCloneHeroSpCap ? " (Clone Hero's rule)" : " (a what-if)");
    out.score_range = s.depth_mode == 0 ? count_label(s.depth_value, "score", "scores")
                                        : count_label(s.depth_value, "point", "points");
    out.path_limit = s.mslimit_enabled ? std::to_string(s.mslimit_value) + " ms" : "off";
    return out;
}

const char* empty_library_message(const app::Settings& s) {
    return s.chartfolders.empty()
               ? "No song folders yet. Click \"Manage folders...\" to add the folder your "
                 "charts are in, then \"Scan library\"."
               : "No charts found yet. Click \"Scan library\" to read your song folders.";
}

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
                app.library_ui.folders_changed = true;
            }
        } else if (dialog_failed) {
            app.set_problem("The folder picker could not be opened.");
        }
    }
    ImGui::SameLine();
    const bool nested_open = ImGui::IsPopupOpen("Remove folder?");
    bool close = ImGui::Button("Close") ||
                 (!nested_open && ImGui::IsKeyPressed(ImGuiKey_Escape, false));
    // A changed list is only real after a scan: offer it right here.
    if (app.library_ui.folders_changed && !app.settings.chartfolders.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Scan now")) {
            app.request_scan = true;  // the main window starts it next frame
            close = true;
        }
    }
    if (close) {
        app.library_ui.folders_changed = false;
        ImGui::CloseCurrentPopup();
    }
    render_status_line(app);

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
            if (ImGui::Button("Remove") || enter_pressed()) {
                app.settings.chartfolders.erase(app.settings.chartfolders.begin() +
                                                (long)*confirm_remove);
                app.settings.is_rescan = false;
                app.commit_settings();
                app.library_ui.folders_changed = true;
                confirm_remove.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
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
            if (ImGui::Button("Continue") || enter_pressed() ||
                ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                app.scan_job.reset();
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::Text("Done: %s found.", count_label(p.charts_found, "chart", "charts").c_str());
            if (!p.errors.empty()) {
                // "problems", not "skipped items": the list can also carry
                // a failed library write, which is not a skipped chart.
                ImGui::TextColored(kWarningColor, "%s during the scan:",
                                   count_label((int64_t)p.errors.size(), "problem", "problems")
                                       .c_str());
                ImGui::BeginChild("scanerrors", ImVec2(-1, px(100)), ImGuiChildFlags_Borders);
                for (const std::string& e : p.errors) ImGui::TextUnformatted(e.c_str());
                ImGui::EndChild();
            }
            if (ImGui::Button("Continue") || enter_pressed() ||
                ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                app.settings.is_rescan = true;
                app.commit_settings();
                app.table_viewpage = 0;
                app.refresh_page();
                app.scan_job.reset();
                ImGui::CloseCurrentPopup();
            }
        }
    } else {
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            app.scan_job->cancel();
    }

    ImGui::EndPopup();
}

// The one question before a batch: how many charts, every setting it will
// run with, and whether to redo charts that already have a result. Esc and
// the title-bar X cancel; Enter starts (unless Cancel has keyboard focus).
void render_batch_confirm(AppState& app) {
    if (!app.batch_confirm_pending) return;
    if (!ImGui::IsPopupOpen("Analyze library")) ImGui::OpenPopup("Analyze library");
    pin_next_modal_width(px(520.0f));
    bool open = true;
    if (!ImGui::BeginPopupModal("Analyze library", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        app.batch_confirm_pending = false;  // closed without our buttons
        app.batch_scope.clear();
        return;
    }

    const int64_t total = static_cast<int64_t>(app.batch_scope.size());
    const int64_t with = app.batch_scope_with_result;
    const int64_t without = total - with;
    const int64_t to_run = app.batch_redo ? total : without;
    std::string question;
    if (to_run == 0)
        question = "Every chart here already has a result.";
    else if (app.batch_redo && with > 0)
        question = "Analyze " + count_label(total, "chart", "charts") + ", re-analyzing " +
                   group_thousands(with) + (with == 1 ? " that already has" : " that already have") +
                   " a result?";
    else
        question = "Analyze " + count_label(without, "chart", "charts") +
                   (without == 1 ? " that has" : " that have") + " no result yet?";
    ImGui::PushFont(nullptr, 20.0f);
    ImGui::TextWrapped("%s", question.c_str());
    ImGui::PopFont();

    const BatchSettingsSummary d = batch_settings_summary(app.settings);
    if (ImGui::BeginTable("##batchsettings", 2,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuter)) {
        auto row = [](const char* key, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", key);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
        };
        row("Difficulty", d.difficulty);
        row("SP cap", d.sp_cap);
        row("Score range", d.score_range);
        row("Path limit", d.path_limit);
        ImGui::EndTable();
    }
    ImGui::TextDisabled("To change these, cancel and edit Analysis settings on the main screen.");

    begin_disabled_checkbox(with == 0);
    ImGui::Checkbox("Also re-analyze charts that already have a result##redo", &app.batch_redo);
    end_disabled_checkbox(with == 0);
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", count_label(with, "chart", "charts").c_str());

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("It runs in the background, so you can keep browsing. Analysis "
                           "settings stay locked until it finishes or you stop it. Stop keeps "
                           "every result finished so far.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const bool can_start = to_run > 0 && !app.analysis_blocked();
    const float start_w = button_slot_width("Start analyzing");
    const float cancel_w = button_slot_width("Cancel");
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - start_w - cancel_w -
                         ImGui::GetStyle().ItemSpacing.x);
    bool cancel = !open || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (ImGui::Button("Cancel")) cancel = true;
    const bool cancel_focused = ImGui::IsItemFocused();
    ImGui::SameLine();
    begin_disabled_button(!can_start);
    bool start = ImGui::Button("Start analyzing");
    end_disabled_button(!can_start);
    ImGui::SetItemDefaultFocus();
    if (can_start && !cancel_focused && enter_pressed()) start = true;

    if (start && can_start) {
        app.start_batch(app.batch_redo);  // clears batch_confirm_pending
        ImGui::CloseCurrentPopup();
    } else if (cancel) {
        app.batch_confirm_pending = false;
        app.batch_scope.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// The running batch, in a strip under the toolbar: what it's doing, how far,
// how long, and Pause / Stop. The buttons sit at fixed x so live numbers
// never walk them around.
void render_batch_strip(AppState& app) {
    if (!app.batch_job) return;
    const BatchJob::Snapshot s = app.batch_job->snapshot();
    if (s.finished) return;
    const bool stopping = app.batch_job->is_cancelled();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kStripBg);
    ImGui::BeginChild("##batchstrip", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float pause_w = std::max(button_slot_width("Pause"), button_slot_width("Resume"));
    const float stop_w = button_slot_width("Stop");
    const float buttons_x = ImGui::GetContentRegionMax().x - pause_w - stop_w - style.ItemSpacing.x;
    const float text_w = buttons_x - ImGui::GetCursorPosX() - style.ItemSpacing.x;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##striptext", ImVec2(text_w, 0.0f), ImGuiChildFlags_AutoResizeY);
    ImGui::PopStyleColor();
    if (s.preparing) {
        ImGui::TextUnformatted("Preparing the chart list...");
    } else {
        std::string line = stopping ? "Stopping..." : s.paused ? "Paused" : "Analyzing...";
        line += "  " + group_thousands(s.completed) + " of " + group_thousands(s.total);
        if (!s.current_title.empty() && !s.paused) {
            line += "  \xC2\xB7  Now: " + s.current_title;
            if (!s.current_artist.empty()) line += " \xC2\xB7 " + s.current_artist;
        }
        text_ellipsized(line.c_str());

        const float frac = s.total > 0 ? (float)s.completed / (float)s.total : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kStripTrack);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccentColor);
        ImGui::ProgressBar(frac, ImVec2(-1.0f, px(6.0f)), "");
        ImGui::PopStyleColor(2);

        std::string time = format_duration(s.elapsed_s) + " elapsed";
        if (s.eta_s) time += " \xC2\xB7 about " + format_duration(*s.eta_s) + " left";
        ImGui::PushStyleColor(ImGuiCol_Text, kStripText2);
        ImGui::TextUnformatted(batch_counts(s).c_str());
        const float time_w = ImGui::CalcTextSize(time.c_str()).x;
        ImGui::SameLine();
        const float right = ImGui::GetContentRegionMax().x - time_w;
        if (right > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right);
        ImGui::TextUnformatted(time.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::SetCursorPosX(buttons_x);
    const bool pause_off = stopping || s.preparing;
    begin_disabled_button(pause_off);
    if (button_in_slot(s.paused ? "Resume" : "Pause", pause_w)) {
        if (s.paused) app.batch_job->resume();
        else app.batch_job->pause();
    }
    end_disabled_button(pause_off);
    ImGui::SameLine();
    begin_disabled_button(stopping);
    if (button_in_slot("Stop", stop_w)) app.batch_job->stop();
    end_disabled_button(stopping);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        const int kept = s.completed - s.failed;
        ImGui::SetTooltip("Keeps the %s already finished",
                          count_label(kept, "result", "results").c_str());
    }
    ImGui::EndChild();
}

// The finished batch: the counts, how long it took, where the report went,
// and what to do with it. It stays until its X is clicked.
void render_batch_done(AppState& app) {
    if (!app.batch_job) return;
    const BatchJob::Snapshot s = app.batch_job->snapshot();
    if (!s.finished) return;
    const bool stopped = app.batch_job->is_cancelled();
    ReportJob* report = app.report_job.get();
    const bool building = report && !report->finished();
    const bool report_ok = report && report->finished() && report->ok();
    const bool report_failed = report && report->finished() && !report->ok() &&
                               !report->is_cancelled();
    const bool open_failed = report_ok && !report->open_problem().empty();
    if (report && report->finished()) app.report_outcome_shown = true;

    const bool problem = report_failed || open_failed;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, problem ? kProblemBg : kDoneBg);
    ImGui::BeginChild("##batchdone", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    const ImVec4 text2 = problem ? kProblemText2 : kDoneText2;

    // Buttons at the right, placed first so the text can take the rest.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float close_w = ImGui::GetFrameHeight();
    const float open_w = button_slot_width("Open report");
    const float folder_w = button_slot_width("Show in folder");
    const float buttons_w = (report_ok ? open_w + folder_w + style.ItemSpacing.x * 2 : 0.0f) + close_w;
    const float left_x = ImGui::GetCursorPosX();
    const float top_y = ImGui::GetCursorPosY();
    const float text_w = ImGui::GetContentRegionAvail().x - buttons_w - style.ItemSpacing.x;
    ImGui::SetCursorPosX(left_x + text_w + style.ItemSpacing.x);
    if (report_ok) {
        const std::string path = report->saved_path().u8string();
        if (button_in_slot("Open report", open_w) &&
            !app::open_in_browser(report->saved_path().wstring()))
            app.set_problem("Windows couldn't open the report in your browser. Open " + path +
                            " directly.");
        ImGui::SameLine();
        if (button_in_slot("Show in folder", folder_w) && !show_in_folder(report->saved_path()))
            app.set_problem("Windows couldn't open the folder that holds " + path + ".");
        ImGui::SameLine();
    }
    if (ImGui::Button("X##dismissdone", ImVec2(close_w, close_w))) {
        app.batch_job.reset();  // update_background_jobs reaps the report after this
        ImGui::EndChild();
        return;
    }
    hint("Dismiss");

    ImGui::SetCursorPos(ImVec2(left_x, top_y));
    ImGui::PushTextWrapPos(left_x + text_w);
    ImGui::TextWrapped("%s: %s \xC2\xB7 took %s", stopped ? "Stopped" : "Finished",
                       batch_counts(s).c_str(), format_duration(s.elapsed_s).c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, text2);
    if (stopped) {
        ImGui::TextWrapped("Every result finished before Stop is kept.");
    } else if (building) {
        ImGui::TextWrapped("Building the path report...");
    } else if (report_failed) {
        ImGui::TextColored(kWarningColor, "The path report could not be built.");
        ImGui::TextWrapped("%s", report->message().c_str());
        ImGui::TextDisabled("%s", report->error().c_str());
    } else if (open_failed) {
        ImGui::PopStyleColor();
        ImGui::TextColored(kWarningColor,
                           "Report saved, but Windows couldn't open it in your browser.");
        ImGui::PushStyleColor(ImGuiCol_Text, text2);
        ImGui::TextWrapped("Open %s directly, or try Open report again.",
                           report->saved_path().u8string().c_str());
    } else if (report_ok) {
        ImGui::TextWrapped("Path report saved to %s", report->saved_path().u8string().c_str());
    }
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();

    if (report_ok) {
        if (ImGui::Checkbox("Open automatically", &app.settings.auto_open_report))
            app.commit_settings();
        hint("Open the report in the browser whenever a batch finishes");
    }
    if (!s.failures.empty()) {
        const std::string head = count_label((int64_t)s.failures.size(), "chart", "charts") +
                                 " failed##batchfailures";
        if (ImGui::TreeNode(head.c_str())) {
            for (size_t i = 0; i < s.failures.size(); ++i) {
                ImGui::TextUnformatted(s.failures[i].c_str());
                if (i < s.failure_details.size())
                    ImGui::TextDisabled("%s", s.failure_details[i].c_str());
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
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
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    // A cancelled report goes back to the list, quietly: the user asked.
    if (app.dm_report_job && app.dm_report_job->finished() && app.dm_report_job->is_cancelled())
        app.dm_report_job.reset();

    if (app.dm_report_job) {
        DmReportJob& job = *app.dm_report_job;
        if (!job.finished()) {
            ImGui::TextUnformatted("Fetching scores and building the report...");
            ImGui::TextDisabled("The leaderboard server can take a moment to wake up.");
            if (ImGui::Button("Cancel") || escape) app.cancel_dm_report();  // never joins
        } else if (!job.ok()) {
            ImGui::TextColored(kWarningColor, "Could not build the report.");
            ImGui::TextWrapped("%s", job.message().c_str());
            ImGui::TextDisabled("%s", job.error().c_str());
            ImGui::Spacing();
            if (ImGui::Button("Back to list") || escape) app.dm_report_job.reset();
        } else {
            const auto& st = job.stats();
            ImGui::TextWrapped("Done: %s matched, %s above optimal, %s not analyzed, %s not in "
                               "your library.",
                               group_thousands(st.matched).c_str(),
                               group_thousands(st.above_optimal).c_str(),
                               group_thousands(st.not_analyzed).c_str(),
                               group_thousands(st.not_in_library).c_str());
            if (job.opened())
                ImGui::TextDisabled("The report opened in your browser.");
            else if (!job.open_problem().empty())
                ImGui::TextColored(kWarningColor,
                                   "Report saved, but Windows couldn't open it in your browser.");
            else
                ImGui::TextDisabled("The report is ready.");
            ImGui::Spacing();
            // "again" only once it really opened.
            const bool opened = job.opened() || app.library_ui.dm_opened_by_click;
            if (ImGui::Button(opened ? "Open report again" : "Open report")) {
                if (app::open_in_browser(job.saved_path().wstring()))
                    app.library_ui.dm_opened_by_click = true;
                else
                    app.set_problem("Windows couldn't open the report in your browser. Open " +
                                    job.saved_path().u8string() + " directly.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Compare another")) app.dm_report_job.reset();
            ImGui::SameLine();
            if (ImGui::Button("Close") || escape) {
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
        if (ImGui::Button("Cancel") || escape) {
            app.cancel_dm_fetch();  // parked, never joined here
            app.dm_picker_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }
    if (app.dm_fetch_job && app.dm_fetch_job->finished()) {
        if (!app.dm_fetch_job->ok()) {
            ImGui::TextColored(kWarningColor, "Could not load the user list:");
            ImGui::TextWrapped("%s", app.dm_fetch_job->message().c_str());
            ImGui::TextDisabled("%s", app.dm_fetch_job->error().c_str());
            ImGui::Spacing();
            if (ImGui::Button("Retry")) app.start_dm_fetch();
            ImGui::SameLine();
            if (ImGui::Button("Close") || escape) {
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

    if (ImGui::Button("Close") || (escape && !ImGui::GetIO().WantTextInput)) {
        app.dm_picker_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace detail

}  // namespace hydra::ui
