#include "ui/app_state.h"

#include <algorithm>

#include "app/config.h"
#include "ui/preview_controller.h"

namespace hydra::ui {

AppState::AppState() : settings(Settings::load()) {
    store = app::open_store(app::db_path());
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
    const store::CapQuery cap = settings.cap_query();
    current_page.summaries.clear();
    current_page.summaries.reserve(current_page.rows.size());
    for (const store::ChartLibraryEntry& row : current_page.rows) {
        store::SummaryLookup summary =
            store->get_summary(row.md5, settings.chartmode_key(), cap);
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
    selected = entry;
    show_details = true;
    refresh_viewed_record();
}

void AppState::refresh_viewed_record() {
    if (!selected) {
        viewed = store::RecordLookup{};
        return;
    }
    viewed =
        store->get_record(selected->md5, settings.chartmode_key(), settings.cap_query());
    record_generation.bump();
}

void AppState::start_scan() {
    if (scan_job && !scan_job->snapshot().finished) return;
    scan_job = std::make_unique<ScanJob>(settings.chartfolders, *store);
    scan_job->start();
}

void AppState::start_batch(bool redo) {
    if (batch_job && !batch_job->snapshot().finished) return;

    // The job loads the (possibly search-filtered) item list on its own
    // thread; doing the unbounded SELECT here froze a frame on big libraries.
    std::optional<std::string> search_opt = search.empty() ? std::nullopt : std::optional(search);
    batch_job = std::make_unique<BatchJob>(search_opt, settings.chartmode_key(),
                                           settings.to_analysis_settings(), *store, redo);
    report_started = false;
    batch_job->start();
}

void AppState::start_analyze() {
    if (!selected) return;
    if (analyze_job && !analyze_job->finished()) return;
    analyze_job = std::make_unique<AnalyzeJob>(*selected, settings.chartmode_key(),
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
        store->add_record(song.md5, analyze_job->chartmode(), result.record);
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
                                                  settings.auto_open_report);
    // Remember the choice so the picker can pre-select it next time.
    settings.dm_last_user = discord_id;
    save_settings();
    dm_report_job->start();
}

void AppState::set_status(std::string message) {
    status_message = std::move(message);
    status_generation.bump();
}

void AppState::save_settings() {
    if (!settings.save())
        set_status("Settings could not be saved — " + app::ini_path() +
                   " is not writable.");
}

}  // namespace hydra::ui
