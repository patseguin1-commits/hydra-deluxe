#include "ui/library_jobs.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>

#include "app/config.h"
#include "app/report.h"
#include "app/report_files.h"
#include "app/user_messages.h"

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

namespace {

double steady_seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

void BatchClock::start(double now) {
    started_ = now;
    paused_at_.reset();
    finished_at_.reset();
    paused_total_ = 0.0;
}

void BatchClock::pause(double now) {
    if (started_ && !paused_at_ && !finished_at_) paused_at_ = now;
}

void BatchClock::resume(double now) {
    if (!paused_at_ || finished_at_) return;
    paused_total_ += now - *paused_at_;
    paused_at_.reset();
}

void BatchClock::finish(double now) {
    if (!started_ || finished_at_) return;
    finished_at_ = paused_at_ ? *paused_at_ : now;
    paused_at_.reset();
}

double BatchClock::elapsed_s(double now) const {
    if (!started_) return 0.0;
    const double end = finished_at_ ? *finished_at_ : paused_at_ ? *paused_at_ : now;
    return std::max(0.0, end - *started_ - paused_total_);
}

std::optional<double> batch_eta_s(double elapsed_s, int completed, int total) {
    if (total <= 0 || completed < kEtaMinFinished || completed > total) return std::nullopt;
    return elapsed_s / completed * (total - completed);
}

BatchJob::BatchJob(std::optional<std::string> search, app::BatchRun run,
                   store::RecordStore& store, bool redo)
    : search_(std::move(search)),
      run_(std::move(run)),
      store_(store),
      redo_(redo),
      workers_(app::batch_worker_count()) {}

BatchJob::BatchJob(std::vector<store::ChartLibraryEntry> charts, app::BatchRun run,
                   store::RecordStore& store, bool redo)
    : given_(std::move(charts)),
      run_(std::move(run)),
      store_(store),
      redo_(redo),
      workers_(app::batch_worker_count()) {}

void BatchJob::set_analyzer_for_test(app::ChartAnalyzer analyze, int workers) {
    analyze_ = std::move(analyze);
    workers_ = std::max(1, workers);
}

void BatchJob::start() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        const double now = steady_seconds();
        clock_.start(now);
        if (snap_.paused) clock_.pause(now);  // paused before it started
    }
    spawn([this] { run(); });
}

void BatchJob::pause() {
    {
        std::lock_guard<std::mutex> lock(pause_mu_);
        if (paused_) return;
        paused_ = true;
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (snap_.finished) return;
    snap_.paused = true;
    clock_.pause(steady_seconds());
}

void BatchJob::resume() {
    {
        std::lock_guard<std::mutex> lock(pause_mu_);
        if (!paused_) return;
        paused_ = false;
    }
    pause_cv_.notify_all();
    std::lock_guard<std::mutex> lock(mu_);
    snap_.paused = false;
    clock_.resume(steady_seconds());
}

void BatchJob::stop() {
    cancel_.store(true);
    {
        // Taking the lock puts this stop between a waiting worker's checks,
        // so no worker can sleep through it.
        std::lock_guard<std::mutex> lock(pause_mu_);
    }
    pause_cv_.notify_all();
}

void BatchJob::wait_while_paused() {
    std::unique_lock<std::mutex> lock(pause_mu_);
    pause_cv_.wait(lock, [this] { return !paused_ || cancel_.load(); });
    if (cancel_.load()) throw app::AnalysisCancelled{};
}

void BatchJob::note_started(const std::string& notespath) {
    auto it = by_path_.find(notespath);
    if (it == by_path_.end()) return;
    const app::ScanItem& item = items_[it->second];
    std::lock_guard<std::mutex> lock(mu_);
    snap_.current_title = item.title;
    snap_.current_artist = item.artist;
}

BatchJob::Snapshot BatchJob::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    Snapshot s = snap_;
    s.elapsed_s = clock_.elapsed_s(steady_seconds());
    if (!s.finished && !s.paused) s.eta_s = batch_eta_s(s.elapsed_s, s.completed, s.total);
    return s;
}

void BatchJob::run() {
    // Load the item list here rather than on the UI thread: an unbounded
    // SELECT over a big library takes long enough to freeze a frame.
    try {
        std::vector<store::ChartLibraryEntry> entries =
            given_ ? std::move(*given_)
                   : store_.list_chart_library(search_, 0, -1);  // LIMIT -1 = no limit
        items_.reserve(entries.size());
        for (const store::ChartLibraryEntry& e : entries)
            items_.push_back({e.md5, e.title, e.artist, e.charter, e.notespath, e.rootfolder});
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.preparing = false;
        snap_.failures.push_back(app::plain_error(e));
        snap_.failure_details.push_back(std::string("Could not load the library: ") + e.what());
        ++snap_.failed;
        clock_.finish(steady_seconds());
        snap_.finished = true;
        return;
    }
    for (size_t i = 0; i < items_.size(); ++i) by_path_.emplace(items_[i].notespath, i);

    {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.preparing = false;
    }
    if (cancel_.load()) {
        std::lock_guard<std::mutex> lock(mu_);
        clock_.finish(steady_seconds());
        snap_.finished = true;
        return;
    }

    bool total_known = false;

    app::BatchCallbacks callbacks;
    callbacks.on_progress = [this, &total_known](const app::BatchProgress& p) {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.total = p.total;
        snap_.completed = p.completed;
        if (!total_known) {
            snap_.skipped = static_cast<int>(items_.size()) - p.total;
            total_known = true;
        }
    };
    callbacks.on_error = [this](const std::string& title, const std::string& error) {
        std::lock_guard<std::mutex> lock(mu_);
        ++snap_.failed;
        snap_.failures.push_back(title + ": " + app::plain_error_text(error));
        snap_.failure_details.push_back(title + ": " + error);
    };
    callbacks.cancel = &cancel_;
    // The pause gate and the "now analyzing" line sit in front of the real
    // analyzer, so run_batch and its pool stay as they are.
    const app::ChartAnalyzer inner =
        analyze_ ? analyze_ : app::ChartAnalyzer(app::analyze_chart_file);
    callbacks.analyze = [this, inner](const std::string& path,
                                      const app::AnalysisSettings& settings,
                                      const std::function<void(float)>& on_progress) {
        wait_while_paused();
        note_started(path);
        return inner(path, settings, on_progress);
    };
    app::run_batch(items_, run_, store_, redo_, workers_, callbacks);

    std::lock_guard<std::mutex> lock(mu_);
    snap_.current_title.clear();
    snap_.current_artist.clear();
    snap_.paused = false;
    clock_.finish(steady_seconds());
    snap_.finished = true;
}

// ---- AnalyzeJob -------------------------------------------------------

AnalyzeJob::AnalyzeJob(store::ChartLibraryEntry song, store::RecordKey key,
                       app::AnalysisSettings settings)
    : song_(std::move(song)),
      key_(std::move(key)),
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
                                throw app::AnalysisCancelled{};
                            progress_.store(f, std::memory_order_relaxed);
                        });
                    return true;
                } catch (const app::AnalysisCancelled&) {
                    return false;  // no error text: the UI discards a cancelled job
                }
            });
        });
    } catch (const std::exception& e) {
        error_ = e.what();
        message_ = app::plain_error(e);
        ok_ = false;
        finished_.store(true);
    }
}

app::AnalysisResult AnalyzeJob::take_result() { return std::move(*result_); }

// ---- ReportJob --------------------------------------------------------

ReportJob::ReportJob(store::RecordStore& store, store::CapQuery cap, store::Lens lens,
                     bool open_when_done, int hit_window_ms)
    : store_(store),
      cap_(cap),
      lens_(lens),
      open_when_done_(open_when_done),
      hit_window_ms_(hit_window_ms) {}

void ReportJob::start() { spawn([this] { run(); }); }

void ReportJob::run() {
    run_guarded([this] {
        // One seam for the whole page — rows, counts, and framing come from
        // generate_report, the same call the hydra_report CLI makes.
        app::report::ReportOptions options;
        options.max_paths = app::report::kDefaultReportPaths;
        options.cap = cap_;
        options.lens = lens_;
        options.hit_window_ms = hit_window_ms_;
        options.db_path = app::db_path();
        // Closing Hydra sets this. Without it the window waits for the whole
        // library to be read before it can shut down.
        options.cancel = &cancel_;
        app::report::GeneratedReport report =
            app::report::generate_report(store_, options);
        // Checked before the "no records" throw and before any file is
        // written: a cancelled run has no rows because it stopped, not
        // because the store is empty, and it must leave the last report on
        // disk alone.
        if (is_cancelled()) return false;
        if (report.rows == 0) throw std::runtime_error("no records stored yet");

        // A browser that won't open the page is not a failed report: the
        // page is saved, and the finished strip says so (audit B1).
        outcome_ = publish_report(app::report_html_path(), report.html, open_when_done_);
        return true;
    });
}

}  // namespace hydra::ui
