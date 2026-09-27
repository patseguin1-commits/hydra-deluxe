#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "uitest_harness.h"

#include "app/config.h"
#include "app/report_files.h"
#include "core/model.h"
#include "imgui_internal.h"
#include "ui/app_state.h"
#include "ui/win32_dialogs.h"

namespace fs = std::filesystem;

namespace uitest {

namespace {

// A child window of the main window, or null. Child window names are
// mangled, so go through WindowInfo; NoError because "not there" is a result.
ImGuiWindow* child_window(ImGuiTestContext* ctx, const char* path) {
    return ctx->WindowInfo(path, ImGuiTestOpFlags_NoError).Window;
}

// Scan testdata/input through the UI and land on the populated library.
// T9 merge-fix: the harness's scan_library still clicks the old "Scan charts"
// label and reads the old "chart(s) found" wording in this worktree; T9
// rewrites it to exactly this. After the wave-3 merge, call scan_library and
// delete this copy.
void scan_songs(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Scan library");
    IM_CHECK(wait_until(ctx, [&] { return h.app->scan_job && h.app->scan_job->snapshot().finished; }, 60));
    ctx->SetRef("//Scanning charts");
    IM_CHECK(h.app->scan_job->snapshot().charts_found > 0);
    ctx->ItemClick("Continue");
    ctx->Yield(2);
    ctx->SetRef("//Hydra");
    IM_CHECK(h.app->scan_job == nullptr);
    IM_CHECK(h.app->library_total > 0);
    IM_CHECK(!h.app->current_page.rows.empty());
}

// Narrow the library to charts matching `search` and batch them through the
// confirm; waits for the batch and its report to finish.
bool batch_search(ImGuiTestContext* ctx, const std::string& search) {
    Harness& h = harness(ctx);
    ctx->SetRef("//Hydra");
    ctx->ItemInputValue("**/##search", search.c_str());
    if (!wait_until(ctx, [&] { return h.app->search == search; }, 5)) return false;
    // T12 merge-fix: the count becomes h.app->library_match_count().
    const std::string label =
        "Analyze search (" + hydra::group_thousands(h.app->current_page.total_count) + ")...";
    ctx->ItemClick(label.c_str());
    ctx->SetRef("//Analyze library");
    ctx->ItemClick("Start analyzing");
    return wait_until(ctx, [&] {
               return h.app->batch_job && h.app->batch_job->snapshot().finished;
           }, 300) &&
           wait_until(ctx, [&] { return h.app->report_job && h.app->report_job->finished(); }, 60);
}

// Click the finished strip's X and wait for the strip to go.
void dismiss_done(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    IM_CHECK(wait_until(ctx, [&] { return child_window(ctx, "//Hydra/##batchdone") != nullptr; }, 5));
    ctx->SetRef(child_window(ctx, "//Hydra/##batchdone"));
    ctx->ItemClick("X##dismissdone");
    ctx->Yield(2);
    IM_CHECK(h.app->batch_job == nullptr);
    ctx->SetRef("//Hydra");
}

// The running strip shows the chart being worked on and live counts. Its Stop
// button and its width must not move as they change.
void test_batch_strip_drift(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_songs(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Analyze library...");
    ctx->SetRef("//Analyze library");
    ctx->ItemClick("Start analyzing");
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job && !h.app->batch_job->snapshot().preparing; }, 30));
    IM_CHECK(wait_until(ctx, [&] { return child_window(ctx, "//Hydra/##batchstrip") != nullptr; }, 5));
    ImGuiWindow* strip = child_window(ctx, "//Hydra/##batchstrip");
    ctx->SetRef(strip);
    const ImRect stop0 = ctx->ItemInfo("Stop").RectFull;
    const float width0 = strip->Size.x;
    int seen_titles = 0;
    std::string last_title;
    while (!h.app->batch_job->snapshot().finished && seen_titles < 6) {
        ctx->Yield();
        std::string t = h.app->batch_job->snapshot().current_title;
        if (!t.empty() && t != last_title) { last_title = t; ++seen_titles; }
        strip = child_window(ctx, "//Hydra/##batchstrip");
        if (!strip) break;  // the run finished between the two looks
        ctx->SetRef(strip);
        ImGuiTestItemInfo stop = ctx->ItemInfo("Stop", ImGuiTestOpFlags_NoError);
        if (stop.ID == 0) break;
        IM_CHECK_FLOAT_NEAR_EQ(stop.RectFull.Min.x, stop0.Min.x, 0.01f);
        IM_CHECK_FLOAT_NEAR_EQ(stop.RectFull.Min.y, stop0.Min.y, 0.01f);
        IM_CHECK_FLOAT_NEAR_EQ(strip->Size.x, width0, 0.01f);
    }
    IM_CHECK(seen_titles >= 2);  // the loop actually saw titles change
    h.app->batch_job->stop();
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job->snapshot().finished; }, 300));
    dismiss_done(ctx);
}

void test_settings_and_reports(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_songs(ctx);
    if (ctx->IsError()) return;

    // A view-setting toggle lands in the INI at once.
    bool before = h.app->settings.view_bass2x;
    ctx->ItemClick("2x Bass");
    ctx->Yield();
    IM_CHECK(h.app->settings.view_bass2x != before);
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).view_bass2x != before);
    ctx->ItemClick("2x Bass");  // restore

    // Batch-analyze just the first chart (search narrows the batch), which
    // builds the path report; with auto-open off no browser is launched.
    // T12 merge-fix: search for the title in quotes ("\"" + title + "\""),
    // so the new word search matches it as one phrase.
    std::string title = h.app->current_page.rows[0].title;
    IM_CHECK(batch_search(ctx, title));
    IM_CHECK(h.app->report_job->ok());
    IM_CHECK(hydra::app::report_file_exists());
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)0);
    ctx->SetRef(child_window(ctx, "//Hydra/##batchdone"));
    ctx->ItemClick("Open report");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);
    dismiss_done(ctx);

    // dmleaderboards comparison against the canned API.
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Compare with dmleaderboards...");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->dm_users.empty(); }, 10));
    IM_CHECK(visible_text(h).find("alice") != std::string::npos);
    ctx->SetRef("//Compare dmleaderboards user");
    ctx->ItemClick("**/###111");
    IM_CHECK(wait_until(ctx, [&] { return h.app->dm_report_job && h.app->dm_report_job->finished(); }, 60));
    IM_CHECK_STR_EQ(h.app->dm_report_job->error().c_str(), "");
    IM_CHECK(fs::exists(hydra::app::dm_report_html_path()));
    IM_CHECK_EQ(h.app->dm_report_job->stats().matched, 1);
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);  // still: auto-open is off
}

// The dmleaderboards comparison, end to end: the button off away from Clone
// Hero's cap and Expert, the name filter, and every button of the finished
// report.
void test_dm_compare_flow(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_songs(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");

    // The ladder plays by Clone Hero's rules at Expert. Any other cap or
    // difficulty disables the button.
    auto compare_disabled = [&] {
        return (ctx->ItemInfo("Compare with dmleaderboards...").ItemFlags &
                ImGuiItemFlags_Disabled) != 0;
    };
    h.app->settings.sp_cap = 8;
    h.app->commit_settings();
    ctx->Yield(2);
    IM_CHECK(compare_disabled());
    h.app->settings.sp_cap = 4;
    h.app->commit_settings();

    ctx->ComboClick("##difficulty/Hard");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.view_difficulty == "Hard"; }, 5));
    ctx->Yield(2);
    IM_CHECK(compare_disabled());
    ctx->ComboClick("##difficulty/Expert");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.view_difficulty == "Expert"; }, 5));
    ctx->Yield(2);
    IM_CHECK(!compare_disabled());

    // The picker opens on the canned ladder; the filter narrows it as you
    // type, ignoring case.
    ctx->ItemClick("Compare with dmleaderboards...");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->dm_users.empty(); }, 10));
    ctx->SetRef("//Compare dmleaderboards user");
    ctx->ItemInputValue("##dmfilter", "");  // an earlier test may have left text
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find("alice") != std::string::npos; }, 5));
    ctx->ItemInputValue("##dmfilter", "bob");
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("alice") == std::string::npos);
    ctx->ItemInputValue("##dmfilter", "ALI");
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("alice") != std::string::npos);

    // Pick alice: the report builds, the choice is remembered, and with
    // auto-open off nothing opens until asked.
    ctx->ItemClick("**/###111");
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->dm_report_job && h.app->dm_report_job->finished();
    }, 60));
    IM_CHECK(h.app->dm_report_job->ok());
    IM_CHECK(visible_text(h).find("not analyzed") != std::string::npos);
    IM_CHECK_STR_EQ(h.app->settings.dm_last_user.c_str(), "111");
    IM_CHECK_STR_EQ(hydra::app::Settings::load_file(h.ini_path).dm_last_user.c_str(), "111");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)0);
    // "again" only once it has really opened.
    IM_CHECK(!ctx->ItemExists("Open report again"));
    ctx->ItemClick("Open report");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);
    IM_CHECK(h.opened_urls[0] == hydra::app::dm_report_html_path());
    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists("Open report again"));

    // Compare another: back to the list, the finished report dropped.
    ctx->ItemClick("Compare another");
    ctx->Yield(2);
    IM_CHECK(h.app->dm_report_job == nullptr);
    IM_CHECK(h.app->dm_picker_open);
    IM_CHECK(visible_text(h).find("Pick a player") != std::string::npos);

    // Close: the picker goes away.
    ctx->ItemClick("Close");
    ctx->Yield(2);
    IM_CHECK(!h.app->dm_picker_open);
}

// The path report's buttons: the toolbar's "Open path report" appears once a
// report exists, "Open automatically" persists and then opens the next report
// by itself, and "Also re-analyze" re-analyzes a stored chart.
void test_report_buttons(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_songs(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    IM_CHECK(!ctx->ItemExists("**/Open path report"));  // no report built yet

    // Batch just the first chart (the search narrows the batch).
    std::string title = h.app->current_page.rows[0].title;
    IM_CHECK(batch_search(ctx, title));
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)0);  // auto-open is off

    // Tick "Open automatically" in the finished strip: it persists at once.
    ctx->SetRef(child_window(ctx, "//Hydra/##batchdone"));
    ctx->ItemClick("Open automatically");
    IM_CHECK(h.app->settings.auto_open_report);
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).auto_open_report);
    dismiss_done(ctx);

    // The toolbar now offers the report, and opens it on a click.
    ctx->SetRef("//Hydra");
    IM_CHECK(wait_until(ctx, [&] { return ctx->ItemExists("**/Open path report"); }, 5));
    ctx->ItemClick("**/Open path report");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);
    IM_CHECK(h.opened_urls[0] == hydra::app::report_html_path());

    // "Also re-analyze" re-analyzes the stored chart, and the confirm says so.
    const std::string label =
        "Analyze search (" + hydra::group_thousands(h.app->current_page.total_count) + ")...";
    ctx->ItemClick(label.c_str());
    ctx->Yield(2);
    ctx->SetRef("//Analyze library");
    ctx->ItemCheck("Also re-analyze charts that already have a result##redo");
    IM_CHECK(h.app->batch_redo);
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("Also re-analyze") != std::string::npos);
    IM_CHECK(visible_text(h).find("re-analyzing 1 that already has a result") != std::string::npos);
    ctx->ItemClick("Cancel");
    ctx->Yield(2);
    IM_CHECK(!h.app->batch_confirm_pending);
    IM_CHECK(batch_search(ctx, title));
    IM_CHECK_EQ(h.app->batch_job->snapshot().skipped, 0);  // nothing skipped: redone

    // With auto-open on, the new report opened by itself.
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)2);
    dismiss_done(ctx);
    h.app->batch_redo = false;
}

// The confirm lists every setting and the real count; Escape backs out,
// Enter starts, and the batch runs in the strip, not a modal.
void test_batch_confirm(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_songs(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Analyze library...");
    ctx->Yield(2);
    IM_CHECK(h.app->batch_confirm_pending);
    std::string text = visible_text(h);
    IM_CHECK(text.find("Analyze 97 charts that have no result yet?") != std::string::npos);
    IM_CHECK(text.find("Expert \xC2\xB7 Pro Drums \xC2\xB7 2x Bass") != std::string::npos);
    IM_CHECK(text.find("4 bars (Clone Hero's rule)") != std::string::npos);
    IM_CHECK(text.find("2 scores") != std::string::npos);
    IM_CHECK(text.find("10 ms") != std::string::npos);
    ctx->SetRef("//Analyze library");
    // Nothing has a result yet, so there is nothing to re-analyze.
    IM_CHECK((ctx->ItemInfo("Also re-analyze charts that already have a result##redo").ItemFlags &
              ImGuiItemFlags_Disabled) != 0);

    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    IM_CHECK(!h.app->batch_confirm_pending);
    IM_CHECK(h.app->batch_job == nullptr);

    ctx->SetRef("//Hydra");
    ctx->ItemClick("Analyze library...");
    ctx->Yield(2);
    ctx->KeyPress(ImGuiKey_Enter);
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job != nullptr; }, 5));
    IM_CHECK(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));  // no modal while it runs
    IM_CHECK(wait_until(ctx, [&] { return child_window(ctx, "//Hydra/##batchstrip") != nullptr; }, 10));
    h.app->batch_job->stop();
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job->snapshot().finished; }, 300));
    dismiss_done(ctx);
}

// Pause holds the run, Resume carries on, Stop ends it and keeps what's done.
void test_batch_pause_stop(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_songs(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Analyze library...");
    ctx->SetRef("//Analyze library");
    ctx->ItemClick("Start analyzing");
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->batch_job && !h.app->batch_job->snapshot().preparing;
    }, 30));
    IM_CHECK(wait_until(ctx, [&] { return child_window(ctx, "//Hydra/##batchstrip") != nullptr; }, 5));
    ctx->SetRef(child_window(ctx, "//Hydra/##batchstrip"));
    ctx->ItemClick("Pause");
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job->snapshot().paused; }, 5));
    ctx->Yield(2);
    IM_CHECK(ctx->ItemExists("Resume"));
    ctx->ItemClick("Resume");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->batch_job->snapshot().paused; }, 5));
    ctx->Yield(2);
    ctx->ItemClick("Stop");
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job->snapshot().finished; }, 300));
    IM_CHECK(wait_until(ctx, [&] { return child_window(ctx, "//Hydra/##batchdone") != nullptr; }, 5));
    IM_CHECK(visible_text(h).find("Stopped:") != std::string::npos);
    ctx->Yield(5);
    IM_CHECK(h.app->report_job == nullptr);  // a stopped run builds no report
    dismiss_done(ctx);
}

// The finished strip: where the report went, Open report, Show in folder.
void test_batch_done_strip(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    // The seam records into a static list, so a failed check that returns
    // early leaves nothing dangling; it is removed again at the end.
    static std::vector<std::wstring> shown;
    shown.clear();
    hydra::ui::set_show_in_folder([](const std::wstring& p) {
        shown.push_back(p);
        return true;
    });
    scan_songs(ctx);
    if (ctx->IsError()) return;
    IM_CHECK(batch_search(ctx, "Burnout"));
    IM_CHECK(h.app->report_job->ok());
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)0);  // auto-open is off
    ImGuiWindow* done = child_window(ctx, "//Hydra/##batchdone");
    IM_CHECK(done != nullptr);
    IM_CHECK(visible_text(h).find("Path report saved to") != std::string::npos);
    ctx->SetRef(done);
    ctx->ItemClick("Open report");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);
    IM_CHECK(h.opened_urls[0] == h.app->report_job->saved_path().wstring());
    ctx->ItemClick("Show in folder");
    IM_CHECK_EQ(shown.size(), (size_t)1);
    IM_CHECK(shown[0] == h.app->report_job->saved_path().wstring());
    ctx->ItemClick("X##dismissdone");
    ctx->Yield(3);
    IM_CHECK(h.app->batch_job == nullptr);
    ctx->SetRef("//Hydra");
    IM_CHECK(wait_until(ctx, [&] { return ctx->ItemExists("**/Open path report"); }, 5));
    hydra::ui::set_show_in_folder({});
}

// A browser that refuses is not a failed report: the page is saved, and the
// strip says only the opening failed.
void test_batch_open_failure(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    h.app->settings.auto_open_report = true;
    h.app->commit_settings();
    hydra::app::set_open_in_browser([](const std::wstring&) { return false; });
    scan_songs(ctx);
    if (ctx->IsError()) return;
    IM_CHECK(batch_search(ctx, "Burnout"));
    IM_CHECK(h.app->report_job->ok());
    IM_CHECK(!h.app->report_job->opened());
    IM_CHECK(visible_text(h).find("Report saved, but Windows couldn't open it in your browser.") !=
             std::string::npos);
    // reset_app reinstalls the recording seam for the next test.
}

// News fades; a problem stays until dismissed.
void test_status_line(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    ctx->SetRef("//Hydra");
    h.app->set_status("Saved the thing.");
    ctx->Yield(2);
    IM_CHECK(h.frame_text.text.find("Saved the thing.") != std::string::npos);
    ctx->Yield(400);  // over 6 s of 1/60 s frames
    IM_CHECK(h.frame_text.text.find("Saved the thing.") == std::string::npos);

    h.app->set_problem("The thing broke.");
    ctx->Yield(400);
    IM_CHECK(h.frame_text.text.find("The thing broke.") != std::string::npos);
    ctx->ItemClick("**/X##dismissstatus");
    ctx->Yield(2);
    IM_CHECK(h.app->status_message.empty());
}

// The comparison only means something at Clone Hero's cap and Expert; off
// either, the button is disabled (with the reason on hover) instead of
// posting a refusal after the click.
void test_compare_disabled(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_songs(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    auto disabled = [&] {
        return (ctx->ItemInfo("Compare with dmleaderboards...").ItemFlags &
                ImGuiItemFlags_Disabled) != 0;
    };
    IM_CHECK(!disabled());
    h.app->settings.sp_cap = 6;
    h.app->commit_settings();
    ctx->Yield(2);
    IM_CHECK(disabled());
    h.app->settings.sp_cap = 4;
    h.app->settings.view_difficulty = "Hard";
    h.app->commit_settings();
    ctx->Yield(2);
    IM_CHECK(disabled());
    h.app->settings.view_difficulty = "Expert";
    h.app->commit_settings();
    ctx->Yield(2);
    IM_CHECK(!disabled());
}

// Escape closes Song folders; Enter continues a finished scan.
void test_dialog_keys(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    ctx->SetRef("//Hydra");
    // By ID, not by name: outside a frame there is no current window for
    // ImGui::IsPopupOpen(const char*) to hash the name against.
    const ImGuiID folders_popup = ctx->GetID("//Hydra/Song folders");
    ctx->ItemClick("Manage folders... (1)");
    ctx->Yield(2);
    IM_CHECK(ImGui::IsPopupOpen(folders_popup, ImGuiPopupFlags_AnyPopupLevel));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    IM_CHECK(!ImGui::IsPopupOpen(folders_popup, ImGuiPopupFlags_AnyPopupLevel));

    ctx->ItemClick("Scan library");
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->scan_job && h.app->scan_job->snapshot().finished;
    }, 60));
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(2);
    IM_CHECK(h.app->scan_job == nullptr);
    IM_CHECK(h.app->library_total > 0);
}

}  // namespace

const std::vector<TestEntry>& batch_report_tests() {
    static const std::vector<TestEntry> entries = {
        // Named "batch-strip-drift" in the plan. uitest_tests.cpp's fixed run
        // order still lists "batch-modal-drift" (and aborts on a name it
        // can't find), so the entry keeps that name until the merge renames
        // both together.
        {"batch-modal-drift", test_batch_strip_drift},
        {"settings-and-reports", test_settings_and_reports},
        {"dm-compare-flow", test_dm_compare_flow},
        {"report-buttons", test_report_buttons},
        {"batch-confirm", test_batch_confirm},
        {"batch-pause-stop", test_batch_pause_stop},
        {"batch-done-strip", test_batch_done_strip},
        {"batch-open-failure", test_batch_open_failure},
        {"status-line", test_status_line},
        {"compare-disabled", test_compare_disabled},
        {"dialog-keys", test_dialog_keys},
    };
    return entries;
}

}  // namespace uitest
