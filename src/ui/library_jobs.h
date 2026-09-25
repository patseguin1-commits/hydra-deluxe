// The library tab's four background jobs: scanning chart folders (ScanJob),
// batch-analyzing many charts (BatchJob), analyzing one chart (AnalyzeJob),
// and building the HTML path report afterwards (ReportJob). AppState owns one
// of each at most; library_view.cpp and details_view.cpp poll them per frame.
//
// Every job is polled once per frame from the render thread via snapshot()/
// finished(); the worker thread never touches ImGui state directly.

#ifndef HYDRA_UI_LIBRARY_JOBS_H
#define HYDRA_UI_LIBRARY_JOBS_H

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "core/model.h"
#include "store/record_store.h"
#include "ui/job_base.h"

namespace hydra::ui {

// ---- ScanJob --------------------------------------------------------------

struct ScanProgress {
    // Enumerating: walking folders (count grows, no denominator yet).
    // Reading: hashing charts / reading metadata, charts_done/charts_total.
    // Writing: replacing the library table. Done: finished (or cancelled).
    enum class Phase { Enumerating, Reading, Writing, Done };
    Phase phase = Phase::Enumerating;
    int folders_seen = 0;
    int charts_total = 0;
    int charts_done = 0;
    int charts_cached = 0;  // satisfied by the rescan cache, no file reads
    int charts_found = 0;   // final scan item count
    std::vector<std::string> errors;
    bool cancelled = false;
    bool finished = false;
};

// A cancelled scan writes nothing: the previous library stays as-is.
class ScanJob : public JobBase {
public:
    ScanJob(std::vector<std::string> rootfolders, store::RecordStore& store);
    ~ScanJob() { shutdown(); }

    void start();
    ScanProgress snapshot() const;

private:
    void run();

    std::vector<std::string> rootfolders_;
    store::RecordStore& store_;

    mutable std::mutex mu_;
    ScanProgress progress_;
};

// ---- BatchJob ---------------------------------------------------------

class BatchJob : public JobBase {
public:
    // The job queries the library itself (filtered by `search`, like the
    // table) on its own thread — loading thousands of rows synchronously
    // before the progress modal appeared froze the UI for the whole query.
    BatchJob(std::optional<std::string> search, std::string chartmode, store::Lens lens,
             app::AnalysisSettings settings, store::RecordStore& store, bool redo);
    ~BatchJob() { shutdown(); }

    void start();

    struct Snapshot {
        bool preparing = true;  // still loading the item list from the store
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

    std::optional<std::string> search_;
    std::vector<app::ScanItem> items_;
    std::string chartmode_;
    store::Lens lens_;
    app::AnalysisSettings settings_;
    store::RecordStore& store_;
    bool redo_;
    int workers_;

    mutable std::mutex mu_;
    Snapshot snap_;
};

// ---- AnalyzeJob -------------------------------------------------------

// cancel() interrupts the search at its next progress tick (the same unwind
// path the Auto cap time budget uses); the result is discarded. The stretch
// before the first tick (parse + graph build) can't be interrupted.
class AnalyzeJob : public ResultJobBase {
public:
    // The job snapshots the song and the record's RecordKey at start so the
    // finished result is always stored against the song it was started for —
    // storing against "whatever is selected when the job finishes" wrote
    // records under the wrong song if the user closed the details modal
    // mid-analysis and clicked another row.
    AnalyzeJob(store::ChartLibraryEntry song, store::RecordKey key,
               app::AnalysisSettings settings);
    ~AnalyzeJob() { shutdown(); }

    void start();

    const store::ChartLibraryEntry& song() const { return song_; }
    const store::RecordKey& key() const { return key_; }
    const app::AnalysisSettings& settings() const { return settings_; }

    // Monotonic 0..1 search progress, or a negative value before the first
    // report (i.e. show an indeterminate spinner until then).
    float progress() const { return progress_.load(std::memory_order_relaxed); }

    // Valid once finished() && ok(); moves the result out (call once).
    app::AnalysisResult take_result();

private:
    store::ChartLibraryEntry song_;
    store::RecordKey key_;
    app::AnalysisSettings settings_;
    std::atomic<float> progress_{-1.0f};
    std::optional<app::AnalysisResult> result_;
};

// ---- ReportJob --------------------------------------------------------

// Builds the sortable HTML path report from everything in the store — the
// same page src/cli/report.cpp writes. Kicked off automatically when a
// library batch analysis finishes; opens the browser only when `open_when_done`
// (the user's "Open report automatically" setting). Collecting the rows
// inflates every stored record, which can take seconds on a big library, so
// it runs off the render thread like every other job. Where the page lands and
// how it reaches the browser live in app/report_files.h.
class ReportJob : public ResultJobBase {
public:
    // hit_window_ms feeds the page's timing-tier bands (settings.hit_window_ms).
    // cap/lens: which records the page lists (the user's current SP cap, ms
    // limit and score range).
    ReportJob(store::RecordStore& store, store::CapQuery cap, store::Lens lens,
              bool open_when_done,
              int hit_window_ms = static_cast<int>(kDefaultHitWindowMs));
    ~ReportJob() { shutdown(); }

    void start();

private:
    void run();

    store::RecordStore& store_;
    store::CapQuery cap_;
    store::Lens lens_;
    bool open_when_done_;
    int hit_window_ms_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_LIBRARY_JOBS_H
