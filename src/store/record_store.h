// SQLite-backed storage for analysis records — the C++ port of hydra/hystore.py,
// on a redesigned binary format (see docs/CPP_PORT_PLAN.md Phase 4). Old
// Python-era .db files are not read; a fresh scan populates a new one.
//
// Three tables carry an analysis (schema user_version 2):
//
//   * `results` — one row per run, keyed by the FULL settings it ran under:
//     the chart, the chart mode, the SP cap, and the Lens (ms limit + score
//     range). Summary columns are denormalized onto it so a sortable library
//     listing never has to inflate anything. The row holds a *structure* blob
//     (the path tree's shape) rather than the paths themselves.
//   * `paths` — every distinct path node, content-addressed by its hash and
//     shared across every result that references it (store/path_codec.h). A
//     path is never stored twice.
//   * `path_refs` — which nodes each result uses, so the store can garbage
//     collect a node the moment nothing points at it.
//
// `songmeta` (one row per chart file, keyed by content hash) is unchanged.
//
// Why the full settings and not just the cap: a run under a different ms
// limit or score range is a different answer, and overwriting one with the
// other lost the first. Now they coexist, and a lookup asks for the one it
// wants. The SP cap half of that identity is docs/adr/0003; lookups name the
// cap with a CapQuery and the rest with a Lens.

#ifndef HYDRA_STORE_RECORD_STORE_H
#define HYDRA_STORE_RECORD_STORE_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/timing.h"
#include "parse/song.h"
#include "store/path_codec.h"

struct sqlite3;
struct sqlite3_stmt;

namespace hydra::store {

// The summary columns computed from a record's best path, matching
// hystore._SUMMARY_COLUMNS / summarize_path. All fields are unset when the
// record has no paths (an empty/incompatible result).
struct PathSummary {
    std::optional<int64_t> score;
    std::optional<int> actcount;
    std::optional<int> maxskip;
    std::optional<double> hardest_ms;
    std::optional<double> avgmult;
    std::optional<int> notecount;
    std::optional<int> sqin_count;
    std::optional<int> sqout_count;
    std::optional<int> pathcount;
};

PathSummary summarize_path(const Path& path);
PathSummary summarize_record(const HydraRecord& record);

// Which cap's record a lookup wants. at(N): the record analyzed at exactly N
// bars. automatic(): the chart's highest cap above Clone Hero's 4 -- what an
// Auto run would reuse -- preferring current-version rows over stale ones.
struct CapQuery {
    std::optional<int> exact;
    static CapQuery at(int cap) { return CapQuery{cap}; }
    static CapQuery automatic() { return CapQuery{std::nullopt}; }
    // The user's SP cap setting as a query: a set cap asks for exactly that
    // one, "Auto" (unset) asks for whatever an Auto run would reuse.
    static CapQuery from_setting(std::optional<int> sp_cap) {
        return sp_cap ? at(*sp_cap) : automatic();
    }
    bool is_auto() const { return !exact.has_value(); }
    // Spelled out rather than defaulted: this project builds as C++17.
    bool operator==(const CapQuery& other) const { return exact == other.exact; }
    bool operator!=(const CapQuery& other) const { return !(*this == other); }
};

// The rest of the settings a run happened under: the ms limit and the score
// range. Two runs of the same chart at the same cap under different lenses
// are two results, neither overwriting the other.
//
// Canonical form, so equal settings always compare equal: a disabled ms limit
// stores value 0, because the engine ignores the number when the limit is off
// -- "off at 10" and "off at 42" ran the identical search.
struct Lens {
    // 1 = ms limit on, 0 = off, -1 = a sentinel (below).
    int ms_enabled = 0;
    int ms_value = 0;
    int depth_mode = 0;  // 0 = scores, 1 = points -- the INI's own ints
    int depth_value = 0;

    // `ms` is Settings::mslimit_value when the limit is on, nullopt when off.
    static Lens from(std::optional<int> ms, int depth_mode, int depth_value) {
        Lens lens;
        lens.ms_enabled = ms ? 1 : 0;
        lens.ms_value = ms ? *ms : 0;
        lens.depth_mode = depth_mode;
        lens.depth_value = depth_value;
        return lens;
    }

    // A row migrated or imported from an older database: it has a result, but
    // nothing records which settings produced it. Such a row always reads
    // Stale, and its stored blob is never decoded.
    static Lens sentinel() {
        Lens lens;
        lens.ms_enabled = -1;
        return lens;
    }
    bool is_sentinel() const { return ms_enabled == -1; }

    // Spelled out rather than defaulted: this project builds as C++17.
    bool operator==(const Lens& other) const {
        return ms_enabled == other.ms_enabled && ms_value == other.ms_value &&
               depth_mode == other.depth_mode && depth_value == other.depth_value;
    }
    bool operator!=(const Lens& other) const { return !(*this == other); }
};

// One result's identity: the chart, the chart mode, the SP cap (ADR-0003) and
// the lens. `cap` is a query because a caller may ask for "whatever Auto would
// reuse"; a row itself always has an exact cap.
struct RecordKey {
    std::string hyhash;
    std::string chartmode;
    CapQuery cap;
    Lens lens;
    bool operator==(const RecordKey& other) const {
        return hyhash == other.hyhash && chartmode == other.chartmode &&
               cap == other.cap && lens == other.lens;
    }
    bool operator!=(const RecordKey& other) const { return !(*this == other); }
};

// A result's row, fully computed and ready to insert — the expensive half of
// a save (summarizing + flattening), kept free of any db connection so a
// worker thread can build it off the main store. Mirrors hystore.prepare_row.
struct PreparedRow {
    std::string hyhash;
    std::string chartmode;
    std::string hyversion;
    int sp_cap = kCloneHeroSpCap;
    Lens lens;
    std::string bestpath;
    // The path tree's shape (store/path_codec.h) and every distinct node it
    // names, deduplicated.
    std::vector<uint8_t> structure;
    std::vector<StoredPathNode> nodes;
    PathSummary summary;
};

// Throws std::invalid_argument if the record carries no sp_cap (every
// analyzer result does), if the key names an exact cap that isn't the cap the
// record was analyzed at, or if the key's lens has the ms limit on at a value
// the record wasn't analyzed under -- each mismatch would file the result
// under settings it doesn't belong to. An automatic key takes whatever cap the
// record carries.
PreparedRow prepare_row(const RecordKey& key, const HydraRecord& record);

// hymisc.RECORD_VERSION equivalent: the app version that produced a row. For
// the store and its own tests only -- production callers must not compare
// version stamps themselves; ask a lookup for its RecordStatus instead.
std::string current_record_version();

// What a stored-record lookup found. The store is the only place that decides
// whether a row is usable: NotAnalyzed (no row at all), Stale (a row another
// Hydra version wrote, or one migrated in with unknown settings -- either way
// its contents are not trusted and its blob is never decoded), or Ready (a
// real result -- which may legitimately have zero paths).
enum class RecordStatus { NotAnalyzed, Stale, Ready };

// The answer to get_record: the status, plus the payload when it is Ready.
struct RecordLookup {
    RecordStatus status = RecordStatus::NotAnalyzed;
    std::string hyversion;              // the row's stamp; empty when NotAnalyzed
    std::optional<HydraRecord> record;  // set only when Ready
    std::optional<SongTiming> timing;   // set when Ready and the song is registered
};

// The answer to get_summary: the same status, without touching the blob.
struct SummaryLookup {
    RecordStatus status = RecordStatus::NotAnalyzed;
    std::string bestpath;  // meaningful only when status == Ready
};

// One row of list_records()/library browsing.
struct RecordListing {
    std::string hyhash;
    std::string ref_name;
    std::string ref_artist;
    std::string ref_charter;
    std::string chartmode;
    int sp_cap = kCloneHeroSpCap;
    std::string bestpath;
    PathSummary summary;
};

enum class SortColumn {
    Score, ActCount, MaxSkip, HardestMs, AvgMult, NoteCount,
    SqInCount, SqOutCount, PathCount, RefName, RefArtist, RefCharter,
};

// One scanned chart file, as browsed in the library table. Mirrors the
// `charts` table hydra_app.py's scan_library()/hyutil.ScanItem builds — that
// table lives in the GUI layer in Python (not hystore.py), so it wasn't part
// of the Phase 4 port; it's added here since it belongs in the same db file.
// `sig` is the chart files' size+mtime fingerprint that powers the rescan
// cache (see chart_library_cache); the UI ignores it.
struct ChartLibraryEntry {
    std::string md5;
    std::string title;
    std::string artist;
    std::string charter;
    std::string notespath;
    std::string rootfolder;
    std::string sig;
};

// What a rescan can reuse for a chart whose files are unchanged: keyed by
// notespath, valid while `sig` still matches what the walk sees on disk.
struct ChartCacheEntry {
    std::string sig;
    std::string md5;
    std::string title;
    std::string artist;
    std::string charter;
};
using ChartLibraryCache = std::unordered_map<std::string, ChartCacheEntry>;

class RecordStore {
public:
    // dbpath may be ":memory:" for an ephemeral store (used by tests). A db
    // from before 1.6 (records keyed without sp_cap) is migrated in place on
    // open, in one transaction; a failure rolls back and rethrows.
    explicit RecordStore(const std::string& dbpath);
    ~RecordStore();

    RecordStore(const RecordStore&) = delete;
    RecordStore& operator=(const RecordStore&) = delete;

    void close();

    // ---- writing ----------------------------------------------------------

    // Registers a song so records can be stored against it. Idempotent (INSERT
    // OR IGNORE), matching hystore.add_song.
    void add_song(const std::string& hyhash, const std::string& ref_name,
                 const std::string& ref_artist, const std::string& ref_charter,
                 const Song& song);

    void add_record(const RecordKey& key, const HydraRecord& record);
    void add_row(const PreparedRow& row);

    // ---- reading ------------------------------------------------------

    // The row's status and best-path string, without touching the blob.
    // Always returns a value; bestpath is set only when status is Ready.
    SummaryLookup get_summary(const RecordKey& key);

    // The row's status and, when Ready, the full record -- inflated and with
    // its timecodes restored against the song's tempo map, which comes back
    // in `timing` so callers never have to re-decode it. Always returns a
    // value; a Stale row's blob is not decoded at all.
    RecordLookup get_record(const RecordKey& key);

    // The song's timing context (tick resolution + tempo/meter maps), needed
    // to restore a loaded record's timecodes. nullopt if the song isn't
    // registered.
    std::optional<SongTiming> get_timing(const std::string& hyhash);

    // True when a current-version record exists for this exact key -- cap and
    // lens both -- the "skip, already analyzed" test for a batch run. Stale
    // rows and sentinel rows don't count, and neither does a result from
    // different settings.
    bool has_record(const RecordKey& key);

    // One record's song identity, as yielded by for_each_blob. Mirrors the
    // songmeta dict hystore.iter_blobs builds per row, plus the row's
    // hyversion and the status it implies (the C++ HydraRecord doesn't carry
    // a version).
    struct BlobRow {
        std::string hyhash;
        std::string ref_name;
        std::string ref_artist;
        std::string ref_charter;
        std::string chartmode;
        std::string hyversion;
        RecordStatus status = RecordStatus::Ready;
        int sp_cap = kCloneHeroSpCap;
    };

    // Calls fn once per stored record (optionally filtered to one chartmode,
    // always filtered to the wanted cap), in insertion order. Every row is
    // yielded, stale ones included; the record pointer is null unless
    // meta.status is Ready, so a stale row's blob is never decoded. Mirrors
    // hystore.iter_blobs: timecodes are NOT restored (the report only needs
    // pathstrings and summaries, which never read them).
    //
    // The lock is taken and released once per record, never held across fn --
    // this walk reads the whole library, and anything else touching the store
    // (the UI thread) must not wait on it. A record that was rewritten after
    // the walk listed it is left out of this walk rather than decoded against
    // the new row's nodes; the next walk picks it up.
    //
    // `cancel`, when given, is read between records with no lock held: set it
    // and the walk stops there. Nothing else is signalled -- the caller knows
    // it asked to stop.
    void for_each_blob(
        const std::optional<std::string>& chartmode, const CapQuery& cap,
        const Lens& lens,
        const std::function<void(const BlobRow&, const HydraRecord*)>& fn,
        const std::atomic<bool>* cancel = nullptr);

    // One-time import of the pre-1.6 Uncapped edition's separate library.
    // Copies that file's current-version records (and their songs) into this
    // store under the cap each was analyzed at. Never writes the other file.
    // Records a note in `meta` so a second call is a no-op. Returns rows
    // copied (0 when already done, the file is missing/unreadable, or it has
    // no records table).
    //
    // The old file records no settings beyond the cap, so every copied row
    // lands with the sentinel lens and reads Stale: "there was a result here,
    // but nobody knows what it answered". Re-analyzing replaces it.
    int import_legacy_uncapped(const std::string& uncapped_db_path);

    // ---- maintenance --------------------------------------------------

    // Removes results that no longer match this store's current version, plus
    // the sentinel rows migrated in from an older database, then collects any
    // path left with nothing pointing at it. Returns the number of result rows
    // removed.
    int drop_stale_records();

    // Recomputes the summary columns from stored paths. Returns rows touched.
    int reindex();

    // The library listing: one row per chart and mode -- the same row a lookup
    // for those settings would pick -- and only the Ready ones. A chart whose
    // best row is stale is left out entirely, so a report reads it the same as
    // a chart nobody has analyzed. `limit` caps the rows returned after that
    // filtering; a negative limit means no limit.
    std::vector<RecordListing> list_records(
        const std::optional<std::string>& chartmode, const CapQuery& cap, const Lens& lens,
        SortColumn order_by, bool descending, std::optional<int> limit = std::nullopt);

    // {songs, results} row counts.
    std::pair<int64_t, int64_t> counts();

    // Which fill-spawn rule wrote this file: "ch11" (Clone Hero 1.1, the
    // normal one) or "ch10" (the legacy CLI mode, search/graph.h
    // FillDeadlineRule). Unset on a db nothing has stamped yet, which reads as
    // "assume the normal rule". This is only a label on the file — the rule is
    // NOT part of a record's identity, so the two rules must never share a
    // database (docs/adr/0010). hydra_batch stamps every run.
    std::optional<std::string> engine_mode();
    void set_engine_mode(const std::string& mode);

    // ---- chart library (scan results) ----------------------------------

    // Replaces the whole library with `items`, matching hydra_app.py's
    // scan_library(): a scan always fully supersedes the previous one.
    void rebuild_chart_library(const std::vector<ChartLibraryEntry>& items);

    // The previous scan's rows as a rescan cache (empty on a fresh db, or a
    // db from before the sig column existed). Read this BEFORE
    // rebuild_chart_library replaces the table.
    ChartLibraryCache chart_library_cache();

    // Case-insensitive substring match against title/artist/charter, or the
    // whole library if search is unset. A negative limit means no limit
    // (SQLite's LIMIT convention).
    int64_t chart_library_count(const std::optional<std::string>& search = std::nullopt);
    std::vector<ChartLibraryEntry> list_chart_library(
        const std::optional<std::string>& search, int offset, int limit);

private:
    sqlite3* db_ = nullptr;
    // The rule: this lock covers sqlite calls and nothing else -- decoding a
    // blob and calling a caller's callback happen outside it. A prepared
    // statement is compiled, stepped, reset and finalized with the lock held,
    // because all four are sqlite calls. A statement's handle may outlive the
    // locked block only if it is reset first, so no cursor is open while
    // unlocked, and only if something guarantees the finalize happens under the
    // lock later; for_each_blob is the one place that does this, reusing two
    // statements across the walk instead of recompiling them per row.
    std::recursive_mutex mutex_;

    void exec(const char* sql);
    bool has_table(const char* table);
    bool has_column(const char* table, const char* column);
    void add_missing_columns();
    void migrate_records_to_cap_key();
    void create_result_tables();
    void migrate_records_to_results();
    // Every path node one result references, keyed by hash — what
    // path_codec::rebuild_record's lookup closure reads. The `stmt` overload
    // reads through a statement its caller compiled: for_each_blob prepares one
    // per walk and reuses it for every row rather than compiling one each time.
    // It resets that statement before returning, so the caller can drop the
    // lock the moment it comes back. The plain overload compiles and finalizes
    // its own statement, for callers that read one result.
    std::unordered_map<std::string, std::vector<uint8_t>> load_nodes(sqlite3_stmt* stmt,
                                                                    int64_t result_id);
    std::unordered_map<std::string, std::vector<uint8_t>> load_nodes(int64_t result_id);
    // Re-reads one result row for_each_blob listed earlier, under the lock the
    // caller holds, through a statement the caller compiled once for the whole
    // walk. Only runs when something was written on this connection after the
    // walk listed its rows -- with no write, the listing's own blob is still
    // this row's blob and re-reading it would only cost time. Resets that
    // statement on every path out, including the two skip paths, so nothing is
    // left mid-step when the caller unlocks. Fills
    // `structure` and returns true when the row at `result_id` is still the
    // record `meta` describes. Returns false when the row is gone, or when its
    // identity (chart, mode, version, cap) differs -- result ids are reused
    // after a delete, so a row rewritten since the walk started can land on the
    // same id, and decoding it as the old record would attach one chart's paths
    // to another chart's name.
    bool reload_row(sqlite3_stmt* stmt, const BlobRow& meta, int64_t result_id,
                    std::vector<uint8_t>& structure);
    std::optional<std::string> meta_get(const std::string& key);
    void meta_set(const std::string& key, const std::string& value);
};

}  // namespace hydra::store

#endif  // HYDRA_STORE_RECORD_STORE_H
