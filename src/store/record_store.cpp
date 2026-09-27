#include "store/record_store.h"

#include <sqlite3.h>

#include <algorithm>
#include <map>
#include <stdexcept>

#include "store/serialize.h"
#include "store/stored_versions.h"

namespace hydra::store {

namespace {

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

// The results table's columns. result_id is the rowid alias: a bigger one
// means "written later", which is how Auto picks the newest run.
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

// ---- which row answers a lookup -------------------------------------------
//
// Every lookup answers one question: "which row ran under these settings?".
// The candidates are the rows with the wanted lens at the wanted cap. An
// older Hydra's migration left some rows with ms_enabled = -1, meaning "the
// settings are unknown". No lens has -1, so those rows are never candidates
// and read as no row at all (user decision 2026-09-26).

// "the row at alias `a` carries exactly this lens". Four bound parameters, in
// Lens's field order. `a` is "" or "r.".
std::string lens_match(const char* a) {
    std::string p = a;
    return "(" + p + "ms_enabled=? AND " + p + "ms_value=? AND " + p +
           "depth_mode=? AND " + p + "depth_value=?)";
}
int bind_lens(sqlite3_stmt* s, int idx, const Lens& lens) {
    sqlite3_bind_int(s, idx, lens.ms_enabled);
    sqlite3_bind_int(s, idx + 1, lens.ms_value);
    sqlite3_bind_int(s, idx + 2, lens.depth_mode);
    sqlite3_bind_int(s, idx + 3, lens.depth_value);
    return idx + 4;
}

// The first 12 bytes a structure blob starts with: the u32 path format, then
// the u64 rules fingerprint (path_codec.cpp flatten_record), little-endian.
constexpr int kStructureHeadBytes = 12;

uint64_t read_le(const std::vector<uint8_t>& b, size_t at, int bytes) {
    uint64_t v = 0;
    for (int i = 0; i < bytes; ++i) v |= static_cast<uint64_t>(b[at + i]) << (8 * i);
    return v;
}
std::vector<uint8_t> write_le(uint64_t v, int bytes) {
    std::vector<uint8_t> b(static_cast<size_t>(bytes));
    for (int i = 0; i < bytes; ++i) b[static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
    return b;
}

// Is this row's stored path tree in a path format this build reads?
bool layout_is_current(const std::vector<uint8_t>& structure_head) {
    return structure_head.size() >= 4 &&
           kPathFormatStamp.is_current(static_cast<uint32_t>(read_le(structure_head, 0, 4)));
}

// Is this row's stored path tree in a path format this build reads, analyzed
// under the rules this process runs, as a fixed-cap run or as an Auto run?
// Takes the whole blob or just the substr(structure,1,12) a query selected.
bool structure_is_current(const std::vector<uint8_t>& structure_head,
                          const core::RulesStamp& rules) {
    if (structure_head.size() < kStructureHeadBytes) return false;
    const uint64_t fingerprint = read_le(structure_head, 4, 8);
    return layout_is_current(structure_head) &&
           (fingerprint == rules.fixed || fingerprint == rules.autocap);
}

// The facts that decide whether a row is readable and how it places among
// the candidates for its chart and mode. rank_row is the only place C++ reads
// them off a row; row_ready_sql below is the same rule for SQL.
struct Candidate {
    bool current = false;  // stamped with this build's results version
    bool format = false;   // this build's path-structure format, analyzed
                           // under the rules this process runs
    int64_t result_id = 0;
    // Readable: stamped with this results version, in a layout this build
    // reads, under these rules. Anything else is Stale: another results
    // version's bytes, or a path tree whose activations this build would read
    // back half-empty.
    bool ready() const { return current && format; }
};

Candidate rank_row(const std::string& hyversion, const std::vector<uint8_t>& structure_head,
                   int64_t result_id, const core::RulesStamp& rules) {
    return Candidate{kResultsStamp.is_current(hyversion),
                     structure_is_current(structure_head, rules), result_id};
}

// Why a row that is not Ready is Stale, for callers that explain it
// (hydra_replay dump). `build`: another Hydra build or an older path layout.
// `rules`: this layout, analyzed under other rules. An older layout has no
// fingerprint to compare, so it is only ever `build`.
struct StaleReasons {
    bool build = false;
    bool rules = false;
};

StaleReasons stale_reasons(const std::string& hyversion,
                           const std::vector<uint8_t>& structure_head,
                           const core::RulesStamp& rules) {
    const bool layout_current = layout_is_current(structure_head);
    StaleReasons why;
    why.build = !kResultsStamp.is_current(hyversion) || !layout_current;
    why.rules = layout_current && !structure_is_current(structure_head, rules);
    return why;
}

// Candidate::ready() spelled in SQL, for the sites that must pick rows in the
// database: has_record only asks whether a readable row exists, and add_row's
// first purge is a DELETE. Negate it with "NOT ", never by spelling the
// opposite, so the rule has one SQL spelling. Both columns are NOT NULL, so
// NOT never meets a NULL.
//
// Built from the same StampRule lists is_current reads, so the two spellings
// cannot drift: every accepted results version, every accepted path format
// (the first four structure bytes), and the two rules fingerprints (the next
// eight). bind_ready_params binds them in that order and returns the next
// free index.
std::string placeholders(size_t n) {
    std::string out;
    for (size_t i = 0; i < n; ++i) out += i ? ", ?" : "?";
    return out;
}

const std::string& row_ready_sql() {
    static const std::string sql =
        "(hyversion IN (" + placeholders(kResultsStamp.accepted.size()) +
        ") AND substr(structure,1,4) IN (" + placeholders(kPathFormatStamp.accepted.size()) +
        ") AND substr(structure,5,8) IN (?, ?))";
    return sql;
}

int bind_ready_params(sqlite3_stmt* s, int idx, const core::RulesStamp& rules) {
    for (std::string_view v : kResultsStamp.accepted) bind_text(s, idx++, std::string(v));
    for (uint32_t f : kPathFormatStamp.accepted) bind_blob(s, idx++, write_le(f, 4));
    bind_blob(s, idx++, write_le(rules.fixed, 8));
    bind_blob(s, idx++, write_le(rules.autocap, 8));
    return idx;
}

// Does `a` beat `b`? This version before another, then this path format
// before an older one, then the newest write. Newest, not tallest: an Auto
// run that settles below an older, taller row (a what-if the user typed) is
// the result the user just asked for, so every lookup must show it. The
// tallest rule showed the old row forever and "Analyze paths!" could never
// replace it. Write order is result_id: add_row deletes and re-inserts, so a
// rewritten row is newest.
bool outranks(const Candidate& a, const Candidate& b) {
    if (a.current != b.current) return a.current;
    if (a.format != b.format) return a.format;
    return a.result_id > b.result_id;
}

// Which chart a winner is picked for: one per chart and mode.
using GroupKey = std::pair<std::string, std::string>;

// The one owner of "which row wins". A lookup offers its candidate rows here
// in any order, then asks which offer won each chart and mode: get_summary
// and get_record for one chart, for_each_blob and list_records for many.
// Nothing else compares two rows.
class WinnerPicker {
public:
    // Offers one candidate. Its offer index is the number of earlier offers.
    void offer(const std::string& hyhash, const std::string& chartmode,
               const Candidate& rank) {
        const size_t index = ranks_.size();
        ranks_.push_back(rank);
        auto [it, inserted] = winner_.emplace(GroupKey{hyhash, chartmode}, index);
        if (!inserted && outranks(rank, ranks_[it->second])) it->second = index;
    }
    // One flag per offer: true for each chart and mode's winner.
    std::vector<bool> winners() const {
        std::vector<bool> won(ranks_.size(), false);
        for (const auto& kv : winner_) won[kv.second] = true;
        return won;
    }
    // The winning offer of a one-chart lookup, or nullopt when nothing was
    // offered.
    std::optional<size_t> only_winner() const {
        if (winner_.empty()) return std::nullopt;
        return winner_.begin()->second;
    }
    const Candidate& rank(size_t index) const { return ranks_[index]; }

private:
    std::vector<Candidate> ranks_;
    std::map<GroupKey, size_t> winner_;  // chart+mode -> index of its best offer
};

// Every row that could answer a lookup: the wanted lens at the wanted cap.
// Which of them wins is WinnerPicker's decision and not SQL's, so there is
// deliberately no ORDER BY or LIMIT here. `a` is the table alias, "" or "r.".
// Appended after a WHERE that already has a term.
void append_candidate_filter(std::string& sql, const char* a, const CapQuery& cap) {
    const std::string p = a;
    sql += " AND " + lens_match(a);
    if (cap.exact) sql += " AND " + p + "sp_cap=?";
    else sql += " AND " + p + "sp_cap>" + std::to_string(kCloneHeroSpCap);
}
// Binds the lens's four parameters, plus one more for an exact cap.
int bind_candidate_filter(sqlite3_stmt* s, int idx, const CapQuery& cap, const Lens& lens) {
    idx = bind_lens(s, idx, lens);
    if (cap.exact) sqlite3_bind_int(s, idx++, *cap.exact);
    return idx;
}

// Rolls back the open transaction, if there still is one. Some failures (a
// full disk, an I/O error) make sqlite roll back by itself, and a second
// ROLLBACK would then throw over the error that caused it.
void rollback_if_open(sqlite3* db) {
    if (!sqlite3_get_autocommit(db)) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
}

// The rows that make a chart "already analyzed" for a batch run: a readable
// row under exactly this chart mode, cap and lens. Never another lens's row,
// or a batch would skip charts whose stored answer came from a different
// question. has_record and analyzed_hashes share it so the two cannot drift.
// Binds, from `idx`: the chart mode, the ready parameters, the lens, then an
// exact cap when there is one.
std::string analyzed_filter(const CapQuery& cap) {
    std::string sql =
        "chartmode=? AND " + row_ready_sql() + " AND " + lens_match("");
    if (cap.exact) sql += " AND sp_cap=?";
    else sql += " AND sp_cap>" + std::to_string(kCloneHeroSpCap);
    return sql;
}
int bind_analyzed_filter(sqlite3_stmt* s, int idx, const std::string& chartmode,
                         const CapQuery& cap, const Lens& lens,
                         const core::RulesStamp& rules_fingerprint) {
    bind_text(s, idx++, chartmode);
    idx = bind_ready_params(s, idx, rules_fingerprint);
    idx = bind_lens(s, idx, lens);
    if (cap.exact) sqlite3_bind_int(s, idx++, *cap.exact);
    return idx;
}

}  // namespace

// ---- summarize_path / summarize_record / prepare_row -----------------------

PathSummary summarize_path(const Path& path) {
    PathSummary s;
    const ActivationWalk acts = path.walk_activations();

    s.score = path.totalscore();
    s.actcount = static_cast<int>(acts.size());

    int maxskip = 0;
    std::optional<double> hardest;
    int sqin = 0, sqout = 0;
    for (const Activation& a : acts) {
        if (a.skips > maxskip) maxskip = a.skips;
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

std::string current_record_version() { return std::string(kResultsStamp.written); }

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

RecordStore::RecordStore(const std::string& dbpath, core::RulesStamp rules_fingerprint)
    : rules_fingerprint_(rules_fingerprint) {
    if (sqlite3_open(dbpath.c_str(), &db_) != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "unknown error";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("failed to open database '" + dbpath + "': " + msg);
    }

    // WAL journal mode (orchestrator's call, 2026-09-26 audit plan): a commit
    // appends to hydra.db-wal instead of rewriting pages in place, and with
    // synchronous=NORMAL it syncs to disk only at checkpoints, not on every
    // commit. A power cut can lose the last few commits but never corrupts
    // the file, and readers stop blocking the writer
    // (https://www.sqlite.org/wal.html). journal_mode is stored in the file;
    // synchronous is per connection, so both are set on every open. A
    // ":memory:" store answers "memory" and is unaffected.
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");

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
    // A charts table from before the rescan cache has no sig column.
    // rebuild_chart_library empties the table rather than recreating it, so
    // the column is added here, once.
    if (!has_column("charts", "sig")) exec("ALTER TABLE charts ADD COLUMN sig TEXT");
    // The library page sorts by name (list_chart_library's ORDER BY name).
    exec("CREATE INDEX IF NOT EXISTS charts_by_name ON charts (name)");
    // Schema 2 = results keyed by the full settings, with shared paths. A
    // database from Hydra 1.6 or older still holds its old `records` table.
    // Nothing reads it (user decision 2026-09-26), so its charts read Not
    // analyzed until they are analyzed again.
    create_result_tables();
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
    insert_dynamics(key, blob, count_version);
}

void RecordStore::insert_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob,
                                  int count_version) {
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

void RecordStore::save_analysis(const std::string& hyhash, const std::string& ref_name,
                                const std::string& ref_artist, const std::string& ref_charter,
                                const Song& song, const PreparedRow& row,
                                const std::optional<DynamicsEntry>& dynamics) {
    const std::vector<uint8_t> tempomap = encode_tempomap(song);  // not a sqlite call
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    exec("BEGIN");
    try {
        upsert_song(hyhash, ref_name, ref_artist, ref_charter, tempomap);
        write_row(row);
        if (dynamics) {
            // Best effort, inside the same transaction: a failed count write
            // is undone on its own and never costs the result.
            exec("SAVEPOINT dynamics");
            try {
                insert_dynamics(dynamics->key, dynamics->blob, dynamics->count_version);
                exec("RELEASE dynamics");
            } catch (const std::exception&) {
                exec("ROLLBACK TO dynamics");
                exec("RELEASE dynamics");
            }
        }
        exec("COMMIT");
    } catch (...) {
        rollback_if_open(db_);
        throw;
    }
}

std::optional<std::vector<uint8_t>> RecordStore::get_dynamics(const DynamicsKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // A row whose count stamp this build doesn't accept reads as missing, so
    // the caller recounts it and put_dynamics restamps it.
    Stmt s = prepare(db_,
        "SELECT blob, count_version FROM dynamics WHERE md5=? AND difficulty=? AND pro=?");
    bind_text(s, 1, key.md5);
    bind_text(s, 2, key.difficulty);
    sqlite3_bind_int(s, 3, key.pro ? 1 : 0);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    if (!kDynamicsCountStamp.is_current(sqlite3_column_int(s, 1))) return std::nullopt;
    return column_blob(s, 0);
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

void RecordStore::add_song(const std::string& hyhash, const std::string& ref_name,
                           const std::string& ref_artist, const std::string& ref_charter,
                           const Song& song) {
    // Encoded before the lock: the lock covers sqlite calls only.
    const std::vector<uint8_t> tempomap = encode_tempomap(song);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    upsert_song(hyhash, ref_name, ref_artist, ref_charter, tempomap);
}

void RecordStore::upsert_song(const std::string& hyhash, const std::string& ref_name,
                              const std::string& ref_artist, const std::string& ref_charter,
                              const std::vector<uint8_t>& tempomap) {
    // A chart already registered takes the names this call carries. They
    // come from the scan, so a fixed song.ini reaches the reports on the next
    // analysis (user decision 2026-09-26). The tempo map is keyed by the same
    // content hash, so it cannot have changed and is left alone.
    Stmt s = prepare(db_,
        "INSERT INTO songmeta (hyhash, ref_name, ref_artist, ref_charter, tempomap) "
        "VALUES (?,?,?,?,?) "
        "ON CONFLICT(hyhash) DO UPDATE SET ref_name = excluded.ref_name, "
        "ref_artist = excluded.ref_artist, ref_charter = excluded.ref_charter");
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
    exec("BEGIN");
    try {
        write_row(row);
        exec("COMMIT");
    } catch (...) {
        rollback_if_open(db_);
        throw;
    }
}

void RecordStore::write_row(const PreparedRow& row) {
    // Deleting a result means deleting its refs first, always: the refs are
    // what keep its paths alive, and the final sweep collects whatever they
    // stopped pointing at. The caller holds the lock and an open transaction,
    // so a failure anywhere leaves the store exactly as it was.
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

    // (1) Anything this chart+mode holds that this build cannot read --
    //     another Hydra version's stamp (which includes every row an old
    //     migration left), an older path layout, a result analyzed under
    //     other rules -- is superseded by a write here. The test is against
    //     what is current, not against this row: a test writing a
    //     deliberately old-stamped row must not take the real rows with it,
    //     and this runs before the insert so the new row is untouched.
    purge("hyhash=? AND chartmode=? AND NOT " + row_ready_sql(),
          [&](sqlite3_stmt* s) {
              bind_text(s, 1, row.hyhash);
              bind_text(s, 2, row.chartmode);
              bind_ready_params(s, 3, rules_fingerprint_);
          },
          "unreadable purge");

    // (2) The row this one replaces, deleted explicitly rather than by
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

    // (3) The result, then its paths (shared, so first writer wins) and the
    //     refs that tie the two together.
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

    {
        // Compiled once per row, not once per node, and bound once with what
        // every node shares. Bindings survive a reset, so each node only
        // rebinds its own hash and payload.
        Stmt path_insert = prepare(db_,
            "INSERT OR IGNORE INTO paths (hyhash, chartmode, phash, payload)"
            " VALUES (?,?,?,?)");
        Stmt ref_insert = prepare(db_,
            "INSERT OR IGNORE INTO path_refs (result_id, hyhash, chartmode, phash)"
            " VALUES (?,?,?,?)");
        bind_text(path_insert, 1, row.hyhash);
        bind_text(path_insert, 2, row.chartmode);
        sqlite3_bind_int64(ref_insert, 1, result_id);
        bind_text(ref_insert, 2, row.hyhash);
        bind_text(ref_insert, 3, row.chartmode);
        for (const StoredPathNode& node : row.nodes) {
            bind_text(path_insert, 3, node.hash);
            bind_blob(path_insert, 4, node.payload);
            run(path_insert, "path insert");
            sqlite3_reset(path_insert);
            bind_text(ref_insert, 4, node.hash);
            run(ref_insert, "path ref insert");
            sqlite3_reset(ref_insert);
        }
    }

    // (4) Whatever the replaced row was the last owner of.
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
}

SummaryLookup RecordStore::get_summary(const RecordKey& key) {
    return get_summaries({key.hyhash}, key.chartmode, key.cap, key.lens).front();
}

std::vector<SummaryLookup> RecordStore::get_summaries(const std::vector<std::string>& hyhashes,
                                                      const std::string& chartmode,
                                                      const CapQuery& cap, const Lens& lens) {
    std::vector<SummaryLookup> out(hyhashes.size());
    if (hyhashes.empty()) return out;

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string sql =
        "SELECT hyhash, hyversion, bestpath, result_id, substr(structure,1,12)"
        " FROM results WHERE chartmode=? AND hyhash IN (";
    for (size_t i = 0; i < hyhashes.size(); ++i) sql += i ? ",?" : "?";
    sql += ")";
    append_candidate_filter(sql, "", cap);
    Stmt s = prepare(db_, sql.c_str());
    int idx = 1;
    bind_text(s, idx++, chartmode);
    for (const std::string& h : hyhashes) bind_text(s, idx++, h);
    bind_candidate_filter(s, idx, cap, lens);

    WinnerPicker picker;
    std::vector<std::pair<std::string, std::string>> offered;  // hyhash, bestpath
    while (sqlite3_step(s) == SQLITE_ROW) {
        std::string hyhash = column_text(s, 0);
        picker.offer(hyhash, chartmode,
                     rank_row(column_text(s, 1), column_blob(s, 4),
                              sqlite3_column_int64(s, 3), rules_fingerprint_));
        offered.emplace_back(std::move(hyhash), column_text(s, 2));
    }

    // One answer per chart, then handed to every position that asked for it
    // (a page can list one chart twice, from two folders). A stale winner is
    // reported as Stale, not hidden: the library's status column has to tell
    // "analyzed by another build" apart from "never analyzed".
    const std::vector<bool> won = picker.winners();
    std::unordered_map<std::string, SummaryLookup> by_hash;
    for (size_t i = 0; i < offered.size(); ++i) {
        if (!won[i]) continue;
        SummaryLookup& answer = by_hash[offered[i].first];
        if (picker.rank(i).ready()) {
            answer.status = RecordStatus::Ready;
            answer.bestpath = offered[i].second;
        } else {
            answer.status = RecordStatus::Stale;
        }
    }
    for (size_t i = 0; i < hyhashes.size(); ++i) {
        auto it = by_hash.find(hyhashes[i]);
        if (it != by_hash.end()) out[i] = it->second;
    }
    return out;
}

RecordLookup RecordStore::get_record(const RecordKey& key) {
    RecordLookup out;
    std::vector<uint8_t> structure;
    std::unordered_map<std::string, std::vector<uint8_t>> nodes;
    std::optional<std::vector<uint8_t>> tempomap;
    {
        // The lock covers the reads and nothing else. The winning row, the
        // nodes it names and the song's tempo map are all read under this one
        // lock, so a write in between can never pair one row's shape with
        // another's paths. Decoding happens after the lock is released: the
        // UI thread calls this, and a big record must not hold up the batch
        // writer.
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::string sql =
            "SELECT result_id, hyversion, structure FROM results"
            " WHERE hyhash=? AND chartmode=?";
        append_candidate_filter(sql, "", key.cap);
        Stmt s = prepare(db_, sql.c_str());
        bind_text(s, 1, key.hyhash);
        bind_text(s, 2, key.chartmode);
        bind_candidate_filter(s, 3, key.cap, key.lens);

        struct Row {
            int64_t result_id = 0;
            std::string hyversion;
            std::vector<uint8_t> structure;
        };
        std::vector<Row> rows;  // by offer index
        WinnerPicker picker;
        while (sqlite3_step(s) == SQLITE_ROW) {
            Row row{sqlite3_column_int64(s, 0), column_text(s, 1), column_blob(s, 2)};
            // The whole blob is here, so its leading twelve bytes are the
            // structure head the format and rules check wants.
            picker.offer(key.hyhash, key.chartmode,
                         rank_row(row.hyversion, row.structure, row.result_id,
                                  rules_fingerprint_));
            rows.push_back(std::move(row));
        }
        const std::optional<size_t> won = picker.only_winner();
        if (!won) return RecordLookup{};
        Row& best = rows[*won];

        out.hyversion = best.hyversion;
        // Stamped by a different version or holding a path tree in an older
        // layout or under other rules: nothing stored is decoded at all.
        // Callers see Stale and prompt a re-analyze.
        if (!picker.rank(*won).ready()) {
            out.status = RecordStatus::Stale;
            const StaleReasons why =
                stale_reasons(best.hyversion, best.structure, rules_fingerprint_);
            out.stale_build = why.build;
            out.stale_rules = why.rules;
            return out;
        }
        structure = std::move(best.structure);
        nodes = load_nodes(best.result_id);
        tempomap = read_tempomap(key.hyhash);
    }

    out.status = RecordStatus::Ready;
    HydraRecord record = rebuild_record(
        structure, [&nodes](const std::string& hash) -> const std::vector<uint8_t>* {
            auto it = nodes.find(hash);
            return it == nodes.end() ? nullptr : &it->second;
        });
    // The tempomap is decoded once, here, and handed back with the record --
    // the display layer needs the same timing and must not query for it again.
    if (tempomap) {
        out.timing = decode_tempomap(*tempomap);
        restore_timecodes(record, *out.timing);
    }
    out.record = std::move(record);
    return out;
}

std::optional<std::vector<uint8_t>> RecordStore::read_tempomap(const std::string& hyhash) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_, "SELECT tempomap FROM songmeta WHERE hyhash=?");
    bind_text(s, 1, hyhash);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    return column_blob(s, 0);
}

std::optional<SongTiming> RecordStore::get_timing(const std::string& hyhash) {
    // Read under the lock, decoded outside it.
    const std::optional<std::vector<uint8_t>> blob = read_tempomap(hyhash);
    if (!blob) return std::nullopt;
    return decode_tempomap(*blob);
}

bool RecordStore::has_record(const RecordKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string sql =
        "SELECT 1 FROM results WHERE hyhash=? AND " + analyzed_filter(key.cap) + " LIMIT 1";
    Stmt s = prepare(db_, sql.c_str());
    bind_text(s, 1, key.hyhash);
    bind_analyzed_filter(s, 2, key.chartmode, key.cap, key.lens, rules_fingerprint_);
    return sqlite3_step(s) == SQLITE_ROW;
}

std::unordered_set<std::string> RecordStore::analyzed_hashes(const std::string& chartmode,
                                                             const CapQuery& cap,
                                                             const Lens& lens) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string sql = "SELECT DISTINCT hyhash FROM results WHERE " + analyzed_filter(cap);
    Stmt s = prepare(db_, sql.c_str());
    bind_analyzed_filter(s, 1, chartmode, cap, lens, rules_fingerprint_);
    std::unordered_set<std::string> out;
    while (sqlite3_step(s) == SQLITE_ROW) out.insert(column_text(s, 0));
    return out;
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
            "r.chartmode, r.hyversion, r.sp_cap, r.result_id, r.structure "
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

        std::vector<Row> candidates;  // by offer index
        WinnerPicker picker;
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
            // rank_row reads only the blob's leading twelve bytes, so the
            // whole blob serves as its head.
            const Candidate rank = rank_row(row.meta.hyversion, row.structure,
                                            row.result_id, rules_fingerprint_);
            row.meta.status = rank.ready() ? RecordStatus::Ready : RecordStatus::Stale;
            picker.offer(row.meta.hyhash, row.meta.chartmode, rank);
            candidates.push_back(std::move(row));
        }

        // One row per chart and mode, the same one a lookup would pick, kept
        // in result_id order. A stale winner is still yielded: this is the
        // export path, and dropping a row here would lose it for good.
        const std::vector<bool> keep = picker.winners();
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

int RecordStore::reindex() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    struct Row {
        int64_t result_id;
        std::string hyversion;
        std::vector<uint8_t> structure;
    };
    std::vector<Row> rows;
    {
        Stmt s = prepare(db_, "SELECT result_id, hyversion, structure"
                              " FROM results ORDER BY result_id");
        while (sqlite3_step(s) == SQLITE_ROW)
            rows.push_back({sqlite3_column_int64(s, 0), column_text(s, 1), column_blob(s, 2)});
    }

    // One transaction for the whole pass, and each statement compiled once.
    // This used to commit once per record: 18,000 commits on a full library.
    exec("BEGIN");
    try {
        Stmt nodes_stmt = prepare(db_, kLoadNodesSql);
        Stmt update = prepare(db_,
            "UPDATE results SET score=?,actcount=?,maxskip=?,hardest_ms=?,avgmult=?,"
            "notecount=?,sqin_count=?,sqout_count=?,pathcount=? WHERE result_id=?");
        int done = 0;
        for (const Row& row : rows) {
            // A stale row gets empty summaries: its stored bytes are not this
            // build's to read, so there is nothing to recompute from.
            PathSummary summary;
            if (rank_row(row.hyversion, row.structure, row.result_id, rules_fingerprint_)
                    .ready()) {
                const std::unordered_map<std::string, std::vector<uint8_t>> nodes =
                    load_nodes(nodes_stmt, row.result_id);
                summary = summarize_record(rebuild_record(
                    row.structure,
                    [&nodes](const std::string& hash) -> const std::vector<uint8_t>* {
                        auto it = nodes.find(hash);
                        return it == nodes.end() ? nullptr : &it->second;
                    }));
            }
            ResetOnExit reset{update};
            bind_summary(update, 1, summary);
            sqlite3_bind_int64(update, 10, row.result_id);
            if (sqlite3_step(update) != SQLITE_DONE)
                throw std::runtime_error(std::string("reindex failed: ") + sqlite3_errmsg(db_));
            ++done;
        }
        exec("COMMIT");
        return done;
    } catch (...) {
        rollback_if_open(db_);
        throw;
    }
}

std::vector<RecordListing> RecordStore::list_records(
    const std::optional<std::string>& chartmode, const CapQuery& cap, const Lens& lens,
    SortColumn order_by, bool descending, std::optional<int> limit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string sql =
        "SELECT s.hyhash, s.ref_name, s.ref_artist, s.ref_charter, r.chartmode, r.bestpath, "
        "r.score, r.actcount, r.maxskip, r.hardest_ms, r.avgmult, r.notecount, "
        "r.sqin_count, r.sqout_count, r.pathcount, r.sp_cap, "
        "r.hyversion, r.result_id, substr(r.structure,1,12) "
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

    std::vector<RecordListing> candidates;  // by offer index
    WinnerPicker picker;
    while (sqlite3_step(s) == SQLITE_ROW) {
        RecordListing listing;
        listing.hyhash = column_text(s, 0);
        listing.ref_name = column_text(s, 1);
        listing.ref_artist = column_text(s, 2);
        listing.ref_charter = column_text(s, 3);
        listing.chartmode = column_text(s, 4);
        listing.bestpath = column_text(s, 5);
        listing.summary = read_summary(s, 6);
        listing.sp_cap = sqlite3_column_int(s, 15);
        picker.offer(listing.hyhash, listing.chartmode,
                     rank_row(column_text(s, 16), column_blob(s, 18),
                              sqlite3_column_int64(s, 17), rules_fingerprint_));
        candidates.push_back(std::move(listing));
    }

    // A listing shows only what this build can read. A stale winner takes its
    // chart out of the listing rather than handing the place to the next
    // candidate -- a chart whose answer nobody can read must read the same as
    // a chart nobody has analyzed. A negative limit means no limit, matching
    // sqlite's own LIMIT convention.
    const std::vector<bool> keep = picker.winners();
    std::vector<RecordListing> out;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!keep[i] || !picker.rank(i).ready()) continue;
        if (limit && *limit >= 0 && out.size() >= static_cast<size_t>(*limit)) break;
        out.push_back(std::move(candidates[i]));
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

    // One transaction around the whole swap, opened before anything changes:
    // a failure anywhere rolls back to the previous scan's rows, which are
    // also the rescan cache. The table is emptied rather than dropped, so its
    // columns and index stay as they are.
    exec("BEGIN");
    try {
        exec("DELETE FROM charts");
        Stmt s = prepare(db_,
            "INSERT INTO charts (md5, name, artist, charter, path, folder, sig)"
            " VALUES (?,?,?,?,?,?,?)");
        for (const ChartLibraryEntry& item : items) {
            sqlite3_reset(s);
            bind_text(s, 1, item.md5);
            bind_text(s, 2, item.title);
            bind_text(s, 3, item.artist);
            bind_text(s, 4, item.charter);
            bind_text(s, 5, item.notespath);
            bind_text(s, 6, item.rootfolder);
            bind_text(s, 7, item.sig);
            if (sqlite3_step(s) != SQLITE_DONE)
                throw std::runtime_error(std::string("rebuild_chart_library failed: ") +
                                         sqlite3_errmsg(db_));
        }
        // Song names follow song.ini (user decision 2026-09-26): a chart that
        // already has a song row takes the names this scan read. When the
        // scan found the same chart twice, the first copy it listed names it
        // (sqlite takes a bare column from the MIN(rowid) row).
        exec("UPDATE songmeta SET ref_name = c.name, ref_artist = c.artist,"
             " ref_charter = c.charter"
             " FROM (SELECT md5, name, artist, charter, MIN(rowid) FROM charts GROUP BY md5)"
             " AS c WHERE songmeta.hyhash = c.md5");
        exec("COMMIT");
    } catch (...) {
        rollback_if_open(db_);
        throw;
    }
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
