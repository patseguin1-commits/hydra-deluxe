// Tests for app/fill_report: the CH 1.0 vs CH 1.1 join (collect_fill_rows),
// the tally, and the comparison page (build_fill_html). Pins the status
// strings as raw literals -- tally_fill_rows and the page's chip-color map
// both compare them.
//
// The two rules are two whole databases (docs/adr/0010), so every case here
// uses two in-memory stores holding the same chart hashes at different scores.

#include "doctest.h"

#include <stdexcept>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "app/fill_report.h"
#include "core/model.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "store/record_store.h"

using namespace hydra;
using app::fill_report::FillCompareRow;

namespace {

constexpr const char* kMode = "Expert Pro Drums, 2x Bass";
constexpr const char* kBoth = "aa11bb22cc33dd44ee55ff6677889900";
constexpr const char* kOldOnly = "bb22cc33dd44ee55ff6677889900aa11";
constexpr const char* kNewOnly = "cc33dd44ee55ff6677889900aa11bb22";

store::RecordKey key_for(const std::string& hash) {
    return store::RecordKey{hash, kMode, store::CapQuery::at(kCloneHeroSpCap),
                            store::Lens{}};
}

// One analyzed corpus chart, kept alive for the whole file: it supplies a real
// Song (songmeta rows, which list_records joins against) and a real record to
// build PreparedRows from. The scores themselves are overridden per row, so
// which chart it is doesn't matter.
const app::AnalysisResult& sample_chart() {
    static app::AnalysisResult result = [] {
        app::AnalysisSettings settings;
        settings.depth_value = 0;
        for (const std::string& path : corpus::chart_paths()) {
            try {
                app::AnalysisResult r = app::analyze_chart_file(path, settings);
                if (r.song.is_empty() || r.record.paths.empty()) continue;
                return r;
            } catch (const std::exception&) {
                continue;
            }
        }
        // Song has no default constructor, so there is no empty AnalysisResult
        // to fall back on. Throwing keeps the lambda's deduced return type as
        // the one `return r;` above gives it.
        throw std::runtime_error("no corpus chart analyzed");
    }();
    return result;
}

// Registers `hash` in `store` and files a result for it with exactly the
// summary given. The structure/nodes come from a real record so the row is
// well-formed; only the numbers the report reads are overridden.
void put(store::RecordStore& store, const std::string& hash, int64_t score,
         int acts, const std::string& bestpath) {
    const app::AnalysisResult& sample = sample_chart();
    store.add_song(hash, "Song " + hash.substr(0, 4), "Test Artist",
                   "Test Charter", sample.song);

    store::PreparedRow row = store::prepare_row(key_for(hash), sample.record);
    row.hyhash = hash;
    row.bestpath = bestpath;
    row.summary.score = score;
    row.summary.actcount = acts;
    row.summary.notecount = 1234;
    store.add_row(row);
}

std::vector<FillCompareRow> compare(store::RecordStore& old_store,
                                    store::RecordStore& new_store) {
    return app::fill_report::collect_fill_rows(
        old_store, new_store, kMode, store::CapQuery::at(kCloneHeroSpCap),
        store::Lens{});
}

const FillCompareRow* find(const std::vector<FillCompareRow>& rows,
                           const std::string& hash) {
    for (const FillCompareRow& r : rows)
        if (r.hyhash == hash) return &r;
    return nullptr;
}

}  // namespace

TEST_CASE("collect_fill_rows: delta sign and status literals") {
    store::RecordStore old_store(":memory:");
    store::RecordStore new_store(":memory:");

    // 1.1 scores higher: the rare long-fill gain.
    put(old_store, kBoth, 1000000, 3, "old-path-A");
    put(new_store, kBoth, 1050000, 4, "new-path-A");

    std::vector<FillCompareRow> rows = compare(old_store, new_store);
    REQUIRE(rows.size() == 1);
    const FillCompareRow& r = rows[0];

    CHECK(r.old_score == 1000000);
    CHECK(r.new_score == 1050000);
    REQUIRE(r.delta.has_value());
    CHECK(*r.delta == 50000);  // new - old
    CHECK(r.status == "1.1 higher");

    // Both sides' paths and act counts survive the join.
    CHECK(r.old_path == "old-path-A");
    CHECK(r.new_path == "new-path-A");
    CHECK(r.old_acts == 3);
    CHECK(r.new_acts == 4);
    CHECK(r.notes == 1234);
}

TEST_CASE("collect_fill_rows: 1.0 higher and same") {
    store::RecordStore old_store(":memory:");
    store::RecordStore new_store(":memory:");

    // The common case: 4 beats is the stricter deadline, so 1.1 drops.
    put(old_store, kBoth, 1050000, 4, "old-path-B");
    put(new_store, kBoth, 1000000, 3, "new-path-B");
    // A chart where the rule change made no difference at all.
    put(old_store, kOldOnly, 777000, 2, "tie-path");
    put(new_store, kOldOnly, 777000, 2, "tie-path");

    std::vector<FillCompareRow> rows = compare(old_store, new_store);
    REQUIRE(rows.size() == 2);

    const FillCompareRow* dropped = find(rows, kBoth);
    REQUIRE(dropped != nullptr);
    CHECK(*dropped->delta == -50000);
    CHECK(dropped->status == "1.0 higher");

    const FillCompareRow* tied = find(rows, kOldOnly);
    REQUIRE(tied != nullptr);
    CHECK(*tied->delta == 0);
    CHECK(tied->status == "same");
}

TEST_CASE("collect_fill_rows: a chart in one database only still gets a row") {
    store::RecordStore old_store(":memory:");
    store::RecordStore new_store(":memory:");

    put(old_store, kOldOnly, 900000, 2, "only-old-path");
    put(new_store, kNewOnly, 800000, 5, "only-new-path");

    std::vector<FillCompareRow> rows = compare(old_store, new_store);
    REQUIRE(rows.size() == 2);  // the union of both key sets

    const FillCompareRow* only_old = find(rows, kOldOnly);
    REQUIRE(only_old != nullptr);
    CHECK(only_old->status == "only 1.0");
    CHECK(only_old->old_score == 900000);
    CHECK_FALSE(only_old->new_score.has_value());
    CHECK_FALSE(only_old->delta.has_value());
    CHECK(only_old->new_path.empty());

    const FillCompareRow* only_new = find(rows, kNewOnly);
    REQUIRE(only_new != nullptr);
    CHECK(only_new->status == "only 1.1");
    CHECK(only_new->new_score == 800000);
    CHECK_FALSE(only_new->old_score.has_value());
    CHECK_FALSE(only_new->delta.has_value());
    CHECK(only_new->old_path.empty());
}

TEST_CASE("tally_fill_rows counts every status") {
    std::vector<FillCompareRow> rows(6);
    rows[0].status = "same";
    rows[1].status = "1.0 higher";
    rows[2].status = "1.0 higher";
    rows[3].status = "1.1 higher";
    rows[4].status = "only 1.0";
    rows[5].status = "only 1.1";

    app::fill_report::FillCompareStats stats =
        app::fill_report::tally_fill_rows(rows);
    CHECK(stats.total == 6);
    CHECK(stats.same == 1);
    CHECK(stats.ch10_higher == 2);
    CHECK(stats.ch11_higher == 1);
    CHECK(stats.only_old == 1);
    CHECK(stats.only_new == 1);
}

TEST_CASE("build_fill_html substitutes every placeholder") {
    store::RecordStore old_store(":memory:");
    store::RecordStore new_store(":memory:");
    put(old_store, kBoth, 1000000, 3, "old-path-C");
    put(new_store, kBoth, 1050000, 4, "new-path-C");

    std::vector<FillCompareRow> rows = compare(old_store, new_store);
    REQUIRE(rows.size() == 1);

    const std::string subtitle = "Subtitle marker 5151";
    const std::string footer = "Footer marker 1515";
    std::string html = app::fill_report::build_fill_html(rows, subtitle, footer);

    CHECK(html.find("__SUBTITLE__") == std::string::npos);
    CHECK(html.find("__FOOTER__") == std::string::npos);
    CHECK(html.find("__DATA__") == std::string::npos);
    CHECK(html.find(subtitle) != std::string::npos);
    CHECK(html.find(footer) != std::string::npos);
    // Both sides' paths reach the page, and the default sort is delta-first.
    CHECK(html.find("old-path-C") != std::string::npos);
    CHECK(html.find("new-path-C") != std::string::npos);
    CHECK(html.find("sortKey: 'delta',") != std::string::npos);
}

TEST_CASE("generate_fill_report: tally and framing behind one seam") {
    store::RecordStore old_store(":memory:");
    store::RecordStore new_store(":memory:");
    put(old_store, kBoth, 1000000, 3, "old-path-D");
    put(new_store, kBoth, 1050000, 4, "new-path-D");   // 1.1 higher
    put(old_store, kOldOnly, 900000, 2, "only-old");   // only 1.0
    put(new_store, kNewOnly, 800000, 5, "only-new");   // only 1.1

    app::fill_report::GeneratedFillReport result =
        app::fill_report::generate_fill_report(old_store, new_store, kMode,
                                               store::CapQuery::at(kCloneHeroSpCap),
                                               store::Lens{});
    CHECK(result.stats.total == 3);
    CHECK(result.stats.ch11_higher == 1);
    CHECK(result.stats.only_old == 1);
    CHECK(result.stats.only_new == 1);
    CHECK(result.stats.same == 0);
    CHECK(result.stats.ch10_higher == 0);
    CHECK_FALSE(result.html.empty());
    CHECK(result.html.find("score higher under 1.1") != std::string::npos);

    // Two empty databases: zero stats, no page.
    store::RecordStore empty_old(":memory:");
    store::RecordStore empty_new(":memory:");
    app::fill_report::GeneratedFillReport none =
        app::fill_report::generate_fill_report(empty_old, empty_new, kMode,
                                               store::CapQuery::at(kCloneHeroSpCap),
                                               store::Lens{});
    CHECK(none.stats.total == 0);
    CHECK(none.html.empty());
}

TEST_CASE("collect_fill_rows: a blank stored song name reads (unknown)") {
    store::RecordStore old_store(":memory:");
    store::RecordStore new_store(":memory:");

    // A blank songmeta name from before the fallback. add_song keeps the latest
    // name it sees, so the blank name goes in after put()'s own "Song aa11".
    put(old_store, kBoth, 1000000, 3, "old-path");
    put(new_store, kBoth, 1000000, 3, "new-path");
    old_store.add_song(kBoth, "", "Test Artist", "Test Charter", sample_chart().song);
    new_store.add_song(kBoth, "", "Test Artist", "Test Charter", sample_chart().song);

    std::vector<FillCompareRow> rows = compare(old_store, new_store);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].song == kUnknownTitle);
}
