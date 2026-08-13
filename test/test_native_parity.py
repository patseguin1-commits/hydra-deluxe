"""Parity tests for the native scoring core.

The C++ port of category_scores is only worth having if it is
indistinguishable from the Python one, so these compare the two directly
rather than checking the C++ against hand-written expectations.

Every test skips cleanly when the library is not built, so the suite still
passes on a machine with no C++ toolchain.

Run with:
    cd docs && PYTHONPATH=.. python -m unittest discover -s ../test -p "test_native_parity.py"

"""

import glob
import itertools
import os
import sys
import unittest

import hydra.hymisc as hymisc
import hydra.hynative as hynative
import hydra.hypath as hypath
import hydra.hyutil as hyutil


# (is_cymbal, is_accent, is_ghost) -- the six states a note can be in.
NOTE_KINDS = [
    (False, False, False),
    (False, True, False),
    (False, False, True),
    (True, False, False),
    (True, True, False),
    (True, False, True),
]


class FakeNote:
    """Duck-typed stand-in exposing only what category_scores reads."""

    def __init__(self, is_cymbal, is_accent, is_ghost):
        self._cymbal = is_cymbal
        self._accent = is_accent
        self._ghost = is_ghost

    def is_cymbal(self):
        return self._cymbal

    def is_accent(self):
        return self._accent

    def is_ghost(self):
        return self._ghost

    def is_dynamic(self):
        return self._accent or self._ghost


class FakeChord:
    def __init__(self, notes):
        self._notes = notes

    def notes(self, basesorted=False):
        # Already in the intended order; the real sort is exercised by the
        # whole-chart tests below.
        return list(self._notes)

    def activation_note(self):
        return self._notes[0] if self._notes else None


def python_category_scores(chord, combo):
    """category_scores with the native path forced off."""
    saved = hynative.ENABLED
    hynative.ENABLED = False
    try:
        return hypath.category_scores(chord, combo)
    finally:
        hynative.ENABLED = saved


@unittest.skipUnless(hynative.AVAILABLE,
                     f"native library not available: {hynative.STATUS}")
class TestCategoryScoresParity(unittest.TestCase):
    """Exhaustive comparison of the two implementations."""

    def _compare(self, notes, combo):
        chord = FakeChord(notes)
        expected = python_category_scores(chord, combo)
        saved = hynative.ENABLED
        hynative.ENABLED = True
        try:
            observed = hypath.category_scores(chord, combo)
        finally:
            hynative.ENABLED = saved
        self.assertEqual(
            observed, expected,
            f"mismatch for combo={combo} notes="
            f"{[(n._cymbal, n._accent, n._ghost) for n in notes]}",
        )

    def test_single_notes_across_combo(self):
        # 0..40 covers every multiplier transition (9/10, 19/20, 29/30).
        for kind in NOTE_KINDS:
            for combo in range(0, 41):
                self._compare([FakeNote(*kind)], combo)

    def test_chords_up_to_four_notes(self):
        # Combos chosen to sit on both sides of each multiplier boundary.
        for size in (2, 3, 4):
            for kinds in itertools.product(NOTE_KINDS, repeat=size):
                for combo in (0, 8, 9, 18, 19, 28, 29, 40):
                    self._compare([FakeNote(*k) for k in kinds], combo)

    def test_five_note_chords(self):
        # A kick plus all four pads is the widest chord the game allows.
        for kinds in itertools.product(NOTE_KINDS, repeat=5):
            self._compare([FakeNote(*k) for k in kinds], 0)

    def test_empty_chord(self):
        self._compare([], 0)

    def test_skipped_dynamics_flag(self):
        # FLAG_SKIPPED_DYNAMICS is off by default, so force it on to cover
        # the skipped_dynamic_reduction branch on both sides.
        saved = hymisc.FLAG_SKIPPED_DYNAMICS
        hymisc.FLAG_SKIPPED_DYNAMICS = True
        try:
            for kinds in itertools.product(NOTE_KINDS, repeat=3):
                for combo in (0, 9, 19, 29):
                    self._compare([FakeNote(*k) for k in kinds], combo)
        finally:
            hymisc.FLAG_SKIPPED_DYNAMICS = saved


@unittest.skipUnless(hynative.AVAILABLE,
                     f"native library not available: {hynative.STATUS}")
class TestWholeChartParity(unittest.TestCase):
    """End-to-end check: identical score and pathstring for every chart.

    This is the check that matters. Per-function parity can still miss a
    wiring mistake -- a flag packed wrong, or the sort order lost at the
    boundary -- and only a full analysis surfaces that.

    """

    CORPUS = os.path.join("..", "test", "input", "common")

    @classmethod
    def _charts(cls):
        found = []
        for ext in ("mid", "chart", "sng"):
            found += glob.glob(
                os.path.join(cls.CORPUS, "**", f"*.{ext}"), recursive=True)
        return sorted(found)

    def _analyze(self, path, native):
        saved = hynative.ENABLED
        hynative.ENABLED = native
        try:
            record = hyutil.analyze_chart_file(
                path, 'Expert', True, True, 'scores', 200, None)
            best = record.best_path()
            return best.totalscore(), best.pathstring()
        finally:
            hynative.ENABLED = saved

    def test_all_charts_identical(self):
        charts = self._charts()
        self.assertGreater(len(charts), 0, f"no charts under {self.CORPUS}")

        for path in charts:
            with self.subTest(chart=path):
                self.assertEqual(
                    self._analyze(path, native=True),
                    self._analyze(path, native=False),
                )


if __name__ == '__main__':
    unittest.main()
