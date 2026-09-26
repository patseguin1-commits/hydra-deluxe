"""play_chart.py's .chart path turns each tick's notes into input lanes.

On a pro-drums .chart, notes 66/67/68 on the same tick as a yellow, blue or
green gem make that gem a CYMBAL; without its marker the gem is a tom. Note 32
is the 2x kick. One gem is one key: pressing a tom and a cymbal key for one
gem is an overhit. These tests feed a tiny .chart through parse_chart.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe.experiments import play_chart  # noqa: E402

# Resolution 192 at 120 BPM: 192 ticks = one beat = 0.5 s.
_CHART = """[Song]
{
  Resolution = 192
}
[SyncTrack]
{
  0 = TS 4
  0 = B 120000
}
[ExpertDrums]
{
  0 = N 2 0
  192 = N 2 0
  192 = N 66 0
  384 = N 3 0
  384 = N 67 0
  576 = N 4 0
  576 = N 68 0
  768 = N 32 0
  960 = N 0 0
  960 = N 1 0
  1152 = N 4 0
  1152 = N 37 0
}
"""


def _parse():
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "notes.chart")
        with open(path, "w", encoding="utf-8") as f:
            f.write(_CHART)
        return play_chart.parse_chart(path)


class ChartPathTest(unittest.TestCase):
    def test_each_tick_gets_one_lane_per_gem(self):
        resolution, _tempos, notes = _parse()
        self.assertEqual(resolution, 192)
        self.assertEqual([(n.tick, list(n.lanes)) for n in notes], [
            (0, [2]),       # yellow tom (J)
            (192, [5]),     # yellow cymbal (U)
            (384, [6]),     # blue cymbal (Y)
            (576, [7]),     # green cymbal (T)
            (768, [4]),     # 2x kick presses the kick (L)
            (960, [1, 4]),  # red + kick
            (1152, [0]),    # green tom (A); the accent marker 37 presses nothing
        ])

    def test_times_follow_the_tempo(self):
        _resolution, _tempos, notes = _parse()
        self.assertAlmostEqual(notes[1].time_s, 0.5)
        self.assertAlmostEqual(notes[-1].time_s, 3.0)

    def test_lane_helper(self):
        self.assertEqual(play_chart.chart_notes_to_lanes([2, 66]), [5])
        self.assertEqual(play_chart.chart_notes_to_lanes([3]), [3])
        self.assertEqual(play_chart.chart_notes_to_lanes([66]), [])
        self.assertEqual(play_chart.chart_notes_to_lanes([0, 32]), [4])


class SharedPiecesTest(unittest.TestCase):
    """play_chart runs on the tracked modules, not private copies."""

    def test_no_private_copies_are_left(self):
        for name in ("find_active_engine", "OFF_SONG_CLOCK", "OFF_SCORE",
                     "NOTE_TO_LANE", "scan_for_engine"):
            self.assertFalse(hasattr(play_chart, name), name)

    def test_lane_names_come_from_the_key_table(self):
        from tools.ch_probe import input_driver
        self.assertIs(play_chart.LANE_NAMES, input_driver.LANE_NAMES)


if __name__ == "__main__":
    unittest.main()
