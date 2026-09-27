#include <cstdio>
#include <filesystem>
#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "app/report_files.h"
#include "ui/app_state.h"

namespace fs = std::filesystem;

namespace uitest {

namespace {

// The Analyzing modal shows the chart being worked on and live counts. It
// must not change width (and walk its Cancel button around) as they change —
// this is where the drift was most visible.
void test_batch_modal_drift(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Analyze library");
    ctx->SetRef("//Analyzing");
    ctx->ItemClick("Start");
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job && !h.app->batch_job->snapshot().preparing; }, 30));
    ImRect cancel0 = ctx->ItemInfo("Cancel").RectFull;
    ImRect win0 = ctx->GetWindowByRef("//Analyzing")->Rect();
    int seen_titles = 0;
    std::string last_title;
    while (!h.app->batch_job->snapshot().finished && seen_titles < 6) {
        ctx->Yield();
        std::string t = h.app->batch_job->snapshot().current_title;
        if (!t.empty() && t != last_title) { last_title = t; ++seen_titles; }
        ImRect cancel = ctx->ItemInfo("Cancel").RectFull;
        ImRect win = ctx->GetWindowByRef("//Analyzing")->Rect();
        IM_CHECK_FLOAT_NEAR_EQ(cancel.Min.x, cancel0.Min.x, 0.01f);
        IM_CHECK_FLOAT_NEAR_EQ(cancel.Min.y, cancel0.Min.y, 0.01f);
        IM_CHECK_FLOAT_NEAR_EQ(win.GetWidth(), win0.GetWidth(), 0.01f);
        IM_CHECK_FLOAT_NEAR_EQ(win.Min.x, win0.Min.x, 0.01f);
    }
    IM_CHECK(seen_titles >= 2);  // the loop actually saw titles change
    h.app->batch_job->cancel();
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job->snapshot().finished; }, 300));
    ctx->ItemClick("Continue");
}

void test_settings_and_reports(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
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
    std::string title = h.app->current_page.rows[0].title;
    ctx->ItemInputValue("##search", title.c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == title; }, 5));
    char label[96];
    std::snprintf(label, sizeof(label), "Analyze search (%lld)",
                  (long long)h.app->current_page.total_count);
    ctx->ItemClick(label);
    ctx->SetRef("//Analyzing");
    ctx->ItemClick("Start");
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job && h.app->batch_job->snapshot().finished; }, 300));
    IM_CHECK(wait_until(ctx, [&] { return h.app->report_job && h.app->report_job->finished(); }, 60));
    IM_CHECK(h.app->report_job->ok());
    IM_CHECK(hydra::app::report_file_exists());
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)0);
    ctx->ItemClick("**/Open path report");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);
    ctx->ItemClick("**/Continue");
    ctx->Yield(2);

    // dmleaderboards comparison against the canned API.
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Compare dmleaderboards user...");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->dm_users.empty(); }, 10));
    IM_CHECK(visible_text(h).find("alice") != std::string::npos);
    ctx->SetRef("//Compare dmleaderboards user");
    ctx->ItemClick("**/###111");
    IM_CHECK(wait_until(ctx, [&] { return h.app->dm_report_job && h.app->dm_report_job->finished(); }, 60));
    IM_CHECK_STR_EQ(h.app->dm_report_job->error().c_str(), "");
    IM_CHECK(fs::exists(hydra::app::dm_report_html_path()));
    IM_CHECK_EQ(h.app->dm_report_job->matched(), 1);
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);  // still: auto-open is off
}

// The dmleaderboards comparison, end to end: the two refusals, the name
// filter, and every button of the finished report.
void test_dm_compare_flow(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");

    // The ladder plays by Clone Hero's rules at Expert. Any other cap or
    // difficulty refuses with a status line and opens nothing.
    h.app->settings.sp_cap = 8;
    h.app->commit_settings();
    ctx->ItemClick("Compare dmleaderboards user...");
    ctx->Yield(2);
    IM_CHECK(!h.app->dm_picker_open);
    IM_CHECK(visible_text(h).find("needs SP cap 4") != std::string::npos);
    h.app->settings.sp_cap = 4;
    h.app->commit_settings();

    ctx->ComboClick("##difficulty/Hard");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.view_difficulty == "Hard"; }, 5));
    ctx->ItemClick("Compare dmleaderboards user...");
    ctx->Yield(2);
    IM_CHECK(!h.app->dm_picker_open);
    IM_CHECK(visible_text(h).find("needs Expert difficulty") != std::string::npos);
    ctx->ComboClick("##difficulty/Expert");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.view_difficulty == "Expert"; }, 5));

    // The picker opens on the canned ladder; the filter narrows it as you
    // type, ignoring case.
    ctx->ItemClick("Compare dmleaderboards user...");
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
    IM_CHECK_STR_EQ(h.app->settings.dm_last_user.c_str(), "111");
    IM_CHECK_STR_EQ(hydra::app::Settings::load_file(h.ini_path).dm_last_user.c_str(), "111");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)0);
    ctx->ItemClick("Open report again");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);
    IM_CHECK(h.opened_urls[0] == hydra::app::dm_report_html_path());

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

// The path report's buttons: the main window's "Open path report" appears
// once a report exists, "Open automatically" persists and then opens the
// next report by itself, and "redo existing" re-analyzes a stored chart.
void test_report_buttons(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    IM_CHECK(!ctx->ItemExists("Open path report"));  // no report built yet

    // Batch just the first chart (the search narrows the batch).
    std::string title = h.app->current_page.rows[0].title;
    ctx->ItemInputValue("##search", title.c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == title; }, 5));
    auto run_batch = [&] {
        char label[96];
        std::snprintf(label, sizeof(label), "Analyze search (%lld)",
                      (long long)h.app->current_page.total_count);
        ctx->SetRef("//Hydra");
        ctx->ItemClick(label);
        ctx->SetRef("//Analyzing");
        ctx->ItemClick("Start");
        return wait_until(ctx, [&] {
                   return h.app->batch_job && h.app->batch_job->snapshot().finished;
               }, 300) &&
               wait_until(ctx, [&] {
                   return h.app->report_job && h.app->report_job->finished();
               }, 60);
    };
    IM_CHECK(run_batch());
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)0);  // auto-open is off

    // Tick "Open automatically" in the finished modal: it persists at once.
    ctx->ItemClick("Open automatically");
    IM_CHECK(h.app->settings.auto_open_report);
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).auto_open_report);
    ctx->ItemClick("Continue");
    ctx->Yield(2);

    // The main window now offers the report, and opens it on a click.
    ctx->SetRef("//Hydra");
    IM_CHECK(wait_until(ctx, [&] { return ctx->ItemExists("Open path report"); }, 5));
    ctx->ItemClick("Open path report");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);
    IM_CHECK(h.opened_urls[0] == hydra::app::report_html_path());

    // "redo existing" re-analyzes the stored chart, and the confirm says so.
    ctx->ItemCheck("redo existing");
    IM_CHECK(h.app->batch_redo);
    char label[96];
    std::snprintf(label, sizeof(label), "Analyze search (%lld)",
                  (long long)h.app->current_page.total_count);
    ctx->ItemClick(label);
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("will be re-analyzed") != std::string::npos);
    ctx->SetRef("//Analyzing");
    ctx->ItemClick("Cancel");
    ctx->Yield(2);
    IM_CHECK(run_batch());
    IM_CHECK_EQ(h.app->batch_job->snapshot().skipped, 0);  // nothing skipped: redone

    // With auto-open on, the new report opened by itself.
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)2);
    ctx->ItemClick("Continue");
    ctx->Yield(2);
    ctx->SetRef("//Hydra");
    ctx->ItemUncheck("redo existing");
}

}  // namespace

const std::vector<TestEntry>& batch_report_tests() {
    static const std::vector<TestEntry> entries = {
        {"batch-modal-drift", test_batch_modal_drift},
        {"settings-and-reports", test_settings_and_reports},
        {"dm-compare-flow", test_dm_compare_flow},
        {"report-buttons", test_report_buttons},
    };
    return entries;
}

}  // namespace uitest
