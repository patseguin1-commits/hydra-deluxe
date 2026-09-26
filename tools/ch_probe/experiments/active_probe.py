"""Experiment 2: the active probe. LIVE-ONLY orchestration.

What it does, in one line: play a song of isolated kick pairs, press the second
kick of each pair late by a planned offset, and record whether the engine
counted it -- so the line between hits and misses shows the window the game
enforces at each spacing.

How it runs:

1. Write the probe song into Clone Hero's songs folder: probe_chart's pairs
   layout, one pair per (spacing, offset), at 480 ticks per beat and 125 BPM
   so one tick is one millisecond, with a silent song.ogg.
2. Wait for that song to be playing, and find the live engine by memory scan.
3. Attach the debugger (kill-on-exit off) and breakpoint the hit check. The
   debug loop runs on this thread, because Windows only delivers debug events
   to the thread that attached.
4. A second thread presses the keys: the first kick of each pair on time, the
   second at note + offset. Hit = the score rose (proven by play_chart.py).
   Measured offset = the +0x2e0 hit time minus the note when that field
   changed, else the estimated send time (walk_edges.py's rule).
5. Detach (always, even on Ctrl+C), write the rows, and summarise per spacing.

The hit-check breakpoint counts how often the game ran its hit check during
each input: evidence that the debugger reaches the hit decision.

    python -m tools.ch_probe.experiments.active_probe --spacings 211 --offsets 70,100
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import json
import os
import sys
import threading
import time
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional, Sequence, Tuple

# Make `tools.ch_probe...` importable when run directly. experiments/ is three
# levels below the repo root.
_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants, engine_finder, probe_chart, probe_songs  # noqa: E402
from tools.ch_probe.experiments import analysis  # noqa: E402
from tools.ch_probe.experiments.walk_edges import SongClock  # noqa: E402
from tools.ch_probe.process import open_process  # noqa: E402
from tools.ch_probe.debugger import Debugger  # noqa: E402
from tools.ch_probe.engine import EngineModel  # noqa: E402
from tools.ch_probe.input_driver import InputDriver, Lane  # noqa: E402


RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
SONG_NAME = "Active Probe"

# Dense near the 180-220 ms region where the clamp decision happens.
DEFAULT_SPACINGS_MS = constants.PROBE_SPACINGS_MS
# A sweep from clearly inside to clearly outside the ~85 ms edge.
DEFAULT_OFFSETS_MS = (70, 75, 80, 82, 84, 86, 88, 90, 95, 100)
SETTLE_MS = 250   # read the result this long after the note (as walk_edges.py)

# One collected input: its spacing, the measured offset (ms), and hit or miss.
ActiveRow = Tuple[float, float, bool]


@dataclass(frozen=True)
class PlannedInput:
    index: int
    spacing_ms: float
    offset_ms: float
    first_ms: float    # the pair's first kick, pressed on time
    second_ms: float   # the pair's second kick, pressed at second_ms + offset_ms


def plan_inputs(spacings_ms: Sequence[float],
                offsets_ms: Sequence[float]) -> List[PlannedInput]:
    """One note pair per (spacing, offset), in chart order. At probe_songs'
    480 ticks per beat and 125 BPM a tick is one millisecond, so the chart's
    note ticks are the note times in ms."""
    order = [(s, o) for s in spacings_ms for o in offsets_ms]
    ticks = probe_chart.probe_note_ticks(
        [s for s, _ in order], resolution=probe_songs.RESOLUTION, bpm=probe_songs.BPM)
    return [PlannedInput(i, float(s), float(o), float(ticks[2 * i]), float(ticks[2 * i + 1]))
            for i, (s, o) in enumerate(order)]


def write_probe_song(root: str, plan: Sequence[PlannedInput]) -> str:
    """Write the playable probe song folder: notes.chart, song.ini, song.ogg."""
    folder = os.path.join(root, SONG_NAME)
    os.makedirs(folder, exist_ok=True)
    text = probe_chart.build_probe_chart_text(
        [p.spacing_ms for p in plan], resolution=probe_songs.RESOLUTION,
        bpm=probe_songs.BPM, note=constants.PROBE_CHART_NOTE_KICK)
    length_ms = int(plan[-1].second_ms) + probe_songs.SILENCE_MS
    full_name = f"Hydra Probe - {SONG_NAME}"
    with open(os.path.join(folder, "notes.chart"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    with open(os.path.join(folder, "song.ini"), "w", encoding="utf-8", newline="\n") as f:
        f.write(probe_songs.song_ini(full_name, length_ms))
    probe_songs.write_silent_ogg(os.path.join(folder, "song.ogg"), length_ms)
    return folder


class ActiveCollector:
    """Rows from the input thread; hit-check counts from the debug loop."""

    def __init__(self) -> None:
        self._rows: List[ActiveRow] = []
        self.current_index: Optional[int] = None   # the input in flight
        self.hit_check_calls: Dict[int, int] = {}

    @property
    def rows(self) -> List[ActiveRow]:
        return list(self._rows)

    def add_row(self, row: ActiveRow) -> None:
        self._rows.append(row)

    def on_hit_check(self, debugger, thread_context) -> None:
        """Breakpoint callback at the hit check: count calls per input."""
        i = self.current_index
        if i is not None:
            self.hit_check_calls[i] = self.hit_check_calls.get(i, 0) + 1


def drive_inputs(engine: EngineModel, driver: InputDriver, collector: ActiveCollector,
                 plan: Sequence[PlannedInput], stop: threading.Event,
                 focus: Callable[[], None]) -> None:
    """Press the planned kicks against the song clock. LIVE-ONLY; runs on the
    input thread while the main thread pumps debug events."""
    clock = SongClock(engine.song_clock)
    raw_s, _ = clock.read()
    last_raw, last_move = raw_s, time.perf_counter()

    def wait_until(t_ms: float) -> float:
        """Poll until the clock estimate reaches t_ms; return it in ms."""
        nonlocal last_raw, last_move
        while True:
            if stop.is_set():
                raise RuntimeError("stopped")
            raw, est = clock.read()
            now = time.perf_counter()
            if raw < last_raw - 1.0:
                raise RuntimeError(f"clock jumped back ({last_raw:.2f} -> {raw:.2f} s)")
            if raw != last_raw:
                last_raw, last_move = raw, now
            elif now - last_move > 5.0:
                raise RuntimeError(f"clock frozen at {raw:.2f} s (song quit or paused)")
            ahead = t_ms / 1000 - est
            if ahead <= 0:
                return est * 1000
            if ahead > 0.04:
                time.sleep(min(ahead - 0.03, 0.5))

    todo = [p for p in plan if p.first_ms > raw_s * 1000 + 150]
    print(f"  {len(todo)}/{len(plan)} pairs still ahead of the clock.")
    print(f"  {'#':>4}  {'spacing':>7}  {'plan':>5}  {'measured':>8}  result")
    for p in todo:
        focus()
        wait_until(p.first_ms)
        driver.press_chord([Lane.KICK])                  # first kick, on time
        sent_ms = wait_until(p.second_ms + p.offset_ms)
        before_score, before_hit = engine.score(), engine.hit_time()
        collector.current_index = p.index
        driver.press_chord([Lane.KICK])                  # second kick, late
        wait_until(max(p.second_ms, p.second_ms + p.offset_ms) + SETTLE_MS)
        after_score, after_hit = engine.score(), engine.hit_time()
        collector.current_index = None
        hit = after_score > before_score
        if after_hit != before_hit:
            measured = after_hit * 1000 - p.second_ms
        else:
            measured = sent_ms - p.second_ms
        collector.add_row((p.spacing_ms, measured, hit))
        print(f"  {p.index + 1:4d}  {p.spacing_ms:7.0f}  {p.offset_ms:+5.0f}  "
              f"{measured:+8.1f}  {'HIT' if hit else 'miss'}")


def find_any_mode_engine(process) -> int:
    """The live engine in either scoring mode."""
    return engine_finder.find_live_engine(process, engine_finder.all_patterns(process))


def find_game_window() -> int:
    return ctypes.windll.user32.FindWindowW(None, "Clone Hero") or 0


def run_active_probe(
    *,
    spacings_ms: Sequence[float] = DEFAULT_SPACINGS_MS,
    offsets_ms: Sequence[float] = DEFAULT_OFFSETS_MS,
    process_name: str = constants.PROCESS_NAME,
    song_root: str = probe_songs.DEFAULT_OUT,
    out_stub: str = "active",
    open_proc: Callable = open_process,
    make_debugger: Callable = Debugger,
    find_engine: Callable = find_any_mode_engine,
    write_song: Callable = write_probe_song,
    make_driver: Callable = InputDriver,
    find_window: Callable[[], int] = find_game_window,
    drive: Callable = drive_inputs,
) -> List[analysis.SpacingEdge]:
    """Write the song, find the engine, attach, drive the inputs, detach,
    report. LIVE-ONLY orchestration. Rows go to results/<out_stub>.csv/.json."""
    plan = plan_inputs(spacings_ms, offsets_ms)
    folder = write_song(song_root, plan)
    print(f"Wrote {folder} ({len(plan)} pairs). Rescan songs in Clone Hero, "
          "then play it on Expert drums.")

    process = open_proc(process_name)
    process.verify_targets()  # milestone 1: refuse a build mismatch.
    engine = EngineModel(process)
    print("  Waiting for the song to play...")
    engine.use_object(find_engine(process))
    print(f"  Engine at {engine.object_ptr:#x}")

    collector = ActiveCollector()
    driver = make_driver()
    hwnd = find_window()

    def focus() -> None:
        if hwnd:
            ctypes.windll.user32.SetForegroundWindow(hwnd)

    stop = threading.Event()
    done = threading.Event()
    failures: List[BaseException] = []

    def worker() -> None:
        try:
            drive(engine, driver, collector, plan, stop, focus)
        except BaseException as e:  # reported below, after detaching
            failures.append(e)
        finally:
            done.set()

    debugger = make_debugger()
    debugger.attach(process.pid)
    print("  Debugger attached (kill-on-exit off).")
    try:
        debugger.set_breakpoint(process.resolve(constants.RVA_HIT_CHECK),
                                collector.on_hit_check)
        threading.Thread(target=worker, name="active-probe-input", daemon=True).start()
        debugger.run(until=done.is_set)
    finally:
        stop.set()
        debugger.stop()
        print("  Detached.")

    if failures and str(failures[0]) != "stopped":
        print(f"  Input thread stopped early: {failures[0]}")
    rows = collector.rows
    _write_rows(rows, out_stub)
    calls = sum(collector.hit_check_calls.values())
    print(f"  Hit-check breakpoint fired {calls} times over "
          f"{len(collector.hit_check_calls)} of {len(rows)} inputs.")

    formula_constants = analysis.normal_formula_constants(engine.constants())
    summary = analysis.summarize_active(rows, formula_constants=formula_constants)
    _print_summary(summary)
    return summary


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
        print("No inputs were collected. Was the probe song playing?")
        return
    print("spacing(ms)  measured_edge(ms)  predicted_edge(ms)  errors")
    for row in summary:
        measured = "n/a" if row.measured_edge_ms is None else f"{row.measured_edge_ms:8.2f}"
        predicted = "n/a" if row.predicted_edge_ms is None else f"{row.predicted_edge_ms:8.2f}"
        print(f"{row.spacing_ms:10.1f}  {measured:>16}  {predicted:>17}  {row.errors:6d}")


def _ms_list(text: str) -> List[float]:
    return [float(x) for x in text.split(",") if x.strip()]


def main(argv: Optional[List[str]] = None) -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--spacings", type=_ms_list,
                    default=list(DEFAULT_SPACINGS_MS), help="e.g. 211,300")
    ap.add_argument("--offsets", type=_ms_list,
                    default=list(DEFAULT_OFFSETS_MS), help="late ms, e.g. 70,100")
    ap.add_argument("--song-root", default=probe_songs.DEFAULT_OUT)
    args = ap.parse_args(argv)
    run_active_probe(spacings_ms=args.spacings, offsets_ms=args.offsets,
                     song_root=args.song_root)


if __name__ == "__main__":
    main()
