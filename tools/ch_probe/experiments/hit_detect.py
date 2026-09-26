"""Send inputs on note detection and measure the hit delta.

Watches total_window for changes (= new note). On each change, sends a
Red input and reads +0xb0 (notes-hit counter) to detect hit vs miss.
Also reads +0x28 and +0x100 as doubles to compute the measured delta.

Play any song with drums, then run:
    python tools\\ch_probe\\experiments\\hit_detect.py

Alt-tab back to Clone Hero within 3 seconds.
"""

from __future__ import annotations

import csv
import os
import struct
import sys
import time

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants
from tools.ch_probe.process import open_process
from tools.ch_probe.input_driver import InputDriver
from tools.ch_probe.engine_finder import scan_for_engine

OFF_TOTAL_WINDOW = 0x20
OFF_TIME_A = 0x28       # song/note time (double)
OFF_HITS = 0xb0         # monotonic notes-hit counter (u32)
OFF_SCORE = 0x94        # score (u32)

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")


def first_engine_with_window(proc):
    """This script's own pick: the first heap engine with a nonzero window.
    (engine_finder.find_live_engine is the proven "whose clock moves" check.)"""
    back_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_BACK), 8)
    front_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_FRONT), 8)
    hits, _, _ = scan_for_engine(proc, back_bytes, front_bytes)
    module_end = proc.module_base + 0x4000000
    heap_hits = [h for h in hits if not (proc.module_base <= h < module_end)]
    for h in heap_hits:
        try:
            tw = proc.read_double(h + OFF_TOTAL_WINDOW)
            if tw > 0.001:
                return h, tw
        except OSError:
            pass
    return None, 0.0


def main() -> None:
    print("Looking for Clone Hero...")
    proc = open_process()
    proc.verify_targets()
    print(f"  PID {proc.pid}, addresses verified.")

    engine_ptr, tw = first_engine_with_window(proc)
    if not engine_ptr:
        print("  No live engine found. Is a song actively playing?")
        proc.close()
        return
    print(f"  Engine at {engine_ptr:#x}, window={tw*1000:.1f} ms")

    driver = InputDriver()
    # All pads + kick. In 4-lane drums, Orange (L) = kick.
    # Press all at once so chords register.
    all_vks = [
        driver.get_binding(0),  # Green = A
        driver.get_binding(1),  # Red = S
        driver.get_binding(2),  # Yellow = J
        driver.get_binding(3),  # Blue = K
        driver.get_binding(4),  # Orange/Kick = L
        0x4F,                   # 2X Kick = O
    ]

    def read_window():
        return proc.read_double(engine_ptr + OFF_TOTAL_WINDOW)

    def read_hits():
        return proc.read_u32(engine_ptr + OFF_HITS)

    def read_time_a():
        return proc.read_double(engine_ptr + OFF_TIME_A)

    def read_hit_time():
        # This script read +0x100 as a hit time; it is the song clock
        # (constants.OFF_SONG_CLOCK). Kept as it ran on 2026-09-25.
        return proc.read_double(engine_ptr + constants.OFF_SONG_CLOCK)

    def read_score():
        return proc.read_u32(engine_ptr + OFF_SCORE)

    print(f"  Initial hits={read_hits()}, score={read_score()}")
    print(f"\n  *** Alt-tab to Clone Hero NOW. Starts in 3 seconds. ***\n")
    time.sleep(3)

    last_window = read_window()
    rows = []
    max_notes = 60

    print(f"  {'#':>3}  {'result':6}  {'window':>9}  {'delta_ms':>9}  {'time_a':>8}  {'hit_time':>8}  {'hits':>4}  {'score':>5}")
    print(f"  {'---':>3}  {'------':6}  {'---------':>9}  {'---------':>9}  {'--------':>8}  {'--------':>8}  {'----':>4}  {'-----':>5}")

    start = time.perf_counter()
    n = 0

    try:
        while n < max_notes and (time.perf_counter() - start) < 120:
            w = read_window()

            if abs(w - last_window) > 0.0001:
                hits_before = read_hits()
                time_a_before = read_time_a()
                hit_time_before = read_hit_time()

                # Press all keys down at once (chords need simultaneous input)
                for vk in all_vks:
                    driver.send_key(vk, key_up=False)
                time.sleep(0.005)
                # Release all
                for vk in all_vks:
                    driver.send_key(vk, key_up=True)
                time.sleep(0.015)

                hits_after = read_hits()
                time_a_after = read_time_a()
                hit_time_after = read_hit_time()
                score_after = read_score()

                hit = hits_after > hits_before
                w_ms = w * 1000

                # The delta between the two timestamp fields might be
                # the hit timing offset
                delta = (hit_time_after - time_a_after) * 1000 if hit else float('nan')

                n += 1
                tag = "HIT" if hit else "miss"

                row = {
                    "n": n, "hit": hit, "window_ms": w_ms,
                    "delta_ms": delta,
                    "time_a": time_a_after, "hit_time": hit_time_after,
                    "hits_total": hits_after, "score": score_after,
                }
                rows.append(row)

                delta_str = f"{delta:9.2f}" if hit else "      n/a"
                print(f"  {n:3d}  {tag:6}  {w_ms:9.1f}  {delta_str}  "
                      f"{time_a_after:8.3f}  {hit_time_after:8.3f}  "
                      f"{hits_after:4d}  {score_after:5d}")

                last_window = w

            time.sleep(0.001)

    except KeyboardInterrupt:
        print("\n  Stopped.")
    except OSError as e:
        print(f"\n  Lost process: {e}")

    total_hits = sum(1 for r in rows if r["hit"])
    print(f"\n  {n} notes, {total_hits} hits, {n - total_hits} misses")

    if rows:
        hit_deltas = [r["delta_ms"] for r in rows if r["hit"] and not (r["delta_ms"] != r["delta_ms"])]
        if hit_deltas:
            print(f"  Hit delta range: {min(hit_deltas):.2f} to {max(hit_deltas):.2f} ms")

    # Save results
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, "hit_detect.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "n", "hit", "window_ms", "delta_ms",
            "time_a", "hit_time", "hits_total", "score",
        ])
        writer.writeheader()
        writer.writerows(rows)
    print(f"  Results: {csv_path}")

    proc.close()
    print("Done.")


if __name__ == "__main__":
    main()
