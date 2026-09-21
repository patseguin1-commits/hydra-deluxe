"""Read the on-screen "Accuracy: X ms" number as an independent cross-check.

Clone Hero prints a hit's timing error on screen, like "Accuracy: -8 ms". The
rest of the probe reads that same error straight out of game memory. This module
reads it off the screen instead, so the two can be compared. When the number we
read from memory matches the number shown on screen, we know our memory offsets
are pointing at the right field, and a whole run's worth of memory reads can be
trusted.

This is the lowest-priority piece. It exists only for that one trust check.

Two parts:

  * parse_accuracy_text -- pure text parsing. Given whatever text the OCR
    produced, pull out the signed millisecond number. No screen, no game; fully
    unit-tested below.
  * read_accuracy_ms -- the live path. Grab a screen rectangle, run Windows'
    built-in OCR on it, then hand the text to the parser. The screen grab and
    the OCR call can only run against a real display, so they are isolated
    behind small seams and cannot be tested without a screen.

OCR (optical character recognition) = turning a picture of text into a string.
We use the OCR engine built into Windows (Windows.Media.Ocr), reached through
the winrt package. That package is imported lazily inside the function, so just
importing this module never fails on a machine that lacks it.
"""

from __future__ import annotations

import ctypes
import re
from ctypes import wintypes
from typing import Optional, Tuple

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


def read_accuracy_ms(crop_region: Tuple[int, int, int, int]) -> Optional[float]:
    """Read "Accuracy: X ms" off the screen and return X as a float.

    crop_region is (left, top, right, bottom) in screen pixels: the rectangle
    the accuracy text sits in. Screenshots that rectangle, runs OCR on it, and
    parses the number out. Returns None when nothing parseable is found.

    Live-only. The two helpers it calls (screen capture and OCR) need a real
    display and Windows' OCR engine, so this whole function is exercised only
    against a running game, never in the unit tests.
    """
    width, height, pixels = _capture_crop(crop_region)
    if width <= 0 or height <= 0:
        return None
    text = _ocr_bgra(width, height, pixels)
    return parse_accuracy_text(text)


# --- Live-only seams --------------------------------------------------------
#
# Everything below touches the screen or the OS OCR engine. None of it can run
# without a real display, so it has no unit tests. It is kept behind these two
# small functions so read_accuracy_ms stays trivial and parse_accuracy_text
# stays pure.


def _capture_crop(crop_region: Tuple[int, int, int, int]) -> Tuple[int, int, bytes]:
    """Screenshot one screen rectangle. Returns (width, height, BGRA bytes).

    Uses the plain Windows drawing API (GDI) to copy pixels off the screen: get
    the screen's drawing handle, make a matching in-memory bitmap, block-copy
    the rectangle into it (BitBlt), then read the raw pixels back out. The
    pixels come back as 32-bit BGRA, top row first, which is exactly what the
    OCR step wants.

    LIVE-ONLY: needs a real screen. Not unit-tested.
    """
    left, top, right, bottom = crop_region
    width = int(right - left)
    height = int(bottom - top)
    if width <= 0 or height <= 0:
        return 0, 0, b""

    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32

    SRCCOPY = 0x00CC0020
    BI_RGB = 0
    DIB_RGB_COLORS = 0

    screen_dc = user32.GetDC(0)
    mem_dc = gdi32.CreateCompatibleDC(screen_dc)
    bitmap = gdi32.CreateCompatibleBitmap(screen_dc, width, height)
    old = gdi32.SelectObject(mem_dc, bitmap)
    try:
        gdi32.BitBlt(mem_dc, 0, 0, width, height, screen_dc, left, top, SRCCOPY)

        # Ask for a top-down 32-bit image. A negative height flips the rows so
        # the first row of bytes is the top of the image (OCR expects that).
        class BITMAPINFOHEADER(ctypes.Structure):
            _fields_ = [
                ("biSize", wintypes.DWORD),
                ("biWidth", wintypes.LONG),
                ("biHeight", wintypes.LONG),
                ("biPlanes", wintypes.WORD),
                ("biBitCount", wintypes.WORD),
                ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD),
                ("biXPelsPerMeter", wintypes.LONG),
                ("biYPelsPerMeter", wintypes.LONG),
                ("biClrUsed", wintypes.DWORD),
                ("biClrImportant", wintypes.DWORD),
            ]

        header = BITMAPINFOHEADER()
        header.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        header.biWidth = width
        header.biHeight = -height  # top-down
        header.biPlanes = 1
        header.biBitCount = 32
        header.biCompression = BI_RGB

        buffer = ctypes.create_string_buffer(width * height * 4)
        gdi32.GetDIBits(
            mem_dc, bitmap, 0, height, buffer,
            ctypes.byref(header), DIB_RGB_COLORS,
        )
        return width, height, buffer.raw
    finally:
        gdi32.SelectObject(mem_dc, old)
        gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(mem_dc)
        user32.ReleaseDC(0, screen_dc)


def _ocr_bgra(width: int, height: int, pixels: bytes) -> str:
    """Run Windows OCR on raw BGRA pixels and return the recognized text.

    Wraps the pixels in a WinRT SoftwareBitmap, hands it to the OCR engine built
    from the user's languages, and joins the recognized lines into one string.

    The winrt package is imported HERE, not at module top, so importing this
    module never fails on a machine without winrt. If winrt is missing the
    import raises and the caller sees the error.

    LIVE-ONLY: needs Windows' OCR engine. Not unit-tested.
    """
    import asyncio

    from winrt.windows.graphics.imaging import (
        BitmapAlphaMode,
        BitmapPixelFormat,
        SoftwareBitmap,
    )
    from winrt.windows.media.ocr import OcrEngine
    from winrt.windows.security.cryptography import CryptographicBuffer

    buffer = CryptographicBuffer.create_from_byte_array(pixels)
    bitmap = SoftwareBitmap.create_copy_from_buffer(
        buffer, BitmapPixelFormat.BGRA8, width, height, BitmapAlphaMode.PREMULTIPLIED
    )

    engine = OcrEngine.try_create_from_user_profile_languages()
    if engine is None:
        # No OCR language pack installed. Nothing to read.
        return ""

    async def _run() -> str:
        result = await engine.recognize_async(bitmap)
        return result.text or ""

    return asyncio.run(_run())
