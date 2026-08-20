// Structural tests for app/report.{h,cpp}: collect_rows must surface one row
// per stored record, and build_html must substitute every template
// placeholder and embed the rows and the subtitle/footer strings.

#include "doctest.h"

#include <string>
#include <vector>

#include "app/analysis.h"
#include "app/report.h"
#include "corpus_util.h"
#include "store/record_store.h"

using namespace hydra;
using namespace hydra::app;

namespace {

// Analyze the first `want` non-empty corpus charts into a fresh in-memory
// store and return how many records landed.
int fill_store(store::RecordStore& store, bool uncapped, int want) {
    AnalysisSettings settings;
    settings.depth_mode = 0;
    settings.depth_value = 10;
    settings.uncapped = uncapped;
    settings.uncapped_time_budget_s = std::nullopt;

    int added = 0;
    for (const std::string& path : corpus::chart_paths()) {
        if (added == want) break;
        try {
            AnalysisResult result = analyze_chart_file(path, settings);
            if (result.song.is_empty() || result.record.paths.empty()) continue;
            const std::string hyhash = "h" + std::to_string(added);
            store.add_song(hyhash, "Title " + std::to_string(added), "Artist",
                           "Charter", result.song);
            store.add_record(hyhash, "mode", result.record);
            ++added;
        } catch (const std::exception&) {
            continue;
        }
    }
    return added;
}

void check_edition(bool uncapped) {
    store::RecordStore store(":memory:", uncapped);
    const int added = fill_store(store, uncapped, 5);
    REQUIRE(added > 0);

    // One row per shown path, so a record surfaces exactly one rank-1 row.
    std::vector<report::ReportRow> rows =
        report::collect_rows(store, /*max_paths=*/100, uncapped);
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

    MESSAGE((uncapped ? "uncapped" : "capped") << ": " << rows.size()
            << " rows, " << html.size() << " bytes");
}

}  // namespace

TEST_CASE("report page embeds every stored record (capped)") {
    check_edition(false);
}

TEST_CASE("report page embeds every stored record (uncapped)") {
    check_edition(true);
}

TEST_CASE("tier_for: W-derived per-hit bands") {
    using report::tier_for;

    // Default window 85: 1 / 21.25 / 42.5 / 63.75 / 85.
    CHECK(tier_for(std::nullopt).first == "None");
    CHECK(tier_for(0.5).first == "Normal");
    CHECK(tier_for(1.0).first == "Hard");
    CHECK(tier_for(21.24).first == "Hard");
    CHECK(tier_for(21.25).first == "Extreme");
    CHECK(tier_for(42.5).first == "Insane");
    CHECK(tier_for(63.75).first == "Insane+");
    CHECK(tier_for(84.9).first == "Insane+");
    CHECK(tier_for(85.0).first == "Beyond");
    CHECK(tier_for(85.0).second == "t5");

    // A non-default window moves every band except the 1 ms Normal floor.
    CHECK(tier_for(21.25, 70.0).first == "Extreme");
    CHECK(tier_for(17.4, 70.0).first == "Hard");
    CHECK(tier_for(17.5, 70.0).first == "Extreme");
    CHECK(tier_for(70.0, 70.0).first == "Beyond");
    CHECK(tier_for(0.5, 70.0).first == "Normal");
}

TEST_CASE("report payload carries the hit window and the tier table") {
    // No store needed: an empty row list still embeds the metadata.
    std::string html =
        report::build_html({}, "sub", "foot", /*hit_window_ms=*/85.0);
    CHECK(html.find("\"hit_window\":85.0") != std::string::npos);
    CHECK(html.find("\"tiers\":[") != std::string::npos);
    CHECK(html.find("{\"name\":\"Normal\",\"tok\":\"t0\",\"cutoff\":1.0}") !=
          std::string::npos);
    CHECK(html.find("{\"name\":\"Insane+\",\"tok\":\"t4\",\"cutoff\":85.0}") !=
          std::string::npos);
    CHECK(html.find("{\"name\":\"Beyond\",\"tok\":\"t5\",\"cutoff\":null}") !=
          std::string::npos);
    CHECK(html.find("{\"name\":\"None\",\"tok\":\"tn\",\"cutoff\":null}") !=
          std::string::npos);
    CHECK(html.find("\"rows\":[]") != std::string::npos);

    // The dropdown is payload-built; no hardcoded band strings remain.
    CHECK(html.find("Beyond 140ms") == std::string::npos);
}
