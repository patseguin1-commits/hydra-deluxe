#!/usr/bin/env python3
"""Benchmark Hydra analysis at various SP meter caps.

Measures how long the uncapped ladder takes per chart, with and without
the native C++ search engine.  Run from the repo root:

    py benchmark_depth128.py                   # first chart it finds
    py benchmark_depth128.py --chart path/to/notes.mid
    py benchmark_depth128.py --native          # force native search on
    py benchmark_depth128.py --no-native       # force native search off

The blink-182 discography baseline from the README (Expert Pro Drums 2x,
depth 4) is 7.3 s capped / 87.5 s uncapped settling at 128 bars.
"""

import argparse
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))


def find_a_chart():
    """Walk the CWD looking for a mid/chart/sng file we can analyse."""
    for ext in ("*.mid", "*.chart", "*.sng"):
        for p in Path(".").rglob(ext):
            return str(p)
    return None


def load_song(chartpath):
    """Load a song from any supported format."""
    from hydra import hysong
    p = chartpath.casefold()
    if p.endswith(".mid"):
        return hysong.load_songpath_mid(chartpath, "expert", True, False)
    elif p.endswith(".chart"):
        return hysong.load_songpath_chart(chartpath, "expert", True, False)
    elif p.endswith(".sng"):
        return hysong.load_songpath_sng(chartpath, "expert", True, False)
    raise RuntimeError(f"Unknown format: {chartpath}")


def run_once(chartpath, sp_cap, depth_mode, depth_value, ms_filter=None,
             song=None):
    """Analyse a single chart at one SP cap.  Returns (record, seconds)."""
    from hydra import hypath

    if song is None:
        song = load_song(chartpath)
    if song.is_empty():
        raise RuntimeError(f"No expert pro drums notes in {chartpath}")

    graph = hypath.ScoreGraph(song, sp_meter_cap=sp_cap)
    pather = hypath.GraphPather()

    t0 = time.perf_counter()
    pather.read(graph, depth_mode, depth_value, ms_filter)
    elapsed = time.perf_counter() - t0

    return pather.record, elapsed


def run_uncapped_ladder(chartpath, depth_mode="scores", depth_value=4,
                        ms_filter=None, max_cap=128):
    """Run the uncapped ladder up to max_cap and report per-rung timings."""
    from hydra import hyutil, hymisc

    # Temporarily switch to uncapped edition
    original_cap = hymisc.SP_METER_CAP
    hymisc.SP_METER_CAP = None

    results = []
    previous_score = None

    for sp_cap in hymisc.SP_CAP_LADDER:
        if sp_cap > max_cap:
            break

        rec, elapsed = run_once(
            chartpath, sp_cap, depth_mode, depth_value, ms_filter)

        score = rec.best_path().totalscore() if rec._paths else None
        n_paths = len(rec._paths) if rec._paths else 0

        results.append({
            'cap': sp_cap,
            'time': elapsed,
            'score': score,
            'paths': n_paths,
        })

        print(f"  cap {sp_cap:>4}: {elapsed:7.2f}s  score={score}  paths={n_paths}")

        if previous_score is not None and score == previous_score:
            print(f"  ** Settled at cap {sp_cap} **")
            break
        previous_score = score

    hymisc.SP_METER_CAP = original_cap
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--chart", help="Path to a .mid/.chart/.sng file")
    parser.add_argument("--native", action="store_true",
                        help="Force native search on (clears HYDRA_NO_NATIVE_SEARCH)")
    parser.add_argument("--no-native", action="store_true",
                        help="Force native search off (sets HYDRA_NO_NATIVE_SEARCH=1)")
    parser.add_argument("--max-cap", type=int, default=128,
                        help="Highest SP cap to try (default 128)")
    parser.add_argument("--depth", type=int, default=4,
                        help="Search depth (default 4)")
    args = parser.parse_args()

    # Apply native-search toggle before importing hydra. Native is the default
    # now, so --native just clears the opt-out and --no-native sets it.
    if args.native:
        os.environ.pop("HYDRA_NO_NATIVE_SEARCH", None)
    elif args.no_native:
        os.environ["HYDRA_NO_NATIVE_SEARCH"] = "1"

    from hydra import hynative

    chartpath = args.chart or find_a_chart()
    if not chartpath:
        print("No chart file found. Pass --chart <path>.")
        return 1

    print("=" * 60)
    print("HYDRA DEPTH-128 BENCHMARK")
    print("=" * 60)
    print(f"Chart:          {chartpath}")
    print(f"Search depth:   {args.depth}")
    print(f"Max SP cap:     {args.max_cap}")
    print(f"Native DLL:     available={hynative.AVAILABLE}  "
          f"search_enabled={hynative.SEARCH_ENABLED}")
    if not hynative.AVAILABLE:
        print(f"  reason: {hynative.STATUS}")
    print()

    # --- Capped baseline ---
    print("--- Capped baseline (SP cap = 4) ---")
    rec, t_capped = run_once(chartpath, 4, "scores", args.depth)
    score = rec.best_path().totalscore() if rec._paths else None
    print(f"  {t_capped:.2f}s  score={score}")

    # --- Uncapped ladder ---
    print(f"\n--- Uncapped ladder (up to cap {args.max_cap}) ---")
    t0 = time.perf_counter()
    results = run_uncapped_ladder(
        chartpath, depth_mode="scores", depth_value=args.depth,
        max_cap=args.max_cap)
    t_total = time.perf_counter() - t0

    print(f"\nTotal uncapped time: {t_total:.2f}s")
    print(f"Capped baseline:    {t_capped:.2f}s")
    print(f"Ratio:              {t_total / t_capped:.1f}x slower")
    print("=" * 60)

    return 0


if __name__ == "__main__":
    sys.exit(main())
