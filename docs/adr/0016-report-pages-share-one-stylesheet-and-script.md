# Report pages share one stylesheet and one script

ADR 0002 pinned the path report's page byte for byte to `hydra_report.py`'s
PAGE string. That script was deleted in the C++ cutover (95f22b0), and no test
compared page bytes after that. The pin was still the only stated reason for
three near-identical page scripts, CSS rules that matched nothing on some
pages, and two copies of the "records by chart hash" index.

## The decision

The path report, the dmleaderboards comparison and the fill-spawn comparison
are one stylesheet and one script around each page's own title, body markup
and a small `PAGE` object. `PAGE` names the columns, the first sort, which rows
the filters keep, how a row is drawn and which stats sit above the table.
Everything else is shared: sorting, the header arrows, drawing rows, the count
line, the stats tiles and the deferred first render.

The stylesheet (`html::kReportCss`) and the script (`html::kReportJs`) live in
`app/html_page.cpp`. They stay ASCII because hydra_core is not built with
/utf-8; a glyph is an HTML entity or a `\uXXXX` escape. The comparison pages
size a few columns differently, scoped by a class on `.wrap`. The comparison
pages look records up through one `report::records_by_hash`.

Page bytes are not pinned. A page change is checked in a real browser instead:
`hydra_tests --no-skip -tc="report pages: write samples*"` writes one sample
page of each kind, and rendering those in headless Edge before and after the
change shows whether a reader would see any difference.

## What this costs

A new column or filter is a `PAGE` edit, not a script copy. A behaviour that
does not fit `PAGE` has to be added to the shared script, where every page gets
it.

This supersedes ADR 0002.
