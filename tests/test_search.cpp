// Parity test for search/ (ScoreGraph + native engine + rebuild + pather).
//
// Reproduces the golden `analysis` block for every chart across the whole config
// matrix and diffs field-by-field. Golden floats are repr() strings, so floats
// (e_offset, squeeze offsets) are compared as exact doubles via as_double rather
// than by string. Also cross-checks that the DP engine's best score equals the
// BFS engine's over a cap ladder (the dp_parity_check port).

#include "doctest.h"

#include <optional>
#include <string>
#include <vector>

#include "core/model.h"
#include "golden_util.h"
#include "parse/song.h"
#include "search/engine.h"
#include "search/graph.h"
#include "search/pather.h"

#ifndef HYDRA_INPUT_DIR
#error "HYDRA_INPUT_DIR must be defined (see CMakeLists.txt)"
#endif

using namespace hydra;

namespace {

std::string diff_activation(const Activation& a, const golden::json& g) {
    if (!a.skips.has_value() || *a.skips != g["skips"].get<int>())
        return "skips";
    if (!a.sp_meter.has_value() || *a.sp_meter != g["sp_meter"].get<int>())
        return "sp_meter";
    if (!a.e_offset.has_value() || *a.e_offset != golden::as_double(g["e_offset"]))
        return "e_offset";
    if (!a.frontend_points.has_value() ||
        *a.frontend_points != g["frontend_points"].get<int>())
        return "frontend_points";

    const golden::json& gt = g["timecode_ticks"];
    if (a.timecode.has_value()) {
        if (gt.is_null() || a.timecode->ticks() != gt.get<int64_t>())
            return "timecode_ticks";
    } else if (!gt.is_null()) {
        return "timecode_ticks";
    }

    if (static_cast<int>(a.backends.size()) != g["backends_count"].get<int>())
        return "backends_count";
    if (a.notationstr() != g["notationstr"].get<std::string>())
        return "notationstr";

    const golden::json& gsq = g["sqinouts"];
    if (a.sqinouts.size() != gsq.size()) return "sqinouts_count";
    for (size_t k = 0; k < a.sqinouts.size(); ++k) {
        if (std::string(a.sqinouts[k].type_name()) !=
            gsq[k][0].get<std::string>())
            return "sqinout_type";
        if (a.sqinouts[k].offset() != golden::as_double(gsq[k][1]))
            return "sqinout_offset";
    }
    return "";
}

std::string diff_path(const Path& p, const golden::json& g) {
    if (p.totalscore() != g["totalscore"].get<int64_t>()) return "totalscore";
    if (p.pathstring() != g["pathstring"].get<std::string>()) return "pathstring";
    if (p.pathstring_verbose() != g["pathstring_verbose"].get<std::string>())
        return "pathstring_verbose";
    if (p.tied_pathcount() != g["tied_pathcount"].get<int>())
        return "tied_pathcount";
    if (p.notecount != g["notecount"].get<int>()) return "notecount";
    if (p.leftover_sp != g["leftover_sp"].get<int>()) return "leftover_sp";
    if (p.skipped_accents != g["skipped_accents"].get<int>())
        return "skipped_accents";
    if (p.skipped_ghosts != g["skipped_ghosts"].get<int>())
        return "skipped_ghosts";
    if (p.score_base != g["score_base"].get<int64_t>()) return "score_base";
    if (p.score_combo != g["score_combo"].get<int64_t>()) return "score_combo";
    if (p.score_sp != g["score_sp"].get<int64_t>()) return "score_sp";
    if (p.score_solo != g["score_solo"].get<int64_t>()) return "score_solo";
    if (p.score_accents != g["score_accents"].get<int64_t>())
        return "score_accents";
    if (p.score_ghosts != g["score_ghosts"].get<int64_t>())
        return "score_ghosts";

    const golden::json& gvp = g["var_point"];
    if (p.var_point.has_value()) {
        if (gvp.is_null() || *p.var_point != gvp.get<int>()) return "var_point";
    } else if (!gvp.is_null()) {
        return "var_point";
    }

    std::vector<Activation> acts = p.all_activations();
    const golden::json& ga = g["activations"];
    if (acts.size() != ga.size()) return "activations_count";
    for (size_t i = 0; i < acts.size(); ++i) {
        std::string d = diff_activation(acts[i], ga[i]);
        if (!d.empty()) return "act[" + std::to_string(i) + "]." + d;
    }

    const golden::json& gv = g["variants"];
    if (p.variants.size() != gv.size()) return "variants_count";
    for (size_t i = 0; i < p.variants.size(); ++i) {
        std::string d = diff_path(p.variants[i], gv[i]);
        if (!d.empty()) return "var[" + std::to_string(i) + "]." + d;
    }
    return "";
}

std::string diff_analysis(const std::optional<HydraRecord>& rec,
                          const std::optional<std::string>& err,
                          const golden::json& g) {
    if (g.contains("error")) {
        if (!err.has_value()) return "expected error, got a record";
        if (*err != g["error"].get<std::string>()) return "error message";
        return "";
    }
    if (err.has_value()) return "unexpected error: " + *err;

    const HydraRecord& r = *rec;
    const golden::json& gc = g["sp_cap"];
    if (r.sp_cap.has_value()) {
        if (gc.is_null() || *r.sp_cap != gc.get<int>()) return "sp_cap";
    } else if (!gc.is_null()) {
        return "sp_cap";
    }
    if (r.sp_cap_converged != g["sp_cap_converged"].get<bool>())
        return "sp_cap_converged";

    const golden::json& gp = g["paths"];
    if (r.paths.size() != gp.size()) return "paths_count";
    for (size_t i = 0; i < r.paths.size(); ++i) {
        std::string d = diff_path(r.paths[i], gp[i]);
        if (!d.empty()) return "path[" + std::to_string(i) + "]." + d;
    }
    return "";
}

struct Config {
    const char* key;
    bool capped;
    int dmode;  // 0 = scores, 1 = points
    int dvalue;
    std::optional<double> ms;
};

}  // namespace

TEST_CASE("analysis matches golden across the corpus and config matrix") {
    const std::vector<Config> matrix = {
        {"capped.scores.10", true, 0, 10, std::nullopt},
        {"capped.scores.200", true, 0, 200, std::nullopt},
        {"capped.scores.0", true, 0, 0, std::nullopt},
        {"capped.scores.1", true, 0, 1, std::nullopt},
        {"capped.scores.3", true, 0, 3, std::nullopt},
        {"capped.points.5000", true, 1, 5000, std::nullopt},
        {"capped.scores.200.ms5", true, 0, 200, 5.0},
        {"capped.scores.200.ms20", true, 0, 200, 20.0},
        {"uncapped.scores.200", false, 0, 200, std::nullopt},
    };

    const golden::json idx = golden::index();
    int checks = 0, mismatches = 0;

    for (const auto& entry : idx) {
        const std::string relpath = entry["relpath"].get<std::string>();
        const std::string slug = entry["slug"].get<std::string>();
        golden::json doc = golden::chart(slug);
        if (!doc.contains("analysis")) continue;

        const std::string path = std::string(HYDRA_INPUT_DIR) + "/" + relpath;
        Song song = load_songpath(path, "Expert", true, true);

        for (const Config& cfg : matrix) {
            const golden::json& gcfg = doc["analysis"][cfg.key];

            std::optional<HydraRecord> rec;
            std::optional<std::string> err;
            try {
                rec = analyze_chart(song, cfg.capped, cfg.dmode, cfg.dvalue,
                                    cfg.ms);
            } catch (const ChartFileError& e) {
                err = e.what();
            }

            std::string d = diff_analysis(rec, err, gcfg);
            ++checks;
            if (!d.empty() && ++mismatches <= 8)
                CHECK_MESSAGE(false, relpath << " [" << cfg.key << "] " << d);
        }
    }

    CHECK(mismatches == 0);
    MESSAGE("checked " << checks << " analyses");
}

TEST_CASE("DP best score equals BFS best score over a cap ladder") {
    const int caps[] = {4, 16, 32};

    const golden::json idx = golden::index();
    int checks = 0, mismatches = 0;

    for (const auto& entry : idx) {
        const std::string relpath = entry["relpath"].get<std::string>();
        const std::string path = std::string(HYDRA_INPUT_DIR) + "/" + relpath;

        Song song = load_songpath(path, "Expert", true, true);
        if (song.is_empty()) continue;

        for (int cap : caps) {
            ScoreGraph graph(song, cap);

            std::vector<Path> bfs = run_search(graph, 0, 4, std::nullopt, false);
            std::vector<Path> dp = run_search(graph, 0, 4, std::nullopt, true);

            int64_t bfs_score = bfs.empty() ? 0 : bfs.front().totalscore();
            int64_t dp_score = dp.empty() ? 0 : dp.front().totalscore();

            ++checks;
            if (bfs_score != dp_score && ++mismatches <= 8)
                CHECK_MESSAGE(bfs_score == dp_score,
                              relpath << " cap " << cap << ": BFS " << bfs_score
                                      << " DP " << dp_score);
        }
    }

    CHECK(mismatches == 0);
    MESSAGE("checked " << checks << " DP/BFS comparisons");
}
