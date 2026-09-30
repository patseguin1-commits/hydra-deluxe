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
#include <unordered_set>
#include <vector>

#include "core/model.h"
#include "core/rules.h"
#include "core/squeeze_rating.h"
#include "core/stars.h"
#include "core/winstr.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "record_bytes.h"
#include "search/graph.h"
#include "search/pather.h"
#include "store/record_store.h"
#include "store/stored_versions.h"
#include "store/serialize.h"

using namespace hydra;
using namespace hydra::store;

namespace {

struct Config {
    const char* key;
    int cap;
    DepthMode dmode;
    int dvalue;
    std::optional<double> ms;
};

// The config matrix the GUI/CLI expose: score depth, points depth, the ms
// filter, and a fixed what-if cap.
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
    if (a.stars != b.stars) return "stars";
    return "";
}

}  // namespace

TEST_CASE("records round-trip through RecordStore across the corpus and config matrix") {
    RecordStore store(":memory:");

    int checks = 0, mismatches = 0;

    for (const std::string& path : corpus::chart_paths()) {
        const Song& song = corpus::song(path, true, true);
        if (song.is_empty()) continue;

        for (const Config& cfg : kMatrix) {
            const HydraRecord* record = nullptr;
            try {
                SearchSettings settings;
                settings.sp_cap = cfg.cap;
                settings.depth_mode = cfg.dmode;
                settings.depth_value = cfg.dvalue;
                settings.ms_filter = cfg.ms;
                record = &corpus::analyzed(path, settings);
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
                if (again.timecode.ticks() != orig.timecode.ticks() ||
                    again.timecode.ms() != orig.timecode.ms())
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

// rate_activation reads the stored transfer scales only. That is safe because
// a Ready record's stored scales equal a live recompute: a Ready row was
// written by this build, which stamps the scales with
// frontend_transfer_scales at copy-out, and the store hands back every input
// that function reads. This pins it through a real store round trip, for
// every activation of every path, all-0 paths included.
TEST_CASE("stored transfer scales equal a live recompute after a store round trip") {
    const std::vector<Config> configs = {
        {"cap4", 4, DepthMode::Scores, 4, std::nullopt},
        {"cap4.ms10", 4, DepthMode::Scores, 4, 10.0},
    };
    RecordStore store(":memory:");
    int acts = 0, mismatches = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;

        for (const Config& cfg : configs) {
            std::optional<HydraRecord> record;
            try {
                SearchSettings settings;
                settings.sp_cap = cfg.cap;
                settings.depth_mode = cfg.dmode;
                settings.depth_value = cfg.dvalue;
                settings.ms_filter = cfg.ms;
                record = analyze_chart(song, settings);
            } catch (const ChartFileError&) {
                continue;
            }
            const CapQuery cap = CapQuery::at(*record->sp_cap);
            const std::string hyhash = path + "|scales|" + cfg.key;
            store.add_song(hyhash, "Title", "Artist", "Charter", song);
            store.add_record(RecordKey{hyhash, "mode", cap}, *record);

            const RecordLookup lookup = store.get_record(RecordKey{hyhash, "mode", cap});
            REQUIRE(lookup.status == RecordStatus::Ready);
            REQUIRE(lookup.timing.has_value());

            std::vector<const Path*> all = lookup.record->all_paths();
            for (const Path* p : lookup.record->all_allzero_paths()) all.push_back(p);
            for (const Path* p : all) {
                for (const Activation& act : p->walk_activations()) {
                    ++acts;
                    const std::optional<ActTransferScales> live =
                        frontend_transfer_scales(act, *lookup.timing);
                    const bool same = live && live->pre.early == act.transfer_pre.early &&
                                      live->pre.late == act.transfer_pre.late &&
                                      live->post.early == act.transfer_post.early &&
                                      live->post.late == act.transfer_post.late;
                    if (!same && ++mismatches <= 8)
                        CHECK_MESSAGE(false, path << " [" << cfg.key << "] activation at tick "
                                                  << act.timecode.ticks());
                }
            }
        }
    }

    CHECK(mismatches == 0);
    REQUIRE(acts > 0);
    MESSAGE("compared " << acts << " stored activations with a live recompute");
}

TEST_CASE("RecordStore maintenance: has_record, list_records, reindex") {
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
    // doesn't count as "already analyzed".
    PreparedRow stale =
        prepare_row(RecordKey{"h2", "Expert Pro Drums, 2x Bass", at4}, *record);
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.counts().second == 2);
    CHECK_FALSE(store.has_record(RecordKey{"h2", "Expert Pro Drums, 2x Bass", at4}));
}

TEST_CASE("RecordStore results stamp: every accepted stamp reads Ready, others Stale") {
    // The stamp is not the app version (ADR 0018). 1.8.3 stamped its app
    // version on results identical to 1.8.2's, so both read Ready, through
    // the C++ rule (get_record, get_summary) and its SQL twin (has_record).
    CHECK(kResultsStamp.is_current("1.8.2"));
    CHECK(kResultsStamp.is_current("1.8.3"));
    CHECK(current_record_version() == std::string(kResultsStamp.written));

    const CapQuery at4 = CapQuery::at(4);
    HydraRecord record;
    for (const std::string& path : corpus::chart_paths()) {
        Song s = load_songpath(path, true, true);
        if (s.is_empty()) continue;
        try {
            SearchSettings settings;
            settings.sp_cap = 4;
            settings.depth_mode = DepthMode::Scores;
            settings.depth_value = 0;
            record = analyze_chart(s, settings);
        } catch (const ChartFileError&) {
            continue;
        }
        break;
    }
    REQUIRE_FALSE(record.paths.empty());

    RecordStore store(":memory:");
    const struct {
        const char* hash;
        const char* stamp;
        RecordStatus want;
    } cases[] = {{"a", "1.8.2", RecordStatus::Ready},
                 {"b", "1.8.3", RecordStatus::Ready},
                 {"c", "1.8.1", RecordStatus::Stale},
                 {"d", "0.0.0", RecordStatus::Stale}};
    for (const auto& c : cases) {
        CAPTURE(c.stamp);
        const RecordKey key{c.hash, "Expert Pro Drums, 2x Bass", at4};
        PreparedRow row = prepare_row(key, record);
        row.hyversion = c.stamp;
        store.add_row(row);
        CHECK(store.get_record(key).status == c.want);
        CHECK(store.get_summary(key).status == c.want);
        CHECK(store.has_record(key) == (c.want == RecordStatus::Ready));
    }
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

TEST_CASE("records at different caps coexist; each lookup sees only its own cap") {
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

    // A stale 64-bar row leaves the 32-bar answer alone -- for single
    // lookups and for the set queries alike.
    PreparedRow stale = prepare_row(RecordKey{"h", "mode", CapQuery::at(64)}, at_cap(64));
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::at(32)}).record->sp_cap == 32);
    std::vector<RecordListing> listed =
        store.list_records(std::nullopt, CapQuery::at(32), Lens{}, SortColumn::Score, true);
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].sp_cap == 32);
    int seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::at(32), Lens{},
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

    // reindex touches each cap's own row.
    CHECK(store.reindex() == 3);
    CHECK(store.list_records(std::nullopt, CapQuery::at(4), Lens{}, SortColumn::Score, true)[0]
              .summary.score == fixture().record.best_path().totalscore());
}

TEST_CASE("a row an old migration marked with unknown settings reads Not analyzed") {
    // Hydra 1.7 to 1.8.1 migrated older rows in with ms_enabled = -1: "a
    // result, settings unknown". No lens has -1, so such a row is no
    // candidate for any lookup. It reads as no row at all, not as Stale
    // (user decision 2026-09-26), and a real run of the chart replaces it.
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    PreparedRow migrated = prepare_row(RecordKey{"h", "mode", CapQuery::at(8)}, at_cap(8));
    migrated.lens.ms_enabled = -1;
    migrated.hyversion = "1.6.0";
    store.add_row(migrated);
    CHECK(store.counts().second == 1);

    const RecordKey key{"h", "mode", CapQuery::at(8)};
    CHECK(store.get_record(key).status == RecordStatus::NotAnalyzed);
    CHECK(store.get_summary(key).status == RecordStatus::NotAnalyzed);
    CHECK_FALSE(store.has_record(key));
    CHECK(store.list_records(std::nullopt, CapQuery::at(8), Lens{}, SortColumn::Score, true)
              .empty());
    int seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::at(8), Lens{},
                        [&](const RecordStore::BlobRow&, const HydraRecord*) { ++seen; });
    CHECK(seen == 0);

    store.add_record(key, at_cap(8));
    CHECK(store.counts().second == 1);
    CHECK(store.get_record(key).status == RecordStatus::Ready);
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

TEST_CASE("a row in the 1.8.1 path layout (structure format 5) reads Stale") {
    RecordStore store(":memory:");
    store.add_song("old", "Song", "Artist", "Charter", fixture().song);
    PreparedRow row = prepare_row(RecordKey{"old", "mode", CapQuery::at(4)}, at_cap(4));
    REQUIRE(row.structure.size() >= 4);
    row.structure[0] = 5;
    store.add_row(row);

    const RecordLookup lookup = store.get_record(RecordKey{"old", "mode", CapQuery::at(4)});
    CHECK(lookup.status == RecordStatus::Stale);
    CHECK(lookup.stale_build);
    CHECK_FALSE(lookup.record.has_value());
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
        RecordStore store(db, core::RulesStamp::of(other));
        CHECK(store.get_record(key).status == RecordStatus::Stale);
        CHECK_FALSE(store.has_record(key));
    }
    {
        // A store gated on "no usable rules" (a bad hydra_rules.ini) reads
        // nothing as Ready.
        RecordStore store(db, core::RulesStamp::none());
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
    // The lock between the two paths. Each chart below holds rows at several
    // caps. At every cap, whatever get_record picks is what the listing must
    // show, and when that pick is not readable the chart must not be listed.
    RecordStore store(":memory:");
    const std::vector<const char*> charts = {"two_caps", "over_stale", "over_sentinel",
                                             "all_stale"};
    for (const char* hash : charts)
        store.add_song(hash, hash, "Artist", "Charter", fixture().song);

    // Two current rows at two caps.
    store.add_record(RecordKey{"two_caps", "mode", CapQuery::at(32)}, at_cap(32));
    store.add_record(RecordKey{"two_caps", "mode", CapQuery::at(16)}, at_cap(16));

    // A current row and a taller stale one. The stale row goes in second
    // because a current-version write purges the chart's other-version rows.
    store.add_record(RecordKey{"over_stale", "mode", CapQuery::at(8)}, at_cap(8));
    PreparedRow stale =
        prepare_row(RecordKey{"over_stale", "mode", CapQuery::at(64)}, at_cap(64));
    stale.hyversion = "0.0.0";
    store.add_row(stale);

    // A current row and a taller row an old migration left: the migrated row
    // is no candidate at all.
    PreparedRow sentinel =
        prepare_row(RecordKey{"over_sentinel", "mode", CapQuery::at(64)}, at_cap(64));
    sentinel.lens.ms_enabled = -1;
    store.add_row(sentinel);
    store.add_record(RecordKey{"over_sentinel", "mode", CapQuery::at(8)}, at_cap(8));

    // Nothing readable at all.
    PreparedRow only_stale =
        prepare_row(RecordKey{"all_stale", "mode", CapQuery::at(16)}, at_cap(16));
    only_stale.hyversion = "0.0.0";
    store.add_row(only_stale);

    auto listed_at = [&](int cap) {
        std::unordered_map<std::string, int> listed;
        for (const RecordListing& r : store.list_records(std::nullopt, CapQuery::at(cap),
                                                         Lens{}, SortColumn::Score, true))
            listed[r.hyhash] = r.sp_cap;
        return listed;
    };

    for (int cap : {8, 16, 32, 64}) {
        std::unordered_map<std::string, int> listed = listed_at(cap);
        for (const char* hash : charts) {
            INFO(hash << " at " << cap);
            const RecordKey key{hash, "mode", CapQuery::at(cap)};
            const RecordLookup rec = store.get_record(key);
            CHECK(store.get_summary(key).status == rec.status);
            if (rec.status == RecordStatus::Ready) {
                REQUIRE(listed.count(hash) == 1);
                CHECK(listed[hash] == rec.record->sp_cap);
            } else {
                CHECK(listed.count(hash) == 0);
            }
        }
    }

    // Spelled out, so a change that moves both paths together still has to
    // answer for itself.
    CHECK(listed_at(16).at("two_caps") == 16);
    CHECK(listed_at(32).at("two_caps") == 32);
    CHECK(listed_at(8).at("over_stale") == 8);
    CHECK(listed_at(64).count("over_stale") == 0);
    CHECK(listed_at(8).at("over_sentinel") == 8);
    CHECK(listed_at(64).count("over_sentinel") == 0);
    CHECK(store.get_record(RecordKey{"all_stale", "mode", CapQuery::at(16)}).status ==
          RecordStatus::Stale);
}

TEST_CASE("prepare_row refuses a key whose exact cap isn't the record's") {
    HydraRecord rec = at_cap(4);

    // An exact key that disagrees with the record would file the result under
    // a cap it was never analyzed at.
    CHECK_THROWS_AS(prepare_row(RecordKey{"h", "mode", CapQuery::at(8)}, rec),
                    std::invalid_argument);

    // The matching key is fine.
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
    CHECK_FALSE(key == RecordKey{"h", "mode", CapQuery::at(4), kLensB});
    CHECK(CapQuery::at(4) != CapQuery::at(8));
    CHECK(CapQuery{} == CapQuery::at(kCloneHeroSpCap));

    // Each of the lens's five fields is part of the identity...
    CHECK(kLensA == Lens::from(10, 0, 20));
    CHECK(kLensA != Lens::from(11, 0, 20));
    CHECK(kLensA != Lens::from(10, 1, 20));
    CHECK(kLensA != Lens::from(10, 0, 21));
    CHECK(kLensA != Lens::from(std::nullopt, 0, 20));
    CHECK(kLensA != Lens::from(10, 0, 20, /*legacy_fills=*/true));

    // ...except the ms value when the limit is off, which the engine ignores:
    // "off at 10" and "off at 42" ran the same search.
    CHECK(Lens::from(std::nullopt, 0, 4) == Lens::from(std::nullopt, 0, 4));
    CHECK(Lens::from(std::nullopt, 0, 4).ms_value == 0);}

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
        before = record_bytes(
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
        CHECK(record_bytes(*store.get_record(RecordKey{"h", "mode", CapQuery::at(4), kLensB})
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

// ---- store correctness (2026-09-26 audit, Task 1) --------------------------

namespace {

ChartLibraryEntry chart_entry(const char* md5, const char* title) {
    return ChartLibraryEntry{md5,
                             title,
                             "Artist",
                             "Charter",
                             std::string("C:\\charts\\") + md5 + "\\notes.chart",
                             "C:\\charts",
                             std::string("sig-") + md5};
}

// Runs a batch of SQL straight on a database file no store has open.
void exec_on_file(const std::string& path, const char* sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    const std::string msg = err ? err : "";
    sqlite3_free(err);
    sqlite3_close(db);
    INFO(msg);
    REQUIRE(rc == SQLITE_OK);
}

}  // namespace

TEST_CASE("a database from Hydra 1.6 or older opens with nothing to show") {
    // User decision 2026-09-26: the pre-1.7 migrations are gone. The old
    // `records` table is left where it is and never read, so its charts read
    // Not analyzed until they are analyzed again. Those rows could not be
    // read since 1.8.1 anyway.
    const std::string path = temp_db("old_records");
    std::remove(path.c_str());
    {
        RecordStore seed(path);
        seed.add_song("old", "Old Song", "A", "C", fixture().song);
    }
    exec_on_file(path,
                 "DROP TABLE results; DROP TABLE path_refs; DROP TABLE paths;"
                 "PRAGMA user_version = 1;"
                 "CREATE TABLE records (hyhash TEXT NOT NULL, chartmode TEXT NOT NULL,"
                 " hyversion TEXT NOT NULL, sp_cap INTEGER NOT NULL, bestpath TEXT NOT NULL,"
                 " blob BLOB NOT NULL, score INTEGER, actcount INTEGER, maxskip INTEGER,"
                 " hardest_ms REAL, avgmult REAL, notecount INTEGER, sqin_count INTEGER,"
                 " sqout_count INTEGER, pathcount INTEGER,"
                 " PRIMARY KEY (hyhash, chartmode, sp_cap));"
                 "INSERT INTO records (hyhash, chartmode, hyversion, sp_cap, bestpath, blob,"
                 " score) VALUES ('old', 'mode', '1.6.0', 8, '1 2 3', x'00', 100);");

    const RecordKey key{"old", "mode", CapQuery::at(8)};
    {
        RecordStore store(path);
        CHECK(store.counts().second == 0);
        CHECK(store.get_record(key).status == RecordStatus::NotAnalyzed);
        CHECK(store.get_summary(key).status == RecordStatus::NotAnalyzed);
        CHECK_FALSE(store.has_record(key));
        // A fresh analysis lands as usual.
        store.add_record(key, at_cap(8));
        CHECK(store.get_record(key).status == RecordStatus::Ready);
    }
    // The old table is left alone: nothing read it and nothing rewrote it.
    CHECK(scalar(path, "SELECT COUNT(*) FROM records") == 1);
    CHECK(scalar(path, "PRAGMA user_version") == 3);
    std::remove(path.c_str());
}

// ---- the fill rule in the key (docs/adr/0010) ------------------------------

namespace {

// The default Lens{} but for the fill rule, so a key differs from its 1.1
// twin in nothing else.
const Lens kLegacy = [] {
    Lens lens;
    lens.legacy_fills = 1;
    return lens;
}();

HydraRecord legacy_at_cap(int cap) {
    HydraRecord r = at_cap(cap);
    r.legacy_fills = true;
    return r;
}

// Turns a schema 3 file back into schema 2: the results table without its
// legacy_fills column, every row and result_id kept, user_version 2.
void downgrade_to_schema2(const std::string& path) {
    exec_on_file(path,
                 "ALTER TABLE results RENAME TO r3;"
                 "CREATE TABLE results ("
                 "  result_id INTEGER PRIMARY KEY, hyhash TEXT NOT NULL,"
                 "  chartmode TEXT NOT NULL, hyversion TEXT NOT NULL,"
                 "  sp_cap INTEGER NOT NULL, ms_enabled INTEGER NOT NULL,"
                 "  ms_value INTEGER NOT NULL, depth_mode INTEGER NOT NULL,"
                 "  depth_value INTEGER NOT NULL, bestpath TEXT NOT NULL,"
                 "  structure BLOB NOT NULL, score INTEGER, actcount INTEGER,"
                 "  maxskip INTEGER, hardest_ms REAL, avgmult REAL, notecount INTEGER,"
                 "  sqin_count INTEGER, sqout_count INTEGER, pathcount INTEGER,"
                 "  stars INTEGER,"
                 "  UNIQUE (hyhash, chartmode, sp_cap, ms_enabled, ms_value, depth_mode,"
                 "          depth_value));"
                 "INSERT INTO results SELECT result_id, hyhash, chartmode, hyversion,"
                 " sp_cap, ms_enabled, ms_value, depth_mode, depth_value, bestpath,"
                 " structure, score, actcount, maxskip, hardest_ms, avgmult, notecount,"
                 " sqin_count, sqout_count, pathcount, stars FROM r3;"
                 "DROP TABLE r3;"
                 "PRAGMA user_version = 2;");
}

}  // namespace

TEST_CASE("1.0 and 1.1 results for one chart sit side by side") {
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    const RecordKey ch11{"h", "mode", CapQuery::at(4)};
    const RecordKey ch10{"h", "mode", CapQuery::at(4), kLegacy};

    store.add_record(ch11, at_cap(4));
    CHECK(store.get_summary(ch10).status == RecordStatus::NotAnalyzed);
    CHECK_FALSE(store.has_record(ch10));

    // Writing the 1.0 result keeps the 1.1 one, and each lookup finds its own.
    store.add_record(ch10, legacy_at_cap(4));
    CHECK(store.counts().second == 2);
    const RecordLookup old_rule = store.get_record(ch10);
    const RecordLookup new_rule = store.get_record(ch11);
    REQUIRE(old_rule.status == RecordStatus::Ready);
    REQUIRE(new_rule.status == RecordStatus::Ready);
    CHECK(old_rule.record->legacy_fills);
    CHECK_FALSE(new_rule.record->legacy_fills);
    CHECK(store.analyzed_hashes("mode", CapQuery::at(4), kLegacy).count("h") == 1);

    // Re-analyzing under 1.0 replaces only the 1.0 row.
    store.add_record(ch10, legacy_at_cap(4));
    CHECK(store.counts().second == 2);
    CHECK(store.get_record(ch11).status == RecordStatus::Ready);
}

TEST_CASE("prepare_row refuses a key that names the other fill rule") {
    const RecordKey ch10{"h", "mode", CapQuery::at(4), kLegacy};
    const RecordKey ch11{"h", "mode", CapQuery::at(4)};
    CHECK_THROWS_AS(prepare_row(ch10, at_cap(4)), std::invalid_argument);
    CHECK_THROWS_AS(prepare_row(ch11, legacy_at_cap(4)), std::invalid_argument);
    CHECK(prepare_row(ch10, legacy_at_cap(4)).lens.legacy_fills == 1);
}

TEST_CASE("a schema 2 database keeps its results, filed under Clone Hero 1.1") {
    const std::string path = temp_db("schema2");
    std::remove(path.c_str());
    const RecordKey key{"h", "mode", CapQuery::at(4)};
    int64_t id_before = 0;
    {
        RecordStore seed(path);
        seed.add_song("h", "Song", "Artist", "Charter", fixture().song);
        seed.add_record(key, at_cap(4));
    }
    id_before = scalar(path, "SELECT result_id FROM results");
    downgrade_to_schema2(path);
    REQUIRE(scalar(path, "SELECT COUNT(*) FROM pragma_table_info('results')"
                         " WHERE name='legacy_fills'") == 0);
    {
        RecordStore store(path);
        // Still Ready: its paths come back through the kept result_id.
        CHECK(store.get_record(key).status == RecordStatus::Ready);
        CHECK(store.get_summary(RecordKey{"h", "mode", CapQuery::at(4), kLegacy}).status ==
              RecordStatus::NotAnalyzed);
    }
    CHECK(scalar(path, "SELECT result_id FROM results") == id_before);
    CHECK(scalar(path, "SELECT legacy_fills FROM results") == 0);
    CHECK(scalar(path, "SELECT COUNT(*) FROM sqlite_master WHERE name='results_schema2'") == 0);
    CHECK(scalar(path, "PRAGMA user_version") == 3);
    std::remove(path.c_str());
}

TEST_CASE("a schema 2 database hydra_batch --legacy-fills filled is filed under 1.0") {
    const std::string path = temp_db("schema2_ch10");
    std::remove(path.c_str());
    const RecordKey ch10{"h", "mode", CapQuery::at(4), kLegacy};
    {
        RecordStore seed(path);
        seed.add_song("h", "Song", "Artist", "Charter", fixture().song);
        seed.add_record(ch10, legacy_at_cap(4));
        seed.set_engine_mode(engine_mode_stamp(FillDeadlineRule::Ch10));
    }
    downgrade_to_schema2(path);
    {
        RecordStore store(path);
        CHECK(store.get_record(ch10).status == RecordStatus::Ready);
        CHECK(store.get_summary(RecordKey{"h", "mode", CapQuery::at(4)}).status ==
              RecordStatus::NotAnalyzed);
    }
    std::remove(path.c_str());
}

TEST_CASE("a failed library rebuild keeps the previous scan") {
    // The rebuild used to drop the table before opening its transaction, so
    // an insert that failed left the library empty, and took the rescan cache
    // with it. A trigger that refuses one md5 makes an insert fail partway.
    const std::string path = temp_db("rebuild_fail");
    std::remove(path.c_str());
    {
        RecordStore store(path);
        store.rebuild_chart_library({chart_entry("a", "A"), chart_entry("b", "B")});
    }
    exec_on_file(path,
                 "CREATE TRIGGER refuse_boom BEFORE INSERT ON charts WHEN NEW.md5 = 'boom'"
                 " BEGIN SELECT RAISE(ABORT, 'boom'); END;");
    {
        RecordStore store(path);
        CHECK_THROWS(
            store.rebuild_chart_library({chart_entry("c", "C"), chart_entry("boom", "Boom")}));
        CHECK(store.chart_library_count() == 2);
        const ChartLibraryCache cache = store.chart_library_cache();
        CHECK(cache.count("C:\\charts\\a\\notes.chart") == 1);
        CHECK(cache.count("C:\\charts\\b\\notes.chart") == 1);
        // No transaction was left open: the next rebuild goes through.
        store.rebuild_chart_library({chart_entry("c", "C")});
        CHECK(store.chart_library_count() == 1);
    }
    std::remove(path.c_str());
}

TEST_CASE("a charts table from before the sig column still rebuilds") {
    // The rebuild empties the table instead of recreating it, so an old
    // table has to gain the column when the store opens.
    const std::string path = temp_db("charts_nosig");
    std::remove(path.c_str());
    exec_on_file(path,
                 "CREATE TABLE charts (md5 TEXT, name TEXT, artist TEXT, charter TEXT,"
                 " path TEXT, folder TEXT);"
                 "INSERT INTO charts VALUES ('a', 'A', 'Artist', 'Charter',"
                 " 'C:\\charts\\a\\notes.chart', 'C:\\charts');");
    {
        RecordStore store(path);
        CHECK(store.chart_library_count() == 1);
        CHECK(store.chart_library_cache().empty());  // no fingerprints yet
        store.rebuild_chart_library({chart_entry("b", "B")});
        CHECK(store.chart_library_count() == 1);
        CHECK(store.chart_library_cache().at("C:\\charts\\b\\notes.chart").sig == "sig-b");
    }
    std::remove(path.c_str());
}

TEST_CASE("a song's stored names follow the latest analysis and the latest scan") {
    // User decision 2026-09-26: fixing song.ini reaches the reports. The
    // names used to be frozen at the chart's first analysis.
    RecordStore store(":memory:");
    store.add_song("h", "Old Title", "Old Artist", "Old Charter", fixture().song);
    store.add_record(RecordKey{"h", "mode", CapQuery::at(4)}, at_cap(4));
    auto listed = [&] {
        std::vector<RecordListing> rows =
            store.list_records(std::nullopt, CapQuery::at(4), Lens{}, SortColumn::Score, true);
        REQUIRE(rows.size() == 1);
        return rows[0];
    };

    // Analyzed again after song.ini changed.
    store.add_song("h", "New Title", "New Artist", "New Charter", fixture().song);
    CHECK(listed().ref_name == "New Title");
    CHECK(listed().ref_artist == "New Artist");
    CHECK(listed().ref_charter == "New Charter");

    // Rescanned after song.ini changed, with no analysis. The scan found two
    // copies of the chart; the first one it listed names it.
    const ChartLibraryEntry first = chart_entry("h", "Scanned Title");
    ChartLibraryEntry second = chart_entry("h", "Second Copy");
    second.notespath = "C:\\charts\\copy\\notes.chart";
    store.rebuild_chart_library({first, second});
    CHECK(listed().ref_name == "Scanned Title");

    // A chart the scan found but nobody analyzed gets no song row.
    store.rebuild_chart_library({first, chart_entry("x", "Never Analyzed")});
    CHECK(store.counts().first == 1);
}

TEST_CASE("get_record reads a whole row while another thread rewrites it") {
    // get_record reads the winning row, its nodes and the tempo map under
    // one lock, then decodes with the lock released. A rewrite landing in
    // between must never pair one row's shape with another row's nodes.
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    const RecordKey key{"h", "mode", CapQuery::at(4)};
    HydraRecord empty = at_cap(4);
    empty.paths.clear();
    empty.allzero_paths.clear();
    store.add_record(key, at_cap(4));
    const size_t full = fixture().record.paths.size();

    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (int i = 0; i < 50; ++i) store.add_record(key, i % 2 ? at_cap(4) : empty);
        stop.store(true);
    });
    int reads = 0;
    bool all_whole = true;
    std::string failure;
    do {
        try {
            const RecordLookup r = store.get_record(key);
            if (r.status != RecordStatus::Ready || !r.record) all_whole = false;
            else if (!r.record->paths.empty() && r.record->paths.size() != full)
                all_whole = false;
            ++reads;
        } catch (const std::exception& e) {
            failure = e.what();
            break;
        }
    } while (!stop.load());
    writer.join();

    // doctest's assertions are not thread-safe, so every check is out here.
    CHECK(failure.empty());
    CHECK(all_whole);
    CHECK(reads > 0);
}

TEST_CASE("has_record and a lookup agree on which rows are readable") {
    // The Ready rule is spelled once in C++ (rank_row) and once in SQL
    // (kRowReadySql, which has_record and add_row's purge use). This pins the
    // two spellings together across every kind of row.
    core::Rules other = core::default_rules();
    other.max_tied_paths = 2;
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);

    const RecordKey ready{"h", "ready", CapQuery::at(8)};
    store.add_record(ready, at_cap(8));

    const RecordKey old_build{"h", "build", CapQuery::at(8)};
    PreparedRow build_row = prepare_row(old_build, at_cap(8));
    build_row.hyversion = "0.0.0";
    store.add_row(build_row);

    const RecordKey old_format{"h", "format", CapQuery::at(8)};
    PreparedRow format_row = prepare_row(old_format, at_cap(8));
    format_row.structure[0] = 1;
    format_row.structure[1] = 0;
    format_row.structure[2] = 0;
    format_row.structure[3] = 0;
    store.add_row(format_row);

    const RecordKey other_rules{"h", "rules", CapQuery::at(8)};
    HydraRecord foreign = at_cap(8);
    foreign.rules_fingerprint = other.fingerprint();
    store.add_row(prepare_row(other_rules, foreign));

    for (const RecordKey& key : {ready, old_build, old_format, other_rules}) {
        INFO(key.chartmode);
        CHECK(store.has_record(key) ==
              (store.get_summary(key).status == RecordStatus::Ready));
    }
    CHECK(store.has_record(ready));
}

// SQLite switches a feature on when its SQLITE_ENABLE_* macro is defined at
// all, whatever its value (sqlite.org/compile.html), so the old
// "SQLITE_ENABLE_FTS5=0" compiled full-text search in. Hydra never uses it.
TEST_CASE("the vendored SQLite is built without FTS5") {
    CHECK(sqlite3_compileoption_used("ENABLE_FTS5") == 0);
}

// ---- store speed (2026-09-26 audit, Task 9) --------------------------------

TEST_CASE("a file store runs in WAL mode with an index on chart names") {
    // WAL: a commit appends to a log instead of rewriting the file in place,
    // so it costs far fewer disk syncs (https://www.sqlite.org/wal.html).
    // The journal mode is stored in the file, so a fresh connection sees it.
    const std::string path = temp_db("wal");
    std::remove(path.c_str());
    { RecordStore store(path); }
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA journal_mode", -1, &s, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(s) == SQLITE_ROW);
    const std::string mode = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
    sqlite3_finalize(s);
    sqlite3_close(db);
    CHECK(mode == "wal");
    CHECK(scalar(path, "SELECT COUNT(*) FROM sqlite_master"
                       " WHERE type='index' AND name='charts_by_name'") == 1);
    std::remove(path.c_str());
}

TEST_CASE("save_analysis writes the song, the result and the count together") {
    RecordStore store(":memory:");
    const RecordKey key{"h", "mode", CapQuery::at(4)};
    const DynamicsEntry count{DynamicsKey{"h", "Expert", true}, {1, 2, 3},
                              kDynamicsCountStamp.written};
    store.save_analysis("h", "Song", "Artist", "Charter", fixture().song,
                        prepare_row(key, at_cap(4)), count);
    CHECK(store.counts() == std::pair<int64_t, int64_t>{1, 1});
    CHECK(store.get_record(key).status == RecordStatus::Ready);
    CHECK(store.get_dynamics(count.key) == std::optional<std::vector<uint8_t>>(count.blob));

    // No count (the parse dropped the 2x kicks): the song and result still land.
    const RecordKey key2{"h2", "mode", CapQuery::at(4)};
    store.save_analysis("h2", "Song 2", "Artist", "Charter", fixture().song,
                        prepare_row(key2, at_cap(4)), std::nullopt);
    CHECK(store.get_record(key2).status == RecordStatus::Ready);
    CHECK(store.counts().first == 2);
}

TEST_CASE("a save_analysis that fails leaves nothing behind") {
    // A trigger that refuses one chart's result makes the save fail after
    // the song row went in. One transaction means the song row goes back out.
    const std::string path = temp_db("save_fail");
    std::remove(path.c_str());
    { RecordStore store(path); }
    exec_on_file(path,
                 "CREATE TRIGGER refuse_boom BEFORE INSERT ON results WHEN NEW.hyhash = 'boom'"
                 " BEGIN SELECT RAISE(ABORT, 'boom'); END;");
    {
        RecordStore store(path);
        const RecordKey boom{"boom", "mode", CapQuery::at(4)};
        CHECK_THROWS(store.save_analysis("boom", "Song", "Artist", "Charter", fixture().song,
                                         prepare_row(boom, at_cap(4)), std::nullopt));
        CHECK(store.counts().first == 0);
        // The store is still usable: no transaction was left open.
        const RecordKey ok{"ok", "mode", CapQuery::at(4)};
        store.save_analysis("ok", "Song", "Artist", "Charter", fixture().song,
                            prepare_row(ok, at_cap(4)), std::nullopt);
        CHECK(store.get_record(ok).status == RecordStatus::Ready);
    }
    std::remove(path.c_str());
}

TEST_CASE("analyzed_hashes names exactly the charts has_record would skip") {
    RecordStore store(":memory:");
    const std::vector<const char*> charts = {"ready", "stale", "other_lens", "other_cap",
                                             "other_mode"};
    for (const char* h : charts) store.add_song(h, h, "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"ready", "mode", CapQuery::at(4)}, at_cap(4));
    PreparedRow stale = prepare_row(RecordKey{"stale", "mode", CapQuery::at(4)}, at_cap(4));
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    store.add_record(RecordKey{"other_lens", "mode", CapQuery::at(4), kLensB}, at_cap(4));
    store.add_record(RecordKey{"other_cap", "mode", CapQuery::at(8)}, at_cap(8));
    store.add_record(RecordKey{"other_mode", "other", CapQuery::at(4)}, at_cap(4));

    for (const CapQuery& cap : {CapQuery::at(4), CapQuery::at(8)}) {
        const std::unordered_set<std::string> got = store.analyzed_hashes("mode", cap, Lens{});
        for (const char* h : charts) {
            INFO(h);
            CHECK((got.count(h) == 1) == store.has_record(RecordKey{h, "mode", cap}));
        }
    }
    CHECK(store.analyzed_hashes("mode", CapQuery::at(4), Lens{}) ==
          std::unordered_set<std::string>{"ready"});
}

TEST_CASE("get_summaries answers a page the same as get_summary row by row") {
    RecordStore store(":memory:");
    for (const char* h : {"ready", "stale", "none", "two_caps"})
        store.add_song(h, h, "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"ready", "mode", CapQuery::at(4)}, at_cap(4));
    PreparedRow stale = prepare_row(RecordKey{"stale", "mode", CapQuery::at(4)}, at_cap(4));
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    store.add_record(RecordKey{"two_caps", "mode", CapQuery::at(32)}, at_cap(32));
    store.add_record(RecordKey{"two_caps", "mode", CapQuery::at(16)}, at_cap(16));

    // "ready" twice: a page can list the same chart from two folders.
    const std::vector<std::string> page = {"ready", "stale", "none", "two_caps", "ready"};
    for (const CapQuery& cap : {CapQuery::at(4), CapQuery::at(16), CapQuery::at(32)}) {
        const std::vector<SummaryLookup> got = store.get_summaries(page, "mode", cap, Lens{});
        REQUIRE(got.size() == page.size());
        for (size_t i = 0; i < page.size(); ++i) {
            INFO(page[i]);
            // Compared with get_record, not get_summary, which this task
            // rebuilds on top of get_summaries. Every Ready row here holds
            // the fixture's paths, so its best path is the fixture's.
            const RecordLookup one = store.get_record(RecordKey{page[i], "mode", cap});
            CHECK(got[i].status == one.status);
            CHECK(got[i].bestpath == (one.status == RecordStatus::Ready
                                          ? fixture().record.best_path().pathstring()
                                          : std::string()));
        }
    }
    CHECK(store.get_summaries({}, "mode", CapQuery::at(4), Lens{}).empty());
}

// ---- Auto removed (2026-09-27, interface redesign Task 6) ------------------

namespace {

// A result the way Hydra 1.8.4's Auto stamped it: the fixture's paths at
// `cap`, under the default rules' Auto fingerprint.
HydraRecord auto_run_at(int cap) {
    HydraRecord r = at_cap(cap);
    r.rules_fingerprint = core::default_rules().retired_auto_fingerprint();
    return r;
}

}  // namespace

TEST_CASE("the first open deletes the results Auto saved, and their paths, once") {
    const std::string path = temp_db("auto_delete");
    std::remove(path.c_str());
    const RecordKey kept{"h", "mode", CapQuery::at(4)};
    const RecordKey whatif{"h", "mode", CapQuery::at(8)};
    const RecordKey auto_same_chart{"h", "mode", CapQuery::at(16)};
    const RecordKey auto_only{"a", "mode", CapQuery::at(32)};

    std::vector<uint8_t> kept_bytes;
    {
        RecordStore store(path);
        store.add_song("h", "Song", "Artist", "Charter", fixture().song);
        store.add_song("a", "Other", "Artist", "Charter", fixture().song);
        store.add_record(kept, at_cap(4));
        store.add_record(whatif, at_cap(8));
        store.add_record(auto_same_chart, auto_run_at(16));
        store.add_record(auto_only, auto_run_at(32));
        kept_bytes = record_bytes(*store.get_record(kept).record);
        // This build accepts only the fixed-cap fingerprint, so an Auto row
        // reads Stale even before anything deletes it.
        CHECK(store.get_record(auto_only).status == RecordStatus::Stale);
        CHECK(store.counts().second == 4);
    }
    REQUIRE(scalar(path, "SELECT COUNT(*) FROM paths WHERE hyhash='a'") > 0);
    // A database Hydra 1.8.4 wrote has no mark yet.
    exec_on_file(path, "DELETE FROM meta WHERE key='auto_results_deleted'");

    {
        RecordStore store(path);
        CHECK(store.counts().second == 2);
        CHECK(store.get_record(auto_only).status == RecordStatus::NotAnalyzed);
        CHECK(store.get_record(auto_same_chart).status == RecordStatus::NotAnalyzed);
        // A fixed-cap what-if above 4 bars is not an Auto result and stays.
        CHECK(store.get_record(whatif).status == RecordStatus::Ready);
        const RecordLookup left = store.get_record(kept);
        REQUIRE(left.status == RecordStatus::Ready);
        CHECK(record_bytes(*left.record) == kept_bytes);
    }
    // The chart that held only an Auto row has no paths left; the other
    // chart's paths are still used by its kept rows.
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths WHERE hyhash='a'") == 0);
    CHECK(scalar(path, "SELECT COUNT(*) FROM path_refs WHERE hyhash='a'") == 0);
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths WHERE hyhash='h'") > 0);
    CHECK(scalar(path, "SELECT COUNT(*) FROM meta WHERE key='auto_results_deleted'") == 1);

    // Marked done: a later open never deletes again.
    {
        RecordStore store(path);
        store.add_record(auto_only, auto_run_at(32));
    }
    {
        RecordStore store(path);
        CHECK(store.counts().second == 3);
        CHECK(store.get_record(auto_only).status == RecordStatus::Stale);
    }
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(path), ec);
}

TEST_CASE("a start with a bad rules file leaves the Auto results for the next good start") {
    const std::string path = temp_db("auto_delete_none");
    std::remove(path.c_str());
    const RecordKey auto_only{"a", "mode", CapQuery::at(32)};
    {
        RecordStore store(path);
        store.add_song("a", "Other", "Artist", "Charter", fixture().song);
        store.add_record(auto_only, auto_run_at(32));
    }
    exec_on_file(path, "DELETE FROM meta WHERE key='auto_results_deleted'");
    {
        // A bad hydra_rules.ini: there is no fingerprint to look for, so
        // nothing is deleted and nothing is marked done.
        RecordStore store(path, core::RulesStamp::none());
        CHECK(store.counts().second == 1);
    }
    CHECK(scalar(path, "SELECT COUNT(*) FROM meta WHERE key='auto_results_deleted'") == 0);
    {
        RecordStore store(path);
        CHECK(store.counts().second == 0);
    }
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(path), ec);
}

// ---- library summaries (interface redesign, Task 7) ------------------------

TEST_CASE("a saved result stores its best path's star count") {
    RecordStore store(":memory:");
    for (const char* h : {"ready", "stale"})
        store.add_song(h, h, "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"ready", "mode", CapQuery::at(4)}, at_cap(4));
    PreparedRow stale = prepare_row(RecordKey{"stale", "mode", CapQuery::at(4)}, at_cap(4));
    stale.hyversion = "0.0.0";
    store.add_row(stale);

    const Path& best = fixture().record.best_path();
    const PathSummary expected = summarize_record(fixture().record);
    REQUIRE(expected.stars.has_value());
    CHECK(*expected.stars == path_stars(best));

    auto check = [&] {
        const std::vector<SummaryLookup> got =
            store.get_summaries({"ready", "stale", "none"}, "mode", CapQuery::at(4), Lens{});
        REQUIRE(got.size() == 3);
        CHECK(got[0].status == RecordStatus::Ready);
        CHECK(got[0].summary.score == best.totalscore());
        CHECK(got[0].summary.hardest_ms == expected.hardest_ms);
        CHECK(got[0].summary.stars == expected.stars);
        CHECK(diff_summary(got[0].summary, expected) == "");
        // Only a Ready answer carries a summary.
        CHECK(got[1].status == RecordStatus::Stale);
        CHECK_FALSE(got[1].summary.score.has_value());
        CHECK(got[2].status == RecordStatus::NotAnalyzed);
        CHECK_FALSE(got[2].summary.stars.has_value());

        const std::vector<RecordListing> listing =
            store.list_records(std::nullopt, CapQuery::at(4), Lens{}, SortColumn::Score, true);
        REQUIRE(listing.size() == 1);
        CHECK(listing[0].summary.stars == expected.stars);
        CHECK(listing[0].sp_cap == 4);
    };
    check();
    // reindex rewrites every summary column, stars included.
    store.reindex();
    check();
}

TEST_CASE("get_summaries answers a whole library in chunks") {
    // SQLite refuses more than 32,766 bound values in one statement. The
    // library asks about every chart at once, so the store must split it.
    RecordStore store(":memory:");
    store.add_song("ready", "ready", "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"ready", "mode", CapQuery::at(4)}, at_cap(4));

    std::vector<std::string> hashes;
    for (int i = 0; i < 20000; ++i) {
        char h[16];
        std::snprintf(h, sizeof(h), "fake%05d", i);
        hashes.emplace_back(h);
    }
    // The real chart at the start, the middle and the end, so it lands in
    // more than one chunk's position.
    for (size_t at : {size_t{0}, size_t{10000}, size_t{19999}}) hashes[at] = "ready";

    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<SummaryLookup> got =
        store.get_summaries(hashes, "mode", CapQuery::at(4), Lens{});
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("get_summaries on 20,000 hashes: " << ms << " ms");

    REQUIRE(got.size() == hashes.size());
    int ready = 0;
    for (size_t i = 0; i < got.size(); ++i) {
        if (hashes[i] == "ready") {
            CHECK(got[i].status == RecordStatus::Ready);
            CHECK(got[i].summary.stars.has_value());
            ++ready;
        } else if (got[i].status != RecordStatus::NotAnalyzed) {
            FAIL("fake hash " << hashes[i] << " read as analyzed");
        }
    }
    CHECK(ready == 3);
}

TEST_CASE("a database from before the stars column gets its stars filled on open") {
    const std::string path = temp_db("stars_fill");
    std::remove(path.c_str());
    {
        RecordStore seed(path);
        for (const char* h : {"ready", "stale"})
            seed.add_song(h, h, "Artist", "Charter", fixture().song);
        seed.add_record(RecordKey{"ready", "mode", CapQuery::at(4)}, at_cap(4));
        PreparedRow old = prepare_row(RecordKey{"stale", "mode", CapQuery::at(4)}, at_cap(4));
        old.hyversion = "0.0.0";
        seed.add_row(old);
    }
    // What the previous Hydra wrote: the same table with no stars column.
    exec_on_file(path, "ALTER TABLE results DROP COLUMN stars");

    const int expected = path_stars(fixture().record.best_path());
    {
        RecordStore store(path);
        const std::vector<SummaryLookup> got =
            store.get_summaries({"ready", "stale"}, "mode", CapQuery::at(4), Lens{});
        CHECK(got[0].summary.stars == expected);
        CHECK(got[1].status == RecordStatus::Stale);
    }
    CHECK(scalar(path, "SELECT stars FROM results WHERE hyhash='ready'") == expected);
    // The Stale row is left exactly as it was: no stars, and its other
    // summaries kept rather than blanked.
    CHECK(scalar(path, "SELECT COUNT(*) FROM results WHERE hyhash='stale'"
                       " AND stars IS NULL AND score IS NOT NULL") == 1);
    // A second open finds nothing left to fill and changes nothing.
    { RecordStore again(path); }
    CHECK(scalar(path, "SELECT stars FROM results WHERE hyhash='ready'") == expected);
    std::remove(path.c_str());
}

TEST_CASE("under rules that make every row Stale, the stars fill changes nothing") {
    // A bad hydra_rules.ini opens the store under RulesStamp::none(), where
    // every row reads Stale. The fill must not blank or skip-mark anything:
    // once the rules are fixed, the next open fills the row.
    const std::string path = temp_db("stars_badrules");
    std::remove(path.c_str());
    {
        RecordStore seed(path);
        seed.add_song("h", "h", "Artist", "Charter", fixture().song);
        seed.add_record(RecordKey{"h", "mode", CapQuery::at(4)}, at_cap(4));
    }
    exec_on_file(path, "ALTER TABLE results DROP COLUMN stars");
    const int64_t score = fixture().record.best_path().totalscore();

    { RecordStore bad(path, core::RulesStamp::none()); }
    CHECK(scalar(path, "SELECT COUNT(*) FROM results WHERE stars IS NULL") == 1);
    CHECK(scalar(path, "SELECT score FROM results WHERE hyhash='h'") == score);

    { RecordStore good(path); }
    CHECK(scalar(path, "SELECT stars FROM results WHERE hyhash='h'") ==
          path_stars(fixture().record.best_path()));
    CHECK(scalar(path, "SELECT score FROM results WHERE hyhash='h'") == score);
    std::remove(path.c_str());
}

TEST_CASE("a stored song keeps its length, and an old songmeta row reads none") {
    // RecordStore has no raw-SQL test hook, so this uses a file database and
    // nulls the column on a second connection, as the stars-fill case does.
    const Song& song = fixture().song;
    REQUIRE_FALSE(song.sequence.empty());
    const double expected = song.sequence.back().timecode.ms();

    const std::string path = temp_db("song_length");
    std::remove(path.c_str());
    const RecordKey key{"h", "mode", CapQuery::at(4)};
    {
        RecordStore store(path);
        store.add_song("h", "Song", "Artist", "Charter", song);
        store.add_record(key, at_cap(4));
        const RecordLookup got = store.get_record(key);
        REQUIRE(got.status == RecordStatus::Ready);
        REQUIRE(got.song_length_ms.has_value());
        CHECK(*got.song_length_ms == doctest::Approx(expected));
    }

    // A row written before the column existed reads no length.
    exec_on_file(path, "UPDATE songmeta SET length_ms = NULL WHERE hyhash = 'h'");
    {
        RecordStore store(path);
        const RecordLookup got = store.get_record(key);
        REQUIRE(got.status == RecordStatus::Ready);
        CHECK_FALSE(got.song_length_ms.has_value());

        // Opening the song fills it from the chart; the result is untouched.
        store.set_song_length("h", expected);
        const RecordLookup filled = store.get_record(key);
        REQUIRE(filled.status == RecordStatus::Ready);
        REQUIRE(filled.song_length_ms.has_value());
        CHECK(*filled.song_length_ms == doctest::Approx(expected));
        CHECK(filled.record->best_path().totalscore() == got.record->best_path().totalscore());

        // A length already there is kept, and an unknown song is ignored.
        store.set_song_length("h", expected + 1000.0);
        CHECK(*store.get_record(key).song_length_ms == doctest::Approx(expected));
        store.set_song_length("unknown", 5.0);
    }
    CHECK(song_length_ms(song) == doctest::Approx(expected));
    std::remove(path.c_str());
}
