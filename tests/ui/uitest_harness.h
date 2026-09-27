// hydra_uitest — the headless GUI test runner (docs/agents/ui-testing.md).
//
// Runs Hydra's real ImGui UI with no window: a WARP (software) D3D11 device,
// an offscreen render target, and the Dear ImGui Test Engine injecting input.
// Tests find widgets by label ("Scan charts"), click them, and read back the
// app's state and the text on screen. Everything runs on scratch files in a
// temp folder; no browser, sound card, or network is touched.

#ifndef HYDRA_TESTS_UI_UITEST_HARNESS_H
#define HYDRA_TESTS_UI_UITEST_HARNESS_H

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include "imgui_te_context.h"
#include "imgui_te_engine.h"
#include "ui/app_shell.h"
#include "ui/app_state.h"


namespace uitest {

struct Harness {
    // Offscreen D3D11 (WARP) + the render target ImGui draws into.
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> rt;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
    int width = 1280;
    int height = 800;

    // Scratch files. temp_dir holds the settings INI, the DB, and the HTML
    // reports the jobs write next to the DB.
    std::string temp_dir;
    std::string db_path;
    std::string ini_path;
    std::string rules_path;
    std::string shots_dir;  // where `screenshot` files go (default: temp_dir)
    bool keep_temp = false;

    std::unique_ptr<hydra::ui::AppState> app;
    ImGuiTestEngine* engine = nullptr;
    hydra::ui::FrameText frame_text;

    // Every path the app asked to open in a browser (the seam records, it
    // never launches one).
    std::vector<std::wstring> opened_urls;

    // Command file for the "script" test (--script).
    std::string script_path;

    // Headless: WARP device, offscreen render target, ImGui context, engine.
    bool init();
    void frame();     // one full ImGui frame + engine PostSwap
    void shutdown();  // engine, ImGui, scratch dir

    // Attached (Hydra.exe --uitest): the app already owns the window, device,
    // and ImGui context; the harness adds scratch paths, the engine (at a
    // watchable speed), and captures from the swapchain. main.cpp drives the
    // frames itself and calls engine_post_swap() after Present.
    bool init_attached(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* swapchain);
    bool attached = false;
    IDXGISwapChain* swapchain = nullptr;  // attached only
    // Queue a test by name or, when `what` names a file, as a script.
    bool queue(const std::string& what);
    // Print [PASS]/[FAIL] per run test (+ the log of each failure) to `out`;
    // returns the failure count.
    int print_results(FILE* out);
    // Stop the engine and drop the app. Must run before ImGui::DestroyContext;
    // shutdown() calls it too (idempotent).
    void stop();
    bool stopped = false;
};

// Fresh app state for a test: rewrite the scratch INI (song folders =
// testdata/input, auto-open off), delete the DB, rebuild AppState on those
// paths, and (re)install the headless seams. Call at the start of TestFunc —
// the GUI thread is parked between frames while TestFunc runs, so swapping
// the AppState here is safe.
// rules_text: the scratch hydra_rules.ini's contents. Empty (the default)
// means no rules file, so every other test runs today's rules.
void reset_app(Harness& h, const std::string& rules_text = "");

// Register every C++ test (the uitest_<area>.cpp files, in the order
// uitest_tests.cpp fixes) and, when h.script_path is set, the "script" test
// (uitest_script.cpp). Each test's UserData is &h.
void register_tests(Harness& h);
void register_script_test(Harness& h);

// ---- helpers shared by the tests and the script interpreter --------------

inline Harness& harness(ImGuiTestContext* ctx) {
    return *static_cast<Harness*>(ctx->Test->UserData);
}

// Yield frames until pred() holds or `seconds` of wall-clock pass. Jobs run
// on real threads, so the wait is wall-clock, not frame-count.
bool wait_until(ImGuiTestContext* ctx, const std::function<bool()>& pred, double seconds);

// True while any background job (scan, batch, analyze, report, DM fetch/report)
// or the Preview's load is still running.
bool jobs_busy(Harness& h);

// All text ImGui drew last frame plus the status line, for substring checks.
std::string visible_text(Harness& h);

// Print the widget tree (label, id, state flags, rect) of one window, or of
// every window when `window_name` is empty.
void dump_widgets(ImGuiTestContext* ctx, const std::string& window_name);

// Print the key AppState fields.
void dump_state(Harness& h);

// Save the current frame to `file` (PNG). Returns false if the capture failed.
bool screenshot(ImGuiTestContext* ctx, const std::string& file);

// Escape '/' and '#' in a label so it can be used as one path segment of an
// ImGuiTestRef ("**/" + escape(title)).
std::string escape_ref(const std::string& label);

// ---- the checked-in C++ tests ---------------------------------------------

// One checked-in test: the name --test and --list use, and its body.
struct TestEntry {
    const char* name;
    void (*fn)(ImGuiTestContext*);
};

// Each area file's tests, in the order that file lists them.
const std::vector<TestEntry>& library_tests();       // uitest_library.cpp
const std::vector<TestEntry>& details_tests();       // uitest_details.cpp
const std::vector<TestEntry>& preview_tests();       // uitest_preview.cpp
const std::vector<TestEntry>& batch_report_tests();  // uitest_batch_reports.cpp

// Steps most tests start with (defined in uitest_harness.cpp).
// Scan testdata/input through the UI and land on the populated library.
void scan_library(ImGuiTestContext* ctx);
// Point the ref at the song panel child (//Hydra/##songpanel).
void set_panel_ref(ImGuiTestContext* ctx);
// Click row `index` of the library view, wait for the song panel, point the
// ref at it, and land on its Paths tab.
void open_details(ImGuiTestContext* ctx, size_t index);
// Type `search` into the library's search box, then open the row titled `title`.
void open_titled(ImGuiTestContext* ctx, const std::string& search, const std::string& title);
// "**/Analyze this song" before a result, "**/Re-analyze" after.
std::string analyze_button_ref(Harness& h);
// Analyze the open song from the panel and wait for a Ready record.
void analyze_open_song(ImGuiTestContext* ctx);
// Fresh app, scan, open chart 0's Preview and wait for the load. False on error.
bool open_preview(ImGuiTestContext* ctx);

}  // namespace uitest

#endif  // HYDRA_TESTS_UI_UITEST_HARNESS_H
