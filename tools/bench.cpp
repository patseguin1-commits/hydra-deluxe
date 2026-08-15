// Timing harness for the GUI's analysis path. The Hydra/HydraUncapped GUIs call
// app::analyze_chart_file -> hydra::analyze_chart -> run_search, i.e. the
// object-based C++ engine in src/search/engine.cpp, then serialize the record
// into the RecordStore. This times exactly that.
//
// With a folder argument it discovers the folder's charts and prints a per-
// chart breakdown at the GUI's UNCAPPED DEFAULT settings (score range 4, 10ms
// limit) -- parse, search, and DB store timed separately, plus the capped
// number for reference:
//   hydra_bench.exe "C:\Clone Hero\songs\...\blink-182 - Discography"
// With no argument it best-of-3 times the golden corpus search across configs.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "golden_util.h"
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

    store::RecordStore store(":memory:", /*uncapped=*/true);

    for (const app::ScanItem& it : items) {
        std::printf("%s\n", it.notespath.c_str());

        auto t = clk::now();
        std::optional<Song> song_opt;
        try {
            song_opt.emplace(load_songpath(it.notespath, "Expert", true, true));
        } catch (const std::exception& e) {
            std::printf("  (skipped: %s)\n\n", e.what());
            continue;
        }
        const Song& song = *song_opt;
        double parse_s = secs_since(t);

        // Uncapped ladder, score range 4, 10ms limit -- the exact default.
        t = clk::now();
        HydraRecord rec = analyze_chart(song, /*capped=*/false, /*dmode=*/0,
                                        /*dvalue=*/4, /*ms_filter=*/10.0,
                                        /*sp_cap=*/std::nullopt, /*on_progress=*/{},
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

static void golden_corpus() {
    std::vector<Song> songs;
    for (const auto& entry : golden::index()) {
        try {
            songs.push_back(load_songpath(
                std::string(HYDRA_INPUT_DIR) + "/" + entry["relpath"].get<std::string>(),
                "Expert", true, true));
        } catch (const std::exception&) {
        }
    }
    std::printf("Golden corpus: %zu charts. Engine = src/search/engine.cpp.\n\n",
                songs.size());
    auto bench = [&](const char* name, bool capped, int dvalue) {
        double best = 1e30;
        for (int rep = 0; rep < 3; ++rep) {
            auto t0 = clk::now();
            for (const Song& s : songs)
                try {
                    analyze_chart(s, capped, 0, dvalue, std::nullopt);
                } catch (const std::exception&) {
                }
            best = std::min(best, secs_since(t0));
        }
        std::printf("  %-16s : %7.2fs (best of 3)\n", name, best);
    };
    bench("capped d4", true, 4);
    bench("uncapped d4", false, 4);
}

int main(int argc, char** argv) {
    if (argc > 1)
        folder_breakdown(argv[1]);
    else
        golden_corpus();
    return 0;
}
