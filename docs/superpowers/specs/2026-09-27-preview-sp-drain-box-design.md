# Preview: Star Power drain box

## What it is

The Preview tab gets a small box that shows how fast Star Power drains at the
playhead. You can scrub through a song and see which sections burn SP quickly
and which burn it slowly.

SP always drains at one bar per two measures. So in seconds, the speed depends
only on tempo and time signature. At 120 BPM in 4/4 a bar lasts 4 seconds. At
160 BPM it lasts 3. A 7/8 section drains faster than 4/4 at the same BPM,
because its measures are shorter.

## What you see

The box sits bottom-right, just left of the SP gauge. It uses the same
translucent dark panel and monospace font as the time box. It always has three
lines.

When the selected path has SP running at the playhead, the box is "active":

```
SP drain                (gold)
1 bar / 3.0 s
empties in 7.3 s        (gold)
```

Everywhere else it is "idle", and tells you what would happen if you activated
right here:

```
SP drain (if activated) (grey)
1 bar / 3.0 s
full meter 11.2 s       (grey)
```

"1 bar / 3.0 s" is the drain speed at this exact moment. "full meter" is how
long a full meter (the record's SP cap, 4 bars in normal play) would last if
activated here with no phrases collected on the way. It follows any tempo or
meter changes coming up, so it is exact rather than 4 times the current speed.
"empties in" is the time left until this activation's SP runs out.

Numbers are rounded to 0.1 s. The box shows only when the gauge shows, so a
chart with no SP phrases gets neither. Nothing else on the Preview changes: the
gauge, the time box and the score box stay exactly as they are.

## Where every number comes from

The box re-derives nothing about Star Power. Each value comes from the record
or from a function the engine already uses.

Whether SP is running comes from the path's own record. Each activation in the
scene starts at its activation note and ends at the deact tick the search
stored (`PreviewActivation::sp_end_ms`, filled from `activation_deact_tick`).
The box only asks whether the playhead falls between the two.

"empties in" is that same stored end minus the playhead. It is the value the
gauge already treats as the truth.

"1 bar / X s" is the drain rule's constant, `kMeasuresPerSpBar`, times how long
one measure lasts at the playhead, `SongTiming::ms_per_measure_at`. The engine
never stores a rate. It adds "2 × bars" measures to a start point, and this is
that rule's speed at this instant. `squeeze_rating.cpp` already uses
`ms_per_measure_at` for the same purpose. The speed is read at the playhead's
tick, so exactly on a tempo or meter change it reads the new section. That is
the same way the time box's BPM line behaves.

"full meter X s" calls the engine's own SP-end function,
`SongTiming::plusmeasure(timecode(now_tick), sp_bars_to_measures(cap))`. It
takes that result's ms minus the ms of `now_tick`. This is the call the search
makes for every activation's end (`graph.cpp`, `add_act_edge`), including its
rounding to whole ticks. `now_tick` is the playhead's tick, found with the same
helper the time box uses. `cap` is `scene.sp_meter.cap`.

What the box deliberately does not use:

- The gauge's curve. Its last stretch before a deact node is forced to hit
  zero exactly there, so its slope can differ slightly from the real speed.
- `SongTiming::sp_end_ms`. The engine does not use it, it works in fractional
  ticks, and it has had no callers outside its own tests since 2026-09-25.

## Edge cases

An old record written before blob v4 has no deact node, so its activations
have no stored end. The box cannot know whether SP is running there, so it
stays idle and never shows "empties in". The gauge already treats those
records the same way.

Exactly at an activation's start the box is active. Exactly at its stored end
it is idle again. That matches how the gauge reads a shared boundary.

A chart whose tempo ramps up through many small tempo changes will show the
speed ticking through values as you scrub. That is the real speed, and 0.1 s
rounding keeps it readable.

If "full meter" would run past the end of the chart, `plusmeasure` still gives
a time from the tempo map, so the number is shown as normal.

## How it is built

A new pure function in `src/app/preview_view.{h,cpp}`, next to
`build_score_box`:

```cpp
struct PreviewDrainBox {
    bool shown = false;   // false when the scene has no SP gauge or no timing
    bool active = false;  // the path has SP running at the playhead
    std::string header;   // "SP drain" or "SP drain (if activated)"
    std::string rate;     // "1 bar / 3.0 s"
    std::string detail;   // "empties in 7.3 s" or "full meter 11.2 s"
};

PreviewDrainBox build_drain_box(const PreviewScene& scene, double now_ms);
```

`PreviewController` gets a `drain_box()` accessor that passes the transport's
`now_ms()`, the same way `score_box()` does. `details_view.cpp` draws the box
right after the gauge, anchored bottom-right, just left of the gauge's column.
Its header and detail are gold (the gauge's `255, 204, 51`) when active and
light grey when idle. The rate line is always white.

## How it is tested

Unit tests go in `tests/test_preview_view.cpp`, written first:

- A steady 120 BPM 4/4 chart reads "1 bar / 4.0 s" and "full meter 16.0 s".
- A tempo change switches the speed exactly at the change tick, not before.
- A 7/8 section at the same BPM reads a shorter bar than 4/4.
- "full meter" that crosses a tempo change equals the `plusmeasure` result.
- Inside a stored activation window the box is active and "empties in" equals
  the stored end minus the playhead. At the stored end it is idle again.
- An activation without a stored end leaves the box idle.
- A default-built scene, and a scene with no SP gauge, give `shown == false`.

The whole suite must still report `Status: SUCCESS!`. Then `hydra_uitest`
checks on the Preview tab that the box shows the right text for an analyzed
chart, both idle and inside an SP window, and saves a screenshot of the active
box to look at.

The test harness always draws at 1280×800, so a narrower window can only be
checked by eye in the real app. If the box covers the highway there, it moves
to the top-right corner beside the top of the gauge, where the highway is
narrowest. It does not shrink.

## Revision after the first look (2026-09-27)

The user ran the first version on "One [Metallica]" (Periphery) and asked for
three changes. This section replaces the parts above that it contradicts.

### "full meter" snaps

"full meter" glided while the playhead moved. It measured a full meter forward
from the playhead, so it counted every time signature change inside the next
eight measures. That chart has twenty. The user wants the box to report the
section the playhead is in and jump when the section changes.

"full meter X s" is now the meter's cap times the bar time in force at the
playhead. That's the same `kMeasuresPerSpBar * ms_per_measure_at(now_tick)`
the rate line reads, times `scene.sp_meter.cap`. Both lines jump together, at
the exact tick of a tempo or time signature change. The box no longer calls
`plusmeasure`. "empties in" is unchanged. It still counts down from the stored
end, because that is time passing.

### A time signature line in the time box

The time box gets a new line under BPM: "Time signature: 6/4". It shows the
signature in force at the playhead's tick, exactly as the chart wrote it, and
4/4 before any.

The parser used to keep only each signature's measure length in ticks. That
can't tell 6/8 from 3/4, because both are 1440 ticks at 480 per quarter note.
So `Song` gets `timesig_changes`, tick to (numerator, denominator). The same
`apply_timesig` call that writes `tpm_changes` records it, so the two can't
disagree. The engine still reads only `tpm_changes`. The Preview re-parses the
chart when it loads, so nothing stored in the database changes.

### The boxes scale to fit beside the highway, and the drain box moves top-right

In a narrow window the drain box sat on the highway. The highway doesn't shrink
when the window narrows. Its size follows the preview's height, because the
renderer makes the track min(height, width × 1.17) tall, and the preview is
always wider than that. So narrowing the window only removes the empty space
at the highway's sides.

Now all three text boxes share one scale, fitted so they sit beside the highway:

- The time box and score box stay top-left.
- The drain box moves to the top-right, just left of the gauge, level with the
  gauge's top. At the bottom-right the highway is at its widest, and at the
  user's width the box would have needed 50%.
- The scale is 1 (today's size) whenever there's room. It shrinks just enough
  in a narrow window, and never goes below 60% (`kOverlayMinScale`). Below
  that the text stops being readable, so the boxes overlap instead.
- The gauge keeps its size.

Where the highway sits on screen comes from the renderer's own camera. The
highway's outer edges are its two railings, which are straight lines, so each
edge is found by projecting two railing corners through the same camera the
renderer draws with. The track height is worked out in one place and shared by
the renderer and this layout, so the two can't disagree. The fit checks each
box at its lowest row, where the highway is widest within the box. That row is
measured at full size, so a shrunken box is only further clear.

### Testing the revision

- Unit tests cover the snapping "full meter".
- The parser keeps 6/8 and 3/4 apart.
- The time box line reads the signature in force.
- The projected highway edges pass through the projected railing corners, stay
  symmetric, and keep their size when only the width changes.
- The fitted scale is 1 with room, floors at 60%, never falls as the image
  widens, and clears the highway exactly whenever it's between the two.
- A GUI test narrows the harness's display, checks the scale drops below 1 but
  not below 60%, and saves a screenshot.
- The user then looks again in the real app.
