"""Send drum inputs to Clone Hero and detect hits via memory polling.

Finds the engine object, then repeatedly sends kick inputs and watches
note_count to see if each one registered as a hit. This is a first test
that SendInput reaches the game and we can detect the result.

Start a song with kick notes, focus the Clone Hero window, then run:

    python tools\\ch_probe\\experiments\\send_inputs.py

The script waits 5 seconds before the first input so you can alt-tab
back to the game.
"""

from __future__ import annotations

import ctypes
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


def find_engine_ptr(proc):
    back_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_BACK), 8)
    front_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_FRONT), 8)
    hits, _, _ = scan_for_engine(proc, back_bytes, front_bytes)
    module_end = proc.module_base + 0x4000000
    heap_hits = [h for h in hits if not (proc.module_base <= h < module_end)]
    if not heap_hits:
        return None
    return heap_hits[0]


def read_note_count(proc, engine_ptr):
    return proc.read_u32(engine_ptr + constants.OFF_NOTE_COUNT)


def read_total_window(proc, engine_ptr):
    return proc.read_double(engine_ptr + 0x20)


def main() -> None:
    print(f"Looking for {constants.PROCESS_NAME}...")
    proc = open_process()
    proc.verify_targets()
    print(f"  PID {proc.pid}, addresses verified.")

    engine_ptr = find_engine_ptr(proc)
    if not engine_ptr:
        print("  No engine object found. Start a song first.")
        proc.close()
        return
    print(f"  Engine at {engine_ptr:#x}")

    nc = read_note_count(proc, engine_ptr)
    tw = read_total_window(proc, engine_ptr) * 1000
    print(f"  note_count = {nc}, total_window = {tw:.1f} ms")

    driver = InputDriver()
    lane = 1  # Red pad
    vk = driver.get_binding(lane)
    print(f"  Red key = VK {vk:#x} ('{chr(vk)}')")

    print(f"\n  *** Alt-tab to Clone Hero NOW. Inputs start in 5 seconds. ***\n")
    time.sleep(5)

    # Send 20 red-pad inputs spaced 0.5s apart and watch what happens
    results = []
    for i in range(20):
        nc_before = read_note_count(proc, engine_ptr)
        tw_before = read_total_window(proc, engine_ptr) * 1000

        driver.tap(lane)  # red pad

        time.sleep(0.05)  # give the engine a moment to process

        nc_after = read_note_count(proc, engine_ptr)
        tw_after = read_total_window(proc, engine_ptr) * 1000

        hit = nc_after != nc_before
        delta_nc = nc_after - nc_before
        tag = "HIT" if hit else "miss"

        results.append({
            "i": i, "hit": hit,
            "nc_before": nc_before, "nc_after": nc_after,
            "tw_before": tw_before, "tw_after": tw_after,
        })

        print(f"  Input {i+1:2d}: {tag}  note_count {nc_before} -> {nc_after}  "
              f"window {tw_before:.1f} -> {tw_after:.1f} ms")

        time.sleep(0.45)  # pace: one input every ~0.5s

    hits = sum(1 for r in results if r["hit"])
    print(f"\n  {hits}/{len(results)} inputs registered as hits.")
    if hits == 0:
        print("  No hits at all. Possible causes:")
        print("    - Clone Hero window wasn't focused (SendInput goes to the focused window)")
        print("    - The song had no kick notes near where we sent inputs")
        print("    - The key binding is wrong (check kick = A)")
    elif hits > 0:
        print("  SendInput is reaching the game and we can detect hits!")

    proc.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
