#include "ui/app_shell.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "imgui.h"
#include "imgui_internal.h"  // g.LogBuffer for FrameText

#include "app/config.h"
#include "ui/app_state.h"
#include "ui/details_view.h"
#include "ui/fonts.h"
#include "ui/library_view.h"
#include "ui/theme.h"

namespace hydra::ui {

void setup_imgui(const ImGuiSetupOptions& options) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // No docking/viewports: Hydra is one primary window filling the OS
    // window, like hydra_app.py's dpg.set_primary_window -- not a docking
    // workspace with panels that can be torn into their own OS windows.

    // Persist ImGui state (library table column widths) next to the exe. The
    // default cwd-relative "imgui.ini" landed wherever the app happened to be
    // launched from.
    static std::string ini_file;
    if (options.ini_file == "-") {
        io.IniFilename = nullptr;
    } else {
        ini_file = options.ini_file.empty()
                       ? app::exe_dir() + "\\hydra_ui.ini"
                       : options.ini_file;
        io.IniFilename = ini_file.c_str();
    }

    ImGui::StyleColorsDark();
    apply_theme();

    const float main_scale = options.dpi_scale;
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    io.ConfigDpiScaleFonts = true;
    g_ui_scale = main_scale;  // for the views' explicit pixel sizes

    // Fonts (resource/ is copied beside the exe by the build; see
    // CMakeLists.txt). exe-relative, not cwd-relative: the app may be
    // launched with any working directory (e.g. a shortcut's Start-in).
    // Falls back to ImGui's built-in font if the files aren't found,
    // rather than asserting.
    std::string resource_dir =
        options.resource_dir.empty() ? app::exe_dir() + "\\resource" : options.resource_dir;
    if (resource_dir.back() != '\\' && resource_dir.back() != '/') resource_dir += "\\";
    ImFont* main_font = io.Fonts->AddFontFromFileTTF(
        (resource_dir + "ShipporiAntiqueB1-Regular.ttf").c_str(), 18.0f);
    g_mono_font = io.Fonts->AddFontFromFileTTF(
        (resource_dir + "CourierPrime-Regular.ttf").c_str(), 18.0f);
    if (main_font) io.FontDefault = main_font;

    // CJK fallback: Clone Hero libraries are full of Japanese (and other
    // non-Latin) titles, which rendered as ?/boxes with the Latin-only fonts.
    // Merge the first available system font into the main font; ImGui's
    // dynamic font loader rasterizes glyphs on demand, so this costs nothing
    // until a non-Latin title is actually drawn.
    if (main_font) {
        const char* cjk_candidates[] = {
            "C:\\Windows\\Fonts\\YuGothM.ttc",   // Yu Gothic Medium (Win 8.1+)
            "C:\\Windows\\Fonts\\meiryo.ttc",    // Meiryo
            "C:\\Windows\\Fonts\\msgothic.ttc",  // MS Gothic (bitmap-ish, last resort)
        };
        for (const char* path : cjk_candidates) {
            if (::GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
            ImFontConfig merge;
            merge.MergeMode = true;
            if (io.Fonts->AddFontFromFileTTF(path, 18.0f, &merge)) break;
        }
    }
}

void shutdown_imgui() { ImGui::DestroyContext(); }

void run_frame(AppState& app, FrameText* capture) {
    const bool capturing = capture && capture->enabled;
    if (capturing) {
        // ImGui's text log spans every window until EndFrame; started here,
        // on the implicit Debug window, it sees all of Hydra's windows. It
        // also un-clips table rows so off-screen rows are captured.
        ImGui::LogToBuffer();
    }

    render_main_window(app);
    render_details_modal(app);

    if (capturing) {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        capture->text.assign(g.LogBuffer.begin(), g.LogBuffer.end());
        ImGui::LogFinish();  // clears the buffer
    }
}

}  // namespace hydra::ui
