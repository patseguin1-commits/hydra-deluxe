"""React to notes by watching total_window and firing inputs on changes.

When total_window changes, the engine just computed a new window for an
incoming note — meaning a note is RIGHT HERE in time. We immediately
send a Red input and snapshot the engine to see which fields changed.

Play any song with drums, then run:
    python tools\\ch_probe\\experiments\\reactive_inputs.py

Alt-tab back to Clone Hero within 3 seconds.
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
from tools.ch_probe.input_driver import InputDriver
from tools.ch_probe.experiments.find_engine import scan_for_engine


def find_live_engine(proc):
    """Find the engine object with a non-zero total_window (the active one)."""
    back_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_BACK), 8)
    front_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_FRONT), 8)
    hits, _, _ = scan_for_engine(proc, back_bytes, front_bytes)
    module_end = proc.module_base + 0x4000000
    heap_hits = [h for h in hits if not (proc.module_base <= h < module_end)]

    for h in heap_hits:
        try:
            tw = proc.read_double(h + 0x20)
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

    engine_ptr, tw = find_live_engine(proc)
    if not engine_ptr:
        print("  No live engine found. Is a song actively playing?")
        proc.close()
        return
    print(f"  Engine at {engine_ptr:#x}, window={tw*1000:.1f} ms")

    driver = InputDriver()
    lane = 1  # Red pad = S
    print(f"  Sending Red pad (S) when notes are detected.")

    SNAP_SIZE = 0x200

    def read_window():
        return proc.read_double(engine_ptr + 0x20)

    def snapshot():
        return proc.read(engine_ptr, SNAP_SIZE)

    print(f"\n  *** Alt-tab to Clone Hero NOW. Starts in 3 seconds. ***\n")
    time.sleep(3)

    last_window = read_window()
    notes_seen = 0
    hits = 0
    max_notes = 30

    print(f"  Watching for window changes (up to {max_notes} notes, 60s max)...\n")
    start = time.perf_counter()

    try:
        while notes_seen < max_notes and (time.perf_counter() - start) < 60:
            w = read_window()

            if abs(w - last_window) > 0.0001:
                snap_before = snapshot()

                driver.tap(lane)
                time.sleep(0.02)

                snap_after = snapshot()

                changed = []
                for off in range(0, SNAP_SIZE - 3, 4):
                    bv = struct.unpack_from("<I", snap_before, off)[0]
                    av = struct.unpack_from("<I", snap_after, off)[0]
                    if bv != av:
                        changed.append((off, bv, av))

                notes_seen += 1
                w_ms = w * 1000

                if changed:
                    hits += 1
                    tag = "HIT?"
                    changes_str = ", ".join(
                        f"+{off:#x}: {bv}->{av}"
                        for off, bv, av in changed[:6]
                    )
                else:
                    tag = "miss"
                    changes_str = "(no fields changed)"

                print(f"  Note {notes_seen:2d}: {tag}  window={w_ms:7.1f} ms  {changes_str}")
                last_window = w

            time.sleep(0.001)

    except KeyboardInterrupt:
        print("\n  Stopped.")
    except OSError as e:
        print(f"\n  Lost process: {e}")

    elapsed = time.perf_counter() - start
    print(f"\n  {notes_seen} notes detected, {hits} had field changes, in {elapsed:.1f}s")
    proc.close()
    print("Done.")


if __name__ == "__main__":
    main()
