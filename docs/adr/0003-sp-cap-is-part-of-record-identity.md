# The SP cap is part of a record's identity

> **Superseded 2026-09-27: Auto was removed.** The SP cap is now always a
> number, and it is still part of a record's identity. The parts below about
> Auto are history. Records Auto made are deleted the first time the new
> version opens a database.

A record is the stored result of analyzing one chart. Since 1.6 it is keyed by
chart, chart mode, and the SP cap it ran at. A 4-bar result and a 64-bar
what-if for the same chart are two rows that never overwrite each other.

## Why

Before 1.6 the cap was a build-time choice: `Hydra.exe` always ran at 4 bars
and `HydraUncapped.exe` ran with no ceiling, each with its own database. Two
programs for one setting was clumsy, so the cap became a runtime setting.

Once it is a setting, a user can change it and analyze again. The old key
(chart + mode) would have made the 8-bar run overwrite the 4-bar one. A
no-ceiling run can take minutes per chart. Paying that again just to get the
4-bar answer back is the exact pain the separate database used to avoid.
Putting the cap in the key keeps every paid-for result.

## Decisions inside this one

**The key holds the cap the run actually used, not how the user asked for
it.** Typing "64" and picking Auto (raise the cap until the score stops
improving) that settles at 64 give identical paths. The only thing Auto adds is
"I also tried the next rung and nothing improved." We judged that not worth a
second row or a label. Imported pre-1.6 records could not have told us anyway.

**Auto is satisfied by the chart's newest record above 4.** Records made by
the current app version win over stale ones, then the most recently written
one wins. If nothing above 4 exists, the ladder runs. This is the rule behind
"only analyze again if there is no record for the current settings".

1.6.0 shipped with "the highest cap wins". That rule had a hole: a chart with
an old tall row (an imported uncapped result, or a what-if the user typed)
could never show a fresh Auto run that settled lower. The user pressed
Analyze, the ladder settled at 64, the store kept the 64-bar row, and the view
went on showing the 1728-bar row with its stale depth and ms settings. Newest
wins means an Auto run always becomes the Auto answer; the taller row stays
for an explicit lookup at its cap. *(Refined by ADR-0009: Auto now also
requires the row's ms limit and score range to match the current settings;
version-then-newest breaks ties within that.)*

**The ms limit and score range do not key a record.** They are stored on it
and shown, as before; re-analyzing with a different value overwrites. Only the
cap changes a record's identity. *(Superseded by ADR-0009: the ms limit and
score range now key a record too.)*

## What this costs

An Auto re-run that settles lower than an existing row leaves the taller row
behind, reachable only by typing its cap. Harmless, but it is there.

Every lookup must say which cap it wants. The leaderboard comparison is pinned
to 4 bars and refuses to run otherwise, because the leaderboard plays by Clone
Hero's rules.

The table had to be rebuilt once (SQLite cannot change a primary key in
place). The cap is read from each stored blob's header, which has carried it
since the first C++ release.

## Alternatives rejected

Keying by the requested mode ("auto" vs a number) was exact but stored the
same paths twice and could not place imported records. Not keying at all and
treating the cap like the ms limit was the smallest change and threw away
minutes-long results.
