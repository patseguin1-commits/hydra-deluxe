#include "ui/app_shell.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"  // g.LogBuffer for FrameText; ImGuiSettingsHandler

#include "app/config.h"
#include "ui/app_state.h"
#include "ui/details_view.h"
#include "ui/fonts.h"
#include "ui/library_view.h"
#include "ui/theme.h"

namespace hydra::ui {

namespace {

// What hydra_ui.ini said, then what main.cpp reported since.
WindowPlacement g_placement;
// What hydra_ui.ini said, then what the user dragged the split to since.
float g_library_share = kDefaultLibraryShare;
// The theme at scale 1, captured by setup_imgui after apply_theme().
ImGuiStyle g_base_style;

// Reads "<a>,<b>" into two ints; false unless the text is exactly that.
bool read_int_pair(std::string_view text, int& a, int& b) {
    const size_t comma = text.find(',');
    if (comma == std::string_view::npos) return false;
    const char* first = text.data();
    const char* mid = first + comma;
    const char* last = first + text.size();
    const auto r1 = std::from_chars(first, mid, a);
    if (r1.ec != std::errc() || r1.ptr != mid) return false;
    const auto r2 = std::from_chars(mid + 1, last, b);
    return r2.ec == std::errc() && r2.ptr == last;
}

// The [Hydra][Window] and [Hydra][Layout] sections' handler. ImGui calls
// ReadOpen for each "[Hydra][<name>]" header and ReadLine for each line under
// it, and WriteAll whenever it saves the file.
void* placement_read_open(ImGuiContext*, ImGuiSettingsHandler*, const char* name) {
    if (std::strcmp(name, "Window") == 0) return &g_placement;
    if (std::strcmp(name, "Layout") == 0) return &g_library_share;
    return nullptr;
}

void placement_read_line(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
    if (entry == &g_library_share)
        parse_layout_line(line, g_library_share);
    else
        parse_window_placement_line(line, *static_cast<WindowPlacement*>(entry));
}

void placement_write_all(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
    if (g_placement.valid) {
        out->appendf("[%s][Window]\n", handler->TypeName);
        out->append(format_window_placement(g_placement).c_str());
        out->append("\n");
    }
    out->appendf("[%s][Layout]\n", handler->TypeName);
    out->append(format_layout(g_library_share).c_str());
    out->append("\n");
}

}  // namespace

bool placement_on_screen(const ScreenRect& r, const std::vector<ScreenRect>& work_areas) {
    if (r.width() < kMinVisiblePx || r.height() < kMinVisiblePx) return false;
    const ScreenRect title_band{r.left, r.top, r.right, r.top + kMinVisiblePx};
    for (const ScreenRect& area : work_areas) {
        const int w = std::min(title_band.right, area.right) - std::max(title_band.left, area.left);
        const int h = std::min(title_band.bottom, area.bottom) - std::max(title_band.top, area.top);
        if (w >= kMinVisiblePx && h >= kMinVisiblePx / 2) return true;
    }
    return false;
}

std::string format_window_placement(const WindowPlacement& p) {
    return "Pos=" + std::to_string(p.normal.left) + "," + std::to_string(p.normal.top) + "\n" +
           "Size=" + std::to_string(p.normal.width()) + "," + std::to_string(p.normal.height()) +
           "\n" + "Maximized=" + (p.maximized ? "1" : "0") + "\n";
}

void parse_window_placement_line(std::string_view line, WindowPlacement& p) {
    int a = 0, b = 0;
    if (line.substr(0, 4) == "Pos=" && read_int_pair(line.substr(4), a, b)) {
        const int w = p.normal.width(), h = p.normal.height();
        p.normal = {a, b, a + w, b + h};
    } else if (line.substr(0, 5) == "Size=" && read_int_pair(line.substr(5), a, b)) {
        p.normal.right = p.normal.left + a;
        p.normal.bottom = p.normal.top + b;
    } else if (line == "Maximized=1") {
        p.maximized = true;
    } else if (line == "Maximized=0") {
        p.maximized = false;
    }
    p.valid = p.normal.width() > 0 && p.normal.height() > 0;
}

WindowPlacement window_placement() { return g_placement; }

void remember_window_placement(const WindowPlacement& p) {
    if (p == g_placement) return;
    g_placement = p;
    // WndProc can run before the context exists (CreateWindow sends its
    // first WM_SIZE early); the placement is kept either way.
    if (ImGui::GetCurrentContext()) ImGui::MarkIniSettingsDirty();
}

std::string format_layout(float library_share) {
    char text[48];
    std::snprintf(text, sizeof(text), "LibraryShare=%.4f\n", library_share);
    return text;
}

void parse_layout_line(std::string_view line, float& library_share) {
    constexpr std::string_view kKey = "LibraryShare=";
    if (line.substr(0, kKey.size()) != kKey) return;
    const std::string value(line.substr(kKey.size()));
    char* end = nullptr;
    const float v = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !(v > 0.0f && v < 1.0f)) return;
    library_share = v;
}

float library_share() { return g_library_share; }

void remember_library_share(float share) {
    if (!(share > 0.0f && share < 1.0f) || share == g_library_share) return;
    g_library_share = share;
    if (ImGui::GetCurrentContext()) ImGui::MarkIniSettingsDirty();
}

float ui_scale_for_dpi(unsigned dpi) {
    return dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;  // 96 = USER_DEFAULT_SCREEN_DPI
}

ImGuiStyle scaled_style(const ImGuiStyle& base, float scale) {
    ImGuiStyle style = base;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    return style;
}

void set_ui_scale(float scale) {
    if (scale <= 0.0f) return;
    ImGuiStyle& style = ImGui::GetStyle();
    // FontSizeBase is filled in by ImGui on the first frame; keep it.
    const float font_size_base = style.FontSizeBase;
    style = scaled_style(g_base_style, scale);
    style.FontSizeBase = font_size_base;
    g_ui_scale = scale;  // for the views' explicit pixel sizes
}

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

    // The main window's placement lives in the same file, as a
    // [Hydra][Window] section. Read the file now instead of on the first
    // frame (ImGui skips its own first-frame read once this has run), so
    // main.cpp can put the window back before it is shown.
    g_placement = WindowPlacement{};
    g_library_share = kDefaultLibraryShare;
    ImGuiSettingsHandler placement_handler;
    placement_handler.TypeName = "Hydra";
    placement_handler.TypeHash = ImHashStr("Hydra");
    placement_handler.ReadOpenFn = placement_read_open;
    placement_handler.ReadLineFn = placement_read_line;
    placement_handler.WriteAllFn = placement_write_all;
    ImGui::AddSettingsHandler(&placement_handler);  // ImGui keeps a copy
    if (io.IniFilename) ImGui::LoadIniSettingsFromDisk(io.IniFilename);

    ImGui::StyleColorsDark();
    apply_theme();

    // Every scale change starts again from this unscaled theme.
    g_base_style = ImGui::GetStyle();
    // Hydra sets the font scale itself, in set_ui_scale, together with the
    // sizes. With ConfigDpiScaleFonts on, ImGui would also overwrite it every
    // frame from the main viewport's DPI, a second writer that Hydra does not
    // keep in step with WM_DPICHANGED.
    io.ConfigDpiScaleFonts = false;
    set_ui_scale(options.dpi_scale);

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

    // State first, once per frame: the panel's closing edge, storing and
    // reaping the analyze job, a finished Dynamics count.
    app.tick(ImGui::GetTime());
    // Batch ends, report starts and reaps, parked leaderboard jobs: state
    // work, done here once a frame rather than inside a view.
    app.update_background_jobs();
    render_main_window(app);
    // The number boxes apply edits live but leave the INI until the edit
    // ends (AppState::edit_settings). An edit has ended once no widget is
    // active: the +/- button is released, or the text box lost focus.
    if (!ImGui::IsAnyItemActive()) app.flush_settings();

    if (capturing) {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        capture->text.assign(g.LogBuffer.begin(), g.LogBuffer.end());
        ImGui::LogFinish();  // clears the buffer
    }
}

}  // namespace hydra::ui
