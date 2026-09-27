#include "ui/details_view.h"
#include "ui/details_parts.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/path_view.h"
#include "core/model.h"   // kCloneHeroSpCap
#include "imgui.h"
#include "ui/fonts.h"
#include "ui/generation.h"
#include "ui/icons.h"
#include "ui/preview_controller.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

namespace hydra::ui {

namespace {

// The record/star/pencil/hash marker before each song-info line, matching
// hydra_app.py's dpg.add_image icons. Falls back to a plain filled circle in
// the icon's tint color when the texture failed to load (see icons.h -- a
// missing/unreadable resource/*.png is best-effort, not fatal). Advances the
// cursor and leaves the line open for more text.
void icon_marker(ImTextureID icon, const ImVec4& fallback_color) {
    float size = px(28.0f);
    if (icon != 0) {
        ImGui::Image(icon, ImVec2(size, size));
    } else {
        float r = px(7.0f);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(p.x + r, p.y + r), r, ImGui::ColorConvertFloat4ToU32(fallback_color));
        ImGui::Dummy(ImVec2(size, size));
    }
    ImGui::SameLine();
}

void render_song_info(AppState& app, float width) {
    ImGui::BeginChild("songinfo", ImVec2(width, px(170)), ImGuiChildFlags_Borders);

    // Ellipsized: long titles used to hard-clip mid-word at the panel edge
    // with no way to read the rest; now they trail off with "..." and the
    // full text is a hover away.
    icon_marker(g_icon_record, ImVec4(0.85f, 0.1f, 0.1f, 1.0f));
    ImGui::PushFont(nullptr, 24.0f);
    text_ellipsized(app.selected->title.c_str());
    ImGui::PopFont();

    icon_marker(g_icon_star, kAccentColor);
    ImGui::PushFont(nullptr, 24.0f);
    text_ellipsized(app.selected->artist.c_str());
    ImGui::PopFont();

    icon_marker(g_icon_pencil, ImVec4(0.9f, 0.75f, 0.1f, 1.0f));
    ImGui::PushFont(nullptr, 24.0f);
    text_ellipsized(app.selected->charter.empty() ? "(unknown charter)"
                                                  : app.selected->charter.c_str());
    ImGui::PopFont();

    icon_marker(g_icon_hash, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    text_ellipsized(app.selected->md5.c_str());
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

// The stored record's status for this song + chartmode. Replaces the old
// "/// Space for future stuff! ///" placeholder (inherited from the Python
// UI), which shipped a literal construction sign across a quarter of the
// modal's top row.
void render_record_status(AppState& app, float width) {
    ImGui::BeginChild("songanalysis", ImVec2(width, px(200)), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Stored result");

    const app::RecordStatusView& status = app.details_ui.paths_tab.status(app.viewed, app.record_generation.n);
    switch (status.state) {
        case store::RecordStatus::NotAnalyzed:
            ImGui::TextDisabled("Not analyzed yet.");
            break;
        case store::RecordStatus::Stale: {
            WarnColor warn;
            ImGui::TextWrapped("Stale: analyzed by an older Hydra version. "
                               "Re-analyze to refresh it.");
            break;
        }
        case store::RecordStatus::Ready:
            for (const std::string& line : status.lines)
                ImGui::TextUnformatted(line.c_str());
            break;
    }
    ImGui::EndChild();
}

void render_controls(AppState& app) {
    bool file_ok = app.selected_file_ok(ImGui::GetTime());

    // The missing-file warning line needs one more row when it shows; the
    // backend-limit row needs one more frame of height than the panel had.
    float panel_h = px(200.0f) + ImGui::GetFrameHeightWithSpacing();
    if (!file_ok) panel_h += ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("controls", ImVec2(0, panel_h), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("More Paths settings");

    ImGui::TextUnformatted("Score range:");
    ImGui::SameLine(px(100));
    ImGui::SetNextItemWidth(px(140));
    // The three number boxes apply every step live but save the INI once the
    // edit ends (AppState::edit_settings, flushed by run_frame).
    if (ImGui::InputInt("##depthvalue", &app.settings.depth_value)) {
        if (app.settings.depth_value < 0) app.settings.depth_value = 0;
        app.edit_settings();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(80));
    int mode_idx = app.settings.depth_mode;
    const char* modes[] = {"scores", "points"};
    if (ImGui::Combo("##depthmode", &mode_idx, modes, 2)) {
        app.settings.depth_mode = mode_idx;
        app.commit_settings();
    }

    ImGui::TextUnformatted("Path limit:");
    ImGui::SameLine(px(100));
    if (ImGui::Checkbox("##mslimit", &app.settings.mslimit_enabled)) app.commit_settings();
    ImGui::SameLine();
    bool mslimit_disabled = !app.settings.mslimit_enabled;
    begin_disabled_input(mslimit_disabled);
    ImGui::SetNextItemWidth(px(100));
    if (ImGui::InputInt("##mslimitvalue", &app.settings.mslimit_value)) {
        app.settings.mslimit_value = std::clamp(app.settings.mslimit_value, -500, 500);
        app.edit_settings();
    }
    ImGui::SameLine();
    // "mslimit_mstext" binds disabled_text ((50,50,50), same gray as the
    // InputInt's own disabled Text color) whenever mslimit is unchecked.
    ImGui::TextUnformatted("ms");
    end_disabled_input(mslimit_disabled);

    // The backend tables' display window. Purely a filter on what the
    // Activations tables draw -- it never reaches the search, so it never
    // re-keys or invalidates a stored record.
    ImGui::TextUnformatted("Backend limit:");
    ImGui::SameLine(px(100));
    if (ImGui::Checkbox("##backendlimit", &app.settings.backendlimit_enabled))
        app.commit_settings();
    ImGui::SameLine();
    bool backendlimit_disabled = !app.settings.backendlimit_enabled;
    begin_disabled_input(backendlimit_disabled);
    ImGui::SetNextItemWidth(px(100));
    if (ImGui::InputInt("##backendlimitvalue", &app.settings.backendlimit_value)) {
        app.settings.backendlimit_value =
            std::clamp(app.settings.backendlimit_value, 0, 500);
        app.commit_settings();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("ms");
    end_disabled_input(backendlimit_disabled);
    hint("Hides backend rows beyond +/- this many ms; squeezed-out notes always "
         "show. Display only: changing it never re-analyzes.");

    // The SP meter ceiling in bars: 4 is Clone Hero's rule; other values are
    // what-ifs. "Auto" raises the ceiling until the score settles. Records are
    // kept per cap, so changing it re-reads which record this song shows.
    {
        ImGui::TextUnformatted("SP cap:");
        ImGui::SameLine(px(100));
        // The number box keeps its last value while Auto is ticked, so
        // unticking returns to it.
        int& last_cap = app.details_ui.last_cap;
        if (app.settings.sp_cap) last_cap = *app.settings.sp_cap;
        bool spcap_auto = !app.settings.sp_cap.has_value();
        begin_disabled_input(spcap_auto);
        ImGui::SetNextItemWidth(px(100));
        if (ImGui::InputInt("##spcapvalue", &last_cap)) {
            if (last_cap < 1) last_cap = 1;
            app.settings.sp_cap = last_cap;
            app.edit_settings();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("bars");
        end_disabled_input(spcap_auto);
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto##spcapauto", &spcap_auto)) {
            app.settings.sp_cap = spcap_auto ? std::nullopt : std::optional<int>(last_cap);
            app.commit_settings();
        }
        hint((std::to_string(kCloneHeroSpCap) +
              " bars is Clone Hero's rule. Higher caps are what-ifs; Auto raises the "
              "cap until the score stops improving.").c_str());
    }

    ImGui::Spacing();
    // The error is a warning line with a remedy, not the button's label -- a
    // CTA that swaps its text for an error message loses its affordance and
    // leaves the user nothing to act on.
    if (!file_ok) {
        ImGui::TextColored(kWarningColor, "Song file not found.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Rescan library")) {
            app.request_scan = true;   // the main window starts the scan
            app.show_details = false;  // close_details() runs on the next frame's edge
            ImGui::CloseCurrentPopup();
        }
    }
    if (app.analysis_blocked())
        ImGui::TextColored(kWarningColor,
                           "Analysis is off until hydra_rules.ini is fixed and Hydra is "
                           "restarted.");
    bool analyze_disabled = app.analysis_blocked() || !file_ok ||
                            (app.analyze_job && !app.analyze_job->finished());
    begin_disabled_button(analyze_disabled);
    if (ImGui::Button("Analyze paths!", ImVec2(-1, px(40)))) {
        app.start_analyze();
    }
    end_disabled_button(analyze_disabled);
    ImGui::EndChild();
}

// The analyze job's lifecycle: store the finished result, reap the job.
// Runs every frame from render_details_modal, before any tab draws.
// Persisting and reaping a finished analysis must not depend on which tab is
// drawn -- when this lived in the Paths tab body, a finished job on the
// Preview tab was never stored and never reaped (the uitest 300 s hang).
void update_analyze_job(AppState& app) {
    // Keyed on the job generation, not the job's address: a freed AnalyzeJob's
    // block can be handed straight back to the next make_unique, and a pointer
    // compare then carries `stored`/`done_at` over from the previous job --
    // the fresh result is never stored and the stale done_at dismisses the
    // modal on its first finished frame. The state itself lives on AppState so
    // it dies with the app state it describes.
    GenerationWatcher& generation = app.details_ui.analyze_watcher;
    double& done_at = app.details_ui.done_at;
    bool& stored = app.details_ui.stored;
    std::string& store_error = app.details_ui.store_error;

    if (generation.changed(app.analyze_generation)) {
        done_at = -1.0;
        stored = false;
        store_error.clear();
    }
    AnalyzeJob* job = app.analyze_job.get();
    if (!job || !job->finished()) return;

    if (job->is_cancelled()) {
        // Cancelled runs have nothing to show or store.
        app.analyze_job.reset();
        return;
    }
    // An error stays until the user clicks Continue in the Paths tab.
    if (!job->ok()) return;

    if (!stored) {
        stored = true;
        // Persistence belongs to AppState, not to a draw call; the view
        // only shows the outcome.
        store_error = app.store_finished_analysis();
        if (store_error.empty()) done_at = ImGui::GetTime();
    }
    if (store_error.empty() && done_at >= 0 && ImGui::GetTime() - done_at > 0.5)
        app.analyze_job.reset();
}

// Display only; the state machine above owns storing and reaping.
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
            // An Auto-cap run can take minutes; the user needs an out that
            // isn't killing the app.
            if (ImGui::Button("Cancel")) job->cancel();
        }
    } else if (!job->ok()) {
        ImGui::TextColored(kWarningColor, "An error occurred:");
        ImGui::TextWrapped("%s", job->error().c_str());
        if (ImGui::Button("Continue")) app.analyze_job.reset();
    } else if (!app.details_ui.store_error.empty()) {
        ImGui::TextColored(kWarningColor, "An error occurred:");
        ImGui::TextWrapped("%s", app.details_ui.store_error.c_str());
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
    if (app.analyze_job) {
        render_analyze_progress(app);
        return false;
    }
    if (app.viewed.status == store::RecordStatus::NotAnalyzed) {
        ImGui::TextUnformatted(not_analyzed_text);
        return false;
    }
    if (app.viewed.status == store::RecordStatus::Stale) {
        ImGui::TextColored(
            kWarningColor,
            "This record is out of date. To make sure you have the latest "
            "results, please re-analyze.");
        return false;
    }
    if (app.viewed.record->paths.empty()) {
        ImGui::TextUnformatted("No paths found.");
        return false;
    }
    return true;
}

}  // namespace detail

void render_details_modal(AppState& app) {
    // Job lifecycles first, every frame -- even with the modal closed or a
    // different tab in front.
    update_analyze_job(app);
    app.reap_dynamics();

    // All of this modal's own state lives on AppState (see DetailsViewState):
    // a static here would outlive the AppState it describes.
    bool& prev_open = app.details_ui.prev_open;
    const Path*& selected_path = app.details_ui.selected_path;
    GenerationWatcher& record_watcher = app.details_ui.record_watcher;

    if (app.show_details && !prev_open) ImGui::OpenPopup("SongDetails");
    // The one teardown, on the open-to-closed edge, whatever closed the
    // window: its X, the Rescan library button, or the resync below.
    if (!app.show_details && prev_open) app.close_details();
    prev_open = app.show_details;
    if (!app.show_details) return;

    // Sized relative to the viewport every frame and not user-resizable,
    // mirroring hydra_app.py's on_viewport_resize ("songdetails" is
    // positioned/sized off dpg.get_viewport_width/height, not a fixed size) --
    // a fixed popup size left the fixed-width song info/analysis panels
    // squeezing the controls panel (and its scores/points dropdown) into too
    // little room on a narrower window.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + px(40), viewport->WorkPos.y + px(40)),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x - px(80), viewport->WorkSize.y - px(80)),
        ImGuiCond_Always);
    bool open = true;
    // The chartmode is baked into the title (matching hydra_app.py's
    // "Song Details\t\t\t\t{chartmode_key}"); "###SongDetails" keeps the
    // popup's identity stable even though the visible label changes with it.
    char title[160];
    std::snprintf(title, sizeof(title), "Song Details\t\t\t\t%s###SongDetails",
                 app.settings.chartmode_key().c_str());
    bool visible =
        ImGui::BeginPopupModal(title, &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // `open` is checked unconditionally below (not only when `visible` is
    // true) because the popup's close button can make BeginPopupModal itself
    // start returning false on/after the closing frame, without ever handing
    // back a true-but-open-false frame to react to. Either way show_details
    // goes false, and the next frame's edge above runs close_details().
    if (!visible) {
        // We know app.show_details was true when we called OpenPopup above,
        // so if ImGui says the popup isn't showing, our state has drifted
        // from ImGui's -- resync.
        app.show_details = false;
        return;
    }

    if (!open) {
        app.show_details = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // selected_path points into app.viewed's record, so it is only valid for
    // the record generation it was chosen under. Re-sync it wherever the
    // record may have changed since the last check: at the top of the frame
    // (an analysis stored on a previous frame) and again after the controls
    // panel, whose SP cap widgets replace the lookup in the middle of this
    // very frame -- the path panel below would otherwise read the freed record.
    auto sync_selected_path = [&] {
        if (record_watcher.changed(app.record_generation)) {
            selected_path = app.viewed.status == store::RecordStatus::Ready &&
                                    !app.viewed.record->paths.empty()
                                ? &app.viewed.record->best_path()
                                : nullptr;
        }
    };
    sync_selected_path();

    // Ctrl+C copies the selected path -- unless a text input has focus, which
    // keeps its own copy behavior.
    // Built only when the chord is pressed, not every frame just in case.
    if (selected_path && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
        ImGui::SetClipboardText(
            selected_path->pathstring_verbose(app.viewed.record->multsqueezes).c_str());

    // Song info / record-status panels scale with the popup's width
    // (itself sized off the viewport, see below) instead of a fixed pixel
    // width, so the controls panel isn't squeezed into negative space on a
    // narrower window.
    float avail_w = ImGui::GetContentRegionAvail().x;
    float side_w = std::max(px(260.0f), avail_w * 0.24f);
    ImGui::BeginGroup();
    render_song_info(app, side_w);
    ImGui::SameLine();
    render_record_status(app, side_w);
    ImGui::SameLine();
    render_controls(app);
    ImGui::EndGroup();
    sync_selected_path();  // the cap widgets above may have swapped the record

    ImGui::Separator();

    // Paths (the existing panel) and the 3D Preview live side by side in a tab
    // bar. The Preview only renders while its tab is the active one; leaving it
    // pauses playback rather than tearing the whole scene down.
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
        // Pause playback whenever the Preview tab isn't the one on screen.
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

    ImGui::EndPopup();
}

}  // namespace hydra::ui
