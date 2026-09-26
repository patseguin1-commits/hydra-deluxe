# Chord codes are spelled out, not looked up

Stored paths carry each chord as a short code string: an activation's
frontend chord, every backend row and every multiplier squeeze. The code used
to come from a lookup table. The Python-era `hyencode.py` handed out codes by
counting down through the keyboard characters, so the codes followed no rule,
and the table had to list every chord. It listed 551 frozen codes, then 736
kick-prefixed ones added by ADR 0012.

The table only held chords with a kick and at most two pads, on the theory
that a player has two hands. Charts are not bound by that. Joe Sibol's Hot
Sexy Girls (Rock Band Network) hits red, yellow cymbal and green cymbal
together, and its 1.8.0 re-analysis died with "chord has no encode-table
entry". The search had scored the chord fine. Only saving the path failed.

## The decision

Hydra scores whatever a chart holds and never judges whether a chord is
playable. So every chord a chart can express needs a code, and the code is
worked out from the chord itself.

`Chord::code` writes one character per lane, in kick, red, yellow, blue,
green order. "." is an empty lane. Otherwise it is `n`, `g` or `a` for a
normal, ghost or accent note, in upper case for a cymbal (yellow, blue,
green) or a 2x kick. Red can be neither, so it is always lower case. Red
with yellow and green cymbals is `.nN.N`. `Chord::from_code` reads it back
and rejects anything malformed.

There is no table, no generator script and no CPython tuple hash.
`tools/gen_chord_tables.py` and `src/core/chord_tables.*` are gone.

## What this costs

Every stored path used the old codes, so the path node format goes to 5 and
the structure format to 5. Every result analyzed before 1.8.1 reads Stale,
and the library needs one re-analysis. Nothing reads the old codes any more.

A code is now five bytes where most used to be one to three. Codes are a
small part of a stored path, and the replay JSON's `chord_code` field is
display-only.

ADR 0012's section on the chord code table is superseded by this one.
