#include "ui/jobs.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <thread>

#include "app/config.h"
#include "app/dm_report.h"
#include "app/report.h"
#include "core/model.h"
#include "net/dmbot_client.h"

namespace hydra::ui {

// ---- ScanJob --------------------------------------------------------------

ScanJob::ScanJob(std::vector<std::string> rootfolders, store::RecordStore& store)
    : rootfolders_(std::move(rootfolders)), store_(store) {}

void ScanJob::start() { spawn([this] { run(); }); }

ScanProgress ScanJob::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return progress_;
}

void ScanJob::run() {
    // The previous scan's rows: any chart whose files are unchanged
    // (size+mtime) reuses its md5/metadata without being read again.
    store::ChartLibraryCache cache;
    try {
        cache = store_.chart_library_cache();
    } catch (const std::exception&) {
        // No cache is only a slow scan, not a failed one.
    }

    app::ScanCallbacks callbacks;
    callbacks.on_folders = [this](int n) {
        std::lock_guard<std::mutex> lock(mu_);
        progress_.folders_seen = n;
    };
    callbacks.on_charts = [this](int done, int total, int cached) {
        std::lock_guard<std::mutex> lock(mu_);
        progress_.phase = ScanProgress::Phase::Reading;
        progress_.charts_done = done;
        progress_.charts_total = total;
        progress_.charts_cached = cached;
    };
    callbacks.cancel = &cancel_;

    auto [items, errors] =
        app::discover_charts(rootfolders_, callbacks, cache.empty() ? nullptr : &cache);

    {
        std::lock_guard<std::mutex> lock(mu_);
        progress_.errors = errors;
        progress_.charts_found = static_cast<int>(items.size());
    }

    if (cancel_.load()) {
        // Leave the existing library untouched.
        std::lock_guard<std::mutex> lock(mu_);
        progress_.cancelled = true;
        progress_.phase = ScanProgress::Phase::Done;
        progress_.finished = true;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        progress_.phase = ScanProgress::Phase::Writing;
    }

    std::vector<store::ChartLibraryEntry> entries;
    entries.reserve(items.size());
    for (const app::ScanItem& item : items)
        entries.push_back({item.md5, item.title, item.artist, item.charter, item.notespath,
                           item.rootfolder, item.sig});

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

BatchJob::BatchJob(std::optional<std::string> search, std::string chartmode,
                   app::AnalysisSettings settings, store::RecordStore& store, bool redo)
    : search_(std::move(search)),
      chartmode_(std::move(chartmode)),
      settings_(std::move(settings)),
      store_(store),
      redo_(redo),
      workers_(app::batch_worker_count()) {}

void BatchJob::start() { spawn([this] { run(); }); }

BatchJob::Snapshot BatchJob::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return snap_;
}

void BatchJob::run() {
    // Load the item list here rather than on the UI thread: an unbounded
    // SELECT over a big library takes long enough to freeze a frame.
    try {
        std::vector<store::ChartLibraryEntry> entries =
            store_.list_chart_library(search_, 0, -1);  // LIMIT -1 = no limit
        items_.reserve(entries.size());
        for (const store::ChartLibraryEntry& e : entries)
            items_.push_back({e.md5, e.title, e.artist, e.charter, e.notespath, e.rootfolder});
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.preparing = false;
        snap_.failures.push_back(std::string("Could not load the library: ") + e.what());
        ++snap_.failed;
        snap_.finished = true;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.preparing = false;
    }
    if (cancel_.load()) {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.finished = true;
        return;
    }

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

// ---- ReportJob --------------------------------------------------------

namespace {

// Report pages live next to the db, edition-suffixed so the two editions
// never overwrite each other's page.
std::wstring html_artifact_path(bool uncapped, const wchar_t* name,
                                const wchar_t* name_uncapped) {
    std::filesystem::path dbp = std::filesystem::u8path(app::db_path(uncapped));
    return (dbp.parent_path() / (uncapped ? name_uncapped : name)).wstring();
}

bool open_in_browser(const std::wstring& path) {
    HINSTANCE rc = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
                                 nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;
}

// The shared report tail: write the page and open it in the browser.
void write_and_open(const std::filesystem::path& outpath, const std::string& html,
                    bool open_when_done) {
    std::ofstream f(outpath, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write " + outpath.u8string());
    f << html;
    f.close();
    if (open_when_done && !open_in_browser(outpath.wstring()))
        throw std::runtime_error("could not open " + outpath.u8string());
}

}  // namespace

std::wstring report_html_path(bool uncapped) {
    return html_artifact_path(uncapped, L"hydra_paths.html",
                              L"hydra_paths_uncapped.html");
}

bool open_report_in_browser(bool uncapped) {
    return open_in_browser(report_html_path(uncapped));
}

bool report_file_exists(bool uncapped) {
    return GetFileAttributesW(report_html_path(uncapped).c_str()) != INVALID_FILE_ATTRIBUTES;
}

ReportJob::ReportJob(store::RecordStore& store, bool uncapped, bool open_when_done)
    : store_(store), uncapped_(uncapped), open_when_done_(open_when_done) {}

void ReportJob::start() { spawn([this] { run(); }); }

void ReportJob::run() {
    run_guarded([this] {
        auto [songs, records] = store_.counts();
        std::vector<app::report::ReportRow> rows =
            app::report::collect_rows(store_, 5, uncapped_);
        if (rows.empty()) throw std::runtime_error("no records stored yet");

        // Same page framing as the hydra_report CLI's defaults.
        std::string subtitle = group_thousands(records) + " records across " +
                               group_thousands(songs) +
                               " songs — top 5 paths per chart";
        std::filesystem::path dbp = std::filesystem::u8path(app::db_path(uncapped_));
        std::string footer = "Generated from " + dbp.filename().u8string() +
                             ". Timing tiers match Hydra's squeeze ratings; "
                             "'Beyond' is past the stock 140 ms window.";

        write_and_open(report_html_path(uncapped_),
                       app::report::build_html(rows, subtitle, footer),
                       open_when_done_);
        return true;
    });
}

// ---- DmFetchUsersJob --------------------------------------------------

void DmFetchUsersJob::start() { spawn([this] { run(); }); }

void DmFetchUsersJob::run() {
    run_guarded([this] {
        users_ = net::fetch_users(net::kDefaultApiBase, &cancel_);
        return true;
    });
}

// ---- DmReportJob ------------------------------------------------------

std::wstring dm_report_html_path(bool uncapped) {
    return html_artifact_path(uncapped, L"hydra_dmcompare.html",
                              L"hydra_dmcompare_uncapped.html");
}

bool open_dm_report_in_browser(bool uncapped) {
    return open_in_browser(dm_report_html_path(uncapped));
}

DmReportJob::DmReportJob(store::RecordStore& store, std::string discord_id, std::string username,
                         std::string chartmode, bool uncapped)
    : store_(store),
      discord_id_(std::move(discord_id)),
      username_(std::move(username)),
      chartmode_(std::move(chartmode)),
      uncapped_(uncapped) {}

void DmReportJob::start() { spawn([this] { run(); }); }

void DmReportJob::run() {
    run_guarded([this] {
        std::vector<net::DmScore> scores =
            net::fetch_scores(discord_id_, net::kDefaultApiBase, &cancel_);
        std::vector<app::dm_report::DmReportRow> rows =
            app::dm_report::collect_dm_rows(store_, scores, chartmode_);
        if (rows.empty())
            throw std::runtime_error("this user has no scores to compare");

        total_ = static_cast<int>(rows.size());
        for (const app::dm_report::DmReportRow& r : rows) {
            if (r.status == "matched") ++matched_;
            else if (r.status == "above optimal") ++above_;
            else ++unmatched_;
        }

        std::string subtitle = username_ + " — " + group_thousands(total_) + " scores: " +
                               group_thousands(matched_) + " matched, " +
                               group_thousands(above_) + " above optimal, " +
                               group_thousands(unmatched_) + " not in your library";
        std::string footer =
            "Actual scores from dmleaderboards.com against Hydra's optimal for " + chartmode_ +
            ". Above-optimal scores are expected — Hydra's optimal excludes several score "
            "backends, and older Clone Hero versions allowed fills that are impossible now.";

        write_and_open(dm_report_html_path(uncapped_),
                       app::dm_report::build_dm_html(rows, subtitle, footer),
                       /*open_when_done=*/true);
        return true;
    });
}

// ---- AnalyzeJob -------------------------------------------------------

namespace {
// Thrown out of the search's progress callback to abort a cancelled analysis
// — the same unwind path pather.cpp's CapBudgetExceeded takes, so the engine
// is already known to survive it.
struct AnalysisCancelled {};
}  // namespace

AnalyzeJob::AnalyzeJob(store::ChartLibraryEntry song, std::string chartmode,
                       app::AnalysisSettings settings)
    : song_(std::move(song)),
      chartmode_(std::move(chartmode)),
      settings_(std::move(settings)) {}

void AnalyzeJob::start() {
    // The thread constructor itself can throw (std::system_error when the OS
    // refuses the thread); route that through the modal's error path instead
    // of letting it escape start_analyze and terminate the app.
    try {
        spawn([this] {
            run_guarded([this] {
                try {
                    result_ = app::analyze_chart_file(
                        song_.notespath, settings_, [this](float f) {
                            if (cancel_.load(std::memory_order_relaxed))
                                throw AnalysisCancelled{};
                            progress_.store(f, std::memory_order_relaxed);
                        });
                    return true;
                } catch (const AnalysisCancelled&) {
                    return false;  // no error text: the UI discards a cancelled job
                }
            });
        });
    } catch (const std::exception& e) {
        error_ = e.what();
        ok_ = false;
        finished_.store(true);
    }
}

app::AnalysisResult AnalyzeJob::take_result() { return std::move(*result_); }

}  // namespace hydra::ui
