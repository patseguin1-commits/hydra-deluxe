# Kick dynamics are priced

Clone Hero reads a drum note's MIDI velocity to decide whether it is a ghost, a
normal hit, or an accent. Velocity 1 is a ghost, velocity 127 is an accent, and
either one scores double. The rule is per note, not per lane.

Hydra applied that rule to the four pads and skipped the kick. Every kick came
out of the MIDI reader as Normal, worth a flat 50. So on any chart that charts
its kicks with dynamics, Hydra's "optimal" was not the optimal — it was the
score of a player who somehow missed the doubling on every ghost kick.

## The proof

Onyxite's "Won't Get Fooled Again (O)" (hyhash
`af44d5c8d941bb33444d18e7871cdc77`) charts 36 kicks at velocity 1.

- Hydra said the optimal was **1,134,335**.
- A player's full combo, on Hydra's own path, scored **1,142,235**.
- With kick dynamics priced, Hydra says **1,143,035**.

A player beating the optimal is the tell. The gap is the 36 ghost kicks: they
were each worth 50 to Hydra and 100 to the game, and the game's own combo
multiplier compounds the difference. Ghost kicks are proven by video on this
chart.

Accent kicks are **assumed**, not proven. Nobody has produced a video of a
chart with velocity-127 kicks. The symmetric rule is what Clone Hero's source
does for every other lane, so we apply it to the kick too; if it ever turns out
the game special-cases the kick's top velocity, this is the assumption to
revisit.

## The decision

The kick uses the pads' velocity rule, in both places the MIDI reader spells a
kick: pitch 96 (the difficulty's own kick) and pitch 95 (the 2x kick). Both now
read the note's velocity instead of hard-coding Normal.

`[ENABLE_CHART_DYNAMICS]` still gates the whole thing. A chart without that
text event has its dynamics dropped for kicks exactly as for pads, so charts
that carry meaningless velocities are unaffected.

The `.chart` format has no kick accent/ghost flag, so the `.chart` parser is
untouched.

`allows_dynamics()` now answers yes for every lane, which is what makes a kick's
dynamic show up in `ChordNote::str()`. That string used to show either the
dynamic or the 2x marker; a kick can now be both, so it shows both, in one
parenthesis: `Kick`, `Kick (Ghost)`, `Kick (2x)`, `Kick (Ghost, 2x)`,
`Kick (Accent, 2x)`. Pad wording is unchanged.

## The chord code table

Superseded by ADR 0015 (1.8.1): chord codes are spelled out lane by lane and
the table below no longer exists. Kept as history.

Every chord Hydra can score has a short code string, and stored paths, the
replay JSON and the analysis chord counts all carry it. A chord with no code
throws.

Priced kick dynamics triple the kick's shapes, so the table grew from 551
chords to 1287. The 551 legacy codes are frozen byte-for-byte, because records
in users' databases store them. Each of the 736 new chords — the ones with a
ghost or accent kick — takes the code of the same chord with a normal kick,
prefixed `g` for ghost or `a` for accent. Every legacy code is one character or
starts with `B`, so a prefixed code can never collide with a legacy one.

`tools/gen_chord_tables.py` generates the table and is idempotent: re-running it
with no source change reproduces `src/core/chord_tables.cpp` byte for byte.

## What this costs

Every stored record is now wrong, so the version goes to 1.7.8 and the whole
library reads Stale. Re-analysis is the fix, and it is the honest answer: the
old scores were computed under a rule the game does not use.

Scores go **up**, never down, and only on charts that chart kick dynamics. A
chart with no velocity-1 or velocity-127 kicks scores exactly what it did
before.

The Preview inherits the change for free, because it reads the note's ghost and
accent flags: a ghost kick now draws as a narrowed bar with the ghost overlay,
an accent kick with the accent overlay, the same treatment the pads already
get. That is Clone Hero's own rendering, not a new invention.
