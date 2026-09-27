// SQLite-backed storage for analysis records, in the binary blob format of
// store/serialize.h. Old
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
#include <unordered_set>
#include <utility>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/rules.h"
#include "core/timing.h"
#include "parse/song.h"
#include "store/path_codec.h"

struct sqlite3;
struct sqlite3_stmt;

namespace hydra::store {

// The summary columns computed from a record's best path. All fields are
// unset when the
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
    // The best path's star count by core/stars' path_stars (solo bonus left
    // out, as Clone Hero counts it). Unset on a row written before the column
    // existed until the store fills it (fill_missing_stars).
    std::optional<int> stars;
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
    // 1 = ms limit on, 0 = off. Rows an older Hydra migrated in carry -1
    // ("settings unknown"); no lens has it, so no lookup finds them.
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
// worker thread can build it off the main store.
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

// The results version this build stamps on a row and accepts (ADR 0018; not
// the app version). For
// the store and its own tests only -- production callers must not compare
// version stamps themselves; ask a lookup for its RecordStatus instead.
std::string current_record_version();

// What a stored-record lookup found. The store is the only place that decides
// whether a row is usable: NotAnalyzed (no row at all), Stale (a row another
// Hydra version wrote, in an older path layout, or under other rules -- its
// contents are not trusted and its blob is never decoded), or Ready (a real
// result -- which may legitimately have zero paths).
enum class RecordStatus { NotAnalyzed, Stale, Ready };

// The answer to get_record: the status, plus the payload when it is Ready.
struct RecordLookup {
    RecordStatus status = RecordStatus::NotAnalyzed;
    std::string hyversion;              // the row's stamp; empty when NotAnalyzed
    // Why a Stale row is Stale; both can be true, neither is when not Stale.
    bool stale_build = false;  // another Hydra build or an older path layout
    bool stale_rules = false;  // this path layout, analyzed under other rules
    std::optional<HydraRecord> record;  // set only when Ready
    std::optional<SongTiming> timing;   // set when Ready and the song is registered
    // The last note's onset, in ms. Empty when the song was saved before
    // Hydra stored lengths; it fills on the chart's next analysis.
    std::optional<double> song_length_ms;
};

// The answer to get_summary: the same status, without touching the blob.
struct SummaryLookup {
    RecordStatus status = RecordStatus::NotAnalyzed;
    std::string bestpath;  // meaningful only when status == Ready
    // The row's summary columns; filled only when status == Ready. A Stale
    // row's numbers came from bytes this build doesn't trust, so they're not
    // handed out.
    PathSummary summary;
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

// One scanned chart file, as browsed in the library table. It lives in the
// same db file as the records.
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

// Key for a dynamics-breakdown cache row: chart identity + difficulty + pro flag.
struct DynamicsKey {
    std::string md5;
    std::string difficulty;  // difficulty_name(), e.g. "Expert"
    bool pro = false;
};

// One dynamics count ready to store: its key, its encoded blob and its count
// stamp (kDynamicsCountStamp.written). Built off the store by
// app::dynamics_entry_from_analysis, saved by RecordStore::save_analysis.
struct DynamicsEntry {
    DynamicsKey key;
    std::vector<uint8_t> blob;
    int count_version = 0;
};

class RecordStore {
public:
    // dbpath may be ":memory:" for an ephemeral store (used by tests). A db
    // from Hydra 1.6 or older keeps its old records table, unread: its charts
    // read Not analyzed (user decision 2026-09-26).
    // rules_fingerprint: core::RulesStamp::of() the rules this process runs
    // under. A row stamped with neither of its two fingerprints reads Stale.
    // core::RulesStamp::none() (a bad hydra_rules.ini) makes every row Stale.
    explicit RecordStore(const std::string& dbpath,
                         core::RulesStamp rules_fingerprint = core::default_stamp());
    ~RecordStore();

    RecordStore(const RecordStore&) = delete;
    RecordStore& operator=(const RecordStore&) = delete;

    void close();

    // ---- writing ----------------------------------------------------------

    // Registers a song so records can be stored against it. Registering it
    // again updates its names and keeps its tempo map.
    void add_song(const std::string& hyhash, const std::string& ref_name,
                 const std::string& ref_artist, const std::string& ref_charter,
                 const Song& song);

    void add_record(const RecordKey& key, const HydraRecord& record);
    void add_row(const PreparedRow& row);

    // One analyzed chart, saved in one transaction: the song's row (as
    // add_song), the result (as add_row) and, when given, its dynamics count
    // (as put_dynamics). A failure in the first two rolls all of it back. A
    // failed dynamics write is dropped on its own and never blocks the result.
    void save_analysis(const std::string& hyhash, const std::string& ref_name,
                       const std::string& ref_artist, const std::string& ref_charter,
                       const Song& song, const PreparedRow& row,
                       const std::optional<DynamicsEntry>& dynamics);

    // Stores a dynamics-breakdown blob (INSERT OR REPLACE) under the caller's
    // count stamp (kDynamicsCountStamp.written; go through app::save_dynamics).
    void put_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob,
                      int count_version);
    // Returns the blob for this key, or nullopt when the row is missing or
    // its count stamp isn't current (kDynamicsCountStamp; the caller then
    // recounts it).
    std::optional<std::vector<uint8_t>> get_dynamics(const DynamicsKey& key);

    // ---- reading ------------------------------------------------------

    // The row's status and best-path string, without touching the blob.
    // Always returns a value; bestpath is set only when status is Ready.
    SummaryLookup get_summary(const RecordKey& key);

    // get_summary for many charts at once: one answer per entry of
    // `hyhashes`, in the same order (a repeated hash gets the same answer
    // twice). The library asks it about every chart, so the hashes are sent
    // in chunks under SQLite's bound-value limit.
    std::vector<SummaryLookup> get_summaries(const std::vector<std::string>& hyhashes,
                                             const std::string& chartmode,
                                             const CapQuery& cap, const Lens& lens);

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
    // rows don't count, and neither does a result from different settings.
    bool has_record(const RecordKey& key);

    // Every chart has_record would say yes to, for one chart mode, cap and
    // lens, in one query: the batch's skip list for the whole library.
    std::unordered_set<std::string> analyzed_hashes(const std::string& chartmode,
                                                    const CapQuery& cap, const Lens& lens);

    // One record's song identity, as yielded by for_each_blob: the song's
    // metadata row, plus the row's
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
    // meta.status is Ready, so a stale row's blob is never decoded. Timecodes
    // are NOT restored (the report only needs
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

    // ---- maintenance --------------------------------------------------

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

    // Replaces the whole library with `items`: a scan always fully
    // supersedes the previous one. All or nothing: a failure keeps the
    // previous scan's rows. Songs that already have a row take the names
    // this scan read (the first copy wins when a chart appears twice).
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
    // The two fingerprints of the rules this process runs under (fixed-cap
    // and Auto); a row stamped with neither reads Stale. Computed once, when
    // the store opens.
    core::RulesStamp rules_fingerprint_;
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
    bool has_column(const char* table, const char* column);
    void create_result_tables();
    // Fills the stars column of every Ready row that lacks it (rows written
    // before the column existed). Runs on every open; with nothing to fill
    // it reads only small columns. Returns rows filled.
    int fill_missing_stars();
    // The song's raw tempomap blob and its stored length, read under the
    // lock; the caller decodes the tempomap with no lock held. nullopt if the
    // song isn't registered.
    struct SongMetaRead {
        std::vector<uint8_t> tempomap;
        std::optional<double> length_ms;
    };
    std::optional<SongMetaRead> read_tempomap(const std::string& hyhash);
    // The bodies of add_song, add_row and put_dynamics. The caller holds the
    // lock; write_row also needs an open transaction.
    void upsert_song(const std::string& hyhash, const std::string& ref_name,
                     const std::string& ref_artist, const std::string& ref_charter,
                     const std::vector<uint8_t>& tempomap,
                     std::optional<double> length_ms);
    void write_row(const PreparedRow& row);
    void insert_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob,
                         int count_version);
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
