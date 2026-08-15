// Background-thread job wrappers for the three long-running operations the UI
// kicks off: scanning chart folders, batch-analyzing many charts, and
// analyzing one chart. Mirrors hydra_app.py's scan_library()/BatchJob/
// on_run_chart, each moved off the render thread the same way DearPyGui's
// version moved them off its own — a chart can take from milliseconds to
// minutes, and a frame that blocks that long reads as "Not Responding".
//
// Every job is polled once per frame from the render thread via snapshot()/
// finished(); the worker thread never touches ImGui state directly.

#ifndef HYDRA_UI_JOBS_H
#define HYDRA_UI_JOBS_H

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app/analysis.h"
#include "store/record_store.h"

namespace hydra::ui {

// ---- ScanJob --------------------------------------------------------------

struct ScanProgress {
    enum class Phase { CountingFolders, Discovering, Writing, Done };
    Phase phase = Phase::CountingFolders;
    int folders_total = 0;
    int folders_seen = 0;
    int charts_found = 0;
    std::vector<std::string> errors;
    bool finished = false;
};

class ScanJob {
public:
    ScanJob(std::vector<std::string> rootfolders, store::RecordStore& store);
    ~ScanJob();

    void start();
    ScanProgress snapshot() const;

private:
    void run();

    std::vector<std::string> rootfolders_;
    store::RecordStore& store_;
    std::thread thread_;

    mutable std::mutex mu_;
    ScanProgress progress_;
};

// ---- BatchJob ---------------------------------------------------------

class BatchJob {
public:
    BatchJob(std::vector<app::ScanItem> items, std::string chartmode,
             app::AnalysisSettings settings, store::RecordStore& store, bool redo);
    ~BatchJob();

    void start();
    void cancel();
    bool is_cancelled() const { return cancel_.load(); }

    struct Snapshot {
        int total = 0;      // items actually dispatched (excludes pre-skipped)
        int completed = 0;
        int skipped = 0;    // already stored, known up front (not part of total)
        int failed = 0;
        std::string current_title;
        std::vector<std::string> failures;
        bool finished = false;
    };
    Snapshot snapshot() const;

private:
    void run();

    std::vector<app::ScanItem> items_;
    std::string chartmode_;
    app::AnalysisSettings settings_;
    store::RecordStore& store_;
    bool redo_;
    int workers_;

    std::thread thread_;
    std::atomic<bool> cancel_{false};

    mutable std::mutex mu_;
    Snapshot snap_;
};

// ---- AnalyzeJob -------------------------------------------------------

class AnalyzeJob {
public:
    AnalyzeJob(std::string filepath, app::AnalysisSettings settings);
    ~AnalyzeJob();

    void start();
    bool finished() const { return finished_.load(); }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

    // Monotonic 0..1 search progress, or a negative value before the first
    // report (i.e. show an indeterminate spinner until then).
    float progress() const { return progress_.load(std::memory_order_relaxed); }

    // Valid once finished() && ok(); moves the result out (call once).
    app::AnalysisResult take_result();

private:
    std::string filepath_;
    app::AnalysisSettings settings_;
    std::thread thread_;
    std::atomic<bool> finished_{false};
    std::atomic<float> progress_{-1.0f};
    bool ok_ = false;
    std::string error_;
    std::optional<app::AnalysisResult> result_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_JOBS_H
