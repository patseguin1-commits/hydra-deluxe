"""Unit tests for the process/address layer.

These test the parts that do NOT need a running game: the byte decoding, the
RVA-to-address math, and the build-drift check's pass/fail decision. Every test
drives the logic through a fake reader that returns bytes we chose by hand, so
no Clone Hero, no debugger, and no Windows API are involved.

What is deliberately NOT tested here (the live-only seams, see process.py):
  - open_process(), _find_pid_by_name(), _find_module_base(): these read the
    real OS process and module tables, so they only run against a live game.
  - _make_reader()/_make_writer(): they wrap ReadProcessMemory/
    WriteProcessMemory and need a real open process handle.
Those are exercised by hand against a running Clone Hero, not in this file.

Run from the repo root with:
    python -m pytest tools/ch_probe/tests/test_process.py -q
or, if pytest is not installed:
    python -m unittest tools.ch_probe.tests.test_process
"""

from __future__ import annotations

import struct
import unittest

from .. import constants, process


def _make_fake_process(memory: dict[int, bytes], module_base: int = 0x140000000):
    """Build a Process whose reads come from a plain dict, keyed by live
    address. The dict stands in for the game's memory. Writes are captured into
    a separate dict so a test can inspect them."""
    writes: dict[int, bytes] = {}

    def reader(addr: int, size: int) -> bytes:
        raw = memory.get(addr)
        if raw is None:
            raise KeyError(f"nothing mapped at {addr:#x} in the fake memory")
        return raw[:size]

    def writer(addr: int, data: bytes) -> None:
        writes[addr] = data

    proc = process.Process(module_base, reader, writer)
    return proc, writes


class DecodeHelpersTest(unittest.TestCase):
    """The pure byte-to-number decoders."""

    def test_decode_double_known_pattern(self):
        # 85.0 as a little-endian IEEE double is a fixed 8-byte pattern.
        raw = struct.pack("<d", 85.0)
        self.assertEqual(process.decode_double(raw), 85.0)

    def test_decode_double_endianness(self):
        # Feed the exact bytes for 37.5 and make sure we read little-endian,
        # not big-endian. The reversed bytes would decode to a wildly different
        # number, so a byte-order bug can't hide.
        raw = struct.pack("<d", 37.5)
        self.assertEqual(process.decode_double(raw), 37.5)
        self.assertNotEqual(process.decode_double(raw[::-1]), 37.5)

    def test_decode_double_wrong_length(self):
        with self.assertRaises(ValueError):
            process.decode_double(b"\x00\x00\x00\x00")

    def test_decode_u32_known_pattern(self):
        self.assertEqual(process.decode_u32(b"\x00\x10\x00\x00"), 0x1000)
        self.assertEqual(process.decode_u32(b"\xff\xff\xff\xff"), 0xFFFFFFFF)

    def test_decode_u32_wrong_length(self):
        with self.assertRaises(ValueError):
            process.decode_u32(b"\x00\x00")

    def test_decode_u64_known_pattern(self):
        self.assertEqual(process.decode_u64(struct.pack("<Q", 0x1122334455667788)),
                         0x1122334455667788)

    def test_decode_u64_wrong_length(self):
        with self.assertRaises(ValueError):
            process.decode_u64(b"\x00" * 4)


class ResolveMathTest(unittest.TestCase):
    """RVA -> live address is just module_base + rva."""

    def test_resolve_adds_base(self):
        proc, _ = _make_fake_process({}, module_base=0x140000000)
        self.assertEqual(proc.resolve(0x20DDDA0), 0x140000000 + 0x20DDDA0)

    def test_resolve_zero_rva(self):
        proc, _ = _make_fake_process({}, module_base=0x7FF000000000)
        self.assertEqual(proc.resolve(0), 0x7FF000000000)


class TypedReadTest(unittest.TestCase):
    """The typed reads pull the right number of bytes and decode them."""

    def test_read_double_through_fake_memory(self):
        base = 0x140000000
        addr = base + 0x30
        proc, _ = _make_fake_process({addr: struct.pack("<d", 85.0)}, base)
        self.assertEqual(proc.read_double(addr), 85.0)

    def test_read_u32_through_fake_memory(self):
        base = 0x140000000
        addr = base + 0x8C
        proc, _ = _make_fake_process({addr: struct.pack("<I", 512)}, base)
        self.assertEqual(proc.read_u32(addr), 512)

    def test_read_const_double_resolves_then_reads(self):
        # read_const_double takes an RVA, resolves it, then reads the double.
        base = 0x140000000
        rva = constants.RVA_CONST_NORMAL_BACK
        proc, _ = _make_fake_process({base + rva: struct.pack("<d", 85.0)}, base)
        self.assertEqual(proc.read_const_double(rva), 85.0)

    def test_read_rejects_short_read(self):
        # The fake returns fewer bytes than asked; read() must flag it rather
        # than hand back a truncated buffer.
        base = 0x140000000
        addr = base + 0x100
        proc, _ = _make_fake_process({addr: b"\x01\x02\x03"}, base)
        with self.assertRaises(OSError):
            proc.read(addr, 8)

    def test_write_goes_through_writer(self):
        proc, writes = _make_fake_process({}, 0x140000000)
        proc.write(0x140001000, b"\xde\xad")
        self.assertEqual(writes[0x140001000], b"\xde\xad")


class CheckNormalConstantsTest(unittest.TestCase):
    """The pure pass/fail decision behind verify_targets."""

    # The game stores the constants in seconds, so the check takes seconds.

    def test_exact_values_pass(self):
        # 0.085 / 0.0375 s -> no error.
        process.check_normal_constants(0.085, 0.0375)

    def test_within_tolerance_passes(self):
        # A read a hair under the tolerance is still accepted.
        tol = constants.CONST_MATCH_TOLERANCE_MS / 1000.0
        process.check_normal_constants(0.085 + tol * 0.9, 0.0375 - tol * 0.9)

    def test_back_off_by_one_fails(self):
        # 0.084 s is a full millisecond off, well past tolerance.
        with self.assertRaises(process.BuildMismatchError):
            process.check_normal_constants(0.084, 0.0375)

    def test_front_off_fails(self):
        with self.assertRaises(process.BuildMismatchError):
            process.check_normal_constants(0.085, 0.040)

    def test_just_past_tolerance_fails(self):
        tol = constants.CONST_MATCH_TOLERANCE_MS / 1000.0
        with self.assertRaises(process.BuildMismatchError):
            process.check_normal_constants(0.085 + tol * 2, 0.0375)

    def test_values_in_ms_are_refused(self):
        # The old unit: a build-drift guard that accepted ms would be wrong.
        with self.assertRaises(process.BuildMismatchError):
            process.check_normal_constants(85.0, 37.5)


class VerifyTargetsTest(unittest.TestCase):
    """verify_targets reads the two constants live, then applies the check."""

    def _proc_with_constants(self, back: float, front: float):
        base = 0x140000000
        memory = {
            base + constants.RVA_CONST_NORMAL_BACK: struct.pack("<d", back),
            base + constants.RVA_CONST_NORMAL_FRONT: struct.pack("<d", front),
        }
        proc, _ = _make_fake_process(memory, base)
        return proc

    def test_good_build_passes(self):
        # The proof-of-life case: 0.085 / 0.0375 s read back clean.
        self.assertIsNone(self._proc_with_constants(0.085, 0.0375).verify_targets())

    def test_drifted_back_constant_raises(self):
        with self.assertRaises(process.BuildMismatchError):
            self._proc_with_constants(0.084, 0.0375).verify_targets()

    def test_drifted_front_constant_raises(self):
        with self.assertRaises(process.BuildMismatchError):
            self._proc_with_constants(0.085, 0.025).verify_targets()


class ProtocolShapeTest(unittest.TestCase):
    """Process must satisfy the ProcessHandle Protocol so the other modules can
    rely on its method names."""

    def test_process_is_a_process_handle(self):
        from .. import interfaces

        proc, _ = _make_fake_process({}, 0x140000000)
        self.assertIsInstance(proc, interfaces.ProcessHandle)


if __name__ == "__main__":
    unittest.main()
