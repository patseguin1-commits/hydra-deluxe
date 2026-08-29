// hydra_report — build a sortable HTML report of every stored path.
// The C++ port of hydra_report.py (the page itself lives in app/report.cpp).
//
//     hydra_report                   # top 5 paths per chart
//     hydra_report --paths 20        # top 20 per chart
//     hydra_report --all-paths       # everything stored
//     hydra_report --out report.html
//     hydra_report --db <path>       # a specific database
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
#include <optional>
#include <string>

#include "app/config.h"
#include "app/report.h"
#include "app/report_files.h"
#include "core/model.h"
#include "store/record_store.h"

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    int64_t max_paths = 5;
    std::string out = "hydra_paths.html";
    std::optional<std::string> dbpath;
    bool open_when_done = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--all-paths") max_paths = 1000000000;
        else if (arg == "--paths" && i + 1 < argc) max_paths = std::atoll(argv[++i]);
        else if (arg == "--out" && i + 1 < argc) out = argv[++i];
        else if (arg == "--db" && i + 1 < argc) dbpath = argv[++i];
        else if (arg == "--no-open") open_when_done = false;
        else {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return 2;
        }
    }

    // One seam for the whole page — rows, counts, and framing come from
    // generate_report, the same call the GUI's ReportJob makes. The hit
    // window is read from the same INI the GUI writes so the two report
    // entry points agree.
    std::string db = dbpath ? *dbpath : hydra::app::db_path();
    hydra::app::Settings settings = hydra::app::Settings::load();
    hydra::app::report::ReportOptions options;
    options.max_paths = max_paths;
    options.cap = settings.cap_query();
    options.lens = settings.lens();
    options.hit_window_ms = settings.hit_window_ms;
    options.db_path = db;

    std::unique_ptr<hydra::store::RecordStore> store = hydra::app::open_store(db);
    hydra::app::report::GeneratedReport report =
        hydra::app::report::generate_report(*store, options);
    store->close();

    if (report.rows == 0) {
        std::printf("No records stored yet. Run hydra_batch first.\n");
        return 1;
    }

    // Make the folder rather than throwing away the work: collecting the rows
    // means inflating every stored record, which is the slow part.
    std::filesystem::path outpath = std::filesystem::absolute(std::filesystem::u8path(out));
    std::error_code ec;
    std::filesystem::create_directories(outpath.parent_path(), ec);

    try {
        hydra::app::write_report_file(outpath, report.html);
    } catch (const std::exception&) {
        std::fprintf(stderr, "Cannot write %s\n", out.c_str());
        return 1;
    }

    std::printf("Wrote %s path rows to %s\n",
                hydra::group_thousands(report.rows).c_str(), out.c_str());

    if (open_when_done) {
        // Hand the page to the default browser (the same call the GUI's
        // report jobs make). Failure (no association, whatever) isn't worth
        // failing the run over — the file is already written and its path was
        // printed.
        if (!hydra::app::open_in_browser(outpath.wstring()))
            std::fprintf(stderr, "Could not open the page automatically.\n");
    }
    return 0;
}
