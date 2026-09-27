// The Paths tab's GUI tests, driven on Burnout (Green Day) from the scratch
// library: the path buttons, the activation rows and their folds, the backend
// table and its limit, the two folds under the list, and Copy path. Labels are
// the plan's label contract (docs/superpowers/plans/2026-09-27-ui-redesign.md).

#include <cstring>
#include <optional>
#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "app/path_view.h"
#include "core/model.h"
#include "imgui_internal.h"
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

// The path list: three headings, the buttons' titles and detail lines, and a
// click that changes the shared selection.
void test_paths_list(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    const std::string dot = " \xC2\xB7 ";
    IM_CHECK(on_screen(h, "Optimal"));
    IM_CHECK(on_screen(h, "Within 2 scores"));
    IM_CHECK(on_screen(h, "Best at 0 ms limit"));
    IM_CHECK(on_screen(h, "378,315" + dot + "3- 1 2"));
    IM_CHECK(on_screen(h, "hardest squeeze 163.0 ms"));
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
    IM_CHECK(on_screen(h, "3" + dot + "3 bars each" + dot + "no SP left over"));
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
// its edge, and the path list keeps its 240 px.
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
    IM_CHECK_FLOAT_NEAR_EQ(list->Size.x, hydra::ui::px(hydra::ui::kMinPathListW), 0.5f);
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
    };
    for (const Entry& e : entries) {
        ImGuiTest* t = IM_REGISTER_TEST(h.engine, "hydra", e.name);
        t->UserData = &h;
        t->TestFunc = e.fn;
    }
}

}  // namespace uitest
