// Analysis orchestration: search/pather.h paths one chart, and this
// file does the rest: finding charts on disk, hashing/reading their
// metadata, and running many of them across a thread pool into a RecordStore.
//
// A std::thread pool runs charts in parallel and shares memory, so no worker
// processes or row copying across a pipe are needed.

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
#include "search/pather.h"
#include "store/record_store.h"

namespace hydra::app {

// One chart file found on disk, with enough metadata to register it in the
// store. `sig` fingerprints the source files
// (sizes + mtimes) so a later rescan can skip re-hashing unchanged charts;
// it never leaves the charts table and is not part of record identity.
struct ScanItem {
    std::string md5;
    std::string title;
    std::string artist;
    std::string charter;
    std::string notespath;
    std::string rootfolder;
    std::string sig;
};

// Progress/cancel hooks for the extended scan. Callbacks fire on the calling
// thread only (never a worker), like run_batch's.
struct ScanCallbacks {
    // Running count of folders visited during enumeration (monotonic; the
    // same values the simple overload's cb_progress sees).
    std::function<void(int)> on_folders;
    // Chart-reading progress: fired once with done=0 when the total becomes
    // known (enumeration finished), then per chart processed. `cached` counts
    // charts satisfied from the rescan cache without touching the file.
    std::function<void(int done, int total, int cached)> on_charts;
    const std::atomic<bool>* cancel = nullptr;
};

// Recursively searches rootfolders for chart-bearing folders: notes.mid (or
// notes.chart, if no .mid) alongside a song.ini, plus every .sng and .srb file.
// Re-encountered folders are skipped.
//
// The walk itself is a serial single pass; hashing/metadata reads run on a
// batch_worker_count() thread pool. Results keep the serial walk's order.
// `cache` (from RecordStore::chart_library_cache), if given, lets a chart
// whose files' sizes+mtimes are unchanged reuse its previous md5/metadata
// without any file I/O. A failing chart file is skipped with an error entry;
// its folder's other charts and subtree still scan (unlike the Python
// original, which dropped the whole folder).
std::pair<std::vector<ScanItem>, std::vector<std::string>> discover_charts(
    const std::vector<std::string>& rootfolders, const ScanCallbacks& callbacks,
    const store::ChartLibraryCache* cache = nullptr);

// Compatibility form: folder progress only, no cache, no cancel.
std::pair<std::vector<ScanItem>, std::vector<std::string>> discover_charts(
    const std::vector<std::string>& rootfolders,
    const std::function<void(int)>& cb_progress = nullptr);

// Chord counts by code, for the "how big is this chart" display. Dispatches
// via load_songpath, so it takes any supported chart type
// (.mid/.chart/.sng/.srb).
std::map<std::string, int> count_chart_chords(const std::string& filepath);

// The chart file's hyhash: the same MD5 the library scan writes to the
// songmeta/charts rows, so a tool can look a chart up in the record store
// by path alone. Returns an empty string if the file cannot be read.
std::string hash_chart_file(const std::string& path);

// The [song] section of a song.ini as lower-cased key -> value, with the
// value's leading blanks trimmed. A key seen twice keeps its last value.
// Section and key names match in any case, `;` and `#` start comments, and a
// UTF-8 BOM is skipped. Throws std::runtime_error when the file cannot be
// read. The library scan (name, artist, charter) and the Preview (delay) both
// read song.ini through here.
std::map<std::string, std::string> read_song_ini_keys(const std::string& path);

// The settings a batch run applies uniformly. Everything the search itself reads
// lives on the SearchSettings base; the two flags here are parse-time only.
struct AnalysisSettings : SearchSettings {
    bool prodrums = true;
    bool bass2x = true;
    // Which charted difficulty to read. Expert by default, so every existing
    // caller keeps the behavior it had.
    Difficulty difficulty = Difficulty::Expert;
};

// Loads and analyzes one chart file (.mid/.chart/.sng/.srb), producing a record and
// the song's timing (for the store's songmeta row).
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
// chart can reach hundreds of MB) stays bounded.
int batch_worker_count();

// Runs analyze_chart_file + store::prepare_row for every item across a
// std::thread pool, writing results into `store` from the calling thread (a
// RecordStore is safe to call from any one thread at a time, but SQLite
// writes are serialized here to keep the store simple).
//
// on_progress, on_error and on_result, if set, are invoked from the calling
// thread only (never from a worker) as each result comes back — safe to touch
// UI state. on_result fires after the row is written to the store, with the
// row it wrote (the batch CLI prints score/bestpath from it).
// `lens` is the store key the caller's settings file results under: pass `Settings::lens()`.
void run_batch(const std::vector<ScanItem>& items, const std::string& chartmode,
               const store::Lens& lens,
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
