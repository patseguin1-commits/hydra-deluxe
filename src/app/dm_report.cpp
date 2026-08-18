#include "app/dm_report.h"

#include <cctype>
#include <cstdio>
#include <unordered_map>

#include "app/html_page.h"
#include "app/report.h"  // report::plain — strips Clone Hero <color> markup

namespace hydra::app::dm_report {

using html::json_escape_into;

namespace {

// The page shell. The CSS block is lifted verbatim from app/report.cpp's kPage
// (theme-neutral, light/dark aware); the body, columns, and script are this
// report's own. __SUBTITLE__/__FOOTER__/__DATA__ are filled by build_dm_html.
const char* const kPage = R"page(<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Hydra vs dmleaderboards</title>
<style>
:root {
  color-scheme: light dark;
  --paper: #faf9f7;
  --surface: #ffffff;
  --raised: #f2f0ec;
  --ink: #15171d;
  --muted: #6a6e79;
  --rule: #e3e1db;
  --sp: #b07d0a;
  --sp-soft: #f6e7c2;
  --t0: #2c7a5e; --t1: #9a7a1e; --t2: #b85f2c; --t3: #b23c3c; --t4: #8e3070; --t5: #5b3fa8;
  --tn: #9aa0ab;
  --shadow: 0 1px 2px rgba(20,22,28,.06), 0 8px 24px rgba(20,22,28,.05);
}
@media (prefers-color-scheme: dark) {
  :root {
    --paper: #101219; --surface: #171a22; --raised: #1e222c;
    --ink: #e9e7e2; --muted: #8f95a1; --rule: #282d39;
    --sp: #f0b429; --sp-soft: #3a2e12;
    --t0: #4fbf94; --t1: #e0b13f; --t2: #f0894e; --t3: #f2686b; --t4: #e07ac0; --t5: #a78bfa;
    --tn: #5c626e;
    --shadow: 0 1px 2px rgba(0,0,0,.4), 0 8px 24px rgba(0,0,0,.3);
  }
}
:root[data-theme="dark"] {
  --paper: #101219; --surface: #171a22; --raised: #1e222c;
  --ink: #e9e7e2; --muted: #8f95a1; --rule: #282d39;
  --sp: #f0b429; --sp-soft: #3a2e12;
  --t0: #4fbf94; --t1: #e0b13f; --t2: #f0894e; --t3: #f2686b; --t4: #e07ac0; --t5: #a78bfa;
  --tn: #5c626e;
  --shadow: 0 1px 2px rgba(0,0,0,.4), 0 8px 24px rgba(0,0,0,.3);
}
:root[data-theme="light"] {
  --paper: #faf9f7; --surface: #ffffff; --raised: #f2f0ec;
  --ink: #15171d; --muted: #6a6e79; --rule: #e3e1db;
  --sp: #b07d0a; --sp-soft: #f6e7c2;
  --t0: #2c7a5e; --t1: #9a7a1e; --t2: #b85f2c; --t3: #b23c3c; --t4: #8e3070; --t5: #5b3fa8;
  --tn: #9aa0ab;
  --shadow: 0 1px 2px rgba(20,22,28,.06), 0 8px 24px rgba(20,22,28,.05);
}

* { box-sizing: border-box; }
body {
  margin: 0;
  background: var(--paper);
  color: var(--ink);
  font-family: ui-sans-serif, system-ui, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  font-size: 14px;
  line-height: 1.5;
}
.mono, td.num, .stat-v {
  font-family: ui-monospace, "Cascadia Mono", "Consolas", "SF Mono", Menlo, monospace;
  font-variant-numeric: tabular-nums;
}

.wrap { max-width: 1760px; margin: 0 auto; padding: 28px 20px 64px; display: flex; flex-direction: column; gap: 20px; }

header { display: flex; flex-direction: column; gap: 6px; }
h1 { margin: 0; font-size: 20px; font-weight: 650; letter-spacing: -.01em; }
h1 .accent { color: var(--sp); }
.sub { color: var(--muted); font-size: 13px; }

.stats { display: flex; flex-wrap: wrap; gap: 10px; }
.stat {
  background: var(--surface); border: 1px solid var(--rule); border-radius: 8px;
  padding: 10px 14px; min-width: 116px; box-shadow: var(--shadow);
}
.stat-k { font-size: 10px; text-transform: uppercase; letter-spacing: .09em; color: var(--muted); }
.stat-v { font-size: 19px; font-weight: 600; margin-top: 3px; }

.controls { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
input[type="search"], select, button {
  font: inherit; color: var(--ink); background: var(--surface);
  border: 1px solid var(--rule); border-radius: 7px; padding: 8px 11px;
}
input[type="search"] { min-width: 220px; flex: 1 1 220px; }
button { cursor: pointer; }
button:hover, select:hover { border-color: var(--sp); }

.sorter { display: inline-flex; align-items: center; gap: 6px; }
.sorter label { color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: .08em; }
#sortdir { min-width: 108px; text-align: left; }
input:focus-visible, select:focus-visible, th:focus-visible, button:focus-visible {
  outline: 2px solid var(--sp); outline-offset: 2px;
}
.count { color: var(--muted); font-size: 13px; margin-left: auto; }

.tablewrap {
  overflow-x: auto; background: var(--surface);
  border: 1px solid var(--rule); border-radius: 10px; box-shadow: var(--shadow);
  scrollbar-color: var(--muted) transparent;
}
.tablewrap::-webkit-scrollbar { height: 12px; }
.tablewrap::-webkit-scrollbar-thumb { background: var(--rule); border-radius: 6px; }
.tablewrap::-webkit-scrollbar-thumb:hover { background: var(--muted); }
table { border-collapse: separate; border-spacing: 0; width: 100%; }
thead th {
  position: sticky; top: 0; z-index: 2;
  background: var(--raised); color: var(--muted);
  font-size: 10px; text-transform: uppercase; letter-spacing: .08em; font-weight: 600;
  text-align: left; padding: 9px 10px; white-space: nowrap;
  border-bottom: 1px solid var(--rule); cursor: pointer;
}
thead th.num, td.num { text-align: right; }
thead th:hover { color: var(--ink); background: var(--surface); }
thead th .arrow { opacity: .35; margin-left: 4px; }
thead th[aria-sort] { color: var(--ink); }
thead th[aria-sort] .arrow { opacity: 1; color: var(--sp); }
tbody td { padding: 7px 10px; border-bottom: 1px solid var(--rule); white-space: nowrap; }
tbody tr:last-child td { border-bottom: 0; }
tbody tr:hover td { background: var(--raised); }

thead th:first-child { left: 0; z-index: 4; }
tbody td:first-child { position: sticky; left: 0; z-index: 1; background: var(--surface); }
tbody tr:hover td:first-child { background: var(--raised); }

td.trunc { overflow: hidden; text-overflow: ellipsis; }
.song { font-weight: 550; max-width: 260px; overflow: hidden; text-overflow: ellipsis; }
td.artist { max-width: 170px; }
td.charter { max-width: 150px; }
.dim { color: var(--muted); }
.pos { color: var(--t0); }
.neg { color: var(--t3); }

.chip {
  display: inline-block; padding: 1px 7px; border-radius: 999px;
  font-size: 11px; font-weight: 600; letter-spacing: .01em;
  border: 1px solid currentColor;
}
.s-matched{color:var(--t0)} .s-above{color:var(--t1)} .s-unmatched{color:var(--tn); border-color:transparent}

.empty { padding: 40px; text-align: center; color: var(--muted); }
footer { color: var(--muted); font-size: 12px; }
</style>

<div class="wrap">
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

<script id="data" type="application/json">__DATA__</script>
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

const sortby = document.getElementById('sortby');
const sortdir = document.getElementById('sortdir');

COLS.forEach(c => {
  const opt = document.createElement('option');
  opt.value = c.k;
  opt.textContent = c.t;
  sortby.appendChild(opt);
});

function setSort(key, dir) {
  sortKey = key;
  sortDir = dir;
  sortby.value = key;
  const numeric = COLS.find(c => c.k === key).num;
  sortdir.textContent = dir === -1
    ? (numeric ? '↓ Highest' : '↓ Z → A')
    : (numeric ? '↑ Lowest' : '↑ A → Z');
  render();
}

sortby.addEventListener('change', () => {
  setSort(sortby.value, COLS.find(c => c.k === sortby.value).num ? -1 : 1);
});
sortdir.addEventListener('click', () => setSort(sortKey, -sortDir));

const head = document.getElementById('head');
COLS.forEach(c => {
  const th = document.createElement('th');
  th.textContent = c.t;
  th.tabIndex = 0;
  th.title = 'Sort by ' + c.t;
  if (c.num) th.className = 'num';
  const arrow = document.createElement('span');
  arrow.className = 'arrow';
  th.appendChild(arrow);
  const activate = () => {
    if (sortKey === c.k) setSort(c.k, -sortDir);
    else setSort(c.k, c.num ? -1 : 1);
  };
  th.addEventListener('click', activate);
  th.addEventListener('keydown', e => {
    if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); activate(); }
  });
  head.appendChild(th);
});

const fmt = n => n === null || n === undefined ? '—' : n.toLocaleString();
const STATUS_CLASS = {'matched':'s-matched', 'above optimal':'s-above', 'unmatched':'s-unmatched'};

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

requestAnimationFrame(() => setTimeout(() => setSort(sortKey, sortDir), 0));
</script>
)page";

std::string lower_hex(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::vector<DmReportRow> collect_dm_rows(store::RecordStore& store,
                                         const std::vector<net::DmScore>& scores,
                                         const std::string& chartmode) {
    // One query for every stored record in this chartmode, indexed by hash.
    std::unordered_map<std::string, store::RecordListing> by_hash;
    for (store::RecordListing& r :
         store.list_records(chartmode, store::SortColumn::Score, /*descending=*/true)) {
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
    return html::render_page(kPage, std::move(data), subtitle, footer);
}

}  // namespace hydra::app::dm_report
