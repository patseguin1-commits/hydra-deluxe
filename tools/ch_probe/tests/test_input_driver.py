"""Unit tests for the input driver's pure logic.

These run with no game and no debugger. They test two things:

  * The binding map round-trips (set then get).
  * schedule_hit fires the tap exactly once, and only after the clock has
    reached the target time -- never before.

The live seams (the real SendInput, and a real song clock) are replaced with
fakes here. A recorder stands in for the key press; a canned sequence stands in
for the clock. What only a running Clone Hero can exercise -- that SendInput
actually reaches the game window -- is out of scope for these tests and is
commented as LIVE-ONLY in the module.

Run from the repo root:
    python -m pytest tools/ch_probe/tests/test_input_driver.py -q
    (or, if pytest is absent) python -m unittest tools.ch_probe.tests.test_input_driver
"""

from __future__ import annotations

import os
import sys
import unittest

# Make the repo root importable so `tools.ch_probe...` resolves regardless of
# where the test runner is launched from.
_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe.input_driver import DEFAULT_BINDINGS, LANE_NAMES, InputDriver, Lane


class _FakeClock:
    """A clock that returns each value in a list, one per call.

    The last value repeats forever, so a loop that overshoots the list does not
    crash -- it just keeps seeing the final time.
    """

    def __init__(self, values):
        self._values = list(values)
        self.calls = 0

    def __call__(self):
        i = min(self.calls, len(self._values) - 1)
        self.calls += 1
        return self._values[i]


def _make_driver():
    """An InputDriver whose key press is recorded, not really sent.

    Returns (driver, taps) where `taps` is a list the fake tap appends to.
    Sleeping is disabled so the polling loop spins instantly.
    """
    driver = InputDriver(poll_interval_s=0.0)
    presses = []
    # Replace the LIVE-ONLY SendInput seam with a recorder. tap() still runs its
    # real down/up logic on top of this.
    driver.send_key = lambda vk, key_up: presses.append((vk, key_up))
    return driver, presses


class TestBindings(unittest.TestCase):
    def test_defaults_are_present(self):
        driver = InputDriver()
        # Every lane in the shipped placeholder map is readable.
        for lane, vk in DEFAULT_BINDINGS.items():
            self.assertEqual(driver.get_binding(lane), vk)

    def test_set_binding_round_trips(self):
        driver = InputDriver()
        driver.set_binding(0, 0x51)  # 'Q'
        self.assertEqual(driver.get_binding(0), 0x51)

    def test_set_binding_adds_new_lane(self):
        driver = InputDriver(bindings={})
        driver.set_binding(7, 0x42)
        self.assertEqual(driver.get_binding(7), 0x42)

    def test_missing_lane_raises(self):
        driver = InputDriver(bindings={})
        with self.assertRaises(KeyError):
            driver.get_binding(99)

    def test_constructor_copies_defaults(self):
        # Overriding one driver's lane must not leak into the module defaults.
        driver = InputDriver()
        original = DEFAULT_BINDINGS[0]
        driver.set_binding(0, 0x99)
        self.assertEqual(DEFAULT_BINDINGS[0], original)


class TestTap(unittest.TestCase):
    def test_tap_sends_down_then_up(self):
        driver, presses = _make_driver()
        vk = driver.get_binding(0)
        driver.tap(0)
        self.assertEqual(presses, [(vk, False), (vk, True)])


class TestScheduleHit(unittest.TestCase):
    def test_fires_once_clock_reaches_target(self):
        driver, presses = _make_driver()
        # Clock climbs 0.0, 0.5, 1.0. Target is 1.0.
        clock = _FakeClock([0.0, 0.5, 1.0])
        driver.schedule_hit(lane=0, at_song_time=1.0, clock=clock)
        # Exactly one tap (down + up = two key events).
        self.assertEqual(len(presses), 2)

    def test_does_not_fire_before_target(self):
        driver, presses = _make_driver()
        # Record the clock reading at the moment the tap fires by wrapping the
        # recorder around a clock we can inspect.
        clock = _FakeClock([0.0, 0.2, 0.4, 0.6, 0.8, 1.0])
        fired_at = {}

        real_send = driver.send_key

        def spy(vk, key_up):
            fired_at.setdefault("clock_calls", clock.calls)
            real_send(vk, key_up)

        driver.send_key = spy
        driver.schedule_hit(lane=0, at_song_time=1.0, clock=clock)
        # The tap must not have fired on any reading below 1.0. The loop calls
        # the clock once per check; it fires only after the 6th reading (1.0),
        # so the recorded call count must be at least 6.
        self.assertGreaterEqual(fired_at["clock_calls"], 6)

    def test_fires_immediately_if_already_past(self):
        driver, presses = _make_driver()
        # Clock is already past the target on the first read.
        clock = _FakeClock([5.0])
        driver.schedule_hit(lane=0, at_song_time=1.0, clock=clock)
        self.assertEqual(len(presses), 2)
        # Only one clock reading was needed.
        self.assertEqual(clock.calls, 1)

    def test_timeout_when_clock_stalls(self):
        # A clock stuck below the target must not hang -- it raises instead.
        driver, presses = _make_driver()
        driver.timeout_s = 0.05
        clock = _FakeClock([0.0])  # forever below target 1.0
        with self.assertRaises(TimeoutError):
            driver.schedule_hit(lane=0, at_song_time=1.0, clock=clock)
        self.assertEqual(presses, [])


class TestKeyTable(unittest.TestCase):
    """One key table. A lane number means the same key everywhere."""

    def test_lanes_follow_the_bind_screen(self):
        keys = {lane: chr(DEFAULT_BINDINGS[lane]) for lane in Lane}
        self.assertEqual(keys, {
            Lane.GREEN: "A", Lane.RED: "S", Lane.YELLOW: "J", Lane.BLUE: "K",
            Lane.KICK: "L", Lane.YELLOW_CYMBAL: "U", Lane.BLUE_CYMBAL: "Y",
            Lane.GREEN_CYMBAL: "T"})

    def test_lane_numbers_are_unchanged(self):
        self.assertEqual([int(lane) for lane in Lane], list(range(8)))
        self.assertEqual(Lane.KICK, 4)

    def test_every_lane_has_a_short_name(self):
        self.assertEqual(set(LANE_NAMES), set(Lane))
        self.assertEqual(LANE_NAMES[Lane.KICK], "Kick")


class TestPressChord(unittest.TestCase):
    """The press play_chart proved at the game: all down, hold, all up."""

    def test_all_down_then_all_up(self):
        driver, presses = _make_driver()
        sent = driver.press_chord([Lane.RED, Lane.KICK],
                                  sleep=lambda s: presses.append(("sleep", s)))
        red, kick = DEFAULT_BINDINGS[Lane.RED], DEFAULT_BINDINGS[Lane.KICK]
        self.assertEqual(presses, [(red, False), (kick, False), ("sleep", 0.003),
                                   (red, True), (kick, True)])
        self.assertEqual(sent, [red, kick])

    def test_unbound_lane_is_skipped(self):
        driver = InputDriver(bindings={Lane.KICK: 0x4C})
        presses = []
        driver.send_key = lambda vk, key_up: presses.append((vk, key_up))
        driver.press_chord([Lane.GREEN, Lane.KICK], sleep=lambda s: None)
        self.assertEqual(presses, [(0x4C, False), (0x4C, True)])


if __name__ == "__main__":
    unittest.main()
