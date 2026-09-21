# The cap-clamp anchor is stored by the engine, never re-derived

An activation's Star Power window normally ends a fixed distance past the
activation. But when a phrase collected mid-Star-Power fills the meter all
the way to the SP cap (the most bars of SP you can hold), the window's end
gets pinned to that phrase's note instead — the meter can't grow past the
cap, so nothing collected after that note can push the end out any further.
We call that note the clamp anchor.

The search knows the clamp anchor exactly: it is the collecting note it
compared against the cap while extending the window. But at copy-out it
used to keep only the resulting end tick, not which note produced it. So the
record could say a window was capped without saying which note did the
capping — the one fact a frontend warning needs to point the player at the
right note.

## The decision

The engine writes the clamp anchor onto the record. Nothing outside the
search reconstructs it — not from the deact node, not from the backend rows,
not from the chart.

`Activation::clamp_tick` is the field. The search stamps it onto the
activation at copy-out: the collecting note that pinned the window's end,
when the cap bound. It is left unset when the window never hit the cap. Blob
format version 5 stores it. Path node payload version 3 carries the new
activation layout.

## No fallback

A record written before blob version 5 has no `clamp_tick`. A consumer that
finds it unset says nothing was clamped — it does not guess. The overfill
warning in Song Details simply does not appear until the chart is
re-analyzed.

## What this costs

Old records show no overfill warning until they are re-analyzed. The store's
Ready rule already checks the stored path format (from ADR 0011), so records
analyzed before this change read Stale right away — the library asks for
re-analysis immediately. No separate version bump is needed beyond the one
above.
