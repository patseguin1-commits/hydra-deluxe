#include "app/config.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <fstream>
#include <optional>

#include "core/winstr.h"

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
    return wide_to_utf8(dir);
}

std::string db_path(bool uncapped) {
    return exe_dir() + "\\hydra" + edition_suffix(uncapped) + ".db";
}

std::string ini_path(bool uncapped) {
    return exe_dir() + "\\hydra" + edition_suffix(uncapped) + "_settings.ini";
}

Settings Settings::load(bool uncapped) {
    return load_file(ini_path(uncapped), uncapped);
}

Settings Settings::load_file(const std::string& path, bool uncapped) {
    Settings s;
    s.uncapped = uncapped;
    std::ifstream f(path);
    if (!f) return s;  // defaults

    std::string line;
    // The pre-1.5 "mslimit_value" key held raw gap ms; the per-hit key
    // replaces it. Remember both so a legacy-only INI can be migrated after
    // the scan (per-hit = raw / 2 at transfer scale 1).
    std::optional<int> legacy_mslimit;
    bool saw_perhit = false;
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
        else if (key == "mslimit_perhit_value") {
            s.mslimit_value = std::atoi(value.c_str());
            saw_perhit = true;
        }
        else if (key == "mslimit_value") legacy_mslimit = std::atoi(value.c_str());
        else if (key == "hit_window_ms") {
            int v = std::atoi(value.c_str());
            if (v > 0) s.hit_window_ms = v;
        }
        else if (key == "display_speed_pct") {
            int v = std::atoi(value.c_str());
            if (v > 0) s.display_speed_pct = v;
        }
        else if (key == "sp_cap_enabled") s.sp_cap_enabled = (value == "1");
        else if (key == "sp_cap_value") s.sp_cap_value = std::atoi(value.c_str());
        else if (key == "auto_open_report") s.auto_open_report = (value == "1");
        else if (key == "dm_last_user") s.dm_last_user = value;
    }
    if (!saw_perhit && legacy_mslimit) s.mslimit_value = *legacy_mslimit / 2;
    return s;
}

bool Settings::save() const { return save_file(ini_path(uncapped)); }

bool Settings::save_file(const std::string& path) const {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;

    f << "is_rescan=" << (is_rescan ? 1 : 0) << "\n";
    f << "view_difficulty=" << view_difficulty << "\n";
    f << "view_prodrums=" << (view_prodrums ? 1 : 0) << "\n";
    f << "view_bass2x=" << (view_bass2x ? 1 : 0) << "\n";
    f << "depth_value=" << depth_value << "\n";
    f << "depth_mode=" << depth_mode << "\n";
    f << "mslimit_enabled=" << (mslimit_enabled ? 1 : 0) << "\n";
    f << "mslimit_perhit_value=" << mslimit_value << "\n";
    f << "hit_window_ms=" << hit_window_ms << "\n";
    f << "display_speed_pct=" << display_speed_pct << "\n";
    f << "sp_cap_enabled=" << (sp_cap_enabled ? 1 : 0) << "\n";
    f << "sp_cap_value=" << sp_cap_value << "\n";
    f << "auto_open_report=" << (auto_open_report ? 1 : 0) << "\n";
    if (!dm_last_user.empty()) f << "dm_last_user=" << dm_last_user << "\n";
    for (const std::string& folder : chartfolders) f << "chartfolder=" << folder << "\n";
    return f.good();
}

std::string Settings::chartmode_key() const {
    std::string prodrums = view_prodrums ? "Pro Drums" : "Drums";
    std::string bass = view_bass2x ? "2x Bass" : "1x Bass";
    return view_difficulty + " " + prodrums + ", " + bass;
}

AnalysisSettings Settings::to_analysis_settings() const {
    AnalysisSettings s;
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
