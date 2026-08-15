// Parity test for store/ (record_store.{h,cpp} + serialize.{h,cpp}) — the
// Phase 4 gate: prepare_row's summary/bestpath/hyversion must match golden's
// `store` block, and a record must round-trip through RecordStore (write,
// reload, restore timecodes) losslessly.

#include "doctest.h"

#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "core/model.h"
#include "golden_util.h"
#include "parse/song.h"
#include "search/pather.h"
#include "store/record_store.h"

using namespace hydra;
using namespace hydra::store;

namespace {

struct Config {
    const char* key;
    bool capped;
    int dmode;
    int dvalue;
    std::optional<double> ms;
};

const std::vector<Config> kMatrix = {
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

// Golden's hyversion is a JSON array (e.g. [1,3,1] or [1,3,1,"uncapped"]);
// current_record_version is the same tuple dotted together.
std::string joined_hyversion(const golden::json& arr) {
    std::string out;
    for (size_t i = 0; i < arr.size(); ++i) {
        if (i) out += ".";
        const golden::json& v = arr[i];
        out += v.is_string() ? v.get<std::string>() : std::to_string(v.get<int>());
    }
    return out;
}

std::string diff_summary(const PathSummary& s, const golden::json& g) {
    auto opt_i64_eq = [](std::optional<int64_t> v, const golden::json& gv) {
        if (v.has_value()) return !gv.is_null() && *v == gv.get<int64_t>();
        return gv.is_null();
    };
    auto opt_i_eq = [](std::optional<int> v, const golden::json& gv) {
        if (v.has_value()) return !gv.is_null() && *v == gv.get<int>();
        return gv.is_null();
    };
    auto opt_f_eq = [](std::optional<double> v, const golden::json& gv) {
        if (v.has_value()) return !gv.is_null() && *v == golden::as_double(gv);
        return gv.is_null();
    };

    if (!opt_i64_eq(s.score, g["score"])) return "score";
    if (!opt_i_eq(s.actcount, g["actcount"])) return "actcount";
    if (!opt_i_eq(s.maxskip, g["maxskip"])) return "maxskip";
    if (!opt_f_eq(s.hardest_ms, g["hardest_ms"])) return "hardest_ms";
    if (!opt_f_eq(s.avgmult, g["avgmult"])) return "avgmult";
    if (!opt_i_eq(s.notecount, g["notecount"])) return "notecount";
    if (!opt_i_eq(s.sqin_count, g["sqin_count"])) return "sqin_count";
    if (!opt_i_eq(s.sqout_count, g["sqout_count"])) return "sqout_count";
    if (!opt_i_eq(s.pathcount, g["pathcount"])) return "pathcount";
    return "";
}

}  // namespace

TEST_CASE("prepare_row matches golden's store block across the corpus and config matrix") {
    const golden::json idx = golden::index();
    int checks = 0, mismatches = 0;

    for (const auto& entry : idx) {
        const std::string relpath = entry["relpath"].get<std::string>();
        const std::string slug = entry["slug"].get<std::string>();
        golden::json doc = golden::chart(slug);
        if (!doc.contains("analysis")) continue;

        const std::string path = std::string(HYDRA_INPUT_DIR) + "/" + relpath;
        Song song = load_songpath(path, "Expert", true, true);

        for (const Config& cfg : kMatrix) {
            const golden::json& gcfg = doc["analysis"][cfg.key];
            if (gcfg.contains("error")) continue;  // no store block for a failed analysis

            HydraRecord record =
                analyze_chart(song, cfg.capped, cfg.dmode, cfg.dvalue, cfg.ms);
            PreparedRow row = prepare_row("h", "mode", record, /*uncapped=*/!cfg.capped);

            const golden::json& gs = gcfg["store"];
            ++checks;

            std::string d;
            if (row.hyversion != joined_hyversion(gs["hyversion"])) d = "hyversion";
            else if (row.bestpath != gs["bestpath"].get<std::string>()) d = "bestpath";
            else d = diff_summary(row.summary, gs);

            if (!d.empty() && ++mismatches <= 8)
                CHECK_MESSAGE(false, relpath << " [" << cfg.key << "] " << d);
        }
    }

    CHECK(mismatches == 0);
    MESSAGE("checked " << checks << " prepare_row calls");
}

TEST_CASE("records round-trip through RecordStore across the corpus and config matrix") {
    RecordStore capped_store(":memory:", /*uncapped=*/false);
    RecordStore uncapped_store(":memory:", /*uncapped=*/true);

    const golden::json idx = golden::index();
    int checks = 0, mismatches = 0;

    for (const auto& entry : idx) {
        const std::string relpath = entry["relpath"].get<std::string>();
        const std::string slug = entry["slug"].get<std::string>();
        golden::json doc = golden::chart(slug);
        if (!doc.contains("analysis")) continue;

        const std::string path = std::string(HYDRA_INPUT_DIR) + "/" + relpath;
        Song song = load_songpath(path, "Expert", true, true);

        for (const Config& cfg : kMatrix) {
            const golden::json& gcfg = doc["analysis"][cfg.key];
            if (gcfg.contains("error")) continue;

            HydraRecord record =
                analyze_chart(song, cfg.capped, cfg.dmode, cfg.dvalue, cfg.ms);
            RecordStore& store = cfg.capped ? capped_store : uncapped_store;

            const std::string hyhash = slug + "|" + cfg.key;
            store.add_song(hyhash, "Title", "Artist", "Charter", song);
            store.add_record(hyhash, "mode", record);

            std::optional<HydraRecord> reloaded = store.get_record(hyhash, "mode");
            ++checks;
            if (!reloaded.has_value()) {
                if (++mismatches <= 8)
                    CHECK_MESSAGE(false, relpath << " [" << cfg.key << "] no row after add");
                continue;
            }

            const golden::json& gs = gcfg["store"];
            std::string bestpath =
                reloaded->paths.empty() ? std::string() : reloaded->best_path().pathstring();

            std::string d;
            if (bestpath != gs["bestpath"].get<std::string>()) {
                d = "reloaded bestpath";
            } else {
                d = diff_summary(summarize_record(*reloaded), gs);
                if (!d.empty()) d = "reloaded " + d;
            }

            // Timecodes are dropped to raw ticks by the blob and rebuilt by
            // restore_timecodes against the songmeta tempomap; check that
            // rebuild actually derives ms/measure position, not just ticks.
            if (d.empty() && !record.paths.empty() && record.best_path().has_activations()) {
                const Activation& orig = record.best_path().all_activations().front();
                const Activation& again = reloaded->best_path().all_activations().front();
                if (!again.timecode.has_value() ||
                    again.timecode->ticks() != orig.timecode->ticks() ||
                    again.timecode->ms() != orig.timecode->ms())
                    d = "restored timecode";
            }

            auto summary_row = store.get_summary(hyhash, "mode");
            if (d.empty() && (!summary_row.has_value() ||
                              summary_row->second != gs["bestpath"].get<std::string>()))
                d = "get_summary bestpath";

            if (!d.empty() && ++mismatches <= 8)
                CHECK_MESSAGE(false, relpath << " [" << cfg.key << "] " << d);
        }
    }

    CHECK(mismatches == 0);
    MESSAGE("checked " << checks << " round trips");
}

TEST_CASE("RecordStore maintenance: has_record, list_records, reindex, drop_stale_records") {
    const golden::json idx = golden::index();
    std::string relpath, path;
    for (const auto& entry : idx) {
        golden::json doc = golden::chart(entry["slug"].get<std::string>());
        if (doc.contains("analysis") &&
            !doc["analysis"]["capped.scores.10"].contains("error")) {
            relpath = entry["relpath"].get<std::string>();
            break;
        }
    }
    REQUIRE_FALSE(relpath.empty());
    path = std::string(HYDRA_INPUT_DIR) + "/" + relpath;

    Song song = load_songpath(path, "Expert", true, true);
    HydraRecord record = analyze_chart(song, true, 0, 10, std::nullopt);

    RecordStore store(":memory:", /*uncapped=*/false);
    CHECK_FALSE(store.has_record("h1", "Expert Pro Drums, 2x Bass"));

    store.add_song("h1", "Song A", "Artist A", "Charter A", song);
    store.add_record("h1", "Expert Pro Drums, 2x Bass", record);
    CHECK(store.has_record("h1", "Expert Pro Drums, 2x Bass"));

    std::vector<RecordListing> listing =
        store.list_records(std::nullopt, SortColumn::Score, true);
    REQUIRE(listing.size() == 1);
    CHECK(listing[0].ref_name == "Song A");
    CHECK(listing[0].bestpath == record.best_path().pathstring());

    auto [nsongs, nrecords] = store.counts();
    CHECK(nsongs == 1);
    CHECK(nrecords == 1);

    int touched = store.reindex();
    CHECK(touched == 1);
    std::vector<RecordListing> relisted =
        store.list_records(std::nullopt, SortColumn::Score, true);
    REQUIRE(relisted.size() == 1);
    CHECK(relisted[0].summary.score == listing[0].summary.score);

    // A row stamped with a different version is stale for this store.
    PreparedRow stale = prepare_row("h2", "Expert Pro Drums, 2x Bass", record, false);
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.counts().second == 2);

    int dropped = store.drop_stale_records();
    CHECK(dropped == 1);
    CHECK(store.counts().second == 1);
}
