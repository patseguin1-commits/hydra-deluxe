// Shared UI state — the C++ port of hydra_app.py's HyAppUserSettings /
// HyAppRecordBook / HyAppState. One AppState is built at startup and handed
// to every view's render function each frame.
//
// Persistence differs from Python on purpose: settings live in their own INI
// next to the executable (not hymisc.INIPATH's format) and the chart library
// table now lives on RecordStore (see store/record_store.h) instead of a
// separate ad hoc scan_library() connection — both were free to redesign,
// same as the record format itself (Phase 4).

#ifndef HYDRA_UI_APP_STATE_H
#define HYDRA_UI_APP_STATE_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "app/config.h"
#include "store/record_store.h"
#include "ui/jobs.h"

namespace hydra::ui {

// The settings struct itself now lives in app/config.h (shared with the CLI
// exes); the GUI binds it to its compile-time edition in AppState's ctor.
using Settings = app::Settings;

// One page of the library table, plus enough to know if there's more.
struct LibraryPage {
    std::vector<store::ChartLibraryEntry> rows;
    int64_t total_count = 0;
};

class AppState {
public:
    AppState();

    Settings settings;
    std::unique_ptr<store::RecordStore> store;

    // Library browsing.
    std::string search;              // empty = no filter
    int table_viewpage = 0;

    // How many library rows fit the current window height. Recomputed by
    // library_view each frame from the available content region (rather than
    // a fixed constant) so a taller window shows more rows instead of
    // leaving blank space below a fixed-size table, and a shorter one still
    // fits without clipping. set_rows_per_page() re-queries the current page
    // only when the count actually changes.
    int rows_per_page = 15;
    void set_rows_per_page(int rows);

    LibraryPage current_page;
    int64_t library_total = 0;  // unfiltered count, for the "Library (N charts)" title
    void refresh_page();  // re-queries current_page + library_total from `store`

    // Selection / details modal.
    std::optional<store::ChartLibraryEntry> selected;
    bool show_details = false;
    void select(const store::ChartLibraryEntry& entry);

    // The record for `selected` under the current chartmode, reloaded on
    // selection and after a fresh analysis. nullopt means "no record yet";
    // an empty-paths record (see RecordStore::get_record) means "stale
    // version, please re-analyze".
    std::optional<HydraRecord> viewed_record;
    int record_generation = 0;  // bumped by refresh_viewed_record(); invalidates UI selection caches
    void refresh_viewed_record();

    // Background jobs (at most one of each kind runs at a time).
    std::unique_ptr<ScanJob> scan_job;
    std::unique_ptr<BatchJob> batch_job;
    std::unique_ptr<AnalyzeJob> analyze_job;

    void start_scan();
    void start_batch(bool redo);
    void start_analyze();  // analyzes `selected` under the current chartmode

    bool batch_redo = false;  // "redo existing" checkbox state

    // Clipboard text set by the details view when a path is picked, copied on
    // Ctrl+C — mirrors appstate.current_path_copytext.
    std::string current_path_copytext;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_APP_STATE_H
