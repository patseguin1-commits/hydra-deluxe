# Stored data has one set of version stamps, and none is the app version

Every row Hydra saves was computed by some build. Before a build shows a
row, it asks: would I have computed the same thing? Until 1.8.4 that
question was answered four different ways, in three files.

Saved results were stamped with the app version from CMakeLists.txt, so
every release made the whole library Stale, even one that changed no
analysis. 1.8.3 added only the Stars tab, and 18,755 results from 1.8.2
still went Stale on upgrade. The path layout had two numbers (one for the
structure blob, one for the nodes) that a comment said to bump together by
hand. Dynamics counts had their own counting number in
`dynamics_breakdown.h`, and their own layout byte in `dynamics_breakdown.cpp`.
Each was compared in its own place, with its own code.

## The decision

`src/store/stored_versions.h` holds every stamp on stored, computed data,
each with a note saying what change bumps it. Each stamp is a `StampRule`:
the stamp this build writes, and the list of stamps it reads as current.
`StampRule::is_current` is the only place a stored stamp is compared. The
store's SQL version of the results check is built from the same lists.

- `kResultsStamp`: the analysis version. It changes only when analysis
  output changes in a way the path format and the rules fingerprint don't
  catch. When it changes it is set to the version of the release that ships
  the change. It is "1.8.2", and "1.8.3" is also accepted, because 1.8.3
  stamped its app version on results identical to 1.8.2's.
- `kPathFormatStamp`: one number for the structure blob and the nodes it
  points at. A node is only ever read through its structure, so one number
  covers both. Its value stays 6, so no stored byte changes.
- `kDynamicsCountStamp` and `kDynamicsBlobStamp`: how notes are counted, and
  the count blob's layout.

Dynamics keeps its own counting stamp instead of sharing the results stamp.
A counting change then costs only a background recount, not a re-analysis
of the whole library (user decision 2026-09-27). The two kinds of data also
fail differently, on purpose. A stale result reads Stale and asks the user
to re-analyze. A stale count reads as missing and is recounted in
milliseconds.

## Consequences

A release that changes only the UI, the reports or the tools keeps every
saved row. Users re-analyze only when the numbers could differ.

The cost is a rule someone has to remember. A change to the engine, the
scoring or what a record holds that leaves the path format and the rules
alone must bump `kResultsStamp`, or old results keep reading Ready when they
are wrong. The app version used to force that bump on every release. Check
it at every release: if anything under `src/search`, the scoring in
`src/core`, or the record contents changed since the last bump, bump it and
shrink its `accepted` list to the new stamp alone.
