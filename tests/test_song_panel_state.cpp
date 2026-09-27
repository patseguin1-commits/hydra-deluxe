// The song panel's state rules on AppState: next/previous walk the rows the
// library shows and never wrap, the panel's teardown runs on its closing
// edge (from tick(), not from draw code), and nothing is locked while idle.

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
#include "core/winstr.h"
#include "store/record_store.h"
#include "ui/app_state.h"

using hydra::app::Settings;
using hydra::store::ChartLibraryEntry;
using hydra::store::RecordStore;
using hydra::ui::AppState;

namespace {

std::string temp_path(const char* tag, const char* ext) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return hydra::wide_to_utf8(tmp) + "hydra_test_" + tag + "_" +
           std::to_string(GetCurrentProcessId()) + ext;
}

// Scratch INI and DB for one test; the process's paths come back afterwards.
struct ScratchPaths {
    hydra::app::PathOverrides previous;
    std::string ini, db;
    explicit ScratchPaths(const char* tag)
        : previous(hydra::app::path_overrides()),
          ini(temp_path(tag, ".ini")),
          db(temp_path(tag, ".db")) {
        std::remove(ini.c_str());
        std::remove(db.c_str());
        hydra::app::PathOverrides o = previous;
        o.ini_path = ini;
        o.db_path = db;
        hydra::app::set_path_overrides(o);
    }
    ~ScratchPaths() {
        hydra::app::set_path_overrides(previous);
        std::remove(ini.c_str());
        std::remove(db.c_str());
    }
};

ChartLibraryEntry entry(int i) {
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

std::unique_ptr<AppState> app_with_library(const ScratchPaths& paths, int charts) {
    auto store = std::make_unique<RecordStore>(paths.db);
    std::vector<ChartLibraryEntry> all;
    for (int i = 0; i < charts; ++i) all.push_back(entry(i));
    store->rebuild_chart_library(all);
    auto app = std::make_unique<AppState>(Settings{}, std::move(store));
    REQUIRE(app->view_row_count() >= 3);
    return app;
}

}  // namespace

TEST_CASE("song panel: next and previous walk the view and never wrap") {
    ScratchPaths paths("panel_nav");
    auto app = app_with_library(paths, 20);
    app->select(app->view_row(0));
    CHECK(app->details_open());
    CHECK_FALSE(app->can_select_relative(-1));  // first row: nothing before it
    CHECK(app->can_select_relative(1));

    app->select_relative(1);
    CHECK(app->selected->notespath == app->view_row(1).notespath);
    app->select_relative(-1);
    CHECK(app->selected->notespath == app->view_row(0).notespath);
    app->select_relative(-1);  // no wrap to the last row
    CHECK(app->selected->notespath == app->view_row(0).notespath);

    const size_t last = app->view_row_count() - 1;
    app->select(app->view_row(last));
    CHECK_FALSE(app->can_select_relative(1));
    app->select_relative(1);
    CHECK(app->selected->notespath == app->view_row(last).notespath);
}

TEST_CASE("song panel: a song outside the view has no neighbours") {
    ScratchPaths paths("panel_outside");
    auto app = app_with_library(paths, 20);
    app->select(entry(999));  // not in the library at all
    CHECK_FALSE(app->can_select_relative(1));
    CHECK_FALSE(app->can_select_relative(-1));
}

TEST_CASE("song panel: tick runs the teardown once, on the closing edge") {
    ScratchPaths paths("panel_edge");
    auto app = app_with_library(paths, 5);
    app->select(app->view_row(0));
    app->tick(0.0);
    app->details_ui.file_checked_at = 1.0;  // as if the file was looked at
    app->tick(0.1);                          // still open: no teardown
    CHECK(app->details_ui.file_checked_at == 1.0);

    app->show_details = false;               // what the X and Escape do
    app->tick(0.2);
    CHECK_FALSE(app->details_open());
    CHECK(app->details_ui.file_checked_at == -1.0);  // close_details ran
}

TEST_CASE("song panel: nothing is locked while idle") {
    ScratchPaths paths("panel_lock");
    auto app = app_with_library(paths, 5);
    CHECK_FALSE(app->settings_locked());
    CHECK_FALSE(app->analyze_running());
    CHECK_FALSE(app->batch_running());
}
