"""Tests for engine_finder: the memory scan's match maths and the "whose clock
moves" check that picks the live engine.

No game: a fake process answers reads from dicts, and a fake scan hands back
the candidate addresses a real scan would find.
"""

from __future__ import annotations

import io
import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants as C  # noqa: E402
from tools.ch_probe import engine_finder as F  # noqa: E402

BASE = 0x180000000
W = C.OFF_TOTAL_WINDOW
K = C.OFF_SONG_CLOCK


class FakeProc:
    """Doubles from a dict. A clock address holds a list of values, read one
    per call; the last value repeats."""

    def __init__(self, doubles=None, clocks=None, raw=None):
        self.module_base = BASE
        self._doubles = dict(doubles or {})
        self._clocks = {a: list(v) for a, v in (clocks or {}).items()}
        self._raw = dict(raw or {})

    def resolve(self, rva):
        return BASE + rva

    def read(self, addr, size):
        return self._raw[addr][:size]

    def read_double(self, addr):
        seq = self._clocks.get(addr)
        if seq is not None:
            return seq.pop(0) if len(seq) > 1 else seq[0]
        if addr in self._doubles:
            return self._doubles[addr]
        raise OSError(f"nothing mapped at {addr:#x}")


class HitsInRegionTest(unittest.TestCase):
    def test_each_match_backs_up_to_the_object_start(self):
        pattern = b"B" * 8 + b"F" * 8
        data = b"\0" * 0x40 + pattern + b"\0" * 0x20 + pattern
        self.assertEqual(F.hits_in_region(0x5000, data, pattern),
                         [0x5000 + 0x40 - 0x30, 0x5000 + 0x70 - 0x30])


class FindLiveEngineTest(unittest.TestCase):
    def test_picks_the_engine_whose_clock_moves(self):
        frozen, live, in_module, empty = 0x10000, 0x20000, BASE + 0x100, 0x30000
        proc = FakeProc(
            doubles={frozen + W: 0.17, live + W: 0.17, in_module + W: 0.17,
                     empty + W: 0.0},
            clocks={frozen + K: [21.95], live + K: [1.0, 1.1],
                    in_module + K: [0.0, 5.0]})
        sleeps = []

        def scan(p, back, front):
            return [frozen, in_module, empty, live], 0, 0

        got = F.find_live_engine(proc, [(b"b", b"f")], scan=scan,
                                 sleep=sleeps.append, out=io.StringIO())
        self.assertEqual(got, live)
        self.assertEqual(sleeps, [0.12])

    def test_keeps_waiting_until_a_clock_moves(self):
        live = 0x20000
        proc = FakeProc(doubles={live + W: 0.17},
                        clocks={live + K: [1.0, 1.0, 1.0, 1.2]})
        sleeps, out = [], io.StringIO()
        got = F.find_live_engine(proc, [(b"b", b"f")],
                                 scan=lambda p, b, f: ([live], 0, 0),
                                 sleep=sleeps.append, out=out)
        self.assertEqual(got, live)
        self.assertEqual(sleeps, [0.12, 0.4, 0.12])
        self.assertEqual(out.getvalue(), ".")

    def test_a_candidate_two_patterns_find_is_checked_once_in_scan_order(self):
        a, b = 0x10000, 0x20000
        proc = FakeProc(doubles={a + W: 0.17, b + W: 0.17},
                        clocks={a + K: [1.0, 1.1], b + K: [2.0, 2.1]})
        answers = iter([([a, b], 0, 0), ([b, a], 0, 0)])
        got = F.find_live_engine(proc, [(b"n", b"n"), (b"p", b"p")],
                                 scan=lambda p, back, front: next(answers),
                                 sleep=lambda s: None, out=io.StringIO())
        self.assertEqual(got, a)


class PatternsTest(unittest.TestCase):
    def test_all_patterns_tries_the_precision_pair_both_ways(self):
        raw = {
            BASE + C.RVA_CONST_NORMAL_BACK: b"NB" * 4,
            BASE + C.RVA_CONST_NORMAL_FRONT: b"NF" * 4,
            BASE + C.RVA_CONST_PRECISION_BACK: b"PB" * 4,
            BASE + C.RVA_CONST_PRECISION_FRONT: b"PF" * 4,
        }
        proc = FakeProc(raw=raw)
        self.assertEqual(F.normal_pattern(proc), (b"NB" * 4, b"NF" * 4))
        self.assertEqual(F.all_patterns(proc), [
            (b"NB" * 4, b"NF" * 4), (b"PB" * 4, b"PF" * 4), (b"PF" * 4, b"PB" * 4)])


if __name__ == "__main__":
    unittest.main()
