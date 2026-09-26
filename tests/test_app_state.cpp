// Tests for ui/app_state's commit_settings: the one place that knows which
// settings change a record's identity. A widget only mutates `settings` and
// commits; whether the library page and the viewed record are re-read, and
// whether the page resets, is decided here and nowhere else.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "app/config.h"
#include "app/dynamics_breakdown.h"
#include "app/report_files.h"
#include "core/model.h"
#include "core/winstr.h"
#include "corpus_util.h"
#include "store/record_store.h"
#include "ui/app_state.h"
#include "ui/generation.h"

using hydra::HydraRecord;
using hydra::app::Settings;
using hydra::store::CapQuery;
using hydra::store::ChartLibraryEntry;
using hydra::store::RecordKey;
using hydra::store::RecordStatus;
using hydra::store::RecordStore;
using hydra::ui::AppState;
using hydra::ui::GenerationWatcher;

namespace {

// The chart mode and cap a default Settings asks for; the seeded record is
// stored under exactly these, so a change to either makes it unfindable.
const char kChartMode[] = "Expert Pro Drums, 2x Bass";
const int kSeededCap = 4;

// Enough charts that page 3 exists at the default 15 rows per page — the cap
// case has to show that the page is NOT reset, which needs a page to stay on.
const int kChartCount = 60;

std::string temp_path(const char* tag, const char* ext) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return hydra::wide_to_utf8(tmp) + "hydra_test_" + tag + "_" +
           std::to_string(GetCurrentProcessId()) + ext;
}

// Points app::ini_path()/db_path() at scratch files for one test, then puts
// the process back the way it was. commit_settings writes the INI through
// Settings::save(), so without this a test would overwrite the developer's
// real hydra_settings.ini.
struct ScratchPaths {
    hydra::app::PathOverrides previous;
    std::string ini;
    std::string db;
    std::string rules;

    explicit ScratchPaths(const char* tag)
        : previous(hydra::app::path_overrides()),
          ini(temp_path(tag, ".ini")),
          db(temp_path(tag, ".db")),
          rules(temp_path(tag, "_rules.ini")) {
        std::remove(ini.c_str());
        std::remove(db.c_str());
        std::remove(rules.c_str());
        hydra::app::PathOverrides overrides = previous;
        overrides.ini_path = ini;
        overrides.db_path = db;
        overrides.rules_path = rules;
        hydra::app::set_path_overrides(overrides);
    }

    ~ScratchPaths() {
        hydra::app::set_path_overrides(previous);
        std::remove(ini.c_str());
        std::remove(db.c_str());
        std::remove(rules.c_str());
    }
};

ChartLibraryEntry library_entry(int i) {
    char hash[32];
    std::snprintf(hash, sizeof(hash), "hash%03d", i);
    ChartLibraryEntry e;
    e.md5 = hash;
    e.title = std::string("Song ") + hash;
    e.artist = "Artist";
    e.charter = "Charter";
    e.notespath = std::string("C:\\charts\\") + hash + "\\notes.chart";
    e.rootfolder = "C:\\charts";
    e.sig = "sig";
    return e;
}

// A library of kChartCount charts, exactly one of which (entry 0) has a
// stored record, under the default chart mode at the default cap. The record
// itself is empty: a current-version row with no paths reads back as Ready
// (see test_store), and these tests only care which row a lookup finds.
std::unique_ptr<RecordStore> seeded_store(const std::string& db) {
    auto store = std::make_unique<RecordStore>(db);

    std::vector<ChartLibraryEntry> charts;
    for (int i = 0; i < kChartCount; ++i) charts.push_back(library_entry(i));
    store->rebuild_chart_library(charts);

    HydraRecord record;
    record.sp_cap = kSeededCap;
    record.ms_limit = Settings{}.mslimit_value;
    store->add_record(RecordKey{library_entry(0).md5, kChartMode, CapQuery::at(kSeededCap),
                                Settings{}.lens()},
                      record);
    return store;
}

// An AppState on the scratch store with the seeded chart selected and its
// record loaded.
std::unique_ptr<AppState> app_on(const ScratchPaths& paths) {
    auto app = std::make_unique<AppState>(Settings{}, seeded_store(paths.db));
    REQUIRE(app->current_page.rows.size() == 15);
    app->selected = library_entry(0);
    app->refresh_viewed_record();
    REQUIRE(app->viewed.status == RecordStatus::Ready);
    return app;
}

}  // namespace

TEST_CASE("commit_settings refreshes the viewed record when the chart mode changes") {
    ScratchPaths paths("appstate_mode");
    std::unique_ptr<AppState> app = app_on(paths);

    GenerationWatcher records;
    records.changed(app->record_generation);  // start from "already seen"
    app->table_viewpage = 3;

    // Pro Drums off is a different chart mode, so the stored record no longer
    // answers the question being asked.
    app->settings.view_prodrums = false;
    app->commit_settings();
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);
    CHECK(app->table_viewpage == 0);  // a new listing starts at page one
    CHECK(records.changed(app->record_generation));

    // The INI is written on the scratch path, not the user's.
    CHECK(Settings::load_file(paths.ini).view_prodrums == false);

    app->settings.view_prodrums = true;
    app->commit_settings();
    CHECK(app->viewed.status == RecordStatus::Ready);
}

TEST_CASE("commit_settings refreshes on an SP cap change without resetting the page") {
    ScratchPaths paths("appstate_cap");
    std::unique_ptr<AppState> app = app_on(paths);

    GenerationWatcher records;
    records.changed(app->record_generation);
    app->table_viewpage = 3;

    // The cap box lives in the details modal; changing it must re-read the
    // record but leave the library where the user left it.
    app->settings.sp_cap = 8;
    app->commit_settings();
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);
    CHECK(app->table_viewpage == 3);
    CHECK(records.changed(app->record_generation));

    app->settings.sp_cap = kSeededCap;
    app->commit_settings();
    CHECK(app->viewed.status == RecordStatus::Ready);
    CHECK(app->table_viewpage == 3);
}

TEST_CASE("commit_settings refreshes when the ms limit or the score range changes") {
    ScratchPaths paths("appstate_lens");
    std::unique_ptr<AppState> app = app_on(paths);

    GenerationWatcher records;
    records.changed(app->record_generation);
    app->table_viewpage = 3;

    // The stored result answered "best path under a 10 ms limit". Move the
    // limit and the question changes, so the answer no longer applies.
    const int seeded_ms = app->settings.mslimit_value;
    app->settings.mslimit_value = seeded_ms + 5;
    app->commit_settings();
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);
    CHECK(app->table_viewpage == 3);  // the library stays where the user left it
    CHECK(records.changed(app->record_generation));
    CHECK(Settings::load_file(paths.ini).mslimit_value == seeded_ms + 5);

    // Moving it back is instant: the old result is still stored, and nothing
    // has to be analyzed again.
    app->settings.mslimit_value = seeded_ms;
    app->commit_settings();
    CHECK(app->viewed.status == RecordStatus::Ready);
    CHECK(app->analyze_job == nullptr);

    // The score range is part of the same identity.
    const int seeded_depth = app->settings.depth_value;
    app->settings.depth_value = seeded_depth + 1;
    app->commit_settings();
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);
    app->settings.depth_value = seeded_depth;
    app->commit_settings();
    CHECK(app->viewed.status == RecordStatus::Ready);
    CHECK(app->analyze_job == nullptr);
}

TEST_CASE("commit_settings with a non-identity change does not bump the record generation") {
    ScratchPaths paths("appstate_plain");
    std::unique_ptr<AppState> app = app_on(paths);

    GenerationWatcher records;
    records.changed(app->record_generation);

    // The hit window is a display setting -- it never reaches the search, so
    // nothing cached goes stale and nothing is re-read.
    app->settings.hit_window_ms += 1;
    app->commit_settings();
    CHECK_FALSE(records.changed(app->record_generation));
    CHECK(app->viewed.status == RecordStatus::Ready);
    CHECK(Settings::load_file(paths.ini).hit_window_ms == app->settings.hit_window_ms);
}

TEST_CASE("a bad hydra_rules.ini names the key and keeps analysis off") {
    ScratchPaths paths("appstate_badrules");
    {
        std::ofstream f(paths.rules);
        f << "max_tied_paths = 0\n";
    }
    seeded_store(paths.db).reset();  // the library and one record, on disk

    // The startup constructor: settings INI, rules file and database from
    // the (scratch) paths, exactly as Hydra.exe starts.
    AppState app;
    CHECK(app.rules_error.find("max_tied_paths") != std::string::npos);
    CHECK(app.analysis_blocked());

    // The buttons are disabled, and the state refuses too, so no other
    // caller can start an analysis on the wrong rules.
    app.selected = library_entry(0);
    app.start_analyze();
    CHECK(app.analyze_job == nullptr);
    app.start_batch(false);
    CHECK(app.batch_job == nullptr);
}

TEST_CASE("no hydra_rules.ini leaves analysis on") {
    ScratchPaths paths("appstate_norules");
    seeded_store(paths.db).reset();
    AppState app;
    CHECK(app.rules_error.empty());
    CHECK_FALSE(app.analysis_blocked());
}

TEST_CASE("under a bad hydra_rules.ini no stored record reads Ready") {
    ScratchPaths paths("appstate_badrules_stale");
    seeded_store(paths.db).reset();
    const RecordKey seeded{library_entry(0).md5, kChartMode, CapQuery::at(kSeededCap),
                           Settings{}.lens()};

    // Good (absent) rules file: the seeded record, written under the
    // default rules, is Ready.
    {
        AppState good;
        CHECK(good.store->get_summary(seeded).status == RecordStatus::Ready);
    }

    // Bad file: the store is gated on RulesStamp::none(), so the same
    // record reads Stale. Nothing is shown as Ready under rules the user
    // did not choose.
    {
        std::ofstream f(paths.rules);
        f << "max_tied_paths = 0\n";
    }
    AppState bad;
    REQUIRE(bad.analysis_blocked());
    CHECK(bad.store->get_summary(seeded).status == RecordStatus::Stale);
}

TEST_CASE("update_dynamics recounts a stored row with an older count stamp") {
    ScratchPaths paths("appstate_dyn_old");
    std::unique_ptr<AppState> app = app_on(paths);
    const hydra::store::DynamicsKey key = hydra::app::dynamics_store_key(
        library_entry(0).md5, app->settings.difficulty(), app->settings.view_prodrums);
    // A row counted before the last bump of the counter.
    app->store->put_dynamics(key, hydra::app::encode_dynamics(hydra::app::DynamicsBreakdown{}),
                             hydra::app::kDynamicsCountVersion - 1);

    app->update_dynamics();

    // The old row was not shown; a background recount started instead. (The
    // chart file does not exist, so the job itself fails. This test only
    // cares that the recount was started.)
    CHECK_FALSE(app->dynamics_result.has_value());
    CHECK(app->dynamics_job != nullptr);
}

TEST_CASE("update_dynamics uses a stored row with the current count stamp") {
    ScratchPaths paths("appstate_dyn_now");
    std::unique_ptr<AppState> app = app_on(paths);
    const hydra::store::DynamicsKey key = hydra::app::dynamics_store_key(
        library_entry(0).md5, app->settings.difficulty(), app->settings.view_prodrums);
    hydra::app::save_dynamics(*app->store, key, hydra::app::DynamicsBreakdown{});

    app->update_dynamics();

    CHECK(app->dynamics_result.has_value());
    CHECK(app->dynamics_job == nullptr);  // no recount
}

// A Dynamics count that finished while another tab showed is stored when the
// window closes, not thrown away. Before, only the Dynamics tab collected the
// job, so the next open parsed the chart again.
TEST_CASE("close_details keeps a Dynamics count that finished on another tab") {
    ScratchPaths paths("appstate_dyn_close");
    std::unique_ptr<AppState> app = app_on(paths);
    app->selected->notespath = corpus::first_chart_with_suffix(".chart");
    app->show_details = true;

    app->update_dynamics();  // the Dynamics tab was shown once: the parse starts
    REQUIRE(app->dynamics_job != nullptr);
    for (int i = 0; i < 1200 && !app->dynamics_job->finished(); ++i) Sleep(50);
    REQUIRE(app->dynamics_job->finished());
    REQUIRE(app->dynamics_job->ok());

    // The user went back to Paths, so update_dynamics never ran again.
    app->close_details();

    CHECK_FALSE(app->show_details);
    CHECK(app->dynamics_job == nullptr);
    CHECK_FALSE(app->dynamics_result.has_value());
    const hydra::store::DynamicsKey key = hydra::app::dynamics_store_key(
        library_entry(0).md5, app->settings.difficulty(), app->settings.view_prodrums);
    CHECK(hydra::app::load_stored_dynamics(*app->store, key).has_value());
}

// The "Song file not found" check asks the disk when the window opens and
// then every two seconds, not on every frame.
TEST_CASE("the chart-file check runs on open and then every two seconds") {
    ScratchPaths paths("appstate_fileok");
    std::unique_ptr<AppState> app = app_on(paths);
    const std::string chart = temp_path("fileok_chart", ".chart");
    { std::ofstream f(chart); f << "[Song]\n"; }
    app->selected->notespath = chart;

    CHECK(app->selected_file_ok(10.0));        // first look
    std::remove(chart.c_str());
    CHECK(app->selected_file_ok(11.0));        // one second later: not asked again
    CHECK_FALSE(app->selected_file_ok(12.5));  // two seconds on: asked, and gone

    { std::ofstream f(chart); f << "[Song]\n"; }
    app->close_details();                      // the next open looks at once
    CHECK(app->selected_file_ok(12.6));
    std::remove(chart.c_str());
}

// The number boxes apply each step at once (the shown record follows live)
// but leave the INI until the edit ends: holding +/- used to rewrite the
// file every frame.
TEST_CASE("number boxes apply at once but write the INI only on flush") {
    ScratchPaths paths("appstate_flush");
    std::unique_ptr<AppState> app = app_on(paths);
    const int seeded_depth = app->settings.depth_value;

    app->settings.depth_value = seeded_depth + 3;
    app->edit_settings();
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);  // applied live
    CHECK(Settings::load_file(paths.ini).depth_value == seeded_depth);  // not saved yet

    app->flush_settings();  // the edit ended
    CHECK(Settings::load_file(paths.ini).depth_value == seeded_depth + 3);
}

// Stepping away and back re-shows a lookup already made, without asking the
// store again (a big record's decode is the expensive part).
TEST_CASE("stepping a number box back reuses the lookup it already made") {
    ScratchPaths paths("appstate_parked");
    std::unique_ptr<AppState> app = app_on(paths);

    app->settings.sp_cap = 8;
    app->edit_settings();
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);
    app->settings.sp_cap = kSeededCap;
    app->edit_settings();
    CHECK(app->viewed.status == RecordStatus::Ready);

    // A cap-8 record appears behind the cache's back. Stepping to 8 shows the
    // parked "not analyzed" answer: proof the store was not asked again.
    HydraRecord at8;
    at8.sp_cap = 8;
    at8.ms_limit = Settings{}.mslimit_value;
    app->store->add_record(
        RecordKey{library_entry(0).md5, kChartMode, CapQuery::at(8), Settings{}.lens()}, at8);
    app->settings.sp_cap = 8;
    app->edit_settings();
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);

    // A new selection drops every parked lookup, so the store is asked again.
    app->select(library_entry(0));
    CHECK(app->viewed.status == RecordStatus::Ready);
}

// The main window's "Open path report" button looks for the file every two
// seconds, not on every frame.
TEST_CASE("the report-file check is cached for two seconds") {
    ScratchPaths paths("appstate_report");
    std::unique_ptr<AppState> app = app_on(paths);
    const std::filesystem::path report(hydra::app::report_html_path());
    std::error_code ec;
    std::filesystem::remove(report, ec);

    CHECK_FALSE(app->report_file_shown(10.0));
    hydra::app::write_report_file(report, "<html></html>");
    CHECK_FALSE(app->report_file_shown(11.0));  // one second later: not asked
    CHECK(app->report_file_shown(12.5));        // two seconds on: asked, found
    std::filesystem::remove(report, ec);
}
