"""Phase 3 verification: DPPather (activation DP) vs GraphPather (BFS).

Runs both search engines on every chart in the test corpus at a ladder of SP
meter caps and asserts they find the same optimal score. This is the Phase 3
gate: the activation DP is a best-score-parity prototype, so the check that
matters is that ``DPPather.record.best_path().totalscore()`` equals the BFS's on
every chart and every cap.

The BFS oracle is the *pure-Python* engine (native search forced off), so this
compares algorithm to algorithm rather than to the C++ port.

Usage:
    py dp_parity_check.py                    # whole corpus, caps 4/16/32/64/128
    py dp_parity_check.py --caps 4 16        # a subset of caps
    py dp_parity_check.py --limit 10         # first 10 charts only
    py dp_parity_check.py --filter Everlong  # charts whose path contains a string
    py dp_parity_check.py --depth 4          # search depth (default 4)

Exits non-zero if any chart/cap disagrees.
"""

import argparse
import glob
import os
import sys
import time

# Force the pure-Python BFS as the oracle before importing the engine, so the
# import-time SEARCH_ENABLED picks it up.
os.environ["HYDRA_NO_NATIVE_SEARCH"] = "1"

from hydra import hypath, hymisc, hynative, hysong

hynative.SEARCH_ENABLED = False  # belt and suspenders if already imported

CORPUS = os.path.join("test", "input", "common")
DEFAULT_CAPS = (4, 16, 32, 64, 128)

LOADERS = {
    ".mid": hysong.load_songpath_mid,
    ".chart": hysong.load_songpath_chart,
    ".sng": hysong.load_songpath_sng,
}


def find_charts(root):
    paths = []
    for ext in LOADERS:
        paths += glob.glob(os.path.join(root, "**", "*" + ext), recursive=True)
    return sorted(paths)


def load(path):
    ext = os.path.splitext(path)[1].lower()
    # Expert Pro Drums 2x, matching the optimization study's settings.
    return LOADERS[ext](path, "Expert", True, True)


def distinct_scores(paths):
    return sorted({p.totalscore() for p in paths}, reverse=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--caps", type=int, nargs="+", default=list(DEFAULT_CAPS))
    ap.add_argument("--depth", type=int, default=4)
    ap.add_argument("--limit", type=int, default=None)
    ap.add_argument("--filter", default=None)
    ap.add_argument("--quiet", action="store_true",
                    help="only print mismatches and the summary")
    args = ap.parse_args()

    charts = find_charts(CORPUS)
    if args.filter:
        charts = [c for c in charts if args.filter in c]
    if args.limit is not None:
        charts = charts[: args.limit]

    print(f"charts={len(charts)}  caps={args.caps}  depth={args.depth}  "
          f"native_search={hynative.SEARCH_ENABLED}")
    print("-" * 72)

    checks = 0
    mismatches = []
    topk_mismatches = []
    empty = 0
    t0 = time.perf_counter()

    for path in charts:
        rel = path.split("common", 1)[-1].replace(os.sep, "/").lstrip("/")
        try:
            song = load(path)
        except Exception as exc:  # a parse failure is not a parity result
            print(f"  LOAD FAIL  {rel}: {exc}")
            mismatches.append((rel, "load", str(exc)))
            continue
        if song.is_empty():
            empty += 1
            continue

        for cap in args.caps:
            graph = hypath.ScoreGraph(song, sp_meter_cap=cap)

            bfs = hypath.GraphPather()
            bfs.read(graph, "scores", args.depth, None)
            dp = hypath.DPPather()
            dp.read(graph, "scores", args.depth, None)

            b_best = bfs.record.best_path().totalscore() if bfs.record._paths else None
            d_best = dp.record.best_path().totalscore() if dp.record._paths else None
            checks += 1

            if b_best != d_best:
                mismatches.append((rel, cap, (b_best, d_best)))
                print(f"  MISMATCH  cap={cap:<4} bfs={b_best} dp={d_best}  {rel}")
                b_top = distinct_scores(bfs.record.all_paths())[:6]
                d_top = distinct_scores(dp.record._paths)[:6]
                print(f"            bfs top: {b_top}")
                print(f"            dp  top: {d_top}")
                continue

            # Soft check: the top-k distinct scores should agree as far as both
            # engines report them. Best-score parity is the gate; this only
            # flags a discrepancy worth a look.
            b_top = distinct_scores(bfs.record.all_paths())
            d_top = distinct_scores(dp.record._paths)
            n = min(len(b_top), len(d_top), args.depth + 1)
            if b_top[:n] != d_top[:n]:
                topk_mismatches.append((rel, cap, (b_top[:n], d_top[:n])))
                if not args.quiet:
                    print(f"  topk~     cap={cap:<4} bfs={b_top[:n]} dp={d_top[:n]}  {rel}")
            elif not args.quiet:
                print(f"  OK        cap={cap:<4} score={b_best}  {rel}")

    dt = time.perf_counter() - t0
    print("-" * 72)
    print(f"checks={checks}  empty_charts={empty}  "
          f"best_score_mismatches={len(mismatches)}  "
          f"topk_only_mismatches={len(topk_mismatches)}  "
          f"time={dt:.1f}s")

    if mismatches:
        print("\nBEST-SCORE MISMATCHES (Phase 3 gate FAILED):")
        for rel, cap, detail in mismatches:
            print(f"  {rel} cap={cap}: {detail}")
        return 1

    print("\nAll charts and caps agree on best score. Phase 3 gate PASSED.")
    if topk_mismatches:
        print(f"({len(topk_mismatches)} top-k-set differences remain; these are "
              f"variant/ordering details deferred to Phase 4.)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
