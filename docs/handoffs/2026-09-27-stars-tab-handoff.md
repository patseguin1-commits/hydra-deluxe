# Handoff: Stars tab

## Where things stand

The user wants a new "Stars" tab in the song details window, right after "Dynamics". It shows the exact score Clone Hero needs for 1 through 7 stars on that song. For songs with a drum solo, it also shows the total solo bonus separately, because the game doesn't count the solo bonus toward stars.

The design is approved, and the plan is written. No code has been written yet.

- Plan: `docs/superpowers/plans/2026-09-27-stars-tab.md`. It has three tasks, with the full code and tests for each.
- Tasks file: the same path plus `.tasks.json`. All three tasks are pending.

The plan and this handoff are **not committed**. Commit them first on `hydra-test`, so any worktree you start forks from a checkout that has them. There is also an untracked `docs/handoffs/2026-09-26-ch-probe-live-runs-handoff.md` from another session. Leave it alone.

Nothing is running in the background, and no worktrees were made for this work.

## The facts behind it

This session decompiled Clone Hero v1.1.0.6142 to find the star math. The plan's section "Where the math comes from" has the whole story. In short:

1. A star's cutoff is the chart's base score times 0.1, 0.5, 1.0, 2.0, 2.8, 3.6 or 4.4, rounded up. The multiply happens in 32-bit float.
2. You get the star when your score *without the solo bonus* is at or above the cutoff. The game stops counting at 7 stars. Seven is a literal cap in its code.
3. The base score is 50 per gem, 65 per cymbal, doubled for ghosts and accents. Hydra already adds this up. It is the same denominator `Path::avg_mult()` uses.

The official Clone Hero wiki and CHOpt's source agree on 3 to 7 stars. The 1-star and 2-star values (0.1 and 0.5) come only from the decompile. The memory file `ch-star-cutoffs.md` has the details, including how to re-dump the game after an update.

One correction to something said in chat. The 32-bit float step never changes a cutoff for drum charts in practice. Drum base scores are multiples of 5, and for those the float and exact results agree well past any real chart size. It only shows on large, odd base scores. At a base of 786,437, the 6-star cutoff is 2,831,173 in the game and 2,831,174 in exact math. The plan pins that case in a unit test and copies the game anyway.

## Decisions already made

Don't re-ask these. They're recorded in the plan's "User decisions" block.

- The tab goes after Dynamics and is called "Stars".
- The solo bonus gets its own line above the table, plus a "With full solo bonus" column showing each cutoff plus the whole solo bonus. That column is what the score on screen has to reach if you also get the full solo bonus. Songs without a drum solo show neither.
- Every number comes from the analyzed record, with no new chart reading. The math lives in a new `src/core/stars.{h,cpp}`, and the tab only draws it.

## How to run it

The tasks run in order. Task 2 uses Task 1's module, and Task 3 needs Task 2's build. The whole plan is small, so one implementer per task is plenty.

Task 1 is the math and a small cleanup: `Path::avg_mult()` gets its base score from a new `chart_base_score()`, so the sum lives in one place. Its unit tests pin exact numbers.

Task 2 is the tab and a GUI test. The test uses two charts from `testdata/input`. The first is "87" by Polyphia, which has a solo in its Expert drums. The second is "I'm A Believer (The Monkees cover)" by Smash Mouth, whose only solo is on guitar, so its drums have none.

Task 3 is the user's own look. Build `Hydra.exe` (`.\build_cpp.ps1 -Target Hydra`; the test targets don't rebuild it) and check that its timestamp is after Task 2's commit. Then hand it to the user. The dev build reads `hydra.db` from its own folder, and records from an older build show as Stale. The user may need to re-analyze a song before the tab shows numbers.

Build and test one target at a time. Overlapping builds have caused GUI-test timeouts before.

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
.\build-cpp\Release\hydra_uitest.exe --all
```

## Rules the hooks enforce

Every agent prompt needs these:

- **Status lines:** every 10 tool calls and every 5 minutes, append "HH:MM done ... | next: ..." to `C:\Users\Patrick\.claude\hooks\state\status\<agent id>.md`. Read-only reviewers too.
- **Commit trailers:** every commit carries `Task:`, `Agent:`, `Session:` and the Co-Authored-By line. Never amend, rebase or reset.
- **Merges:** use `git merge --no-ff <branch> -m "...trailers..."`. The trailer hook rejects `-F`.

## Loose ends, not part of this work

- **Other unmerged worktrees:** the drain-box handoff (`docs/handoffs/2026-09-27-drain-box-revision-handoff.md`) lists them: `objective-maxwell-56ed0c` (`64981da`) and `adoring-hopper-b9c009` (`a301d0b`). Neither touches the files this plan changes.
- **One open question for later:** on 4-lane (non-pro) drums, the game's code would pay cymbal and ghost/accent points if the notes were marked. Whether 4-lane charts ever mark them can't be seen in the code alone. Hydra already scores this its own way, and this plan doesn't change that.
- **Pushing:** nothing has been pushed.
