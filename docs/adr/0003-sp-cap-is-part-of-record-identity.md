# The SP cap is part of a record's identity

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

**Auto is satisfied by the chart's highest record above 4.** Records made by
the current app version win over stale ones, then the highest cap wins. If
nothing above 4 exists, the ladder runs. This is the rule behind "only analyze
again if there is no record for the current settings".

**The ms limit and score range do not key a record.** They are stored on it
and shown, as before; re-analyzing with a different value overwrites. Only the
cap changes a record's identity.

## What this costs

An Auto re-run that settles lower than an existing row leaves an unused row
behind: the higher one still wins. Harmless, but it is there.

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
