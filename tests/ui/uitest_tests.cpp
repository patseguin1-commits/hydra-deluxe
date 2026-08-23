// The checked-in GUI tests. Each one starts from a fresh scratch app
// (reset_app), drives the real UI by widget label, and checks both the app's
// state and what is on screen. See docs/agents/ui-testing.md for the label
// cheat-sheet and how to add one.

#include <filesystem>
#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "ui/app_state.h"
#include "ui/jobs.h"
#include "ui/preview_controller.h"

namespace fs = std::filesystem;

namespace uitest {

namespace {

// Scan testdata/input through the UI and land on the populated library.
void scan_library(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Scan charts");
    IM_CHECK(wait_until(ctx, [&] { return h.app->scan_job && h.app->scan_job->snapshot().finished; }, 60));
    ctx->SetRef("//Scanning charts");
    IM_CHECK(visible_text(h).find("chart(s) found") != std::string::npos);
    ctx->ItemClick("Continue");
    ctx->Yield(2);
    ctx->SetRef("//Hydra");
    IM_CHECK(h.app->scan_job == nullptr);
    IM_CHECK(h.app->library_total > 0);
    IM_CHECK(!h.app->current_page.rows.empty());
}

// Click library row `index` and wait for the Song Details modal.
void open_details(ImGuiTestContext* ctx, size_t index) {
    Harness& h = harness(ctx);
    IM_CHECK(index < h.app->current_page.rows.size());
    std::string title = h.app->current_page.rows[index].title;
    ctx->SetRef("//Hydra");
    ctx->ItemClick(("**/" + escape_ref(title)).c_str());
    ctx->Yield(3);
    IM_CHECK(h.app->show_details);
    IM_CHECK(h.app->selected && h.app->selected->title == title);
    ctx->SetRef("//$FOCUSED");
    IM_CHECK(visible_text(h).find("Song Details") != std::string::npos);
}

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

void test_analyze(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    IM_CHECK(visible_text(h).find("After analyzing this song") != std::string::npos);
    ctx->ItemClick("**/Analyze paths!");
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed_record.has_value());
    IM_CHECK(!h.app->viewed_record->paths.empty());
    std::string best = h.app->viewed_record->best_path().pathstring();
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(best) != std::string::npos; }, 5));
    // The library row's Best Path cell now shows it too.
    IM_CHECK(h.app->current_page.summaries[0].state ==
             hydra::ui::LibraryPage::SummaryState::Current);

    // Records are kept per SP cap: switching the cap away from 4 shows the
    // song as not analyzed (no record at that cap), switching back finds the
    // 4-bar record again, and the INI follows every change.
    using hydra::ui::LibraryPage;
    ctx->ItemInputValue("**/##spcapvalue", 8);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 8; }, 5));
    IM_CHECK(h.app->current_page.summaries[0].state == LibraryPage::SummaryState::New);
    IM_CHECK(!h.app->viewed_record.has_value());
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).sp_cap == 8);
    ctx->ItemInputValue("**/##spcapvalue", 4);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 4; }, 5));
    IM_CHECK(h.app->current_page.summaries[0].state == LibraryPage::SummaryState::Current);
    IM_CHECK(h.app->viewed_record.has_value());
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find("SP cap:  4 bars") != std::string::npos; }, 5));
    // Auto has nothing above 4 bars to reuse, so it reads as new too.
    ctx->ItemClick("**/Auto##spcapauto");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->settings.sp_cap.has_value(); }, 5));
    IM_CHECK(h.app->current_page.summaries[0].state == LibraryPage::SummaryState::New);
    IM_CHECK(!hydra::app::Settings::load_file(h.ini_path).sp_cap.has_value());
    ctx->ItemClick("**/Auto##spcapauto");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 4; }, 5));
    IM_CHECK(h.app->current_page.summaries[0].state == LibraryPage::SummaryState::Current);
}

// Switching the SP cap between two caps that both have a record swaps
// viewed_record mid-frame, after the modal already chose which path to show.
// That used to leave the details panel reading the freed record (1.5.1 crash:
// bad_alloc from a garbage vector copy, 0xc0000409 on the UI thread).
void test_cap_switch(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    auto analyze = [&] {
        ctx->ItemClick("**/Analyze paths!");
        return wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300) &&
               h.app->viewed_record.has_value() && !h.app->viewed_record->paths.empty();
    };
    IM_CHECK(analyze());
    std::string best4 = h.app->viewed_record->best_path().pathstring();
    ctx->ItemInputValue("**/##spcapvalue", 6);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 6; }, 5));
    IM_CHECK(analyze());
    std::string best6 = h.app->viewed_record->best_path().pathstring();

    // Flip back and forth; every switch must land on the current record's
    // best path, never on whatever the previous record's memory now holds.
    for (int cap : {4, 6, 4, 6, 4}) {
        ctx->ItemInputValue("**/##spcapvalue", cap);
        IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == cap; }, 5));
        IM_CHECK(h.app->viewed_record.has_value());
        IM_CHECK(h.app->viewed_record->sp_cap == cap);
        const std::string& best = cap == 4 ? best4 : best6;
        IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(best) != std::string::npos; }, 5));
    }
}

void test_preview(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    ctx->ItemClick("**/Preview");
    IM_CHECK(wait_until(ctx, [&] { return h.app->preview && h.app->preview->active(); }, 10));
    IM_CHECK(wait_until(ctx, [&] { return !h.app->preview->loading(); }, 120));
    IM_CHECK_STR_EQ(h.app->preview->error().c_str(), "");
    IM_CHECK(!h.app->preview->playing());
    ctx->ItemClick("**/Play");
    IM_CHECK(h.app->preview->playing());
    ctx->ItemClick("**/Pause");
    IM_CHECK(!h.app->preview->playing());
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
    IM_CHECK(hydra::ui::report_file_exists());
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
    IM_CHECK(fs::exists(hydra::ui::dm_report_html_path()));
    IM_CHECK_EQ(h.app->dm_report_job->matched(), 1);
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);  // still: auto-open is off
}

}  // namespace

void register_tests(Harness& h) {
    struct Entry {
        const char* name;
        void (*fn)(ImGuiTestContext*);
    };
    const Entry entries[] = {
        {"scan", test_scan},
        {"analyze", test_analyze},
        {"cap-switch", test_cap_switch},
        {"preview", test_preview},
        {"settings-and-reports", test_settings_and_reports},
    };
    for (const Entry& e : entries) {
        ImGuiTest* t = IM_REGISTER_TEST(h.engine, "hydra", e.name);
        t->UserData = &h;
        t->TestFunc = e.fn;
    }
}

}  // namespace uitest
