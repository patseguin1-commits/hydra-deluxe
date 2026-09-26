"""Shared live-game pieces for the hit-window experiments.

watch_window.py and walk_edges.py both read the same engine fields and load a
probe song's manifest. They share this file so the two scripts agree on how a
sample is read. Every offset comes from constants.py. Finding the engine is
engine_finder.find_live_engine with engine_finder.all_patterns, which also
finds an engine in precision mode.
"""

from __future__ import annotations

import json
import os
import struct
import sys
from dataclasses import dataclass

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants as C

# One read covers every watched field, so a sample is a consistent snapshot.
# The hit-time candidate at +0x2e0 is the furthest field out.
SNAPSHOT_SIZE = C.OFF_HIT_TIME + 8

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
        return bool(self.flags & C.PRECISION_MODE_BIT)


def decode_snapshot(raw: bytes) -> Snapshot:
    """Turn SNAPSHOT_SIZE bytes read from the engine base into a Snapshot."""
    def dbl(off: int) -> float:
        return struct.unpack_from("<d", raw, off)[0]

    def u32(off: int) -> int:
        return struct.unpack_from("<I", raw, off)[0]

    return Snapshot(
        window_ms=dbl(C.OFF_TOTAL_WINDOW) * 1000.0,
        clock_s=dbl(C.OFF_SONG_CLOCK),
        score=u32(C.OFF_SCORE),
        hit_time_s=dbl(C.OFF_HIT_TIME),
        flags=u32(C.OFF_FLAGS),
    )


def read_snapshot(proc, engine: int) -> Snapshot:
    return decode_snapshot(proc.read(engine, SNAPSHOT_SIZE))


def load_manifest(song_dir: str) -> dict:
    with open(os.path.join(song_dir, "manifest.json"), encoding="utf-8") as f:
        return json.load(f)
