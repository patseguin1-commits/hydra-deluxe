// Structural tests for app/report.{h,cpp}: collect_rows must surface one row
// per stored record, and build_html must substitute every template
// placeholder and embed the rows and the subtitle/footer strings.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <map>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/analysis.h"
#include "app/config.h"
#include "app/dm_report.h"
#include "env_util.h"
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
int fill_store(store::RecordStore& store, int cap, int want) {
    AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 10;
    settings.sp_cap = cap;

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
                store::RecordKey{hyhash, "mode", store::CapQuery::at(cap)},
                result.record);
            ++added;
        } catch (const std::exception&) {
            continue;
        }
    }
    return added;
}

void check_cap(int cap) {
    store::RecordStore store(":memory:");
    const int added = fill_store(store, cap, 5);
    REQUIRE(added > 0);

    // One row per shown path, so a record surfaces exactly one rank-1 row.
    const store::CapQuery query = store::CapQuery::at(cap);
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

    MESSAGE(std::to_string(cap) << " bars: " << rows.size()
            << " rows, " << html.size() << " bytes");
}

}  // namespace

TEST_CASE("report page embeds every stored record (4 bars)") { check_cap(4); }

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
    CHECK(four.html.find("1 record across 1 chart") != std::string::npos);
    int rank1 = 0;
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
                store::RecordKey{hyhash, "mode", store::CapQuery::at(settings.sp_cap)},
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
    std::string subtitle = report::counted(result.records, "record", "records") +
                           " across " + report::counted(result.songs, "chart", "charts") +
                           " — top 5 paths per chart";
    CHECK(result.html.find(subtitle) != std::string::npos);
    CHECK(result.html.find("Generated from hydra.db. Timing tiers match") !=
          std::string::npos);
    CHECK(result.html.find("past the 170 ms window") != std::string::npos);

    // --all-paths wording.
    options.max_paths = report::kEveryPathSentinel;
    CHECK(report::generate_report(store, options)
              .html.find(report::counted(result.songs, "chart", "charts") + " — every path") !=
          std::string::npos);

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
    const std::optional<std::string> dir = read_env("HYDRA_PAGE_SAMPLES");
    REQUIRE(dir.has_value());
    const std::filesystem::path out = std::filesystem::u8path(*dir);
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
    add_dm("Song C", 90000, std::nullopt, "not in library", false, std::nullopt);
    add_dm("Song D", 80000, std::nullopt, "not analyzed", false, std::nullopt);

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

// ---- where reports are saved ------------------------------------------------

namespace {

namespace fs = std::filesystem;

// One test's view of the Documents folder. It clears the database override
// (a harness override keeps reports next to its database, which would hide
// the Documents rule), and afterwards puts the overrides and the lookup back
// and deletes its scratch folder.
struct DocumentsSandbox {
    PathOverrides previous = path_overrides();
    fs::path root;

    explicit DocumentsSandbox(const char* tag) {
        PathOverrides cleared = previous;
        cleared.db_path.clear();
        set_path_overrides(cleared);
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        root = fs::path(tmp) / ("hydra_test_docs_" + std::to_string(GetCurrentProcessId())) /
               fs::u8path(tag);
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }

    ~DocumentsSandbox() {
        set_documents_dir_lookup({});
        set_path_overrides(previous);
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

}  // namespace

TEST_CASE("reports_dir is Documents\\Hydra, made on first use") {
    DocumentsSandbox box("made");
    set_documents_dir_lookup([&box] { return std::optional<fs::path>(box.root); });

    const fs::path dir = reports_dir();
    CHECK(dir == box.root / "Hydra");
    CHECK(fs::is_directory(dir));
    // Every report path helper lives in it.
    CHECK(fs::path(report_html_path()) == dir / "hydra_paths.html");
    CHECK(fs::path(dm_report_html_path()) == dir / "hydra_dmcompare.html");
}

TEST_CASE("reports_dir falls back to the database folder without Documents") {
    DocumentsSandbox box("fallback");
    const fs::path db_folder = fs::u8path(db_path()).parent_path();

    // No Documents folder at all.
    set_documents_dir_lookup([] { return std::optional<fs::path>(); });
    CHECK(reports_dir() == db_folder);

    // A Documents folder where "Hydra" can't be made: a plain file is in the way.
    { std::ofstream(box.root / "Hydra") << "not a folder"; }
    set_documents_dir_lookup([&box] { return std::optional<fs::path>(box.root); });
    CHECK(reports_dir() == db_folder);
}

TEST_CASE("reports_dir keeps a harness's reports next to its database") {
    DocumentsSandbox box("override");
    set_documents_dir_lookup([&box] { return std::optional<fs::path>(box.root); });
    PathOverrides scratch = path_overrides();
    scratch.db_path = (box.root / "scratch" / "hydra.db").u8string();
    set_path_overrides(scratch);

    CHECK(reports_dir() == box.root / "scratch");
    CHECK_FALSE(fs::exists(box.root / "Hydra"));  // Documents was never touched
}

// ---- the page shell and the path report -------------------------------------

namespace {

// The one COLS line of a page's script that declares column `key`.
std::string col_line(const std::string& page, const std::string& key) {
    const size_t at = page.find("{k:'" + key + "',");
    if (at == std::string::npos) return {};
    return page.substr(at, page.find('\n', at) - at);
}

size_t occurrences(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

// WCAG 2.2 relative luminance of "#rrggbb".
double luminance(const std::string& hex) {
    auto channel = [&hex](size_t at) {
        const double c = std::stoi(hex.substr(at, 2), nullptr, 16) / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(1) + 0.7152 * channel(3) + 0.0722 * channel(5);
}

double contrast(const std::string& a, const std::string& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// The "--name: #rrggbb" tokens of the first ":root {" block at or after `from`.
std::map<std::string, std::string> css_tokens(const std::string& css, size_t from) {
    std::map<std::string, std::string> out;
    const size_t open = css.find(":root {", from);
    REQUIRE(open != std::string::npos);
    const std::string block = css.substr(open, css.find('}', open) - open);
    size_t at = 0;
    while ((at = block.find("--", at)) != std::string::npos) {
        const size_t colon = block.find(':', at);
        const size_t semi = block.find(';', colon);
        const size_t hash = block.find('#', colon);
        if (hash != std::string::npos && hash < semi && semi - hash == 7)
            out[block.substr(at + 2, colon - at - 2)] = block.substr(hash, 7);
        if (semi == std::string::npos) break;
        at = semi;
    }
    return out;
}

}  // namespace

TEST_CASE("report pages are standards-mode documents") {
    const std::string paths = report::build_html({}, "sub", "foot", 85.0);
    const std::string dm = dm_report::build_dm_html({}, "sub", "foot");
    const std::string fill = fill_report::build_fill_html({}, "sub", "foot");
    for (const std::string* page : {&paths, &dm, &fill}) {
        CHECK(page->rfind("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n", 0) == 0);
        CHECK(page->find("</style>\n</head>\n<body>\n") != std::string::npos);
        REQUIRE(page->size() > 16);
        CHECK(page->substr(page->size() - 16) == "</body>\n</html>\n");
        CHECK(occurrences(*page, "<title>") == 1);
    }
}

TEST_CASE("report colours meet WCAG contrast in both themes") {
    const std::string css = html::kReportCss;
    const std::map<std::string, std::string> light = css_tokens(css, 0);
    const std::map<std::string, std::string> dark =
        css_tokens(css, css.find("prefers-color-scheme: dark"));
    for (const auto* theme : {&light, &dark}) {
        // Body text, dim text, header text and every chip colour, on the page,
        // on a cell, and on a hovered row or the header band.
        for (const char* text : {"ink", "muted", "t0", "t1", "t2", "t3", "t4", "t5", "tn"}) {
            for (const char* ground : {"paper", "surface", "raised"}) {
                INFO(text << " on " << ground);
                CHECK(contrast(theme->at(text), theme->at(ground)) >= 4.5);
            }
        }
        // The accent is the large bold heading word and the focus ring: 3:1.
        CHECK(contrast(theme->at("sp"), theme->at("surface")) >= 3.0);
        CHECK(contrast(theme->at("sp"), theme->at("raised")) >= 3.0);
    }
}

TEST_CASE("report table header sticks and the page prints") {
    const std::string css = html::kReportCss;
    const size_t at = css.find(".tablewrap {");
    REQUIRE(at != std::string::npos);
    const std::string rule = css.substr(at, css.find('}', at) - at);
    // Scrolling both ways makes the wrapper the header's sticky container.
    CHECK(rule.find("overflow: auto;") != std::string::npos);
    CHECK(rule.find("max-height:") != std::string::npos);
    CHECK(rule.find("overflow-x") == std::string::npos);

    CHECK(css.find("@media print {") != std::string::npos);
    CHECK(css.find("thead { display: table-header-group; }") != std::string::npos);
    CHECK(css.find(".controls { display: none; }") != std::string::npos);
}

TEST_CASE("report pages number their rows in a # column") {
    // The shared script adds the column, so all three pages get it.
    const std::string js = html::kReportJs;
    CHECK(js.find("th.textContent = '#';") != std::string::npos);
    CHECK(js.find("idx.textContent = (++n).toLocaleString();") != std::string::npos);
    // The sort control and the arrows walk the real columns, not the # one.
    CHECK(js.find("document.querySelectorAll('#head th.sortable')") != std::string::npos);
    CHECK(std::string(html::kReportCss).find("th.idx, td.idx {") != std::string::npos);
}

TEST_CASE("path report explains and renames its columns") {
    const std::string html = report::build_html({}, "sub", "foot", 85.0);
    CHECK(html.find("t:'Early fill (ms)'") != std::string::npos);
    CHECK(html.find("t:'Avg multiplier'") != std::string::npos);
    CHECK(html.find("t:'Early fill',") == std::string::npos);
    CHECK(html.find("t:'Avg mult',") == std::string::npos);
    for (const char* key : {"mode", "path", "score", "acts", "skip", "ms", "tier", "efill",
                            "mult", "sqin", "sqout", "notes"}) {
        INFO(key);
        CHECK(col_line(html, key).find("d:'") != std::string::npos);
    }
    // Hover text and the footer legend both read the definitions.
    CHECK(html.find("<dl class=\"legend\" id=\"legend\"></dl>") != std::string::npos);
    CHECK(html.find("const legend = document.getElementById('legend');") != std::string::npos);
    // One name per tier for the dropdown and the chip ("No squeezes" in both).
    CHECK(html.find("function tierLabel(name)") != std::string::npos);
    CHECK(html.find("o.textContent = tierLabel(t.name);") != std::string::npos);
    CHECK(html.find("['chip ' + r.tok, tierLabel(r.tier), 'chip']") != std::string::npos);
    // The search box and the tier dropdown have names a screen reader reads.
    CHECK(html.find("id=\"q\" aria-label=\"Search paths\"") != std::string::npos);
    CHECK(html.find("id=\"tier\" aria-label=\"Timing tier\"") != std::string::npos);
}

TEST_CASE("path report counts charts by hash in the tile and the subtitle") {
    report::ReportRow a;
    a.song = "Same";
    a.artist = "Name";
    a.path = "1";
    a.tier = "None";
    a.tok = "tn";
    a.hyhash = "h1";
    report::ReportRow a2 = a;
    a2.rank = 2;
    report::ReportRow b = a;  // another chart with the same title and artist
    b.hyhash = "h2";
    const std::string html = report::build_html({a, a2, b}, "sub", "foot", 85.0);

    // Each chart gets a small id in order of first appearance.
    CHECK(occurrences(html, "\"c\":0") == 2);
    CHECK(occurrences(html, "\"c\":1") == 1);
    CHECK(html.find("['Charts', new Set(rows.map(r => r.c)).size.toLocaleString()]") !=
          std::string::npos);
    CHECK(html.find("r.song + r.artist))") == std::string::npos);
}

TEST_CASE("comparison page explains its columns and splits the missing scores") {
    const std::string html = dm_report::build_dm_html({}, "sub", "foot");
    for (const char* key : {"actual", "optimal", "delta", "pct", "fc", "speed", "rank",
                            "posted", "status"}) {
        INFO(key);
        CHECK(col_line(html, key).find("d:'") != std::string::npos);
    }
    CHECK(html.find("<option value=\"not analyzed\">") != std::string::npos);
    CHECK(html.find("<option value=\"not in library\">") != std::string::npos);
    CHECK(html.find("value=\"unmatched\"") == std::string::npos);
    CHECK(html.find("'not analyzed':'s-notanalyzed'") != std::string::npos);
    CHECK(html.find("id=\"q\" aria-label=\"Search scores\"") != std::string::npos);
    CHECK(html.find("id=\"status\" aria-label=\"Status\"") != std::string::npos);
    CHECK(html.find("<dl class=\"legend\" id=\"legend\"></dl>") != std::string::npos);
}
