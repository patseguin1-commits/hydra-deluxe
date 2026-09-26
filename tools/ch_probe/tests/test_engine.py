"""Unit tests for engine.py, run with no game and no debugger.

The whole point of the engine model taking a ProcessHandle and a Debugger
through its constructor is that we can hand it fakes here. The fakes are dumb:
the fake process returns whatever double or dword we told it to for a given
address, and the fake debugger fires the breakpoint callback the instant it is
set. That is enough to check every piece of logic in engine.py -- the offset
arithmetic, the precision-mode bit test, the constant reads, and capturing the
object pointer from rcx.

These tests import engine.py as a top-level module. To make that work without a
package root, we put the ch_probe directory itself on sys.path first, so
`import engine` and `import constants` resolve.
"""

import os
import sys
import unittest

# Put the ch_probe directory (parent of this tests/ dir) on the path so the
# sibling modules import as top-level names.
_CH_PROBE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _CH_PROBE_DIR not in sys.path:
    sys.path.insert(0, _CH_PROBE_DIR)

import constants as C  # noqa: E402
import engine  # noqa: E402


# --- fakes -------------------------------------------------------------------


class FakeThreadContext:
    """A register snapshot. Only rcx matters for capture_object."""

    def __init__(self, rcx):
        self.rcx = rcx
        self.rip = 0

    def xmm0_double(self):
        return 0.0


class FakeProcess:
    """A stand-in for the real Process.

    Give it two lookup tables: `doubles` maps an absolute address to the double
    stored there, and `dwords` maps an absolute address to the u32 stored there.
    `consts` maps an RVA to the double at that .rdata spot. Every read is exact:
    ask for an address we weren't told about and we raise, so a wrong offset in
    engine.py shows up as a loud KeyError instead of a silent zero.
    """

    def __init__(self, module_base=0x7FF000000000, doubles=None, dwords=None, consts=None):
        self.module_base = module_base
        self._doubles = dict(doubles or {})
        self._dwords = dict(dwords or {})
        self._consts = dict(consts or {})
        # Record which RVAs were asked for, so a test can assert coverage.
        self.const_rvas_read = []

    def resolve(self, rva):
        return self.module_base + rva

    def read(self, addr, size):
        raise NotImplementedError("raw read not needed for these tests")

    def write(self, addr, data):
        raise NotImplementedError

    def read_double(self, addr):
        return self._doubles[addr]

    def read_u32(self, addr):
        return self._dwords[addr]

    def read_u64(self, addr):
        raise NotImplementedError

    def read_const_double(self, rva):
        self.const_rvas_read.append(rva)
        return self._consts.get(rva, 0.0)

    def verify_targets(self):
        pass


class FakeDebugger:
    """A stand-in for the real Debugger.

    set_breakpoint fires the callback right away with a thread context carrying
    the rcx we were built with. That mimics the real thing catching the
    constructor -- minus the wait -- so capture_object() completes in one call.
    """

    def __init__(self, ctor_rcx):
        self._ctor_rcx = ctor_rcx
        self.breakpoints = []
        self.run_called = False

    def attach(self, pid):
        pass

    def set_breakpoint(self, addr, callback):
        self.breakpoints.append(addr)
        callback(self, FakeThreadContext(rcx=self._ctor_rcx))

    def clear_breakpoint(self, addr):
        pass

    def read(self, addr, size):
        raise NotImplementedError

    def write(self, addr, data):
        raise NotImplementedError

    def run(self, until=None):
        # Should never be reached in these tests, because set_breakpoint already
        # captured the pointer. Flag it so a regression shows up.
        self.run_called = True

    def stop(self):
        pass


# --- helpers -----------------------------------------------------------------

OBJ = 0x1234000  # a made-up but fixed engine object address


def make_engine(object_ptr=OBJ, doubles=None, dwords=None, consts=None,
                module_base=0x7FF000000000):
    proc = FakeProcess(module_base=module_base, doubles=doubles,
                       dwords=dwords, consts=consts)
    dbg = FakeDebugger(ctor_rcx=object_ptr)
    return engine.EngineModel(proc, dbg), proc, dbg


# --- tests: capture ----------------------------------------------------------


class TestCaptureObject(unittest.TestCase):
    def test_captures_rcx_as_object_ptr(self):
        eng, proc, dbg = make_engine(object_ptr=OBJ)
        self.assertIsNone(eng.object_ptr)
        got = eng.capture_object()
        self.assertEqual(got, OBJ)
        self.assertEqual(eng.object_ptr, OBJ)

    def test_breakpoint_set_at_resolved_ctor_address(self):
        eng, proc, dbg = make_engine(object_ptr=OBJ)
        eng.capture_object()
        expected_addr = proc.module_base + C.RVA_DRUMS_ENGINE_CTOR
        self.assertIn(expected_addr, dbg.breakpoints)

    def test_run_not_needed_when_callback_fires_immediately(self):
        eng, proc, dbg = make_engine(object_ptr=OBJ)
        eng.capture_object()
        # The fake fires the callback inside set_breakpoint, so run() is skipped.
        self.assertFalse(dbg.run_called)


# --- tests: offset arithmetic ------------------------------------------------


class TestFieldReads(unittest.TestCase):
    def _engine_with_fields(self, **field_values):
        """Build an engine whose object fields hold the given doubles.

        Pass e.g. total=170.0 and the double at OBJ+OFF_TOTAL_WINDOW is set.
        """
        offset_of = {
            "total": C.OFF_TOTAL_WINDOW,
            "back": C.OFF_BACK_WINDOW,
            "front": C.OFF_FRONT_WINDOW,
            "hit_time": C.OFF_HIT_TIME,
        }
        doubles = {OBJ + offset_of[k]: v for k, v in field_values.items()}
        eng, proc, dbg = make_engine(object_ptr=OBJ, doubles=doubles)
        eng.capture_object()
        return eng

    def test_total_window_reads_off_0x20(self):
        eng = self._engine_with_fields(total=170.0)
        self.assertEqual(eng.total_window(), 170.0)

    def test_back_window_reads_off_0x30(self):
        eng = self._engine_with_fields(back=85.0)
        self.assertEqual(eng.back_window(), 85.0)

    def test_front_window_reads_off_0x38(self):
        eng = self._engine_with_fields(front=37.5)
        self.assertEqual(eng.front_window(), 37.5)

    def test_hit_time_reads_off_0x2e0(self):
        # +0x100 is the song clock (proven live); the hit-time candidate is
        # +0x2e0, still to be confirmed at the game.
        self.assertEqual(C.OFF_HIT_TIME, 0x2E0)
        eng = self._engine_with_fields(hit_time=12.34)
        self.assertEqual(eng.hit_time(), 12.34)

    def test_total_is_twice_back_at_construction(self):
        # Not engine logic, just a sanity check the offsets are distinct fields
        # and the reads land where we put them.
        eng = self._engine_with_fields(total=170.0, back=85.0)
        self.assertEqual(eng.total_window(), 2 * eng.back_window())

    def test_reads_before_capture_raise(self):
        eng, proc, dbg = make_engine(object_ptr=OBJ, doubles={})
        with self.assertRaises(RuntimeError):
            eng.total_window()


class TestNoteCount(unittest.TestCase):
    def test_note_count_reads_u32_off_0x8c(self):
        dwords = {OBJ + C.OFF_NOTE_COUNT: 512}
        eng, proc, dbg = make_engine(object_ptr=OBJ, dwords=dwords)
        eng.capture_object()
        self.assertEqual(eng.note_count(), 512)


# --- tests: precision-mode bit -----------------------------------------------


class TestPrecisionMode(unittest.TestCase):
    def _mode(self, flags_value):
        dwords = {OBJ + C.OFF_FLAGS: flags_value}
        eng, proc, dbg = make_engine(object_ptr=OBJ, dwords=dwords)
        eng.capture_object()
        return eng.precision_mode()

    def test_bit_clear_is_normal_mode(self):
        self.assertFalse(self._mode(0x0000))

    def test_bit_set_is_precision_mode(self):
        self.assertTrue(self._mode(C.PRECISION_MODE_BIT))

    def test_only_the_precision_bit_matters(self):
        # Other bits set, precision bit clear -> still normal.
        other_bits = 0xFFFFFFFF & ~C.PRECISION_MODE_BIT
        self.assertFalse(self._mode(other_bits))

    def test_precision_bit_among_other_bits(self):
        # Precision bit set alongside noise -> precision.
        self.assertTrue(self._mode(0x0FF1 | C.PRECISION_MODE_BIT))

    def test_precision_bit_is_0x1000(self):
        # Guards against the constant drifting; the spec pins it at 0x1000.
        self.assertEqual(C.PRECISION_MODE_BIT, 0x1000)


# --- tests: constants --------------------------------------------------------


class TestConstants(unittest.TestCase):
    def _all_expected_rvas(self):
        rvas = [
            C.RVA_CONST_NORMAL_BACK,
            C.RVA_CONST_NORMAL_FRONT,
            C.RVA_CONST_PRECISION_BACK,
            C.RVA_CONST_PRECISION_FRONT,
            C.RVA_FORMULA_DIVISOR,
            C.RVA_FORMULA_EXPONENT,
            C.RVA_HITCHECK_THRESHOLD,
        ]
        rvas += list(C.RVA_FORMULA_NORMAL.values())
        rvas += list(C.RVA_FORMULA_PRECISION.values())
        return rvas

    def test_reads_every_expected_rva(self):
        eng, proc, dbg = make_engine(object_ptr=OBJ)
        eng.constants()
        for rva in self._all_expected_rvas():
            self.assertIn(rva, proc.const_rvas_read,
                          msg="constants() did not read RVA 0x%X" % rva)

    def test_returns_a_value_for_every_name(self):
        # Feed known values so we can check they land under the right keys.
        consts = {
            C.RVA_CONST_NORMAL_BACK: 85.0,
            C.RVA_CONST_NORMAL_FRONT: 37.5,
            C.RVA_CONST_PRECISION_BACK: 40.0,
            C.RVA_CONST_PRECISION_FRONT: 25.0,
            C.RVA_FORMULA_DIVISOR: 1000.0,
            C.RVA_FORMULA_EXPONENT: 2.0,
            C.RVA_HITCHECK_THRESHOLD: 0.5,
        }
        for rva in C.RVA_FORMULA_NORMAL.values():
            consts[rva] = 1.0
        for rva in C.RVA_FORMULA_PRECISION.values():
            consts[rva] = 2.0
        eng, proc, dbg = make_engine(object_ptr=OBJ, consts=consts)
        got = eng.constants()
        self.assertEqual(got["normal_back"], 85.0)
        self.assertEqual(got["normal_front"], 37.5)
        self.assertEqual(got["precision_back"], 40.0)
        self.assertEqual(got["precision_front"], 25.0)
        self.assertEqual(got["divisor"], 1000.0)
        self.assertEqual(got["exponent"], 2.0)
        self.assertEqual(got["hitcheck_threshold"], 0.5)
        # Every normal-branch and precision-branch coefficient shows up, keyed
        # with its mode prefix.
        for name in C.RVA_FORMULA_NORMAL:
            self.assertIn("normal_" + name, got)
        for name in C.RVA_FORMULA_PRECISION:
            self.assertIn("precision_" + name, got)

    def test_constants_need_no_captured_object(self):
        # constants() reads .rdata, which does not depend on the object pointer.
        eng, proc, dbg = make_engine(object_ptr=OBJ)
        self.assertIsNone(eng.object_ptr)
        eng.constants()  # must not raise


# --- tests: song clock (live-only seam) --------------------------------------


class TestSongClock(unittest.TestCase):
    def test_reads_the_proven_clock_at_0x100(self):
        # play_chart.py plays whole songs off this field (2026-09-25), so the
        # song clock is +0x100, in seconds.
        self.assertEqual(C.OFF_SONG_CLOCK, 0x100)
        doubles = {OBJ + C.OFF_SONG_CLOCK: 3.5}
        eng, proc, dbg = make_engine(object_ptr=OBJ, doubles=doubles)
        eng.capture_object()
        self.assertEqual(eng.song_clock(), 3.5)

    def test_no_placeholder_offset_is_left(self):
        self.assertFalse(hasattr(engine, "_PLACEHOLDER_SONG_CLOCK_OFFSET"))


class TestScoreAndFoundObject(unittest.TestCase):
    def test_score_reads_u32_off_0x94(self):
        eng, proc, dbg = make_engine(object_ptr=OBJ,
                                     dwords={OBJ + C.OFF_SCORE: 4200})
        eng.capture_object()
        self.assertEqual(eng.score(), 4200)

    def test_use_object_takes_a_scanned_pointer_without_a_debugger(self):
        proc = FakeProcess(doubles={OBJ + C.OFF_SONG_CLOCK: 1.25})
        eng = engine.EngineModel(proc)
        self.assertEqual(eng.use_object(OBJ), OBJ)
        self.assertEqual(eng.object_ptr, OBJ)
        self.assertEqual(eng.song_clock(), 1.25)

    def test_capture_without_a_debugger_raises(self):
        eng = engine.EngineModel(FakeProcess())
        with self.assertRaises(RuntimeError):
            eng.capture_object()


if __name__ == "__main__":
    unittest.main()
