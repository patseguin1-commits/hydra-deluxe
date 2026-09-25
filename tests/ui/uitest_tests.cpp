// The checked-in GUI tests. Each one starts from a fresh scratch app
// (reset_app), drives the real UI by widget label, and checks both the app's
// state and what is on screen. See docs/agents/ui-testing.md for the label
// cheat-sheet and how to add one.

#include <filesystem>
#include <string>
#include <vector>

#include "uitest_harness.h"

#include "app/config.h"
#include "app/dynamics_breakdown.h"
#include "app/preview_view.h"
#include "app/report_files.h"
#include "ui/app_state.h"
#include "ui/dynamics_load_job.h"
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
    // The ImGui context outlives reset_app, so the tab bar remembers the tab a
    // previous test left selected. Land on Paths deterministically.
    ctx->ItemClick("##DetailsTabs/Paths");
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

// An analysis started while the Preview tab is visible must still store its
// record and reap the job: persistence must not depend on the Paths tab
// drawing. Pre-fix, analyze_job sat "finished" forever and the record was
// never stored (the preview-then-analyze 300 s hang).
void test_analyze_on_preview(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    ctx->ItemClick("**/Analyze paths!");
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());
    IM_CHECK(!h.app->viewed.record->paths.empty());
    // Switching to Paths shows the stored result, no re-analyze.
    ctx->ItemClick("##DetailsTabs/Paths");
    std::string best = h.app->viewed.record->best_path().pathstring();
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(best) != std::string::npos; }, 5));
}

// The path overlay follows the Paths tab's selection. Re-opening the Preview
// for a chart that was already open used to be a plain no-op, so the overlay
// stayed on whatever path had been selected the first time -- the record's
// optimal path. Picking another path must swap the overlay in place: no
// re-parse, no audio re-decode, and the playhead left where it was.
void test_preview_path_overlay(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    ctx->ItemClick("**/Analyze paths!");
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());

    // A second path to switch to. Path rows are labeled by pathstring, so the
    // one picked must differ from the first path's and be unique among every
    // row the panel draws (the all-0 section included).
    std::vector<const hydra::Path*> paths = h.app->viewed.record->all_paths();
    std::vector<const hydra::Path*> rows = paths;
    for (const hydra::Path* p : h.app->viewed.record->all_allzero_paths())
        rows.push_back(p);
    const hydra::Path* other = nullptr;
    for (size_t i = 1; i < paths.size() && other == nullptr; ++i) {
        std::string label = paths[i]->pathstring();
        if (label == paths[0]->pathstring()) continue;
        size_t seen = 0;
        for (const hydra::Path* p : rows)
            if (p->pathstring() == label) ++seen;
        if (seen == 1) other = paths[i];
    }
    IM_CHECK(other != nullptr);  // the fixture must keep 2+ distinguishable paths
    const std::string first_key = hydra::app::path_overlay_key(paths[0]);
    const std::string other_key = hydra::app::path_overlay_key(other);
    const std::string first_label = paths[0]->pathstring();
    const std::string other_label = other->pathstring();
    IM_CHECK(first_key != other_key);
    size_t first_rows = 0;
    for (const hydra::Path* p : rows)
        if (p->pathstring() == first_label) ++first_rows;
    IM_CHECK_EQ(first_rows, (size_t)1);  // the first path's row is addressable too

    // The Preview opens on the default selection: the record's first path.
    ctx->ItemClick("##DetailsTabs/Preview");
    IM_CHECK(wait_until(ctx, [&] { return h.app->preview && h.app->preview->active(); }, 10));
    IM_CHECK(wait_until(ctx, [&] { return !h.app->preview->loading(); }, 120));
    IM_CHECK_STR_EQ(h.app->preview->error().c_str(), "");
    // The overlay key carries the SP cap after the path's own key, so match the
    // prefix and then compare whole keys against this first one.
    const std::string first_overlay = h.app->preview->overlay_path_key();
    IM_CHECK_EQ(first_overlay.rfind(first_key, 0), (size_t)0);

    // Park the playhead mid-song: a reload would rewind it to zero.
    IM_CHECK(h.app->preview->length_ms() > 0.0);
    h.app->preview->seek_ms(h.app->preview->length_ms() * 0.5);
    ctx->Yield(2);
    double held = h.app->preview->position_ms();
    IM_CHECK(held > 0.0);

    // Pick the other path and come back to the Preview.
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->ItemClick(("**/" + escape_ref(other_label)).c_str());
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
    IM_CHECK(!h.app->preview->loading());  // swapped in place, not reloaded
    IM_CHECK_FLOAT_NEAR_EQ(h.app->preview->position_ms(), held, 1.0);
    IM_CHECK_EQ(h.app->preview->overlay_path_key().rfind(other_key, 0), (size_t)0);
    IM_CHECK(h.app->preview->overlay_path_key() != first_overlay);

    // The same chart still previews the first path when it is selected again.
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->ItemClick(("**/" + escape_ref(first_label)).c_str());
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
    IM_CHECK(!h.app->preview->loading());
    IM_CHECK_STR_EQ(h.app->preview->overlay_path_key().c_str(), first_overlay.c_str());
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

// The backend limit is a display-only setting: it filters the Backends tables
// and nothing else, so flipping it must persist to the INI without touching
// the stored record. Also pins the renamed "Path limit" status line.
void test_backend_limit(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    ctx->ItemClick("**/Analyze paths!");
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());

    // The stored-result panel says "Path limit", not the old "Limit timings".
    IM_CHECK(wait_until(
        ctx, [&] { return visible_text(h).find("Path limit:") != std::string::npos; }, 5));

    // Off by default, and the number box is inert until it is ticked.
    IM_CHECK(!h.app->settings.backendlimit_enabled);
    IM_CHECK_EQ(h.app->settings.backendlimit_value, 50);
    IM_CHECK((ctx->ItemInfo("**/##backendlimitvalue").ItemFlags &
              ImGuiItemFlags_Disabled) != 0);

    ctx->ItemClick("**/##backendlimit");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_enabled; }, 5));
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).backendlimit_enabled);

    ctx->ItemInputValue("**/##backendlimitvalue", 30);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_value == 30; }, 5));
    IM_CHECK_EQ(hydra::app::Settings::load_file(h.ini_path).backendlimit_value, 30);
    IM_CHECK(h.app->settings.backend_limit() == 30.0);

    // The full engine window (500 ms) is reachable; beyond it clamps back.
    ctx->ItemInputValue("**/##backendlimitvalue", 500);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_value == 500; }, 5));
    ctx->ItemInputValue("**/##backendlimitvalue", 600);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_value == 500; }, 5));

    // Display-only: the record the modal shows is still the analyzed one.
    IM_CHECK(h.app->viewed.record.has_value());
    IM_CHECK(h.app->current_page.summaries[0].state ==
             hydra::store::RecordStatus::Ready);

    // Unticking turns the filter off again, and that persists too.
    ctx->ItemClick("**/##backendlimit");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->settings.backendlimit_enabled; }, 5));
    IM_CHECK(!hydra::app::Settings::load_file(h.ini_path).backendlimit_enabled);
    IM_CHECK(!h.app->settings.backend_limit().has_value());
}

void test_dynamics(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;

    // Search for the chart that the doctest pins dynamics on.
    ctx->SetRef("//Hydra");
    ctx->ItemInputValue("##search", "Acid Romance");
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == "Acid Romance"; }, 5));
    IM_CHECK(wait_until(ctx, [&] {
        return !h.app->current_page.rows.empty() &&
               h.app->current_page.rows[0].title == "Acid Romance";
    }, 5));
    open_details(ctx, 0);
    if (ctx->IsError()) return;

    // Click the Dynamics tab and wait for the background parse to finish.
    ctx->ItemClick("##DetailsTabs/Dynamics");
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->dynamics_result.has_value();
    }, 60));

    std::string text = visible_text(h);
    IM_CHECK(text.find("Dynamics enabled: yes") != std::string::npos);
    IM_CHECK(text.find("2x kicks:") != std::string::npos);
    // The doctest pins 5 ghosts for this chart (all from the red snare).
    IM_CHECK(text.find("Ghosts: 5") != std::string::npos);

    // Toggle 2x Bass off via app state (the checkbox is behind the modal)
    // and verify the Dynamics tab updates without re-parsing.
    h.app->settings.view_bass2x = false;
    h.app->commit_settings();
    ctx->Yield(2);
    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("not counted (2x Bass off)") != std::string::npos;
    }, 5));

    // Restore.
    h.app->settings.view_bass2x = true;
    h.app->commit_settings();
}

// Stored dynamics: the first open parses and stores; a second open reads
// the store and skips the parse job entirely. An analysis with 2x Bass on
// also stores the breakdown as a by-product, so the Dynamics tab after an
// analysis shows counts with no parse job.
void test_dynamics_stored(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);

    // ---- Scenario 1: parse, store, then re-open from store ----
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;

    // Open Acid Romance. Set the search via app state to avoid the ImGui
    // input-buffer residue from the previous dynamics test.
    h.app->search = "Acid Romance";
    h.app->refresh_page();
    ctx->Yield(2);
    IM_CHECK(!h.app->current_page.rows.empty());
    IM_CHECK(h.app->current_page.rows[0].title == "Acid Romance");
    open_details(ctx, 0);
    if (ctx->IsError()) return;

    // Click the Dynamics tab and wait for the background parse to finish.
    ctx->ItemClick("##DetailsTabs/Dynamics");
    IM_CHECK(wait_until(ctx, [&] { return h.app->dynamics_result.has_value(); }, 60));
    IM_CHECK(visible_text(h).find("Ghosts: 5") != std::string::npos);

    // Select a different chart so the in-memory dynamics cache for Acid
    // Romance is dropped, then close the modal so the tab stops rendering.
    h.app->search.clear();
    h.app->refresh_page();
    size_t other_idx = 0;
    for (size_t i = 0; i < h.app->current_page.rows.size(); ++i) {
        if (h.app->current_page.rows[i].title != "Acid Romance") {
            other_idx = i;
            break;
        }
    }
    h.app->select(h.app->current_page.rows[other_idx]);
    h.app->show_details = false;
    ctx->Yield(3);

    // Reopen Acid Romance. The Dynamics tab loads its counts from the
    // store (put there by the first open's job), so no parse job starts.
    h.app->search = "Acid Romance";
    h.app->refresh_page();
    ctx->Yield(2);
    IM_CHECK(!h.app->current_page.rows.empty());
    IM_CHECK(h.app->current_page.rows[0].title == "Acid Romance");
    // Clear any leftover dynamics state from the other chart.
    h.app->dynamics_result.reset();
    h.app->dynamics_key.clear();
    if (h.app->dynamics_job) { h.app->dynamics_job->cancel(); h.app->dynamics_job.reset(); }
    open_details(ctx, 0);
    if (ctx->IsError()) return;

    ctx->ItemClick("##DetailsTabs/Dynamics");
    ctx->Yield(3);
    // The stored breakdown was read from the store: no job was started.
    IM_CHECK(h.app->dynamics_result.has_value());
    IM_CHECK(h.app->dynamics_job == nullptr);
    IM_CHECK(visible_text(h).find("Ghosts: 5") != std::string::npos);

    // ---- Scenario 2: analysis stores dynamics as a by-product ----
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;

    // Open any chart (first row) and analyze it with 2x Bass on (the default).
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    IM_CHECK(h.app->settings.effective_bass2x());
    ctx->ItemClick("**/Analyze paths!");
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());

    // Now click the Dynamics tab. The analysis stored the breakdown, so the
    // tab should show counts with no parse job.
    ctx->ItemClick("##DetailsTabs/Dynamics");
    ctx->Yield(3);
    IM_CHECK(h.app->dynamics_result.has_value());
    IM_CHECK(h.app->dynamics_job == nullptr);
    // The counts are on screen (the first chart has notes, so "All" > 0).
    std::string text = visible_text(h);
    IM_CHECK(text.find("Ghosts:") != std::string::npos ||
             text.find("Accents:") != std::string::npos);
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
        {"difficulty", test_difficulty},
        {"analyze-on-preview", test_analyze_on_preview},
        {"preview-path-overlay", test_preview_path_overlay},
        {"scrub-hold", test_scrub_hold},
        {"layout-drift", test_layout_drift},
        {"batch-modal-drift", test_batch_modal_drift},
        {"settings-and-reports", test_settings_and_reports},
        {"backend-limit", test_backend_limit},
        {"dynamics", test_dynamics},
        {"dynamics-stored", test_dynamics_stored},
        {"rules-error", test_rules_error},
    };
    for (const Entry& e : entries) {
        ImGuiTest* t = IM_REGISTER_TEST(h.engine, "hydra", e.name);
        t->UserData = &h;
        t->TestFunc = e.fn;
    }
}

}  // namespace uitest
