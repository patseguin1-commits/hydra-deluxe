// hydra_batch — batch-analyze many charts straight into the record store.
// The C++ port of hydra_batch.py.
//
//     hydra_batch                    # every folder in the app's settings
//     hydra_batch <folder> [...]     # specific folders instead
//     hydra_batch --redo             # re-analyze charts already stored
//     hydra_batch --reindex          # only rebuild sort columns, no analysis
//     hydra_batch --db <path>        # target a specific database
//     hydra_batch --legacy-fills     # score fills by Clone Hero 1.0's rule
//     hydra_batch --rules <path>     # rule choices from this file, not the exe's hydra_rules.ini
//
// --legacy-fills needs its own --db, and each run stamps its database with the
// rule it used; a run whose rule disagrees with an existing stamp exits 2
// without writing. --reindex never stamps. Compare two such databases with
// hydra_fillcompare. Every result now carries its rule in its key, so the app
// keeps 1.0 and 1.1 results side by side in hydra.db; these two guards are the
// command line's behavior from before that, kept as it was (docs/adr/0010).
//
// Reads difficulty / pro drums / 2x bass / depth / SP cap from the app's
// settings INI (app/config.h), so results match what the app would produce
// for the same songs. Safe to interrupt and re-run: charts already stored for
// the current chartmode and SP cap are skipped unless --redo is given.
//
// Unlike the Python version this analyzes charts across a thread pool (the
// same pool the GUI's "Analyze library" uses), so lines can complete out of
// chart-discovery order.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "app/analysis.h"
#include "app/config.h"
#include "app/rules_file.h"
#include "core/model.h"
#include "core/strutil.h"
#include "core/winstr.h"
#include "search/graph.h"
#include "store/record_store.h"

namespace {

// Codepoint-safe prefix of a UTF-8 string, mirroring Python's label[:n] which
// slices characters, not bytes. Multi-byte sequences count as one column —
// close enough for console alignment.
std::string clip_utf8(const std::string& s, size_t max_chars) {
    size_t chars = 0, i = 0;
    while (i < s.size() && chars < max_chars) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        i += len;
        ++chars;
    }
    return s.substr(0, i) + std::string(max_chars > chars ? max_chars - chars : 0, ' ');
}

// Do these two paths name the same file? Compared after resolving `.`, `..`
// and relative prefixes (weakly_canonical works on a file that doesn't exist
// yet), then case-insensitively, because Windows paths are. Used only by the
// --legacy-fills guard, where a false negative is the dangerous answer, so a
// filesystem error falls back to comparing the strings rather than allowing
// the write.
bool same_file(const std::string& a, const std::string& b) {
    auto normalize = [](const std::string& p) {
        std::error_code ec;
        std::filesystem::path canon =
            std::filesystem::weakly_canonical(std::filesystem::u8path(p), ec);
        return hydra::to_lower_ascii(ec ? p : canon.u8string());
    };
    return normalize(a) == normalize(b);
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);  // chart titles/artists are UTF-8
    const std::vector<std::string> args = hydra::utf8_argv();
    const int argc = static_cast<int>(args.size());

    bool redo = false, reindex_only = false, legacy_fills = false;
    std::optional<std::string> dbpath;
    std::optional<std::string> rulespath;
    std::vector<std::string> folder_args;

    for (int i = 1; i < argc; ++i) {
        const std::string& arg = args[i];
        if (arg == "--redo") redo = true;
        else if (arg == "--reindex") reindex_only = true;
        else if (arg == "--legacy-fills") legacy_fills = true;
        else if (arg == "--db" && i + 1 < argc) dbpath = args[++i];
        else if (arg == "--rules" && i + 1 < argc) rulespath = args[++i];
        else if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return 2;
        } else {
            folder_args.push_back(arg);
        }
    }

    hydra::app::Settings settings = hydra::app::Settings::load();
    try {
        settings.rules = hydra::app::load_rules_file(
            rulespath ? std::filesystem::u8path(*rulespath) : hydra::app::default_rules_path());
    } catch (const hydra::app::RulesFileError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
    // The fill rule comes from the flag, never from the app's own "1.0 fills"
    // setting, so a run means the same thing whatever the app was left on.
    settings.legacy_fills = legacy_fills;
    hydra::app::BatchRun run = settings.batch_run();
    const hydra::app::AnalysisSettings& analysis = run.settings;
    const std::string& chartmode = run.chartmode;
    std::string db = dbpath ? *dbpath : hydra::app::db_path();

    // A legacy run gets its own file, and never writes the one the GUI reads:
    // the command line's rule from before results carried their fill rule
    // (docs/adr/0010), kept unchanged. The app's "1.0 fills" setting is the
    // way to put 1.0 results in hydra.db.
    if (legacy_fills && same_file(db, hydra::app::db_path())) {
        std::fprintf(stderr,
                     "--legacy-fills would write Clone Hero 1.0 results into the "
                     "database Hydra itself reads:\n  %s\n"
                     "Legacy results are not tagged as legacy, so they cannot share "
                     "a file with normal ones.\n"
                     "Give the run its own database, e.g. --db legacy.db\n",
                     db.c_str());
        return 2;
    }

    std::unique_ptr<hydra::store::RecordStore> store_ptr = hydra::app::open_store(db, hydra::core::RulesStamp::of(settings.rules));
    hydra::store::RecordStore& store = *store_ptr;

    // Reindexing only re-reads stored rows. It scores nothing, so it must not
    // relabel the file: a Clone Hero 1.0 database stays stamped ch10.
    if (reindex_only) {
        std::printf("Rebuilding sort columns from stored records...\n");
        int n = store.reindex();
        std::printf("Reindexed %d records.\n", n);
        return 0;
    }

    // One database holds one fill rule (docs/adr/0010). A file with results
    // but no stamp was written before hydra_batch stamped, by the normal rule;
    // hydra_fillcompare reads it the same way. A file with neither is new.
    const char* ch10 = hydra::engine_mode_stamp(hydra::FillDeadlineRule::Ch10);
    const char* ch11 = hydra::engine_mode_stamp(hydra::FillDeadlineRule::Ch11);
    const std::string run_mode = legacy_fills ? ch10 : ch11;
    std::optional<std::string> file_mode = store.engine_mode();
    if (!file_mode && store.counts().second > 0) file_mode = ch11;
    if (file_mode && *file_mode != run_mode) {
        auto rule_name = [&](const std::string& mode) {
            return mode == ch10 ? "Clone Hero 1.0" : mode == ch11 ? "Clone Hero 1.1"
                                                                  : "unknown";
        };
        std::fprintf(stderr,
                     "This database holds results scored by the %s fill rule "
                     "(engine_mode=%s):\n  %s\n"
                     "This run scores by the %s rule. The rule is not stored on each "
                     "result, so the two cannot share a file.\n"
                     "%s --legacy-fills to write into this database, or give this run "
                     "its own database with --db.\n",
                     rule_name(*file_mode), file_mode->c_str(), db.c_str(),
                     rule_name(run_mode), legacy_fills ? "Drop" : "Add");
        return 2;
    }
    // Stamp the file with the rule this run used, so hydra_fillcompare can tell
    // a 1.0 database from a 1.1 one.
    store.set_engine_mode(run_mode);

    std::vector<std::string> folders =
        folder_args.empty() ? settings.chartfolders : folder_args;
    if (folders.empty()) {
        std::printf("No chart folders. Add them in Hydra, or pass folders as arguments.\n");
        return 1;
    }

    std::printf("Database   : %s\n", db.c_str());
    std::printf("Chart mode : %s\n", chartmode.c_str());
    const char* depth_name = "scores";
    switch (analysis.depth_mode) {
        case hydra::DepthMode::Scores: depth_name = "scores"; break;
        case hydra::DepthMode::Points: depth_name = "points"; break;
    }
    std::printf("Depth      : %s %d\n", depth_name, settings.depth_value);
    std::printf("SP cap     : %d bars\n", settings.sp_cap);
    if (settings.mslimit_enabled)
        std::printf("Timing cap : %d ms\n", settings.mslimit_value);
    else
        std::printf("Timing cap : none\n");
    std::printf("Squeeze win: %d ms\n", static_cast<int>(hydra::kSqueezeWindowMs));
    std::printf("Fill rule  : %s\n",
                legacy_fills ? "Clone Hero 1.0 (legacy)" : "Clone Hero 1.1");
    std::printf("Folders    : %zu\n", folders.size());
    for (const std::string& f : folders) std::printf("    %s\n", f.c_str());

    std::printf("\nDiscovering charts...\n");
    // The GUI's last library scan, if this database has one. A chart whose
    // files are unchanged (size and modified time) reuses its hash and song
    // fields instead of being read again. hydra_batch only reads this cache;
    // it never rewrites the GUI's library.
    hydra::store::ChartLibraryCache cache;
    try {
        cache = store.chart_library_cache();
    } catch (const std::exception&) {
        // No cache is only a slower scan.
    }
    auto [scanitems, folder_errors] = hydra::app::discover_charts(
        folders, hydra::app::ScanCallbacks{}, cache.empty() ? nullptr : &cache);
    for (const std::string& err : folder_errors) std::printf("  ! %s\n", err.c_str());
    std::printf("Found %zu charts.\n\n", scanitems.size());

    auto started = std::chrono::steady_clock::now();

    int total = 0, skipped = 0, analyzed = 0, failed = 0, done = 0;
    bool total_known = false;
    std::vector<std::string> failures;

    hydra::app::BatchCallbacks callbacks;
    callbacks.on_progress = [&](const hydra::app::BatchProgress& p) {
        if (!total_known) {
            total = p.total;
            skipped = static_cast<int>(scanitems.size()) - p.total;
            total_known = true;
        }
    };
    callbacks.on_error = [&](const std::string& title, const std::string& error) {
        ++failed;
        ++done;
        failures.push_back(title + ": " + error);
        std::printf("[%d/%d] FAILED %s: %s\n", done, total, title.c_str(),
                    error.c_str());
    };
    callbacks.on_result = [&](const hydra::app::ScanItem& item,
                              const hydra::store::PreparedRow& row) {
        ++analyzed;
        ++done;
        std::string label = item.artist + " - " + item.title;
        std::string score =
            row.summary.score ? hydra::group_thousands(*row.summary.score) : "-";
        std::printf("[%d/%d] %10s  %s %s\n", done, total, score.c_str(),
                    clip_utf8(label, 52).c_str(), clip_utf8(row.bestpath, 36).c_str());
        std::fflush(stdout);
    };
    hydra::app::run_batch(scanitems, run, store, redo, hydra::app::batch_worker_count(),
                          callbacks);

    double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    auto [songs, records] = store.counts();

    std::printf("\nAnalyzed %d, skipped %d already stored, %d failed in %.1fs.\n", analyzed,
                skipped, failed, elapsed);
    std::printf("Store now holds %lld records across %lld songs.\n",
                static_cast<long long>(records), static_cast<long long>(songs));

    if (!failures.empty()) {
        std::printf("\nFailures:\n");
        size_t shown = failures.size() < 20 ? failures.size() : 20;
        for (size_t i = 0; i < shown; ++i) std::printf("  %s\n", failures[i].c_str());
        if (failures.size() > 20)
            std::printf("  ...and %zu more.\n", failures.size() - 20);
    }

    return 0;
}
