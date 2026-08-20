// Sortable HTML path report: row collection and page building. Split from
// the CLI's main() so the GUI's ReportJob and the tests
// (tests/test_report.cpp) can build the page without a process spawn.

#ifndef HYDRA_APP_REPORT_H
#define HYDRA_APP_REPORT_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "store/record_store.h"

namespace hydra::app::report {

// One table row. Field order is the JSON key order the page's script reads;
// keep it stable so old and new report files stay comparable.
struct ReportRow {
    std::string song;
    std::string artist;
    std::string charter;
    std::string mode;
    int rank = 1;
    std::string path;
    int64_t score = 0;
    int64_t delta = 0;
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
};

// Strips Clone Hero's <color=...> markup from a charter credit / title and
// trims whitespace. Mirrors hydra_report.plain.
std::string plain(const std::string& text);

// (label, token) for a hardest per-hit squeeze value, e.g. (Extreme, t2).
// nullopt -> (None, tn). Bands derive from the hit window W: <1 Normal (an
// absolute floor -- sub-ms is free at any window), then quarters of W up to
// Beyond at >= W.
std::pair<std::string, std::string> tier_for(const std::optional<double>& ms,
                                             double hit_window_ms = 85.0);

// Reads every stored record (skipping ones stamped by another version or
// edition) and produces up to max_paths rows per chart, best score first.
// hit_window_ms feeds the tier labels only; `ms` itself is W-free.
std::vector<ReportRow> collect_rows(store::RecordStore& store, int64_t max_paths,
                                    bool uncapped, double hit_window_ms = 85.0);

// The self-contained page: the PAGE template with subtitle/footer escaped in
// and a JSON payload {hit_window, tiers, rows} embedded, so the page's tier
// dropdown and stats derive from the same window the rows were labeled with.
std::string build_html(const std::vector<ReportRow>& rows, const std::string& subtitle,
                       const std::string& footer, double hit_window_ms = 85.0);

// repr(float) / json.dumps float formatting (shortest round-trip). Exposed
// for tests.
std::string py_repr(double v);

// round(v, 3), matching Python's correctly-rounded decimal rounding.
double py_round3(double v);

}  // namespace hydra::app::report

#endif  // HYDRA_APP_REPORT_H
