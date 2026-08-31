// Comparison report: the same charts scored under Clone Hero 1.0's fill-spawn
// rule against Clone Hero 1.1's. A parallel of app/dm_report.h, kept separate
// so app/report.h's byte-exact parity test is never disturbed.
//
// A drum fill only spawns if the player's SP meter filled up by some deadline.
// Clone Hero 1.0 set that deadline about one fill-length before the fill;
// Clone Hero 1.1 made it a flat 4 beats (search/graph.h FillDeadlineRule).
// Short fills therefore got stricter and long fills got looser, so most charts
// lose or tie under 1.1 and a few gain.
//
// The rule is not part of a record's identity (docs/adr/0010), so the two
// answers live in two separate database files. This joins them by chart hash
// and emits a self-contained sortable HTML page.

#ifndef HYDRA_APP_FILL_REPORT_H
#define HYDRA_APP_FILL_REPORT_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "store/record_store.h"

namespace hydra::app::fill_report {

// One table row: one chart as both databases scored it. Every score field is
// optional because a chart may be stored in only one of the two.
struct FillCompareRow {
    std::string song, artist, charter, hyhash;
    std::optional<int64_t> old_score, new_score;  // CH 1.0, CH 1.1
    std::optional<int64_t> delta;                 // new - old, only when both
    std::string old_path, new_path;
    std::optional<int> old_acts, new_acts;
    std::optional<int> notes;
    // "same" | "1.0 higher" | "1.1 higher" | "only 1.0" | "only 1.1".
    // Exact literals: tally_fill_rows and the page's chip colors compare them.
    std::string status;
};

// Joins the two stores' records for identical settings, indexed by lowercased
// hyhash, over the union of both key sets — a chart stored on one side only
// still gets a row. Song/artist/charter prefer the 1.1 (new) side and are run
// through report::plain to strip Clone Hero color markup.
std::vector<FillCompareRow> collect_fill_rows(store::RecordStore& old_store,
                                              store::RecordStore& new_store,
                                              const std::string& chartmode,
                                              const store::CapQuery& cap,
                                              const store::Lens& lens);

struct FillCompareStats {
    int total = 0;
    int same = 0;
    int ch10_higher = 0;
    int ch11_higher = 0;
    int only_old = 0;
    int only_new = 0;
};
FillCompareStats tally_fill_rows(const std::vector<FillCompareRow>& rows);

// The self-contained comparison page. Same __SUBTITLE__/__FOOTER__/__DATA__
// placeholder mechanism as report::build_html, with its own columns.
std::string build_fill_html(const std::vector<FillCompareRow>& rows,
                            const std::string& subtitle,
                            const std::string& footer);

struct GeneratedFillReport {
    std::string html;  // empty when neither database had a record
    FillCompareStats stats;
};

// The whole comparison in one call: join + tally + the standard page framing.
GeneratedFillReport generate_fill_report(store::RecordStore& old_store,
                                         store::RecordStore& new_store,
                                         const std::string& chartmode,
                                         const store::CapQuery& cap,
                                         const store::Lens& lens);

}  // namespace hydra::app::fill_report

#endif  // HYDRA_APP_FILL_REPORT_H
