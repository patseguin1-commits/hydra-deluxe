// Parity test for app/report.{h,cpp} — the Phase 6 gate: the HTML page must
// be byte-identical to the Python hydra_report.py output frozen in
// golden/report.html (and report_uncapped.html), for the same charts analyzed
// natively and stored/read back through this port's own RecordStore.
//
// Golden inputs come from tools/gen_golden_report.py: report_meta.json pins
// the chart list (insertion order), the analysis config, and the
// subtitle/footer strings, so the diff isolates collect_rows/build_html.

#include "doctest.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/analysis.h"
#include "app/report.h"
#include "golden_util.h"
#include "store/record_store.h"

using namespace hydra;
using namespace hydra::app;

namespace {

std::string read_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(bool(f), "cannot open " << path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// notespath -> relpath under test/input, forward slashes, matching the
// gen_golden_report.py keying.
std::string relpath_of(const std::string& notespath) {
    std::string root = HYDRA_INPUT_DIR;
    std::string rel = notespath;
    if (rel.size() > root.size() && rel.compare(0, root.size(), root) == 0)
        rel = rel.substr(root.size() + 1);
    for (char& c : rel)
        if (c == '\\') c = '/';
    return rel;
}

// Where the first byte difference is, with context — a raw CHECK on two
// ~megabyte strings is useless when it fails.
void check_same_bytes(const std::string& actual, const std::string& expected,
                      const char* label) {
    if (actual == expected) {
        CHECK(true);
        return;
    }
    size_t n = std::min(actual.size(), expected.size());
    size_t at = n;
    for (size_t i = 0; i < n; ++i) {
        if (actual[i] != expected[i]) {
            at = i;
            break;
        }
    }
    size_t from = at > 60 ? at - 60 : 0;
    FAIL_CHECK(label << ": first difference at byte " << at << " (actual size "
                     << actual.size() << ", expected " << expected.size() << ")\n  golden: ..."
                     << expected.substr(from, 120) << "...\n  actual: ..."
                     << actual.substr(from, 120) << "...");
}

void run_edition(const golden::json& meta, bool uncapped, const char* html_name) {
    const golden::json& section = meta[uncapped ? "uncapped" : "capped"];

    auto [items, errors] = discover_charts({std::string(HYDRA_INPUT_DIR)});
    REQUIRE(errors.empty());
    std::unordered_map<std::string, const ScanItem*> by_relpath;
    for (const ScanItem& item : items) by_relpath[relpath_of(item.notespath)] = &item;

    AnalysisSettings settings;
    settings.difficulty = meta["difficulty"].get<std::string>();
    settings.prodrums = meta["prodrums"].get<bool>();
    settings.bass2x = meta["bass2x"].get<bool>();
    settings.depth_mode = meta["d_mode"].get<std::string>() == "scores" ? 0 : 1;
    settings.depth_value = meta["d_value"].get<int>();
    if (!meta["ms_filter"].is_null()) settings.ms_filter = meta["ms_filter"].get<double>();
    settings.uncapped = uncapped;
    // gen_golden_report disables the ladder's time budget for determinism;
    // nullopt runs every rung to completion, the same thing.
    settings.uncapped_time_budget_s = std::nullopt;

    const std::string chartmode = meta["chartmode"].get<std::string>();
    store::RecordStore store(":memory:", uncapped);

    for (const auto& rel : section["charts"]) {
        const std::string relpath = rel.get<std::string>();
        auto it = by_relpath.find(relpath);
        REQUIRE_MESSAGE(it != by_relpath.end(), "chart not discovered: " << relpath);
        const ScanItem& item = *it->second;

        AnalysisResult result = analyze_chart_file(item.notespath, settings);
        store.add_song(item.md5, item.title, item.artist, item.charter, result.song);
        store.add_record(item.md5, chartmode, result.record);
    }

    std::vector<report::ReportRow> rows =
        report::collect_rows(store, meta["max_paths"].get<int64_t>(), uncapped);
    std::string html = report::build_html(rows, section["subtitle"].get<std::string>(),
                                          section["footer"].get<std::string>());

    std::string expected = read_bytes(golden::root() + "/" + html_name);
    check_same_bytes(html, expected, html_name);
    MESSAGE(html_name << ": " << rows.size() << " rows, " << html.size() << " bytes");
}

}  // namespace

TEST_CASE("report page matches golden byte-for-byte (capped)") {
    golden::json meta = golden::load_file(golden::root() + "/report_meta.json");
    run_edition(meta, false, "report.html");
}

TEST_CASE("report page matches golden byte-for-byte (uncapped)") {
    golden::json meta = golden::load_file(golden::root() + "/report_meta.json");
    run_edition(meta, true, "report_uncapped.html");
}
