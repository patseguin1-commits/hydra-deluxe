"""Parity tests for the native path search.

The whole value of this app is that its paths are correct, so a faster search
earns its place only by being indistinguishable from the one it replaces.
These compare the two engines directly on real charts rather than checking the
C++ against hand-written expectations, because a hand-written expectation is
just a third implementation to get wrong.

The golden check is test_all_charts_identical: the best path's score and
pathstring for every chart in the corpus, with the engine off and on. It runs
twice, once per edition -- the uncapped edition removes the SP meter ceiling,
which changes how many paths are live by orders of magnitude and exercises
completely different reduction behaviour.

Every test skips cleanly when the library is not built, so the suite still
passes on a machine with no C++ toolchain.

Run with:
    cd docs && PYTHONPATH=.. python -m unittest discover -s ../test -p "test_search_parity.py"

"""

import glob
import os
import unittest

import hydra.hydata as hydata
import hydra.hymisc as hymisc
import hydra.hynative as hynative
import hydra.hypath as hypath
import hydra.hyutil as hyutil


CORPUS = os.path.join("..", "test", "input", "common")


def charts():
    found = []
    for ext in ("mid", "chart", "sng"):
        found += glob.glob(os.path.join(CORPUS, "**", f"*.{ext}"), recursive=True)
    return sorted(found)


class _EngineSwitch:
    """Runs an analysis with the native search forced on or off."""

    def __init__(self, native):
        self.native = native

    def __enter__(self):
        self._saved = hynative.SEARCH_ENABLED
        hynative.SEARCH_ENABLED = self.native
        return self

    def __exit__(self, *exc):
        hynative.SEARCH_ENABLED = self._saved
        return False


def analyze(path, native, d_mode='scores', d_value=200, ms_filter=None):
    with _EngineSwitch(native):
        return hyutil.analyze_chart_file(
            path, 'Expert', True, True, d_mode, d_value, ms_filter)


def summary(record):
    """What the golden check compares: the answer the app reports."""
    best = record.best_path()
    return best.totalscore(), best.pathstring()


def full_detail(path):
    """Everything reachable on a finished path, for the deeper comparison.

    Goes past score and pathstring into the fields the details panel reads,
    because those come off graph objects the engine addresses by index and a
    wrong index would not necessarily move the score.

    """
    return (
        path.totalscore(), path.pathstring(), path.tied_pathcount(),
        path.notecount, path.leftover_sp,
        path.skipped_accents, path.skipped_ghosts,
        path.score_base, path.score_combo, path.score_sp,
        path.score_solo, path.score_accents, path.score_ghosts,
        path.var_point,
        tuple(
            (a.skips, a.sp_meter, a.e_offset, a.frontend_points,
             a.timecode.ticks if a.timecode is not None else None,
             len(a.backends),
             tuple((type(s).__name__, s.offset) for s in a.sqinouts))
            for a in path.all_activations()
        ),
        tuple(full_detail(v) for v in path.variants),
    )


@unittest.skipUnless(hynative.AVAILABLE,
                     f"native library not available: {hynative.STATUS}")
class TestSearchParity(unittest.TestCase):

    def test_all_charts_identical(self):
        """The golden check: same score, same pathstring, every chart.

        Run once per edition. The capped run is what ships; the uncapped run
        raises the meter ceiling up a ladder until the score settles, so it
        calls the search several times per chart with far more live paths.

        """
        found = charts()
        self.assertGreater(len(found), 0, f"no charts under {CORPUS}")

        saved = hymisc.SP_METER_CAP
        try:
            for edition, cap in (("capped", 4), ("uncapped", None)):
                hymisc.SP_METER_CAP = cap
                for path in found:
                    with self.subTest(edition=edition, chart=path):
                        self.assertEqual(
                            summary(analyze(path, native=True)),
                            summary(analyze(path, native=False)),
                        )
        finally:
            hymisc.SP_METER_CAP = saved

    def test_full_result_identical(self):
        """Not just the best path: every path, every field, every variant."""
        for path in charts():
            with self.subTest(chart=path):
                native = analyze(path, native=True)._paths
                python = analyze(path, native=False)._paths
                self.assertEqual(len(native), len(python))
                for i, (n, p) in enumerate(zip(native, python)):
                    self.assertEqual(full_detail(n), full_detail(p),
                                     f"path {i} differs")

    def test_ms_filter_identical(self):
        """The ms filter is the one place the search compares floats.

        It also changes which paths may eliminate which, so it reaches
        reduction branches an unfiltered run never takes.

        """
        for ms_filter in (5.0, 20.0):
            for path in charts()[::7]:
                with self.subTest(chart=path, ms=ms_filter):
                    self.assertEqual(
                        summary(analyze(path, True, ms_filter=ms_filter)),
                        summary(analyze(path, False, ms_filter=ms_filter)),
                    )

    def test_points_depth_mode_identical(self):
        """'points' mode takes the other branch of every depth comparison."""
        for path in charts()[::7]:
            with self.subTest(chart=path):
                self.assertEqual(
                    summary(analyze(path, True, 'points', 5000)),
                    summary(analyze(path, False, 'points', 5000)),
                )

    def test_shallow_depth_identical(self):
        """A small depth makes the reduction actually drop paths.

        At depth 200 most groups take the early-out in _reduce_group; at
        depth 1 they go through the tie merge and the elimination pass.

        """
        for depth in (0, 1, 3):
            for path in charts()[::7]:
                with self.subTest(chart=path, depth=depth):
                    self.assertEqual(
                        summary(analyze(path, True, 'scores', depth)),
                        summary(analyze(path, False, 'scores', depth)),
                    )

    def test_skipped_dynamics_flag_identical(self):
        """FLAG_SKIPPED_DYNAMICS is off by default, so force it on.

        It is the only thing that reads an activation note's dynamics, and the
        only path by which an activation can cost a path points.

        """
        saved = hymisc.FLAG_SKIPPED_DYNAMICS
        hymisc.FLAG_SKIPPED_DYNAMICS = True
        try:
            for path in charts()[::5]:
                with self.subTest(chart=path):
                    self.assertEqual(
                        summary(analyze(path, native=True)),
                        summary(analyze(path, native=False)),
                    )
        finally:
            hymisc.FLAG_SKIPPED_DYNAMICS = saved

    def test_progress_callback_matches(self):
        """The engine calls back once per iteration, as the Python loop does."""
        chart = charts()[0]
        seen = {}
        for native in (True, False):
            calls = []
            with _EngineSwitch(native):
                hyutil.analyze_chart_file(
                    chart, 'Expert', True, True, 'scores', 200, None,
                    cb_pathsprogress=lambda tc, f: calls.append(f))
            seen[native] = calls
        self.assertEqual(len(seen[True]), len(seen[False]))
        self.assertEqual(seen[True], seen[False])

    def test_progress_callback_can_abandon_the_search(self):
        """Raising out of the progress callback must stop the engine.

        This is how hyutil's adaptive SP meter ladder drops a rung that has
        overrun its time budget, and it is the one mechanism a C engine cannot
        inherit for free: ctypes prints an exception raised inside a callback
        and returns to C anyway. Catching it and re-raising afterwards would
        give the right answer while still doing all the work, which defeats
        the point of a time budget -- so what this asserts is that no further
        iterations run once the callback has raised.

        """
        chart = charts()[0]

        class Stop(Exception):
            pass

        for limit in (1, 3):
            for native in (True, False):
                with self.subTest(limit=limit, native=native):
                    calls = []

                    def cb(tc, fraction):
                        calls.append(fraction)
                        if len(calls) >= limit:
                            raise Stop

                    with _EngineSwitch(native):
                        with self.assertRaises(Stop):
                            hyutil.analyze_chart_file(
                                chart, 'Expert', True, True, 'scores', 200,
                                None, cb_pathsprogress=cb)

                    # Exactly the iteration that raised, and not one more.
                    self.assertEqual(len(calls), limit)

    def test_variants_are_not_shared_between_paths(self):
        """Two finished paths must not share a variant object.

        hydata.Path.prepare_variants writes each variant's tail onto it, so a
        shared variant would be written twice and report the wrong activations
        for one of its owners. The pure-Python search shares variants while it
        runs and calls detach_variants to fix that; the engine rebuilds each
        variant separately, and this is what says so.

        """
        for chart in charts()[::11]:
            with self.subTest(chart=chart):
                record = analyze(chart, native=True)
                seen = set()

                def walk(p):
                    self.assertNotIn(id(p), seen, "variant reached twice")
                    seen.add(id(p))
                    for v in p.variants:
                        walk(v)

                for p in record._paths:
                    walk(p)

    def test_tied_counts_are_consistent(self):
        """_tied_count must equal 1 + the sum over the variant subtree."""
        for chart in charts()[::11]:
            with self.subTest(chart=chart):
                for p in analyze(chart, native=True)._paths:
                    self._check_tied(p)

    def _check_tied(self, p):
        expected = 1
        for v in p.variants:
            self._check_tied(v)
            expected += v.tied_pathcount()
        self.assertEqual(p.tied_pathcount(), expected)
        self.assertLessEqual(p.tied_pathcount(), hypath.MAX_TIED_PATHS)

    def test_result_is_ordered_by_score(self):
        for chart in charts()[::11]:
            with self.subTest(chart=chart):
                scores = [p.totalscore() for p in analyze(chart, True)._paths]
                self.assertEqual(scores, sorted(scores, reverse=True))

    def test_paths_are_hydata_objects(self):
        """The engine reports a decision log; the data model stays Python."""
        record = analyze(charts()[0], native=True)
        for p in record._paths:
            self.assertIsInstance(p, hydata.Path)
            for a in p.all_activations():
                self.assertIsInstance(a, hydata.Activation)


if __name__ == '__main__':
    unittest.main()
