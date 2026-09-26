"""Delivery test: does SendInput actually deliver every drum key?

Why this exists. In play_chart.py the keys A, S, L score in Clone Hero but
J, K, U, Y, T never do -- a clean 100% split on five keys, even though the
game's own registry map binds all eight correctly and send_key runs the
SAME code for every key. That should be impossible from the Python side, so
this test checks delivery OUTSIDE the game: it types the eight drum letters
into whatever window has focus.

How to read the result. Put the cursor in an empty Notepad (or any text box),
run this, and DON'T touch the keyboard during the countdown. It types, with a
space between each so you can see exactly which arrived:

    a s j k l u y t

If all eight letters appear -> SendInput delivers every key, so the miss is
game-side (focus / raw input / something eating them), NOT my send path.
If only "a s   l" appear (j, k, u, y, t missing) -> the send path itself is
dropping those five keys, and that's the bug to fix.

This uses the real InputDriver.send_key, the same one play_chart.py uses, so
whatever it shows applies directly to the real run.
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

from tools.ch_probe.input_driver import DEFAULT_BINDINGS, LANE_NAMES, InputDriver

# The eight drum keys, in lane order, as (label, virtual-key code), from the
# one key table in input_driver.py.
KEYS = [(f"{chr(vk)} ({LANE_NAMES[lane]})", vk) for lane, vk in DEFAULT_BINDINGS.items()]

VK_SPACE = 0x20


def main() -> None:
    driver = InputDriver()

    print(__doc__)
    print("Focus an empty Notepad window now. Typing starts in:")
    for n in (5, 4, 3, 2, 1):
        print(f"  {n}...")
        time.sleep(1.0)
    print("Typing:", " ".join(lbl.split()[0] for lbl, _ in KEYS))

    for label, vk in KEYS:
        driver.send_key(vk, key_up=False)
        time.sleep(0.02)
        driver.send_key(vk, key_up=True)
        time.sleep(0.02)
        # a space between letters so each key's arrival is unambiguous
        driver.send_key(VK_SPACE, key_up=False)
        time.sleep(0.02)
        driver.send_key(VK_SPACE, key_up=True)
        time.sleep(0.08)

    print("\nDone. Look at Notepad and tell me which letters appeared.")
    print("Expected if delivery is perfect:  a s j k l u y t")


if __name__ == "__main__":
    main()
