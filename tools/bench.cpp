// Timing harness for the GUI's analysis path. The Hydra GUI calls
// app::analyze_chart_file -> hydra::analyze_chart -> run_search, i.e. the
// object-based C++ engine in src/search/engine.cpp, then serialize the record
// into the RecordStore. This times exactly that.
//
// With a folder argument it discovers the folder's charts and prints a per-
// chart breakdown at the GUI's UNCAPPED DEFAULT settings (score range 4, 10ms
// limit) -- parse, search, and DB store timed separately, plus the capped
// number for reference:
//   hydra_bench.exe "C:\Clone Hero\songs\...\blink-182 - Discography"
// With no argument it best-of-3 times the testdata corpus search across
// configs.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "json.hpp"

#include "app/analysis.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/pather.h"
#include "store/record_store.h"

using namespace hydra;
using clk = std::chrono::steady_clock;

static double secs_since(clk::time_point t0) {
    return std::chrono::duration<double>(clk::now() - t0).count();
}

static int count_paths(const HydraRecord& r) {
    int n = 0;
    std::vector<const Path*> q;
    for (const Path& p : r.paths) q.push_back(&p);
    while (!q.empty()) {
        const Path* p = q.back();
        q.pop_back();
        ++n;
        for (const Path& v : p->variants) q.push_back(&v);
    }
    return n;
}

// Folder mode: the real GUI default (uncapped, score range 4, 10ms limit),
// broken into the phases the app actually pays, per chart.
static void folder_breakdown(const std::string& folder) {
    auto [items, errors] = app::discover_charts({folder});
    std::printf("Discovered %zu chart(s) (%zu folder error(s)).\n", items.size(),
                errors.size());
    std::printf("Settings: UNCAPPED, score range 4, 10ms limit (the GUI default).\n\n");

    store::RecordStore store(":memory:");

    for (const app::ScanItem& it : items) {
        std::printf("%s\n", it.notespath.c_str());

        auto t = clk::now();
        std::optional<Song> song_opt;
        try {
            song_opt.emplace(load_songpath(it.notespath, true, true));
        } catch (const std::exception& e) {
            std::printf("  (skipped: %s)\n\n", e.what());
            continue;
        }
        const Song& song = *song_opt;
        double parse_s = secs_since(t);

        // Uncapped ladder, score range 4, 10ms limit -- the exact default.
        t = clk::now();
        HydraRecord rec = analyze_chart(song, /*sp_cap=*/std::nullopt, /*dmode=*/0,
                                        /*dvalue=*/4, /*ms_filter=*/10.0,
                                        /*on_progress=*/{},
                                        /*time_budget=*/std::nullopt);
        double search_s = secs_since(t);

        t = clk::now();
        store.add_song(it.md5, it.title, it.artist, it.charter, song);
        store.add_record(it.md5, "Expert Pro Drums, 2x Bass", rec);
        double store_s = secs_since(t);

        long long best = rec.paths.empty() ? 0 : rec.best_path().totalscore();
        std::printf("  parse %.2fs | search %.2fs | store %.2fs  => TOTAL %.2fs\n",
                    parse_s, search_s, store_s, parse_s + search_s + store_s);
        std::printf("  best score %lld | %d paths | sp_cap %d (%s)\n\n", best,
                    count_paths(rec), rec.sp_cap.value_or(-1),
                    rec.sp_cap_converged ? "settled" : "unsettled");
        std::fflush(stdout);
    }
}

// Scan mode: times the library scan (discovery + hashing) the way ScanJob
// runs it, without any chart analysis. Optionally writes the discovered rows
// into a store (--db, timing the library rebuild and exercising the rescan
// cache on a second run) and/or dumps the items as JSON for equivalence
// diffs (--dump; --dump-rel makes paths relative to the given root, forward
// slashes, so dumps compare across machines).
static void scan_mode(const std::string& folder, const std::string& dbpath,
                      const std::string& dumppath, const std::string& dumprel) {
    std::printf("Scanning %s\n", folder.c_str());

    std::unique_ptr<store::RecordStore> store;
    if (!dbpath.empty()) store = std::make_unique<store::RecordStore>(dbpath);

    // With --db, a prior scan's rows become the rescan cache — running the
    // same command twice measures cold full scan then warm rescan.
    store::ChartLibraryCache cache;
    if (store) cache = store->chart_library_cache();
    if (!cache.empty()) std::printf("  (rescan cache: %zu rows)\n", cache.size());

    int folders_seen = 0, cached = 0;
    double enumerate_s = 0.0;
    auto t0 = clk::now();
    app::ScanCallbacks callbacks;
    callbacks.on_folders = [&](int n) { folders_seen = n; };
    callbacks.on_charts = [&](int done, int, int cached_now) {
        if (done == 0) enumerate_s = secs_since(t0);  // walk finished, reads start
        cached = cached_now;
    };
    auto [items, errors] =
        app::discover_charts({folder}, callbacks, cache.empty() ? nullptr : &cache);
    double scan_s = secs_since(t0);

    std::printf("  folders %d | charts %zu | cached %d | errors %zu\n", folders_seen,
                items.size(), cached, errors.size());
    std::printf("  enumerate     : %7.2fs\n", enumerate_s);
    std::printf("  read+hash     : %7.2fs\n", scan_s - enumerate_s);
    std::printf("  discover total: %7.2fs\n", scan_s);
    for (size_t i = 0; i < errors.size() && i < 10; ++i)
        std::printf("  ! %s\n", errors[i].c_str());
    if (errors.size() > 10) std::printf("  ! ...and %zu more\n", errors.size() - 10);

    if (store) {
        std::vector<store::ChartLibraryEntry> entries;
        entries.reserve(items.size());
        for (const app::ScanItem& it : items)
            entries.push_back({it.md5, it.title, it.artist, it.charter, it.notespath,
                               it.rootfolder, it.sig});
        t0 = clk::now();
        store->rebuild_chart_library(entries);
        std::printf("  library write : %7.2fs (%lld rows)\n", secs_since(t0),
                    static_cast<long long>(store->chart_library_count()));
    }

    if (!dumppath.empty()) {
        auto relify = [&](std::string p) {
            if (!dumprel.empty() && p.size() > dumprel.size() &&
                p.compare(0, dumprel.size(), dumprel) == 0)
                p = p.substr(dumprel.size() + 1);
            for (char& c : p)
                if (c == '\\') c = '/';
            return p;
        };
        nlohmann::json arr = nlohmann::json::array();
        std::vector<const app::ScanItem*> sorted;
        for (const app::ScanItem& it : items) sorted.push_back(&it);
        std::sort(sorted.begin(), sorted.end(),
                  [](const app::ScanItem* a, const app::ScanItem* b) {
                      return a->notespath < b->notespath;
                  });
        for (const app::ScanItem* it : sorted)
            arr.push_back({{"path", relify(it->notespath)},
                           {"folder", relify(it->rootfolder)},
                           {"md5", it->md5},
                           {"title", it->title},
                           {"artist", it->artist},
                           {"charter", it->charter}});
        // Real libraries carry ANSI-encoded song.ini metadata; replace
        // invalid UTF-8 instead of throwing (both sides of a diff replace
        // identically, so equivalence still holds).
        std::ofstream f(dumppath, std::ios::binary | std::ios::trunc);
        f << arr.dump(1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
        std::printf("  dumped %zu items to %s\n", sorted.size(), dumppath.c_str());
    }
}

static void corpus_bench() {
    std::vector<Song> songs;
    for (const std::string& path : corpus::chart_paths()) {
        try {
            songs.push_back(load_songpath(path, true, true));
        } catch (const std::exception&) {
        }
    }
    std::printf("Test corpus: %zu charts. Engine = src/search/engine.cpp.\n\n",
                songs.size());
    auto bench = [&](const char* name, std::optional<int> cap, int dvalue) {
        double best = 1e30;
        for (int rep = 0; rep < 3; ++rep) {
            auto t0 = clk::now();
            for (const Song& s : songs)
                try {
                    analyze_chart(s, cap, 0, dvalue, std::nullopt);
                } catch (const std::exception&) {
                }
            best = std::min(best, secs_since(t0));
        }
        std::printf("  %-16s : %7.2fs (best of 3)\n", name, best);
    };
    bench("cap4 d4", 4, 4);
    bench("auto d4", std::nullopt, 4);
}

// Dump a store's charts table as the same JSON shape --dump writes, so two
// scans' results can be diffed even when one came from another build.
static void dump_db(const std::string& dbpath, const std::string& outpath) {
    store::RecordStore db(dbpath);
    std::vector<store::ChartLibraryEntry> rows =
        db.list_chart_library(std::nullopt, 0, INT_MAX);
    std::sort(rows.begin(), rows.end(),
              [](const store::ChartLibraryEntry& a, const store::ChartLibraryEntry& b) {
                  return a.notespath < b.notespath;
              });
    nlohmann::json arr = nlohmann::json::array();
    for (const store::ChartLibraryEntry& e : rows)
        arr.push_back({{"path", e.notespath},
                       {"folder", e.rootfolder},
                       {"md5", e.md5},
                       {"title", e.title},
                       {"artist", e.artist},
                       {"charter", e.charter}});
    std::ofstream f(outpath, std::ios::binary | std::ios::trunc);
    f << arr.dump(1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    std::printf("dumped %zu rows from %s\n", rows.size(), dbpath.c_str());
}

int main(int argc, char** argv) {
    if (argc > 3 && std::string(argv[1]) == "--dump-db") {
        dump_db(argv[2], argv[3]);
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "--scan") {
        std::string folder = argv[2], db, dump, dumprel;
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--db" && i + 1 < argc) db = argv[++i];
            else if (arg == "--dump" && i + 1 < argc) dump = argv[++i];
            else if (arg == "--dump-rel" && i + 1 < argc) dumprel = argv[++i];
        }
        scan_mode(folder, db, dump, dumprel);
    } else if (argc > 1) {
        folder_breakdown(argv[1]);
    } else {
        corpus_bench();
    }
    return 0;
}
