// Tests for store/ (record_store.{h,cpp} + serialize.{h,cpp}): a record must
// round-trip through RecordStore (write, reload, restore timecodes)
// losslessly, across the corpus and the full config matrix.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <sqlite3.h>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <optional>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/winstr.h"
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
    std::optional<int> cap;  // nullopt = Auto
    DepthMode dmode;
    int dvalue;
    std::optional<double> ms;
};

// The config matrix the GUI/CLI expose: score depth, points depth, the ms
// filter, a fixed what-if cap, and Auto.
const std::vector<Config> kMatrix = {
    {"cap4.scores.10", 4, DepthMode::Scores, 10, std::nullopt},
    {"cap4.scores.200", 4, DepthMode::Scores, 200, std::nullopt},
    {"cap4.scores.0", 4, DepthMode::Scores, 0, std::nullopt},
    {"cap4.scores.1", 4, DepthMode::Scores, 1, std::nullopt},
    {"cap4.scores.3", 4, DepthMode::Scores, 3, std::nullopt},
    {"cap4.points.5000", 4, DepthMode::Points, 5000, std::nullopt},
    {"cap4.scores.200.ms5", 4, DepthMode::Scores, 200, 5.0},
    {"cap4.scores.200.ms20", 4, DepthMode::Scores, 200, 20.0},
    {"cap8.scores.200", 8, DepthMode::Scores, 200, std::nullopt},
    {"auto.scores.200", std::nullopt, DepthMode::Scores, 200, std::nullopt},
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
    RecordStore store(":memory:");

    int checks = 0, mismatches = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;

        for (const Config& cfg : kMatrix) {
            std::optional<HydraRecord> record;
            try {
                SearchSettings settings;
                settings.sp_cap = cfg.cap;
                settings.depth_mode = cfg.dmode;
                settings.depth_value = cfg.dvalue;
                settings.ms_filter = cfg.ms;
                record = analyze_chart(song, settings);
            } catch (const ChartFileError&) {
                continue;  // charts the engine rejects have no row to store
            }
            REQUIRE(record->sp_cap.has_value());
            const CapQuery cap = CapQuery::at(*record->sp_cap);

            const std::string hyhash = path + "|" + cfg.key;
            store.add_song(hyhash, "Title", "Artist", "Charter", song);
            store.add_record(RecordKey{hyhash, "mode", cap}, *record);

            RecordLookup lookup = store.get_record(RecordKey{hyhash, "mode", cap});
            ++checks;
            if (lookup.status != RecordStatus::Ready) {
                if (++mismatches <= 8)
                    CHECK_MESSAGE(false, path << " [" << cfg.key << "] no row after add");
                continue;
            }
            const std::optional<HydraRecord>& reloaded = lookup.record;

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
                // all_activations() returns by value; keep the vectors alive.
                const auto orig_acts = record->best_path().all_activations();
                const Activation& orig = orig_acts.front();
                const auto again_acts = reloaded->best_path().all_activations();
                const Activation& again = again_acts.front();
                if (!again.timecode.has_value() ||
                    again.timecode->ticks() != orig.timecode->ticks() ||
                    again.timecode->ms() != orig.timecode->ms())
                    d = "restored timecode";
                // The transfer scales ride in the v3 blob bit-exactly.
                else if (again.transfer_pre.early != orig.transfer_pre.early ||
                         again.transfer_pre.late != orig.transfer_pre.late ||
                         again.transfer_post.early != orig.transfer_post.early ||
                         again.transfer_post.late != orig.transfer_post.late)
                    d = "restored transfer scales";
            }

            SummaryLookup summary_row = store.get_summary(RecordKey{hyhash, "mode", cap});
            if (d.empty() && (summary_row.status != RecordStatus::Ready ||
                              summary_row.bestpath != bestpath))
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

// The blob grew allzero_paths in format version 2 and per-activation transfer
// scales in version 3. Older blobs must still read (scales default to 1.0),
// or bumping the format would silently strand every stored record.
TEST_CASE("record blob: v3 carries transfer scales, v1/v2 still read") {
    std::optional<HydraRecord> record;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        try {
            SearchSettings settings;
            settings.sp_cap = 4;
            settings.depth_mode = DepthMode::Scores;
            settings.depth_value = 4;
            settings.ms_filter = 10.0;
            HydraRecord r = analyze_chart(song, settings);
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

    // Version 3 activations carry the transfer scales bit-exactly.
    const auto orig_acts = record->best_path().all_activations();
    const auto again_acts = again.best_path().all_activations();
    const Activation& orig_act = orig_acts.front();
    const Activation& again_act = again_acts.front();
    CHECK(again_act.transfer_pre.early == orig_act.transfer_pre.early);
    CHECK(again_act.transfer_pre.late == orig_act.transfer_pre.late);
    CHECK(again_act.transfer_post.early == orig_act.transfer_post.early);
    CHECK(again_act.transfer_post.late == orig_act.transfer_post.late);

    // Pre-v3 blobs have no per-activation transfer scales. This synthetic old
    // blob is hand-rolled with the primitives on purpose — an independent
    // spelling of the v2 layout, so a symmetric bug in the versioned writer
    // and reader can't hide. The versioned-writer round-trip below
    // cross-checks write_record(record, 2) against the same reader.
    BinaryWriter w;
    w.u32(2);                 // version
    w.opt_f64(std::nullopt);  // ms_limit
    w.opt_i32(std::nullopt);  // sp_cap
    w.boolean(true);          // sp_cap_converged
    w.u32(1);                 // one path
    w.u32(0);                 //   no multsqueezes
    w.u32(1);                 //   one activation
    w.opt_i32(0);             //     skips
    w.i64(960);               //     timecode ticks
    w.opt_str(std::nullopt);  //     chord
    w.opt_i32(4);             //     sp_meter
    w.opt_i32(std::nullopt);  //     frontend_points
    w.u32(0);                 //     no backends
    w.u32(1);                 //     one sqinout:
    w.u8(1);                  //       SqOut
    w.f64(-12.0);             //       offset_ms
    w.opt_f64(300.0);         //     e_offset (not e-critical)
    w.i64(100);               //   score_base
    w.i64(0);                 //   score_combo
    w.i64(0);                 //   score_sp
    w.i64(0);                 //   score_solo
    w.i64(0);                 //   score_accents
    w.i64(0);                 //   score_ghosts
    w.i32(10);                //   notecount
    w.i32(0);                 //   leftover_sp
    w.i32(0);                 //   skipped_ghosts
    w.i32(0);                 //   skipped_accents
    w.u32(0);                 //   no variants
    w.opt_i32(std::nullopt);  //   var_point
    w.u32(0);                 // no allzero paths (v2 tail)

    HydraRecord old2 = read_record(w.bytes);
    REQUIRE(old2.paths.size() == 1);
    const auto a2_acts = old2.best_path().all_activations();
    const Activation& a2 = a2_acts.front();
    // Missing scales default to the flat-tempo identity...
    CHECK(a2.transfer_pre.early == 1.0);
    CHECK(a2.transfer_pre.late == 1.0);
    CHECK(a2.transfer_post.early == 1.0);
    CHECK(a2.transfer_post.late == 1.0);
    // ...and difficulty stays the raw 12 ms gap (scales are display-only).
    CHECK(*old2.best_path().difficulty() == doctest::Approx(12.0));

    // A version 1 blob is the same layout without the trailing all-0 list.
    std::vector<uint8_t> v1(w.bytes.begin(), w.bytes.end() - 4);
    v1[0] = 1;
    HydraRecord old1 = read_record(v1);
    CHECK(old1.allzero_paths.empty());
    CHECK(old1.paths.size() == 1);
    CHECK(old1.best_path().pathstring() == old2.best_path().pathstring());

    // The versioned writer produces byte-for-byte what the hand-rolled
    // spelling produced: the write and read gates cannot drift apart.
    HydraRecord same = old2;
    CHECK(write_record(same, 2) == w.bytes);

    // A v1 write drops the all-0 tail; reading it back keeps the paths.
    HydraRecord old1w = read_record(write_record(same, 1));
    CHECK(old1w.allzero_paths.empty());
    CHECK(old1w.paths.size() == 1);

    // The writer refuses versions outside 1..kBlobFormatVersion.
    CHECK_THROWS_AS(write_record(same, 0), SerializeError);
    CHECK_THROWS_AS(write_record(same, kBlobFormatVersion + 1), SerializeError);

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
            SearchSettings settings;
            settings.sp_cap = 4;
            settings.depth_mode = DepthMode::Scores;
            settings.depth_value = 10;
            settings.ms_filter = std::nullopt;
            record = analyze_chart(s, settings);
        } catch (const ChartFileError&) {
            continue;
        }
        song = std::move(s);
        break;
    }
    REQUIRE(song.has_value());

    const CapQuery at4 = CapQuery::at(4);
    RecordStore store(":memory:");
    CHECK_FALSE(store.has_record(RecordKey{"h1", "Expert Pro Drums, 2x Bass", at4}));

    store.add_song("h1", "Song A", "Artist A", "Charter A", *song);
    store.add_record(RecordKey{"h1", "Expert Pro Drums, 2x Bass", at4}, *record);
    CHECK(store.has_record(RecordKey{"h1", "Expert Pro Drums, 2x Bass", at4}));
    CHECK_FALSE(store.has_record(RecordKey{"h1", "Expert Pro Drums, 2x Bass", CapQuery::at(8)}));
    CHECK_FALSE(store.has_record(RecordKey{"h1", "Expert Pro Drums, 2x Bass", CapQuery::automatic()}));

    std::vector<RecordListing> listing =
        store.list_records(std::nullopt, at4, Lens{}, SortColumn::Score, true);
    REQUIRE(listing.size() == 1);
    CHECK(listing[0].ref_name == "Song A");
    CHECK(listing[0].bestpath == record->best_path().pathstring());

    auto [nsongs, nrecords] = store.counts();
    CHECK(nsongs == 1);
    CHECK(nrecords == 1);

    int touched = store.reindex();
    CHECK(touched == 1);
    std::vector<RecordListing> relisted =
        store.list_records(std::nullopt, at4, Lens{}, SortColumn::Score, true);
    REQUIRE(relisted.size() == 1);
    CHECK(relisted[0].summary.score == listing[0].summary.score);

    // A row stamped with a different version is stale for this store: it
    // doesn't count as "already analyzed", and drop_stale_records removes it.
    PreparedRow stale =
        prepare_row(RecordKey{"h2", "Expert Pro Drums, 2x Bass", at4}, *record);
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.counts().second == 2);
    CHECK_FALSE(store.has_record(RecordKey{"h2", "Expert Pro Drums, 2x Bass", at4}));

    int dropped = store.drop_stale_records();
    CHECK(dropped == 1);
    CHECK(store.counts().second == 1);
}

namespace {

// One analyzed corpus chart, for the cap-identity tests below.
struct Fixture {
    Song song;
    HydraRecord record;  // at 4 bars
};
const Fixture& fixture() {
    static Fixture f = [] {
        for (const std::string& path : corpus::chart_paths()) {
            Song s = load_songpath(path, true, true);
            if (s.is_empty()) continue;
            try {
                SearchSettings settings;
                settings.sp_cap = 4;
                settings.depth_mode = DepthMode::Scores;
                settings.depth_value = 0;
                settings.ms_filter = std::nullopt;
                HydraRecord r = analyze_chart(s, settings);
                if (r.paths.empty()) continue;
                return Fixture{std::move(s), std::move(r)};
            } catch (const ChartFileError&) {
                continue;
            }
        }
        throw std::runtime_error("no corpus chart analyzed");
    }();
    return f;
}

// The same record relabeled as if it had run at another cap. The paths are
// the 4-bar paths, which is fine: these tests check which row a lookup picks,
// not what is in it.
HydraRecord at_cap(int cap) {
    HydraRecord r = fixture().record;
    r.sp_cap = cap;
    return r;
}

std::string temp_db(const char* tag) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return hydra::wide_to_utf8(tmp) + "hydra_test_" + tag + "_" +
           std::to_string(GetCurrentProcessId()) + ".db";
}

// Writes a pre-1.6 database by hand: records keyed without sp_cap, one row
// stamped by the main edition and one by the Uncapped edition. The songmeta
// rows come from the real store (that table's shape never changed); only the
// records table is rebuilt in its old shape.
void write_legacy_db(const std::string& path, const std::string& main_stamp,
                     const std::string& uncapped_stamp, int uncapped_cap) {
    std::remove(path.c_str());
    {
        RecordStore seed(path);
        seed.add_song("legacy", "Legacy Song", "A", "C", fixture().song);
        seed.add_song("legacy_unc", "Legacy Song", "A", "C", fixture().song);
    }
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    auto exec = [&](const char* sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        std::string msg = err ? err : "";
        INFO(msg);
        REQUIRE(rc == SQLITE_OK);
    };
    exec("DROP TABLE IF EXISTS records; DROP TABLE IF EXISTS results;"
         "DROP TABLE IF EXISTS path_refs; DROP TABLE IF EXISTS paths;"
         "DROP TABLE IF EXISTS meta; PRAGMA user_version = 0;"
         "CREATE TABLE records (hyhash TEXT NOT NULL, chartmode TEXT NOT NULL,"
         " hyversion TEXT NOT NULL, bestpath TEXT NOT NULL, blob BLOB NOT NULL,"
         " score INTEGER, actcount INTEGER, maxskip INTEGER, hardest_ms REAL, avgmult REAL,"
         " notecount INTEGER, sqin_count INTEGER, sqout_count INTEGER, pathcount INTEGER,"
         " PRIMARY KEY (hyhash, chartmode));");

    auto insert = [&](const char* hash, const std::string& stamp, const HydraRecord& rec) {
        PreparedRow row = prepare_row(RecordKey{hash, "mode", CapQuery::automatic()}, rec);
        // The old table held one nested blob per record, not a structure blob.
        const std::vector<uint8_t> blob = write_record(rec);
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(db,
                    "INSERT INTO records (hyhash, chartmode, hyversion, bestpath, blob, score)"
                    " VALUES (?,?,?,?,?,?)", -1, &s, nullptr) == SQLITE_OK);
        sqlite3_bind_text(s, 1, hash, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, "mode", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, stamp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 4, row.bestpath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(s, 5, blob.data(), (int)blob.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 6, row.summary.score.value_or(0));
        REQUIRE(sqlite3_step(s) == SQLITE_DONE);
        sqlite3_finalize(s);
    };
    insert("legacy", main_stamp, at_cap(4));
    insert("legacy_unc", uncapped_stamp, at_cap(uncapped_cap));
    sqlite3_close(db);
}

// One integer straight out of a closed database file — how these tests look at
// the paths/path_refs tables without the store growing an accessor for them.
int64_t scalar(const std::string& path, const char* sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &s, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(s) == SQLITE_ROW);
    int64_t v = sqlite3_column_int64(s, 0);
    sqlite3_finalize(s);
    sqlite3_close(db);
    return v;
}

int user_version(const std::string& path) {
    return static_cast<int>(scalar(path, "PRAGMA user_version"));
}

// Writes a 1.6-era database by hand: one `records` table keyed by cap, no
// results/paths/path_refs. This is the shape the v1 -> v2 migration reads.
void write_v1_db(const std::string& path, const std::string& stamp, int cap) {
    std::remove(path.c_str());
    {
        RecordStore seed(path);
        seed.add_song("v1", "V1 Song", "A", "C", fixture().song);
        seed.add_song("other", "Other Song", "A", "C", fixture().song);
    }
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    auto exec = [&](const char* sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        std::string msg = err ? err : "";
        INFO(msg);
        REQUIRE(rc == SQLITE_OK);
    };
    exec("DROP TABLE IF EXISTS results; DROP TABLE IF EXISTS path_refs;"
         "DROP TABLE IF EXISTS paths; PRAGMA user_version = 1;"
         "CREATE TABLE records (hyhash TEXT NOT NULL, chartmode TEXT NOT NULL,"
         " hyversion TEXT NOT NULL, sp_cap INTEGER NOT NULL, bestpath TEXT NOT NULL,"
         " blob BLOB NOT NULL,"
         " score INTEGER, actcount INTEGER, maxskip INTEGER, hardest_ms REAL, avgmult REAL,"
         " notecount INTEGER, sqin_count INTEGER, sqout_count INTEGER, pathcount INTEGER,"
         " PRIMARY KEY (hyhash, chartmode, sp_cap));");

    HydraRecord rec = at_cap(cap);
    const std::vector<uint8_t> blob = write_record(rec);
    const std::string bestpath = rec.best_path().pathstring();
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(db,
                "INSERT INTO records (hyhash, chartmode, hyversion, sp_cap, bestpath, blob,"
                " score) VALUES (?,?,?,?,?,?,?)", -1, &s, nullptr) == SQLITE_OK);
    sqlite3_bind_text(s, 1, "v1", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, "mode", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 3, stamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(s, 4, cap);
    sqlite3_bind_text(s, 5, bestpath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(s, 6, blob.data(), (int)blob.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int64(s, 7, rec.best_path().totalscore());
    REQUIRE(sqlite3_step(s) == SQLITE_DONE);
    sqlite3_finalize(s);
    sqlite3_close(db);
}

// The two lenses the coexistence tests use: same chart, same cap, different
// searches. Lens C is a third nobody stored anything under.
const Lens kLensA = Lens::from(10, 0, 20);
const Lens kLensB = Lens::from(std::nullopt, 1, 5000);
const Lens kLensC = Lens::from(25, 0, 3);

// at_cap plus the ms limit lens A claims, so prepare_row's guard is satisfied.
HydraRecord at_cap_ms10(int cap) {
    HydraRecord r = at_cap(cap);
    r.ms_limit = 10.0;
    return r;
}

}  // namespace

TEST_CASE("records at different caps coexist; Auto picks the newest current one") {
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"h", "mode", CapQuery::at(4)}, at_cap(4));
    store.add_record(RecordKey{"h", "mode", CapQuery::at(32)}, at_cap(32));
    CHECK(store.counts().second == 2);

    // Exact lookups see exactly their cap.
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::at(4)}).record->sp_cap == 4);
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::at(32)}).record->sp_cap == 32);
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::at(8)}).status ==
          RecordStatus::NotAnalyzed);

    // Auto takes the newest row above 4 and counts it as already analyzed.
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic()}).record->sp_cap == 32);
    CHECK(store.has_record(RecordKey{"h", "mode", CapQuery::automatic()}));
    CHECK(store.get_summary(RecordKey{"h", "mode", CapQuery::automatic()}).status ==
          RecordStatus::Ready);

    // A stale 64-bar row does not outrank a current 32-bar one -- for single
    // lookups and for the set queries alike.
    PreparedRow stale = prepare_row(RecordKey{"h", "mode", CapQuery::at(64)}, at_cap(64));
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic()}).record->sp_cap == 32);
    std::vector<RecordListing> listed =
        store.list_records(std::nullopt, CapQuery::automatic(), Lens{}, SortColumn::Score, true);
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].sp_cap == 32);
    int seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::automatic(), Lens{},
                        [&](const RecordStore::BlobRow& meta, const HydraRecord*) {
                            CHECK(meta.sp_cap == 32);
                            ++seen;
                        });
    CHECK(seen == 1);

    // Asked for cap 64 exactly, that stale row is reported as stale: no
    // record comes back and its blob is never decoded.
    RecordLookup stale_lookup = store.get_record(RecordKey{"h", "mode", CapQuery::at(64)});
    CHECK(stale_lookup.status == RecordStatus::Stale);
    CHECK_FALSE(stale_lookup.record.has_value());
    CHECK(stale_lookup.hyversion == "0.0.0");
    CHECK(store.get_summary(RecordKey{"h", "mode", CapQuery::at(64)}).status == RecordStatus::Stale);

    // for_each_blob still yields the stale row (at its own cap), with a null
    // record pointer.
    int stale_seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::at(64), Lens{},
                        [&](const RecordStore::BlobRow& meta, const HydraRecord* rec) {
                            CHECK(meta.status == RecordStatus::Stale);
                            CHECK(rec == nullptr);
                            ++stale_seen;
                        });
    CHECK(stale_seen == 1);

    // With only a 4-bar row, Auto has nothing to reuse.
    RecordStore only4(":memory:");
    only4.add_song("h", "Song", "Artist", "Charter", fixture().song);
    only4.add_record(RecordKey{"h", "mode", CapQuery::at(4)}, at_cap(4));
    CHECK(only4.get_record(RecordKey{"h", "mode", CapQuery::automatic()}).status ==
          RecordStatus::NotAnalyzed);
    CHECK_FALSE(only4.has_record(RecordKey{"h", "mode", CapQuery::automatic()}));

    // reindex touches each cap's own row.
    CHECK(store.reindex() == 3);
    CHECK(store.list_records(std::nullopt, CapQuery::at(4), Lens{}, SortColumn::Score, true)[0]
              .summary.score == fixture().record.best_path().totalscore());
}

TEST_CASE("an Auto run that settles below an existing row becomes the Auto answer") {
    // The real sequence behind the bug: a chart already holds a tall row (an
    // imported uncapped run, or a what-if the user typed), then the user
    // presses Analyze under Auto and the ladder settles lower. The fresh row
    // is the one the user just paid for and the one that matches the current
    // depth / ms settings, so every Auto lookup must show it -- not the
    // taller, older one.
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"h", "mode", CapQuery::at(32)}, at_cap(32));
    store.add_record(RecordKey{"h", "mode", CapQuery::automatic()}, at_cap(16));
    CHECK(store.counts().second == 2);

    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic()}).record->sp_cap == 16);
    std::vector<RecordListing> listed =
        store.list_records(std::nullopt, CapQuery::automatic(), Lens{}, SortColumn::Score, true);
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].sp_cap == 16);
    int seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::automatic(), Lens{},
                        [&](const RecordStore::BlobRow& meta, const HydraRecord*) {
                            CHECK(meta.sp_cap == 16);
                            ++seen;
                        });
    CHECK(seen == 1);

    // The taller row is still there for an explicit lookup.
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::at(32)}).record->sp_cap == 32);

    // Re-analyzing at the taller cap makes it the newest again.
    store.add_record(RecordKey{"h", "mode", CapQuery::at(32)}, at_cap(32));
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic()}).record->sp_cap == 32);
}

TEST_CASE("prepare_row refuses a key whose exact cap isn't the record's") {
    HydraRecord rec = at_cap(4);

    // An exact key that disagrees with the record would file the result under
    // a cap it was never analyzed at.
    CHECK_THROWS_AS(prepare_row(RecordKey{"h", "mode", CapQuery::at(8)}, rec),
                    std::invalid_argument);

    // An automatic key takes whatever cap the record carries.
    PreparedRow row = prepare_row(RecordKey{"h", "mode", CapQuery::automatic()}, rec);
    CHECK(row.sp_cap == 4);

    // So does the matching exact key.
    CHECK(prepare_row(RecordKey{"h", "mode", CapQuery::at(4)}, rec).sp_cap == 4);
}

TEST_CASE("prepare_row refuses a key whose ms limit isn't the record's") {
    // Same failure mode as the cap guard: the row would claim settings the
    // search never ran under, and every later lookup would believe it.
    HydraRecord none = at_cap(4);  // analyzed with no ms limit
    CHECK_THROWS_AS(prepare_row(RecordKey{"h", "mode", CapQuery::at(4), kLensA}, none),
                    std::invalid_argument);

    HydraRecord ten = at_cap_ms10(4);
    CHECK_THROWS_AS(prepare_row(RecordKey{"h", "mode", CapQuery::at(4), kLensC}, ten),
                    std::invalid_argument);

    // The matching lens is fine, and rides onto the row.
    PreparedRow row = prepare_row(RecordKey{"h", "mode", CapQuery::at(4), kLensA}, ten);
    CHECK(row.lens == kLensA);

    // A lens with the limit off says nothing about the record's ms_limit, so
    // there is nothing to disagree with.
    CHECK(prepare_row(RecordKey{"h", "mode", CapQuery::at(4), kLensB}, ten).lens == kLensB);
}

TEST_CASE("RecordKey compares on every part of the identity") {
    const RecordKey key{"h", "mode", CapQuery::at(4), kLensA};
    CHECK(key == RecordKey{"h", "mode", CapQuery::at(4), kLensA});
    CHECK_FALSE(key == RecordKey{"other", "mode", CapQuery::at(4), kLensA});
    CHECK_FALSE(key == RecordKey{"h", "other mode", CapQuery::at(4), kLensA});
    CHECK_FALSE(key == RecordKey{"h", "mode", CapQuery::at(8), kLensA});
    CHECK_FALSE(key == RecordKey{"h", "mode", CapQuery::automatic(), kLensA});
    CHECK_FALSE(key == RecordKey{"h", "mode", CapQuery::at(4), kLensB});
    CHECK(CapQuery::at(4) != CapQuery::automatic());
    CHECK(CapQuery::automatic() == CapQuery::from_setting(std::nullopt));
    CHECK(CapQuery::at(8) == CapQuery::from_setting(8));

    // Each of the lens's four fields is part of the identity...
    CHECK(kLensA == Lens::from(10, 0, 20));
    CHECK(kLensA != Lens::from(11, 0, 20));
    CHECK(kLensA != Lens::from(10, 1, 20));
    CHECK(kLensA != Lens::from(10, 0, 21));
    CHECK(kLensA != Lens::from(std::nullopt, 0, 20));

    // ...except the ms value when the limit is off, which the engine ignores:
    // "off at 10" and "off at 42" ran the same search.
    CHECK(Lens::from(std::nullopt, 0, 4) == Lens::from(std::nullopt, 0, 4));
    CHECK(Lens::from(std::nullopt, 0, 4).ms_value == 0);

    // The sentinel is its own thing and never equals a real lens.
    CHECK(Lens::sentinel().is_sentinel());
    CHECK_FALSE(Lens{}.is_sentinel());
    CHECK(Lens::sentinel() != Lens{});
}

TEST_CASE("a current-version record with no paths is Ready, not Stale") {
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    HydraRecord empty = at_cap(4);
    empty.paths.clear();
    store.add_record(RecordKey{"h", "mode", CapQuery::at(4)}, empty);

    // "Analyzed, and nothing survived" is a real result, so it reads back as
    // Ready with an empty record -- the status, not the path count, is what
    // says whether a row is usable.
    RecordLookup lookup = store.get_record(RecordKey{"h", "mode", CapQuery::at(4)});
    CHECK(lookup.status == RecordStatus::Ready);
    REQUIRE(lookup.record.has_value());
    CHECK(lookup.record->paths.empty());
    CHECK(lookup.hyversion == current_record_version());
    CHECK(store.get_summary(RecordKey{"h", "mode", CapQuery::at(4)}).status == RecordStatus::Ready);
    CHECK(store.has_record(RecordKey{"h", "mode", CapQuery::at(4)}));
}

TEST_CASE("a pre-1.6 database migrates to the cap key on open") {
    const std::string path = temp_db("migrate");
    const std::string current = current_record_version();
    write_legacy_db(path, current, current + ".uncapped", 16);

    {
        RecordStore store(path);
        // Both rows survive, each under the cap its blob records. Neither
        // says which ms limit or score range produced it, so both land with
        // the sentinel lens and read Stale: there is a result here, but
        // nothing that says what question it answered.
        CHECK(store.counts().second == 2);
        RecordLookup main_row = store.get_record(RecordKey{"legacy", "mode", CapQuery::at(4)});
        CHECK(main_row.status == RecordStatus::Stale);
        CHECK_FALSE(main_row.record.has_value());
        RecordLookup unc_row = store.get_record(RecordKey{"legacy_unc", "mode", CapQuery::at(16)});
        CHECK(unc_row.status == RecordStatus::Stale);
        CHECK_FALSE(unc_row.record.has_value());
        CHECK_FALSE(store.has_record(RecordKey{"legacy_unc", "mode", CapQuery::automatic()}));
    }
    CHECK(user_version(path) == 2);

    // A second open is a no-op (the records table is gone).
    {
        RecordStore again(path);
        CHECK(again.counts().second == 2);
    }
    std::remove(path.c_str());
}

TEST_CASE("a 1.6 database migrates to results + shared paths on open") {
    const std::string path = temp_db("migrate_v1");
    write_v1_db(path, current_record_version(), 8);
    CHECK(user_version(path) == 1);

    {
        RecordStore store(path);
        // The row is kept, at its cap, as a sentinel: unknown settings, so it
        // answers no lens and never has its blob decoded.
        CHECK(store.counts().second == 1);
        for (const Lens& lens : {Lens{}, kLensA, kLensB}) {
            RecordLookup row = store.get_record(RecordKey{"v1", "mode", CapQuery::at(8), lens});
            CHECK(row.status == RecordStatus::Stale);
            CHECK_FALSE(row.record.has_value());
            CHECK(store.get_summary(RecordKey{"v1", "mode", CapQuery::at(8), lens}).status ==
                  RecordStatus::Stale);
            CHECK_FALSE(store.has_record(RecordKey{"v1", "mode", CapQuery::at(8), lens}));
        }

        // The set queries still show it, with no record to hand out.
        std::vector<RecordListing> listed =
            store.list_records(std::nullopt, CapQuery::at(8), kLensA, SortColumn::Score, true);
        CHECK(listed.size() == 1);
        int seen = 0;
        store.for_each_blob(std::nullopt, CapQuery::at(8), kLensA,
                            [&](const RecordStore::BlobRow& meta, const HydraRecord* rec) {
                                CHECK(meta.status == RecordStatus::Stale);
                                CHECK(rec == nullptr);
                                ++seen;
                            });
        CHECK(seen == 1);

        // A real run at that cap is the answer the placeholder stood in for.
        store.add_record(RecordKey{"v1", "mode", CapQuery::at(8), kLensA}, at_cap_ms10(8));
        CHECK(store.counts().second == 1);
        CHECK(store.get_record(RecordKey{"v1", "mode", CapQuery::at(8), kLensA}).status ==
              RecordStatus::Ready);
        CHECK(store.has_record(RecordKey{"v1", "mode", CapQuery::at(8), kLensA}));
    }
    CHECK(user_version(path) == 2);
    // The migration is one-way: nothing re-reads a records table afterwards.
    CHECK(scalar(path, "SELECT COUNT(*) FROM sqlite_master WHERE name='records'") == 0);
    std::remove(path.c_str());
}

TEST_CASE("import_legacy_uncapped copies current-version rows once, under their cap") {
    const std::string main_path = temp_db("import_main");
    const std::string unc_path = temp_db("import_unc");
    const std::string current = current_record_version();
    std::remove(main_path.c_str());
    // The old uncapped file: one current row (cap 64) and one stale row.
    write_legacy_db(unc_path, "0.0.0.uncapped", current + ".uncapped", 64);

    {
        RecordStore store(main_path);
        store.add_song("legacy_unc", "Song", "Artist", "Charter", fixture().song);
        // A row this build already made at the same key must win.
        HydraRecord mine = at_cap(64);
        mine.paths.clear();
        store.add_record(RecordKey{"legacy_unc", "mode", CapQuery::at(64)}, mine);

        CHECK(store.import_legacy_uncapped(unc_path) == 0);  // same-key row kept
        // An empty-paths record this build wrote is a real result, not stale.
        RecordLookup kept = store.get_record(RecordKey{"legacy_unc", "mode", CapQuery::at(64)});
        CHECK(kept.status == RecordStatus::Ready);
        CHECK(kept.record->paths.empty());
        // Done once: a second call copies nothing even with the row gone.
        CHECK(store.import_legacy_uncapped(unc_path) == 0);
    }
    std::remove(main_path.c_str());

    {
        RecordStore store(main_path);
        CHECK(store.import_legacy_uncapped(unc_path) == 1);
        // The old file records the cap and nothing else, so the copied row is
        // a sentinel: a result exists, but not the settings behind it, and it
        // reads Stale until the user re-analyzes.
        RecordLookup row = store.get_record(RecordKey{"legacy_unc", "mode", CapQuery::at(64)});
        CHECK(row.status == RecordStatus::Stale);
        CHECK_FALSE(row.record.has_value());
        CHECK(store.counts().first == 1);  // its song came along (not the stale one's)
        // The stale row (0.0.0) stayed behind.
        CHECK(store.get_record(RecordKey{"legacy", "mode", CapQuery::at(4)}).status ==
              RecordStatus::NotAnalyzed);
        CHECK(store.import_legacy_uncapped(unc_path) == 0);
        // A missing file is not an error and does not mark the import done.
        RecordStore fresh(":memory:");
        CHECK(fresh.import_legacy_uncapped(unc_path + ".missing") == 0);
    }
    std::remove(main_path.c_str());
    std::remove(unc_path.c_str());
}

// ---- lens identity --------------------------------------------------------
//
// The lens is the rest of a result's identity: the ms limit and the score
// range it ran under. These cases pin that two lenses coexist, that each
// lookup gets its own answer, and that the shared paths table stays honest
// while results come and go.

TEST_CASE("the same chart at the same cap keeps one result per lens") {
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);

    // The two records differ in the one field that says which search ran:
    // lens A's ms limit is 10, lens B's is off.
    store.add_record(RecordKey{"h", "mode", CapQuery::at(4), kLensA}, at_cap_ms10(4));
    store.add_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB}, at_cap(4));
    CHECK(store.counts().second == 2);

    RecordLookup a = store.get_record(RecordKey{"h", "mode", CapQuery::at(4), kLensA});
    REQUIRE(a.status == RecordStatus::Ready);
    CHECK(a.record->ms_limit == 10.0);
    RecordLookup b = store.get_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB});
    REQUIRE(b.status == RecordStatus::Ready);
    CHECK_FALSE(b.record->ms_limit.has_value());

    // A lens nobody ran under has no answer here, and no stored row stands in
    // for it -- this is what stops a batch run skipping the chart.
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::at(4), kLensC}).status ==
          RecordStatus::NotAnalyzed);
    CHECK(store.has_record(RecordKey{"h", "mode", CapQuery::at(4), kLensA}));
    CHECK(store.has_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB}));
    CHECK_FALSE(store.has_record(RecordKey{"h", "mode", CapQuery::at(4), kLensC}));

    // Re-running one lens replaces that row and leaves the other alone.
    HydraRecord redone = at_cap_ms10(4);
    redone.paths.clear();
    redone.allzero_paths.clear();
    store.add_record(RecordKey{"h", "mode", CapQuery::at(4), kLensA}, redone);
    CHECK(store.counts().second == 2);
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::at(4), kLensA})
              .record->paths.empty());
    CHECK_FALSE(store.get_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB})
                    .record->paths.empty());
}

TEST_CASE("Auto answers inside the lens it was asked about") {
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"h", "mode", CapQuery::at(32), kLensA}, at_cap_ms10(32));
    store.add_record(RecordKey{"h", "mode", CapQuery::at(64), kLensB}, at_cap(64));

    // Auto reaches for the tall rows, but only the ones its own lens wrote.
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic(), kLensA})
              .record->sp_cap == 32);
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic(), kLensB})
              .record->sp_cap == 64);
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic(), kLensC}).status ==
          RecordStatus::NotAnalyzed);
    CHECK(store.has_record(RecordKey{"h", "mode", CapQuery::automatic(), kLensA}));
    CHECK_FALSE(store.has_record(RecordKey{"h", "mode", CapQuery::automatic(), kLensC}));

    auto auto_caps = [&](const Lens& lens) {
        std::vector<int> caps;
        for (const RecordListing& r : store.list_records(std::nullopt, CapQuery::automatic(),
                                                         lens, SortColumn::Score, true))
            caps.push_back(r.sp_cap);
        return caps;
    };
    CHECK(auto_caps(kLensA) == std::vector<int>{32});
    CHECK(auto_caps(kLensB) == std::vector<int>{64});
    CHECK(auto_caps(kLensC).empty());

    // A stale 128-bar row in lens A does not outrank the current 32-bar one.
    PreparedRow stale =
        prepare_row(RecordKey{"h", "mode", CapQuery::at(128), kLensA}, at_cap_ms10(128));
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic(), kLensA})
              .record->sp_cap == 32);
    CHECK(auto_caps(kLensA) == std::vector<int>{32});

    // An Auto run that settles below the existing row is still the answer --
    // per lens, so the other lens's taller row is untouched.
    store.add_record(RecordKey{"h", "mode", CapQuery::automatic(), kLensA}, at_cap_ms10(16));
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic(), kLensA})
              .record->sp_cap == 16);
    CHECK(auto_caps(kLensA) == std::vector<int>{16});
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic(), kLensB})
              .record->sp_cap == 64);
    CHECK(auto_caps(kLensB) == std::vector<int>{64});
}

TEST_CASE("a path stored under two lenses is stored once") {
    const std::string path = temp_db("dedup");
    std::remove(path.c_str());

    {
        RecordStore store(path);
        store.add_song("h", "Song", "Artist", "Charter", fixture().song);
        store.add_record(RecordKey{"h", "mode", CapQuery::at(4), kLensA}, at_cap_ms10(4));
    }
    const int64_t nodes = scalar(path, "SELECT COUNT(*) FROM paths");
    const int64_t refs = scalar(path, "SELECT COUNT(*) FROM path_refs");
    REQUIRE(nodes > 0);
    CHECK(refs == nodes);

    {
        RecordStore store(path);
        store.add_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB}, at_cap(4));
    }
    // The second result names the identical nodes, so only the references
    // grow: a path is written once no matter how many results point at it.
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths") == nodes);
    CHECK(scalar(path, "SELECT COUNT(*) FROM path_refs") == refs * 2);
    std::remove(path.c_str());
}

TEST_CASE("replacing one lens's result leaves the other's bytes untouched") {
    const std::string path = temp_db("gc");
    std::remove(path.c_str());

    std::vector<uint8_t> before;
    {
        RecordStore store(path);
        store.add_song("h", "Song", "Artist", "Charter", fixture().song);
        store.add_record(RecordKey{"h", "mode", CapQuery::at(4), kLensA}, at_cap_ms10(4));
        store.add_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB}, at_cap(4));
        before = write_record(
            *store.get_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB}).record);
    }
    const int64_t shared = scalar(path, "SELECT COUNT(*) FROM paths");
    REQUIRE(shared > 0);

    {
        RecordStore store(path);
        HydraRecord redone = at_cap_ms10(4);
        redone.paths.clear();
        redone.allzero_paths.clear();
        store.add_record(RecordKey{"h", "mode", CapQuery::at(4), kLensA}, redone);
        // Lens B loads exactly the bytes it loaded before: the collection that
        // followed A's rewrite took nothing B still points at.
        CHECK(write_record(*store.get_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB})
                                .record) == before);
    }
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths") == shared);

    // With B replaced too, nothing points at those nodes and they go.
    {
        RecordStore store(path);
        HydraRecord redone = at_cap(4);
        redone.paths.clear();
        redone.allzero_paths.clear();
        store.add_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB}, redone);
    }
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths") == 0);
    CHECK(scalar(path, "SELECT COUNT(*) FROM path_refs") == 0);

    // drop_stale_records collects the orphans it makes.
    {
        RecordStore store(path);
        PreparedRow stale =
            prepare_row(RecordKey{"h", "mode", CapQuery::at(8), kLensB}, at_cap(8));
        stale.hyversion = "0.0.0";
        store.add_row(stale);
    }
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths") == shared);
    {
        RecordStore store(path);
        CHECK(store.drop_stale_records() == 1);
    }
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths") == 0);
    CHECK(scalar(path, "SELECT COUNT(*) FROM path_refs") == 0);
    std::remove(path.c_str());
}

TEST_CASE("a current-version write purges the chart's old-version rows and their paths") {
    const std::string path = temp_db("purge");
    std::remove(path.c_str());

    {
        RecordStore store(path);
        store.add_song("h", "Song", "Artist", "Charter", fixture().song);
        store.add_song("other", "Other", "Artist", "Charter", fixture().song);
        for (const char* hash : {"h", "other"}) {
            PreparedRow old_row =
                prepare_row(RecordKey{hash, "mode", CapQuery::at(4), kLensB}, at_cap(4));
            old_row.hyversion = "0.0.0";
            store.add_row(old_row);
        }
        CHECK(store.counts().second == 2);
    }
    const int64_t others = scalar(path, "SELECT COUNT(*) FROM paths WHERE hyhash='other'");
    REQUIRE(others > 0);
    REQUIRE(scalar(path, "SELECT COUNT(*) FROM paths WHERE hyhash='h'") == others);

    {
        RecordStore store(path);
        HydraRecord fresh = at_cap_ms10(32);
        fresh.paths.clear();
        fresh.allzero_paths.clear();
        store.add_record(RecordKey{"h", "mode", CapQuery::at(32), kLensA}, fresh);

        // This build cannot read what another version wrote for this chart, so
        // the write supersedes it -- at every cap and lens...
        CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB}).status ==
              RecordStatus::NotAnalyzed);
        // ...while another chart's old row is none of this write's business.
        CHECK(store.get_record(RecordKey{"other", "mode", CapQuery::at(4), kLensB}).status ==
              RecordStatus::Stale);
        CHECK(store.counts().second == 2);
    }
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths WHERE hyhash='h'") == 0);
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths WHERE hyhash='other'") == others);
    std::remove(path.c_str());
}
