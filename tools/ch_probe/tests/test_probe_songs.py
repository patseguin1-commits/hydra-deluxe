"""Tests for the probe-song layout. Pure text/data; no game, no ffmpeg."""

from __future__ import annotations

import os
import re
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import probe_songs as P  # noqa: E402


def chart_ticks(text: str) -> list[int]:
    return [int(m) for m in re.findall(r"^\s*(\d+) = N 0 0$", text, re.M)]


class ProbeSongsTest(unittest.TestCase):
    def test_one_tick_is_one_ms(self):
        self.assertEqual(P.RESOLUTION * P.BPM / 60000.0, 1.0)

    def test_chart_ticks_match_manifest_times(self):
        for build in (P.window_map, P.edge_walk):
            notes = build().notes
            ticks = chart_ticks(P.chart_text("x", notes))
            self.assertEqual(ticks, [n.time_ms for n in notes])
            self.assertEqual(ticks, sorted(set(ticks)))

    def test_run_notes_have_the_same_gap_both_sides(self):
        rows = P.manifest("x", P.window_map().notes)["notes"]
        for gap in P.CAP_GAPS_MS + P.FLOOR_GAPS_MS:
            label = "cap" if gap in P.CAP_GAPS_MS else "floor"
            run = [r for r in rows if r["block"] == f"{label}_{gap}" and r["role"] == "run"]
            self.assertEqual(len(run), P.RUN_NOTES)
            for r in run[:-1]:
                self.assertEqual(r["gap_before_ms"], gap)
                self.assertEqual(r["gap_after_ms"], gap)

    def test_uneven_middle_notes(self):
        rows = P.manifest("x", P.window_map().notes)["notes"]
        for before, after in P.UNEVEN_GAPS_MS:
            mid = [r for r in rows if r["block"] == f"uneven_{before}_{after}"
                   and r["role"] == "middle"]
            self.assertEqual(len(mid), 1)
            self.assertEqual((mid[0]["gap_before_ms"], mid[0]["gap_after_ms"]),
                             (before, after))

    def test_blocks_are_separated_by_silence(self):
        rows = P.manifest("x", P.window_map().notes)["notes"]
        for prev, cur in zip(rows, rows[1:]):
            if prev["block"] != cur["block"]:
                self.assertGreaterEqual(cur["gap_before_ms"], P.SILENCE_MS)

    def test_edge_walk_is_isolated(self):
        rows = P.manifest("x", P.edge_walk().notes)["notes"]
        self.assertEqual(len(rows), P.EDGE_WALK_NOTES)
        self.assertTrue(all(r["gap_before_ms"] in (None, P.EDGE_WALK_GAP_MS) for r in rows))

    def test_song_ini_has_no_delay(self):
        self.assertIn("delay = 0\n", P.song_ini("x", 1000))


if __name__ == "__main__":
    unittest.main()
