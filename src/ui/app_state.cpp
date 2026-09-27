#include "ui/app_state.h"

#include <algorithm>
#include <unordered_set>

#include "app/config.h"
#include "app/rules_file.h"
#include "app/report_files.h"
#include "core/winstr.h"
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

// A bad rules file gates the store on RulesStamp::none(), so no row reads
// Ready under the defaults the settings still hold.
AppState::AppState(StartupSettings start)
    : AppState(start.settings,
               app::open_store(app::db_path(),
                               start.rules_error.empty()
                                   ? core::RulesStamp::of(start.settings.rules)
                                   : core::RulesStamp::none())) {
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
AppState::~AppState() { flush_settings(); }  // an edit in progress still lands

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
    refresh_summaries();

    library_total =
        search.empty() ? current_page.total_count : store->chart_library_count(std::nullopt);
}

void AppState::refresh_summaries() {
    // Resolve each row's Best Path summary once here instead of per row per
    // frame in the render loop (a SQLite query at 60fps x 200 rows, on the
    // render thread, against the same mutex the batch workers hold).
    // One query for the whole page, not one per row.
    std::vector<std::string> hashes;
    hashes.reserve(current_page.rows.size());
    for (const store::ChartLibraryEntry& row : current_page.rows) hashes.push_back(row.md5);
    std::vector<store::SummaryLookup> lookups = store->get_summaries(
        hashes, settings.chartmode_key(), settings.cap_query(), settings.lens());
    current_page.summaries.clear();
    current_page.summaries.reserve(lookups.size());
    for (store::SummaryLookup& summary : lookups) {
        LibraryPage::RowSummary rs;
        rs.state = summary.status;
        rs.bestpath = std::move(summary.bestpath);
        current_page.summaries.push_back(std::move(rs));
    }
}

void AppState::set_rows_per_page(int rows) {
    if (rows == rows_per_page) return;
    rows_per_page = rows;
    refresh_page();
}

void AppState::select(const store::ChartLibraryEntry& entry) {
    // The previous chart's window, torn down the one way. A row can only be
    // clicked while the window is closed, when this already ran, so it is a
    // no-op in the app; it matters for callers that select directly.
    close_details();
    parked_lookups_.clear();  // a new chart: nothing parked applies
    selected = entry;
    show_details = true;
    refresh_viewed_record();
}

void AppState::close_details() {
    show_details = false;
    // With the window gone there is nowhere to show an analysis' progress,
    // and the search would keep burning CPU unseen.
    // The main window reaps the job once the cancel lands.
    if (analyze_job && !analyze_job->finished()) analyze_job->cancel();
    // The audio device must stop, and the GPU and decode work must not keep
    // running behind a hidden window.
    if (preview) preview->close();
    // Keep a count that already finished; cancel one still parsing.
    reap_dynamics();
    if (dynamics_job) {
        dynamics_job->cancel();
        dynamics_job.reset();
    }
    dynamics_result.reset();
    dynamics_key.clear();
    dynamics_store_error.clear();
    // The next open looks at the chart file at once.
    details_ui.file_checked_at = -1.0;
}

bool AppState::selected_file_ok(double now) {
    if (!selected) return false;
    if (details_ui.file_checked_at < 0.0 ||
        now - details_ui.file_checked_at >= kFileCheckSeconds) {
        details_ui.file_ok = file_exists_utf8(selected->notespath);
        details_ui.file_checked_at = now;
    }
    return details_ui.file_ok;
}

void AppState::refresh_viewed_record() {
    if (!selected) {
        viewed = store::RecordLookup{};
        viewed_key_.reset();
        return;
    }
    store::RecordKey key = settings.record_key(selected->md5);
    viewed = store->get_record(key);
    viewed_key_ = std::move(key);
    record_generation.bump();
}

void AppState::show_record_for_settings() {
    if (!selected) {
        refresh_viewed_record();
        return;
    }
    store::RecordKey key = settings.record_key(selected->md5);
    if (viewed_key_ && *viewed_key_ == key) return;

    std::optional<store::RecordLookup> found;
    for (auto it = parked_lookups_.begin(); it != parked_lookups_.end(); ++it) {
        if (it->first == key) {
            found = std::move(it->second);
            parked_lookups_.erase(it);
            break;
        }
    }
    // Park what is showing now. Moves, not copies: the decoded record changes
    // hands without being copied.
    if (viewed_key_) {
        parked_lookups_.emplace_back(std::move(*viewed_key_), std::move(viewed));
        if (parked_lookups_.size() > kParkedLookups)
            parked_lookups_.erase(parked_lookups_.begin());
    }
    if (found) {
        viewed = std::move(*found);
        viewed_key_ = std::move(key);
        record_generation.bump();  // selected_path must re-sync, as after a read
    } else {
        refresh_viewed_record();
    }
}

bool AppState::report_file_shown(double now) {
    if (library_ui.report_checked_at < 0.0 ||
        now - library_ui.report_checked_at >= kReportCheckSeconds) {
        library_ui.report_exists = app::report_file_exists();
        library_ui.report_checked_at = now;
    }
    return library_ui.report_exists;
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

    reap_dynamics();
}

void AppState::reap_dynamics() {
    if (!dynamics_job || !dynamics_job->finished()) return;
    // On failure, keep the job around so the tab can read its error().
    if (!dynamics_job->ok()) return;
    dynamics_result = dynamics_job->take_result();
    dynamics_key = dynamics_job->key();
    // Persist under what the job counted, so the next open is instant.
    try {
        app::save_dynamics(*store,
                           app::dynamics_store_key(dynamics_job->entry().md5,
                                                   dynamics_job->difficulty(),
                                                   dynamics_job->pro()),
                           *dynamics_result);
        dynamics_store_error.clear();
    } catch (const std::exception& e) {
        dynamics_store_error = std::string("Counted, but saving failed: ") + e.what();
    }
    dynamics_job.reset();
}

void AppState::start_scan() {
    if (scan_job && !scan_job->snapshot().finished) return;
    scan_job = std::make_unique<ScanJob>(settings.chartfolders, *store);
    scan_job->start();
}

void AppState::open_batch_confirm() {
    // T12 merge-fix: these two lines become `batch_scope = library_matches();`
    // and `batch_scope_with_result = library.counts().analyzed;`, the rows the
    // library shows. In this worktree the library is still the SQL-searched page.
    std::optional<std::string> search_opt = search.empty() ? std::nullopt : std::optional(search);
    batch_scope = store->list_chart_library(search_opt, 0, -1);  // -1 = no limit
    const std::unordered_set<std::string> done =
        store->analyzed_hashes(settings.chartmode_key(), settings.cap_query(), settings.lens());
    batch_scope_with_result = 0;
    for (const store::ChartLibraryEntry& e : batch_scope)
        if (done.count(e.md5)) ++batch_scope_with_result;
    batch_confirm_pending = true;
}

void AppState::start_batch(bool redo) {
    if (analysis_blocked()) return;
    if (batch_job && !batch_job->snapshot().finished) return;
    // A direct call (a test) has no confirm open: load the list here.
    if (!batch_confirm_pending) open_batch_confirm();
    batch_confirm_pending = false;
    // Exactly the charts the confirm counted -- not a search string that SQL
    // would match differently from the library's own search.
    batch_job = std::make_unique<BatchJob>(std::move(batch_scope), settings.batch_run(), *store,
                                           redo);
    batch_scope.clear();
    batch_scope_with_result = 0;
    report_started = false;
    report_outcome_shown = false;
    batch_finish_seen_ = false;
    batch_job->start();
}

void AppState::update_background_jobs() {
    if (batch_job && !batch_finish_seen_ && batch_job->snapshot().finished) {
        batch_finish_seen_ = true;
        refresh_page();  // T12 merge-fix: delete; tick_library re-reads after a batch
        // One path report per finished run. A stopped run keeps its results
        // but builds no report: a report of part of the library would read
        // as the whole of it.
        if (!batch_job->is_cancelled() && !report_started) {
            report_started = true;
            report_job = std::make_unique<ReportJob>(*store, settings.cap_query(), settings.lens(),
                                                     settings.auto_open_report,
                                                     settings.hit_window_ms);
            report_job->start();
        }
    }

    // The finished strip shows the report's outcome. Dismissed before the
    // report landed, the outcome goes to the status line instead.
    if (!batch_job && report_job && report_job->finished()) {
        library_ui.report_checked_at = -1.0;  // a new report: look at once
        if (!report_outcome_shown && !report_job->is_cancelled()) {
            const std::string where = report_job->saved_path().u8string();
            if (!report_job->ok())
                set_problem("The path report could not be built. " + report_job->message());
            else if (!report_job->open_problem().empty())
                set_problem("Path report saved to " + where + ". " + report_job->open_problem());
            else
                set_status("Path report saved to " + where + ".");
        }
        report_job.reset();
    }

    // Let go of cancelled leaderboard jobs once their request has returned.
    parked_dm_fetches.erase(
        std::remove_if(parked_dm_fetches.begin(), parked_dm_fetches.end(),
                       [](const std::unique_ptr<DmFetchUsersJob>& job) { return job->finished(); }),
        parked_dm_fetches.end());
    parked_dm_reports.erase(
        std::remove_if(parked_dm_reports.begin(), parked_dm_reports.end(),
                       [](const std::unique_ptr<DmReportJob>& job) { return job->finished(); }),
        parked_dm_reports.end());
}

void AppState::cancel_dm_fetch() {
    if (!dm_fetch_job) return;
    dm_fetch_job->cancel();
    if (!dm_fetch_job->finished()) parked_dm_fetches.push_back(std::move(dm_fetch_job));
    dm_fetch_job.reset();
}

void AppState::cancel_dm_report() {
    if (!dm_report_job) return;
    dm_report_job->cancel();
    if (!dm_report_job->finished()) parked_dm_reports.push_back(std::move(dm_report_job));
    dm_report_job.reset();
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
        // Key the dynamics by the settings the job snapshotted, not the
        // current ones: the user may have moved the difficulty box since it
        // started.
        const app::AnalysisSettings& as = analyze_job->settings();
        store->save_analysis(song.md5, song.title, song.artist, song.charter, result.song,
                             store::prepare_row(analyze_job->key(), result.record),
                             app::dynamics_entry_from_analysis(song.md5, result.song,
                                                               as.bass2x, as.difficulty,
                                                               as.prodrums));
        parked_lookups_.clear();  // a record just changed
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
    library_ui.dm_opened_by_click = false;
    dm_report_job->start();
}

void AppState::set_status(std::string message) {
    status_message = std::move(message);
    status_is_problem = false;
    status_generation.bump();
}

void AppState::set_problem(std::string message) {
    status_message = std::move(message);
    status_is_problem = true;
    status_generation.bump();
}

void AppState::dismiss_status() {
    status_message.clear();
    status_is_problem = false;
    status_generation.bump();
}

void AppState::commit_settings() {
    save_settings();
    apply_settings();
}

void AppState::edit_settings() {
    settings_unsaved_ = true;
    apply_settings();
}

void AppState::flush_settings() {
    if (settings_unsaved_) save_settings();
}

void AppState::save_settings() {
    settings_unsaved_ = false;
    if (!settings.save())
        set_problem("Settings could not be saved — " + app::ini_path() +
                   " is not writable.");
}

void AppState::apply_settings() {
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
        // user who never touched it. The page's rows and counts do not depend
        // on the cap or lens, so only the Best Path summaries are asked again.
        refresh_summaries();
        show_record_for_settings();
    }
    committed_chartmode_ = std::move(chartmode);
    committed_cap_ = cap;
    committed_lens_ = lens;
}

}  // namespace hydra::ui
