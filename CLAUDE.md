Ask questions if something is unclear.

## How to explain things

- Lead with the plain-English version. One idea per sentence.
- No jargon without a one-line gloss right after it.
- Cut throat-clearing ("It's worth noting", "In order to", "It's important to note"). Say the thing.
- Prefer short sentences. Avoid comma-spliced 40-word run-ons.
- When I ask "why", give the reason, not a feature tour.
- If a concept is genuinely hard, use a concrete analogy before the precise definition.
- This applies to plans and design docs too — no dense nested-bullet walls; explain each step in one plain line.

**Before showing any plan, design doc, or long answer: stop and check if it looks like the BAD example below. If it does, rewrite it as the GOOD example before showing it to me.**

BAD (what I keep doing — a research-note dump, not a deliverable):
```
- Fix squeeze scaling
  - src/core/squeeze_rating.cpp:142 — anchor recalculated on wrong node
  - src/core/squeeze_rating.h:30 — add `deact_index` field
  - src/app/report.cpp:210 — update caller to pass new field
  - tests/test_squeeze_rating.cpp — add regression test
```

GOOD (what I actually want — same information, said like I'm explaining it out loud):
```
The anchor is being recalculated on the wrong node, which throws off the
scaling. The fix is in squeeze_rating.cpp: track the deactivation node instead
(squeeze_rating.h needs a new field for that), then update the one caller in
report.cpp that uses it. I'll add a regression test so this doesn't regress
again.
```

Same content, no bullet wall, no isolated file:line list with no sentences around it. If you're about to write something that looks like the BAD example, that's the signal to rewrite — not a feeling, an actual pattern check.

## Agent skills

### Issue tracker

Issues live as GitHub issues in `splax77/hydra-deluxe`, managed with the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

The five canonical triage roles, each label string equal to its name. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `CONTEXT.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.

### GUI testing

To verify anything in the GUI, run `hydra_uitest` (headless, drives the real UI by widget label, returns text) instead of launching the app and taking screenshots. See `docs/agents/ui-testing.md`.
