// hydra_batch — batch-analyze many charts straight into the record store.
// The C++ port of hydra_batch.py.
//
//     hydra_batch                    # every folder in the app's settings
//     hydra_batch <folder> [...]     # specific folders instead
//     hydra_batch --redo             # re-analyze charts already stored
//     hydra_batch --reindex          # only rebuild sort columns, no analysis
//     hydra_batch --db <path>        # target a specific database
//     hydra_batch --legacy-fills     # score fills by Clone Hero 1.0's rule
//
// --legacy-fills needs its own --db: the rule is not recorded on a row, so
// 1.0 and 1.1 results must not share a file (docs/adr/0010). Compare two such
// databases with hydra_fillcompare.
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
#include "core/model.h"
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
        std::string s = ec ? p : canon.u8string();
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return normalize(a) == normalize(b);
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);  // chart titles/artists are UTF-8

    bool redo = false, reindex_only = false, legacy_fills = false;
    std::optional<std::string> dbpath;
    std::vector<std::string> folder_args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--redo") redo = true;
        else if (arg == "--reindex") reindex_only = true;
        else if (arg == "--legacy-fills") legacy_fills = true;
        else if (arg == "--db" && i + 1 < argc) dbpath = argv[++i];
        else if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return 2;
        } else {
            folder_args.push_back(arg);
        }
    }

    hydra::app::Settings settings = hydra::app::Settings::load();
    hydra::app::AnalysisSettings analysis = settings.to_analysis_settings();
    analysis.legacy_fill_deadline = legacy_fills;
    std::string chartmode = settings.chartmode_key();
    std::string db = dbpath ? *dbpath : hydra::app::db_path();

    // A legacy run scores fills by a rule the app does not know about, and the
    // rule is not part of a record's identity (docs/adr/0010) — a legacy row
    // and a normal row for the same chart are indistinguishable once stored.
    // So keep them in separate files, and refuse to write the one the GUI reads.
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

    std::unique_ptr<hydra::store::RecordStore> store_ptr = hydra::app::open_store(db);
    hydra::store::RecordStore& store = *store_ptr;

    // Stamp the file with the rule this run used, every run, so hydra_fillcompare
    // can tell a 1.0 database from a 1.1 one.
    store.set_engine_mode(hydra::engine_mode_stamp(
        legacy_fills ? hydra::FillDeadlineRule::Ch10 : hydra::FillDeadlineRule::Ch11));

    if (reindex_only) {
        std::printf("Rebuilding sort columns from stored records...\n");
        int n = store.reindex();
        std::printf("Reindexed %d records.\n", n);
        return 0;
    }

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
    if (settings.sp_cap) std::printf("SP cap     : %d bars\n", *settings.sp_cap);
    else std::printf("SP cap     : Auto\n");
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
    auto [scanitems, folder_errors] = hydra::app::discover_charts(folders);
    for (const std::string& err : folder_errors) std::printf("  ! %s\n", err.c_str());
    std::printf("Found %zu charts.\n\n", scanitems.size());

    auto started = std::chrono::steady_clock::now();

    int total = 0, skipped = 0, analyzed = 0, failed = 0, done = 0;
    bool total_known = false;
    std::vector<std::string> failures;

    hydra::app::run_batch(
        scanitems, chartmode, analysis, store, redo,
        hydra::app::batch_worker_count(),
        [&](const hydra::app::BatchProgress& p) {
            if (!total_known) {
                total = p.total;
                skipped = static_cast<int>(scanitems.size()) - p.total;
                total_known = true;
            }
        },
        [&](const std::string& title, const std::string& error) {
            ++failed;
            ++done;
            failures.push_back(title + ": " + error);
            std::printf("[%d/%d] FAILED %s: %s\n", done, total, title.c_str(),
                        error.c_str());
        },
        [&](const hydra::app::ScanItem& item, const hydra::store::PreparedRow& row) {
            ++analyzed;
            ++done;
            std::string label = item.artist + " - " + item.title;
            std::string score =
                row.summary.score ? hydra::group_thousands(*row.summary.score) : "-";
            std::printf("[%d/%d] %10s  %s %s\n", done, total, score.c_str(),
                        clip_utf8(label, 52).c_str(), clip_utf8(row.bestpath, 36).c_str());
            std::fflush(stdout);
        });

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
