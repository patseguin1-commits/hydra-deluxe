"""The meaning layer: turn raw addresses into named facts about the engine.

Everything above this module talks about "the total window" or "precision
mode", never about byte offsets or RVAs. This module is the only place in the
probe that knows an engine field lives at object_ptr + 0x20. It gets its
addresses from constants.py and its plumbing (memory reads, breakpoints) from a
ProcessHandle and a Debugger passed in.

Why the dependency injection: this module never imports process.py or
debugger.py. It only depends on the ProcessHandle and Debugger Protocols in
interfaces.py -- the shapes, not the concrete classes. That is what lets the
unit tests drive it with fake objects and no running game.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Optional

try:
    # Normal case: imported as part of the ch_probe package.
    from . import constants as C
except ImportError:  # pragma: no cover - exercised only by direct-script runs
    # Fallback: the module's own directory is on sys.path and it is imported as
    # a top-level module (how the unit tests load it without a package root).
    import constants as C  # type: ignore[no-redef]

if TYPE_CHECKING:
    # Type hints only. These are Protocols (duck-typed shapes), not concrete
    # classes, so importing them creates no link to process.py or debugger.py.
    from .interfaces import Debugger, ProcessHandle, ThreadContext


class EngineModel:
    """Reads named facts off the live DrumsEngine object.

    Build it with a ProcessHandle (memory reads) and a Debugger (breakpoints).
    Call capture_object() once to learn where the engine object is, then use the
    reader methods.
    """

    def __init__(self, process: "ProcessHandle",
                 debugger: Optional["Debugger"] = None) -> None:
        self._process = process
        # Only capture_object() needs a debugger. A runner that finds the
        # engine by memory scan (engine_finder) passes none.
        self._debugger = debugger
        # Filled in by capture_object(). None until then.
        self.object_ptr: Optional[int] = None

    # --- capture -------------------------------------------------------------

    def capture_object(self) -> int:
        """Learn the live engine object's address by catching its constructor.

        On entry to the DrumsEngine constructor the object pointer sits in rcx
        (x64 fastcall / IL2CPP calling convention). So: put a breakpoint on the
        constructor, and when it fires read rcx and remember it.

        LIVE-ONLY SEAM: with a real debugger, setting the breakpoint only arms
        it; the callback fires later when the game constructs the engine, which
        is why we then pump run() until the pointer shows up. The fake debugger
        in the tests fires the callback the moment the breakpoint is set, so the
        pointer is already captured and run() is never called.
        """
        if self._debugger is None:
            raise RuntimeError(
                "capture_object needs a debugger; use use_object() with a "
                "pointer from engine_finder instead")
        addr = self._process.resolve(C.RVA_DRUMS_ENGINE_CTOR)

        def _on_ctor(debugger: "Debugger", ctx: "ThreadContext") -> None:
            self.object_ptr = ctx.rcx

        self._debugger.set_breakpoint(addr, _on_ctor)

        if self.object_ptr is None:
            # Real path: wait for the game to actually build the engine.
            self._debugger.run(until=lambda: self.object_ptr is not None)

        if self.object_ptr is None:
            raise RuntimeError(
                "constructor breakpoint never fired; engine object not captured"
            )
        return self.object_ptr

    def use_object(self, object_ptr: int) -> int:
        """Adopt an engine object found another way: the memory scan in
        engine_finder, the route play_chart.py proved live. Returns it."""
        self.object_ptr = object_ptr
        return object_ptr

    # --- per-object reads ----------------------------------------------------

    def _addr(self, offset: int) -> int:
        """Absolute address of a field, given its byte offset off the object."""
        if self.object_ptr is None:
            raise RuntimeError(
                "no engine object captured yet; call capture_object() first"
            )
        return self.object_ptr + offset

    def total_window(self) -> float:
        """The total search window (seconds). This is the field the passive probe
        watches: set to back*2 at construction, then overwritten per note by the
        formula's caller."""
        return self._process.read_double(self._addr(C.OFF_TOTAL_WINDOW))

    def back_window(self) -> float:
        """Back-window constant (seconds): the max per-side, ~85 in normal mode."""
        return self._process.read_double(self._addr(C.OFF_BACK_WINDOW))

    def front_window(self) -> float:
        """Front-window constant (seconds): the min per-side, ~37.5 in normal mode."""
        return self._process.read_double(self._addr(C.OFF_FRONT_WINDOW))

    def hit_time(self) -> float:
        """Song time of the last hit, in seconds, from +0x2e0. The code reading
        says the game copies the clock here on a hit; not yet confirmed live."""
        return self._process.read_double(self._addr(C.OFF_HIT_TIME))

    def score(self) -> int:
        """The game score. It rises only when a note is hit, which is how
        play_chart.py tells a hit from a miss."""
        return self._process.read_u32(self._addr(C.OFF_SCORE))

    def note_count(self) -> int:
        """Note count the processing loop uses."""
        return self._process.read_u32(self._addr(C.OFF_NOTE_COUNT))

    def precision_mode(self) -> bool:
        """True when the PrecisionMode flag bit is set.

        The flags field is a dword; PrecisionMode is one bit inside it
        (0x1000). Read the whole dword and test that single bit. A clear bit
        means normal mode.
        """
        flags = self._process.read_u32(self._addr(C.OFF_FLAGS))
        return (flags & C.PRECISION_MODE_BIT) != 0

    def song_clock(self) -> float:
        """Current song time in seconds, from +0x100. Proven live on
        2026-09-25: play_chart.py plays whole songs off this field. The game
        writes it once per frame, so a read can be a few ms stale."""
        return self._process.read_double(self._addr(C.OFF_SONG_CLOCK))

    # --- global constants ----------------------------------------------------

    def constants(self) -> dict:
        """Read every .rdata window/formula constant into a flat dict of floats.

        These live at fixed spots in the game's read-only data, so they don't
        need a captured object -- just the module base, which the ProcessHandle
        already knows. Keys are readable names; values are the decoded doubles.
        """
        read = self._process.read_const_double
        out = {
            # Per-side window constants, both modes.
            "normal_back": read(C.RVA_CONST_NORMAL_BACK),
            "normal_front": read(C.RVA_CONST_NORMAL_FRONT),
            "precision_back": read(C.RVA_CONST_PRECISION_BACK),
            "precision_front": read(C.RVA_CONST_PRECISION_FRONT),
            # Shared divisor and exponent.
            C.CONST_KEY_DIVISOR: read(C.RVA_FORMULA_DIVISOR),
            C.CONST_KEY_EXPONENT: read(C.RVA_FORMULA_EXPONENT),
            # Threshold used in the hit-check comparison.
            "hitcheck_threshold": read(C.RVA_HITCHECK_THRESHOLD),
        }
        # Normal-branch formula constants, prefixed so the names stay clear.
        for name, rva in C.RVA_FORMULA_NORMAL.items():
            out[C.CONST_KEY_PREFIX_NORMAL + name] = read(rva)
        # Precision-branch formula constants.
        for name, rva in C.RVA_FORMULA_PRECISION.items():
            out[C.CONST_KEY_PREFIX_PRECISION + name] = read(rva)
        return out
