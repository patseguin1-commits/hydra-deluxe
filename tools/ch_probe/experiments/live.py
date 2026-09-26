"""Shared live-game pieces for the hit-window experiments.

watch_window.py and walk_edges.py both need the same three things: find the
live engine object, read the fields they watch, and load a probe song's
manifest. They live here so the two scripts agree on every offset.

Finding the engine works in either scoring mode. The finder searches memory
for the window constants the engine keeps at +0x30/+0x38. play_chart.py only
searches for the normal-mode pair, but in precision mode the engine holds the
precision pair instead, so this finder searches for both. The precision labels
in constants.py are known to be backwards, so it tries that pair in both orders.
"""

from __future__ import annotations

import json
import os
import struct
import sys
import time
from dataclasses import dataclass

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants
from tools.ch_probe.experiments.find_engine import scan_for_engine

# Engine-object byte offsets the experiments watch.
OFF_WINDOW = 0x20      # double s: total window for the current note
OFF_SCORE = 0x94       # u32: game score, rises only when a note is hit
OFF_CLOCK = 0x100      # double s: song clock (play_chart.py relies on it)
OFF_FLAGS = 0x198      # u32: bit 0x1000 = precision mode
OFF_HIT_TIME = 0x2E0   # double s: the code copies the song clock here on a hit

# One read covers every watched field, so a sample is a consistent snapshot.
SNAPSHOT_SIZE = OFF_HIT_TIME + 8

PROBE_ROOT = r"C:\Clone Hero\songs\Hydra Probe"


@dataclass(frozen=True)
class Snapshot:
    window_ms: float
    clock_s: float
    score: int
    hit_time_s: float
    flags: int

    @property
    def precision(self) -> bool:
        return bool(self.flags & constants.PRECISION_MODE_BIT)


def decode_snapshot(raw: bytes) -> Snapshot:
    """Turn SNAPSHOT_SIZE bytes read from the engine base into a Snapshot."""
    def dbl(off: int) -> float:
        return struct.unpack_from("<d", raw, off)[0]

    def u32(off: int) -> int:
        return struct.unpack_from("<I", raw, off)[0]

    return Snapshot(
        window_ms=dbl(OFF_WINDOW) * 1000.0,
        clock_s=dbl(OFF_CLOCK),
        score=u32(OFF_SCORE),
        hit_time_s=dbl(OFF_HIT_TIME),
        flags=u32(OFF_FLAGS),
    )


def read_snapshot(proc, engine: int) -> Snapshot:
    return decode_snapshot(proc.read(engine, SNAPSHOT_SIZE))


def _engine_patterns(proc) -> list[tuple[bytes, bytes]]:
    """(back, front) byte pairs an engine may hold at +0x30/+0x38."""
    def raw(rva: int) -> bytes:
        return proc.read(proc.resolve(rva), 8)

    normal = (raw(constants.RVA_CONST_NORMAL_BACK), raw(constants.RVA_CONST_NORMAL_FRONT))
    p1 = raw(constants.RVA_CONST_PRECISION_BACK)
    p2 = raw(constants.RVA_CONST_PRECISION_FRONT)
    return [normal, (p1, p2), (p2, p1)]


def find_live_engine(proc) -> int:
    """Return the engine object whose song clock is moving, in either mode.

    Clone Hero leaves old engine objects frozen on the heap after a restart,
    so the live one is the only candidate whose clock changes between two
    reads (see play_chart.find_active_engine).
    """
    patterns = _engine_patterns(proc)
    module_end = proc.module_base + 0x4000000

    while True:
        candidates = set()
        for back, front in patterns:
            hits, _, _ = scan_for_engine(proc, back, front)
            candidates.update(h for h in hits if not (proc.module_base <= h < module_end))

        first = {}
        for e in candidates:
            try:
                if proc.read_double(e + OFF_WINDOW) < 0.001:
                    continue
                first[e] = proc.read_double(e + OFF_CLOCK)
            except OSError:
                pass

        time.sleep(0.12)

        for e, c0 in first.items():
            try:
                if abs(proc.read_double(e + OFF_CLOCK) - c0) > 1e-6:
                    return e
            except OSError:
                continue

        sys.stdout.write(".")
        sys.stdout.flush()
        time.sleep(0.4)


def load_manifest(song_dir: str) -> dict:
    with open(os.path.join(song_dir, "manifest.json"), encoding="utf-8") as f:
        return json.load(f)
