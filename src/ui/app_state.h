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
#include <utility>
#include <vector>

#include "app/analysis.h"
#include "app/config.h"
#include "app/dynamics_breakdown.h"
#include "app/path_view.h"
#include "core/rules.h"
#include "store/record_store.h"
#include "ui/dm_jobs.h"
#include "ui/dynamics_load_job.h"
#include "ui/generation.h"
#include "ui/library_jobs.h"
#include "ui/library_model.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace hydra::ui {

class PreviewController;

// The settings struct itself lives in app/config.h (shared with the CLI
// exes).
using Settings = app::Settings;

// Per-frame UI state of the Song Details modal. Owned here, not as statics in
// the draw code: a static outlives this AppState (the UI test runner builds one
// per test) and a static pointer into `viewed`'s record outlived the record
// itself (1.5.1 SP-cap crash). Everything in here is derived from, and must be
// re-synced against, the AppState fields it mirrors.
struct DetailsViewState {
    // Tracks show_details' false->true edge, which is what opens the popup.
    bool prev_open = false;
    // Points into `viewed`'s record; valid only for record_watcher's generation.
    const Path* selected_path = nullptr;
    GenerationWatcher record_watcher;
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
    // The Paths tab's built views, kept between frames (app::PathsTabCache).
    app::PathsTabCache paths_tab;
    // The Preview overlay's key for selected_path (app::path_overlay_key),
    // built when the selection or the record changes instead of every frame.
    std::string overlay_key;
    const Path* overlay_key_path = nullptr;
    int overlay_key_generation = -1;
    // The chart file's presence (the "Song file not found" line), as of the
    // last look. Looked at when the window opens and then every
    // AppState::kFileCheckSeconds, not every frame: on a sleeping or network
    // drive one look can stall a frame. -1 = look now.
    bool file_ok = true;
    double file_checked_at = -1.0;
};

// Per-frame UI state of the main window's library view. Owned here, not as
// statics in the draw code, for the same reason as DetailsViewState: a static
// outlives this AppState, so the UI test runner's next app inherited the last
// test's search text and dmleaderboards filter.
struct LibraryViewState {
    // The status line's fade. The watcher starts at "already seen" for this
    // app's counter (0), so app startup does not start a fade.
    GenerationWatcher status_watcher{/*seen=*/0};
    double status_shown_at = -1.0;
    // The folder waiting on the "Remove folder?" confirm.
    std::optional<size_t> confirm_remove;
    // The search box's text and whether it was filled from `search` yet.
    // Typing is applied at most every 150 ms: `search_pending` holds an edit
    // not applied yet, `search_applied_at` when the last one was.
    char search_buf[256] = "";
    bool search_synced = false;
    bool search_pending = false;
    double search_applied_at = -1.0;
    // Whether the table's sort was read from its header yet. The ImGui
    // context outlives an AppState (the GUI tests build one per test), so a
    // fresh model reads the header's current sort on its first frame.
    bool sort_synced = false;
    // The panel state the table last fitted its Charter and Folder columns
    // to; unset = fit them on the next frame.
    std::optional<bool> columns_for_panel;
    // The selection (notespath) the table last scrolled to, so it scrolls
    // only when the selection changes (the panel's Previous/Next song).
    std::string scrolled_to;
    // The dmleaderboards picker's name filter.
    char dm_filter[128] = "";
    // Whether the path report file exists, as of the last look.
    bool report_exists = false;
    double report_checked_at = -1.0;  // -1 = look now
};

// What the app reads before it opens the store: the settings, with
// hydra_rules.ini already loaded, and the loader's error if the file was bad.
struct StartupSettings {
    app::Settings settings;
    std::string rules_error;
};

class AppState {
public:
    AppState();
    // Injecting form, for tests and harnesses: same startup work as the
    // default constructor, but on a caller-supplied settings struct and store
    // instead of the user's INI and database.
    AppState(app::Settings settings, std::unique_ptr<store::RecordStore> store);
    ~AppState();  // out-of-line: PreviewController is only forward-declared here

    Settings settings;
    std::unique_ptr<store::RecordStore> store;
    // Set at startup when hydra_rules.ini is bad (the loader's message, which
    // names the key). While set, analysis is off: the Analyze buttons are
    // disabled and start_batch/start_analyze do nothing. It clears only on a
    // restart with a fixed file; there is no fallback to the default rules.
    std::string rules_error;
    bool analysis_blocked() const { return !rules_error.empty(); }

    // Library browsing: every scanned chart in memory, with its stored
    // summary, filtered by the search and the status chip and sorted by the
    // table (ui/library_model.h).
    LibraryModel library;
    std::string search;          // the applied search text; empty = no filter
    int64_t library_total = 0;   // every chart, for "5 of 97 charts"
    // Applies a search at once (the box throttles its own calls).
    void set_search(std::string text);
    // Re-reads every chart and summary: at startup and after a scan.
    void reload_library();
    // Re-reads every row's summary: after a settings change or a batch step.
    void refresh_library_summaries();
    // Re-reads one chart's summary: after one song's analysis is stored.
    void refresh_library_row(const std::string& md5);
    // Once per frame, from the library pane: reloads after a scan finishes,
    // and re-reads summaries while a batch runs -- at most once a second, and
    // only when the batch stored something since the last look.
    void tick_library(double now);

    // The rows on screen, in order, as indices into library.rows().
    const std::vector<size_t>& library_view_order() const { return library.order(); }
    size_t library_shown_count() const { return library.order().size(); }
    // The row at position `view_index` of library_view_order().
    const LibraryRow& library_row_at(size_t view_index) const {
        return library.rows()[library.order()[view_index]];
    }
    // How many charts the search matches (the "All" chip's count): the N of
    // "Analyze search (N)...".
    size_t library_match_count() const { return library.counts().all; }
    // Those charts, in table order: what "Analyze search (N)..." analyzes.
    std::vector<store::ChartLibraryEntry> library_matches() const;

    // Selection / details modal.
    std::optional<store::ChartLibraryEntry> selected;
    bool show_details = false;
    void select(const store::ChartLibraryEntry& entry);

    // Everything that must stop when the Song Details window closes. It runs
    // once, on the window's open-to-closed edge, whatever closed it: the X,
    // the Rescan library button, or a new selection. An unfinished analysis
    // is cancelled, the Preview stops and lets go of its audio device, a
    // finished Dynamics count is kept and an unfinished one is cancelled.
    // Safe to call when already closed.
    void close_details();

    // Whether the selected chart's file exists, as of the last look; looks
    // again once `now` (seconds) is kFileCheckSeconds past it.
    bool selected_file_ok(double now);
    static constexpr double kFileCheckSeconds = 2.0;

    // The details modal's own per-frame state (see DetailsViewState above).
    DetailsViewState details_ui;
    // The library view's own per-frame state (see LibraryViewState above).
    LibraryViewState library_ui;

    // Whether the path report file exists, as of the last look; looks again
    // once `now` (seconds) is kReportCheckSeconds past it, or at once after
    // library_ui.report_checked_at is reset to -1.
    bool report_file_shown(double now);
    static constexpr double kReportCheckSeconds = 2.0;

    // The stored-record lookup for `selected` under the current chartmode,
    // reloaded on selection and after a fresh analysis. It carries the status
    // (not analyzed / stale / ready), the record when there is one, and that
    // song's timing context — the store is DB+mutex, so the per-frame details
    // view must never query it again. Reset to a default (NotAnalyzed) when
    // nothing is selected.
    store::RecordLookup viewed;
    Generation record_generation;  // bumped by refresh_viewed_record(); invalidates UI selection caches
    void refresh_viewed_record();

    // 3D Preview (Phase 5). The GUI's shared D3D11 device is injected once at
    // startup (set_render_device, mirroring load_icons); the controller is
    // created lazily on first use -- a session that never opens the Preview tab
    // pays nothing. preview_controller() returns null when no device was set.
    void set_render_device(ID3D11Device* device, ID3D11DeviceContext* context);
    PreviewController* preview_controller();
    std::unique_ptr<PreviewController> preview;

    // Dynamics tab: a background job that re-parses the chart for per-pad
    // ghost/accent/normal counts, plus its cached result (keyed by chart +
    // pro + difficulty; invalidated when any of those change).
    std::unique_ptr<DynamicsLoadJob> dynamics_job;
    std::optional<app::DynamicsBreakdown> dynamics_result;
    std::string dynamics_key;  // the key the cached result was built for
    std::string dynamics_store_error;  // non-empty when put_dynamics failed

    // Dynamics lifecycle: check the store for a cached breakdown, manage the
    // background parse job, and persist new results. Called every frame from
    // the details modal, before the Dynamics tab draws. Keeps store access
    // on the UI thread and out of the render function.
    void update_dynamics();

    // Stores a finished Dynamics parse and drops its job. The details window
    // calls it every frame, whichever tab shows; a parse that finished while
    // another tab was up used to be thrown away at close.
    void reap_dynamics();

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

    // Whether the batch modal already showed this report job's outcome. When
    // it didn't (you clicked Continue while the report was still building),
    // the main window posts a status line when the job lands instead.
    bool report_outcome_shown = false;

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

    // The one way to finish a settings change: write the INI (with a status
    // message when it can't be written — a silent failure made changes look
    // persisted when they weren't) and then refresh whatever the change
    // invalidated.
    //
    // This is the single place that knows which settings change a record's
    // identity — the chart mode, the SP cap, and the lens (the ms limit and
    // the score range a result ran under). Every widget just mutates
    // `settings` and calls this, so none of them can forget a refresh. That
    // forgetting is exactly the bug class here: the 1.5.1 SP-cap crash came
    // from this path, and the Pro Drums / 2x Bass checkboxes used to refresh
    // the library page while leaving `viewed` pointing at the old record.
    void commit_settings();

    // The number boxes' form of commit_settings: the same refresh at once,
    // but the INI waits for flush_settings. A held +/- button changes the
    // value every frame, and each change used to rewrite the file.
    void edit_settings();
    // Writes the INI if an edit_settings change is not saved yet. run_frame
    // calls it once no widget is active, which is when an edit has ended.
    void flush_settings();

private:
    explicit AppState(StartupSettings start);
    ID3D11Device* render_device_ = nullptr;
    ID3D11DeviceContext* render_context_ = nullptr;

    // The identity-relevant settings as of the last commit, so commit_settings
    // can tell an identity change from any other settings edit.
    std::string committed_chartmode_;
    store::CapQuery committed_cap_;
    store::Lens committed_lens_;

    // An edit_settings change the INI does not have yet.
    bool settings_unsaved_ = false;
    void save_settings();
    // The refresh half of commit_settings.
    void apply_settings();
    // tick_library's memory: whether the current scan's result was read
    // yet, and the batch's stored count and time at the last summary read.
    bool scan_reloaded_ = true;
    int batch_seen_completed_ = 0;
    double batch_refreshed_at_ = -1.0;

    // The number boxes step through settings one value at a time, and each
    // step used to decode the chart's record again. `viewed_key_` is what
    // `viewed` answers; lookups the boxes stepped away from are parked here
    // and come back without asking the store. Cleared whenever a record may
    // have changed under them: a new selection or a stored analysis.
    std::optional<store::RecordKey> viewed_key_;
    std::vector<std::pair<store::RecordKey, store::RecordLookup>> parked_lookups_;
    static constexpr size_t kParkedLookups = 16;
    // Shows the lookup for the current settings: parked if seen, read otherwise.
    void show_record_for_settings();
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_APP_STATE_H
