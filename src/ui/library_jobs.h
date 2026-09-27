// The library tab's four background jobs: scanning chart folders (ScanJob),
// batch-analyzing many charts (BatchJob), analyzing one chart (AnalyzeJob),
// and building the HTML path report afterwards (ReportJob). AppState owns one
// of each at most; the library files (library_toolbar.cpp, library_dialogs.cpp)
// and details_panel.cpp poll them per frame.
//
// Every job is polled once per frame from the render thread via snapshot()/
// finished(); the worker thread never touches ImGui state directly.

#ifndef HYDRA_UI_LIBRARY_JOBS_H
#define HYDRA_UI_LIBRARY_JOBS_H

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/analysis.h"
#include "core/model.h"
#include "store/record_store.h"
#include "ui/job_base.h"
#include "ui/report_outcome.h"

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

// How long a batch has been working, with paused stretches left out. Times
// are seconds on any steady clock: BatchJob feeds it steady_clock readings,
// tests feed it plain numbers.
class BatchClock {
public:
    void start(double now);
    void pause(double now);   // does nothing unless running
    void resume(double now);  // does nothing unless paused
    void finish(double now);  // freezes the total; a paused clock stops at its pause
    double elapsed_s(double now) const;
    bool paused() const { return paused_at_.has_value(); }

private:
    std::optional<double> started_, paused_at_, finished_at_;
    double paused_total_ = 0.0;
};

// How many charts must finish before a time-left estimate shows.
inline constexpr int kEtaMinFinished = 3;

// Time left: the average wall time per finished chart so far, times the
// charts still to go. Empty until kEtaMinFinished charts have finished.
std::optional<double> batch_eta_s(double elapsed_s, int completed, int total);

class BatchJob : public JobBase {
public:
    // The job queries the library itself (filtered by `search`, like the
    // table) on its own thread — loading thousands of rows synchronously
    // before the progress modal appeared froze the UI for the whole query.
    BatchJob(std::optional<std::string> search, app::BatchRun run,
             store::RecordStore& store, bool redo);
    // Analyzes exactly these charts, in this order. The library screen's own
    // search (T12) decides which rows match, so "Analyze search (N)..." hands
    // over the N rows it shows instead of a search string SQL would read
    // differently.
    BatchJob(std::vector<store::ChartLibraryEntry> charts, app::BatchRun run,
             store::RecordStore& store, bool redo);
    ~BatchJob() {
        stop();
        shutdown();
    }

    // Test seam: what analyzes one chart, and how many workers run. Call
    // before start().
    void set_analyzer_for_test(app::ChartAnalyzer analyze, int workers);

    void start();

    // Pause stops handing out new charts. Charts already being analyzed
    // finish and are stored. Resume carries on. Both may be called before
    // start(); neither does anything once the run has finished.
    void pause();
    void resume();
    // Ends the run. Results already stored stay stored. A chart in the middle
    // of its analysis stops at its next progress tick and is neither stored
    // nor counted as failed. Works while paused.
    void stop();
    // The old name for stop(), kept until T13 moves the batch modal's
    // callers to stop().
    void cancel() { stop(); }

    struct Snapshot {
        bool preparing = true;  // still loading the item list from the store
        int total = 0;      // items actually dispatched (excludes pre-skipped)
        int completed = 0;  // finished charts, stored or failed
        int skipped = 0;    // already stored, known up front (not part of total)
        int failed = 0;
        bool paused = false;
        // Wall time since start(), paused time left out; frozen once finished.
        double elapsed_s = 0.0;
        // Seconds left. Empty until kEtaMinFinished charts finish, while
        // paused, and once finished.
        std::optional<double> eta_s;
        // The chart a worker started most recently. Empty once finished.
        std::string current_title;
        std::string current_artist;
        std::vector<std::string> failures;         // "Title: plain message"
        std::vector<std::string> failure_details;  // "Title: raw error", same order
        bool finished = false;
    };
    Snapshot snapshot() const;

private:
    void run();
    // On a worker, before each chart: waits while paused. Throws
    // app::AnalysisCancelled once stopped, so the chart counts as stopped.
    void wait_while_paused();
    // On a worker: records the chart it is starting as the current one.
    void note_started(const std::string& notespath);

    std::optional<std::string> search_;
    std::optional<std::vector<store::ChartLibraryEntry>> given_;
    std::vector<app::ScanItem> items_;
    // notespath -> index into items_. Built before run_batch starts and only
    // read after that, so workers read it without a lock.
    std::unordered_map<std::string, size_t> by_path_;
    app::BatchRun run_;
    store::RecordStore& store_;
    bool redo_;
    int workers_;
    app::ChartAnalyzer analyze_;  // empty = app::analyze_chart_file

    std::mutex pause_mu_;
    std::condition_variable pause_cv_;
    bool paused_ = false;  // guarded by pause_mu_

    mutable std::mutex mu_;
    Snapshot snap_;     // guarded by mu_
    BatchClock clock_;  // guarded by mu_
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

    // Valid once finished() && ok(): where the page was written, whether the
    // browser opened it, and why not when it was asked to and didn't.
    const std::filesystem::path& saved_path() const { return outcome_.saved_path; }
    bool opened() const { return outcome_.opened; }
    const std::string& open_problem() const { return outcome_.open_problem; }

private:
    void run();

    store::RecordStore& store_;
    store::CapQuery cap_;
    store::Lens lens_;
    bool open_when_done_;
    int hit_window_ms_;
    ReportOutcome outcome_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_LIBRARY_JOBS_H
