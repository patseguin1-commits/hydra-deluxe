// The window-independent half of the GUI shell: ImGui context setup and the
// per-frame draw, shared by Hydra.exe (src/ui/main.cpp) and the headless GUI
// test runner (tests/ui, docs/agents/ui-testing.md). main.cpp keeps only the
// Win32 window, swapchain, and message pump; the runner swaps those for an
// offscreen render target and the Test Engine's injected input.

#ifndef HYDRA_UI_APP_SHELL_H
#define HYDRA_UI_APP_SHELL_H

#include <string>

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
