# Handoff: Preview score box and finer time control (2026-09-25)

## Where things stand

No code has changed. Nothing is committed. No agents or background jobs are running.

A draft plan exists at `docs/superpowers/plans/2026-09-25-preview-score-and-stepping.md`. **The user has not approved it.** They said they never gave permission to start the plan. The previous session's notes recorded their message as "create the plan", but the user's word stands. Treat the plan as an unreviewed draft. Do not execute it, dispatch agents for it, or write its `.tasks.json` until the user says so.

**First move next session:** ask the user what they want. The options are to review the draft plan together, go back over the design, or delete the draft. Don't start any of these without a yes.

## What the user asked for

Two additions to the Preview tab.

The first is a score box that shows the running score as the notes come in.

The second is finer time control. Buttons go back or forward 5 seconds, mapped to the Left and Right arrow keys. Other buttons go back or forward one tick, mapped to comma and period.

The original instruction was to plan the changes, ask about anything that needs judgement, and make a mock design before changing any code. The questions were asked and answered, and a mockup was shown in chat. I don't have a record of where the mockup file lives, if it was saved at all.

## Decisions the user made

- A tick step is exactly one chart tick.
- The box shows the score, the multiplier and the combo.
- The solo bonus counts like the game does: the whole bonus lands on the solo's last note.
- The box is hidden when the chart has no analyzed path.

## The design as presented

The score isn't worked out again from scratch. It comes from the existing replay (`replay_path` in `src/core/replay.h`), which is already proven to match the engine's totals on every corpus path. The replay runs once whenever the Preview scene is built.

The box only shows a number when the replay provably matches the path. Every activation must have become a Star Power window, and the replay's total must equal the stored one. Otherwise it reads "Score unavailable".

The box shows the last chord at or before the playhead. The multiplier doubles while the playhead is inside a Star Power window.

A 5-second jump keeps playing if it was playing, and stops at the song's start or end. A tick step pauses first. Each press changes the time box's `[measure:beat:tick]` by exactly one.

The keys only act while the Preview is showing and no text field has the keyboard. The Preview takes Left and Right away from ImGui's keyboard navigation so an arrow press doesn't also move focus.

The box sits under the time box in the same see-through dark panel. The score is in large type, with "x4 · combo 42" on a smaller grey line below. The transport row becomes `-5s`, `< Tick`, `Play`, `Tick >`, `+5s`. Each button's hover hint names its key.

## What the draft plan contains

There are five tasks, run in order.

1. The replay keeps the combo after each chord (a new `combo_after` field), so the view never adds up note counts itself.
2. The Preview scene carries the running score, and a `build_score_box` function turns it into the box's text.
3. A `step_tick_ms` function works out where "one tick away" is.
4. The controller gains jump, step and score calls. The user's scoring rules are passed to every scene build, so a custom `hydra_rules.ini` doesn't read as "Score unavailable".
5. The panel gets the buttons, the keys and the drawn box.

## What I checked against the source, and what I didn't

These parts of the draft match the real code:

- The time box format, including the "[1:3:288] / [3:3:000]" example.
- The test fixtures' helper names and how they build a song.
- The controller's existing methods.
- The replay's fields, and `replay_path`'s default rules argument.
- The variable names in the panel's time-box drawing code.
- Where `group_thousands` lives: `src/core/model.h`.

These parts I haven't checked yet:

- That this ImGui version has the owner-aware `IsKeyPressed` and `SetKeyOwner`.
- That the test engine has `ctx->KeyPress`.
- The exact signature of the `hint()` tooltip helper.
- What `windows_for_path` does with an activation that has no stored deactivation node: skip it, or throw.

## Known fixes the draft still needs

The Task 4 GUI test waits a fixed two frames for the Preview to pick up the newly analyzed path. It should wait until the score box is shown instead (`wait_until`), and wait for loading to finish rather than assert it already has.

"What we're building" says older records "from before blob v6" lack a deactivation node. The memory notes say `deact_tick` arrived in blob v4. Reword it to "older records" or confirm the version.

The `.tasks.json` next to the plan was never written.

## Working tree

The user has uncommitted files that aren't part of this work: `docs/cap-clamped-squeeze-frontend-anchor.md`, the scratch `.txt` files at the repo root, `docs/audit/`, other files in `docs/handoffs/`, `scratch_ms/`, `srb_audio_dump/` and `srb_songs/`. Never stage, revert, stash or delete them.

The only files this work created are the draft plan and this handoff.
