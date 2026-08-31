// hydra_fillcompare — build a sortable HTML page comparing the same charts
// scored under Clone Hero 1.0's fill-spawn rule against Clone Hero 1.1's.
// The two answers live in two separate database files (docs/adr/0010); this
// tool joins them by chart hash and reports where they agree or disagree.
//
//     hydra_fillcompare --old ch10.db --new ch11.db
//     hydra_fillcompare --old ch10.db --new ch11.db --out fill_compare.html
//     hydra_fillcompare --old ch10.db --new ch11.db --no-open
//
// The page is self-contained: open it anywhere, click any column to sort. It
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
#include "app/fill_report.h"
#include "app/report_files.h"
#include "core/model.h"
#include "search/graph.h"
#include "store/record_store.h"

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    std::optional<std::string> old_path;
    std::optional<std::string> new_path;
    std::string out = "fill_compare.html";
    bool open_when_done = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--old" && i + 1 < argc) old_path = argv[++i];
        else if (arg == "--new" && i + 1 < argc) new_path = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out = argv[++i];
        else if (arg == "--no-open") open_when_done = false;
        else {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return 2;
        }
    }

    if (!old_path || !new_path) {
        std::fprintf(stderr,
            "Usage: hydra_fillcompare --old <ch10.db> --new <ch11.db> "
            "[--out fill_compare.html] [--no-open]\n");
        return 2;
    }

    hydra::app::Settings settings = hydra::app::Settings::load();
    std::string chartmode = settings.chartmode_key();
    hydra::store::CapQuery cap = settings.cap_query();
    hydra::store::Lens lens = settings.lens();

    std::unique_ptr<hydra::store::RecordStore> old_store = hydra::app::open_store(*old_path);
    std::unique_ptr<hydra::store::RecordStore> new_store = hydra::app::open_store(*new_path);

    // Engine-mode sanity check: a stamp that disagrees with the flag it was
    // passed under is a warning, not a fatal error — an unstamped (nullopt)
    // db just means "assume the normal rule" and never warns.
    const char* ch10_stamp = hydra::engine_mode_stamp(hydra::FillDeadlineRule::Ch10);
    const char* ch11_stamp = hydra::engine_mode_stamp(hydra::FillDeadlineRule::Ch11);
    std::optional<std::string> old_mode = old_store->engine_mode();
    if (old_mode && *old_mode != ch10_stamp) {
        std::fprintf(stderr,
            "Warning: %s is stamped engine_mode=%s, not %s\n",
            old_path->c_str(), old_mode->c_str(), ch10_stamp);
    }
    std::optional<std::string> new_mode = new_store->engine_mode();
    if (new_mode && *new_mode != ch11_stamp) {
        std::fprintf(stderr,
            "Warning: %s is stamped engine_mode=%s, not %s\n",
            new_path->c_str(), new_mode->c_str(), ch11_stamp);
    }

    hydra::app::fill_report::GeneratedFillReport report =
        hydra::app::fill_report::generate_fill_report(*old_store, *new_store, chartmode, cap, lens);

    old_store->close();
    new_store->close();

    if (report.stats.total == 0) {
        std::printf("No records to compare. Run hydra_batch into both databases first.\n");
        return 1;
    }

    std::filesystem::path outpath = std::filesystem::absolute(std::filesystem::u8path(out));
    std::error_code ec;
    std::filesystem::create_directories(outpath.parent_path(), ec);

    try {
        hydra::app::write_report_file(outpath, report.html);
    } catch (const std::exception&) {
        std::fprintf(stderr, "Cannot write %s\n", out.c_str());
        return 1;
    }

    const hydra::app::fill_report::FillCompareStats& stats = report.stats;
    std::printf(
        "Compared %s charts: %s same, %s 1.0 higher, %s 1.1 higher, "
        "%s only in 1.0, %s only in 1.1\n",
        hydra::group_thousands(stats.total).c_str(),
        hydra::group_thousands(stats.same).c_str(),
        hydra::group_thousands(stats.ch10_higher).c_str(),
        hydra::group_thousands(stats.ch11_higher).c_str(),
        hydra::group_thousands(stats.only_old).c_str(),
        hydra::group_thousands(stats.only_new).c_str());
    std::printf("Wrote %s\n", out.c_str());

    if (open_when_done) {
        if (!hydra::app::open_in_browser(outpath.wstring()))
            std::fprintf(stderr, "Could not open the page automatically.\n");
    }
    return 0;
}
