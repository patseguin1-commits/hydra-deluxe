// Analysis orchestration — the C++ port of hydra/hyutil.py's discovery half
// and hydra/hybatch.py's unit of work. Phase 3's search/pather.{h,cpp} already
// ports hyutil's _analyze/_analyze_at_cap/_analyze_uncapped (one chart's
// pathing); this is the rest: finding charts on disk, hashing/reading their
// metadata, and running many of them across a thread pool into a RecordStore.
//
// Unlike hybatch.py, this doesn't need to avoid the GIL by spawning
// processes — a native std::thread pool genuinely runs charts in parallel,
// sharing memory instead of pickling rows across a pipe.

#ifndef HYDRA_APP_ANALYSIS_H
#define HYDRA_APP_ANALYSIS_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/model.h"
#include "parse/song.h"
#include "store/record_store.h"

namespace hydra::app {

// One chart file found on disk, with enough metadata to register it in the
// store. Mirrors hyutil.ScanItem.
struct ScanItem {
    std::string md5;
    std::string title;
    std::string artist;
    std::string charter;
    std::string notespath;
    std::string rootfolder;

    static ScanItem from_notes_ini_pair(const std::string& notes_path,
                                        const std::string& ini_path,
                                        const std::string& rootfolder);
    static ScanItem from_sng(const std::string& sng_path, const std::string& rootfolder);
};

// Recursively searches rootfolders for chart-bearing folders: notes.mid (or
// notes.chart, if no .mid) alongside a song.ini, plus every .sng file.
// Re-encountered folders are skipped. Mirrors hyutil.discover_charts.
// cb_progress, if set, is called with the running count of folders visited.
std::pair<std::vector<ScanItem>, std::vector<std::string>> discover_charts(
    const std::vector<std::string>& rootfolders,
    const std::function<void(int)>& cb_progress = nullptr);

// Same folder walk as discover_charts, counting only. Lets a caller show
// progress for the (usually longer) metadata-reading pass that follows.
// Mirrors hyutil.get_folder_count.
int get_folder_count(const std::vector<std::string>& rootfolders,
                     const std::function<void(int)>& cb_progress = nullptr);

// Chord counts by code, for the "how big is this chart" display. Mirrors
// hyutil.count_chart_chords. filepath must end in .mid or .chart.
std::map<std::string, int> count_chart_chords(const std::string& filepath);

// The settings a batch run applies uniformly, mirroring the `settings` tuple
// hybatch.analyze_for_store's job carries.
struct AnalysisSettings {
    std::string difficulty = "Expert";
    bool prodrums = true;
    bool bass2x = true;
    int depth_mode = 0;   // matches search/graph.h's DepthMode
    int depth_value = 4;
    std::optional<double> ms_filter;
    bool uncapped = false;
    // Uncapped edition only: a manual SP meter ceiling in bars (any value).
    // nullopt runs the auto-settling ladder. Ignored when !uncapped.
    std::optional<int> sp_cap;
    // Uncapped ladder only: seconds before a too-slow rung is abandoned
    // (hymisc.SP_CAP_TIME_BUDGET). nullopt runs every rung to completion.
    std::optional<double> uncapped_time_budget_s;
};

// Loads and analyzes one chart file (.mid/.chart/.sng), producing a record and
// the song's timing (for the store's songmeta row). Mirrors
// hyutil.analyze_chart_file + hybatch.analyze_for_store's non-store half.
struct AnalysisResult {
    HydraRecord record;
    Song song;  // carries tick_resolution/tpm_changes/bpm_changes for add_song
};
// on_progress, if set, is called from the calling thread with a monotonic 0..1
// fraction as the search sweeps the chart — for a single-chart progress bar.
AnalysisResult analyze_chart_file(const std::string& filepath,
                                  const AnalysisSettings& settings,
                                  const std::function<void(float)>& on_progress = {});

// One unit of batch work: analyze a ScanItem's chart and store the result
// under `chartmode`. Skips charts that already have a record unless `redo`.
struct BatchProgress {
    int completed = 0;
    int total = 0;
    std::string current_title;
};

// The default batch pool size: one core is left for the UI (or shell) and for
// whatever else the machine is doing; capped so peak memory (a discography
// chart can reach hundreds of MB) stays bounded. Mirrors hydra_app.py's
// BATCH_MAX_WORKERS/batch_workercount.
int batch_worker_count();

// Runs analyze_chart_file + store::prepare_row for every item across a
// std::thread pool, writing results into `store` from the calling thread (a
// RecordStore is safe to call from any one thread at a time, but SQLite
// writes are serialized here to keep the store simple). Mirrors the
// concurrency hybatch/BatchJob got from multiprocessing, without the pickling.
//
// on_progress, on_error and on_result, if set, are invoked from the calling
// thread only (never from a worker) as each result comes back — safe to touch
// UI state. on_result fires after the row is written to the store, with the
// row it wrote (the batch CLI prints score/bestpath from it).
void run_batch(const std::vector<ScanItem>& items, const std::string& chartmode,
               const AnalysisSettings& settings, store::RecordStore& store, bool redo,
               int worker_count,
               const std::function<void(const BatchProgress&)>& on_progress = nullptr,
               const std::function<void(const std::string& title, const std::string& error)>&
                   on_error = nullptr,
               const std::function<void(const ScanItem&, const store::PreparedRow&)>&
                   on_result = nullptr,
               const std::atomic<bool>* cancel = nullptr);

}  // namespace hydra::app

#endif  // HYDRA_APP_ANALYSIS_H
