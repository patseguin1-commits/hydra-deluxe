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
#include "net/dmbot_client.h"
#include "store/record_store.h"

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

class ScanJob {
public:
    ScanJob(std::vector<std::string> rootfolders, store::RecordStore& store);
    ~ScanJob();

    void start();
    // A cancelled scan writes nothing: the previous library stays as-is.
    void cancel();
    ScanProgress snapshot() const;

private:
    void run();

    std::vector<std::string> rootfolders_;
    store::RecordStore& store_;
    std::thread thread_;
    std::atomic<bool> cancel_{false};

    mutable std::mutex mu_;
    ScanProgress progress_;
};

// ---- BatchJob ---------------------------------------------------------

class BatchJob {
public:
    // The job queries the library itself (filtered by `search`, like the
    // table) on its own thread — loading thousands of rows synchronously
    // before the progress modal appeared froze the UI for the whole query.
    BatchJob(std::optional<std::string> search, std::string chartmode,
             app::AnalysisSettings settings, store::RecordStore& store, bool redo);
    ~BatchJob();

    void start();
    void cancel();
    bool is_cancelled() const { return cancel_.load(); }

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

    std::thread thread_;
    std::atomic<bool> cancel_{false};

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
class ReportJob {
public:
    ReportJob(store::RecordStore& store, bool uncapped, bool open_when_done);
    ~ReportJob();

    void start();
    bool finished() const { return finished_.load(); }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

private:
    void run();

    store::RecordStore& store_;
    bool uncapped_;
    bool open_when_done_;
    std::thread thread_;
    std::atomic<bool> finished_{false};
    bool ok_ = false;
    std::string error_;
};

// ---- AnalyzeJob -------------------------------------------------------

class AnalyzeJob {
public:
    // The job snapshots the song's identity and the chartmode at start so the
    // finished result is always stored against the song it was started for —
    // storing against "whatever is selected when the job finishes" wrote
    // records under the wrong song if the user closed the details modal
    // mid-analysis and clicked another row.
    AnalyzeJob(store::ChartLibraryEntry song, std::string chartmode,
               app::AnalysisSettings settings);
    ~AnalyzeJob();

    void start();
    // Interrupts the search at its next progress tick (the same unwind path
    // the uncapped time budget uses); the result is discarded. The stretch
    // before the first tick (parse + graph build) can't be interrupted.
    void cancel();
    bool is_cancelled() const { return cancel_.load(); }
    bool finished() const { return finished_.load(); }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

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
    std::thread thread_;
    std::atomic<bool> finished_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<float> progress_{-1.0f};
    bool ok_ = false;
    std::string error_;
    std::optional<app::AnalysisResult> result_;
};

// ---- DmFetchUsersJob --------------------------------------------------

// Fetches the dmleaderboards ladder (GET /api/all-users) for the searchable
// picker. Off the render thread because the render.com backend cold-starts —
// the first request after an idle spell can take tens of seconds.
class DmFetchUsersJob {
public:
    DmFetchUsersJob();
    ~DmFetchUsersJob();

    void start();
    void cancel() { cancel_.store(true); }
    bool finished() const { return finished_.load(); }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

    // Valid once finished() && ok(); the picker takes ownership (call once).
    std::vector<net::DmUser>& users() { return users_; }

private:
    void run();
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> finished_{false};
    bool ok_ = false;
    std::string error_;
    std::vector<net::DmUser> users_;
};

// ---- DmReportJob ------------------------------------------------------

// Where the comparison page lives on disk (next to the db, edition-suffixed).
std::wstring dm_report_html_path(bool uncapped);
bool open_dm_report_in_browser(bool uncapped);

// Fetches one user's scores (GET /api/user/{id}/scores), joins them against the
// store by chart hash, writes the HTML comparison report and opens it. Same
// off-thread + cold-start handling as DmFetchUsersJob.
class DmReportJob {
public:
    DmReportJob(store::RecordStore& store, std::string discord_id, std::string username,
                std::string chartmode, bool uncapped);
    ~DmReportJob();

    void start();
    void cancel() { cancel_.store(true); }
    bool finished() const { return finished_.load(); }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

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
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> finished_{false};
    bool ok_ = false;
    std::string error_;
    int total_ = 0, matched_ = 0, above_ = 0, unmatched_ = 0;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_JOBS_H
