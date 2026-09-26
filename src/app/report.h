// Sortable HTML path report: row collection and page building. Split from
// the CLI's main() so the GUI's ReportJob and the tests
// (tests/test_report.cpp) can build the page without a process spawn.

#ifndef HYDRA_APP_REPORT_H
#define HYDRA_APP_REPORT_H

#include <atomic>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <string>
#include <vector>

#include "store/record_store.h"
#include "core/squeeze_rating.h"

namespace hydra::app::report {

// One table row. Field order is the JSON key order the page's script reads,
// except `hyhash`, which the page never sees.
struct ReportRow {
    std::string song;
    std::string artist;
    std::string charter;
    std::string mode;
    int rank = 1;
    std::string path;
    int64_t score = 0;
    int acts = 0;
    int skip = 0;
    std::optional<double> ms;
    std::string tier;
    std::string tok;
    // Hardest calibration fill among the path's E0 activations, in the same
    // difficulty convention as `ms` (e_difficulty = -e_offset: positive means
    // you must hit that much early, negative is slack). Skipped E activations
    // don't count — their fill is irrelevant to the path. Unset when the path
    // has no E0 fill (distinct from a real 0.0ms fill).
    std::optional<double> efill;
    double mult = 0.0;  // already round(x, 3)'d, like the Python row
    int sqin = 0;
    int sqout = 0;
    int notes = 0;
    // The chart this row belongs to. Not written to the page: generate_report
    // counts the distinct charts on the page with it.
    std::string hyhash;
};

// Strips Clone Hero's <color=...> markup from a charter credit / title and
// trims whitespace. Mirrors hydra_report.plain.
std::string plain(const std::string& text);

// (label, token) for a hardest-squeeze value (raw ms), e.g. (Extreme, t2).
// nullopt -> (None, tn). Bands derive from the two-hit budget 2*W: <2 Normal
// (an absolute floor), then quarters of 2*W up to Beyond at >= 2*W. At the
// historical W = 70 this is the original 2/35/70/105/140 ladder.
std::pair<std::string, std::string> tier_for(const std::optional<double>& ms,
                                             double hit_window_ms = kDefaultHitWindowMs);

// The same label, read from an already-built timing_tiers table, so a caller
// labeling many rows builds the table once.
std::pair<std::string, std::string> tier_for(const std::optional<double>& ms,
                                             const std::vector<TimingTier>& tiers);

// Every listed record for one chart mode, cap and lens, keyed by its chart
// hash in lower case. Both comparison pages join on this.
std::unordered_map<std::string, store::RecordListing> records_by_hash(
    store::RecordStore& store, const std::string& chartmode, const store::CapQuery& cap,
    const store::Lens& lens);

// Reads every stored record at the wanted cap and lens (skipping ones the
// store calls stale) and produces up to max_paths rows per chart, best score
// first. hit_window_ms feeds the tier labels only.
// `cancel`, when given, stops the walk between records and leaves the rows
// collected so far; the caller is expected to throw the half-built result
// away.
std::vector<ReportRow> collect_rows(store::RecordStore& store, int64_t max_paths,
                                    const store::CapQuery& cap, const store::Lens& lens,
                                    double hit_window_ms = kDefaultHitWindowMs,
                                    const std::atomic<bool>* cancel = nullptr);

// The self-contained page: the PAGE template with subtitle/footer escaped in
// and a JSON payload {hit_window, tiers, rows} embedded, so the page's tier
// dropdown and stats derive from the same window the rows were labeled with.
std::string build_html(const std::vector<ReportRow>& rows, const std::string& subtitle,
                       const std::string& footer,
                       double hit_window_ms = kDefaultHitWindowMs);

// ---- generate_report -------------------------------------------------------
// The whole report in one call: counts + rows + the standard page framing
// (subtitle and footer). The GUI's ReportJob and the hydra_report CLI are
// adapters over this seam; both previously composed the same framing strings
// by hand, where they could (and did) drift.

// The path report's default: the top 5 paths per chart.
inline constexpr int64_t kDefaultReportPaths = 5;
// "--all-paths" asks for this many, which no chart reaches.
inline constexpr int64_t kEveryPathSentinel = 1000000000;
// A max_paths above this is labeled "every path" in the subtitle.
inline constexpr int64_t kEveryPathLabelThreshold = 100000000;

struct ReportOptions {
    int64_t max_paths = kDefaultReportPaths;
    // Which records the page lists: the user's current SP cap and lens.
    store::CapQuery cap = store::CapQuery::at(kCloneHeroSpCap);
    store::Lens lens;
    int hit_window_ms = static_cast<int>(kDefaultHitWindowMs);
    std::string db_path;  // names the footer's source database
    // Set this and the walk stops between records and generate_report hands
    // back an empty result -- no rows, no html. Closing the app while a report
    // builds goes through here; the CLI never sets it.
    const std::atomic<bool>* cancel = nullptr;
};

struct GeneratedReport {
    std::string html;  // empty when the store held no reportable rows
    int64_t songs = 0;    // distinct charts with rows on the page
    int64_t records = 0;  // records with rows on the page (one rank-1 row each)
    int64_t rows = 0;
};

GeneratedReport generate_report(store::RecordStore& store,
                                const ReportOptions& options);

}  // namespace hydra::app::report

#endif  // HYDRA_APP_REPORT_H
