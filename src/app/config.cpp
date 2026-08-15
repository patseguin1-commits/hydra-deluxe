#include "app/config.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <fstream>

namespace hydra::app {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string edition_suffix(bool uncapped) { return uncapped ? "_uncapped" : ""; }

}  // namespace

std::string exe_dir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);
    size_t pos = path.find_last_of(L"\\/");
    std::wstring dir = pos == std::wstring::npos ? L"." : path.substr(0, pos);

    int len = WideCharToMultiByte(CP_UTF8, 0, dir.data(), static_cast<int>(dir.size()), nullptr,
                                  0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, dir.data(), static_cast<int>(dir.size()), &out[0], len,
                        nullptr, nullptr);
    return out;
}

std::string db_path(bool uncapped) {
    return exe_dir() + "\\hydra" + edition_suffix(uncapped) + ".db";
}

std::string ini_path(bool uncapped) {
    return exe_dir() + "\\hydra" + edition_suffix(uncapped) + "_settings.ini";
}

Settings Settings::load(bool uncapped) {
    Settings s;
    s.uncapped = uncapped;
    std::ifstream f(ini_path(uncapped));
    if (!f) return s;  // defaults

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "chartfolder") s.chartfolders.push_back(value);
        else if (key == "is_rescan") s.is_rescan = (value == "1");
        else if (key == "view_difficulty") s.view_difficulty = value;
        else if (key == "view_prodrums") s.view_prodrums = (value == "1");
        else if (key == "view_bass2x") s.view_bass2x = (value == "1");
        else if (key == "depth_value") s.depth_value = std::atoi(value.c_str());
        else if (key == "depth_mode") s.depth_mode = std::atoi(value.c_str());
        else if (key == "mslimit_enabled") s.mslimit_enabled = (value == "1");
        else if (key == "mslimit_value") s.mslimit_value = std::atoi(value.c_str());
        else if (key == "sp_cap_enabled") s.sp_cap_enabled = (value == "1");
        else if (key == "sp_cap_value") s.sp_cap_value = std::atoi(value.c_str());
    }
    return s;
}

void Settings::save() const {
    std::ofstream f(ini_path(uncapped), std::ios::trunc);
    if (!f) return;  // best-effort, like hymisc's own config write

    f << "is_rescan=" << (is_rescan ? 1 : 0) << "\n";
    f << "view_difficulty=" << view_difficulty << "\n";
    f << "view_prodrums=" << (view_prodrums ? 1 : 0) << "\n";
    f << "view_bass2x=" << (view_bass2x ? 1 : 0) << "\n";
    f << "depth_value=" << depth_value << "\n";
    f << "depth_mode=" << depth_mode << "\n";
    f << "mslimit_enabled=" << (mslimit_enabled ? 1 : 0) << "\n";
    f << "mslimit_value=" << mslimit_value << "\n";
    f << "sp_cap_enabled=" << (sp_cap_enabled ? 1 : 0) << "\n";
    f << "sp_cap_value=" << sp_cap_value << "\n";
    for (const std::string& folder : chartfolders) f << "chartfolder=" << folder << "\n";
}

std::string Settings::chartmode_key() const {
    std::string prodrums = view_prodrums ? "Pro Drums" : "Drums";
    std::string bass = view_bass2x ? "2x Bass" : "1x Bass";
    return view_difficulty + " " + prodrums + ", " + bass;
}

AnalysisSettings Settings::to_analysis_settings() const {
    AnalysisSettings s;
    s.difficulty = view_difficulty;
    s.prodrums = view_prodrums;
    s.bass2x = view_bass2x;
    s.depth_mode = depth_mode;
    s.depth_value = depth_value;
    s.ms_filter = mslimit_enabled ? std::optional<double>(mslimit_value) : std::nullopt;
    s.uncapped = uncapped;
    // The manual SP cap only applies in the uncapped edition; the capped edition
    // is always 4 bars.
    s.sp_cap =
        (uncapped && sp_cap_enabled) ? std::optional<int>(sp_cap_value) : std::nullopt;
    // Bound the uncapped ladder so a pathologically heavy chart can't hang the
    // app for minutes (hymisc.SP_CAP_TIME_BUDGET). Capped analysis is a single
    // fast run and needs no budget.
    s.uncapped_time_budget_s = uncapped ? std::optional<double>(120.0) : std::nullopt;
    return s;
}

}  // namespace hydra::app
