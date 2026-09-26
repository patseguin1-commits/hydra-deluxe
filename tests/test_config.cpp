// Tests for app/config: INI round-trip through an explicit temp file,
// tolerance for malformed input, chartmode_key, and the Settings ->
// AnalysisSettings mapping (the SP cap included).

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
    s.chartfolders = {"C:\\charts\\a", "C:\\charts\\b"};
    s.is_rescan = true;
    s.view_prodrums = false;
    s.view_bass2x = false;
    s.depth_value = 25;
    s.depth_mode = 1;
    s.mslimit_enabled = false;
    s.mslimit_value = 42;
    s.backendlimit_enabled = true;
    s.backendlimit_value = 42;
    s.hit_window_ms = 79;
    s.preview_volume = 23;
    s.sp_cap = 16;
    s.auto_open_report = true;
    s.dm_last_user = "123456789";

    const std::string path = temp_ini("roundtrip");
    REQUIRE(s.save_file(path));
    Settings r = Settings::load_file(path);
    std::remove(path.c_str());

    CHECK(r.chartfolders == s.chartfolders);
    CHECK(r.is_rescan == s.is_rescan);
    CHECK(r.view_difficulty == s.view_difficulty);
    CHECK(r.view_prodrums == s.view_prodrums);
    CHECK(r.view_bass2x == s.view_bass2x);
    CHECK(r.depth_value == s.depth_value);
    CHECK(r.depth_mode == s.depth_mode);
    CHECK(r.mslimit_enabled == s.mslimit_enabled);
    CHECK(r.mslimit_value == s.mslimit_value);
    CHECK(r.backendlimit_enabled == s.backendlimit_enabled);
    CHECK(r.backendlimit_value == s.backendlimit_value);
    CHECK(r.hit_window_ms == s.hit_window_ms);
    CHECK(r.preview_volume == s.preview_volume);
    CHECK(r.sp_cap == s.sp_cap);
    CHECK(r.auto_open_report == s.auto_open_report);
    CHECK(r.dm_last_user == s.dm_last_user);
}

TEST_CASE("a missing INI yields defaults") {
    Settings r = Settings::load_file(temp_ini("missing_never_written"));
    Settings d;
    CHECK(r.sp_cap == 4);
    CHECK(r.chartfolders.empty());
    CHECK(r.view_difficulty == d.view_difficulty);
    CHECK(r.depth_value == d.depth_value);
    CHECK(r.mslimit_enabled == d.mslimit_enabled);
    CHECK(r.mslimit_value == d.mslimit_value);
    CHECK(r.mslimit_value == 10);
    CHECK(r.backendlimit_enabled == false);
    CHECK(r.backendlimit_value == 50);
    CHECK(r.hit_window_ms == 85);
    CHECK(r.preview_volume == 40);
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
          << "  mslimit_value =  25  \n"
          << "view_prodrums=0\n";
    }
    Settings r = Settings::load_file(path);
    std::remove(path.c_str());

    CHECK(r.mslimit_value == 25);       // whitespace-trimmed key/value
    CHECK(r.view_prodrums == false);
    CHECK(r.depth_value == 0);          // atoi("not_a_number") == 0, no throw
    CHECK(r.chartfolders.empty());
}

TEST_CASE("chartmode_key names the view flags") {
    // All four Expert keys, byte for byte: every record already in a user's
    // store is filed under one of these, so none of them may ever move.
    Settings s;
    CHECK(s.chartmode_key() == "Expert Pro Drums, 2x Bass");
    s.view_bass2x = false;
    CHECK(s.chartmode_key() == "Expert Pro Drums, 1x Bass");
    s.view_prodrums = false;
    CHECK(s.chartmode_key() == "Expert Drums, 1x Bass");
    s.view_bass2x = true;
    CHECK(s.chartmode_key() == "Expert Drums, 2x Bass");
}

TEST_CASE("chartmode_key: a non-Expert difficulty is always 1x Bass") {
    Settings s;
    s.view_difficulty = "Hard";
    s.view_prodrums = true;
    s.view_bass2x = true;  // stored true, but 2x is an Expert-only concept
    CHECK(s.difficulty() == hydra::Difficulty::Hard);
    CHECK(s.effective_bass2x() == false);
    CHECK(s.chartmode_key() == "Hard Pro Drums, 1x Bass");
    CHECK(s.to_analysis_settings().bass2x == false);
    CHECK(s.to_analysis_settings().difficulty == hydra::Difficulty::Hard);

    s.view_prodrums = false;
    CHECK(s.chartmode_key() == "Hard Drums, 1x Bass");
    s.view_difficulty = "Medium";
    CHECK(s.chartmode_key() == "Medium Drums, 1x Bass");
    s.view_difficulty = "Easy";
    CHECK(s.chartmode_key() == "Easy Drums, 1x Bass");

    // Back on Expert the stored 2x flag is still there and takes effect again.
    s.view_difficulty = "Expert";
    s.view_prodrums = true;
    CHECK(s.effective_bass2x() == true);
    CHECK(s.chartmode_key() == "Expert Pro Drums, 2x Bass");
}

TEST_CASE("view_difficulty round-trips, and a junk value normalizes to Expert") {
    const std::string path = temp_ini("difficulty");

    Settings s;
    s.view_difficulty = "Hard";
    REQUIRE(s.save_file(path));
    Settings r = Settings::load_file(path);
    CHECK(r.view_difficulty == "Hard");
    CHECK(r.difficulty() == hydra::Difficulty::Hard);

    // A hand-edited INI can hold anything; the loaded settings never do, so a
    // junk word can't reach chartmode_key and invent a chartmode.
    {
        std::ofstream f(path, std::ios::trunc);
        f << "view_difficulty=Legendary\n";
    }
    Settings junk = Settings::load_file(path);
    CHECK(junk.view_difficulty == "Expert");
    CHECK(junk.difficulty() == hydra::Difficulty::Expert);
    CHECK(junk.chartmode_key() == "Expert Pro Drums, 2x Bass");
    std::remove(path.c_str());
}

TEST_CASE("view_difficulty matches any case and loads as the real name") {
    const std::string path = temp_ini("difficulty_case");
    for (const char* word : {"hard", "HARD", "hArD"}) {
        CAPTURE(word);
        {
            std::ofstream f(path, std::ios::trunc);
            f << "view_difficulty=" << word << "\n";
        }
        Settings r = Settings::load_file(path);
        // What the app carries, and bakes into the store key, is the real name.
        CHECK(r.view_difficulty == "Hard");
        CHECK(r.difficulty() == hydra::Difficulty::Hard);
        CHECK(r.chartmode_key() == "Hard Pro Drums, 1x Bass");
    }
    std::remove(path.c_str());

    // The in-memory lookup matches any case too.
    Settings s;
    s.view_difficulty = "easy";
    CHECK(s.difficulty() == hydra::Difficulty::Easy);
}

TEST_CASE("record_key carries the chartmode, the SP cap and the lens") {
    namespace store = hydra::store;

    Settings s;
    s.sp_cap = std::nullopt;
    s.mslimit_enabled = true;
    s.mslimit_value = 10;
    s.depth_mode = 0;
    s.depth_value = 4;
    store::RecordKey auto_key = s.record_key("abc");
    CHECK(auto_key.hyhash == "abc");
    CHECK(auto_key.chartmode == s.chartmode_key());
    CHECK(auto_key.cap == store::CapQuery::automatic());
    CHECK(auto_key.lens == store::Lens::from(10, 0, 4));

    // A fixed cap asks for exactly that cap, and the chartmode follows the
    // view flags.
    s.sp_cap = 32;
    s.view_prodrums = false;
    store::RecordKey exact_key = s.record_key("abc");
    CHECK(exact_key.chartmode == "Expert Drums, 2x Bass");
    CHECK(exact_key.cap == store::CapQuery::at(32));

    // Each searched-over setting moves the lens...
    s.depth_mode = 1;
    s.depth_value = 5000;
    CHECK(s.record_key("abc").lens == store::Lens::from(10, 1, 5000));

    // ...and switching the ms limit off drops its number, because the search
    // stops reading it.
    s.mslimit_enabled = false;
    CHECK(s.record_key("abc").lens == store::Lens::from(std::nullopt, 1, 5000));
    CHECK(s.lens().ms_value == 0);
    s.mslimit_value = 42;
    CHECK(s.lens() == store::Lens::from(std::nullopt, 1, 5000));
}

TEST_CASE("batch_run bundles one Settings' chartmode, lens and search settings") {
    Settings s;
    s.view_difficulty = "Hard";
    s.view_prodrums = false;
    s.mslimit_enabled = false;
    s.depth_mode = 1;
    s.depth_value = 5000;
    s.sp_cap = std::nullopt;

    const hydra::app::BatchRun run = s.batch_run();
    CHECK(run.chartmode == s.chartmode_key());
    CHECK(run.lens == s.lens());
    CHECK(run.settings.difficulty == hydra::Difficulty::Hard);
    CHECK(run.settings.prodrums == false);
    CHECK(!run.settings.ms_filter.has_value());
    CHECK(run.settings.depth_mode == hydra::DepthMode::Points);
    CHECK(run.settings.depth_value == 5000);
    CHECK(!run.settings.sp_cap.has_value());
}

TEST_CASE("sp_cap round-trips as a number or auto; pre-1.6 keys are ignored") {
    const std::string path = temp_ini("spcap");

    // Auto writes the word and reads back as nullopt.
    Settings s;
    s.sp_cap = std::nullopt;
    REQUIRE(s.save_file(path));
    CHECK_FALSE(Settings::load_file(path).sp_cap.has_value());
    CHECK(Settings::load_file(path).cap_query().is_auto());

    // A number reads back as that number, and cap_query asks for it exactly.
    s.sp_cap = 64;
    REQUIRE(s.save_file(path));
    CHECK(Settings::load_file(path).sp_cap == 64);
    CHECK(Settings::load_file(path).cap_query().exact == 64);

    // The old Uncapped-edition keys, which the main app also used to write as
    // "off, 8", must not turn an existing INI into Auto or 8 bars. Garbage and
    // zero keep the default too.
    {
        std::ofstream f(path, std::ios::trunc);
        f << "sp_cap_enabled=0\n"
          << "sp_cap_value=8\n";
    }
    CHECK(Settings::load_file(path).sp_cap == 4);
    {
        std::ofstream f(path, std::ios::trunc);
        f << "sp_cap=0\n";
    }
    CHECK(Settings::load_file(path).sp_cap == 4);
    {
        std::ofstream f(path, std::ios::trunc);
        f << "sp_cap=banana\n";
    }
    CHECK(Settings::load_file(path).sp_cap == 4);
    std::remove(path.c_str());
}

TEST_CASE("to_analysis_settings maps the cap and its Auto budget") {
    Settings s;
    s.depth_mode = 1;
    s.depth_value = 5000;
    s.mslimit_enabled = true;
    s.mslimit_value = 20;

    // A fixed cap: single run, no time budget.
    s.sp_cap = 16;
    AnalysisSettings a = s.to_analysis_settings();
    CHECK(a.depth_mode == hydra::DepthMode::Points);
    CHECK(a.depth_value == 5000);
    CHECK(a.ms_filter == 20.0);
    CHECK(a.sp_cap == 16);
    CHECK_FALSE(a.time_budget_s.has_value());

    // Auto: the ladder and its budget apply.
    s.sp_cap = std::nullopt;
    a = s.to_analysis_settings();
    CHECK_FALSE(a.sp_cap.has_value());
    CHECK(a.time_budget_s.has_value());

    // Disabled ms limit maps to no filter.
    s.mslimit_enabled = false;
    a = s.to_analysis_settings();
    CHECK_FALSE(a.ms_filter.has_value());
}

// The INI keeps depth_mode as a plain int, so the mapping to the search's
// enum is where a stray value has to land somewhere safe: anything that isn't
// 1 means the default, scores.
TEST_CASE("to_analysis_settings maps depth_mode onto the search's enum") {
    Settings s;

    s.depth_mode = 1;
    CHECK(s.to_analysis_settings().depth_mode == hydra::DepthMode::Points);

    s.depth_mode = 0;
    CHECK(s.to_analysis_settings().depth_mode == hydra::DepthMode::Scores);

    s.depth_mode = 7;  // out of range: falls back to scores, never a bad enum
    CHECK(s.to_analysis_settings().depth_mode == hydra::DepthMode::Scores);
}
