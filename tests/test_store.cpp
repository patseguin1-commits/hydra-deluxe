// Tests for store/ (record_store.{h,cpp} + serialize.{h,cpp}): a record must
// round-trip through RecordStore (write, reload, restore timecodes)
// losslessly, across the corpus and the full config matrix.

#include "doctest.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/model.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/pather.h"
#include "store/record_store.h"
#include "store/serialize.h"

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

// The config matrix the GUI/CLI expose: score depth, points depth, the ms
// filter, and the uncapped edition.
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

// First field where two summaries differ, empty when equal.
std::string diff_summary(const PathSummary& a, const PathSummary& b) {
    if (a.score != b.score) return "score";
    if (a.actcount != b.actcount) return "actcount";
    if (a.maxskip != b.maxskip) return "maxskip";
    if (a.hardest_ms != b.hardest_ms) return "hardest_ms";
    if (a.avgmult != b.avgmult) return "avgmult";
    if (a.notecount != b.notecount) return "notecount";
    if (a.sqin_count != b.sqin_count) return "sqin_count";
    if (a.sqout_count != b.sqout_count) return "sqout_count";
    if (a.pathcount != b.pathcount) return "pathcount";
    return "";
}

}  // namespace

TEST_CASE("records round-trip through RecordStore across the corpus and config matrix") {
    RecordStore capped_store(":memory:", /*uncapped=*/false);
    RecordStore uncapped_store(":memory:", /*uncapped=*/true);

    int checks = 0, mismatches = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;

        for (const Config& cfg : kMatrix) {
            std::optional<HydraRecord> record;
            try {
                record = analyze_chart(song, cfg.capped, cfg.dmode, cfg.dvalue,
                                       cfg.ms);
            } catch (const ChartFileError&) {
                continue;  // charts the engine rejects have no row to store
            }
            RecordStore& store = cfg.capped ? capped_store : uncapped_store;

            const std::string hyhash = path + "|" + cfg.key;
            store.add_song(hyhash, "Title", "Artist", "Charter", song);
            store.add_record(hyhash, "mode", *record);

            std::optional<HydraRecord> reloaded = store.get_record(hyhash, "mode");
            ++checks;
            if (!reloaded.has_value()) {
                if (++mismatches <= 8)
                    CHECK_MESSAGE(false, path << " [" << cfg.key << "] no row after add");
                continue;
            }

            const std::string bestpath =
                record->paths.empty() ? std::string()
                                      : record->best_path().pathstring();
            const std::string re_bestpath =
                reloaded->paths.empty() ? std::string()
                                        : reloaded->best_path().pathstring();

            std::string d;
            if (re_bestpath != bestpath) {
                d = "reloaded bestpath";
            } else {
                d = diff_summary(summarize_record(*reloaded),
                                 summarize_record(*record));
                if (!d.empty()) d = "reloaded " + d;
            }

            // Timecodes are dropped to raw ticks by the blob and rebuilt by
            // restore_timecodes against the songmeta tempomap; check that
            // rebuild actually derives ms/measure position, not just ticks.
            if (d.empty() && !record->paths.empty() &&
                record->best_path().has_activations()) {
                const Activation& orig = record->best_path().all_activations().front();
                const Activation& again = reloaded->best_path().all_activations().front();
                if (!again.timecode.has_value() ||
                    again.timecode->ticks() != orig.timecode->ticks() ||
                    again.timecode->ms() != orig.timecode->ms())
                    d = "restored timecode";
            }

            auto summary_row = store.get_summary(hyhash, "mode");
            if (d.empty() && (!summary_row.has_value() ||
                              summary_row->second != bestpath))
                d = "get_summary bestpath";

            // The all-0 path rides in the same blob and is restored the same
            // way, but must stay out of the summary (pathcount above is
            // unchanged by it).
            if (d.empty()) {
                std::vector<const Path*> want = record->all_allzero_paths();
                std::vector<const Path*> got = reloaded->all_allzero_paths();
                if (want.size() != got.size()) {
                    d = "allzero count";
                } else {
                    for (size_t i = 0; i < want.size(); ++i) {
                        if (want[i]->pathstring() != got[i]->pathstring() ||
                            want[i]->totalscore() != got[i]->totalscore()) {
                            d = "allzero path " + std::to_string(i);
                            break;
                        }
                    }
                }
            }

            if (!d.empty() && ++mismatches <= 8)
                CHECK_MESSAGE(false, path << " [" << cfg.key << "] " << d);
        }
    }

    CHECK(mismatches == 0);
    REQUIRE(checks > 0);
    MESSAGE("checked " << checks << " round trips");
}

// The blob grew allzero_paths in format version 2. Version 1 blobs must still
// read, or bumping the format would silently strand every stored record.
TEST_CASE("record blob: version 2 carries all-0 paths and version 1 still reads") {
    std::optional<HydraRecord> record;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        try {
            HydraRecord r = analyze_chart(song, /*capped=*/true, 0, 4, 10.0);
            if (!r.allzero_paths.empty()) {
                record = std::move(r);
                break;
            }
        } catch (const ChartFileError&) {
            continue;
        }
    }
    REQUIRE_MESSAGE(record.has_value(), "no corpus chart produced an all-0 path");

    HydraRecord again = read_record(write_record(*record));
    CHECK(again.allzero_paths.size() == record->allzero_paths.size());
    CHECK(again.all_allzero_paths().size() == record->all_allzero_paths().size());
    CHECK(again.allzero_paths.front().pathstring() ==
          record->allzero_paths.front().pathstring());
    CHECK(again.allzero_paths.front().totalscore() ==
          record->allzero_paths.front().totalscore());

    // A version 1 blob is a version 2 blob without the trailing all-0 path
    // list. Build one from a record that has none, then relabel the header.
    HydraRecord bare = *record;
    bare.allzero_paths.clear();
    std::vector<uint8_t> blob = write_record(bare);
    REQUIRE(blob.size() > 8);
    std::vector<uint8_t> v1(blob.begin(), blob.end() - 4);  // drop the count 0
    v1[0] = 1;
    v1[1] = 0;
    v1[2] = 0;
    v1[3] = 0;

    HydraRecord old = read_record(v1);
    CHECK(old.allzero_paths.empty());
    CHECK(old.paths.size() == record->paths.size());
    CHECK(old.best_path().pathstring() == record->best_path().pathstring());

    // A blob from a future format is still refused.
    std::vector<uint8_t> future = write_record(*record);
    future[0] = kBlobFormatVersion + 1;
    CHECK_THROWS_AS(read_record(future), SerializeError);
}

TEST_CASE("RecordStore maintenance: has_record, list_records, reindex, drop_stale_records") {
    std::optional<Song> song;
    std::optional<HydraRecord> record;
    for (const std::string& path : corpus::chart_paths()) {
        Song s = load_songpath(path, true, true);
        if (s.is_empty()) continue;
        try {
            record = analyze_chart(s, true, 0, 10, std::nullopt);
        } catch (const ChartFileError&) {
            continue;
        }
        song = std::move(s);
        break;
    }
    REQUIRE(song.has_value());

    RecordStore store(":memory:", /*uncapped=*/false);
    CHECK_FALSE(store.has_record("h1", "Expert Pro Drums, 2x Bass"));

    store.add_song("h1", "Song A", "Artist A", "Charter A", *song);
    store.add_record("h1", "Expert Pro Drums, 2x Bass", *record);
    CHECK(store.has_record("h1", "Expert Pro Drums, 2x Bass"));

    std::vector<RecordListing> listing =
        store.list_records(std::nullopt, SortColumn::Score, true);
    REQUIRE(listing.size() == 1);
    CHECK(listing[0].ref_name == "Song A");
    CHECK(listing[0].bestpath == record->best_path().pathstring());

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
    PreparedRow stale = prepare_row("h2", "Expert Pro Drums, 2x Bass", *record, false);
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.counts().second == 2);

    int dropped = store.drop_stale_records();
    CHECK(dropped == 1);
    CHECK(store.counts().second == 1);
}
