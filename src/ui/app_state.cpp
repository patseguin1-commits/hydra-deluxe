#include "ui/app_state.h"

#include <algorithm>

#include "app/config.h"
#include "app/rules_file.h"
#include "app/report_files.h"
#include "app/user_messages.h"
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

// Over the page the table shows today. T12 re-implements these three over its
// whole-library model (library_view_order()); every caller stays the same.
size_t AppState::view_row_count() const { return current_page.rows.size(); }

const store::ChartLibraryEntry& AppState::view_row(size_t i) const {
    return current_page.rows[i];
}

store::RecordStatus AppState::view_row_status(size_t i) const {
    return i < current_page.summaries.size() ? current_page.summaries[i].state
                                             : store::RecordStatus::NotAnalyzed;
}

std::optional<size_t> AppState::relative_row(int delta) const {
    if (!selected || delta == 0) return std::nullopt;
    const size_t n = view_row_count();
    for (size_t i = 0; i < n; ++i) {
        // notespath, not md5: the same chart can sit in two folders.
        if (view_row(i).notespath != selected->notespath) continue;
        const long long j = static_cast<long long>(i) + delta;
        if (j < 0 || j >= static_cast<long long>(n)) return std::nullopt;
        return static_cast<size_t>(j);
    }
    return std::nullopt;
}

bool AppState::can_select_relative(int delta) const { return relative_row(delta).has_value(); }

void AppState::select_relative(int delta) {
    if (std::optional<size_t> i = relative_row(delta)) select(view_row(*i));
}

void AppState::select(const store::ChartLibraryEntry& entry) {
    // The previous chart's panel, torn down the one way: a row click or
    // previous / next swaps the song while the panel stays open.
    close_details();
    parked_lookups_.clear();  // a new chart: nothing parked applies
    selected = entry;
    show_details = true;
    refresh_viewed_record();
}

void AppState::close_details() {
    show_details = false;
    // A running analysis keeps going: tick() stores it when it finishes,
    // whichever song is showing by then. (Closing used to cancel it.)
    // The audio device must stop, and the GPU and decode work must not keep
    // running behind a hidden panel.
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
        viewed_summary = store::PathSummary{};
        viewed = store::RecordLookup{};
        viewed_key_.reset();
        return;
    }
    store::RecordKey key = settings.record_key(selected->md5);
    viewed = store->get_record(key);
    viewed_key_ = std::move(key);
    record_generation.bump();
    refresh_viewed_summary();
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
        refresh_viewed_summary();
    } else {
        refresh_viewed_record();
    }
}

void AppState::refresh_viewed_summary() {
    viewed_summary = store::PathSummary{};
    if (!selected || viewed.status != store::RecordStatus::Ready) return;
    // One chart, one query, only when the record changes -- never per frame.
    std::vector<store::SummaryLookup> found = store->get_summaries(
        {selected->md5}, settings.chartmode_key(), settings.cap_query(), settings.lens());
    if (!found.empty()) viewed_summary = std::move(found.front().summary);
}

bool AppState::analyze_running() const { return analyze_job && !analyze_job->finished(); }

bool AppState::batch_running() const { return batch_job && !batch_job->snapshot().finished; }

bool AppState::analyze_job_shown() const {
    return analyze_job && show_details && selected &&
           analyze_job->song().notespath == selected->notespath;
}

void AppState::tick(double now) {
    if (!show_details && details_ui.prev_open) close_details();
    details_ui.prev_open = show_details;
    update_analyze_job(now);
    reap_dynamics();
}

void AppState::update_analyze_job(double now) {
    // Keyed on the job generation, not the job's address: a freed AnalyzeJob's
    // block can be handed straight back to the next make_unique, and a pointer
    // compare then carries `stored`/`done_at` over from the previous job.
    DetailsViewState& d = details_ui;
    if (d.analyze_watcher.changed(analyze_generation)) {
        d.done_at = -1.0;
        d.stored = false;
        d.store_error.clear();
    }
    AnalyzeJob* job = analyze_job.get();
    if (!job || !job->finished()) return;
    if (job->is_cancelled()) {  // the panel's Cancel: nothing to store
        analyze_job.reset();
        return;
    }
    // A result or error for a song the panel isn't showing goes to the status
    // line; one for the shown song stays in the panel until Continue.
    const bool shown = analyze_job_shown();
    const std::string title = job->song().title;
    if (!job->ok()) {
        if (!shown) {
            // message() is T4's plain sentence; the raw error() stays in the
            // panel's detail line for a song that is showing.
            set_status("Could not analyze " + title + ". " + job->message());
            analyze_job.reset();
        }
        return;
    }
    if (!d.stored) {
        d.stored = true;
        d.store_error = store_finished_analysis();
        if (d.store_error.empty()) d.done_at = now;
    }
    if (!d.store_error.empty()) {
        if (!shown) {
            set_status(d.store_error);
            analyze_job.reset();
        }
        return;
    }
    // The panel flashes "Done!" for half a second; nobody sees it otherwise.
    if (!shown || now - d.done_at > 0.5) analyze_job.reset();
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
        return "Analyzed, but saving failed. " + app::plain_error(e);
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
        set_status("Settings could not be saved — " + app::ini_path() +
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
