"""The input driver: fire drum-lane keystrokes at the running game.

What this does, in one line: it presses and releases a keyboard key for a
drum lane, and it can wait until the song clock reaches a target time before
pressing.

Why the timing can be sloppy. Windows cannot deliver a keystroke at an exact
millisecond -- the scheduler jitters by several ms, which is huge next to an
85 ms hit window. That would be fatal if we needed the input to be precise.
It is not. The game measures its own hit delta and the debugger reads that
number back, so what we record is what the engine measured, not what we
intended. We only have to land the key NEAR the note edge. A plain poll-the-
clock-then-press loop is enough; there is no point chasing sub-millisecond
precision.

Two seams here can only be exercised by a live game, and are marked LIVE-ONLY
in the code:

  * _send_key -- the real ctypes SendInput call. It needs a real desktop and
    the game window in focus. Unit tests monkeypatch it.
  * schedule_hit driven by the engine's real song clock. Unit tests pass a
    fake clock instead.

Everything else -- the binding map and the polling logic -- is plain Python
and is unit-tested with fakes.
"""

from __future__ import annotations

import ctypes
import time
from ctypes import wintypes
from typing import Callable, Dict, Optional


# --- Placeholder drum-lane key bindings --------------------------------------
#
# OPEN QUESTION (see the spec's "Drum key bindings"): Clone Hero's drum lane
# keys are user-configurable and were NOT captured. The map below is a guess so
# the module imports and its logic can be tested. It is almost certainly wrong
# for any given install.
#
# Before a real active-probe run you MUST either read the real bindings out of
# the game's config or call set_binding() for each lane with a key you have
# confirmed. Do not trust these defaults silently.
#
# Lane numbering follows the probe-chart generator's `lane` argument. The
# values are Windows virtual-key codes.
#
# These are taken directly from the game's own Controller Remap screen
# (Options -> Controls, Player1 drums), which is the authoritative source:
#   Green=A  Red=S  Yellow=J  Blue=K  Orange/Kick=L  2X Kick=O
#   Yellow Cymbal=U  Blue Cymbal=Y  Green Cymbal=T
# Every drum lane is bound to the action of its own color; Orange is the kick.
DEFAULT_BINDINGS: Dict[int, int] = {
    0: 0x41,  # 'A'   -- Green pad      (bind screen 2026-09-25)
    1: 0x53,  # 'S'   -- Red pad        (bind screen 2026-09-25)
    2: 0x4A,  # 'J'   -- Yellow pad     (bind screen 2026-09-25)
    3: 0x4B,  # 'K'   -- Blue pad       (bind screen 2026-09-25)
    4: 0x4C,  # 'L'   -- Orange / Kick  (bind screen 2026-09-25)
    5: 0x55,  # 'U'   -- Yellow Cymbal  (bind screen 2026-09-25)
    6: 0x59,  # 'Y'   -- Blue Cymbal    (bind screen 2026-09-25)
    7: 0x54,  # 'T'   -- Green Cymbal   (bind screen 2026-09-25)
}


# --- Win32 SendInput plumbing (used only by the live seam) -------------------
#
# The INPUT structure Windows expects for SendInput. We only ever send keyboard
# events, but the struct's size must match what Windows knows (40 bytes on
# x64), so the union is padded out to the size of the largest member.

INPUT_KEYBOARD = 1
KEYEVENTF_KEYUP = 0x0002

# Pointer-sized unsigned integer, used for the dwExtraInfo field.
ULONG_PTR = ctypes.c_size_t


class _KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    ]


class _INPUTUNION(ctypes.Union):
    # Pad to 32 bytes so sizeof(INPUT) matches the real MOUSEINPUT-sized union.
    _fields_ = [("ki", _KEYBDINPUT), ("_pad", ctypes.c_byte * 32)]


class _INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("u", _INPUTUNION)]


class InputDriver:
    """Send drum-lane keystrokes, optionally scheduled against the song clock.

    Implements the InputDriver Protocol in interfaces.py.
    """

    def __init__(
        self,
        bindings: Optional[Dict[int, int]] = None,
        *,
        poll_interval_s: float = 0.0005,
        timeout_s: float = 30.0,
    ) -> None:
        # A copy of the defaults so callers can override lanes one at a time
        # without mutating the shared module-level dict.
        self._bindings: Dict[int, int] = dict(
            DEFAULT_BINDINGS if bindings is None else bindings
        )
        # How long to sleep between clock checks in schedule_hit. Small so we
        # stay near the edge; set to 0 in tests to spin without sleeping.
        self.poll_interval_s = poll_interval_s
        # Safety valve: if the clock never reaches the target (song ended,
        # paused, wrong clock source), give up instead of hanging forever.
        self.timeout_s = timeout_s

    # -- bindings -------------------------------------------------------------

    def set_binding(self, lane: int, vk: int) -> None:
        """Map a drum lane to a Windows virtual-key code.

        Use this to install the real, confirmed bindings before a run. See the
        DEFAULT_BINDINGS note above: the shipped defaults are placeholders.
        """
        self._bindings[lane] = vk

    def get_binding(self, lane: int) -> int:
        """Return the virtual-key code currently mapped to `lane`.

        Raises KeyError if the lane has no binding, so a missing lane fails
        loudly rather than pressing the wrong key.
        """
        return self._bindings[lane]

    # -- pressing keys --------------------------------------------------------

    def tap(self, lane: int) -> None:
        """Press then release the key for `lane` (key-down, key-up)."""
        vk = self.get_binding(lane)
        self._send_key(vk, key_up=False)
        self._send_key(vk, key_up=True)

    def _send_key(self, vk: int, key_up: bool) -> None:
        """LIVE-ONLY seam: one real key event via Win32 SendInput.

        This is the only place that touches the OS. It needs a real desktop and
        the game window in focus, so it cannot run under unit tests -- tests
        replace this method with a recorder. Everything above it is pure logic.
        """
        flags = KEYEVENTF_KEYUP if key_up else 0
        event = _INPUT()
        event.type = INPUT_KEYBOARD
        event.u.ki = _KEYBDINPUT(
            wVk=vk,
            wScan=0,
            dwFlags=flags,
            time=0,
            dwExtraInfo=0,
        )
        sent = ctypes.windll.user32.SendInput(
            1, ctypes.byref(event), ctypes.sizeof(_INPUT)
        )
        if sent != 1:
            err = ctypes.get_last_error()
            raise OSError(f"SendInput failed (returned {sent}, GetLastError={err})")

    # -- scheduling -----------------------------------------------------------

    def schedule_hit(
        self, lane: int, at_song_time: float, clock: Callable[[], float]
    ) -> None:
        """Wait until `clock()` reaches `at_song_time`, then tap `lane`.

        `clock` returns the engine's song time in seconds. This is the pure
        scheduling logic -- it takes the clock and the tap as things it calls,
        so a test can hand it a fake clock and a fake tap. Only over-precise
        timing is pointless here (see the module docstring), so this just polls.
        """
        deadline = time.monotonic() + self.timeout_s
        while clock() < at_song_time:
            if time.monotonic() >= deadline:
                raise TimeoutError(
                    f"clock never reached {at_song_time} within "
                    f"{self.timeout_s}s (last read below target)"
                )
            if self.poll_interval_s:
                time.sleep(self.poll_interval_s)
        self.tap(lane)
