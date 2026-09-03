// Structural tests for app/report.{h,cpp}: collect_rows must surface one row
// per stored record, and build_html must substitute every template
// placeholder and embed the rows and the subtitle/footer strings.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/analysis.h"
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
    settings.time_budget_s = std::nullopt;

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


TEST_CASE("generate_report: one seam frames the page for every entry point") {
    store::RecordStore store(":memory:");
    const int added = fill_store(store, 4, 2);
    REQUIRE(added > 0);

    report::ReportOptions options;
    options.max_paths = 5;
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
    options.max_paths = 1000000000;
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
