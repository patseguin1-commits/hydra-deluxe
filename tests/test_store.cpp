// Tests for store/ (record_store.{h,cpp} + serialize.{h,cpp}): a record must
// round-trip through RecordStore (write, reload, restore timecodes)
// losslessly, across the corpus and the full config matrix.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/model.h"
#include "core/rules.h"
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

// The blob grew allzero_paths in format version 2, per-activation transfer
// scales in version 3, and each activation's deact_tick in version 4. Older
// blobs must still read (scales default to 1.0, deact_tick to unset), or
// bumping the format would silently strand every stored record.
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

    // Version 4 activations also carry the deactivation node D bit-exactly --
    // the search stamped it on this record, so a round trip must not drop it.
    REQUIRE(orig_act.deact_tick.has_value());
    CHECK(again_act.deact_tick == orig_act.deact_tick);

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
    // This hand-rolled blob stamps version 2, well under the version-4 gate,
    // so it carries no deact_tick bytes at all -- reading it back must leave
    // the field unset rather than inventing a value.
    CHECK_FALSE(a2.deact_tick.has_value());

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

// The version gate for deact_tick specifically: a v3 write has nowhere to put
// the field, so it must come back unset, not guessed at from an older blob. A
// small hand-built record is enough to prove this -- it doesn't need a real
// search, just one activation with a timecode and a deact_tick set.
TEST_CASE("record blob: a v3 write drops deact_tick, a v4 write keeps it") {
    Activation act;
    act.timecode = Timecode::raw(960);
    act.deact_tick = 4800;

    Path path;
    path.activations.push_back(act);

    HydraRecord record;
    record.sp_cap = 4;
    record.paths.push_back(path);

    // write_record's version argument defaults to kBlobFormatVersion; pass 3
    // explicitly to get the old layout.
    HydraRecord as_v3 = read_record(write_record(record, 3));
    REQUIRE(as_v3.paths.size() == 1);
    CHECK_FALSE(as_v3.best_path().all_activations().front().deact_tick.has_value());

    // The default (current) version carries it through.
    HydraRecord as_current = read_record(write_record(record));
    REQUIRE(as_current.paths.size() == 1);
    CHECK(as_current.best_path().all_activations().front().deact_tick == 4800);
}

// Same gate, one version later: clamp_tick is the field version 5 added, so a
// v4 write has nowhere to put it and must come back unset, while the default
// (v5) write keeps it.
TEST_CASE("record blob: a v4 write drops clamp_tick, a v5 write keeps it") {
    Activation act;
    act.timecode = Timecode::raw(960);
    act.clamp_tick = 3072;

    Path path;
    path.activations.push_back(act);

    HydraRecord record;
    record.sp_cap = 4;
    record.paths.push_back(path);

    HydraRecord as_v4 = read_record(write_record(record, 4));
    REQUIRE(as_v4.paths.size() == 1);
    CHECK_FALSE(
        as_v4.best_path().all_activations().front().clamp_tick.has_value());

    HydraRecord as_current = read_record(write_record(record));
    REQUIRE(as_current.paths.size() == 1);
    CHECK(as_current.best_path().all_activations().front().clamp_tick == 3072);
}

TEST_CASE("record blob: a v5 write drops sqout_tick and collected_phrase_ticks, a v6 write keeps them") {
    Activation act;
    act.timecode = Timecode::raw(960);
    act.deact_tick = 7680;
    act.sqout_tick = 7488;
    act.collected_phrase_ticks = {1920, 3840};
    Path path;
    path.activations.push_back(act);
    HydraRecord record;
    record.sp_cap = 4;
    record.rules_fingerprint = 0x0123456789abcdefull;
    record.paths.push_back(path);

    HydraRecord v6 = read_record(write_record(record, 6));
    const Activation& a6 = v6.paths.at(0).activations.at(0);
    REQUIRE(a6.sqout_tick.has_value());
    CHECK(*a6.sqout_tick == 7488);
    CHECK(a6.collected_phrase_ticks == std::vector<int64_t>{1920, 3840});
    CHECK(v6.rules_fingerprint == 0x0123456789abcdefull);

    // A v5 blob has none of the three. They read back empty, and the
    // fingerprint reads kNoRulesFingerprint, which no Rules value produces,
    // so an old record can never pass as analyzed under the current rules.
    HydraRecord v5 = read_record(write_record(record, 5));
    const Activation& a5 = v5.paths.at(0).activations.at(0);
    CHECK_FALSE(a5.sqout_tick.has_value());
    CHECK(a5.collected_phrase_ticks.empty());
    CHECK(v5.rules_fingerprint == core::kNoRulesFingerprint);
    // v5 still keeps what v5 always kept.
    REQUIRE(a5.deact_tick.has_value());
    CHECK(*a5.deact_tick == 7680);

    CHECK_THROWS_AS(write_record(record, kBlobFormatVersion + 1), SerializeError);
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

    // The listing does not. A stale row takes its chart out of the listing
    // entirely, so a report counts it as never analyzed rather than reading
    // numbers this build cannot vouch for.
    CHECK(store.list_records(std::nullopt, CapQuery::at(64), Lens{}, SortColumn::Score, true)
              .empty());

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

TEST_CASE("a legacy-imported sentinel row is left out of the listing") {
    // The case the reports hit: a row came across from an older database
    // carrying a result but not the settings behind it. Nothing can read it,
    // so the listing must not offer it as the chart's answer.
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    PreparedRow imported = prepare_row(RecordKey{"h", "mode", CapQuery::at(8)}, at_cap(8));
    imported.lens = Lens::sentinel();
    store.add_row(imported);
    CHECK(store.counts().second == 1);

    // A lookup still says the row is there, as Stale -- that is what the
    // library's status column shows.
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::at(8)}).status ==
          RecordStatus::Stale);
    CHECK(store.get_summary(RecordKey{"h", "mode", CapQuery::at(8)}).status ==
          RecordStatus::Stale);

    // The listing leaves the chart out, so a report counts it as unanalyzed.
    CHECK(store.list_records(std::nullopt, CapQuery::at(8), Lens{}, SortColumn::Score, true)
              .empty());

    // The export path keeps yielding it, with no record to hand out.
    int seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::at(8), Lens{},
                        [&](const RecordStore::BlobRow& meta, const HydraRecord* rec) {
                            CHECK(meta.status == RecordStatus::Stale);
                            CHECK(rec == nullptr);
                            ++seen;
                        });
    CHECK(seen == 1);

    // A real run at that cap is the answer the placeholder stood in for, and
    // the listing shows it.
    store.add_record(RecordKey{"h", "mode", CapQuery::at(8)}, at_cap(8));
    CHECK(store.list_records(std::nullopt, CapQuery::at(8), Lens{}, SortColumn::Score, true)
              .size() == 1);
}

TEST_CASE("a row in an older path format is Stale even when this build stamped it") {
    // The hyversion stamp alone cannot catch this. A released build writes
    // its own current hyversion no matter what structure format it emits, so
    // a row can carry today's version and still hold a tree this build no
    // longer knows how to decode. Only the structure format number inside
    // the blob can tell the two apart, so that is what has to gate Ready.
    RecordStore store(":memory:");
    store.add_song("old", "Song", "Artist", "Charter", fixture().song);
    PreparedRow old_format = prepare_row(RecordKey{"old", "mode", CapQuery::at(8)}, at_cap(8));
    REQUIRE(old_format.structure.size() >= 4);
    old_format.structure[0] = 1;
    old_format.structure[1] = 0;
    old_format.structure[2] = 0;
    old_format.structure[3] = 0;
    store.add_row(old_format);
    CHECK(store.counts().second == 1);

    // Stale everywhere a lookup can ask.
    CHECK_FALSE(store.has_record(RecordKey{"old", "mode", CapQuery::at(8)}));
    RecordLookup lookup = store.get_record(RecordKey{"old", "mode", CapQuery::at(8)});
    CHECK(lookup.status == RecordStatus::Stale);
    CHECK_FALSE(lookup.record.has_value());
    CHECK(store.get_summary(RecordKey{"old", "mode", CapQuery::at(8)}).status ==
          RecordStatus::Stale);
    CHECK(store.list_records(std::nullopt, CapQuery::at(8), Lens{}, SortColumn::Score, true)
              .empty());
    int seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::at(8), Lens{},
                        [&](const RecordStore::BlobRow& meta, const HydraRecord* rec) {
                            CHECK(meta.status == RecordStatus::Stale);
                            CHECK(rec == nullptr);
                            ++seen;
                        });
    CHECK(seen == 1);

    // drop_stale_records treats it the same as any other unreadable row.
    CHECK(store.drop_stale_records() == 1);
    CHECK(store.counts().second == 0);

    // A normal row, same store, still reads Ready -- this isn't blanket
    // breakage, just this one row's format.
    store.add_song("new", "Song", "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"new", "mode", CapQuery::at(8)}, at_cap(8));
    CHECK(store.get_record(RecordKey{"new", "mode", CapQuery::at(8)}).status ==
          RecordStatus::Ready);
    CHECK(store.has_record(RecordKey{"new", "mode", CapQuery::at(8)}));
    CHECK(store.list_records(std::nullopt, CapQuery::at(8), Lens{}, SortColumn::Score, true)
              .size() == 1);

    // And a current write purges an old-format row for the same chart, the
    // same way it purges an other-version one.
    RecordStore purge(":memory:");
    purge.add_song("purge", "Song", "Artist", "Charter", fixture().song);
    PreparedRow old_format2 =
        prepare_row(RecordKey{"purge", "mode", CapQuery::at(8)}, at_cap(8));
    old_format2.structure[0] = 1;
    old_format2.structure[1] = 0;
    old_format2.structure[2] = 0;
    old_format2.structure[3] = 0;
    purge.add_row(old_format2);
    CHECK(purge.counts().second == 1);
    purge.add_record(RecordKey{"purge", "mode", CapQuery::at(16)}, at_cap(16));
    CHECK(purge.counts().second == 1);
}

TEST_CASE("a row analyzed under other rules reads Stale until the rules match again") {
    core::Rules other = core::default_rules();
    other.max_tied_paths = 2;
    const RecordKey key{"h", "mode", CapQuery::at(8)};

    // A store running the default rules sees a row stamped with other rules
    // as Stale everywhere a lookup can ask.
    {
        RecordStore store(":memory:");
        store.add_song("h", "Song", "Artist", "Charter", fixture().song);
        HydraRecord foreign = at_cap(8);
        foreign.rules_fingerprint = other.fingerprint();
        store.add_row(prepare_row(key, foreign));
        CHECK_FALSE(store.has_record(key));
        CHECK(store.get_record(key).status == RecordStatus::Stale);
        CHECK(store.get_summary(key).status == RecordStatus::Stale);
        CHECK(store.list_records(std::nullopt, CapQuery::at(8), Lens{}, SortColumn::Score, true)
                  .empty());
    }

    // The same row reads Ready again once the store runs those rules.
    const std::string db = temp_db("rules_fp");
    {
        RecordStore store(db);
        store.add_song("h", "Song", "Artist", "Charter", fixture().song);
        store.add_record(key, at_cap(8));
        CHECK(store.get_record(key).status == RecordStatus::Ready);
    }
    {
        RecordStore store(db, other.fingerprint());
        CHECK(store.get_record(key).status == RecordStatus::Stale);
        CHECK_FALSE(store.has_record(key));
    }
    {
        // A store gated on "no usable rules" (a bad hydra_rules.ini) reads
        // nothing as Ready.
        RecordStore store(db, core::kNoRulesFingerprint);
        CHECK(store.get_record(key).status == RecordStatus::Stale);
    }
    {
        RecordStore store(db);
        CHECK(store.get_record(key).status == RecordStatus::Ready);
    }
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(db), ec);
}

TEST_CASE("a Stale lookup says why: another build, other rules, or both") {
    core::Rules other = core::default_rules();
    other.max_tied_paths = 2;
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);

    // This build, other rules.
    const RecordKey rules_key{"h", "rules", CapQuery::at(8)};
    HydraRecord foreign = at_cap(8);
    foreign.rules_fingerprint = other.fingerprint();
    store.add_row(prepare_row(rules_key, foreign));
    RecordLookup by_rules = store.get_record(rules_key);
    CHECK(by_rules.status == RecordStatus::Stale);
    CHECK(by_rules.stale_rules);
    CHECK_FALSE(by_rules.stale_build);

    // Another build, these rules.
    const RecordKey build_key{"h", "build", CapQuery::at(8)};
    PreparedRow old_build = prepare_row(build_key, at_cap(8));
    old_build.hyversion = "0.0.0";
    store.add_row(old_build);
    RecordLookup by_build = store.get_record(build_key);
    CHECK(by_build.status == RecordStatus::Stale);
    CHECK(by_build.stale_build);
    CHECK_FALSE(by_build.stale_rules);

    // Another build and other rules: both reasons.
    const RecordKey both_key{"h", "both", CapQuery::at(8)};
    PreparedRow both = prepare_row(both_key, foreign);
    both.hyversion = "0.0.0";
    store.add_row(both);
    RecordLookup by_both = store.get_record(both_key);
    CHECK(by_both.status == RecordStatus::Stale);
    CHECK(by_both.stale_build);
    CHECK(by_both.stale_rules);

    // A Ready row carries no reason.
    const RecordKey ready_key{"h", "ready", CapQuery::at(8)};
    store.add_record(ready_key, at_cap(8));
    RecordLookup ready = store.get_record(ready_key);
    CHECK(ready.status == RecordStatus::Ready);
    CHECK_FALSE(ready.stale_build);
    CHECK_FALSE(ready.stale_rules);
}

TEST_CASE("for_each_blob does not hold the store lock across its callback") {
    // The walk used to keep the store's lock from its first row to its last,
    // so anything else that touched the store -- a click on the UI thread --
    // waited for the whole report. The lock now covers the sqlite calls only.
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"h", "mode", CapQuery::at(4)}, at_cap(4));

    std::atomic<bool> in_callback{false};
    std::atomic<bool> probe_done{false};
    // Started before the walk on purpose. On the old code the probe blocks on
    // the lock until the walk has finished, so this test fails on the flag
    // rather than hanging forever.
    std::thread probe([&] {
        while (!in_callback.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        store.counts();
        probe_done.store(true);
    });

    int seen = 0;
    bool probe_arrived_during_callback = false;
    store.for_each_blob(std::nullopt, CapQuery::at(4), Lens{},
                        [&](const RecordStore::BlobRow&, const HydraRecord*) {
                            ++seen;
                            in_callback.store(true);
                            const auto deadline = std::chrono::steady_clock::now() +
                                                  std::chrono::seconds(2);
                            while (!probe_done.load() &&
                                   std::chrono::steady_clock::now() < deadline)
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            probe_arrived_during_callback = probe_done.load();
                        });
    probe.join();

    // doctest's assertions are not thread-safe, so every check lands here on
    // the main thread once the probe is joined.
    CHECK(seen == 1);
    CHECK(probe_arrived_during_callback);
}

TEST_CASE("a write during for_each_blob skips the row it replaced") {
    // Re-analyzing a chart deletes its result row and inserts a new one. The
    // walk listed the old row, so when it reaches it the row is gone: that
    // chart is left out of this one walk rather than decoded against paths
    // that are no longer its own. The next walk picks it up.
    //
    // Three records and not two, on purpose. sqlite hands a deleted id
    // straight back when it was the highest in the table, so with only "a"
    // and "b" the rewritten row lands on the same id, still passes the
    // identity check, and is simply read fresh -- which is right, but it
    // isn't the case this test is about.
    RecordStore store(":memory:");
    for (const char* h : {"a", "b", "c"})
        store.add_song(h, "Song", "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"a", "mode", CapQuery::at(4)}, at_cap(4));
    store.add_record(RecordKey{"b", "mode", CapQuery::at(4)}, at_cap(4));
    store.add_record(RecordKey{"c", "mode", CapQuery::at(4)}, at_cap(4));

    std::vector<std::string> yielded;
    CHECK_NOTHROW(store.for_each_blob(
        std::nullopt, CapQuery::at(4), Lens{},
        [&](const RecordStore::BlobRow& meta, const HydraRecord*) {
            yielded.push_back(meta.hyhash);
            if (meta.hyhash == "a")
                store.add_record(RecordKey{"b", "mode", CapQuery::at(4)}, at_cap(4));
        }));
    CHECK(yielded == std::vector<std::string>{"a", "c"});

    int second = 0;
    store.for_each_blob(std::nullopt, CapQuery::at(4), Lens{},
                        [&](const RecordStore::BlobRow&, const HydraRecord*) { ++second; });
    CHECK(second == 3);
}

TEST_CASE("a rewritten row that lands on its own id is read fresh, not mixed up") {
    // The other half of the same write: sqlite reuses the id when the deleted
    // row was the highest one, so the walk finds a row where it expected one.
    // The identity columns still match, and the blob and the nodes it names
    // are read together under one lock, so what comes back is the new row --
    // never one chart's shape paired with another chart's paths.
    RecordStore store(":memory:");
    store.add_song("a", "Song A", "Artist", "Charter", fixture().song);
    store.add_song("b", "Song B", "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"a", "mode", CapQuery::at(4)}, at_cap(4));
    store.add_record(RecordKey{"b", "mode", CapQuery::at(4)}, at_cap(4));

    std::vector<std::string> yielded;
    int decoded = 0;
    CHECK_NOTHROW(store.for_each_blob(
        std::nullopt, CapQuery::at(4), Lens{},
        [&](const RecordStore::BlobRow& meta, const HydraRecord* record) {
            yielded.push_back(meta.hyhash);
            if (record) ++decoded;
            if (meta.hyhash == "a")
                store.add_record(RecordKey{"b", "mode", CapQuery::at(4)}, at_cap(4));
        }));
    CHECK(yielded == std::vector<std::string>{"a", "b"});
    CHECK(decoded == 2);
}

TEST_CASE("for_each_blob stops between rows when its cancel flag is set") {
    RecordStore store(":memory:");
    store.add_song("a", "Song A", "Artist", "Charter", fixture().song);
    store.add_song("b", "Song B", "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"a", "mode", CapQuery::at(4)}, at_cap(4));
    store.add_record(RecordKey{"b", "mode", CapQuery::at(4)}, at_cap(4));

    std::atomic<bool> cancel{false};
    int seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::at(4), Lens{},
                        [&](const RecordStore::BlobRow&, const HydraRecord*) {
                            ++seen;
                            cancel.store(true);
                        },
                        &cancel);
    CHECK(seen == 1);
}

TEST_CASE("the listing and a lookup agree on which row is a chart's answer") {
    // The lock between the two paths. Each chart below holds several
    // candidates; whatever get_record picks is what the listing must show, and
    // when that pick is not readable the chart must not be listed at all.
    RecordStore store(":memory:");
    const std::vector<const char*> charts = {"newest", "over_stale", "over_sentinel",
                                             "all_stale"};
    for (const char* hash : charts)
        store.add_song(hash, hash, "Artist", "Charter", fixture().song);

    // Two current rows: the newer write wins, not the taller cap.
    store.add_record(RecordKey{"newest", "mode", CapQuery::at(32)}, at_cap(32));
    store.add_record(RecordKey{"newest", "mode", CapQuery::at(16)}, at_cap(16));

    // A current row and a taller stale one: this build's stamp wins. The stale
    // row goes in second because a current-version write purges the chart's
    // other-version rows.
    store.add_record(RecordKey{"over_stale", "mode", CapQuery::at(8)}, at_cap(8));
    PreparedRow stale =
        prepare_row(RecordKey{"over_stale", "mode", CapQuery::at(64)}, at_cap(64));
    stale.hyversion = "0.0.0";
    store.add_row(stale);

    // A current row and a taller sentinel: a real result wins.
    PreparedRow sentinel =
        prepare_row(RecordKey{"over_sentinel", "mode", CapQuery::at(64)}, at_cap(64));
    sentinel.lens = Lens::sentinel();
    store.add_row(sentinel);
    store.add_record(RecordKey{"over_sentinel", "mode", CapQuery::at(8)}, at_cap(8));

    // Nothing readable at all.
    PreparedRow only_stale =
        prepare_row(RecordKey{"all_stale", "mode", CapQuery::at(16)}, at_cap(16));
    only_stale.hyversion = "0.0.0";
    store.add_row(only_stale);

    std::unordered_map<std::string, int> listed;
    for (const RecordListing& r : store.list_records(std::nullopt, CapQuery::automatic(),
                                                     Lens{}, SortColumn::Score, true))
        listed[r.hyhash] = r.sp_cap;

    for (const char* hash : charts) {
        INFO(hash);
        const RecordKey key{hash, "mode", CapQuery::automatic()};
        const RecordLookup rec = store.get_record(key);
        CHECK(store.get_summary(key).status == rec.status);
        if (rec.status == RecordStatus::Ready) {
            REQUIRE(listed.count(hash) == 1);
            CHECK(listed[hash] == rec.record->sp_cap);
        } else {
            CHECK(listed.count(hash) == 0);
        }
    }

    // Spelled out, so a comparator change that moves both paths together still
    // has to answer for itself.
    CHECK(listed.at("newest") == 16);
    CHECK(listed.at("over_stale") == 8);
    CHECK(listed.at("over_sentinel") == 8);
    CHECK(store.get_record(RecordKey{"all_stale", "mode", CapQuery::automatic()}).status ==
          RecordStatus::Stale);
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

        // The export path still shows it, with no record to hand out. The
        // listing does not: nothing here can be read, so the chart reads the
        // same as one nobody has analyzed.
        CHECK(store.list_records(std::nullopt, CapQuery::at(8), kLensA, SortColumn::Score, true)
                  .empty());
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
