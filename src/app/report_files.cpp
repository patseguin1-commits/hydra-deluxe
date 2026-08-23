#include "app/report_files.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <fstream>
#include <stdexcept>
#include <utility>

#include "app/config.h"

namespace hydra::app {

namespace {

// Report pages live next to the db.
std::wstring html_artifact_path(const wchar_t* name) {
    std::filesystem::path dbp = std::filesystem::u8path(app::db_path());
    return (dbp.parent_path() / name).wstring();
}

OpenInBrowserFn g_open_in_browser;

}  // namespace

void set_open_in_browser(OpenInBrowserFn fn) { g_open_in_browser = std::move(fn); }

bool open_in_browser(const std::wstring& path) {
    if (g_open_in_browser) return g_open_in_browser(path);
    HINSTANCE rc = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
                                 nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;
}

std::wstring report_html_path() { return html_artifact_path(L"hydra_paths.html"); }

std::wstring dm_report_html_path() { return html_artifact_path(L"hydra_dmcompare.html"); }

bool open_report_in_browser() { return open_in_browser(report_html_path()); }

bool open_dm_report_in_browser() { return open_in_browser(dm_report_html_path()); }

bool report_file_exists() {
    return GetFileAttributesW(report_html_path().c_str()) != INVALID_FILE_ATTRIBUTES;
}

void write_report_file(const std::filesystem::path& outpath, const std::string& html) {
    std::ofstream f(outpath, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write " + outpath.u8string());
    f << html;
    f.close();
}

}  // namespace hydra::app
