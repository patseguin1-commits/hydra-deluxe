"""Build a sortable HTML report of every stored path.

    python hydra_report.py                  # top 5 paths per chart
    python hydra_report.py --paths 20       # top 20 per chart
    python hydra_report.py --all-paths      # everything stored
    python hydra_report.py --out report.html

The page is self-contained: open it anywhere, click any column to sort,
filter by text, difficulty tier, or best-path-only.

"""

import html
import json
import os
import re
import sys

import hydra.hydata as hydata
import hydra.hystore as hystore


# Hydra's own squeeze rating tiers, so the report speaks the same language
# as the details panel. Upper bound in ms, label, token.
TIERS = [
    (2, "Normal", "t0"),
    (35, "Hard", "t1"),
    (70, "Extreme", "t2"),
    (105, "Insane", "t3"),
    (140, "Insane+", "t4"),
    (float('inf'), "Beyond", "t5"),
]


# Charters write their credit with Clone Hero's colour markup, which the game
# renders and everything else shows as literal angle brackets. Read the name
# out of it.
_MARKUP = re.compile(r'</?color[^>]*>', re.IGNORECASE)


def plain(text):
    if not text:
        return text
    return _MARKUP.sub('', text).strip()


def tier_for(ms):
    if ms is None:
        return ("None", "tn")
    for bound, label, token in TIERS:
        if ms < bound:
            return (label, token)
    return ("Beyond", "t5")


def collect_rows(store, max_paths):
    rows = []
    for meta, chartmode, record in store.iter_blobs():
        if not record.is_version_compatible():
            continue

        paths = list(record.all_paths())
        paths.sort(key=lambda p: -p.totalscore())
        best_score = paths[0].totalscore() if paths else 0

        for rank, path in enumerate(paths[:max_paths], 1):
            s = hystore.summarize_path(path)
            label, token = tier_for(s['hardest_ms'])
            rows.append({
                'song': plain(meta['ref_name']) or "(unknown)",
                'artist': plain(meta['ref_artist']) or "",
                'charter': plain(meta['ref_charter']) or "",
                'mode': chartmode,
                'rank': rank,
                'path': path.pathstring(),
                'score': s['score'],
                'delta': s['score'] - best_score,
                'acts': s['actcount'],
                'skip': s['maxskip'],
                'ms': s['hardest_ms'],
                'tier': label,
                'tok': token,
                'mult': round(s['avgmult'], 3),
                'sqin': s['sqin_count'],
                'sqout': s['sqout_count'],
                'notes': s['notecount'],
            })
    return rows


PAGE = """<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Hydra Path Index</title>
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
.mono, td.num, .path, .stat-v {
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

/* Sorting is the point of this page, so it gets a control of its own rather
   than living only on column headers -- with thirteen columns, the ones worth
   sorting by are usually scrolled off the right-hand side. */
.sorter { display: inline-flex; align-items: center; gap: 6px; }
.sorter label { color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: .08em; }
#sortdir { min-width: 108px; text-align: left; }
input:focus-visible, select:focus-visible, th:focus-visible, button:focus-visible {
  outline: 2px solid var(--sp); outline-offset: 2px;
}
.toggle { display: inline-flex; align-items: center; gap: 7px; color: var(--muted); cursor: pointer; user-select: none; }
.count { color: var(--muted); font-size: 13px; margin-left: auto; }

.tablewrap {
  overflow-x: auto; background: var(--surface);
  border: 1px solid var(--rule); border-radius: 10px; box-shadow: var(--shadow);
  /* Always show the horizontal bar: the numeric columns live off to the
     right, and a scroller you cannot see is a scroller nobody uses. */
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
/* Every header carries its affordance, not just the active one. */
thead th .arrow { opacity: .35; margin-left: 4px; }
thead th[aria-sort] { color: var(--ink); }
thead th[aria-sort] .arrow { opacity: 1; color: var(--sp); }
tbody td { padding: 7px 10px; border-bottom: 1px solid var(--rule); white-space: nowrap; }
tbody tr:last-child td { border-bottom: 0; }
tbody tr:hover td { background: var(--raised); }
tbody tr.best td:first-child { box-shadow: inset 3px 0 0 var(--sp); }

/* Keep the song visible while reading the numbers off to the right. */
thead th:first-child { left: 0; z-index: 4; }
tbody td:first-child { position: sticky; left: 0; z-index: 1; background: var(--surface); }
tbody tr:hover td:first-child { background: var(--raised); }

/* Every text column is capped. Left to size themselves, a full-discography
   path string (hundreds of activations) or a charter credit carrying Clone
   Hero colour markup stretches its column to thousands of pixels and pushes
   score, skip and timing off the far right of the page. Hover for the full
   value; the title attribute carries it. */
td.trunc { overflow: hidden; text-overflow: ellipsis; }
.song { font-weight: 550; max-width: 240px; overflow: hidden; text-overflow: ellipsis; }
td.artist { max-width: 150px; }
td.charter { max-width: 150px; }
td.path { max-width: 230px; }
.dim { color: var(--muted); }
.path { color: var(--ink); }
.rank { color: var(--muted); font-size: 12px; }
.delta { color: var(--muted); font-size: 12px; }

.chip {
  display: inline-block; padding: 1px 7px; border-radius: 999px;
  font-size: 11px; font-weight: 600; letter-spacing: .01em;
  border: 1px solid currentColor;
}
.t0{color:var(--t0)} .t1{color:var(--t1)} .t2{color:var(--t2)}
.t3{color:var(--t3)} .t4{color:var(--t4)} .t5{color:var(--t5)} .tn{color:var(--tn); border-color:transparent}

.empty { padding: 40px; text-align: center; color: var(--muted); }
footer { color: var(--muted); font-size: 12px; }
</style>

<div class="wrap">
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
    <input type="search" id="q" placeholder="Search song, artist, charter, or path notation">
    <select id="tier">
      <option value="">All timing tiers</option>
      <option value="Normal">Normal</option>
      <option value="Hard">Hard</option>
      <option value="Extreme">Extreme</option>
      <option value="Insane">Insane</option>
      <option value="Insane+">Insane+</option>
      <option value="Beyond">Beyond 140ms</option>
      <option value="None">No squeezes</option>
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

  <footer>__FOOTER__</footer>
</div>

<script id="data" type="application/json">__DATA__</script>
<script>
const ROWS = JSON.parse(document.getElementById('data').textContent);

const COLS = [
  {k:'song',    t:'Song',     num:false},
  {k:'artist',  t:'Artist',   num:false},
  {k:'charter', t:'Charter',  num:false},
  {k:'path',    t:'Path',     num:false},
  {k:'score',   t:'Score',    num:true},
  {k:'acts',    t:'Acts',     num:true},
  {k:'skip',    t:'Max skip', num:true},
  {k:'ms',      t:'Hardest ms', num:true},
  {k:'tier',    t:'Timing',   num:false},
  {k:'mult',    t:'Avg mult', num:true},
  {k:'sqin',    t:'SqIn',     num:true},
  {k:'sqout',   t:'SqOut',    num:true},
  {k:'notes',   t:'Notes',    num:true},
];

let sortKey = 'score', sortDir = -1;

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
    ? (numeric ? '\\u2193 Highest' : '\\u2193 Z \\u2192 A')
    : (numeric ? '\\u2191 Lowest' : '\\u2191 A \\u2192 Z');
  render();
}

sortby.addEventListener('change', () => {
  // A fresh column starts the way that column is usually wanted: biggest
  // number first, but names from the top.
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
const fmtMs = n => n === null || n === undefined ? '—' : n.toFixed(1);

function visible() {
  const q = document.getElementById('q').value.trim().toLowerCase();
  const tier = document.getElementById('tier').value;
  const bestOnly = document.getElementById('bestonly').checked;

  return ROWS.filter(r => {
    if (bestOnly && r.rank !== 1) return false;
    if (tier && r.tier !== tier) return false;
    if (!q) return true;
    return (r.song + ' ' + r.artist + ' ' + r.charter + ' ' + r.path).toLowerCase().includes(q);
  });
}

function render() {
  const rows = visible();
  const dir = sortDir;
  rows.sort((a, b) => {
    let x = a[sortKey], y = b[sortKey];
    // Nulls always sort to the bottom, whichever direction is active.
    if (x === null || x === undefined) return 1;
    if (y === null || y === undefined) return -1;
    if (typeof x === 'string') return dir * x.localeCompare(y);
    return dir * (x - y);
  });

  document.querySelectorAll('#head th').forEach((th, i) => {
    const c = COLS[i];
    if (c.k === sortKey) th.setAttribute('aria-sort', dir === 1 ? 'ascending' : 'descending');
    else th.removeAttribute('aria-sort');
    // Inactive columns keep a dim double arrow, so it is obvious every one
    // of them can be sorted.
    th.querySelector('.arrow').textContent =
      c.k === sortKey ? (dir === 1 ? '\\u2191' : '\\u2193') : '\\u21c5';
  });

  const body = document.getElementById('body');
  body.textContent = '';
  const frag = document.createDocumentFragment();

  for (const r of rows) {
    const tr = document.createElement('tr');
    if (r.rank === 1) tr.className = 'best';

    const cells = [
      ['song trunc', r.song],
      ['dim trunc artist', r.artist],
      ['dim trunc charter', r.charter],
      ['path mono trunc', r.path],
      ['num', fmt(r.score)],
      ['num', r.acts],
      ['num', r.skip],
      ['num', fmtMs(r.ms)],
      ['tier', null],
      ['num', r.mult.toFixed(3)],
      ['num', r.sqin],
      ['num', r.sqout],
      ['num', fmt(r.notes)],
    ];

    cells.forEach(([cls, val], i) => {
      const td = document.createElement('td');
      if (cls === 'tier') {
        const chip = document.createElement('span');
        chip.className = 'chip ' + r.tok;
        chip.textContent = r.tier;
        td.appendChild(chip);
      } else {
        td.className = cls;
        td.textContent = val;
        // Truncated cells still have to be readable somehow.
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
    rows.length.toLocaleString() + ' of ' + ROWS.length.toLocaleString() + ' paths';

  renderStats(rows);
}

function renderStats(rows) {
  const best = rows.filter(r => r.rank === 1);
  const withMs = rows.filter(r => r.ms !== null && r.ms !== undefined);
  const tightest = withMs.length ? Math.max(...withMs.map(r => r.ms)) : null;
  const maxSkip = rows.length ? Math.max(...rows.map(r => r.skip)) : 0;
  const beyond = rows.filter(r => r.ms !== null && r.ms >= 140).length;

  const stats = [
    ['Charts', new Set(best.map(r => r.song + r.artist)).size.toLocaleString()],
    ['Paths shown', rows.length.toLocaleString()],
    ['Tightest squeeze', tightest === null ? '—' : tightest.toFixed(1) + ' ms'],
    ['Past 140 ms', beyond.toLocaleString()],
    ['Highest skip', maxSkip],
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
document.getElementById('tier').addEventListener('change', render);
document.getElementById('bestonly').addEventListener('change', render);

// Building tens of thousands of rows takes a moment, and doing it inline
// leaves the window blank until it finishes - which reads as a broken page.
// Let the shell paint first, placeholder and all, then fill the table.
requestAnimationFrame(() => setTimeout(() => setSort(sortKey, sortDir), 0));
</script>
"""


def build_html(rows, subtitle, footer):
    data = json.dumps(rows, separators=(',', ':'))
    # Keep the payload from closing its own <script> element.
    data = data.replace('</', '<\\/')
    return (
        PAGE
        .replace('__SUBTITLE__', html.escape(subtitle))
        .replace('__FOOTER__', html.escape(footer))
        .replace('__DATA__', data)
    )


def main(argv):
    max_paths = 5
    out = "hydra_paths.html"
    dbpath = None

    if '--all-paths' in argv:
        max_paths = 10 ** 9
    if '--paths' in argv:
        max_paths = int(argv[argv.index('--paths') + 1])
    if '--out' in argv:
        out = argv[argv.index('--out') + 1]
    if '--db' in argv:
        dbpath = argv[argv.index('--db') + 1]

    store = hystore.RecordStore(dbpath)
    songs, records = store.counts()
    rows = collect_rows(store, max_paths)
    store.close()

    if not rows:
        print("No records stored yet. Run hydra_batch.py first.")
        return 1

    shown = "every path" if max_paths > 10 ** 8 else f"top {max_paths} paths per chart"
    subtitle = f"{records:,} records across {songs:,} songs — {shown}"
    footer = (
        f"Generated from {os.path.basename(str(store.dbpath))}. "
        f"Timing tiers match Hydra's squeeze ratings; "
        f"'Beyond' is past the stock 140 ms window."
    )

    # Make the folder rather than throwing away the work: collecting the rows
    # means inflating every stored record, which is the slow part.
    parent = os.path.dirname(os.path.abspath(out))
    os.makedirs(parent, exist_ok=True)

    with open(out, 'w', encoding='utf-8') as f:
        f.write(build_html(rows, subtitle, footer))

    print(f"Wrote {len(rows):,} path rows to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
