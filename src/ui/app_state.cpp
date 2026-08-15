#include "ui/app_state.h"

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
    current_page.rows =
        store->list_chart_library(search_opt, table_viewpage * rows_per_page, rows_per_page);
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
        return;
    }
    viewed_record = store->get_record(selected->md5, settings.chartmode_key());
    ++record_generation;
}

void AppState::start_scan() {
    if (scan_job && !scan_job->snapshot().finished) return;
    scan_job = std::make_unique<ScanJob>(settings.chartfolders, *store);
    scan_job->start();
}

void AppState::start_batch(bool redo) {
    if (batch_job && !batch_job->snapshot().finished) return;

    std::optional<std::string> search_opt = search.empty() ? std::nullopt : std::optional(search);
    int64_t count = store->chart_library_count(search_opt);
    std::vector<store::ChartLibraryEntry> entries =
        store->list_chart_library(search_opt, 0, static_cast<int>(count));

    std::vector<app::ScanItem> items;
    items.reserve(entries.size());
    for (const store::ChartLibraryEntry& e : entries)
        items.push_back({e.md5, e.title, e.artist, e.charter, e.notespath, e.rootfolder});

    batch_job = std::make_unique<BatchJob>(std::move(items), settings.chartmode_key(),
                                           settings.to_analysis_settings(), *store, redo);
    batch_job->start();
}

void AppState::start_analyze() {
    if (!selected) return;
    if (analyze_job && !analyze_job->finished()) return;
    analyze_job =
        std::make_unique<AnalyzeJob>(selected->notespath, settings.to_analysis_settings());
    analyze_job->start();
}

}  // namespace hydra::ui
