"""Find the song clock — wider search.

Scans 16KB of the engine object (and follows pointers to nearby objects)
for any field advancing at ~1 second per second. Checks doubles, floats,
and integers (as ms or us counters).

Start a song and make sure it's ACTIVELY PLAYING (not paused), then run:
    python tools\\ch_probe\\experiments\\find_clock2.py
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


def safe_read(proc, addr, size):
    try:
        return proc.read(addr, size)
    except OSError:
        return None


def scan_block(label, data1, data2, real_dt):
    """Check a block of bytes for advancing fields."""
    results = []
    size = min(len(data1), len(data2))

    # 8-byte doubles
    for off in range(0, size - 7, 8):
        v1 = struct.unpack_from("<d", data1, off)[0]
        v2 = struct.unpack_from("<d", data2, off)[0]
        if math.isnan(v1) or math.isnan(v2) or math.isinf(v1) or math.isinf(v2):
            continue
        if v1 <= 0 or abs(v1) > 1e10:
            continue
        delta = v2 - v1
        if 0.01 < abs(delta) < 5.0:
            results.append(("double", off, v1, v2, delta))

    # 4-byte floats
    for off in range(0, size - 3, 4):
        v1 = struct.unpack_from("<f", data1, off)[0]
        v2 = struct.unpack_from("<f", data2, off)[0]
        if math.isnan(v1) or math.isnan(v2) or math.isinf(v1) or math.isinf(v2):
            continue
        if v1 <= 0 or abs(v1) > 1e8:
            continue
        delta = v2 - v1
        if 0.01 < abs(delta) < 5.0:
            results.append(("float", off, v1, v2, delta))

    # 4-byte unsigned ints (could be ms counter)
    for off in range(0, size - 3, 4):
        v1 = struct.unpack_from("<I", data1, off)[0]
        v2 = struct.unpack_from("<I", data2, off)[0]
        if v1 == 0 or v2 == 0:
            continue
        delta_ms = v2 - v1  # if ms counter, delta should be ~real_dt*1000
        expected_ms = real_dt * 1000
        if abs(delta_ms - expected_ms) < 50:
            results.append(("u32_ms", off, v1, v2, delta_ms / 1000.0))

    # Sort by closeness to expected delta
    results.sort(key=lambda r: abs(r[4] - real_dt))
    return results


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

    # First check: is the window actually changing? (= song is playing)
    w1 = proc.read_double(engine_ptr + 0x20)
    time.sleep(0.3)
    w2 = proc.read_double(engine_ptr + 0x20)
    if abs(w1 - w2) < 0.0001:
        print(f"  WARNING: window is static ({w1*1000:.1f} ms). Is the song playing (not paused)?")

    DELAY = 0.5
    BLOCK = 0x4000  # 16KB

    # Scan the engine object itself
    print(f"\n--- Scanning engine object (16KB) ---")
    d1 = safe_read(proc, engine_ptr, BLOCK)
    t1 = time.perf_counter()
    time.sleep(DELAY)
    d2 = safe_read(proc, engine_ptr, BLOCK)
    t2 = time.perf_counter()
    real_dt = t2 - t1

    if d1 and d2:
        results = scan_block("engine", d1, d2, real_dt)
        if results:
            print(f"  {'type':>8}  {'offset':>8}  {'read1':>12}  {'read2':>12}  {'delta':>8}  match?")
            for typ, off, v1, v2, delta in results[:15]:
                match = "YES" if abs(delta - real_dt) < 0.05 else ""
                print(f"  {typ:>8}  +{off:#06x}  {v1:12.4f}  {v2:12.4f}  {delta:8.4f}  {match}")
        else:
            print("  No advancing fields found in engine object.")

    # Follow pointers: read 8-byte values at each offset, check if they
    # look like heap pointers, and scan the objects they point to
    print(f"\n--- Following pointers from engine object ---")
    ptrs_checked = 0
    for off in range(0, min(0x200, len(d1 or b"") - 7), 8):
        ptr = struct.unpack_from("<Q", d1, off)[0]
        # Heuristic: heap pointers on Win64 are typically 0x1_0000_0000 to 0x7FFF_FFFF_FFFF
        if 0x10000000000 <= ptr <= 0x7FFFFFFFFFFF:
            sub1 = safe_read(proc, ptr, 0x400)
            if sub1 is None:
                continue
            time.sleep(0.05)
            sub2 = safe_read(proc, ptr, 0x400)
            if sub2 is None:
                continue
            real_dt2 = 0.05
            results = scan_block(f"+{off:#x}->", sub1, sub2, real_dt2)
            if results:
                ptrs_checked += 1
                print(f"\n  Pointer at +{off:#x} -> {ptr:#x}:")
                for typ, sub_off, v1, v2, delta in results[:5]:
                    match = "CLOCK?" if abs(delta - real_dt2) < 0.02 else ""
                    print(f"    {typ:>8} +{sub_off:#06x}: {v1:.4f} -> {v2:.4f} "
                          f"(delta {delta:.4f}, expect ~{real_dt2:.3f}) {match}")

    if ptrs_checked == 0:
        print("  No pointer targets had advancing fields.")

    proc.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
