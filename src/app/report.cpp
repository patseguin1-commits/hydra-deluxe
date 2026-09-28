#include "app/report.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>

#include "app/display_format.h"
#include "app/html_page.h"
#include "core/squeeze_rating.h"
#include "core/strutil.h"
#include "parse/song.h"

namespace hydra::app::report {

using html::json_escape_into;

namespace {

// The path report's own pieces. The stylesheet and the script that sorts,
// filters and draws the table are shared with the other two report pages
// (html::page_template, docs/adr/0016).
const char* const kTitle = "Hydra Path Index";

const char* const kBody = R"page(<div class="wrap">
  <header>
    <h1>Hydra <span class="accent">Path Index</span></h1>
    <div class="sub">__SUBTITLE__</div>
  </header>

  <div class="stats" id="stats"></div>

  <div class="controls">
    <span class="sorter">
      <label for="sortby">Sort by</label>
      <select id="sortby"></select>
      <button id="sortdir" type="button" title="Switch between highest-first and lowest-first"></button>
    </span>
    <input type="search" id="q" aria-label="Search paths" placeholder="Search song, artist, charter, or path notation">
    <select id="tier" aria-label="Timing tier">
      <option value="">All timing tiers</option>
    </select>
    <label class="toggle"><input type="checkbox" id="bestonly" checked> Best path only</label>
    <span class="count" id="count"></span>
  </div>

  <div class="tablewrap">
    <table>
      <thead><tr id="head"></tr></thead>
      <tbody id="body"></tbody>
    </table>
    <div class="empty" id="empty">Reading paths&hellip;</div>
  </div>

  <footer>
    <p>__FOOTER__</p>
    <dl class="legend" id="legend"></dl>
  </footer>
</div>

)page";

// The payload is {hit_window, tiers, rows}. The tier dropdown and the
// "Past N ms" tile read the tier table, so they always match the bands the
// rows were labeled with.
const char* const kPageJs = R"page(const BEYOND = Math.max(...DATA.tiers.filter(t => t.cutoff !== null).map(t => t.cutoff));

// One name per tier, for both the dropdown and the chips, so a row's chip
// reads the same words as the filter that finds it.
function tierLabel(name) {
  return name === 'Beyond' ? 'Beyond ' + BEYOND + ' ms'
       : name === 'None' ? 'No squeezes'
       : name;
}

// The tier dropdown mirrors the bands the rows were labeled with.
{
  const sel = document.getElementById('tier');
  for (const t of DATA.tiers) {
    const o = document.createElement('option');
    o.value = t.name;
    o.textContent = tierLabel(t.name);
    sel.appendChild(o);
  }
}

const PAGE = {
  rows: DATA.rows,
  noun: 'paths',
  sortKey: 'score',
  sortDir: -1,
  cols: [
    {k:'song',    t:'Song',     num:false},
    {k:'artist',  t:'Artist',   num:false},
    {k:'charter', t:'Charter',  num:false},
    {k:'mode',    t:'Mode',     num:false, d:'The difficulty and drum options the path was found for.'},
    {k:'path',    t:'Path',     num:false, d:'The path in path notation: one entry per activation, with its skip count and squeeze symbols.'},
    {k:'score',   t:'Score',    num:true,  d:'The total score the path reaches.'},
    {k:'acts',    t:'Acts',     num:true,  d:'Activations: how many times the path uses Star Power.'},
    {k:'skip',    t:'Max skip', num:true,  d:'The most fills any one activation passes over before activating.'},
    {k:'ms',      t:'Hardest ms', num:true, d:'The hardest squeeze or early fill the path needs, in raw ms. A dash means it needs none.'},
    {k:'tier',    t:'Timing',   num:false, d:'How hard Hardest ms is, in bands of your hit window. Beyond means at least twice the hit window.'},
    {k:'efill',   t:'Early fill (ms)', num:true, d:'The hardest early fill (E0) on the path: how many ms early you must hit to summon the fill. Negative means slack. A dash means the path has none.'},
    {k:'mult',    t:'Avg multiplier', num:true, d:'Points per note on average: the score without solo bonuses divided by the base score (every note at 1x).'},
    {k:'sqin',    t:'SqIn',     num:true,  d:'SP phrase notes squeezed into an active Star Power window (+ in the path).'},
    {k:'sqout',   t:'SqOut',    num:true,  d:'SP phrase notes squeezed out of an active Star Power window (- in the path).'},
    {k:'notes',   t:'Notes',    num:true,  d:'Notes in the chart.'},
  ],
  controls: [['q', 'input'], ['tier', 'change'], ['bestonly', 'change']],
  filter(q) {
    const tier = document.getElementById('tier').value;
    const bestOnly = document.getElementById('bestonly').checked;
    return r => {
      if (bestOnly && r.rank !== 1) return false;
      if (tier && r.tier !== tier) return false;
      if (!q) return true;
      return (r.song + ' ' + r.artist + ' ' + r.charter + ' ' + r.path).toLowerCase().includes(q);
    };
  },
  rowClass: r => r.rank === 1 ? 'best' : '',
  cells: r => [
    ['song trunc', r.song],
    ['dim trunc artist', r.artist],
    ['dim trunc charter', r.charter],
    ['dim trunc mode', r.mode],
    ['path mono trunc', r.path],
    ['num', fmt(r.score)],
    ['num', r.acts],
    ['num', r.skip],
    ['num', fmtMs(r.ms)],
    ['chip ' + r.tok, tierLabel(r.tier), 'chip'],
    ['num', fmtMs(r.efill)],
    ['num', r.mult.toFixed(3)],
    ['num', r.sqin],
    ['num', r.sqout],
    ['num', fmt(r.notes)],
  ],
  stats(rows) {
    const withMs = rows.filter(r => r.ms !== null && r.ms !== undefined);
    const tightest = withMs.length ? Math.max(...withMs.map(r => r.ms)) : null;
    const maxSkip = rows.length ? Math.max(...rows.map(r => r.skip)) : 0;
    const beyond = rows.filter(r => r.ms !== null && r.ms >= BEYOND).length;
    return [
      ['Charts', new Set(rows.map(r => r.c)).size.toLocaleString()],
      ['Paths shown', rows.length.toLocaleString()],
      ['Tightest squeeze', tightest === null ? DASH : tightest.toFixed(1) + ' ms'],
      ['Past ' + BEYOND + ' ms', beyond.toLocaleString()],
      ['Highest skip', maxSkip],
    ];
  },
};
)page";

// The page shell, built once on first use.
const std::string& page_template() {
    static const std::string page = html::page_template(kTitle, kBody, kPageJs);
    return page;
}

// repr(float) / json.dumps float formatting for the page payload.
std::string py_repr(double v) {
    // std::to_chars with no precision produces the shortest string that
    // round-trips -- the same contract as CPython's float repr. The one
    // cosmetic difference: Python prints integral floats as "140.0" where
    // to_chars gives "140".
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    std::string s(buf, res.ptr);
    if (s.find_first_of(".eE") == std::string::npos &&
        s.find_first_of("0123456789") != std::string::npos)
        s += ".0";
    return s;
}

}  // namespace

std::string plain(const std::string& text) {
    if (text.empty()) return text;

    // re.sub(r'</?color[^>]*>', '', text, flags=IGNORECASE)
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<') {
            size_t j = i + 1;
            if (j < text.size() && text[j] == '/') ++j;
            static const char* kWord = "color";
            bool word = true;
            for (int k = 0; k < 5; ++k) {
                if (j + k >= text.size() ||
                    std::tolower(static_cast<unsigned char>(text[j + k])) != kWord[k]) {
                    word = false;
                    break;
                }
            }
            if (word) {
                size_t close = text.find('>', j + 5);
                if (close != std::string::npos) {
                    i = close + 1;  // drop the whole tag
                    continue;
                }
            }
        }
        out.push_back(text[i]);
        ++i;
    }

    return trim(out);
}

std::string counted(int64_t n, const char* one, const char* many) {
    return group_thousands(n) + " " + (n == 1 ? one : many);
}

std::pair<std::string, std::string> tier_for(const std::optional<double>& ms,
                                             double hit_window_ms) {
    // The ladder itself lives in core/squeeze_rating.h (timing_tiers) so
    // these labels and the page's embedded tier table cannot drift apart.
    return tier_for(ms, timing_tiers(hit_window_ms));
}

std::pair<std::string, std::string> tier_for(const std::optional<double>& ms,
                                             const std::vector<TimingTier>& tiers) {
    // The two open bands are the table's last two entries: "Beyond", then
    // the "None" (no squeeze) entry.
    const TimingTier& none = tiers.back();
    const TimingTier& beyond = tiers[tiers.size() - 2];
    if (!ms) return {none.name, none.tok};
    for (const TimingTier& t : tiers)
        if (t.cutoff && *ms < *t.cutoff) return {t.name, t.tok};
    return {beyond.name, beyond.tok};
}

std::unordered_map<std::string, store::RecordListing> records_by_hash(
    store::RecordStore& store, const std::string& chartmode, const store::CapQuery& cap,
    const store::Lens& lens) {
    std::unordered_map<std::string, store::RecordListing> by_hash;
    for (store::RecordListing& r : store.list_records(chartmode, cap, lens,
                                                       store::SortColumn::Score,
                                                       /*descending=*/true))
        by_hash.emplace(to_lower_ascii(r.hyhash), std::move(r));
    return by_hash;
}

std::vector<ReportRow> collect_rows(store::RecordStore& store, int64_t max_paths,
                                    const store::CapQuery& cap, const store::Lens& lens,
                                    double hit_window_ms,
                                    const std::atomic<bool>* cancel) {
    std::vector<ReportRow> rows;
    // Built once for the whole report, not once per row.
    const std::vector<TimingTier> tiers = timing_tiers(hit_window_ms);

    store.for_each_blob(std::nullopt, cap, lens,
                        [&](const store::RecordStore::BlobRow& meta,
                            const HydraRecord* record) {
        // Only rows the store calls Ready have a decoded record; anything
        // else (a stale stamp) is skipped, as record.is_version_compatible()
        // did in Python.
        if (!record) return;

        std::vector<const Path*> paths = record->all_paths();
        std::stable_sort(paths.begin(), paths.end(), [](const Path* a, const Path* b) {
            return a->totalscore() > b->totalscore();
        });

        int64_t shown = std::min<int64_t>(max_paths, static_cast<int64_t>(paths.size()));
        for (int64_t idx = 0; idx < shown; ++idx) {
            const Path* path = paths[static_cast<size_t>(idx)];
            store::PathSummary s = store::summarize_path(*path);
            auto [label, token] = tier_for(s.hardest_ms, tiers);

            ReportRow row;
            row.song = title_or_unknown(plain(meta.ref_name));
            row.artist = plain(meta.ref_artist);
            row.charter = plain(meta.ref_charter);
            row.mode = meta.chartmode;
            row.rank = static_cast<int>(idx + 1);
            row.path = path->pathstring();
            row.score = *s.score;
            row.acts = *s.actcount;
            row.skip = *s.maxskip;
            row.ms = s.hardest_ms;
            row.tier = label;
            row.tok = token;
            for (const Activation& a : path->walk_activations()) {
                std::optional<double> ediff = a.e_difficulty();
                if (ediff.has_value() && (!row.efill || *ediff > *row.efill))
                    row.efill = *ediff;
            }
            row.mult = py_round3(*s.avgmult);
            row.sqin = *s.sqin_count;
            row.sqout = *s.sqout_count;
            row.notes = *s.notecount;
            row.hyhash = meta.hyhash;
            rows.push_back(std::move(row));
        }
    }, cancel);
    return rows;
}

std::string build_html(const std::vector<ReportRow>& rows, const std::string& subtitle,
                       const std::string& footer, double hit_window_ms) {
    // The payload: {hit_window, tiers, rows}. The page builds its tier
    // dropdown and the stats tiles from hit_window/tiers, so the embedded UI
    // can never drift from the bands the rows were labeled with.
    std::string data;
    data.reserve(rows.size() * 160 + 256);
    data += "{\"hit_window\":" + py_repr(hit_window_ms);
    data += ",\"tiers\":[";
    {
        bool first_tier = true;
        for (const TimingTier& t : timing_tiers(hit_window_ms)) {
            if (!first_tier) data.push_back(',');
            first_tier = false;
            data += "{\"name\":";
            json_escape_into(data, t.name);
            data += ",\"tok\":\"";
            data += t.tok;
            data += "\",\"cutoff\":" +
                    (t.cutoff ? py_repr(*t.cutoff) : std::string("null"));
            data.push_back('}');
        }
    }
    data += "],\"rows\":[";
    // A small number per chart, in order of first appearance: the Charts
    // tile counts distinct charts by it, the way the subtitle counts chart
    // hashes, without the 32-character hash on every row.
    std::unordered_map<std::string, int> chart_ids;
    bool first_row = true;
    for (const ReportRow& r : rows) {
        if (!first_row) data.push_back(',');
        first_row = false;
        const int chart_id =
            chart_ids.emplace(r.hyhash, static_cast<int>(chart_ids.size())).first->second;

        data += "{\"c\":" + std::to_string(chart_id);
        data += ",\"song\":";
        json_escape_into(data, r.song);
        data += ",\"artist\":";
        json_escape_into(data, r.artist);
        data += ",\"charter\":";
        json_escape_into(data, r.charter);
        data += ",\"mode\":";
        json_escape_into(data, r.mode);
        data += ",\"rank\":" + std::to_string(r.rank);
        data += ",\"path\":";
        json_escape_into(data, r.path);
        data += ",\"score\":" + std::to_string(r.score);
        data += ",\"acts\":" + std::to_string(r.acts);
        data += ",\"skip\":" + std::to_string(r.skip);
        data += ",\"ms\":" + (r.ms ? py_repr(*r.ms) : std::string("null"));
        data += ",\"tier\":";
        json_escape_into(data, r.tier);
        data += ",\"tok\":";
        json_escape_into(data, r.tok);
        data += ",\"efill\":" + (r.efill ? py_repr(*r.efill) : std::string("null"));
        data += ",\"mult\":" + py_repr(r.mult);
        data += ",\"sqin\":" + std::to_string(r.sqin);
        data += ",\"sqout\":" + std::to_string(r.sqout);
        data += ",\"notes\":" + std::to_string(r.notes);
        data.push_back('}');
    }
    data += "]}";
    return html::render_page(page_template().c_str(), std::move(data), subtitle,
                             footer);
}

GeneratedReport generate_report(store::RecordStore& store,
                                const ReportOptions& options) {
    GeneratedReport out;
    const double w = static_cast<double>(options.hit_window_ms);
    std::vector<ReportRow> rows =
        collect_rows(store, options.max_paths, options.cap, options.lens, w, options.cancel);
    // Cancelled part-way through: whatever the walk collected is a partial
    // library, so nothing is built from it. An empty result says "no report",
    // and the caller that set the flag already knows why.
    if (options.cancel && options.cancel->load()) return GeneratedReport{};
    out.rows = static_cast<int64_t>(rows.size());
    if (rows.empty()) return out;
    // The subtitle counts what the page lists: every record on it has exactly
    // one rank-1 row, and its songs are the distinct charts among the rows.
    std::unordered_set<std::string> songs;
    for (const ReportRow& r : rows) {
        if (r.rank == 1) ++out.records;
        songs.insert(r.hyhash);
    }
    out.songs = static_cast<int64_t>(songs.size());

    std::string shown = options.max_paths > kEveryPathLabelThreshold
                            ? "every path"
                            : "top " + std::to_string(options.max_paths) +
                                  " paths per chart";
    std::string cap_label = "SP cap " + std::to_string(options.cap.exact) + " bars";
    std::string subtitle = counted(out.records, "record", "records") + " across " +
                           counted(out.songs, "chart", "charts") + " — " + shown + " — " +
                           cap_label;
    std::string dbname =
        std::filesystem::u8path(options.db_path).filename().u8string();
    std::string footer = "Generated from " + dbname +
                         ". Timing tiers match Hydra's squeeze ratings; "
                         "'Beyond' is past the " +
                         std::to_string(static_cast<int64_t>(beyond_edge_ms(w))) +
                         " ms window.";
    out.html = build_html(rows, subtitle, footer, w);
    return out;
}

}  // namespace hydra::app::report
