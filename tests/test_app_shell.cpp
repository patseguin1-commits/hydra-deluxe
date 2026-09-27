// Unit tests for the window-placement and DPI half of ui/app_shell: which
// saved rectangles are safe to reopen at, the hydra_ui.ini text, and the UI
// scale. A test can't move a real window between monitors, so the Win32
// side in main.cpp stays thin and everything it decides is tested here.

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"

#include "ui/app_shell.h"
#include "ui/fonts.h"

using hydra::ui::ScreenRect;
using hydra::ui::WindowPlacement;
using hydra::ui::placement_on_screen;

namespace {

const std::vector<ScreenRect> kOneMonitor = {{0, 0, 1920, 1040}};
const std::vector<ScreenRect> kTwoMonitors = {{0, 0, 1920, 1040}, {-1920, 0, 0, 1080}};

hydra::ui::ImGuiSetupOptions test_options(const std::string& ini_file) {
    hydra::ui::ImGuiSetupOptions opts;
    opts.dpi_scale = 1.0f;
    opts.ini_file = ini_file;
    opts.resource_dir = std::string(HYDRA_TESTDATA_DIR) + "/../resource";
    return opts;
}

}  // namespace

TEST_CASE("app_shell: a saved window is reopened only where its title bar can be reached") {
    CHECK(placement_on_screen({100, 100, 1380, 820}, kOneMonitor));
    CHECK_FALSE(placement_on_screen({5000, 100, 6280, 820}, kOneMonitor));  // monitor gone
    CHECK_FALSE(placement_on_screen({-1250, 100, 30, 820}, kOneMonitor));   // 30 px showing
    CHECK(placement_on_screen({-1180, 100, 100, 820}, kOneMonitor));        // 100 px showing
    CHECK_FALSE(placement_on_screen({100, -700, 1380, 20}, kOneMonitor));   // title bar above
    CHECK_FALSE(placement_on_screen({100, 100, 100, 100}, kOneMonitor));    // no size
    CHECK_FALSE(placement_on_screen({-1800, 40, -400, 940}, kOneMonitor));
    CHECK(placement_on_screen({-1800, 40, -400, 940}, kTwoMonitors));      // left monitor
    CHECK_FALSE(placement_on_screen({100, 100, 1380, 820}, {}));           // no monitors known
}

TEST_CASE("app_shell: the [Hydra][Window] text round-trips") {
    WindowPlacement p;
    p.valid = true;
    p.normal = {-1700, 40, -300, 940};
    p.maximized = true;
    const std::string text = hydra::ui::format_window_placement(p);
    CHECK(text == "Pos=-1700,40\nSize=1400,900\nMaximized=1\n");

    WindowPlacement back;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);)
        hydra::ui::parse_window_placement_line(line, back);
    CHECK(back == p);

    // Size before Pos reads the same.
    WindowPlacement reordered;
    hydra::ui::parse_window_placement_line("Size=1400,900", reordered);
    hydra::ui::parse_window_placement_line("Pos=-1700,40", reordered);
    CHECK(reordered.normal == p.normal);
    CHECK(reordered.valid);

    // Lines Hydra doesn't know, or a zero size, leave nothing usable.
    WindowPlacement junk;
    hydra::ui::parse_window_placement_line("Pos=abc,1", junk);
    hydra::ui::parse_window_placement_line("Size=0,0", junk);
    hydra::ui::parse_window_placement_line("Colour=blue", junk);
    CHECK_FALSE(junk.valid);
}

TEST_CASE("app_shell: the UI scale for a monitor DPI") {
    CHECK(hydra::ui::ui_scale_for_dpi(96) == doctest::Approx(1.0f));
    CHECK(hydra::ui::ui_scale_for_dpi(120) == doctest::Approx(1.25f));
    CHECK(hydra::ui::ui_scale_for_dpi(144) == doctest::Approx(1.5f));
    CHECK(hydra::ui::ui_scale_for_dpi(192) == doctest::Approx(2.0f));
    CHECK(hydra::ui::ui_scale_for_dpi(0) == doctest::Approx(1.0f));  // no reading: unscaled
}

TEST_CASE("app_shell: scaling always starts from the unscaled style") {
    const ImGuiStyle base;
    const ImGuiStyle once = hydra::ui::scaled_style(base, 1.5f);
    const ImGuiStyle again = hydra::ui::scaled_style(base, 1.5f);
    CHECK(once.FramePadding.x == again.FramePadding.x);
    CHECK(once.ItemSpacing.y == again.ItemSpacing.y);
    CHECK(once.FontScaleDpi == doctest::Approx(1.5f));
    const ImGuiStyle doubled = hydra::ui::scaled_style(base, 2.0f);
    CHECK(doubled.FramePadding.x == base.FramePadding.x * 2.0f);
    const ImGuiStyle unscaled = hydra::ui::scaled_style(base, 1.0f);
    CHECK(unscaled.FramePadding.x == base.FramePadding.x);
    CHECK(unscaled.ScrollbarSize == base.ScrollbarSize);
}

TEST_CASE("app_shell: hydra_ui.ini remembers the window placement") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "hydra_app_shell_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path ini = dir / "hydra_ui.ini";
    {
        std::ofstream f(ini);
        f << "[Hydra][Window]\nPos=-1700,40\nSize=1400,900\nMaximized=1\n\n";
    }

    hydra::ui::setup_imgui(test_options(ini.string()));
    const WindowPlacement read = hydra::ui::window_placement();
    CHECK(read.valid);
    CHECK(read.normal == ScreenRect{-1700, 40, -300, 940});
    CHECK(read.maximized);

    WindowPlacement moved;
    moved.valid = true;
    moved.normal = {200, 150, 1480, 870};
    moved.maximized = false;
    hydra::ui::remember_window_placement(moved);
    hydra::ui::shutdown_imgui();  // DestroyContext writes the ini

    std::ifstream f(ini);
    std::stringstream text;
    text << f.rdbuf();
    CHECK(text.str().find("[Hydra][Window]\nPos=200,150\nSize=1280,720\nMaximized=0\n") !=
          std::string::npos);
    f.close();
    std::filesystem::remove_all(dir);
}

TEST_CASE("app_shell: the [Hydra][Layout] text round-trips and refuses junk") {
    CHECK(hydra::ui::format_layout(0.35f) == "LibraryShare=0.3500\n");
    float share = hydra::ui::kDefaultLibraryShare;
    hydra::ui::parse_layout_line("LibraryShare=0.3500", share);
    CHECK(share == doctest::Approx(0.35f));
    // Anything else leaves the share alone.
    for (const char* line : {"LibraryShare=abc", "LibraryShare=0", "LibraryShare=1",
                             "LibraryShare=1.5", "LibraryShare=-0.2", "LibraryShare=0.3x",
                             "LibraryShare=", "Colour=blue"})
        hydra::ui::parse_layout_line(line, share);
    CHECK(share == doctest::Approx(0.35f));
}

TEST_CASE("app_shell: hydra_ui.ini remembers the library split, not the child's own width") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "hydra_app_shell_split_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path ini = dir / "hydra_ui.ini";
    {
        // What the bug left: the library child at the largest split, no share.
        std::ofstream f(ini);
        f << "[Window][Hydra/##library_06E76512]\nSize=2064,1313\n\n";
    }
    hydra::ui::setup_imgui(test_options(ini.string()));
    CHECK(hydra::ui::library_share() == doctest::Approx(hydra::ui::kDefaultLibraryShare));
    hydra::ui::remember_library_share(0.25f);
    hydra::ui::shutdown_imgui();  // DestroyContext writes the ini

    hydra::ui::setup_imgui(test_options(ini.string()));
    CHECK(hydra::ui::library_share() == doctest::Approx(0.25f));
    hydra::ui::shutdown_imgui();

    std::ifstream f(ini);
    std::stringstream text;
    text << f.rdbuf();
    CHECK(text.str().find("[Hydra][Layout]\nLibraryShare=0.2500\n") != std::string::npos);
    f.close();
    std::filesystem::remove_all(dir);
}

TEST_CASE("app_shell: set_ui_scale rescales sizes, fonts and px() together") {
    hydra::ui::setup_imgui(test_options("-"));
    const float before = ImGui::GetStyle().FramePadding.x;

    hydra::ui::set_ui_scale(2.0f);
    CHECK(ImGui::GetStyle().FramePadding.x ==
          static_cast<float>(static_cast<int>(before * 2.0f)));
    CHECK(ImGui::GetStyle().FontScaleDpi == doctest::Approx(2.0f));
    CHECK(hydra::ui::g_ui_scale == doctest::Approx(2.0f));
    CHECK(hydra::ui::px(10.0f) == doctest::Approx(20.0f));

    hydra::ui::set_ui_scale(1.0f);
    CHECK(ImGui::GetStyle().FramePadding.x == before);
    CHECK(ImGui::GetStyle().FontScaleDpi == doctest::Approx(1.0f));
    CHECK(hydra::ui::px(10.0f) == doctest::Approx(10.0f));
    hydra::ui::shutdown_imgui();
}
