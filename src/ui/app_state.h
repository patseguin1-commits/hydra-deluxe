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
#include "ui/generation.h"
#include "ui/jobs.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace hydra::ui {

class PreviewController;

// The settings struct itself lives in app/config.h (shared with the CLI
// exes).
using Settings = app::Settings;

// One page of the library table, plus enough to know if there's more.
struct LibraryPage {
    // The "Best Path" cell's state, resolved once per page refresh — querying
    // the store per visible row per frame contended the store's mutex with
    // batch workers thousands of times a second.
    enum class SummaryState { New, Current, Stale };
    struct RowSummary {
        SummaryState state = SummaryState::New;
        std::string bestpath;  // set when state == Current
    };

    std::vector<store::ChartLibraryEntry> rows;
    std::vector<RowSummary> summaries;  // parallel to `rows`
    int64_t total_count = 0;
};

// Per-frame UI state of the Song Details modal. Owned here, not as statics in
// the draw code: a static outlives this AppState (the UI test runner builds one
// per test) and a static pointer into viewed_record outlived the record itself
// (1.5.1 SP-cap crash). Everything in here is derived from, and must be
// re-synced against, the AppState fields it mirrors.
struct DetailsViewState {
    // Tracks show_details' false->true edge, which is what opens the popup.
    bool prev_open = false;
    // Points into viewed_record; valid only for record_watcher's generation.
    const Path* selected_path = nullptr;
    GenerationWatcher record_watcher;
    // The SP cap number box keeps its last value while Auto is ticked, so
    // unticking returns to it.
    int last_cap = kCloneHeroSpCap;
    // When "Copied!" last flashed after Copy path string; -1 = never.
    double copied_at = -1.0;
    // Analyze-progress completion state, keyed on analyze_generation -- not on
    // the AnalyzeJob's address: a freed job's block can be handed straight back
    // to the next make_unique, and a pointer compare then carries
    // `stored`/`done_at` over from the previous job -- the fresh result is
    // never stored and the stale done_at dismisses the modal on its first
    // finished frame.
    GenerationWatcher analyze_watcher;
    double done_at = -1.0;
    bool stored = false;
    std::string store_error;
};

class AppState {
public:
    AppState();
    ~AppState();  // out-of-line: PreviewController is only forward-declared here

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

    // The details modal's own per-frame state (see DetailsViewState above).
    DetailsViewState details_ui;

    // The record for `selected` under the current chartmode, reloaded on
    // selection and after a fresh analysis. nullopt means "no record yet";
    // an empty-paths record (see RecordStore::get_record) means "stale
    // version, please re-analyze".
    std::optional<HydraRecord> viewed_record;
    Generation record_generation;  // bumped by refresh_viewed_record(); invalidates UI selection caches
    void refresh_viewed_record();

    // Timing context for `selected`'s song, loaded alongside viewed_record —
    // the store is DB+mutex, so the per-frame details view must never query
    // it. nullopt when the song isn't registered.
    std::optional<SongTiming> viewed_timing;

    // 3D Preview (Phase 5). The GUI's shared D3D11 device is injected once at
    // startup (set_render_device, mirroring load_icons); the controller is
    // created lazily on first use -- a session that never opens the Preview tab
    // pays nothing. preview_controller() returns null when no device was set.
    void set_render_device(ID3D11Device* device, ID3D11DeviceContext* context);
    PreviewController* preview_controller();
    std::unique_ptr<PreviewController> preview;

    // Background jobs (at most one of each kind runs at a time).
    std::unique_ptr<ScanJob> scan_job;
    std::unique_ptr<BatchJob> batch_job;
    std::unique_ptr<AnalyzeJob> analyze_job;
    // Bumped by start_analyze(). The details modal keys its per-job completion
    // state on this, NOT on the AnalyzeJob's address: the heap can hand a new
    // job the previous job's block, and a pointer compare then leaves the
    // "already stored" flag stale, silently discarding the finished analysis.
    Generation analyze_generation;
    std::unique_ptr<ReportJob> report_job;

    // Whether this batch run has already kicked off its path report — one
    // report per run, however long the finished modal stays open.
    bool report_started = false;

    // "Compare dmleaderboards user" picker + its two network jobs. The fetch
    // job loads the ladder into dm_users; the report job builds the HTML.
    bool dm_picker_open = false;
    std::vector<net::DmUser> dm_users;
    std::unique_ptr<DmFetchUsersJob> dm_fetch_job;
    std::unique_ptr<DmReportJob> dm_report_job;

    void start_scan();
    void start_batch(bool redo);
    void start_analyze();  // analyzes `selected` under the current chartmode

    // Persists the finished analyze_job's result (song + record) and
    // refreshes the views that cache it. Returns an error message on a failed
    // save, empty on success. Lives here, not in the details modal's draw
    // code: persistence is state work, the view only shows the outcome.
    std::string store_finished_analysis();
    void start_dm_fetch();  // loads the dmleaderboards user list
    void start_dm_report(const std::string& discord_id, const std::string& username);

    bool batch_redo = false;  // "redo existing" checkbox state

    // "Analyze library" opens its modal in a confirm stage before any work
    // starts; true while that stage is showing (batch_job not yet created).
    bool batch_confirm_pending = false;

    // Set by the details modal's "Rescan library" remedy: the main window
    // starts the scan on its next frame (the scan modal belongs to it).
    bool request_scan = false;

    // Transient feedback line ("folder already added", save failures, ...).
    // The view times the fade-out off status_generation changing.
    std::string status_message;
    Generation status_generation;
    void set_status(std::string message);

    // settings.save() + a status message when the INI can't be written —
    // save() failing silently made changes look persisted when they weren't.
    void save_settings();

private:
    ID3D11Device* render_device_ = nullptr;
    ID3D11DeviceContext* render_context_ = nullptr;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_APP_STATE_H
