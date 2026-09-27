// Tests for app/dm_report: the score-vs-optimal join (collect_dm_rows) and
// the comparison page (build_dm_html). Pins the status strings the page's
// filter and chip classes key on, and the four counts the app shows. Also
// drives the real WinHTTP transport against a loopback server.

// winsock2.h has to come before anything that pulls in windows.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
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
                store::RecordKey{kHash, kMode, store::CapQuery::at(kCloneHeroSpCap)},
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

namespace {

// A one-shot HTTP server on 127.0.0.1 for the real WinHTTP transport (the
// Fetcher seam swaps http_get out wholesale, so it can't reach that code).
// With an empty `reply` nothing ever accepts: the kernel completes the TCP
// handshake from the listen backlog, WinHTTP sends its request, and no answer
// comes, like a render.com cold start. Otherwise one thread accepts one
// connection, reads the request headers and sends `reply`.
class LoopbackServer {
public:
    explicit LoopbackServer(std::string reply) : reply_(std::move(reply)) {
        WSADATA wsa;
        REQUIRE(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(listener_ != INVALID_SOCKET);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;  // any free port
        REQUIRE(bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(listen(listener_, 4) == 0);
        int len = sizeof(addr);
        REQUIRE(getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);
        if (!reply_.empty()) thread_ = std::thread([this] { serve_one(); });
    }
    ~LoopbackServer() {
        closesocket(listener_);  // also unblocks an accept() still waiting
        if (thread_.joinable()) thread_.join();
        WSACleanup();
    }
    LoopbackServer(const LoopbackServer&) = delete;
    LoopbackServer& operator=(const LoopbackServer&) = delete;

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }

private:
    void serve_one() {
        SOCKET s = accept(listener_, nullptr, nullptr);
        if (s == INVALID_SOCKET) return;
        std::string request;
        char buf[4096];
        while (request.find("\r\n\r\n") == std::string::npos) {
            int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0) break;
            request.append(buf, static_cast<size_t>(n));
        }
        size_t sent = 0;
        while (sent < reply_.size()) {
            int n = send(s, reply_.data() + sent,
                         static_cast<int>(std::min<size_t>(reply_.size() - sent, 1 << 16)), 0);
            if (n <= 0) break;
            sent += static_cast<size_t>(n);
        }
        // Graceful close: FIN, then wait for the client to hang up, so no
        // unread bytes turn the close into a reset that drops the reply.
        shutdown(s, SD_SEND);
        while (recv(s, buf, sizeof(buf), 0) > 0) {}
        closesocket(s);
    }

    std::string reply_;
    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::thread thread_;
};

std::string http_response(const std::string& status_line, const std::string& body) {
    return "HTTP/1.1 " + status_line +
           "\r\nContent-Type: application/json\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

}  // namespace

TEST_CASE("http transport: cancel aborts a request the server never answers") {
    net::set_fetcher({});  // the real WinHTTP transport
    LoopbackServer server("");

    // Set before the call: fails at once.
    std::atomic<bool> cancel{true};
    CHECK_THROWS_WITH_AS((void)net::fetch_users(server.url(), &cancel), "cancelled",
                         std::runtime_error);

    // Set while WinHTTP waits on a response that never comes (the receive
    // timeout is 120 s, so only the cancel can end this promptly).
    cancel = false;
    using clock = std::chrono::steady_clock;
    clock::time_point cancel_at;
    std::thread setter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        cancel_at = clock::now();
        cancel = true;
    });
    std::string error;
    try {
        (void)net::fetch_users(server.url(), &cancel);
    } catch (const std::runtime_error& e) {
        error = e.what();
    }
    const clock::time_point returned = clock::now();
    setter.join();

    CHECK(error == "cancelled");
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(returned - cancel_at).count();
    MESSAGE("fetch returned " << ms << " ms after the cancel flag was set");
    CHECK(ms >= 0);
    CHECK(ms < 500);  // ~50 ms expected; slack for a loaded machine
}

TEST_CASE("http transport: a 200 reply is read in full, a 404 keeps its message") {
    net::set_fetcher({});  // the real WinHTTP transport

    // Big enough (~250 KB) to arrive over several read chunks.
    std::string body = "[";
    for (int i = 0; i < 5000; ++i) {
        if (i) body += ",";
        body += R"({"id":")" + std::to_string(i) + R"(","username":"user)" +
                std::to_string(i) + R"(","elo":1500})";
    }
    body += "]";
    {
        LoopbackServer server(http_response("200 OK", body));
        std::atomic<bool> cancel{false};  // present but never set
        std::vector<net::DmUser> users = net::fetch_users(server.url(), &cancel);
        REQUIRE(users.size() == 5000);
        CHECK(users.front().id == "0");
        CHECK(users.back().username == "user4999");
    }
    {
        LoopbackServer server(http_response("404 Not Found", ""));
        CHECK_THROWS_WITH_AS((void)net::fetch_users(server.url()),
                             "leaderboard returned HTTP 404", std::runtime_error);
    }
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
}
