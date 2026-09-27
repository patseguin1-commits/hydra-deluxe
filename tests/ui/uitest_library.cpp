#include <cstring>
#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "ui/app_state.h"
#include "ui/library_model.h"
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
    const std::string& title = h.app->library_row_at(0).title;
    IM_CHECK(visible_text(h).find(title) != std::string::npos);
    // The scan flipped the button to its rescan label and persisted that.
    IM_CHECK(h.app->settings.is_rescan);
    IM_CHECK(ctx->ItemInfo("Scan library").ID != 0);
}

// The settings bar's difficulty dropdown: it drives the chartmode everything
// else is keyed by, and it disables 2x Bass (an Expert-only charting concept)
// without forgetting the user's stored setting.
void test_difficulty(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef(ctx->WindowInfo("//Hydra/##settingsbar").Window);

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
    ctx->SetRef("//Hydra");
    ctx->ItemInputValue("**/##search", "Pokemon Theme");
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == "Pokemon Theme"; }, 5));
    IM_CHECK(wait_until(ctx, [&] { return h.app->library_shown_count() > 0; }, 5));
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    // The settings bar, not the panel, names the difficulty now.
    IM_CHECK_STR_EQ(h.app->settings.chartmode_key().c_str(), "Hard Pro Drums, 1x Bass");

    ctx->ItemClick(analyze_button_ref(h).c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());
    IM_CHECK(!h.app->viewed.record->paths.empty());
    std::string best = h.app->viewed.record->best_path().pathstring();
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(best) != std::string::npos; }, 5));
    // The Hard record is filed under the Hard chartmode, so the library row
    // now reads Ready under it.
    IM_CHECK(h.app->library_row_at(0).status ==
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
    IM_CHECK((ctx->ItemInfo(analyze_button_ref(h).c_str()).ItemFlags & ImGuiItemFlags_Disabled) != 0);
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
    ctx->ItemInputValue("**/##search", "zzqx");
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

// The View row and the library's own controls: Pro Drums, backing out of
// "Analyze library", and removing a song folder.
void test_view_settings(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");

    // Pro Drums off is a different chart mode: persisted at once.
    IM_CHECK(h.app->settings.view_prodrums);
    ctx->ItemClick("**/Pro Drums");
    IM_CHECK(!h.app->settings.view_prodrums);
    IM_CHECK(!hydra::app::Settings::load_file(h.ini_path).view_prodrums);
    IM_CHECK(h.app->settings.chartmode_key().find("Pro Drums") == std::string::npos);
    ctx->ItemClick("**/Pro Drums");
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
    IM_CHECK((ctx->ItemInfo("Scan library").ItemFlags & ImGuiItemFlags_Disabled) != 0);
}

// The search box, the chips, the second line and the empty state, on the
// scratch library (97 charts in testdata\input).
void test_library_search(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    const auto shown = [&] { return h.app->library_shown_count(); };

    IM_CHECK_EQ(shown(), (size_t)97);
    IM_CHECK(visible_text(h).find("97 charts") != std::string::npos);
    IM_CHECK(ctx->ItemExists("**/All (97)##chipall"));
    IM_CHECK(ctx->ItemExists("**/Not analyzed (97)##chipnew"));
    IM_CHECK((ctx->ItemInfo("**/Stale (0)##chipstale").ItemFlags & ImGuiItemFlags_Disabled) != 0);

    // A quoted phrase: the five charts under "...\Tier 4".
    ctx->ItemInputValue("**/##search", "\"tier 4\"");
    IM_CHECK(wait_until(ctx, [&] { return shown() == 5; }, 5));
    IM_CHECK(ctx->ItemExists("**/All (5)##chipall"));
    std::string text = visible_text(h);
    IM_CHECK(text.find("5 of 97 charts") != std::string::npos);
    for (const char* title : {"Burnout", "Chair", "Limb From Limb", "Unbound (The Wild Ride)", "YYZ"})
        IM_CHECK(text.find(title) != std::string::npos);

    // With a song open, Folder makes way, so each row says where it matched.
    h.app->select(h.app->library_row_at(0).entry);
    IM_CHECK(wait_until(ctx, [&] { return !ctx->ItemExists("**/Folder"); }, 5));
    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("Matched on folder.") != std::string::npos;
    }, 5));
    h.app->close_details();
    IM_CHECK(wait_until(ctx, [&] { return ctx->ItemExists("**/Folder"); }, 5));

    // Words in any order, across title and artist.
    ctx->ItemInputValue("**/##search", "green burnout");
    IM_CHECK(wait_until(ctx, [&] { return shown() == 1; }, 5));
    IM_CHECK(h.app->library_row_at(0).title == "Burnout");

    // A charter stored with colour tags is found and drawn without them.
    // Bloodline charted 15 of the scratch charts (13 of them tagged), so
    // every row shown is theirs, Acid Romance among them.
    ctx->ItemInputValue("**/##search", "bloodline");
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == "bloodline" && shown() > 1; }, 5));
    bool acid_shown = false;
    for (size_t i = 0; i < shown(); ++i) {
        IM_CHECK(h.app->library_row_at(i).charter == "Bloodline");
        acid_shown = acid_shown || h.app->library_row_at(i).title == "Acid Romance";
    }
    IM_CHECK(acid_shown);
    text = visible_text(h);
    IM_CHECK(text.find("Acid Romance") != std::string::npos);
    IM_CHECK(text.find("Bloodline") != std::string::npos);
    IM_CHECK(text.find("<color=") == std::string::npos);

    // A filter it can't read says so under the box.
    ctx->ItemInputValue("**/##search", "stars:9");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->library.query().errors.empty(); }, 5));
    IM_CHECK(visible_text(h).find(h.app->library.query().errors[0]) != std::string::npos);

    // Nothing matches: the empty state, and Clear search brings it all back.
    ctx->ItemInputValue("**/##search", "zzqx");
    IM_CHECK(wait_until(ctx, [&] { return shown() == 0; }, 5));
    IM_CHECK(visible_text(h).find("No charts match your search.") != std::string::npos);
    ctx->ItemClick("**/Clear search");
    IM_CHECK(wait_until(ctx, [&] { return shown() == 97 && h.app->search.empty(); }, 5));

    // Escape in the box clears it; a second Escape leaves the box.
    ctx->ItemInputValue("**/##search", "chair");
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == "chair"; }, 5));
    ctx->ItemClick("**/##search");
    ctx->KeyPress(ImGuiKey_Escape);
    IM_CHECK(wait_until(ctx, [&] { return h.app->search.empty(); }, 5));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);

    // Ctrl+F puts the cursor in the box.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_F);
    ctx->Yield(2);
    IM_CHECK_EQ(ctx->UiContext->ActiveId, ctx->ItemInfo("**/##search").ID);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
}

// Sorting and scrolling: every chart is one scroll away, and only the rows on
// screen are drawn.
void test_library_sort_scroll(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");

    IM_CHECK(h.app->library.sort_column() == hydra::ui::LibrarySort::Title);
    IM_CHECK(h.app->library.ascending());
    const size_t count = h.app->library_shown_count();
    const std::string first = h.app->library_row_at(0).title;
    const std::string last = h.app->library_row_at(count - 1).title;
    IM_CHECK(visible_text(h).find(first) != std::string::npos);
    IM_CHECK(visible_text(h).find("Not analyzed") != std::string::npos);
    IM_CHECK(visible_text(h).find("-----") == std::string::npos);  // no filler rows

    // The table scrolls like any list: its end brings the last title into
    // view. (The harness's text log turns the row clipper off, so every row
    // is submitted while testing; "in view" is judged by the row's place
    // against the table's visible area instead of by what was drawn.)
    ImGuiWindow* table = nullptr;
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if ((w->Flags & ImGuiWindowFlags_ChildWindow) && std::strstr(w->Name, "##librarytable"))
            table = w;
    IM_CHECK(table != nullptr);
    if (table == nullptr) return;
    const std::string last_ref = "**/" + escape_ref(last);
    const auto last_in_view = [&] {
        ImGuiTestItemInfo info = ctx->ItemInfo(last_ref.c_str(), ImGuiTestOpFlags_NoError);
        return info.ID != 0 && table->InnerRect.Contains(info.RectFull.GetCenter());
    };
    IM_CHECK(!last_in_view());  // below the fold
    ctx->ScrollToBottom(ImGuiTestRef(table->ID));
    IM_CHECK(wait_until(ctx, last_in_view, 5));

    // The Title header reverses the order; a second click restores it (the
    // ImGui context, and so the table's sort, outlives this test's app).
    ctx->ItemClick("**/Title");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->library.ascending(); }, 5));
    IM_CHECK(h.app->library_row_at(0).title == last);
    ctx->ItemClick("**/Title");
    IM_CHECK(wait_until(ctx, [&] { return h.app->library.ascending(); }, 5));
    IM_CHECK(h.app->library_row_at(0).title == first);
}

}  // namespace

const std::vector<TestEntry>& library_tests() {
    static const std::vector<TestEntry> entries = {
        {"scan", test_scan},
        {"difficulty", test_difficulty},
        {"rules-error", test_rules_error},
        {"library-state-per-app", test_library_state_per_app},
        {"view-settings", test_view_settings},
        {"library-search", test_library_search},
        {"library-sort-scroll", test_library_sort_scroll},
    };
    return entries;
}

}  // namespace uitest
