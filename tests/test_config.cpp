// Tests for app/config: INI round-trip through an explicit temp file,
// tolerance for malformed input, chartmode_key, and the Settings ->
// AnalysisSettings mapping (edition gating included).

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "app/config.h"
#include "core/winstr.h"

using hydra::app::AnalysisSettings;
using hydra::app::Settings;

namespace {

// A per-process temp INI path, so parallel test runs never collide.
std::string temp_ini(const char* tag) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return hydra::wide_to_utf8(tmp) + "hydra_test_" + tag + "_" +
           std::to_string(GetCurrentProcessId()) + ".ini";
}

}  // namespace

TEST_CASE("settings round-trip through an INI file") {
    Settings s;
    s.uncapped = true;
    s.chartfolders = {"C:\\charts\\a", "C:\\charts\\b"};
    s.is_rescan = true;
    s.view_prodrums = false;
    s.view_bass2x = false;
    s.depth_value = 25;
    s.depth_mode = 1;
    s.mslimit_enabled = false;
    s.mslimit_value = 42;
    s.hit_window_ms = 79;
    s.display_speed_pct = 130;
    s.sp_cap_enabled = true;
    s.sp_cap_value = 16;
    s.auto_open_report = true;
    s.dm_last_user = "123456789";

    const std::string path = temp_ini("roundtrip");
    REQUIRE(s.save_file(path));
    Settings r = Settings::load_file(path, /*uncapped=*/true);
    std::remove(path.c_str());

    CHECK(r.uncapped == s.uncapped);
    CHECK(r.chartfolders == s.chartfolders);
    CHECK(r.is_rescan == s.is_rescan);
    CHECK(r.view_difficulty == s.view_difficulty);
    CHECK(r.view_prodrums == s.view_prodrums);
    CHECK(r.view_bass2x == s.view_bass2x);
    CHECK(r.depth_value == s.depth_value);
    CHECK(r.depth_mode == s.depth_mode);
    CHECK(r.mslimit_enabled == s.mslimit_enabled);
    CHECK(r.mslimit_value == s.mslimit_value);
    CHECK(r.hit_window_ms == s.hit_window_ms);
    CHECK(r.display_speed_pct == s.display_speed_pct);
    CHECK(r.sp_cap_enabled == s.sp_cap_enabled);
    CHECK(r.sp_cap_value == s.sp_cap_value);
    CHECK(r.auto_open_report == s.auto_open_report);
    CHECK(r.dm_last_user == s.dm_last_user);
}

TEST_CASE("a missing INI yields defaults") {
    Settings r = Settings::load_file(temp_ini("missing_never_written"), false);
    Settings d;
    CHECK(r.uncapped == false);
    CHECK(r.chartfolders.empty());
    CHECK(r.view_difficulty == d.view_difficulty);
    CHECK(r.depth_value == d.depth_value);
    CHECK(r.mslimit_enabled == d.mslimit_enabled);
    CHECK(r.mslimit_value == d.mslimit_value);
    CHECK(r.mslimit_value == 5);
    CHECK(r.hit_window_ms == 85);
    CHECK(r.display_speed_pct == 100);
}

TEST_CASE("a legacy raw-ms limit migrates to per-hit") {
    const std::string path = temp_ini("legacy_mslimit");

    // Legacy key only: per-hit seeds at half the raw value.
    {
        std::ofstream f(path, std::ios::trunc);
        f << "mslimit_value=10\n";
    }
    Settings r = Settings::load_file(path, false);
    CHECK(r.mslimit_value == 5);

    // Both keys present: the per-hit key wins regardless of order.
    {
        std::ofstream f(path, std::ios::trunc);
        f << "mslimit_perhit_value=7\n"
          << "mslimit_value=40\n";
    }
    r = Settings::load_file(path, false);
    std::remove(path.c_str());
    CHECK(r.mslimit_value == 7);
}

TEST_CASE("malformed INI lines are tolerated") {
    const std::string path = temp_ini("malformed");
    {
        std::ofstream f(path, std::ios::trunc);
        f << "# a comment line\n"
          << "\n"
          << "   \t \n"
          << "no_equals_sign_here\n"
          << "unknown_key=whatever\n"
          << "depth_value=not_a_number\n"
          << "  mslimit_perhit_value =  25  \n"
          << "view_prodrums=0\n";
    }
    Settings r = Settings::load_file(path, false);
    std::remove(path.c_str());

    CHECK(r.mslimit_value == 25);       // whitespace-trimmed key/value
    CHECK(r.view_prodrums == false);
    CHECK(r.depth_value == 0);          // atoi("not_a_number") == 0, no throw
    CHECK(r.chartfolders.empty());
}

TEST_CASE("chartmode_key names the view flags") {
    Settings s;
    CHECK(s.chartmode_key() == "Expert Pro Drums, 2x Bass");
    s.view_prodrums = false;
    CHECK(s.chartmode_key() == "Expert Drums, 2x Bass");
    s.view_bass2x = false;
    CHECK(s.chartmode_key() == "Expert Drums, 1x Bass");
}

TEST_CASE("to_analysis_settings maps and gates by edition") {
    Settings s;
    s.depth_mode = 1;
    s.depth_value = 5000;
    s.mslimit_enabled = true;
    s.mslimit_value = 20;
    s.sp_cap_enabled = true;
    s.sp_cap_value = 16;

    // Capped edition: no sp cap, no ladder time budget.
    s.uncapped = false;
    AnalysisSettings a = s.to_analysis_settings();
    CHECK(a.depth_mode == 1);
    CHECK(a.depth_value == 5000);
    CHECK(a.ms_filter == 20.0);
    CHECK(a.uncapped == false);
    CHECK_FALSE(a.sp_cap.has_value());
    CHECK_FALSE(a.uncapped_time_budget_s.has_value());

    // Uncapped edition: the manual cap and the ladder budget apply.
    s.uncapped = true;
    a = s.to_analysis_settings();
    CHECK(a.uncapped == true);
    CHECK(a.sp_cap == 16);
    CHECK(a.uncapped_time_budget_s.has_value());

    // Disabled ms limit maps to no filter.
    s.mslimit_enabled = false;
    a = s.to_analysis_settings();
    CHECK_FALSE(a.ms_filter.has_value());
}
