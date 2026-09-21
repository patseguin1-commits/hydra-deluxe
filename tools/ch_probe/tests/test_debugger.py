"""Unit tests for the pure logic in debugger.py.

What we can test with no game running: the breakpoint byte bookkeeping (does
arming write 0xCC and remember the real byte, does disarming put it back), the
XMM0-to-double decode, the rip-minus-one fix-up, the read masking that hides
0xCC, and the ThreadContext register snapshot.

What we cannot test here: the Win32 event pump (attach, WaitForDebugEvent,
GetThreadContext). Those need a live debuggee and are marked LIVE-ONLY in the
module. We still confirm the Debugger class exposes the right method names so
the interface contract is met.
"""

from __future__ import annotations

import struct
import unittest

from tools.ch_probe import debugger
from tools.ch_probe.debugger import (
    INT3,
    BreakpointTable,
    Debugger,
    ThreadContext,
    adjust_rip_after_int3,
    decode_xmm0_double,
    mask_breakpoints,
)


class FakeMemory:
    """A stand-in for process memory: a dict of address -> byte.

    Gives the BreakpointTable the same read/write shape the live debugger does,
    so the whole save/restore/re-arm state machine runs with no OS calls.
    """

    def __init__(self, initial: dict[int, int] | None = None) -> None:
        self.cells: dict[int, int] = dict(initial or {})

    def read(self, addr: int, size: int) -> bytes:
        return bytes(self.cells.get(addr + i, 0) for i in range(size))

    def write(self, addr: int, data: bytes) -> None:
        for i, b in enumerate(data):
            self.cells[addr + i] = b


class BreakpointTableTests(unittest.TestCase):
    def test_arm_writes_int3_and_saves_original(self):
        mem = FakeMemory({0x1000: 0x55})  # 0x55 = a real "push rbp" byte
        table = BreakpointTable(mem.read, mem.write)
        table.add(0x1000)
        table.arm(0x1000)

        self.assertEqual(mem.cells[0x1000], INT3)      # 0xCC is now installed
        self.assertEqual(table.original(0x1000), 0x55)  # the real byte was saved
        self.assertTrue(table.is_armed(0x1000))

    def test_disarm_restores_original_byte(self):
        mem = FakeMemory({0x2000: 0x90})  # 0x90 = nop
        table = BreakpointTable(mem.read, mem.write)
        table.add(0x2000)
        table.arm(0x2000)
        table.disarm(0x2000)

        self.assertEqual(mem.cells[0x2000], 0x90)  # back to the real byte
        self.assertFalse(table.is_armed(0x2000))

    def test_rearm_after_disarm_saves_no_stale_byte(self):
        # The re-arm must reinstall 0xCC without re-reading (which would now read
        # a stale 0xCC if it read at the wrong moment). Original stays the truth.
        mem = FakeMemory({0x3000: 0xAB})
        table = BreakpointTable(mem.read, mem.write)
        table.add(0x3000)
        table.arm(0x3000)
        table.disarm(0x3000)
        table.arm(0x3000)   # re-arm, as the single-step handler does

        self.assertEqual(mem.cells[0x3000], INT3)
        self.assertEqual(table.original(0x3000), 0xAB)

    def test_arm_is_idempotent_and_preserves_original(self):
        mem = FakeMemory({0x4000: 0x48})
        table = BreakpointTable(mem.read, mem.write)
        table.add(0x4000)
        table.arm(0x4000)
        table.arm(0x4000)  # second arm must not overwrite the saved byte with 0xCC

        self.assertEqual(table.original(0x4000), 0x48)
        self.assertEqual(mem.cells[0x4000], INT3)

    def test_remove_restores_and_forgets(self):
        mem = FakeMemory({0x5000: 0x33})
        table = BreakpointTable(mem.read, mem.write)
        table.add(0x5000)
        table.arm(0x5000)
        table.remove(0x5000)

        self.assertEqual(mem.cells[0x5000], 0x33)  # restored
        self.assertFalse(table.has(0x5000))        # forgotten

    def test_armed_originals_lists_only_armed(self):
        mem = FakeMemory({0x6000: 0x11, 0x6100: 0x22})
        table = BreakpointTable(mem.read, mem.write)
        table.add(0x6000)
        table.add(0x6100)
        table.arm(0x6000)  # arm one, leave the other unarmed

        self.assertEqual(table.armed_originals(), {0x6000: 0x11})

    def test_disarm_of_unarmed_is_a_safe_no_op(self):
        # Disarming something that was never armed must not touch memory or throw
        # -- the pump can call disarm defensively.
        mem = FakeMemory({0x7000: 0x77})
        table = BreakpointTable(mem.read, mem.write)
        table.add(0x7000)
        table.disarm(0x7000)  # never armed
        self.assertEqual(mem.cells[0x7000], 0x77)
        self.assertFalse(table.is_armed(0x7000))


class MaskBreakpointsTests(unittest.TestCase):
    def test_masks_installed_cc_back_to_original(self):
        # A read of four bytes where the second byte holds our 0xCC.
        data = bytes([0x48, INT3, 0x89, 0xE5])
        originals = {0x1001: 0x8B}  # the real byte at the patched address
        out = mask_breakpoints(0x1000, data, originals)

        self.assertEqual(out, bytes([0x48, 0x8B, 0x89, 0xE5]))

    def test_ignores_breakpoints_outside_the_range(self):
        data = bytes([0x01, 0x02, 0x03])
        originals = {0x9999: 0xFF}  # far outside the read window
        out = mask_breakpoints(0x1000, data, originals)

        self.assertEqual(out, data)

    def test_no_breakpoints_returns_input(self):
        data = bytes([0xDE, 0xAD])
        self.assertEqual(mask_breakpoints(0x0, data, {}), data)


class Xmm0DecodeTests(unittest.TestCase):
    def test_decodes_known_double_from_low_eight_bytes(self):
        # Pack 89.5 (the "is there a clamp?" number) into the low 8 bytes and
        # fill the high 8 with junk that must be ignored.
        low = struct.pack("<d", 89.5)
        high = b"\xAA" * 8
        self.assertEqual(decode_xmm0_double(low + high), 89.5)

    def test_decodes_the_normal_back_window(self):
        buf = struct.pack("<d", 85.0) + b"\x00" * 8
        self.assertEqual(decode_xmm0_double(buf), 85.0)

    def test_short_buffer_raises(self):
        with self.assertRaises(ValueError):
            decode_xmm0_double(b"\x00\x00\x00")


class AdjustRipTests(unittest.TestCase):
    def test_rip_minus_one_points_at_the_breakpoint(self):
        # The CPU reports rip one past the 0xCC it just ran.
        self.assertEqual(adjust_rip_after_int3(0x1_0000_0041), 0x1_0000_0040)


class ThreadContextTests(unittest.TestCase):
    def test_registers_are_plain_int_attributes(self):
        ctx = ThreadContext({"rip": 0x140001000, "rcx": 0xABCD})
        self.assertEqual(ctx.rip, 0x140001000)
        self.assertEqual(ctx.rcx, 0xABCD)
        self.assertEqual(ctx.rax, 0)  # unspecified registers default to zero

    def test_xmm0_double_reads_the_formula_return(self):
        xmm0 = struct.pack("<d", 37.5) + b"\x00" * 8
        ctx = ThreadContext({"rip": 0}, xmm0)
        self.assertEqual(ctx.xmm0_double(), 37.5)


class DebuggerSurfaceTests(unittest.TestCase):
    """The Win32 pump is LIVE-ONLY, but the class must still expose the method
    names the interface contract names, so construction and attribute presence
    are worth a cheap check."""

    def test_debugger_constructs_without_attaching(self):
        dbg = Debugger()
        for name in ("attach", "set_breakpoint", "clear_breakpoint",
                     "set_hw_data_breakpoint", "read", "write", "run", "stop"):
            self.assertTrue(callable(getattr(dbg, name)), name)

    def test_hw_data_breakpoint_flags_itself_live_only(self):
        # It must not silently pretend to work before attach; it is unverified.
        dbg = Debugger()
        with self.assertRaises(RuntimeError):
            dbg.set_hw_data_breakpoint(0x140002000)

    def test_module_exports_the_expected_names(self):
        for name in ("Debugger", "ThreadContext", "BreakpointTable",
                     "decode_xmm0_double", "mask_breakpoints",
                     "adjust_rip_after_int3", "INT3"):
            self.assertTrue(hasattr(debugger, name), name)


if __name__ == "__main__":
    unittest.main()
