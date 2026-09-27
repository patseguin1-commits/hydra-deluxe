// Tests for app/dm_report: the score-vs-optimal join (collect_dm_rows) and
// the comparison page (build_dm_html). Pins the status strings the page's
// filter and chip classes key on, and the four counts the app shows.

#include "doctest.h"

#include <optional>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "core/model.h"
#include "app/dm_report.h"
#include "corpus_util.h"
#include "net/dmbot_client.h"
#include "parse/song.h"
#include "store/record_store.h"

using namespace hydra;
using app::dm_report::DmReportRow;

namespace {

constexpr const char* kMode = "Expert Pro Drums, 2x Bass";
constexpr const char* kHash = "aa11bb22cc33dd44ee55ff6677889900";

// An in-memory store holding one analyzed corpus chart under kHash/kMode.
// Returns the record's best score (the "optimal" side of the join).
int64_t fill_store(store::RecordStore& store, const std::string& name = "Stored Title") {
    app::AnalysisSettings settings;
    settings.depth_value = 0;
    for (const std::string& path : corpus::chart_paths()) {
        try {
            app::AnalysisResult result = app::analyze_chart_file(path, settings);
            if (result.song.is_empty() || result.record.paths.empty()) continue;
            store.add_song(kHash, name, "Stored Artist",
                           "Stored Charter", result.song);
            store.add_record(
                store::RecordKey{kHash, kMode, store::CapQuery::automatic()},
                result.record);
            // A what-if record at 8 bars for the same chart: the comparison
            // must never pick it up (the leaderboard plays at 4 bars).
            HydraRecord whatif = result.record;
            whatif.sp_cap = 8;
            store.add_record(store::RecordKey{kHash, kMode, store::CapQuery::at(8)},
                             whatif);
            return result.record.best_path().totalscore();
        } catch (const std::exception&) {
            continue;
        }
    }
    REQUIRE_MESSAGE(false, "no corpus chart analyzed");
    return 0;
}

net::DmScore make_score(const std::string& identifier, int64_t score) {
    net::DmScore s;
    s.identifier = identifier;
    s.song_name = "Board Title";
    s.artist = "Board Artist";
    s.charter = "Board Charter";
    s.score = score;
    s.is_fc = false;
    s.percent = 100;
    s.speed = 100;
    s.posted = "2026-01-01T00:00:00Z";
    s.known = true;
    return s;
}

}  // namespace

TEST_CASE("collect_dm_rows joins scores to records and labels them") {
    store::RecordStore store(":memory:");
    const int64_t optimal = fill_store(store);
    REQUIRE(optimal > 0);

    std::vector<net::DmScore> scores;
    scores.push_back(make_score(kHash, optimal - 1000));       // matched
    scores.push_back(make_score(kHash, optimal + 5));          // above optimal
    scores.push_back(make_score("00ff00ff00ff00ff00ff00ff00ff00ff",
                                123456));                      // unmatched

    std::vector<DmReportRow> rows =
        app::dm_report::collect_dm_rows(store, scores, kMode, store::Lens{});
    REQUIRE(rows.size() == 3);

    // These exact strings are load-bearing: the page's status filter and chip
    // classes key on them.
    CHECK(rows[0].status == "matched");
    CHECK(rows[1].status == "above optimal");
    CHECK(rows[2].status == "not in library");

    CHECK(rows[0].optimal == optimal);
    CHECK(rows[0].delta == 1000);
    REQUIRE(rows[0].pct.has_value());
    CHECK(*rows[0].pct == doctest::Approx(
        static_cast<double>(optimal - 1000) / static_cast<double>(optimal) * 100.0));

    CHECK(rows[1].delta == -5);
    CHECK_FALSE(rows[2].optimal.has_value());
    CHECK_FALSE(rows[2].delta.has_value());

    // The leaderboard's own metadata wins when it has it.
    CHECK(rows[0].song == "Board Title");
}

TEST_CASE("collect_dm_rows: no pct off 100% speed; store identity fallback") {
    store::RecordStore store(":memory:");
    const int64_t optimal = fill_store(store);

    net::DmScore fast = make_score(kHash, optimal - 10);
    fast.speed = 150;
    net::DmScore unknown_meta = make_score(kHash, optimal - 10);
    unknown_meta.known = false;  // "unknown song" entry: no usable metadata

    std::vector<DmReportRow> rows = app::dm_report::collect_dm_rows(
        store, {fast, unknown_meta}, kMode, store::Lens{});
    REQUIRE(rows.size() == 2);

    CHECK_FALSE(rows[0].pct.has_value());  // speed != 100
    CHECK(rows[1].song == "Stored Title");  // fell back to the matched record
}

TEST_CASE("dmbot JSON parsers handle canned payloads") {
    // The users endpoint: elo/stats may be null or missing; entries without
    // an id are dropped.
    const std::string users_body = R"([
        {"id":"111","username":"alice","elo":1500,
         "stats":{"total_scores":10,"total_score":123456}},
        {"id":"222","username":"bob","elo":null},
        {"username":"no_id_dropped"}
    ])";
    std::vector<net::DmUser> users = net::parse_users_json(users_body);
    REQUIRE(users.size() == 2);
    CHECK(users[0].id == "111");
    CHECK(users[0].username == "alice");
    CHECK(users[0].elo == 1500);
    CHECK(users[0].total_scores == 10);
    CHECK(users[0].total_score == 123456);
    CHECK_FALSE(users[1].elo.has_value());

    // The scores endpoint: two arrays, flattened and told apart by `known`;
    // identifiers are lowercased; charter_refs join with ", "; missing speed
    // defaults to 100.
    const std::string scores_body = R"({
        "scores":[
            {"identifier":"AABB01","song_name":"Song","artist":"Artist",
             "charter_refs":["c1","c2"],"score":1000,"is_fc":1,"percent":98,
             "speed":150,"rank":3,"posted":"2026-01-01"}
        ],
        "unknown_scores":[
            {"identifier":"ccdd02","score":500}
        ]
    })";
    std::vector<net::DmScore> scores = net::parse_scores_json(scores_body);
    REQUIRE(scores.size() == 2);
    CHECK(scores[0].identifier == "aabb01");
    CHECK(scores[0].charter == "c1, c2");
    CHECK(scores[0].score == 1000);
    CHECK(scores[0].is_fc);
    CHECK(scores[0].speed == 150);
    CHECK(scores[0].rank == 3);
    CHECK(scores[0].known);
    CHECK(scores[1].known == false);
    CHECK(scores[1].speed == 100);

    // Unparseable bodies throw the user-facing error, not a JSON exception.
    CHECK_THROWS_AS(net::parse_users_json("not json"), std::runtime_error);
    CHECK_THROWS_AS(net::parse_scores_json("<html>503</html>"), std::runtime_error);

    // A non-array user body is rejected.
    CHECK_THROWS_AS(net::parse_users_json("{\"a\":1}"), std::runtime_error);
}

TEST_CASE("build_dm_html substitutes every placeholder") {
    store::RecordStore store(":memory:");
    const int64_t optimal = fill_store(store);

    std::vector<DmReportRow> rows = app::dm_report::collect_dm_rows(
        store, {make_score(kHash, optimal - 1)}, kMode, store::Lens{});
    REQUIRE(rows.size() == 1);

    const std::string subtitle = "Subtitle marker 5151";
    const std::string footer = "Footer marker 1515";
    std::string html = app::dm_report::build_dm_html(rows, subtitle, footer);

    CHECK(html.find("__SUBTITLE__") == std::string::npos);
    CHECK(html.find("__FOOTER__") == std::string::npos);
    CHECK(html.find("__DATA__") == std::string::npos);
    CHECK(html.find(subtitle) != std::string::npos);
    CHECK(html.find(footer) != std::string::npos);
    CHECK(html.find("Board Title") != std::string::npos);
}

TEST_CASE("generate_dm_report: tally and framing behind one seam") {
    store::RecordStore store(":memory:");
    const int64_t optimal = fill_store(store);
    REQUIRE(optimal > 0);

    std::vector<net::DmScore> scores;
    scores.push_back(make_score(kHash, optimal - 1000));  // matched
    scores.push_back(make_score(kHash, optimal + 5));     // above optimal
    scores.push_back(make_score("00ff00ff00ff00ff00ff00ff00ff00ff",
                                123456));                 // unmatched

    app::dm_report::GeneratedDmReport result =
        app::dm_report::generate_dm_report(store, scores, kMode, store::Lens{}, "TestUser");
    CHECK(result.stats.total == 3);
    CHECK(result.stats.matched == 1);
    CHECK(result.stats.above_optimal == 1);
    CHECK(result.stats.not_analyzed == 0);
    CHECK(result.stats.not_in_library == 1);

    // The subtitle the finished modal's counts must agree with.
    CHECK(result.html.find("TestUser — 3 scores: 1 matched, 1 above optimal, "
                           "0 not analyzed, 1 not in your library") != std::string::npos);
    // (The apostrophe in "Hydra's" is HTML-escaped, so match up to it.)
    CHECK(result.html.find(
              "Actual scores from dmleaderboards.com against Hydra") !=
          std::string::npos);

    // No scores: zero stats, no page.
    app::dm_report::GeneratedDmReport none =
        app::dm_report::generate_dm_report(store, {}, kMode, store::Lens{}, "TestUser");
    CHECK(none.stats.total == 0);
    CHECK(none.html.empty());
}

TEST_CASE("collect_dm_rows: a blank stored song name reads (unknown)") {
    store::RecordStore store(":memory:");
    const int64_t optimal = fill_store(store, "");

    // The leaderboard has no metadata for it, so the row falls back to the
    // matched record, whose stored name is blank.
    net::DmScore unknown_meta = make_score(kHash, optimal - 10);
    unknown_meta.known = false;

    std::vector<DmReportRow> rows = app::dm_report::collect_dm_rows(
        store, {unknown_meta}, kMode, store::Lens{});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].song == kUnknownTitle);
}

TEST_CASE("collect_dm_rows tells not analyzed from not in library") {
    store::RecordStore store(":memory:");
    const int64_t optimal = fill_store(store);
    REQUIRE(optimal > 0);

    // The last scan found kHash and one more chart nobody has analyzed. The
    // scanned hash is upper case on purpose: the join ignores case.
    constexpr const char* kScannedUpper = "ABCDEF00112233445566778899AABBCC";
    constexpr const char* kScanned = "abcdef00112233445566778899aabbcc";
    store::ChartLibraryEntry analyzed;
    analyzed.md5 = kHash;
    analyzed.title = "Stored Title";
    store::ChartLibraryEntry scanned;
    scanned.md5 = kScannedUpper;
    scanned.title = "Scanned Only";
    store.rebuild_chart_library({analyzed, scanned});

    std::vector<DmReportRow> rows = app::dm_report::collect_dm_rows(
        store,
        {make_score(kHash, optimal - 1000),                        // matched
         make_score(kScanned, 5000),                               // in the library, no result
         make_score("00ff00ff00ff00ff00ff00ff00ff00ff", 123456)},  // never scanned
        kMode, store::Lens{});
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].status == "matched");
    CHECK(rows[1].status == "not analyzed");
    CHECK(rows[2].status == "not in library");
    CHECK_FALSE(rows[1].optimal.has_value());

    const app::dm_report::DmReportStats stats = app::dm_report::tally_dm_rows(rows);
    CHECK(stats.total == 3);
    CHECK(stats.matched == 1);
    CHECK(stats.above_optimal == 0);
    CHECK(stats.not_analyzed == 1);
    CHECK(stats.not_in_library == 1);
    // The old names, kept filled until ui/dm_jobs.cpp reads the new ones.
    CHECK(stats.above == 0);
    CHECK(stats.unmatched == 2);
}
