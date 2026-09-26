Carry out the derivation-fixes plan. Every decision is already made. Do not re-ask any of them.

## Where things are

The plan is `docs/superpowers/plans/2026-09-24-derivation-fixes.md`. It has 19 tasks, run in number order. A task list for the executor sits next to it, in `2026-09-24-derivation-fixes.md.tasks.json`. The plan's header lists my 28 decisions. If a question comes up that one of them answers, use the answer and keep going.

The plan grew out of the audit in `docs/audit/2026-09-24-derivation-audit.md`. The story behind the audit is in `docs/handoffs/2026-09-24-backend-scoring-drift.md`. Read the plan first. Read the other two only when a task sends you there.

Nothing has been built yet. Both repos are exactly as the audit left them.

## How to run it

Use superpowers-extended-cc:subagent-driven-development, or superpowers-extended-cc:executing-plans if I say so at the start. Give each task its own fresh subagent, and review its work before starting the next task.

Run it in the main checkout, on branch `hydra-test`. Do not use a worktree. The plan was written against four files I have not committed: src/app/path_view.cpp, tests/test_path_view.cpp, tests/test_search.cpp and docs/cap-clamped-squeeze-frontend-anchor.md. A worktree would not have my edits in them. Never revert, stash or overwrite those four files. Leave the loose scratch files in the repo root (d1.txt, e1.txt, err_*.txt and so on) alone too.

## Where the plan stops for me

Task 17 is a user gate. Before any code, stop and ask me to play Moonlight Haze "Lunaris" (Offset 0.25, delay 0) and one chart with a nonzero delay. I'll report which way Clone Hero applies the delay and which setting wins. Write no code for Task 17 until I answer.

Tasks 2 and 3 each have a speed limit. The collected-phrase bookkeeping in Task 2 may cost at most 3% of search time. The shared backend-pricing function in Task 3 may cost at most 5%. If either goes past its limit, stop and report the numbers. Do not build a workaround without asking.

If any check in Task 19 fails, stop and report. Do not patch it on the spot.

## Things that will bite if missed

Task 3 writes the baseline batch output to `$env:TEMP\hydra_task3\batch_sorted.txt` before it touches code. Tasks 9, 10 and 19 compare against that file. Do not delete it until Task 19 is done.

Tasks 1, 8 and 9 all edit `category_scores` in src/core/scoring.cpp, in that order. The code a later task quotes may already have been changed by an earlier task. When that happens, apply the same change to the new lines and keep what the earlier task added.

Task 8 also changes the separate video-tools repo at C:\Users\Patrick\Downloads\Hydra\video-tools. It has six edits I have not committed. Commit them as-is first, in a commit of their own. Then make the plan's change there in a separate commit. check_refs.py must still pass all 9 cases afterwards. check_refs runs the hydra_replay.exe built in this checkout's build-cpp\Release.

The GUI's build target is named `Hydra`, with a capital H. The other targets are lowercase.

## House rules the hooks enforce

Every agent brief carries the status rule. It writes a status line to C:\Users\Patrick\.claude\hooks\state\status\<agent id>.md every 10 tool calls or 5 minutes. Run workers in the foreground, never in the background and never with Monitor. Relay their status to me as one plain sentence per agent.

Every commit message ends with `Task:`, `Agent:` and `Session:` lines, then the Co-Authored-By line. Never amend, rebase or reset. Make source edits with the Edit tool.

Explain things to me the way CLAUDE.md asks: plain English, one idea per sentence, no walls of bare file:line bullets.

When the last task is done, show me the Task 19 report. That means the test counts, every line of the batch diff with its reason, and the check_refs summary line.
