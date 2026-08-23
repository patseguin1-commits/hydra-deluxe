#include "store/record_store.h"

#include <sqlite3.h>

#include <algorithm>
#include <stdexcept>

#include "core/version.h"
#include "store/serialize.h"

namespace hydra::store {

namespace {

// Stamps every stored row; a mismatch marks the row stale (see reindex /
// drop_stale_records). Single-sourced from CMake's project version.
constexpr const char* kHydraVersion = HYDRA_VERSION;

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

// The records table's columns, single-sourced so the create, the migration
// rebuild and the legacy import all agree on the layout.
constexpr const char* kRecordsColumnDefs =
    "  hyhash      TEXT NOT NULL,"
    "  chartmode   TEXT NOT NULL,"
    "  hyversion   TEXT NOT NULL,"
    "  sp_cap      INTEGER NOT NULL,"
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
    "  PRIMARY KEY (hyhash, chartmode, sp_cap)";
constexpr const char* kRecordsColumnList =
    "hyhash, chartmode, hyversion, sp_cap, bestpath, blob, score, actcount, maxskip, "
    "hardest_ms, avgmult, notecount, sqin_count, sqout_count, pathcount";

// The blob header that peek_sp_cap needs: u32 version + opt_f64 + opt_i32.
constexpr int kBlobHeadBytes = 18;

// The cap filter for a single-row lookup (get_summary / get_record). Appended
// after "WHERE hyhash=? AND chartmode=?"; bind_cap_lookup binds its one
// parameter at the given index. Auto takes the best row: current version
// first, then the newest. Newest, not tallest: an Auto run that settles below
// an older, taller row (an imported uncapped result, a what-if the user
// typed) is the result the user just asked for, and the only one that ran
// under the current depth / ms settings. The tallest rule showed the old row
// forever and "Analyze paths!" could never replace it. Write order is the
// rowid: INSERT OR REPLACE gives a rewritten row a fresh one.
void append_cap_lookup(std::string& sql, const CapQuery& cap) {
    if (cap.exact) sql += " AND sp_cap=?";
    else sql += " AND sp_cap>" + std::to_string(kCloneHeroSpCap) +
                " ORDER BY (hyversion=?) DESC, rowid DESC LIMIT 1";
}
void bind_cap_lookup(sqlite3_stmt* s, int idx, const CapQuery& cap) {
    if (cap.exact) sqlite3_bind_int(s, idx, *cap.exact);
    else bind_text(s, idx, current_record_version());
}

// The cap filter for a set query over alias r (list_records / for_each_blob):
// exact keeps rows at that cap; Auto keeps, per (hyhash, chartmode), the one
// row an Auto lookup would pick -- no other row above 4 outranks it, where
// "outranks" is current-version first, then newer (see append_cap_lookup).
// Binds four parameters for Auto, one for exact.
void append_cap_set_filter(std::string& sql, const CapQuery& cap) {
    if (cap.exact) {
        sql += " AND r.sp_cap = ?";
        return;
    }
    std::string four = std::to_string(kCloneHeroSpCap);
    sql += " AND r.sp_cap > " + four +
           " AND NOT EXISTS (SELECT 1 FROM records x"
           "   WHERE x.hyhash = r.hyhash AND x.chartmode = r.chartmode"
           "     AND x.sp_cap > " + four +
           "     AND ((x.hyversion = ?) > (r.hyversion = ?)"
           "          OR ((x.hyversion = ?) = (r.hyversion = ?) AND x.rowid > r.rowid)))";
}
int bind_cap_set_filter(sqlite3_stmt* s, int idx, const CapQuery& cap) {
    if (cap.exact) {
        sqlite3_bind_int(s, idx, *cap.exact);
        return idx + 1;
    }
    for (int i = 0; i < 4; ++i) bind_text(s, idx + i, current_record_version());
    return idx + 4;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
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

std::string current_record_version() { return kHydraVersion; }

PreparedRow prepare_row(const RecordKey& key, const HydraRecord& record) {
    if (!record.sp_cap)
        throw std::invalid_argument("prepare_row: record carries no sp_cap");
    if (key.cap.exact && *key.cap.exact != *record.sp_cap)
        throw std::invalid_argument("prepare_row: key asks for sp_cap " +
                                    std::to_string(*key.cap.exact) +
                                    " but the record was analyzed at " +
                                    std::to_string(*record.sp_cap));
    PreparedRow row;
    row.hyhash = key.hyhash;
    row.chartmode = key.chartmode;
    row.hyversion = current_record_version();
    row.sp_cap = *record.sp_cap;
    row.bestpath = record.paths.empty() ? std::string() : record.best_path().pathstring();
    row.summary = summarize_record(record);
    row.blob = write_record(record);
    return row;
}

// ---- RecordStore ------------------------------------------------------

RecordStore::RecordStore(const std::string& dbpath) {
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
        "CREATE TABLE IF NOT EXISTS charts ("
        "  md5    TEXT,"
        "  name   TEXT,"
        "  artist TEXT,"
        "  charter TEXT,"
        "  path   TEXT,"
        "  folder TEXT,"
        "  sig    TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS meta ("
        "  key   TEXT PRIMARY KEY,"
        "  value TEXT"
        ");");
    exec((std::string("CREATE TABLE IF NOT EXISTS records (") + kRecordsColumnDefs + ");")
             .c_str());

    add_missing_columns();
    if (!has_column("records", "sp_cap")) migrate_records_to_cap_key();
    // Schema 1 = records keyed by cap. A fresh db is born at it.
    exec("PRAGMA user_version = 1");
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

bool RecordStore::has_column(const char* table, const char* column) {
    std::string sql = std::string("PRAGMA table_info(") + table + ")";
    Stmt info = prepare(db_, sql.c_str());
    while (sqlite3_step(info) == SQLITE_ROW)
        if (column_text(info, 1) == column) return true;
    return false;
}

std::optional<std::string> RecordStore::meta_get(const std::string& key) {
    Stmt s = prepare(db_, "SELECT value FROM meta WHERE key=?");
    bind_text(s, 1, key);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    return column_text(s, 0);
}

void RecordStore::meta_set(const std::string& key, const std::string& value) {
    Stmt s = prepare(db_, "INSERT OR REPLACE INTO meta (key, value) VALUES (?,?)");
    bind_text(s, 1, key);
    bind_text(s, 2, value);
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string("meta_set failed: ") + sqlite3_errmsg(db_));
}

void RecordStore::migrate_records_to_cap_key() {
    // A pre-1.6 table is keyed (hyhash, chartmode) with no sp_cap column.
    // SQLite can't change a primary key in place, so: add the column, fill it
    // from each blob's header, copy everything into a table with the new key,
    // and swap. One transaction -- a failure leaves the old table untouched
    // and the next open tries again.
    exec("BEGIN");
    try {
        exec("ALTER TABLE records ADD COLUMN sp_cap INTEGER");

        struct Fix {
            int64_t rowid;
            std::optional<int> cap;
            std::string version;
        };
        std::vector<Fix> fixes;
        {
            std::string sql = "SELECT rowid, hyversion, substr(blob,1," +
                              std::to_string(kBlobHeadBytes) + ") FROM records";
            Stmt s = prepare(db_, sql.c_str());
            while (sqlite3_step(s) == SQLITE_ROW) {
                Fix fix{sqlite3_column_int64(s, 0), peek_sp_cap(column_blob(s, 2)),
                        column_text(s, 1)};
                // The old Uncapped edition stamped its rows ".uncapped"; the
                // cap lives in the blob, so the suffix carries nothing now.
                // A plain-stamped row with no readable cap was a 4-bar run
                // (the only kind the main edition made); an uncapped one
                // with no readable cap can't be placed and is dropped.
                const std::string suffix = ".uncapped";
                bool was_uncapped = ends_with(fix.version, suffix);
                if (was_uncapped) fix.version.resize(fix.version.size() - suffix.size());
                if (!fix.cap && !was_uncapped) fix.cap = kCloneHeroSpCap;
                fixes.push_back(std::move(fix));
            }
        }
        for (const Fix& fix : fixes) {
            Stmt s = prepare(db_, "UPDATE records SET sp_cap=?, hyversion=? WHERE rowid=?");
            if (fix.cap) sqlite3_bind_int(s, 1, *fix.cap);
            else sqlite3_bind_null(s, 1);
            bind_text(s, 2, fix.version);
            sqlite3_bind_int64(s, 3, fix.rowid);
            if (sqlite3_step(s) != SQLITE_DONE)
                throw std::runtime_error(std::string("migration update failed: ") +
                                         sqlite3_errmsg(db_));
        }

        exec((std::string("CREATE TABLE records_new (") + kRecordsColumnDefs + ")").c_str());
        exec((std::string("INSERT INTO records_new (") + kRecordsColumnList + ") SELECT " +
              kRecordsColumnList + " FROM records WHERE sp_cap IS NOT NULL ORDER BY rowid")
                 .c_str());
        exec("DROP TABLE records");
        exec("ALTER TABLE records_new RENAME TO records");
        exec("PRAGMA user_version = 1");
        exec("COMMIT");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}

int RecordStore::import_legacy_uncapped(const std::string& uncapped_db_path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (meta_get("uncapped_import")) return 0;

    // ATTACH can't run inside a transaction, so it brackets the whole copy.
    {
        Stmt attach = prepare(db_, "ATTACH DATABASE ? AS unc");
        bind_text(attach, 1, uncapped_db_path);
        // Unreadable (locked, corrupt): leave the note unset so a later open
        // can try again.
        if (sqlite3_step(attach) != SQLITE_DONE) return 0;
    }

    int copied = 0;
    bool in_txn = false;
    try {
        bool has_records = false;
        {
            Stmt probe = prepare(db_, "SELECT COUNT(*) FROM unc.sqlite_master "
                                      "WHERE type='table' AND name='records'");
            has_records = sqlite3_step(probe) == SQLITE_ROW && sqlite3_column_int(probe, 0) > 0;
        }
        const std::string legacy_stamp = current_record_version() + ".uncapped";

        exec("BEGIN");
        in_txn = true;
        if (has_records) {
            {
                Stmt s = prepare(db_,
                    "INSERT OR IGNORE INTO main.songmeta "
                    "(hyhash, ref_name, ref_artist, ref_charter, tempomap) "
                    "SELECT hyhash, ref_name, ref_artist, ref_charter, tempomap "
                    "FROM unc.songmeta WHERE hyhash IN "
                    "(SELECT hyhash FROM unc.records WHERE hyversion=?)");
                bind_text(s, 1, legacy_stamp);
                if (sqlite3_step(s) != SQLITE_DONE)
                    throw std::runtime_error(std::string("import songmeta failed: ") +
                                             sqlite3_errmsg(db_));
            }

            struct Src {
                int64_t rowid;
                int cap;
            };
            std::vector<Src> sources;
            {
                std::string sql = "SELECT rowid, substr(blob,1," +
                                  std::to_string(kBlobHeadBytes) +
                                  ") FROM unc.records WHERE hyversion=?";
                Stmt s = prepare(db_, sql.c_str());
                bind_text(s, 1, legacy_stamp);
                while (sqlite3_step(s) == SQLITE_ROW)
                    if (auto cap = peek_sp_cap(column_blob(s, 1)))
                        sources.push_back({sqlite3_column_int64(s, 0), *cap});
            }
            for (const Src& src : sources) {
                // An existing row at the same key was made by this build and
                // is newer; OR IGNORE keeps it.
                Stmt s = prepare(db_,
                    "INSERT OR IGNORE INTO main.records "
                    "(hyhash, chartmode, hyversion, sp_cap, bestpath, blob, score, actcount, "
                    " maxskip, hardest_ms, avgmult, notecount, sqin_count, sqout_count, "
                    " pathcount) "
                    "SELECT hyhash, chartmode, ?, ?, bestpath, blob, score, actcount, "
                    " maxskip, hardest_ms, avgmult, notecount, sqin_count, sqout_count, "
                    " pathcount FROM unc.records WHERE rowid=?");
                bind_text(s, 1, current_record_version());
                sqlite3_bind_int(s, 2, src.cap);
                sqlite3_bind_int64(s, 3, src.rowid);
                if (sqlite3_step(s) != SQLITE_DONE)
                    throw std::runtime_error(std::string("import record failed: ") +
                                             sqlite3_errmsg(db_));
                copied += sqlite3_changes(db_);
            }
        }
        meta_set("uncapped_import",
                 std::to_string(copied) + " records from " + uncapped_db_path);
        exec("COMMIT");
        in_txn = false;
    } catch (...) {
        if (in_txn) exec("ROLLBACK");
        exec("DETACH DATABASE unc");
        throw;
    }
    exec("DETACH DATABASE unc");
    return copied;
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

void RecordStore::add_record(const RecordKey& key, const HydraRecord& record) {
    add_row(prepare_row(key, record));
}

void RecordStore::add_row(const PreparedRow& row) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    Stmt s = prepare(db_,
        "INSERT OR REPLACE INTO records "
        "(hyhash, chartmode, hyversion, sp_cap, bestpath, blob, "
        " score, actcount, maxskip, hardest_ms, avgmult, notecount, sqin_count, "
        " sqout_count, pathcount) "
        "VALUES (?,?,?,?,?,?, ?,?,?,?,?,?,?,?,?)");
    bind_text(s, 1, row.hyhash);
    bind_text(s, 2, row.chartmode);
    bind_text(s, 3, row.hyversion);
    sqlite3_bind_int(s, 4, row.sp_cap);
    bind_text(s, 5, row.bestpath);
    bind_blob(s, 6, row.blob);
    bind_summary(s, 7, row.summary);
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string("add_row failed: ") + sqlite3_errmsg(db_));
}

SummaryLookup RecordStore::get_summary(const RecordKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string sql = "SELECT hyversion, bestpath FROM records WHERE hyhash=? AND chartmode=?";
    append_cap_lookup(sql, key.cap);
    Stmt s = prepare(db_, sql.c_str());
    bind_text(s, 1, key.hyhash);
    bind_text(s, 2, key.chartmode);
    bind_cap_lookup(s, 3, key.cap);
    if (sqlite3_step(s) != SQLITE_ROW) return SummaryLookup{};

    SummaryLookup out;
    if (column_text(s, 0) != current_record_version()) {
        out.status = RecordStatus::Stale;
        return out;
    }
    out.status = RecordStatus::Ready;
    out.bestpath = column_text(s, 1);
    return out;
}

RecordLookup RecordStore::get_record(const RecordKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string sql = "SELECT hyversion, blob FROM records WHERE hyhash=? AND chartmode=?";
    append_cap_lookup(sql, key.cap);
    Stmt s = prepare(db_, sql.c_str());
    bind_text(s, 1, key.hyhash);
    bind_text(s, 2, key.chartmode);
    bind_cap_lookup(s, 3, key.cap);
    if (sqlite3_step(s) != SQLITE_ROW) return RecordLookup{};

    RecordLookup out;
    out.hyversion = column_text(s, 0);
    if (out.hyversion != current_record_version()) {
        // Stamped by a different version: the blob is not decoded at all,
        // matching hydata.json_load's short-circuit on hyversion mismatch.
        // Callers see Stale and prompt a re-analyze.
        out.status = RecordStatus::Stale;
        return out;
    }

    out.status = RecordStatus::Ready;
    const std::vector<uint8_t> blob = column_blob(s, 1);
    // The tempomap is decoded once, here, and handed back with the record --
    // the display layer needs the same timing and must not query for it again.
    out.timing = get_timing(key.hyhash);
    out.record = out.timing ? read_record(blob, *out.timing) : read_record(blob);
    return out;
}

std::optional<SongTiming> RecordStore::get_timing(const std::string& hyhash) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_, "SELECT tempomap FROM songmeta WHERE hyhash=?");
    bind_text(s, 1, hyhash);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    return decode_tempomap(column_blob(s, 0));
}

bool RecordStore::has_record(const RecordKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string sql = "SELECT 1 FROM records WHERE hyhash=? AND chartmode=? AND hyversion=?";
    if (key.cap.exact) sql += " AND sp_cap=?";
    else sql += " AND sp_cap>" + std::to_string(kCloneHeroSpCap);
    Stmt s = prepare(db_, sql.c_str());
    bind_text(s, 1, key.hyhash);
    bind_text(s, 2, key.chartmode);
    bind_text(s, 3, current_record_version());
    if (key.cap.exact) sqlite3_bind_int(s, 4, *key.cap.exact);
    return sqlite3_step(s) == SQLITE_ROW;
}

void RecordStore::for_each_blob(
    const std::optional<std::string>& chartmode, const CapQuery& cap,
    const std::function<void(const BlobRow&, const HydraRecord*)>& fn) {
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
            "r.chartmode, r.hyversion, r.sp_cap, r.blob "
            "FROM records r JOIN songmeta s ON s.hyhash = r.hyhash WHERE 1=1";
        if (chartmode) sql += " AND r.chartmode = ?";
        append_cap_set_filter(sql, cap);
        // Python's iter_blobs has no ORDER BY and gets insertion order from
        // sqlite's table scan; say so explicitly here.
        sql += " ORDER BY r.rowid";

        Stmt s = prepare(db_, sql.c_str());
        int idx = 1;
        if (chartmode) bind_text(s, idx++, *chartmode);
        bind_cap_set_filter(s, idx, cap);
        const std::string current = current_record_version();
        while (sqlite3_step(s) == SQLITE_ROW) {
            Row row;
            row.meta.hyhash = column_text(s, 0);
            row.meta.ref_name = column_text(s, 1);
            row.meta.ref_artist = column_text(s, 2);
            row.meta.ref_charter = column_text(s, 3);
            row.meta.chartmode = column_text(s, 4);
            row.meta.hyversion = column_text(s, 5);
            row.meta.status = row.meta.hyversion == current ? RecordStatus::Ready
                                                            : RecordStatus::Stale;
            row.meta.sp_cap = sqlite3_column_int(s, 6);
            row.blob = column_blob(s, 7);
            rows.push_back(std::move(row));
        }
    }

    for (const Row& row : rows) {
        if (row.meta.status != RecordStatus::Ready) {
            fn(row.meta, nullptr);
            continue;
        }
        HydraRecord record = read_record(row.blob);
        fn(row.meta, &record);
    }
}

int RecordStore::drop_stale_records() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_, "DELETE FROM records WHERE hyversion != ?");
    bind_text(s, 1, current_record_version());
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string("drop_stale_records failed: ") +
                                 sqlite3_errmsg(db_));
    return sqlite3_changes(db_);
}

int RecordStore::reindex() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    struct Row {
        std::string hyhash, chartmode, hyversion;
        int sp_cap;
        std::vector<uint8_t> blob;
    };
    std::vector<Row> rows;
    {
        Stmt s = prepare(db_, "SELECT hyhash, chartmode, hyversion, sp_cap, blob FROM records");
        while (sqlite3_step(s) == SQLITE_ROW)
            rows.push_back({column_text(s, 0), column_text(s, 1), column_text(s, 2),
                            sqlite3_column_int(s, 3), column_blob(s, 4)});
    }

    std::string current = current_record_version();
    int done = 0;
    for (const Row& row : rows) {
        PathSummary summary;
        if (row.hyversion == current) summary = summarize_record(read_record(row.blob));

        Stmt s = prepare(db_,
            "UPDATE records SET score=?,actcount=?,maxskip=?,hardest_ms=?,avgmult=?,"
            "notecount=?,sqin_count=?,sqout_count=?,pathcount=? "
            "WHERE hyhash=? AND chartmode=? AND sp_cap=?");
        bind_summary(s, 1, summary);
        bind_text(s, 10, row.hyhash);
        bind_text(s, 11, row.chartmode);
        sqlite3_bind_int(s, 12, row.sp_cap);
        if (sqlite3_step(s) != SQLITE_DONE)
            throw std::runtime_error(std::string("reindex failed: ") + sqlite3_errmsg(db_));
        ++done;
    }
    return done;
}

std::vector<RecordListing> RecordStore::list_records(
    const std::optional<std::string>& chartmode, const CapQuery& cap, SortColumn order_by,
    bool descending, std::optional<int> limit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string sql =
        "SELECT s.hyhash, s.ref_name, s.ref_artist, s.ref_charter, r.chartmode, r.bestpath, "
        "r.score, r.actcount, r.maxskip, r.hardest_ms, r.avgmult, r.notecount, "
        "r.sqin_count, r.sqout_count, r.pathcount, r.sp_cap "
        "FROM records r JOIN songmeta s ON s.hyhash = r.hyhash WHERE 1=1";
    if (chartmode) sql += " AND r.chartmode = ?";
    append_cap_set_filter(sql, cap);

    const char* prefix = sort_column_is_songmeta(order_by) ? "s." : "r.";
    sql += " ORDER BY ";
    sql += prefix;
    sql += sort_column_name(order_by);
    sql += descending ? " DESC" : " ASC";
    if (limit) sql += " LIMIT " + std::to_string(*limit);

    Stmt s = prepare(db_, sql.c_str());
    int idx = 1;
    if (chartmode) bind_text(s, idx++, *chartmode);
    bind_cap_set_filter(s, idx, cap);

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
        rl.sp_cap = sqlite3_column_int(s, 15);
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
        "path TEXT, folder TEXT, sig TEXT)");

    exec("BEGIN");
    Stmt s = prepare(db_, "INSERT INTO charts VALUES (?,?,?,?,?,?,?)");
    for (const ChartLibraryEntry& item : items) {
        sqlite3_reset(s);
        bind_text(s, 1, item.md5);
        bind_text(s, 2, item.title);
        bind_text(s, 3, item.artist);
        bind_text(s, 4, item.charter);
        bind_text(s, 5, item.notespath);
        bind_text(s, 6, item.rootfolder);
        bind_text(s, 7, item.sig);
        if (sqlite3_step(s) != SQLITE_DONE) {
            exec("ROLLBACK");
            throw std::runtime_error(std::string("rebuild_chart_library failed: ") +
                                     sqlite3_errmsg(db_));
        }
    }
    exec("COMMIT");
}

ChartLibraryCache RecordStore::chart_library_cache() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    ChartLibraryCache cache;
    // A db written before the sig column existed has no usable fingerprints;
    // treat it as no cache rather than failing the scan.
    Stmt probe = prepare(db_, "SELECT COUNT(*) FROM pragma_table_info('charts') "
                              "WHERE name='sig'");
    if (sqlite3_step(probe) != SQLITE_ROW || sqlite3_column_int(probe, 0) == 0)
        return cache;

    Stmt s = prepare(db_, "SELECT path, sig, md5, name, artist, charter FROM charts");
    while (sqlite3_step(s) == SQLITE_ROW) {
        std::string sig = column_text(s, 1);
        if (sig.empty()) continue;
        cache[column_text(s, 0)] = {std::move(sig), column_text(s, 2), column_text(s, 3),
                                    column_text(s, 4), column_text(s, 5)};
    }
    return cache;
}

int64_t RecordStore::chart_library_count(const std::optional<std::string>& search) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string sql = "SELECT COUNT(*) FROM charts";
    if (search) sql += " WHERE name LIKE ? OR artist LIKE ? OR charter LIKE ?";
    Stmt s = prepare(db_, sql.c_str());
    if (search) {
        std::string param = "%" + *search + "%";
        bind_text(s, 1, param);
        bind_text(s, 2, param);
        bind_text(s, 3, param);
    }
    if (sqlite3_step(s) != SQLITE_ROW) return 0;
    return sqlite3_column_int64(s, 0);
}

std::vector<ChartLibraryEntry> RecordStore::list_chart_library(
    const std::optional<std::string>& search, int offset, int limit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string sql = "SELECT md5, name, artist, charter, path, folder FROM charts";
    if (search) sql += " WHERE name LIKE ? OR artist LIKE ? OR charter LIKE ?";
    sql += " ORDER BY name LIMIT ? OFFSET ?";

    Stmt s = prepare(db_, sql.c_str());
    int idx = 1;
    std::string param;
    if (search) {
        param = "%" + *search + "%";
        bind_text(s, idx++, param);
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
