"""Diagnostic: does Clone Hero survive debugger attachment at all?

Attaches as a Win32 debugger, pumps events for a few seconds WITHOUT
setting any breakpoints, then detaches. If the game crashes during
this, it has anti-debug detection and we need a different approach
(pure ReadProcessMemory polling instead of breakpoints).

    python tools\\ch_probe\\experiments\\test_attach.py
"""

from __future__ import annotations

import ctypes
import os
import sys
import time

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants
from tools.ch_probe.process import open_process


def main() -> None:
    print(f"Looking for {constants.PROCESS_NAME}...")
    proc = open_process()
    print(f"  PID {proc.pid}")

    proc.verify_targets()
    print("  Addresses verified.")

    # --- Test 1: bare attach/detach, no breakpoints, no event pump -----------
    print("\nTest 1: attach debugger, wait 3s, detach (no breakpoints)...")
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)

    ok = k32.DebugActiveProcess(proc.pid)
    if not ok:
        err = ctypes.get_last_error()
        print(f"  DebugActiveProcess failed: error {err}")
        return
    print("  Attached. Game should still be running (check its window).")

    # Pump events for 3 seconds so the initial breakpoint gets handled.
    # Without pumping, the debuggee stays frozen.
    from tools.ch_probe.debugger import (
        DEBUG_EVENT, EXCEPTION_DEBUG_EVENT, EXCEPTION_BREAKPOINT,
        DBG_CONTINUE, DBG_EXCEPTION_NOT_HANDLED,
    )
    ev = DEBUG_EVENT()
    deadline = time.monotonic() + 3.0
    events = 0
    while time.monotonic() < deadline:
        got = k32.WaitForDebugEvent(ctypes.byref(ev), 200)
        if got:
            events += 1
            code = ev.dwDebugEventCode
            status = DBG_CONTINUE
            if code == EXCEPTION_DEBUG_EVENT:
                exc_code = ev.u.Exception.ExceptionRecord.ExceptionCode
                exc_addr = ev.u.Exception.ExceptionRecord.ExceptionAddress
                print(f"  Exception event: code={exc_code:#x} addr={exc_addr:#x}")
                if exc_code != EXCEPTION_BREAKPOINT:
                    status = DBG_EXCEPTION_NOT_HANDLED
            else:
                print(f"  Debug event code={code}")
            k32.ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, status)

    print(f"  Pumped {events} events in 3s.")
    print("  Is Clone Hero still running? (check its window)")

    # Can we still read memory?
    try:
        back = proc.read_const_double(constants.RVA_CONST_NORMAL_BACK)
        print(f"  Memory read OK: back window = {back*1000:.1f} ms")
    except Exception as e:
        print(f"  Memory read FAILED: {e}")

    print("  Detaching...")
    k32.DebugActiveProcessStop(proc.pid)
    print("  Detached. Check if Clone Hero is still alive.")

    proc.close()


if __name__ == "__main__":
    main()
