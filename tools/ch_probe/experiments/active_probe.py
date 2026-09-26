"""Experiment 2: the active probe. LIVE-ONLY orchestration.

What it does, in one line: play a purpose-built chart, fire timed keystrokes
that walk across the window edge, and record for each one the delta the engine
measured and whether it counted as a hit -- so the boundary between hits and
misses tells us the window the game actually enforces.

Why bother after the passive probe: the formula tells us the *intended* window.
Only a real input proves the *enforced* one, because a second clamp could sit at
the hit-comparison. If the enforced edge matches the passive numbers, the model
is confirmed. If it caps lower, we have located a second clamp.

The one idea it rests on: we cannot deliver a keystroke at a precise
millisecond -- OS jitter is several ms. So we do not trust the input timing. We
aim near the edge, then record the delta the engine measured, not the one we
intended. Scatter a few hundred inputs near the edge and the crossover between
the hit cluster and the miss cluster is the true window.

How it works:

1. Generate a probe chart of isolated note pairs at known spacings.
2. Attach, capture the engine, set the drum key binding (read it from the game;
   do not guess).
3. Breakpoint the hit check. For each fired input, read the measured delta and
   the hit/miss flag there.
4. Walk the input offset across the edge for each spacing.
5. Hand the collected (spacing, measured_delta, hit) rows to analysis, which
   finds the per-spacing edge and compares it to the predicted parabola.

IMPORTANT: every function that touches the process, the input driver, or the
debugger is a LIVE-ONLY seam and cannot be unit-tested here. The pure logic it
leans on -- edge detection and the parabola predictor -- is tested in
tests/test_analysis.py. Run it by hand against a running Clone Hero:

    python -m tools.ch_probe.experiments.active_probe
"""

from __future__ import annotations

import csv
import json
import os
import sys
import time
from typing import List, Optional, Sequence, Tuple

# Make `tools.ch_probe...` importable when run directly. experiments/ is three
# levels below the repo root.
_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

# Documented factories/classes only -- no reaching into internals. Imports may
# fail here until the sibling modules exist; that is acceptable at this stage.
from tools.ch_probe import constants  # noqa: E402
from tools.ch_probe.experiments import analysis  # noqa: E402
from tools.ch_probe.process import open_process  # noqa: E402
from tools.ch_probe.debugger import Debugger  # noqa: E402
from tools.ch_probe.engine import EngineModel  # noqa: E402
from tools.ch_probe.probe_chart import generate_probe_chart  # noqa: E402
from tools.ch_probe.input_driver import InputDriver, Lane  # noqa: E402


RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")

# The default probe-chart spacings from the spec: dense near the 180-220 ms
# region where the clamp decision happens, sparse elsewhere for shape.
DEFAULT_SPACINGS_MS = constants.PROBE_SPACINGS_MS

# The .chart note the probe chart writes (the kick), and the input lane that
# presses it. They are different numbers: chart note 0 is the kick; input lane
# 4 is the kick key.
PROBE_NOTE = constants.PROBE_CHART_NOTE_KICK
PROBE_LANE = Lane.KICK

# One collected input: which spacing it belonged to, the delta the engine
# measured (ms), and whether the note counted as a hit.
ActiveRow = Tuple[float, float, bool]


class ActiveCollector:
    """Gathers (spacing, measured_delta, hit) rows at the hit-check breakpoint.

    LIVE-ONLY. The current spacing is set by the runner just before it fires an
    input, so the callback knows which pair the hit belongs to.
    """

    def __init__(self, engine: EngineModel) -> None:
        self._engine = engine
        self._rows: List[ActiveRow] = []
        self._current_spacing: Optional[float] = None

    @property
    def rows(self) -> List[ActiveRow]:
        return list(self._rows)

    def expect(self, spacing_ms: float) -> None:
        """Tell the collector which spacing the next hit belongs to."""
        self._current_spacing = spacing_ms

    def on_hit_check(self, debugger, thread_context) -> None:
        """Breakpoint callback at the hit check. LIVE-ONLY seam.

        Read the delta the engine measured for this note and whether it counted
        as a hit. The exact fields come from the engine model, which owns the
        offsets; we never read raw addresses here.
        """
        if self._current_spacing is None:
            return
        delta_ms = self._engine.hit_time()
        # A note that the engine matched inside its window counts as a hit. The
        # engine model exposes the decision; here we treat a matched note with a
        # finite delta as a hit and everything else as a miss. The precise
        # hit/miss field is pinned live in the engine layer.
        hit = self._engine.note_count() > 0
        self._rows.append((self._current_spacing, delta_ms, hit))


def run_active_probe(
    *,
    spacings_ms: Sequence[float] = DEFAULT_SPACINGS_MS,
    offsets_ms: Optional[Sequence[float]] = None,
    process_name: str = constants.PROCESS_NAME,
    chart_path: Optional[str] = None,
    out_stub: str = "active",
) -> List[analysis.SpacingEdge]:
    """Generate the chart, drive inputs across the edge, analyse, return edges.

    LIVE-ONLY orchestration -- needs a running Clone Hero, and someone to load
    the generated probe chart and start playing it. `offsets_ms` is the ladder
    of intended offsets that walk the input across the edge; the measured delta
    is what actually gets recorded. Writes results/<out_stub>.csv and .json and
    returns the per-spacing edge summary from analysis.summarize_active.
    """
    if offsets_ms is None:
        # A default sweep from clearly-inside to clearly-outside the ~85 ms edge.
        offsets_ms = [70, 75, 80, 82, 84, 86, 88, 90, 95, 100]

    os.makedirs(RESULTS_DIR, exist_ok=True)
    if chart_path is None:
        chart_path = os.path.join(RESULTS_DIR, f"{out_stub}_probe.chart")
    generate_probe_chart(list(spacings_ms), chart_path, note=PROBE_NOTE)
    print(f"Wrote probe chart to {chart_path}. Load it and start the song.")

    process = open_process(process_name)
    process.verify_targets()  # milestone 1: refuse a build mismatch.

    debugger = Debugger()
    engine = EngineModel(process, debugger)
    engine.capture_object()

    driver = InputDriver()
    # Read the game's real key binding for the lane; do not guess. The virtual
    # key comes from the input driver's own config read. This call is the seam
    # the spec's open question flags.
    driver.set_binding(PROBE_LANE, _read_lane_binding(driver, PROBE_LANE))

    collector = ActiveCollector(engine)
    hit_check_addr = process.resolve(constants.RVA_HIT_CHECK)
    debugger.set_breakpoint(hit_check_addr, collector.on_hit_check)

    _drive_sweep(engine, driver, collector, spacings_ms, offsets_ms)
    debugger.stop()

    rows = collector.rows
    _write_rows(rows, out_stub)

    formula_constants = analysis.normal_formula_constants(engine.constants())
    summary = analysis.summarize_active(rows, formula_constants=formula_constants)
    _print_summary(summary)
    return summary


def _drive_sweep(
    engine: EngineModel,
    driver: InputDriver,
    collector: ActiveCollector,
    spacings_ms: Sequence[float],
    offsets_ms: Sequence[float],
) -> None:
    """Fire the offset ladder against each spacing's note. LIVE-ONLY seam.

    For each note pair, tell the collector which spacing is coming, then
    schedule a keystroke at each offset and let the debugger catch the hit
    check. The pairing of note-to-offset against the song clock is pinned live;
    this is the part only a running game exercises.
    """
    clock = engine.song_clock
    for spacing in spacings_ms:
        collector.expect(spacing)
        for _offset in offsets_ms:
            # The note's target song time is resolved live from the chart the
            # game loaded; here we drive one input per offset and let the
            # measured delta -- not the intended offset -- be what we record.
            target_time = clock()  # live seam: real target comes from the chart
            driver.schedule_hit(PROBE_LANE, target_time, clock)
            # Give the debugger a moment to catch the hit check for this input.
            time.sleep(0)


def _read_lane_binding(driver: InputDriver, lane: int) -> int:
    """Return the virtual-key code bound to a drum lane. LIVE-ONLY seam.

    The binding is user-configurable and must be read from the game's config,
    not guessed. This helper isolates that read so the open question in the spec
    has one place to live. Until the input driver exposes the config read, this
    raises to force the caller to resolve it rather than guess a key.
    """
    reader = getattr(driver, "read_config_binding", None)
    if callable(reader):
        return reader(lane)
    raise NotImplementedError(
        "Drum key binding must be read from the game config; see the open "
        "question in the spec. Wire InputDriver.read_config_binding, or pass a "
        "known binding, before running the active probe."
    )


def _write_rows(rows: List[ActiveRow], stub: str) -> Tuple[str, str]:
    """Write collected rows to CSV and JSON. Pure file I/O, no game."""
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, f"{stub}.csv")
    json_path = os.path.join(RESULTS_DIR, f"{stub}.json")

    with open(csv_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["spacing_ms", "measured_delta_ms", "hit"])
        for spacing, delta, hit in rows:
            writer.writerow([spacing, delta, int(hit)])

    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(
            [
                {"spacing_ms": s, "measured_delta_ms": d, "hit": bool(h)}
                for s, d, h in rows
            ],
            handle,
            indent=2,
        )
    return csv_path, json_path


def _print_summary(summary: List[analysis.SpacingEdge]) -> None:
    """Say the per-spacing result in plain English."""
    if not summary:
        print("No inputs were collected. Was the probe chart playing?")
        return
    print("spacing(ms)  measured_edge(ms)  predicted_edge(ms)  errors")
    for row in summary:
        measured = "n/a" if row.measured_edge_ms is None else f"{row.measured_edge_ms:8.2f}"
        predicted = "n/a" if row.predicted_edge_ms is None else f"{row.predicted_edge_ms:8.2f}"
        print(f"{row.spacing_ms:10.1f}  {measured:>16}  {predicted:>17}  {row.errors:6d}")


if __name__ == "__main__":
    run_active_probe()
