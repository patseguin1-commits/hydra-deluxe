// SQLite-backed storage for analysis records — the C++ port of hydra/hystore.py,
// on a redesigned binary format (see docs/CPP_PORT_PLAN.md Phase 4). Old
// Python-era .db files are not read; a fresh scan populates a new one.
//
// Schema keeps hystore's shape: a `songmeta` table (one row per chart file,
// keyed by content hash) and a `records` table (one row per (hyhash,
// chartmode, sp_cap) analysis), with denormalized summary columns on
// `records` so a sortable library listing never has to inflate a blob.
//
// The SP cap is part of a record's identity (docs/adr/0003): a chart keeps
// one record per cap it was analyzed at, so a 4-bar result and a 64-bar
// what-if never overwrite each other. Lookups say which cap they want with a
// CapQuery.

#ifndef HYDRA_STORE_RECORD_STORE_H
#define HYDRA_STORE_RECORD_STORE_H

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

struct sqlite3;

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

// A record's row, fully computed and ready to insert — the expensive half of
// a save (summarizing + serializing), kept free of any db connection so a
// worker thread can build it off the main store. Mirrors hystore.prepare_row.
struct PreparedRow {
    std::string hyhash;
    std::string chartmode;
    std::string hyversion;
    int sp_cap = kCloneHeroSpCap;
    std::string bestpath;
    std::vector<uint8_t> blob;
    PathSummary summary;
};

// Throws std::invalid_argument if the record carries no sp_cap (every
// analyzer result does).
PreparedRow prepare_row(const std::string& hyhash, const std::string& chartmode,
                        const HydraRecord& record);

// hymisc.RECORD_VERSION equivalent: the app version that produced a row. A
// row stamped by another version reads as stale.
std::string current_record_version();

// Which cap's record a lookup wants. at(N): the record analyzed at exactly N
// bars. automatic(): the chart's highest cap above Clone Hero's 4 -- what an
// Auto run would reuse -- preferring current-version rows over stale ones.
struct CapQuery {
    std::optional<int> exact;
    static CapQuery at(int cap) { return CapQuery{cap}; }
    static CapQuery automatic() { return CapQuery{std::nullopt}; }
    bool is_auto() const { return !exact.has_value(); }
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

    void add_record(const std::string& hyhash, const std::string& chartmode,
                    const HydraRecord& record);
    void add_row(const PreparedRow& row);

    // ---- reading ------------------------------------------------------

    // (hyversion, bestpath), without touching the blob. nullopt if there's no
    // record for this key and cap.
    std::optional<std::pair<std::string, std::string>> get_summary(
        const std::string& hyhash, const std::string& chartmode, const CapQuery& cap);

    // The full record, inflated and with its timecodes restored against the
    // song's tempo map. nullopt if there's no row; a record with empty paths
    // if the stored version doesn't match the current one.
    std::optional<HydraRecord> get_record(const std::string& hyhash,
                                          const std::string& chartmode,
                                          const CapQuery& cap);

    // The song's timing context (tick resolution + tempo/meter maps), needed
    // to restore a loaded record's timecodes. nullopt if the song isn't
    // registered.
    std::optional<SongTiming> get_timing(const std::string& hyhash);

    // True when a current-version record exists for this key and cap -- the
    // "skip, already analyzed" test for a batch run. Stale rows don't count.
    bool has_record(const std::string& hyhash, const std::string& chartmode,
                    const CapQuery& cap);

    // One record's song identity, as yielded by for_each_blob. Mirrors the
    // songmeta dict hystore.iter_blobs builds per row, plus the row's
    // hyversion (the C++ HydraRecord doesn't carry one).
    struct BlobRow {
        std::string hyhash;
        std::string ref_name;
        std::string ref_artist;
        std::string ref_charter;
        std::string chartmode;
        std::string hyversion;
        int sp_cap = kCloneHeroSpCap;
    };

    // Calls fn once per stored record (optionally filtered to one chartmode,
    // always filtered to the wanted cap), in insertion order, with the record
    // inflated from its blob. Mirrors
    // hystore.iter_blobs: timecodes are NOT restored (the report only needs
    // pathstrings and summaries, which never read them), and a record stamped
    // by another version/edition arrives empty — json_load's short-circuit,
    // same as get_record.
    void for_each_blob(
        const std::optional<std::string>& chartmode, const CapQuery& cap,
        const std::function<void(const BlobRow&, const HydraRecord&)>& fn);

    // One-time import of the pre-1.6 Uncapped edition's separate library.
    // Copies that file's current-version records (and their songs) into this
    // store under the cap each was analyzed at, restamped with the plain
    // version. Never writes the other file. Records a note in `meta` so a
    // second call is a no-op. Returns rows copied (0 when already done, the
    // file is missing/unreadable, or it has no records table).
    int import_legacy_uncapped(const std::string& uncapped_db_path);

    // ---- maintenance --------------------------------------------------

    // Removes records that no longer match this store's current version.
    // Returns the number of rows removed.
    int drop_stale_records();

    // Recomputes the summary columns from stored blobs. Returns rows touched.
    int reindex();

    std::vector<RecordListing> list_records(
        const std::optional<std::string>& chartmode, const CapQuery& cap,
        SortColumn order_by, bool descending, std::optional<int> limit = std::nullopt);

    // {songs, records} row counts.
    std::pair<int64_t, int64_t> counts();

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
    std::recursive_mutex mutex_;

    void exec(const char* sql);
    bool has_column(const char* table, const char* column);
    void add_missing_columns();
    void migrate_records_to_cap_key();
    std::optional<std::string> meta_get(const std::string& key);
    void meta_set(const std::string& key, const std::string& value);
};

}  // namespace hydra::store

#endif  // HYDRA_STORE_RECORD_STORE_H
