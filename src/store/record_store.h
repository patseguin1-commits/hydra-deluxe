// SQLite-backed storage for analysis records — the C++ port of hydra/hystore.py,
// on a redesigned binary format (see docs/CPP_PORT_PLAN.md Phase 4). Old
// Python-era .db files are not read; a fresh scan populates a new one.
//
// Schema keeps hystore's shape: a `songmeta` table (one row per chart file,
// keyed by content hash) and a `records` table (one row per (hyhash,
// chartmode) analysis), with denormalized summary columns on `records` so a
// sortable library listing never has to inflate a blob.

#ifndef HYDRA_STORE_RECORD_STORE_H
#define HYDRA_STORE_RECORD_STORE_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
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
    std::string bestpath;
    std::vector<uint8_t> blob;
    PathSummary summary;
};

PreparedRow prepare_row(const std::string& hyhash, const std::string& chartmode,
                        const HydraRecord& record, bool uncapped);

// hymisc.RECORD_VERSION equivalent: capped and uncapped records are stamped
// with versions that can never compare equal, so a record from the other
// edition reads as incompatible instead of silently mixing in.
std::string current_record_version(bool uncapped);

// One row of list_records()/library browsing.
struct RecordListing {
    std::string hyhash;
    std::string ref_name;
    std::string ref_artist;
    std::string ref_charter;
    std::string chartmode;
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
struct ChartLibraryEntry {
    std::string md5;
    std::string title;
    std::string artist;
    std::string charter;
    std::string notespath;
    std::string rootfolder;
};

class RecordStore {
public:
    // dbpath may be ":memory:" for an ephemeral store (used by tests). uncapped
    // selects which edition's records this store considers current — mirrors
    // hymisc.apply_edition driving hymisc.RECORD_VERSION.
    RecordStore(const std::string& dbpath, bool uncapped);
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
    // record for this key.
    std::optional<std::pair<std::string, std::string>> get_summary(
        const std::string& hyhash, const std::string& chartmode);

    // The full record, inflated and with its timecodes restored against the
    // song's tempo map. nullopt if there's no row; a record with empty paths
    // if the stored version doesn't match this store's current edition.
    std::optional<HydraRecord> get_record(const std::string& hyhash,
                                          const std::string& chartmode);

    // The song's timing context (tick resolution + tempo/meter maps), needed
    // to restore a loaded record's timecodes. nullopt if the song isn't
    // registered.
    std::optional<SongTiming> get_timing(const std::string& hyhash);

    bool has_record(const std::string& hyhash, const std::string& chartmode);

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
    };

    // Calls fn once per stored record (optionally filtered to one chartmode),
    // in insertion order, with the record inflated from its blob. Mirrors
    // hystore.iter_blobs: timecodes are NOT restored (the report only needs
    // pathstrings and summaries, which never read them), and a record stamped
    // by another version/edition arrives empty — json_load's short-circuit,
    // same as get_record.
    void for_each_blob(
        const std::optional<std::string>& chartmode,
        const std::function<void(const BlobRow&, const HydraRecord&)>& fn);

    // ---- maintenance --------------------------------------------------

    // Removes records that no longer match this store's current version.
    // Returns the number of rows removed.
    int drop_stale_records();

    // Recomputes the summary columns from stored blobs. Returns rows touched.
    int reindex();

    std::vector<RecordListing> list_records(
        const std::optional<std::string>& chartmode, SortColumn order_by,
        bool descending, std::optional<int> limit = std::nullopt);

    // {songs, records} row counts.
    std::pair<int64_t, int64_t> counts();

    // ---- chart library (scan results) ----------------------------------

    // Replaces the whole library with `items`, matching hydra_app.py's
    // scan_library(): a scan always fully supersedes the previous one.
    void rebuild_chart_library(const std::vector<ChartLibraryEntry>& items);

    // Case-insensitive substring match against title/artist, or the whole
    // library if search is unset.
    int64_t chart_library_count(const std::optional<std::string>& search = std::nullopt);
    std::vector<ChartLibraryEntry> list_chart_library(
        const std::optional<std::string>& search, int offset, int limit);

private:
    sqlite3* db_ = nullptr;
    bool uncapped_;
    std::recursive_mutex mutex_;

    void exec(const char* sql);
    void add_missing_columns();
};

}  // namespace hydra::store

#endif  // HYDRA_STORE_RECORD_STORE_H
