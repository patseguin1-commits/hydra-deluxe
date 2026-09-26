#include "ui/app_state.h"

#include <algorithm>

#include "app/config.h"
#include "app/rules_file.h"
#include "ui/preview_controller.h"

namespace hydra::ui {

namespace {

// Settings plus hydra_rules.ini. A bad file is not fatal: the app still
// opens so the user can read the error, but analysis stays off.
StartupSettings load_startup_settings() {
    StartupSettings start{Settings::load(), {}};
    try {
        start.settings.rules = app::load_rules_file(app::default_rules_path());
    } catch (const app::RulesFileError& e) {
        start.rules_error = e.what();
    }
    return start;
}

}  // namespace

AppState::AppState() : AppState(load_startup_settings()) {}

// A bad rules file gates the store on kNoRulesFingerprint, so no row reads
// Ready under the defaults the settings still hold.
AppState::AppState(StartupSettings start)
    : AppState(start.settings,
               app::open_store(app::db_path(),
                               start.rules_error.empty()
                                   ? start.settings.rules.fingerprint()
                                   : core::kNoRulesFingerprint)) {
    rules_error = std::move(start.rules_error);
}

AppState::AppState(app::Settings initial_settings,
                   std::unique_ptr<store::RecordStore> initial_store)
    : settings(std::move(initial_settings)),
      store(std::move(initial_store)),
      committed_chartmode_(settings.chartmode_key()),
      committed_cap_(settings.cap_query()),
      committed_lens_(settings.lens()) {
    refresh_page();
}

// Out-of-line so the unique_ptr<PreviewController> can be a forward declaration
// in the header (its destructor needs the full type, which lives here).
AppState::~AppState() = default;

void AppState::set_render_device(ID3D11Device* device, ID3D11DeviceContext* context) {
    render_device_ = device;
    render_context_ = context;
}

PreviewController* AppState::preview_controller() {
    if (!preview && render_device_)
        preview = std::make_unique<PreviewController>(render_device_, render_context_);
    return preview.get();
}

void AppState::refresh_page() {
    std::optional<std::string> search_opt = search.empty() ? std::nullopt : std::optional(search);
    current_page.total_count = store->chart_library_count(search_opt);

    // Keep the page in range: a shrinking result set (new search, rescan) or
    // a taller window (more rows per page) can leave table_viewpage pointing
    // past the last page, stranding the user on an empty page of "-----".
    int64_t last_page =
        std::max<int64_t>(0, (current_page.total_count - 1) / rows_per_page);
    table_viewpage = std::clamp(table_viewpage, 0, static_cast<int>(last_page));

    current_page.rows =
        store->list_chart_library(search_opt, table_viewpage * rows_per_page, rows_per_page);

    // Resolve each row's Best Path summary once here instead of per row per
    // frame in the render loop (a SQLite query at 60fps x 200 rows, on the
    // render thread, against the same mutex the batch workers hold).
    current_page.summaries.clear();
    current_page.summaries.reserve(current_page.rows.size());
    for (const store::ChartLibraryEntry& row : current_page.rows) {
        store::SummaryLookup summary = store->get_summary(settings.record_key(row.md5));
        LibraryPage::RowSummary rs;
        rs.state = summary.status;
        rs.bestpath = std::move(summary.bestpath);
        current_page.summaries.push_back(std::move(rs));
    }

    library_total =
        search.empty() ? current_page.total_count : store->chart_library_count(std::nullopt);
}

void AppState::set_rows_per_page(int rows) {
    if (rows == rows_per_page) return;
    rows_per_page = rows;
    refresh_page();
}

void AppState::select(const store::ChartLibraryEntry& entry) {
    // Tear down any preview for the previous chart: its audio device must stop
    // before a new chart's is opened, and the highway must not keep playing the
    // old song.
    if (preview) preview->close();
    // Drop the dynamics cache: the new chart needs its own parse.
    if (dynamics_job) { dynamics_job->cancel(); dynamics_job.reset(); }
    dynamics_result.reset();
    dynamics_key.clear();
    dynamics_store_error.clear();
    selected = entry;
    show_details = true;
    refresh_viewed_record();
}

void AppState::refresh_viewed_record() {
    if (!selected) {
        viewed = store::RecordLookup{};
        return;
    }
    viewed = store->get_record(settings.record_key(selected->md5));
    record_generation.bump();
}

void AppState::update_dynamics() {
    if (!selected) return;

    bool pro = settings.view_prodrums;
    Difficulty diff = settings.difficulty();
    std::string want_key = app::dynamics_cache_key(selected->notespath, pro, diff);

    // If the cached result is from a different key, drop it.
    if (!dynamics_key.empty() && dynamics_key != want_key) {
        dynamics_result.reset();
        dynamics_key.clear();
        dynamics_store_error.clear();
    }
    // Cancel any in-flight job that was started for a different key.
    if (dynamics_job && dynamics_job->key() != want_key) {
        dynamics_job->cancel();
        dynamics_job.reset();
    }

    // Already have a result -- nothing to do.
    if (dynamics_result) return;

    // Try the store before starting a background parse.
    if (!dynamics_job) {
        auto stored =
            app::load_stored_dynamics(*store, app::dynamics_store_key(selected->md5, diff, pro));
        if (stored) {
            dynamics_result = std::move(*stored);
            dynamics_key = want_key;
            return;
        }
        // Store miss, an older count stamp or a decode failure: start the
        // background job.
        dynamics_job = std::make_unique<DynamicsLoadJob>(
            *selected, pro, diff);
        dynamics_job->start();
    }

    // Reap a finished job.
    if (dynamics_job && dynamics_job->finished()) {
        if (dynamics_job->ok()) {
            dynamics_result = dynamics_job->take_result();
            dynamics_key = dynamics_job->key();
            // Persist to the store so the next open is instant.
            try {
                app::save_dynamics(*store, app::dynamics_store_key(selected->md5, diff, pro),
                                   *dynamics_result);
                dynamics_store_error.clear();
            } catch (const std::exception& e) {
                dynamics_store_error =
                    std::string("Counted, but saving failed: ") + e.what();
            }
            dynamics_job.reset();
        }
        // On failure, keep the job around so we can read its error().
    }
}

void AppState::start_scan() {
    if (scan_job && !scan_job->snapshot().finished) return;
    scan_job = std::make_unique<ScanJob>(settings.chartfolders, *store);
    scan_job->start();
}

void AppState::start_batch(bool redo) {
    if (analysis_blocked()) return;
    if (batch_job && !batch_job->snapshot().finished) return;

    // The job loads the (possibly search-filtered) item list on its own
    // thread; doing the unbounded SELECT here froze a frame on big libraries.
    std::optional<std::string> search_opt = search.empty() ? std::nullopt : std::optional(search);
    batch_job = std::make_unique<BatchJob>(search_opt, settings.batch_run(), *store, redo);
    report_started = false;
    report_outcome_shown = false;
    batch_job->start();
}

void AppState::start_analyze() {
    if (analysis_blocked()) return;
    if (!selected) return;
    if (analyze_job && !analyze_job->finished()) return;
    analyze_job = std::make_unique<AnalyzeJob>(*selected, settings.record_key(selected->md5),
                                               settings.to_analysis_settings());
    analyze_generation.bump();
    analyze_job->start();
}

std::string AppState::store_finished_analysis() {
    if (!analyze_job || !analyze_job->finished() || !analyze_job->ok()) return "";
    try {
        // Store against the identity the job snapshotted at start -- NOT
        // `selected`, which can point at a different song by now (close the
        // modal mid-analysis, click another row).
        const store::ChartLibraryEntry& song = analyze_job->song();
        app::AnalysisResult result = analyze_job->take_result();
        store->add_song(song.md5, song.title, song.artist, song.charter,
                        result.song);
        store->add_record(analyze_job->key(), result.record);
        // Key the dynamics by the settings the job snapshotted, not the
        // current ones: the user may have moved the difficulty box since it
        // started.
        const app::AnalysisSettings& as = analyze_job->settings();
        app::store_dynamics_from_analysis(*store, song.md5, result.song, as.bass2x,
                                          as.difficulty, as.prodrums);
        refresh_viewed_record();
        refresh_page();  // the library row's Best Path cell is cached per page
        return "";
    } catch (const std::exception& e) {
        return std::string("Analyzed, but saving failed: ") + e.what();
    }
}

void AppState::start_dm_fetch() {
    if (dm_fetch_job && !dm_fetch_job->finished()) return;
    dm_users.clear();
    dm_fetch_job = std::make_unique<DmFetchUsersJob>();
    dm_fetch_job->start();
}

void AppState::start_dm_report(const std::string& discord_id, const std::string& username) {
    if (dm_report_job && !dm_report_job->finished()) return;
    dm_report_job = std::make_unique<DmReportJob>(*store, discord_id, username,
                                                  settings.chartmode_key(),
                                                  settings.lens(),
                                                  settings.auto_open_report);
    // Remember the choice so the picker can pre-select it next time.
    settings.dm_last_user = discord_id;
    commit_settings();
    dm_report_job->start();
}

void AppState::set_status(std::string message) {
    status_message = std::move(message);
    status_generation.bump();
}

void AppState::commit_settings() {
    if (!settings.save())
        set_status("Settings could not be saved — " + app::ini_path() +
                   " is not writable.");

    // Which record a chart shows is (chart, chart mode, SP cap, lens). When
    // any of the last three moves, every cached lookup is answering the old
    // question and has to be re-asked.
    std::string chartmode = settings.chartmode_key();
    store::CapQuery cap = settings.cap_query();
    store::Lens lens = settings.lens();
    if (chartmode != committed_chartmode_) {
        // A different chart mode is a different library listing, so the user
        // starts over at page one.
        table_viewpage = 0;
        refresh_page();
        refresh_viewed_record();
    } else if (cap != committed_cap_ || lens != committed_lens_) {
        // The cap box and the search controls live in the details modal.
        // Resetting the page here would yank the library out from under a
        // user who never touched it.
        refresh_page();
        refresh_viewed_record();
    }
    committed_chartmode_ = std::move(chartmode);
    committed_cap_ = cap;
    committed_lens_ = lens;
}

}  // namespace hydra::ui
