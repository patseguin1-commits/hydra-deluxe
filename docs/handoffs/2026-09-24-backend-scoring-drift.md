# Handoff: the squeezed-out backend row disagrees with the score (2026-09-24)

## What the user reported

On Round and Round (Ratt, Expert Pro Drums, 4-bar SP cap, 10 ms path limit), the second
activation's backend table has this row:

```
480.0  [ RY ]  260  Free SqOut <-- squeezed out (-200)
```

The optimal path does not count that chord under Star Power at all. The table makes it look
like the path banks 260 and gives up 200. The real figure is 0 either way. The user called
this "unacceptable drift": the display uses a different rule from the scoring.

## What is wrong, in one paragraph

The search and the details display each decide separately what a backend row is worth, and
they use different rules. The search counts a backend only if it sits at or before the SP
end, or less than 3 ms after it (the "leeway"). The display never checks where the row sits.
For any squeezed-out row it prints the reduced value and "full minus reduced" as a loss. That
is right for a note at or before the SP end and wrong for anything past the leeway. Nothing
kept them in step because the scoring rule has no function of its own. It is written as
inline branches inside the engine, so the display had nothing to call.

## How each place counts it today (read from the code, not re-run)

**The two stored numbers.** When the graph is built, every chord gets a full SP value
("points") and a reduced one ("sqout_points", the full value minus the first note's share).
For [RY] at 4x that is 460 and 260. This happens in `src/search/graph.cpp` around line 129,
using `sqout_reduction` from `src/core/scoring.cpp` around line 68.

**The search.** `create_deactivated_path` in `src/search/engine.cpp` (lines 566-597) prices
the squeezed-out chord in one of three ways. At or before the SP end, the chord was already
counted at full value, so the squeeze-out takes back the difference (keeps 260, loses 200).
Less than 3 ms after the SP end, it adds the reduced value (260). Further out, it adds and
takes away nothing. The user's row is at +480 ms, so it is the third case: 0.

**The replay.** `src/core/replay.cpp` (lines 101-122) has its own hand-written copy of the
same window-plus-leeway rule. It agrees with the search: +480 ms scores 0. It is still a
second copy that could drift.

**The display.** `src/app/path_view.cpp` (lines 230-245) prints `sqout_points` in the Points
column and `points - sqout_points` as the loss for every squeezed-out row, regardless of
offset. This is the only one of the three that is wrong.

## How it got this way

The tag came from the original Python app (`hydra_app.py`, since deleted). That code printed
the flat formula and never looked at the offset. The C++ port copied the line as-is on
2026-08-15 (commit 3b69958). On 2026-08-22 (commit ba0885a) the 3 ms leeway became a shared
constant, `kBackendLeewayMs` in `src/core/model.h`. Its comment says it exists so "the price
and the label cannot drift apart." That fix covered the "Standard" edge in
`BackendSqueeze::summarystr` and missed the squeezed-out tag.

## Related leads spotted in passing (not verified as bugs)

These are the same kind of problem. A later session should check them, not assume them.

The engine spots the squeezed-out chord by exact tick (`be_tick == e.sqinout_time`). The
display spots it by comparing float offsets within 0.01 ms (`Activation::is_sqout_backend`
in `src/core/model.cpp`). The replay uses a third test, its own `kSameNoteMs = 0.01`
constant (`src/core/replay.cpp:23`). That is three ways of answering one question.

`Activation::display_backends` has its own "is this row past the squeezed-out note" test
using the same 0.01 ms epsilon.

`summarystr` labels rows "Hard (uncounted)" and "Insane (uncounted)" from the hit window and
the leeway constant. It is a separate judgement from whether the engine counted the row,
although today they share the leeway edge.

Four source files still justify behavior with "Mirrors hydata..." or "Mirrors
hydra_app.py..." comments. That Python code no longer exists in the repo, so those
justifications point at nothing.

## The fix that was proposed (not started, not approved)

Add one core function that answers "on this path, how much SP score does this backend row
add?" It returns 0, the reduced value or the full value, by exactly the search's three cases.
The engine, the replay and the display would all call it. The squeezed-out row would then
show 0 for the +480 ms case, and the "(-N)" tag would only appear when the squeeze-out
really costs points.

Two decisions are still open and belong to the user. First: should the engine's hot loop be
switched to the shared function too, or only the display and replay? If the engine moves,
the plan was to re-run a set of songs through `hydra_batch` and diff before and after to prove
the scores are identical. Second: what wording goes on the 0-point row? The candidates were
"(uncounted)" to match the existing labels, or "(0 pts)".

The user then widened the scope. They want a full-project audit for duplicated derivations,
drift and unjustified assumptions before any fix goes in. The prompt for that session is in
`2026-09-24-audit-prompt.md` next to this file.

## State of the working tree

No code from this session is left in the tree. A first attempt at the display fix plus a
test was written and then fully reverted after the user stopped it. The user's rule: explain
how it works today and get a yes before editing. The tree holds only the uncommitted changes
that were already there when the session started. Those are in
`docs/cap-clamped-squeeze-frontend-anchor.md`, `src/app/path_view.cpp` (the shortened "SP
overfilled" warning), `tests/test_path_view.cpp` and `tests/test_search.cpp`. Their origin is
an earlier session and they are not part of this work.

Nothing is in flight. The handoff hook's journal check found no workflow runs and no
unfinished subagents for this session.

The numbers above were not re-priced on the actual record. If proof on this song is wanted,
`hydra_replay score --path` can price the path with and without that chord.
