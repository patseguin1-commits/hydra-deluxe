"""The probe runners attach before any breakpoint and always detach.

No game: a fake process, a fake debugger that logs every call and refuses a
breakpoint before attach, and a fake engine finder.
"""

from __future__ import annotations

import contextlib
import io
import os
import struct
import sys
import tempfile
import time
import unittest
from unittest import mock

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants as C  # noqa: E402
from tools.ch_probe.debugger import ThreadContext  # noqa: E402
from tools.ch_probe.experiments import active_probe, passive_probe  # noqa: E402

BASE = 0x180000000
OBJ = 0x1234000


class FakeProcess:
    pid = 4242
    module_base = BASE

    def __init__(self, log):
        self.log = log

    def verify_targets(self):
        self.log.append("verify")

    def resolve(self, rva):
        return BASE + rva

    def read_double(self, addr):
        return 0.0

    def read_u32(self, addr):
        return 0

    def read_const_double(self, rva):
        return 0.0


class FakeDebugger:
    def __init__(self, log, raise_in_run=None):
        self.log = log
        self.attached = False
        self.raise_in_run = raise_in_run

    def attach(self, pid):
        self.attached = True
        self.log.append(("attach", pid))

    def set_breakpoint(self, addr, callback):
        if not self.attached:
            raise AssertionError("breakpoint set before attach")
        self.log.append(("set_breakpoint", addr))

    def run(self, until=None):
        self.log.append("run")
        if self.raise_in_run is not None:
            raise self.raise_in_run
        deadline = time.monotonic() + 2.0
        while until is not None and not until() and time.monotonic() < deadline:
            time.sleep(0.001)

    def stop(self):
        self.log.append("stop")


def _names(log):
    return [e[0] if isinstance(e, tuple) else e for e in log]


class PassiveRunnerTest(unittest.TestCase):
    def _run(self, log, dbg):
        with tempfile.TemporaryDirectory() as d, \
                mock.patch.object(passive_probe, "RESULTS_DIR", d), \
                contextlib.redirect_stdout(io.StringIO()):
            passive_probe.run_passive_probe(
                duration_s=0.0,
                open_proc=lambda name: FakeProcess(log),
                make_debugger=lambda: dbg,
                find_engine=lambda proc: OBJ)

    def test_attach_comes_before_any_breakpoint(self):
        log = []
        self._run(log, FakeDebugger(log))
        self.assertEqual(_names(log), ["verify", "attach", "set_breakpoint", "run", "stop"])
        self.assertEqual(log[1], ("attach", 4242))
        self.assertEqual(log[2], ("set_breakpoint", BASE + C.RVA_WINDOW_FORMULA))

    def test_detaches_when_the_loop_is_interrupted(self):
        log = []
        with self.assertRaises(KeyboardInterrupt):
            self._run(log, FakeDebugger(log, raise_in_run=KeyboardInterrupt()))
        self.assertEqual(log[-1], "stop")


class FakeEngine:
    def __init__(self, clocks, windows):
        self._clocks = list(clocks)
        self._windows = list(windows)

    def song_clock(self):
        return self._clocks.pop(0)

    def total_window(self):
        return self._windows.pop(0)


class FakeStackDebugger:
    """Answers the [rsp] read with a fixed return address."""

    RET = 0x7000

    def __init__(self):
        self.planted = []

    def read(self, addr, size):
        return struct.pack("<Q", self.RET)

    def set_breakpoint(self, addr, callback):
        self.planted.append(addr)


def _ctx(xmm0=0.0):
    return ThreadContext({"rsp": 0x9000}, struct.pack("<d", xmm0) + b"\0" * 8)


class PassiveCollectorTest(unittest.TestCase):
    def test_raw_at_return_stored_at_next_call(self):
        engine = FakeEngine(clocks=[10.0, 10.2, 10.4], windows=[0.170, 0.172])
        dbg = FakeStackDebugger()
        col = passive_probe.PassiveCollector(engine)
        col.on_formula_entry(dbg, _ctx())
        col.on_formula_return(dbg, _ctx(0.0895))
        col.on_formula_entry(dbg, _ctx())
        col.on_formula_return(dbg, _ctx(0.0850))
        col.on_formula_entry(dbg, _ctx())
        rows = col.rows
        self.assertEqual(len(rows), 2)
        for got, want in zip(rows, [(0.0, 89.5, 170.0), (200.0, 85.0, 172.0)]):
            for g, w in zip(got, want):
                self.assertAlmostEqual(g, w, places=6)
        self.assertEqual(dbg.planted, [FakeStackDebugger.RET])

    def test_a_result_with_no_next_call_is_not_logged(self):
        engine = FakeEngine(clocks=[10.0], windows=[])
        col = passive_probe.PassiveCollector(engine)
        col.on_formula_entry(FakeStackDebugger(), _ctx())
        col.on_formula_return(FakeStackDebugger(), _ctx(0.09))
        self.assertEqual(col.rows, [])


class ActiveRunnerTest(unittest.TestCase):
    def _run(self, log, dbg):
        def drive(engine, driver, collector, plan, stop, focus):
            log.append("drive")

        with tempfile.TemporaryDirectory() as d, \
                mock.patch.object(active_probe, "RESULTS_DIR", d), \
                contextlib.redirect_stdout(io.StringIO()):
            active_probe.run_active_probe(
                spacings_ms=[211], offsets_ms=[70],
                open_proc=lambda name: FakeProcess(log),
                make_debugger=lambda: dbg,
                find_engine=lambda proc: OBJ,
                write_song=lambda root, plan: "fake-song",
                make_driver=lambda: None,
                find_window=lambda: 0,
                drive=drive)

    def test_attach_comes_before_the_breakpoint_and_the_inputs(self):
        log = []
        self._run(log, FakeDebugger(log))
        names = _names(log)
        self.assertLess(names.index("attach"), names.index("set_breakpoint"))
        self.assertLess(names.index("set_breakpoint"), names.index("drive"))
        self.assertIn(("set_breakpoint", BASE + C.RVA_HIT_CHECK), log)
        self.assertEqual(names[-1], "stop")

    def test_detaches_when_the_loop_is_interrupted(self):
        log = []
        with self.assertRaises(KeyboardInterrupt):
            self._run(log, FakeDebugger(log, raise_in_run=KeyboardInterrupt()))
        # The input thread may still log "drive" after "stop", so check
        # presence and order rather than the last entry.
        names = _names(log)
        self.assertIn("stop", names)
        self.assertLess(names.index("run"), names.index("stop"))


class ActivePlanTest(unittest.TestCase):
    def test_one_pair_per_spacing_and_offset(self):
        plan = active_probe.plan_inputs([211, 30], [70, 100])
        self.assertEqual([(p.spacing_ms, p.offset_ms) for p in plan],
                         [(211, 70), (211, 100), (30, 70), (30, 100)])
        for p in plan:
            self.assertEqual(p.second_ms - p.first_ms, p.spacing_ms)
        self.assertEqual(plan[0].first_ms, 3840.0)   # 2 bars of lead-in, 1 tick = 1 ms

    def test_hit_checks_are_counted_only_during_an_input(self):
        col = active_probe.ActiveCollector()
        col.on_hit_check(None, None)
        col.current_index = 3
        col.on_hit_check(None, None)
        col.on_hit_check(None, None)
        self.assertEqual(col.hit_check_calls, {3: 2})


if __name__ == "__main__":
    unittest.main()
