#include "ui/details_parts.h"

#include "core/model.h"  // group_thousands
#include "core/stars.h"
#include "imgui.h"

#include <cstdint>

namespace hydra::ui::detail {

// ---- Stars tab -------------------------------------------------------------

// Clone Hero's star cutoffs for this chart. Every number is star_cutoffs()'s,
// read from the record's best path (the base score and solo bonus are the
// same on every path); this only draws them. The states before the table are
// render_record_state's, shared with the Paths tab.
void render_stars_panel(AppState& app) {
    if (!render_record_state(
            app, "After analyzing this song, star cutoffs will show up here."))
        return;

    const StarCutoffs sc = star_cutoffs(app.viewed.record->best_path());
    const bool has_solo = sc.solo_bonus > 0;

    ImGui::Text("Base score: %s", group_thousands(sc.base).c_str());
    if (has_solo)
        ImGui::Text("Solo bonus: %s (not counted toward stars)",
                    group_thousands(sc.solo_bonus).c_str());
    ImGui::Spacing();

    const int table_flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable("##startable", has_solo ? 4 : 3, table_flags)) {
        ImGui::TableSetupColumn("Stars");
        ImGui::TableSetupColumn("Multiplier");
        ImGui::TableSetupColumn("Cutoff");
        if (has_solo) ImGui::TableSetupColumn("With full solo bonus");
        ImGui::TableHeadersRow();

        for (int stars = 1; stars <= kMaxStars; ++stars) {
            const int64_t cutoff = sc.cutoffs[stars - 1];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", stars);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", static_cast<double>(kStarMultipliers[stars - 1]));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(group_thousands(cutoff).c_str());
            if (has_solo) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(group_thousands(cutoff + sc.solo_bonus).c_str());
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(
        "A star counts once your score, without the solo bonus, reaches its cutoff.");
}

}  // namespace hydra::ui::detail
