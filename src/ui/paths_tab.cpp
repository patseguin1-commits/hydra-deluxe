#include "ui/details_parts.h"

#include "app/path_view.h"
#include "core/model.h"
#include "imgui.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <string>
#include <vector>

namespace hydra::ui {

namespace {

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

void render_multsqueeze_section(const std::vector<app::MultSqueezeView>& squeezes) {
    if (begin_section("Multiplier squeezes")) {
        if (squeezes.empty()) {
            ImGui::TextDisabled("None.");
        } else {
            for (size_t i = 0; i < squeezes.size(); ++i) {
                const app::MultSqueezeView& msq = squeezes[i];
                // Keyed on the index, not the element address: the cached vector
                // is rebuilt whenever the path changes, so its addresses move.
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

void render_activations_section(const app::ActivationsView& view) {
    if (begin_section("Activations")) {
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

void render_score_breakdown_section(const std::vector<std::string>& breakdown) {
    if (begin_section("Score breakdown")) {
        for (const std::string& line : breakdown) ImGui::TextUnformatted(line.c_str());
        end_section();
    }
}

// `copied_at` (when "Copied!" last flashed) is owned by AppState's
// DetailsViewState and passed by reference, so it dies with the app state.
void render_path_details(const Path* path, const HydraRecord& record,
                         const app::PathsTabCache::Details& details, double& copied_at) {
    ImGui::PushFont(nullptr, 0.0f);  // default font for the button, like Python's MainFont
    if (ImGui::Button("Copy path string", ImVec2(px(180), px(30)))) {
        ImGui::SetClipboardText(path->pathstring_verbose(record.multsqueezes).c_str());
        copied_at = ImGui::GetTime();
    }
    hint("Ctrl+C also copies the selected path");
    if (copied_at >= 0.0 && ImGui::GetTime() - copied_at < 2.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("Copied!");
    }
    ImGui::PopFont();
    ImGui::Spacing();

    render_multsqueeze_section(details.squeezes);
    render_activations_section(details.activations);
    render_score_breakdown_section(details.breakdown);
}

// Path list on the left + details on the right, grouped into score tiers.
// selected_path/record_watcher live in AppState's DetailsViewState, passed by
// reference so this stays a free function instead of a lambda closure.
// One selectable path row: pathstring on the left, difficulty (ms) right-
// aligned, warning-colored when the path is difficult. Mirrors the two-column
// dpg.table row hydra_app.py builds per path.
void render_path_row(const Path* p, const app::PathsTabCache::Row& row,
                     const Path*& selected_path) {
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
        if (row_selectable(row.label.c_str(), p == selected_path))
            selected_path = p;

        if (!row.cell.ms.empty()) {
            ImGui::TableSetColumnIndex(1);
            if (row.cell.warn) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
            ImGui::TextUnformatted(row.cell.ms.c_str());
            if (row.cell.warn) ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

}  // namespace

namespace detail {

// The path list: every unique score along the traversal gets its own
// default-open tree node labeled with the comma-grouped score, and every tied
// path at that score is listed inside it -- mirrors hydra_app.py's
// dpg.add_tree_node(label=f"{current_score:,}") grouping. The grouping (and
// the all-0 section's visibility rule) comes from app::build_path_list.
void render_path_panel(AppState& app, const Path*& selected_path) {
    app::PathsTabCache& cache = app.details_ui.paths_tab;
    ImGui::BeginChild("pathlist", ImVec2(px(600), 0), ImGuiChildFlags_Borders);
    const app::PathListView& list = cache.list(*app.viewed.record, app.record_generation.n);

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
            for (const Path* p : group.paths) render_path_row(p, cache.row(p), selected_path);
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
            for (const Path* p : list.allzero) render_path_row(p, cache.row(p), selected_path);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("pathdetails", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (selected_path) {
        const app::PathsTabCache::Details& details = cache.details(
            *selected_path, *app.viewed.record, app.record_generation.n,
            app.viewed.timing ? &*app.viewed.timing : nullptr,
            static_cast<double>(app.settings.hit_window_ms), app.settings.backend_limit(),
            app.settings.rules);
        render_path_details(selected_path, *app.viewed.record, details, app.details_ui.copied_at);
    }
    ImGui::EndChild();
}

}  // namespace detail

}  // namespace hydra::ui
