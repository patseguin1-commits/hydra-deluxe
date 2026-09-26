"""Poll the engine's total_window field while you play a song.

Finds the engine object, then polls total_window at ~200 Hz for up to
60 seconds, recording every value change with a timestamp. At the end,
writes the collected data and prints a summary of the window range.

This is the passive probe without the debugger — it sees the STORED
window (after any clamp the engine applies), not the raw formula output.
If the stored window exceeds the back-window constant (85 ms), there is
no clamp. If it caps at 85 ms while note spacings get wider, there is.

Start a song, then run:
    python tools\\ch_probe\\experiments\\poll_windows.py

Press Ctrl+C to stop early.
"""

from __future__ import annotations

import ctypes
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

from tools.ch_probe.engine_finder import scan_for_engine

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
POLL_HZ = 200
DURATION_S = 60


def main() -> None:
    print(f"Looking for {constants.PROCESS_NAME}...")
    proc = open_process()
    proc.verify_targets()
    print(f"  PID {proc.pid}, addresses verified.")

    # Read exact bytes for the signature
    back_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_BACK), 8)
    front_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_FRONT), 8)

    print("  Scanning for engine object...")
    hits, _, _ = scan_for_engine(proc, back_bytes, front_bytes)
    module_end = proc.module_base + 0x4000000
    heap_hits = [h for h in hits if not (proc.module_base <= h < module_end)]

    if not heap_hits:
        print("  No engine object found. Start a song first.")
        proc.close()
        return

    engine_ptr = heap_hits[0]
    print(f"  Engine at {engine_ptr:#x}")

    tw = proc.read_double(engine_ptr + 0x20)
    bw = proc.read_double(engine_ptr + 0x30)
    print(f"  total_window = {tw*1000:.2f} ms, back_window = {bw*1000:.1f} ms")

    # Poll
    print(f"\n  Polling at ~{POLL_HZ} Hz for up to {DURATION_S}s...")
    print("  Play a song with varied note spacings. Ctrl+C to stop.\n")

    interval = 1.0 / POLL_HZ
    rows = []  # (elapsed_s, total_window_ms)
    t0 = time.perf_counter()
    last_w = None
    changes = 0

    try:
        while True:
            now = time.perf_counter()
            elapsed = now - t0
            if elapsed >= DURATION_S:
                break

            try:
                w = proc.read_double(engine_ptr + 0x20)
            except OSError:
                print("  Lost the process (song ended or game closed).")
                break

            w_ms = w * 1000.0

            if last_w is None or abs(w_ms - last_w) > 0.001:
                rows.append((elapsed, w_ms))
                changes += 1
                if changes <= 50 or changes % 20 == 0:
                    print(f"  {elapsed:7.3f}s  window = {w_ms:8.3f} ms")
                last_w = w_ms

            time.sleep(interval)

    except KeyboardInterrupt:
        print("\n  Stopped by user.")

    elapsed_total = time.perf_counter() - t0
    print(f"\n  Collected {len(rows)} distinct values in {elapsed_total:.1f}s")

    if not rows:
        print("  No data. Was a song playing?")
        proc.close()
        return

    windows = [r[1] for r in rows]
    w_min = min(windows)
    w_max = max(windows)
    back_ms = bw * 1000.0

    print(f"  Window range: {w_min:.3f} — {w_max:.3f} ms")
    print(f"  Back window (constant): {back_ms:.1f} ms")
    print(f"  Total window = 2 × back window: {2*back_ms:.1f} ms")

    if w_max > 2 * back_ms + 0.5:
        print(f"\n  *** Window EXCEEDED 2×back ({2*back_ms:.1f} ms). ***")
        print(f"  *** Max observed: {w_max:.3f} ms. NO CLAMP on the stored field. ***")
    elif len(set(round(w, 2) for w in windows)) == 1:
        print(f"\n  Window never changed — either notes were uniform or no notes hit.")
    else:
        print(f"\n  Window varied but stayed ≤ {2*back_ms:.1f} ms.")
        print(f"  This could mean a clamp is active, or the song just had close spacings.")

    # Write results
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, "poll_windows.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["elapsed_s", "total_window_ms"])
        for elapsed, w_ms in rows:
            writer.writerow([f"{elapsed:.6f}", f"{w_ms:.6f}"])
    print(f"\n  Results written to {csv_path}")

    proc.close()
    print("Done.")


if __name__ == "__main__":
    main()
