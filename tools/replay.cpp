// hydra_replay — price an arbitrary Star Power path, chord by chord.
//
// The GUI only ever shows the paths the search found. This tool answers the
// other question: "what is MY path worth, and where does each point come
// from?" It has four modes.
//
//   score     Walk a chart under a list of activation windows and dump every
//             chord with its own score breakdown, its running totals, and
//             whether it fell under Star Power. The windows come either from
//             --acts, typed by hand, or from --path, read straight out of a
//             file `dump` or `target` wrote so that nothing is retyped and
//             the squeeze-out offsets survive.
//   dump      Read the paths a record already holds out of the database, with
//             each activation's deactivation node resolved to a tick — the
//             input `score` wants.
//   target    Hand the engine an activation set -- "activate at exactly these
//             fill ticks and nowhere else" -- and get that path back priced the
//             engine's own way, with deactivation nodes, SP meters and squeeze
//             variants stamped as usual. What `dump` gives you for the paths a
//             search happened to keep, this gives you for the path you name.
//   selfcheck Re-analyze the test corpus, replay every path the engine found,
//             and prove the replay's six score categories match the engine's
//             own. This is what keeps `score` honest.
//
// The scoring itself lives in core/replay.h so this tool and tests/test_replay
// share one implementation.

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <memory>
#include <optional>
#include <process.h>
#include <string>
#include <vector>

#include "json.hpp"

#include "app/analysis.h"
#include "app/config.h"
#include "app/rules_file.h"
#include "core/replay.h"
#include "core/squeeze_rating.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/pather.h"
#include "store/record_store.h"

using namespace hydra;
using json = nlohmann::json;

namespace {

// ---- argument plumbing ---------------------------------------------------

struct Args {
    std::string command;
    std::string chart;
    std::string db;
    std::string out;
    std::string cap = "4";
    std::string ms = "10";
    std::string depth_mode = "scores";
    int depth = 4;
    std::string acts;
    std::string path;   // a dump/target JSON file to read a path out of
    int index = 0;      // which entry of that file's "paths" array
    std::string ticks;
    bool prodrums = true;
    bool bass2x = true;
    std::string difficulty = "expert";
    bool pretty = false;
    bool no_analyze = false;
    bool legacy_fills = false;
    std::string rules_path;  // --rules; empty = hydra_rules.ini next to the exe
    core::Rules rules;       // loaded once in main, before any command runs
};

bool flag_bool(const std::string& v) { return v == "1" || v == "true" || v == "yes"; }

void usage() {
    std::printf(
        "hydra_replay — score an arbitrary Star Power path, engine-exactly.\n\n"
        "  hydra_replay score --chart <file> [--acts \"actTick:deactTick[:sqoutMs],...\"]\n"
        "                     [--path <dump-or-target.json>] [--index N]\n"
        "                     [--out <json>] [--pretty] [--prodrums 0|1] [--bass2x 0|1]\n"
        "                     [--difficulty expert|hard|medium|easy]\n"
        "  hydra_replay dump  --chart <file> --db <path> [--cap N|auto]\n"
        "                     [--ms N|off] [--depth-mode scores|points] [--depth N]\n"
        "                     [--out <json>] [--pretty] [--no-analyze] [--legacy-fills]\n"
        "  hydra_replay target --chart <file> --ticks \"t1,t2,...\" [--cap N]\n"
        "                     [--out <json>] [--pretty] [--prodrums 0|1] [--bass2x 0|1]\n"
        "                     [--difficulty expert|hard|medium|easy] [--legacy-fills]\n"
        "  hydra_replay selfcheck [--chart <file>] [--verbose]\n\n"
        "Omitting both --acts and --path scores the chart with no Star Power\n"
        "anywhere. --path reads a path straight out of a file dump or target\n"
        "wrote, so nothing is retyped and no field is lost -- in particular the\n"
        "squeeze-out offset, which an --acts string typed by hand usually drops\n"
        "and which is worth real points. --index picks the entry of that file's\n"
        "\"paths\" array (default 0). --path and --acts cannot both be given.\n"
        "A typed SqOut offset is matched to the nearest phrase chord within 500 ms of the SP end and the chord used is printed on stderr; a chord the engine would never squeeze out is refused.\n"
        "Where a window ends on a Star Power phrase note but carries no\n"
        "squeeze-out offset, score prints a warning: the score is right if the\n"
        "player did not squeeze that note out, and high if they did. The\n"
        "warnings are also in the JSON, as \"warnings\".\n"
        "score's JSON also carries \"sections\": the chart's practice sections, "
        "each with tick, ms, and name.\n"
        "dump copies the database to a per-process file under %%TEMP%% before\n"
        "reading it, so a running Hydra.exe is never disturbed; the snapshot is\n"
        "deleted again when dump finishes. If the stored record is missing or\n"
        "stale, dump analyzes the chart fresh instead of failing (JSON field\n"
        "\"source\" says which happened); --no-analyze turns that off.\n"
        "target asks the engine to price one specific path: activate at exactly\n"
        "the --ticks fills and nowhere else. Its JSON carries dump's \"paths\"\n"
        "shape plus \"realized\"; when that is false, \"failed_tick\" names the\n"
        "activation the engine could not make and \"realized_prefix\" how many of\n"
        "the leading ticks it did manage.\n"
        "--legacy-fills prices the chart under Clone Hero 1.0's fill deadline,\n"
        "which is what a 1.0 run was played under. Every stored row is a 1.1\n"
        "result, so dump ignores the database and always analyzes fresh; its\n"
        "\"source\" then reads \"analyzed-ch10\".\n"
        "Every command takes --rules <file>: the rule choices to price under\n"
        "(default: hydra_rules.ini next to the exe). A bad file exits with 2.\n"
        "JSON is printed compact by default; --pretty indents it.\n");
}

// ---- shared settings -----------------------------------------------------

app::Settings settings_from(const Args& a) {
    app::Settings s;  // struct defaults are the GUI defaults
    s.view_prodrums = a.prodrums;
    s.view_bass2x = a.bass2x;
    s.view_difficulty =
        difficulty_name(difficulty_from_name(a.difficulty).value_or(Difficulty::Expert));

    if (a.cap == "auto") s.sp_cap = std::nullopt;
    else s.sp_cap = std::atoi(a.cap.c_str());

    if (a.ms == "off") s.mslimit_enabled = false;
    else { s.mslimit_enabled = true; s.mslimit_value = std::atoi(a.ms.c_str()); }

    s.depth_mode = a.depth_mode == "points" ? 1 : 0;
    s.depth_value = a.depth;
    s.rules = a.rules;
    return s;
}

void emit(const json& j, const std::string& out, bool pretty) {
    const std::string text = pretty ? j.dump(1, ' ') : j.dump();
    if (out.empty()) {
        std::fwrite(text.data(), 1, text.size(), stdout);
        std::fputc('\n', stdout);
        return;
    }
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    f << text << "\n";
    std::printf("wrote %s (%zu bytes)\n", out.c_str(), text.size());
}

json score_json(const ReplayScore& s) {
    json j = json::object();
    for (const ReplayScoreField& f : kReplayScoreFields) j[f.name] = s.*(f.member);
    return j;
}

// The path list `dump` and `target` both print. One shape, so anything that
// reads dump's JSON reads target's too.
json paths_json(const std::vector<const Path*>& all, const SongTiming& timing) {
    json paths = json::array();
    int index = 0;
    for (const Path* p : all) {
        json acts = json::array();
        for (const Activation& act : p->all_activations()) {
            json sq = json::array();
            for (const SPSqueeze& s2 : act.sqinouts)
                sq.push_back(json{{"kind", s2.type_name()},
                                  {"offset_ms", s2.offset()}});

            const int64_t act_tick = act.timecode ? act.timecode->ticks() : -1;
            const std::optional<int64_t>& d = act.deact_tick;
            int64_t nominal = -1;
            if (act.timecode && act.sp_meter)
                nominal = timing
                              .plusmeasure(*act.timecode, sp_bars_to_measures(*act.sp_meter))
                              .ticks();

            acts.push_back(json{
                {"act_tick", act_tick},
                {"deact_tick", d ? *d : -1},
                {"sqout_tick", act.sqout_tick ? *act.sqout_tick : -1},
                {"nominal_deact_tick", nominal},
                {"sp_meter", act.sp_meter ? *act.sp_meter : -1},
                {"skips", act.skips ? *act.skips : -1},
                {"chord_code", act.chord ? act.chord->code() : std::string()},
                {"sqinouts", sq},
            });
        }
        paths.push_back(json{
            {"index", index++},
            {"pathstring", p->pathstring()},
            {"total", p->totalscore()},
            {"score", score_json(score_of(*p))},
            {"activations", acts},
        });
    }
    return paths;
}

// ---- score ---------------------------------------------------------------

std::string trimmed(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

// A strict, whole-string std::strtoll: no leading/trailing junk, no empty
// field. `what` and `item` name the offending piece in the exception, so a
// typo in --acts says exactly which field and which item it broke on rather
// than printing a silently wrong score.
int64_t strict_ll(const std::string& field, const std::string& what,
                  const std::string& item) {
    const std::string t = trimmed(field);
    if (t.empty())
        throw std::runtime_error("--acts item '" + item + "' has an empty " +
                                 what);
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(t.c_str(), &end, 10);
    if (end != t.c_str() + t.size() || errno == ERANGE)
        throw std::runtime_error("--acts item '" + item + "' has a bad " +
                                 what + " '" + t + "'");
    return v;
}

double strict_d(const std::string& field, const std::string& what,
                const std::string& item) {
    const std::string t = trimmed(field);
    if (t.empty())
        throw std::runtime_error("--acts item '" + item + "' has an empty " +
                                 what);
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size() || errno == ERANGE)
        throw std::runtime_error("--acts item '" + item + "' has a bad " +
                                 what + " '" + t + "'");
    return v;
}

// "12:3456,7890:9012" -> windows. Throws on anything malformed: a silently
// dropped or misparsed activation would print a wrong score with no hint
// why. Each field is parsed strictly (strtoll/strtod with an end-pointer
// check after trimming spaces); leftover characters, an empty field, a
// negative tick, or a deactivation before its activation all throw, naming
// the offending item.
std::vector<ReplayWindow> parse_acts(const std::string& spec) {
    std::vector<ReplayWindow> out;
    size_t i = 0;
    while (i < spec.size()) {
        size_t comma = spec.find(',', i);
        if (comma == std::string::npos) comma = spec.size();
        std::string item = spec.substr(i, comma - i);
        i = comma + 1;
        item = trimmed(item);
        if (item.empty()) continue;

        const size_t colon = item.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error("--acts item '" + item +
                                     "' is not actTick:deactTick");
        const std::string rest = item.substr(colon + 1);
        const size_t colon2 = rest.find(':');
        const std::string deact_field = rest.substr(0, colon2);

        ReplayWindow w;
        w.act_tick = strict_ll(item.substr(0, colon), "act tick", item);
        w.deact_tick = strict_ll(deact_field, "deact tick", item);
        if (w.act_tick < 0)
            throw std::runtime_error("--acts item '" + item +
                                     "' has a negative act tick");
        if (w.deact_tick < 0)
            throw std::runtime_error("--acts item '" + item +
                                     "' has a negative deact tick");
        if (w.deact_tick < w.act_tick)
            throw std::runtime_error("--acts item '" + item +
                                     "' has deact tick before act tick");
        // Optional third field: the SqOut phrase note's ms offset from the
        // deactivation node, which `dump` prints for a squeeze-out activation.
        if (colon2 != std::string::npos)
            w.sqout_offset_ms =
                strict_d(rest.substr(colon2 + 1), "sqout offset", item);
        out.push_back(w);
    }
    return out;
}

// --index, whole-string. std::atoi would turn "1x" or "one" into 0 and price
// the first path as if that were what was asked for.
int parse_index(const std::string& v) {
    const std::string t = trimmed(v);
    char* end = nullptr;
    errno = 0;
    const long long n = std::strtoll(t.c_str(), &end, 10);
    if (t.empty() || end != t.c_str() + t.size() || errno == ERANGE || n < 0 ||
        n > 1000000)
        throw std::runtime_error("--index '" + v +
                                 "' is not a whole path number");
    return static_cast<int>(n);
}

// Read one path out of the JSON `dump` or `target` wrote. The conversion to
// windows itself lives in core/replay.h so tests can pin it; this only opens
// the file and picks the entry out of the "paths" array. Every failure names
// the file and the index, because "score came out wrong" is much harder to
// notice than "that file does not have a path 7".
std::vector<ReplayWindow> windows_from_file(const std::string& file, int index) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read --path file: " + file);

    json doc;
    try {
        in >> doc;
    } catch (const std::exception& e) {
        throw std::runtime_error("--path file " + file +
                                 " is not valid JSON: " + e.what());
    }
    if (!doc.is_object() || !doc.contains("paths") || !doc["paths"].is_array())
        throw std::runtime_error("--path file " + file +
                                 " has no \"paths\" array; it should be the "
                                 "JSON that dump or target writes");

    const json& paths = doc["paths"];
    if (index < 0 || static_cast<size_t>(index) >= paths.size())
        throw std::runtime_error("--path file " + file + " holds " +
                                 std::to_string(paths.size()) +
                                 " path(s), so --index " +
                                 std::to_string(index) + " is out of range");

    try {
        return windows_from_json(paths[index]);
    } catch (const std::exception& e) {
        throw std::runtime_error("--path file " + file + ", index " +
                                 std::to_string(index) + ": " + e.what());
    }
}

int cmd_score(const Args& a) {
    if (a.chart.empty()) { usage(); return 2; }
    if (!a.path.empty() && !a.acts.empty())
        throw std::runtime_error(
            "--path and --acts each name a path to score; give one or the "
            "other, not both");
    const app::Settings s = settings_from(a);

    Song song = load_songpath(a.chart, s.view_prodrums, s.effective_bass2x(),
                              s.difficulty(), s.rules);
    if (song.is_empty()) {
        std::fprintf(stderr, "chart has no notes: %s\n", a.chart.c_str());
        return 1;
    }

    std::vector<ReplayWindow> windows =
        a.path.empty() ? parse_acts(a.acts)
                       : windows_from_file(a.path, a.index);
    // A typed offset (or a dump from before sqout_tick existed) names a chord
    // only approximately. Resolve it and say which chord was used, so a typo
    // cannot quietly price a different squeeze-out. resolve_sqout_note throws
    // for a chord the engine never squeezes out; main prints "error: ..." and
    // exits 1, so nothing is priced.
    for (ReplayWindow& w : windows) {
        if (!w.sqout_offset_ms || w.sqout_tick) continue;
        const SqOutNote n = resolve_sqout_note(song, w);
        std::fprintf(stderr,
                     "note: window %lld:%lld SqOut %.2f ms -> phrase chord at "
                     "tick %lld (%.2f ms from the SP end)\n",
                     (long long)w.act_tick, (long long)w.deact_tick,
                     *w.sqout_offset_ms, (long long)n.tick, n.offset_ms);
        w.sqout_tick = n.tick;
    }
    const ReplayResult r = replay_path(song, windows, s.rules);

    // Say so when a window could be hiding a squeeze-out. The score is left
    // exactly as it is: only the player knows whether they squeezed.
    const std::vector<std::string> warnings =
        ambiguous_window_warnings(song, r, windows);
    for (const std::string& w : warnings)
        std::fprintf(stderr, "warning: %s\n", w.c_str());

    json acts = json::array();
    for (const ReplayWindow& w : windows) {
        json one{{"act_tick", w.act_tick}, {"deact_tick", w.deact_tick}};
        if (w.sqout_offset_ms) one["sqout_offset_ms"] = *w.sqout_offset_ms;
        if (w.sqout_tick) one["sqout_tick"] = *w.sqout_tick;
        acts.push_back(one);
    }

    json chords = json::array();
    for (const ReplayChord& c : r.chords) {
        json notes = json::array();
        for (const ReplayNote& n : c.notes)
            notes.push_back(json{{"color", color_str(n.color)},
                                 {"cymbal", n.cymbal},
                                 {"sp_points", n.sp_points},
                                 {"multiplier", n.multiplier},
                                 {"dynamics_bonus", n.dynamics_bonus},
                                 {"dynamic", dynamic_str(n.dynamic)}});
        json cum = score_json(c.cum);
        cum["total"] = c.cum.total();
        chords.push_back(json{
            {"index", c.index},
            {"tick", c.tick},
            {"ms", c.ms},
            {"measure", json{{"measure", c.measure},
                             {"beat", c.beat},
                             {"tick", c.measure_tick},
                             {"decimal", c.measures_decimal}}},
            {"chord_code", c.chord_code},
            {"notes", notes},
            {"is_fill", c.is_fill},
            {"is_solo", c.is_solo},
            {"is_sp_phrase_end", c.is_sp_phrase_end},
            {"combo_before", c.combo_before},
            {"multiplier", c.multiplier},
            {"multiplier_after", c.multiplier_after},
            {"in_sp", c.in_sp},
            {"points", score_json(c.points)},
            {"cum", cum},
            {"cum_onscreen_total", c.cum_onscreen_total},
        });
    }

    json final = score_json(r.final);
    final["total"] = r.final.total();

    json sections = json::array();
    for (const SongSection& sec : song.practice_sections)
        sections.push_back(json{{"tick", sec.tick},
                                {"ms", song.timing().timecode(sec.tick).ms()},
                                {"name", sec.name}});

    json out{{"chart", a.chart},
             {"chartmode", s.chartmode_key()},
             {"activations", acts},
             {"chords", chords},
             {"final", final},
             {"sections", sections},
             {"warnings", warnings}};
    // Only when the path came from a file, so a caller can tell a file-read
    // path from a hand-typed one without guessing.
    if (!a.path.empty())
        out["path_source"] = json{{"file", a.path}, {"index", a.index}};

    emit(out, a.out, a.pretty);
    return 0;
}

// ---- dump ----------------------------------------------------------------

// A private snapshot of the database. The user's Hydra.exe holds the real
// file open, and RecordStore opens read-write (it creates tables and can
// migrate), so reading the original in place could block or change it. If
// the snapshot cannot be made, this throws rather than silently falling back
// to the live file -- opening it read-write out from under a running Hydra
// is exactly what the snapshot exists to prevent.
std::string snapshot_db(const std::string& src) {
    std::error_code ec;
    const std::filesystem::path tmp = std::filesystem::temp_directory_path(ec);
    if (ec)
        throw std::runtime_error(
            "cannot make a snapshot of the database (no temp directory): " +
            ec.message() + "; refusing to open the live database " + src);

    const std::string dst =
        (tmp / ("hydra_replay_snapshot_" + std::to_string(_getpid()) + ".db"))
            .string();

    std::ifstream in(src, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read database: " + src);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error(
            "cannot make a snapshot of the database (cannot write " + dst +
            "); refusing to open the live database " + src);
    out << in.rdbuf();
    if (!out)
        throw std::runtime_error(
            "cannot make a snapshot of the database (copy to " + dst +
            " failed); refusing to open the live database " + src);
    out.close();
    std::printf("(read from a snapshot at %s)\n", dst.c_str());
    return dst;
}

// dump's JSON. Written once here because the paths can come from a stored row
// or from a fresh analysis, and both have to print the same shape.
int emit_dump(const Args& a, const app::Settings& s, const std::string& hyhash,
              const std::string& source, const HydraRecord& rec,
              const SongTiming& timing) {
    const json paths = paths_json(rec.all_paths(), timing);
    const std::string bestpath = rec.paths.empty() ? "" : rec.best_path().pathstring();
    const int64_t best = rec.paths.empty() ? 0 : rec.best_path().totalscore();

    emit(json{{"hyhash", hyhash},
              {"chartmode", s.chartmode_key()},
              {"source", source},
              {"sp_cap", rec.sp_cap ? *rec.sp_cap : -1},
              {"result", json{{"score", best}, {"bestpath", bestpath}}},
              {"paths", paths}},
         a.out, a.pretty);
    return 0;
}

int cmd_dump(const Args& a) {
    // --legacy-fills never reads the database, so it does not need one.
    if (a.chart.empty() || (a.db.empty() && !a.legacy_fills)) { usage(); return 2; }
    const app::Settings s = settings_from(a);

    const std::string hyhash = app::hash_chart_file(a.chart);
    if (hyhash.empty()) {
        std::fprintf(stderr, "cannot hash chart: %s\n", a.chart.c_str());
        return 1;
    }

    // Clone Hero 1.0 spawned fills 1.1 rejects, so a 1.0 run has to be priced
    // under 1.0's fill deadline. Every row in the database was written under
    // the 1.1 rule, which makes all of them the wrong answer here: the chart
    // is always analyzed fresh, and --no-analyze has nothing to switch off.
    // Nothing is written back — dump and target never touch a database — so
    // ADR 0010's guard against storing results from non-default settings does
    // not apply to either of them.
    if (a.legacy_fills) {
        const Song song = load_songpath(a.chart, s.view_prodrums,
                                        s.effective_bass2x(), s.difficulty(), s.rules);
        if (song.is_empty()) {
            std::fprintf(stderr, "chart has no notes: %s\n", a.chart.c_str());
            return 1;
        }
        std::fprintf(stderr,
                     "--legacy-fills: analyzing under the Clone Hero 1.0 fill "
                     "rule; the stored rows are 1.1 results and are ignored.\n");
        SearchSettings cfg = s.to_analysis_settings();
        cfg.legacy_fill_deadline = true;
        const HydraRecord rec = analyze_chart(song, cfg);
        const SongTiming& timing = song.timing();
        return emit_dump(a, s, hyhash, "analyzed-ch10", rec, timing);
    }

    const std::string snapshot_path = snapshot_db(a.db);
    // Best-effort cleanup: the snapshot is a scratch copy, not the tool's
    // output, so it should not linger in %TEMP% after the process exits.
    struct SnapshotGuard {
        std::string path;
        ~SnapshotGuard() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } snapshot_guard{snapshot_path};

    store::RecordStore store(snapshot_path, s.rules.fingerprint());
    const store::RecordKey key = s.record_key(hyhash);
    store::RecordLookup lookup = store.get_record(key);

    // NotAnalyzed and Stale both mean "no readable stored row" -- by default
    // that is not a hard failure, it just means dump runs a fresh analysis
    // instead and says so. --no-analyze keeps the old strict behaviour.
    std::string source = "record";
    HydraRecord fresh_record;
    std::optional<SongTiming> fresh_timing;
    const HydraRecord* rec_ptr = nullptr;
    const SongTiming* timing_ptr = nullptr;

    if (lookup.status == store::RecordStatus::NotAnalyzed ||
        lookup.status == store::RecordStatus::Stale) {
        if (lookup.status == store::RecordStatus::NotAnalyzed) {
            std::fprintf(stderr,
                         "NotAnalyzed: no record for %s under '%s', cap %s, "
                         "ms %s, depth %s/%d.%s\n",
                         hyhash.c_str(), s.chartmode_key().c_str(), a.cap.c_str(),
                         a.ms.c_str(), a.depth_mode.c_str(), a.depth,
                         a.no_analyze ? "" : " Analyzing the chart fresh instead.");
        } else {
            // One line per reason. The follow-up ("Re-analyze" or "Analyzing
            // fresh") goes on the last line printed.
            const char* next_step = a.no_analyze ? " Re-analyze the chart."
                                                 : " Analyzing the chart fresh instead.";
            if (lookup.stale_build)
                std::fprintf(stderr,
                             "Stale: the stored row was written by Hydra %s, not "
                             "this build; its paths cannot be read.%s\n",
                             lookup.hyversion.c_str(), lookup.stale_rules ? "" : next_step);
            if (lookup.stale_rules)
                std::fprintf(stderr,
                             "Stale: the stored row was analyzed with different rules "
                             "(hydra_rules.ini changed).%s\n",
                             next_step);
        }
        if (a.no_analyze) return 1;

        const Song fresh_song = load_songpath(a.chart, s.view_prodrums,
                                              s.effective_bass2x(), s.difficulty(), s.rules);
        if (fresh_song.is_empty()) {
            std::fprintf(stderr, "chart has no notes: %s\n", a.chart.c_str());
            return 1;
        }
        fresh_record = analyze_chart(fresh_song, s.to_analysis_settings());
        fresh_timing = fresh_song.timing();
        rec_ptr = &fresh_record;
        timing_ptr = &*fresh_timing;
        source = "analyzed";
    } else {
        if (!lookup.timing) {
            std::fprintf(stderr,
                         "the record is Ready but the song is not registered in "
                         "songmeta, so no tempo map is available.\n");
            return 1;
        }
        rec_ptr = &*lookup.record;
        timing_ptr = &*lookup.timing;
    }

    return emit_dump(a, s, hyhash, source, *rec_ptr, *timing_ptr);
}

// ---- target --------------------------------------------------------------

// "129600,296160,..." -> ticks. Strict like parse_acts: a comma-separated list
// of non-negative integers, spaces trimmed, anything else throws naming the
// offending item. A misparsed tick would silently ask the engine for a
// different path than the one the user meant.
std::vector<int64_t> parse_ticks(const std::string& spec) {
    std::vector<int64_t> out;
    size_t i = 0;
    while (i < spec.size()) {
        size_t comma = spec.find(',', i);
        if (comma == std::string::npos) comma = spec.size();
        std::string item = trimmed(spec.substr(i, comma - i));
        i = comma + 1;
        if (item.empty()) continue;
        const int64_t t = strict_ll(item, "tick", item);
        if (t < 0)
            throw std::runtime_error("--ticks item '" + item +
                                     "' is a negative tick");
        out.push_back(t);
    }
    return out;
}

int cmd_target(const Args& a) {
    if (a.chart.empty()) { usage(); return 2; }
    if (a.cap == "auto") {
        std::fprintf(stderr,
                     "target needs a fixed --cap: Auto has no single graph to "
                     "price the path against.\n");
        return 2;
    }
    const app::Settings s = settings_from(a);

    const std::string hyhash = app::hash_chart_file(a.chart);
    const std::vector<int64_t> ticks = parse_ticks(a.ticks);

    Song song = load_songpath(a.chart, s.view_prodrums, s.effective_bass2x(),
                              s.difficulty(), s.rules);
    if (song.is_empty()) {
        std::fprintf(stderr, "chart has no notes: %s\n", a.chart.c_str());
        return 1;
    }

    SearchSettings cfg = s.to_analysis_settings();
    // Price the named path under Clone Hero 1.0's fill deadline when asked.
    // target only ever prints, so ADR 0010's don't-store-non-default-settings
    // guard has nothing to guard against here.
    cfg.legacy_fill_deadline = a.legacy_fills;
    HydraRecord rec;
    rec.paths = search_target(song, cfg, ticks);

    // When the set is unrealizable, walk the prefixes to name the activation
    // that broke it. Each run is milliseconds, so this costs nothing worth
    // saving and turns "it failed" into "it failed here".
    const bool realized = !rec.paths.empty();
    int64_t failed_tick = -1;
    size_t realized_prefix = ticks.size();
    if (!realized) {
        realized_prefix = 0;
        for (size_t k = 1; k <= ticks.size(); ++k) {
            const std::vector<int64_t> prefix(ticks.begin(), ticks.begin() + k);
            if (search_target(song, cfg, prefix).empty()) break;
            realized_prefix = k;
        }
        failed_tick = realized_prefix < ticks.size() ? ticks[realized_prefix] : -1;
    }

    const std::string bestpath =
        rec.paths.empty() ? "" : rec.best_path().pathstring();
    const int64_t best = rec.paths.empty() ? 0 : rec.best_path().totalscore();

    emit(json{{"hyhash", hyhash},
              {"chartmode", s.chartmode_key()},
              {"source", "target"},
              {"sp_cap", cfg.sp_cap ? *cfg.sp_cap : -1},
              {"ticks", ticks},
              {"result", json{{"score", best}, {"bestpath", bestpath}}},
              {"paths", paths_json(rec.all_paths(), song.timing())},
              {"realized", realized},
              {"failed_tick", failed_tick},
              {"realized_prefix", realized_prefix}},
         a.out, a.pretty);
    return 0;
}

// ---- selfcheck -----------------------------------------------------------

struct Tally {
    int charts = 0;
    int paths = 0;
    int pass = 0;
    int fail = 0;
    int skipped = 0;
};

bool g_verbose = false;

void check_chart(const std::string& path, const core::Rules& rules, Tally* tally) {
    // The GUI's defaults, straight from app::Settings rather than five
    // hand-written literals, under the rules this run loaded.
    app::Settings defaults;
    defaults.rules = rules;
    const app::AnalysisSettings cfg = defaults.to_analysis_settings();

    std::optional<Song> song_opt;
    try {
        song_opt.emplace(
            load_songpath(path, cfg.prodrums, cfg.bass2x, cfg.difficulty, cfg.rules));
    } catch (const std::exception& e) {
        std::printf("SKIP %s (%s)\n", path.c_str(), e.what());
        ++tally->skipped;
        return;
    }
    const Song& song = *song_opt;
    if (song.is_empty()) { ++tally->skipped; return; }

    HydraRecord rec;
    try {
        rec = analyze_chart(song, cfg);
    } catch (const std::exception& e) {
        std::printf("SKIP %s (%s)\n", path.c_str(), e.what());
        ++tally->skipped;
        return;
    }
    ++tally->charts;

    const SongTiming& timing = song.timing();
    int index = 0, chart_fail = 0;
    for (const Path* p : rec.all_paths()) {
        const int i = index++;
        ++tally->paths;

        std::vector<ReplayWindow> windows = windows_for_path(*p, song);
        const ReplayResult r = replay_path(song, windows, rules);
        const ReplayScore want = score_of(*p);

        std::string diffs;
        if (!(r.final == want)) {
            for (const ReplayScoreField& f : kReplayScoreFields) {
                const int64_t got = r.final.*(f.member);
                const int64_t wanted = want.*(f.member);
                if (got == wanted) continue;
                if (!diffs.empty()) diffs += ", ";
                diffs += std::string(f.name) + " " + std::to_string(got) + " vs " +
                         std::to_string(wanted) + " (" + std::to_string(got - wanted) + ")";
            }
        }
        if (windows.size() != p->all_activations().size())
            diffs += (diffs.empty() ? "" : ", ") + std::string("only ") +
                     std::to_string(windows.size()) + " of " +
                     std::to_string(p->all_activations().size()) +
                     " activations resolved";

        if (g_verbose) {
            std::printf("path %d [%s] windows:", i, p->pathstring().c_str());
            for (const ReplayWindow& w : windows)
                std::printf(" %lld:%lld", (long long)w.act_tick,
                            (long long)w.deact_tick);
            std::printf("\n");
        }
        if (diffs.empty()) {
            ++tally->pass;
        } else {
            ++tally->fail;
            ++chart_fail;
            if (chart_fail <= 5) {
                std::printf("FAIL %s\n     path %d [%s]: %s\n", path.c_str(), i,
                            p->pathstring().c_str(), diffs.c_str());
                // The windows the replay used, so a mismatch names the
                // activation whose deactivation node was not recovered.
                const std::vector<Activation> acts = p->all_activations();
                for (size_t k = 0; k < acts.size(); ++k) {
                    const Activation& act = acts[k];
                    const int64_t act_tick =
                        act.timecode ? act.timecode->ticks() : -1;
                    int64_t nominal = -1;
                    if (act.timecode && act.sp_meter)
                        nominal = timing
                                      .plusmeasure(*act.timecode,
                                                   sp_bars_to_measures(*act.sp_meter))
                                      .ticks();
                    std::printf(
                        "       act %zu: tick %lld  deact %lld  nominal %lld  "
                        "sp_meter %d  skips %d  backends %zu  sqinouts %zu\n",
                        k, (long long)act_tick,
                        (long long)act.deact_tick.value_or(-1),
                        (long long)nominal, act.sp_meter.value_or(-1),
                        act.skips.value_or(-1), act.backends.size(),
                        act.sqinouts.size());
                }
            }
        }
    }
    if (chart_fail > 5)
        std::printf("     ...and %d more failing paths in this chart\n",
                    chart_fail - 5);
    std::fflush(stdout);
}

int cmd_selfcheck(const Args& a) {
    Tally tally;
    if (!a.chart.empty()) {
        check_chart(a.chart, a.rules, &tally);
    } else {
        for (const std::string& p : corpus::chart_paths()) check_chart(p, a.rules, &tally);
    }
    std::printf(
        "\nselfcheck: %d chart(s), %d path(s) — PASS %d, FAIL %d (%d chart(s) "
        "skipped)\n",
        tally.charts, tally.paths, tally.pass, tally.fail, tally.skipped);
    return tally.fail == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }

    Args a;
    a.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string k = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + k);
            return argv[++i];
        };
        try {
            if (k == "--chart") a.chart = next();
            else if (k == "--db") a.db = next();
            else if (k == "--rules") a.rules_path = next();
            else if (k == "--out") a.out = next();
            else if (k == "--cap") a.cap = next();
            else if (k == "--ms") a.ms = next();
            else if (k == "--depth-mode") a.depth_mode = next();
            else if (k == "--depth") a.depth = std::atoi(next().c_str());
            else if (k == "--acts") a.acts = next();
            else if (k == "--path") a.path = next();
            // Parsed strictly: std::atoi turns a typo into 0, which would
            // quietly price the wrong path instead of saying anything.
            else if (k == "--index") a.index = parse_index(next());
            else if (k == "--ticks") a.ticks = next();
            else if (k == "--verbose") g_verbose = true;
            else if (k == "--prodrums") a.prodrums = flag_bool(next());
            else if (k == "--bass2x") a.bass2x = flag_bool(next());
            else if (k == "--difficulty") a.difficulty = next();
            else if (k == "--pretty") a.pretty = true;
            else if (k == "--no-analyze") a.no_analyze = true;
            else if (k == "--legacy-fills") a.legacy_fills = true;
            else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); usage(); return 2; }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s\n", e.what());
            return 2;
        }
    }

    try {
        a.rules = app::load_rules_file(a.rules_path.empty()
                                           ? app::default_rules_path()
                                           : std::filesystem::u8path(a.rules_path));
    } catch (const app::RulesFileError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }

    try {
        if (a.command == "score") return cmd_score(a);
        if (a.command == "dump") return cmd_dump(a);
        if (a.command == "target") return cmd_target(a);
        if (a.command == "selfcheck") return cmd_selfcheck(a);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    usage();
    return 2;
}
