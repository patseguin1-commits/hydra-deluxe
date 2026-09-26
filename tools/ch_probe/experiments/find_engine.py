"""Find the DrumsEngine object by scanning memory for its field signature.

The engine object has the back-window constant (0.085 s) at offset +0x30
and the front-window constant (0.0375 s) at offset +0x38. These are copied
from .rdata at construction and never change. We scan all committed RW
pages for that 16-byte pattern and report every hit.

Start a song first (so the engine object exists), then run:

    python tools\\ch_probe\\experiments\\find_engine.py
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


from tools.ch_probe.engine_finder import scan_for_engine  # moved there 2026-09


def main() -> None:
    print(f"Looking for {constants.PROCESS_NAME}...")
    proc = open_process()
    print(f"  PID {proc.pid}, module at {proc.module_base:#x}")
    proc.verify_targets()
    print("  Addresses verified.")

    # Read the exact bytes from .rdata so we match precisely
    back_addr = proc.resolve(constants.RVA_CONST_NORMAL_BACK)
    front_addr = proc.resolve(constants.RVA_CONST_NORMAL_FRONT)
    back_bytes = proc.read(back_addr, 8)
    front_bytes = proc.read(front_addr, 8)

    back_val = struct.unpack("<d", back_bytes)[0]
    front_val = struct.unpack("<d", front_bytes)[0]
    print(f"\n  Searching for back={back_val} front={front_val}")
    print(f"  Pattern: {back_bytes.hex()} {front_bytes.hex()}")

    print("\n  Scanning memory (this may take a few seconds)...")
    t0 = time.monotonic()
    hits, regions, nbytes = scan_for_engine(proc, back_bytes, front_bytes)
    elapsed = time.monotonic() - t0
    print(f"  Scanned {regions} regions ({nbytes/1024/1024:.1f} MB) in {elapsed:.1f}s")

    if not hits:
        print("\n  NO ENGINE OBJECT FOUND.")
        print("  Make sure a song is playing (the engine is created when you start a song).")
        proc.close()
        return

    # Filter: skip hits that are inside the GameAssembly.dll .rdata section
    # (those are the source constants, not the engine object)
    module_end = proc.module_base + 0x4000000  # rough upper bound
    heap_hits = [h for h in hits if not (proc.module_base <= h < module_end)]
    rdata_hits = [h for h in hits if proc.module_base <= h < module_end]

    # Filter heap hits: the ACTIVE engine has a non-zero total_window.
    # Stale engines from previous songs linger with zeroed-out fields.
    live_hits = []
    for h in heap_hits:
        try:
            tw = proc.read_double(h + 0x20)
            if tw > 0.001:
                live_hits.append(h)
        except OSError:
            pass
    if live_hits:
        heap_hits = live_hits

    print(f"\n  Found {len(hits)} total hits:")
    if rdata_hits:
        print(f"    {len(rdata_hits)} in GameAssembly.dll (.rdata copies, expected)")
    if heap_hits:
        print(f"    {len(heap_hits)} on the heap (engine object candidates)")

    for h in heap_hits:
        print(f"\n  Candidate engine object at {h:#x}")
        try:
            tw = proc.read_double(h + 0x20)
            bw = proc.read_double(h + 0x30)
            fw = proc.read_double(h + 0x38)
            nc = proc.read_u32(h + 0x8C)
            flags = proc.read_u32(h + 0x198)
            prec = (flags & constants.PRECISION_MODE_BIT) != 0
            print(f"    total_window  = {tw:.6f} s  ({tw*1000:.1f} ms)")
            print(f"    back_window   = {bw:.6f} s  ({bw*1000:.1f} ms)")
            print(f"    front_window  = {fw:.6f} s  ({fw*1000:.1f} ms)")
            print(f"    note_count    = {nc}")
            print(f"    precision     = {prec}")

            # Quick poll to see if total_window changes
            print("    Polling total_window for 3s...")
            seen = set()
            deadline = time.monotonic() + 3.0
            while time.monotonic() < deadline:
                try:
                    w = proc.read_double(h + 0x20)
                    w_ms = round(w * 1000, 4)
                    if w_ms not in seen:
                        seen.add(w_ms)
                except Exception:
                    break
                time.sleep(0.005)
            print(f"    Saw {len(seen)} distinct values: {sorted(seen)[:10]}")
            if len(seen) > 1:
                print("    *** This is the live engine — window is changing per note! ***")
        except Exception as e:
            print(f"    Read error: {e}")

    proc.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
