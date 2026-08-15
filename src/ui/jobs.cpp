#include "ui/jobs.h"

#include <algorithm>
#include <thread>

namespace hydra::ui {

// ---- ScanJob --------------------------------------------------------------

ScanJob::ScanJob(std::vector<std::string> rootfolders, store::RecordStore& store)
    : rootfolders_(std::move(rootfolders)), store_(store) {}

ScanJob::~ScanJob() {
    if (thread_.joinable()) thread_.join();
}

void ScanJob::start() {
    thread_ = std::thread([this] { run(); });
}

ScanProgress ScanJob::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return progress_;
}

void ScanJob::run() {
    int total = app::get_folder_count(rootfolders_, [this](int n) {
        std::lock_guard<std::mutex> lock(mu_);
        progress_.folders_seen = n;
    });
    {
        std::lock_guard<std::mutex> lock(mu_);
        progress_.folders_total = total;
        progress_.phase = ScanProgress::Phase::Discovering;
    }

    auto [items, errors] = app::discover_charts(rootfolders_, [this](int n) {
        std::lock_guard<std::mutex> lock(mu_);
        progress_.folders_seen = n;
    });

    {
        std::lock_guard<std::mutex> lock(mu_);
        progress_.errors = errors;
        progress_.charts_found = static_cast<int>(items.size());
        progress_.phase = ScanProgress::Phase::Writing;
    }

    std::vector<store::ChartLibraryEntry> entries;
    entries.reserve(items.size());
    for (const app::ScanItem& item : items)
        entries.push_back({item.md5, item.title, item.artist, item.charter, item.notespath,
                           item.rootfolder});

    try {
        store_.rebuild_chart_library(entries);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mu_);
        progress_.errors.push_back(std::string("Failed to write chart library: ") + e.what());
    }

    std::lock_guard<std::mutex> lock(mu_);
    progress_.phase = ScanProgress::Phase::Done;
    progress_.finished = true;
}

// ---- BatchJob ---------------------------------------------------------

BatchJob::BatchJob(std::vector<app::ScanItem> items, std::string chartmode,
                   app::AnalysisSettings settings, store::RecordStore& store, bool redo)
    : items_(std::move(items)),
      chartmode_(std::move(chartmode)),
      settings_(std::move(settings)),
      store_(store),
      redo_(redo),
      workers_(app::batch_worker_count()) {}

BatchJob::~BatchJob() {
    if (thread_.joinable()) {
        cancel();
        thread_.join();
    }
}

void BatchJob::start() {
    thread_ = std::thread([this] { run(); });
}

void BatchJob::cancel() { cancel_.store(true); }

BatchJob::Snapshot BatchJob::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return snap_;
}

void BatchJob::run() {
    bool total_known = false;

    app::run_batch(
        items_, chartmode_, settings_, store_, redo_, workers_,
        [this, &total_known](const app::BatchProgress& p) {
            std::lock_guard<std::mutex> lock(mu_);
            snap_.total = p.total;
            snap_.completed = p.completed;
            snap_.current_title = p.current_title;
            if (!total_known) {
                snap_.skipped = static_cast<int>(items_.size()) - p.total;
                total_known = true;
            }
        },
        [this](const std::string& title, const std::string& error) {
            std::lock_guard<std::mutex> lock(mu_);
            ++snap_.failed;
            snap_.failures.push_back(title + ": " + error);
        },
        /*on_result=*/nullptr, &cancel_);

    std::lock_guard<std::mutex> lock(mu_);
    snap_.current_title.clear();
    snap_.finished = true;
}

// ---- AnalyzeJob -------------------------------------------------------

AnalyzeJob::AnalyzeJob(std::string filepath, app::AnalysisSettings settings)
    : filepath_(std::move(filepath)), settings_(std::move(settings)) {}

AnalyzeJob::~AnalyzeJob() {
    if (thread_.joinable()) thread_.join();
}

void AnalyzeJob::start() {
    thread_ = std::thread([this] {
        try {
            result_ = app::analyze_chart_file(filepath_, settings_, [this](float f) {
                progress_.store(f, std::memory_order_relaxed);
            });
            ok_ = true;
        } catch (const std::exception& e) {
            error_ = e.what();
            ok_ = false;
        }
        finished_.store(true);
    });
}

app::AnalysisResult AnalyzeJob::take_result() { return std::move(*result_); }

}  // namespace hydra::ui
