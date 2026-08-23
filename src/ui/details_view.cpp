#include "ui/details_view.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/path_view.h"
#include "core/winstr.h"
#include "imgui.h"
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

    // The missing-file warning line needs one more row when it shows.
    float panel_h = px(200.0f);
    if (!file_ok) panel_h += ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("controls", ImVec2(0, panel_h), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("More Paths settings");

    ImGui::TextUnformatted("Score range:");
    ImGui::SameLine(px(100));
    ImGui::SetNextItemWidth(px(140));
    if (ImGui::InputInt("##depthvalue", &app.settings.depth_value)) {
        if (app.settings.depth_value < 0) app.settings.depth_value = 0;
        app.commit_settings();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(80));
    int mode_idx = app.settings.depth_mode;
    const char* modes[] = {"scores", "points"};
    if (ImGui::Combo("##depthmode", &mode_idx, modes, 2)) {
        app.settings.depth_mode = mode_idx;
        app.commit_settings();
    }

    ImGui::TextUnformatted("Limit timings:");
    ImGui::SameLine(px(100));
    if (ImGui::Checkbox("##mslimit", &app.settings.mslimit_enabled)) app.commit_settings();
    ImGui::SameLine();
    bool mslimit_disabled = !app.settings.mslimit_enabled;
    begin_disabled_input(mslimit_disabled);
    ImGui::SetNextItemWidth(px(100));
    if (ImGui::InputInt("##mslimitvalue", &app.settings.mslimit_value)) {
        app.settings.mslimit_value = std::clamp(app.settings.mslimit_value, -200, 200);
        app.commit_settings();
    }
    ImGui::SameLine();
    // "mslimit_mstext" binds disabled_text ((50,50,50), same gray as the
    // InputInt's own disabled Text color) whenever mslimit is unchecked.
    ImGui::TextUnformatted("ms");
    end_disabled_input(mslimit_disabled);

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
            app.commit_settings();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("bars");
        end_disabled_input(spcap_auto);
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto##spcapauto", &spcap_auto)) {
            app.settings.sp_cap = spcap_auto ? std::nullopt : std::optional<int>(last_cap);
            app.commit_settings();
        }
        hint("4 bars is Clone Hero's rule. Higher caps are what-ifs; Auto raises the "
             "cap until the score stops improving.");
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
    bool analyze_disabled = !file_ok || (app.analyze_job && !app.analyze_job->finished());
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
            static_cast<double>(settings.hit_window_ms));

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
        ImGui::SeparatorText("Best All-0 Path (Limit timings: 0 ms)");
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

void render_analyze_progress(AppState& app) {
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

    AnalyzeJob* job = app.analyze_job.get();
    if (generation.changed(app.analyze_generation)) {
        done_at = -1.0;
        stored = false;
        store_error.clear();
    }
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
    } else if (job->is_cancelled()) {
        // Cancelled runs have nothing to show or store.
        ImGui::EndChild();
        app.analyze_job.reset();
        return;
    } else if (!job->ok()) {
        ImGui::TextColored(kWarningColor, "An error occurred:");
        ImGui::TextWrapped("%s", job->error().c_str());
        if (ImGui::Button("Continue")) app.analyze_job.reset();
    } else {
        if (!stored) {
            stored = true;
            // Persistence belongs to AppState, not to a draw call; the view
            // only shows the outcome.
            store_error = app.store_finished_analysis();
            if (store_error.empty()) done_at = ImGui::GetTime();
        }

        if (!store_error.empty()) {
            ImGui::TextColored(kWarningColor, "An error occurred:");
            ImGui::TextWrapped("%s", store_error.c_str());
            if (ImGui::Button("Continue")) app.analyze_job.reset();
        } else {
            ImGui::TextUnformatted("Done!");
            if (done_at >= 0 && ImGui::GetTime() - done_at > 0.5) app.analyze_job.reset();
        }
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
    pc->open(*app.selected, app.settings.view_prodrums, app.settings.view_bass2x,
             selected_path);
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

    // Transport row: play/pause, a scrubber, and the time readout.
    if (ImGui::Button(pc->playing() ? "Pause" : "Play")) pc->toggle();
    ImGui::SameLine();

    // The clock drives the scrubber, so a chart with no audio still scrubs.
    double len_ms = pc->length_ms();
    float pos_s = static_cast<float>(pc->position_ms() / 1000.0);
    float len_s = static_cast<float>(len_ms / 1000.0);
    const float volume_w = px(110.0f);
    ImGui::SetNextItemWidth(std::max(
        px(120.0f), ImGui::GetContentRegionAvail().x - px(150.0f) - volume_w - px(60.0f)));
    if (ImGui::SliderFloat("##scrub", &pos_s, 0.0f, len_s > 0.0f ? len_s : 1.0f, "%.1fs"))
        pc->seek_ms(static_cast<double>(pos_s) * 1000.0);
    ImGui::SameLine();
    ImGui::Text("%.1f / %.1f s", pos_s, len_s);

    // Volume: applied live and remembered in the settings file.
    ImGui::SameLine();
    ImGui::TextUnformatted("Vol");
    ImGui::SameLine();
    int volume = app.settings.preview_volume;
    ImGui::SetNextItemWidth(volume_w);
    if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d%%")) {
        app.settings.preview_volume = volume;
        pc->set_volume(volume);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) app.commit_settings();

    // Highway viewport: size the offscreen target to the remaining region.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = static_cast<int>(avail.x);
    int h = static_cast<int>(avail.y);
    ID3D11ShaderResourceView* srv = pc->render(w, h);
    if (srv != nullptr && w > 0 && h > 0) {
        ImGui::Image((ImTextureID)(intptr_t)srv,
                     ImVec2(static_cast<float>(w), static_cast<float>(h)));

        // The time box, drawn over the image the way Onyx draws its own
        // (top-left, monospace, on a translucent dark panel): time,
        // measure:beat, BPM.
        hydra::app::PreviewTimeBox box = pc->time_box();
        const char* lines[3] = {box.timestamp.c_str(), box.measure_beat.c_str(),
                                box.bpm.c_str()};
        ImFont* font = g_mono_font ? g_mono_font : ImGui::GetFont();
        const float size = px(15.0f);
        const float margin = px(10.0f);
        const float pad = px(8.0f);
        ImVec2 origin = ImGui::GetItemRectMin();
        float text_w = 0.0f;
        for (const char* s : lines)
            text_w = std::max(text_w, font->CalcTextSizeA(size, FLT_MAX, 0.0f, s).x);
        const float line_h = size * 1.25f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 box_min(origin.x, origin.y);
        ImVec2 box_max(origin.x + margin + text_w + pad * 2.0f,
                       origin.y + margin + line_h * 3.0f + pad);
        dl->AddRectFilled(box_min, box_max, IM_COL32(0, 0, 0, 128), px(6.0f),
                          ImDrawFlags_RoundCornersBottomRight);
        for (int i = 0; i < 3; ++i)
            dl->AddText(font, size, ImVec2(origin.x + margin, origin.y + margin + line_h * i),
                        IM_COL32(255, 255, 255, 255), lines[i]);
    }
}

}  // namespace

void render_details_modal(AppState& app) {
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

    if (!visible) {
        // We know app.show_details was true when we called OpenPopup above
        // (that's the only way to reach this point), so if ImGui says the
        // popup isn't actually showing, our state has drifted from ImGui's
        // -- resync unconditionally rather than trusting `open`, which may
        // never have been written if BeginPopupModal bailed out early.
        app.show_details = false;
        cancel_running_analysis();
        close_preview();
        return;
    }

    if (!open) {
        app.show_details = false;
        cancel_running_analysis();
        close_preview();
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

        ImGui::EndTabBar();
    }

    ImGui::EndPopup();
}

}  // namespace hydra::ui
