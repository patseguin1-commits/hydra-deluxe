// The Paths tab's GUI tests, driven on Burnout (Green Day) from the scratch
// library: the path buttons, the activation rows and their folds, the backend
// table and its limit, the two folds under the list, and Copy path. Labels are
// the plan's label contract (docs/superpowers/plans/2026-09-27-ui-redesign.md).

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "app/path_view.h"
#include "core/model.h"
#include "imgui_internal.h"
#include "ui/activation_row_layout.h"
#include "ui/app_state.h"
#include "ui/details_view.h"
#include "ui/fonts.h"  // px()
#include "ui/preview_controller.h"

namespace uitest {

namespace {

// Keeps a test off the real Windows clipboard: while alive, ImGui reads and
// writes text() instead, and the previous handlers come back afterwards.
struct FakeClipboard {
    static std::string& text() {
        static std::string t;
        return t;
    }
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    const char* (*old_get)(ImGuiContext*) = pio.Platform_GetClipboardTextFn;
    void (*old_set)(ImGuiContext*, const char*) = pio.Platform_SetClipboardTextFn;
    FakeClipboard() {
        text().clear();
        pio.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
            return text().c_str();
        };
        pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* s) {
            text() = s ? s : "";
        };
    }
    ~FakeClipboard() {
        pio.Platform_GetClipboardTextFn = old_get;
        pio.Platform_SetClipboardTextFn = old_set;
    }
};

bool on_screen(Harness& h, const std::string& s) {
    return visible_text(h).find(s) != std::string::npos;
}

// A fresh app with Burnout analyzed and its Paths tab showing.
bool open_burnout(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return false;
    open_titled(ctx, "burnout", "Burnout");
    if (ctx->IsError()) return false;
    analyze_open_song(ctx);  // clicks the Paths tab first, then analyzes
    if (ctx->IsError()) return false;
    IM_CHECK_RETV(h.app->viewed.record->best_path().pathstring() == "3- 1 2", false);
    ctx->Yield(2);
    return true;
}

// The path list: three headings, the buttons' titles, each path's own timing
// and the detail line, and a click that changes the shared selection.
void test_paths_list(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    const std::string dot = " \xC2\xB7 ";
    IM_CHECK(on_screen(h, "Optimal"));
    IM_CHECK(on_screen(h, "Within 2 scores"));
    IM_CHECK(on_screen(h, "Best all-0 path"));
    IM_CHECK(!on_screen(h, "0 ms limit"));
    IM_CHECK(on_screen(h, "378,315" + dot + "3- 1 2"));
    IM_CHECK(on_screen(h, "163.0 ms"));
    IM_CHECK(!on_screen(h, "hardest squeeze"));
    IM_CHECK(on_screen(h, "378,175" + dot + "0 4 1"));
    IM_CHECK(on_screen(h, "375,955" + dot + "0 0 0 0"));
    IM_CHECK(on_screen(h, "2,360 below optimal"));
    // The old list's headings are gone.
    IM_CHECK(!on_screen(h, "Optimal Path"));
    IM_CHECK(!on_screen(h, "More Paths"));

    IM_CHECK(h.app->details_ui.selected_path == &h.app->viewed.record->best_path());
    ctx->ItemClick("**/##path1");
    ctx->Yield(2);
    IM_CHECK(h.app->details_ui.selected_path != nullptr);
    IM_CHECK(h.app->details_ui.selected_path->pathstring() == "0 4 1");
    // A new path opens on its first row.
    IM_CHECK(h.app->details_ui.paths_tab.ui().act_open.size() == 3);
    IM_CHECK(h.app->details_ui.paths_tab.ui().act_open[0] == 1);
    ctx->ItemClick("**/##path3");
    ctx->Yield(2);
    IM_CHECK(h.app->details_ui.selected_path->pathstring() == "0 0 0 0");
}

// The activation rows: one line each, the first open, one open at a time,
// Expand all and Collapse all, and the plain sentence instead of the old line.
void test_paths_rows(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    const std::string dot = " \xC2\xB7 ";
    IM_CHECK(on_screen(h, "Activations"));
    IM_CHECK(on_screen(h, "3" + dot + "no SP left over"));
    IM_CHECK(!on_screen(h, "bars each"));
    IM_CHECK(on_screen(h, "m32.1.0"));
    IM_CHECK(on_screen(h, "m58.1.0"));
    IM_CHECK(on_screen(h, "m88.1.0"));
    IM_CHECK(on_screen(h, "squeeze out 163 ms"));
    // Row 1 starts open: its chord and sentence show, rows 2 and 3 are shut.
    IM_CHECK(on_screen(h, "[Kick - GreenCym]"));
    IM_CHECK(on_screen(h, "Hit the [  Y  ] note more than 163.0 ms late so it lands after "
                          "Star Power ends."));
    IM_CHECK(on_screen(h, "3 notes near the SP end"));
    IM_CHECK(!on_screen(h, "6 notes near the SP end"));
    IM_CHECK(!on_screen(h, "SqOut: Note timing"));
    IM_CHECK(!on_screen(h, "Frontend:"));

    // "Show in Preview" asks the Preview for activation 1 (Task 11 consumes it)
    // and switches to the Preview tab, which starts the Preview.
    ctx->ItemClick("**/Show in Preview >##showact1");
    IM_CHECK(h.app->details_ui.paths_tab.ui().preview_jump == std::optional<size_t>(0));
    IM_CHECK(wait_until(ctx, [&] { return h.app->preview && h.app->preview->active(); }, 10));
    IM_CHECK(wait_until(ctx, [&] { return !h.app->preview->loading(); }, 120));
    h.app->details_ui.paths_tab.ui().preview_jump.reset();
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);

    ctx->ItemClick("**/##act2");  // opens row 2, closes row 1
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "6 notes near the SP end"));
    IM_CHECK(!on_screen(h, "3 notes near the SP end"));
    ctx->ItemClick("**/##act2");  // closes it again
    ctx->Yield(2);
    IM_CHECK(!on_screen(h, "6 notes near the SP end"));

    ctx->ItemClick("**/Expand all");
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "3 notes near the SP end"));
    IM_CHECK(on_screen(h, "6 notes near the SP end"));
    IM_CHECK(on_screen(h, "7 notes near the SP end"));
    IM_CHECK(h.app->details_ui.paths_tab.ui().all_open());
    ctx->ItemClick("**/Collapse all");
    ctx->Yield(2);
    IM_CHECK(!on_screen(h, "3 notes near the SP end"));
    IM_CHECK(!on_screen(h, "7 notes near the SP end"));
}

// The backend table folds per activation, and the Backend limit moved here.
void test_paths_backend_timings(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    IM_CHECK(!on_screen(h, "Insane SqOut"));
    ctx->ItemClick("**/Backend timings##act1");
    ctx->Yield(2);
    IM_CHECK(h.app->details_ui.paths_tab.ui().backends_open[0] == 1);
    IM_CHECK(on_screen(h, "Insane SqOut <-- squeezed out (-260)"));
    IM_CHECK(on_screen(h, "Timing is how far each note sits from the Star Power end"));

    // Off by default, and the number box is inert until it is ticked.
    IM_CHECK(!h.app->settings.backendlimit_enabled);
    IM_CHECK_EQ(h.app->settings.backendlimit_value, 50);
    IM_CHECK((ctx->ItemInfo("**/##backendlimitvalue").ItemFlags & ImGuiItemFlags_Disabled) != 0);
    ctx->ItemClick("**/Hide backend rows beyond##backendlimit");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_enabled; }, 5));
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).backendlimit_enabled);

    // At 30 ms the -489.1 and -326.1 rows go; the squeezed-out -163.0 row stays.
    ctx->ItemInputValue("**/##backendlimitvalue", 30);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_value == 30; }, 5));
    IM_CHECK_EQ(hydra::app::Settings::load_file(h.ini_path).backendlimit_value, 30);
    IM_CHECK(wait_until(ctx, [&] { return on_screen(h, "1 note near the SP end"); }, 5));
    IM_CHECK(on_screen(h, "squeezed out (-260)"));

    // The full engine window (500 ms) is reachable; beyond it clamps back.
    ctx->ItemInputValue("**/##backendlimitvalue", 500);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_value == 500; }, 5));
    ctx->ItemInputValue("**/##backendlimitvalue", 600);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_value == 500; }, 5));

    // Display only: the record is still the analyzed one. Unticking restores
    // every row and persists too.
    IM_CHECK(h.app->viewed.status == hydra::store::RecordStatus::Ready);
    ctx->ItemClick("**/Hide backend rows beyond##backendlimit");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->settings.backendlimit_enabled; }, 5));
    IM_CHECK(!hydra::app::Settings::load_file(h.ini_path).backendlimit_enabled);
    IM_CHECK(wait_until(ctx, [&] { return on_screen(h, "3 notes near the SP end"); }, 5));
}

// The two folds under the list, and Copy path with its "Copied!" flash.
void test_paths_folds_copy(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    FakeClipboard clipboard;

    IM_CHECK(on_screen(h, "+15"));  // beside the Multiplier squeeze fold
    IM_CHECK(!on_screen(h, "Hit [Red] first."));
    ctx->ItemClick("**/Multiplier squeeze##mult");
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "Hit [Red] first."));
    IM_CHECK(on_screen(h, "2x   (+15 pts):   [Red - YellowCym]"));

    IM_CHECK(!on_screen(h, "Total Score:"));
    ctx->ItemClick("**/Score breakdown##breakdown");
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "Total Score:"));
    IM_CHECK(on_screen(h, "Avg. Multiplier:"));
    ctx->ItemClick("**/Score breakdown##breakdown");
    ctx->Yield(2);
    IM_CHECK(!on_screen(h, "Total Score:"));

    IM_CHECK(!on_screen(h, "Copied!"));
    ctx->ItemClick("**/Copy path");
    ctx->Yield(2);
    const hydra::HydraRecord& rec = *h.app->viewed.record;
    IM_CHECK_STR_EQ(FakeClipboard::text().c_str(),
                    rec.best_path().pathstring_verbose(rec.multsqueezes).c_str());
    IM_CHECK(on_screen(h, "Copied!"));

    // Ctrl+C goes through the same call: it copies again and still flashes.
    FakeClipboard::text().clear();
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
    ctx->Yield(2);
    IM_CHECK_STR_EQ(FakeClipboard::text().c_str(),
                    rec.best_path().pathstring_verbose(rec.multsqueezes).c_str());
    IM_CHECK(on_screen(h, "Copied!"));
}

// A squeezed-out row the engine never counted: the table says "(uncounted)"
// and the sentence says it costs nothing. Found by the skipped doctest "find a
// chart with an uncounted squeezed-out row" (tests/test_path_view.cpp).
void test_paths_uncounted(ImGuiTestContext* ctx) {
    static const char* kTitle = "Tapestry of the Starless Abstract (Shortened)";
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_titled(ctx, "tapestry", kTitle);
    if (ctx->IsError()) return;
    analyze_open_song(ctx);
    if (ctx->IsError()) return;
    ctx->ItemClick("**/Expand all");
    ctx->Yield(2);
    const size_t rows = h.app->details_ui.paths_tab.ui().act_open.size();
    IM_CHECK(rows > 0);
    for (size_t i = 1; i <= rows; ++i) {
        ctx->ItemClick(("**/Backend timings##act" + std::to_string(i)).c_str());
        ctx->Yield(1);
    }
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "squeezed out (uncounted)"));
    IM_CHECK(on_screen(h, "It costs no points, because Hydra's score never counted that "
                          "note under Star Power"));
}

// A window drawn this frame whose name holds `part` (child names are mangled).
ImGuiWindow* window_named(const char* part) {
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->WasActive && std::strstr(w->Name, part)) return w;
    return nullptr;
}

// At the panel's narrowest the Paths tab still fits: with every row, a
// backend table and "Copied!" showing, nothing in the right column runs past
// its edge, and the path list keeps at least its 240 px (Burnout's optimal
// row, with its timing beside it, is a little wider than that).
void test_paths_fit_narrow(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    FakeClipboard clipboard;
    hydra::ui::remember_library_share(0.99f);  // the library as wide as it goes
    h.app->library_ui.panel_was_open = false;   // the split sets its width next frame
    ctx->Yield(3);
    ImGuiWindow* panel = ctx->WindowInfo("//Hydra/##songpanel").Window;
    IM_CHECK_FLOAT_NEAR_EQ(panel->Size.x, hydra::ui::px(hydra::ui::kMinSongPanelW), 1.0f);

    ctx->ItemClick("**/Expand all");
    ctx->Yield(1);
    ctx->ItemClick("**/Backend timings##act1");
    ctx->ItemClick("**/Copy path");
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "Copied!"));
    ImGuiWindow* details = window_named("##pathdetails");
    ImGuiWindow* list = window_named("##pathlist");
    IM_CHECK(details != nullptr && list != nullptr);
    if (ctx->IsError()) return;
    IM_CHECK_LE(details->ContentSize.x, details->ContentRegionRect.GetWidth() + 0.5f);
    IM_CHECK_GE(list->Size.x, hydra::ui::px(hydra::ui::kMinPathListW) - 0.5f);
}

// The scratch library's longest path (9 activations): the path list takes at
// most its share of the tab, the activations get the rest, and nothing in
// either column runs past its edge (a long title wraps instead).
void test_paths_long_path(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_titled(ctx, "spiraling void", "The Spiraling Void");
    if (ctx->IsError()) return;
    analyze_open_song(ctx);
    if (ctx->IsError()) return;
    ctx->Yield(2);
    for (float share : {0.01f, 0.99f}) {  // the widest panel, then the narrowest
        hydra::ui::remember_library_share(share);
        h.app->library_ui.panel_was_open = false;
        ctx->Yield(3);
        ImGuiWindow* list = window_named("##pathlist");
        ImGuiWindow* details = window_named("##pathdetails");
        IM_CHECK(list != nullptr && details != nullptr);
        if (ctx->IsError()) return;
        const float tab = details->Pos.x + details->Size.x - list->Pos.x;
        IM_CHECK_LE(list->Size.x, (std::max)(hydra::ui::px(hydra::ui::kMinPathListW),
                                             tab * hydra::ui::kMaxPathListShare) + 1.0f);
        IM_CHECK_GE(details->Size.x, hydra::ui::px(hydra::ui::kMinPathDetailsW) - 1.0f);
        IM_CHECK_LE(list->ContentSize.x, list->ContentRegionRect.GetWidth() + 0.5f);
        IM_CHECK_LE(details->ContentSize.x, details->ContentRegionRect.GetWidth() + 0.5f);
    }
}

// A width in the font the app draws with (`font` null: the default one).
float text_w(const char* s, ImFont* font = nullptr) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float size = st.FontSizeBase * st.FontScaleMain * st.FontScaleDpi;
    if (!font) font = ImGui::GetIO().FontDefault ? ImGui::GetIO().FontDefault : ImGui::GetFont();
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, s).x;
}

// The narrowest song panel: the library as wide as it goes.
bool narrowest_panel(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    hydra::ui::remember_library_share(0.99f);
    h.app->library_ui.panel_was_open = false;  // the split sets its width next frame
    ctx->Yield(3);
    ImGuiWindow* panel = ctx->WindowInfo("//Hydra/##songpanel").Window;
    IM_CHECK_RETV(panel != nullptr, false);
    IM_CHECK_RETV(std::fabs(panel->Size.x - hydra::ui::px(hydra::ui::kMinSongPanelW)) <= 1.0f,
                  false);
    return true;
}

// The backend table drawn this frame for activation `number` (its id starts
// "##backends<number>_"; the rest is its column widths).
ImGuiTable* backend_table(int number) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* details = window_named("##pathdetails");
    if (!details) return nullptr;
    const std::string prefix = "##backends" + std::to_string(number) + "_";
    for (int n = 0; n < g.Tables.GetMapSize(); ++n) {
        ImGuiTable* t = g.Tables.TryGetMapData(n);
        if (!t || t->ColumnsCount != 4 || t->LastFrameActive < g.FrameCount - 2) continue;
        if (t->OuterWindow != details) continue;
        // Match by id, rebuilt from the widths the table was set up with.
        char id[64];
        std::snprintf(id, sizeof(id), "%s%d_%d_%d", prefix.c_str(),
                      static_cast<int>(t->Columns[0].InitStretchWeightOrWidth),
                      static_cast<int>(t->Columns[1].InitStretchWeightOrWidth),
                      static_cast<int>(t->Columns[2].InitStretchWeightOrWidth));
        if (t->ID == details->GetID(id)) return t;
    }
    return nullptr;
}

// At the narrowest panel, Burnout's first backend table: Timing, Chord and
// Points are as wide as their text, and the squeezed-out rating, the longest
// line, wraps inside its Rating cell instead of running past it.
void test_paths_backend_fit(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    if (!narrowest_panel(ctx)) return;
    ctx->ItemClick("**/Backend timings##act1");
    ctx->Yield(3);
    IM_CHECK(on_screen(h, "Insane SqOut <-- squeezed out (-260)"));
    ImGuiTable* t = backend_table(1);
    IM_CHECK(t != nullptr);
    if (ctx->IsError()) return;
    // Rating's content never reaches past its cell's right edge.
    const ImGuiTableColumn& rating = t->Columns[3];
    const float content_max = (std::max)(rating.ContentMaxXUnfrozen, rating.ContentMaxXFrozen);
    ctx->LogInfo("rating column %.1f..%.1f, content to %.1f", rating.WorkMinX, rating.WorkMaxX,
                 content_max);
    IM_CHECK_LE(content_max, rating.WorkMaxX + 0.5f);
    // The squeezed-out line is longer than the cell, so it did wrap: the
    // cell is narrower than the line.
    const float line_w = text_w("Insane SqOut <-- squeezed out (-260)", hydra::ui::g_mono_font);
    IM_CHECK_LT(rating.WorkMaxX - rating.WorkMinX, line_w);
    // The three fixed columns fit their text: no cell runs past its column.
    for (int c = 0; c < 3; ++c) {
        const ImGuiTableColumn& col = t->Columns[c];
        IM_CHECK_LE((std::max)(col.ContentMaxXUnfrozen, col.ContentMaxXFrozen), col.WorkMaxX + 0.5f);
        IM_CHECK_LT(col.WidthGiven, hydra::ui::px(80.0f));
    }
    // And the table stays inside the details column.
    ImGuiWindow* details = window_named("##pathdetails");
    IM_CHECK_LE(details->ContentSize.x, details->ContentRegionRect.GetWidth() + 0.5f);
    ctx->LogInfo("fixed widths %.1f %.1f %.1f", t->Columns[0].WidthGiven, t->Columns[1].WidthGiven,
                 t->Columns[2].WidthGiven);
}

// The activation rows at the narrowest panel: the measure, the bars and the
// badge never overlap, on Burnout's own rows and on the longest pieces real
// charts have (an 11-character measure, a two-digit bar count, the longest
// badge wording with a 3-digit time).
void test_paths_row_layout(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    if (!narrowest_panel(ctx)) return;
    const float row_w = ctx->ItemInfo("**/##act1").RectFull.GetWidth();
    IM_CHECK_GT(row_w, 0.0f);
    const float scale = hydra::ui::px(1.0f);
    const float gap = hydra::ui::px(hydra::ui::kRowBarsGap);
    ImFont* mono = hydra::ui::g_mono_font;

    auto check = [&](const std::string& widest_measure, const std::string& measure,
                     const std::string& bars, const std::string& badge) {
        const hydra::ui::ActivationRowLayout l = hydra::ui::activation_row_layout(
            text_w(widest_measure.c_str(), mono), badge.empty() ? 0.0f : text_w(badge.c_str()),
            row_w, scale);
        const float measure_end = l.measure_x + text_w(measure.c_str(), mono);
        const float bars_end = l.bars_x + text_w(bars.c_str());
        ctx->LogInfo("measure %s ends %.1f, bars at %.1f..%.1f, badge pill from %.1f (row %.1f)",
                     measure.c_str(), measure_end, l.bars_x, bars_end, l.badge_pill_min, row_w);
        IM_CHECK_LE(measure_end + gap, l.bars_x + 0.01f);
        IM_CHECK_GE(l.bars_x, hydra::ui::px(hydra::ui::kRowMinBarsX));
        if (!badge.empty()) IM_CHECK_LT(bars_end, l.badge_pill_min);
    };
    // The same call the tab makes, so this is the cached view it draws.
    hydra::ui::AppState& app = *h.app;
    const hydra::app::ActivationsView& view =
        app.details_ui.paths_tab
            .details(*app.details_ui.selected_path, *app.viewed.record, app.record_generation.n,
                     app.viewed.timing ? &*app.viewed.timing : nullptr,
                     static_cast<double>(app.settings.hit_window_ms), app.settings.backend_limit(),
                     app.settings.rules, app.viewed.song_length_ms)
            .activations;
    std::string widest;
    for (const hydra::app::ActivationRowView& a : view.acts)
        if (text_w(a.measure.c_str(), mono) > text_w(widest.c_str(), mono)) widest = a.measure;
    IM_CHECK(!view.acts.empty());
    for (const hydra::app::ActivationRowView& a : view.acts) check(widest, a.measure, a.bars, a.badge);
    check("m1024.1.120", "m1024.1.120", "12 bars", "calibration fill 999 ms");
    check("m1024.1.120", "m1024.1.120", "12 bars", "squeeze out 999 ms");
}

// A result saved before Hydra stored song lengths has none, so the timeline
// has no end. Opening the song reads the chart for it and the timeline shows,
// with no re-analysis: the same length an analysis stores, the same record.
void test_paths_length_backfill(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    hydra::ui::AppState& app = *h.app;
    IM_CHECK(app.viewed.song_length_ms.has_value());
    if (!app.viewed.song_length_ms) return;
    const double stored = *app.viewed.song_length_ms;
    const hydra::HydraRecord* record = &*app.viewed.record;
    IM_CHECK(on_screen(h, "m96"));  // the timeline's end label

    // As an old result reads: no length.
    app.viewed.song_length_ms.reset();
    IM_CHECK(wait_until(ctx, [&] { return app.viewed.song_length_ms.has_value(); }, 10));
    if (!app.viewed.song_length_ms) return;
    IM_CHECK_FLOAT_NEAR_EQ(static_cast<float>(*app.viewed.song_length_ms),
                           static_cast<float>(stored), 0.01f);
    IM_CHECK(&*app.viewed.record == record);  // not re-read, not re-analyzed
    IM_CHECK(!app.analyze_job);
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "m96"));
}

}  // namespace

// Registers this file's tests; register_tests() (uitest_tests.cpp) calls it.
void register_paths_tests(Harness& h) {
    struct Entry {
        const char* name;
        void (*fn)(ImGuiTestContext*);
    };
    const Entry entries[] = {
        {"paths-list", test_paths_list},
        {"paths-rows", test_paths_rows},
        {"paths-backend-timings", test_paths_backend_timings},
        {"paths-folds-copy", test_paths_folds_copy},
        {"paths-uncounted", test_paths_uncounted},
        {"paths-fit-narrow", test_paths_fit_narrow},
        {"paths-long-path", test_paths_long_path},
        {"paths-backend-fit", test_paths_backend_fit},
        {"paths-row-layout", test_paths_row_layout},
        {"paths-length-backfill", test_paths_length_backfill},
    };
    for (const Entry& e : entries) {
        ImGuiTest* t = IM_REGISTER_TEST(h.engine, "hydra", e.name);
        t->UserData = &h;
        t->TestFunc = e.fn;
    }
}

}  // namespace uitest
