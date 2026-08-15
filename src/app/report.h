// Sortable HTML path report — the C++ port of hydra_report.py's row
// collection and page building. Split from the CLI's main() so the parity
// test (tests/test_report.cpp) can diff build_html's output byte-for-byte
// against the Python-generated golden.

#ifndef HYDRA_APP_REPORT_H
#define HYDRA_APP_REPORT_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "store/record_store.h"

namespace hydra::app::report {

// One table row, the same shape as the dicts hydra_report.collect_rows built.
// Field order here is the JSON key order json.dumps preserved (dict insertion
// order), which the byte-diff against golden depends on.
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
    double mult = 0.0;  // already round(x, 3)'d, like the Python row
    int sqin = 0;
    int sqout = 0;
    int notes = 0;
};

// Strips Clone Hero's <color=...> markup from a charter credit / title and
// trims whitespace. Mirrors hydra_report.plain.
std::string plain(const std::string& text);

// (label, token) for a hardest-squeeze value, e.g. (Extreme, t2). nullopt ->
// (None, tn). Mirrors hydra_report.tier_for / TIERS.
std::pair<std::string, std::string> tier_for(const std::optional<double>& ms);

// Reads every stored record (skipping ones stamped by another version or
// edition) and produces up to max_paths rows per chart, best score first.
// Mirrors hydra_report.collect_rows.
std::vector<ReportRow> collect_rows(store::RecordStore& store, int64_t max_paths,
                                    bool uncapped);

// The self-contained page: the PAGE template with subtitle/footer escaped in
// and the rows embedded as JSON. Byte-identical to the Python build_html for
// the same inputs.
std::string build_html(const std::vector<ReportRow>& rows, const std::string& subtitle,
                       const std::string& footer);

// repr(float) / json.dumps float formatting (shortest round-trip). Exposed
// for tests.
std::string py_repr(double v);

// round(v, 3), matching Python's correctly-rounded decimal rounding.
double py_round3(double v);

}  // namespace hydra::app::report

#endif  // HYDRA_APP_REPORT_H
