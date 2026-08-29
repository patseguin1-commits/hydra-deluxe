# Analysis settings key a record; paths are stored once

A record is now keyed by chart, chart mode, SP cap, ms limit, and score
range — the full settings the analysis ran with. Records accumulate: one row
per settings combination, and re-running a combination replaces only its own
row. Underneath, the paths themselves are stored once each, identified by a
fingerprint of their content, and every record that kept a path points at the
same stored copy.

This supersedes the last clause of ADR-0003 ("the ms limit and score range do
not key a record").

## Why

With the ms limit outside the key, changing it and re-analyzing overwrote the
old result, and a batch run skipped charts whose stored record ran under a
different limit. Changing a setting quietly threw away paid-for work — the
same pain that put the SP cap into the key in 1.6, one setting over.

The first design was cleverer: prove that an existing record answers a new
limit (a 100ms record answering a 25ms request) and skip the search. Tracing
the engine killed it. The limit changes the pruning at every step of the
search, in both directions: two runs at different limits can each keep paths
the other throws away, and the effect can reach the best path itself. There
is no cheap proof; the only honest way to know the 25ms answer is to run it
once. So the feature became: never lose a result, never store a path twice.

## Decisions inside this one

**A path's identity is its content, not its notation.** Two different paths
can print the same notation string (the display code already guards against
exactly that), so the fingerprint hashes the path's stored bytes. Sharing is
safe because everything tree-contextual about a stored path — tie counts,
variant tails, a variant's displayed scores — is rebuilt from scratch every
time a record loads; the flat payload is the whole truth.

**"Limit off" is one setting, not many.** A disabled ms limit stores value
zero whatever the greyed-out box says, because the engine behaves identically
either way. Two lensings that cannot differ must not be two keys.

**Auto cap stops guessing.** Auto used to take the chart's newest row above 4
bars and hope it ran under the current ms and depth settings — a heuristic
ADR-0003 adopted because the settings were invisible. Now they are columns,
so Auto matches them exactly: rows above 4 bars with the same ms limit and
score range, current version first, newest first.

**Results survive across settings, not across versions.** When a new Hydra
version re-analyzes a chart and mode, that chart's old-version rows are
deleted. Two versions can silently disagree about scores; mixing their paths
in one pool would be poison.

**Old rows migrate as stale, not as answers.** Rows written before this
schema (including legacy uncapped imports) carry unknown settings, so they
read Stale — the same thing users see after any release — and are replaced
on re-analysis. The uncapped import used to restamp rows to Ready; it no
longer can, because claiming a settings match we cannot verify is the exact
bug this change removes.

## What this costs

No compute is saved across limits: a never-analyzed combination always costs
a full run. The savings are storage (shared paths) and never re-paying for a
combination already run.

Every lookup and report now says which settings it wants, not just which
cap. One more visible truth: changing the ms limit or score range flips a
viewed record to "not analyzed" until that combination has run — the status
finally tracks the settings on screen instead of pretending.

The single-blob row became three tables (results, paths, references), and
deleting a result must garbage-collect paths nothing references anymore.

## Alternatives rejected

Proof-based reuse across limits: disproven by the engine's pruning, above.
Reworking the engine to search once per cap and treat the limit as a pure
display filter: would have changed what limited views show, and unchanged
user-visible behavior was a hard requirement. Content-addressing whole
records instead of paths: record bytes are settings-dependent by
construction (the header carries the limit), so identical path sets would
still store twice.
