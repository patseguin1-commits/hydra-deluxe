"""Find the live DrumsEngine object by reading memory; no debugger needed.

Plain version: the engine object keeps copies of the two window constants side
by side at +0x30 and +0x38. So we search the game's writable memory for those
16 bytes, and every match is an engine-shaped object. Clone Hero leaves old
engine objects on the heap after a restart, frozen at their last clock, so the
live one is the only candidate whose song clock moves between two reads.

This is the route play_chart.py proved at the game on 2026-09-25. The memory
scan moved here from experiments/find_engine.py; the "whose clock moves" check
from play_chart.py and experiments/live.py. Behaviour is unchanged.
"""

from __future__ import annotations

import ctypes
import sys
import time
from typing import Callable, List, Optional, Sequence, TextIO, Tuple

from . import constants as C

Pattern = Tuple[bytes, bytes]   # (back-window bytes, front-window bytes)

# Matches this far past the module base are GameAssembly.dll's own .rdata
# copies of the constants, not engine objects.
MODULE_SPAN = 0x4000000


# --- the memory scan ---------------------------------------------------------

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", ctypes.c_ulong),
        ("RegionSize", ctypes.c_size_t),
        ("State", ctypes.c_ulong),
        ("Protect", ctypes.c_ulong),
        ("Type", ctypes.c_ulong),
    ]


MEM_COMMIT = 0x1000
PAGE_READWRITE = 0x04
PAGE_WRITECOPY = 0x08
PAGE_EXECUTE_READWRITE = 0x40
PAGE_EXECUTE_WRITECOPY = 0x80


def is_rw(protect: int) -> bool:
    return protect in (
        PAGE_READWRITE, PAGE_WRITECOPY,
        PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY,
    )


def hits_in_region(base: int, data: bytes, pattern: bytes) -> List[int]:
    """Object addresses for every match of `pattern` in one region's bytes.
    The pattern sits at +0x30 in the object, so each object starts 0x30
    before its match."""
    hits: List[int] = []
    offset = 0
    while True:
        pos = data.find(pattern, offset)
        if pos == -1:
            return hits
        hits.append(base + pos - C.OFF_BACK_WINDOW)
        offset = pos + 1


def scan_for_engine(proc, back_bytes: bytes, front_bytes: bytes):
    """Scan committed RW memory for +0x30 = back, +0x38 = front. LIVE-ONLY.
    Returns (object addresses, regions scanned, bytes scanned)."""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    handle = ctypes.c_void_p(proc.handle)
    mbi = MEMORY_BASIC_INFORMATION()
    mbi_size = ctypes.sizeof(mbi)
    pattern = back_bytes + front_bytes  # 16 contiguous bytes

    addr = 0
    hits: List[int] = []
    regions_scanned = 0
    bytes_scanned = 0
    max_addr = 0x7FFFFFFFFFFF  # user-mode limit on 64-bit Windows

    while addr < max_addr:
        ret = k32.VirtualQueryEx(
            handle, ctypes.c_void_p(addr), ctypes.byref(mbi), mbi_size
        )
        if ret == 0:
            break

        if mbi.State == MEM_COMMIT and is_rw(mbi.Protect) and mbi.RegionSize > 0:
            base = mbi.BaseAddress or 0
            size = mbi.RegionSize
            # Skip tiny regions and absurdly large ones
            if 0x100 <= size <= 256 * 1024 * 1024:
                try:
                    data = proc.read(base, size)
                    regions_scanned += 1
                    bytes_scanned += len(data)
                    hits.extend(hits_in_region(base, data, pattern))
                except OSError:
                    pass  # unreadable region

        next_addr = (mbi.BaseAddress or 0) + mbi.RegionSize
        if next_addr <= addr:
            break
        addr = next_addr

    return hits, regions_scanned, bytes_scanned


# --- which window constants to look for ---------------------------------------

def _raw(proc, rva: int) -> bytes:
    return proc.read(proc.resolve(rva), 8)


def normal_pattern(proc) -> Pattern:
    """The pair a normal-mode engine holds at +0x30/+0x38."""
    return (_raw(proc, C.RVA_CONST_NORMAL_BACK), _raw(proc, C.RVA_CONST_NORMAL_FRONT))


def all_patterns(proc) -> List[Pattern]:
    """Normal mode, plus the precision pair in both orders. The precision
    labels are not confirmed live, so both orders are searched."""
    p1 = _raw(proc, C.RVA_CONST_PRECISION_BACK)
    p2 = _raw(proc, C.RVA_CONST_PRECISION_FRONT)
    return [normal_pattern(proc), (p1, p2), (p2, p1)]


# --- whose clock moves ----------------------------------------------------------

def find_live_engine(
    proc,
    patterns: Sequence[Pattern],
    *,
    scan: Callable = scan_for_engine,
    sleep: Callable[[float], None] = time.sleep,
    out: Optional[TextIO] = None,
) -> int:
    """Return the engine object whose song clock is advancing. Blocks, printing
    a dot per try, until a song is playing.

    Clone Hero keeps several engine-shaped objects on the heap, and restarting
    a song allocates a NEW one while the old one stays frozen at its final
    time. So read every candidate's clock twice, a moment apart, and return
    the first one that changed.
    """
    out = sys.stdout if out is None else out
    module_end = proc.module_base + MODULE_SPAN
    while True:
        candidates: dict = {}   # ordered, so scan order decides ties
        for back, front in patterns:
            hits, _, _ = scan(proc, back, front)
            for h in hits:
                if not (proc.module_base <= h < module_end):
                    candidates[h] = None

        first = {}
        for e in candidates:
            try:
                if proc.read_double(e + C.OFF_TOTAL_WINDOW) < 0.001:
                    continue
                first[e] = proc.read_double(e + C.OFF_SONG_CLOCK)
            except OSError:
                pass

        sleep(0.12)

        for e, c0 in first.items():
            try:
                c1 = proc.read_double(e + C.OFF_SONG_CLOCK)
            except OSError:
                continue
            if abs(c1 - c0) > 1e-6:   # clock moved -> this is the live song
                return e

        out.write(".")
        out.flush()
        sleep(0.4)
