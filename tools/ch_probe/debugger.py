"""The Win32 debug loop for the Clone Hero hit-window probe.

Plain version: this is the measuring instrument. It attaches to the running
game as a debugger, plants breakpoints at chosen code addresses, and hands you
the CPU registers and memory each time one is hit. That is how the layers above
read the numbers the engine computes.

A software breakpoint here is one byte: we overwrite the first byte of an
instruction with 0xCC (the x86 "int3" trap), remember the byte we replaced, and
the CPU traps into us when it reaches that spot. To let the program keep
running we put the real byte back, step over that one instruction, then write
0xCC again so the breakpoint fires next time too. That put-back / step /
re-arm dance is the heart of this file.

The code splits into two halves on purpose:

  * Pure logic with no Windows calls -- the breakpoint bookkeeping (which byte
    to save, when it is armed), decoding the XMM0 register into a double, and
    the "rip minus one" fix-up. This half is unit-tested with a fake memory
    dict; no game needs to run.
  * The live Win32 pump -- ctypes calls to DebugActiveProcess,
    WaitForDebugEvent, GetThreadContext and friends. This half only works
    against a real debuggee, so it is quarantined behind a clear seam and the
    tests do not touch it. Every such spot is marked "LIVE-ONLY".

Do not import process.py from here; this module keeps its own thin Win32
bindings so the two layers stay independent.
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes  # _Win32 uses it; don't rely on another module importing it
import struct
import time
from typing import Callable, Dict, Optional


# The one-byte trap instruction. Writing this over an instruction's first byte
# is what makes a software breakpoint.
INT3 = 0xCC


# ---------------------------------------------------------------------------
# Pure logic: no Windows, unit-tested against a fake memory dict.
# ---------------------------------------------------------------------------


def decode_xmm0_double(xmm0_bytes: bytes) -> float:
    """Read the low 8 bytes of the 16-byte XMM0 register as a double.

    The hit-window formula returns its answer in XMM0. A double lives in the
    low half of that 128-bit register, little-endian. So take the first 8 bytes
    and unpack them as an IEEE-754 double.
    """
    if len(xmm0_bytes) < 8:
        raise ValueError("XMM0 buffer must be at least 8 bytes")
    return struct.unpack("<d", bytes(xmm0_bytes[:8]))[0]


def adjust_rip_after_int3(rip: int) -> int:
    """Give back the address of the breakpoint the CPU just hit.

    When a 0xCC traps, the instruction pointer has already moved one byte past
    it. So the breakpoint's real address is rip minus one. We rewind to there
    before restoring the original byte and stepping over it.
    """
    return rip - 1


def mask_breakpoints(base_addr: int, data: bytes, originals: Dict[int, int]) -> bytes:
    """Hide our 0xCC bytes from a memory read.

    When a caller reads code we have patched, they would see 0xCC where the real
    instruction byte belongs. This swaps each patched byte that falls inside the
    read back to the original, so readers always see the true code.

    `originals` maps a breakpoint address to the byte we saved there.
    """
    if not originals:
        return data
    out = bytearray(data)
    end = base_addr + len(data)
    for addr, original in originals.items():
        if base_addr <= addr < end:
            out[addr - base_addr] = original & 0xFF
    return bytes(out)


class _BP:
    """One breakpoint's bookkeeping: the saved byte, whether 0xCC is live now,
    and the callback to run when it fires."""

    __slots__ = ("original", "armed", "callback")

    def __init__(self, callback: Optional[Callable] = None) -> None:
        self.original: Optional[int] = None  # the real byte we overwrote
        self.armed: bool = False             # is 0xCC installed right now?
        self.callback = callback


class BreakpointTable:
    """Tracks software breakpoints and drives the save / restore / re-arm bytes.

    This class does the byte manipulation but nothing Windows-specific. It talks
    to memory through two plain callables you pass in:

        read(addr, size) -> bytes
        write(addr, data: bytes) -> None

    In the live debugger these are ReadProcessMemory / WriteProcessMemory. In
    the tests they are backed by a dict. Because the class never knows the
    difference, the whole state machine is unit-testable with no game running.
    """

    def __init__(self, read, write) -> None:
        self._read = read
        self._write = write
        self._bps: Dict[int, _BP] = {}

    def add(self, addr: int, callback: Optional[Callable] = None) -> None:
        """Register a breakpoint address (does not write 0xCC yet)."""
        if addr in self._bps:
            self._bps[addr].callback = callback
        else:
            self._bps[addr] = _BP(callback)

    def arm(self, addr: int) -> None:
        """Install 0xCC at the address, saving the real byte the first time."""
        bp = self._bps[addr]
        if bp.armed:
            return
        if bp.original is None:
            bp.original = self._read(addr, 1)[0]
        self._write(addr, bytes([INT3]))
        bp.armed = True

    def disarm(self, addr: int) -> None:
        """Put the real byte back, leaving the breakpoint remembered."""
        bp = self._bps[addr]
        if not bp.armed:
            return
        if bp.original is None:
            raise RuntimeError("cannot disarm a breakpoint with no saved byte")
        self._write(addr, bytes([bp.original & 0xFF]))
        bp.armed = False

    def remove(self, addr: int) -> None:
        """Restore the real byte if needed and forget the breakpoint."""
        bp = self._bps.get(addr)
        if bp is None:
            return
        if bp.armed:
            self.disarm(addr)
        del self._bps[addr]

    def addresses(self) -> list:
        """Every registered breakpoint address, armed or not."""
        return list(self._bps)

    # --- read-only queries, handy for the pump and the tests ---------------

    def has(self, addr: int) -> bool:
        return addr in self._bps

    def is_armed(self, addr: int) -> bool:
        bp = self._bps.get(addr)
        return bool(bp and bp.armed)

    def original(self, addr: int) -> Optional[int]:
        bp = self._bps.get(addr)
        return bp.original if bp else None

    def callback(self, addr: int) -> Optional[Callable]:
        bp = self._bps.get(addr)
        return bp.callback if bp else None

    def armed_originals(self) -> Dict[int, int]:
        """Map of every currently-armed address to its saved byte. The read
        path uses this to hide 0xCC from callers."""
        return {
            addr: bp.original
            for addr, bp in self._bps.items()
            if bp.armed and bp.original is not None
        }


class ThreadContext:
    """A snapshot of one thread's registers at a breakpoint.

    Registers are plain integer attributes (rip, rcx, rax, ... r8..r15). XMM0
    is kept as its raw 16 bytes and decoded on demand, because the formula
    returns its double there.

    Deliberately built from plain values, not a live handle, so the tests can
    fabricate one and check xmm0_double without a running game.
    """

    _GENERAL = (
        "rip", "rax", "rbx", "rcx", "rdx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    )

    def __init__(self, regs: Dict[str, int], xmm0: bytes = b"\x00" * 16) -> None:
        for name in self._GENERAL:
            setattr(self, name, int(regs.get(name, 0)))
        self._xmm0 = bytes(xmm0)

    def xmm0_double(self) -> float:
        """The double sitting in XMM0 (the formula's return value)."""
        return decode_xmm0_double(self._xmm0)


# ---------------------------------------------------------------------------
# LIVE-ONLY: Win32 ctypes plumbing below this line.
#
# Everything from here down needs a real process to attach to. It is not
# exercised by the unit tests -- there is no way to fake WaitForDebugEvent
# without a debuggee. The bindings are lazily created (see _Win32) so this
# module imports cleanly and the pure logic above stays testable even on a
# machine where these calls could not run.
# ---------------------------------------------------------------------------

# Debug-event codes from the Windows headers.
EXCEPTION_DEBUG_EVENT = 1
EXIT_PROCESS_DEBUG_EVENT = 5

# Exception codes we care about.
EXCEPTION_BREAKPOINT = 0x80000003   # a 0xCC trapped
EXCEPTION_SINGLE_STEP = 0x80000004  # the trap flag fired after one instruction

# How to answer ContinueDebugEvent.
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001

# CONTEXT_ALL for x64: control + integer + segments + float + debug registers.
CONTEXT_AMD64 = 0x00100000
CONTEXT_ALL = CONTEXT_AMD64 | 0x1F

# The trap flag in EFlags. Setting it makes the CPU single-step one instruction.
TRAP_FLAG = 0x100

# Wait this long (ms) for a debug event before looping back to check `until`.
_WAIT_TIMEOUT_MS = 100


class M128A(ctypes.Structure):
    _fields_ = [("Low", ctypes.c_ulonglong), ("High", ctypes.c_longlong)]


class XSAVE_FORMAT(ctypes.Structure):
    """The legacy FXSAVE area. We only reach in for XmmRegisters[0] = XMM0, but
    the full layout has to be right so that offset lands correctly."""

    _fields_ = [
        ("ControlWord", ctypes.c_ushort),
        ("StatusWord", ctypes.c_ushort),
        ("TagWord", ctypes.c_ubyte),
        ("Reserved1", ctypes.c_ubyte),
        ("ErrorOpcode", ctypes.c_ushort),
        ("ErrorOffset", ctypes.c_ulong),
        ("ErrorSelector", ctypes.c_ushort),
        ("Reserved2", ctypes.c_ushort),
        ("DataOffset", ctypes.c_ulong),
        ("DataSelector", ctypes.c_ushort),
        ("Reserved3", ctypes.c_ushort),
        ("MxCsr", ctypes.c_ulong),
        ("MxCsr_Mask", ctypes.c_ulong),
        ("FloatRegisters", M128A * 8),
        ("XmmRegisters", M128A * 16),
        ("Reserved4", ctypes.c_ubyte * 96),
    ]


class CONTEXT(ctypes.Structure):
    """The x64 CONTEXT record: every register GetThreadContext can hand back.

    LIVE-ONLY note: the real struct is 16-byte aligned. ctypes gives this
    8-byte alignment, which GetThreadContext tolerates in practice; if a future
    Windows build rejects it, allocate a 16-aligned buffer and cast. Not
    testable without a live thread.
    """

    _fields_ = [
        ("P1Home", ctypes.c_ulonglong),
        ("P2Home", ctypes.c_ulonglong),
        ("P3Home", ctypes.c_ulonglong),
        ("P4Home", ctypes.c_ulonglong),
        ("P5Home", ctypes.c_ulonglong),
        ("P6Home", ctypes.c_ulonglong),
        ("ContextFlags", ctypes.c_ulong),
        ("MxCsr", ctypes.c_ulong),
        ("SegCs", ctypes.c_ushort),
        ("SegDs", ctypes.c_ushort),
        ("SegEs", ctypes.c_ushort),
        ("SegFs", ctypes.c_ushort),
        ("SegGs", ctypes.c_ushort),
        ("SegSs", ctypes.c_ushort),
        ("EFlags", ctypes.c_ulong),
        ("Dr0", ctypes.c_ulonglong),
        ("Dr1", ctypes.c_ulonglong),
        ("Dr2", ctypes.c_ulonglong),
        ("Dr3", ctypes.c_ulonglong),
        ("Dr6", ctypes.c_ulonglong),
        ("Dr7", ctypes.c_ulonglong),
        ("Rax", ctypes.c_ulonglong),
        ("Rcx", ctypes.c_ulonglong),
        ("Rdx", ctypes.c_ulonglong),
        ("Rbx", ctypes.c_ulonglong),
        ("Rsp", ctypes.c_ulonglong),
        ("Rbp", ctypes.c_ulonglong),
        ("Rsi", ctypes.c_ulonglong),
        ("Rdi", ctypes.c_ulonglong),
        ("R8", ctypes.c_ulonglong),
        ("R9", ctypes.c_ulonglong),
        ("R10", ctypes.c_ulonglong),
        ("R11", ctypes.c_ulonglong),
        ("R12", ctypes.c_ulonglong),
        ("R13", ctypes.c_ulonglong),
        ("R14", ctypes.c_ulonglong),
        ("R15", ctypes.c_ulonglong),
        ("Rip", ctypes.c_ulonglong),
        ("FltSave", XSAVE_FORMAT),
        ("VectorRegister", M128A * 26),
        ("VectorControl", ctypes.c_ulonglong),
        ("DebugControl", ctypes.c_ulonglong),
        ("LastBranchToRip", ctypes.c_ulonglong),
        ("LastBranchFromRip", ctypes.c_ulonglong),
        ("LastExceptionToRip", ctypes.c_ulonglong),
        ("LastExceptionFromRip", ctypes.c_ulonglong),
    ]


class EXCEPTION_RECORD(ctypes.Structure):
    _fields_ = [
        ("ExceptionCode", ctypes.c_ulong),
        ("ExceptionFlags", ctypes.c_ulong),
        ("ExceptionRecord", ctypes.c_void_p),
        ("ExceptionAddress", ctypes.c_void_p),
        ("NumberParameters", ctypes.c_ulong),
        ("ExceptionInformation", ctypes.c_ulonglong * 15),
    ]


class EXCEPTION_DEBUG_INFO(ctypes.Structure):
    _fields_ = [
        ("ExceptionRecord", EXCEPTION_RECORD),
        ("dwFirstChance", ctypes.c_ulong),
    ]


class _DEBUG_EVENT_UNION(ctypes.Union):
    # Only the exception arm is decoded here; the pad covers the other event
    # types so the struct is big enough.
    _fields_ = [
        ("Exception", EXCEPTION_DEBUG_INFO),
        ("pad", ctypes.c_ubyte * 176),
    ]


class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [
        ("dwDebugEventCode", ctypes.c_ulong),
        ("dwProcessId", ctypes.c_ulong),
        ("dwThreadId", ctypes.c_ulong),
        ("u", _DEBUG_EVENT_UNION),
    ]


# Access masks for OpenThread.
THREAD_GET_CONTEXT = 0x0008
THREAD_SET_CONTEXT = 0x0010
THREAD_ALL_ACCESS = 0x1F03FF


class _Win32:
    """Thin lazily-built wrapper over the Win32 debug and memory calls.

    LIVE-ONLY. Built the first time we attach so importing this module never
    touches windll (which keeps the pure logic importable everywhere).
    """

    def __init__(self) -> None:
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        wintypes = ctypes.wintypes
        self.k32 = k32

        k32.DebugActiveProcess.argtypes = [wintypes.DWORD]
        k32.DebugActiveProcess.restype = wintypes.BOOL

        k32.DebugActiveProcessStop.argtypes = [wintypes.DWORD]
        k32.DebugActiveProcessStop.restype = wintypes.BOOL

        k32.DebugSetProcessKillOnExit.argtypes = [wintypes.BOOL]
        k32.DebugSetProcessKillOnExit.restype = wintypes.BOOL

        k32.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), wintypes.DWORD]
        k32.WaitForDebugEvent.restype = wintypes.BOOL

        k32.ContinueDebugEvent.argtypes = [
            wintypes.DWORD, wintypes.DWORD, wintypes.DWORD]
        k32.ContinueDebugEvent.restype = wintypes.BOOL

        k32.OpenThread.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        k32.OpenThread.restype = wintypes.HANDLE

        k32.GetThreadContext.argtypes = [wintypes.HANDLE, ctypes.POINTER(CONTEXT)]
        k32.GetThreadContext.restype = wintypes.BOOL

        k32.SetThreadContext.argtypes = [wintypes.HANDLE, ctypes.POINTER(CONTEXT)]
        k32.SetThreadContext.restype = wintypes.BOOL

        k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        k32.OpenProcess.restype = wintypes.HANDLE

        k32.ReadProcessMemory.argtypes = [
            wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
            ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        k32.ReadProcessMemory.restype = wintypes.BOOL

        k32.WriteProcessMemory.argtypes = [
            wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
            ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        k32.WriteProcessMemory.restype = wintypes.BOOL

        k32.FlushInstructionCache.argtypes = [
            wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t]
        k32.FlushInstructionCache.restype = wintypes.BOOL

        k32.CloseHandle.argtypes = [wintypes.HANDLE]
        k32.CloseHandle.restype = wintypes.BOOL


PROCESS_ALL_ACCESS = 0x1F0FFF


class Debugger:
    """Attach to a process, plant int3 breakpoints, and pump debug events.

    This is the class interfaces.py names as the debugger. The pure state
    machine (BreakpointTable) does the byte work; this class wires it to the
    live Win32 event loop. See the LIVE-ONLY banner above -- the pump is only
    meaningful against a real debuggee.
    """

    def __init__(self) -> None:
        self._pid: Optional[int] = None
        self._win32: Optional[_Win32] = None
        self._proc_handle = None
        self._table = BreakpointTable(self._raw_read, self._raw_write)
        # Threads that just stepped over a restored breakpoint and now need the
        # 0xCC written back: thread id -> breakpoint address.
        self._pending_rearm: Dict[int, int] = {}
        self._seen_initial = False   # swallow the one system breakpoint on attach
        self._stopped = False
        # Breakpoints stop() has removed. A hit on one may already be queued.
        self._removed: set = set()

    # --- attach / detach ---------------------------------------------------

    def attach(self, pid: int) -> None:
        """Attach to a running process by id. LIVE-ONLY.

        Right after attaching, turn kill-on-exit off. Windows' default kills
        every process a debugger thread is attached to when that thread exits,
        so a Python crash or Ctrl+C would take Clone Hero down too. With it
        off, the thread detaches instead. The call needs the debugging
        connection to exist first, so it comes after DebugActiveProcess:
        https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-debugsetprocesskillonexit
        """
        self._win32 = _Win32()
        self._pid = pid
        k32 = self._win32.k32
        if not k32.DebugActiveProcess(pid):
            raise ctypes.WinError(ctypes.get_last_error())
        if not k32.DebugSetProcessKillOnExit(False):
            err = ctypes.get_last_error()
            k32.DebugActiveProcessStop(pid)   # never stay attached with kill-on-exit on
            raise ctypes.WinError(err)
        self._proc_handle = k32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
        if not self._proc_handle:
            raise ctypes.WinError(ctypes.get_last_error())
        self._stopped = False
        self._seen_initial = False

    def stop(self) -> None:
        """Detach and let the process run free. LIVE-ONLY.

        Call it from the thread that attached; only that thread gets debug
        events. Order matters, because a 0xCC or a single-step trap left for
        the game after we detach would crash it:
          1. Put every patched byte back and forget the breakpoints.
          2. Answer queued debug events until none is left and no thread is
             still mid-step over a breakpoint (at most one second).
          3. Detach.
        """
        self._stopped = True
        if self._win32 and self._pid is not None:
            for addr in self._table.addresses():
                try:
                    self._table.remove(addr)
                except Exception:
                    pass
                self._removed.add(addr)
            self._drain(timeout_s=1.0)
            self._win32.k32.DebugActiveProcessStop(self._pid)
        if self._win32 and self._proc_handle:
            self._win32.k32.CloseHandle(self._proc_handle)
            self._proc_handle = None

    def _drain(self, timeout_s: float) -> None:
        """Answer queued debug events until the queue is empty and no thread
        is mid-step, or until timeout_s passes. LIVE-ONLY."""
        ev = DEBUG_EVENT()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if not self._win32.k32.WaitForDebugEvent(ctypes.byref(ev), 50):
                if not self._pending_rearm:
                    return
                continue
            status = DBG_CONTINUE
            if ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT:
                status = self._handle_exception(ev)
            self._win32.k32.ContinueDebugEvent(
                ev.dwProcessId, ev.dwThreadId, status)

    def _rewind_rip(self, tid: int) -> None:
        """Move one thread's rip back one byte, onto the instruction our 0xCC
        had replaced (its real byte is back now). LIVE-ONLY."""
        handle = self._win32.k32.OpenThread(THREAD_ALL_ACCESS, False, tid)
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            ctx = CONTEXT()
            ctx.ContextFlags = CONTEXT_ALL
            if not self._win32.k32.GetThreadContext(handle, ctypes.byref(ctx)):
                raise ctypes.WinError(ctypes.get_last_error())
            ctx.Rip = adjust_rip_after_int3(ctx.Rip)
            if not self._win32.k32.SetThreadContext(handle, ctypes.byref(ctx)):
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            self._win32.k32.CloseHandle(handle)

    # --- breakpoints -------------------------------------------------------

    def set_breakpoint(self, addr: int, callback: Callable) -> None:
        """Plant a software breakpoint at a live address and run `callback`
        (debugger, ThreadContext) each time it fires."""
        self._table.add(addr, callback)
        self._table.arm(addr)

    def clear_breakpoint(self, addr: int) -> None:
        """Remove a breakpoint and restore its original byte."""
        self._table.remove(addr)

    # --- memory ------------------------------------------------------------

    def read(self, addr: int, size: int) -> bytes:
        """Read process memory, hiding any 0xCC we planted from the caller."""
        data = self._raw_read(addr, size)
        return mask_breakpoints(addr, data, self._table.armed_originals())

    def write(self, addr: int, data: bytes) -> None:
        """Write process memory."""
        self._raw_write(addr, data)

    def _raw_read(self, addr: int, size: int) -> bytes:
        """LIVE-ONLY. Straight ReadProcessMemory with no breakpoint masking --
        the BreakpointTable calls this so it sees true bytes."""
        buf = (ctypes.c_ubyte * size)()
        got = ctypes.c_size_t(0)
        ok = self._win32.k32.ReadProcessMemory(
            self._proc_handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(got))
        if not ok:
            raise ctypes.WinError(ctypes.get_last_error())
        return bytes(buf[:got.value])

    def _raw_write(self, addr: int, data: bytes) -> None:
        """LIVE-ONLY. WriteProcessMemory plus an instruction-cache flush so the
        CPU sees a freshly written 0xCC."""
        buf = (ctypes.c_ubyte * len(data)).from_buffer_copy(bytes(data))
        wrote = ctypes.c_size_t(0)
        ok = self._win32.k32.WriteProcessMemory(
            self._proc_handle, ctypes.c_void_p(addr), buf, len(data),
            ctypes.byref(wrote))
        if not ok:
            raise ctypes.WinError(ctypes.get_last_error())
        self._win32.k32.FlushInstructionCache(
            self._proc_handle, ctypes.c_void_p(addr), len(data))

    # --- the event pump ----------------------------------------------------

    def run(self, until: Optional[Callable[[], bool]] = None) -> None:
        """Pump debug events until `until()` is true or the process exits.

        LIVE-ONLY. The loop waits a short time for each event so it can keep
        re-checking the `until` predicate even when nothing is happening. Each
        breakpoint hit runs its callback, then the byte is restored, stepped
        over, and re-armed (see _handle_exception).
        """
        if self._win32 is None:
            raise RuntimeError("attach before running the debug loop")
        ev = DEBUG_EVENT()
        while not self._stopped:
            if until is not None and until():
                break
            got = self._win32.k32.WaitForDebugEvent(
                ctypes.byref(ev), _WAIT_TIMEOUT_MS)
            if not got:
                continue  # timed out with no event; loop back and re-check until
            status = DBG_CONTINUE
            if ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT:
                status = self._handle_exception(ev)
            elif ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT:
                self._stopped = True
            self._win32.k32.ContinueDebugEvent(
                ev.dwProcessId, ev.dwThreadId, status)

    def _handle_exception(self, ev: "DEBUG_EVENT") -> int:
        """Decide what to do with one exception event. LIVE-ONLY.

        Returns the status to hand ContinueDebugEvent. The put-back / step /
        re-arm sequence lives here; the pure byte work it calls is in
        BreakpointTable and is unit-tested on its own.
        """
        rec = ev.u.Exception.ExceptionRecord
        code = rec.ExceptionCode
        tid = ev.dwThreadId
        addr = rec.ExceptionAddress or 0

        if code == EXCEPTION_BREAKPOINT:
            if self._table.is_armed(addr):
                self._on_our_breakpoint(addr, tid)
                return DBG_CONTINUE
            if addr in self._removed:
                # A thread reached one of our 0xCC bytes just before stop()
                # put the real byte back; its event was already queued. Step
                # rip back onto the restored instruction so it runs normally.
                self._rewind_rip(tid)
                return DBG_CONTINUE
            if not self._seen_initial:
                # The system fires one breakpoint right after attach. Swallow it.
                self._seen_initial = True
                return DBG_CONTINUE
            return DBG_EXCEPTION_NOT_HANDLED

        if code == EXCEPTION_SINGLE_STEP:
            addr_to_rearm = self._pending_rearm.pop(tid, None)
            if addr_to_rearm is not None:
                # We stepped over the restored instruction; put 0xCC back,
                # unless stop() has removed that breakpoint meanwhile.
                if self._table.has(addr_to_rearm):
                    self._table.arm(addr_to_rearm)
                return DBG_CONTINUE
            return DBG_EXCEPTION_NOT_HANDLED

        return DBG_EXCEPTION_NOT_HANDLED

    def _on_our_breakpoint(self, addr: int, tid: int) -> None:
        """Handle a hit on one of our breakpoints. LIVE-ONLY.

        Restore the real byte, rewind rip to the breakpoint, run the callback,
        then set the trap flag so the next event is a single-step where we
        re-arm the 0xCC.
        """
        handle = self._win32.k32.OpenThread(THREAD_ALL_ACCESS, False, tid)
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            ctx = CONTEXT()
            ctx.ContextFlags = CONTEXT_ALL
            if not self._win32.k32.GetThreadContext(handle, ctypes.byref(ctx)):
                raise ctypes.WinError(ctypes.get_last_error())

            # The real breakpoint address is rip minus one; rewind to it.
            ctx.Rip = adjust_rip_after_int3(ctx.Rip)
            # Put the true instruction byte back so it can execute.
            self._table.disarm(addr)
            # Ask for a single-step so we get a chance to re-arm afterwards.
            ctx.EFlags |= TRAP_FLAG
            if not self._win32.k32.SetThreadContext(handle, ctypes.byref(ctx)):
                raise ctypes.WinError(ctypes.get_last_error())

            self._pending_rearm[tid] = addr
            callback = self._table.callback(addr)
            if callback is not None:
                callback(self, self._context_from_ctx(ctx))
        finally:
            self._win32.k32.CloseHandle(handle)

    @staticmethod
    def _context_from_ctx(ctx: "CONTEXT") -> ThreadContext:
        """Copy a live CONTEXT into a plain ThreadContext. LIVE-ONLY.

        The register names are lower-cased; XMM0's 16 bytes are packed out so
        xmm0_double can decode them without any Windows types leaking upward.
        """
        regs = {
            "rip": ctx.Rip, "rax": ctx.Rax, "rbx": ctx.Rbx, "rcx": ctx.Rcx,
            "rdx": ctx.Rdx, "rsp": ctx.Rsp, "rbp": ctx.Rbp, "rsi": ctx.Rsi,
            "rdi": ctx.Rdi, "r8": ctx.R8, "r9": ctx.R9, "r10": ctx.R10,
            "r11": ctx.R11, "r12": ctx.R12, "r13": ctx.R13, "r14": ctx.R14,
            "r15": ctx.R15,
        }
        xmm0 = ctx.FltSave.XmmRegisters[0]
        xmm0_bytes = struct.pack("<Qq", xmm0.Low, xmm0.High)
        return ThreadContext(regs, xmm0_bytes)
