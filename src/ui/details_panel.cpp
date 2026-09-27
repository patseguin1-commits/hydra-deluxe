#include "ui/details_view.h"
#include "ui/details_parts.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/library_query.h"  // strip_rich_tags
#include "app/path_view.h"
#include "core/model.h"
#include "imgui.h"
#include "ui/fonts.h"
#include "ui/generation.h"
#include "ui/preview_controller.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

namespace hydra::ui {

namespace {

// Title, "artist · charted by charter", and previous / next / close at the
// right. Clone Hero rich-text tags (<color=...>) are stripped for display.
void render_panel_header(AppState& app) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float button = ImGui::GetFrameHeight();
    const float buttons_w = button * 3.0f + style.ItemSpacing.x * 2.0f;
    const float left_x = ImGui::GetCursorPosX();
    const float top_y = ImGui::GetCursorPosY();
    const float text_w = ImGui::GetContentRegionAvail().x - buttons_w - style.ItemSpacing.x;

    // The buttons first, at the right edge, so the title can take the rest.
    ImGui::SetCursorPosX(left_x + text_w + style.ItemSpacing.x);
    const bool can_prev = app.can_select_relative(-1);
    begin_disabled_button(!can_prev);
    if (ImGui::Button("<##prevsong", ImVec2(button, button))) app.select_relative(-1);
    end_disabled_button(!can_prev);
    hint("Previous song in the list");
    ImGui::SameLine();
    const bool can_next = app.can_select_relative(1);
    begin_disabled_button(!can_next);
    if (ImGui::Button(">##nextsong", ImVec2(button, button))) app.select_relative(1);
    end_disabled_button(!can_next);
    hint("Next song in the list");
    ImGui::SameLine();
    if (ImGui::Button("X##closepanel", ImVec2(button, button))) app.show_details = false;
    hint("Close (Esc)");

    ImGui::SetCursorPos(ImVec2(left_x, top_y));
    const store::ChartLibraryEntry& song = *app.selected;
    ImGui::PushFont(nullptr, 26.0f);
    text_ellipsized(app::strip_rich_tags(song.title).c_str(), text_w);
    ImGui::PopFont();
    std::string byline = app::strip_rich_tags(song.artist);
    const std::string charter = app::strip_rich_tags(song.charter);
    if (!charter.empty()) byline += " \xC2\xB7 charted by " + charter;
    ImGui::PushStyleColor(ImGuiCol_Text, kSubtleTextColor);
    text_ellipsized(byline.c_str(), text_w);
    ImGui::PopStyleColor();
}

// The optimal score and path in gold with one line of facts under it, and the
// analyze button at the right. Before a Ready record, the same place says why
// there is nothing to show. Stars and the hardest squeeze are the stored
// summary's (T7), never worked out again here.
void render_headline(AppState& app) {
    const store::RecordStatus status = app.viewed.status;
    const char* label = status == store::RecordStatus::NotAnalyzed ? "Analyze this song"
                                                                   : "Re-analyze";
    const float button_w = button_slot_width("Analyze this song");  // the wider label
    const float left_x = ImGui::GetCursorPosX();
    const float top_y = ImGui::GetCursorPosY();
    const float text_w =
        ImGui::GetContentRegionAvail().x - button_w - ImGui::GetStyle().ItemSpacing.x;

    ImGui::SetCursorPosX(left_x + text_w + ImGui::GetStyle().ItemSpacing.x);
    const bool file_ok = app.selected_file_ok(ImGui::GetTime());
    const bool busy = app.analyze_running();
    const bool off = app.analysis_blocked() || !file_ok || busy;
    begin_disabled_button(off);
    if (button_in_slot(label, button_w)) app.start_analyze();
    end_disabled_button(off);
    if (busy && !app.analyze_job_shown() &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Wait for %s to finish analyzing.",
                          app.analyze_job->song().title.c_str());

    ImGui::SetCursorPos(ImVec2(left_x, top_y));
    ImGui::BeginGroup();
    if (status == store::RecordStatus::NotAnalyzed) {
        ImGui::TextDisabled("Not analyzed yet.");
    } else if (status == store::RecordStatus::Stale) {
        ImGui::PushTextWrapPos(left_x + text_w);
        ImGui::TextColored(kWarningColor,
                           "Out of date: this result came from another Hydra version or "
                           "from different rules in hydra_rules.ini. Re-analyze to refresh it.");
        ImGui::PopTextWrapPos();
    } else if (app.viewed.record->paths.empty()) {
        ImGui::TextUnformatted("No paths found.");
    } else {
        const Path& best = app.viewed.record->best_path();
        ImGui::PushStyleColor(ImGuiCol_Text, kBestPathColor);
        ImGui::PushFont(nullptr, 40.0f);
        ImGui::TextUnformatted(group_thousands(best.totalscore()).c_str());
        ImGui::PopFont();
        ImGui::SameLine(0.0f, px(18.0f));
        ImGui::PushFont(g_mono_font, 24.0f);
        text_ellipsized(best.pathstring().c_str(), left_x + text_w - ImGui::GetCursorPosX());
        ImGui::PopFont();
        ImGui::PopStyleColor();

        const store::PathSummary& summary = app.viewed_summary;
        std::string facts = "Optimal path";
        if (summary.stars)
            facts += " \xC2\xB7 " + std::to_string(*summary.stars) +
                     (*summary.stars == 1 ? " star" : " stars");
        ImGui::PushStyleColor(ImGuiCol_Text, kSubtleTextColor);
        if (summary.hardest_ms) {
            facts += " \xC2\xB7 hardest squeeze";
            ImGui::TextUnformatted(facts.c_str());
            ImGui::SameLine();
            char ms[32];
            std::snprintf(ms, sizeof(ms), "%.1f ms", *summary.hardest_ms);
            ImGui::TextColored(kWarningColor, "%s", ms);
        } else {
            facts += " \xC2\xB7 no squeezes";
            ImGui::TextUnformatted(facts.c_str());
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
}

// The lines that say why analysis can't run, each with its remedy.
void render_panel_notices(AppState& app) {
    // The error is a warning line with a remedy, not the button's label.
    if (!app.selected_file_ok(ImGui::GetTime())) {
        ImGui::TextColored(kWarningColor, "Song file not found.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Rescan library")) {
            app.request_scan = true;   // the main window starts the scan
            app.show_details = false;  // tick() runs close_details() on the edge
        }
    }
    if (app.analysis_blocked())
        ImGui::TextColored(kWarningColor,
                           "Analysis is off until hydra_rules.ini is fixed and Hydra is "
                           "restarted.");
}

// Display only; AppState::tick() owns storing and reaping.
void render_analyze_progress(AppState& app) {
    AnalyzeJob* job = app.analyze_job.get();
    if (!job) return;

    ImGui::BeginChild("analyzeprogress", ImVec2(0, px(140)), ImGuiChildFlags_Borders);

    if (!job->finished()) {
        if (job->is_cancelled()) {
            ImGui::TextUnformatted("Cancelling...");
        } else {
            double t = ImGui::GetTime();
            int dots = (int)(t * 2) % 4;
            ImGui::Text("Analyzing chart%.*s", dots, "...");
            // A real bar once the search starts reporting; until the first tick
            // (parse + graph build) there's nothing to show, so leave it off.
            float f = job->progress();
            if (f >= 0.0f) {
                char overlay[16];
                std::snprintf(overlay, sizeof(overlay), "%.0f%%", f * 100.0f);
                ImGui::ProgressBar(f, ImVec2(-1.0f, 0.0f), overlay);
            }
            // A long chart can take a while; the user needs an out that isn't
            // killing the app.
            if (ImGui::Button("Cancel")) job->cancel();
        }
    } else if (!job->ok()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
        ImGui::TextWrapped("%s", job->message().c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", job->error().c_str());
        if (ImGui::Button("Continue")) app.analyze_job.reset();
    } else if (!app.details_ui.store_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
        ImGui::TextWrapped("%s", app.details_ui.store_error.c_str());
        ImGui::PopStyleColor();
        if (ImGui::Button("Continue")) app.analyze_job.reset();
    } else {
        ImGui::TextUnformatted("Done!");
    }

    ImGui::EndChild();
}

}  // namespace

namespace detail {

// The states a record-backed tab shows before its own content: the analyze
// progress, the not-analyzed prompt, the stale warning and "No paths found.".
// Returns true only when the record is ready to draw. The Paths and Stars tabs
// both call this, so the states read the same on each.
bool render_record_state(AppState& app, const char* not_analyzed_text) {
    if (app.analyze_job_shown()) {
        render_analyze_progress(app);
        return false;
    }
    if (app.viewed.status == store::RecordStatus::NotAnalyzed) {
        ImGui::TextUnformatted(not_analyzed_text);
        return false;
    }
    if (app.viewed.status == store::RecordStatus::Stale) {
        ImGui::TextColored(kWarningColor,
                           "Out of date: this result came from another Hydra version or "
                           "from different rules in hydra_rules.ini. Re-analyze to refresh it.");
        return false;
    }
    if (app.viewed.record->paths.empty()) {
        ImGui::TextUnformatted("No paths found.");
        return false;
    }
    return true;
}

}  // namespace detail

void render_song_panel(AppState& app) {
    if (!app.selected) return;

    // All of this panel's own state lives on AppState (DetailsViewState).
    const Path*& selected_path = app.details_ui.selected_path;
    GenerationWatcher& record_watcher = app.details_ui.record_watcher;

    // selected_path points into app.viewed's record, so it is only valid for
    // the record generation it was chosen under. Re-sync wherever the record
    // may have changed: at the top of the frame, and after the header, whose
    // previous / next buttons swap the song in the middle of this frame.
    auto sync_selected_path = [&] {
        if (record_watcher.changed(app.record_generation)) {
            selected_path = app.viewed.status == store::RecordStatus::Ready &&
                                    !app.viewed.record->paths.empty()
                                ? &app.viewed.record->best_path()
                                : nullptr;
        }
    };
    sync_selected_path();

    // Escape closes the panel like its X -- but not while a popup (a
    // confirm, an open combo) or a text box has the key.
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        app.show_details = false;

    // Ctrl+C copies the selected path -- unless a text input has focus.
    if (selected_path && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
        ImGui::SetClipboardText(
            selected_path->pathstring_verbose(app.viewed.record->multsqueezes).c_str());

    render_panel_header(app);
    if (!app.selected) return;
    sync_selected_path();
    ImGui::Spacing();
    render_headline(app);
    render_panel_notices(app);
    ImGui::Spacing();

    // The tabs, as before. The Preview only renders while its tab is the
    // active one; leaving it pauses playback.
    if (ImGui::BeginTabBar("##DetailsTabs")) {
        if (ImGui::BeginTabItem("Paths")) {
            if (detail::render_record_state(
                    app, "After analyzing this song, paths will show up here."))
                detail::render_path_panel(app, selected_path);
            ImGui::EndTabItem();
        }

        bool preview_shown = false;
        if (ImGui::BeginTabItem("Preview")) {
            preview_shown = true;
            detail::render_preview_panel(app, selected_path);
            ImGui::EndTabItem();
        }
        if (!preview_shown && app.preview) app.preview->pause();

        if (ImGui::BeginTabItem("Dynamics")) {
            detail::render_dynamics_panel(app);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stars")) {
            detail::render_stars_panel(app);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

}  // namespace hydra::ui
