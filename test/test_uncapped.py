"""Tests for the uncapped edition: Hydra without Clone Hero's 4-bar SP cap.

The edition is process-wide state, so every test here puts it back the way it
found it. Leaving it set would silently re-path the rest of the suite under
the wrong rules.

"""

import unittest

import hydra.hymisc as hymisc
import hydra.hypath as hypath
import hydra.hyutil as hyutil

import hydra_app


CHARTS = {
    'deadbolt': "..\\test\\input\\common\\IB24\\T3\\Thrice - Deadbolt\\notes.mid",
    'themata': "..\\test\\input\\common\\IB24\\T2\\Karnivool - Themata\\notes.mid",
    'knightmare': "..\\test\\input\\common\\IB24\\T2\\Area 11 - Knightmare Frame\\notes.mid",
    'overrated': "..\\test\\input\\common\\IB24\\T1\\Allister - Overrated\\notes.mid",
}


class UncappedTestCase(unittest.TestCase):
    def setUp(self):
        self._was_uncapped = hymisc.UNCAPPED_SP
        self._was_budget = hymisc.SP_CAP_TIME_BUDGET
        self._was_ladder = hymisc.SP_CAP_LADDER

    def tearDown(self):
        hymisc.apply_edition(self._was_uncapped)
        hymisc.SP_CAP_TIME_BUDGET = self._was_budget
        hymisc.SP_CAP_LADDER = self._was_ladder

    def record(self, chart, uncapped):
        hymisc.apply_edition(uncapped)
        return hyutil.analyze_chart_file(
            CHARTS[chart], 'Expert', True, True, 'scores', 1)

    def best(self, chart, uncapped):
        return self.record(chart, uncapped).best_path()


class TestEdition(UncappedTestCase):
    """The edition is a matched set: the pathing rule and the version a
    record is stamped with have to move together."""

    def test_capped_edition_values(self):
        hymisc.apply_edition(False)
        self.assertEqual(hymisc.SP_METER_CAP, 4)
        self.assertEqual(hymisc.EDITION_NAME, "Hydra")
        self.assertEqual(hymisc.RECORD_VERSION, hymisc.HYDRA_VERSION)

    def test_uncapped_edition_values(self):
        hymisc.apply_edition(True)
        self.assertIsNone(hymisc.SP_METER_CAP)
        self.assertEqual(hymisc.EDITION_NAME, "Hydra Uncapped")
        self.assertEqual(hymisc.RECORD_VERSION, hymisc.HYDRA_VERSION + ('uncapped',))

    def test_records_never_read_as_the_other_edition(self):
        """A capped record opened uncapped (or the reverse) has to read as out
        of date, not as a result this edition would have produced."""
        hymisc.apply_edition(False)
        capped_record = hyutil.analyze_chart_file(
            CHARTS['overrated'], 'Expert', True, True, 'scores', 1)
        self.assertTrue(capped_record.is_version_compatible())

        hymisc.apply_edition(True)
        self.assertFalse(capped_record.is_version_compatible())

        uncapped_record = hyutil.analyze_chart_file(
            CHARTS['overrated'], 'Expert', True, True, 'scores', 1)
        self.assertTrue(uncapped_record.is_version_compatible())

        hymisc.apply_edition(False)
        self.assertFalse(uncapped_record.is_version_compatible())


class TestUncappedPaths(UncappedTestCase):

    def test_uncapped_is_never_worse(self):
        """Every capped path is still legal uncapped -- the cap only removes
        options -- so uncapped can tie but can never score less."""
        for chart in CHARTS:
            with self.subTest(chart=chart):
                capped = self.best(chart, False)
                uncapped = self.best(chart, True)
                self.assertGreaterEqual(
                    uncapped.totalscore(), capped.totalscore())

    def test_capped_paths_never_bank_past_the_meter(self):
        for chart in CHARTS:
            with self.subTest(chart=chart):
                path = self.best(chart, False)
                for act in path.all_activations():
                    self.assertLessEqual(act.sp_meter, 4)
                self.assertLessEqual(path.leftover_sp, 4)

    def test_uncapped_banks_past_the_meter(self):
        """Deadbolt is the small case where the cap actually costs something:
        capped it can only spend 4 bars, uncapped it banks 5 and scores more.
        """
        capped = self.best('deadbolt', False)
        uncapped = self.best('deadbolt', True)

        self.assertGreater(uncapped.totalscore(), capped.totalscore())
        self.assertGreater(
            max(a.sp_meter for a in uncapped.all_activations()), 4)

    def test_an_explicit_cap_beats_a_lower_one_and_ties_a_higher_one(self):
        """The ladder rests on cost rising with the ceiling while the score
        stops moving, so a raised ceiling must never lose points."""
        song = hyutil.hysong.load_songpath_mid(
            CHARTS['deadbolt'], 'Expert', True, True)

        scores = []
        for cap in (4, 8, 16, 32):
            graph = hypath.ScoreGraph(song, sp_meter_cap=cap)
            pather = hypath.GraphPather()
            pather.read(graph, 'scores', 1, None, None)
            scores.append(pather.record.best_path().totalscore())

        for lower, higher in zip(scores, scores[1:]):
            self.assertGreaterEqual(higher, lower)

    def test_a_chart_with_no_activations_is_unaffected(self):
        """Nothing to activate means the cap was never in play, so removing it
        must not change the score."""
        capped = self.best('themata', False)
        uncapped = self.best('themata', True)

        self.assertEqual(list(capped.all_activations()), [])
        self.assertEqual(uncapped.totalscore(), capped.totalscore())


class TestAdaptiveCap(UncappedTestCase):
    """The uncapped edition reaches "no ceiling" by raising one until the
    score stops moving, and says which ceiling it settled on."""

    def test_capped_records_report_the_rule_not_an_approximation(self):
        record = self.record('deadbolt', False)
        self.assertEqual(record.sp_cap, 4)
        self.assertTrue(record.sp_cap_converged)

    def test_uncapped_records_report_the_ceiling_they_settled_on(self):
        record = self.record('deadbolt', True)
        self.assertIn(record.sp_cap, hymisc.SP_CAP_LADDER)
        self.assertTrue(record.sp_cap_converged)

    def test_running_out_of_ladder_is_reported_not_hidden(self):
        """A chart that never settles must say so rather than pass for a
        finished answer."""
        hymisc.apply_edition(True)
        # A ladder of one rung can never produce two agreeing runs.
        hymisc.SP_CAP_LADDER = (16,)

        record = hyutil.analyze_chart_file(
            CHARTS['deadbolt'], 'Expert', True, True, 'scores', 1)

        self.assertEqual(record.sp_cap, 16)
        self.assertFalse(record.sp_cap_converged)

    def test_the_first_rung_always_finishes(self):
        """Even with no time at all, there has to be an answer to report."""
        hymisc.apply_edition(True)
        hymisc.SP_CAP_TIME_BUDGET = 0.0001

        record = hyutil.analyze_chart_file(
            CHARTS['deadbolt'], 'Expert', True, True, 'scores', 1)

        self.assertEqual(record.sp_cap, hymisc.SP_CAP_LADDER[0])
        self.assertFalse(record.sp_cap_converged)
        self.assertTrue(record._paths)


class TestBatchOrdering(UncappedTestCase):
    """Charts arrive sorted by name, which files every discography together."""

    class FakeItem:
        def __init__(self, notespath):
            self.notespath = notespath

    def order_of(self, sizes):
        """Order the scheduler produces for charts of the given sizes."""
        items = [self.FakeItem(f"chart{i}") for i in range(len(sizes))]
        weights = dict(zip(items, sizes))

        real_weight = hydra_app.chart_weight
        hydra_app.chart_weight = weights.get
        try:
            return [weights[item] for item in hydra_app.interleave_by_size(items)]
        finally:
            hydra_app.chart_weight = real_weight

    def test_the_big_ones_are_spread_out(self):
        """Eight discographies in a row is what put every worker on a
        six-figure chart at once."""
        order = self.order_of([100] * 8 + [1] * 8)

        # No more than two large charts land in any window of four.
        for i in range(len(order) - 3):
            self.assertLessEqual(sum(1 for w in order[i:i + 4] if w == 100), 2)

    def test_every_chart_is_still_scheduled_exactly_once(self):
        sizes = [5, 3, 9, 1, 7, 2]
        self.assertEqual(sorted(self.order_of(sizes)), sorted(sizes))

    def test_unknown_sizes_do_not_crash_the_schedule(self):
        """Nothing is measurable in a fresh temp library, and every weight
        comes back None."""
        order = self.order_of([None] * 5)
        self.assertEqual(len(order), 5)


if __name__ == '__main__':
    unittest.main()
