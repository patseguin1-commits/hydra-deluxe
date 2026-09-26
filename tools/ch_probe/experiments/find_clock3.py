"""Find the song clock — waits for an active song.

Continuously rescans for a live engine whose window is CHANGING
(proof the song is actively playing), then searches for a field
advancing at ~1 second per second.

Run this first, then start a song in Clone Hero:
    python tools\\ch_probe\\experiments\\find_clock3.py
"""

from __future__ import annotations

import math
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


def find_all_engines(proc):
    back_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_BACK), 8)
    front_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_FRONT), 8)
    hits, _, _ = scan_for_engine(proc, back_bytes, front_bytes)
    module_end = proc.module_base + 0x4000000
    return [h for h in hits if not (proc.module_base <= h < module_end)]


def safe_read(proc, addr, size):
    try:
        return proc.read(addr, size)
    except OSError:
        return None


def main() -> None:
    print("Looking for Clone Hero...")
    proc = open_process()
    proc.verify_targets()
    print(f"  PID {proc.pid}, addresses verified.\n")

    print("  Start/unpause a song in Clone Hero.")
    print("  Waiting for an engine with a changing window...\n")

    engine_ptr = None

    # Phase 1: find an engine whose window is actively changing
    while True:
        engines = find_all_engines(proc)
        for e in engines:
            try:
                w1 = proc.read_double(e + 0x20)
                time.sleep(0.2)
                w2 = proc.read_double(e + 0x20)
                if abs(w2 - w1) > 0.0001 and w1 > 0.001:
                    engine_ptr = e
                    print(f"  Found active engine at {e:#x}")
                    print(f"    window moved: {w1*1000:.1f} -> {w2*1000:.1f} ms")
                    break
            except OSError:
                pass
        if engine_ptr:
            break
        sys.stdout.write(".")
        sys.stdout.flush()
        time.sleep(1.0)

    # Phase 2: scan for clock fields
    # Do three reads with known delays
    BLOCK = 0x4000
    DELAY = 0.3

    print(f"\n  Scanning {BLOCK} bytes with {DELAY}s gaps...")

    d1 = safe_read(proc, engine_ptr, BLOCK)
    t1 = time.perf_counter()
    time.sleep(DELAY)
    d2 = safe_read(proc, engine_ptr, BLOCK)
    t2 = time.perf_counter()
    time.sleep(DELAY)
    d3 = safe_read(proc, engine_ptr, BLOCK)
    t3 = time.perf_counter()

    dt12 = t2 - t1
    dt23 = t3 - t2

    if not (d1 and d2 and d3):
        print("  Read failed.")
        proc.close()
        return

    sz = min(len(d1), len(d2), len(d3))
    candidates = []

    # Check doubles
    for off in range(0, sz - 7, 8):
        vals = [struct.unpack_from("<d", d, off)[0] for d in [d1, d2, d3]]
        if any(math.isnan(v) or math.isinf(v) for v in vals):
            continue
        if any(v <= 0 or abs(v) > 1e10 for v in vals):
            continue
        d_12 = vals[1] - vals[0]
        d_23 = vals[2] - vals[1]
        # Both deltas should match real time
        if abs(d_12 - dt12) < 0.05 and abs(d_23 - dt23) < 0.05:
            candidates.append(("double", off, vals, d_12, d_23))

    # Check floats
    for off in range(0, sz - 3, 4):
        vals = [struct.unpack_from("<f", d, off)[0] for d in [d1, d2, d3]]
        if any(math.isnan(v) or math.isinf(v) for v in vals):
            continue
        if any(v <= 0 or abs(v) > 1e8 for v in vals):
            continue
        d_12 = vals[1] - vals[0]
        d_23 = vals[2] - vals[1]
        if abs(d_12 - dt12) < 0.05 and abs(d_23 - dt23) < 0.05:
            candidates.append(("float", off, vals, d_12, d_23))

    # Check u32 as ms
    for off in range(0, sz - 3, 4):
        vals = [struct.unpack_from("<I", d, off)[0] for d in [d1, d2, d3]]
        if any(v == 0 for v in vals):
            continue
        d_12 = (vals[1] - vals[0]) / 1000.0
        d_23 = (vals[2] - vals[1]) / 1000.0
        if abs(d_12 - dt12) < 0.05 and abs(d_23 - dt23) < 0.05:
            candidates.append(("u32ms", off, vals, d_12, d_23))

    if candidates:
        print(f"\n  FOUND {len(candidates)} clock candidate(s):\n")
        print(f"  {'type':>6} {'offset':>8}  {'val1':>12} {'val2':>12} {'val3':>12}  "
              f"{'d12':>6} {'d23':>6}  (expect ~{dt12:.3f}, ~{dt23:.3f})")
        for typ, off, vals, d12, d23 in candidates:
            if typ == "u32ms":
                print(f"  {typ:>6} +{off:#06x}  {vals[0]:12d} {vals[1]:12d} {vals[2]:12d}  "
                      f"{d12:6.3f} {d23:6.3f}")
            else:
                print(f"  {typ:>6} +{off:#06x}  {vals[0]:12.4f} {vals[1]:12.4f} {vals[2]:12.4f}  "
                      f"{d12:6.3f} {d23:6.3f}")
    else:
        print("\n  No clock found on engine object.")

        # Try following pointers
        print("  Following pointers from first 512 bytes...")
        for off in range(0, min(512, sz - 7), 8):
            ptr = struct.unpack_from("<Q", d1, off)[0]
            if not (0x10000000000 <= ptr <= 0x7FFFFFFFFFFF):
                continue
            s1 = safe_read(proc, ptr, 0x800)
            if not s1:
                continue
            time.sleep(DELAY)
            s2 = safe_read(proc, ptr, 0x800)
            if not s2:
                continue
            time.sleep(DELAY)
            s3 = safe_read(proc, ptr, 0x800)
            if not s3:
                continue

            ssz = min(len(s1), len(s2), len(s3))
            for soff in range(0, ssz - 7, 8):
                vals = [struct.unpack_from("<d", s, soff)[0] for s in [s1, s2, s3]]
                if any(math.isnan(v) or math.isinf(v) for v in vals):
                    continue
                if any(v <= 0 or abs(v) > 1e10 for v in vals):
                    continue
                d_12 = vals[1] - vals[0]
                d_23 = vals[2] - vals[1]
                if abs(d_12 - DELAY) < 0.1 and abs(d_23 - DELAY) < 0.1:
                    print(f"\n  CLOCK at engine+{off:#x} -> {ptr:#x} +{soff:#x}:")
                    print(f"    {vals[0]:.4f} -> {vals[1]:.4f} -> {vals[2]:.4f}")
                    print(f"    deltas: {d_12:.4f}, {d_23:.4f}")

    proc.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
