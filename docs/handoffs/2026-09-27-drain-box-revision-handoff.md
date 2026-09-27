# Handoff: SP drain box revision (Tasks 4 to 7)

## Where things stand

The Preview has an SP drain box. It sits beside the SP gauge and shows how long one bar of Star Power lasts at the playhead, then either "empties in X s" (gold, while the path has SP running) or "full meter X s" (grey, if you activated here). Tasks 1 and 2 of the plan built it. They are merged into `hydra-test` at `c072f11`, and the unit suite (451 cases) and all 24 GUI tests passed on the merged code.

The user tried it on "One [Metallica]" (Periphery) and asked for three changes. The spec and plan now describe them, committed at `55928de`:

1. "full meter" should jump when the section changes. Today it glides, because it measures eight measures ahead and that chart changes time signature twenty times.
2. The time box gets a new line under BPM, reading "Time signature: 6/4".
3. In a narrow window the drain box sat on the highway. All three text boxes (time, score, drain) should share one scale that keeps them beside the highway. The drain box moves to the top-right, where the highway is narrowest.

Your job is to run Tasks 4, 5 and 6 as a workflow, merge them here, and hand Task 7 (a look in the real app) to the user.

- Spec: `docs/superpowers/specs/2026-09-27-preview-sp-drain-box-design.md`. Read the section "Revision after the first look".
- Plan: `docs/superpowers/plans/2026-09-27-preview-sp-drain-box.md`. Tasks 4 to 7 and their "Revision constraints" and "User decisions" blocks.
- Tasks file: the same path plus `.tasks.json`. Tasks 1 to 3 are marked completed.

Every decision is already made and recorded in the plan. Don't re-ask them. That includes the 60% floor, overlapping below it, the top-right move, and the label "Time signature:".

## How to run it

The user asked for a workflow with the merges done in the main session. The one that built Tasks 1 and 2 worked well, and its script is a good starting point:

`C:\Users\Patrick\.claude\projects\C--Users-Patrick-Downloads-Hydra-hydra-test\5e6160cc-2d69-4996-97f3-561759ed2843\workflows\scripts\sp-drain-box-wf_6716a077-f5b.js`

It ran one implementer per task in its own worktree (`isolation: 'worktree'`). Each implementer then went straight to a read-only reviewer, in a `pipeline`. The reviewer checked four things: the files touched, a line-by-line match with the plan's code, the value sources, and whether any test's expected value had changed.

The shape this time:

- **Task 4 and Task 5 start together**, both from `55928de`. Each agent runs `git checkout -B drain/T4 55928de` (or `drain/T5`) first. The worktrees can fork from a stale main, so this step matters.
- **Task 6 starts from Task 5's branch**, with `git checkout -B drain/T6 drain/T5`. It redraws the time box that Task 5 adds a line to. Start it only after Task 5's reviewer approves, for example by making it a later stage of Task 5's pipeline item.
- **The reviewers' allowed-file lists** are each task's **Files** block. For Task 6, the diff against `drain/T5` should also show Task 5's files, and that's expected.
- **Task 6's reviewer should also check two things:** `track_height` is the only place the track height is worked out, and the renderer's `resize` calls it. And the drain box is anchored to the top (`origin.y + v_margin`).

Rules the hooks enforce, which every agent prompt needs:

- **Status lines:** every 10 tool calls and every 5 minutes, append "HH:MM done ... | next: ..." to `C:\Users\Patrick\.claude\hooks\state\status\<agent id>.md`. Read-only reviewers too.
- **Commit trailers:** every commit carries `Task:`, `Agent:`, `Session:` and the Co-Authored-By line. Never amend, rebase or reset.
- **Merges here:** use `git merge --no-ff drain/T4 -m "...trailers..."`. The trailer hook rejects `-F` because it can't read a file.

## Merging and checking

Merge in this order: `drain/T4`, then `drain/T5`, then `drain/T6`. T6 contains T5, so merging T5 first keeps the history readable.

Tasks 4 and 5 both touch `src/app/preview_view.{h,cpp}` and `tests/test_preview_view.cpp`, in different places. The plan tells each one exactly where to put its tests so they don't collide. If a conflict still happens, it will be two separate additions side by side. Keep both.

After merging, build and run everything in the main checkout, one build at a time:

```bash
.\build_cpp.ps1 -Target hydra_tests
```

```bash
.\build-cpp\Release\hydra_tests.exe
```

```bash
.\build_cpp.ps1 -Target hydra_uitest
```

```bash
.\build-cpp\Release\hydra_uitest.exe --all --keep-temp
```

Last time, one full GUI run inside a worktree had 8 timeouts while another worktree was building at the same time. Two quiet reruns passed all 24. If you see failures while builds overlap, rerun on a quiet machine before you conclude anything, and say which run was which.

Look at the screenshots yourself with the Read tool: `overlay-fit-wide.png`, `overlay-fit-narrow.png` and `drain-box-active.png`. Check that the drain box is top-right, the "Time signature:" line is under BPM, and the narrow frame is readable and clear of the lanes.

## Before handing Task 7 to the user

Build the app itself. The test targets don't rebuild it, and last time I sent the user to an exe from the day before:

```bash
.\build_cpp.ps1 -Target Hydra
```

Then check that `build-cpp\Release\Hydra.exe`'s timestamp is from after the merge.

The dev build reads `hydra_settings.ini` and `hydra.db` from its own folder. With the user's OK, both were copied (hash-checked) from `C:\Program Files\Hydra`, and the empty library the dev build had made is kept as `hydra.db.empty-2026-09-27`. That copy came from the installed app, which is dated 9/25, before the audit fixes changed the record format. So the dev build will probably show those charts' results as Stale. If the Preview has no path overlay on the test chart, have the user (or the GUI) re-analyze that one chart first.

## Loose ends, not part of this work

- **Old worktrees:** Tasks 1 and 2's worktrees are removed. Their merged branches `drain/T1` and `drain/T2` are still there.
- **A separate session:** worktree `objective-maxwell-56ed0c` (session `bc9f5a7b`) has commit `64981da`, not yet merged into `hydra-test`. It removes `SongTiming::sp_end_ms` and other port-era dead code. It also touches `src/render/preview_renderer.{h,cpp}` (it removes `width()`/`height()`), and Task 6 edits `preview_renderer.cpp`'s `resize`. Whichever merges second may need a small conflict resolved there. Keep both changes.
- **An older unmerged fix:** worktree `adoring-hopper-b9c009` has commit `a301d0b` (9/25), "Read command-line arguments as UTF-8 in every entry point". It was never merged into `hydra-test`, and its `utf8_argv()` helper isn't there. Don't remove that worktree until the user decides what to do with it.
- **Pushing:** nothing has been pushed.
