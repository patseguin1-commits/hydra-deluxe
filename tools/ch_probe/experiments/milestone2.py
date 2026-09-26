"""Milestone 2: attach the debugger and capture the engine object.

Opens the running Clone Hero, attaches as a Win32 debugger, breakpoints
the DrumsEngine constructor, and waits for you to start a song. When the
constructor fires, reads the initial window values off the live engine
object to prove the whole chain works: process → debugger → engine fields.

Run this BEFORE you start a song. It will print "Waiting for you to start
a song..." — that's your cue to pick any chart and press Enter/Green.

    python tools\\ch_probe\\experiments\\milestone2.py
"""

from __future__ import annotations

import os
import sys
import time

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants
from tools.ch_probe.process import open_process
from tools.ch_probe.debugger import Debugger
from tools.ch_probe.engine import EngineModel


def main() -> None:
    print(f"Looking for {constants.PROCESS_NAME}...")
    proc = open_process()
    print(f"  PID {proc.pid}, GameAssembly.dll at {proc.module_base:#x}")

    proc.verify_targets()
    print("  Address pipeline verified.")

    print("\nAttaching debugger...")
    dbg = Debugger()
    dbg.attach(proc.pid)
    print("  Debugger attached.")

    engine = EngineModel(proc, dbg)
    ctor_addr = proc.resolve(constants.RVA_DRUMS_ENGINE_CTOR)
    print(f"  Constructor breakpoint at {ctor_addr:#x}")
    print("\n*** Waiting for you to start a song... ***")
    print("    (pick any chart and press Enter/Green)\n")

    t0 = time.monotonic()
    engine.capture_object()
    elapsed = time.monotonic() - t0
    print(f"  Engine object captured at {engine.object_ptr:#x}  ({elapsed:.1f}s wait)")

    print("\nReading engine fields:")
    print(f"  total_window  = {engine.total_window():.6f} s  ({engine.total_window()*1000:.1f} ms)")
    print(f"  back_window   = {engine.back_window():.6f} s  ({engine.back_window()*1000:.1f} ms)")
    print(f"  front_window  = {engine.front_window():.6f} s  ({engine.front_window()*1000:.1f} ms)")
    print(f"  precision     = {engine.precision_mode()}")
    print(f"  note_count    = {engine.note_count()}")

    try:
        clock = engine.song_clock()
        print(f"  song_clock    = {clock:.4f} s  (UNCONFIRMED offset)")
    except Exception as e:
        print(f"  song_clock    = ERROR: {e}")

    print("\nPolling total_window for 5 seconds to see if it changes per note...")
    seen = set()
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        try:
            w = engine.total_window()
            w_ms = round(w * 1000, 2)
            if w_ms not in seen:
                seen.add(w_ms)
                print(f"  window = {w_ms:.2f} ms")
        except Exception:
            pass
        time.sleep(0.01)

    print(f"\n  Saw {len(seen)} distinct window values in 5s.")
    if len(seen) > 1:
        print("  The window IS changing per note — the formula is active.")
    else:
        print("  Window stayed constant. Either a slow song or the field isn't updating.")

    dbg.stop()
    proc.close()
    print("\nDone. Debugger detached.")


if __name__ == "__main__":
    main()
