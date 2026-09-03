# The deact node is stored by the engine, never re-derived

An activation's Star Power ends at one exact chart tick. We call it the deact
node, or D. It is two measures per banked bar past the activation, plus two
more measures for every Star Power phrase the player collects while Star Power
is running.

The search knows D exactly. It has to: D is the graph node the deactivation
edge points at. But at copy-out it used to copy that edge's backend squeeze
rows onto the activation and drop D itself.

So the record never said where Star Power ended. Three display layers worked it
out again, from the only trace left — the backend rows, whose timing offsets
are measured against D. Each of the three had its own guess for the case where
there are no rows, and the guesses did not agree.

## What went wrong

A backend row only exists when some note lands within 500 ms after D. Usually
one does. When none does, the record stores no row at all, and there is nothing
left to read D out of.

`squeeze_rating.cpp` then fell back to "two measures per banked bar", plus two
more if the activation recorded a Star Power squeeze-in. That fallback cannot
see an ordinary mid-Star-Power collection, because a collection leaves no other
mark on the activation.

A user caught it on video. The Preview's Star Power meter drained to empty two
measures early, and then showed a bar the player had never banked — the
Preview counts collected phrases by measuring D's own extension, so a short D
made a collected phrase look like a squeezed-out one.

`replay.cpp` had a third answer: with the chart in hand it re-counted the
phrases inside the window and grew the end until the count settled. That one
was right, but it was solving a problem that should not have existed.

## The decision

The engine writes D onto the record. Every consumer reads that field. Nothing
outside the search reconstructs D — not from backend rows, not from squeeze-ins,
not from the chart.

`Activation::deact_tick` is the field. The search stamps it at copy-out: the
deactivation edge's destination tick, or the Star Power end it tracked when
Star Power outlasted the chart. Blob format version 4 stores it. Path node
payload version 2 carries the new activation layout.

`activation_deact_tick()` still exists, but it now returns the field and
nothing else. `deact_tick_from_rows()` and `deact_tick_for()` are gone.

## No fallback

A record written before blob version 4 has no `deact_tick`. A consumer that
finds it unset says it cannot tell. The Preview draws no active window, the
transfer scales come back unset, the replay skips the activation.

That is deliberate. A fallback is what caused the bug: it looked like an
answer, so nobody checked it. Saying nothing is visibly wrong, and the fix is
one re-analysis away.

## What this costs

Old records lose their Star Power window in the Preview until they are
re-analyzed. The store's Ready rule now also checks the stored path format,
so every record analyzed before this change reads Stale right away -- the
library asks for re-analysis immediately. No version bump is involved.

The store also grew nine bytes per activation — a presence flag and an
eight-byte tick.
