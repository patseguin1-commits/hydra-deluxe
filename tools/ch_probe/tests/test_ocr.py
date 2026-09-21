"""Tests for the OCR text parser.

Only the pure parser is testable here. The screen grab and the OCR call in
ocr.py need a real display and Windows' OCR engine, so they cannot run under the
unit tests -- they are left uncovered on purpose and marked live-only in the
source.

Run from the repo root:  python -m pytest tools/ch_probe/tests/test_ocr.py -q
Falls back to:           python -m unittest tools.ch_probe.tests.test_ocr
"""

from __future__ import annotations

import os
import sys
import unittest

# Make the repo root importable so "from tools.ch_probe ..." works no matter
# where the test runner is launched from.
_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe.ocr import parse_accuracy_text


class ParseAccuracyTextTests(unittest.TestCase):
    def test_plain_decimal(self):
        self.assertEqual(parse_accuracy_text("Accuracy: 12.3 ms"), 12.3)

    def test_negative_integer(self):
        self.assertEqual(parse_accuracy_text("Accuracy: -8 ms"), -8.0)

    def test_leading_plus_is_dropped(self):
        self.assertEqual(parse_accuracy_text("Accuracy: +45.0 ms"), 45.0)

    def test_junk_is_none(self):
        self.assertIsNone(parse_accuracy_text("no number here"))

    def test_empty_is_none(self):
        self.assertIsNone(parse_accuracy_text(""))

    def test_number_without_unit_is_none(self):
        # The "ms" unit is required, so a bare number is not enough.
        self.assertIsNone(parse_accuracy_text("Accuracy: 12.3"))

    def test_zero(self):
        self.assertEqual(parse_accuracy_text("Accuracy: 0 ms"), 0.0)

    def test_unit_spacing_and_case(self):
        # OCR may drop the space before "ms" or change its case.
        self.assertEqual(parse_accuracy_text("Accuracy: 7ms"), 7.0)
        self.assertEqual(parse_accuracy_text("Accuracy: -3.5 MS"), -3.5)

    def test_extra_surrounding_text(self):
        # Real OCR output often carries stray characters around the value.
        self.assertEqual(parse_accuracy_text("| Accuracy:  -21.0 ms |"), -21.0)

    def test_none_input_is_none(self):
        # A None slips through the empty check; make sure it does not crash.
        self.assertIsNone(parse_accuracy_text(None))  # type: ignore[arg-type]


if __name__ == "__main__":
    unittest.main()
