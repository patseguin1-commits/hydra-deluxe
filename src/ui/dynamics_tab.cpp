#include "ui/details_parts.h"

#include "app/dynamics_breakdown.h"
#include "core/model.h"  // group_thousands
#include "imgui.h"
#include "ui/dynamics_load_job.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace hydra::ui {

namespace {

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
    // The style's own disabled text grey: it reads on the dark panel.
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

}  // namespace

namespace detail {

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

}  // namespace detail

}  // namespace hydra::ui
