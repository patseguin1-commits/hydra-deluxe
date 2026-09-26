"""Experiment 1: the passive probe. LIVE-ONLY orchestration.

What it does, in one line: watch a real song play, and for every note read the
raw window the formula computed next to the window the engine actually stored,
so we can see whether the stored value is clamped.

How it runs:

1. Wait for a song to be playing and find the live engine by memory scan
   (engine_finder, the route play_chart.py proved at the game).
2. Attach the debugger. Debugger.attach turns kill-on-exit off, so a crash
   here detaches instead of killing Clone Hero.
3. Breakpoint the formula's first instruction. There the return address sits
   at [rsp], so plant a second breakpoint on it (once per call site). When the
   formula returns, its result is in xmm0: that is the raw window.
4. At the formula's next call the caller has stored the previous result, so
   read +0x20 then: that is the stored window for the previous note.
5. Consecutive calls' song clocks give the spacing.
6. After the chosen time, detach (always, even on Ctrl+C), write the rows and
   print the clamp verdict.

The game keeps times in seconds; rows are in milliseconds. +0x20 holds the
whole window, but whether the formula returns one side or the whole is not
known until this runs. So the first rows are printed and the verdict is given
against both edges.

    python -m tools.ch_probe.experiments.passive_probe --seconds 20
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import struct
import sys
import time
from typing import Callable, List, Optional, Tuple

# Make `tools.ch_probe...` importable when this file is run directly, not just
# under `python -m`. experiments/ is three levels below the repo root.
_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants, engine_finder  # noqa: E402
from tools.ch_probe.experiments import analysis  # noqa: E402
from tools.ch_probe.process import open_process  # noqa: E402
from tools.ch_probe.debugger import Debugger  # noqa: E402
from tools.ch_probe.engine import EngineModel  # noqa: E402


# Where result files land. A sibling `results/` folder next to this script.
RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")

# One collected note, in ms: spacing since the previous call, the formula's
# raw result, and the +0x20 value the caller stored for it.
PassiveRow = Tuple[float, float, float]


def find_any_mode_engine(process) -> int:
    """The live engine in either scoring mode."""
    return engine_finder.find_live_engine(process, engine_finder.all_patterns(process))


class PassiveCollector:
    """Pairs each formula result with the window the caller stored for it.

    The callbacks run inside the debug loop. They only read memory and plant
    breakpoints, so the game is held for as short a time as possible.
    """

    def __init__(self, engine: EngineModel) -> None:
        self._engine = engine
        self._rows: List[PassiveRow] = []
        self._last_note_time: Optional[float] = None
        self._spacing_ms = 0.0
        self._pending: Optional[Tuple[float, float]] = None  # (spacing, raw) awaiting stored
        self.return_sites: set = set()

    @property
    def rows(self) -> List[PassiveRow]:
        return list(self._rows)

    def on_formula_entry(self, debugger, thread_context) -> None:
        """Breakpoint callback on the formula's first instruction. LIVE-ONLY seam.

        Three jobs. The previous call's result has been stored by now, so read
        +0x20 and finish that row. The return address is at [rsp]; plant a
        breakpoint there once per call site so the result can be read in xmm0.
        And note the song clock, so consecutive calls give a spacing.
        """
        self._finish_pending()
        ret = struct.unpack("<Q", debugger.read(thread_context.rsp, 8))[0]
        if ret not in self.return_sites:
            self.return_sites.add(ret)
            debugger.set_breakpoint(ret, self.on_formula_return)
        now = self._engine.song_clock()
        self._spacing_ms = (0.0 if self._last_note_time is None
                            else (now - self._last_note_time) * 1000.0)
        self._last_note_time = now

    def on_formula_return(self, debugger, thread_context) -> None:
        """Breakpoint callback where the formula returns. Its result (seconds)
        is in xmm0."""
        self._pending = (self._spacing_ms, thread_context.xmm0_double() * 1000.0)

    def _finish_pending(self) -> None:
        if self._pending is None:
            return
        spacing_ms, raw_ms = self._pending
        self._rows.append((spacing_ms, raw_ms, self._engine.total_window() * 1000.0))
        self._pending = None


def run_passive_probe(
    *,
    duration_s: float = 60.0,
    process_name: str = constants.PROCESS_NAME,
    out_stub: str = "passive",
    open_proc: Callable = open_process,
    make_debugger: Callable = Debugger,
    find_engine: Callable = find_any_mode_engine,
    now: Callable[[], float] = time.monotonic,
) -> List[analysis.ClampResult]:
    """Find the engine, attach, collect for `duration_s`, detach, report.

    LIVE-ONLY orchestration: start a chart with a wide spread of note spacings
    first. Rows go to results/<out_stub>.csv and .json. Returns the verdicts
    against one side's edge and against the whole window's.
    """
    process = open_proc(process_name)
    # Milestone 1: refuse to run if the address pipeline does not match the
    # build. This raises rather than reading garbage.
    process.verify_targets()

    engine = EngineModel(process)
    print("Waiting for a song to play (start or unpause it)...")
    engine.use_object(find_engine(process))
    print(f"  Engine at {engine.object_ptr:#x}")

    collector = PassiveCollector(engine)
    debugger = make_debugger()
    debugger.attach(process.pid)
    print("  Debugger attached (kill-on-exit off).")
    try:
        debugger.set_breakpoint(process.resolve(constants.RVA_WINDOW_FORMULA),
                                collector.on_formula_entry)
        deadline = now() + duration_s
        debugger.run(until=lambda: now() >= deadline)
    finally:
        debugger.stop()
        print("  Detached.")

    rows = collector.rows
    _write_rows(rows, out_stub)
    _print_first_rows(rows)

    # Judge against the edge of the mode actually being probed: precision
    # mode's back window is 40 ms, not normal mode's 85.
    back_ms = (constants.EXPECT_PRECISION_BACK_MS if engine.precision_mode()
               else constants.EXPECT_NORMAL_BACK_MS)
    verdicts = []
    for label, cap_ms in (("one side", back_ms), ("whole window", 2 * back_ms)):
        verdict = analysis.clamp_verdict(rows, cap_ms=cap_ms)
        _print_verdict(verdict, label)
        verdicts.append(verdict)
    return verdicts


def _write_rows(rows: List[PassiveRow], stub: str) -> Tuple[str, str]:
    """Write the collected rows to CSV and JSON. Pure file I/O, no game."""
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, f"{stub}.csv")
    json_path = os.path.join(RESULTS_DIR, f"{stub}.json")

    with open(csv_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["spacing_ms", "raw_formula_ms", "stored_window_ms"])
        for spacing, raw, stored in rows:
            writer.writerow([spacing, raw, stored])

    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(
            [
                {"spacing_ms": s, "raw_formula_ms": r, "stored_window_ms": w}
                for s, r, w in rows
            ],
            handle,
            indent=2,
        )
    return csv_path, json_path


def _print_first_rows(rows: List[PassiveRow], n: int = 10) -> None:
    """Show the scale: is raw one side of the window, or the whole of it?"""
    print(f"Collected {len(rows)} notes. First {min(n, len(rows))}, in ms "
          "(spacing, raw formula, stored +0x20):")
    for spacing, raw, stored in rows[:n]:
        print(f"  {spacing:8.1f}  {raw:9.3f}  {stored:9.3f}")


def _print_verdict(verdict: analysis.ClampResult, label: str) -> None:
    """Say the answer in plain English, for one reading of the scale."""
    head = f"Against the {label} edge ({verdict.cap_ms:.0f} ms): "
    if verdict.verdict == analysis.CLAMP_ABSENT:
        print(head + "no clamp. The stored window followed the raw formula past "
              f"the edge ({verdict.tracked_fraction:.0%} of {verdict.n_above} notes).")
    elif verdict.verdict == analysis.CLAMP_PRESENT:
        print(head + "clamp. The stored window stayed pinned at the edge while "
              f"the raw formula rose above it ({verdict.flat_fraction:.0%} of "
              f"{verdict.n_above} notes).")
    else:
        print(head + "inconclusive. No note pushed the raw window past this edge, "
              "or the evidence split.")


def main(argv: Optional[List[str]] = None) -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seconds", type=float, default=60.0, help="how long to watch")
    args = ap.parse_args(argv)
    run_passive_probe(duration_s=args.seconds)


if __name__ == "__main__":
    main()
