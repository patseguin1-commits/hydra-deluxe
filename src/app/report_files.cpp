#include "app/report_files.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "app/config.h"

namespace hydra::app {

namespace {

std::filesystem::path db_folder() {
    return std::filesystem::u8path(app::db_path()).parent_path();
}

// The user's Documents folder, or nullopt when Windows can't name one.
std::optional<std::filesystem::path> known_documents_dir() {
    PWSTR raw = nullptr;
    std::optional<std::filesystem::path> out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &raw)) &&
        raw)
        out = std::filesystem::path(raw);
    CoTaskMemFree(raw);  // safe on nullptr
    return out;
}

// Every report page lives in reports_dir().
std::wstring html_artifact_path(const wchar_t* name) { return (reports_dir() / name).wstring(); }

DocumentsDirFn g_documents_dir;
OpenInBrowserFn g_open_in_browser;

}  // namespace

std::filesystem::path reports_dir() {
    // A harness pointed the app at a scratch database: keep its pages there.
    if (!path_overrides().db_path.empty()) return db_folder();

    std::optional<std::filesystem::path> docs =
        g_documents_dir ? g_documents_dir() : known_documents_dir();
    if (!docs || docs->empty()) return db_folder();

    const std::filesystem::path dir = *docs / L"Hydra";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::is_directory(dir, ec)) return db_folder();
    return dir;
}

void set_documents_dir_lookup(DocumentsDirFn fn) { g_documents_dir = std::move(fn); }

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
    // A reader can click "Open path report" at any moment, including while
    // we're mid-write. To make sure they never see a half-written page, we
    // finish writing under a temp name first and only swap it into place
    // with one rename once it's complete.
    std::filesystem::path tmp = outpath;
    tmp += ".tmp";

    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write " + outpath.u8string());
    f << html;
    f.close();
    if (!f) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        throw std::runtime_error("cannot write " + outpath.u8string());
    }

    try {
        std::filesystem::rename(tmp, outpath);
    } catch (const std::filesystem::filesystem_error&) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        throw std::runtime_error("cannot write " + outpath.u8string());
    }
}

}  // namespace hydra::app
