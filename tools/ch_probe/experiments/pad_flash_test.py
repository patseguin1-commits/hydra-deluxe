"""Game-side reception test: which drum keys does Clone Hero actually see?

The Notepad test proved SendInput delivers all eight keys to the OS. But in a
real run only A, S, L score; J, K, U, Y, T never do. So the question now is
narrow: does the GAME'S input layer receive J/K/U/Y/T at all?

This fires each drum key one at a time, slowly, with a long visible hold, into
the focused Clone Hero window. On the gameplay highway, pressing a lane's key
lights that lane's pad (or flashes the kick bar) even when no note is there --
so you can watch which keys the game registers, independent of note timing.

How to run it:
  1. Start the song in Clone Hero so the highway is up and responsive.
     (During the count-in or early play is fine -- ignore the falling notes;
     just watch the PADS at the bottom.)
  2. Run this script. It prints each key as it fires; watch the pad row.
  3. Tell me which pads lit: e.g. "green and red flashed, kick flashed,
     nothing for J/K/U/Y/T".

If A/S/L light their pads but J/K/U/Y/T light nothing -> the game's input
layer is dropping those five keys, and that's the real bug to chase.
If every pad lights -> the game receives all keys, and the miss is about
timing/targeting in play_chart, not reception.
"""

from __future__ import annotations

import ctypes
import os
import sys
import time

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe.input_driver import InputDriver

# Drum keys in lane order: (label, virtual-key code, which pad to watch).
KEYS = [
    ("A", 0x41, "GREEN pad"),
    ("S", 0x53, "RED pad"),
    ("J", 0x4A, "YELLOW pad"),
    ("K", 0x4B, "BLUE pad"),
    ("L", 0x4C, "KICK bar (flash across the lane)"),
    ("U", 0x55, "YELLOW cymbal"),
    ("Y", 0x59, "BLUE cymbal"),
    ("T", 0x54, "GREEN cymbal"),
]

HOLD_S = 0.5      # long enough to see the pad stay lit
GAP_S = 1.2       # clear gap between keys


def main() -> None:
    driver = InputDriver()

    user32 = ctypes.windll.user32
    ch_hwnd = user32.FindWindowW(None, "Clone Hero")
    if not ch_hwnd:
        print("Could not find the Clone Hero window. Start the game first.")
        return
    print(f"Found Clone Hero window: {ch_hwnd:#x}")
    print("Make sure the song is playing (highway visible). Starting in:")
    for n in (4, 3, 2, 1):
        print(f"  {n}...")
        time.sleep(1.0)

    for label, vk, watch in KEYS:
        user32.SetForegroundWindow(ch_hwnd)
        time.sleep(0.05)
        print(f"\n>>> Firing {label}  -> watch: {watch}")
        driver._send_key(vk, key_up=False)
        time.sleep(HOLD_S)
        driver._send_key(vk, key_up=True)
        time.sleep(GAP_S)

    print("\nDone. Which pads lit up, and which did nothing?")


if __name__ == "__main__":
    main()
