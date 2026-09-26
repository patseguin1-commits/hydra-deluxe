// Non-ASCII install folders. Hydra runs from wherever the user put it, and a
// folder like C:\Users\Zoë\... is a UTF-8 path in our std::strings. The narrow
// CRT and std::fstream calls read such a path through the ANSI code page and
// miss the file, so every reader goes through core/winstr's wide calls.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "app/config.h"
#include "core/winstr.h"
#include "render/preview_renderer.h"
#include "warp_util.h"

#ifndef HYDRA_ASSET_DIR
#error "HYDRA_ASSET_DIR must be defined (see CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace {

// A fresh %TEMP%\hydra_Zoë_<tag>_<pid> folder, as a UTF-8 string.
std::string non_ascii_dir(const char* tag) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    fs::path dir = fs::path(tmp) / (std::wstring(L"hydra_Zo\u00EB_") +
                                    hydra::utf8_to_wide(tag) + L"_" +
                                    std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return hydra::wide_to_utf8(dir.wstring());
}

void remove_dir(const std::string& utf8_dir) {
    std::error_code ec;
    fs::remove_all(fs::path(hydra::utf8_to_wide(utf8_dir)), ec);
}

}  // namespace

TEST_CASE("read_file_text reads a file under a non-ASCII folder") {
    const std::string dir = non_ascii_dir("text");
    const std::string file = dir + "\\note.txt";
    std::FILE* f = hydra::fopen_utf8(file, L"wb");
    REQUIRE(f != nullptr);
    std::fputs("hello\r\nworld", f);
    std::fclose(f);

    CHECK(hydra::read_file_text(file) == "hello\r\nworld");  // bytes as they are
    CHECK_THROWS_AS(hydra::read_file_text(dir + "\\missing.txt"), std::runtime_error);
    remove_dir(dir);
}

TEST_CASE("the settings INI saves and loads under a non-ASCII folder") {
    const std::string dir = non_ascii_dir("ini");
    const std::string ini = dir + "\\hydra_settings.ini";
    hydra::app::Settings s;
    s.depth_value = 7;
    s.dm_last_user = "111";
    REQUIRE(s.save_file(ini));
    CHECK(hydra::file_exists_utf8(ini));

    hydra::app::Settings back = hydra::app::Settings::load_file(ini);
    CHECK(back.depth_value == 7);
    CHECK(back.dm_last_user == "111");
    remove_dir(dir);
}

TEST_CASE("the Preview renderer loads its assets from a non-ASCII folder (WARP)") {
    Microsoft::WRL::ComPtr<ID3D11Device> dev;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    REQUIRE(warp::make_device(dev, ctx));
    const std::string dir = non_ascii_dir("assets");
    fs::copy(fs::u8path(HYDRA_ASSET_DIR), fs::path(hydra::utf8_to_wide(dir)),
             fs::copy_options::recursive | fs::copy_options::overwrite_existing);

    // Before the fix this threw "PreviewRenderer: missing ...\3d-config.json".
    hydra::render::PreviewRenderer r(dev.Get(), ctx.Get(), dir);
    CHECK(r.config().hydra.msaa == 4);
    remove_dir(dir);
}
