#include "store/record_store.h"

#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>

#include "store/serialize.h"

namespace hydra::store {

namespace {

constexpr const char* kHydraVersion = "1.3.1";

// RAII wrapper so every query site finalizes even on an early throw.
struct Stmt {
    sqlite3_stmt* p = nullptr;
    ~Stmt() {
        if (p) sqlite3_finalize(p);
    }
    operator sqlite3_stmt*() const { return p; }
};

Stmt prepare(sqlite3* db, const char* sql) {
    Stmt s;
    if (sqlite3_prepare_v2(db, sql, -1, &s.p, nullptr) != SQLITE_OK)
        throw std::runtime_error(std::string("prepare failed: ") + sqlite3_errmsg(db) +
                                 " (" + sql + ")");
    return s;
}

void bind_text(sqlite3_stmt* s, int i, const std::string& v) {
    sqlite3_bind_text(s, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
}
void bind_blob(sqlite3_stmt* s, int i, const std::vector<uint8_t>& v) {
    sqlite3_bind_blob(s, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
}
std::string column_text(sqlite3_stmt* s, int i) {
    const unsigned char* p = sqlite3_column_text(s, i);
    int n = sqlite3_column_bytes(s, i);
    return p ? std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(n))
             : std::string();
}
std::vector<uint8_t> column_blob(sqlite3_stmt* s, int i) {
    const void* p = sqlite3_column_blob(s, i);
    int n = sqlite3_column_bytes(s, i);
    if (!p || n <= 0) return {};
    const uint8_t* b = static_cast<const uint8_t*>(p);
    return std::vector<uint8_t>(b, b + n);
}
std::optional<int64_t> column_opt_i64(sqlite3_stmt* s, int i) {
    if (sqlite3_column_type(s, i) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int64(s, i);
}
std::optional<double> column_opt_f64(sqlite3_stmt* s, int i) {
    if (sqlite3_column_type(s, i) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_double(s, i);
}

// ---- tempomap blob (songmeta.tempomap) ------------------------------------

std::vector<uint8_t> encode_tempomap(const Song& song) {
    BinaryWriter w;
    w.i64(song.tick_resolution());
    w.u32(static_cast<uint32_t>(song.tpm_changes.size()));
    for (const auto& [tick, tpm] : song.tpm_changes) {
        w.i64(tick);
        w.i64(tpm);
    }
    w.u32(static_cast<uint32_t>(song.bpm_changes.size()));
    for (const auto& [tick, bpm] : song.bpm_changes) {
        w.i64(tick);
        w.f64(bpm);
    }
    return std::move(w.bytes);
}

SongTiming decode_tempomap(const std::vector<uint8_t>& blob) {
    BinaryReader r(blob);
    int64_t res = r.i64();

    std::map<int64_t, int64_t> tpm;
    uint32_t ntpm = r.u32();
    for (uint32_t i = 0; i < ntpm; ++i) {
        int64_t tick = r.i64();
        tpm[tick] = r.i64();
    }

    std::map<int64_t, double> bpm;
    uint32_t nbpm = r.u32();
    for (uint32_t i = 0; i < nbpm; ++i) {
        int64_t tick = r.i64();
        bpm[tick] = r.f64();
    }

    return SongTiming(res, tpm, bpm);
}

// ---- summary column binding, in the schema's declared order ---------------

void bind_summary(sqlite3_stmt* s, int first_idx, const PathSummary& sum) {
    if (sum.score) sqlite3_bind_int64(s, first_idx, *sum.score);
    else sqlite3_bind_null(s, first_idx);
    if (sum.actcount) sqlite3_bind_int(s, first_idx + 1, *sum.actcount);
    else sqlite3_bind_null(s, first_idx + 1);
    if (sum.maxskip) sqlite3_bind_int(s, first_idx + 2, *sum.maxskip);
    else sqlite3_bind_null(s, first_idx + 2);
    if (sum.hardest_ms) sqlite3_bind_double(s, first_idx + 3, *sum.hardest_ms);
    else sqlite3_bind_null(s, first_idx + 3);
    if (sum.avgmult) sqlite3_bind_double(s, first_idx + 4, *sum.avgmult);
    else sqlite3_bind_null(s, first_idx + 4);
    if (sum.notecount) sqlite3_bind_int(s, first_idx + 5, *sum.notecount);
    else sqlite3_bind_null(s, first_idx + 5);
    if (sum.sqin_count) sqlite3_bind_int(s, first_idx + 6, *sum.sqin_count);
    else sqlite3_bind_null(s, first_idx + 6);
    if (sum.sqout_count) sqlite3_bind_int(s, first_idx + 7, *sum.sqout_count);
    else sqlite3_bind_null(s, first_idx + 7);
    if (sum.pathcount) sqlite3_bind_int(s, first_idx + 8, *sum.pathcount);
    else sqlite3_bind_null(s, first_idx + 8);
}

PathSummary read_summary(sqlite3_stmt* s, int first_idx) {
    PathSummary sum;
    sum.score = column_opt_i64(s, first_idx);
    if (auto v = column_opt_i64(s, first_idx + 1)) sum.actcount = static_cast<int>(*v);
    if (auto v = column_opt_i64(s, first_idx + 2)) sum.maxskip = static_cast<int>(*v);
    sum.hardest_ms = column_opt_f64(s, first_idx + 3);
    sum.avgmult = column_opt_f64(s, first_idx + 4);
    if (auto v = column_opt_i64(s, first_idx + 5)) sum.notecount = static_cast<int>(*v);
    if (auto v = column_opt_i64(s, first_idx + 6)) sum.sqin_count = static_cast<int>(*v);
    if (auto v = column_opt_i64(s, first_idx + 7)) sum.sqout_count = static_cast<int>(*v);
    if (auto v = column_opt_i64(s, first_idx + 8)) sum.pathcount = static_cast<int>(*v);
    return sum;
}

const char* sort_column_name(SortColumn c) {
    switch (c) {
        case SortColumn::Score: return "score";
        case SortColumn::ActCount: return "actcount";
        case SortColumn::MaxSkip: return "maxskip";
        case SortColumn::HardestMs: return "hardest_ms";
        case SortColumn::AvgMult: return "avgmult";
        case SortColumn::NoteCount: return "notecount";
        case SortColumn::SqInCount: return "sqin_count";
        case SortColumn::SqOutCount: return "sqout_count";
        case SortColumn::PathCount: return "pathcount";
        case SortColumn::RefName: return "ref_name";
        case SortColumn::RefArtist: return "ref_artist";
        case SortColumn::RefCharter: return "ref_charter";
    }
    return "score";
}
bool sort_column_is_songmeta(SortColumn c) {
    return c == SortColumn::RefName || c == SortColumn::RefArtist ||
           c == SortColumn::RefCharter;
}

}  // namespace

// ---- summarize_path / summarize_record / prepare_row -----------------------

PathSummary summarize_path(const Path& path) {
    PathSummary s;
    std::vector<Activation> acts = path.all_activations();

    s.score = path.totalscore();
    s.actcount = static_cast<int>(acts.size());

    int maxskip = 0;
    std::optional<double> hardest;
    int sqin = 0, sqout = 0;
    for (const Activation& a : acts) {
        if (a.skips && *a.skips > maxskip) maxskip = *a.skips;
        if (auto d = a.difficulty()) {
            if (!hardest || *d > *hardest) hardest = d;
        }
        for (const SPSqueeze& sq : a.sqinouts) {
            if (sq.kind == SqueezeKind::SqIn) ++sqin;
            else ++sqout;
        }
    }
    s.maxskip = maxskip;
    s.hardest_ms = hardest;
    s.avgmult = path.avg_mult();
    s.notecount = path.notecount;
    s.sqin_count = sqin;
    s.sqout_count = sqout;
    return s;
}

PathSummary summarize_record(const HydraRecord& record) {
    if (record.paths.empty()) return PathSummary{};

    PathSummary s = summarize_path(record.best_path());
    int pathcount = 0;
    for (const Path& p : record.paths) pathcount += p.tied_pathcount();
    s.pathcount = pathcount;
    return s;
}

std::string current_record_version(bool uncapped) {
    std::string v = kHydraVersion;
    if (uncapped) v += ".uncapped";
    return v;
}

PreparedRow prepare_row(const std::string& hyhash, const std::string& chartmode,
                        const HydraRecord& record, bool uncapped) {
    PreparedRow row;
    row.hyhash = hyhash;
    row.chartmode = chartmode;
    row.hyversion = current_record_version(uncapped);
    row.bestpath = record.paths.empty() ? std::string() : record.best_path().pathstring();
    row.summary = summarize_record(record);
    row.blob = write_record(record);
    return row;
}

// ---- RecordStore ------------------------------------------------------

RecordStore::RecordStore(const std::string& dbpath, bool uncapped) : uncapped_(uncapped) {
    if (sqlite3_open(dbpath.c_str(), &db_) != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "unknown error";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("failed to open database '" + dbpath + "': " + msg);
    }

    exec(
        "CREATE TABLE IF NOT EXISTS songmeta ("
        "  hyhash      TEXT PRIMARY KEY,"
        "  ref_name    TEXT,"
        "  ref_artist  TEXT,"
        "  ref_charter TEXT,"
        "  tempomap    BLOB NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS records ("
        "  hyhash      TEXT NOT NULL,"
        "  chartmode   TEXT NOT NULL,"
        "  hyversion   TEXT NOT NULL,"
        "  bestpath    TEXT NOT NULL,"
        "  blob        BLOB NOT NULL,"
        "  score       INTEGER,"
        "  actcount    INTEGER,"
        "  maxskip     INTEGER,"
        "  hardest_ms  REAL,"
        "  avgmult     REAL,"
        "  notecount   INTEGER,"
        "  sqin_count  INTEGER,"
        "  sqout_count INTEGER,"
        "  pathcount   INTEGER,"
        "  PRIMARY KEY (hyhash, chartmode)"
        ");"
        "CREATE TABLE IF NOT EXISTS charts ("
        "  md5    TEXT,"
        "  name   TEXT,"
        "  artist TEXT,"
        "  charter TEXT,"
        "  path   TEXT,"
        "  folder TEXT"
        ");");

    add_missing_columns();
}

RecordStore::~RecordStore() { close(); }

void RecordStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void RecordStore::exec(const char* sql) {
    char* errmsg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("sqlite exec failed: " + msg);
    }
}

void RecordStore::add_missing_columns() {
    // Bring an older on-disk copy of this schema up to the current summary
    // column set, the same way hystore._add_missing_columns does.
    std::vector<std::string> existing;
    Stmt info = prepare(db_, "PRAGMA table_info(records)");
    while (sqlite3_step(info) == SQLITE_ROW) existing.push_back(column_text(info, 1));

    static const std::pair<const char*, const char*> summary_defs[] = {
        {"score", "INTEGER"}, {"actcount", "INTEGER"}, {"maxskip", "INTEGER"},
        {"hardest_ms", "REAL"}, {"avgmult", "REAL"}, {"notecount", "INTEGER"},
        {"sqin_count", "INTEGER"}, {"sqout_count", "INTEGER"}, {"pathcount", "INTEGER"},
    };
    for (const auto& [name, type] : summary_defs) {
        if (std::find(existing.begin(), existing.end(), name) == existing.end())
            exec((std::string("ALTER TABLE records ADD COLUMN ") + name + " " + type).c_str());
    }
}

void RecordStore::add_song(const std::string& hyhash, const std::string& ref_name,
                           const std::string& ref_artist, const std::string& ref_charter,
                           const Song& song) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<uint8_t> tempomap = encode_tempomap(song);

    Stmt s = prepare(db_,
        "INSERT OR IGNORE INTO songmeta (hyhash, ref_name, ref_artist, ref_charter, tempomap) "
        "VALUES (?,?,?,?,?)");
    bind_text(s, 1, hyhash);
    bind_text(s, 2, ref_name);
    bind_text(s, 3, ref_artist);
    bind_text(s, 4, ref_charter);
    bind_blob(s, 5, tempomap);
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string("add_song failed: ") + sqlite3_errmsg(db_));
}

void RecordStore::add_record(const std::string& hyhash, const std::string& chartmode,
                             const HydraRecord& record) {
    add_row(prepare_row(hyhash, chartmode, record, uncapped_));
}

void RecordStore::add_row(const PreparedRow& row) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    Stmt s = prepare(db_,
        "INSERT OR REPLACE INTO records "
        "(hyhash, chartmode, hyversion, bestpath, blob, "
        " score, actcount, maxskip, hardest_ms, avgmult, notecount, sqin_count, "
        " sqout_count, pathcount) "
        "VALUES (?,?,?,?,?, ?,?,?,?,?,?,?,?,?)");
    bind_text(s, 1, row.hyhash);
    bind_text(s, 2, row.chartmode);
    bind_text(s, 3, row.hyversion);
    bind_text(s, 4, row.bestpath);
    bind_blob(s, 5, row.blob);
    bind_summary(s, 6, row.summary);
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string("add_row failed: ") + sqlite3_errmsg(db_));
}

std::optional<std::pair<std::string, std::string>> RecordStore::get_summary(
    const std::string& hyhash, const std::string& chartmode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_,
        "SELECT hyversion, bestpath FROM records WHERE hyhash=? AND chartmode=?");
    bind_text(s, 1, hyhash);
    bind_text(s, 2, chartmode);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    return std::make_pair(column_text(s, 0), column_text(s, 1));
}

std::optional<HydraRecord> RecordStore::get_record(const std::string& hyhash,
                                                    const std::string& chartmode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_, "SELECT hyversion, blob FROM records WHERE hyhash=? AND chartmode=?");
    bind_text(s, 1, hyhash);
    bind_text(s, 2, chartmode);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;

    std::string hyversion = column_text(s, 0);
    if (hyversion != current_record_version(uncapped_)) {
        // Stamped by a different version/edition: read as empty, matching
        // hydata.json_load's short-circuit on hyversion mismatch — no
        // paths to speak of, so callers should prompt a re-analyze.
        return HydraRecord{};
    }

    HydraRecord record = read_record(column_blob(s, 1));
    if (auto timing = get_timing(hyhash)) restore_timecodes(record, *timing);
    return record;
}

std::optional<SongTiming> RecordStore::get_timing(const std::string& hyhash) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_, "SELECT tempomap FROM songmeta WHERE hyhash=?");
    bind_text(s, 1, hyhash);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    return decode_tempomap(column_blob(s, 0));
}

bool RecordStore::has_record(const std::string& hyhash, const std::string& chartmode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_, "SELECT 1 FROM records WHERE hyhash=? AND chartmode=?");
    bind_text(s, 1, hyhash);
    bind_text(s, 2, chartmode);
    return sqlite3_step(s) == SQLITE_ROW;
}

void RecordStore::for_each_blob(
    const std::optional<std::string>& chartmode,
    const std::function<void(const BlobRow&, const HydraRecord&)>& fn) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Read every row up front (like reindex) so fn never runs under an open
    // sqlite cursor — it may call back into this store.
    struct Row {
        BlobRow meta;
        std::vector<uint8_t> blob;
    };
    std::vector<Row> rows;
    {
        std::string sql =
            "SELECT s.hyhash, s.ref_name, s.ref_artist, s.ref_charter, "
            "r.chartmode, r.hyversion, r.blob "
            "FROM records r JOIN songmeta s ON s.hyhash = r.hyhash";
        if (chartmode) sql += " WHERE r.chartmode = ?";
        // Python's iter_blobs has no ORDER BY and gets insertion order from
        // sqlite's table scan; say so explicitly here.
        sql += " ORDER BY r.rowid";

        Stmt s = prepare(db_, sql.c_str());
        if (chartmode) bind_text(s, 1, *chartmode);
        while (sqlite3_step(s) == SQLITE_ROW)
            rows.push_back({{column_text(s, 0), column_text(s, 1), column_text(s, 2),
                             column_text(s, 3), column_text(s, 4), column_text(s, 5)},
                            column_blob(s, 6)});
    }

    std::string current = current_record_version(uncapped_);
    for (const Row& row : rows) {
        HydraRecord record;
        if (row.meta.hyversion == current) record = read_record(row.blob);
        fn(row.meta, record);
    }
}

int RecordStore::drop_stale_records() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_, "DELETE FROM records WHERE hyversion != ?");
    bind_text(s, 1, current_record_version(uncapped_));
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string("drop_stale_records failed: ") +
                                 sqlite3_errmsg(db_));
    return sqlite3_changes(db_);
}

int RecordStore::reindex() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    struct Row {
        std::string hyhash, chartmode, hyversion;
        std::vector<uint8_t> blob;
    };
    std::vector<Row> rows;
    {
        Stmt s = prepare(db_, "SELECT hyhash, chartmode, hyversion, blob FROM records");
        while (sqlite3_step(s) == SQLITE_ROW)
            rows.push_back({column_text(s, 0), column_text(s, 1), column_text(s, 2),
                            column_blob(s, 3)});
    }

    std::string current = current_record_version(uncapped_);
    int done = 0;
    for (const Row& row : rows) {
        PathSummary summary;
        if (row.hyversion == current) summary = summarize_record(read_record(row.blob));

        Stmt s = prepare(db_,
            "UPDATE records SET score=?,actcount=?,maxskip=?,hardest_ms=?,avgmult=?,"
            "notecount=?,sqin_count=?,sqout_count=?,pathcount=? "
            "WHERE hyhash=? AND chartmode=?");
        bind_summary(s, 1, summary);
        bind_text(s, 10, row.hyhash);
        bind_text(s, 11, row.chartmode);
        if (sqlite3_step(s) != SQLITE_DONE)
            throw std::runtime_error(std::string("reindex failed: ") + sqlite3_errmsg(db_));
        ++done;
    }
    return done;
}

std::vector<RecordListing> RecordStore::list_records(
    const std::optional<std::string>& chartmode, SortColumn order_by, bool descending,
    std::optional<int> limit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string sql =
        "SELECT s.hyhash, s.ref_name, s.ref_artist, s.ref_charter, r.chartmode, r.bestpath, "
        "r.score, r.actcount, r.maxskip, r.hardest_ms, r.avgmult, r.notecount, "
        "r.sqin_count, r.sqout_count, r.pathcount "
        "FROM records r JOIN songmeta s ON s.hyhash = r.hyhash";
    if (chartmode) sql += " WHERE r.chartmode = ?";

    const char* prefix = sort_column_is_songmeta(order_by) ? "s." : "r.";
    sql += " ORDER BY ";
    sql += prefix;
    sql += sort_column_name(order_by);
    sql += descending ? " DESC" : " ASC";
    if (limit) sql += " LIMIT " + std::to_string(*limit);

    Stmt s = prepare(db_, sql.c_str());
    if (chartmode) bind_text(s, 1, *chartmode);

    std::vector<RecordListing> out;
    while (sqlite3_step(s) == SQLITE_ROW) {
        RecordListing rl;
        rl.hyhash = column_text(s, 0);
        rl.ref_name = column_text(s, 1);
        rl.ref_artist = column_text(s, 2);
        rl.ref_charter = column_text(s, 3);
        rl.chartmode = column_text(s, 4);
        rl.bestpath = column_text(s, 5);
        rl.summary = read_summary(s, 6);
        out.push_back(std::move(rl));
    }
    return out;
}

std::pair<int64_t, int64_t> RecordStore::counts() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt songs = prepare(db_, "SELECT COUNT(*) FROM songmeta");
    sqlite3_step(songs);
    int64_t nsongs = sqlite3_column_int64(songs, 0);

    Stmt records = prepare(db_, "SELECT COUNT(*) FROM records");
    sqlite3_step(records);
    int64_t nrecords = sqlite3_column_int64(records, 0);

    return {nsongs, nrecords};
}

// ---- chart library ------------------------------------------------------

void RecordStore::rebuild_chart_library(const std::vector<ChartLibraryEntry>& items) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    exec("DROP TABLE IF EXISTS charts");
    exec("CREATE TABLE charts (md5 TEXT, name TEXT, artist TEXT, charter TEXT, "
        "path TEXT, folder TEXT)");

    exec("BEGIN");
    Stmt s = prepare(db_, "INSERT INTO charts VALUES (?,?,?,?,?,?)");
    for (const ChartLibraryEntry& item : items) {
        sqlite3_reset(s);
        bind_text(s, 1, item.md5);
        bind_text(s, 2, item.title);
        bind_text(s, 3, item.artist);
        bind_text(s, 4, item.charter);
        bind_text(s, 5, item.notespath);
        bind_text(s, 6, item.rootfolder);
        if (sqlite3_step(s) != SQLITE_DONE) {
            exec("ROLLBACK");
            throw std::runtime_error(std::string("rebuild_chart_library failed: ") +
                                     sqlite3_errmsg(db_));
        }
    }
    exec("COMMIT");
}

int64_t RecordStore::chart_library_count(const std::optional<std::string>& search) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string sql = "SELECT COUNT(*) FROM charts";
    if (search) sql += " WHERE name LIKE ? OR artist LIKE ?";
    Stmt s = prepare(db_, sql.c_str());
    if (search) {
        std::string param = "%" + *search + "%";
        bind_text(s, 1, param);
        bind_text(s, 2, param);
    }
    if (sqlite3_step(s) != SQLITE_ROW) return 0;
    return sqlite3_column_int64(s, 0);
}

std::vector<ChartLibraryEntry> RecordStore::list_chart_library(
    const std::optional<std::string>& search, int offset, int limit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string sql = "SELECT md5, name, artist, charter, path, folder FROM charts";
    if (search) sql += " WHERE name LIKE ? OR artist LIKE ?";
    sql += " ORDER BY name LIMIT ? OFFSET ?";

    Stmt s = prepare(db_, sql.c_str());
    int idx = 1;
    std::string param;
    if (search) {
        param = "%" + *search + "%";
        bind_text(s, idx++, param);
        bind_text(s, idx++, param);
    }
    sqlite3_bind_int(s, idx++, limit);
    sqlite3_bind_int(s, idx++, offset);

    std::vector<ChartLibraryEntry> out;
    while (sqlite3_step(s) == SQLITE_ROW) {
        ChartLibraryEntry e;
        e.md5 = column_text(s, 0);
        e.title = column_text(s, 1);
        e.artist = column_text(s, 2);
        e.charter = column_text(s, 3);
        e.notespath = column_text(s, 4);
        e.rootfolder = column_text(s, 5);
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace hydra::store
