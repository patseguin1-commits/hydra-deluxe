#include "app/fill_report.h"

#include <map>
#include <unordered_map>
#include <utility>

#include "app/html_page.h"
#include "app/report.h"  // report::plain -- strips Clone Hero <color> markup
#include "core/model.h"  // group_thousands
#include "parse/song.h"  // title_or_unknown

namespace hydra::app::fill_report {

using html::json_escape_into;

namespace {

// This comparison page's own pieces. The stylesheet and the script that sorts,
// filters and draws the table are shared with the other report pages
// (html::page_template, docs/adr/0016). __SUBTITLE__/__FOOTER__/__DATA__ are
// filled by build_fill_html.
//
// Every literal here is ASCII: this file compiles into hydra_core, which is
// not built with /utf-8. Glyphs the page needs go in as HTML entities (markup)
// or \uXXXX escapes (JavaScript).
const char* const kTitle = "Fill spawn comparison &mdash; CH 1.0 vs CH 1.1";

const char* const kBody = R"page(<div class="wrap fill">
  <header>
    <h1>Fill spawn <span class="accent">CH 1.0 vs CH 1.1</span></h1>
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
      <option value="1.1 higher">1.1 higher</option>
      <option value="1.0 higher">1.0 higher</option>
      <option value="same">Same score</option>
      <option value="only 1.0">Only in 1.0 db</option>
      <option value="only 1.1">Only in 1.1 db</option>
    </select>
    <span class="count" id="count"></span>
  </div>

  <div class="tablewrap">
    <table>
      <thead><tr id="head"></tr></thead>
      <tbody id="body"></tbody>
    </table>
    <div class="empty" id="empty">Joining databases&hellip;</div>
  </div>

  <footer>__FOOTER__</footer>
</div>

)page";

const char* const kPageJs = R"page(const STATUS_CLASS = {'1.1 higher':'s-newhigh', '1.0 higher':'s-oldhigh',
                      'same':'s-same', 'only 1.0':'s-only', 'only 1.1':'s-only'};

const PAGE = {
  rows: DATA,
  noun: 'charts',
  sortKey: 'delta',
  sortDir: -1,
  cols: [
    {k:'song',    t:'Song',        num:false},
    {k:'artist',  t:'Artist',      num:false},
    {k:'charter', t:'Charter',     num:false},
    {k:'s10',     t:'CH 1.0',      num:true},
    {k:'s11',     t:'CH 1.1',      num:true},
    {k:'delta',   t:'Delta',       num:true},
    {k:'p10',     t:'CH 1.0 path', num:false},
    {k:'p11',     t:'CH 1.1 path', num:false},
    {k:'acts',    t:'Acts',        num:true},
    {k:'notes',   t:'Notes',       num:true},
    {k:'status',  t:'Status',      num:false},
  ],
  controls: [['q', 'input'], ['status', 'change']],
  filter(q) {
    const status = document.getElementById('status').value;
    return r => {
      if (status && r.status !== status) return false;
      if (!q) return true;
      return (r.song + ' ' + r.artist + ' ' + r.charter).toLowerCase().includes(q);
    };
  },
  cells(r) {
    const hasDelta = r.delta !== null && r.delta !== undefined;
    const deltaCls = !hasDelta ? 'num dim' : (r.delta > 0 ? 'num pos'
                   : (r.delta < 0 ? 'num neg' : 'num dim'));
    const deltaTxt = !hasDelta ? DASH
                   : (r.delta > 0 ? '+' + r.delta.toLocaleString() : fmt(r.delta));
    return [
      ['song trunc', r.song],
      ['dim trunc artist', r.artist],
      ['dim trunc charter', r.charter],
      ['num', fmt(r.s10)],
      ['num', fmt(r.s11)],
      [deltaCls, deltaTxt],
      ['path trunc', r.p10 || DASH],
      ['path trunc', r.p11 || DASH],
      ['num', r.acts_txt],
      ['num', fmt(r.notes)],
      ['chip ' + (STATUS_CLASS[r.status] || 's-only'), r.status, 'chip'],
    ];
  },
  stats(rows) {
    const n = s => rows.filter(r => r.status === s).length;
    const gains = rows.filter(r => r.delta > 0).reduce((a, r) => a + r.delta, 0);
    const losses = rows.filter(r => r.delta < 0).reduce((a, r) => a - r.delta, 0);
    return [
      ['Charts', rows.length.toLocaleString()],
      ['1.1 higher', n('1.1 higher').toLocaleString()],
      ['1.0 higher', n('1.0 higher').toLocaleString()],
      ['Same', n('same').toLocaleString()],
      ['Only one side', (n('only 1.0') + n('only 1.1')).toLocaleString()],
      ['Points gained in 1.1', gains.toLocaleString()],
      ['Points lost in 1.1', losses.toLocaleString()],
    ];
  },
};
)page";

// The page shell, built once on first use.
const std::string& page_template() {
    static const std::string page = html::page_template(kTitle, kBody, kPageJs);
    return page;
}

}  // namespace

std::vector<FillCompareRow> collect_fill_rows(store::RecordStore& old_store,
                                              store::RecordStore& new_store,
                                              const std::string& chartmode,
                                              const store::CapQuery& cap,
                                              const store::Lens& lens) {
    // One query per store under identical settings. RecordListing already
    // carries the best path and its summary, so no blob is ever inflated.
    const std::unordered_map<std::string, store::RecordListing> old_by_hash =
        report::records_by_hash(old_store, chartmode, cap, lens);
    const std::unordered_map<std::string, store::RecordListing> new_by_hash =
        report::records_by_hash(new_store, chartmode, cap, lens);

    // Walk the union of both key sets so a chart in only one database still
    // gets a row. Ordered so the page's rows come out deterministically.
    std::map<std::string, std::pair<const store::RecordListing*,
                                    const store::RecordListing*>> united;
    for (const auto& [hash, rec] : old_by_hash) united[hash].first = &rec;
    for (const auto& [hash, rec] : new_by_hash) united[hash].second = &rec;

    std::vector<FillCompareRow> rows;
    rows.reserve(united.size());
    for (const auto& [hash, pair] : united) {
        const store::RecordListing* old_rec = pair.first;
        const store::RecordListing* new_rec = pair.second;

        FillCompareRow row;
        row.hyhash = hash;

        // Identity prefers the 1.1 side; either side names the same chart.
        const store::RecordListing* id = new_rec ? new_rec : old_rec;
        row.song = title_or_unknown(report::plain(id->ref_name));
        row.artist = report::plain(id->ref_artist);
        row.charter = report::plain(id->ref_charter);

        if (old_rec) {
            row.old_score = old_rec->summary.score;
            row.old_path = old_rec->bestpath;
            row.old_acts = old_rec->summary.actcount;
        }
        if (new_rec) {
            row.new_score = new_rec->summary.score;
            row.new_path = new_rec->bestpath;
            row.new_acts = new_rec->summary.actcount;
        }
        row.notes = new_rec ? new_rec->summary.notecount : old_rec->summary.notecount;

        // A row counts as one-sided when the other database has no record for
        // the chart, or has one that produced no score at all.
        if (row.old_score && row.new_score) {
            int64_t delta = *row.new_score - *row.old_score;
            row.delta = delta;
            row.status = delta == 0 ? "same" : (delta > 0 ? "1.1 higher" : "1.0 higher");
        } else if (row.old_score) {
            row.status = "only 1.0";
        } else {
            row.status = "only 1.1";
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

FillCompareStats tally_fill_rows(const std::vector<FillCompareRow>& rows) {
    FillCompareStats stats;
    stats.total = static_cast<int>(rows.size());
    for (const FillCompareRow& r : rows) {
        if (r.status == "same") ++stats.same;
        else if (r.status == "1.0 higher") ++stats.ch10_higher;
        else if (r.status == "1.1 higher") ++stats.ch11_higher;
        else if (r.status == "only 1.0") ++stats.only_old;
        else ++stats.only_new;
    }
    return stats;
}

std::string build_fill_html(const std::vector<FillCompareRow>& rows,
                            const std::string& subtitle,
                            const std::string& footer) {
    auto opt_num = [](const auto& o) {
        return o ? std::to_string(*o) : std::string("null");
    };

    std::string data;
    data.reserve(rows.size() * 220 + 2);
    data.push_back('[');
    bool first = true;
    for (const FillCompareRow& r : rows) {
        if (!first) data.push_back(',');
        first = false;

        data += "{\"song\":";
        json_escape_into(data, r.song);
        data += ",\"artist\":";
        json_escape_into(data, r.artist);
        data += ",\"charter\":";
        json_escape_into(data, r.charter);
        data += ",\"s10\":" + opt_num(r.old_score);
        data += ",\"s11\":" + opt_num(r.new_score);
        data += ",\"delta\":" + opt_num(r.delta);
        data += ",\"p10\":";
        json_escape_into(data, r.old_path);
        data += ",\"p11\":";
        json_escape_into(data, r.new_path);
        // `acts` is what the Acts column sorts on (the 1.1 count, falling back
        // to 1.0); `acts_txt` is what it shows: "1.0 / 1.1".
        data += ",\"acts\":" + opt_num(r.new_acts ? r.new_acts : r.old_acts);
        std::string acts_txt = (r.old_acts ? std::to_string(*r.old_acts)
                                           : std::string("\xe2\x80\x94"));
        acts_txt += " / ";
        acts_txt += (r.new_acts ? std::to_string(*r.new_acts)
                                : std::string("\xe2\x80\x94"));
        data += ",\"acts_txt\":";
        json_escape_into(data, acts_txt);
        data += ",\"notes\":" + opt_num(r.notes);
        data += ",\"status\":";
        json_escape_into(data, r.status);
        data.push_back('}');
    }
    data.push_back(']');
    return html::render_page(page_template().c_str(), std::move(data), subtitle,
                             footer);
}

GeneratedFillReport generate_fill_report(store::RecordStore& old_store,
                                         store::RecordStore& new_store,
                                         const std::string& chartmode,
                                         const store::CapQuery& cap,
                                         const store::Lens& lens) {
    GeneratedFillReport out;
    std::vector<FillCompareRow> rows =
        collect_fill_rows(old_store, new_store, chartmode, cap, lens);
    out.stats = tally_fill_rows(rows);
    if (rows.empty()) return out;

    std::string subtitle =
        group_thousands(out.stats.total) + " charts in " + chartmode + ": " +
        group_thousands(out.stats.ch11_higher) + " score higher under 1.1, " +
        group_thousands(out.stats.ch10_higher) + " higher under 1.0, " +
        group_thousands(out.stats.same) + " unchanged, " +
        group_thousands(out.stats.only_old + out.stats.only_new) +
        " in one database only";
    std::string footer =
        "A drum fill only appears if your Star Power meter filled up in time. "
        "Clone Hero 1.0 gave you until about one fill-length before the fill. "
        "Clone Hero 1.1 made it a flat 4 beats. Four beats is usually the "
        "longer wait, so short fills got stricter and most charts tie or drop. "
        "Long fills got looser, which is where the rare gains come from. "
        "Delta is the 1.1 score minus the 1.0 score.";
    out.html = build_fill_html(rows, subtitle, footer);
    return out;
}

}  // namespace hydra::app::fill_report
