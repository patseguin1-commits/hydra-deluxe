"""Watch the hit window against the song clock, then map it to the notes.

This is poll_windows.py plus the song clock. poll_windows logs the window
field (+0x20) against wall-clock time, which counts distinct values but can't
tie a value to a note. This script reads the song clock (+0x100) on every
sample too. After the song ends it loads the song's manifest.json and prints
each test block's window values in the order they appeared, next to the notes
around them.

It also logs the score (+0x94) and the field at +0x2e0, which the code
reading says holds the song time of the last hit. That makes the same script
cover step 4 of the hit-window plan: run it on Edge Walk while play_chart.py
hits the notes, and the report checks whether +0x2e0 changes once per hit and
sits next to the note time.

Usage (start the song, then run; or run first and it waits for the song):
    python tools\\ch_probe\\experiments\\watch_window.py ["Window Map" | "Edge Walk" | folder]

It stops by itself a couple of seconds after the last note, or when the clock
stops moving (song quit or restarted). Ctrl+C stops early and still reports.
"""

from __future__ import annotations

import csv
import os
import statistics
import sys
import time
from dataclasses import dataclass
from typing import Optional

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe.process import open_process
from tools.ch_probe.experiments import live

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")

# The capped total window the 2026-09-25 session measured. Step 1 checks that
# every run at 170 ms and wider reads this.
CAP_EXPECT_MS = 171.43
CAP_CHECK_FROM_GAP_MS = 170

STALL_S = 8.0          # clock frozen this long = song quit, paused or restarted
TAIL_S = 2.0           # keep watching this long after the last note


@dataclass(frozen=True)
class Sample:
    clock_ms: float
    window_ms: float
    score: int
    hit_time_ms: float


# --- Pure analysis (unit-tested) -------------------------------------------

def changes(samples: list[Sample], field: str) -> list[tuple[float, float]]:
    """(clock_ms, value) for the first sample and every change of `field`."""
    out: list[tuple[float, float]] = []
    for s in samples:
        v = getattr(s, field)
        if not out or abs(v - out[-1][1]) > 1e-6:
            out.append((s.clock_ms, v))
    return out


def value_at(chg: list[tuple[float, float]], t_ms: float) -> Optional[float]:
    """The value in effect at song time t_ms, or None before the first sample."""
    v = None
    for c, val in chg:
        if c > t_ms:
            break
        v = val
    return v


def block_spans(notes: list[dict]) -> list[tuple[str, float, float]]:
    """(block, start_ms, end_ms) in song order.

    Blocks are split halfway through the silence between them, so a window
    change that lands in the silence is filed with the nearer block.
    """
    order: list[str] = []
    first: dict[str, int] = {}
    last: dict[str, int] = {}
    for n in notes:
        b = n["block"]
        if b not in first:
            order.append(b)
            first[b] = n["time_ms"]
        last[b] = n["time_ms"]
    spans = []
    for i, b in enumerate(order):
        start = float("-inf") if i == 0 else (last[order[i - 1]] + first[b]) / 2
        end = float("inf") if i == len(order) - 1 else (last[b] + first[order[i + 1]]) / 2
        spans.append((b, start, end))
    return spans


def note_around(notes: list[dict], t_ms: float) -> tuple[Optional[dict], Optional[dict]]:
    """The last note at or before t_ms and the first note after it."""
    prev = nxt = None
    for n in notes:
        if n["time_ms"] <= t_ms:
            prev = n
        else:
            nxt = n
            break
    return prev, nxt


def steady_value(chg: list[tuple[float, float]], notes: list[dict],
                 block: str, role: str, inner: slice) -> list[float]:
    """Distinct window values in effect at the inner notes of one role.

    The inner notes have the same gap on both sides and sit two notes away
    from either end, so the value there is the run's own value whichever gap
    the engine uses and even if it updates a note early or late.
    """
    times = [n["time_ms"] for n in notes if n["block"] == block and n["role"] == role]
    vals = []
    for t in times[inner]:
        v = value_at(chg, t)
        if v is not None and round(v, 2) not in vals:
            vals.append(round(v, 2))
    return vals


def _gap(v) -> str:
    return "-" if v is None else f"{v}"


def _note_label(n: Optional[dict]) -> str:
    if n is None:
        return "none"
    return f"#{n['index']} ({_gap(n['gap_before_ms'])}|{_gap(n['gap_after_ms'])})"


def window_report(samples: list[Sample], notes: list[dict]) -> list[str]:
    chg = changes(samples, "window_ms")
    lines: list[str] = []
    if len(chg) <= 1:
        lines.append("The window never changed. It probably only updates when a note")
        lines.append("is hit. Play the song again with play_chart.py running alongside.")
        return lines

    lines.append("Window changes by block. '#i (a|b)' is a note with gap a before")
    lines.append("and gap b after it; 'after' is the last note the clock had passed.")
    for block, start, end in block_spans(notes):
        rows = [(c, v) for c, v in chg if start <= c < end]
        lines.append("")
        lines.append(block)
        if not rows:
            lines.append("  (no change)")
            continue
        for c, v in rows:
            prev, nxt = note_around(notes, c)
            lines.append(f"  {c/1000:8.3f} s  {v:7.2f}  after {_note_label(prev)}, "
                         f"next {_note_label(nxt)}")
        lines.append("  values in order: " + ", ".join(f"{v:.2f}" for _, v in rows))

    blocks = [b for b, _, _ in block_spans(notes)]
    markers = sorted({v for b in blocks
                      for v in steady_value(chg, notes, b, "marker", slice(1, 4))})
    lines.append("")
    lines.append("Marker notes (100 ms gaps): " + ", ".join(f"{v:.2f}" for v in markers))

    lines.append("")
    lines.append("Step 1, the cap: the window in the middle of each run.")
    for b in [b for b in blocks if b.startswith("cap_")]:
        gap = int(b.split("_")[1])
        vals = steady_value(chg, notes, b, "run", slice(2, -2))
        text = ", ".join(f"{v:.2f}" for v in vals) or "no value"
        verdict = ""
        if gap >= CAP_CHECK_FROM_GAP_MS:
            ok = len(vals) == 1 and abs(vals[0] - CAP_EXPECT_MS) < 0.01
            verdict = "  matches the cap" if ok else f"  DIFFERENT from {CAP_EXPECT_MS}"
        lines.append(f"  {gap:4d} ms gap: {text}{verdict}")

    lines.append("")
    lines.append("Step 2, the floor: a real floor shows the same value for every gap.")
    for b in [b for b in blocks if b.startswith("floor_")]:
        gap = int(b.split("_")[1])
        vals = steady_value(chg, notes, b, "run", slice(2, -2))
        lines.append(f"  {gap:4d} ms gap: " + (", ".join(f"{v:.2f}" for v in vals) or "no value"))

    lines.append("")
    lines.append("Step 3, which gap: see the uneven_* blocks above. Compare the value")
    lines.append("around each middle note with the run values for the same gaps.")
    return lines


def hit_time_report(samples: list[Sample], notes: list[dict]) -> list[str]:
    """Check whether +0x2e0 behaves like the hit time (step 4)."""
    hits = changes(samples, "hit_time_ms")[1:]   # [0] is the starting value
    score_rises = sum(1 for a, b in zip(samples, samples[1:]) if b.score > a.score)
    lines = ["+0x2e0 (hit time) against the note times:"]
    if not hits:
        lines.append("  +0x2e0 never changed.")
        lines.append(f"  The score rose {score_rises} times.")
        return lines

    diffs = []
    for c, v in hits:
        n = min(notes, key=lambda n: abs(n["time_ms"] - v))
        d = v - n["time_ms"]
        diffs.append(d)
        lines.append(f"  clock {c/1000:8.3f} s  +0x2e0 {v/1000:8.3f} s  "
                     f"note #{n['index']} at {n['time_ms']/1000:8.3f} s  diff {d:+6.1f} ms")
    lines.append("")
    lines.append(f"  +0x2e0 changed {len(hits)} times; the score rose {score_rises} times.")
    lines.append(f"  |diff|: median {statistics.median(abs(d) for d in diffs):.1f} ms, "
                 f"max {max(abs(d) for d in diffs):.1f} ms")
    if len(hits) == score_rises and max(abs(d) for d in diffs) <= 10.0:
        lines.append("  Looks like the hit time: once per hit, within 10 ms of the note.")
    else:
        lines.append("  Does NOT look like a clean hit time yet; read the rows above.")
    return lines


def clock_step_line(steps_ms: list[float]) -> str:
    if not steps_ms:
        return "The song clock never moved."
    return (f"The song clock moved in steps of {statistics.median(steps_ms):.2f} ms "
            f"(median of {len(steps_ms)}); readings are quantized to that.")


# --- Live run ----------------------------------------------------------------

def resolve_song_dir(arg: Optional[str]) -> str:
    if not arg:
        return os.path.join(live.PROBE_ROOT, "Window Map")
    if os.path.isdir(arg):
        return arg
    return os.path.join(live.PROBE_ROOT, arg)


def main() -> None:
    song_dir = resolve_song_dir(sys.argv[1] if len(sys.argv) > 1 else None)
    manifest = live.load_manifest(song_dir)
    notes = manifest["notes"]
    end_ms = notes[-1]["time_ms"] + TAIL_S * 1000
    print(f"Song: {manifest['song']} ({len(notes)} notes)")

    print("Connecting to Clone Hero...")
    proc = open_process()
    proc.verify_targets()
    print("  Waiting for the song to play (start or unpause it)...")
    engine = live.find_live_engine(proc)
    snap = live.read_snapshot(proc, engine)
    back = proc.read_double(engine + 0x30) * 1000
    front = proc.read_double(engine + 0x38) * 1000
    mode = "PRECISION" if snap.precision else "normal"
    print(f"  Engine at {engine:#x}, {mode} mode, back {back:.2f} ms, front {front:.2f} ms")
    print(f"  Clock {snap.clock_s:.2f} s. Watching until {end_ms/1000:.1f} s.\n")

    samples: list[Sample] = []
    steps: list[float] = []
    last = None
    last_clock = snap.clock_s
    last_move = time.perf_counter()
    try:
        while True:
            try:
                s = live.read_snapshot(proc, engine)
            except OSError:
                print("  Lost the engine (game closed?).")
                break
            now = time.perf_counter()
            if s.clock_s < last_clock - 1.0:
                print(f"  Clock jumped back ({last_clock:.2f} -> {s.clock_s:.2f} s). "
                      "Stopping; rerun for a clean log.")
                break
            if s.clock_s != last_clock:
                steps.append((s.clock_s - last_clock) * 1000)
                last_clock = s.clock_s
                last_move = now
            elif now - last_move > STALL_S:
                print(f"  Clock frozen for {STALL_S:.0f} s. Stopping.")
                break

            cur = Sample(s.clock_s * 1000, s.window_ms, s.score, s.hit_time_s * 1000)
            if (last is None or abs(cur.window_ms - last.window_ms) > 1e-6
                    or cur.score != last.score
                    or abs(cur.hit_time_ms - last.hit_time_ms) > 1e-6):
                samples.append(cur)
                if last is None or abs(cur.window_ms - last.window_ms) > 1e-6:
                    print(f"  {cur.clock_ms/1000:8.3f} s  window {cur.window_ms:7.2f} ms")
                last = cur

            if cur.clock_ms > end_ms:
                break
            time.sleep(0.001)
    except KeyboardInterrupt:
        print("\n  Stopped by user.")
    proc.close()

    print()
    print(clock_step_line(steps[1:]))
    print(f"Mode: {mode}")
    print()
    for line in window_report(samples, notes):
        print(line)
    print()
    for line in hit_time_report(samples, notes):
        print(line)

    os.makedirs(RESULTS_DIR, exist_ok=True)
    slug = os.path.basename(song_dir.rstrip("\\/")).replace(" ", "_").lower()
    path = os.path.join(RESULTS_DIR, f"watch_window_{slug}_{mode.lower()}_"
                                     f"{time.strftime('%Y%m%d_%H%M%S')}.csv")
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["clock_ms", "window_ms", "score", "hit_time_ms"])
        for s in samples:
            w.writerow([f"{s.clock_ms:.3f}", f"{s.window_ms:.4f}", s.score,
                        f"{s.hit_time_ms:.3f}"])
    print(f"\nSamples written to {path}")


if __name__ == "__main__":
    main()
