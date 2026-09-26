"""Walk the hit window's edges on Edge Walk (step 5 of the hit-window plan).

This is play_chart.py with one change: each note gets a planned offset instead
of "on time". Edge Walk is 120 kicks one second apart, so a late or early press
can only ever land in its own note's window.

The default plan for one play:
  * 3 on-time notes first. If these miss, the setup is broken (wrong key,
    game not focused) and the rest of the run means nothing.
  * The late side, +80 to +92 ms in 1 ms steps, 3 notes per step (39 notes).
  * The early side, -80 to -92 ms the same way (39 notes).
  * Every note left over is pressed on time.

If a side misses at every step, its edge is below 80. Replay with that side
walked lower, for example:  --late 68:80 --early none

For each note it records the planned offset, when the key was actually sent
(song time), what the engine stored at +0x2e0 if that changed, and whether the
score rose. The score only rises on a hit.

About the song clock. The clock field only changes once per game frame, so a
raw read can be several ms stale. The driver estimates the time between frames
from the last clock change it saw (SongClock below) and presses on that
estimate. Both the raw and estimated send times are logged.

Usage (run it, then start Edge Walk; or start it mid-song):
    python tools\\ch_probe\\experiments\\walk_edges.py [--late 80:92] [--early 80:92] [--reps 3]
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import os
import sys
import time
from dataclasses import dataclass
from typing import Callable, Optional

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe.process import open_process
from tools.ch_probe.input_driver import InputDriver
from tools.ch_probe.experiments import live

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
KICK_LANE = 4          # InputDriver lane for the kick (L), as in play_chart.py
CONTROL_NOTES = 3
SETTLE_MS = 250        # wait this long past the note before reading the result


# --- Pure logic (unit-tested) -------------------------------------------------

def parse_range(text: str) -> Optional[tuple[int, int]]:
    """'80:92' -> (80, 92); 'none' -> None. Magnitudes in ms, inclusive."""
    if text.lower() == "none":
        return None
    lo, hi = (int(p) for p in text.split(":"))
    if lo < 0 or hi < lo:
        raise ValueError(f"bad range {text!r}: want LO:HI with 0 <= LO <= HI")
    return lo, hi


def build_schedule(late: Optional[tuple[int, int]], early: Optional[tuple[int, int]],
                   reps: int, total: int) -> list[int]:
    """Planned offset in ms for every note; + is late, - is early, 0 on time."""
    plan = [0] * CONTROL_NOTES
    if late:
        plan += [ms for ms in range(late[0], late[1] + 1) for _ in range(reps)]
    if early:
        plan += [-ms for ms in range(early[0], early[1] + 1) for _ in range(reps)]
    if len(plan) > total:
        raise ValueError(f"plan needs {len(plan)} notes but the song has {total}")
    return plan + [0] * (total - len(plan))


class SongClock:
    """The song clock, with the time since the last frame filled in.

    The game writes the clock once per frame. When we see it change right
    after a read that still showed the old value, we know the new value is
    fresh, and we add the wall time since then. If the change was not seen
    promptly, we don't know how stale it is, so we use the raw value until the
    next fresh change. The fill-in is capped so a pause can't run it away.
    """

    def __init__(self, read_raw: Callable[[], float],
                 now: Callable[[], float] = time.perf_counter,
                 fresh_s: float = 0.002, max_fill_s: float = 0.05) -> None:
        self._read_raw = read_raw
        self._now = now
        self._fresh_s = fresh_s
        self._max_fill_s = max_fill_s
        self._last_raw: Optional[float] = None
        self._last_read_t: Optional[float] = None
        self._anchor_t: Optional[float] = None

    def read(self) -> tuple[float, float]:
        """(raw, estimate), both song seconds."""
        raw = self._read_raw()
        t = self._now()
        if raw != self._last_raw:
            fresh = self._last_read_t is not None and t - self._last_read_t <= self._fresh_s
            self._anchor_t = t if fresh else None
            self._last_raw = raw
        self._last_read_t = t
        if self._anchor_t is None:
            return raw, raw
        return raw, raw + min(t - self._anchor_t, self._max_fill_s)


@dataclass
class Row:
    index: int
    note_ms: float
    planned_ms: int
    sent_ms: float         # estimated song time at the key-down, minus the note
    sent_raw_ms: float     # raw clock at the key-down, minus the note
    engine_ms: Optional[float]  # +0x2e0 minus the note, if +0x2e0 changed
    hit: bool

    @property
    def measured_ms(self) -> float:
        """The engine's own number when there is one, else the send time."""
        return self.engine_ms if self.engine_ms is not None else self.sent_ms


def summarize(rows: list[Row]) -> list[str]:
    lines: list[str] = []
    controls = [r for r in rows if r.planned_ms == 0]
    if controls:
        n = sum(r.hit for r in controls)
        lines.append(f"On-time notes: {n}/{len(controls)} hit.")
        if n < len(controls):
            lines.append("  WARNING: an on-time note missed. Check focus and the kick key"
                         " before trusting the edges below.")

    for name, sign in (("Late", 1), ("Early", -1)):
        side = [r for r in rows if r.planned_ms * sign > 0]
        if not side:
            continue
        lines.append("")
        lines.append(f"{name} side (planned, hits, measured offsets; "
                     "'e' = engine +0x2e0, 's' = send time):")
        for p in sorted({r.planned_ms for r in side}, key=abs):
            step = [r for r in side if r.planned_ms == p]
            marks = "  ".join(
                f"{r.measured_ms:+6.1f}{'e' if r.engine_ms is not None else 's'}"
                f"{' HIT' if r.hit else ' miss'}" for r in step)
            lines.append(f"  {p:+4d}  {sum(r.hit for r in step)}/{len(step)}   {marks}")

        hits = [abs(r.measured_ms) for r in side if r.hit]
        misses = [abs(r.measured_ms) for r in side if not r.hit]
        lo, hi = min(abs(r.planned_ms) for r in side), max(abs(r.planned_ms) for r in side)
        if not hits:
            lines.append(f"  Every note missed: the {name.lower()} edge is below {lo} ms."
                         f" Replay this side lower, e.g. {lo - 12}:{lo}.")
        elif not misses:
            lines.append(f"  Every note hit: the {name.lower()} edge is above {hi} ms."
                         f" Replay this side higher, e.g. {hi}:{hi + 12}.")
        else:
            widest, narrowest = max(hits), min(misses)
            lines.append(f"  Widest hit {widest:.1f} ms, narrowest miss {narrowest:.1f} ms.")
            if narrowest <= widest:
                lines.append("  Hits and misses overlap: either timing jitter, or a note"
                             " past the edge counted again. Read the rows above.")
            else:
                lines.append(f"  The {name.lower()} edge is between {widest:.1f} and"
                             f" {narrowest:.1f} ms.")
    return lines


# --- Live run -------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("song_dir", nargs="?",
                    default=os.path.join(live.PROBE_ROOT, "Edge Walk"))
    ap.add_argument("--late", default="80:92", help="LO:HI ms, or 'none'")
    ap.add_argument("--early", default="80:92", help="LO:HI ms, or 'none'")
    ap.add_argument("--reps", type=int, default=3, help="notes per 1 ms step")
    args = ap.parse_args()

    manifest = live.load_manifest(args.song_dir)
    notes = manifest["notes"]
    plan = build_schedule(parse_range(args.late), parse_range(args.early),
                          args.reps, len(notes))
    walked = sum(1 for p in plan if p)
    print(f"Song: {manifest['song']}, {len(notes)} notes, {walked} walked, "
          f"{len(notes) - walked} on time.")

    print("Connecting to Clone Hero...")
    proc = open_process()
    proc.verify_targets()
    print("  Waiting for the song to play (start or unpause it)...")
    engine = live.find_live_engine(proc)
    snap = live.read_snapshot(proc, engine)
    print(f"  Engine at {engine:#x}, {'PRECISION' if snap.precision else 'normal'} mode,"
          f" clock {snap.clock_s:.2f} s")

    driver = InputDriver()
    vk = driver.get_binding(KICK_LANE)
    user32 = ctypes.windll.user32
    hwnd = user32.FindWindowW(None, "Clone Hero")
    if not hwnd:
        print("  WARNING: could not find the Clone Hero window")

    clock = SongClock(lambda: proc.read_double(engine + live.OFF_CLOCK))
    raw_s, _ = clock.read()
    cursor = 0
    while cursor < len(notes) and notes[cursor]["time_ms"] < raw_s * 1000 + 150:
        cursor += 1
    print(f"  Starting at note {cursor + 1}/{len(notes)}.\n")
    print(f"  {'#':>4}  {'plan':>5}  {'sent':>7}  {'raw':>7}  {'engine':>7}  result")

    rows: list[Row] = []
    last_raw, last_move = raw_s, time.perf_counter()

    def wait_until(t_ms: float) -> tuple[float, float]:
        """Poll the clock until the estimate reaches t_ms; return (raw, est) in ms."""
        nonlocal last_raw, last_move
        while True:
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
                return raw * 1000, est * 1000
            if ahead > 0.04:
                time.sleep(min(ahead - 0.03, 0.5))
            # Inside the last 40 ms, spin: every read is a chance to catch a
            # frame change promptly, which is what keeps the estimate fresh.

    try:
        for i in range(cursor, len(notes)):
            note_ms = float(notes[i]["time_ms"])
            planned = plan[i]
            if hwnd:
                user32.SetForegroundWindow(hwnd)   # well before the press
            before = live.read_snapshot(proc, engine)

            raw_ms, est_ms = wait_until(note_ms + planned)
            driver._send_key(vk, key_up=False)
            time.sleep(0.003)
            driver._send_key(vk, key_up=True)

            wait_until(max(note_ms, note_ms + planned) + SETTLE_MS)
            after = live.read_snapshot(proc, engine)
            engine_ms = None
            if after.hit_time_s != before.hit_time_s:
                engine_ms = after.hit_time_s * 1000 - note_ms
            row = Row(i, note_ms, planned, est_ms - note_ms, raw_ms - note_ms,
                      engine_ms, after.score > before.score)
            rows.append(row)
            eng = f"{engine_ms:+7.1f}" if engine_ms is not None else "      -"
            print(f"  {i + 1:4d}  {planned:+5d}  {row.sent_ms:+7.1f}  {row.sent_raw_ms:+7.1f}"
                  f"  {eng}  {'HIT' if row.hit else 'miss'}")
    except KeyboardInterrupt:
        print("\n  Stopped by user.")
    except (RuntimeError, OSError) as e:
        print(f"\n  Stopped: {e}")
    proc.close()

    print()
    for line in summarize(rows):
        print(line)

    os.makedirs(RESULTS_DIR, exist_ok=True)
    path = os.path.join(RESULTS_DIR, f"walk_edges_{time.strftime('%Y%m%d_%H%M%S')}.csv")
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["index", "note_ms", "planned_ms", "sent_ms", "sent_raw_ms",
                    "engine_ms", "hit"])
        for r in rows:
            w.writerow([r.index, f"{r.note_ms:.0f}", r.planned_ms, f"{r.sent_ms:.2f}",
                        f"{r.sent_raw_ms:.2f}",
                        "" if r.engine_ms is None else f"{r.engine_ms:.2f}", int(r.hit)])
    print(f"\nRows written to {path}")


if __name__ == "__main__":
    main()
