Audit the whole Hydra project for duplicated derivations, drift between them, and assumptions
nobody wrote down or justified. This is a read-only audit. Do not change any source, test or
build file. The deliverable is a findings document. Every fix waits for my yes.

## Why

Read `docs/handoffs/2026-09-24-backend-scoring-drift.md` first. In short: the details display
told me a squeezed-out note cost 200 points when the search had counted it as 0. The search,
the replay and the display each wrote their own version of "what does this backend row score",
and one of them drifted. My rule going forward: every fact, rule or calculation is derived in
exactly one place in the project. Every other module calls that one place. It never
re-derives the fact, copies the formula, or keeps a parallel constant.

## What counts as a finding

**Duplicate derivation.** The same question is answered in two or more places by separate
code. The copies count even if they agree today. Examples: two functions that each decide
whether a note is inside an SP window, a formula copied between the engine and a view, two
constants for the same epsilon, a test helper that recomputes what production code computes
instead of calling it. Wrappers that call the single owner are fine.

**Drift.** Two copies that give different answers for some input. Show the input.

**Undocumented or unjustified assumption.** A threshold, magic number, epsilon, ordering
rule, fallback or special case with no stated reason. This includes reasons that point at
code that no longer exists; for example "Mirrors hydata..." or "Mirrors hydra_app.py..."
comments, since the Python is deleted. A reason in a nearby comment, an ADR in `docs/adr/`, or
`CONTEXT.md` counts as documented. Say which one covers it.

**Display re-derivation.** Any UI, report or CLI output that computes a game fact itself
instead of reading what the engine stored or calling the engine's own rule. The engine is the
single source of truth for scoring facts.

## Scope

Everything under `src/` (app, audio, cli, core, image, net, parse, render, search, store, ui),
`tests/`, and `tools/`. Also the docs that make claims about behavior: `CONTEXT.md`,
`docs/adr/`, `docs/UserGuide.md`, `docs/cap-clamped-squeeze-frontend-anchor.md`. Skip
`third_party/`.

Start with the leads the handoff already names. These are the backend-row pricing (engine,
replay, display), the three different "is this the squeezed-out note" tests (tick equality,
`is_sqout_backend`'s 0.01 ms, `kSameNoteMs`), `display_backends`, and `summarystr`'s
"uncounted" labels. Then sweep the rest.

## How to run it

Use a workflow and fan out by module group, keeping it under 10 agents. Each agent reads its
modules and lists candidate findings with the exact code locations of every copy. Then run a
separate verification pass: a different agent checks each candidate against the code. A
finding survives only if the verifier confirms both copies exist and says whether they agree.
For a drift claim, the verifier must produce the concrete input where they disagree. Reading
is the default. Running `hydra_tests`, `hydra_replay` or `hydra_batch` to prove a
disagreement is fine. Do not edit anything to do it.

Rules the agents must follow. A grep that doesn't find something does not prove it's absent;
say "not found where I looked" instead. Never call existing code wrong without checking ground
truth (the engine, a test, or a run). Every song, file or function name must come from the
repo, not memory.

## The deliverable

Write `docs/audit/2026-09-24-derivation-audit.md`, then show it to me. Follow the "How to
explain things" rules in `CLAUDE.md`: plain English, one idea per sentence, no bullet walls of
bare file:line references. Each finding gets a short paragraph that covers:

- what question is being answered
- every place that answers it
- whether they agree today, and the input where they don't
- what a user would see if they drifted
- which module should own it, and why

Group findings by kind: drift first, then duplicates that agree today, then undocumented
assumptions. Within each group, rank by how visible the effect is to a user. End with a short
proposed order of fixes. Present it as a proposal, not a plan in motion. Wait for my
approval before touching code.
