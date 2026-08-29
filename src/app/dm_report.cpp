#include "app/dm_report.h"

#include <cctype>
#include <cstdio>
#include <unordered_map>

#include "app/html_page.h"
#include "app/report.h"  // report::plain — strips Clone Hero <color> markup

namespace hydra::app::dm_report {

using html::json_escape_into;

namespace {

// The per-page pieces of the comparison page; the shared skeleton (theme +
// chrome + table CSS, the sort machinery, the deferred first render) lives
// in app/html_page.cpp (html::kSortable*), canon from the path report. The
// columns, chip colors, body, and script below are this report's own.
// __SUBTITLE__/__FOOTER__/__DATA__ are filled by build_dm_html.
const char* const kTitle = R"page(<title>Hydra vs dmleaderboards</title>
<style>
)page";

const char* const kCssColumns = R"page(
td.trunc { overflow: hidden; text-overflow: ellipsis; }
.song { font-weight: 550; max-width: 260px; overflow: hidden; text-overflow: ellipsis; }
td.artist { max-width: 170px; }
td.charter { max-width: 150px; }
.dim { color: var(--muted); }
.pos { color: var(--t0); }
.neg { color: var(--t3); }

)page";

const char* const kChipColors = R"page(.s-matched{color:var(--t0)} .s-above{color:var(--t1)} .s-unmatched{color:var(--tn); border-color:transparent}
)page";

const char* const kBody = R"page(<div class="wrap">
  <header>
    <h1>Hydra <span class="accent">vs dmleaderboards</span></h1>
    <div class="sub">__SUBTITLE__</div>
  </header>

  <div class="stats" id="stats"></div>

  <div class="controls">
    <span class="sorter">
      <label for="sortby">Sort by</label>
      <select id="sortby"></select>
      <button id="sortdir" type="button" title="Switch between highest-first and lowest-first"></button>
    </span>
    <input type="search" id="q" placeholder="Search song, artist, or charter">
    <select id="status">
      <option value="">All charts</option>
      <option value="matched">Matched</option>
      <option value="above optimal">Above optimal</option>
      <option value="unmatched">Unmatched (not in library)</option>
    </select>
    <span class="count" id="count"></span>
  </div>

  <div class="tablewrap">
    <table>
      <thead><tr id="head"></tr></thead>
      <tbody id="body"></tbody>
    </table>
    <div class="empty" id="empty">Joining scores&hellip;</div>
  </div>

  <footer>__FOOTER__</footer>
</div>

)page";

const char* const kDataJs = R"page(<script id="data" type="application/json">__DATA__</script>
<script>
const ROWS = JSON.parse(document.getElementById('data').textContent);

const COLS = [
  {k:'song',    t:'Song',      num:false},
  {k:'artist',  t:'Artist',    num:false},
  {k:'charter', t:'Charter',   num:false},
  {k:'actual',  t:'Actual',    num:true},
  {k:'optimal', t:'Hydra opt', num:true},
  {k:'delta',   t:'Points left', num:true},
  {k:'pct',     t:'% of opt',  num:true},
  {k:'fc',      t:'FC',        num:true},
  {k:'percent', t:'Percent',   num:true},
  {k:'speed',   t:'Speed',     num:true},
  {k:'rank',    t:'Rank',      num:true},
  {k:'posted',  t:'Posted',    num:false},
  {k:'status',  t:'Status',    num:false},
];

let sortKey = 'delta', sortDir = -1;

)page";

const char* const kPageJs = R"page(const STATUS_CLASS = {'matched':'s-matched', 'above optimal':'s-above', 'unmatched':'s-unmatched'};

function visible() {
  const q = document.getElementById('q').value.trim().toLowerCase();
  const status = document.getElementById('status').value;
  return ROWS.filter(r => {
    if (status && r.status !== status) return false;
    if (!q) return true;
    return (r.song + ' ' + r.artist + ' ' + r.charter).toLowerCase().includes(q);
  });
}

function render() {
  const rows = visible();
  const dir = sortDir;
  rows.sort((a, b) => {
    let x = a[sortKey], y = b[sortKey];
    if (x === null || x === undefined) return 1;
    if (y === null || y === undefined) return -1;
    if (typeof x === 'string') return dir * x.localeCompare(y);
    return dir * (x - y);
  });

  document.querySelectorAll('#head th').forEach((th, i) => {
    const c = COLS[i];
    if (c.k === sortKey) th.setAttribute('aria-sort', dir === 1 ? 'ascending' : 'descending');
    else th.removeAttribute('aria-sort');
    th.querySelector('.arrow').textContent =
      c.k === sortKey ? (dir === 1 ? '↑' : '↓') : '⇅';
  });

  const body = document.getElementById('body');
  body.textContent = '';
  const frag = document.createDocumentFragment();

  for (const r of rows) {
    const tr = document.createElement('tr');
    const deltaCls = r.delta === null || r.delta === undefined ? 'num dim'
                   : (r.delta < 0 ? 'num neg' : 'num');
    const deltaTxt = r.delta === null || r.delta === undefined ? '—'
                   : (r.delta < 0 ? '+' + (-r.delta).toLocaleString() + ' over' : fmt(r.delta));
    const cells = [
      ['song trunc', r.song],
      ['dim trunc artist', r.artist],
      ['dim trunc charter', r.charter],
      ['num', fmt(r.actual)],
      ['num', fmt(r.optimal)],
      [deltaCls, deltaTxt],
      ['num', r.pct === null || r.pct === undefined ? '—' : r.pct.toFixed(2) + '%'],
      ['num', r.fc ? '✓' : '—'],
      ['num', r.percent + '%'],
      ['num', r.speed + '%'],
      ['num', r.rank === null || r.rank === undefined ? '—' : '#' + r.rank],
      ['dim', r.posted ? r.posted.slice(0, 10) : '—'],
      ['status', null],
    ];

    cells.forEach(([cls, val]) => {
      const td = document.createElement('td');
      if (cls === 'status') {
        const chip = document.createElement('span');
        chip.className = 'chip ' + (STATUS_CLASS[r.status] || 's-unmatched');
        chip.textContent = r.status;
        td.appendChild(chip);
      } else {
        td.className = cls;
        td.textContent = val;
        if (cls.includes('trunc') && val) td.title = val;
      }
      tr.appendChild(td);
    });
    frag.appendChild(tr);
  }
  body.appendChild(frag);

  const empty = document.getElementById('empty');
  empty.textContent = 'Nothing matches those filters.';
  empty.hidden = rows.length > 0;
  document.getElementById('count').textContent =
    rows.length.toLocaleString() + ' of ' + ROWS.length.toLocaleString() + ' scores';

  renderStats(rows);
}

function renderStats(rows) {
  const matched = rows.filter(r => r.status === 'matched');
  const above = rows.filter(r => r.status === 'above optimal');
  const unmatched = rows.filter(r => r.status === 'unmatched');
  const withPct = rows.filter(r => r.pct !== null && r.pct !== undefined);
  const avgPct = withPct.length
    ? (withPct.reduce((a, r) => a + r.pct, 0) / withPct.length).toFixed(2) + '%' : '—';
  const left = matched.reduce((a, r) => a + (r.delta > 0 ? r.delta : 0), 0);

  const stats = [
    ['Scores', rows.length.toLocaleString()],
    ['Matched', matched.length.toLocaleString()],
    ['Above optimal', above.length.toLocaleString()],
    ['Unmatched', unmatched.length.toLocaleString()],
    ['Avg % of optimal', avgPct],
    ['Points left on table', left.toLocaleString()],
  ];

  const el = document.getElementById('stats');
  el.textContent = '';
  for (const [k, v] of stats) {
    const d = document.createElement('div');
    d.className = 'stat';
    const kk = document.createElement('div'); kk.className = 'stat-k'; kk.textContent = k;
    const vv = document.createElement('div'); vv.className = 'stat-v'; vv.textContent = v;
    d.append(kk, vv);
    el.appendChild(d);
  }
}

document.getElementById('q').addEventListener('input', render);
document.getElementById('status').addEventListener('change', render);

)page";

// The page shell, concatenated once on first use.
const std::string& page_template() {
    static const std::string page = std::string(html::kSortableHead) + kTitle +
                                    html::kSortableCssCore +
                                    html::kSortableCssTable + kCssColumns +
                                    html::kSortableCssChip + kChipColors +
                                    html::kSortableCssTail + kBody + kDataJs +
                                    html::kSortableJsSorter + kPageJs +
                                    html::kSortableJsBoot;
    return page;
}


std::string lower_hex(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::vector<DmReportRow> collect_dm_rows(store::RecordStore& store,
                                         const std::vector<net::DmScore>& scores,
                                         const std::string& chartmode,
                                         const store::Lens& lens) {
    // One query for every stored record in this chartmode, indexed by hash.
    // Only 4-bar records: the leaderboard plays by Clone Hero's rules, and a
    // what-if cap's score would read as "above optimal" nonsense.
    std::unordered_map<std::string, store::RecordListing> by_hash;
    for (store::RecordListing& r :
         store.list_records(chartmode, store::CapQuery::at(kCloneHeroSpCap), lens,
                            store::SortColumn::Score, /*descending=*/true)) {
        by_hash.emplace(lower_hex(r.hyhash), std::move(r));
    }

    std::vector<DmReportRow> rows;
    rows.reserve(scores.size());
    for (const net::DmScore& s : scores) {
        DmReportRow row;
        row.identifier = s.identifier;
        row.actual = s.score;
        row.is_fc = s.is_fc;
        row.percent = s.percent;
        row.speed = s.speed;
        row.rank = s.rank;
        row.posted = s.posted;

        auto it = by_hash.find(s.identifier);
        const store::RecordListing* rec = it != by_hash.end() ? &it->second : nullptr;

        // Identity: the leaderboard's own metadata when it has it, else the
        // matched Hydra record's, else the "Unknown Song: <hash>" placeholder.
        if (s.known && !s.song_name.empty()) {
            row.song = s.song_name;
            row.artist = s.artist;
            row.charter = s.charter;
        } else if (rec) {
            row.song = report::plain(rec->ref_name);
            row.artist = report::plain(rec->ref_artist);
            row.charter = report::plain(rec->ref_charter);
        } else {
            row.song = s.song_name;
            row.artist = s.artist;
            row.charter = s.charter;
        }
        if (row.charter.empty() && rec) row.charter = report::plain(rec->ref_charter);

        if (rec && rec->summary.score) {
            int64_t opt = *rec->summary.score;
            row.optimal = opt;
            row.delta = opt - s.score;
            if (s.speed == 100 && opt > 0)
                row.pct = static_cast<double>(s.score) / static_cast<double>(opt) * 100.0;
            row.status = s.score > opt ? "above optimal" : "matched";
        } else {
            row.status = "unmatched";
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string build_dm_html(const std::vector<DmReportRow>& rows, const std::string& subtitle,
                          const std::string& footer) {
    std::string data;
    data.reserve(rows.size() * 200 + 2);
    data.push_back('[');
    bool first = true;
    char num[32];
    for (const DmReportRow& r : rows) {
        if (!first) data.push_back(',');
        first = false;

        data += "{\"song\":";
        json_escape_into(data, r.song);
        data += ",\"artist\":";
        json_escape_into(data, r.artist);
        data += ",\"charter\":";
        json_escape_into(data, r.charter);
        data += ",\"actual\":" + std::to_string(r.actual);
        data += ",\"optimal\":" + (r.optimal ? std::to_string(*r.optimal) : std::string("null"));
        data += ",\"delta\":" + (r.delta ? std::to_string(*r.delta) : std::string("null"));
        if (r.pct) {
            std::snprintf(num, sizeof(num), "%.4f", *r.pct);
            data += ",\"pct\":" + std::string(num);
        } else {
            data += ",\"pct\":null";
        }
        data += ",\"fc\":" + std::string(r.is_fc ? "1" : "0");
        data += ",\"percent\":" + std::to_string(r.percent);
        data += ",\"speed\":" + std::to_string(r.speed);
        data += ",\"rank\":" + (r.rank ? std::to_string(*r.rank) : std::string("null"));
        data += ",\"posted\":";
        json_escape_into(data, r.posted);
        data += ",\"status\":";
        json_escape_into(data, r.status);
        data.push_back('}');
    }
    data.push_back(']');
    return html::render_page(page_template().c_str(), std::move(data), subtitle,
                             footer);
}

DmReportStats tally_dm_rows(const std::vector<DmReportRow>& rows) {
    DmReportStats stats;
    stats.total = static_cast<int>(rows.size());
    for (const DmReportRow& r : rows) {
        if (r.status == "matched") ++stats.matched;
        else if (r.status == "above optimal") ++stats.above;
        else ++stats.unmatched;
    }
    return stats;
}

GeneratedDmReport generate_dm_report(store::RecordStore& store,
                                     const std::vector<net::DmScore>& scores,
                                     const std::string& chartmode,
                                     const store::Lens& lens,
                                     const std::string& username) {
    GeneratedDmReport out;
    std::vector<DmReportRow> rows = collect_dm_rows(store, scores, chartmode, lens);
    out.stats = tally_dm_rows(rows);
    if (rows.empty()) return out;

    std::string subtitle =
        username + " — " + group_thousands(out.stats.total) + " scores: " +
        group_thousands(out.stats.matched) + " matched, " +
        group_thousands(out.stats.above) + " above optimal, " +
        group_thousands(out.stats.unmatched) + " not in your library";
    std::string footer =
        "Actual scores from dmleaderboards.com against Hydra's optimal for " + chartmode +
        ". Above-optimal scores are expected — Hydra's optimal excludes several score "
        "backends, and older Clone Hero versions allowed fills that are impossible now.";
    out.html = build_dm_html(rows, subtitle, footer);
    return out;
}

}  // namespace hydra::app::dm_report
