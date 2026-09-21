"""The process/address layer for the Clone Hero drum hit-window probe.

Plain version: this is the piece that reaches into the running game's memory.
It opens the Clone Hero process, finds where GameAssembly.dll got loaded, and
turns the fixed addresses in constants.py into live addresses it can read.

Two ideas keep this file honest:

  * An RVA is "how far into the DLL", not a real address. The DLL loads at a
    different spot every run (Windows randomizes it). So a live address is
    always module_base + rva, and module_base is discovered when we attach.
  * Before trusting any read, we byte-check two known constants against the
    values Ghidra saw. If Clone Hero updated and shifted everything, those two
    reads come back wrong and we refuse to run instead of reporting garbage.
    That check is milestone 1 in the spec: if 85.0 and 37.5 come out, the whole
    address pipeline is proven.

The raw Windows calls (OpenProcess, ReadProcessMemory, the process/module
snapshots) live behind small seams so the decode and decision logic can be
tested with fabricated bytes, no game required. The only part that genuinely
needs a live Clone Hero is open_process itself.
"""

from __future__ import annotations

import struct
from typing import Callable, Optional

from . import constants


# --- Pure helpers: decode raw bytes, decide pass/fail ------------------------
#
# These touch no Windows API. They are the testable core. Everything the class
# does that matters (turning bytes into numbers, deciding the build matches) is
# routed through here so a unit test can drive it with hand-made bytes.

def decode_double(raw: bytes) -> float:
    """Turn 8 raw bytes into a float. The game stores doubles little-endian
    (least significant byte first), the normal x86-64 layout."""
    if len(raw) != 8:
        raise ValueError(f"a double needs exactly 8 bytes, got {len(raw)}")
    return struct.unpack("<d", raw)[0]


def decode_u32(raw: bytes) -> int:
    """Turn 4 raw bytes into an unsigned 32-bit integer, little-endian."""
    if len(raw) != 4:
        raise ValueError(f"a u32 needs exactly 4 bytes, got {len(raw)}")
    return struct.unpack("<I", raw)[0]


def decode_u64(raw: bytes) -> int:
    """Turn 8 raw bytes into an unsigned 64-bit integer, little-endian."""
    if len(raw) != 8:
        raise ValueError(f"a u64 needs exactly 8 bytes, got {len(raw)}")
    return struct.unpack("<Q", raw)[0]


def check_normal_constants(
    back_ms: float,
    front_ms: float,
    tolerance_ms: float = constants.CONST_MATCH_TOLERANCE_MS,
) -> None:
    """Decide whether the two normal-mode window constants we just read match
    what Ghidra recorded. Raises BuildMismatchError if either is off by more
    than the tolerance. This is the build-drift guard: a Clone Hero update
    shifts every address, so a wrong value here means the addresses no longer
    point where we think and nothing downstream can be trusted."""
    back_off = abs(back_ms - constants.EXPECT_NORMAL_BACK_MS)
    front_off = abs(front_ms - constants.EXPECT_NORMAL_FRONT_MS)
    problems = []
    if back_off > tolerance_ms:
        problems.append(
            f"back window read {back_ms:.4f} ms, expected "
            f"{constants.EXPECT_NORMAL_BACK_MS} ms (off by {back_off:.4f})"
        )
    if front_off > tolerance_ms:
        problems.append(
            f"front window read {front_ms:.4f} ms, expected "
            f"{constants.EXPECT_NORMAL_FRONT_MS} ms (off by {front_off:.4f})"
        )
    if problems:
        raise BuildMismatchError(
            "address pipeline check failed -- the running build does not match "
            "the addresses in constants.py. This usually means Clone Hero "
            "updated. Re-run Ghidra and update constants.py. Details: "
            + "; ".join(problems)
        )


class BuildMismatchError(RuntimeError):
    """Raised when the live window constants disagree with what Ghidra saw, so
    the addresses are pointing at the wrong bytes and we must not continue."""


class ProcessNotFoundError(RuntimeError):
    """Raised when the named process, or its GameAssembly.dll module, is not
    running."""


# --- The Process class -------------------------------------------------------
#
# Implements the ProcessHandle Protocol from interfaces.py. It holds the live
# module base and two callables that actually touch memory: a reader and a
# writer. In production those callables are backed by ReadProcessMemory /
# WriteProcessMemory. In a test they are fakes that return fabricated bytes,
# which is how the decode and verify logic gets exercised without a game.

Reader = Callable[[int, int], bytes]
Writer = Callable[[int, bytes], None]


class Process:
    """A live handle to the Clone Hero process and its GameAssembly.dll module.

    Construct it through open_process() in normal use. The direct constructor
    takes the pieces separately so a test can supply a fake reader/writer and
    a made-up module base."""

    def __init__(
        self,
        module_base: int,
        reader: Reader,
        writer: Writer,
        *,
        pid: Optional[int] = None,
        handle: Optional[int] = None,
    ) -> None:
        self.module_base = module_base
        self._reader = reader
        self._writer = writer
        self.pid = pid
        self._handle = handle

    # Address math -----------------------------------------------------------

    def resolve(self, rva: int) -> int:
        """Turn an RVA (an offset into the DLL, from constants.py) into a live
        absolute address by adding where the DLL actually loaded."""
        return self.module_base + rva

    # Raw memory access ------------------------------------------------------

    def read(self, addr: int, size: int) -> bytes:
        """Read `size` bytes from the live address `addr`."""
        data = self._reader(addr, size)
        if len(data) != size:
            raise OSError(
                f"short read at {addr:#x}: asked for {size} bytes, got {len(data)}"
            )
        return data

    def write(self, addr: int, data: bytes) -> None:
        """Write raw bytes to the live address `addr`."""
        self._writer(addr, data)

    # Typed reads ------------------------------------------------------------

    def read_double(self, addr: int) -> float:
        """Read an 8-byte little-endian double at a live address."""
        return decode_double(self.read(addr, 8))

    def read_u32(self, addr: int) -> int:
        """Read a 4-byte little-endian unsigned int at a live address."""
        return decode_u32(self.read(addr, 4))

    def read_u64(self, addr: int) -> int:
        """Read an 8-byte little-endian unsigned int at a live address."""
        return decode_u64(self.read(addr, 8))

    def read_const_double(self, rva: int) -> float:
        """Resolve an .rdata RVA to a live address and read the double there.
        This is the shortcut every constant read uses."""
        return self.read_double(self.resolve(rva))

    # The build-drift guard --------------------------------------------------

    def verify_targets(self) -> None:
        """Read the two normal-mode window constants live and check them against
        the values Ghidra recorded. Raises BuildMismatchError on a mismatch.

        This is milestone 1: if the back window reads 85.0 and the front reads
        37.5, the address math is correct and everything downstream can be
        trusted. If they are wrong, the addresses point at the wrong bytes
        (almost always because Clone Hero updated) and we stop here."""
        back_ms = self.read_const_double(constants.RVA_CONST_NORMAL_BACK)
        front_ms = self.read_const_double(constants.RVA_CONST_NORMAL_FRONT)
        check_normal_constants(back_ms, front_ms)

    # Cleanup ----------------------------------------------------------------

    def close(self) -> None:
        """Release the OS process handle, if we hold one."""
        if self._handle is not None:
            _kernel32().CloseHandle(_wintypes().HANDLE(self._handle))
            self._handle = None

    def __enter__(self) -> "Process":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()


# --- The live Windows backend ------------------------------------------------
#
# Everything below here needs a real Windows and a real running game. It is the
# seam the unit tests do NOT cross. The tests build a Process with fake reader
# and writer callables and never call open_process. Only a live Clone Hero
# exercises this section, so it is commented as such and kept thin.

_KERNEL32 = None
_PSAPI = None


def _kernel32():
    """Load kernel32 lazily so importing this module never fails on a machine
    without the Windows API (e.g. a CI box running only the pure tests)."""
    global _KERNEL32
    if _KERNEL32 is None:
        import ctypes

        _KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
    return _KERNEL32


def _wintypes():
    import ctypes.wintypes

    return ctypes.wintypes


# Win32 constants for OpenProcess and the toolhelp snapshots.
_PROCESS_VM_READ = 0x0010
_PROCESS_VM_WRITE = 0x0020
_PROCESS_VM_OPERATION = 0x0008
_PROCESS_QUERY_INFORMATION = 0x0400
_TH32CS_SNAPPROCESS = 0x00000002
_TH32CS_SNAPMODULE = 0x00000008
_TH32CS_SNAPMODULE32 = 0x00000010
_INVALID_HANDLE_VALUE = -1


def _find_pid_by_name(process_name: str) -> int:
    """Walk the list of running processes and return the PID whose executable
    name matches `process_name`. LIVE-ONLY: needs a real process table.

    Raises ProcessNotFoundError if no such process is running."""
    import ctypes
    from ctypes import wintypes

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [
            ("dwSize", wintypes.DWORD),
            ("cntUsage", wintypes.DWORD),
            ("th32ProcessID", wintypes.DWORD),
            ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
            ("th32ModuleID", wintypes.DWORD),
            ("cntThreads", wintypes.DWORD),
            ("th32ParentProcessID", wintypes.DWORD),
            ("pcPriClassBase", ctypes.c_long),
            ("dwFlags", wintypes.DWORD),
            ("szExeFile", ctypes.c_wchar * 260),
        ]

    k32 = _kernel32()
    snap = k32.CreateToolhelp32Snapshot(_TH32CS_SNAPPROCESS, 0)
    if snap == _INVALID_HANDLE_VALUE:
        raise OSError(ctypes.get_last_error(), "CreateToolhelp32Snapshot failed")
    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)
        wanted = process_name.lower()
        if not k32.Process32FirstW(snap, ctypes.byref(entry)):
            raise ProcessNotFoundError("could not read the process list")
        while True:
            if entry.szExeFile.lower() == wanted:
                return int(entry.th32ProcessID)
            if not k32.Process32NextW(snap, ctypes.byref(entry)):
                break
    finally:
        k32.CloseHandle(snap)
    raise ProcessNotFoundError(
        f"process {process_name!r} is not running -- start Clone Hero first"
    )


def _find_module_base(pid: int, module_name: str) -> int:
    """Return the load address of `module_name` inside process `pid`.
    LIVE-ONLY: needs a real loaded module. Raises ProcessNotFoundError if the
    module is not loaded in that process."""
    import ctypes
    from ctypes import wintypes

    class MODULEENTRY32W(ctypes.Structure):
        _fields_ = [
            ("dwSize", wintypes.DWORD),
            ("th32ModuleID", wintypes.DWORD),
            ("th32ProcessID", wintypes.DWORD),
            ("GlblcntUsage", wintypes.DWORD),
            ("ProccntUsage", wintypes.DWORD),
            ("modBaseAddr", ctypes.POINTER(ctypes.c_byte)),
            ("modBaseSize", wintypes.DWORD),
            ("hModule", wintypes.HMODULE),
            ("szModule", ctypes.c_wchar * 256),
            ("szExePath", ctypes.c_wchar * 260),
        ]

    k32 = _kernel32()
    snap = k32.CreateToolhelp32Snapshot(
        _TH32CS_SNAPMODULE | _TH32CS_SNAPMODULE32, pid
    )
    if snap == _INVALID_HANDLE_VALUE:
        raise OSError(ctypes.get_last_error(), "CreateToolhelp32Snapshot (module) failed")
    try:
        entry = MODULEENTRY32W()
        entry.dwSize = ctypes.sizeof(MODULEENTRY32W)
        wanted = module_name.lower()
        if not k32.Module32FirstW(snap, ctypes.byref(entry)):
            raise ProcessNotFoundError("could not read the module list")
        while True:
            if entry.szModule.lower() == wanted:
                return ctypes.cast(entry.modBaseAddr, ctypes.c_void_p).value or 0
            if not k32.Module32NextW(snap, ctypes.byref(entry)):
                break
    finally:
        k32.CloseHandle(snap)
    raise ProcessNotFoundError(
        f"module {module_name!r} is not loaded in pid {pid}"
    )


def _make_reader(handle: int) -> Reader:
    """Build a reader callable backed by ReadProcessMemory. LIVE-ONLY: the
    handle must refer to a real, open process."""
    import ctypes

    k32 = _kernel32()

    def read(addr: int, size: int) -> bytes:
        buf = (ctypes.c_char * size)()
        got = ctypes.c_size_t(0)
        ok = k32.ReadProcessMemory(
            ctypes.c_void_p(handle),
            ctypes.c_void_p(addr),
            buf,
            ctypes.c_size_t(size),
            ctypes.byref(got),
        )
        if not ok:
            raise OSError(
                ctypes.get_last_error(),
                f"ReadProcessMemory failed at {addr:#x} for {size} bytes",
            )
        return bytes(buf[: got.value])

    return read


def _make_writer(handle: int) -> Writer:
    """Build a writer callable backed by WriteProcessMemory. LIVE-ONLY."""
    import ctypes

    k32 = _kernel32()

    def write(addr: int, data: bytes) -> None:
        buf = (ctypes.c_char * len(data)).from_buffer_copy(data)
        put = ctypes.c_size_t(0)
        ok = k32.WriteProcessMemory(
            ctypes.c_void_p(handle),
            ctypes.c_void_p(addr),
            buf,
            ctypes.c_size_t(len(data)),
            ctypes.byref(put),
        )
        if not ok:
            raise OSError(
                ctypes.get_last_error(),
                f"WriteProcessMemory failed at {addr:#x} for {len(data)} bytes",
            )

    return write


def open_process(process_name: str = constants.PROCESS_NAME) -> Process:
    """Open the named Clone Hero process, find where GameAssembly.dll loaded,
    and return a ready-to-use Process.

    LIVE-ONLY: this is the one function that needs a running game. It finds the
    process by name, opens it with read/write memory rights, locates the module
    base, and wires the ReadProcessMemory/WriteProcessMemory backend into a
    Process. It does NOT call verify_targets for you -- the caller should, as
    the first thing it does, to prove the address pipeline (milestone 1).

    Raises ProcessNotFoundError if the game or its module is not running, and
    OSError if opening the process fails (usually a rights problem -- run as
    the same user, or elevated)."""
    import ctypes

    pid = _find_pid_by_name(process_name)
    module_base = _find_module_base(pid, constants.MODULE_NAME)

    k32 = _kernel32()
    access = (
        _PROCESS_VM_READ
        | _PROCESS_VM_WRITE
        | _PROCESS_VM_OPERATION
        | _PROCESS_QUERY_INFORMATION
    )
    handle = k32.OpenProcess(access, False, pid)
    if not handle:
        raise OSError(
            ctypes.get_last_error(),
            f"OpenProcess failed for pid {pid} -- check you have rights to it",
        )

    return Process(
        module_base,
        _make_reader(handle),
        _make_writer(handle),
        pid=pid,
        handle=handle,
    )
