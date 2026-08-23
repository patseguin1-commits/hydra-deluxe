// The checked-in GUI tests. Each one starts from a fresh scratch app
// (reset_app), drives the real UI by widget label, and checks both the app's
// state and what is on screen. See docs/agents/ui-testing.md for the label
// cheat-sheet and how to add one.

#include <filesystem>
#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "app/report_files.h"
#include "ui/app_state.h"
#include "ui/preview_controller.h"
#include "ui/preview_load_job.h"

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
    IM_CHECK(h.app->viewed.record.has_value());
    IM_CHECK(!h.app->viewed.record->paths.empty());
    std::string best = h.app->viewed.record->best_path().pathstring();
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(best) != std::string::npos; }, 5));
    // The library row's Best Path cell now shows it too.
    IM_CHECK(h.app->current_page.summaries[0].state ==
             hydra::store::RecordStatus::Ready);

    // Records are kept per SP cap: switching the cap away from 4 shows the
    // song as not analyzed (no record at that cap), switching back finds the
    // 4-bar record again, and the INI follows every change.
    ctx->ItemInputValue("**/##spcapvalue", 8);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 8; }, 5));
    IM_CHECK(h.app->current_page.summaries[0].state == hydra::store::RecordStatus::NotAnalyzed);
    IM_CHECK(!h.app->viewed.record.has_value());
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).sp_cap == 8);
    ctx->ItemInputValue("**/##spcapvalue", 4);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 4; }, 5));
    IM_CHECK(h.app->current_page.summaries[0].state == hydra::store::RecordStatus::Ready);
    IM_CHECK(h.app->viewed.record.has_value());
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find("SP cap:  4 bars") != std::string::npos; }, 5));
    // Auto has nothing above 4 bars to reuse, so it reads as new too.
    ctx->ItemClick("**/Auto##spcapauto");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->settings.sp_cap.has_value(); }, 5));
    IM_CHECK(h.app->current_page.summaries[0].state == hydra::store::RecordStatus::NotAnalyzed);
    IM_CHECK(!hydra::app::Settings::load_file(h.ini_path).sp_cap.has_value());
    ctx->ItemClick("**/Auto##spcapauto");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 4; }, 5));
    IM_CHECK(h.app->current_page.summaries[0].state == hydra::store::RecordStatus::Ready);
}

// Switching the SP cap between two caps that both have a record swaps
// the viewed record mid-frame, after the modal already chose which path to show.
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
               h.app->viewed.record.has_value() && !h.app->viewed.record->paths.empty();
    };
    IM_CHECK(analyze());
    std::string best4 = h.app->viewed.record->best_path().pathstring();
    ctx->ItemInputValue("**/##spcapvalue", 6);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 6; }, 5));
    IM_CHECK(analyze());
    std::string best6 = h.app->viewed.record->best_path().pathstring();

    // Flip back and forth; every switch must land on the current record's
    // best path, never on whatever the previous record's memory now holds.
    for (int cap : {4, 6, 4, 6, 4}) {
        ctx->ItemInputValue("**/##spcapvalue", cap);
        IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == cap; }, 5));
        IM_CHECK(h.app->viewed.record.has_value());
        IM_CHECK(h.app->viewed.record->sp_cap == cap);
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
    // The loading bar's numbers: reading sits at 0, decoding spreads stems
    // over the middle, mixing and building fill the tail. The test charts
    // load too fast to catch on screen, so pin the math down here.
    {
        using P = hydra::ui::PreviewLoadJob::Progress;
        using S = hydra::ui::PreviewLoadJob::Step;
        // Locals, not P{...} inline: the braces' commas split the IM_CHECK
        // macro arguments.
        const P reading{S::Reading, 0, 0};
        const P decode0{S::Decoding, 0, 4};
        const P decode2{S::Decoding, 2, 4};
        const P decode4{S::Decoding, 4, 4};
        const P mixing{S::Mixing, 4, 4};
        const P building{S::Building, 4, 4};
        IM_CHECK_EQ(reading.fraction(), 0.0f);
        IM_CHECK_STR_EQ(reading.label().c_str(), "Reading chart");
        IM_CHECK_FLOAT_NEAR_EQ(decode0.fraction(), 0.10f, 1e-5f);
        IM_CHECK_FLOAT_NEAR_EQ(decode2.fraction(), 0.475f, 1e-5f);
        IM_CHECK_STR_EQ(decode2.label().c_str(), "Decoding audio 3/4");
        IM_CHECK_STR_EQ(decode4.label().c_str(), "Decoding audio 4/4");
        IM_CHECK_FLOAT_NEAR_EQ(mixing.fraction(), 0.85f, 1e-5f);
        IM_CHECK_FLOAT_NEAR_EQ(building.fraction(), 0.95f, 1e-5f);
        IM_CHECK_STR_EQ(building.label().c_str(), "Building scene");
    }

    ctx->ItemClick("**/Preview");
    IM_CHECK(wait_until(ctx, [&] { return h.app->preview && h.app->preview->active(); }, 10));
    // While the load is in flight the tab shows a step label and a progress
    // bar, not a bare "Loading..." (a big chart decodes for seconds). The
    // test charts load fast, so only check when we actually caught it loading.
    if (h.app->preview->loading()) {
        std::string text = visible_text(h);
        IM_CHECK(text.find("Loading preview:") != std::string::npos);
        IM_CHECK(text.find('%') != std::string::npos);
        IM_CHECK(!h.app->preview->load_progress().label.empty());
    }
    IM_CHECK(wait_until(ctx, [&] { return !h.app->preview->loading(); }, 120));
    IM_CHECK_STR_EQ(h.app->preview->error().c_str(), "");
    IM_CHECK(!h.app->preview->playing());
    ctx->ItemClick("**/Play");
    IM_CHECK(h.app->preview->playing());
    ctx->ItemClick("**/Pause");
    IM_CHECK(!h.app->preview->playing());
}

// Shared: open chart 0's Preview and wait for the load. Returns false on error.
bool open_preview(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return false;
    open_details(ctx, 0);
    if (ctx->IsError()) return false;
    ctx->ItemClick("**/Preview");
    IM_CHECK_RETV(wait_until(ctx, [&] { return h.app->preview && h.app->preview->active(); }, 10), false);
    IM_CHECK_RETV(wait_until(ctx, [&] { return !h.app->preview->loading(); }, 120), false);
    IM_CHECK_RETV(h.app->preview->error().empty(), false);
    return true;
}

// Click-and-hold on the time bar while playing. Onyx pauses playback for the
// hold; Hydra used to keep playing and re-seek the audio to the held time
// every frame, which came out as a buzz. The transport must be paused while
// the mouse is down, sit still at the held time, and resume on release.
void test_scrub_hold(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    ctx->ItemClick("**/Play");
    IM_CHECK(h.app->preview->playing());
    ctx->MouseMove("**/##scrub");
    ctx->MouseDown(0);
    ctx->Yield(5);
    IM_CHECK(!h.app->preview->playing());
    double held = h.app->preview->position_ms();
    ctx->Yield(30);
    IM_CHECK_FLOAT_NEAR_EQ(h.app->preview->position_ms(), held, 0.5);
    ctx->MouseUp(0);
    ctx->Yield(2);
    IM_CHECK(h.app->preview->playing());
    // The same hold while paused stays paused afterwards.
    ctx->ItemClick("**/Pause");
    ctx->MouseMove("**/##scrub");
    ctx->MouseDown(0);
    ctx->Yield(5);
    ctx->MouseUp(0);
    ctx->Yield(2);
    IM_CHECK(!h.app->preview->playing());
}

// A widget must not move under the mouse because a number next to it changed
// width (the font is proportional). The Vol slider sits after the live time
// readout; the page arrows straddle the page counter.
void test_layout_drift(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    ctx->ItemClick("**/Play");
    ImVec2 vol0 = ctx->ItemInfo("**/##volume").RectFull.Min;
    ImVec2 play0 = ctx->ItemInfo("**/Pause").RectFull.Min;
    ImVec2 scrub0 = ctx->ItemInfo("**/##scrub").RectFull.Min;
    float scrubw0 = ctx->ItemInfo("**/##scrub").RectFull.GetWidth();
    double t0 = h.app->preview->position_ms();
    // Let the readout pass through several different digit strings.
    IM_CHECK(wait_until(ctx, [&] { return h.app->preview->position_ms() > t0 + 1500.0; }, 10));
    for (int i = 0; i < 20; ++i) {
        ctx->Yield(3);
        IM_CHECK_FLOAT_NEAR_EQ(ctx->ItemInfo("**/##volume").RectFull.Min.x, vol0.x, 0.01f);
        IM_CHECK_FLOAT_NEAR_EQ(ctx->ItemInfo("**/##scrub").RectFull.Min.x, scrub0.x, 0.01f);
        IM_CHECK_FLOAT_NEAR_EQ(ctx->ItemInfo("**/##scrub").RectFull.GetWidth(), scrubw0, 0.01f);
    }
    ctx->ItemClick("**/Pause");
    // Play/Pause swap must not shift the scrubber either.
    IM_CHECK_FLOAT_NEAR_EQ(ctx->ItemInfo("**/##scrub").RectFull.Min.x, scrub0.x, 0.01f);
    (void)play0;
}

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
        {"scrub-hold", test_scrub_hold},
        {"layout-drift", test_layout_drift},
        {"batch-modal-drift", test_batch_modal_drift},
        {"settings-and-reports", test_settings_and_reports},
    };
    for (const Entry& e : entries) {
        ImGuiTest* t = IM_REGISTER_TEST(h.engine, "hydra", e.name);
        t->UserData = &h;
        t->TestFunc = e.fn;
    }
}

}  // namespace uitest
