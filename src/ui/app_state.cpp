#include "ui/app_state.h"

#include <algorithm>

#include "app/config.h"
#include "app/edition.h"

namespace hydra::ui {

AppState::AppState() : settings(Settings::load(hydra::kUncapped)) {
    store = std::make_unique<store::RecordStore>(app::db_path(hydra::kUncapped),
                                                 hydra::kUncapped);
    refresh_page();
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
    std::string current_version = store::current_record_version(hydra::kUncapped);
    current_page.summaries.clear();
    current_page.summaries.reserve(current_page.rows.size());
    for (const store::ChartLibraryEntry& row : current_page.rows) {
        LibraryPage::RowSummary rs;
        if (auto summary = store->get_summary(row.md5, settings.chartmode_key())) {
            if (summary->first == current_version) {
                rs.state = LibraryPage::SummaryState::Current;
                rs.bestpath = summary->second;
            } else {
                rs.state = LibraryPage::SummaryState::Stale;
            }
        }
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
    selected = entry;
    show_details = true;
    refresh_viewed_record();
}

void AppState::refresh_viewed_record() {
    if (!selected) {
        viewed_record.reset();
        viewed_timing.reset();
        return;
    }
    viewed_record = store->get_record(selected->md5, settings.chartmode_key());
    viewed_timing = store->get_timing(selected->md5);
    ++record_generation;
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
    ++analyze_generation;
    analyze_job->start();
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
                                                  settings.chartmode_key(), settings.uncapped);
    // Remember the choice so the picker can pre-select it next time.
    settings.dm_last_user = discord_id;
    save_settings();
    dm_report_job->start();
}

void AppState::set_status(std::string message) {
    status_message = std::move(message);
    ++status_generation;
}

void AppState::save_settings() {
    if (!settings.save())
        set_status("Settings could not be saved — " + app::ini_path(settings.uncapped) +
                   " is not writable.");
}

}  // namespace hydra::ui
