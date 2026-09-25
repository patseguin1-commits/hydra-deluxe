"""Unit tests for the pure analysis layer.

Everything here runs with fabricated numbers. No Clone Hero, no debugger, no
ctypes. Written as unittest.TestCase so it runs under both `python -m pytest`
and `python -m unittest`.
"""

from __future__ import annotations

import os
import sys
import unittest

# Make the repo root importable however the test is launched (pytest, unittest,
# or run directly). tests/ is three levels below the repo root.
_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe.experiments import analysis  # noqa: E402


class TestFindWindowEdge(unittest.TestCase):
    def test_clean_crossover_lands_on_the_midpoint(self):
        # Hits below 85, misses above. The edge should be the midpoint of the
        # boundary pair, 84 and 86 -> 85.
        rows = [
            (80.0, True), (82.0, True), (84.0, True),
            (86.0, False), (88.0, False), (90.0, False),
        ]
        result = analysis.find_window_edge(rows)
        self.assertIsNotNone(result.edge_ms)
        self.assertAlmostEqual(result.edge_ms, 85.0, places=6)
        self.assertEqual(result.errors, 0)
        self.assertEqual(result.n_hits, 3)
        self.assertEqual(result.n_misses, 3)

    def test_works_on_signed_deltas_via_magnitude(self):
        # A front-edge sweep: early hits are negative, early misses more
        # negative. The detector works on magnitude, so the edge is ~37.5.
        rows = [
            (-30.0, True), (-35.0, True), (-37.0, True),
            (-38.0, False), (-40.0, False), (-45.0, False),
        ]
        result = analysis.find_window_edge(rows)
        self.assertAlmostEqual(result.edge_ms, 37.5, places=6)
        self.assertEqual(result.errors, 0)

    def test_one_noisy_outlier_stays_within_tolerance(self):
        # One late hit sits among the misses. The best split still puts the edge
        # near 85; assert it is within a few ms.
        rows = [
            (80.0, True), (82.0, True), (84.0, True), (91.0, True),
            (86.0, False), (88.0, False), (90.0, False), (92.0, False),
        ]
        result = analysis.find_window_edge(rows)
        self.assertIsNotNone(result.edge_ms)
        self.assertTrue(
            abs(result.edge_ms - 85.0) <= 5.0,
            msg=f"edge {result.edge_ms} strayed from ~85",
        )
        self.assertGreaterEqual(result.errors, 1)

    def test_empty_input_returns_no_edge(self):
        result = analysis.find_window_edge([])
        self.assertIsNone(result.edge_ms)
        self.assertEqual(result.n_hits, 0)
        self.assertEqual(result.n_misses, 0)

    def test_all_hits_edge_sits_above_the_data(self):
        rows = [(10.0, True), (20.0, True), (30.0, True)]
        result = analysis.find_window_edge(rows)
        self.assertEqual(result.errors, 0)
        self.assertGreater(result.edge_ms, 30.0)


class TestClampVerdict(unittest.TestCase):
    def test_stored_tracks_raw_means_no_clamp(self):
        # Stored equals raw all the way up the parabola, well past the 85 cap.
        rows = [
            (100.0, 80.0, 80.0),
            (150.0, 87.0, 87.0),
            (190.0, 88.0, 88.0),
            (211.0, 89.5, 89.5),
        ]
        result = analysis.clamp_verdict(rows, cap_ms=C.EXPECT_NORMAL_BACK_MS)
        self.assertEqual(result.verdict, analysis.CLAMP_ABSENT)
        self.assertAlmostEqual(result.tracked_fraction, 1.0, places=6)
        self.assertEqual(result.n_above, 3)

    def test_stored_flat_at_cap_means_clamp(self):
        # Raw climbs past 85, but stored is pinned at the cap.
        rows = [
            (100.0, 80.0, 80.0),
            (150.0, 87.0, 85.0),
            (190.0, 88.0, 85.0),
            (211.0, 89.5, 85.0),
        ]
        result = analysis.clamp_verdict(rows, cap_ms=C.EXPECT_NORMAL_BACK_MS)
        self.assertEqual(result.verdict, analysis.CLAMP_PRESENT)
        self.assertAlmostEqual(result.flat_fraction, 1.0, places=6)
        self.assertEqual(result.n_above, 3)

    def test_never_crossing_the_cap_is_inconclusive(self):
        # No note pushes the raw window past 85, so nothing could reveal a clamp.
        rows = [
            (30.0, 40.0, 40.0),
            (60.0, 60.0, 60.0),
            (100.0, 80.0, 80.0),
        ]
        result = analysis.clamp_verdict(rows, cap_ms=C.EXPECT_NORMAL_BACK_MS)
        self.assertEqual(result.verdict, analysis.CLAMP_INCONCLUSIVE)
        self.assertEqual(result.n_above, 0)

    def test_split_evidence_is_inconclusive(self):
        # Half the above-cap notes track, half flat-line: no clear majority.
        rows = [
            (150.0, 88.0, 88.0),
            (160.0, 88.0, 88.0),
            (190.0, 89.0, 85.0),
            (211.0, 89.0, 85.0),
        ]
        result = analysis.clamp_verdict(rows, cap_ms=C.EXPECT_NORMAL_BACK_MS)
        self.assertEqual(result.verdict, analysis.CLAMP_INCONCLUSIVE)


class TestParabolaPredictor(unittest.TestCase):
    def test_simple_hand_computed_point(self):
        # With divisor 1 the pre-scale is a no-op, so t == spacing. With
        # c1=1, c2=0, c3=1, c4=0 the formula reduces to plain t. So window(50)=50.
        value = analysis.predicted_window_normal(
            50.0, c1=1.0, c2=0.0, c3=1.0, c4=0.0, divisor=1.0, exponent=2.0
        )
        self.assertAlmostEqual(value, 50.0, places=9)

    def test_quadratic_and_offset_hand_computed(self):
        # divisor 1, exponent 2: window = (t*2 - t^2*0.01)*10 - 5.
        # At t=10: (20 - 1)*10 - 5 = 190 - 5 = 185.
        value = analysis.predicted_window_normal(
            10.0, c1=2.0, c2=0.01, c3=10.0, c4=5.0, divisor=1.0, exponent=2.0
        )
        self.assertAlmostEqual(value, 185.0, places=9)

    def test_prescale_by_divisor_is_applied(self):
        # divisor 1000, so t = spacing*1000. With c1=1, c2=0, c3=1, c4=0 the
        # window is (t*1)/1000 = spacing*1000/1000 = spacing. window(0.2)=0.2.
        value = analysis.predicted_window_normal(
            0.2, c1=1.0, c2=0.0, c3=1.0, c4=0.0, divisor=1000.0, exponent=2.0
        )
        self.assertAlmostEqual(value, 0.2, places=9)

    def test_precision_shape_hand_computed(self):
        # divisor 1: window = c0 - (t*c1 - t^2*c2)*c3.
        # At t=10, c0=100, c1=2, c2=0.01, c3=10: 100 - (20-1)*10 = 100-190 = -90.
        value = analysis.predicted_window_precision(
            10.0, c1=2.0, c2=0.01, c3=10.0, c0=100.0, divisor=1.0, exponent=2.0
        )
        self.assertAlmostEqual(value, -90.0, places=9)


class TestSummarizeActive(unittest.TestCase):
    def test_groups_and_pairs_measured_with_predicted(self):
        # Two spacings, each a clean crossover at a known edge.
        rows = [
            (190.0, 80.0, True), (190.0, 84.0, True),
            (190.0, 86.0, False), (190.0, 90.0, False),
            (100.0, 60.0, True), (100.0, 64.0, True),
            (100.0, 66.0, False), (100.0, 70.0, False),
        ]
        constants = {
            "c1": 1.0, "c2": 0.0, "c3": 1.0, "c4": 0.0,
            "divisor": 1.0, "exponent": 2.0,
        }
        summary = analysis.summarize_active(rows, formula_constants=constants)
        # Sorted by spacing: 100 then 190.
        self.assertEqual([s.spacing_ms for s in summary], [100.0, 190.0])
        self.assertAlmostEqual(summary[0].measured_edge_ms, 65.0, places=6)
        self.assertAlmostEqual(summary[1].measured_edge_ms, 85.0, places=6)
        # Predicted with these trivial constants is just the spacing itself.
        self.assertAlmostEqual(summary[0].predicted_edge_ms, 100.0, places=6)
        self.assertAlmostEqual(summary[1].predicted_edge_ms, 190.0, places=6)

    def test_without_constants_predicted_is_none(self):
        rows = [(100.0, 60.0, True), (100.0, 70.0, False)]
        summary = analysis.summarize_active(rows)
        self.assertEqual(len(summary), 1)
        self.assertIsNone(summary[0].predicted_edge_ms)


from tools.ch_probe import constants as C  # noqa: E402
from tools.ch_probe import probe_chart  # noqa: E402
from tools.ch_probe.engine import EngineModel  # noqa: E402


class _ConstProcess:
    """Just enough of a ProcessHandle for EngineModel.constants(): every RVA
    reads back as a distinct float, so a mis-mapped key shows up."""

    def read_const_double(self, rva):
        return float(rva % 100003) + 0.25


class TestNormalFormulaConstants(unittest.TestCase):
    def test_maps_the_real_engine_shape(self):
        decoded = EngineModel(_ConstProcess(), None).constants()
        got = analysis.normal_formula_constants(decoded)
        self.assertIsNotNone(got)
        for name in C.RVA_FORMULA_NORMAL:
            self.assertEqual(got[name], decoded[C.CONST_KEY_PREFIX_NORMAL + name])
        self.assertEqual(got["divisor"], decoded[C.CONST_KEY_DIVISOR])
        self.assertEqual(got["exponent"], decoded[C.CONST_KEY_EXPONENT])

    def test_missing_exponent_gives_no_prediction(self):
        decoded = EngineModel(_ConstProcess(), None).constants()
        del decoded[C.CONST_KEY_EXPONENT]
        self.assertIsNone(analysis.normal_formula_constants(decoded))


class TestProbeSettingsHaveOneHome(unittest.TestCase):
    def test_clamp_verdict_needs_an_explicit_edge(self):
        with self.assertRaises(TypeError):
            analysis.clamp_verdict([])

    def test_probe_chart_spacings_and_lane_come_from_constants(self):
        self.assertEqual(tuple(probe_chart.DEFAULT_SPACINGS_MS), tuple(C.PROBE_SPACINGS_MS))
        self.assertEqual(probe_chart.DRUM_LANE_KICK, C.PROBE_LANE_KICK)


if __name__ == "__main__":
    unittest.main()
