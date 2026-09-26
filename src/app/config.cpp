#include "app/config.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <fstream>

#include "core/winstr.h"

namespace hydra::app {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

}  // namespace

std::string exe_dir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);
    size_t pos = path.find_last_of(L"\\/");
    std::wstring dir = pos == std::wstring::npos ? L"." : path.substr(0, pos);
    return wide_to_utf8(dir);
}

namespace {
PathOverrides g_overrides;
}

void set_path_overrides(PathOverrides overrides) { g_overrides = std::move(overrides); }
const PathOverrides& path_overrides() { return g_overrides; }

std::string db_path() {
    if (!g_overrides.db_path.empty()) return g_overrides.db_path;
    return exe_dir() + "\\hydra.db";
}

std::string ini_path() {
    if (!g_overrides.ini_path.empty()) return g_overrides.ini_path;
    return exe_dir() + "\\hydra_settings.ini";
}

std::unique_ptr<store::RecordStore> open_store(const std::string& db,
                                               core::RulesStamp rules) {
    return std::make_unique<store::RecordStore>(db, rules);
}

std::string asset_dir() {
    if (!g_overrides.asset_dir.empty()) return g_overrides.asset_dir;
    return exe_dir() + "\\assets\\preview";
}

Settings Settings::load() { return load_file(ini_path()); }

Settings Settings::load_file(const std::string& path) {
    Settings s;
    std::ifstream f(utf8_to_wide(path));
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
        else if (key == "backendlimit_enabled") s.backendlimit_enabled = (value == "1");
        else if (key == "backendlimit_value")
            s.backendlimit_value = std::atoi(value.c_str());
        else if (key == "preview_volume") {
            int v = std::atoi(value.c_str());
            if (v >= 0 && v <= 100) s.preview_volume = v;
        }
        else if (key == "hit_window_ms") {
            int v = std::atoi(value.c_str());
            if (v > 0) s.hit_window_ms = v;
        }
        else if (key == "sp_cap") {
            if (value == "auto") s.sp_cap = std::nullopt;
            else if (int v = std::atoi(value.c_str()); v >= 1) s.sp_cap = v;
        }
        else if (key == "auto_open_report") s.auto_open_report = (value == "1");
        else if (key == "dm_last_user") s.dm_last_user = value;
    }
    // Normalize the difficulty word: whatever was in the file, what the app
    // carries (and bakes into chartmode_key) is one of the four real names.
    s.view_difficulty = difficulty_name(s.difficulty());
    return s;
}

bool Settings::save() const { return save_file(ini_path()); }

bool Settings::save_file(const std::string& path) const {
    std::ofstream f(utf8_to_wide(path), std::ios::trunc);
    if (!f) return false;

    f << "is_rescan=" << (is_rescan ? 1 : 0) << "\n";
    f << "view_difficulty=" << view_difficulty << "\n";
    f << "view_prodrums=" << (view_prodrums ? 1 : 0) << "\n";
    f << "view_bass2x=" << (view_bass2x ? 1 : 0) << "\n";
    f << "depth_value=" << depth_value << "\n";
    f << "depth_mode=" << depth_mode << "\n";
    f << "mslimit_enabled=" << (mslimit_enabled ? 1 : 0) << "\n";
    f << "mslimit_value=" << mslimit_value << "\n";
    f << "backendlimit_enabled=" << (backendlimit_enabled ? 1 : 0) << "\n";
    f << "backendlimit_value=" << backendlimit_value << "\n";
    f << "hit_window_ms=" << hit_window_ms << "\n";
    f << "preview_volume=" << preview_volume << "\n";
    if (sp_cap) f << "sp_cap=" << *sp_cap << "\n";
    else f << "sp_cap=auto\n";
    f << "auto_open_report=" << (auto_open_report ? 1 : 0) << "\n";
    if (!dm_last_user.empty()) f << "dm_last_user=" << dm_last_user << "\n";
    for (const std::string& folder : chartfolders) f << "chartfolder=" << folder << "\n";
    return f.good();
}

Difficulty Settings::difficulty() const {
    return difficulty_from_name(view_difficulty).value_or(Difficulty::Expert);
}

bool Settings::effective_bass2x() const {
    return view_bass2x && difficulty() == Difficulty::Expert;
}

std::string Settings::chartmode_key() const {
    std::string prodrums = view_prodrums ? "Pro Drums" : "Drums";
    // Only Expert can be 2x, so every other difficulty's key ends "1x Bass" —
    // and the four Expert keys are byte-for-byte the ones already in the store.
    std::string bass = effective_bass2x() ? "2x Bass" : "1x Bass";
    return view_difficulty + " " + prodrums + ", " + bass;
}

AnalysisSettings Settings::to_analysis_settings() const {
    AnalysisSettings s;
    s.prodrums = view_prodrums;
    s.bass2x = effective_bass2x();
    s.difficulty = difficulty();
    s.depth_mode = depth_mode == 1 ? DepthMode::Points : DepthMode::Scores;
    s.depth_value = depth_value;
    s.ms_filter = mslimit_enabled ? std::optional<double>(mslimit_value) : std::nullopt;
    s.sp_cap = sp_cap;
    s.rules = rules;
    return s;
}

std::optional<double> Settings::backend_limit() const {
    return backendlimit_enabled
               ? std::optional<double>(std::abs(backendlimit_value))
               : std::nullopt;
}

store::CapQuery Settings::cap_query() const {
    return store::CapQuery::from_setting(sp_cap);
}

store::Lens Settings::lens() const {
    return store::Lens::from(
        mslimit_enabled ? std::optional<int>(mslimit_value) : std::nullopt,
        depth_mode, depth_value);
}

store::RecordKey Settings::record_key(const std::string& hyhash) const {
    return store::RecordKey{hyhash, chartmode_key(), cap_query(), lens()};
}

BatchRun Settings::batch_run() const {
    return BatchRun{chartmode_key(), lens(), to_analysis_settings()};
}

}  // namespace hydra::app
