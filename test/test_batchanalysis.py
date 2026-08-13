import concurrent.futures
import os
import pathlib
import tempfile
import threading
import time
import unittest

import mido

import hydra.hymisc as hymisc
import hydra.hysong as hysong
import hydra.hyutil as hyutil

import hydra_app


TESTROOT = pathlib.Path(__file__).resolve().parent
CHARTFOLDER = TESTROOT / "input" / "common" / "IB24" / "T1"


def guitar_only_midi():
    """A .mid with a tempo map but no drums track at all.

    The shape of a guitar-only Rock Band rip, of which there are plenty in a
    real library.

    """
    midi = mido.MidiFile()

    tempo = mido.MidiTrack()
    tempo.name = "TEMPO TRACK"
    tempo.append(mido.MetaMessage('set_tempo', tempo=500000, time=0))
    tempo.append(mido.MetaMessage('time_signature', numerator=4, denominator=4, time=0))
    midi.tracks.append(tempo)

    guitar = mido.MidiTrack()
    guitar.name = "PART GUITAR"
    guitar.append(mido.Message('note_on', note=96, velocity=100, time=0))
    guitar.append(mido.Message('note_off', note=96, velocity=0, time=480))
    midi.tracks.append(guitar)

    return midi


def scanitem(chartfolder, md5):
    """A ScanItem pointing at one of the test charts."""
    return hyutil.ScanItem(
        md5, chartfolder.name, "Test Artist", "Test Charter",
        str(chartfolder / "notes.mid"), str(CHARTFOLDER),
    )


class StubStore:
    """Just the record-store calls BatchJob makes, recording the thread."""

    def __init__(self, stored=()):
        self.stored = set(stored)
        self.songs = []
        self.rows = []
        self.threads = set()

    @property
    def records(self):
        return [(row[0], row[1]) for row in self.rows]

    def has_record(self, hyhash, chartmode):
        return (hyhash, chartmode) in self.stored

    def add_song(self, scanitem, tempomap):
        self.threads.add(threading.current_thread())
        self.songs.append(scanitem.md5)

    def add_row(self, row):
        self.threads.add(threading.current_thread())
        self.rows.append(row)


class StubBook:
    def __init__(self, store):
        self.store = store

    def add_song(self, scanitem, tempomap):
        self.store.add_song(scanitem, tempomap)

    def add_row(self, row):
        self.store.add_row(row)


class StubState:
    def __init__(self, store):
        self.hydatabook = StubBook(store)


class SteppedPool:
    """A pool whose charts finish only when the test says so, in any order."""

    def __init__(self):
        self.futures = {}       # hyhash -> Future
        self._lock = threading.Lock()

    def submit(self, fn, job):
        hyhash = job[0]
        future = concurrent.futures.Future()
        with self._lock:
            self.futures[hyhash] = future
        return future

    def await_submissions(self, count, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                if len(self.futures) >= count:
                    return
            time.sleep(0.005)
        raise AssertionError(f"only {len(self.futures)} of {count} charts submitted")

    def finish(self, hyhash, chartmode=None):
        """Hand back a plausible stored row for one chart."""
        row = (hyhash, chartmode or CHARTMODE, "[1,3,1]", "0 1 2", b"blob")
        with self._lock:
            self.futures[hyhash].set_result((row, {'res': 192}, None))


SETTINGS = ('Expert', True, True, 'scores', 1, None)
CHARTMODE = "Expert Pro Drums, 2x Bass"


class TestEmptyChart(unittest.TestCase):
    """A chart with nothing charted for drums has to say so.

    These are real library entries -- guitar-only Rock Band rips, and songs
    whose drums track has no notes at the difficulty being viewed. They used
    to surface as IndexError from deep inside ScoreGraph.

    """
    def test_empty_song_is_a_chart_file_error(self):
        song = hysong.Song(192)
        self.assertTrue(song.is_empty())

        with self.assertRaises(hymisc.ChartFileError) as caught:
            hyutil._analyze(song, 'Expert', True, True, 'scores', 1)

        self.assertIn("Expert", str(caught.exception))
        self.assertIn("notes", str(caught.exception))

    def test_chart_with_no_drums_track_names_the_reason(self):
        """End to end, from a .mid that only has a guitar track."""
        midi = guitar_only_midi()

        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "notes.mid")
            midi.save(path)

            with self.assertRaises(hymisc.ChartFileError) as caught:
                hyutil.analyze_chart_file(path, 'Expert', True, True, 'scores', 1)

        self.assertIn("pro drums", str(caught.exception))

    def test_message_follows_the_difficulty_asked_for(self):
        song = hysong.Song(192)

        with self.assertRaises(hymisc.ChartFileError) as caught:
            hyutil._analyze(song, 'Hard', False, False, 'scores', 1)

        self.assertIn("Hard drums", str(caught.exception))


class TestBatchJob(unittest.TestCase):
    """BatchJob has to do its work away from the render thread.

    Pathing a full-album chart takes seconds. Run between rendered frames,
    that stalls the message pump long enough for Windows to grey the window
    out and title it "Not Responding", which is what "Analyze library" did
    over a large library.

    """
    def setUp(self):
        self.charts = sorted(p for p in CHARTFOLDER.iterdir()
                             if (p / "notes.mid").is_file())
        self.assertTrue(self.charts, f"no test charts under {CHARTFOLDER}")

        self.store = StubStore()
        self._real_appstate = getattr(hydra_app, 'appstate', None)
        hydra_app.appstate = StubState(self.store)

    def tearDown(self):
        if self._real_appstate is None:
            del hydra_app.appstate
        else:
            hydra_app.appstate = self._real_appstate

    def run_job(self, items, redo=False, stored=(), workers=1, timeout=300):
        self.store.stored = set(stored)
        job = hydra_app.BatchJob(items, CHARTMODE, redo, SETTINGS, workers=workers)
        job.start()
        job._thread.join(timeout)
        self.assertFalse(job._thread.is_alive(), "batch worker did not finish")
        return job

    def test_analyzes_every_chart_off_the_calling_thread(self):
        items = [scanitem(c, f"md5-{i}") for i, c in enumerate(self.charts[:3])]

        job = self.run_job(items)
        progress = job.progress()

        self.assertTrue(progress['finished'])
        self.assertEqual(progress['analyzed'], len(items))
        self.assertEqual(progress['failed'], 0)
        self.assertEqual(progress['index'], len(items))
        self.assertEqual(len(self.store.records), len(items))

        self.assertTrue(self.store.threads)
        self.assertNotIn(threading.current_thread(), self.store.threads)

    def test_stored_charts_are_skipped_unless_redoing(self):
        items = [scanitem(c, f"md5-{i}") for i, c in enumerate(self.charts[:2])]
        stored = [(items[0].md5, CHARTMODE)]

        job = self.run_job(items, stored=stored)
        self.assertEqual(job.progress()['skipped'], 1)
        self.assertEqual(job.progress()['analyzed'], 1)

        job = self.run_job(items, redo=True, stored=stored)
        self.assertEqual(job.progress()['skipped'], 0)
        self.assertEqual(job.progress()['analyzed'], 2)

    def test_one_bad_chart_does_not_stop_the_run(self):
        good = [scanitem(c, f"md5-{i}") for i, c in enumerate(self.charts[:2])]
        bad = hyutil.ScanItem("md5-bad", "Nope", "Nobody", "Nobody",
                              str(CHARTFOLDER / "does_not_exist" / "notes.mid"),
                              str(CHARTFOLDER))
        items = [good[0], bad, good[1]]

        progress = self.run_job(items).progress()

        self.assertEqual(progress['analyzed'], 2)
        self.assertEqual(progress['failed'], 1)
        self.assertEqual(len(progress['failures']), 1)
        self.assertIn("Nobody - Nope", progress['failures'][0])

    def test_a_failure_reads_as_the_reason_it_failed(self):
        """The eight failures in an 18506-chart run said nothing useful."""
        midi = guitar_only_midi()

        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "notes.mid")
            midi.save(path)
            item = hyutil.ScanItem("md5-guitar", "Guitar Only", "Somebody",
                                   "Somebody", path, tmp)

            progress = self.run_job([item]).progress()

        self.assertEqual(progress['failed'], 1)
        message = progress['failures'][0]
        self.assertIn("Somebody - Guitar Only", message)
        self.assertIn("No Expert pro drums notes", message)
        self.assertNotIn("IndexError", message)

    def test_charts_go_to_worker_processes(self):
        """Pathing is CPU-bound Python, so parallelism means processes.

        The point of the pool is that it produces the same rows as doing
        the work here, only several charts at a time.

        """
        items = [scanitem(c, f"md5-{i}") for i, c in enumerate(self.charts)]

        alone = self.run_job(items, workers=1).progress()
        rows_alone = sorted(self.store.rows)

        self.store = StubStore()
        hydra_app.appstate = StubState(self.store)
        pooled = self.run_job(items, workers=2).progress()
        rows_pooled = sorted(self.store.rows)

        self.assertEqual(pooled['analyzed'], len(items))
        self.assertEqual(pooled['failed'], 0)
        self.assertEqual(pooled['index'], alone['index'])
        self.assertEqual(rows_pooled, rows_alone)

    def test_a_pooled_failure_still_reads_as_its_reason(self):
        """A worker never raises across the pipe; it reports the reason."""
        midi = guitar_only_midi()

        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "notes.mid")
            midi.save(path)
            good = scanitem(self.charts[0], "md5-good")
            bad = hyutil.ScanItem("md5-guitar", "Guitar Only", "Somebody",
                                  "Somebody", path, tmp)

            progress = self.run_job([bad, good], workers=2).progress()

        self.assertEqual(progress['analyzed'], 1)
        self.assertEqual(progress['failed'], 1)
        self.assertIn("No Expert pro drums notes", progress['failures'][0])

    def test_results_are_counted_as_they_land_not_in_order(self):
        """A slow chart must not hold up the count for the ones behind it.

        Taking results in submission order left the progress bar sitting on
        14/30 for the twenty-five seconds one album chart took, which reads
        exactly like the freeze this design exists to avoid.

        """
        items = [
            hyutil.ScanItem(f"md5-{i}", f"Song {i}", f"Artist {i}",
                            "Charter", f"nowhere-{i}.mid", "root")
            for i in range(3)
        ]
        pool = SteppedPool()

        job = hydra_app.BatchJob(items, CHARTMODE, False, SETTINGS, workers=2)
        runner = threading.Thread(target=job._run_pool, args=(pool,), daemon=True)
        runner.start()

        pool.await_submissions(3)
        self.assertEqual(job.progress()['index'], 0)
        # The oldest chart still running is the one named.
        self.assertEqual(job.progress()['current'], "Artist 0 - Song 0")

        # The last chart finishes first, while the first is still running.
        pool.finish("md5-2")
        self.await_until(lambda: job.progress()['analyzed'] == 1)

        progress = job.progress()
        self.assertEqual(progress['index'], 1, "count waited on the slow chart")
        self.assertEqual(progress['current'], "Artist 0 - Song 0",
                         "still naming the chart actually being waited on")

        pool.finish("md5-1")
        self.await_until(lambda: job.progress()['analyzed'] == 2)

        pool.finish("md5-0")
        runner.join(30)
        self.assertFalse(runner.is_alive())
        self.assertEqual(job.progress()['analyzed'], 3)
        self.assertEqual(sorted(r[0] for r in self.store.rows),
                         ["md5-0", "md5-1", "md5-2"])

    def await_until(self, condition, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if condition():
                return
            time.sleep(0.005)
        self.fail("condition never became true")

    def test_falls_back_to_this_thread_when_no_pool_starts(self):
        """A machine or build that won't spawn workers still gets a run.

        The fallback has to happen before any chart is counted, or the
        charts already done would be redone and counted twice.

        """
        items = [scanitem(c, f"md5-{i}") for i, c in enumerate(self.charts)]

        job = hydra_app.BatchJob(items, CHARTMODE, False, SETTINGS, workers=4)
        job._open_pool = lambda: None
        job.start()
        job._thread.join(300)

        progress = job.progress()
        self.assertFalse(job._thread.is_alive())
        self.assertTrue(progress['finished'])
        self.assertEqual(progress['analyzed'], len(items))
        self.assertEqual(progress['index'], len(items))
        self.assertEqual(len(self.store.rows), len(items))

    def test_worker_count_leaves_a_core_free(self):
        self.assertGreaterEqual(hydra_app.batch_workercount(), 1)
        self.assertLessEqual(hydra_app.batch_workercount(), hydra_app.BATCH_MAX_WORKERS)
        self.assertLess(hydra_app.batch_workercount(), max(2, os.cpu_count() or 2))

    def test_cancelling_stops_the_run(self):
        items = [scanitem(c, f"md5-{i}") for i, c in enumerate(self.charts)]

        job = hydra_app.BatchJob(items, CHARTMODE, False, SETTINGS, workers=1)
        job.cancel()
        job.start()
        job._thread.join(120)

        self.assertFalse(job._thread.is_alive())
        self.assertTrue(job.is_cancelled())

        progress = job.progress()
        self.assertTrue(progress['finished'])
        self.assertEqual(progress['index'], 0)
        self.assertEqual(self.store.rows, [])

    def test_progress_reports_the_chart_in_flight(self):
        items = [scanitem(c, f"md5-{i}") for i, c in enumerate(self.charts)]
        seen = []

        job = hydra_app.BatchJob(items, CHARTMODE, False, SETTINGS, workers=1)
        job.start()
        while job._thread.is_alive():
            # Stands in for the render thread polling once a frame.
            current = job.progress()['current']
            if current and current not in seen:
                seen.append(current)
            time.sleep(0.005)
        job._thread.join(120)

        self.assertEqual(job.progress()['current'], "")
        self.assertTrue(seen)
        for label in seen:
            self.assertIn(f"{items[0].artist} - ", label)


if __name__ == '__main__':
    unittest.main()
