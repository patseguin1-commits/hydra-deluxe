#include "ui/library_view.h"

#include "imgui.h"
#include "store/record_store.h"
#include "ui/fonts.h"
#include "ui/generation.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/win32_dialogs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace hydra::ui {

namespace {

HWND main_hwnd() {
    return static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
}

// Transient feedback for actions that used to fail silently (duplicate
// folder, unwritable INI, folder picker not opening). Fades out a few
// seconds after the message changes. same_line appends it to the current
// row (the main action bar); the folder manager renders it on its own line.
void render_status_line(AppState& app, bool same_line) {
    // seen starts at 0 (the first AppState's initial counter value), not the
    // watcher's default -1: app startup must not count as a change and start
    // a fade. A later AppState in the same process (UI test runner) starts
    // higher and does register once, which the empty-message check below
    // turns into a no-op.
    static GenerationWatcher generation{/*seen=*/0};
    static double shown_at = -1.0;
    if (generation.changed(app.status_generation)) shown_at = ImGui::GetTime();
    if (shown_at < 0.0 || app.status_message.empty()) return;
    if (ImGui::GetTime() - shown_at > 6.0) return;
    if (same_line) ImGui::SameLine();
    ImGui::TextColored(kWarningColor, "%s", app.status_message.c_str());
}

// The "Song folders" modal: the folder list with add/remove, opened from the
// "Manage folders..." button. Folder management moved off the main screen so
// the library table owns it — setup is a once-in-a-while task, but its list
// used to sit above the table on every launch.
void render_folder_manager(AppState& app) {
    // Folder pending removal, confirmed through the nested popup below —
    // deleting a library source was previously a single un-undoable click.
    static std::optional<size_t> confirm_remove;

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

// The single row of library-wide actions at the top of the main screen.
void render_actions_row(AppState& app) {
    char manage_label[64];
    std::snprintf(manage_label, sizeof(manage_label), "Manage folders... (%d)",
                  (int)app.settings.chartfolders.size());
    if (ImGui::Button(manage_label)) ImGui::OpenPopup("Song folders");
    hint("Add or remove the folders Hydra scans for charts");
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
    // Nothing to analyze -> disabled, like "Scan charts" with no folders
    // (running a batch over 0 charts just failed the report afterwards).
    int64_t analyzable = searching ? app.current_page.total_count : app.library_total;
    begin_disabled_button(analyzable == 0);
    if (ImGui::Button(label)) {
        // Confirm before starting: a library batch can be hours of all-core
        // CPU, which shouldn't fire irrevocably from one click.
        app.batch_confirm_pending = true;
        ImGui::OpenPopup("Analyzing");
    }
    end_disabled_button(analyzable == 0);
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

    render_status_line(app, /*same_line=*/true);
    render_folder_manager(app);
}

void render_view_controls(AppState& app) {
    ImGui::TextUnformatted("View:");
    ImGui::SameLine();
    // Only "Expert" exists end-to-end today. A plain label says so honestly;
    // the permanently-disabled combo this replaces rendered exactly like a
    // working dropdown (DisabledAlpha is 1.0, so a bare BeginDisabled has no
    // visual effect) and silently swallowed clicks.
    ImGui::TextDisabled("Expert");
    hint("Only Expert difficulty is supported right now.");

    ImGui::SameLine();
    if (ImGui::Checkbox("Pro Drums", &app.settings.view_prodrums)) app.commit_settings();
    ImGui::SameLine();
    if (ImGui::Checkbox("2x Bass", &app.settings.view_bass2x)) app.commit_settings();
}

void render_search_box(AppState& app) {
    static char buf[256] = "";
    static bool synced = false;
    static double edited_at = -1.0;
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
    if (!s.current_title.empty()) ImGui::TextUnformatted(s.current_title.c_str());

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
                                                         app.settings.auto_open_report,
                                                         app.settings.hit_window_ms);
            app.report_job->start();
        }
        if (app.report_job && !app.report_job->finished()) {
            ImGui::TextDisabled("Building path report...");
        } else if (app.report_job && !app.report_job->ok()) {
            ImGui::TextColored(kWarningColor, "Path report failed: %s",
                               app.report_job->error().c_str());
        } else if (app.report_job) {
            if (ImGui::Button("Open path report")) open_report_in_browser();
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
            if (ImGui::Button("Open report again")) open_dm_report_in_browser();
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
    static char filter[128] = "";
    ImGui::SetNextItemWidth(px(380));
    ImGui::InputTextWithHint("##dmfilter", "filter by name", filter, sizeof(filter));

    std::string needle = filter;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    ImGui::BeginChild("dmusers", ImVec2(px(480), px(320)), ImGuiChildFlags_Borders);
    if (app.dm_users.empty()) {
        ImGui::TextDisabled("(No users found.)");
    } else {
        for (const net::DmUser& u : app.dm_users) {
            if (!needle.empty()) {
                std::string name = u.username;
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return (char)std::tolower(c); });
                if (name.find(needle) == std::string::npos) continue;
            }
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

    // A finished report job has nothing left to show once the batch modal is
    // gone (its status lines live there); reclaim the thread.
    if (!app.batch_job && app.report_job && app.report_job->finished())
        app.report_job.reset();

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

    render_actions_row(app);

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
    render_view_controls(app);
    render_search_box(app);

    // A way back into the last batch's HTML report (it used to exist only as
    // an unrequested browser launch right after a batch).
    if (report_file_exists()) {
        ImGui::SameLine();
        if (ImGui::Button("Open path report") && !open_report_in_browser())
            app.set_status("The path report could not be opened.");
    }
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
    } else if (app.library_total > 0) {
        // The library has charts; the search just matched none of them —
        // the "no songs scanned" onboarding text here was misleading.
        ImGui::TextUnformatted("No charts match your search.");
    } else {
        // Copy names the actual controls -- it used to say "Set a folder",
        // which doesn't exist.
        ImGui::TextUnformatted(
            "No songs scanned. Click \"Manage folders...\" to add your song folder, "
            "then \"Scan charts\" to get started!");
    }

    if (app.scan_job) render_scan_modal(app);
    if (app.batch_job || app.batch_confirm_pending) render_batch_modal(app);
    if (app.dm_picker_open) render_dm_picker_modal(app);

    ImGui::End();
}

}  // namespace hydra::ui
