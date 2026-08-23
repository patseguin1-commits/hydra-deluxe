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
    int dmode;
    int dvalue;
    std::optional<double> ms;
};

// The config matrix the GUI/CLI expose: score depth, points depth, the ms
// filter, a fixed what-if cap, and Auto.
const std::vector<Config> kMatrix = {
    {"cap4.scores.10", 4, 0, 10, std::nullopt},
    {"cap4.scores.200", 4, 0, 200, std::nullopt},
    {"cap4.scores.0", 4, 0, 0, std::nullopt},
    {"cap4.scores.1", 4, 0, 1, std::nullopt},
    {"cap4.scores.3", 4, 0, 3, std::nullopt},
    {"cap4.points.5000", 4, 1, 5000, std::nullopt},
    {"cap4.scores.200.ms5", 4, 0, 200, 5.0},
    {"cap4.scores.200.ms20", 4, 0, 200, 20.0},
    {"cap8.scores.200", 8, 0, 200, std::nullopt},
    {"auto.scores.200", std::nullopt, 0, 200, std::nullopt},
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
                record = analyze_chart(song, cfg.cap, cfg.dmode, cfg.dvalue, cfg.ms);
            } catch (const ChartFileError&) {
                continue;  // charts the engine rejects have no row to store
            }
            REQUIRE(record->sp_cap.has_value());
            const CapQuery cap = CapQuery::at(*record->sp_cap);

            const std::string hyhash = path + "|" + cfg.key;
            store.add_song(hyhash, "Title", "Artist", "Charter", song);
            store.add_record(hyhash, "mode", *record);

            RecordLookup lookup = store.get_record(hyhash, "mode", cap);
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
                const Activation& orig = record->best_path().all_activations().front();
                const Activation& again = reloaded->best_path().all_activations().front();
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

            SummaryLookup summary_row = store.get_summary(hyhash, "mode", cap);
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
            HydraRecord r = analyze_chart(song, /*sp_cap=*/4, 0, 4, 10.0);
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
    const Activation& orig_act = record->best_path().all_activations().front();
    const Activation& again_act = again.best_path().all_activations().front();
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
    const Activation& a2 = old2.best_path().all_activations().front();
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
            record = analyze_chart(s, 4, 0, 10, std::nullopt);
        } catch (const ChartFileError&) {
            continue;
        }
        song = std::move(s);
        break;
    }
    REQUIRE(song.has_value());

    const CapQuery at4 = CapQuery::at(4);
    RecordStore store(":memory:");
    CHECK_FALSE(store.has_record("h1", "Expert Pro Drums, 2x Bass", at4));

    store.add_song("h1", "Song A", "Artist A", "Charter A", *song);
    store.add_record("h1", "Expert Pro Drums, 2x Bass", *record);
    CHECK(store.has_record("h1", "Expert Pro Drums, 2x Bass", at4));
    CHECK_FALSE(store.has_record("h1", "Expert Pro Drums, 2x Bass", CapQuery::at(8)));
    CHECK_FALSE(store.has_record("h1", "Expert Pro Drums, 2x Bass", CapQuery::automatic()));

    std::vector<RecordListing> listing =
        store.list_records(std::nullopt, at4, SortColumn::Score, true);
    REQUIRE(listing.size() == 1);
    CHECK(listing[0].ref_name == "Song A");
    CHECK(listing[0].bestpath == record->best_path().pathstring());

    auto [nsongs, nrecords] = store.counts();
    CHECK(nsongs == 1);
    CHECK(nrecords == 1);

    int touched = store.reindex();
    CHECK(touched == 1);
    std::vector<RecordListing> relisted =
        store.list_records(std::nullopt, at4, SortColumn::Score, true);
    REQUIRE(relisted.size() == 1);
    CHECK(relisted[0].summary.score == listing[0].summary.score);

    // A row stamped with a different version is stale for this store: it
    // doesn't count as "already analyzed", and drop_stale_records removes it.
    PreparedRow stale = prepare_row("h2", "Expert Pro Drums, 2x Bass", *record);
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.counts().second == 2);
    CHECK_FALSE(store.has_record("h2", "Expert Pro Drums, 2x Bass", at4));

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
                HydraRecord r = analyze_chart(s, 4, 0, 0, std::nullopt);
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
    exec("DROP TABLE records; DROP TABLE meta; PRAGMA user_version = 0;"
         "CREATE TABLE records (hyhash TEXT NOT NULL, chartmode TEXT NOT NULL,"
         " hyversion TEXT NOT NULL, bestpath TEXT NOT NULL, blob BLOB NOT NULL,"
         " score INTEGER, actcount INTEGER, maxskip INTEGER, hardest_ms REAL, avgmult REAL,"
         " notecount INTEGER, sqin_count INTEGER, sqout_count INTEGER, pathcount INTEGER,"
         " PRIMARY KEY (hyhash, chartmode));");

    auto insert = [&](const char* hash, const std::string& stamp, const HydraRecord& rec) {
        PreparedRow row = prepare_row(hash, "mode", rec);
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(db,
                    "INSERT INTO records (hyhash, chartmode, hyversion, bestpath, blob, score)"
                    " VALUES (?,?,?,?,?,?)", -1, &s, nullptr) == SQLITE_OK);
        sqlite3_bind_text(s, 1, hash, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, "mode", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, stamp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 4, row.bestpath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(s, 5, row.blob.data(), (int)row.blob.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 6, row.summary.score.value_or(0));
        REQUIRE(sqlite3_step(s) == SQLITE_DONE);
        sqlite3_finalize(s);
    };
    insert("legacy", main_stamp, at_cap(4));
    insert("legacy_unc", uncapped_stamp, at_cap(uncapped_cap));
    sqlite3_close(db);
}

int user_version(const std::string& path) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &s, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(s) == SQLITE_ROW);
    int v = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    sqlite3_close(db);
    return v;
}

}  // namespace

TEST_CASE("records at different caps coexist; Auto picks the highest current one") {
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    store.add_record("h", "mode", at_cap(4));
    store.add_record("h", "mode", at_cap(32));
    CHECK(store.counts().second == 2);

    // Exact lookups see exactly their cap.
    CHECK(store.get_record("h", "mode", CapQuery::at(4)).record->sp_cap == 4);
    CHECK(store.get_record("h", "mode", CapQuery::at(32)).record->sp_cap == 32);
    CHECK(store.get_record("h", "mode", CapQuery::at(8)).status ==
          RecordStatus::NotAnalyzed);

    // Auto takes the highest cap above 4 and counts it as already analyzed.
    CHECK(store.get_record("h", "mode", CapQuery::automatic()).record->sp_cap == 32);
    CHECK(store.has_record("h", "mode", CapQuery::automatic()));
    CHECK(store.get_summary("h", "mode", CapQuery::automatic()).status ==
          RecordStatus::Ready);

    // A stale 64-bar row does not outrank a current 32-bar one -- for single
    // lookups and for the set queries alike.
    PreparedRow stale = prepare_row("h", "mode", at_cap(64));
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.get_record("h", "mode", CapQuery::automatic()).record->sp_cap == 32);
    std::vector<RecordListing> listed =
        store.list_records(std::nullopt, CapQuery::automatic(), SortColumn::Score, true);
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].sp_cap == 32);
    int seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::automatic(),
                        [&](const RecordStore::BlobRow& meta, const HydraRecord*) {
                            CHECK(meta.sp_cap == 32);
                            ++seen;
                        });
    CHECK(seen == 1);

    // Asked for cap 64 exactly, that stale row is reported as stale: no
    // record comes back and its blob is never decoded.
    RecordLookup stale_lookup = store.get_record("h", "mode", CapQuery::at(64));
    CHECK(stale_lookup.status == RecordStatus::Stale);
    CHECK_FALSE(stale_lookup.record.has_value());
    CHECK(stale_lookup.hyversion == "0.0.0");
    CHECK(store.get_summary("h", "mode", CapQuery::at(64)).status == RecordStatus::Stale);

    // for_each_blob still yields the stale row (at its own cap), with a null
    // record pointer.
    int stale_seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::at(64),
                        [&](const RecordStore::BlobRow& meta, const HydraRecord* rec) {
                            CHECK(meta.status == RecordStatus::Stale);
                            CHECK(rec == nullptr);
                            ++stale_seen;
                        });
    CHECK(stale_seen == 1);

    // With only a 4-bar row, Auto has nothing to reuse.
    RecordStore only4(":memory:");
    only4.add_song("h", "Song", "Artist", "Charter", fixture().song);
    only4.add_record("h", "mode", at_cap(4));
    CHECK(only4.get_record("h", "mode", CapQuery::automatic()).status ==
          RecordStatus::NotAnalyzed);
    CHECK_FALSE(only4.has_record("h", "mode", CapQuery::automatic()));

    // reindex touches each cap's own row.
    CHECK(store.reindex() == 3);
    CHECK(store.list_records(std::nullopt, CapQuery::at(4), SortColumn::Score, true)[0]
              .summary.score == fixture().record.best_path().totalscore());
}

TEST_CASE("a current-version record with no paths is Ready, not Stale") {
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    HydraRecord empty = at_cap(4);
    empty.paths.clear();
    store.add_record("h", "mode", empty);

    // "Analyzed, and nothing survived" is a real result, so it reads back as
    // Ready with an empty record -- the status, not the path count, is what
    // says whether a row is usable.
    RecordLookup lookup = store.get_record("h", "mode", CapQuery::at(4));
    CHECK(lookup.status == RecordStatus::Ready);
    REQUIRE(lookup.record.has_value());
    CHECK(lookup.record->paths.empty());
    CHECK(lookup.hyversion == current_record_version());
    CHECK(store.get_summary("h", "mode", CapQuery::at(4)).status == RecordStatus::Ready);
    CHECK(store.has_record("h", "mode", CapQuery::at(4)));
}

TEST_CASE("a pre-1.6 database migrates to the cap key on open") {
    const std::string path = temp_db("migrate");
    const std::string current = current_record_version();
    write_legacy_db(path, current, current + ".uncapped", 16);

    {
        RecordStore store(path);
        // Both rows survive, each under the cap its blob records, and the
        // Uncapped stamp is gone.
        CHECK(store.counts().second == 2);
        RecordLookup main_row = store.get_record("legacy", "mode", CapQuery::at(4));
        REQUIRE(main_row.status == RecordStatus::Ready);
        CHECK_FALSE(main_row.record->paths.empty());
        RecordLookup unc_row = store.get_record("legacy_unc", "mode", CapQuery::at(16));
        REQUIRE(unc_row.status == RecordStatus::Ready);  // restamped: reads as current
        CHECK_FALSE(unc_row.record->paths.empty());
        CHECK(store.has_record("legacy_unc", "mode", CapQuery::automatic()));
    }
    CHECK(user_version(path) == 1);

    // A second open is a no-op (the column exists).
    {
        RecordStore again(path);
        CHECK(again.counts().second == 2);
    }
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
        store.add_record("legacy_unc", "mode", mine);

        CHECK(store.import_legacy_uncapped(unc_path) == 0);  // same-key row kept
        // An empty-paths record this build wrote is a real result, not stale.
        RecordLookup kept = store.get_record("legacy_unc", "mode", CapQuery::at(64));
        CHECK(kept.status == RecordStatus::Ready);
        CHECK(kept.record->paths.empty());
        // Done once: a second call copies nothing even with the row gone.
        CHECK(store.import_legacy_uncapped(unc_path) == 0);
    }
    std::remove(main_path.c_str());

    {
        RecordStore store(main_path);
        CHECK(store.import_legacy_uncapped(unc_path) == 1);
        RecordLookup row = store.get_record("legacy_unc", "mode", CapQuery::at(64));
        REQUIRE(row.status == RecordStatus::Ready);  // restamped to the current version
        CHECK_FALSE(row.record->paths.empty());
        CHECK(store.counts().first == 1);  // its song came along (not the stale one's)
        // The stale row (0.0.0) stayed behind.
        CHECK(store.get_record("legacy", "mode", CapQuery::at(4)).status ==
              RecordStatus::NotAnalyzed);
        CHECK(store.import_legacy_uncapped(unc_path) == 0);
        // A missing file is not an error and does not mark the import done.
        RecordStore fresh(":memory:");
        CHECK(fresh.import_legacy_uncapped(unc_path + ".missing") == 0);
    }
    std::remove(main_path.c_str());
    std::remove(unc_path.c_str());
}
