"""Tests for the pure parts of watch_window.py, walk_edges.py and live.py.

No game needed: synthetic samples, fake clocks, and the real probe-song layout
from probe_songs.py.
"""

from __future__ import annotations

import os
import struct
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import probe_songs as P  # noqa: E402
from tools.ch_probe.experiments import live  # noqa: E402
from tools.ch_probe.experiments import walk_edges as W  # noqa: E402
from tools.ch_probe.experiments import watch_window as WW  # noqa: E402


def window_map_notes() -> list[dict]:
    return P.manifest("x", P.window_map().notes)["notes"]


def samples_following(notes: list[dict], window_for) -> list[WW.Sample]:
    """A sample at every note time, window set by window_for(note)."""
    out = [WW.Sample(0.0, 0.0, 0, 0.0)]
    for n in notes:
        out.append(WW.Sample(float(n["time_ms"]), window_for(n), 0, 0.0))
    return out


class LiveSnapshotTest(unittest.TestCase):
    def test_decode_reads_each_field_at_its_offset(self):
        raw = bytearray(live.SNAPSHOT_SIZE)
        struct.pack_into("<d", raw, live.OFF_WINDOW, 0.17143)
        struct.pack_into("<I", raw, live.OFF_SCORE, 1234)
        struct.pack_into("<d", raw, live.OFF_CLOCK, 12.5)
        struct.pack_into("<I", raw, live.OFF_FLAGS, 0x1000)
        struct.pack_into("<d", raw, live.OFF_HIT_TIME, 12.49)
        s = live.decode_snapshot(bytes(raw))
        self.assertAlmostEqual(s.window_ms, 171.43)
        self.assertEqual(s.score, 1234)
        self.assertEqual(s.clock_s, 12.5)
        self.assertEqual(s.hit_time_s, 12.49)
        self.assertTrue(s.precision)


class WatchWindowTest(unittest.TestCase):
    def test_value_at_takes_the_last_change_at_or_before(self):
        chg = [(0.0, 1.0), (100.0, 2.0), (200.0, 3.0)]
        self.assertIsNone(WW.value_at(chg, -1.0))
        self.assertEqual(WW.value_at(chg, 100.0), 2.0)
        self.assertEqual(WW.value_at(chg, 199.9), 2.0)

    def test_block_spans_split_in_the_silence_and_cover_every_note(self):
        notes = window_map_notes()
        spans = WW.block_spans(notes)
        self.assertEqual(len(spans), len({n["block"] for n in notes}))
        for n in notes:
            owner = [b for b, s, e in spans if s <= n["time_ms"] < e]
            self.assertEqual(owner, [n["block"]])

    def test_run_values_come_from_the_run_not_the_markers(self):
        notes = window_map_notes()
        # Window follows the gap after each note, markers read 100.
        samples = samples_following(
            notes, lambda n: float(n["gap_after_ms"] or 999))
        lines = WW.window_report(samples, notes)
        cap = [l for l in lines if l.strip().startswith("400 ms gap")]
        self.assertEqual(cap, ["   400 ms gap: 400.00  DIFFERENT from 171.43"])
        floor = [l for l in lines if l.strip().startswith("30 ms gap")]
        self.assertEqual(floor, ["    30 ms gap: 30.00"])
        self.assertIn("Marker notes (100 ms gaps): 100.00", lines)

    def test_cap_verdict_passes_when_every_wide_run_reads_the_cap(self):
        notes = window_map_notes()
        samples = samples_following(
            notes, lambda n: 171.43 if (n["gap_after_ms"] or 0) >= 170 else 100.0)
        lines = WW.window_report(samples, notes)
        self.assertEqual(sum("matches the cap" in l for l in lines), 5)
        self.assertFalse(any("DIFFERENT" in l for l in lines))

    def test_frozen_window_says_so(self):
        notes = window_map_notes()
        samples = samples_following(notes, lambda n: 170.0)[1:]
        self.assertIn("The window never changed", WW.window_report(samples, notes)[0])

    def test_hit_time_report_matches_changes_to_notes(self):
        notes = [{"index": i, "time_ms": 3000 + 1000 * i} for i in range(3)]
        samples = [WW.Sample(2900.0, 171.43, 0, 0.0)]
        for i, n in enumerate(notes):
            samples.append(WW.Sample(n["time_ms"] + 8.0, 171.43, 100 * (i + 1),
                                     n["time_ms"] + 2.0))
        lines = WW.hit_time_report(samples, notes)
        self.assertIn("  +0x2e0 changed 3 times; the score rose 3 times.", lines)
        self.assertTrue(any("Looks like the hit time" in l for l in lines))


class WalkEdgesTest(unittest.TestCase):
    def test_default_plan_fits_edge_walk(self):
        plan = W.build_schedule((80, 92), (80, 92), 3, P.EDGE_WALK_NOTES)
        self.assertEqual(len(plan), 120)
        self.assertEqual(plan[:3], [0, 0, 0])
        self.assertEqual(plan[3:6], [80, 80, 80])
        self.assertEqual(plan[39:42], [92, 92, 92])
        self.assertEqual(plan[42:45], [-80, -80, -80])
        self.assertEqual(plan[78:81], [-92, -92, -92])
        self.assertEqual(set(plan[81:]), {0})

    def test_plan_that_does_not_fit_is_refused(self):
        with self.assertRaises(ValueError):
            W.build_schedule((0, 40), (0, 40), 3, 120)

    def test_parse_range(self):
        self.assertEqual(W.parse_range("68:80"), (68, 80))
        self.assertIsNone(W.parse_range("none"))
        with self.assertRaises(ValueError):
            W.parse_range("92:80")

    def test_song_clock_fills_in_after_a_fresh_change(self):
        t = [0.0]
        raw = [1.000]
        clock = W.SongClock(lambda: raw[0], now=lambda: t[0])
        self.assertEqual(clock.read(), (1.000, 1.000))   # first read: no anchor
        t[0] = 0.001; raw[0] = 1.016                      # change seen 1 ms later
        self.assertEqual(clock.read(), (1.016, 1.016))
        t[0] = 0.006
        r, est = clock.read()
        self.assertAlmostEqual(est, 1.021)
        t[0] = 1.0                                        # capped fill-in
        self.assertAlmostEqual(clock.read()[1], 1.016 + 0.05)

    def test_song_clock_ignores_a_change_seen_late(self):
        t = [0.0]
        raw = [1.0]
        clock = W.SongClock(lambda: raw[0], now=lambda: t[0])
        clock.read()
        t[0] = 0.5; raw[0] = 1.5                          # slept through it
        clock.read()
        t[0] = 0.51
        self.assertEqual(clock.read(), (1.5, 1.5))

    def test_summary_brackets_the_edge(self):
        def row(p, m, hit):
            return W.Row(0, 0.0, p, m, m, m if hit else None, hit)
        rows = [row(0, 0.5, True), row(85, 85.2, True), row(85, 85.4, True),
                row(86, 86.1, False), row(-85, -85.3, True), row(-86, -86.2, False)]
        lines = W.summarize(rows)
        self.assertIn("On-time notes: 1/1 hit.", lines)
        self.assertIn("  The late edge is between 85.4 and 86.1 ms.", lines)
        self.assertIn("  The early edge is between 85.3 and 86.2 ms.", lines)

    def test_summary_flags_overlap_and_all_miss(self):
        def row(p, m, hit):
            return W.Row(0, 0.0, p, m, m, None, hit)
        lines = W.summarize([row(85, 85.0, False), row(86, 86.0, True),
                             row(-80, -80.0, False)])
        self.assertTrue(any("overlap" in l for l in lines))
        self.assertTrue(any("early edge is below 80 ms" in l for l in lines))


if __name__ == "__main__":
    unittest.main()
