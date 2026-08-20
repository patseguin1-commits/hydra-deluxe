// hydra_report — build a sortable HTML report of every stored path.
// The C++ port of hydra_report.py (the page itself lives in app/report.cpp).
//
//     hydra_report                   # top 5 paths per chart
//     hydra_report --paths 20        # top 20 per chart
//     hydra_report --all-paths       # everything stored
//     hydra_report --out report.html
//     hydra_report --db <path>       # a specific database
//     hydra_report --uncapped        # the uncapped edition's records
//     hydra_report --no-open         # don't launch the page when done
//
// The page is self-contained: open it anywhere, click any column to sort,
// filter by text, difficulty tier, or best-path-only. The finished page
// opens in the default browser unless --no-open is given.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "app/config.h"
#include "app/report.h"
#include "core/model.h"
#include "store/record_store.h"

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    int64_t max_paths = 5;
    std::string out = "hydra_paths.html";
    std::optional<std::string> dbpath;
    bool uncapped = false;
    bool open_when_done = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--all-paths") max_paths = 1000000000;
        else if (arg == "--paths" && i + 1 < argc) max_paths = std::atoll(argv[++i]);
        else if (arg == "--out" && i + 1 < argc) out = argv[++i];
        else if (arg == "--db" && i + 1 < argc) dbpath = argv[++i];
        else if (arg == "--uncapped") uncapped = true;
        else if (arg == "--no-open") open_when_done = false;
        else {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return 2;
        }
    }

    // The hit window drives the timing-tier bands; read it from the same INI
    // the GUI writes so the two report entry points agree.
    const int hit_window_ms =
        hydra::app::Settings::load(uncapped).hit_window_ms;
    const double w = static_cast<double>(hit_window_ms);

    std::string db = dbpath ? *dbpath : hydra::app::db_path(uncapped);
    hydra::store::RecordStore store(db, uncapped);
    auto [songs, records] = store.counts();
    std::vector<hydra::app::report::ReportRow> rows =
        hydra::app::report::collect_rows(store, max_paths, uncapped, w);
    store.close();

    if (rows.empty()) {
        std::printf("No records stored yet. Run hydra_batch first.\n");
        return 1;
    }

    std::string shown = max_paths > 100000000
                            ? "every path"
                            : "top " + std::to_string(max_paths) + " paths per chart";
    std::string subtitle = hydra::group_thousands(records) + " records across " +
                           hydra::group_thousands(songs) + " songs — " + shown;
    std::string dbname = std::filesystem::u8path(db).filename().u8string();
    std::string footer = "Generated from " + dbname +
                         ". Squeeze timings are ms of error per hit, after "
                         "frontend transfer scaling; 'Beyond' is past the " +
                         std::to_string(hit_window_ms) + " ms hit window.";

    // Make the folder rather than throwing away the work: collecting the rows
    // means inflating every stored record, which is the slow part.
    std::filesystem::path outpath = std::filesystem::absolute(std::filesystem::u8path(out));
    std::error_code ec;
    std::filesystem::create_directories(outpath.parent_path(), ec);

    std::ofstream f(outpath, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::fprintf(stderr, "Cannot write %s\n", out.c_str());
        return 1;
    }
    f << hydra::app::report::build_html(rows, subtitle, footer, w);
    f.close();

    std::printf("Wrote %s path rows to %s\n", hydra::group_thousands(
                    static_cast<int64_t>(rows.size())).c_str(), out.c_str());

    if (open_when_done) {
        // Hand the page to the default browser. ShellExecuteW returns > 32 on
        // success; failure (no association, whatever) isn't worth failing the
        // run over — the file is already written and its path was printed.
        HINSTANCE rc = ShellExecuteW(nullptr, L"open", outpath.c_str(), nullptr,
                                     nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(rc) <= 32)
            std::fprintf(stderr, "Could not open the page automatically.\n");
    }
    return 0;
}
