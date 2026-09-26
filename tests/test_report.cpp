// Structural tests for app/report.{h,cpp}: collect_rows must surface one row
// per stored record, and build_html must substitute every template
// placeholder and embed the rows and the subtitle/footer strings.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/analysis.h"
#include "app/dm_report.h"
#include "app/fill_report.h"
#include "app/html_page.h"
#include "app/report.h"
#include "app/report_files.h"
#include "core/squeeze_rating.h"
#include "core/winstr.h"
#include "corpus_util.h"
#include "store/record_store.h"

using namespace hydra;
using namespace hydra::app;

namespace {

// Analyze the first `want` non-empty corpus charts into a fresh in-memory
// store and return how many records landed.
int fill_store(store::RecordStore& store, std::optional<int> cap, int want) {
    AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 10;
    settings.sp_cap = cap;
    settings.rules.auto_budget_s = std::nullopt;

    int added = 0;
    for (const std::string& path : corpus::chart_paths()) {
        if (added == want) break;
        try {
            AnalysisResult result = analyze_chart_file(path, settings);
            if (result.song.is_empty() || result.record.paths.empty()) continue;
            const std::string hyhash = "h" + std::to_string(added);
            store.add_song(hyhash, "Title " + std::to_string(added), "Artist",
                           "Charter", result.song);
            store.add_record(
                store::RecordKey{hyhash, "mode", store::CapQuery::from_setting(cap)},
                result.record);
            ++added;
        } catch (const std::exception&) {
            continue;
        }
    }
    return added;
}

void check_cap(std::optional<int> cap) {
    store::RecordStore store(":memory:");
    const int added = fill_store(store, cap, 5);
    REQUIRE(added > 0);

    // One row per shown path, so a record surfaces exactly one rank-1 row.
    const store::CapQuery query = cap ? store::CapQuery::at(*cap) : store::CapQuery::automatic();
    std::vector<report::ReportRow> rows =
        report::collect_rows(store, /*max_paths=*/100, query, store::Lens{});
    int rank1 = 0;
    for (const report::ReportRow& row : rows)
        if (row.rank == 1) ++rank1;
    CHECK(rank1 == added);
    CHECK(static_cast<int>(rows.size()) >= added);

    const std::string subtitle = "Subtitle marker 4242";
    const std::string footer = "Footer marker 2424";
    std::string html = report::build_html(rows, subtitle, footer);

    // Every placeholder is substituted, and the substituted content is there.
    CHECK(html.find("__SUBTITLE__") == std::string::npos);
    CHECK(html.find("__FOOTER__") == std::string::npos);
    CHECK(html.find("__DATA__") == std::string::npos);
    CHECK(html.find(subtitle) != std::string::npos);
    CHECK(html.find(footer) != std::string::npos);
    for (int i = 0; i < added; ++i)
        CHECK(html.find("Title " + std::to_string(i)) != std::string::npos);

    MESSAGE((cap ? std::to_string(*cap) + " bars" : "auto") << ": " << rows.size()
            << " rows, " << html.size() << " bytes");
}

}  // namespace

TEST_CASE("report page embeds every stored record (4 bars)") { check_cap(4); }

TEST_CASE("report page embeds every stored record (Auto)") { check_cap(std::nullopt); }

TEST_CASE("report lists only the wanted cap and names it") {
    store::RecordStore store(":memory:");
    REQUIRE(fill_store(store, 4, 1) == 1);
    // The same chart again at 8 bars, under the same key.
    AnalysisSettings settings;
    settings.depth_value = 10;
    settings.sp_cap = 8;
    for (const std::string& path : corpus::chart_paths()) {
        try {
            AnalysisResult result = analyze_chart_file(path, settings);
            if (result.song.is_empty() || result.record.paths.empty()) continue;
            store.add_record(store::RecordKey{"h0", "mode", store::CapQuery::at(8)},
                             result.record);
            break;
        } catch (const std::exception&) {
            continue;
        }
    }
    REQUIRE(store.counts().second == 2);

    report::ReportOptions options;
    options.cap = store::CapQuery::at(4);
    report::GeneratedReport four = report::generate_report(store, options);
    CHECK(four.html.find("SP cap 4 bars") != std::string::npos);
    // The subtitle counts what the page lists: the one record at 4 bars, not
    // the 8-bar record the database also holds for the same chart.
    CHECK(four.records == 1);
    CHECK(four.songs == 1);
    CHECK(four.html.find("1 records across 1 songs") != std::string::npos);
    int rank1 = 0;
    for (const report::ReportRow& row : report::collect_rows(store, 100, options.cap, options.lens))
        if (row.rank == 1) ++rank1;
    CHECK(rank1 == 1);

    options.cap = store::CapQuery::automatic();
    report::GeneratedReport automatic = report::generate_report(store, options);
    CHECK(automatic.html.find("SP cap Auto") != std::string::npos);
    rank1 = 0;
    for (const report::ReportRow& row : report::collect_rows(store, 100, options.cap, options.lens))
        if (row.rank == 1) ++rank1;
    CHECK(rank1 == 1);
}

TEST_CASE("collect_rows: a blank or old-placeholder song name reads (unknown)") {
    store::RecordStore store(":memory:");
    AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 10;
    settings.sp_cap = 4;
    settings.rules.auto_budget_s = std::nullopt;

    // songmeta names written before the fallback existed.
    const std::vector<std::string> stored_names = {"", "<unknown title>"};
    size_t added = 0;
    for (const std::string& path : corpus::chart_paths()) {
        if (added == stored_names.size()) break;
        try {
            AnalysisResult result = analyze_chart_file(path, settings);
            if (result.song.is_empty() || result.record.paths.empty()) continue;
            const std::string hyhash = "u" + std::to_string(added);
            store.add_song(hyhash, stored_names[added], "Artist", "Charter", result.song);
            store.add_record(
                store::RecordKey{hyhash, "mode", store::CapQuery::from_setting(settings.sp_cap)},
                result.record);
            ++added;
        } catch (const std::exception&) {
            continue;
        }
    }
    REQUIRE(added == stored_names.size());

    std::vector<report::ReportRow> rows =
        report::collect_rows(store, /*max_paths=*/100, store::CapQuery::at(4), store::Lens{});
    REQUIRE(!rows.empty());
    for (const report::ReportRow& row : rows) CHECK(row.song == kUnknownTitle);
}

TEST_CASE("tier_for: raw-ms bands derived from the two-hit budget") {
    using report::tier_for;

    // Default window 85 -> budget 170: bands 2 / 42.5 / 85 / 127.5 / 170.
    CHECK(tier_for(std::nullopt).first == "None");
    CHECK(tier_for(1.9).first == "Normal");
    CHECK(tier_for(2.0).first == "Hard");
    CHECK(tier_for(42.4).first == "Hard");
    CHECK(tier_for(42.5).first == "Extreme");
    CHECK(tier_for(85.0).first == "Insane");
    CHECK(tier_for(127.5).first == "Insane+");
    CHECK(tier_for(169.9).first == "Insane+");
    CHECK(tier_for(170.0).first == "Beyond");
    CHECK(tier_for(170.0).second == "t5");

    // At the historical 70 ms window the original 2/35/70/105/140 ladder
    // reproduces exactly.
    CHECK(tier_for(1.9, 70.0).first == "Normal");
    CHECK(tier_for(34.9, 70.0).first == "Hard");
    CHECK(tier_for(35.0, 70.0).first == "Extreme");
    CHECK(tier_for(70.0, 70.0).first == "Insane");
    CHECK(tier_for(105.0, 70.0).first == "Insane+");
    CHECK(tier_for(140.0, 70.0).first == "Beyond");
}

TEST_CASE("tier_for walks the timing_tiers table edge by edge") {
    // tier_for and the page's embedded tier table read the one ladder in
    // core/squeeze_rating.h, so every banded entry's cutoff is exactly where
    // the label flips to the next entry's.
    std::vector<TimingTier> tiers = timing_tiers(85.0);
    REQUIRE(tiers.size() >= 2);
    for (size_t i = 0; i + 1 < tiers.size(); ++i) {
        if (!tiers[i].cutoff) continue;
        const double cutoff = *tiers[i].cutoff;
        CHECK(report::tier_for(cutoff - 0.01, 85.0).first == tiers[i].name);
        CHECK(report::tier_for(cutoff, 85.0).first == tiers[i + 1].name);
    }
}

TEST_CASE("report payload carries the hit window and the tier table") {
    // No store needed: an empty row list still embeds the metadata.
    std::string html =
        report::build_html({}, "sub", "foot", /*hit_window_ms=*/85.0);
    CHECK(html.find("\"hit_window\":85.0") != std::string::npos);
    CHECK(html.find("\"tiers\":[") != std::string::npos);
    CHECK(html.find("{\"name\":\"Normal\",\"tok\":\"t0\",\"cutoff\":2.0}") !=
          std::string::npos);
    CHECK(html.find("{\"name\":\"Insane+\",\"tok\":\"t4\",\"cutoff\":170.0}") !=
          std::string::npos);
    CHECK(html.find("{\"name\":\"Beyond\",\"tok\":\"t5\",\"cutoff\":null}") !=
          std::string::npos);
    CHECK(html.find("{\"name\":\"None\",\"tok\":\"tn\",\"cutoff\":null}") !=
          std::string::npos);
    CHECK(html.find("\"rows\":[]") != std::string::npos);

    // The dropdown is payload-built; no hardcoded band strings remain.
    CHECK(html.find("Beyond 140ms") == std::string::npos);
}

TEST_CASE("report page reads the Beyond edge from the tier table") {
    std::string html = report::build_html({}, "sub", "foot", /*hit_window_ms=*/85.0);
    CHECK(html.find("const BEYOND = Math.max(") != std::string::npos);
    CHECK(html.find("HIT_WINDOW * 2") == std::string::npos);
    CHECK(html.find("'Past ' + BEYOND + ' ms'") != std::string::npos);
}


TEST_CASE("generate_report: one seam frames the page for every entry point") {
    store::RecordStore store(":memory:");
    const int added = fill_store(store, 4, 2);
    REQUIRE(added > 0);

    report::ReportOptions options;
    options.max_paths = report::kDefaultReportPaths;
    options.db_path = "C:/somewhere/hydra.db";
    report::GeneratedReport result = report::generate_report(store, options);
    CHECK(result.songs == added);
    CHECK(result.records == added);
    CHECK(result.rows >= added);

    // The framing strings are part of the interface: the CLI and the GUI's
    // ReportJob both ship exactly this subtitle and footer.
    std::string subtitle = group_thousands(result.records) +
                           " records across " + group_thousands(result.songs) +
                           " songs — top 5 paths per chart";
    CHECK(result.html.find(subtitle) != std::string::npos);
    CHECK(result.html.find("Generated from hydra.db. Timing tiers match") !=
          std::string::npos);
    CHECK(result.html.find("past the 170 ms window") != std::string::npos);

    // --all-paths wording.
    options.max_paths = report::kEveryPathSentinel;
    CHECK(report::generate_report(store, options)
              .html.find("songs — every path") != std::string::npos);

    // An empty store yields counts but no page.
    store::RecordStore empty(":memory:");
    report::GeneratedReport none = report::generate_report(empty, options);
    CHECK(none.rows == 0);
    CHECK(none.html.empty());
}

TEST_CASE("generate_report hands back nothing when its cancel flag is set") {
    // Closing Hydra while the report builds. The walk stops between records,
    // and no page is framed from the part of the library it managed to read.
    store::RecordStore store(":memory:");
    REQUIRE(fill_store(store, 4, 2) > 0);

    std::atomic<bool> cancel{true};
    report::ReportOptions options;
    options.max_paths = 5;
    options.db_path = "C:/somewhere/hydra.db";
    options.cancel = &cancel;

    report::GeneratedReport result = report::generate_report(store, options);
    CHECK(result.rows == 0);
    CHECK(result.html.empty());
}

TEST_CASE("write_report_file swaps the page in and leaves no .tmp behind") {
    // Same temp-path recipe as tests/test_store.cpp's temp_db: the Windows
    // temp directory, tagged and pid-suffixed so parallel test runs don't
    // collide.
    wchar_t tmp_dir[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp_dir);
    const std::string path = hydra::wide_to_utf8(tmp_dir) + "hydra_test_report_file_" +
                              std::to_string(GetCurrentProcessId()) + ".html";

    hydra::app::write_report_file(path, "<html>old</html>");
    hydra::app::write_report_file(path, "<html>new</html>");

    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    CHECK(contents.str() == "<html>new</html>");

    CHECK_FALSE(std::filesystem::exists(path + ".tmp"));

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("path report shows each row's chart mode in its own column") {
    report::ReportRow a;
    a.song = "Song A";
    a.mode = "Expert Pro Drums, 2x Bass";
    a.path = "1";
    a.tier = "None";
    a.tok = "tn";
    report::ReportRow b = a;
    b.mode = "Hard Drums, 1x Bass";
    const std::string html = report::build_html({a, b}, "sub", "foot", 85.0);

    CHECK(html.find("{k:'mode',") != std::string::npos);
    CHECK(html.find("t:'Mode'") != std::string::npos);
    CHECK(html.find("['dim trunc mode', r.mode]") != std::string::npos);
    CHECK(html.find("\"mode\":\"Expert Pro Drums, 2x Bass\"") != std::string::npos);
    CHECK(html.find("\"mode\":\"Hard Drums, 1x Bass\"") != std::string::npos);
    // The page never read each row's gap to the best path, so the payload no
    // longer carries it.
    CHECK(html.find("\"delta\":") == std::string::npos);
}

TEST_CASE("the three report pages share one stylesheet and one script") {
    const std::string paths = report::build_html({}, "sub", "foot", 85.0);
    const std::string dm = dm_report::build_dm_html({}, "sub", "foot");
    const std::string fill = fill_report::build_fill_html({}, "sub", "foot");
    for (const std::string* page : {&paths, &dm, &fill}) {
        CHECK(page->find(html::kReportCss) != std::string::npos);
        CHECK(page->find(html::kReportJs) != std::string::npos);
        // Rules no page used, and the theme switch nothing ever sets, are gone.
        CHECK(page->find(".delta {") == std::string::npos);
        CHECK(page->find(".rank {") == std::string::npos);
        CHECK(page->find("data-theme") == std::string::npos);
    }
    CHECK(paths.find("<div class=\"wrap\">") != std::string::npos);
    CHECK(dm.find("<div class=\"wrap dm\">") != std::string::npos);
    CHECK(fill.find("<div class=\"wrap fill\">") != std::string::npos);
}

TEST_CASE("tier_for over a built table matches the window form") {
    const std::vector<TimingTier> tiers = timing_tiers(85.0);
    const std::vector<std::optional<double>> samples = {
        std::nullopt, 0.0, 1.9, 2.0, 42.5, 127.4, 169.9, 170.0, 500.0};
    for (const std::optional<double>& ms : samples)
        CHECK(report::tier_for(ms, tiers) == report::tier_for(ms, 85.0));
}

TEST_CASE("records_by_hash keys every listed record by its lower-case hash") {
    store::RecordStore store(":memory:");
    AnalysisSettings settings;
    settings.depth_value = 0;
    settings.rules.auto_budget_s = std::nullopt;
    bool added = false;
    for (const std::string& path : corpus::chart_paths()) {
        try {
            AnalysisResult result = analyze_chart_file(path, settings);
            if (result.song.is_empty() || result.record.paths.empty()) continue;
            store.add_song("ABCDEF0123", "Title", "Artist", "Charter", result.song);
            store.add_record(
                store::RecordKey{"ABCDEF0123", "mode", store::CapQuery::at(4)},
                result.record);
            added = true;
            break;
        } catch (const std::exception&) {
            continue;
        }
    }
    REQUIRE(added);

    const std::unordered_map<std::string, store::RecordListing> by_hash =
        report::records_by_hash(store, "mode", store::CapQuery::at(4), store::Lens{});
    REQUIRE(by_hash.size() == 1);
    REQUIRE(by_hash.count("abcdef0123") == 1);
    CHECK(by_hash.at("abcdef0123").hyhash == "ABCDEF0123");
    CHECK(report::records_by_hash(store, "other mode", store::CapQuery::at(4),
                                  store::Lens{})
              .empty());
}

// Not an invariant: writes one small page of each kind into the folder named
// by HYDRA_PAGE_SAMPLES, built from fixed rows, so a page change can be
// checked in a real browser before and after (docs/adr/0016). Run it with
//   hydra_tests.exe --no-skip -tc="report pages: write samples*"
TEST_CASE("report pages: write samples for the browser check" * doctest::skip()) {
    const char* dir = std::getenv("HYDRA_PAGE_SAMPLES");
    REQUIRE(dir != nullptr);
    const std::filesystem::path out = std::filesystem::u8path(dir);
    std::filesystem::create_directories(out);

    std::vector<report::ReportRow> paths;
    auto add_path = [&](const std::string& song, const char* mode, int rank,
                        const std::string& path, int64_t score, std::optional<double> ms,
                        std::optional<double> efill) {
        report::ReportRow r;
        r.song = song;
        r.artist = "Artist of " + song;
        r.charter = "Charter & Co";
        r.mode = mode;
        r.rank = rank;
        r.path = path;
        r.score = score;
        r.acts = 3 + rank;
        r.skip = rank - 1;
        r.ms = ms;
        auto [tier, tok] = report::tier_for(ms, 85.0);
        r.tier = tier;
        r.tok = tok;
        r.efill = efill;
        r.mult = 2.345 + rank;
        r.sqin = rank;
        r.sqout = 2 - rank % 2;
        r.notes = 1200 + rank;
        paths.push_back(r);
    };
    std::string long_path;
    for (int i = 0; i < 80; ++i) long_path += "1-E2+ ";
    add_path("Song A", "Expert Pro Drums, 2x Bass", 1, "1-E2+ 0-E1", 123456, 12.5, -3.25);
    add_path("Song A", "Expert Pro Drums, 2x Bass", 2, "1-E2 0-E1-", 123000, 48.0, std::nullopt);
    add_path("Song A", "Hard Drums, 1x Bass", 1, "0 0 1", 98000, std::nullopt, std::nullopt);
    add_path("Song B", "Expert Pro Drums, 2x Bass", 1, long_path, 250000, 171.0, 4.5);
    add_path("Song B", "Expert Pro Drums, 2x Bass", 2, "2 1-E3", 249500, 1.5, 0.0);
    add_path("Song C", "Expert Drums, 1x Bass", 1, "1 1 1", 77000, 90.0, std::nullopt);

    std::vector<dm_report::DmReportRow> dm;
    auto add_dm = [&](const char* song, int64_t actual, std::optional<int64_t> optimal,
                      const char* status, bool fc, std::optional<int> rank) {
        dm_report::DmReportRow r;
        r.song = song;
        r.artist = "Artist";
        r.charter = "Charter";
        r.identifier = "hash";
        r.actual = actual;
        r.optimal = optimal;
        if (optimal) r.delta = *optimal - actual;
        if (optimal && *optimal > 0)
            r.pct = static_cast<double>(actual) / static_cast<double>(*optimal) * 100.0;
        r.is_fc = fc;
        r.percent = fc ? 100 : 97;
        r.speed = 100;
        r.rank = rank;
        r.posted = "2026-09-20T12:34:56Z";
        r.status = status;
        dm.push_back(r);
    };
    add_dm("Song A", 120000, 123456, "matched", true, 3);
    add_dm("Song B", 251000, 250000, "above optimal", false, 1);
    add_dm("Song C", 90000, std::nullopt, "unmatched", false, std::nullopt);

    std::vector<fill_report::FillCompareRow> fill;
    auto add_fill = [&](const char* song, std::optional<int64_t> old_score,
                        std::optional<int64_t> new_score, const char* status) {
        fill_report::FillCompareRow r;
        r.song = song;
        r.artist = "Artist";
        r.charter = "Charter";
        r.hyhash = song;
        r.old_score = old_score;
        r.new_score = new_score;
        if (old_score && new_score) r.delta = *new_score - *old_score;
        if (old_score) { r.old_path = "1-E2 0"; r.old_acts = 2; }
        if (new_score) { r.new_path = "1-E2 0-E1"; r.new_acts = 3; }
        r.notes = 900;
        r.status = status;
        fill.push_back(r);
    };
    add_fill("Song A", 100000, 100500, "1.1 higher");
    add_fill("Song B", 100000, 99000, "1.0 higher");
    add_fill("Song C", 100000, 100000, "same");
    add_fill("Song D", 100000, std::nullopt, "only 1.0");
    add_fill("Song E", std::nullopt, 100000, "only 1.1");

    write_report_file(out / "paths.html",
                      report::build_html(paths, "Sample subtitle", "Sample footer", 85.0));
    write_report_file(out / "dm.html",
                      dm_report::build_dm_html(dm, "Sample subtitle", "Sample footer"));
    write_report_file(out / "fill.html",
                      fill_report::build_fill_html(fill, "Sample subtitle", "Sample footer"));
}
