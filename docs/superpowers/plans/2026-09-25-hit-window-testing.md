# Hit window testing: next round

This plan picks up from the 2026-09-25 live session. The formula and its
constants are settled. What's still open is the 170 ms cap, whether there is a
floor, which gap sets a note's window, where the game stores hit timing, where
real hits turn into misses, and what precision mode does. Until step 5 is done,
Hydra keeps the fixed 85 ms.

## The probe songs

Both songs are installed under `C:\Clone Hero\songs\Hydra Probe\`. They are
built by `tools/ch_probe/probe_songs.py`. Clone Hero needs a song rescan to
see them.

Every note is a kick. The chart uses 480 ticks per beat at 125 BPM, so one
tick is exactly one millisecond and every gap is exact. The chart Offset and
the song.ini delay are both 0. The audio is silent.

Each folder also holds a `manifest.json`. It lists every note's time, which
test block it belongs to, and the gap before and after it. It holds no
predicted values. A watcher uses it to match window changes to notes.

**Window Map** (62 s, 142 notes) covers steps 1 to 3 with no inputs needed.

- The cap test uses runs of 8 notes at 150, 170, 180, 200, 250 and 400 ms.
- The floor test uses the same run shape at 30, 40, 50 and 60 ms.
- Each run starts with 5 notes at 100 ms. These pull the window to a different
  value first, so a run that leaves the window alone is a real result, not a
  sign the engine stopped updating.
- The which-gap test uses three-note groups. Each middle note has uneven gaps:
  60 then 140, 140 then 60, 90 then 130, and 130 then 90. Each pair appears in
  both orders, which lets us tell "gap before" apart from "gap after".
- Three seconds of silence separate the blocks.

**Edge Walk** (125 s, 120 notes) covers step 5. It is 120 kicks one second
apart. Every note is far from its neighbours, so a late hit on one note can't
land in the next note's window.

## Steps

### 1–3. Play Window Map and log the window against the song clock

Here is how it works today. `experiments/poll_windows.py` logs the window
field (+0x20) with wall-clock time only. That's enough to count distinct
values. It can't tie a value to a note.

The change: a new `experiments/watch_window.py`. It is a copy of poll_windows
that also reads the song clock (+0x100, which `play_chart.py` already relies
on) on each sample. After the song, it reads the song's manifest. It then
prints each block's window value in the order the values appeared.

- Step 1 (cap): for 170, 180, 200, 250 and 400 ms, the window should read
  171.43 in every run. If any run shows a different value, the cap is wrong.
- Step 2 (floor): compare the 30, 40, 50 and 60 ms runs against the values in
  your notes. The floor is real if every value under 75 ms reads 75.
- Step 3 (which gap): the order of values across each three-note group tells
  us whether the window follows the gap before, the gap after, or the smaller
  of the two. The mirrored groups check that answer.

We don't yet know whether the window updates when no key is pressed. The first
run will tell us. If the window freezes, play the song again with
`play_chart.py` hitting every note on time. On-time hits carry the same gaps,
so the answer is the same.

### 4. Read the game's own hit timing at +0x2e0

The code reading says the game copies the song time into +0x2e0 on a hit. The
live session tried +0x28 and never tried +0x2e0.

The test: play Edge Walk with `play_chart.py`, and log +0x2e0 next to the song
clock and the note times. If +0x2e0 changes once per hit and sits within a few
ms of the note time, it is the hit time. Each hit's error is then that value
minus the note time. This comes before step 5, because step 5 is much stronger
if it can use the game's own measurement.

### 5. Walk the edges on Edge Walk

This needs a driver: a variant of `play_chart.py` that takes a planned offset
for each note in place of "on time".

Start by walking the late side. Cover +80 to +92 ms in 1 ms steps, with 3
notes per step. That is 39 notes. Then walk the early side the same way over
the same range, for another 39 notes. That fits in one 120-note play. If a side misses at every step, its edge
is lower than 80. Then replay with that side walked lower.

For each note, record the planned offset, the measured offset (+0x2e0 if step
4 worked, otherwise the key-send time), and whether it hit. We can tell a hit
by whether combo and score (+0x94) went up.

The boundary is where hits turn into misses on each side. This answers three
questions. Is the limit ~85.7 ms (half the window) or the fixed 85? Are the
early and late sides the same? Can +85.9 ms count again?

After Edge Walk, build a dense version for the narrow windows that matter for
squeezes. It would use note pairs 60 and 100 ms apart, with a second of
silence between pairs, and the walk would move away from the partner note. The
note layout depends on the step 3 result, so it waits until then.

### 6. Precision mode

Turn on precision mode in Clone Hero and repeat steps 1–3 on Window Map. This
settles whether the odd precision constants are real.

## Known leftovers, not done here

The precision/normal branch labels in `constants.py` are still backwards (from
the live notes). The old `C:\Clone Hero\songs\Hydra Probe - Hit Window Test`
folder (60 red notes at 500 ms, no audio) is still installed. It can go once
the new songs load.
