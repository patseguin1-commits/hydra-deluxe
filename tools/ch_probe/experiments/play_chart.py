"""Play a chart by reading the song clock and sending timed inputs.

Reads the song clock (+0x100, double seconds) continuously, looks up
which notes are due from the parsed chart, and sends the correct
lane inputs at the right time. Fully reactive to the game's own
clock — no sync drift.

Usage:
    python tools\\ch_probe\\experiments\\play_chart.py [chart_folder]

Defaults to the Synovial chart. Run this, then start the song.
"""

from __future__ import annotations

import math
import os
import re
import sys
import time
from dataclasses import dataclass, field
from typing import List

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants
from tools.ch_probe.process import open_process
from tools.ch_probe.input_driver import InputDriver
from tools.ch_probe.experiments.find_engine import scan_for_engine

OFF_SONG_CLOCK = 0x100  # double, seconds
OFF_SCORE = 0x94        # u32, game score (monotonically increasing)

# .chart note -> input lane. Note 66 = cymbal flag, skip it.
NOTE_TO_LANE = {0: 4, 1: 1, 2: 2, 3: 3, 4: 0, 5: 0}
LANE_NAMES = {0: "Grn", 1: "Red", 2: "Yel", 3: "Blu", 4: "Kick",
              5: "YCym", 6: "BCym", 7: "GCym"}

SYNOVIAL_DIR = (
    r"C:\Clone Hero\songs\synchotic\Sync Charts\BirdmanExe Drive"
    r"\Xane60\Synovial\Synovial - Slipping"
)


@dataclass
class TempoEvent:
    tick: int
    bpm: float


@dataclass
class NoteEvent:
    tick: int
    time_s: float = 0.0
    lanes: list = field(default_factory=list)


def parse_chart(chart_path: str):
    with open(chart_path, "r", encoding="utf-8-sig") as f:
        text = f.read()

    m = re.search(r"Resolution\s*=\s*(\d+)", text)
    resolution = int(m.group(1)) if m else 480

    tempos = []
    sync_match = re.search(r"\[SyncTrack\]\s*\{([^}]*)\}", text, re.DOTALL)
    if sync_match:
        for line in sync_match.group(1).strip().split("\n"):
            bm = re.match(r"\s*(\d+)\s*=\s*B\s+(\d+)", line)
            if bm:
                tempos.append(TempoEvent(int(bm.group(1)), int(bm.group(2)) / 1000.0))
    if not tempos:
        tempos = [TempoEvent(0, 120.0)]

    raw_notes: dict[int, list[int]] = {}
    drums_match = re.search(r"\[ExpertDrums\]\s*\{([^}]*)\}", text, re.DOTALL)
    if drums_match:
        for line in drums_match.group(1).strip().split("\n"):
            nm = re.match(r"\s*(\d+)\s*=\s*N\s+(\d+)\s+\d+", line)
            if nm:
                tick = int(nm.group(1))
                note_num = int(nm.group(2))
                if note_num in NOTE_TO_LANE:
                    raw_notes.setdefault(tick, []).append(NOTE_TO_LANE[note_num])

    notes = []
    for tick in sorted(raw_notes.keys()):
        lanes = sorted(set(raw_notes[tick]))
        notes.append(NoteEvent(tick=tick, lanes=lanes))

    tempos.sort(key=lambda t: t.tick)
    for note in notes:
        note.time_s = ticks_to_seconds(note.tick, tempos, resolution)

    return resolution, tempos, notes


# Expert drum gems live on MIDI notes 96-100. Notes 110/111/112 are NOT
# separate gems -- they are TOM MARKERS. In the Rock Band Pro Drums MIDI
# convention, a yellow/blue/green gem defaults to a CYMBAL; the marker at the
# same tick converts it to a TOM. So note 98 alone is a yellow cymbal, and
# note 98 + note 110 together is one yellow tom (still one gem, one key).
#
# So we collect the raw MIDI note numbers per tick, then resolve each gem to
# exactly one lane: if its tom marker is present, use the tom lane, otherwise
# the cymbal lane. Pressing both the tom key and the cymbal key for one gem is
# an overhit and misses the note.
DRUM_NOTES = frozenset({95, 96, 97, 98, 99, 100, 110, 111, 112})

# gem note -> (tom lane, its tom-marker note, cymbal lane)
_CYMBAL_UPGRADE = {
    98: (2, 110, 5),   # Yellow: tom J / cymbal U
    99: (3, 111, 6),   # Blue:   tom K / cymbal Y
    100: (0, 112, 7),  # Green:  tom A / cymbal T
}


def midi_notes_to_lanes(midi_notes) -> list:
    """Turn the set of MIDI note numbers at one tick into input lanes.

    One gem -> one lane. Yellow/blue/green default to cymbals; a tom marker
    (110/111/112) at the same tick converts that gem to a tom instead of
    adding a second lane.
    """
    s = set(midi_notes)
    lanes = []
    if 95 in s or 96 in s:
        lanes.append(4)   # Kick (L). 95 = 2x-kick pedal, 96 = normal kick;
                          # both are single kick hits and never share a tick.
    if 97 in s:
        lanes.append(1)   # Red (S)
    for gem_note, (tom_lane, marker, cym_lane) in _CYMBAL_UPGRADE.items():
        if gem_note in s:
            lanes.append(tom_lane if marker in s else cym_lane)
    return sorted(set(lanes))


def _read_vlq(data, pos):
    val = 0
    while True:
        b = data[pos]; pos += 1
        val = (val << 7) | (b & 0x7F)
        if not (b & 0x80):
            return val, pos


def parse_midi(mid_path: str):
    import struct as st
    with open(mid_path, "rb") as f:
        data = f.read()

    ppqn = st.unpack(">H", data[12:14])[0]

    # Find all track offsets
    tracks = []
    pos = 0
    while True:
        pos = data.find(b"MTrk", pos)
        if pos == -1:
            break
        trk_len = st.unpack(">I", data[pos+4:pos+8])[0]
        chunk = data[pos+8:pos+8+min(200, trk_len)]
        name_pos = chunk.find(b"\xff\x03")
        name = ""
        if name_pos != -1:
            nl = chunk[name_pos+2]
            name = chunk[name_pos+3:name_pos+3+nl].decode("ascii", errors="replace")
        tracks.append((pos, trk_len, name))
        pos += 8 + trk_len

    # Parse tempo from the first track
    tempos = []
    if tracks:
        t_start = tracks[0][0] + 8
        t_end = t_start + tracks[0][1]
        p = t_start; tick = 0
        while p < t_end:
            dt, p = _read_vlq(data, p)
            tick += dt
            if data[p] == 0xFF:
                mt = data[p+1]
                ln, p2 = _read_vlq(data, p+2)
                if mt == 0x51 and ln == 3:
                    us = (data[p2]<<16)|(data[p2+1]<<8)|data[p2+2]
                    tempos.append(TempoEvent(tick, 60_000_000/us))
                p = p2 + ln
            elif data[p] & 0xF0 in (0x80,0x90,0xA0,0xB0,0xE0):
                p += 3
            elif data[p] & 0xF0 in (0xC0,0xD0):
                p += 2
            else:
                p += 1
    if not tempos:
        tempos = [TempoEvent(0, 120.0)]

    # Find PART DRUMS track
    drums_track = None
    for tpos, tlen, tname in tracks:
        if "DRUMS" in tname.upper():
            drums_track = (tpos, tlen)
            break

    # Collect raw MIDI note numbers per tick; resolve to lanes afterwards so
    # cymbal markers can upgrade their co-located tom instead of double-firing.
    raw_midi: dict[int, list[int]] = {}
    if drums_track:
        p = drums_track[0] + 8
        end = p + drums_track[1]
        tick = 0
        rs = 0
        while p < end:
            dt, p = _read_vlq(data, p)
            tick += dt
            b = data[p]
            if b == 0xFF:
                p += 1; mt = data[p]; p += 1
                ln, p = _read_vlq(data, p); p += ln
            elif b & 0x80:
                rs = b; p += 1
                if b & 0xF0 == 0x90:
                    note = data[p]; vel = data[p+1]; p += 2
                    if vel > 0 and note in DRUM_NOTES:
                        raw_midi.setdefault(tick, []).append(note)
                elif b & 0xF0 == 0x80:
                    p += 2
                elif b & 0xF0 in (0xA0,0xB0,0xE0):
                    p += 2
                elif b & 0xF0 in (0xC0,0xD0):
                    p += 1
            else:
                if rs & 0xF0 == 0x90:
                    note = b; vel = data[p+1]; p += 2
                    if vel > 0 and note in DRUM_NOTES:
                        raw_midi.setdefault(tick, []).append(note)
                elif rs & 0xF0 == 0x80:
                    p += 2
                elif rs & 0xF0 in (0xA0,0xB0,0xE0):
                    p += 2
                elif rs & 0xF0 in (0xC0,0xD0):
                    p += 1

    notes = []
    for tick in sorted(raw_midi.keys()):
        lanes = midi_notes_to_lanes(raw_midi[tick])
        if lanes:
            notes.append(NoteEvent(tick=tick, lanes=lanes))

    tempos.sort(key=lambda t: t.tick)
    for note in notes:
        note.time_s = ticks_to_seconds(note.tick, tempos, ppqn)

    return ppqn, tempos, notes


def ticks_to_seconds(tick: int, tempos: List[TempoEvent], resolution: int) -> float:
    time_s = 0.0
    prev_tick = 0
    prev_bpm = tempos[0].bpm

    for tempo in tempos:
        if tempo.tick >= tick:
            break
        time_s += (tempo.tick - prev_tick) / resolution * (60.0 / prev_bpm)
        prev_tick = tempo.tick
        prev_bpm = tempo.bpm

    time_s += (tick - prev_tick) / resolution * (60.0 / prev_bpm)
    return time_s


def find_active_engine(proc):
    """Return the engine object whose song clock is actively advancing.

    Clone Hero keeps several engine-shaped objects on the heap at once, and
    restarting a song allocates a NEW one while leaving the old one frozen at
    its final time (e.g. stuck at 21.95s with the last score). So we cannot
    grab the first object with a plausible clock -- that is often a dead one.
    The live song engine is the only object whose clock moves, so we read every
    candidate twice a moment apart and return the one that changed.
    """
    back_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_BACK), 8)
    front_bytes = proc.read(proc.resolve(constants.RVA_CONST_NORMAL_FRONT), 8)
    module_end = proc.module_base + 0x4000000

    while True:
        hits, _, _ = scan_for_engine(proc, back_bytes, front_bytes)
        heap = [h for h in hits if not (proc.module_base <= h < module_end)]

        first = {}
        for e in heap:
            try:
                if proc.read_double(e + 0x20) < 0.001:
                    continue
                first[e] = proc.read_double(e + OFF_SONG_CLOCK)
            except OSError:
                pass

        time.sleep(0.12)

        for e, c0 in first.items():
            try:
                c1 = proc.read_double(e + OFF_SONG_CLOCK)
            except OSError:
                continue
            if abs(c1 - c0) > 1e-6:   # clock moved -> this is the live song
                return e

        sys.stdout.write(".")
        sys.stdout.flush()
        time.sleep(0.4)


def main() -> None:
    chart_dir = sys.argv[1] if len(sys.argv) > 1 else SYNOVIAL_DIR
    chart_path = os.path.join(chart_dir, "notes.chart")

    # Support both .chart and .mid formats
    mid_path = os.path.join(chart_dir, "notes.mid")
    if os.path.exists(chart_path):
        print(f"Parsing {os.path.basename(chart_dir)} (.chart)...")
        resolution, tempos, notes = parse_chart(chart_path)
    elif os.path.exists(mid_path):
        print(f"Parsing {os.path.basename(chart_dir)} (.mid)...")
        resolution, tempos, notes = parse_midi(mid_path)
        chart_path = mid_path
    else:
        print(f"  No notes.chart or notes.mid found in {chart_dir}")
        return
    print(f"  {len(notes)} notes, resolution={resolution}")
    print(f"  First note at {notes[0].time_s:.2f}s, last at {notes[-1].time_s:.2f}s")

    print("\nConnecting to Clone Hero...")
    proc = open_process()
    proc.verify_targets()

    print("  Waiting for active engine (start/unpause the song)...")
    engine_ptr = find_active_engine(proc)
    clock_now = proc.read_double(engine_ptr + OFF_SONG_CLOCK)
    print(f"  Engine at {engine_ptr:#x}, song clock = {clock_now:.2f}s")

    driver = InputDriver()

    # Keep Clone Hero focused so SendInput reaches it
    import ctypes
    user32 = ctypes.windll.user32
    ch_hwnd = user32.FindWindowW(None, "Clone Hero")
    if ch_hwnd:
        print(f"  Clone Hero window handle: {ch_hwnd:#x}")
    else:
        print("  WARNING: could not find Clone Hero window")

    def ensure_focus():
        if ch_hwnd:
            user32.SetForegroundWindow(ch_hwnd)

    def read_clock():
        return proc.read_double(engine_ptr + OFF_SONG_CLOCK)

    def read_score():
        return proc.read_u32(engine_ptr + OFF_SCORE)

    # Start from wherever the song already is -- no restart required. Skip past
    # every note whose time has already gone by and begin at the next one due.
    clock_now = read_clock()
    cursor = 0
    while cursor < len(notes) and notes[cursor].time_s < clock_now - 0.05:
        cursor += 1
    if cursor >= len(notes):
        print(f"  Song clock at {clock_now:.2f}s is past the last note; nothing to play.")
        proc.close()
        return
    print(f"  Song clock at {clock_now:.2f}s; starting at note {cursor + 1}/{len(notes)} "
          f"(t={notes[cursor].time_s:.2f}s)")

    score_before = read_score()
    total_sent = 0
    total_hit = 0
    streak = 0
    max_streak = 0

    print(f"\n  {'#':>4}  {'chart_t':>7}  {'clock':>7}  {'diff_ms':>7}  {'lanes':10}  {'result':6}  {'streak':>6}")

    last_status = 0.0
    prev_clock = read_clock()

    try:
        while cursor < len(notes):
            note = notes[cursor]
            target = note.time_s

            # Poll the clock and wait for the note's time
            stall_start = time.perf_counter()
            last_clock = read_clock()
            while True:
                clock = read_clock()

                # Restart/seek detection: clock jumped backward by >1s. Rebuild
                # the cursor from the new clock position rather than assuming the
                # song went back to note 0.
                if clock < prev_clock - 1.0:
                    print(f"\n  Song jumped back (clock {prev_clock:.2f} -> {clock:.2f}). Re-syncing.")
                    cursor = 0
                    while cursor < len(notes) and notes[cursor].time_s < clock - 0.05:
                        cursor += 1
                    score_before = read_score()
                    total_sent = 0; total_hit = 0; streak = 0; max_streak = 0
                    if cursor >= len(notes):
                        raise KeyboardInterrupt
                    note = notes[cursor]
                    target = note.time_s
                prev_clock = clock

                if clock >= target - 0.002:
                    break

                # Print status every 2 seconds so the user knows we're alive
                now_real = time.perf_counter()
                if now_real - last_status > 2.0:
                    print(f"  ...waiting: clock {clock:.2f}s, next note at {target:.2f}s "
                          f"({target - clock:.1f}s away)")
                    last_status = now_real

                # Stall detection: if clock hasn't moved in 5s, song stopped
                if abs(clock - last_clock) > 0.001:
                    stall_start = now_real
                    last_clock = clock
                elif now_real - stall_start > 5.0:
                    print(f"  Clock stalled at {clock:.2f}s for 5s. Song ended or paused?")
                    raise KeyboardInterrupt

                ahead = target - clock
                if ahead > 0.05:
                    time.sleep(min(ahead - 0.04, 0.5))
                elif ahead > 0.01:
                    time.sleep(0.001)

            # Ensure CH has focus before every input
            ensure_focus()

            # Send input for this note's lanes — press all down, hold, release
            vks = []
            for lane in note.lanes:
                try:
                    vk = driver.get_binding(lane)
                    vks.append(vk)
                    driver._send_key(vk, key_up=False)
                except KeyError:
                    pass

            time.sleep(0.003)
            for vk in vks:
                driver._send_key(vk, key_up=True)

            # Check hit: the game score only rises when a note registers.
            time.sleep(0.005)
            score_after = read_score()
            hit = score_after > score_before

            total_sent += 1
            if hit:
                total_hit += 1
                streak += 1
                if streak > max_streak:
                    max_streak = streak
            else:
                streak = 0

            diff_ms = (clock - target) * 1000

            lane_str = "+".join(LANE_NAMES.get(l, "?") for l in note.lanes)
            tag = "HIT" if hit else "MISS"

            if total_sent <= 30 or total_sent % 20 == 0 or not hit:
                print(f"  {total_sent:4d}  {target:7.2f}  {clock:7.2f}  {diff_ms:+7.1f}  "
                      f"{lane_str:10}  {tag:6}  {streak:6}")

            score_before = score_after
            cursor += 1

    except KeyboardInterrupt:
        print("\n  Stopped.")
    except OSError as e:
        print(f"\n  Lost process: {e}")

    print(f"\n  Notes: {total_sent}, Hits: {total_hit}, Misses: {total_sent - total_hit}")
    print(f"  Hit rate: {total_hit/max(total_sent,1)*100:.1f}%")
    print(f"  Best streak: {max_streak}")

    proc.close()
    print("Done.")


if __name__ == "__main__":
    main()
