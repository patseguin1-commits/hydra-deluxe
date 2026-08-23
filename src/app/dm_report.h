// Comparison report: one dmleaderboards user's actual scores against Hydra's
// computed optimal for the same charts. A parallel of app/report.h (kept
// separate so report.h's byte-exact parity test is never disturbed): it joins
// the fetched scores to stored records by chart-file MD5 (leaderboard
// `identifier` == Hydra `hyhash`) and emits a self-contained sortable HTML page.
//
// "Above optimal" is expected, not an error: Hydra's optimal intentionally
// excludes several score backends, and many leaderboard scores were set on an
// older Clone Hero version whose fill-spawning let players reach totals that
// are impossible in the current game. Such rows are labelled, not flagged.

#ifndef HYDRA_APP_DM_REPORT_H
#define HYDRA_APP_DM_REPORT_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/dmbot_client.h"
#include "store/record_store.h"

namespace hydra::app::dm_report {

// One table row: a single leaderboard score plus the Hydra record it matched.
struct DmReportRow {
    std::string song;
    std::string artist;
    std::string charter;
    std::string identifier;             // chart-file MD5 (the join key)
    int64_t actual = 0;                 // score the player posted
    std::optional<int64_t> optimal;     // Hydra best-path score; unset if unmatched
    std::optional<int64_t> delta;       // optimal - actual (points left); <0 == above optimal
    std::optional<double> pct;          // actual/optimal*100, only when speed==100
    bool is_fc = false;
    int percent = 0;
    int speed = 100;
    std::optional<int> rank;
    std::string posted;                 // ISO-8601 timestamp
    std::string status;                 // "matched" | "above optimal" | "unmatched"
};

// Joins every fetched score against the store's records for `chartmode`,
// preferring the leaderboard's own song/artist metadata and falling back to the
// matched Hydra record's when the leaderboard entry is an "unknown" one.
std::vector<DmReportRow> collect_dm_rows(store::RecordStore& store,
                                         const std::vector<net::DmScore>& scores,
                                         const std::string& chartmode);

// The self-contained comparison page. Same __SUBTITLE__/__FOOTER__/__DATA__
// placeholder mechanism as report::build_html, with its own columns.
std::string build_dm_html(const std::vector<DmReportRow>& rows, const std::string& subtitle,
                          const std::string& footer);

// ---- generate_dm_report ----------------------------------------------------
// The whole comparison in one call: join + tally + the standard page framing.
// The GUI's DmReportJob is an adapter over this seam; the tally previously
// lived in the job layer, comparing the status literals the tests pin.

struct DmReportStats {
    int total = 0;
    int matched = 0;
    int above = 0;      // "above optimal"
    int unmatched = 0;  // not in the library
};
DmReportStats tally_dm_rows(const std::vector<DmReportRow>& rows);

struct GeneratedDmReport {
    std::string html;  // empty when the user had no scores to compare
    DmReportStats stats;
};

GeneratedDmReport generate_dm_report(store::RecordStore& store,
                                     const std::vector<net::DmScore>& scores,
                                     const std::string& chartmode,
                                     const std::string& username);

}  // namespace hydra::app::dm_report

#endif  // HYDRA_APP_DM_REPORT_H
