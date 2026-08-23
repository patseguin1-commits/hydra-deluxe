# Sortable-page fragments are canon from the pinned path report

The two HTML reports (path index, dmleaderboards comparison) assemble from
shared fragments in `app/html_page.cpp`. The path report's assembled bytes are
pinned to `hydra_report.py`'s PAGE string (byte-exact parity), so the
fragments are taken verbatim from that page — comments, `\uXXXX` escape style
and all — and the dm page adopts them, which leaves a few rules there that
match nothing (`.toggle`, `td.path`, `tr.best`). The alternative — normalizing
both pages to a cleaner shared template — was rejected because it breaks the
parity pin. Do not strip the "unused" rules from the dm page's output or
re-inline the fragments per page; per-page differences belong in each page's
own column-width, chip-color, body, and script pieces only.
