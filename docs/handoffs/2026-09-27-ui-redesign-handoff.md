# Handoff: Interface redesign

## Where things stand

The user asked for a full audit of Hydra's interface, then approved a redesign built from it. The design is a mockup on a canvas, approved screen by screen. The implementation plan is written. No code has been changed.

The plan is `docs/superpowers/plans/2026-09-27-ui-redesign.md`. It has sixteen tasks: Task 0, Tasks 1 to 14, and Task 15. Each has its full steps. The tasks file sits beside it (`.md.tasks.json`), with every task pending. The design spec is `docs/superpowers/specs/2026-09-27-ui-redesign-design.md`: the whole design in prose, screen by screen. The mockup lives at https://claude.ai/artifact/TRLProeKnWPrDmmD1dtacM, and its five screens are already copied into `docs/superpowers/specs/2026-09-27-ui-redesign-mockup/`.

None of these files are committed. Task 0 commits them first, so every worktree forks from a checkout that has them. There is also an untracked `docs/handoffs/2026-09-26-ch-probe-live-runs-handoff.md` from another session. Leave it alone.

Nothing is running. No worktrees exist for this work. Nothing has been pushed.

## What the redesign does, in short

The song details stop being a blocking window and become a panel beside the library. All six analysis settings move into one bar on the main screen, and SP cap loses Auto. Batch analysis runs in a strip at the top while you keep browsing. The library gets sorting, filter chips, a scroll bar, and a search that understands quotes, fields and a couple of filters. The Paths tab shows every activation as one line each, with details folded, and a timeline. The Preview names its path and jumps between activations. Reports move to Documents\Hydra and get fixed. The plan's "What you'll see when it's done" has the full picture.

## Decisions already made

Don't re-ask these. The plan's "User decisions" block quotes each one.

The details become a docked panel, and the mockup came first and is approved. Batch runs in the background. All analysis settings move to the main screen. Reports save to Documents\Hydra. Auto is removed completely, SP cap defaults to 4, and results saved under Auto are deleted on the first start after the update. The activation list, the search upgrades and the rest of the audit's recommendations go ahead. `stars:N` means exactly N stars. An old path report is left where it is. It runs as a workflow: parallel agents in worktrees, merged by the main session.

## Calls made during planning

The plan lists them under "Calls I made myself", and the user accepted all of them ("the rest sounds good"). Closing the panel no longer cancels an analysis. Old `hydra_rules.ini` Auto keys are ignored rather than an error. Results saved before the update show no Paths timeline until that song is analyzed again. The all-0 path reads `(0 ms limit)`, and the transport buttons keep their labels. The user also settled the last two questions: `stars:N` means exactly N, and an old path report stays where it is. No questions are open; start with Task 0.

## How to run it

Start with Task 0 in the main session: commit, check the mockup copy, record the baselines. Then run the four waves as the plan's "Running it as a workflow" describes. The main session makes each wave's worktrees, launches one Workflow run per wave, merges in task order, and makes the join edits in "The merge checklist". Then it builds and tests before the next wave starts. Task 15 is the final check, and it ends with the user trying the new Hydra.exe.

Wave 1 is T1 split files, T2 search module, T3 reports, T4 batch job and T5 window placement. Wave 2 is T6 remove Auto, T7 store summaries and song length, and T8 view data. Wave 3 is T9 layout and panel, T10 Paths tab, T11 Preview tab, T12 library table and T13 batch UI. Wave 4 is T14 docs.

## Rules the hooks enforce

Every agent prompt needs these:

- **Status lines:** every 10 tool calls and every 5 minutes, append "HH:MM done ... | next: ..." to `C:\Users\Patrick\.claude\hooks\state\status\<agent id>.md` with `status_append.ps1`. Read-only reviewers too.
- **Commit trailers:** every commit carries `Task:`, `Agent:`, `Session:` and the Co-Authored-By line. Never amend, rebase or reset.
- **Merges:** use `git merge --no-ff <branch> -m "...trailers..."`. The trailer hook rejects `-F`.
- **Big deletions:** the deletion gate denies an Edit that keeps under 40% of its block. Write the new file to `<file>.new` and move it over.
- **Workflow scripts:** a sequential loop needs a `// serial because: <reason>` comment. A failed review needs a follow-up agent, not a `break`. Parallel tasks need their own worktrees.

## Things to watch

Build and test one heavy job at a time on the shared machine. Overlapping builds have caused GUI-test timeouts before, so the plan caps concurrent builds at four inside a wave.

`hydra_batch.exe` with no arguments analyzes the whole library next to the exe. Always pass `--db` and a folder.

Two branches can merge with no conflict and still not compile together. Wave 3 is where this bites: T12 replaces the paged library that T9, T11 and T13 were written against. The merge checklist lists the known joins. Build the whole wave after merging, before the next wave.

T12's reviewer will see edits in `library_view.cpp`, `library_toolbar.cpp` and `library_dialogs.cpp`. Those are temporary compile fixes, dropped at merge, so its scope check should accept them.

The dev build's `hydra.db` (next to `build-cpp\Release\Hydra.exe`) may hold Auto results. Nobody has counted them. The first run of the new build deletes any it has, which is what the user chose. The installed database under Program Files has none. Never point a test at that database; Task 7 only copies it for a timing check.

One thing is outside this plan on purpose. Closing Hydra while a cancelled leaderboard download is still waiting can pause the exit for up to two minutes. The fix belongs in `src/net/dmbot_client.cpp`, and it was filed as its own follow-up task.
