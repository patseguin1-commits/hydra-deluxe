"""Find the song clock field in the engine object.

Reads a large block of the engine twice with a known delay, then
reports every 8-byte-aligned field that advanced by approximately
that delay (interpreted as a double in seconds). The field that
tracks real time 1:1 is the song clock.

Start a song, then run:
    python tools\\ch_probe\\experiments\\find_clock.py
"""

from __future__ import annotations

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
from tools.ch_probe.experiments.find_engine import scan_for_engine


def find_live_engine(proc):
    back_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_BACK), 8)
    front_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_FRONT), 8)
    hits, _, _ = scan_for_engine(proc, back_bytes, front_bytes)
    module_end = proc.module_base + 0x4000000
    heap_hits = [h for h in hits if not (proc.module_base <= h < module_end)]
    for h in heap_hits:
        try:
            tw = proc.read_double(h + 0x20)
            if tw > 0.001:
                return h
        except OSError:
            pass
    return None


def main() -> None:
    print("Looking for Clone Hero...")
    proc = open_process()
    proc.verify_targets()

    engine_ptr = find_live_engine(proc)
    if not engine_ptr:
        print("  No live engine. Start a song first.")
        proc.close()
        return
    print(f"  Engine at {engine_ptr:#x}")

    BLOCK = 0x400  # read 1024 bytes
    DELAY = 0.5     # wait 500ms between reads

    print(f"\n  Reading {BLOCK} bytes, waiting {DELAY}s, reading again...")
    print(f"  Looking for doubles that advanced by ~{DELAY}s.\n")

    snap1 = proc.read(engine_ptr, BLOCK)
    t1 = time.perf_counter()
    time.sleep(DELAY)
    snap2 = proc.read(engine_ptr, BLOCK)
    t2 = time.perf_counter()

    real_dt = t2 - t1

    # Check every 8-byte-aligned offset as a double
    candidates = []
    for off in range(0, BLOCK - 7, 8):
        v1 = struct.unpack_from("<d", snap1, off)[0]
        v2 = struct.unpack_from("<d", snap2, off)[0]

        # Skip NaN, inf, zero, negative
        if v1 != v1 or v2 != v2:  # NaN check
            continue
        if abs(v1) > 1e15 or abs(v2) > 1e15:
            continue
        if v1 <= 0 or v2 <= 0:
            continue

        delta = v2 - v1

        # The song clock should advance by ~real_dt seconds
        if abs(delta - real_dt) < 0.05:
            candidates.append((off, v1, v2, delta))

    if candidates:
        print(f"  {'offset':>8}  {'read1':>10}  {'read2':>10}  {'delta':>8}  {'expected':>8}")
        for off, v1, v2, delta in candidates:
            print(f"  +{off:#06x}  {v1:10.4f}  {v2:10.4f}  {delta:8.4f}  {real_dt:8.4f}")
    else:
        print("  No exact matches. Showing all advancing positive doubles:\n")
        print(f"  {'offset':>8}  {'read1':>10}  {'read2':>10}  {'delta':>8}")
        advancing = []
        for off in range(0, BLOCK - 7, 8):
            v1 = struct.unpack_from("<d", snap1, off)[0]
            v2 = struct.unpack_from("<d", snap2, off)[0]
            if v1 != v1 or v2 != v2:
                continue
            if abs(v1) > 1e10 or abs(v2) > 1e10:
                continue
            delta = v2 - v1
            if 0.01 < delta < 10.0 and v1 > 0:
                advancing.append((off, v1, v2, delta))

        advancing.sort(key=lambda x: abs(x[3] - real_dt))
        for off, v1, v2, delta in advancing[:20]:
            marker = " <-- CLOCK?" if abs(delta - real_dt) < 0.1 else ""
            print(f"  +{off:#06x}  {v1:10.4f}  {v2:10.4f}  {delta:8.4f}{marker}")

    # Also do a second pass to confirm: read 3 times quickly
    print(f"\n  Confirming with 3 rapid reads (100ms apart)...")
    s1 = proc.read(engine_ptr, BLOCK)
    ta = time.perf_counter()
    time.sleep(0.1)
    s2 = proc.read(engine_ptr, BLOCK)
    tb = time.perf_counter()
    time.sleep(0.1)
    s3 = proc.read(engine_ptr, BLOCK)
    tc = time.perf_counter()

    dt1 = tb - ta
    dt2 = tc - tb

    print(f"  Real deltas: {dt1:.4f}s, {dt2:.4f}s\n")

    for off, _, _, _ in (candidates or []):
        v1 = struct.unpack_from("<d", s1, off)[0]
        v2 = struct.unpack_from("<d", s2, off)[0]
        v3 = struct.unpack_from("<d", s3, off)[0]
        d1 = v2 - v1
        d2 = v3 - v2
        print(f"  +{off:#06x}: {v1:.4f} → {v2:.4f} → {v3:.4f}  "
              f"(deltas {d1:.4f}, {d2:.4f})")

    proc.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
