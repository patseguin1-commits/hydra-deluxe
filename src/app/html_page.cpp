#include "app/html_page.h"

#include <cstdint>
#include <cstdio>

namespace hydra::app::html {

std::string replace_all(std::string s, const std::string& from,
                        const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#x27;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

void json_escape_into(std::string& out, const std::string& s) {
    char buf[16];
    out.push_back('"');
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
            }
            ++i;
            continue;
        }

        // Decode one UTF-8 sequence to a code point. Malformed bytes fall
        // back to passing the single byte through as an escape, which the
        // parsers' valid UTF-8 output never exercises.
        uint32_t cp = 0;
        size_t len = 1;
        if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = (c & 0x1Fu) << 6 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = (c & 0x0Fu) << 12 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6 |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = (c & 0x07u) << 18 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12 |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6 |
                 (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
            len = 4;
        } else {
            cp = c;  // lone byte; emit as-is escaped
        }

        if (cp >= 0x10000) {
            uint32_t v = cp - 0x10000;
            std::snprintf(buf, sizeof(buf), "\\u%04x\\u%04x", 0xD800 + (v >> 10),
                          0xDC00 + (v & 0x3FF));
        } else {
            std::snprintf(buf, sizeof(buf), "\\u%04x", cp);
        }
        out += buf;
        i += len;
    }
    out.push_back('"');
}

std::string render_page(const char* page_template, std::string data_json,
                        const std::string& subtitle, const std::string& footer) {
    data_json = replace_all(std::move(data_json), "</", "<\\/");

    std::string page = page_template;
    page = replace_all(std::move(page), "__SUBTITLE__", html_escape(subtitle));
    page = replace_all(std::move(page), "__FOOTER__", html_escape(footer));
    page = replace_all(std::move(page), "__DATA__", data_json);
    return page;
}


// ---- sortable page fragments ----------------------------------------------
// The shared skeleton of the two sortable report pages. The path report's
// bytes are pinned to hydra_report.py's PAGE (byte-exact parity), so these
// fragments are canon from that page verbatim, comments and \uXXXX escape
// style included; the dm page assembles from the same fragments (a few
// rules, like .toggle and td.path, simply match nothing there). Column
// widths, chip colors, the body, and each page's own script stay per-page.

const char* const kSortableHead = R"frag(<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
)frag";

const char* const kSortableCssCore = R"frag(:root {
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
   than living only on column headers -- with fourteen columns, the ones worth
   sorting by are usually scrolled off the right-hand side. */
.sorter { display: inline-flex; align-items: center; gap: 6px; }
.sorter label { color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: .08em; }
#sortdir { min-width: 108px; text-align: left; }
input:focus-visible, select:focus-visible, th:focus-visible, button:focus-visible {
  outline: 2px solid var(--sp); outline-offset: 2px;
}
.toggle { display: inline-flex; align-items: center; gap: 7px; color: var(--muted); cursor: pointer; user-select: none; }
.count { color: var(--muted); font-size: 13px; margin-left: auto; }
)frag";

const char* const kSortableCssTable = R"frag(
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
)frag";

const char* const kSortableCssChip = R"frag(.chip {
  display: inline-block; padding: 1px 7px; border-radius: 999px;
  font-size: 11px; font-weight: 600; letter-spacing: .01em;
  border: 1px solid currentColor;
}
)frag";

const char* const kSortableCssTail = R"frag(
.empty { padding: 40px; text-align: center; color: var(--muted); }
footer { color: var(--muted); font-size: 12px; }
</style>

)frag";

const char* const kSortableJsSorter = R"frag(const sortby = document.getElementById('sortby');
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
    ? (numeric ? '\u2193 Highest' : '\u2193 Z \u2192 A')
    : (numeric ? '\u2191 Lowest' : '\u2191 A \u2192 Z');
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
)frag";

const char* const kSortableJsBoot = R"frag(// Building tens of thousands of rows takes a moment, and doing it inline
// leaves the window blank until it finishes - which reads as a broken page.
// Let the shell paint first, placeholder and all, then fill the table.
requestAnimationFrame(() => setTimeout(() => setSort(sortKey, sortDir), 0));
</script>
)frag";

}  // namespace hydra::app::html
