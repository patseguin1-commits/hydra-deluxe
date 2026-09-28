#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "uitest_harness.h"

#include "app/preview_view.h"
#include "core/model.h"
#include "imgui_internal.h"
#include "render/overlay_layout.h"
#include "ui/app_state.h"
#include "ui/fonts.h"  // g_mono_font
#include "ui/preview_controller.h"
#include "ui/preview_load_job.h"

namespace uitest {

namespace {

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

// An analysis started while the Preview tab is visible must still store its
// record and reap the job: persistence must not depend on the Paths tab
// drawing. Pre-fix, analyze_job sat "finished" forever and the record was
// never stored (the preview-then-analyze 300 s hang).
void test_analyze_on_preview(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    ctx->ItemClick(analyze_button_ref(h).c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());
    IM_CHECK(!h.app->viewed.record->paths.empty());
    // Switching to Paths shows the stored result, no re-analyze.
    ctx->ItemClick("##DetailsTabs/Paths");
    std::string best = h.app->viewed.record->best_path().pathstring();
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(best) != std::string::npos; }, 5));
}

// The "Showing" list's ID. Dear ImGui's BeginCombo reports no label to the
// test engine, so a "**/##previewpath" wildcard never finds it. It is hashed
// on the same ID stack as the "< Act" button under it, whose parent ID the
// engine does know, so this works wherever the tab is drawn.
ImGuiID preview_path_combo(ImGuiTestContext* ctx) {
    const ImGuiTestItemInfo act = ctx->ItemInfo("**/< Act##prevact");
    return ImHashStr("##previewpath", 0, act.ParentID);
}

// Pick path `index` (its place in the Paths tab's list, ##path<index>) in the
// Preview's "Showing" list. The Preview tab must be showing.
void pick_preview_path(ImGuiTestContext* ctx, size_t index) {
    Harness& h = harness(ctx);
    const hydra::app::PathButtonsView list = hydra::app::build_path_buttons(
        *h.app->viewed.record, h.app->settings.depth_mode, h.app->settings.depth_value);
    IM_CHECK(index < list.buttons.size());
    if (index >= list.buttons.size()) return;
    const std::string item =
        hydra::app::preview_path_label(list.buttons[index]) + "##" + std::to_string(index);
    ctx->ItemClick(preview_path_combo(ctx));
    ctx->Yield(1);
    ctx->ItemClick(("//$FOCUSED/" + item).c_str());
    ctx->Yield(2);
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
    ctx->ItemClick(analyze_button_ref(h).c_str());
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
    size_t other_index = 0;  // its place in the list: the list follows all_paths()
    for (size_t i = 1; i < paths.size() && other == nullptr; ++i) {
        std::string label = paths[i]->pathstring();
        if (label == paths[0]->pathstring()) continue;
        size_t seen = 0;
        for (const hydra::Path* p : rows)
            if (p->pathstring() == label) ++seen;
        if (seen == 1) {
            other = paths[i];
            other_index = i;
        }
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
    // The path's overlay can land a frame or two after the load itself.
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->preview->overlay_path_key().rfind(first_key, 0) == 0;
    }, 10));
    const std::string first_overlay = h.app->preview->overlay_path_key();
    IM_CHECK_EQ(first_overlay.rfind(first_key, 0), (size_t)0);

    // Park the playhead mid-song: a reload would rewind it to zero.
    IM_CHECK(h.app->preview->length_ms() > 0.0);
    h.app->preview->seek_ms(h.app->preview->length_ms() * 0.5);
    ctx->Yield(2);
    double held = h.app->preview->position_ms();
    IM_CHECK(held > 0.0);

    // Pick the other path in the Preview's list, then visit Paths and come
    // back, so the Preview is re-opened on the same chart.
    pick_preview_path(ctx, other_index);
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
    IM_CHECK(!h.app->preview->loading());  // swapped in place, not reloaded
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->preview->overlay_path_key().rfind(other_key, 0) == 0;
    }, 10));
    IM_CHECK_FLOAT_NEAR_EQ(h.app->preview->position_ms(), held, 1.0);
    IM_CHECK(h.app->preview->overlay_path_key() != first_overlay);

    // The same chart still previews the first path when it is picked again.
    pick_preview_path(ctx, 0);
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
    IM_CHECK(!h.app->preview->loading());
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->preview->overlay_path_key() == first_overlay;
    }, 10));
}

// The Preview's finer time controls and running score, driven through the
// controller inside the real app. preview-buttons-keys drives the same
// through the panel's buttons and keys.
void test_preview_controls(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;  // chart 0, not analyzed yet
    auto& pc = *h.app->preview;

    // No analyzed path: no score box.
    IM_CHECK(!pc.score_box().shown);

    // Analyze from the Preview tab, then visit Paths and come back so the
    // overlay (and with it the score) is rebuilt from the new record's path.
    ctx->ItemClick(analyze_button_ref(h).c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    // Wait for the reload to finish and the score box to appear, rather than
    // assume a fixed number of frames is enough.
    IM_CHECK(wait_until(ctx, [&] { return !pc.loading() && pc.score_box().shown; }, 60));

    // At the song's end the box reads the selected path's total.
    IM_CHECK(pc.length_ms() > 12000.0);
    pc.seek_ms(pc.length_ms());
    hydra::app::PreviewScoreBox end = pc.score_box();
    IM_CHECK(end.shown);
    IM_CHECK(end.available);
    const hydra::Path* shown = h.app->viewed.record->all_paths().front();
    IM_CHECK_STR_EQ(end.score.c_str(), hydra::group_thousands(shown->totalscore()).c_str());

    // 5 s jumps, clamped to the song's ends.
    pc.seek_ms(10000.0);
    pc.jump_ms(5000.0);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 15000.0, 0.5);
    pc.jump_ms(-5000.0);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 10000.0, 0.5);
    pc.jump_ms(-60000.0);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 0.0, 0.5);
    pc.jump_ms(pc.length_ms() + 60000.0);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), pc.length_ms(), 0.5);

    // A jump while playing keeps playing.
    pc.seek_ms(10000.0);
    pc.play();
    IM_CHECK(pc.playing());
    pc.jump_ms(5000.0);
    IM_CHECK(pc.playing());

    // A tick step pauses, and one step each way returns to the same tick.
    pc.step_ticks(0);  // pause and snap onto the displayed tick
    IM_CHECK(!pc.playing());
    const double t0 = pc.position_ms();
    const std::string mb0 = pc.time_box().position;
    pc.step_ticks(1);
    IM_CHECK(pc.position_ms() > t0);
    IM_CHECK(pc.position_ms() - t0 < 20.0);  // one tick, at any real tempo
    IM_CHECK(pc.time_box().position != mb0);
    pc.step_ticks(-1);
    IM_CHECK_STR_EQ(pc.time_box().position.c_str(), mb0.c_str());
}

// The SP drain box beside the gauge. Its values are build_drain_box's, pinned
// by the unit tests; this checks the panel feeds it the playhead and the
// viewed path, and saves a frame of the active box to look at.
void test_preview_drain_box(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;  // chart 0, not analyzed yet
    auto& pc = *h.app->preview;

    // Analyze, then visit Paths and come back so the overlay is rebuilt from
    // the new record's path (as preview-controls does).
    ctx->ItemClick(analyze_button_ref(h).c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    IM_CHECK(wait_until(ctx, [&] { return !pc.loading() && pc.score_box().shown; }, 60));
    IM_CHECK(pc.sp_meter_has_curve());

    // Before anything is banked or spent: the idle box.
    pc.seek_ms(0.0);
    hydra::app::PreviewDrainBox idle = pc.drain_box();
    IM_CHECK(idle.shown);
    IM_CHECK(!idle.active);
    IM_CHECK_STR_EQ(idle.header.c_str(), "SP drain (if activated)");
    IM_CHECK(idle.rate.rfind("1 bar / ", 0) == 0);
    IM_CHECK(idle.detail.rfind("full meter ", 0) == 0);

    // Somewhere the path has SP running. Walk the playhead to find it, so the
    // test needs no timing of its own.
    double active_ms = -1.0;
    for (double t = 0.0; t <= pc.length_ms() && active_ms < 0.0; t += 50.0) {
        pc.seek_ms(t);
        if (pc.drain_box().active) active_ms = t;
    }
    IM_CHECK(active_ms >= 0.0);
    pc.seek_ms(active_ms);
    hydra::app::PreviewDrainBox on = pc.drain_box();
    IM_CHECK_STR_EQ(on.header.c_str(), "SP drain");
    IM_CHECK(on.rate.rfind("1 bar / ", 0) == 0);
    IM_CHECK(on.detail.rfind("empties in ", 0) == 0);

    // A frame of the active box, for a person to look at.
    ctx->Yield(2);
    IM_CHECK(screenshot(ctx, "drain-box-active.png"));
}

// Sets the harness display width for one test and puts it back however the
// test ends, so a failed check cannot leave later tests on a narrow display.
struct DisplayWidth {
    Harness& h;
    int saved;
    DisplayWidth(Harness& harness_, int width) : h(harness_), saved(harness_.width) {
        h.width = width;
    }
    ~DisplayWidth() { h.width = saved; }
};

// The Preview's text boxes shrink to sit beside the highway in a narrow
// panel and keep their size in a wide one. The song panel shares the window
// with the library, so at the harness's 1280 px the default split already
// shrinks them a little; with the library dragged to its narrowest they are
// full size, and a 640 px display shrinks them more.
void test_preview_overlay_fit(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    auto& pc = *h.app->preview;

    ctx->Yield(3);
    const float split_scale = pc.overlay_scale();
    IM_CHECK(split_scale <= 1.0f);
    IM_CHECK(split_scale >= hydra::render::kOverlayMinScale);

    // The library at its narrowest (what dragging its edge left does): the
    // split sets its width from the share the next time the panel opens.
    hydra::ui::remember_library_share(0.01f);
    h.app->library_ui.panel_was_open = false;
    ctx->Yield(5);
    IM_CHECK_EQ(pc.overlay_scale(), 1.0f);  // a wide panel: full size
    IM_CHECK(screenshot(ctx, "overlay-fit-wide.png"));

    DisplayWidth narrow(h, 640);
    ctx->Yield(5);
    IM_CHECK(pc.overlay_scale() < 1.0f);
    IM_CHECK(pc.overlay_scale() >= hydra::render::kOverlayMinScale);
    IM_CHECK(screenshot(ctx, "overlay-fit-narrow.png"));
}

// The Preview's new transport buttons and keys: -5s / +5s and Left / Right
// jump 5 s, < 5 Ticks / 5 Ticks > and comma / period step 5 chart ticks, Space
// plays and pauses.
void test_preview_buttons_keys(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    auto& pc = *h.app->preview;
    IM_CHECK(!pc.playing());
    IM_CHECK(pc.length_ms() > 16000.0);

    pc.seek_ms(10000.0);
    ctx->ItemClick("**/+5s");
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 15000.0, 0.5);
    ctx->ItemClick("**/-5s");
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 10000.0, 0.5);

    // The arrows jump exactly 5 s. Had keyboard navigation also taken the
    // arrow and nudged the scrubber, the playhead would be off by the nudge.
    ctx->KeyPress(ImGuiKey_RightArrow);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 15000.0, 0.5);
    ctx->KeyPress(ImGuiKey_LeftArrow);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 10000.0, 0.5);

    // A tick step pauses.
    ctx->ItemClick("**/Play");
    IM_CHECK(pc.playing());
    ctx->ItemClick("**/5 Ticks >");
    IM_CHECK(!pc.playing());

    // After a step the playhead sits on a tick; period then comma returns
    // the time box to it.
    ctx->ItemClick(("**/" + escape_ref("< 5 Ticks")).c_str());
    const double t0 = pc.position_ms();
    const std::string mb0 = pc.time_box().position;
    ctx->KeyPress(ImGuiKey_Period);
    const double t5 = pc.position_ms();
    IM_CHECK(t5 > t0);
    IM_CHECK(pc.time_box().position != mb0);
    ctx->KeyPress(ImGuiKey_Comma);
    IM_CHECK_STR_EQ(pc.time_box().position.c_str(), mb0.c_str());

    // The key steps 5 ticks: the same place five single steps reach.
    for (int i = 0; i < 5; ++i) pc.step_ticks(1);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), t5, 0.001);
    pc.step_ticks(-5);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), t0, 0.001);

    // Space plays and pauses. Tab puts the keyboard focus box on the button
    // after "< 5 Ticks"; had navigation also taken Space it would have
    // pressed that button too, which undoes the toggle or steps the playhead.
    ctx->ItemClick(("**/" + escape_ref("< 5 Ticks")).c_str());
    ctx->KeyPress(ImGuiKey_Tab);
    IM_CHECK(!pc.playing());
    const double t1 = pc.position_ms();
    ctx->KeyPress(ImGuiKey_Space);
    IM_CHECK(pc.playing());
    IM_CHECK(pc.position_ms() >= t1);
    ctx->KeyPress(ImGuiKey_Space);
    IM_CHECK(!pc.playing());
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

// Burnout analyzed, its Preview open with the optimal path's overlay loaded.
// Returns false (the check already failed) when a step did not work.
bool open_burnout_preview(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return false;
    open_titled(ctx, "burnout", "Burnout");
    if (ctx->IsError()) return false;
    analyze_open_song(ctx);
    if (ctx->IsError()) return false;
    IM_CHECK_RETV(h.app->viewed.record->best_path().pathstring() == "3- 1 2", false);
    ctx->ItemClick("##DetailsTabs/Preview");
    IM_CHECK_RETV(wait_until(ctx, [&] { return h.app->preview && h.app->preview->active(); }, 10),
                  false);
    IM_CHECK_RETV(wait_until(ctx, [&] {
        return !h.app->preview->loading() && h.app->preview->scrub_marks().size() == 3;
    }, 120), false);
    IM_CHECK_RETV(h.app->preview->error().empty(), false);
    return true;
}

// The "Showing" list: it names the drawn path, lists the all-0 path under its
// own name, and a pick changes the one selection the Paths tab reads too. A
// pending "Show in Preview" lands the playhead on its activation. (Clicking
// the link itself is Task 10's, checked in paths-rows.)
void test_preview_path_picker(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout_preview(ctx)) return;
    auto& pc = *h.app->preview;
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("Showing") != std::string::npos);
    IM_CHECK(visible_text(h).find("3- 1 2  (optimal)") != std::string::npos);

    // The list sits on its own line above the buttons, as wide as its longest
    // path, so no path is cut off under the arrow.
    {
        const ImRect combo = ctx->ItemInfo(preview_path_combo(ctx)).RectFull;
        const ImRect act = ctx->ItemInfo("**/< Act##prevact").RectFull;
        IM_CHECK_GE(act.Min.y, combo.Max.y);
        const ImGuiStyle& s = ImGui::GetStyle();
        const float size = s.FontSizeBase * s.FontScaleMain * s.FontScaleDpi;
        const hydra::app::PathButtonsView list = hydra::app::build_path_buttons(
            *h.app->viewed.record, h.app->settings.depth_mode, h.app->settings.depth_value);
        float widest = 0.0f;
        for (const hydra::app::PathButtonView& b : list.buttons) {
            const std::string label = hydra::app::preview_path_label(b);
            widest = (std::max)(widest, hydra::ui::g_mono_font
                                            ->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str())
                                            .x);
        }
        const float fit = widest + s.FramePadding.x * 2.0f + combo.GetHeight();
        IM_CHECK_FLOAT_NEAR_EQ(combo.GetWidth(), fit, 1.0f);
    }

    // A path too long for its line (40 activations run to about 100
    // characters) ends in "…" inside the box, in the picker's own font, at
    // the narrowest panel. The picker draws its label through this same cut.
    {
        hydra::ui::remember_library_share(0.99f);
        h.app->library_ui.panel_was_open = false;
        ctx->Yield(3);
        const ImGuiTestItemInfo combo = ctx->ItemInfo(preview_path_combo(ctx));
        ImGuiWindow* win = combo.Window;
        IM_CHECK(win != nullptr);
        if (win == nullptr) return;
        const ImGuiStyle& s = ImGui::GetStyle();
        const float size = s.FontSizeBase * s.FontScaleMain * s.FontScaleDpi;
        const float line = win->WorkRect.Max.x - combo.RectFull.Min.x;  // the most the box gets
        const float chrome = s.FramePadding.x * 2.0f + combo.RectFull.GetHeight();
        auto width_of = [&](const std::string& t) {
            return hydra::ui::g_mono_font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.c_str()).x;
        };
        std::string long_label;
        for (int i = 0; i < 40; ++i) long_label += (i % 3 == 0 ? "2- " : "1 ");
        long_label += " (optimal)";
        IM_CHECK(width_of(long_label) > line - chrome);  // too long for the line
        const std::string shown = hydra::render::ellipsize(long_label, line - chrome, width_of);
        IM_CHECK(shown != long_label);
        IM_CHECK(width_of(shown) <= line - chrome);
        IM_CHECK(shown.size() > 3 && shown.substr(shown.size() - 3) == "\xE2\x80\xA6");
        IM_CHECK(hydra::ui::g_mono_font->IsGlyphInFont(0x2026));  // drawn, not a "?"
        hydra::ui::remember_library_share(0.5f);
        h.app->library_ui.panel_was_open = false;
        ctx->Yield(3);
    }

    ctx->ItemClick(preview_path_combo(ctx));
    ctx->Yield(1);
    IM_CHECK(visible_text(h).find("0 0 0 0  (best all-0)") != std::string::npos);
    // Close the list on the path already shown. (PopupCloseAll would also
    // close the Song Details modal the tab sits in.)
    ctx->ItemClick("//$FOCUSED/3- 1 2  (optimal)##0");
    ctx->Yield(1);
    IM_CHECK(h.app->details_ui.selected_path == &h.app->viewed.record->best_path());

    pick_preview_path(ctx, 1);
    IM_CHECK(h.app->details_ui.selected_path != nullptr);
    IM_CHECK(h.app->details_ui.selected_path->pathstring() == "0 4 1");
    const std::string key = hydra::app::path_overlay_key(h.app->details_ui.selected_path);
    IM_CHECK(wait_until(ctx, [&] { return pc.overlay_path_key().rfind(key, 0) == 0; }, 30));
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);
    IM_CHECK(h.app->details_ui.selected_path->pathstring() == "0 4 1");
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);

    // Back to the optimal path, then a pending jump to its first activation.
    pick_preview_path(ctx, 0);
    IM_CHECK(h.app->details_ui.selected_path == &h.app->viewed.record->best_path());
    pc.seek_ms(0.0);
    h.app->details_ui.paths_tab.ui().preview_jump = 0;
    IM_CHECK(wait_until(ctx, [&] {
        return !h.app->details_ui.paths_tab.ui().preview_jump.has_value();
    }, 30));
    IM_CHECK(pc.position_ms() > 0.0);
    IM_CHECK_STR_EQ(pc.next_act_box().header.c_str(), "Next: activation 1 of 3");
    IM_CHECK(pc.next_act_box().detail.rfind("at m32.1.0", 0) == 0);
}

// "Preview failed: …" wraps inside the panel. An error naming a long file
// path used to run on past the panel's edge, cut off with no way to read it.
void test_preview_error_wraps(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    auto& pc = *h.app->preview;
    // The selection now names a chart whose file is gone, in a deep folder,
    // so the Preview reloads and fails on a message carrying that long path.
    IM_CHECK(h.app->selected.has_value());
    if (!h.app->selected) return;
    std::string folder = h.app->selected->rootfolder;
    for (int i = 0; i < 6; ++i) folder += "\\A folder with a long name to push the path past the edge";
    h.app->selected->notespath = folder + "\\notes.chart";
    h.app->selected->md5 = "0123456789abcdef0123456789abcdef";
    IM_CHECK(wait_until(ctx, [&] { return pc.has_error(); }, 30));
    ctx->Yield(3);

    // The message is wider than the whole screen, so on one line it could fit
    // in no window: nothing overflowing below means it wrapped.
    const std::string message = "Preview failed: " + pc.error();
    IM_CHECK(visible_text(h).find("Preview failed:") != std::string::npos);
    IM_CHECK_GT(ImGui::CalcTextSize(message.c_str()).x, ImGui::GetIO().DisplaySize.x);
    // The song panel's windows, the message's among them. (The settings bar
    // above the library is another task's.)
    int checked = 0;
    int overflowing = 0;
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        if (!w->WasActive || (w->Flags & ImGuiWindowFlags_Tooltip) ||
            (w->Flags & ImGuiWindowFlags_HorizontalScrollbar) ||
            std::strstr(w->Name, "##songpanel") == nullptr)
            continue;
        ++checked;
        if (w->ContentSize.x > w->ContentRegionRect.GetWidth() + 0.5f) {
            ++overflowing;
            std::fprintf(stderr, "OVERFLOW %s: content %.0f px, room %.0f px\n", w->Name,
                         w->ContentSize.x, w->ContentRegionRect.GetWidth());
        }
    }
    IM_CHECK_GT(checked, 0);
    IM_CHECK_EQ(overflowing, 0);
}

// The text boxes keep one size all through a path. The next-activation box
// used to count toward the shared scale with whatever line it showed, so on
// Burnout's long second chord every box shrank and grew back later. Checked
// at the default split and with the song panel at its narrowest.
void test_preview_overlay_steady(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout_preview(ctx)) return;
    auto& pc = *h.app->preview;
    for (float share : {0.5f, 0.99f}) {
        hydra::ui::remember_library_share(share);
        h.app->library_ui.panel_was_open = false;
        ctx->Yield(3);
        pc.seek_ms(0.0);
        ctx->Yield(2);
        const float scale = pc.overlay_scale();
        IM_CHECK(scale >= hydra::render::kOverlayMinScale);
        std::fprintf(stderr, "library share %.2f: overlay scale %.4f\n", share, scale);
        for (int act = 1; act <= 3; ++act) {
            IM_CHECK(pc.jump_activation(+1));
            ctx->Yield(2);
            if (act == 1) IM_CHECK_STR_EQ(pc.time_box().position.c_str(), "m32.1.0");
            if (act == 2) {
                IM_CHECK_STR_EQ(pc.time_box().position.c_str(), "m58.1.0");
                // The long chord's box, wrapped beside the lane.
                IM_CHECK(screenshot(ctx, share > 0.9f ? "overlay-steady-narrow.png"
                                                      : "overlay-steady-split.png"));
            }
            IM_CHECK_EQ(pc.overlay_scale(), scale);
        }
        // Past the last activation the box is gone; the scale stays.
        pc.seek_ms(pc.length_ms());
        ctx->Yield(2);
        IM_CHECK(!pc.next_act_box().shown);
        IM_CHECK_EQ(pc.overlay_scale(), scale);
    }
    hydra::ui::remember_library_share(0.5f);
    h.app->library_ui.panel_was_open = false;
    ctx->Yield(3);
}

// Activation jumps by button and key, the scrubber marks, the next-activation
// box, the SP readout, the one measure format, and the key hint.
void test_preview_activation_jumps(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout_preview(ctx)) return;
    auto& pc = *h.app->preview;

    const std::vector<double> marks = pc.scrub_marks();
    IM_CHECK_EQ(marks.size(), (size_t)3);
    IM_CHECK(marks[0] > 0.0);
    IM_CHECK(marks[0] < marks[1]);
    IM_CHECK(marks[1] < marks[2]);
    IM_CHECK(marks[2] < 1.0);

    pc.seek_ms(0.0);
    IM_CHECK_STR_EQ(pc.next_act_box().header.c_str(), "Next: activation 1 of 3");
    IM_CHECK_STR_EQ(pc.next_act_box().detail.c_str(), "at m32.1.0 \xC2\xB7 [Kick - GreenCym]");

    ctx->ItemClick("**/Act >##nextact");
    const double act1 = pc.position_ms();
    IM_CHECK(act1 > 0.0);
    IM_CHECK_STR_EQ(pc.time_box().position.c_str(), "m32.1.0");
    ctx->KeyPress(ImGuiKey_RightBracket);
    IM_CHECK_STR_EQ(pc.time_box().position.c_str(), "m58.1.0");
    IM_CHECK_STR_EQ(pc.next_act_box().header.c_str(), "Next: activation 2 of 3");
    ctx->KeyPress(ImGuiKey_LeftBracket);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), act1, 0.5);
    // Nothing before the first activation: the playhead stays.
    ctx->ItemClick("**/< Act##prevact");
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), act1, 0.5);

    // A jump keeps playing when playing.
    ctx->ItemClick("**/Play");
    IM_CHECK(pc.playing());
    ctx->KeyPress(ImGuiKey_RightBracket);
    IM_CHECK(pc.playing());
    ctx->ItemClick("**/Pause");
    IM_CHECK(!pc.playing());

    const std::string readout = pc.sp_meter_readout();
    IM_CHECK(readout.size() >= 5);
    IM_CHECK(readout.substr(readout.size() - 2) == "/4");
    pc.seek_ms(pc.length_ms());
    IM_CHECK_STR_EQ(pc.time_box().length.c_str(), "m96.3.240");
    IM_CHECK_STR_EQ(pc.time_box().position.c_str(), pc.time_box().length.c_str());
    IM_CHECK(pc.time_box().tempo.rfind("BPM ", 0) == 0);

    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("[ ] previous/next activation") != std::string::npos);
}

}  // namespace

const std::vector<TestEntry>& preview_tests() {
    static const std::vector<TestEntry> entries = {
        {"preview", test_preview},
        {"analyze-on-preview", test_analyze_on_preview},
        {"preview-path-overlay", test_preview_path_overlay},
        {"preview-controls", test_preview_controls},
        {"preview-drain-box", test_preview_drain_box},
        {"preview-overlay-fit", test_preview_overlay_fit},
        {"preview-buttons-keys", test_preview_buttons_keys},
        {"scrub-hold", test_scrub_hold},
        {"layout-drift", test_layout_drift},
        {"preview-path-picker", test_preview_path_picker},
        {"preview-activation-jumps", test_preview_activation_jumps},
        {"preview-error-wraps", test_preview_error_wraps},
        {"preview-overlay-steady", test_preview_overlay_steady},
    };
    return entries;
}

}  // namespace uitest
