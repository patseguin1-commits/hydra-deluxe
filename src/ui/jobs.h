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
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app/analysis.h"
#include "net/dmbot_client.h"
#include "store/record_store.h"

namespace hydra::ui {

// ---- JobBase --------------------------------------------------------------

// Common lifecycle for every job: an owning worker thread, a cancel flag the
// worker polls, and a finished flag whose release-store publishes everything
// the worker wrote before it (the render thread's finished() load acquires).
// Derived destructors call shutdown() so the join happens while the derived
// members the worker touches are still alive.
class JobBase {
public:
    void cancel() { cancel_.store(true); }
    bool is_cancelled() const { return cancel_.load(); }
    bool finished() const { return finished_.load(); }

    JobBase(const JobBase&) = delete;
    JobBase& operator=(const JobBase&) = delete;

protected:
    JobBase() = default;
    ~JobBase() = default;  // jobs are held and destroyed by concrete type

    void spawn(std::function<void()> fn) { thread_ = std::thread(std::move(fn)); }
    void shutdown() {
        cancel_.store(true);
        if (thread_.joinable()) thread_.join();
    }

    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> finished_{false};
};

// Adds the ok/error result surface and the guarded-run tail shared by the
// jobs that produce one result instead of a mutex-guarded snapshot.
class ResultJobBase : public JobBase {
public:
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

protected:
    // Runs the job body; f returns whether the job succeeded. Any escaping
    // exception becomes the job's error text. Always publishes finished.
    template <class F>
    void run_guarded(F&& f) {
        try {
            ok_ = f();
        } catch (const std::exception& e) {
            error_ = e.what();
            ok_ = false;
        }
        finished_.store(true);
    }

    bool ok_ = false;
    std::string error_;
};

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
    BatchJob(std::optional<std::string> search, std::string chartmode,
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
    app::AnalysisSettings settings_;
    store::RecordStore& store_;
    bool redo_;
    int workers_;

    mutable std::mutex mu_;
    Snapshot snap_;
};

// ---- ReportJob --------------------------------------------------------

// Where the batch path report lives on disk (next to the db, edition-suffixed
// so the two editions never overwrite each other's page).
std::wstring report_html_path(bool uncapped);

// Opens the report page in the default browser. Returns false when the shell
// refuses (e.g. the file doesn't exist yet).
bool open_report_in_browser(bool uncapped);

// Whether a previously built report page exists on disk (gates the library
// view's "Open path report" button).
bool report_file_exists(bool uncapped);

// Builds the sortable HTML path report from everything in the store — the
// same page src/cli/report.cpp writes. Kicked off automatically when a
// library batch analysis finishes; opens the browser only when `open_when_done`
// (the user's "Open report automatically" setting). Collecting the rows
// inflates every stored record, which can take seconds on a big library, so
// it runs off the render thread like every other job.
class ReportJob : public ResultJobBase {
public:
    ReportJob(store::RecordStore& store, bool uncapped, bool open_when_done);
    ~ReportJob() { shutdown(); }

    void start();

private:
    void run();

    store::RecordStore& store_;
    bool uncapped_;
    bool open_when_done_;
};

// ---- AnalyzeJob -------------------------------------------------------

// cancel() interrupts the search at its next progress tick (the same unwind
// path the uncapped time budget uses); the result is discarded. The stretch
// before the first tick (parse + graph build) can't be interrupted.
class AnalyzeJob : public ResultJobBase {
public:
    // The job snapshots the song's identity and the chartmode at start so the
    // finished result is always stored against the song it was started for —
    // storing against "whatever is selected when the job finishes" wrote
    // records under the wrong song if the user closed the details modal
    // mid-analysis and clicked another row.
    AnalyzeJob(store::ChartLibraryEntry song, std::string chartmode,
               app::AnalysisSettings settings);
    ~AnalyzeJob() { shutdown(); }

    void start();

    const store::ChartLibraryEntry& song() const { return song_; }
    const std::string& chartmode() const { return chartmode_; }

    // Monotonic 0..1 search progress, or a negative value before the first
    // report (i.e. show an indeterminate spinner until then).
    float progress() const { return progress_.load(std::memory_order_relaxed); }

    // Valid once finished() && ok(); moves the result out (call once).
    app::AnalysisResult take_result();

private:
    store::ChartLibraryEntry song_;
    std::string chartmode_;
    app::AnalysisSettings settings_;
    std::atomic<float> progress_{-1.0f};
    std::optional<app::AnalysisResult> result_;
};

// ---- DmFetchUsersJob --------------------------------------------------

// Fetches the dmleaderboards ladder (GET /api/all-users) for the searchable
// picker. Off the render thread because the render.com backend cold-starts —
// the first request after an idle spell can take tens of seconds.
class DmFetchUsersJob : public ResultJobBase {
public:
    DmFetchUsersJob() = default;
    ~DmFetchUsersJob() { shutdown(); }

    void start();

    // Valid once finished() && ok(); the picker takes ownership (call once).
    std::vector<net::DmUser>& users() { return users_; }

private:
    void run();
    std::vector<net::DmUser> users_;
};

// ---- DmReportJob ------------------------------------------------------

// Where the comparison page lives on disk (next to the db, edition-suffixed).
std::wstring dm_report_html_path(bool uncapped);
bool open_dm_report_in_browser(bool uncapped);

// Fetches one user's scores (GET /api/user/{id}/scores), joins them against the
// store by chart hash, writes the HTML comparison report and opens it. Same
// off-thread + cold-start handling as DmFetchUsersJob.
class DmReportJob : public ResultJobBase {
public:
    DmReportJob(store::RecordStore& store, std::string discord_id, std::string username,
                std::string chartmode, bool uncapped);
    ~DmReportJob() { shutdown(); }

    void start();

    // Headline join counts for the finished modal; valid once ok().
    int total() const { return total_; }
    int matched() const { return matched_; }
    int above() const { return above_; }
    int unmatched() const { return unmatched_; }

private:
    void run();
    store::RecordStore& store_;
    std::string discord_id_;
    std::string username_;
    std::string chartmode_;
    bool uncapped_;
    int total_ = 0, matched_ = 0, above_ = 0, unmatched_ = 0;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_JOBS_H
