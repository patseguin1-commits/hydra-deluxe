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
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
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
    ImGui::BeginChild("songanalysis", ImVec2(width, px(170)), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Stored result");

    if (!app.viewed_record) {
        ImGui::TextDisabled("Not analyzed yet.");
    } else if (app.viewed_record->paths.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
        ImGui::TextWrapped("Stale: analyzed by an older Hydra version or a different "
                           "edition. Re-analyze to refresh it.");
        ImGui::PopStyleColor();
    } else {
        const HydraRecord& rec = *app.viewed_record;
        ImGui::Text("Best score:  %s", group_thousands(rec.best_path().totalscore()).c_str());
        ImGui::Text("Paths kept:  %d", (int)rec.all_paths().size());
        if (rec.ms_limit)
            ImGui::Text("Limit timings:  %d ms", (int)*rec.ms_limit);
        else
            ImGui::TextUnformatted("Limit timings:  off");
        if (kUncapped && rec.sp_cap) ImGui::Text("SP meter:  %d bars", *rec.sp_cap);
    }
    ImGui::EndChild();
}

void render_controls(AppState& app) {
    bool file_ok = file_exists_utf8(app.selected->notespath);

    // The uncapped edition adds an SP-cap row, so it needs a taller panel;
    // the missing-file warning line needs one more row when it shows.
    float panel_h = px(kUncapped ? 200.0f : 170.0f);
    if (!file_ok) panel_h += ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("controls", ImVec2(0, panel_h), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("More Paths settings");

    ImGui::TextUnformatted("Score range:");
    ImGui::SameLine(px(100));
    ImGui::SetNextItemWidth(px(140));
    if (ImGui::InputInt("##depthvalue", &app.settings.depth_value)) {
        if (app.settings.depth_value < 0) app.settings.depth_value = 0;
        app.save_settings();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(80));
    int mode_idx = app.settings.depth_mode;
    const char* modes[] = {"scores", "points"};
    if (ImGui::Combo("##depthmode", &mode_idx, modes, 2)) {
        app.settings.depth_mode = mode_idx;
        app.save_settings();
    }

    ImGui::TextUnformatted("Limit timings:");
    ImGui::SameLine(px(100));
    if (ImGui::Checkbox("##mslimit", &app.settings.mslimit_enabled)) app.save_settings();
    ImGui::SameLine();
    bool mslimit_disabled = !app.settings.mslimit_enabled;
    begin_disabled_input(mslimit_disabled);
    ImGui::SetNextItemWidth(px(100));
    if (ImGui::InputInt("##mslimitvalue", &app.settings.mslimit_value)) {
        app.settings.mslimit_value = std::clamp(app.settings.mslimit_value, -200, 200);
        app.save_settings();
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
        ImGui::SameLine(px(100));
        if (ImGui::Checkbox("##spcap", &app.settings.sp_cap_enabled)) app.save_settings();
        ImGui::SameLine();
        bool spcap_disabled = !app.settings.sp_cap_enabled;
        begin_disabled_input(spcap_disabled);
        ImGui::SetNextItemWidth(px(100));
        if (ImGui::InputInt("##spcapvalue", &app.settings.sp_cap_value)) {
            if (app.settings.sp_cap_value < 1) app.settings.sp_cap_value = 1;
            app.save_settings();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("bars");
        end_disabled_input(spcap_disabled);
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

// |r - 1| below this: the frontend transfer scale is not worth a warning.
constexpr double kTransferWarnBand = 0.05;

void render_path_details(const Path* path, const HydraRecord& record,
                         const SongTiming* timing) {
    static double copied_at = -1.0;
    ImGui::PushFont(nullptr, 0.0f);  // default font for the button, like Python's MainFont
    if (ImGui::Button("Copy path string", ImVec2(px(180), px(30)))) {
        ImGui::SetClipboardText(path->pathstring_verbose().c_str());
        copied_at = ImGui::GetTime();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Ctrl+C also copies the selected path");
    if (copied_at >= 0.0 && ImGui::GetTime() - copied_at < 2.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("Copied!");
    }
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

                    std::vector<BackendSqueeze> backends = act.display_backends();

                    // SP length is measure-based, so frontend timing error
                    // reaches the SP end scaled by the ratio of local measure
                    // durations -- warn when that ratio breaks the 1:1 the
                    // raw backend ms imply. Late (+) and early (-) hits can
                    // scale differently when the activation or SP end sits
                    // exactly on a meter/tempo change.
                    std::optional<TransferScale> scale;
                    if (timing) scale = frontend_transfer_scale(act, *timing);
                    bool late_warns = scale && std::abs(scale->late - 1.0) > kTransferWarnBand;
                    bool early_warns = scale && std::abs(scale->early - 1.0) > kTransferWarnBand;
                    if (scale) {
                        bool late_relevant = false, early_relevant = false;
                        for (const BackendSqueeze& bsq : backends) {
                            if (!bsq.offset_ms) continue;
                            if (act.is_sqout_backend(bsq)) early_relevant = true;
                            else if (*bsq.offset_ms > 2.0) late_relevant = true;
                        }
                        for (const SPSqueeze& sq : act.sqinouts)
                            if (sq.kind == SqueezeKind::SqOut) early_relevant = true;

                        bool show_late = late_warns && late_relevant;
                        bool show_early = early_warns && early_relevant;
                        if (show_late || show_early) {
                            char buf[192];
                            if (show_late && show_early &&
                                std::abs(scale->late - scale->early) > 0.005) {
                                std::snprintf(buf, sizeof(buf),
                                             "Frontend timing scales x%.2f (late) / x%.2f "
                                             "(early) at the SP end.",
                                             scale->late, scale->early);
                            } else if (show_late && show_early) {
                                // Both directions apply and are (nearly) equal.
                                std::snprintf(buf, sizeof(buf),
                                             "Frontend timing scales x%.2f to the SP end: "
                                             "10ms at the frontend moves the SP end %s%.1fms.",
                                             scale->late,
                                             scale->late < 1.0 ? "only " : "",
                                             10.0 * scale->late);
                            } else {
                                double r = show_late ? scale->late : scale->early;
                                std::snprintf(buf, sizeof(buf),
                                             "Frontend timing scales x%.2f to the SP end: "
                                             "%s at the frontend moves the SP end %s%s%.1fms.",
                                             r,
                                             show_late ? "+10ms (late)" : "-10ms (early)",
                                             r < 1.0 ? "only " : "",
                                             show_late ? "+" : "-", 10.0 * r);
                            }
                            ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
                            // Wrap at the panel edge -- the details child can
                            // be narrower than the line.
                            ImGui::PushTextWrapPos(0.0f);
                            ImGui::TextUnformatted(buf);
                            ImGui::PopTextWrapPos();
                            ImGui::PopStyleColor();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                                ImGui::SetTooltip(
                                    "SP length is measured in measures, so frontend timing\n"
                                    "reaches the SP end scaled by the measure-length ratio.\n"
                                    "Early and late hits scale differently when the activation\n"
                                    "or SP end sits exactly on a signature or tempo change.");
                        }
                    }

                    for (const SPSqueeze& sq : act.sqinouts) {
                        if (sq.is_difficult()) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
                        ImGui::TextUnformatted(sq.description().c_str());
                        if (sq.is_difficult()) ImGui::PopStyleColor();
                    }

                    if (backends.empty()) {
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

                            for (const BackendSqueeze& bsq : backends) {
                                bool squeezed = act.is_sqout_backend(bsq);

                                // The scale that governs this row: sqout rows
                                // need an early frontend, positive rows a late
                                // one. eff maps the row's raw ms onto the
                                // nominal 140ms budget the ratings assume
                                // (the real combined budget is 70*(1+r)).
                                std::optional<double> eff;
                                double row_r = 1.0;
                                if (scale && bsq.offset_ms) {
                                    bool applies = false;
                                    if (squeezed) {
                                        row_r = scale->early;
                                        applies = early_warns;
                                    } else if (*bsq.offset_ms > 2.0) {
                                        row_r = scale->late;
                                        applies = late_warns;
                                    }
                                    if (applies)
                                        eff = std::abs(*bsq.offset_ms) * 2.0 / (1.0 + row_r);
                                }

                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%.1f", bsq.offset_ms.value_or(0.0));
                                if (eff && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                                    ImGui::SetTooltip(
                                        "Effectively %.1fms on the normal 140ms scale:\n"
                                        "frontend timing scales x%.2f here, so the combined\n"
                                        "squeeze budget is %.0fms, not 140ms.",
                                        *eff, row_r, 70.0 * (1.0 + row_r));
                                ImGui::TableSetColumnIndex(1);
                                ImGui::TextUnformatted(bsq.chord.notationstr().c_str());
                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("%d", squeezed ? bsq.sqout_points : bsq.points);
                                ImGui::TableSetColumnIndex(3);
                                std::string text = bsq.summarystr();
                                if (eff) {
                                    char effbuf[32];
                                    std::snprintf(effbuf, sizeof(effbuf), " (eff. %.1fms)",
                                                 *eff);
                                    text += effbuf;
                                }
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
                ImGui::Text("SP meter: %d bars.", *record.sp_cap);
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
        ImGui::TableSetupColumn("diff", ImGuiTableColumnFlags_WidthFixed, px(130.0f));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        float path_avail = ImGui::GetContentRegionAvail().x;
        bool is_selected = (p == selected_path);
        if (ImGui::Selectable(p->pathstring().c_str(), is_selected,
                              ImGuiSelectableFlags_SpanAllColumns))
            selected_path = p;
        // Long paths clip at the column edge; offer the full string on hover
        // (only over the path column -- the ms column speaks for itself).
        if (ImGui::TableGetHoveredColumn() == 0 &&
            ImGui::CalcTextSize(p->pathstring().c_str()).x > path_avail)
            overflow_tooltip(p->pathstring().c_str());

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

// The all-0 path section, appended below the generated list. It is only worth
// showing when the generated list does not already contain that path: the main
// search keeps paths by score band, so the all-0 path usually falls below the
// band, but on an easy chart it can be high up or even optimal.
void render_allzero_section(const HydraRecord& record,
                            const std::vector<const Path*>& flat,
                            const Path*& selected_path) {
    std::vector<const Path*> allzero = record.all_allzero_paths();
    if (allzero.empty()) return;

    // Already in the generated list? Same score and same notation is the same
    // path. Score alone would hide a genuinely different all-0 path that ties
    // some listed path, and notation alone would hide one that reads the same
    // but banks differently.
    for (const Path* z : allzero) {
        const int64_t score = z->totalscore();
        const std::string notation = z->pathstring();
        for (const Path* p : flat)
            if (p->totalscore() == score && p->pathstring() == notation) return;
    }

    const int64_t score = allzero.front()->totalscore();
    std::string label = group_thousands(score);
    // The first thing a user asks of this row is what it costs against the
    // optimal path, so answer it in the header.
    if (!flat.empty()) {
        const int64_t delta = score - flat.front()->totalscore();
        if (delta != 0)
            label += "   (" + std::string(delta > 0 ? "+" : "-") +
                     group_thousands(delta < 0 ? -delta : delta) + ")";
    }

    ImGui::SeparatorText("Best All-0 Path (Limit timings: 0 ms)");
    ImGui::PushID("allzero");
    ImGui::PushFont(g_mono_font, 0.0f);
    bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopFont();
    if (open) {
        for (const Path* p : allzero) render_path_row(p, selected_path);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

// The path list: every unique score along the traversal gets its own
// default-open tree node labeled with the comma-grouped score, and every tied
// path at that score is listed inside it -- mirrors hydra_app.py's
// dpg.add_tree_node(label=f"{current_score:,}") grouping.
void render_path_panel(AppState& app, const Path*& selected_path) {
    ImGui::BeginChild("pathlist", ImVec2(px(600), 0), ImGuiChildFlags_Borders);
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

    render_allzero_section(*app.viewed_record, flat, selected_path);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("pathdetails", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (selected_path)
        render_path_details(selected_path, *app.viewed_record,
                           app.viewed_timing ? &*app.viewed_timing : nullptr);
    ImGui::EndChild();
}

void render_analyze_progress(AppState& app) {
    // Keyed on the job generation, not the job's address: a freed AnalyzeJob's
    // block can be handed straight back to the next make_unique, and a pointer
    // compare then carries `stored`/`done_at` over from the previous job --
    // the fresh result is never stored and the stale done_at dismisses the
    // modal on its first finished frame.
    static int last_generation = -1;
    static double done_at = -1.0;
    static bool stored = false;
    static std::string store_error;

    AnalyzeJob* job = app.analyze_job.get();
    if (app.analyze_generation != last_generation) {
        last_generation = app.analyze_generation;
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
            // An uncapped run can take minutes; the user needs an out that
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
            try {
                // Store against the identity the job snapshotted at start --
                // NOT app.selected, which can point at a different song by now
                // (close the modal mid-analysis, click another row).
                const store::ChartLibraryEntry& song = job->song();
                app::AnalysisResult result = job->take_result();
                app.store->add_song(song.md5, song.title, song.artist, song.charter,
                                    result.song);
                app.store->add_record(song.md5, job->chartmode(), result.record);
                app.refresh_viewed_record();
                app.refresh_page();  // the library row's Best Path cell is cached per page
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
    // otherwise keep burning CPU (up to the full uncapped budget) invisibly.
    // The main window reaps the job once the cancel lands.
    auto cancel_running_analysis = [&app] {
        if (app.analyze_job && !app.analyze_job->finished()) app.analyze_job->cancel();
    };

    if (!visible) {
        // We know app.show_details was true when we called OpenPopup above
        // (that's the only way to reach this point), so if ImGui says the
        // popup isn't actually showing, our state has drifted from ImGui's
        // -- resync unconditionally rather than trusting `open`, which may
        // never have been written if BeginPopupModal bailed out early.
        app.show_details = false;
        cancel_running_analysis();
        return;
    }

    if (!open) {
        app.show_details = false;
        cancel_running_analysis();
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

    // Ctrl+C copies the selected path (the long-documented shortcut behind
    // appstate.current_path_copytext, finally wired) -- unless a text input
    // has focus, which keeps its own copy behavior.
    app.current_path_copytext = selected_path ? selected_path->pathstring_verbose() : "";
    if (!app.current_path_copytext.empty() && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
        ImGui::SetClipboardText(app.current_path_copytext.c_str());

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
