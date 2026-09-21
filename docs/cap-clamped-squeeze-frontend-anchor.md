# A capped Star Power window's frontend lever is the collection note, not the activation

Status: **warning shipped; re-anchoring the math still open.**

Song Details now tells you when this is happening. If an activation's Star
Power window hit the cap and that activation has a squeeze the frontend
decides, the details view shows an overfill warning naming the collecting
note (the phrase note that filled the meter) and explaining that its timing,
not the activation's, moves the SP end. That warning is display-only: it
reads a new stored fact (`clamp_tick`, the collecting note the search pinned
the window to) and says something about it. The actual math this note
describes is still wrong — `sp_end_shift_ms`, `required_frontend_ms`,
`exact_even_split_ms`, and `frontend_transfer_scales` still anchor on the
activation instead of the collecting note. That re-anchoring is the part left
for a future session; everything below still describes the open gap.

## The one-line mechanic

A squeeze's frontend timing pivots on whichever note the Star Power end is
rigidly tied to. That is usually the activation. But when the meter is full to
the SP cap, the end is measured from the last phrase you *collected* — so that
collection note, not the activation gem, is the note whose early/late hit moves
the end.

## Why this is easy to miss

Star Power lasts a fixed number of measures — song distance — not a fixed number
of milliseconds. So moving where the window *starts* moves where it *ends* by
the same measure count (scaled by tempo; see **Transfer scale** in CONTEXT.md).

Normally the window starts at the activation, so the activation is the frontend
lever. But collecting a phrase mid-Star-Power extends the end (+2 measures per
phrase), and the **SP cap** puts a ceiling on the total. When the ceiling binds,
the end stops tracking the activation and instead sits a fixed
`2 * sp_cap` measures past the *collection note* that filled the meter. From that
point on, the activation can be nudged and the end does not move — it is pinned
to the collection note.

In the engine this is `extend_deacts` (`src/search/graph.cpp`): the end is
`min(prev_end + 2 measures, plusmeasure(collection_note, 2 * sp_cap))`. When the
second term is the smaller one, the window is **cap-clamped** and the collection
note is its anchor.

## Where Hydra prices it wrong

`sp_end_shift_ms` (`src/core/squeeze_rating.cpp`) computes how far the SP end
moves for a given frontend error, and it always builds the end from the
activation's hit-time and `2 * sp_meter` measures. Its own comment says so:
"it does not model the +2-measure extension per SP phrase collected
mid-activation." `required_frontend_ms` and `exact_even_split_ms` inherit that
anchor, and `frontend_transfer_scales` takes both of its transfer scales from
`act_tick` as well.

So in a cap-clamped window every frontend number Hydra reports is anchored on
the wrong note:

- The printed recipe names the activation ("activate N ms early"), but in a
  clamped window the activation is inert — it cannot move the end until it is
  hit roughly a full gap-width early. The real early hit the player needs is on
  the collection note.
- The transfer scale `r` is read at the activation's measure. The collection
  note sits in a different measure, so on any tempo or time-signature change `r`
  differs, and the **Squeeze rating** difficulty tier can come out wrong. (On a
  flat-tempo chart `r` is 1.0 both ways and only the *recipe* is misleading, not
  the tier — which is why this hid for so long.)

There is a self-check hiding in this: in a clamped window an honest
activation-anchored `sp_end_shift_ms` returns ~0 for any realistic displacement
(the ceiling binds), so `required_frontend_ms` should blow up toward infinity.
That explosion is the tell that the anchor is wrong. The current code never sees
it because it prices with the plain unclamped `2 * sp_meter` formula.

## The proposed rule (for whoever implements it)

Detect the clamp and re-anchor the frontend math on the collection note.

A window is cap-clamped when its recorded **deact node** equals
`plusmeasure(collection_note, 2 * sp_cap)` and that is earlier than the
activation's own `plusmeasure(activation, 2 * sp_meter)` reach. When it is,
`sp_end_shift_ms`, `required_frontend_ms`, `exact_even_split_ms`, and both legs
of `frontend_transfer_scales` should use the collection note's hit-time and tick
as the frontend anchor instead of the activation's.

The record does not currently carry the clamp anchor — `frontend_transfer_scales`
only has `act.timecode`, `sp_meter`, `deact_tick`, and `sqinouts`, and the
anchoring collection note cannot be recovered from those alone. So this follows
the same pattern as the deact node in **ADR 0011**: a fact the record lacks gets
a stored field written by the search where it already computes the ceiling —
never another re-derivation at display time. **Difficulty** stays raw gap ms
(**ADR 0001**); this only fixes which note the frontend recipe and the transfer
scale are measured from.

## The case that proved it

Video: `Rolling in the Deep (Dirty Loops) Pro Drums FC [XF1l2c1Pv8Y].mkv`
(streamer Splax77), Expert Pro Drums, 2x Bass, sp_cap 4. Chart hyhash
`8a49ee680e10947f4fa28a5da6ef0026`. The player's first activation is identical
to Hydra's optimal path, so the geometry below is exactly what
`squeeze_scout.py` reports for that activation.

- Activation at tick 45000. Base SP end `plusmeasure(45000, 8)` = tick 60360.
- A phrase collected mid-Star-Power completes at tick **48720**. Its ceiling
  `plusmeasure(48720, 8)` = tick **64080**, which is earlier than the activation
  path's tick 64200, so the end clamps to **64080** (song 60768.078 ms).
- The squeezed-out phrase's completion note is tick 63960 (song 60652.694 ms).
  Gap to the SP end = **115.384 ms** — this is the "impossible over-100 ms"
  squeeze.

The squeeze needs collection-note-early `E` plus phrase-note-late `B` to exceed
115.384 ms. Read straight off the in-game Accuracy overlay, frame-accurate:

- Collection note (tick 48720): **−63.1 ms** (early). Registers at video 50.05 s;
  its nominal video time is 50.114 s. This is the frontend lever.
- Squeezed phrase note (tick 63960): **+67.3 ms** (late). Registers at
  video 64.85 s; nominal 64.768 s.
- 63.1 + 67.3 = **130.4 ms > 115.384 ms** → the note lands after Star Power ends,
  is not doubled, and its phrase is banked. Squeezed.

The activation was −3.4 ms early and contributed nothing, exactly as the clamp
predicts. Video-to-song alignment for this file is `song_ms = video_ms − 4114.9`.

## What to be careful of if you reproduce this

The multiplier disc dropping x8→x4 lands a couple of render frames after Star
Power actually empties, so it corroborates the direction but is not the precise
instrument — quote the Accuracy overlay, caught on the frame each note registers,
not the disc. `squeeze_scout.py --result <id>` rebuilds the window
engine-exactly and is the source for the tick/ms geometry above.
