#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "ui/app_state.h"
#include "ui/preview_controller.h"

namespace uitest {

namespace {

void test_scan(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    IM_CHECK_EQ(h.app->library_total, 0);
    scan_library(ctx);
    if (ctx->IsError()) return;
    // The first row's title is drawn in the table.
    const std::string& title = h.app->current_page.rows[0].title;
    IM_CHECK(visible_text(h).find(title) != std::string::npos);
    // The scan flipped the button to its rescan label and persisted that.
    IM_CHECK(h.app->settings.is_rescan);
    IM_CHECK(ctx->ItemInfo("Refresh scan").ID != 0);
}

// The View row's difficulty dropdown: it drives the chartmode everything else
// is keyed by, and it disables 2x Bass (an Expert-only charting concept)
// without forgetting the user's stored setting.
void test_difficulty(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");

    IM_CHECK(h.app->settings.view_bass2x);  // the default the test relies on
    IM_CHECK((ctx->ItemInfo("2x Bass").ItemFlags & ImGuiItemFlags_Disabled) == 0);

    ctx->ComboClick("##difficulty/Hard");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.view_difficulty == "Hard"; }, 5));
    IM_CHECK_STR_EQ(hydra::app::Settings::load_file(h.ini_path).view_difficulty.c_str(),
                    "Hard");
    IM_CHECK_STR_EQ(h.app->settings.chartmode_key().c_str(), "Hard Pro Drums, 1x Bass");

    // Visibly disabled, unchecked, and the stored flag is untouched.
    ImGuiTestItemInfo bass = ctx->ItemInfo("2x Bass");
    IM_CHECK((bass.ItemFlags & ImGuiItemFlags_Disabled) != 0);
    IM_CHECK(h.app->settings.view_bass2x);
    IM_CHECK(!h.app->settings.effective_bass2x());

    // Back on Expert the box is live again, still carrying the user's own
    // setting. (Checked here rather than at the end of the test: the details
    // modal opened below has no close button the harness can address.)
    ctx->ComboClick("##difficulty/Expert");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.view_difficulty == "Expert"; }, 5));
    IM_CHECK((ctx->ItemInfo("2x Bass").ItemFlags & ImGuiItemFlags_Disabled) == 0);
    IM_CHECK(h.app->settings.effective_bass2x());
    IM_CHECK_STR_EQ(h.app->settings.chartmode_key().c_str(), "Expert Pro Drums, 2x Bass");

    ctx->ComboClick("##difficulty/Hard");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.view_difficulty == "Hard"; }, 5));

    // Narrow to a chart that actually has a [HardDrums] section, so the
    // analysis below has notes to work with.
    ctx->ItemInputValue("##search", "Pokemon Theme");
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == "Pokemon Theme"; }, 5));
    IM_CHECK(wait_until(ctx, [&] { return !h.app->current_page.rows.empty(); }, 5));
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    // The details title carries the chartmode, so it names the difficulty.
    IM_CHECK(visible_text(h).find("Hard Pro Drums, 1x Bass") != std::string::npos);

    ctx->ItemClick("**/Analyze paths!");
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());
    IM_CHECK(!h.app->viewed.record->paths.empty());
    std::string best = h.app->viewed.record->best_path().pathstring();
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(best) != std::string::npos; }, 5));
    // The Hard record is filed under the Hard chartmode, so the library row
    // now reads Ready under it.
    IM_CHECK(h.app->current_page.summaries[0].state ==
             hydra::store::RecordStatus::Ready);

    // The Preview follows the selected difficulty: Hard's notes must load,
    // with no "Preview failed".
    ctx->ItemClick("**/Preview");
    IM_CHECK(wait_until(ctx, [&] { return h.app->preview && h.app->preview->active(); }, 10));
    IM_CHECK(wait_until(ctx, [&] { return !h.app->preview->loading(); }, 120));
    IM_CHECK_STR_EQ(h.app->preview->error().c_str(), "");
}

// A bad hydra_rules.ini: the app still opens and scans, the error naming the
// key stays on screen, and both Analyze buttons are disabled.
void test_rules_error(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h, "max_tied_paths = 0\n");
    IM_CHECK(h.app->analysis_blocked());
    IM_CHECK(h.app->rules_error.find("max_tied_paths") != std::string::npos);
    scan_library(ctx);
    if (ctx->IsError()) return;

    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("analysis is off") != std::string::npos;
    }, 5));
    IM_CHECK(visible_text(h).find("max_tied_paths") != std::string::npos);
    IM_CHECK((ctx->ItemInfo("Analyze library").ItemFlags & ImGuiItemFlags_Disabled) != 0);

    open_details(ctx, 0);
    if (ctx->IsError()) return;
    IM_CHECK((ctx->ItemInfo("**/Analyze paths!").ItemFlags & ImGuiItemFlags_Disabled) != 0);
    // The state refuses as well as the button.
    h.app->start_analyze();
    IM_CHECK(h.app->analyze_job == nullptr);
}

// The library view's own state (the search text, the dmleaderboards filter,
// the status fade, the folder confirm) belongs to the AppState. When it lived
// in function statics, the next test's fresh app still showed the last test's
// search text and filter.
void test_library_state_per_app(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    ctx->ItemInputValue("##search", "zzqx");
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == "zzqx"; }, 5));
    ctx->ItemClick("Compare dmleaderboards user...");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->dm_users.empty(); }, 10));
    ctx->SetRef("//Compare dmleaderboards user");
    ctx->ItemInputValue("##dmfilter", "zzqx");
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("alice") == std::string::npos);
    ctx->ItemClick("Close");
    ctx->Yield(2);

    // A fresh app: both boxes start empty. ImGui's text log shows an input
    // box's contents, so leftover text would be on screen.
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    IM_CHECK(h.app->search.empty());
    IM_CHECK(visible_text(h).find("zzqx") == std::string::npos);
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Compare dmleaderboards user...");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->dm_users.empty(); }, 10));
    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("alice") != std::string::npos;
    }, 5));
    IM_CHECK(visible_text(h).find("zzqx") == std::string::npos);
    ctx->SetRef("//Compare dmleaderboards user");
    ctx->ItemClick("Close");
    ctx->Yield(2);
}

// The View row and the library's own controls: Pro Drums, the page arrows,
// backing out of "Analyze library", and removing a song folder.
void test_view_settings(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");

    // The page arrows move one page each way.
    IM_CHECK(h.app->current_page.total_count > h.app->rows_per_page);  // 2+ pages
    IM_CHECK_EQ(h.app->table_viewpage, 0);
    ctx->ItemClick("##pageright");
    IM_CHECK_EQ(h.app->table_viewpage, 1);
    ctx->ItemClick("##pageleft");
    IM_CHECK_EQ(h.app->table_viewpage, 0);

    // Pro Drums off is a different chart mode: persisted at once, and the
    // library starts over at page one.
    ctx->ItemClick("##pageright");
    IM_CHECK_EQ(h.app->table_viewpage, 1);
    IM_CHECK(h.app->settings.view_prodrums);
    ctx->ItemClick("Pro Drums");
    IM_CHECK(!h.app->settings.view_prodrums);
    IM_CHECK(!hydra::app::Settings::load_file(h.ini_path).view_prodrums);
    IM_CHECK(h.app->settings.chartmode_key().find("Pro Drums") == std::string::npos);
    IM_CHECK_EQ(h.app->table_viewpage, 0);
    ctx->ItemClick("Pro Drums");
    IM_CHECK(h.app->settings.view_prodrums);

    // "Analyze library" asks first; Cancel starts nothing.
    ctx->ItemClick("Analyze library");
    ctx->SetRef("//Analyzing");
    IM_CHECK(visible_text(h).find("will be skipped") != std::string::npos);
    ctx->ItemClick("Cancel");
    ctx->Yield(2);
    IM_CHECK(h.app->batch_job == nullptr);
    IM_CHECK(!h.app->batch_confirm_pending);

    // Removing a song folder goes through a confirm. Cancel keeps it;
    // Remove drops it, persists that, and turns the scan button off.
    ctx->SetRef("//Hydra");
    IM_CHECK_EQ(h.app->settings.chartfolders.size(), (size_t)1);
    ctx->ItemClick("Manage folders... (1)");
    ctx->SetRef("//Song folders");
    ctx->ItemClick("**/X");
    ctx->SetRef("//Remove folder?");
    ctx->ItemClick("Cancel");
    ctx->Yield(2);
    IM_CHECK_EQ(h.app->settings.chartfolders.size(), (size_t)1);
    ctx->SetRef("//Song folders");
    ctx->ItemClick("**/X");
    ctx->SetRef("//Remove folder?");
    ctx->ItemClick("Remove");
    ctx->Yield(2);
    IM_CHECK(h.app->settings.chartfolders.empty());
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).chartfolders.empty());
    IM_CHECK(!h.app->settings.is_rescan);
    ctx->SetRef("//Song folders");
    ctx->ItemClick("Close");
    ctx->Yield(2);
    ctx->SetRef("//Hydra");
    IM_CHECK((ctx->ItemInfo("Scan charts").ItemFlags & ImGuiItemFlags_Disabled) != 0);
}

}  // namespace

const std::vector<TestEntry>& library_tests() {
    static const std::vector<TestEntry> entries = {
        {"scan", test_scan},
        {"difficulty", test_difficulty},
        {"rules-error", test_rules_error},
        {"library-state-per-app", test_library_state_per_app},
        {"view-settings", test_view_settings},
    };
    return entries;
}

}  // namespace uitest
