"""Generate the golden HTML report for the C++ port's Phase 6 gate.

hydra_report.py is ported to C++ (src/app/report.cpp) as deterministic string
building, so its gate is a byte-for-byte diff: this script analyzes a fixed
slice of the corpus with pure-Python Hydra, stores the records the way
hydra_batch.py would, runs the real hydra_report.collect_rows/build_html over
them, and freezes the resulting page. The C++ test (tests/test_report.cpp)
rebuilds the same page from its own analysis of the same charts and compares
bytes.

    .venv\\Scripts\\python.exe tools\\gen_golden_report.py

Produces:
    golden/report.html            capped edition page (whole corpus)
    golden/report_uncapped.html   uncapped edition page (first few charts)
    golden/report_meta.json       chart lists, config, and the pinned
                                  subtitle/footer strings the C++ test reuses
"""

import json
import os
import pathlib
import sys

os.environ["HYDRA_NO_NATIVE"] = "1"

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT))

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except (AttributeError, ValueError):
    pass

import hydra.hymisc as hymisc          # noqa: E402
import hydra.hynative as hynative      # noqa: E402
import hydra.hyutil as hyutil          # noqa: E402
import hydra.hystore as hystore        # noqa: E402
import hydra_report                    # noqa: E402

hynative.ENABLED = False
hynative.SEARCH_ENABLED = False

INPUT_ROOT = ROOT / "test" / "input"
GOLDEN_ROOT = ROOT / "golden"

# Fixed analysis config: the app's defaults (Expert Pro 2x, depth scores 4),
# no ms filter so squeeze-heavy paths keep their full spread of rows.
DIFFICULTY, PRO, BASS2X = "Expert", True, True
D_MODE, D_VALUE, MS_FILTER = "scores", 4, None
CHARTMODE = f"{DIFFICULTY} Pro Drums, 2x Bass"
MAX_PATHS = 5

# The uncapped ladder re-runs the search per rung in pure Python, so its page
# covers only the first few charts (sorted) rather than the whole corpus.
UNCAPPED_CHART_COUNT = 5


def relpath_of(path):
    return pathlib.PurePath(os.path.relpath(path, INPUT_ROOT)).as_posix()


def analyze_into_store(scanitems, uncapped):
    """Analyze each chart and store it; returns (store, [relpaths kept])."""
    saved_uncapped = hymisc.UNCAPPED_SP
    hymisc.apply_edition(uncapped)
    try:
        store = hystore.RecordStore(":memory:")
        kept = []
        for i, item in enumerate(scanitems, 1):
            rel = relpath_of(item.notespath)
            print(f"[{i}/{len(scanitems)}] {'uncapped ' if uncapped else ''}{rel}",
                  flush=True)
            try:
                record, tempomap = hyutil.analyze_chart_file(
                    item.notespath, DIFFICULTY, PRO, BASS2X,
                    D_MODE, D_VALUE, ms_filter=MS_FILTER,
                    export_tempomap=True,
                )
            except Exception as e:
                print(f"    skipped ({type(e).__name__}: {e})", flush=True)
                continue
            store.add_song(item, tempomap)
            store.add_record(item.md5, CHARTMODE, record)
            kept.append(rel)
        return store, kept
    finally:
        hymisc.apply_edition(saved_uncapped)


def build_page(store, uncapped, dbname):
    """collect_rows + build_html the way hydra_report.main does, with the
    db-derived strings pinned to a fixed name."""
    saved_uncapped = hymisc.UNCAPPED_SP
    # collect_rows checks record.is_version_compatible() against the applied
    # edition's RECORD_VERSION; keep it in lockstep with what was stored.
    hymisc.apply_edition(uncapped)
    try:
        songs, records = store.counts()
        rows = hydra_report.collect_rows(store, MAX_PATHS)
        subtitle = (f"{records:,} records across {songs:,} songs — "
                    f"top {MAX_PATHS} paths per chart")
        footer = (f"Generated from {dbname}. "
                  f"Timing tiers match Hydra's squeeze ratings; "
                  f"'Beyond' is past the stock 140 ms window.")
        return hydra_report.build_html(rows, subtitle, footer), subtitle, footer
    finally:
        hymisc.apply_edition(saved_uncapped)


def main():
    if not INPUT_ROOT.exists():
        print(f"error: corpus not found at {INPUT_ROOT}", file=sys.stderr)
        return 2

    # Determinism: never abandon a ladder rung for being slow (see gen_golden).
    hymisc.SP_CAP_TIME_BUDGET = 0

    scanitems, folder_errors = hyutil.discover_charts([str(INPUT_ROOT)])
    for err in folder_errors:
        print(f"  ! {err}", file=sys.stderr)
    scanitems.sort(key=lambda s: relpath_of(s.notespath))

    store, kept = analyze_into_store(scanitems, uncapped=False)
    html, subtitle, footer = build_page(store, False, "golden.db")
    store.close()

    unc_items = scanitems[:UNCAPPED_CHART_COUNT]
    unc_store, unc_kept = analyze_into_store(unc_items, uncapped=True)
    unc_html, unc_subtitle, unc_footer = build_page(unc_store, True,
                                                    "golden_uncapped.db")
    unc_store.close()

    GOLDEN_ROOT.mkdir(parents=True, exist_ok=True)
    # newline='' keeps the page's \n bytes as-is (no \r\n translation); the
    # C++ side compares against these bytes exactly.
    with open(GOLDEN_ROOT / "report.html", "w", encoding="utf-8", newline="") as f:
        f.write(html)
    with open(GOLDEN_ROOT / "report_uncapped.html", "w", encoding="utf-8",
              newline="") as f:
        f.write(unc_html)

    meta = {
        "chartmode": CHARTMODE,
        "difficulty": DIFFICULTY,
        "prodrums": PRO,
        "bass2x": BASS2X,
        "d_mode": D_MODE,
        "d_value": D_VALUE,
        "ms_filter": MS_FILTER,
        "max_paths": MAX_PATHS,
        "capped": {"charts": kept, "subtitle": subtitle, "footer": footer},
        "uncapped": {"charts": unc_kept, "subtitle": unc_subtitle,
                     "footer": unc_footer},
    }
    (GOLDEN_ROOT / "report_meta.json").write_text(
        json.dumps(meta, sort_keys=True, indent=2, ensure_ascii=True),
        encoding="utf-8")

    print(f"wrote golden report pages ({len(kept)} capped charts, "
          f"{len(unc_kept)} uncapped) to {GOLDEN_ROOT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
