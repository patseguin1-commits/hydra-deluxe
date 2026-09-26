"""Read the on-screen "Accuracy: X ms" number as an independent cross-check.

Clone Hero prints a hit's timing error on screen, like "Accuracy: -8 ms". The
rest of the probe reads that same error straight out of game memory. This module
reads it off the screen instead, so the two can be compared. When the number we
read from memory matches the number shown on screen, we know our memory offsets
are pointing at the right field, and a whole run's worth of memory reads can be
trusted.

This is the lowest-priority piece. It exists only for that one trust check.

Only the pure parser remains: parse_accuracy_text pulls the signed millisecond
number out of whatever text an OCR produced. The screen-capture half (the
function that ran Windows' built-in OCR on a screen crop) had no caller and
was deleted in 2026-09. Git history has it if the spec's OCR
cross-check (build-order step 4) is ever run.
"""

from __future__ import annotations

import re
from typing import Optional

# A signed number, optionally with decimals, that is followed by the "ms" unit.
# The unit is required so we do not latch onto a stray number that happens to be
# on screen. Examples matched: "12.3 ms", "-8 ms", "+45.0 ms".
_ACCURACY_RE = re.compile(r"([-+]?\d+(?:\.\d+)?)\s*ms", re.IGNORECASE)


def parse_accuracy_text(text: str) -> Optional[float]:
    """Pull the signed millisecond value out of an OCR string.

    Returns the number as a float, dropping any leading plus sign. Returns None
    when there is no "<number> ms" anywhere in the text (junk, or empty).

    This is the whole tested surface of the module. Keep it pure: text in,
    number or None out.
    """
    if not text:
        return None
    match = _ACCURACY_RE.search(text)
    if match is None:
        return None
    try:
        return float(match.group(1))
    except ValueError:
        # The regex already guarantees a valid number shape, so this is only a
        # belt-and-suspenders guard; treat anything unparseable as "not found".
        return None
