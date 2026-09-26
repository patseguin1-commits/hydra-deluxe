"""Tests for the probe-chart generator.

All of this runs with no game and no debugger. We generate chart text, parse it
back with a small reader, and check the numbers. That covers the only thing this
module has to get right: notes land the correct number of ticks apart, the
sections appear in order, and no two pairs overlap.

Written to run under pytest (`python -m pytest <path> -q`). It also runs under
plain unittest (`python -m unittest`) because every check is a unittest assert.
"""

from __future__ import annotations

import os
import re
import sys
import unittest

# Make the package importable when this file is run directly (the repo root is
# two levels up from tools/ch_probe/tests/).
_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe.probe_chart import (  # noqa: E402
    DEFAULT_SPACINGS_MS,
    build_probe_chart_text,
    generate_probe_chart,
    ms_to_ticks,
    probe_note_ticks,
)


def parse_drum_ticks(text: str) -> list[int]:
    """Pull the note ticks out of an [ExpertDrums] section, in file order.

    This is the round-trip half of the test: it reads back exactly what the
    generator wrote, so we can compare tick deltas to the milliseconds we asked
    for. It only understands the note line the generator emits.
    """
    lines = text.splitlines()
    in_drums = False
    ticks: list[int] = []
    for line in lines:
        stripped = line.strip()
        if stripped == "[ExpertDrums]":
            in_drums = True
            continue
        if in_drums:
            if stripped == "}":
                break
            match = re.match(r"^(\d+)\s*=\s*N\s+(\d+)\s+(\d+)$", stripped)
            if match:
                ticks.append(int(match.group(1)))
    return ticks


class TestMsToTicks(unittest.TestCase):
    def test_known_conversion_default(self):
        # 192 res, 120 bpm: tick rate is 192*120/60 = 384 ticks/sec, so 0.384
        # ticks per ms. 100 ms rounds to 38 ticks.
        self.assertEqual(ms_to_ticks(100, 192, 120.0), 38)

    def test_quarter_note_worth_of_ms(self):
        # At 120 bpm a quarter note is 500 ms and must be exactly `resolution`
        # ticks, whatever the resolution.
        for res in (192, 480, 96):
            self.assertEqual(ms_to_ticks(500, res, 120.0), res)

    def test_one_second_at_various_bpm(self):
        # One second is always bpm/60 quarter notes = resolution*bpm/60 ticks.
        self.assertEqual(ms_to_ticks(1000, 480, 120.0), 960)
        self.assertEqual(ms_to_ticks(1000, 192, 200.0), 640)

    def test_rounds_to_nearest(self):
        # 30 ms at the default is 11.52 ticks, which rounds to 12.
        self.assertEqual(ms_to_ticks(30, 192, 120.0), 12)


class TestSectionsPresentAndOrdered(unittest.TestCase):
    def test_required_sections_in_order(self):
        text = build_probe_chart_text([100, 200])
        i_song = text.find("[Song]")
        i_sync = text.find("[SyncTrack]")
        i_drums = text.find("[ExpertDrums]")
        self.assertNotEqual(i_song, -1, "missing [Song]")
        self.assertNotEqual(i_sync, -1, "missing [SyncTrack]")
        self.assertNotEqual(i_drums, -1, "missing [ExpertDrums]")
        self.assertLess(i_song, i_sync)
        self.assertLess(i_sync, i_drums)

    def test_resolution_and_tempo_written(self):
        text = build_probe_chart_text([100], resolution=480, bpm=140.0)
        self.assertIn("Resolution = 480", text)
        # Tempo is BPM * 1000 as an integer.
        self.assertIn("0 = B 140000", text)

    def test_braces_balanced(self):
        text = build_probe_chart_text([100, 150])
        self.assertEqual(text.count("{"), text.count("}"))


class TestPairsAndSpacing(unittest.TestCase):
    def test_two_notes_per_spacing(self):
        spacings = [50, 100, 200]
        ticks = parse_drum_ticks(build_probe_chart_text(spacings))
        self.assertEqual(len(ticks), 2 * len(spacings))

    def test_each_pair_is_the_right_delta_apart(self):
        spacings = list(DEFAULT_SPACINGS_MS)
        res, bpm = 192, 120.0
        ticks = parse_drum_ticks(
            build_probe_chart_text(spacings, resolution=res, bpm=bpm)
        )
        for idx, ms in enumerate(spacings):
            first = ticks[2 * idx]
            second = ticks[2 * idx + 1]
            self.assertEqual(
                second - first,
                ms_to_ticks(ms, res, bpm),
                f"spacing {ms} ms produced the wrong tick delta",
            )

    def test_pairs_do_not_overlap(self):
        # Every note must sit strictly after the previous one, and the gap
        # between one pair's second note and the next pair's first note must be
        # far larger than any single spacing -- that is the silent moat.
        spacings = list(DEFAULT_SPACINGS_MS)
        res, bpm = 192, 120.0
        ticks = parse_drum_ticks(
            build_probe_chart_text(spacings, resolution=res, bpm=bpm)
        )
        self.assertEqual(ticks, sorted(ticks))
        biggest_spacing = max(ms_to_ticks(ms, res, bpm) for ms in spacings)
        # Between-pair gap = ticks[2] - ticks[1], ticks[4] - ticks[3], ...
        for idx in range(1, len(spacings)):
            prev_second = ticks[2 * idx - 1]
            this_first = ticks[2 * idx]
            gap = this_first - prev_second
            self.assertGreater(
                gap,
                biggest_spacing,
                "pairs are too close -- windows could interact",
            )

    def test_ticks_are_strictly_increasing(self):
        ticks = parse_drum_ticks(build_probe_chart_text([300, 30, 211]))
        for a, b in zip(ticks, ticks[1:]):
            self.assertLess(a, b)

    def test_probe_note_ticks_are_the_written_ticks(self):
        # 480 ticks per beat at 125 BPM: one tick is one millisecond. Lead-in
        # is two 4-beat bars (3840), each pair is followed by a 4-bar pad (7680).
        ticks = probe_note_ticks([211, 30], resolution=480, bpm=125.0)
        self.assertEqual(ticks, [3840, 4051, 11731, 11761])
        text = build_probe_chart_text([211, 30], resolution=480, bpm=125.0)
        self.assertEqual(parse_drum_ticks(text), ticks)


class TestFileWrite(unittest.TestCase):
    def test_generate_writes_a_readable_chart(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "probe.chart")
            generate_probe_chart([100, 200], path, resolution=192, bpm=120.0)
            with open(path, "r", encoding="utf-8") as handle:
                text = handle.read()
            ticks = parse_drum_ticks(text)
            self.assertEqual(len(ticks), 4)
            self.assertEqual(ticks[1] - ticks[0], ms_to_ticks(100, 192, 120.0))
            self.assertEqual(ticks[3] - ticks[2], ms_to_ticks(200, 192, 120.0))


if __name__ == "__main__":
    unittest.main()
