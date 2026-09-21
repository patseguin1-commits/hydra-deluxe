"""Pure number-crunching for the two hit-window experiments.

This file has no ctypes, no debugger, no game. It takes rows of numbers the
runners collected and turns them into answers. Because it touches nothing live,
its whole logic is unit-testable with fabricated data, and its unit test always
runs.

Three jobs live here.

1. Boundary detector. The active probe fires inputs near one edge of the hit
   window and records, per input, "the engine measured this delta, and it
   counted as HIT / MISS". Small deltas hit, big deltas miss. The edge is the
   value where hit turns into miss. `find_window_edge` finds it.

2. Clamp verdict. The passive probe reads two numbers per note: the raw value
   the window formula returns, and the value the engine actually stores and
   uses. If the stored value climbs right along with the raw one, there is no
   clamp. If the stored value flat-lines near 85 ms while the raw one keeps
   rising, the clamp is real. `clamp_verdict` decides which.

3. Parabola predictor. `predicted_window_normal` reproduces the game's
   normal-mode formula so a plot can draw predicted-versus-measured on the same
   axes.

Everything here is stdlib only, so the test never needs a game or a debugger.
"""

from __future__ import annotations

import statistics
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple


# A single active-probe sample: the delta the engine measured (ms) and whether
# the note counted as a hit.
DeltaHitRow = Tuple[float, bool]

# A single passive-probe sample: the note spacing (ms), the raw formula output,
# and the window the engine stored. Raw and stored must be in the SAME
# convention -- both per-side, or both total -- because this file just compares
# the two numbers it is handed. Making them comparable is the runner's job.
SpacingRawStoredRow = Tuple[float, float, float]


# --- 1. Boundary detector ----------------------------------------------------

@dataclass
class WindowEdge:
    """Where the hit cluster ends and the miss cluster begins.

    `edge_ms` is the detected window edge as a positive magnitude in
    milliseconds. `errors` is how many samples fall on the wrong side of that
    edge -- zero means the two clusters separated cleanly. The counts are there
    so a caller can judge how trustworthy the edge is.
    """

    edge_ms: Optional[float]
    errors: int
    n_hits: int
    n_misses: int


def find_window_edge(rows: Sequence[DeltaHitRow]) -> WindowEdge:
    """Find the delta where hits turn into misses for one edge sweep.

    Pass the samples for a single edge (one spacing, one side of the window).
    Deltas may be signed; this works on their magnitude, because the window is a
    distance from perfect timing. The rule it assumes is physical: a small
    magnitude hits, a large one misses.

    It tries every threshold that sits between two neighbouring samples, counts
    how many samples that threshold gets wrong (a hit above it, or a miss at or
    below it), and keeps the threshold with the fewest wrong. When several
    thresholds tie, it takes their median, which lands in the middle of the
    ambiguous gap. The returned edge is that threshold in milliseconds.

    Returns a WindowEdge. `edge_ms` is None only when `rows` is empty.
    """
    points: List[Tuple[float, bool]] = [
        (abs(float(delta)), bool(hit)) for delta, hit in rows
    ]
    n_hits = sum(1 for _, hit in points if hit)
    n_misses = len(points) - n_hits

    if not points:
        return WindowEdge(edge_ms=None, errors=0, n_hits=0, n_misses=0)

    points.sort(key=lambda p: p[0])
    mags = [m for m, _ in points]

    # Candidate edges: just below the smallest sample, the midpoint between each
    # neighbouring pair, and just above the largest sample. One of these is the
    # best clean cut.
    candidates: List[float] = [mags[0] - 1.0]
    for i in range(len(mags) - 1):
        candidates.append((mags[i] + mags[i + 1]) / 2.0)
    candidates.append(mags[-1] + 1.0)

    best_errors: Optional[int] = None
    best_thresholds: List[float] = []
    for threshold in candidates:
        errors = 0
        for mag, hit in points:
            if hit and mag > threshold:
                errors += 1
            elif (not hit) and mag <= threshold:
                errors += 1
        if best_errors is None or errors < best_errors:
            best_errors = errors
            best_thresholds = [threshold]
        elif errors == best_errors:
            best_thresholds.append(threshold)

    edge = statistics.median(best_thresholds)
    return WindowEdge(
        edge_ms=edge,
        errors=best_errors or 0,
        n_hits=n_hits,
        n_misses=n_misses,
    )


# --- 2. Clamp verdict --------------------------------------------------------

# The three answers the passive probe can reach.
CLAMP_PRESENT = "clamp"          # stored flat-lines at the cap; a clamp is real
CLAMP_ABSENT = "no_clamp"        # stored tracks the raw formula; no clamp
CLAMP_INCONCLUSIVE = "inconclusive"  # the data never pushed past the cap, or is mixed


@dataclass
class ClampResult:
    """The passive probe's answer, with the evidence behind it.

    `verdict` is one of CLAMP_PRESENT, CLAMP_ABSENT, CLAMP_INCONCLUSIVE.
    `tracked_fraction` and `flat_fraction` are, among the notes whose raw window
    rose above the cap, how many had the stored value follow the raw one versus
    sit flat at the cap. `n_above` is how many notes actually pushed past the
    cap -- if that is zero, no clamp could ever have shown itself and the
    verdict is inconclusive.
    """

    verdict: str
    tracked_fraction: float
    flat_fraction: float
    n_above: int
    cap_ms: float


def clamp_verdict(
    rows: Sequence[SpacingRawStoredRow],
    *,
    cap_ms: float = 85.0,
    tolerance_ms: float = 1.0,
    decisive_fraction: float = 0.8,
) -> ClampResult:
    """Decide whether the engine clamps the window at the cap.

    Look only at notes whose raw formula output rose above the cap -- those are
    the only notes where a clamp would change anything. For each, ask: did the
    stored window follow the raw value up (no clamp), or stay pinned near the
    cap (clamp)? If a clear majority followed the raw value, the verdict is "no
    clamp". If a clear majority stayed flat at the cap, the verdict is "clamp".
    If no note ever crossed the cap, or the notes split, the verdict is
    "inconclusive".

    `tolerance_ms` is how close counts as "equal". `decisive_fraction` is the
    share of above-cap notes that must agree before the verdict is called.
    """
    above = [
        (raw, stored)
        for _spacing, raw, stored in rows
        if raw > cap_ms + tolerance_ms
    ]
    n_above = len(above)
    if n_above == 0:
        return ClampResult(
            verdict=CLAMP_INCONCLUSIVE,
            tracked_fraction=0.0,
            flat_fraction=0.0,
            n_above=0,
            cap_ms=cap_ms,
        )

    tracked = sum(1 for raw, stored in above if abs(stored - raw) <= tolerance_ms)
    flat = sum(1 for raw, stored in above if abs(stored - cap_ms) <= tolerance_ms)
    tracked_fraction = tracked / n_above
    flat_fraction = flat / n_above

    if tracked_fraction >= decisive_fraction:
        verdict = CLAMP_ABSENT
    elif flat_fraction >= decisive_fraction:
        verdict = CLAMP_PRESENT
    else:
        verdict = CLAMP_INCONCLUSIVE

    return ClampResult(
        verdict=verdict,
        tracked_fraction=tracked_fraction,
        flat_fraction=flat_fraction,
        n_above=n_above,
        cap_ms=cap_ms,
    )


# --- 3. Parabola predictor ---------------------------------------------------

def predicted_window_normal(
    spacing: float,
    *,
    c1: float,
    c2: float,
    c3: float,
    c4: float,
    divisor: float,
    exponent: float = 2.0,
) -> float:
    """Reproduce the game's normal-mode window formula for one spacing.

    This mirrors the decompiled code exactly. The incoming spacing is first
    pre-scaled by the divisor (the code does `t = t * C5`), then the window is

        ((t*C1 - t**e * C2) * C3 - C4) / divisor

    which is the spec's `(linear - quadratic) * scale - offset, over the
    divisor` shape. Pass the constants exactly as the engine model decoded them
    from .rdata. `spacing` must be in the same unit the game feeds the formula;
    the pre-scale by the divisor is what turns it into the internal unit, so
    keep the caller's unit and the divisor consistent.
    """
    t = spacing * divisor
    return ((t * c1 - (t ** exponent) * c2) * c3 - c4) / divisor


def predicted_window_precision(
    spacing: float,
    *,
    c1: float,
    c2: float,
    c3: float,
    c0: float,
    divisor: float,
    exponent: float = 2.0,
) -> float:
    """Reproduce the game's precision-mode window formula.

    A different shape from normal mode: an offset minus a scaled term, still
    over the divisor:

        (C0 - (t*C1 - t**e * C2) * C3) / divisor

    with the same `t = spacing * divisor` pre-scale. Provided for the same
    predicted-versus-measured plot when the test runs in precision mode.
    """
    t = spacing * divisor
    return (c0 - (t * c1 - (t ** exponent) * c2) * c3) / divisor


# --- Small aggregators the runners lean on -----------------------------------

def group_by_spacing(
    rows: Sequence[Tuple[float, float, bool]],
) -> Dict[float, List[DeltaHitRow]]:
    """Bucket active-probe samples by their spacing.

    Each input row is (spacing, measured_delta, hit). The output maps each
    spacing to its list of (measured_delta, hit) samples, ready to hand to
    `find_window_edge` one spacing at a time.
    """
    buckets: Dict[float, List[DeltaHitRow]] = {}
    for spacing, delta, hit in rows:
        buckets.setdefault(float(spacing), []).append((float(delta), bool(hit)))
    return buckets


@dataclass
class SpacingEdge:
    """One spacing's result: the measured edge next to the predicted one."""

    spacing_ms: float
    measured_edge_ms: Optional[float]
    predicted_edge_ms: Optional[float]
    errors: int


def summarize_active(
    rows: Sequence[Tuple[float, float, bool]],
    *,
    formula_constants: Optional[Dict[str, float]] = None,
) -> List[SpacingEdge]:
    """Turn a whole active-probe run into one row per spacing.

    For each spacing, detect the measured edge, and -- when the normal-mode
    formula constants are supplied -- also compute the predicted edge. The
    constants dict must carry the keys `c1, c2, c3, c4, divisor` and optionally
    `exponent`; anything else is ignored. Rows come back sorted by spacing so a
    plot can walk them left to right.
    """
    out: List[SpacingEdge] = []
    for spacing, samples in sorted(group_by_spacing(rows).items()):
        edge = find_window_edge(samples)
        predicted: Optional[float] = None
        if formula_constants is not None:
            predicted = predicted_window_normal(
                spacing,
                c1=formula_constants["c1"],
                c2=formula_constants["c2"],
                c3=formula_constants["c3"],
                c4=formula_constants["c4"],
                divisor=formula_constants["divisor"],
                exponent=formula_constants.get("exponent", 2.0),
            )
        out.append(
            SpacingEdge(
                spacing_ms=spacing,
                measured_edge_ms=edge.edge_ms,
                predicted_edge_ms=predicted,
                errors=edge.errors,
            )
        )
    return out
