#include "ui/details_view.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/dynamics_breakdown.h"
#include "app/path_view.h"
#include "core/model.h"   // group_thousands
#include "core/winstr.h"
#include "imgui.h"
#include "imgui_internal.h"  // SetKeyOwner, owner-aware IsKeyPressed
#include "ui/dynamics_load_job.h"
#include "ui/fonts.h"
#include "ui/generation.h"
#include "ui/icons.h"
#include "ui/preview_controller.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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

    app::RecordStatusView status = app::build_record_status(app.viewed);
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
    bool file_ok = file_exists_utf8(app.selected->notespath);

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
            app.request_scan = true;  // the main window starts the scan
            app.show_details = false;
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

// Section headers (Multiplier squeezes/Activations/Score breakdown) render in
// the big display font; their tabular content renders in MonoFont so numbers
// line up -- mirrors hydra_app.py binding "MainFont24" to each tree_node
// label and "MonoFont" to what's inside. RAII-free: call end_section() after.
bool begin_section(const char* label) {
    ImGui::PushFont(nullptr, 24.0f);
    bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopFont();
    if (open) ImGui::PushFont(g_mono_font, 0.0f);
    return open;
}
void end_section() {
    ImGui::PopFont();
    ImGui::TreePop();
}

// One line that pushes the warning color when the view-model flagged it.
void warnable_text(const app::TextLine& line) {
    if (line.warn) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
    ImGui::TextUnformatted(line.text.c_str());
    if (line.warn) ImGui::PopStyleColor();
}

void render_multsqueeze_section(const Path* path) {
    if (begin_section("Multiplier squeezes")) {
        std::vector<app::MultSqueezeView> squeezes = app::build_multsqueezes(*path);
        if (squeezes.empty()) {
            ImGui::TextDisabled("None.");
        } else {
            for (size_t i = 0; i < squeezes.size(); ++i) {
                const app::MultSqueezeView& msq = squeezes[i];
                // Keyed on the index, not the element address: the view-model
                // vector is rebuilt per frame, so its addresses are unstable.
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::TreeNode(msq.label.c_str())) {
                    ImGui::PushFont(g_mono_font, 0.0f);
                    ImGui::TextUnformatted(msq.howto.c_str());
                    ImGui::PopFont();
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        end_section();
    }
}

void render_activations_section(const Path* path, const HydraRecord& record,
                                const SongTiming* timing,
                                const Settings& settings) {
    if (begin_section("Activations")) {
        app::ActivationsView view = app::build_activations(
            *path, record, timing,
            static_cast<double>(settings.hit_window_ms),
            settings.backend_limit(), settings.rules);

        if (view.acts.empty()) ImGui::TextDisabled("None.");
        for (const app::ActivationDetailsView& av : view.acts) {
            if (av.difficult) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
            bool open = ImGui::TreeNode(av.header.c_str());
            if (av.difficult) ImGui::PopStyleColor();
            if (!open) continue;

            if (!av.calibration.empty())
                ImGui::TextUnformatted(av.calibration.c_str());
            ImGui::TextUnformatted(av.frontend.c_str());

            if (!av.scale_warning.empty()) {
                {
                    WarnColor warn;
                    // Wrap at the panel edge -- the details child can be
                    // narrower than the line.
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(av.scale_warning.c_str());
                    ImGui::PopTextWrapPos();
                }
                hint(app::kTransferScaleHint);
            }

            if (!av.overfill_warning.empty()) {
                {
                    WarnColor warn;
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(av.overfill_warning.c_str());
                    ImGui::PopTextWrapPos();
                }
                hint(app::kOverfillHint);
            }

            for (const app::TextLine& sq : av.sqinouts) warnable_text(sq);

            if (av.backends.empty()) {
                ImGui::TextUnformatted("Backends: None.");
            } else {
                ImGui::TextUnformatted("Backends:");
                if (ImGui::BeginTable("backends", 4,
                                      ImGuiTableFlags_Borders |
                                          ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn("Timing", ImGuiTableColumnFlags_WidthFixed,
                                            px(80));
                    ImGui::TableSetupColumn("Chord", ImGuiTableColumnFlags_WidthFixed,
                                            px(80));
                    ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed,
                                            px(80));
                    ImGui::TableSetupColumn("Rating", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (const app::BackendRowView& row : av.backends) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(row.timing.c_str());
                        if (!row.tooltip.empty() &&
                            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip("%s", row.tooltip.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(row.chord.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(row.points.c_str());
                        ImGui::TableSetColumnIndex(3);
                        if (row.warn) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
                        ImGui::TextUnformatted(row.rating.c_str());
                        if (row.warn) ImGui::PopStyleColor();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::Spacing();
            ImGui::TreePop();
        }

        for (const app::TextLine& line : view.footer) warnable_text(line);
        end_section();
    }
}

void render_score_breakdown_section(const Path* path) {
    if (begin_section("Score breakdown")) {
        for (const std::string& line : app::build_score_breakdown(*path))
            ImGui::TextUnformatted(line.c_str());
        end_section();
    }
}

// `copied_at` (when "Copied!" last flashed) is owned by AppState's
// DetailsViewState and passed by reference, so it dies with the app state.
void render_path_details(const Path* path, const HydraRecord& record,
                         const SongTiming* timing, const Settings& settings,
                         double& copied_at) {
    ImGui::PushFont(nullptr, 0.0f);  // default font for the button, like Python's MainFont
    if (ImGui::Button("Copy path string", ImVec2(px(180), px(30)))) {
        ImGui::SetClipboardText(path->pathstring_verbose().c_str());
        copied_at = ImGui::GetTime();
    }
    hint("Ctrl+C also copies the selected path");
    if (copied_at >= 0.0 && ImGui::GetTime() - copied_at < 2.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("Copied!");
    }
    ImGui::PopFont();
    ImGui::Spacing();

    render_multsqueeze_section(path);
    render_activations_section(path, record, timing, settings);
    render_score_breakdown_section(path);
}

// Path list on the left + details on the right, grouped into score tiers.
// selected_path/record_watcher live in AppState's DetailsViewState, passed by
// reference so this stays a free function instead of a lambda closure.
// One selectable path row: pathstring on the left, difficulty (ms) right-
// aligned, warning-colored when the path is difficult. Mirrors the two-column
// dpg.table row hydra_app.py builds per path.
void render_path_row(const Path* p, const Path*& selected_path) {
    ImGui::PushID(p);
    // Zero CellPadding: a per-row table's default (4,2) padding made rows
    // visibly looser/taller than the plain-text list this replaces (and than
    // hydra_app.py's equivalent, which has no such padding either).
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    // hydra_app.py binds MonoFont to the score tree node, and DPG's font
    // binding cascades to descendant widgets that don't set their own font --
    // so every path row (pathstring + ms) actually renders in MonoFont
    // (CourierPrime), not MainFont. CourierPrime's space glyph is a true
    // monospace ~9.6px vs MainFont's ~2.8px, which is the entire reason the
    // path tokens looked cramped: this row was rendering in the wrong font.
    ImGui::PushFont(g_mono_font, 0.0f);
    if (ImGui::BeginTable("pathrow", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("diff", ImGuiTableColumnFlags_WidthFixed, px(130.0f));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        // Long paths clip at the column edge; row_selectable offers the full
        // string on hover (only over the path column -- the ms column speaks
        // for itself).
        if (row_selectable(p->pathstring().c_str(), p == selected_path))
            selected_path = p;

        app::PathRowView row = app::build_path_row(*p);
        if (!row.ms.empty()) {
            ImGui::TableSetColumnIndex(1);
            if (row.warn) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
            ImGui::TextUnformatted(row.ms.c_str());
            if (row.warn) ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

// The path list: every unique score along the traversal gets its own
// default-open tree node labeled with the comma-grouped score, and every tied
// path at that score is listed inside it -- mirrors hydra_app.py's
// dpg.add_tree_node(label=f"{current_score:,}") grouping. The grouping (and
// the all-0 section's visibility rule) comes from app::build_path_list.
void render_path_panel(AppState& app, const Path*& selected_path) {
    ImGui::BeginChild("pathlist", ImVec2(px(600), 0), ImGuiChildFlags_Borders);
    app::PathListView list = app::build_path_list(*app.viewed.record);

    int tier = 0;
    for (const app::PathGroupView& group : list.groups) {
        ++tier;
        if (tier == 1) {
            ImGui::SeparatorText("Optimal Path");
        } else if (tier == 2) {
            ImGui::SeparatorText(list.more_label.c_str());
        }

        ImGui::PushID(tier);
        ImGui::PushFont(g_mono_font, 0.0f);  // score header, like Python's MonoFont tree node
        bool tree_open = ImGui::TreeNodeEx(group.score_label.c_str(),
                                           ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopFont();
        ImGui::PopID();

        if (tree_open) {
            for (const Path* p : group.paths) render_path_row(p, selected_path);
            ImGui::TreePop();
        }
    }

    // The all-0 path section, appended below the generated list.
    if (list.show_allzero) {
        ImGui::SeparatorText("Best All-0 Path (Path limit: 0 ms)");
        ImGui::PushID("allzero");
        ImGui::PushFont(g_mono_font, 0.0f);
        bool open = ImGui::TreeNodeEx(list.allzero_label.c_str(),
                                      ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopFont();
        if (open) {
            for (const Path* p : list.allzero) render_path_row(p, selected_path);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("pathdetails", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (selected_path)
        render_path_details(selected_path, *app.viewed.record,
                           app.viewed.timing ? &*app.viewed.timing : nullptr,
                           app.settings, app.details_ui.copied_at);
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

// The Preview tab: a transport row over the 3D note highway. Reached only while
// the tab is shown, so the controller (and its decode + GPU work) spins up lazily
// on first view, per the "render only while active" gating.
void render_preview_panel(AppState& app, const Path* selected_path) {
    PreviewController* pc = app.preview_controller();
    if (pc == nullptr) {
        ImGui::TextUnformatted("Preview is unavailable (no render device).");
        return;
    }
    if (!app.selected) {
        ImGui::TextUnformatted("Select a song to preview.");
        return;
    }

    // Open (or keep open) for the current selection; a no-op once running for
    // this chart. This is where the async decode starts.
    pc->set_volume(app.settings.preview_volume);  // before the audio exists too
    // The meter's ceiling is the cap the viewed record was analyzed at; a
    // chart with no record yet previews at the Clone Hero cap.
    const int sp_cap = app.viewed.status == store::RecordStatus::Ready
                           ? app.viewed.record->sp_cap.value_or(kCloneHeroSpCap)
                           : kCloneHeroSpCap;
    pc->open(*app.selected, app.settings.view_prodrums, app.settings.effective_bass2x(),
             app.settings.difficulty(), selected_path, sp_cap, app.settings.rules);
    pc->poll();

    if (pc->has_error()) {
        ImGui::TextColored(kWarningColor, "Preview failed: %s", pc->error().c_str());
        return;
    }
    if (pc->loading()) {
        // A big chart decodes for several seconds; the step label and bar
        // are what tell the user it is still moving.
        PreviewController::LoadProgress lp = pc->load_progress();
        ImGui::Text("Loading preview: %s", lp.label.c_str());
        char overlay[16];
        std::snprintf(overlay, sizeof(overlay), "%.0f%%", lp.fraction * 100.0f);
        ImGui::ProgressBar(lp.fraction, ImVec2(-1.0f, 0.0f), overlay);
        return;
    }

    // Transport row: back 5 s, back 5 ticks, play/pause, forward 5 ticks,
    // forward 5 s, a scrubber, and the time readout. Every piece whose text
    // changes while playing sits in a fixed slot (see widgets.h): otherwise
    // the Vol slider walked under a held mouse as the readout's digits
    // changed width. The four step buttons have fixed labels, so they need
    // no slot.
    // The tick buttons and comma/period step this many chart ticks.
    constexpr int kTickStep = 5;
    if (ImGui::Button("-5s")) pc->jump_ms(-5000.0);
    hint("Back 5 seconds (Left arrow)");
    ImGui::SameLine();
    if (ImGui::Button("< 5 Ticks")) pc->step_ticks(-kTickStep);
    hint("Back 5 ticks (Comma)");
    ImGui::SameLine();
    const float play_w = std::max(button_slot_width("Play"), button_slot_width("Pause"));
    if (button_in_slot(pc->playing() ? "Pause" : "Play", play_w)) pc->toggle();
    hint("Play or pause (Space)");
    ImGui::SameLine();
    if (ImGui::Button("5 Ticks >")) pc->step_ticks(kTickStep);
    hint("Forward 5 ticks (Period)");
    ImGui::SameLine();
    if (ImGui::Button("+5s")) pc->jump_ms(5000.0);
    hint("Forward 5 seconds (Right arrow)");
    ImGui::SameLine();

    // The clock drives the scrubber, so a chart with no audio still scrubs.
    double len_ms = pc->length_ms();
    float pos_s = static_cast<float>(pc->position_ms() / 1000.0);
    float len_s = static_cast<float>(len_ms / 1000.0);
    std::string len_digits = widest_digits(digit_count((long long)len_s));
    std::string readout_sample = len_digits + "." + len_digits.substr(0, 1) + " / " +
                                 len_digits + "." + len_digits.substr(0, 1) + " s";
    const float readout_w = text_slot_width(readout_sample.c_str());
    const float volume_w = px(110.0f);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(
        px(120.0f), ImGui::GetContentRegionAvail().x - readout_w - text_slot_width("Vol") -
                        volume_w - 3.0f * spacing));
    if (ImGui::SliderFloat("##scrub", &pos_s, 0.0f, len_s > 0.0f ? len_s : 1.0f, "%.1fs"))
        pc->seek_ms(static_cast<double>(pos_s) * 1000.0);
    // Holding the scrubber pauses playback (Onyx's rule); release resumes.
    pc->set_scrubbing(ImGui::IsItemActive());
    ImGui::SameLine();
    char readout[48];
    std::snprintf(readout, sizeof(readout), "%.1f / %.1f s", pos_s, len_s);
    text_in_slot(readout, readout_w);

    // Volume: applied live and remembered in the settings file.
    ImGui::TextUnformatted("Vol");
    ImGui::SameLine();
    int volume = app.settings.preview_volume;
    ImGui::SetNextItemWidth(volume_w);
    if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d%%")) {
        app.settings.preview_volume = volume;
        pc->set_volume(volume);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) app.commit_settings();

    // Keys: Space plays or pauses, Left/Right jump 5 s, comma/period step
    // 5 ticks; a held arrow or comma/period repeats. Not while a text field
    // has the keyboard. Keyboard navigation (on in app_shell.cpp) reads the
    // arrows and Space only when nobody owns them, so the Preview claims them
    // every frame it shows; a claim made this frame still holds during next
    // frame's navigation update, so even the first press lands here rather
    // than moving focus, nudging the scrubber, or pressing whichever button
    // was clicked last.
    if (!ImGui::GetIO().WantTextInput) {
        const ImGuiID keys_owner = ImGui::GetID("##preview_keys");
        ImGui::SetKeyOwner(ImGuiKey_LeftArrow, keys_owner);
        ImGui::SetKeyOwner(ImGuiKey_RightArrow, keys_owner);
        ImGui::SetKeyOwner(ImGuiKey_Space, keys_owner);
        if (ImGui::IsKeyPressed(ImGuiKey_Space, ImGuiInputFlags_None, keys_owner)) pc->toggle();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat, keys_owner))
            pc->jump_ms(-5000.0);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, ImGuiInputFlags_Repeat, keys_owner))
            pc->jump_ms(5000.0);
        if (ImGui::IsKeyPressed(ImGuiKey_Comma, true)) pc->step_ticks(-kTickStep);
        if (ImGui::IsKeyPressed(ImGuiKey_Period, true)) pc->step_ticks(kTickStep);
    }

    // Highway viewport: size the offscreen target to the remaining region.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = static_cast<int>(avail.x);
    int h = static_cast<int>(avail.y);
    ID3D11ShaderResourceView* srv = pc->render(w, h);
    if (srv != nullptr && w > 0 && h > 0) {
        ImGui::Image((ImTextureID)(intptr_t)srv,
                     ImVec2(static_cast<float>(w), static_cast<float>(h)));

        // The time box, drawn over the image the way Onyx draws its own
        // (top-left, monospace, on a translucent dark panel): time / length,
        // [measure:beat:tick] for both, BPM, and the practice section. The
        // section line is absent on charts that have no sections.
        hydra::app::PreviewTimeBox box = pc->time_box();
        const char* lines[4];
        int line_count = 0;
        lines[line_count++] = box.timestamp.c_str();
        lines[line_count++] = box.measure_beat.c_str();
        lines[line_count++] = box.bpm.c_str();
        if (!box.section.empty()) lines[line_count++] = box.section.c_str();
        ImFont* font = g_mono_font ? g_mono_font : ImGui::GetFont();
        const render::PreviewConfig& pcfg = pc->preview_config();
        const float size = px(pcfg.text.time_box_size);
        const float margin = px(pcfg.text.time_box_margin);
        const float pad = px(8.0f);
        ImVec2 origin = ImGui::GetItemRectMin();
        float text_w = 0.0f;
        for (int i = 0; i < line_count; ++i)
            text_w = std::max(text_w,
                              font->CalcTextSizeA(size, FLT_MAX, 0.0f, lines[i]).x);
        const float line_h = size * 1.25f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 box_min(origin.x, origin.y);
        ImVec2 box_max(origin.x + margin + text_w + pad * 2.0f,
                       origin.y + margin + line_h * static_cast<float>(line_count) + pad);
        dl->AddRectFilled(box_min, box_max, IM_COL32(0, 0, 0, 128), px(6.0f),
                          ImDrawFlags_RoundCornersBottomRight);
        for (int i = 0; i < line_count; ++i)
            dl->AddText(font, size, ImVec2(origin.x + margin, origin.y + margin + line_h * i),
                        IM_COL32(255, 255, 255, 255), lines[i]);

        // The score box, under the time box in the same panel style: the
        // running score in large type, then "x<mult> · combo <n>" in light
        // grey. Absent until the chart is analyzed; "Score unavailable" (at
        // the time box's size) when the path can't be replayed to its stored
        // score. Right corners rounded, since it sits against the left edge.
        hydra::app::PreviewScoreBox score = pc->score_box();
        if (score.shown) {
            const float score_size = score.available ? size * 1.8f : size;
            const float score_h = score_size * 1.2f;
            const float gap = px(6.0f);
            const float score_w =
                font->CalcTextSizeA(score_size, FLT_MAX, 0.0f, score.score.c_str()).x;
            const float detail_w =
                score.detail.empty()
                    ? 0.0f
                    : font->CalcTextSizeA(size, FLT_MAX, 0.0f, score.detail.c_str()).x;
            const float lines_h = score_h + (score.detail.empty() ? 0.0f : line_h);
            ImVec2 s_min(origin.x, box_max.y + gap);
            ImVec2 s_max(origin.x + margin + std::max(score_w, detail_w) + pad * 2.0f,
                         s_min.y + pad + lines_h + pad);
            dl->AddRectFilled(s_min, s_max, IM_COL32(0, 0, 0, 128), px(6.0f),
                              ImDrawFlags_RoundCornersRight);
            dl->AddText(font, score_size, ImVec2(origin.x + margin, s_min.y + pad),
                        IM_COL32(255, 255, 255, 255), score.score.c_str());
            if (!score.detail.empty())
                dl->AddText(font, size, ImVec2(origin.x + margin, s_min.y + pad + score_h),
                            IM_COL32(200, 200, 200, 255), score.detail.c_str());
        }

        // The Star Power meter: a gauge down the image's right edge, filling
        // bottom-up as phrases are collected and draining while SP is active.
        // Hydra's own overlay, like the time box above -- not part of the Onyx
        // render. The value is the view-model's curve read at the playhead, so
        // it is anchored to the same engine truth the path overlay is.
        if (pc->sp_meter_has_curve()) {
            ImVec2 img_max = ImGui::GetItemRectMax();
            const float bar_w = px(14.0f);
            const float inset = px(10.0f);
            const float v_margin = px(10.0f);
            ImVec2 gauge_min(img_max.x - inset - bar_w, origin.y + v_margin);
            ImVec2 gauge_max(img_max.x - inset, img_max.y - v_margin);
            if (gauge_max.y > gauge_min.y) {
                dl->AddRectFilled(gauge_min, gauge_max, IM_COL32(0, 0, 0, 128), px(4.0f));

                const int cap = std::max(1, pc->sp_meter_cap());
                float fill = static_cast<float>(pc->sp_meter_bars()) / static_cast<float>(cap);
                fill = fill < 0.0f ? 0.0f : (fill > 1.0f ? 1.0f : fill);

                const float fill_pad = px(2.0f);
                ImVec2 in_min(gauge_min.x + fill_pad, gauge_min.y + fill_pad);
                ImVec2 in_max(gauge_max.x - fill_pad, gauge_max.y - fill_pad);
                const float in_h = in_max.y - in_min.y;
                if (in_h > 0.0f && fill > 0.0f)
                    dl->AddRectFilled(ImVec2(in_min.x, in_max.y - in_h * fill), in_max,
                                      IM_COL32(255, 204, 51, 230));  // Star Power gold
                // One line per whole-bar boundary, over the fill, so a glance
                // reads how many bars are banked and not just how full it is.
                for (int b = 1; b < cap; ++b) {
                    const float y = in_max.y - in_h * (static_cast<float>(b) /
                                                       static_cast<float>(cap));
                    dl->AddLine(ImVec2(in_min.x, y), ImVec2(in_max.x, y),
                                IM_COL32(0, 0, 0, 160), px(1.0f));
                }
            }
        }
    }
}

// ---- Dynamics tab ----------------------------------------------------------

// Pad dot colours — Clone Hero's standard lane colours.
ImVec4 pad_color(app::DynamicsRow row) {
    switch (row) {
        case app::DynamicsRow::RedSnare:     return ImVec4(0.85f, 0.15f, 0.15f, 1.0f);
        case app::DynamicsRow::YellowCymbal: return ImVec4(0.90f, 0.85f, 0.10f, 1.0f);
        case app::DynamicsRow::YellowTom:    return ImVec4(0.90f, 0.85f, 0.10f, 1.0f);
        case app::DynamicsRow::BlueCymbal:   return ImVec4(0.20f, 0.45f, 0.90f, 1.0f);
        case app::DynamicsRow::BlueTom:      return ImVec4(0.20f, 0.45f, 0.90f, 1.0f);
        case app::DynamicsRow::GreenCymbal:  return ImVec4(0.15f, 0.75f, 0.20f, 1.0f);
        case app::DynamicsRow::GreenTom:     return ImVec4(0.15f, 0.75f, 0.20f, 1.0f);
        case app::DynamicsRow::Kick:         return ImVec4(0.90f, 0.55f, 0.10f, 1.0f);
        case app::DynamicsRow::Kick2x:       return ImVec4(0.90f, 0.55f, 0.10f, 1.0f);
        default:                             return ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
    }
}

// Draw a small filled circle in `color` before the next text on this line.
void pad_dot(const ImVec4& color) {
    float r = px(5.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float y_off = (ImGui::GetTextLineHeight() - 2.0f * r) * 0.5f;
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(p.x + r, p.y + y_off + r), r,
        ImGui::ColorConvertFloat4ToU32(color));
    ImGui::Dummy(ImVec2(2.0f * r + px(4.0f), ImGui::GetTextLineHeight()));
    ImGui::SameLine();
}

// A row in the Ghost/Accent/Normal/All table. `disabled` dims the text.
void dynamics_table_row(const char* label, const app::DynamicsCounts& c,
                        bool disabled, const ImVec4* dot_color = nullptr) {
    ImGui::TableNextRow();
    // kDisabledTextColor is tuned for text on a teal button; on the dark panel
    // it disappears. The style's own disabled text grey reads fine here.
    if (disabled)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    ImGui::TableNextColumn();
    if (dot_color) pad_dot(*dot_color);
    ImGui::TextUnformatted(label);

    ImGui::TableNextColumn();
    ImGui::Text("%s", group_thousands(c.ghost).c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%s", group_thousands(c.accent).c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%s", group_thousands(c.normal).c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%s", group_thousands(c.all()).c_str());

    if (disabled) ImGui::PopStyleColor();
}

void render_dynamics_panel(AppState& app) {
    if (!app.selected) return;

    // Lifecycle (store lookup, job start/reap, persistence) runs on AppState
    // so it stays out of render code — same pattern as update_analyze_job.
    app.update_dynamics();

    // Loading state: job in flight but not finished yet.
    if (app.dynamics_job && !app.dynamics_job->finished()) {
        ImGui::TextUnformatted("Reading chart...");
        return;
    }
    // Error state: job finished but failed (kept around for its message).
    if (app.dynamics_job && app.dynamics_job->finished() && !app.dynamics_job->ok()) {
        ImGui::Text("Dynamics failed: %s", app.dynamics_job->error().c_str());
        return;
    }
    if (!app.dynamics_result) return;

    // A put_dynamics failure is shown as a status line, not a blocker.
    if (!app.dynamics_store_error.empty())
        ImGui::TextColored(kWarningColor, "%s", app.dynamics_store_error.c_str());

    const app::DynamicsBreakdown& bd = *app.dynamics_result;
    bool pro = app.settings.view_prodrums;
    bool bass2x = app.settings.effective_bass2x();
    const app::DynamicsCounts played = bd.played_total(bass2x);

    // "This chart has no ghost or accent notes." above everything when no dynamics.
    if (!played.has_dynamics()) {
        ImGui::TextUnformatted("This chart has no ghost or accent notes.");
        ImGui::Spacing();
    }

    float avail_w = ImGui::GetContentRegionAvail().x;
    float left_w = avail_w * 0.63f;

    // ---- Left box ----
    ImGui::BeginChild("dynleft", ImVec2(left_w, 0), ImGuiChildFlags_Borders);

    // Pads section.
    ImGui::SeparatorText("Pads");

    const int table_flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable("##padtable", 5, table_flags)) {
        ImGui::TableSetupColumn("Pad");
        ImGui::TableSetupColumn("Ghost");
        ImGui::TableSetupColumn("Accent");
        ImGui::TableSetupColumn("Normal");
        ImGui::TableSetupColumn("All");
        ImGui::TableHeadersRow();

        // Pad rows in DynamicsRow order, Red through Green tom.
        // With Pro Drums off, skip the three Cymbal rows.
        for (int i = 0; i <= static_cast<int>(app::DynamicsRow::GreenTom); ++i) {
            auto r = static_cast<app::DynamicsRow>(i);
            // Skip cymbal rows when not pro.
            if (!pro && (r == app::DynamicsRow::YellowCymbal ||
                         r == app::DynamicsRow::BlueCymbal ||
                         r == app::DynamicsRow::GreenCymbal))
                continue;
            const app::DynamicsCounts& c = bd.row(r);
            bool disabled = !c.has_dynamics();
            ImVec4 dot = pad_color(r);
            dynamics_table_row(app::dynamics_row_label(r, pro), c,
                               disabled, &dot);
        }
        ImGui::EndTable();
    }

    // Kicks section.
    ImGui::SeparatorText("Kicks");

    {
        const app::DynamicsCounts k2x = bd.row(app::DynamicsRow::Kick2x);
        const app::DynamicsCounts ktot = bd.kicks_total();
        int pct = ktot.all() > 0
                      ? static_cast<int>(100.0 * k2x.all() / ktot.all())
                      : 0;
        ImGui::Text("2x kicks: %s of %s kick notes (%d%%)",
                    group_thousands(k2x.all()).c_str(),
                    group_thousands(ktot.all()).c_str(), pct);
    }

    if (ImGui::BeginTable("##kicktable", 5, table_flags)) {
        ImGui::TableSetupColumn("Pad");
        ImGui::TableSetupColumn("Ghost");
        ImGui::TableSetupColumn("Accent");
        ImGui::TableSetupColumn("Normal");
        ImGui::TableSetupColumn("All");
        ImGui::TableHeadersRow();

        {
            const app::DynamicsCounts& k = bd.row(app::DynamicsRow::Kick);
            ImVec4 kdot = pad_color(app::DynamicsRow::Kick);
            dynamics_table_row("Kick", k, !k.has_dynamics(), &kdot);
        }
        {
            const app::DynamicsCounts& k2 = bd.row(app::DynamicsRow::Kick2x);
            // With 2x Bass off, always draw the 2x kick row disabled
            // but keep its numbers.
            bool disabled = !bass2x || !k2.has_dynamics();
            ImVec4 k2dot = pad_color(app::DynamicsRow::Kick2x);
            dynamics_table_row("2x kick", k2, disabled, &k2dot);
        }
        {
            const app::DynamicsCounts ktot = bd.kicks_total();
            dynamics_table_row("All kicks", ktot, !ktot.has_dynamics());
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // ---- Right box ----
    ImGui::BeginChild("dynright", ImVec2(0, 0), ImGuiChildFlags_Borders);

    // Totals section.
    ImGui::SeparatorText("Totals");
    ImGui::Text("Ghosts: %s", group_thousands(played.ghost).c_str());
    ImGui::Text("Accents: %s", group_thousands(played.accent).c_str());
    {
        int dyn = played.ghost + played.accent;
        int total = played.all();
        int pct = total > 0 ? static_cast<int>(100.0 * dyn / total) : 0;
        ImGui::Text("Dynamic notes: %s of %s (%d%%)",
                    group_thousands(dyn).c_str(),
                    group_thousands(total).c_str(), pct);
    }

    // Chart section.
    ImGui::SeparatorText("Chart");
    if (bd.dynamics_enabled)
        ImGui::TextUnformatted("Dynamics enabled: yes");
    else
        ImGui::TextUnformatted("Dynamics enabled: no (markings ignored by Clone Hero)");

    if (bass2x)
        ImGui::TextUnformatted("2x kicks: counted (2x Bass on)");
    else
        ImGui::TextUnformatted("2x kicks: not counted (2x Bass off)");

    ImGui::EndChild();
}

}  // namespace

void render_details_modal(AppState& app) {
    // Job lifecycle first, every frame -- even with the modal closed or a
    // different tab in front.
    update_analyze_job(app);

    // All of this modal's own state lives on AppState (see DetailsViewState):
    // a static here would outlive the AppState it describes.
    bool& prev_open = app.details_ui.prev_open;
    const Path*& selected_path = app.details_ui.selected_path;
    GenerationWatcher& record_watcher = app.details_ui.record_watcher;

    if (app.show_details && !prev_open) ImGui::OpenPopup("SongDetails");
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
    // true) because the popup's close button can make BeginPopupModal
    // itself start returning false on/after the closing frame, without ever
    // handing back a true-but-open-false frame to react to. Gating the
    // app.show_details reset on that transition left it permanently true
    // once a popup was closed -- OpenPopup only fires on show_details'
    // false->true edge, so no row click could ever reopen the popup again
    // (rows still "worked", select() still ran, but nothing visible ever
    // happened, which is what looked like every song becoming unclickable).
    // Closing the modal abandons any in-flight analysis: with the modal gone
    // there is nowhere to show its progress or result, and the search would
    // otherwise keep burning CPU (up to the full Auto-cap budget) invisibly.
    // The main window reaps the job once the cancel lands.
    auto cancel_running_analysis = [&app] {
        if (app.analyze_job && !app.analyze_job->finished()) app.analyze_job->cancel();
    };

    // Closing the modal also tears down the Preview: its audio device must stop
    // and its GPU/decode work must not keep running behind a hidden popup.
    auto close_preview = [&app] {
        if (app.preview) app.preview->close();
    };

    // Cancel any in-flight dynamics parse so it doesn't run behind a hidden popup.
    auto close_dynamics = [&app] {
        if (app.dynamics_job) { app.dynamics_job->cancel(); app.dynamics_job.reset(); }
        app.dynamics_result.reset();
        app.dynamics_key.clear();
        app.dynamics_store_error.clear();
    };

    if (!visible) {
        // We know app.show_details was true when we called OpenPopup above
        // (that's the only way to reach this point), so if ImGui says the
        // popup isn't actually showing, our state has drifted from ImGui's
        // -- resync unconditionally rather than trusting `open`, which may
        // never have been written if BeginPopupModal bailed out early.
        app.show_details = false;
        cancel_running_analysis();
        close_preview();
        close_dynamics();
        return;
    }

    if (!open) {
        app.show_details = false;
        cancel_running_analysis();
        close_preview();
        close_dynamics();
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
    std::string copytext = selected_path ? selected_path->pathstring_verbose() : "";
    if (!copytext.empty() && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
        ImGui::SetClipboardText(copytext.c_str());

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
            if (app.analyze_job) {
                render_analyze_progress(app);
            } else if (app.viewed.status == store::RecordStatus::NotAnalyzed) {
                ImGui::TextUnformatted(
                    "After analyzing this song, paths will show up here.");
            } else if (app.viewed.status == store::RecordStatus::Stale) {
                ImGui::TextColored(
                    kWarningColor,
                    "This record is out of date. To make sure you have the latest "
                    "results, please re-analyze.");
            } else if (app.viewed.record->paths.empty()) {
                ImGui::TextUnformatted("No paths found.");
            } else {
                render_path_panel(app, selected_path);
            }
            ImGui::EndTabItem();
        }

        bool preview_shown = false;
        if (ImGui::BeginTabItem("Preview")) {
            preview_shown = true;
            render_preview_panel(app, selected_path);
            ImGui::EndTabItem();
        }
        // Pause playback whenever the Preview tab isn't the one on screen.
        if (!preview_shown && app.preview) app.preview->pause();

        if (ImGui::BeginTabItem("Dynamics")) {
            render_dynamics_panel(app);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndPopup();
}

}  // namespace hydra::ui
