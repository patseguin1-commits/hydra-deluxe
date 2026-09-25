#include "store/record_store.h"

#include <sqlite3.h>

#include <algorithm>
#include <map>
#include <stdexcept>

#include "core/version.h"
#include "store/serialize.h"

namespace hydra::store {

namespace {

// Stamps every stored row; a mismatch marks the row stale (see reindex /
// drop_stale_records). Single-sourced from CMake's project version.
constexpr const char* kHydraVersion = HYDRA_VERSION;

// RAII wrapper so every query site finalizes even on an early throw. Movable
// but not copyable: a copy would finalize the same handle twice. Moving lets a
// statement be parked in a std::optional and destroyed on purpose later --
// for_each_blob does that, so its two per-row statements live across the whole
// walk instead of being compiled again for every row.
struct Stmt {
    sqlite3_stmt* p = nullptr;
    Stmt() = default;
    Stmt(Stmt&& other) noexcept : p(other.p) { other.p = nullptr; }
    Stmt& operator=(Stmt&& other) noexcept {
        if (this != &other) {
            if (p) sqlite3_finalize(p);
            p = other.p;
            other.p = nullptr;
        }
        return *this;
    }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    ~Stmt() {
        if (p) sqlite3_finalize(p);
    }
    operator sqlite3_stmt*() const { return p; }
};

// Clears a reused statement's cursor so it can be bound and stepped again.
// Bindings survive a reset and are overwritten by the next bind, so
// sqlite3_clear_bindings is not needed. Every reuse site calls this before its
// caller drops the store lock: a half-stepped statement holds a read cursor
// open on the table, and the locking rule is that no sqlite state outlives the
// locked block.
struct ResetOnExit {
    sqlite3_stmt* s;
    ~ResetOnExit() {
        if (s) sqlite3_reset(s);
    }
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

// The results table's columns, single-sourced so the create, the v1->v2
// migration and the legacy import all agree on the layout. result_id is the
// rowid alias: a bigger one means "written later", which is how Auto picks
// the newest run.
constexpr const char* kResultsColumnDefs =
    "  result_id   INTEGER PRIMARY KEY,"
    "  hyhash      TEXT NOT NULL,"
    "  chartmode   TEXT NOT NULL,"
    "  hyversion   TEXT NOT NULL,"
    "  sp_cap      INTEGER NOT NULL,"
    "  ms_enabled  INTEGER NOT NULL,"
    "  ms_value    INTEGER NOT NULL,"
    "  depth_mode  INTEGER NOT NULL,"
    "  depth_value INTEGER NOT NULL,"
    "  bestpath    TEXT NOT NULL,"
    "  structure   BLOB NOT NULL,"
    "  score       INTEGER,"
    "  actcount    INTEGER,"
    "  maxskip     INTEGER,"
    "  hardest_ms  REAL,"
    "  avgmult     REAL,"
    "  notecount   INTEGER,"
    "  sqin_count  INTEGER,"
    "  sqout_count INTEGER,"
    "  pathcount   INTEGER,"
    "  UNIQUE (hyhash, chartmode, sp_cap, ms_enabled, ms_value, depth_mode, depth_value)";

// The summary columns, in the order bind_summary/read_summary use.
constexpr const char* kSummaryColumnList =
    "score, actcount, maxskip, hardest_ms, avgmult, notecount, sqin_count, "
    "sqout_count, pathcount";

// The pre-1.6-era records table's columns, single-sourced so the cap-key
// migration's rebuild agrees with the shape it reads.
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

// ---- lens / cap filters ---------------------------------------------------
//
// Every lookup answers one question: "which row ran under these settings?".
// The candidate set is always the wanted lens plus the sentinel rows -- a
// migrated row is a placeholder that answers any question badly, so it is
// offered only when nothing better exists, and a real row always outranks it.

// "the row at alias `a` carries exactly this lens". Four bound parameters, in
// Lens's field order. `a` is "", "r." or "x.".
std::string lens_match(const char* a) {
    std::string p = a;
    return "(" + p + "ms_enabled=? AND " + p + "ms_value=? AND " + p +
           "depth_mode=? AND " + p + "depth_value=?)";
}
std::string lens_or_sentinel(const char* a) {
    return "(" + lens_match(a) + " OR " + std::string(a) + "ms_enabled=-1)";
}
int bind_lens(sqlite3_stmt* s, int idx, const Lens& lens) {
    sqlite3_bind_int(s, idx, lens.ms_enabled);
    sqlite3_bind_int(s, idx + 1, lens.ms_value);
    sqlite3_bind_int(s, idx + 2, lens.depth_mode);
    sqlite3_bind_int(s, idx + 3, lens.depth_value);
    return idx + 4;
}

// The first 12 bytes a structure blob starts with: the u32 structure format,
// then the u64 rules fingerprint (path_codec.cpp flatten_record).
constexpr int kStructureHeadBytes = 12;

std::vector<uint8_t> structure_head_for(uint64_t rules_fingerprint) {
    std::vector<uint8_t> b(kStructureHeadBytes);
    for (int i = 0; i < 4; ++i)
        b[static_cast<size_t>(i)] =
            static_cast<uint8_t>(kPathStructureFormatVersion >> (8 * i));
    for (int i = 0; i < 8; ++i)
        b[static_cast<size_t>(4 + i)] = static_cast<uint8_t>(rules_fingerprint >> (8 * i));
    return b;
}

// Is this row's stored path tree in the layout this build reads, analyzed
// under the rules this process runs? Takes the whole blob or just the
// substr(structure,1,12) a query selected.
bool structure_is_current(const std::vector<uint8_t>& structure_head,
                          uint64_t rules_fingerprint) {
    if (structure_head.size() < kStructureHeadBytes) return false;
    const std::vector<uint8_t> want = structure_head_for(rules_fingerprint);
    return std::equal(want.begin(), want.end(), structure_head.begin());
}

// The four facts that decide how a row places among the candidates for one
// key. The first three also decide whether the row is readable at all, which
// is why they are read off the row exactly here.
struct Candidate {
    bool real = false;     // not a sentinel: it says which settings it ran under
    bool current = false;  // stamped by this build
    bool format = false;   // its paths are in this build's path-structure format,
                           // analyzed under the rules this process runs
    int64_t result_id = 0;
};

Candidate rank_row(const std::string& hyversion, int ms_enabled,
                   const std::vector<uint8_t>& structure_head, int64_t result_id,
                   uint64_t rules_fingerprint) {
    return Candidate{ms_enabled != -1, hyversion == current_record_version(),
                     structure_is_current(structure_head, rules_fingerprint), result_id};
}

// Is this row's content trustworthy? Only when this build wrote it, it says
// which settings it ran under, and its paths are stored in the layout this
// build reads. Everything else is Stale: another version's bytes, a migrated
// row whose question nobody recorded, or a path tree from an older layout
// whose activations this build would read back half-empty. Every path that
// classifies a row asks here -- the SQL sites below are the exception, and
// spell the same rule out for the database.
bool row_is_ready(const std::string& hyversion, int ms_enabled,
                  const std::vector<uint8_t>& structure_head, uint64_t rules_fingerprint) {
    const Candidate c = rank_row(hyversion, ms_enabled, structure_head, 0, rules_fingerprint);
    return c.real && c.current && c.format;
}

// Why a row that is not Ready is Stale, for callers that explain it
// (hydra_replay dump). `build`: another Hydra build, an older path layout, or
// a migrated row. `rules`: this layout, analyzed under other rules. An older
// layout has no fingerprint to compare, so it is only ever `build`.
struct StaleReasons {
    bool build = false;
    bool rules = false;
};

StaleReasons stale_reasons(const std::string& hyversion, int ms_enabled,
                           const std::vector<uint8_t>& structure_head,
                           uint64_t rules_fingerprint) {
    const std::vector<uint8_t> want = structure_head_for(rules_fingerprint);
    const bool layout_current =
        structure_head.size() >= kStructureHeadBytes &&
        std::equal(want.begin(), want.begin() + 4, structure_head.begin());
    StaleReasons why;
    why.build = hyversion != current_record_version() || ms_enabled == -1 || !layout_current;
    why.rules = layout_current &&
                !std::equal(want.begin() + 4, want.end(), structure_head.begin() + 4);
    return why;
}

// row_is_ready spelled in SQL, positive and negated. Some sites have to pick
// their rows in the database rather than in C++: a DELETE cannot call back
// into row_is_ready, and has_record only wants to know whether a readable row
// exists. This is one rule spelled twice; the C++ half above and these two
// must be changed together.
//
// Both take two bound parameters, in this order: the current version text and
// the current structure format and rules fingerprint, 12 bytes.
// bind_ready_params binds them and returns the next free index.
constexpr const char* kRowReadySql =
    "(hyversion = ? AND ms_enabled != -1 AND substr(structure,1,12) = ?)";
constexpr const char* kRowNotReadySql =
    "(hyversion != ? OR ms_enabled = -1 OR substr(structure,1,12) != ?)";

int bind_ready_params(sqlite3_stmt* s, int idx, uint64_t rules_fingerprint) {
    bind_text(s, idx, current_record_version());
    bind_blob(s, idx + 1, structure_head_for(rules_fingerprint));
    return idx + 2;
}

// Does `a` beat `b`? A real row before a sentinel, then this version before
// another, then this path format before an older one, then the newest write.
// Newest, not tallest: an Auto run that settles below an older, taller row
// (an imported uncapped result, a what-if the user typed) is the result the
// user just asked for, so every lookup must show it. The tallest rule showed
// the old row forever and "Analyze paths!" could never replace it. Write
// order is result_id: add_row deletes and re-inserts, so a rewritten row is
// newest.
bool outranks(const Candidate& a, const Candidate& b) {
    if (a.real != b.real) return a.real;
    if (a.current != b.current) return a.current;
    if (a.format != b.format) return a.format;
    return a.result_id > b.result_id;
}

// Which chart a set query's winner is picked for: one per chart and mode.
using GroupKey = std::pair<std::string, std::string>;

// Every row that could answer a lookup: the wanted lens or a sentinel, at the
// wanted cap. Which of them wins is `outranks`'s decision and not SQL's, so
// there is deliberately no ORDER BY or LIMIT here. `a` is the table alias,
// "" or "r.". Appended after a WHERE that already has a term.
void append_candidate_filter(std::string& sql, const char* a, const CapQuery& cap) {
    const std::string p = a;
    sql += " AND " + lens_or_sentinel(a);
    if (cap.exact) sql += " AND " + p + "sp_cap=?";
    else sql += " AND " + p + "sp_cap>" + std::to_string(kCloneHeroSpCap);
}
// Binds the lens's four parameters, plus one more for an exact cap.
int bind_candidate_filter(sqlite3_stmt* s, int idx, const CapQuery& cap, const Lens& lens) {
    idx = bind_lens(s, idx, lens);
    if (cap.exact) sqlite3_bind_int(s, idx++, *cap.exact);
    return idx;
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
    // Both sides come from the same int (Settings::mslimit_value, widened to
    // the engine's double), so an exact comparison is the right one.
    if (key.lens.ms_enabled == 1 &&
        (!record.ms_limit || *record.ms_limit != static_cast<double>(key.lens.ms_value)))
        throw std::invalid_argument(
            "prepare_row: key asks for an ms limit of " +
            std::to_string(key.lens.ms_value) + " but the record was analyzed " +
            (record.ms_limit ? "at " + std::to_string(*record.ms_limit) : "without one"));

    PreparedRow row;
    row.hyhash = key.hyhash;
    row.chartmode = key.chartmode;
    row.hyversion = current_record_version();
    row.sp_cap = *record.sp_cap;
    row.lens = key.lens;
    row.bestpath = record.paths.empty() ? std::string() : record.best_path().pathstring();
    row.summary = summarize_record(record);

    FlatRecord flat = flatten_record(record);
    row.structure = std::move(flat.structure);
    row.nodes = std::move(flat.nodes);
    return row;
}

// ---- RecordStore ------------------------------------------------------

RecordStore::RecordStore(const std::string& dbpath, uint64_t rules_fingerprint)
    : rules_fingerprint_(rules_fingerprint) {
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
        ");"
        "CREATE TABLE IF NOT EXISTS dynamics ("
        "  md5           TEXT NOT NULL,"
        "  difficulty    TEXT NOT NULL,"
        "  pro           INTEGER NOT NULL,"
        "  blob          BLOB NOT NULL,"
        "  count_version INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (md5, difficulty, pro)"
        ");");
    // A dynamics table from before the stamp gets the column. Its rows read
    // 0, which matches no real stamp, so each is recounted once.
    if (!has_column("dynamics", "count_version"))
        exec("ALTER TABLE dynamics ADD COLUMN count_version INTEGER NOT NULL DEFAULT 0");
    // A pre-1.6 or 1.6 file still has the old single-blob `records` table.
    // Bring it to the v1 shape (keyed by cap) first, then fold it into the
    // three v2 tables. A fresh db skips both and is born at v2.
    const bool had_records = has_table("records");
    if (had_records) {
        add_missing_columns();
        if (!has_column("records", "sp_cap")) migrate_records_to_cap_key();
    }
    create_result_tables();
    if (had_records) migrate_records_to_results();
    // Schema 2 = results keyed by the full settings, with shared paths.
    exec("PRAGMA user_version = 2");
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

bool RecordStore::has_table(const char* table) {
    Stmt s = prepare(db_,
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?");
    bind_text(s, 1, table);
    return sqlite3_step(s) == SQLITE_ROW && sqlite3_column_int(s, 0) > 0;
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

std::optional<std::string> RecordStore::engine_mode() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return meta_get("engine_mode");
}

void RecordStore::set_engine_mode(const std::string& mode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    meta_set("engine_mode", mode);
}

void RecordStore::put_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob,
                               int count_version) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_,
        "INSERT OR REPLACE INTO dynamics (md5, difficulty, pro, blob, count_version)"
        " VALUES (?,?,?,?,?)");
    bind_text(s, 1, key.md5);
    bind_text(s, 2, key.difficulty);
    sqlite3_bind_int(s, 3, key.pro ? 1 : 0);
    bind_blob(s, 4, blob);
    sqlite3_bind_int(s, 5, count_version);
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string("put_dynamics failed: ") + sqlite3_errmsg(db_));
}

std::optional<std::vector<uint8_t>> RecordStore::get_dynamics(const DynamicsKey& key,
                                                               int count_version) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // A row with another stamp reads as missing, so the caller recounts it
    // and put_dynamics restamps it.
    Stmt s = prepare(db_,
        "SELECT blob FROM dynamics WHERE md5=? AND difficulty=? AND pro=? AND count_version=?");
    bind_text(s, 1, key.md5);
    bind_text(s, 2, key.difficulty);
    sqlite3_bind_int(s, 3, key.pro ? 1 : 0);
    sqlite3_bind_int(s, 4, count_version);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    return column_blob(s, 0);
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

void RecordStore::create_result_tables() {
    exec((std::string("CREATE TABLE IF NOT EXISTS results (") + kResultsColumnDefs + ");")
             .c_str());
    // A node belongs to one chart+mode: the same bytes under a different chart
    // are a different path, and scoping the table this way keeps the garbage
    // collection after a write to the rows that write could have orphaned.
    exec("CREATE TABLE IF NOT EXISTS paths ("
         "  hyhash    TEXT NOT NULL,"
         "  chartmode TEXT NOT NULL,"
         "  phash     TEXT NOT NULL,"
         "  payload   BLOB NOT NULL,"
         "  PRIMARY KEY (hyhash, chartmode, phash)"
         ");"
         // hyhash/chartmode are denormalized here on purpose: collecting a
         // chart's orphaned nodes must never have to decode a structure blob.
         "CREATE TABLE IF NOT EXISTS path_refs ("
         "  result_id INTEGER NOT NULL,"
         "  hyhash    TEXT NOT NULL,"
         "  chartmode TEXT NOT NULL,"
         "  phash     TEXT NOT NULL,"
         "  PRIMARY KEY (result_id, phash)"
         ");"
         "CREATE INDEX IF NOT EXISTS path_refs_by_node"
         "  ON path_refs (hyhash, chartmode, phash);");
}

void RecordStore::migrate_records_to_results() {
    // Every v1 row becomes a results row with the sentinel lens: the result is
    // real, but the file never recorded which ms limit or score range produced
    // it, so it can only be offered as "something was here" and must never be
    // decoded. Its old single-blob payload rides along in `structure` as
    // ballast -- kept so nothing is silently thrown away, never read.
    exec("BEGIN");
    try {
        exec((std::string("INSERT INTO results"
                          " (hyhash, chartmode, hyversion, sp_cap, ms_enabled, ms_value,"
                          "  depth_mode, depth_value, bestpath, structure, ") +
              kSummaryColumnList +
              ") SELECT hyhash, chartmode, hyversion, sp_cap, -1, 0, 0, 0, bestpath, blob, " +
              kSummaryColumnList + " FROM records ORDER BY rowid")
                 .c_str());
        exec("DROP TABLE records");
        exec("PRAGMA user_version = 2");
        exec("COMMIT");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}

// The SQL for the two per-row reads a walk repeats. Named here so
// for_each_blob can compile each of them once and reuse it, and the one-off
// callers still get the same text.
constexpr const char* kLoadNodesSql =
    "SELECT p.phash, p.payload FROM path_refs pr JOIN paths p"
    "  ON p.hyhash = pr.hyhash AND p.chartmode = pr.chartmode AND p.phash = pr.phash"
    " WHERE pr.result_id = ?";
constexpr const char* kReloadRowSql =
    "SELECT hyhash, chartmode, hyversion, sp_cap, structure"
    " FROM results WHERE result_id = ?";

std::unordered_map<std::string, std::vector<uint8_t>> RecordStore::load_nodes(
    sqlite3_stmt* stmt, int64_t result_id) {
    // `stmt` is kLoadNodesSql, compiled by the caller. Reset before returning,
    // so the caller may drop the lock the moment this comes back.
    ResetOnExit reset{stmt};
    std::unordered_map<std::string, std::vector<uint8_t>> nodes;
    sqlite3_bind_int64(stmt, 1, result_id);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        nodes.emplace(column_text(stmt, 0), column_blob(stmt, 1));
    return nodes;
}

std::unordered_map<std::string, std::vector<uint8_t>> RecordStore::load_nodes(
    int64_t result_id) {
    Stmt s = prepare(db_, kLoadNodesSql);
    return load_nodes(s, result_id);
}

bool RecordStore::reload_row(sqlite3_stmt* stmt, const BlobRow& meta, int64_t result_id,
                             std::vector<uint8_t>& structure) {
    // `stmt` is kReloadRowSql, compiled once by for_each_blob. Every path out
    // of here resets it first, including the skip paths below, so nothing is
    // left mid-step when the caller unlocks.
    ResetOnExit reset{stmt};
    sqlite3_bind_int64(stmt, 1, result_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) return false;  // deleted since the walk listed it
    // Every identity column, not just "a row is here". result_id is a plain
    // INTEGER PRIMARY KEY, so sqlite hands the same id out again after a
    // delete and a replacement row can occupy it.
    if (column_text(stmt, 0) != meta.hyhash) return false;
    if (column_text(stmt, 1) != meta.chartmode) return false;
    if (column_text(stmt, 2) != meta.hyversion) return false;
    if (sqlite3_column_int(stmt, 3) != meta.sp_cap) return false;
    structure = column_blob(stmt, 4);
    return true;
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
                // A sentinel-lens row, like the v1->v2 migration writes: the
                // old file records the cap and nothing else, so the settings
                // behind the result are unknown and it reads Stale. Its
                // hyversion is kept as the old file stamped it -- restamping
                // it as current would claim a provenance it doesn't have.
                //
                // An existing row at the same key was made by this build and
                // is newer; OR IGNORE keeps it.
                Stmt s = prepare(db_,
                    (std::string(
                        "INSERT OR IGNORE INTO main.results "
                        "(hyhash, chartmode, hyversion, sp_cap, ms_enabled, ms_value, "
                        " depth_mode, depth_value, bestpath, structure, ") +
                     kSummaryColumnList +
                     ") SELECT u.hyhash, u.chartmode, u.hyversion, ?, -1, 0, 0, 0,"
                     " u.bestpath, u.blob, u.score, u.actcount, u.maxskip, u.hardest_ms,"
                     " u.avgmult, u.notecount, u.sqin_count, u.sqout_count, u.pathcount"
                     " FROM unc.records u WHERE u.rowid=?"
                     "   AND NOT EXISTS (SELECT 1 FROM main.results m"
                     "     WHERE m.hyhash = u.hyhash AND m.chartmode = u.chartmode"
                     "       AND m.sp_cap = ?)")
                        .c_str());
                sqlite3_bind_int(s, 1, src.cap);
                sqlite3_bind_int64(s, 2, src.rowid);
                sqlite3_bind_int(s, 3, src.cap);
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
    // column set by adding the columns it lacks.
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

    // Deleting a result means deleting its refs first, always: the refs are
    // what keep its paths alive, and the final sweep collects whatever they
    // stopped pointing at. Every step below runs in one transaction, so a
    // failure anywhere leaves the store exactly as it was.
    auto run = [&](Stmt& s, const char* what) {
        if (sqlite3_step(s) != SQLITE_DONE)
            throw std::runtime_error(std::string("add_row ") + what + " failed: " +
                                     sqlite3_errmsg(db_));
    };
    // Deletes the results a subquery names, and their refs. `where` is a
    // fragment over `results`, bound by `bind`.
    auto purge = [&](const std::string& where,
                     const std::function<void(sqlite3_stmt*)>& bind, const char* what) {
        std::string refs = "DELETE FROM path_refs WHERE result_id IN"
                           " (SELECT result_id FROM results WHERE " + where + ")";
        Stmt r = prepare(db_, refs.c_str());
        bind(r);
        run(r, what);

        std::string rows = "DELETE FROM results WHERE " + where;
        Stmt d = prepare(db_, rows.c_str());
        bind(d);
        run(d, what);
    };

    exec("BEGIN");
    try {
        // (1) Anything this chart+mode holds that this build cannot read --
        //     another Hydra version's stamp, a sentinel, an older path
        //     layout, a result analyzed under other rules -- is superseded by
        //     a write here. The test is against
        //     what is current, not against this row: a test writing a
        //     deliberately old-stamped row must not take the real rows with
        //     it, and this runs before the insert so the new row is untouched.
        purge("hyhash=? AND chartmode=? AND " + std::string(kRowNotReadySql),
              [&](sqlite3_stmt* s) {
                  bind_text(s, 1, row.hyhash);
                  bind_text(s, 2, row.chartmode);
                  bind_ready_params(s, 3, rules_fingerprint_);
              },
              "unreadable purge");

        // (2) A sentinel at this cap was a placeholder for "some result ran
        //     here"; a real run at that cap is the answer it stood in for.
        purge("hyhash=? AND chartmode=? AND sp_cap=? AND ms_enabled=-1",
              [&](sqlite3_stmt* s) {
                  bind_text(s, 1, row.hyhash);
                  bind_text(s, 2, row.chartmode);
                  sqlite3_bind_int(s, 3, row.sp_cap);
              },
              "sentinel purge");

        // (3) The row this one replaces, deleted explicitly rather than by
        //     INSERT OR REPLACE: the refs bookkeeping has to be ours, and the
        //     re-insert must take a fresh result_id so Auto sees it as newest.
        purge("hyhash=? AND chartmode=? AND sp_cap=? AND ms_enabled=? AND ms_value=?"
              " AND depth_mode=? AND depth_value=?",
              [&](sqlite3_stmt* s) {
                  bind_text(s, 1, row.hyhash);
                  bind_text(s, 2, row.chartmode);
                  sqlite3_bind_int(s, 3, row.sp_cap);
                  bind_lens(s, 4, row.lens);
              },
              "replace purge");

        // (4) The result, then its paths (shared, so first writer wins) and
        //     the refs that tie the two together.
        {
            Stmt s = prepare(db_,
                (std::string("INSERT INTO results "
                             "(hyhash, chartmode, hyversion, sp_cap, ms_enabled, ms_value,"
                             " depth_mode, depth_value, bestpath, structure, ") +
                 kSummaryColumnList + ") VALUES (?,?,?,?,?,?,?,?,?,?, ?,?,?,?,?,?,?,?,?)")
                    .c_str());
            bind_text(s, 1, row.hyhash);
            bind_text(s, 2, row.chartmode);
            bind_text(s, 3, row.hyversion);
            sqlite3_bind_int(s, 4, row.sp_cap);
            bind_lens(s, 5, row.lens);
            bind_text(s, 9, row.bestpath);
            bind_blob(s, 10, row.structure);
            bind_summary(s, 11, row.summary);
            run(s, "insert");
        }
        const int64_t result_id = sqlite3_last_insert_rowid(db_);

        for (const StoredPathNode& node : row.nodes) {
            {
                Stmt s = prepare(db_,
                    "INSERT OR IGNORE INTO paths (hyhash, chartmode, phash, payload)"
                    " VALUES (?,?,?,?)");
                bind_text(s, 1, row.hyhash);
                bind_text(s, 2, row.chartmode);
                bind_text(s, 3, node.hash);
                bind_blob(s, 4, node.payload);
                run(s, "path insert");
            }
            Stmt s = prepare(db_,
                "INSERT OR IGNORE INTO path_refs (result_id, hyhash, chartmode, phash)"
                " VALUES (?,?,?,?)");
            sqlite3_bind_int64(s, 1, result_id);
            bind_text(s, 2, row.hyhash);
            bind_text(s, 3, row.chartmode);
            bind_text(s, 4, node.hash);
            run(s, "path ref insert");
        }

        // (5) Whatever the replaced row was the last owner of.
        {
            Stmt s = prepare(db_,
                "DELETE FROM paths WHERE hyhash=? AND chartmode=? AND phash NOT IN"
                " (SELECT phash FROM path_refs WHERE hyhash=? AND chartmode=?)");
            bind_text(s, 1, row.hyhash);
            bind_text(s, 2, row.chartmode);
            bind_text(s, 3, row.hyhash);
            bind_text(s, 4, row.chartmode);
            run(s, "path gc");
        }

        exec("COMMIT");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}

SummaryLookup RecordStore::get_summary(const RecordKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string sql =
        "SELECT hyversion, ms_enabled, bestpath, result_id, substr(structure,1,12)"
        " FROM results WHERE hyhash=? AND chartmode=?";
    append_candidate_filter(sql, "", key.cap);
    Stmt s = prepare(db_, sql.c_str());
    bind_text(s, 1, key.hyhash);
    bind_text(s, 2, key.chartmode);
    bind_candidate_filter(s, 3, key.cap, key.lens);

    struct Winner {
        Candidate rank;
        std::string hyversion;
        int ms_enabled = 0;
        std::vector<uint8_t> structure_head;
        std::string bestpath;
    };
    std::optional<Winner> best;
    while (sqlite3_step(s) == SQLITE_ROW) {
        Winner w;
        w.hyversion = column_text(s, 0);
        w.ms_enabled = sqlite3_column_int(s, 1);
        w.bestpath = column_text(s, 2);
        w.structure_head = column_blob(s, 4);
        w.rank = rank_row(w.hyversion, w.ms_enabled, w.structure_head,
                          sqlite3_column_int64(s, 3), rules_fingerprint_);
        if (!best || outranks(w.rank, best->rank)) best = std::move(w);
    }
    if (!best) return SummaryLookup{};

    SummaryLookup out;
    // A stale winner is reported as Stale, not hidden: the library's status
    // column has to tell "analyzed by another build" apart from "never
    // analyzed", and only a lookup can say which this is.
    if (!row_is_ready(best->hyversion, best->ms_enabled, best->structure_head,
                      rules_fingerprint_)) {
        out.status = RecordStatus::Stale;
        return out;
    }
    out.status = RecordStatus::Ready;
    out.bestpath = best->bestpath;
    return out;
}

RecordLookup RecordStore::get_record(const RecordKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    int64_t result_id = 0;
    std::vector<uint8_t> structure;
    RecordLookup out;
    {
        std::string sql =
            "SELECT result_id, hyversion, ms_enabled, structure FROM results"
            " WHERE hyhash=? AND chartmode=?";
        append_candidate_filter(sql, "", key.cap);
        Stmt s = prepare(db_, sql.c_str());
        bind_text(s, 1, key.hyhash);
        bind_text(s, 2, key.chartmode);
        bind_candidate_filter(s, 3, key.cap, key.lens);

        struct Winner {
            Candidate rank;
            int64_t result_id = 0;
            std::string hyversion;
            int ms_enabled = 0;
            std::vector<uint8_t> structure;
        };
        std::optional<Winner> best;
        while (sqlite3_step(s) == SQLITE_ROW) {
            Winner w;
            w.result_id = sqlite3_column_int64(s, 0);
            w.hyversion = column_text(s, 1);
            w.ms_enabled = sqlite3_column_int(s, 2);
            w.structure = column_blob(s, 3);
            // The whole blob is here, so its leading twelve bytes are the
            // structure head the format and rules check wants.
            w.rank = rank_row(w.hyversion, w.ms_enabled, w.structure, w.result_id,
                              rules_fingerprint_);
            if (!best || outranks(w.rank, best->rank)) best = std::move(w);
        }
        if (!best) return RecordLookup{};

        out.hyversion = best->hyversion;
        // Stamped by a different version, migrated in with unknown settings,
        // or holding a path tree in an older layout: nothing stored is
        // decoded at all. Callers see Stale and prompt a re-analyze.
        if (!row_is_ready(best->hyversion, best->ms_enabled, best->structure,
                          rules_fingerprint_)) {
            out.status = RecordStatus::Stale;
            const StaleReasons why = stale_reasons(best->hyversion, best->ms_enabled,
                                                   best->structure, rules_fingerprint_);
            out.stale_build = why.build;
            out.stale_rules = why.rules;
            return out;
        }
        result_id = best->result_id;
        structure = std::move(best->structure);
    }

    out.status = RecordStatus::Ready;
    const std::unordered_map<std::string, std::vector<uint8_t>> nodes = load_nodes(result_id);
    HydraRecord record = rebuild_record(
        structure, [&nodes](const std::string& hash) -> const std::vector<uint8_t>* {
            auto it = nodes.find(hash);
            return it == nodes.end() ? nullptr : &it->second;
        });
    // The tempomap is decoded once, here, and handed back with the record --
    // the display layer needs the same timing and must not query for it again.
    out.timing = get_timing(key.hyhash);
    if (out.timing) restore_timecodes(record, *out.timing);
    out.record = std::move(record);
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
    // The exact lens, never a sentinel: "already analyzed" has to mean "under
    // these settings", or a batch run skips charts whose stored answer came
    // from a different question.
    std::string sql = "SELECT 1 FROM results WHERE hyhash=? AND chartmode=? AND " +
                      std::string(kRowReadySql) + " AND " + lens_match("");
    if (key.cap.exact) sql += " AND sp_cap=?";
    else sql += " AND sp_cap>" + std::to_string(kCloneHeroSpCap);
    Stmt s = prepare(db_, sql.c_str());
    bind_text(s, 1, key.hyhash);
    bind_text(s, 2, key.chartmode);
    int idx = bind_ready_params(s, 3, rules_fingerprint_);
    idx = bind_lens(s, idx, key.lens);
    if (key.cap.exact) sqlite3_bind_int(s, idx, *key.cap.exact);
    return sqlite3_step(s) == SQLITE_ROW;
}

void RecordStore::for_each_blob(
    const std::optional<std::string>& chartmode, const CapQuery& cap, const Lens& lens,
    const std::function<void(const BlobRow&, const HydraRecord*)>& fn,
    const std::atomic<bool>* cancel) {
    // Two passes, and the lock is short in both. The first lists which rows to
    // visit, and their blobs, under one lock. The second takes the lock once
    // per row, just long enough to read that row's nodes -- and to re-read the
    // row itself, but only when something wrote in between -- then decodes and
    // calls fn with nothing held. A walk of the whole library used to hold the
    // lock end to end, so a click on the UI thread waited for the whole report.
    //
    // The two per-row reads share one statement each, compiled before the loop
    // and reset before every unlock, instead of being compiled per row --
    // 37,000 compilations on an 18.5k-record library, which cost more than the
    // shorter lock saved.
    struct Row {
        BlobRow meta;
        int64_t result_id = 0;
        std::vector<uint8_t> structure;
    };
    std::vector<Row> rows;
    // How many rows this connection had written when the listing below ran.
    // Phase 2 compares against it to decide whether the listing is still exact
    // -- see the comment at the per-row read.
    int64_t snapshot_changes = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        // The whole structure blob, not just its head. It is small in total
        // (a few megabytes across a big library) and reading it here means the
        // common walk -- nothing writing -- never re-reads a row.
        std::string sql =
            "SELECT s.hyhash, s.ref_name, s.ref_artist, s.ref_charter, "
            "r.chartmode, r.hyversion, r.sp_cap, r.result_id, r.structure, "
            "r.ms_enabled "
            "FROM results r JOIN songmeta s ON s.hyhash = r.hyhash WHERE 1=1";
        if (chartmode) sql += " AND r.chartmode = ?";
        append_candidate_filter(sql, "r.", cap);
        // Python's iter_blobs has no ORDER BY and gets insertion order from
        // sqlite's table scan; say so explicitly here.
        sql += " ORDER BY r.result_id";

        Stmt s = prepare(db_, sql.c_str());
        int idx = 1;
        if (chartmode) bind_text(s, idx++, *chartmode);
        bind_candidate_filter(s, idx, cap, lens);

        std::vector<Row> candidates;
        std::vector<Candidate> ranks;
        std::map<GroupKey, size_t> winner;  // chart+mode -> index of its best row
        while (sqlite3_step(s) == SQLITE_ROW) {
            Row row;
            row.meta.hyhash = column_text(s, 0);
            row.meta.ref_name = column_text(s, 1);
            row.meta.ref_artist = column_text(s, 2);
            row.meta.ref_charter = column_text(s, 3);
            row.meta.chartmode = column_text(s, 4);
            row.meta.hyversion = column_text(s, 5);
            row.meta.sp_cap = sqlite3_column_int(s, 6);
            row.result_id = sqlite3_column_int64(s, 7);
            row.structure = column_blob(s, 8);
            const int ms_enabled = sqlite3_column_int(s, 9);
            // Both helpers read only the blob's leading twelve bytes, and take
            // the whole blob or just that head -- see structure_is_current.
            row.meta.status =
                row_is_ready(row.meta.hyversion, ms_enabled, row.structure, rules_fingerprint_)
                    ? RecordStatus::Ready
                    : RecordStatus::Stale;

            const Candidate rank = rank_row(row.meta.hyversion, ms_enabled, row.structure,
                                            row.result_id, rules_fingerprint_);
            const GroupKey key{row.meta.hyhash, row.meta.chartmode};
            auto it = winner.find(key);
            if (it == winner.end()) winner.emplace(key, candidates.size());
            else if (outranks(rank, ranks[it->second])) it->second = candidates.size();
            ranks.push_back(rank);
            candidates.push_back(std::move(row));
        }

        // One row per chart and mode, the same one a lookup would pick, kept
        // in result_id order. A stale winner is still yielded: this is the
        // export path, and dropping a row here would lose it for good.
        std::vector<bool> keep(candidates.size(), false);
        for (const auto& kv : winner) keep[kv.second] = true;
        for (size_t i = 0; i < candidates.size(); ++i)
            if (keep[i]) rows.push_back(std::move(candidates[i]));

        // Read under the same lock as the listing, so it names exactly the
        // database state the rows above came from.
        snapshot_changes = sqlite3_total_changes64(db_);
    }

    // Both statements are sqlite objects, so they are compiled, used, reset and
    // destroyed with the lock held. The guard is what makes the destroy happen
    // on every way out of this function -- the cancel return below, and a throw
    // out of rebuild_record or fn -- since a Stmt destroyed on a plain unwind
    // would finalize with no lock held.
    std::optional<Stmt> reload_stmt;
    std::optional<Stmt> nodes_stmt;
    struct StmtGuard {
        std::recursive_mutex& mutex;
        std::optional<Stmt>& reload_stmt;
        std::optional<Stmt>& nodes_stmt;
        ~StmtGuard() {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            reload_stmt.reset();  // optional::reset -- destroys, so finalizes
            nodes_stmt.reset();
        }
    } stmt_guard{mutex_, reload_stmt, nodes_stmt};
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        reload_stmt = prepare(db_, kReloadRowSql);
        nodes_stmt = prepare(db_, kLoadNodesSql);
    }

    for (Row& row : rows) {
        // Between records, with nothing held: the caller (app shutdown) gets
        // its thread back within one record instead of one library.
        if (cancel && cancel->load()) return;

        if (row.meta.status != RecordStatus::Ready) {
            fn(row.meta, nullptr);  // no lock: a stale row has nothing to read
            continue;
        }

        std::vector<uint8_t> structure;
        std::unordered_map<std::string, std::vector<uint8_t>> nodes;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            // Has anything been written on this connection since the listing?
            // sqlite3_total_changes64 counts rows this connection changed with
            // INSERT, UPDATE or DELETE, ever, and only goes up. (An INSERT OR
            // IGNORE that ignores counts nothing, which is exactly right here:
            // nothing changed, so the snapshot is still good.)
            //
            // When the count has not moved, no row can have been rewritten, so
            // the blob listed in phase 1 is still this row's blob and we use it
            // as is. That is the whole point of the check: re-reading every row
            // costs about 40% of the walk on a big library, and it only ever
            // matters when a write landed mid-walk -- which the common case (a
            // report right after a batch, nothing else writing) never does.
            //
            // Otherwise a write did land, so fall back to re-reading the row:
            // reload_row returns false when the row is gone or a different
            // record now sits on its id, and that chart is left out of this
            // walk rather than decoded against paths that are not its own. The
            // next walk picks it up.
            //
            // Either way the blob and the nodes it names come from inside one
            // lock, so a write between them can never pair one row's shape with
            // another's paths. Both calls reset their statement before they
            // return, so this block leaves no cursor open -- the skip path
            // included.
            if (sqlite3_total_changes64(db_) == snapshot_changes) {
                structure = std::move(row.structure);  // rows is not walked again
            } else if (!reload_row(*reload_stmt, row.meta, row.result_id, structure)) {
                continue;
            }
            nodes = load_nodes(*nodes_stmt, row.result_id);
        }

        HydraRecord record = rebuild_record(
            structure, [&nodes](const std::string& hash) -> const std::vector<uint8_t>* {
                auto it = nodes.find(hash);
                return it == nodes.end() ? nullptr : &it->second;
            });
        fn(row.meta, &record);
    }
}

int RecordStore::drop_stale_records() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Stale is four things now: a stamp from another Hydra version, a
    // sentinel migrated in from an older database, a path tree stored in
    // an older layout, and a result analyzed under other rules. None of them
    // can be read under this build and these rules. kRowNotReadySql is
    // row_is_ready negated, spelled in SQL because a DELETE has to pick its
    // rows in the database; the two must be changed together.
    const char* kWhere = kRowNotReadySql;

    {
        Stmt s = prepare(db_,
            (std::string("DELETE FROM path_refs WHERE result_id IN"
                         " (SELECT result_id FROM results WHERE ") + kWhere + ")").c_str());
        bind_ready_params(s, 1, rules_fingerprint_);
        if (sqlite3_step(s) != SQLITE_DONE)
            throw std::runtime_error(std::string("drop_stale_records refs failed: ") +
                                     sqlite3_errmsg(db_));
    }

    int removed = 0;
    {
        Stmt s = prepare(db_,
            (std::string("DELETE FROM results WHERE ") + kWhere).c_str());
        bind_ready_params(s, 1, rules_fingerprint_);
        if (sqlite3_step(s) != SQLITE_DONE)
            throw std::runtime_error(std::string("drop_stale_records failed: ") +
                                     sqlite3_errmsg(db_));
        removed = sqlite3_changes(db_);
    }

    exec("DELETE FROM paths WHERE NOT EXISTS (SELECT 1 FROM path_refs pr"
         "  WHERE pr.hyhash = paths.hyhash AND pr.chartmode = paths.chartmode"
         "    AND pr.phash = paths.phash)");
    return removed;
}

int RecordStore::reindex() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    struct Row {
        int64_t result_id;
        std::string hyversion;
        int ms_enabled;
        std::vector<uint8_t> structure;
    };
    std::vector<Row> rows;
    {
        Stmt s = prepare(db_, "SELECT result_id, hyversion, ms_enabled, structure"
                              " FROM results ORDER BY result_id");
        while (sqlite3_step(s) == SQLITE_ROW)
            rows.push_back({sqlite3_column_int64(s, 0), column_text(s, 1),
                            sqlite3_column_int(s, 2), column_blob(s, 3)});
    }

    int done = 0;
    for (const Row& row : rows) {
        // A stale or sentinel row gets empty summaries: its stored bytes are
        // not this build's to read, so there is nothing to recompute from.
        PathSummary summary;
        if (row_is_ready(row.hyversion, row.ms_enabled, row.structure, rules_fingerprint_)) {
            const std::unordered_map<std::string, std::vector<uint8_t>> nodes =
                load_nodes(row.result_id);
            summary = summarize_record(rebuild_record(
                row.structure,
                [&nodes](const std::string& hash) -> const std::vector<uint8_t>* {
                    auto it = nodes.find(hash);
                    return it == nodes.end() ? nullptr : &it->second;
                }));
        }

        Stmt s = prepare(db_,
            "UPDATE results SET score=?,actcount=?,maxskip=?,hardest_ms=?,avgmult=?,"
            "notecount=?,sqin_count=?,sqout_count=?,pathcount=? WHERE result_id=?");
        bind_summary(s, 1, summary);
        sqlite3_bind_int64(s, 10, row.result_id);
        if (sqlite3_step(s) != SQLITE_DONE)
            throw std::runtime_error(std::string("reindex failed: ") + sqlite3_errmsg(db_));
        ++done;
    }
    return done;
}

std::vector<RecordListing> RecordStore::list_records(
    const std::optional<std::string>& chartmode, const CapQuery& cap, const Lens& lens,
    SortColumn order_by, bool descending, std::optional<int> limit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string sql =
        "SELECT s.hyhash, s.ref_name, s.ref_artist, s.ref_charter, r.chartmode, r.bestpath, "
        "r.score, r.actcount, r.maxskip, r.hardest_ms, r.avgmult, r.notecount, "
        "r.sqin_count, r.sqout_count, r.pathcount, r.sp_cap, "
        "r.hyversion, r.ms_enabled, r.result_id, substr(r.structure,1,12) "
        "FROM results r JOIN songmeta s ON s.hyhash = r.hyhash WHERE 1=1";
    if (chartmode) sql += " AND r.chartmode = ?";
    append_candidate_filter(sql, "r.", cap);

    // The sort stays in SQL, so the listing keeps sqlite's own ordering; the
    // passes below only drop rows, never reorder them. The limit cannot stay
    // here: a SQL LIMIT would count rows that are about to be dropped and hand
    // back fewer than the caller asked for.
    const char* prefix = sort_column_is_songmeta(order_by) ? "s." : "r.";
    sql += " ORDER BY ";
    sql += prefix;
    sql += sort_column_name(order_by);
    sql += descending ? " DESC" : " ASC";

    Stmt s = prepare(db_, sql.c_str());
    int idx = 1;
    if (chartmode) bind_text(s, idx++, *chartmode);
    bind_candidate_filter(s, idx, cap, lens);

    struct Row {
        RecordListing listing;
        Candidate rank;
        bool ready = false;
    };
    std::vector<Row> candidates;
    std::map<GroupKey, size_t> winner;  // chart+mode -> index of its best row
    while (sqlite3_step(s) == SQLITE_ROW) {
        Row row;
        row.listing.hyhash = column_text(s, 0);
        row.listing.ref_name = column_text(s, 1);
        row.listing.ref_artist = column_text(s, 2);
        row.listing.ref_charter = column_text(s, 3);
        row.listing.chartmode = column_text(s, 4);
        row.listing.bestpath = column_text(s, 5);
        row.listing.summary = read_summary(s, 6);
        row.listing.sp_cap = sqlite3_column_int(s, 15);

        const std::string hyversion = column_text(s, 16);
        const int ms_enabled = sqlite3_column_int(s, 17);
        const std::vector<uint8_t> structure_head = column_blob(s, 19);
        row.rank = rank_row(hyversion, ms_enabled, structure_head,
                            sqlite3_column_int64(s, 18), rules_fingerprint_);
        row.ready = row_is_ready(hyversion, ms_enabled, structure_head, rules_fingerprint_);

        const GroupKey key{row.listing.hyhash, row.listing.chartmode};
        auto it = winner.find(key);
        if (it == winner.end()) winner.emplace(key, candidates.size());
        else if (outranks(row.rank, candidates[it->second].rank)) it->second = candidates.size();
        candidates.push_back(std::move(row));
    }

    std::vector<bool> keep(candidates.size(), false);
    for (const auto& kv : winner) keep[kv.second] = true;

    // A listing shows only what this build can read. A stale winner takes its
    // chart out of the listing rather than handing the place to the next
    // candidate -- a chart whose answer nobody can read must read the same as
    // a chart nobody has analyzed. A negative limit means no limit, matching
    // sqlite's own LIMIT convention.
    std::vector<RecordListing> out;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!keep[i] || !candidates[i].ready) continue;
        if (limit && *limit >= 0 && out.size() >= static_cast<size_t>(*limit)) break;
        out.push_back(std::move(candidates[i].listing));
    }
    return out;
}

std::pair<int64_t, int64_t> RecordStore::counts() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt songs = prepare(db_, "SELECT COUNT(*) FROM songmeta");
    sqlite3_step(songs);
    int64_t nsongs = sqlite3_column_int64(songs, 0);

    Stmt records = prepare(db_, "SELECT COUNT(*) FROM results");
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
