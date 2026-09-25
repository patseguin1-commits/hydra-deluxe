"""Experiment 1: the passive probe. LIVE-ONLY orchestration.

What it does, in one line: watch a real song play, and for every note read the
raw window the formula computed next to the window the engine actually stored,
so we can see whether the stored value is clamped.

This is the experiment that answers the clamp question on its own. No synthetic
input is needed -- the chart's own notes supply the spacings.

How it works:

1. Attach to the running Clone Hero and capture the live engine object.
2. Breakpoint the window formula's return. The formula hands its result back in
   xmm0, so at that breakpoint we read xmm0 as a double -- that is the raw
   window for this note.
3. Right after, sample self+0x20, the field the engine stores and uses. That is
   the stored window.
4. Also read the current song clock, so consecutive notes give us a spacing.
5. Log every (spacing, raw, stored) row, then write them to disk and print the
   clamp verdict from analysis.clamp_verdict.

IMPORTANT: none of this can run or be unit-tested without a live game and an
attached debugger. The only piece with real test coverage is the analysis it
calls (analysis.clamp_verdict), tested in tests/test_analysis.py. Every
function below that touches the process is a live-only seam, marked as such.
Run it by hand against a running Clone Hero:

    python -m tools.ch_probe.experiments.passive_probe
"""

from __future__ import annotations

import csv
import json
import os
import sys
import time
from typing import List, Optional, Tuple

# Make `tools.ch_probe...` importable when this file is run directly, not just
# under `python -m`. experiments/ is three levels below the repo root.
_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

# These imports reach the sibling modules through their documented factories and
# classes only -- never into their internals. If a sibling is not written yet,
# the import fails here; that is acceptable at this stage per the task.
from tools.ch_probe import constants  # noqa: E402
from tools.ch_probe.experiments import analysis  # noqa: E402
from tools.ch_probe.process import open_process  # noqa: E402
from tools.ch_probe.debugger import Debugger  # noqa: E402
from tools.ch_probe.engine import EngineModel  # noqa: E402


# Where result files land. A sibling `results/` folder next to this script.
RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")

# One collected note. spacing is ms since the previous note; raw is the formula
# output; stored is self+0x20 read right after.
PassiveRow = Tuple[float, float, float]


class PassiveCollector:
    """Gathers (spacing, raw, stored) rows as the formula fires per note.

    LIVE-ONLY. Every method here reads the running engine. The class exists so
    the breakpoint callback stays tiny and the collected rows are easy to hand
    to the pure analysis afterward.
    """

    def __init__(self, engine: EngineModel) -> None:
        self._engine = engine
        self._rows: List[PassiveRow] = []
        self._last_note_time: Optional[float] = None

    @property
    def rows(self) -> List[PassiveRow]:
        return list(self._rows)

    def on_formula_return(self, debugger, thread_context) -> None:
        """Breakpoint callback at the formula's return. LIVE-ONLY seam.

        The formula returns its double in xmm0, so we read xmm0_double for the
        raw window. Then we read self+0x20 for the stored window, and the song
        clock to turn consecutive note times into a spacing.
        """
        raw = thread_context.xmm0_double()
        stored = self._engine.total_window()
        now = self._engine.song_clock()

        spacing_ms = 0.0
        if self._last_note_time is not None:
            spacing_ms = (now - self._last_note_time) * 1000.0
        self._last_note_time = now

        self._rows.append((spacing_ms, raw, stored))


def run_passive_probe(
    *,
    duration_s: float = 60.0,
    process_name: str = constants.PROCESS_NAME,
    out_stub: str = "passive",
) -> analysis.ClampResult:
    """Attach, collect for a while, write results, return the clamp verdict.

    LIVE-ONLY orchestration -- needs a running Clone Hero with a song playing.
    Start a chart that has a wide spread of note spacings before calling this.
    `duration_s` is how long to watch. The rows are written to
    results/<out_stub>.csv and results/<out_stub>.json, and the clamp verdict is
    both printed and returned.
    """
    process = open_process(process_name)
    # Milestone 1: refuse to run if the address pipeline does not match the
    # build. This raises rather than reading garbage.
    process.verify_targets()

    debugger = Debugger()
    engine = EngineModel(process, debugger)
    engine.capture_object()

    collector = PassiveCollector(engine)

    formula_addr = process.resolve(constants.RVA_WINDOW_FORMULA)
    # The formula returns near the top of the routine; the concrete return
    # address is pinned live by the debugger/engine layer. We breakpoint the
    # formula entry's return site through the debugger's own bookkeeping.
    debugger.set_breakpoint(formula_addr, collector.on_formula_return)

    deadline = time.monotonic() + duration_s
    debugger.run(until=lambda: time.monotonic() >= deadline)
    debugger.stop()

    rows = collector.rows
    _write_rows(rows, out_stub)

    # Judge against the edge of the mode actually being probed: precision
    # mode's back window is 40 ms, not normal mode's 85.
    cap_ms = (constants.EXPECT_PRECISION_BACK_MS if engine.precision_mode()
              else constants.EXPECT_NORMAL_BACK_MS)
    verdict = analysis.clamp_verdict(rows, cap_ms=cap_ms)
    _print_verdict(verdict, len(rows))
    return verdict


def _write_rows(rows: List[PassiveRow], stub: str) -> Tuple[str, str]:
    """Write the collected rows to CSV and JSON. Pure file I/O, no game."""
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, f"{stub}.csv")
    json_path = os.path.join(RESULTS_DIR, f"{stub}.json")

    with open(csv_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["spacing_ms", "raw_formula_output", "stored_window"])
        for spacing, raw, stored in rows:
            writer.writerow([spacing, raw, stored])

    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(
            [
                {"spacing_ms": s, "raw_formula_output": r, "stored_window": w}
                for s, r, w in rows
            ],
            handle,
            indent=2,
        )
    return csv_path, json_path


def _print_verdict(verdict: analysis.ClampResult, n_rows: int) -> None:
    """Say the answer in plain English."""
    print(f"Collected {n_rows} notes.")
    if verdict.verdict == analysis.CLAMP_ABSENT:
        print(
            "No clamp: the stored window followed the raw formula past the "
            f"{verdict.cap_ms:.0f} ms cap "
            f"({verdict.tracked_fraction:.0%} of {verdict.n_above} above-cap notes)."
        )
    elif verdict.verdict == analysis.CLAMP_PRESENT:
        print(
            "Clamp is real: the stored window stayed pinned at "
            f"{verdict.cap_ms:.0f} ms while the raw formula rose above it "
            f"({verdict.flat_fraction:.0%} of {verdict.n_above} above-cap notes)."
        )
    else:
        print(
            "Inconclusive: no note pushed the raw window far past the cap, or "
            "the evidence split. Play a chart with wider spacings and retry."
        )


if __name__ == "__main__":
    run_passive_probe()
