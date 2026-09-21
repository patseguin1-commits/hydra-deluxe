"""Write tiny Clone Hero .chart files made of isolated note pairs.

The active probe needs clean geometry to test. Real songs bury the spacing we
want inside a wall of other notes. So instead we generate our own chart: for
each spacing we care about, drop exactly two drum notes that far apart in time,
with a long silence around them so nothing else can interfere.

The output is the classic Moonscraper text .chart format -- the same one the
existing notes.chart files in this repo use. It has a [Song] header, a
[SyncTrack] that sets the tempo, and an [ExpertDrums] section holding the notes.

Everything here is pure text generation. No game and no debugger is involved,
so the whole module is unit-testable: generate a chart, parse it back, and
check the tick spacing came out right.
"""

from __future__ import annotations

from typing import Sequence


# The default set of spacings the spec asks for, in milliseconds. Dense near
# the 180-220 ms region where the hit-window parabola peaks (that is where the
# clamp decision happens), sparse elsewhere just to see the shape.
DEFAULT_SPACINGS_MS = [
    30, 50, 100, 150, 180, 185, 190, 195, 205, 211, 220, 240, 300,
]

# Drum note numbers in the .chart format. 0 is the kick. 1-4 are the four
# colored pads (red, yellow, blue, green). We default to the kick because it is
# a single lane with no cymbal-vs-tom ambiguity, which keeps the probe clean.
DRUM_LANE_KICK = 0

# How much silent room to leave around each pair, expressed as whole notes. One
# whole note at 120 BPM is two seconds, so four whole notes is a wide moat -- no
# pair's window can reach into another's.
_PAD_WHOLE_NOTES = 4

# Where the first pair starts, in whole notes from the top of the chart. A short
# lead-in so the engine has settled before the first note.
_LEAD_IN_WHOLE_NOTES = 2


def ms_to_ticks(ms: float, resolution: int, bpm: float) -> int:
    """Turn a duration in milliseconds into chart ticks, rounded to the nearest.

    A .chart measures time in ticks. `resolution` ticks make one quarter note,
    and at `bpm` beats per minute one quarter note lasts 60/bpm seconds. So the
    tick rate is resolution * bpm / 60 ticks per second, and a span of `ms`
    milliseconds is ms/1000 of a second times that rate.

    We round to the nearest whole tick because ticks are integers. This is the
    one bit of math the rest of the module leans on, so it lives alone where a
    test can pin it.
    """
    ticks_per_ms = resolution * bpm / 60000.0
    return int(round(ms * ticks_per_ms))


def _song_section(resolution: int) -> str:
    """The [Song] header. Only Resolution matters to us; the rest is filler the
    loader tolerates."""
    return (
        "[Song]\n"
        "{\n"
        '  Name = "CH hit-window probe"\n'
        '  Artist = "Hydra ch_probe"\n'
        '  Charter = "ch_probe"\n'
        "  Offset = 0\n"
        f"  Resolution = {resolution}\n"
        '  Genre = "Test"\n'
        '  MediaType = "cd"\n'
        "}\n"
    )


def _sync_track_section(bpm: float) -> str:
    """The [SyncTrack]. One time signature and one tempo, both at tick 0.

    Tempo in a .chart is stored as microbeats: beats-per-minute times 1000,
    written as an integer. So 120.0 BPM becomes the value 120000.
    """
    bpm_microbeats = int(round(bpm * 1000))
    return (
        "[SyncTrack]\n"
        "{\n"
        "  0 = TS 4\n"
        f"  0 = B {bpm_microbeats}\n"
        "}\n"
    )


def _expert_drums_section(note_ticks: Sequence[int], lane: int) -> str:
    """The [ExpertDrums] section. One line per note: `<tick> = N <lane> 0`.

    The trailing 0 is the sustain length; drum notes are instantaneous, so it is
    always zero. Lines are sorted by tick, which the loader expects.
    """
    lines = ["[ExpertDrums]", "{"]
    for tick in sorted(note_ticks):
        lines.append(f"  {tick} = N {lane} 0")
    lines.append("}")
    return "\n".join(lines) + "\n"


def build_probe_chart_text(
    spacings_ms: Sequence[float],
    *,
    resolution: int = 192,
    bpm: float = 120.0,
    lane: int = DRUM_LANE_KICK,
) -> str:
    """Build the full .chart text for the given spacings and return it.

    This is `generate_probe_chart` without the file write, split out so a test
    can inspect the text directly. For each spacing we emit two notes that many
    ticks apart, then jump a wide silent gap before the next pair, so no two
    pairs can overlap or interact.
    """
    pad_ticks = _PAD_WHOLE_NOTES * resolution * 4
    cursor = _LEAD_IN_WHOLE_NOTES * resolution * 4

    note_ticks: list[int] = []
    for ms in spacings_ms:
        gap = ms_to_ticks(ms, resolution, bpm)
        first = cursor
        second = cursor + gap
        note_ticks.append(first)
        note_ticks.append(second)
        # Next pair starts a full pad past this pair's second note.
        cursor = second + pad_ticks

    return (
        _song_section(resolution)
        + _sync_track_section(bpm)
        + _expert_drums_section(note_ticks, lane)
    )


def generate_probe_chart(
    spacings_ms: Sequence[float],
    path: str,
    *,
    resolution: int = 192,
    bpm: float = 120.0,
    lane: int = 0,
) -> None:
    """Write a probe .chart to `path`. See interfaces.py for the contract.

    One isolated note pair per spacing in `spacings_ms`, each pair that many
    milliseconds apart, with wide silence around each so nothing overlaps. The
    result round-trips through the game's chart loader.

    Only the text generation lives here; it is exercised by tests. The write is
    the one line a test cannot verify without touching the filesystem, so keep
    it thin -- all the logic sits in build_probe_chart_text above.
    """
    text = build_probe_chart_text(
        spacings_ms, resolution=resolution, bpm=bpm, lane=lane
    )
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


if __name__ == "__main__":
    # Default caller: write the spec's suggested spacings to a file next to this
    # script so someone can eyeball the output.
    import os

    out = os.path.join(os.path.dirname(__file__), "probe.chart")
    generate_probe_chart(DEFAULT_SPACINGS_MS, out)
    print(f"wrote {out} with {len(DEFAULT_SPACINGS_MS)} note pairs")
