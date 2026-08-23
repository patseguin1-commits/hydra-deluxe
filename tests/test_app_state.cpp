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
#include <memory>
#include <string>
#include <vector>

#include "app/config.h"
#include "core/model.h"
#include "core/winstr.h"
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

    explicit ScratchPaths(const char* tag)
        : previous(hydra::app::path_overrides()),
          ini(temp_path(tag, ".ini")),
          db(temp_path(tag, ".db")) {
        std::remove(ini.c_str());
        std::remove(db.c_str());
        hydra::app::PathOverrides overrides = previous;
        overrides.ini_path = ini;
        overrides.db_path = db;
        hydra::app::set_path_overrides(overrides);
    }

    ~ScratchPaths() {
        hydra::app::set_path_overrides(previous);
        std::remove(ini.c_str());
        std::remove(db.c_str());
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
    store->add_record(RecordKey{library_entry(0).md5, kChartMode, CapQuery::at(kSeededCap)},
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

TEST_CASE("commit_settings with a non-identity change does not bump the record generation") {
    ScratchPaths paths("appstate_plain");
    std::unique_ptr<AppState> app = app_on(paths);

    GenerationWatcher records;
    records.changed(app->record_generation);

    // The score range is a search parameter, not part of a record's identity:
    // nothing cached goes stale, so nothing is re-read.
    app->settings.depth_value += 1;
    app->commit_settings();
    CHECK_FALSE(records.changed(app->record_generation));
    CHECK(app->viewed.status == RecordStatus::Ready);
    CHECK(Settings::load_file(paths.ini).depth_value == app->settings.depth_value);
}
