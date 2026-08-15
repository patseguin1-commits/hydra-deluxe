#include "ui/details_view.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/edition.h"
#include "imgui.h"
#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace hydra::ui {

namespace {

bool file_exists_utf8(const std::string& utf8_path) {
    int wlen =
        MultiByteToWideChar(CP_UTF8, 0, utf8_path.data(), (int)utf8_path.size(), nullptr, 0);
    std::wstring wpath((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_path.data(), (int)utf8_path.size(), &wpath[0], wlen);
    return GetFileAttributesW(wpath.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string measurestr(const Timecode& tc) {
    const int64_t* mbt = tc.measure_beats_ticks();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "m%lld.%lld.%lld", (long long)mbt[0] + 1,
                 (long long)mbt[1] + 1, (long long)mbt[2]);
    return buf;
}

// The record/star/pencil/hash marker before each song-info line, matching
// hydra_app.py's dpg.add_image icons. Falls back to a plain filled circle in
// the icon's tint color when the texture failed to load (see icons.h -- a
// missing/unreadable resource/*.png is best-effort, not fatal). Advances the
// cursor and leaves the line open for more text.
void icon_marker(ImTextureID icon, const ImVec4& fallback_color) {
    float size = 28.0f;
    if (icon != 0) {
        ImGui::Image(icon, ImVec2(size, size));
    } else {
        float r = 7.0f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(p.x + r, p.y + r), r, ImGui::ColorConvertFloat4ToU32(fallback_color));
        ImGui::Dummy(ImVec2(size, size));
    }
    ImGui::SameLine();
}

void render_song_info(AppState& app, float width) {
    ImGui::BeginChild("songinfo", ImVec2(width, 170), ImGuiChildFlags_Borders);

    icon_marker(g_icon_record, ImVec4(0.85f, 0.1f, 0.1f, 1.0f));
    ImGui::PushFont(nullptr, 24.0f);
    ImGui::TextUnformatted(app.selected->title.c_str());
    ImGui::PopFont();

    icon_marker(g_icon_star, kAccentColor);
    ImGui::PushFont(nullptr, 24.0f);
    ImGui::TextUnformatted(app.selected->artist.c_str());
    ImGui::PopFont();

    icon_marker(g_icon_pencil, ImVec4(0.9f, 0.75f, 0.1f, 1.0f));
    ImGui::PushFont(nullptr, 24.0f);
    ImGui::TextUnformatted(app.selected->charter.empty() ? "(unknown charter)"
                                                          : app.selected->charter.c_str());
    ImGui::PopFont();

    icon_marker(g_icon_hash, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", app.selected->md5.c_str());
    ImGui::EndChild();
}

// A placeholder for future per-song analysis/traits, mirroring
// songdetails_songanalysis's "/// Space for future stuff! ///" -- reserved by
// the Python UI but never filled in; kept here for the same reason (and so
// the panel layout matches).
void render_song_analysis_placeholder(float width) {
    ImGui::BeginChild("songanalysis", ImVec2(width, 170), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("/// Space for future stuff! ///");
    ImGui::EndChild();
}

void render_controls(AppState& app) {
    // The uncapped edition adds an SP-cap row, so it needs a taller panel.
    ImGui::BeginChild("controls", ImVec2(0, kUncapped ? 200.0f : 170.0f),
                      ImGuiChildFlags_Borders);
    ImGui::SeparatorText("More Paths settings");

    ImGui::TextUnformatted("Score range:");
    ImGui::SameLine(100);
    ImGui::SetNextItemWidth(140);
    if (ImGui::InputInt("##depthvalue", &app.settings.depth_value)) {
        if (app.settings.depth_value < 0) app.settings.depth_value = 0;
        app.settings.save();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    int mode_idx = app.settings.depth_mode;
    const char* modes[] = {"scores", "points"};
    if (ImGui::Combo("##depthmode", &mode_idx, modes, 2)) {
        app.settings.depth_mode = mode_idx;
        app.settings.save();
    }

    ImGui::TextUnformatted("Limit timings:");
    ImGui::SameLine(100);
    if (ImGui::Checkbox("##mslimit", &app.settings.mslimit_enabled)) app.settings.save();
    ImGui::SameLine();
    bool mslimit_disabled = !app.settings.mslimit_enabled;
    begin_disabled_input(mslimit_disabled);
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("##mslimitvalue", &app.settings.mslimit_value)) {
        app.settings.mslimit_value = std::clamp(app.settings.mslimit_value, -200, 200);
        app.settings.save();
    }
    ImGui::SameLine();
    // "mslimit_mstext" binds disabled_text ((50,50,50), same gray as the
    // InputInt's own disabled Text color) whenever mslimit is unchecked.
    ImGui::TextUnformatted("ms");
    end_disabled_input(mslimit_disabled);

    // Uncapped edition only: a manual SP meter ceiling in bars. Unchecked runs
    // the auto-settling ladder (default); checked forces the given cap, and the
    // backend accepts any value (analyze_chart -> analyze_at_cap).
    if (kUncapped) {
        ImGui::TextUnformatted("SP cap:");
        ImGui::SameLine(100);
        if (ImGui::Checkbox("##spcap", &app.settings.sp_cap_enabled)) app.settings.save();
        ImGui::SameLine();
        bool spcap_disabled = !app.settings.sp_cap_enabled;
        begin_disabled_input(spcap_disabled);
        ImGui::SetNextItemWidth(100);
        if (ImGui::InputInt("##spcapvalue", &app.settings.sp_cap_value)) {
            if (app.settings.sp_cap_value < 1) app.settings.sp_cap_value = 1;
            app.settings.save();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("bars");
        end_disabled_input(spcap_disabled);
    }

    ImGui::Spacing();
    bool file_ok = file_exists_utf8(app.selected->notespath);
    bool analyze_disabled = !file_ok || (app.analyze_job && !app.analyze_job->finished());
    begin_disabled_button(analyze_disabled);
    if (ImGui::Button(file_ok ? "Analyze paths!" : "Song file not found. Try scanning again.",
                      ImVec2(-1, 40))) {
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

void render_path_details(const Path* path, const HydraRecord& record) {
    ImGui::PushFont(nullptr, 0.0f);  // default font for the button, like Python's MainFont
    if (ImGui::Button("Copy path string", ImVec2(180, 30)))
        ImGui::SetClipboardText(path->pathstring_verbose().c_str());
    ImGui::PopFont();
    ImGui::Spacing();

    if (begin_section("Multiplier squeezes")) {
        if (path->multsqueezes.empty()) {
            ImGui::TextDisabled("None.");
        } else {
            for (const MultSqueeze& msq : path->multsqueezes) {
                std::string label = msq.notationstr() + "   (+" + std::to_string(msq.points()) +
                                    " pts):   " + msq.chord().rowstr();
                ImGui::PushID(&msq);
                if (ImGui::TreeNode(label.c_str())) {
                    ImGui::PushFont(g_mono_font, 0.0f);
                    ImGui::TextUnformatted(msq.howto().c_str());
                    ImGui::PopFont();
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        end_section();
    }

    if (begin_section("Activations")) {
        if (!path->has_activations()) {
            ImGui::TextDisabled("None.");
        } else {
            for (const Activation& act : path->all_activations()) {
                // Mirrors hydra_app.py:951-953 exactly, including the literal
                // tabs: f"{notationstr:6}({sp_meter} SP)\t{measurestr:>9}" and
                // (when difficult) f"\t{ms:7.1f}ms". The header renders in
                // MonoFont (begin_section pushed g_mono_font), and ImGui's '\t'
                // is a fixed 4-space advance (IM_TABSIZE) shared with DearPyGui,
                // so the columns line up identically to the Python app.
                std::string ntn = act.notationstr();
                std::string meas = act.timecode ? measurestr(*act.timecode) : "";
                char hbuf[128];
                std::snprintf(hbuf, sizeof(hbuf), "%-6s(%d SP)\t%9s", ntn.c_str(),
                             act.sp_meter.value_or(0), meas.c_str());
                std::string header = hbuf;
                if (auto ms = act.difficulty()) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "\t%7.1fms", *ms);
                    header += buf;
                }
                bool difficult = act.is_difficult();
                if (difficult) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
                bool open = ImGui::TreeNode(header.c_str());
                if (difficult) ImGui::PopStyleColor();

                if (open) {
                    if (act.is_e_critical()) {
                        char buf[96];
                        std::snprintf(buf, sizeof(buf), "Calibration fill: %.1fms (%s)",
                                     *act.e_offset, act.is_E0() ? "required" : "optional");
                        ImGui::TextUnformatted(buf);
                    }

                    ImGui::Text("Frontend: %s",
                               act.chord ? act.chord->rowstr().c_str() : "None");

                    for (const SPSqueeze& sq : act.sqinouts) {
                        if (sq.is_difficult()) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
                        ImGui::TextUnformatted(sq.description().c_str());
                        if (sq.is_difficult()) ImGui::PopStyleColor();
                    }

                    std::vector<BackendSqueeze> backends = act.display_backends();
                    if (backends.empty()) {
                        ImGui::TextUnformatted("Backends: None.");
                    } else {
                        ImGui::TextUnformatted("Backends:");
                        if (ImGui::BeginTable("backends", 4,
                                              ImGuiTableFlags_Borders |
                                                  ImGuiTableFlags_Resizable |
                                                  ImGuiTableFlags_SizingFixedFit)) {
                            ImGui::TableSetupColumn("Timing", ImGuiTableColumnFlags_WidthFixed,
                                                    80);
                            ImGui::TableSetupColumn("Chord", ImGuiTableColumnFlags_WidthFixed,
                                                    80);
                            ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed,
                                                    80);
                            ImGui::TableSetupColumn("Rating", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableHeadersRow();

                            for (const BackendSqueeze& bsq : backends) {
                                bool squeezed = act.is_sqout_backend(bsq);
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%.1f", bsq.offset_ms.value_or(0.0));
                                ImGui::TableSetColumnIndex(1);
                                ImGui::TextUnformatted(bsq.chord.notationstr().c_str());
                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("%d", squeezed ? bsq.sqout_points : bsq.points);
                                ImGui::TableSetColumnIndex(3);
                                std::string text = bsq.summarystr();
                                if (squeezed) {
                                    char extra[48];
                                    std::snprintf(extra, sizeof(extra),
                                                 " <-- squeezed out (-%d)",
                                                 bsq.points - bsq.sqout_points);
                                    text += extra;
                                    ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
                                }
                                ImGui::TextUnformatted(text.c_str());
                                if (squeezed) ImGui::PopStyleColor();
                            }
                            ImGui::EndTable();
                        }
                    }
                    ImGui::Spacing();
                    ImGui::TreePop();
                }
            }
        }

        ImGui::Text("Leftover SP: %d.", path->leftover_sp);

        // Which SP ceiling this result was found under -- only meaningful in the
        // uncapped edition, where the ceiling approximates "no cap" rather than
        // being the rule. Mirrors hydra_app.py:1017-1031 (warning-colored when
        // the ladder ran out of time before the score settled).
        if (kUncapped && record.sp_cap) {
            if (record.sp_cap_converged) {
                ImGui::Text("SP meter: %d bars; raising it further stopped changing "
                            "the score.",
                            *record.sp_cap);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
                ImGui::Text("SP meter: %d bars. The search ran out of time before the "
                            "score settled, so a higher meter may still score more.",
                            *record.sp_cap);
                ImGui::PopStyleColor();
            }
        }

        if (path->skipped_accents > 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
            ImGui::Text("This path has %d skipped (unhittable) accent%s!", path->skipped_accents,
                       path->skipped_accents == 1 ? "" : "s");
            ImGui::PopStyleColor();
        }
        if (path->skipped_ghosts > 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
            ImGui::Text("This path has %d skipped (unhittable) ghost%s!", path->skipped_ghosts,
                       path->skipped_ghosts == 1 ? "" : "s");
            ImGui::PopStyleColor();
        }
        end_section();
    }

    if (begin_section("Score breakdown")) {
        // hydra_app.py:1044 formats the average as (str(avg_mult()) + "000")[:5]
        // -- a string slice, which TRUNCATES to three decimals rather than
        // rounding (%.3f would round). %.10f gives a long-enough decimal
        // expansion; slicing its first five chars reproduces Python exactly.
        char avgbuf[32];
        std::snprintf(avgbuf, sizeof(avgbuf), "%.10f", path->avg_mult());
        std::string avgs = (std::string(avgbuf) + "000").substr(0, 5);
        ImGui::Text("Avg. Multiplier:      %sx", avgs.c_str());
        // Leading '\n' on Notes and Total Score reproduces the blank lines
        // hydra_app.py:1046,1058 add; Python uses no separator between them.
        ImGui::Text("\nNotes:            %10s", group_thousands(path->score_base).c_str());
        ImGui::Text("Combo Bonus:      %10s", group_thousands(path->score_combo).c_str());
        ImGui::Text("Star Power:       %10s", group_thousands(path->score_sp).c_str());
        ImGui::Text("Solo Bonus:       %10s", group_thousands(path->score_solo).c_str());
        ImGui::Text("Accent Notes:     %10s", group_thousands(path->score_accents).c_str());
        ImGui::Text("Ghost Notes:      %10s", group_thousands(path->score_ghosts).c_str());
        ImGui::Text("\nTotal Score:      %10s", group_thousands(path->totalscore()).c_str());
        end_section();
    }
}

// Path list on the left + details on the right, grouped into score tiers.
// selected_path/last_generation are the render function's own statics, passed
// by reference so this stays a free function instead of a lambda closure.
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
        ImGui::TableSetupColumn("diff", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool is_selected = (p == selected_path);
        if (ImGui::Selectable(p->pathstring().c_str(), is_selected,
                              ImGuiSelectableFlags_SpanAllColumns))
            selected_path = p;

        if (auto diff = p->difficulty()) {
            ImGui::TableSetColumnIndex(1);
            bool warn = *diff > 2.0;
            if (warn) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
            ImGui::Text("%9.1f ms", *diff);
            if (warn) ImGui::PopStyleColor();
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
// dpg.add_tree_node(label=f"{current_score:,}") grouping.
void render_path_panel(AppState& app, const Path*& selected_path) {
    ImGui::BeginChild("pathlist", ImVec2(600, 0), ImGuiChildFlags_Borders);
    std::vector<const Path*> flat = app.viewed_record->all_paths();

    int64_t current_score = INT64_MIN;
    int tier = 0;
    bool tree_open = false;
    for (const Path* p : flat) {
        if (p->totalscore() != current_score) {
            if (tree_open) ImGui::TreePop();

            ++tier;
            if (tier == 1) {
                ImGui::SeparatorText("Optimal Path");
            } else if (tier == 2) {
                std::string label = "More Paths";
                if (app.viewed_record->ms_limit)
                    label += " (Limit timings: " + std::to_string(*app.viewed_record->ms_limit) +
                             " ms)";
                ImGui::SeparatorText(label.c_str());
            }
            current_score = p->totalscore();

            ImGui::PushID(tier);
            ImGui::PushFont(g_mono_font, 0.0f);  // score header, like Python's MonoFont tree node
            tree_open =
                ImGui::TreeNodeEx(group_thousands(current_score).c_str(),
                                  ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopFont();
            ImGui::PopID();
        }

        if (tree_open) render_path_row(p, selected_path);
    }
    if (tree_open) ImGui::TreePop();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("pathdetails", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (selected_path) render_path_details(selected_path, *app.viewed_record);
    ImGui::EndChild();
}

void render_analyze_progress(AppState& app) {
    static AnalyzeJob* last_job = nullptr;
    static double done_at = -1.0;
    static bool stored = false;
    static std::string store_error;

    AnalyzeJob* job = app.analyze_job.get();
    if (job != last_job) {
        last_job = job;
        done_at = -1.0;
        stored = false;
        store_error.clear();
    }
    if (!job) return;

    ImGui::BeginChild("analyzeprogress", ImVec2(0, 140), ImGuiChildFlags_Borders);

    if (!job->finished()) {
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
    } else if (!job->ok()) {
        ImGui::TextColored(kWarningColor, "An error occurred:");
        ImGui::TextWrapped("%s", job->error().c_str());
        if (ImGui::Button("Continue")) app.analyze_job.reset();
    } else {
        if (!stored) {
            stored = true;
            try {
                app::AnalysisResult result = job->take_result();
                app.store->add_song(app.selected->md5, app.selected->title, app.selected->artist,
                                    app.selected->charter, result.song);
                app.store->add_record(app.selected->md5, app.settings.chartmode_key(),
                                      result.record);
                app.refresh_viewed_record();
                done_at = ImGui::GetTime();
            } catch (const std::exception& e) {
                store_error = std::string("Analyzed, but saving failed: ") + e.what();
            }
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

}  // namespace

void render_details_modal(AppState& app) {
    static bool prev_open = false;
    static const Path* selected_path = nullptr;
    static int last_generation = -1;

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
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 40, viewport->WorkPos.y + 40),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x - 80, viewport->WorkSize.y - 80), ImGuiCond_Always);
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
    if (!visible) {
        // We know app.show_details was true when we called OpenPopup above
        // (that's the only way to reach this point), so if ImGui says the
        // popup isn't actually showing, our state has drifted from ImGui's
        // -- resync unconditionally rather than trusting `open`, which may
        // never have been written if BeginPopupModal bailed out early.
        app.show_details = false;
        return;
    }

    if (!open) {
        app.show_details = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    if (app.record_generation != last_generation) {
        last_generation = app.record_generation;
        selected_path = app.viewed_record && !app.viewed_record->paths.empty()
                            ? &app.viewed_record->best_path()
                            : nullptr;
    }

    // Song info / analysis-placeholder panels scale with the popup's width
    // (itself sized off the viewport, see below) instead of a fixed pixel
    // width, so the controls panel isn't squeezed into negative space on a
    // narrower window.
    float avail_w = ImGui::GetContentRegionAvail().x;
    float side_w = std::max(260.0f, avail_w * 0.24f);
    ImGui::BeginGroup();
    render_song_info(app, side_w);
    ImGui::SameLine();
    render_song_analysis_placeholder(side_w);
    ImGui::SameLine();
    render_controls(app);
    ImGui::EndGroup();

    ImGui::Separator();

    if (app.analyze_job) {
        render_analyze_progress(app);
    } else if (!app.viewed_record) {
        ImGui::TextUnformatted("After analyzing this song, paths will show up here.");
    } else if (app.viewed_record->paths.empty()) {
        ImGui::TextColored(
            kWarningColor,
            "This record is out of date. To make sure you have the latest results, "
            "please re-analyze.");
    } else {
        render_path_panel(app, selected_path);
    }

    ImGui::EndPopup();
}

}  // namespace hydra::ui
