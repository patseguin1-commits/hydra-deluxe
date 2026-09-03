# Hydra

Score optimizer and path viewer for Clone Hero drums: it scans a song library,
analyzes each chart, and shows the Star Power paths that give the best score.

## Language

### Library

**Chart**:
One playable song file (`.mid`, `.chart`, `.sng`, or `.srb`) plus its metadata.
_Avoid_: song file, track

**Library**:
The set of charts found by the latest scan of the user's folders.

**Scan**:
The pass that discovers charts in the configured folders and registers them in
the library. Distinct from analysis.

**Analyze**:
The search that computes a chart's paths and stores the result as a record.

**Record**:
The stored result of one analysis: the kept paths, their scores, and the
settings the analysis ran with. Its key is the chart, the chart mode, the SP
cap, and the analysis settings (the ms limit and the score range). A chart
keeps one record per settings combination; records share their stored paths,
so a path found under several combinations is stored once. A record is stale
unless all three hold: this build's version stamped it, it records which
settings it ran under, and its stored paths are in this build's
path-structure format. A lookup reports it as one of three statuses: not
analyzed, stale, or ready. A listing returns only ready records, so a stale
record reads the same as no record at all.

**SP cap**:
The Star Power meter ceiling an analysis runs under, in bars. 4 is Clone
Hero's rule and the default. Other values answer what-if questions; their
scores are not achievable in game.
_Avoid_: edition, uncapped, SP meter

**Auto cap**:
Raising the SP cap until the best score stops improving, so the result
approximates no ceiling at all. The record keeps the cap it settled on.

### Paths

**Path**:
One way to play a chart's Star Power: which activations to take and what each
is worth. The first path in a record is optimal.

**Activation**:
One use of banked Star Power, written in path notation with its skip count and
squeeze symbols (e.g. `E2+-`).

**Skip**:
A fill an activation deliberately passes over before activating.

**SP phrase**:
A chart section that awards a bar of Star Power when hit fully.
_Avoid_: star power section

**All-0 path**:
The best path whose activations all record zero skips, found under a 0 ms
timing limit.

### Squeezes

**Squeeze**:
A deliberately early or late hit that moves points across a scoring boundary.

**Frontend squeeze**:
Hitting the activation chord's activation note first, so the chord's other
notes score under Star Power.

**Backend squeeze**:
Hitting a note near the SP end early, so it lands inside Star Power.

**SqIn / SqOut**:
Squeezing an SP phrase's note into (+) or out of (-) an active Star Power
window, written as the `+`/`-` symbols in path notation.

**Multiplier squeeze**:
Ordering the hits of a multi-note chord on a combo-multiplier boundary so the
more valuable notes score on the higher multiplier.

**Calibration fill (E)**:
An activation timing (the `E` notation) where the fill must be summoned by
hitting early; its window is fixed, not the hit-window setting.

**Fill spawn deadline (CH 1.1)**:
The latest your SP meter can fill up and still have a fill appear. Clone Hero
1.1 puts it a flat 4 beats before the fill starts. This is what Hydra scores
by, always.

**Fill spawn deadline (CH 1.0)**:
The older rule: roughly one fill-length of lead time before the fill, clamped
to 250..10000 ms. Short fills got stricter in 1.1 and long fills got looser.
CLI only (`hydra_batch --legacy-fills`), needs its own database, and
`hydra_fillcompare` diffs the two. See docs/adr/0010.

**Hit window**:
The per-side ms window Clone Hero registers a hit in. A setting; feeds the
squeeze budgets, ratings, and report tiers, never the search.

**Transfer scale**:
How frontend timing error carries to the SP end. SP length is measured in
measures, so a hit `d` ms off moves the SP end `r*d` ms; early and late hits
can scale differently on a signature or tempo change.

**Deact node**:
The exact chart position where an activation's Star Power ends; backend
squeeze timings are measured against it. The search stamps it onto the
record as it runs, and nothing downstream re-derives it.
_Avoid_: SP end tick (when the anchored search position is meant)

**Squeeze rating**:
The displayed difficulty judgement of a squeeze: its rating label, its
effective ms once the transfer scale is applied, and whether the scale is
material enough to warn about.

**Difficulty**:
A path's or activation's hardest required squeeze, in raw gap ms — never
scaled by the transfer scale.

### Preview

**Preview**:
The rendered, playable view of a chart: its notes as a scrolling 3D highway,
synced to the song audio. Distinct from a Path (the scoring plan) and a Chart
(the song file).
_Avoid_: player, viewer

**Note highway**:
The 3D fretboard the Preview draws, with the drum notes scrolling toward the
strike line as the song plays.
_Avoid_: fretboard, track

**Gem**:
One drawn note on the note highway. Its model shows the drum type (tom, cymbal,
or kick), its texture shows the lane, the note's dynamics, and whether it sits
in an SP phrase.
_Avoid_: note (the chart datum), block

**Lane**:
One column of the note highway, in KRYBG order. The four playable lanes
(Red..Green) spread across the highway; Kick is the full-width bar.

**Strike line**:
The fixed line on the note highway where a note is due to be hit. Notes scroll
down to it as the song plays; a gem flashes there when it passes.
_Avoid_: hit line, target line, target (the Onyx name)

**Beat line**:
A line across the note highway at a bar, a beat, or the half-beat before one,
drawn from the chart's timing.

**Active SP window**:
The stretch of the note highway from an activation to its deact node, where
Star Power is being spent. Comes from the path, not the chart; drawn as a
tinted floor.

**Path overlay**:
Hydra's own analysis drawn on the note highway: the active SP windows and the
fills as a player following the path would see them. A fill the path
activates on is *taken* (all four lanes lit, the activation note's lane
highlighted); a fill the path had enough SP for but passed over is *offered*
(lanes lit dimly); every other candidate fill is hidden, because the game would
not have shown it. Offered fills come from the activation's skip count, not a
re-derived SP meter; fills after the last activation are hidden because the
engine records nothing about them.

**SP meter gauge**:
The vertical gauge on the note highway's right edge showing banked Star Power
at the playhead: up one bar at each collected phrase, draining through each
activation to hit empty exactly at the deact node. Anchored to the record's
per-activation bank and deact node, never re-derived — which phrases SP
collects is counted off the deact node's own extension, so a squeezed-out
phrase inside the window banks when SP ends rather than during the drain.
Without a path it fills and pins at the cap, since nothing spends it.

**Stem**:
One of the several audio files a chart may ship instead of a single mix (e.g.
`drums`, `guitar`, `song`). The Preview decodes and mixes all of a chart's
stems into one output.

**Mixer**:
The step that decodes a chart's stems, resamples them to one common format,
and sums them into a single signal to play. A stem it cannot decode is
skipped, so one broken stem does not silence the rest.

**Transport**:
The Preview's play, pause, and seek control together with its clock. The clock
is the master: while playing it is the time at play plus the time since; the
note highway reads it to place the notes, and the audio follows it.
_Avoid_: player (the whole Preview), scrubber (the UI control only), playhead
(the audio follower only)
