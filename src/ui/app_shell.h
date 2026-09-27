// The window-independent half of the GUI shell: ImGui context setup, the UI
// scale, the remembered window placement, and the per-frame draw, shared by
// Hydra.exe (src/ui/main.cpp) and the headless GUI test runner (tests/ui,
// docs/agents/ui-testing.md). main.cpp keeps only the Win32 window,
// swapchain, and message pump; the runner swaps those for an offscreen render
// target and the Test Engine's injected input.

#ifndef HYDRA_UI_APP_SHELL_H
#define HYDRA_UI_APP_SHELL_H

#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"

namespace hydra::ui {

class AppState;

struct ImGuiSetupOptions {
    // Monitor DPI scale; sizes, fonts, and px() all follow it.
    float dpi_scale = 1.0f;
    // Where ImGui persists table column widths. Empty = the exe-relative
    // default (hydra_ui.ini); "-" = don't persist at all (tests).
    std::string ini_file;
    // Directory holding the fonts. Empty = exe_dir()\resource.
    std::string resource_dir;
};

// CreateContext + flags + theme + DPI scaling + fonts. The platform/renderer
// backends (ImGui_ImplWin32_Init / ImGui_ImplDX11_Init) are the caller's:
// the app has a window, the runner has none.
void setup_imgui(const ImGuiSetupOptions& options);
void shutdown_imgui();  // DestroyContext

// ---- The main window's placement, kept in hydra_ui.ini ----------------

// A rectangle in virtual-screen pixels, the units of GetWindowRect and of a
// monitor's work area. right/bottom are one past the last pixel.
struct ScreenRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    // Spelled out: the project builds as C++17, which has no defaulted ==.
    bool operator==(const ScreenRect& o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
    bool operator!=(const ScreenRect& o) const { return !(*this == o); }
};

// Where the main window was when Hydra last closed.
struct WindowPlacement {
    bool valid = false;  // false: nothing saved yet, so use the default
    ScreenRect normal;   // the un-maximized rectangle
    bool maximized = false;
    bool operator==(const WindowPlacement& o) const {
        return valid == o.valid && normal == o.normal && maximized == o.maximized;
    }
    bool operator!=(const WindowPlacement& o) const { return !(*this == o); }
};

// How much of a saved window's title-bar band must sit on one monitor for
// the user to grab it: this many pixels wide, half as many tall.
inline constexpr int kMinVisiblePx = 64;

// True when the top kMinVisiblePx rows of `r` (where the title bar is)
// overlap one of `work_areas` by at least kMinVisiblePx wide and
// kMinVisiblePx / 2 tall. False for a rectangle smaller than that.
bool placement_on_screen(const ScreenRect& r, const std::vector<ScreenRect>& work_areas);

// The body of the [Hydra][Window] section: "Pos=x,y", "Size=w,h" and
// "Maximized=0|1", one per line. Parsing takes one line at a time, in any
// order, and ignores lines it doesn't know.
std::string format_window_placement(const WindowPlacement& p);
void parse_window_placement_line(std::string_view line, WindowPlacement& p);

// The placement setup_imgui read from hydra_ui.ini, updated by every
// remember_window_placement since. valid is false when there was none.
WindowPlacement window_placement();
// Records where the window is now. It reaches hydra_ui.ini with ImGui's own
// settings: a few seconds later, and again when ImGui shuts down.
void remember_window_placement(const WindowPlacement& p);

// ---- The library/song-panel split, kept in hydra_ui.ini ---------------

// The library's share of the main window's width while the song panel is
// open, until the user drags the edge between them.
inline constexpr float kDefaultLibraryShare = 0.4f;

// The body of the [Hydra][Layout] section: "LibraryShare=<0..1>". Parsing
// takes one line and ignores lines it doesn't know and shares outside (0, 1).
std::string format_layout(float library_share);
void parse_layout_line(std::string_view line, float& library_share);

// The share setup_imgui read from hydra_ui.ini (kDefaultLibraryShare when
// there was none), updated by every remember_library_share since.
float library_share();
// Records a split the user dragged to. It reaches hydra_ui.ini with ImGui's
// own settings, like the window placement.
void remember_library_share(float share);

// ---- UI scale ----------------------------------------------------------

// The UI scale for a monitor's DPI: 96 DPI is 1.0. A DPI of 0 reads as 1.0.
float ui_scale_for_dpi(unsigned dpi);

// The theme at `scale`: base with ScaleAllSizes(scale) and FontScaleDpi =
// scale. Always from the unscaled base, because ImGui rounds every size on
// each ScaleAllSizes call, so scaling an already-scaled style drifts.
ImGuiStyle scaled_style(const ImGuiStyle& base, float scale);

// Rescales the running UI to `scale`: ImGui's paddings and sizes, the font
// size, and px(). setup_imgui calls it once; main.cpp calls it again when the
// window lands on a monitor with another scale. Call between frames only.
void set_ui_scale(float scale);

// Everything ImGui drew this frame, as text, in draw order. Filled by
// run_frame when `enabled`; the runner's wait-text/expect-text search it.
struct FrameText {
    bool enabled = false;
    std::string text;
};

// One frame of Hydra's UI between ImGui::NewFrame() and ImGui::Render():
// the library window and the details modal.
void run_frame(AppState& app, FrameText* capture = nullptr);

}  // namespace hydra::ui

#endif  // HYDRA_UI_APP_SHELL_H
