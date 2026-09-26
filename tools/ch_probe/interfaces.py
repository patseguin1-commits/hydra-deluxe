"""The contract between the seven pieces of the probe tool.

Read this before writing any module. Each piece implements the Protocol named
for it here. Keeping the signatures fixed is what lets the pieces be built
independently and still snap together: the engine model calls the process and
debugger through these methods, and the experiment runners call everything
through them, without anyone having seen anyone else's implementation.

These are structural Protocols (duck typing). A module does NOT have to inherit
from them -- it just has to expose methods with these names and shapes. The
concrete class names in parentheses are the expected ones; match them so the
experiment runners can import them by name.

Nothing here does real work. This file is documentation with teeth: a module
that drifts from its Protocol will fail the type/attribute checks the
integration step runs.
"""

from __future__ import annotations

from typing import Callable, Optional, Protocol, Sequence, runtime_checkable


# --- process.py : the process/address layer ---------------------------------
#
# Opens the Clone Hero process, finds where GameAssembly.dll loaded, turns RVAs
# into live addresses, reads/writes memory. Before anything else it byte-checks
# its targets against what Ghidra saw and refuses to run on a mismatch.

@runtime_checkable
class ProcessHandle(Protocol):
    """Concrete class expected: `Process` in process.py."""

    module_base: int  # live load address of GameAssembly.dll

    def resolve(self, rva: int) -> int:
        """Turn an RVA (see constants.py) into a live absolute address."""
        ...

    def read(self, addr: int, size: int) -> bytes:
        ...

    def write(self, addr: int, data: bytes) -> None:
        ...

    def read_double(self, addr: int) -> float:
        """Read an 8-byte little-endian IEEE double at a live address."""
        ...

    def read_u32(self, addr: int) -> int:
        ...

    def read_u64(self, addr: int) -> int:
        ...

    def read_const_double(self, rva: int) -> float:
        """Convenience: resolve an .rdata RVA and read the double there."""
        ...

    def verify_targets(self) -> None:
        """Sanity-check the address pipeline. Reads the known window constants
        (and any cheap code-prologue checks) and raises on a mismatch instead
        of letting a wrong-build read poison everything downstream. This is the
        spec's milestone 1."""
        ...


def open_process(process_name: str = ...) -> ProcessHandle:
    """Factory in process.py. Opens the named process (default from
    constants.PROCESS_NAME), locates GameAssembly.dll, returns a ProcessHandle.
    Raises if the process or module is not found."""
    ...


# --- debugger.py : the Win32 debug loop -------------------------------------
#
# WaitForDebugEvent engine. Sets int3 (0xCC) software breakpoints, catches
# them, exposes registers and memory, restores/single-steps to continue.

@runtime_checkable
class ThreadContext(Protocol):
    """A register snapshot at a breakpoint. Attribute access by register name,
    at least: rip, rax, rbx, rcx, rdx, rsp, rbp, r8..r15. xmm0 is exposed as a
    float via `xmm0_double` because that is where the formula returns."""

    rip: int
    rcx: int

    def xmm0_double(self) -> float:
        ...


# A breakpoint callback receives the debugger (for reads/register access) and
# the thread context at the hit. Return value is ignored.
BreakpointCallback = Callable[["Debugger", ThreadContext], None]


@runtime_checkable
class Debugger(Protocol):
    """Concrete class expected: `Debugger` in debugger.py."""

    def attach(self, pid: int) -> None:
        ...

    def set_breakpoint(self, addr: int, callback: BreakpointCallback) -> None:
        """Place a software (int3) breakpoint at a live address."""
        ...

    def clear_breakpoint(self, addr: int) -> None:
        ...

    def read(self, addr: int, size: int) -> bytes:
        ...

    def write(self, addr: int, data: bytes) -> None:
        ...

    def run(self, until: Optional[Callable[[], bool]] = None) -> None:
        """Pump WaitForDebugEvent, dispatching breakpoint callbacks, until the
        `until` predicate returns True (or the process exits)."""
        ...

    def stop(self) -> None:
        ...


# --- engine.py : the meaning layer ------------------------------------------
#
# Knows the RVAs and offsets. Captures the live object pointer at the ctor
# breakpoint. Exposes clean reads so nothing above it speaks in raw addresses.

@runtime_checkable
class EngineModel(Protocol):
    """Concrete class expected: `EngineModel` in engine.py. Built from a
    ProcessHandle and a Debugger."""

    object_ptr: Optional[int]  # captured at the constructor breakpoint

    def capture_object(self) -> int:
        """Breakpoint the constructor, grab rcx, remember it. Returns the ptr."""
        ...

    def use_object(self, object_ptr: int) -> int:
        """Adopt a pointer found by engine_finder's memory scan."""
        ...

    def score(self) -> int:
        """self+0x94, rises only on a hit."""
        ...

    def total_window(self) -> float:
        """self+0x20, the field the passive probe watches (seconds)."""
        ...

    def back_window(self) -> float:
        ...

    def front_window(self) -> float:
        ...

    def hit_time(self) -> float:
        ...

    def note_count(self) -> int:
        ...

    def precision_mode(self) -> bool:
        """True if the PrecisionMode flag bit is set."""
        ...

    def song_clock(self) -> float:
        """Current song time (seconds), self+0x100, proven live."""
        ...

    def constants(self) -> dict:
        """Read and decode the .rdata window/formula constants into a dict of
        floats, keyed by the names used in constants.py."""
        ...


# --- probe_chart.py : the probe-chart generator -----------------------------
#
# Writes small .chart files of isolated note pairs at controlled spacings.

def generate_probe_chart(
    spacings_ms: Sequence[float],
    path: str,
    *,
    resolution: int = 192,
    bpm: float = 120.0,
    note: int = 0,
) -> None:
    """Function in probe_chart.py. Emit a valid Expert-drums .chart at `path`
    with one isolated note pair per spacing in `spacings_ms`: two notes that
    many ticks apart, with wide silence around each pair so nothing overlaps.
    Must round-trip through the game's chart loader."""
    ...


# --- input_driver.py : the input driver -------------------------------------
#
# Wraps SendInput; schedules a keystroke against the song clock at a target
# offset. Its timing only needs to land NEAR the edge -- the measured delta is
# what gets recorded, not the intended one.

@runtime_checkable
class InputDriver(Protocol):
    """Concrete class expected: `InputDriver` in input_driver.py."""

    def set_binding(self, lane: int, vk: int) -> None:
        """Map a drum lane to a virtual-key code (read the game's config; don't
        guess -- see the open question in the spec)."""
        ...

    def schedule_hit(
        self, lane: int, at_song_time: float, clock: Callable[[], float]
    ) -> None:
        """Fire a keystroke for `lane` when `clock()` reaches `at_song_time`.
        `clock` is the engine song clock (seconds)."""
        ...

    def tap(self, lane: int) -> None:
        """Fire the keystroke for `lane` immediately (down then up)."""
        ...

    def send_key(self, vk: int, key_up: bool) -> None:
        """One key event through SendInput (the only OS call)."""
        ...

    def press_chord(self, lanes: Sequence[int], *, hold_s: float = 0.003) -> list:
        """All lanes' keys down, hold, all up."""
        ...


# --- ocr.py : the OCR cross-check -------------------------------------------
#
# Only the text parser remains (parse_accuracy_text). The screen-capture path
# had no caller and was deleted in 2026-09; git history has it.
