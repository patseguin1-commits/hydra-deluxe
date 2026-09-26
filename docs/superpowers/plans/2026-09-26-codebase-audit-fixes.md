# Codebase Audit Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix every finding in the 2026-09-25 codebase audit: the real bugs, the dead code, the slow spots, the design problems, and the seven changes you approved.

**Architecture:** The work is split into 21 fleet tasks that each own a clear set of files, plus two tasks the main session does itself. Tasks that share no files run at the same time, each in its own git worktree (a second checkout of the repo in its own folder, on its own branch). The main session merges each wave into `hydra-test`, runs the whole test suite once, and then starts the next wave from the merged result. Three waves cover everything, because a few tasks have to build on another task's change to the same file.

**Tech Stack:** C++20, CMake via build_cpp.ps1, SQLite, Dear ImGui, doctest (hydra_tests), hydra_uitest for GUI checks, Python 3 and pytest for tools/ch_probe.

**Spec:** docs/audit/2026-09-25-codebase-audit.md. Background: CONTEXT.md, docs/adr/, and the previous fix plan docs/superpowers/plans/2026-09-24-derivation-fixes.md.

## How to read this plan

Each task opens with a plain paragraph: what is wrong today, what changes, and what you will see. Most say "nothing", because most of this plan is fixes you never notice until they go wrong. Then come the task's dependencies, the other tasks that touch the same files, the goal, the files, the acceptance checks, one verify command, and the steps. The steps are test-first: write the failing test, watch it fail, make the change, watch it pass, commit.

Task 0 and Task 22 are done by the main session, not by the fleet. Task 0 gets the repo ready and records the "before" numbers. Task 22 checks the finished whole.

Three things stop and wait for you. Task 0 asks you to confirm that whoever was editing ch_probe this morning is finished before your files get committed. Task 21 ends with a check you do at the game, because only a live run proves the probe runners attach. After that check, Task 21 asks whether three older ch_probe diagnostics can go, once their newer replacements have run at the game.

One more thing to know before merging wave 2: Task 10 changes the rules fingerprint's text and Task 12 changes the record format. Each alone would mark every record Stale. They land in the same wave, so you re-analyze once, not twice. Ship them in the same release.

## What you'll see when it's done

Almost everything in this plan is invisible. These are the only changes you will notice, and you approved each one.

The path report gains a Mode column, and its subtitle counts the rows on the page. Report song names follow song.ini the next time a song is scanned. The Preview uses the chart's audio offset for .sng and .srb charts too. On a PC with no audio device the Preview draws the highway with one "audio unavailable" line, instead of "Preview failed". A 1.5 or 1.6 database shows "Not analyzed" instead of "Stale". Editing the Auto time budget no longer marks records Stale, and editing the Auto ladder only marks Auto records Stale. Every record reads Stale once after upgrading, because the record format changes (Task 12), so the library needs one re-analysis.

## The waves

**Task 0 (main session):** commit your pending work, move the loose data out of the repo, and record the baselines.

**Wave 1, twelve tasks at once:** T1 store correctness, T2 engine cleanup, T3 batch runner, T4 CLI tools, T5 reports, T6 details window and Preview bugs, T7 library view, T8 audio, T14 build and repo, T15 vendored trim, T18 GUI test coverage, T19 ch_probe fixes.

**Wave 2, five tasks at once:** T9 store speed, T10 rules fingerprint, T11 Paths tab and Preview caching, T12 record format v7, T20 ch_probe restructure.

**Wave 3, four tasks at once:** T13 engine speed, T16 string helpers, T17 test-suite speed, T21 ch_probe runners.

**Task 22 (main session):** the final check.

A task waits for a later wave only when it edits the same code as an earlier task. T9 and T10 rewrite parts of the record store that T1 also changes. T12 changes the engine fields that T2 cleans up. T11 builds on T2's new walk over a path and on T6's details-window changes. T13 speeds up the engine loop that T12 reshapes. T16 and T17 touch dozens of files, so they go last, when nothing else is moving. The ch_probe tasks form their own chain, T19 then T20 then T21.

Some tasks in the same wave still touch the same file in different functions. Each task names these under "Expected overlaps", so the merger knows the conflict is expected and which side keeps what.

## Global Constraints

Code blocks quote the code as it is on disk when the plan was written. When an earlier wave already changed those lines, apply the same change to the new lines, and keep what the earlier task added.

Every task works in its own worktree, made by the workflow before the agent starts: `git worktree add C:\Users\Patrick\Downloads\Hydra\wt-T<n> -b audit/T<n> <base>`. `<base>` is the Task 0 commit for wave 1, and the merged `hydra-test` head for later waves. The agent works and commits only inside its worktree and never touches `hydra-test` itself. Only the main session merges.

Each worktree builds into its own `build-cpp` folder inside the worktree. So the first build in a worktree is a full build. Twelve full builds at once is heavy, so the workflow runs at most six builds at the same time (see "Running it as a workflow").

A task edits only the files it lists, plus test files it creates. It never deletes, moves or rewrites anything else, and never touches the user's data folders. The reviewer checks this with `git diff --stat <base>..audit/T<n>`, and any file outside the list fails the review.

Every commit message ends with four lines: `Task: <task name>`, `Agent: <executor>`, `Session: <session>`, and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never amend, rebase or reset. Source edits use the Edit tool, not patch scripts.

Build from the worktree root in PowerShell: `.\build_cpp.ps1 -Target <target>`. Unit tests: `.\build-cpp\Release\hydra_tests.exe`, optionally `-tc="<name>"`. GUI tests: `.\build_cpp.ps1 -Target hydra_uitest`, then `.\build-cpp\Release\hydra_uitest.exe --test <name>` or `--all`. GUI checks go through hydra_uitest (docs/agents/ui-testing.md), never screenshots. ch_probe tests: `python -m pytest tools/ch_probe/tests -q`.

**Score-neutral proof.** Every task that touches the engine, the store or the analysis proves that no score changed. It compares the sorted `[n/N]` lines of `hydra_batch` on testdata\input against the Task 0 baseline. The database line and the elapsed-time line change on every run, so the recipe keeps only the per-chart lines:

```powershell
$out = "$env:TEMP\hydra_T<n>"; New-Item -ItemType Directory -Force $out | Out-Null
Remove-Item "$out\after.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_batch.exe --db "$out\after.db" testdata\input |
  Select-String '^\[\d+/\d+\] ' | ForEach-Object { $_.Line -replace '^\[\d+/\d+\] ', '' } |
  Sort-Object | Set-Content "$out\batch_sorted.txt"
Compare-Object (Get-Content "$env:TEMP\hydra_audit_base\batch_sorted.txt") (Get-Content "$out\batch_sorted.txt")
```

It must print nothing. The baseline folder `$env:TEMP\hydra_audit_base\` must not be deleted until Task 22 is done.

**Speed numbers.** Speed tasks (T9, T13, T14's link-time optimization, T17) measure on a quiet machine. Before timing, the agent waits until no `cl.exe`, `link.exe`, `MSBuild.exe` or `hydra_*.exe` has run for 60 seconds, checked with `tasklist`. It waits at most 20 minutes, and otherwise reports "not measured: machine busy". The main session re-runs every timing in Task 22 on a quiet machine, and those are the numbers that count. A speed task that makes nothing faster is reverted, not merged.

No user-visible change beyond the decisions below. If a task finds that its fix would change a number, label or wording you see, it stops and reports instead.

Your uncommitted and untracked files stay yours. Task 0 commits them as they are, and no later task edits them except where a task names one.

**User decisions (already made, 2026-09-26):**

1. Report song names follow song.ini: the stored name is updated on the next scan or analysis ("Follow song.ini").
2. The path report gets a Mode column and keeps every row. Its subtitle counts the rows shown ("Add a Mode column").
3. The score-range, ms-limit and SP-cap boxes stay live exactly as today, but cheap. The INI is saved when you let go, the decoded record is cached, and only the small summary query reruns ("Live, but cheap").
4. The Preview's delay/Offset rule applies to .sng (its metadata delay, else the chart's Offset) and .srb (the chart's Offset) ("Yes, same rule").
5. The pre-1.7 database migrations are dropped. A 1.5 or 1.6 database shows "Not analyzed" instead of "Stale" ("Drop them").
6. ADR 0002 is retired. The three report pages share one script and one stylesheet, and look and behave the same ("Retire it").
7. The Auto time budget leaves the rules fingerprint, and the Auto ladder marks only Auto records Stale ("Budget out, ladder Auto-only").
8. The record-format bump happens now. It drops unused stored fields and stores the multiplier squeezes once per chart. You re-analyze once, and the shown numbers stay identical ("Now, in this plan").
9. The loose data moves to C:\Users\Patrick\Downloads\Hydra\hydra-data\, and the logs are deleted ("Move out, delete logs").
10. Your pending work is committed as-is before the fleet starts ("Commit as-is first").
11. ch_probe's passive_probe.py and active_probe.py are fixed so they attach and run ("Fix them").
12. ch_probe is restructured. The working pieces of play_chart.py move into tested modules, superseded experiments are deleted, and behaviour at the game stays the same ("Restructure").

**Calls I made myself (not yours; say if you disagree):**

- The record store switches to WAL mode (write-ahead logging: new writes go to a side file and are folded in later) with `synchronous=NORMAL`. This makes each save much cheaper. The cost is that a power cut can lose the last few seconds of saved results, which you would simply re-analyze. The database never gets corrupted by it. WAL doesn't work on a network drive, and Hydra's database lives next to the exe, so that's fine.
- `hydra_batch` refuses a run (exit code 2, with a message) when its `--legacy-fills` flag disagrees with the database's stamp, and `--reindex` never changes the stamp.
- The Preview with no audio device shows one line, "Audio unavailable: <reason>. The preview is muted.", and draws everything else as normal.

## What others do

SQLite rolls back table drops and creates inside a transaction like any other write ([sqlite.org: transactions](https://www.sqlite.org/lang_transaction.html)). That is why T1 can simply move BEGIN to the top of the library rebuild.

SQLite's WAL page lists the tradeoffs T9 relies on: faster commits, readers that don't block the writer, `synchronous=NORMAL` giving up durability only on power loss, no network drives, and the mode sticking to the file once set ([sqlite.org/wal.html](https://www.sqlite.org/wal.html)).

SQLite's compile-options page says its feature switches only check whether a macro is defined, so `=0` means on ([sqlite.org/compile.html](https://www.sqlite.org/compile.html)). That is the FTS5 bug T14 fixes.

The batch cancel hang is the classic "lost wakeup": a thread waits for a signal that was already sent. CERT's rule CON55-CPP ([SEI CERT](https://wiki.sei.cmu.edu/confluence/display/cplusplus/CON55-CPP.+Preserve+thread+safety+and+liveness+when+using+condition+variables)) and Harvard's CS 61 notes ([cs61](https://cs61.seas.harvard.edu/site/2021/Synch2/)) give the same fix T3 uses: wait on a condition the waker also sets under the lock, and wake everyone when the last worker leaves.

Microsoft documents that a debugger thread kills every process it is attached to when it exits, unless `DebugSetProcessKillOnExit(FALSE)` is called ([learn.microsoft.com](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-debugsetprocesskillonexit)). That is why a Python crash takes Clone Hero down with it today (T19).

MSVC reads a source file with no byte-order mark in the user's code page unless `/utf-8` is set ([learn.microsoft.com: /utf-8](https://learn.microsoft.com/en-us/cpp/build/reference/utf-8-set-source-and-executable-character-sets-to-utf-8)). So the em dashes in Hydra's string literals only survive on Western code pages today (T14).

miniaudio's own header documents `MA_NO_ENGINE`, `MA_NO_NODE_GRAPH`, and `MA_ENABLE_ONLY_SPECIFIC_BACKENDS` with `MA_ENABLE_WASAPI` for building only what an app uses ([miniaudio.h](https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h)). T14 uses them.

## Running it as a workflow

The plan runs as three Workflow runs, one per wave, with the main session merging between them. The same script shape serves every wave.

For each task in the wave, the script first makes the task's worktree from the wave's base commit. Then an Opus implementer does the task in that worktree, from the task's section of this plan. Then a Sonnet reviewer checks it. The reviewer runs the scope gate (`git diff --stat` must show only the task's files), re-runs the task's Verify command and acceptance checks, and reads the diff against the task text. If the review fails, a follow-up Opus agent gets the reviewer's findings and fixes them in the same worktree, and the reviewer runs again. After two failed rounds the task is marked "needs the main session" and the run goes on without it.

The tasks in a wave run through `pipeline()`, so each task's review starts as soon as its implementer finishes. Builds are the heavy part. The script lets at most six implementers build at once, and the rest wait for a free slot. The speed tasks wait for a quiet machine before timing, as the Global Constraints say.

Every implementer and reviewer prompt carries the same rules: the task line ("Task N of 22: <name>"), the worktree path, the status-file line (append one line to `C:\Users\Patrick\.claude\hooks\state\status\<agent id>.md` every 10 tool calls or 5 minutes), "never hand off to a background job", and "never touch files outside your list". Prompts stay under 4,000 characters by pointing at this plan's task section instead of pasting it.

When a wave's run finishes, the main session merges. It checks each branch's `git diff --stat` against the task's file list, and runs `git reflog -8` for commits nobody in the fleet made. Then it merges the branches into `hydra-test` in task-number order with `git merge --no-ff audit/T<n>`, and resolves the expected overlaps each task lists. After the last merge it builds everything, runs `hydra_tests.exe`, `hydra_uitest.exe --all`, the ch_probe tests and the batch diff. Only then does the next wave start, from that merged head. The worktrees of merged tasks are removed with `git worktree remove` once the wave's checks pass.

---

### Task 0: Get the repo ready and record the baselines (main session)

Today the repo root holds about 2.6 GB of untracked song data and logs that nothing ignores. One `git add -A` would commit all of it. Your pending work (the ch_probe edits and experiments, the audit, the handoffs, the plan files and the cap-clamped doc) is uncommitted. The worktrees the fleet works in only see committed files, so they would start without it. Git also tracks `claude.md` in lowercase while the file on disk is `CLAUDE.md`, which only works because Windows ignores case.

This task commits your work as-is, moves the data out, deletes the logs, adds ignore rules, fixes the file name's case, and records the "before" numbers every later task compares against.

What the user sees: the repo root is clean, and the three data folders live in `C:\Users\Patrick\Downloads\Hydra\hydra-data\`.

**Depends on:** nothing. **Expected overlaps:** none.

**Goal:** A clean, committed base for the fleet, with baselines recorded in `$env:TEMP\hydra_audit_base\`.

> **USER-ORDERED GATE — NON-SKIPPABLE.** This task was requested by the user in the current conversation. It MUST NOT be closed by walking around it, by declaring it "verified inline", or by substituting a cheaper check. Close only after every item in `acceptanceCriteria` has been re-validated independently, with output captured.

**Files:**
- Commit as-is: `docs/cap-clamped-squeeze-frontend-anchor.md`, `tools/ch_probe/` (all modified and untracked files), `docs/audit/`, `docs/handoffs/`, `docs/superpowers/plans/`
- Move: `srb_songs/`, `srb_audio_dump/`, `scratch_ms/` to `C:\Users\Patrick\Downloads\Hydra\hydra-data\`
- Delete: `d1.txt`, `d2.txt`, `e1.txt`, `e1b.txt`, `e2.txt`, `e2b.txt`, `e3.txt`, `e3b.txt`, `e4.txt`, `e4b.txt`, `err_a.txt`, `err_b.txt`, `err_d.txt`, `err_e.txt`, `err_f.txt`, `err_f2.txt`, `err_g2.txt`, `err_g3.txt`, `err_g4.txt`, `err_g5.txt`, `err_g_simple.txt`, `fixtureB.json`, `libvpx_err.txt`
- Modify: `.gitignore`, `tools/ch_probe/constants.py` (the one docstring line naming `scratch_ms/`), `docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md` (its `scratch_ms` references)
- Rename: `claude.md` to `CLAUDE.md` in git

**Acceptance Criteria:**
- [ ] You confirmed in chat that the morning's ch_probe edits are finished before step 2 ran.
- [ ] `git status --short` prints nothing after the task.
- [ ] `git ls-files | Select-String -CaseSensitive '^CLAUDE.md$'` prints one line, and `git ls-files | Select-String -CaseSensitive '^claude.md$'` prints nothing.
- [ ] `Test-Path C:\Users\Patrick\Downloads\Hydra\hydra-data\srb_songs` is True, and the repo root has no `srb_songs`, `srb_audio_dump`, `scratch_ms` or loose `.txt` logs.
- [ ] `$env:TEMP\hydra_audit_base\` holds `batch_sorted.txt`, `batch_time.txt`, `reindex_time.txt`, `bench.txt`, `tests_time.txt`, `tests_summary.txt`, `uitest.txt`, `pytest.txt` and `base_commit.txt`.

**Verify:** `git status --short; Get-ChildItem $env:TEMP\hydra_audit_base | Select-Object Name` → an empty status, then the nine baseline files.

**Steps:**

- [ ] **Step 1: Ask before committing.** Something outside this planning session edited tools/ch_probe on 2026-09-26 between 09:34 and 09:37 (test_process.py changed; live.py, watch_window.py and walk_edges.py appeared; origin unknown, and no planning agent wrote them). Ask the user in chat whether that work is finished. Wait for a yes. Then run `git status --short` and show the user the list that step 2 will commit.

- [ ] **Step 2: Commit the pending work as-is.**

```powershell
git add docs/cap-clamped-squeeze-frontend-anchor.md tools/ch_probe docs/audit docs/handoffs docs/superpowers/plans
git status --short   # only the data folders and the loose logs remain
git commit -m "Commit pending ch_probe work, audit, handoffs and plans as-is

Task: Task 0 - repo ready
Agent: main session
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Check the staged list before committing: `tools/ch_probe/experiments/__pycache__` must not appear (`.gitignore` already covers `__pycache__/`).

- [ ] **Step 3: Move the data out and delete the logs.**

```powershell
$dst = "C:\Users\Patrick\Downloads\Hydra\hydra-data"
New-Item -ItemType Directory -Force $dst | Out-Null
foreach ($d in 'srb_songs','srb_audio_dump','scratch_ms') { Move-Item -LiteralPath ".\$d" -Destination "$dst\$d" }
$logs = 'd1.txt','d2.txt','e1.txt','e1b.txt','e2.txt','e2b.txt','e3.txt','e3b.txt','e4.txt','e4b.txt',
        'err_a.txt','err_b.txt','err_d.txt','err_e.txt','err_f.txt','err_f2.txt','err_g2.txt','err_g3.txt',
        'err_g4.txt','err_g5.txt','err_g_simple.txt','fixtureB.json','libvpx_err.txt'
Remove-Item -LiteralPath $logs
```

Nothing in the repo reads `fixtureB.json` (only the audit mentions it), and the logs are old benchmark and ffmpeg output.

- [ ] **Step 4: Point the two scratch_ms references at the new place.** In tools/ch_probe/constants.py, change the docstring line

```python
cross-checked them against the Ghidra dumps in scratch_ms/. This is the ONE
```

to

```python
cross-checked them against the Ghidra dumps in ..\hydra-data\scratch_ms\. This is the ONE
```

In docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md, replace each `scratch_ms/` path with `C:\Users\Patrick\Downloads\Hydra\hydra-data\scratch_ms\` (find them with `Select-String -Path docs\superpowers\specs\2026-09-17-ch-dynamic-input-probe.md -Pattern scratch_ms`).

- [ ] **Step 5: Add ignore guards and drop the dead lines.** In .gitignore, delete these three lines, because those folders no longer exist:

```
# Retired Python-era build output locations.
build/
dist/
```

Append:

```
# Song data, audio dumps and Ghidra output live in ..\hydra-data, never here.
/srb_songs/
/srb_audio_dump/
/scratch_ms/
# Loose logs and scratch files at the repo root.
/*.txt
/*.log
!/CMakeLists.txt
```

The `!/CMakeLists.txt` line keeps the `/*.txt` rule from ever ignoring CMakeLists.txt. Check with `git check-ignore -v CMakeLists.txt`, which must print nothing.

- [ ] **Step 6: Fix the file name's case.** On Windows a case-only rename needs two steps:

```powershell
git mv claude.md claude.md.tmp
git mv claude.md.tmp CLAUDE.md
```

- [ ] **Step 7: Commit the cleanup.**

```powershell
git add .gitignore tools/ch_probe/constants.py docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md CLAUDE.md
git status --short   # must print nothing
git commit -m "Move song data out of the repo, delete loose logs, track CLAUDE.md by its real name

Task: Task 0 - repo ready
Agent: main session
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git rev-parse HEAD | Set-Content "$env:TEMP\hydra_audit_base\base_commit.txt"
```

(Create the folder first: `New-Item -ItemType Directory -Force $env:TEMP\hydra_audit_base`.)

- [ ] **Step 8: Record the baselines on a quiet machine.** Nothing else may be building or running a test while these run.

```powershell
$b = "$env:TEMP\hydra_audit_base"
.\build_cpp.ps1
.\build_cpp.ps1 -Target hydra_bench
.\build_cpp.ps1 -Target hydra_replay
.\build_cpp.ps1 -Target hydra_uitest

# Scores: the per-chart lines every score-neutral task compares against.
Remove-Item "$b\base.db" -ErrorAction SilentlyContinue
$t = Measure-Command {
  .\build-cpp\Release\hydra_batch.exe --db "$b\base.db" testdata\input | Set-Content "$b\batch_full.txt" }
$t.TotalSeconds | Set-Content "$b\batch_time.txt"
Get-Content "$b\batch_full.txt" | Select-String '^\[\d+/\d+\] ' |
  ForEach-Object { $_.Line -replace '^\[\d+/\d+\] ', '' } | Sort-Object | Set-Content "$b\batch_sorted.txt"
(Measure-Command { .\build-cpp\Release\hydra_batch.exe --db "$b\base.db" --reindex }).TotalSeconds |
  Set-Content "$b\reindex_time.txt"

# Engine speed.
.\build-cpp\Release\hydra_bench.exe testdata\input | Set-Content "$b\bench.txt"

# Tests: pass counts and wall time.
$t = Measure-Command { .\build-cpp\Release\hydra_tests.exe | Set-Content "$b\tests_summary.txt" }
$t.TotalSeconds | Set-Content "$b\tests_time.txt"
.\build-cpp\Release\hydra_uitest.exe --all | Set-Content "$b\uitest.txt"
python -m pytest tools/ch_probe/tests -q 2>&1 | Set-Content "$b\pytest.txt"
```

If `hydra_bench` or `--reindex` take different arguments than shown, run each with no arguments first, read its usage line, and record the command used at the top of the file. When the plan was written, the three test_process failures the audit names were already fixed by an uncommitted edit of unknown origin (134 passed on 2026-09-26). Write down what it shows; T19 starts from it.

---

### Task 1: The record store stops losing the library, stops holding its lock while decoding, and has one rule for which row wins

Five things are wrong in `src/store/record_store.cpp` today.

First, `rebuild_chart_library` drops and recreates the `charts` table before it opens its transaction. If an insert then fails, the rollback leaves an empty table. That also throws away the rescan cache, so the next scan re-hashes every chart. The fix opens the transaction first and empties the table inside it. The table is emptied with `DELETE` instead of dropped, so its columns, and the index Task 9 adds, survive. A table from before the `sig` column gets that column once, when the store opens.

Second, `get_record` holds the store lock while it decodes the whole record. The header says the lock covers SQLite calls only, and the UI thread calls this function. The fix reads the winning row, its path nodes and the tempo map under the lock, then decodes after releasing it.

Third, the "which row wins" logic is written out four times, in `get_summary`, `get_record`, `for_each_blob` and `list_records`. The Ready rule is also spelled twice in SQL, once positive and once negated. The fix adds one small class, `WinnerPicker`, that all four offer their candidate rows to. The SQL keeps one spelling, and the purge negates it with `NOT`.

Fourth, dead code. The Uncapped-edition import can never copy a row. It only matches rows stamped with this build's version plus ".uncapped", and no real file carries that. It goes, with its call in `open_store` and its test. The pre-1.7 migrations go too (user decision 5). So do the helpers only they use: `add_missing_columns`, `has_table`, `ends_with`, the old `records` column lists, `kBlobHeadBytes` and `peek_sp_cap` in serialize. `drop_stale_records` has no production caller and goes. `Lens::sentinel()` and `is_sentinel()` are test-only and go.

Removing the migrations changes one thing a user can see, and decision 5 covers it. A 1.5 or 1.6 database keeps its old `records` table, untouched and never read, so its charts read "Not analyzed" instead of "Stale". Rows that a 1.7 to 1.8.1 build already migrated carry `ms_enabled = -1` ("settings unknown"). No lens has -1, so those rows stop being lookup candidates at all. They also read "Not analyzed", and the next analysis of that chart deletes them. These are the same 1.5/1.6 rows decision 5 is about.

Fifth, song names are frozen at a chart's first analysis, because `add_song` is `INSERT OR IGNORE`. Decision 1 says names follow song.ini. `add_song` becomes an upsert (insert, or update the names if the row exists), so the next analysis carries the scan's names. `rebuild_chart_library` also copies the scanned names onto songs that already have a row, so a rescan alone is enough. When one chart appears twice in the scan, the first copy the scan listed names it.

What the user sees: a fixed song.ini reaches the reports after the next scan or analysis. A 1.5/1.6 user sees "Not analyzed" instead of "Stale" for charts analyzed before 1.7 (decision 5). The UserGuide and README stop promising the Uncapped import. Nothing else.

**Depends on:** nothing (wave 1).

**Expected overlaps:** Task 9 edits the same file afterwards (`add_row`, `add_song`, `has_record`, `reindex`, the constructor, `get_summary`) and is written against this task's result. Task 10 changes `rank_row`, `structure_is_current`, `stale_reasons`, `bind_ready_params` and `kRowReadySql` afterwards. Task 12 edits `src/store/serialize.h/.cpp`; this task only deletes `peek_sp_cap` there. Task 4 edits `src/cli/batch.cpp`, which this task does not touch.

**Goal:** The store keeps the old library when a rebuild fails, decodes records without holding its lock, picks every winning row through one helper, carries no dead import or migration code, and lets song names follow song.ini.

**Files:**
- Modify: `src/store/record_store.h`
- Modify: `src/store/record_store.cpp`
- Modify: `src/store/serialize.h`, `src/store/serialize.cpp` (delete `peek_sp_cap`)
- Modify: `src/app/config.h`, `src/app/config.cpp` (`open_store`)
- Modify: `README.md`, `docs/UserGuide.md`, `installer/hydra.iss` (the Uncapped import text)
- Modify: `CONTEXT.md` (the **Record** entry), `docs/adr/0009-analysis-settings-key-a-record-paths-stored-once.md` (a dated note)
- Test: `tests/test_store.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="a database from Hydra 1.6 or older opens with nothing to show"` passes.
- [ ] `hydra_tests.exe -tc="a row an old migration marked with unknown settings reads Not analyzed"` passes.
- [ ] `hydra_tests.exe -tc="a failed library rebuild keeps the previous scan"` passes.
- [ ] `hydra_tests.exe -tc="a charts table from before the sig column still rebuilds"` passes.
- [ ] `hydra_tests.exe -tc="a song's stored names follow the latest analysis and the latest scan"` passes.
- [ ] `hydra_tests.exe -tc="get_record reads a whole row while another thread rewrites it"` passes.
- [ ] `hydra_tests.exe -tc="has_record and a lookup agree on which rows are readable"` passes.
- [ ] The whole suite, `hydra_tests.exe`, ends `Status: SUCCESS!`.
- [ ] `hydra_uitest.exe --all` passes.
- [ ] `Get-ChildItem -Recurse src,tests,tools -Include *.cpp,*.h | Select-String -Pattern 'import_legacy_uncapped|drop_stale_records|migrate_records|is_sentinel|Lens::sentinel|peek_sp_cap|add_missing_columns|lens_or_sentinel|kRowNotReadySql|row_is_ready'` prints nothing.
- [ ] In `RecordStore::get_record`, `rebuild_record` and `restore_timecodes` run after the locked block's closing brace (read the function).
- [ ] The score-neutral proof prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests.** In `tests/test_store.cpp`, delete the whole test case `TEST_CASE("a legacy-imported sentinel row is left out of the listing")` (it starts with that line and ends at the `}` before `TEST_CASE("a row in an older path format is Stale even when this build stamped it")`). Put this in its place:

```cpp
TEST_CASE("a row an old migration marked with unknown settings reads Not analyzed") {
    // Hydra 1.7 to 1.8.1 migrated older rows in with ms_enabled = -1: "a
    // result, settings unknown". No lens has -1, so such a row is no
    // candidate for any lookup. It reads as no row at all, not as Stale
    // (user decision 2026-09-26), and a real run of the chart replaces it.
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    PreparedRow migrated = prepare_row(RecordKey{"h", "mode", CapQuery::at(8)}, at_cap(8));
    migrated.lens.ms_enabled = -1;
    migrated.hyversion = "1.6.0";
    store.add_row(migrated);
    CHECK(store.counts().second == 1);

    const RecordKey key{"h", "mode", CapQuery::at(8)};
    CHECK(store.get_record(key).status == RecordStatus::NotAnalyzed);
    CHECK(store.get_summary(key).status == RecordStatus::NotAnalyzed);
    CHECK_FALSE(store.has_record(key));
    CHECK(store.list_records(std::nullopt, CapQuery::at(8), Lens{}, SortColumn::Score, true)
              .empty());
    int seen = 0;
    store.for_each_blob(std::nullopt, CapQuery::at(8), Lens{},
                        [&](const RecordStore::BlobRow&, const HydraRecord*) { ++seen; });
    CHECK(seen == 0);

    store.add_record(key, at_cap(8));
    CHECK(store.counts().second == 1);
    CHECK(store.get_record(key).status == RecordStatus::Ready);
}
```

Then append this at the end of the file:

```cpp
// ---- store correctness (2026-09-26 audit, Task 1) --------------------------

namespace {

ChartLibraryEntry chart_entry(const char* md5, const char* title) {
    return ChartLibraryEntry{md5,
                             title,
                             "Artist",
                             "Charter",
                             std::string("C:\\charts\\") + md5 + "\\notes.chart",
                             "C:\\charts",
                             std::string("sig-") + md5};
}

// Runs a batch of SQL straight on a database file no store has open.
void exec_on_file(const std::string& path, const char* sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    const std::string msg = err ? err : "";
    sqlite3_free(err);
    sqlite3_close(db);
    INFO(msg);
    REQUIRE(rc == SQLITE_OK);
}

}  // namespace

TEST_CASE("a database from Hydra 1.6 or older opens with nothing to show") {
    // User decision 2026-09-26: the pre-1.7 migrations are gone. The old
    // `records` table is left where it is and never read, so its charts read
    // Not analyzed until they are analyzed again. Those rows could not be
    // read since 1.8.1 anyway.
    const std::string path = temp_db("old_records");
    std::remove(path.c_str());
    {
        RecordStore seed(path);
        seed.add_song("old", "Old Song", "A", "C", fixture().song);
    }
    exec_on_file(path,
                 "DROP TABLE results; DROP TABLE path_refs; DROP TABLE paths;"
                 "PRAGMA user_version = 1;"
                 "CREATE TABLE records (hyhash TEXT NOT NULL, chartmode TEXT NOT NULL,"
                 " hyversion TEXT NOT NULL, sp_cap INTEGER NOT NULL, bestpath TEXT NOT NULL,"
                 " blob BLOB NOT NULL, score INTEGER, actcount INTEGER, maxskip INTEGER,"
                 " hardest_ms REAL, avgmult REAL, notecount INTEGER, sqin_count INTEGER,"
                 " sqout_count INTEGER, pathcount INTEGER,"
                 " PRIMARY KEY (hyhash, chartmode, sp_cap));"
                 "INSERT INTO records (hyhash, chartmode, hyversion, sp_cap, bestpath, blob,"
                 " score) VALUES ('old', 'mode', '1.6.0', 8, '1 2 3', x'00', 100);");

    const RecordKey key{"old", "mode", CapQuery::at(8)};
    {
        RecordStore store(path);
        CHECK(store.counts().second == 0);
        CHECK(store.get_record(key).status == RecordStatus::NotAnalyzed);
        CHECK(store.get_summary(key).status == RecordStatus::NotAnalyzed);
        CHECK_FALSE(store.has_record(key));
        // A fresh analysis lands as usual.
        store.add_record(key, at_cap(8));
        CHECK(store.get_record(key).status == RecordStatus::Ready);
    }
    // The old table is left alone: nothing read it and nothing rewrote it.
    CHECK(scalar(path, "SELECT COUNT(*) FROM records") == 1);
    CHECK(scalar(path, "PRAGMA user_version") == 2);
    std::remove(path.c_str());
}

TEST_CASE("a failed library rebuild keeps the previous scan") {
    // The rebuild used to drop the table before opening its transaction, so
    // an insert that failed left the library empty, and took the rescan cache
    // with it. A trigger that refuses one md5 makes an insert fail partway.
    const std::string path = temp_db("rebuild_fail");
    std::remove(path.c_str());
    {
        RecordStore store(path);
        store.rebuild_chart_library({chart_entry("a", "A"), chart_entry("b", "B")});
    }
    exec_on_file(path,
                 "CREATE TRIGGER refuse_boom BEFORE INSERT ON charts WHEN NEW.md5 = 'boom'"
                 " BEGIN SELECT RAISE(ABORT, 'boom'); END;");
    {
        RecordStore store(path);
        CHECK_THROWS(
            store.rebuild_chart_library({chart_entry("c", "C"), chart_entry("boom", "Boom")}));
        CHECK(store.chart_library_count() == 2);
        const ChartLibraryCache cache = store.chart_library_cache();
        CHECK(cache.count("C:\\charts\\a\\notes.chart") == 1);
        CHECK(cache.count("C:\\charts\\b\\notes.chart") == 1);
        // No transaction was left open: the next rebuild goes through.
        store.rebuild_chart_library({chart_entry("c", "C")});
        CHECK(store.chart_library_count() == 1);
    }
    std::remove(path.c_str());
}

TEST_CASE("a charts table from before the sig column still rebuilds") {
    // The rebuild empties the table instead of recreating it, so an old
    // table has to gain the column when the store opens.
    const std::string path = temp_db("charts_nosig");
    std::remove(path.c_str());
    exec_on_file(path,
                 "CREATE TABLE charts (md5 TEXT, name TEXT, artist TEXT, charter TEXT,"
                 " path TEXT, folder TEXT);"
                 "INSERT INTO charts VALUES ('a', 'A', 'Artist', 'Charter',"
                 " 'C:\\charts\\a\\notes.chart', 'C:\\charts');");
    {
        RecordStore store(path);
        CHECK(store.chart_library_count() == 1);
        CHECK(store.chart_library_cache().empty());  // no fingerprints yet
        store.rebuild_chart_library({chart_entry("b", "B")});
        CHECK(store.chart_library_count() == 1);
        CHECK(store.chart_library_cache().at("C:\\charts\\b\\notes.chart").sig == "sig-b");
    }
    std::remove(path.c_str());
}

TEST_CASE("a song's stored names follow the latest analysis and the latest scan") {
    // User decision 2026-09-26: fixing song.ini reaches the reports. The
    // names used to be frozen at the chart's first analysis.
    RecordStore store(":memory:");
    store.add_song("h", "Old Title", "Old Artist", "Old Charter", fixture().song);
    store.add_record(RecordKey{"h", "mode", CapQuery::at(4)}, at_cap(4));
    auto listed = [&] {
        std::vector<RecordListing> rows =
            store.list_records(std::nullopt, CapQuery::at(4), Lens{}, SortColumn::Score, true);
        REQUIRE(rows.size() == 1);
        return rows[0];
    };

    // Analyzed again after song.ini changed.
    store.add_song("h", "New Title", "New Artist", "New Charter", fixture().song);
    CHECK(listed().ref_name == "New Title");
    CHECK(listed().ref_artist == "New Artist");
    CHECK(listed().ref_charter == "New Charter");

    // Rescanned after song.ini changed, with no analysis. The scan found two
    // copies of the chart; the first one it listed names it.
    const ChartLibraryEntry first = chart_entry("h", "Scanned Title");
    ChartLibraryEntry second = chart_entry("h", "Second Copy");
    second.notespath = "C:\\charts\\copy\\notes.chart";
    store.rebuild_chart_library({first, second});
    CHECK(listed().ref_name == "Scanned Title");

    // A chart the scan found but nobody analyzed gets no song row.
    store.rebuild_chart_library({first, chart_entry("x", "Never Analyzed")});
    CHECK(store.counts().first == 1);
}

TEST_CASE("get_record reads a whole row while another thread rewrites it") {
    // get_record reads the winning row, its nodes and the tempo map under
    // one lock, then decodes with the lock released. A rewrite landing in
    // between must never pair one row's shape with another row's nodes.
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);
    const RecordKey key{"h", "mode", CapQuery::at(4)};
    HydraRecord empty = at_cap(4);
    empty.paths.clear();
    empty.allzero_paths.clear();
    store.add_record(key, at_cap(4));
    const size_t full = fixture().record.paths.size();

    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (int i = 0; i < 50; ++i) store.add_record(key, i % 2 ? at_cap(4) : empty);
        stop.store(true);
    });
    int reads = 0;
    bool all_whole = true;
    std::string failure;
    do {
        try {
            const RecordLookup r = store.get_record(key);
            if (r.status != RecordStatus::Ready || !r.record) all_whole = false;
            else if (!r.record->paths.empty() && r.record->paths.size() != full)
                all_whole = false;
            ++reads;
        } catch (const std::exception& e) {
            failure = e.what();
            break;
        }
    } while (!stop.load());
    writer.join();

    // doctest's assertions are not thread-safe, so every check is out here.
    CHECK(failure.empty());
    CHECK(all_whole);
    CHECK(reads > 0);
}

TEST_CASE("has_record and a lookup agree on which rows are readable") {
    // The Ready rule is spelled once in C++ (rank_row) and once in SQL
    // (kRowReadySql, which has_record and add_row's purge use). This pins the
    // two spellings together across every kind of row.
    core::Rules other = core::default_rules();
    other.max_tied_paths = 2;
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);

    const RecordKey ready{"h", "ready", CapQuery::at(8)};
    store.add_record(ready, at_cap(8));

    const RecordKey old_build{"h", "build", CapQuery::at(8)};
    PreparedRow build_row = prepare_row(old_build, at_cap(8));
    build_row.hyversion = "0.0.0";
    store.add_row(build_row);

    const RecordKey old_format{"h", "format", CapQuery::at(8)};
    PreparedRow format_row = prepare_row(old_format, at_cap(8));
    format_row.structure[0] = 1;
    format_row.structure[1] = 0;
    format_row.structure[2] = 0;
    format_row.structure[3] = 0;
    store.add_row(format_row);

    const RecordKey other_rules{"h", "rules", CapQuery::at(8)};
    HydraRecord foreign = at_cap(8);
    foreign.rules_fingerprint = other.fingerprint();
    store.add_row(prepare_row(other_rules, foreign));

    for (const RecordKey& key : {ready, old_build, old_format, other_rules}) {
        INFO(key.chartmode);
        CHECK(store.has_record(key) ==
              (store.get_summary(key).status == RecordStatus::Ready));
    }
    CHECK(store.has_record(ready));
}
```

- [ ] **Step 2: Run them and watch four fail.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="a database from Hydra 1.6*,a row an old migration*,a failed library rebuild*,a song's stored names*,a charts table from before*,get_record reads a whole row*,has_record and a lookup agree*"`. Expected: four failures. The old-database test fails on `counts().second == 0` (the migration made a row) and on the `records` count (the migration dropped the table). The old-migration test fails because the row reads Stale. The rebuild test fails on `CHECK_THROWS`, because dropping the table also dropped the trigger. The names test fails on "New Title". The sig, get_record and has_record tests pass already; they guard the change.

- [ ] **Step 3: Delete the Uncapped import.** In `src/app/config.cpp`, replace:

```cpp
std::unique_ptr<store::RecordStore> open_store(const std::string& db,
                                               uint64_t rules_fingerprint) {
    auto store = std::make_unique<store::RecordStore>(db, rules_fingerprint);
    // The legacy file is looked for beside the db being opened, not beside
    // the exe: a test harness pointing at a scratch db must never swallow a
    // developer's real library.
    std::filesystem::path dir = std::filesystem::u8path(db).parent_path();
    std::filesystem::path legacy = dir / "hydra_uncapped.db";
    std::error_code ec;
    if (std::filesystem::exists(legacy, ec))
        store->import_legacy_uncapped(legacy.u8string());
    return store;
}
```

with:

```cpp
std::unique_ptr<store::RecordStore> open_store(const std::string& db,
                                               uint64_t rules_fingerprint) {
    return std::make_unique<store::RecordStore>(db, rules_fingerprint);
}
```

and delete `#include <filesystem>` from the top of that file (nothing else in it uses `std::filesystem`). In `src/app/config.h`, replace:

```cpp
// Opens the store at `db` and, the first time, pulls in any records from a
// pre-1.6 "hydra_uncapped.db" sitting in the same folder (the old Uncapped
// edition's separate library). The old file is never modified.
// rules_fingerprint gates Ready: a row analyzed under other rules reads Stale.
```

with:

```cpp
// Opens the store at `db`. rules_fingerprint gates Ready: a row analyzed
// under other rules reads Stale.
```

In `src/store/record_store.h`, delete the declaration of `import_legacy_uncapped` and the comment block above it (from `// One-time import of the pre-1.6 Uncapped edition's separate library.` through `int import_legacy_uncapped(const std::string& uncapped_db_path);`). In `src/store/record_store.cpp`, delete the whole function `int RecordStore::import_legacy_uncapped(const std::string& uncapped_db_path)` (from that line through its closing `}` just before `void RecordStore::add_missing_columns()`). In `tests/test_store.cpp`, delete `TEST_CASE("import_legacy_uncapped copies current-version rows once, under their cap")` whole (it ends at the `}` before the `// ---- lens identity ----` comment).

In `README.md`, replace:

```
Before 1.6 this shipped as a second program, Hydra Uncapped, with its own
`hydra_uncapped.db`. The first time 1.6 opens it copies those records into
`hydra.db` under the cap each ran at, and leaves the old file alone.
```

with:

```
Before 1.6 this shipped as a second program, Hydra Uncapped, with its own
`hydra_uncapped.db`. Hydra does not read that file; analyze those charts again
at the cap you want.
```

In `docs/UserGuide.md`, under "#### SP cap", delete this line and the blank line after it:

```
If you used the pre-1.6 Hydra Uncapped app, its records are copied into the main library the first time 1.6 opens. The old `hydra_uncapped.db` is left untouched.
```

In `installer/hydra.iss`, replace:

```
;     That includes hydra_uncapped.db from the pre-1.6 Uncapped edition: the
;     app imports it on first launch and never touches it again.
```

with:

```
;     That includes hydra_uncapped.db from the pre-1.6 Uncapped edition,
;     which the app no longer reads.
```

- [ ] **Step 4: Delete the migrations and what only they use.** In `src/store/record_store.cpp`, in the constructor, replace:

```cpp
    // A pre-1.6 or 1.6 file still has the old single-blob `records` table.
    // Bring it to the v1 shape (keyed by cap) first, then fold it into the
    // three v2 tables. A fresh db skips both and is born at v2.
    const bool had_records = has_table("records");
    if (had_records) {
        add_missing_columns();
        if (!has_column("records", "sp_cap")) migrate_records_to_cap_key();
    }
    create_result_tables();
    if (had_records) migrate_records_to_results();
    // Schema 2 = results keyed by the full settings, with shared paths.
    exec("PRAGMA user_version = 2");
```

with:

```cpp
    // A charts table from before the rescan cache has no sig column.
    // rebuild_chart_library empties the table rather than recreating it, so
    // the column is added here, once.
    if (!has_column("charts", "sig")) exec("ALTER TABLE charts ADD COLUMN sig TEXT");
    // Schema 2 = results keyed by the full settings, with shared paths. A
    // database from Hydra 1.6 or older still holds its old `records` table.
    // Nothing reads it (user decision 2026-09-26), so its charts read Not
    // analyzed until they are analyzed again.
    create_result_tables();
    exec("PRAGMA user_version = 2");
```

Delete these functions whole: `bool RecordStore::has_table(const char* table)`, `void RecordStore::migrate_records_to_cap_key()`, `void RecordStore::migrate_records_to_results()` and `void RecordStore::add_missing_columns()`. In `src/store/record_store.h`, delete their four private declarations: `bool has_table(const char* table);`, `void add_missing_columns();`, `void migrate_records_to_cap_key();` and `void migrate_records_to_results();`. Replace the constructor comment:

```cpp
    // dbpath may be ":memory:" for an ephemeral store (used by tests). A db
    // from before 1.6 (records keyed without sp_cap) is migrated in place on
    // open, in one transaction; a failure rolls back and rethrows.
```

with:

```cpp
    // dbpath may be ":memory:" for an ephemeral store (used by tests). A db
    // from Hydra 1.6 or older keeps its old records table, unread: its charts
    // read Not analyzed (user decision 2026-09-26).
```

In `src/store/serialize.h`, delete `peek_sp_cap`'s declaration and its comment (from `// The SP cap a blob was analyzed at, read from its fixed header alone (the` through `std::optional<int> peek_sp_cap(const std::vector<uint8_t>& head);`). In `src/store/serialize.cpp`, delete the function `std::optional<int> peek_sp_cap(const std::vector<uint8_t>& head)` whole.

In `tests/test_store.cpp`, delete the helpers `write_legacy_db` (with its comment starting `// Writes a pre-1.6 database by hand`), `user_version`, and `write_v1_db` (with its comment starting `// Writes a 1.6-era database by hand`). Delete the test cases `TEST_CASE("a pre-1.6 database migrates to the cap key on open")` and `TEST_CASE("a 1.6 database migrates to results + shared paths on open")` whole.

- [ ] **Step 5: One rule for which row wins, no sentinels, one SQL spelling.** In `src/store/record_store.cpp`, replace the comment above `kResultsColumnDefs`:

```cpp
// The results table's columns, single-sourced so the create, the v1->v2
// migration and the legacy import all agree on the layout. result_id is the
// rowid alias: a bigger one means "written later", which is how Auto picks
// the newest run.
```

with:

```cpp
// The results table's columns. result_id is the rowid alias: a bigger one
// means "written later", which is how Auto picks the newest run.
```

Then replace everything from the line `// The pre-1.6-era records table's columns, single-sourced so the cap-key` through the end of the `ends_with` function (the `}` just before `}  // namespace`) with:

```cpp
// ---- which row answers a lookup -------------------------------------------
//
// Every lookup answers one question: "which row ran under these settings?".
// The candidates are the rows with the wanted lens at the wanted cap. An
// older Hydra's migration left some rows with ms_enabled = -1, meaning "the
// settings are unknown". No lens has -1, so those rows are never candidates
// and read as no row at all (user decision 2026-09-26).

// "the row at alias `a` carries exactly this lens". Four bound parameters, in
// Lens's field order. `a` is "" or "r.".
std::string lens_match(const char* a) {
    std::string p = a;
    return "(" + p + "ms_enabled=? AND " + p + "ms_value=? AND " + p +
           "depth_mode=? AND " + p + "depth_value=?)";
}
int bind_lens(sqlite3_stmt* s, int idx, const Lens& lens) {
    sqlite3_bind_int(s, idx, lens.ms_enabled);
    sqlite3_bind_int(s, idx + 1, lens.ms_value);
    sqlite3_bind_int(s, idx + 2, lens.depth_mode);
    sqlite3_bind_int(s, idx + 3, lens.depth_value);
    return idx + 4;
}

// The first 12 bytes a structure blob starts with: the u32 structure format,
// then the u64 rules fingerprint (path_codec.cpp flatten_record).
constexpr int kStructureHeadBytes = 12;

std::vector<uint8_t> structure_head_for(uint64_t rules_fingerprint) {
    std::vector<uint8_t> b(kStructureHeadBytes);
    for (int i = 0; i < 4; ++i)
        b[static_cast<size_t>(i)] =
            static_cast<uint8_t>(kPathStructureFormatVersion >> (8 * i));
    for (int i = 0; i < 8; ++i)
        b[static_cast<size_t>(4 + i)] = static_cast<uint8_t>(rules_fingerprint >> (8 * i));
    return b;
}

// Is this row's stored path tree in the layout this build reads, analyzed
// under the rules this process runs? Takes the whole blob or just the
// substr(structure,1,12) a query selected.
bool structure_is_current(const std::vector<uint8_t>& structure_head,
                          uint64_t rules_fingerprint) {
    if (structure_head.size() < kStructureHeadBytes) return false;
    const std::vector<uint8_t> want = structure_head_for(rules_fingerprint);
    return std::equal(want.begin(), want.end(), structure_head.begin());
}

// The facts that decide whether a row is readable and how it places among
// the candidates for its chart and mode. rank_row is the only place C++ reads
// them off a row; kRowReadySql below is the same rule for SQL.
struct Candidate {
    bool current = false;  // stamped by this build
    bool format = false;   // this build's path-structure format, analyzed
                           // under the rules this process runs
    int64_t result_id = 0;
    // Readable: this build wrote it, in a layout this build reads, under
    // these rules. Anything else is Stale: another version's bytes, or a path
    // tree whose activations this build would read back half-empty.
    bool ready() const { return current && format; }
};

Candidate rank_row(const std::string& hyversion, const std::vector<uint8_t>& structure_head,
                   int64_t result_id, uint64_t rules_fingerprint) {
    return Candidate{hyversion == current_record_version(),
                     structure_is_current(structure_head, rules_fingerprint), result_id};
}

// Why a row that is not Ready is Stale, for callers that explain it
// (hydra_replay dump). `build`: another Hydra build or an older path layout.
// `rules`: this layout, analyzed under other rules. An older layout has no
// fingerprint to compare, so it is only ever `build`.
struct StaleReasons {
    bool build = false;
    bool rules = false;
};

StaleReasons stale_reasons(const std::string& hyversion,
                           const std::vector<uint8_t>& structure_head,
                           uint64_t rules_fingerprint) {
    const std::vector<uint8_t> want = structure_head_for(rules_fingerprint);
    const bool layout_current =
        structure_head.size() >= kStructureHeadBytes &&
        std::equal(want.begin(), want.begin() + 4, structure_head.begin());
    StaleReasons why;
    why.build = hyversion != current_record_version() || !layout_current;
    why.rules = layout_current &&
                !std::equal(want.begin() + 4, want.end(), structure_head.begin() + 4);
    return why;
}

// Candidate::ready() spelled in SQL, for the sites that must pick rows in the
// database: has_record only asks whether a readable row exists, and add_row's
// first purge is a DELETE. Negate it with "NOT ", never by spelling the
// opposite, so the rule has one SQL spelling. Both columns are NOT NULL, so
// NOT never meets a NULL.
//
// Two bound parameters: the current version text, then the 12-byte structure
// head (format + rules fingerprint). bind_ready_params binds them and returns
// the next free index.
constexpr const char* kRowReadySql = "(hyversion = ? AND substr(structure,1,12) = ?)";

int bind_ready_params(sqlite3_stmt* s, int idx, uint64_t rules_fingerprint) {
    bind_text(s, idx, current_record_version());
    bind_blob(s, idx + 1, structure_head_for(rules_fingerprint));
    return idx + 2;
}

// Does `a` beat `b`? This version before another, then this path format
// before an older one, then the newest write. Newest, not tallest: an Auto
// run that settles below an older, taller row (a what-if the user typed) is
// the result the user just asked for, so every lookup must show it. The
// tallest rule showed the old row forever and "Analyze paths!" could never
// replace it. Write order is result_id: add_row deletes and re-inserts, so a
// rewritten row is newest.
bool outranks(const Candidate& a, const Candidate& b) {
    if (a.current != b.current) return a.current;
    if (a.format != b.format) return a.format;
    return a.result_id > b.result_id;
}

// Which chart a winner is picked for: one per chart and mode.
using GroupKey = std::pair<std::string, std::string>;

// The one owner of "which row wins". A lookup offers its candidate rows here
// in any order, then asks which offer won each chart and mode: get_summary
// and get_record for one chart, for_each_blob and list_records for many.
// Nothing else compares two rows.
class WinnerPicker {
public:
    // Offers one candidate. Its offer index is the number of earlier offers.
    void offer(const std::string& hyhash, const std::string& chartmode,
               const Candidate& rank) {
        const size_t index = ranks_.size();
        ranks_.push_back(rank);
        auto [it, inserted] = winner_.emplace(GroupKey{hyhash, chartmode}, index);
        if (!inserted && outranks(rank, ranks_[it->second])) it->second = index;
    }
    // One flag per offer: true for each chart and mode's winner.
    std::vector<bool> winners() const {
        std::vector<bool> won(ranks_.size(), false);
        for (const auto& kv : winner_) won[kv.second] = true;
        return won;
    }
    // The winning offer of a one-chart lookup, or nullopt when nothing was
    // offered.
    std::optional<size_t> only_winner() const {
        if (winner_.empty()) return std::nullopt;
        return winner_.begin()->second;
    }
    const Candidate& rank(size_t index) const { return ranks_[index]; }

private:
    std::vector<Candidate> ranks_;
    std::map<GroupKey, size_t> winner_;  // chart+mode -> index of its best offer
};

// Every row that could answer a lookup: the wanted lens at the wanted cap.
// Which of them wins is WinnerPicker's decision and not SQL's, so there is
// deliberately no ORDER BY or LIMIT here. `a` is the table alias, "" or "r.".
// Appended after a WHERE that already has a term.
void append_candidate_filter(std::string& sql, const char* a, const CapQuery& cap) {
    const std::string p = a;
    sql += " AND " + lens_match(a);
    if (cap.exact) sql += " AND " + p + "sp_cap=?";
    else sql += " AND " + p + "sp_cap>" + std::to_string(kCloneHeroSpCap);
}
// Binds the lens's four parameters, plus one more for an exact cap.
int bind_candidate_filter(sqlite3_stmt* s, int idx, const CapQuery& cap, const Lens& lens) {
    idx = bind_lens(s, idx, lens);
    if (cap.exact) sqlite3_bind_int(s, idx++, *cap.exact);
    return idx;
}

// Rolls back the open transaction, if there still is one. Some failures (a
// full disk, an I/O error) make sqlite roll back by itself, and a second
// ROLLBACK would then throw over the error that caused it.
void rollback_if_open(sqlite3* db) {
    if (!sqlite3_get_autocommit(db)) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
}
```

At the top of the same file, replace:

```cpp
// Stamps every stored row; a mismatch marks the row stale (see reindex /
// drop_stale_records). Single-sourced from CMake's project version.
```

with:

```cpp
// Stamps every stored row; a mismatch marks the row stale (see Candidate).
// Single-sourced from CMake's project version.
```

In `add_row`, replace the first two purges:

```cpp
        // (1) Anything this chart+mode holds that this build cannot read --
        //     another Hydra version's stamp, a sentinel, an older path
        //     layout, a result analyzed under other rules -- is superseded by
        //     a write here. The test is against
        //     what is current, not against this row: a test writing a
        //     deliberately old-stamped row must not take the real rows with
        //     it, and this runs before the insert so the new row is untouched.
        purge("hyhash=? AND chartmode=? AND " + std::string(kRowNotReadySql),
              [&](sqlite3_stmt* s) {
                  bind_text(s, 1, row.hyhash);
                  bind_text(s, 2, row.chartmode);
                  bind_ready_params(s, 3, rules_fingerprint_);
              },
              "unreadable purge");

        // (2) A sentinel at this cap was a placeholder for "some result ran
        //     here"; a real run at that cap is the answer it stood in for.
        purge("hyhash=? AND chartmode=? AND sp_cap=? AND ms_enabled=-1",
              [&](sqlite3_stmt* s) {
                  bind_text(s, 1, row.hyhash);
                  bind_text(s, 2, row.chartmode);
                  sqlite3_bind_int(s, 3, row.sp_cap);
              },
              "sentinel purge");

        // (3) The row this one replaces, deleted explicitly rather than by
```

with:

```cpp
        // (1) Anything this chart+mode holds that this build cannot read --
        //     another Hydra version's stamp (which includes every row an old
        //     migration left), an older path layout, a result analyzed under
        //     other rules -- is superseded by a write here. The test is
        //     against what is current, not against this row: a test writing
        //     a deliberately old-stamped row must not take the real rows with
        //     it, and this runs before the insert so the new row is untouched.
        purge("hyhash=? AND chartmode=? AND NOT " + std::string(kRowReadySql),
              [&](sqlite3_stmt* s) {
                  bind_text(s, 1, row.hyhash);
                  bind_text(s, 2, row.chartmode);
                  bind_ready_params(s, 3, rules_fingerprint_);
              },
              "unreadable purge");

        // (2) The row this one replaces, deleted explicitly rather than by
```

and renumber the two later comments in that function from `// (4) The result, then its paths` to `// (3) The result, then its paths`, and from `// (5) Whatever the replaced row was the last owner of.` to `// (4) Whatever the replaced row was the last owner of.`

Replace the whole of `SummaryLookup RecordStore::get_summary(const RecordKey& key)` with:

```cpp
SummaryLookup RecordStore::get_summary(const RecordKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string sql =
        "SELECT hyversion, bestpath, result_id, substr(structure,1,12)"
        " FROM results WHERE hyhash=? AND chartmode=?";
    append_candidate_filter(sql, "", key.cap);
    Stmt s = prepare(db_, sql.c_str());
    bind_text(s, 1, key.hyhash);
    bind_text(s, 2, key.chartmode);
    bind_candidate_filter(s, 3, key.cap, key.lens);

    WinnerPicker picker;
    std::vector<std::string> bestpaths;  // by offer index
    while (sqlite3_step(s) == SQLITE_ROW) {
        picker.offer(key.hyhash, key.chartmode,
                     rank_row(column_text(s, 0), column_blob(s, 3),
                              sqlite3_column_int64(s, 2), rules_fingerprint_));
        bestpaths.push_back(column_text(s, 1));
    }
    const std::optional<size_t> won = picker.only_winner();
    if (!won) return SummaryLookup{};

    SummaryLookup out;
    // A stale winner is reported as Stale, not hidden: the library's status
    // column has to tell "analyzed by another build" apart from "never
    // analyzed", and only a lookup can say which this is.
    if (!picker.rank(*won).ready()) {
        out.status = RecordStatus::Stale;
        return out;
    }
    out.status = RecordStatus::Ready;
    out.bestpath = std::move(bestpaths[*won]);
    return out;
}
```

Replace the whole of `RecordLookup RecordStore::get_record(const RecordKey& key)` and `std::optional<SongTiming> RecordStore::get_timing(const std::string& hyhash)` (they sit next to each other) with:

```cpp
RecordLookup RecordStore::get_record(const RecordKey& key) {
    RecordLookup out;
    std::vector<uint8_t> structure;
    std::unordered_map<std::string, std::vector<uint8_t>> nodes;
    std::optional<std::vector<uint8_t>> tempomap;
    {
        // The lock covers the reads and nothing else. The winning row, the
        // nodes it names and the song's tempo map are all read under this one
        // lock, so a write in between can never pair one row's shape with
        // another's paths. Decoding happens after the lock is released: the
        // UI thread calls this, and a big record must not hold up the batch
        // writer.
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::string sql =
            "SELECT result_id, hyversion, structure FROM results"
            " WHERE hyhash=? AND chartmode=?";
        append_candidate_filter(sql, "", key.cap);
        Stmt s = prepare(db_, sql.c_str());
        bind_text(s, 1, key.hyhash);
        bind_text(s, 2, key.chartmode);
        bind_candidate_filter(s, 3, key.cap, key.lens);

        struct Row {
            int64_t result_id = 0;
            std::string hyversion;
            std::vector<uint8_t> structure;
        };
        std::vector<Row> rows;  // by offer index
        WinnerPicker picker;
        while (sqlite3_step(s) == SQLITE_ROW) {
            Row row{sqlite3_column_int64(s, 0), column_text(s, 1), column_blob(s, 2)};
            // The whole blob is here, so its leading twelve bytes are the
            // structure head the format and rules check wants.
            picker.offer(key.hyhash, key.chartmode,
                         rank_row(row.hyversion, row.structure, row.result_id,
                                  rules_fingerprint_));
            rows.push_back(std::move(row));
        }
        const std::optional<size_t> won = picker.only_winner();
        if (!won) return RecordLookup{};
        Row& best = rows[*won];

        out.hyversion = best.hyversion;
        // Stamped by a different version or holding a path tree in an older
        // layout or under other rules: nothing stored is decoded at all.
        // Callers see Stale and prompt a re-analyze.
        if (!picker.rank(*won).ready()) {
            out.status = RecordStatus::Stale;
            const StaleReasons why =
                stale_reasons(best.hyversion, best.structure, rules_fingerprint_);
            out.stale_build = why.build;
            out.stale_rules = why.rules;
            return out;
        }
        structure = std::move(best.structure);
        nodes = load_nodes(best.result_id);
        tempomap = read_tempomap(key.hyhash);
    }

    out.status = RecordStatus::Ready;
    HydraRecord record = rebuild_record(
        structure, [&nodes](const std::string& hash) -> const std::vector<uint8_t>* {
            auto it = nodes.find(hash);
            return it == nodes.end() ? nullptr : &it->second;
        });
    // The tempomap is decoded once, here, and handed back with the record --
    // the display layer needs the same timing and must not query for it again.
    if (tempomap) {
        out.timing = decode_tempomap(*tempomap);
        restore_timecodes(record, *out.timing);
    }
    out.record = std::move(record);
    return out;
}

std::optional<std::vector<uint8_t>> RecordStore::read_tempomap(const std::string& hyhash) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_, "SELECT tempomap FROM songmeta WHERE hyhash=?");
    bind_text(s, 1, hyhash);
    if (sqlite3_step(s) != SQLITE_ROW) return std::nullopt;
    return column_blob(s, 0);
}

std::optional<SongTiming> RecordStore::get_timing(const std::string& hyhash) {
    // Read under the lock, decoded outside it.
    const std::optional<std::vector<uint8_t>> blob = read_tempomap(hyhash);
    if (!blob) return std::nullopt;
    return decode_tempomap(*blob);
}
```

In `src/store/record_store.h`, in the private section, right after `void create_result_tables();`, add:

```cpp
    // The song's raw tempomap blob, read under the lock; the caller decodes it
    // with no lock held. nullopt if the song isn't registered.
    std::optional<std::vector<uint8_t>> read_tempomap(const std::string& hyhash);
```

In `has_record`, replace the comment:

```cpp
    // The exact lens, never a sentinel: "already analyzed" has to mean "under
    // these settings", or a batch run skips charts whose stored answer came
    // from a different question.
```

with:

```cpp
    // The exact lens: "already analyzed" has to mean "under these settings",
    // or a batch run skips charts whose stored answer came from a different
    // question.
```

In `for_each_blob`, replace the listing query's column list:

```cpp
            "SELECT s.hyhash, s.ref_name, s.ref_artist, s.ref_charter, "
            "r.chartmode, r.hyversion, r.sp_cap, r.result_id, r.structure, "
            "r.ms_enabled "
            "FROM results r JOIN songmeta s ON s.hyhash = r.hyhash WHERE 1=1";
```

with:

```cpp
            "SELECT s.hyhash, s.ref_name, s.ref_artist, s.ref_charter, "
            "r.chartmode, r.hyversion, r.sp_cap, r.result_id, r.structure "
            "FROM results r JOIN songmeta s ON s.hyhash = r.hyhash WHERE 1=1";
```

and replace the loop that ranks the rows, from `std::vector<Row> candidates;` through `if (keep[i]) rows.push_back(std::move(candidates[i]));`:

```cpp
        std::vector<Row> candidates;
        std::vector<Candidate> ranks;
        std::map<GroupKey, size_t> winner;  // chart+mode -> index of its best row
        while (sqlite3_step(s) == SQLITE_ROW) {
            Row row;
            row.meta.hyhash = column_text(s, 0);
            row.meta.ref_name = column_text(s, 1);
            row.meta.ref_artist = column_text(s, 2);
            row.meta.ref_charter = column_text(s, 3);
            row.meta.chartmode = column_text(s, 4);
            row.meta.hyversion = column_text(s, 5);
            row.meta.sp_cap = sqlite3_column_int(s, 6);
            row.result_id = sqlite3_column_int64(s, 7);
            row.structure = column_blob(s, 8);
            const int ms_enabled = sqlite3_column_int(s, 9);
            // Both helpers read only the blob's leading twelve bytes, and take
            // the whole blob or just that head -- see structure_is_current.
            row.meta.status =
                row_is_ready(row.meta.hyversion, ms_enabled, row.structure, rules_fingerprint_)
                    ? RecordStatus::Ready
                    : RecordStatus::Stale;

            const Candidate rank = rank_row(row.meta.hyversion, ms_enabled, row.structure,
                                            row.result_id, rules_fingerprint_);
            const GroupKey key{row.meta.hyhash, row.meta.chartmode};
            auto it = winner.find(key);
            if (it == winner.end()) winner.emplace(key, candidates.size());
            else if (outranks(rank, ranks[it->second])) it->second = candidates.size();
            ranks.push_back(rank);
            candidates.push_back(std::move(row));
        }

        // One row per chart and mode, the same one a lookup would pick, kept
        // in result_id order. A stale winner is still yielded: this is the
        // export path, and dropping a row here would lose it for good.
        std::vector<bool> keep(candidates.size(), false);
        for (const auto& kv : winner) keep[kv.second] = true;
        for (size_t i = 0; i < candidates.size(); ++i)
            if (keep[i]) rows.push_back(std::move(candidates[i]));
```

with:

```cpp
        std::vector<Row> candidates;  // by offer index
        WinnerPicker picker;
        while (sqlite3_step(s) == SQLITE_ROW) {
            Row row;
            row.meta.hyhash = column_text(s, 0);
            row.meta.ref_name = column_text(s, 1);
            row.meta.ref_artist = column_text(s, 2);
            row.meta.ref_charter = column_text(s, 3);
            row.meta.chartmode = column_text(s, 4);
            row.meta.hyversion = column_text(s, 5);
            row.meta.sp_cap = sqlite3_column_int(s, 6);
            row.result_id = sqlite3_column_int64(s, 7);
            row.structure = column_blob(s, 8);
            // rank_row reads only the blob's leading twelve bytes, so the
            // whole blob serves as its head.
            const Candidate rank = rank_row(row.meta.hyversion, row.structure,
                                            row.result_id, rules_fingerprint_);
            row.meta.status = rank.ready() ? RecordStatus::Ready : RecordStatus::Stale;
            picker.offer(row.meta.hyhash, row.meta.chartmode, rank);
            candidates.push_back(std::move(row));
        }

        // One row per chart and mode, the same one a lookup would pick, kept
        // in result_id order. A stale winner is still yielded: this is the
        // export path, and dropping a row here would lose it for good.
        const std::vector<bool> keep = picker.winners();
        for (size_t i = 0; i < candidates.size(); ++i)
            if (keep[i]) rows.push_back(std::move(candidates[i]));
```

Delete `int RecordStore::drop_stale_records()` whole. In `reindex`, replace:

```cpp
    struct Row {
        int64_t result_id;
        std::string hyversion;
        int ms_enabled;
        std::vector<uint8_t> structure;
    };
    std::vector<Row> rows;
    {
        Stmt s = prepare(db_, "SELECT result_id, hyversion, ms_enabled, structure"
                              " FROM results ORDER BY result_id");
        while (sqlite3_step(s) == SQLITE_ROW)
            rows.push_back({sqlite3_column_int64(s, 0), column_text(s, 1),
                            sqlite3_column_int(s, 2), column_blob(s, 3)});
    }

    int done = 0;
    for (const Row& row : rows) {
        // A stale or sentinel row gets empty summaries: its stored bytes are
        // not this build's to read, so there is nothing to recompute from.
        PathSummary summary;
        if (row_is_ready(row.hyversion, row.ms_enabled, row.structure, rules_fingerprint_)) {
```

with:

```cpp
    struct Row {
        int64_t result_id;
        std::string hyversion;
        std::vector<uint8_t> structure;
    };
    std::vector<Row> rows;
    {
        Stmt s = prepare(db_, "SELECT result_id, hyversion, structure"
                              " FROM results ORDER BY result_id");
        while (sqlite3_step(s) == SQLITE_ROW)
            rows.push_back({sqlite3_column_int64(s, 0), column_text(s, 1), column_blob(s, 2)});
    }

    int done = 0;
    for (const Row& row : rows) {
        // A stale row gets empty summaries: its stored bytes are not this
        // build's to read, so there is nothing to recompute from.
        PathSummary summary;
        if (rank_row(row.hyversion, row.structure, row.result_id, rules_fingerprint_).ready()) {
```

Replace the whole of `std::vector<RecordListing> RecordStore::list_records(...)` with:

```cpp
std::vector<RecordListing> RecordStore::list_records(
    const std::optional<std::string>& chartmode, const CapQuery& cap, const Lens& lens,
    SortColumn order_by, bool descending, std::optional<int> limit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::string sql =
        "SELECT s.hyhash, s.ref_name, s.ref_artist, s.ref_charter, r.chartmode, r.bestpath, "
        "r.score, r.actcount, r.maxskip, r.hardest_ms, r.avgmult, r.notecount, "
        "r.sqin_count, r.sqout_count, r.pathcount, r.sp_cap, "
        "r.hyversion, r.result_id, substr(r.structure,1,12) "
        "FROM results r JOIN songmeta s ON s.hyhash = r.hyhash WHERE 1=1";
    if (chartmode) sql += " AND r.chartmode = ?";
    append_candidate_filter(sql, "r.", cap);

    // The sort stays in SQL, so the listing keeps sqlite's own ordering; the
    // passes below only drop rows, never reorder them. The limit cannot stay
    // here: a SQL LIMIT would count rows that are about to be dropped and hand
    // back fewer than the caller asked for.
    const char* prefix = sort_column_is_songmeta(order_by) ? "s." : "r.";
    sql += " ORDER BY ";
    sql += prefix;
    sql += sort_column_name(order_by);
    sql += descending ? " DESC" : " ASC";

    Stmt s = prepare(db_, sql.c_str());
    int idx = 1;
    if (chartmode) bind_text(s, idx++, *chartmode);
    bind_candidate_filter(s, idx, cap, lens);

    std::vector<RecordListing> candidates;  // by offer index
    WinnerPicker picker;
    while (sqlite3_step(s) == SQLITE_ROW) {
        RecordListing listing;
        listing.hyhash = column_text(s, 0);
        listing.ref_name = column_text(s, 1);
        listing.ref_artist = column_text(s, 2);
        listing.ref_charter = column_text(s, 3);
        listing.chartmode = column_text(s, 4);
        listing.bestpath = column_text(s, 5);
        listing.summary = read_summary(s, 6);
        listing.sp_cap = sqlite3_column_int(s, 15);
        picker.offer(listing.hyhash, listing.chartmode,
                     rank_row(column_text(s, 16), column_blob(s, 18),
                              sqlite3_column_int64(s, 17), rules_fingerprint_));
        candidates.push_back(std::move(listing));
    }

    // A listing shows only what this build can read. A stale winner takes its
    // chart out of the listing rather than handing the place to the next
    // candidate -- a chart whose answer nobody can read must read the same as
    // a chart nobody has analyzed. A negative limit means no limit, matching
    // sqlite's own LIMIT convention.
    const std::vector<bool> keep = picker.winners();
    std::vector<RecordListing> out;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!keep[i] || !picker.rank(i).ready()) continue;
        if (limit && *limit >= 0 && out.size() >= static_cast<size_t>(*limit)) break;
        out.push_back(std::move(candidates[i]));
    }
    return out;
}
```

In `src/store/record_store.h`, replace the Lens comment and the sentinel members:

```cpp
    // 1 = ms limit on, 0 = off, -1 = a sentinel (below).
    int ms_enabled = 0;
```

with:

```cpp
    // 1 = ms limit on, 0 = off. Rows an older Hydra migrated in carry -1
    // ("settings unknown"); no lens has it, so no lookup finds them.
    int ms_enabled = 0;
```

and delete the block from `    // A row migrated or imported from an older database: it has a result, but` through `    bool is_sentinel() const { return ms_enabled == -1; }`. Replace the `RecordStatus` comment:

```cpp
// What a stored-record lookup found. The store is the only place that decides
// whether a row is usable: NotAnalyzed (no row at all), Stale (a row another
// Hydra version wrote, or one migrated in with unknown settings -- either way
// its contents are not trusted and its blob is never decoded), or Ready (a
// real result -- which may legitimately have zero paths).
```

with:

```cpp
// What a stored-record lookup found. The store is the only place that decides
// whether a row is usable: NotAnalyzed (no row at all), Stale (a row another
// Hydra version wrote, in an older path layout, or under other rules -- its
// contents are not trusted and its blob is never decoded), or Ready (a real
// result -- which may legitimately have zero paths).
```

Replace:

```cpp
    bool stale_build = false;  // another Hydra build, an older path layout, or a migrated row
```

with:

```cpp
    bool stale_build = false;  // another Hydra build or an older path layout
```

Replace the comment above `has_record`:

```cpp
    // True when a current-version record exists for this exact key -- cap and
    // lens both -- the "skip, already analyzed" test for a batch run. Stale
    // rows and sentinel rows don't count, and neither does a result from
    // different settings.
```

with:

```cpp
    // True when a current-version record exists for this exact key -- cap and
    // lens both -- the "skip, already analyzed" test for a batch run. Stale
    // rows don't count, and neither does a result from different settings.
```

Delete the `drop_stale_records` declaration and its comment (from `    // Removes results that no longer match this store's current version, plus` through `    int drop_stale_records();`).

Now fix the existing tests that used the deleted names. In `tests/test_store.cpp`:

Rename `TEST_CASE("RecordStore maintenance: has_record, list_records, reindex, drop_stale_records")` to `TEST_CASE("RecordStore maintenance: has_record, list_records, reindex")`, and replace its ending:

```cpp
    // A row stamped with a different version is stale for this store: it
    // doesn't count as "already analyzed", and drop_stale_records removes it.
    PreparedRow stale =
        prepare_row(RecordKey{"h2", "Expert Pro Drums, 2x Bass", at4}, *record);
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.counts().second == 2);
    CHECK_FALSE(store.has_record(RecordKey{"h2", "Expert Pro Drums, 2x Bass", at4}));

    int dropped = store.drop_stale_records();
    CHECK(dropped == 1);
    CHECK(store.counts().second == 1);
}
```

with:

```cpp
    // A row stamped with a different version is stale for this store: it
    // doesn't count as "already analyzed".
    PreparedRow stale =
        prepare_row(RecordKey{"h2", "Expert Pro Drums, 2x Bass", at4}, *record);
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    CHECK(store.counts().second == 2);
    CHECK_FALSE(store.has_record(RecordKey{"h2", "Expert Pro Drums, 2x Bass", at4}));
}
```

In `TEST_CASE("a row in an older path format is Stale even when this build stamped it")`, delete:

```cpp
    // drop_stale_records treats it the same as any other unreadable row.
    CHECK(store.drop_stale_records() == 1);
    CHECK(store.counts().second == 0);

```

In `TEST_CASE("the listing and a lookup agree on which row is a chart's answer")`, replace:

```cpp
    // A current row and a taller sentinel: a real result wins.
    PreparedRow sentinel =
        prepare_row(RecordKey{"over_sentinel", "mode", CapQuery::at(64)}, at_cap(64));
    sentinel.lens = Lens::sentinel();
    store.add_row(sentinel);
```

with:

```cpp
    // A current row and a taller row an old migration left: the migrated row
    // is no candidate at all.
    PreparedRow sentinel =
        prepare_row(RecordKey{"over_sentinel", "mode", CapQuery::at(64)}, at_cap(64));
    sentinel.lens.ms_enabled = -1;
    store.add_row(sentinel);
```

In `TEST_CASE("RecordKey compares on every part of the identity")`, delete:

```cpp

    // The sentinel is its own thing and never equals a real lens.
    CHECK(Lens::sentinel().is_sentinel());
    CHECK_FALSE(Lens{}.is_sentinel());
    CHECK(Lens::sentinel() != Lens{});
```

In `TEST_CASE("replacing one lens's result leaves the other's bytes untouched")`, delete from `    // drop_stale_records collects the orphans it makes.` through the second `CHECK(scalar(path, "SELECT COUNT(*) FROM path_refs") == 0);` (the one right after the `drop_stale_records() == 1` block), keeping the final `std::remove(path.c_str());`.

- [ ] **Step 6: The rebuild opens its transaction first, and copies the scanned names.** Replace the whole of `void RecordStore::rebuild_chart_library(const std::vector<ChartLibraryEntry>& items)` with:

```cpp
void RecordStore::rebuild_chart_library(const std::vector<ChartLibraryEntry>& items) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // One transaction around the whole swap, opened before anything changes:
    // a failure anywhere rolls back to the previous scan's rows, which are
    // also the rescan cache. The table is emptied rather than dropped, so its
    // columns and index stay as they are.
    exec("BEGIN");
    try {
        exec("DELETE FROM charts");
        Stmt s = prepare(db_,
            "INSERT INTO charts (md5, name, artist, charter, path, folder, sig)"
            " VALUES (?,?,?,?,?,?,?)");
        for (const ChartLibraryEntry& item : items) {
            sqlite3_reset(s);
            bind_text(s, 1, item.md5);
            bind_text(s, 2, item.title);
            bind_text(s, 3, item.artist);
            bind_text(s, 4, item.charter);
            bind_text(s, 5, item.notespath);
            bind_text(s, 6, item.rootfolder);
            bind_text(s, 7, item.sig);
            if (sqlite3_step(s) != SQLITE_DONE)
                throw std::runtime_error(std::string("rebuild_chart_library failed: ") +
                                         sqlite3_errmsg(db_));
        }
        // Song names follow song.ini (user decision 2026-09-26): a chart that
        // already has a song row takes the names this scan read. When the
        // scan found the same chart twice, the first copy it listed names it
        // (sqlite takes a bare column from the MIN(rowid) row).
        exec("UPDATE songmeta SET ref_name = c.name, ref_artist = c.artist,"
             " ref_charter = c.charter"
             " FROM (SELECT md5, name, artist, charter, MIN(rowid) FROM charts GROUP BY md5)"
             " AS c WHERE songmeta.hyhash = c.md5");
        exec("COMMIT");
    } catch (...) {
        rollback_if_open(db_);
        throw;
    }
}
```

In `src/store/record_store.h`, replace the comment above `rebuild_chart_library`:

```cpp
    // Replaces the whole library with `items`: a scan always fully
    // supersedes the previous one.
```

with:

```cpp
    // Replaces the whole library with `items`: a scan always fully
    // supersedes the previous one. All or nothing: a failure keeps the
    // previous scan's rows. Songs that already have a row take the names
    // this scan read (the first copy wins when a chart appears twice).
```

- [ ] **Step 7: add_song becomes an upsert.** In `RecordStore::add_song`, replace:

```cpp
    Stmt s = prepare(db_,
        "INSERT OR IGNORE INTO songmeta (hyhash, ref_name, ref_artist, ref_charter, tempomap) "
        "VALUES (?,?,?,?,?)");
```

with:

```cpp
    // A chart already registered takes the names this call carries. They
    // come from the scan, so a fixed song.ini reaches the reports on the next
    // analysis (user decision 2026-09-26). The tempo map is keyed by the same
    // content hash, so it cannot have changed and is left alone.
    Stmt s = prepare(db_,
        "INSERT INTO songmeta (hyhash, ref_name, ref_artist, ref_charter, tempomap) "
        "VALUES (?,?,?,?,?) "
        "ON CONFLICT(hyhash) DO UPDATE SET ref_name = excluded.ref_name, "
        "ref_artist = excluded.ref_artist, ref_charter = excluded.ref_charter");
```

In `src/store/record_store.h`, replace:

```cpp
    // Registers a song so records can be stored against it. Idempotent (INSERT
    // OR IGNORE).
```

with:

```cpp
    // Registers a song so records can be stored against it. Registering it
    // again updates its names and keeps its tempo map.
```

- [ ] **Step 8: Bring the domain docs along.** In `CONTEXT.md`, in the **Record** entry, replace:

```
so a path found under several combinations is stored once. A record is stale
unless all three hold: this build's version stamped it, it records which
settings it ran under, and its stored paths are in this build's
path-structure format. A lookup reports it as one of three statuses: not
```

with:

```
so a path found under several combinations is stored once. A record is stale
unless both hold: this build's version stamped it, and its stored paths are in
this build's path-structure format under the rules in force. A lookup reports
it as one of three statuses: not
```

In `docs/adr/0009-analysis-settings-key-a-record-paths-stored-once.md`, add at the end of the file:

```
## Note, 2026-09-26

The migrations that brought 1.6 and older databases into this schema are
gone, and so is the Uncapped import (user decision 5 of the 2026-09-26 audit
plan). An old `records` table is left in the file, unread. Rows those
migrations already wrote carry `ms_enabled = -1`; no lens has that value, so
lookups never see them and the chart reads Not analyzed. The next analysis of
the chart deletes them along with any other row this build cannot read.
```

- [ ] **Step 9: Build and run everything.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`. Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test passes. Run the grep from the acceptance list; expected: no output. Build `hydra_batch` with `.\build_cpp.ps1 -Target hydra_batch` and run the score-neutral proof with a fresh `$out` folder; expected: `Compare-Object` prints nothing.

- [ ] **Step 10: Commit.**

```bash
git add src/store/record_store.h src/store/record_store.cpp src/store/serialize.h src/store/serialize.cpp src/app/config.h src/app/config.cpp tests/test_store.cpp README.md docs/UserGuide.md installer/hydra.iss CONTEXT.md docs/adr/0009-analysis-settings-key-a-record-paths-stored-once.md
git commit -m "Store: rebuild in one transaction, decode unlocked, one winner rule, drop dead imports

Task: Task 1: The record store stops losing the library, stops holding its lock while decoding, and has one rule for which row wins
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/store/record_store.h","src/store/record_store.cpp","src/store/serialize.h","src/store/serialize.cpp","src/app/config.h","src/app/config.cpp","tests/test_store.cpp","README.md","docs/UserGuide.md","installer/hydra.iss","CONTEXT.md","docs/adr/0009-analysis-settings-key-a-record-paths-stored-once.md"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe","acceptanceCriteria":["The seven named test cases pass","hydra_tests.exe ends Status: SUCCESS!","hydra_uitest.exe --all passes","The dead-name grep prints nothing","get_record decodes after its locked block","Score-neutral proof prints nothing"],"modelTier":"standard"}
```

---

### Task 2: Engine cleanup and interfaces

The engine carries a pile of things nobody reads. `ScoreGraph` has three getters with no callers (`length()`, `sp_start()`, `fill_rule()`) and a flag it sets but never checks (`head_time_set_`). The engine's output record reserves an `OutAct::sqout_tick` that is always -1. Every activation edge keeps a `FrontendSqueeze` whose chord nobody reads; only its points are used. The base track copies every SP extension map, but only the SP track looks inside one. `cymbal_flip()` has no callers, `allows_dynamics()` has returned true for every lane since ADR 0012, `MeasureIndex` takes a `tick_r` it throws away, and `windows_for_path` takes a `Song` it never reads. All of that goes. None of it is stored, so no record changes.

Three interfaces get safer. `run_search` takes its knobs as loose arguments (two booleans in a row, then a raw pointer), so two swapped flags would still compile. They become one `EngineOptions` value with named fields. `Path::all_activations()` deep-copies every activation, backends and all, just so a caller can loop over them. `Path` gains `walk_activations()`, a read-only view that copies nothing. This task switches the model's own methods, the store's summary, the targeted search and the replay code to it. The Paths tab and Preview callers (path_view.cpp, preview_view.cpp) are T11's, and the report's (report.cpp) is T5's. `rate_activation` recomputes the transfer scales live whenever a tempo map is at hand, and reads the stored ones only otherwise. That is the "re-derive when you can" pattern ADRs 0011, 0013 and 0014 removed everywhere else. It now reads the stored scales only. A corpus test first proves stored equals live for every activation that comes back Ready from a real store round trip. If any differ, the task stops and reports.

Last, `analyze_chart` runs Clone Hero's 4-bar cap down its own branch, with the graph built a flat 4 bars tall. Every other fixed cap builds the graph only as tall as the song has phrases. The two give the same answer, because a song with p phrases never holds more than p bars. A test proves that byte for byte before the 4-bar branch folds into the general one. If the test fails on any song, that part stops and the branch stays.

What the user sees: nothing.

**Depends on:** nothing.

**Expected overlaps:** T1 and T9 edit src/store/record_store.cpp; this task touches only `summarize_path` there (one line). T4 may edit tools/replay.cpp; this task touches only the failure printout loop in its self-check. T6 and T11 edit src/app/path_view.cpp; this task changes only the `rate_activation(...)` call inside `build_activations`. T10 may edit src/search/pather.cpp (the Auto time budget); this task edits `read`, `search_allzero`, `search_target` and `analyze_chart`. T12 and T13 build on everything here.

**Goal:** Delete the engine's dead members, give `run_search` one options struct, add a non-copying activation walk, make `rate_activation` read stored scales only, and fold `analyze_chart`'s duplicate 4-bar branch.

**Files:**
- Modify: `src/search/engine.h`, `src/search/engine.cpp`, `src/search/graph.h`, `src/search/graph.cpp`, `src/search/pather.cpp`
- Modify: `src/core/model.h`, `src/core/model.cpp`, `src/core/squeeze_rating.h`, `src/core/squeeze_rating.cpp`, `src/core/timing.h`, `src/core/timing.cpp`, `src/core/replay.h`, `src/core/replay.cpp`
- Modify: `src/app/path_view.cpp`, `src/store/record_store.cpp`, `tools/replay.cpp`
- Test: `tests/test_search.cpp`, `tests/test_store.cpp`, `tests/test_model.cpp`, `tests/test_squeeze_rating.cpp`, `tests/test_rules.cpp`, `tests/test_preview_view.cpp`, `tests/test_timing.cpp`, `tests/test_replay.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="stored transfer scales equal a live recompute after a store round trip"` passes on the unchanged code before `rate_activation` is touched, and again at the end.
- [ ] `hydra_tests.exe -tc="a 4-bar graph built at the song's phrase count stores the same paths"` passes before the 4-bar branch is removed.
- [ ] `hydra_tests.exe -tc="run_search: EngineOptions carries each knob to the engine"`, `-tc="Path::walk_activations*"` and `-tc="rate_activation: the stored scales are the only scales"` pass.
- [ ] The full `hydra_tests.exe` run prints `Status: SUCCESS!`.
- [ ] `hydra_uitest.exe --all` passes.
- [ ] This prints nothing: `Get-ChildItem src,tests,tools -Recurse -Include *.cpp,*.h | Select-String -Pattern 'sp_start\(\)|fill_rule\(\)|head_time_set_|cymbal_flip|allows_dynamics|FrontendSqueeze|oa\.sqout_tick|\blength_\b|run_search\(graph, DepthMode'`
- [ ] The score-neutral batch diff (brief recipe) prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` -> `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test for `EngineOptions`.** In tests/test_search.cpp, add this case right after the TEST_CASE "SP past the last note: synthesized rows survive a store round-trip" (it uses `build_tail_song` from the anonymous namespace above it):

```cpp
// run_search takes its knobs in one EngineOptions value, so no two flags can
// be swapped at a call site. Each knob must still do its own job.
TEST_CASE("run_search: EngineOptions carries each knob to the engine") {
    Song song = build_tail_song({{0, true, false},
                                 {768, true, false},
                                 {1536},
                                 {2304, false, true},
                                 {3072},
                                 {3840},
                                 {4608},
                                 {5136},
                                 {5280}});
    ScoreGraph graph(song, 4);

    // The defaults are a plain best-path search: score depth 0, no limit.
    const std::vector<Path> best = run_search(graph, EngineOptions{});
    REQUIRE_FALSE(best.empty());
    REQUIRE_FALSE(best.front().activations.empty());

    // no_skips plus a hard 0 ms limit is exactly the all-0 search.
    EngineOptions allzero;
    allzero.ms_filter = 0.0;
    allzero.no_skips = true;
    allzero.hard_ms_filter = true;
    const std::vector<Path> z = run_search(graph, allzero);
    const std::vector<Path> want_z = search_allzero(graph);
    REQUIRE_FALSE(want_z.empty());
    REQUIRE(z.size() == want_z.size());
    for (size_t i = 0; i < z.size(); ++i) {
        CHECK(z[i].pathstring() == want_z[i].pathstring());
        CHECK(z[i].totalscore() == want_z[i].totalscore());
    }

    // Pinning the best path's activation ticks hands that path back.
    EngineOptions pinned;
    pinned.depth_mode = DepthMode::Points;
    pinned.depth_value = 1'000'000'000;
    std::vector<int64_t> ticks;
    for (const Activation& a : best.front().activations) ticks.push_back(a.timecode->ticks());
    pinned.target_act_ticks = ticks;
    const std::vector<Path> again = run_search(graph, pinned);
    REQUIRE_FALSE(again.empty());
    CHECK(again.front().pathstring() == best.front().pathstring());
    CHECK(again.front().totalscore() == best.front().totalscore());
}
```

- [ ] **Step 2: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error, because `EngineOptions` does not exist.

- [ ] **Step 3: Add `EngineOptions` and the new `run_search` signature.** In src/search/engine.h, add `#include <cstdint>` to the includes. Replace this whole block:

```cpp
// Run the BFS over the graph and return finished, best-score-first,
// variant-prepared Paths.
// Throws std::runtime_error if the search reaches a broken state.
// on_progress, if set, receives a monotonic 0..1 fraction as the BFS frontier
// sweeps the chart. Lets the UI show a real progress bar for a heavy chart
// instead of an indeterminate spinner.
// no_skips constrains the search to paths whose activations all record
// skips == 0 -- the "all-0" path a player hits by activating at every first
// opportunity. It removes all activation branching, so such a search is far
// cheaper than an unconstrained one.
// hard_ms_filter turns ms_filter from a preference into a requirement. By
// default an over-limit path still survives while nothing outscores it, so the
// best path a search reports can need more timing than the limit allows; with
// this set, an over-limit path is dropped outright.
// target_act_ticks, when non-null, pins the activation set: a sorted list of
// node ticks where the search MUST activate, and nowhere else. Every other
// activation opportunity is declined. It replaces no_skips's rule for the same
// branch point, so the search returns exactly one path -- the caller's -- with
// all its squeeze variants, priced the engine's own way. An unrealizable set
// (an activation with SP under 2 bars, a fill the engine cannot spawn in time,
// a tick that is not a fill node) empties the frontier, which surfaces as the
// usual std::runtime_error.
std::vector<Path> run_search(const ScoreGraph& graph, DepthMode depth_mode,
                             int depth_value, std::optional<double> ms_filter,
                             bool no_skips = false,
                             bool hard_ms_filter = false,
                             const std::function<void(float)>& on_progress = {},
                             const std::vector<int64_t>* target_act_ticks = nullptr);
```

with:

```cpp
// Everything one run_search call can be asked to do, in one value, so no two
// flags can be swapped at a call site. The defaults are a plain best-path
// search: score depth 0, no timing limit, no constraints.
struct EngineOptions {
    // Which losing paths to keep (see DepthMode) and how many.
    DepthMode depth_mode = DepthMode::Scores;
    int depth_value = 0;
    // The ms limit; nullopt is off.
    std::optional<double> ms_filter;
    // Only paths whose activations all record skips == 0: the "all-0" path a
    // player hits by activating at every first opportunity. It removes all
    // activation branching, so such a search is far cheaper.
    bool no_skips = false;
    // Make ms_filter a requirement. By default an over-limit path still
    // survives while nothing outscores it, so the best path can need more
    // timing than the limit allows; with this set it is dropped outright.
    bool hard_ms_filter = false;
    // When set, the node ticks the search must activate at, ascending, and
    // nowhere else. It replaces no_skips's rule for the same branch point, so
    // the search returns exactly one path (the caller's) with all its squeeze
    // variants, priced the engine's own way. An unrealizable set (an
    // activation with SP under 2 bars, a fill the engine cannot spawn in time,
    // a tick that is not a fill node) empties the frontier, which surfaces as
    // the usual std::runtime_error.
    std::optional<std::vector<int64_t>> target_act_ticks;
};

// Run the BFS over the graph and return finished, best-score-first,
// variant-prepared Paths.
// Throws std::runtime_error if the search reaches a broken state.
// on_progress, if set, receives a monotonic 0..1 fraction as the BFS frontier
// sweeps the chart, so the UI can show a real progress bar.
std::vector<Path> run_search(const ScoreGraph& graph, const EngineOptions& options,
                             const std::function<void(float)>& on_progress = {});
```

In src/search/engine.cpp, replace the `Engine` constructor:

```cpp
    Engine(const Enum& en, bool has_sp_cap, int32_t sp_cap, DepthMode depth_mode,
           int32_t depth_value, bool has_ms_filter, double ms_filter,
           bool no_skips, bool hard_ms_filter, double backend_leeway_ms,
           int32_t max_tied_paths,
           const std::vector<int64_t>* target_act_ticks = nullptr)
        : en_(en),
          backend_leeway_ms_(backend_leeway_ms),
          max_tied_paths_(max_tied_paths),
          has_sp_cap_(has_sp_cap),
          sp_cap_(sp_cap),
          depth_mode_(depth_mode),
          depth_value_(depth_value),
          has_ms_filter_(has_ms_filter),
          ms_filter_(ms_filter),
          no_skips_(no_skips),
          hard_ms_filter_(hard_ms_filter),
          target_act_ticks_(target_act_ticks) {}
```

with:

```cpp
    // `options` must outlive the engine: target_act_ticks_ points into it.
    Engine(const Enum& en, bool has_sp_cap, int32_t sp_cap,
           const EngineOptions& options, double backend_leeway_ms,
           int32_t max_tied_paths)
        : en_(en),
          backend_leeway_ms_(backend_leeway_ms),
          max_tied_paths_(max_tied_paths),
          has_sp_cap_(has_sp_cap),
          sp_cap_(sp_cap),
          depth_mode_(options.depth_mode),
          depth_value_(options.depth_value),
          has_ms_filter_(options.ms_filter.has_value()),
          ms_filter_(options.ms_filter.value_or(0.0)),
          no_skips_(options.no_skips),
          hard_ms_filter_(options.hard_ms_filter),
          target_act_ticks_(options.target_act_ticks ? &*options.target_act_ticks
                                                     : nullptr) {}
```

At the bottom of engine.cpp, replace the `run_search` definition's head and engine construction:

```cpp
std::vector<MPath> run_search(const ScoreGraph& graph, DepthMode depth_mode,
                              int depth_value, std::optional<double> ms_filter,
                              bool no_skips, bool hard_ms_filter,
                              const std::function<void(float)>& on_progress,
                              const std::vector<int64_t>* target_act_ticks) {
    Enum en = enumerate(graph);

    const bool has_cap = graph.sp_meter_cap().has_value();
    const int32_t cap = static_cast<int32_t>(graph.sp_meter_cap().value_or(0));

    Engine engine(en, has_cap, cap, depth_mode, depth_value,
                  ms_filter.has_value(), ms_filter.value_or(0.0),
                  no_skips, hard_ms_filter, graph.rules().backend_leeway_ms,
                  static_cast<int32_t>(graph.rules().max_tied_paths),
                  target_act_ticks);
```

with:

```cpp
std::vector<MPath> run_search(const ScoreGraph& graph, const EngineOptions& options,
                              const std::function<void(float)>& on_progress) {
    Enum en = enumerate(graph);

    const bool has_cap = graph.sp_meter_cap().has_value();
    const int32_t cap = static_cast<int32_t>(graph.sp_meter_cap().value_or(0));

    Engine engine(en, has_cap, cap, options, graph.rules().backend_leeway_ms,
                  static_cast<int32_t>(graph.rules().max_tied_paths));
```

- [ ] **Step 4: Move the three production callers.** In src/search/pather.cpp, inside `read`, replace:

```cpp
    record.paths = run_search(graph, depth_mode, depth_value, ms_filter,
                              /*no_skips=*/false, /*hard_ms_filter=*/false,
                              on_progress);
```

with:

```cpp
    EngineOptions options;
    options.depth_mode = depth_mode;
    options.depth_value = depth_value;
    options.ms_filter = ms_filter;
    record.paths = run_search(graph, options, on_progress);
```

Inside `search_allzero`, replace:

```cpp
        paths = run_search(graph, DepthMode::Scores, /*depth_value=*/0,
                           /*ms_filter=*/0.0,
                           /*no_skips=*/true, /*hard_ms_filter=*/true,
                           on_progress);
```

with:

```cpp
        EngineOptions options;  // score depth 0: only the top score, plus its ties
        options.ms_filter = 0.0;
        options.no_skips = true;
        options.hard_ms_filter = true;
        paths = run_search(graph, options, on_progress);
```

Inside `search_target`, replace:

```cpp
        paths = run_search(graph, DepthMode::Points, /*depth_value=*/1'000'000'000,
                           /*ms_filter=*/std::nullopt, /*no_skips=*/false,
                           /*hard_ms_filter=*/false, {}, &ticks);
```

with:

```cpp
        EngineOptions options;
        options.depth_mode = DepthMode::Points;
        options.depth_value = 1'000'000'000;
        options.target_act_ticks = ticks;
        paths = run_search(graph, options);
```

- [ ] **Step 5: Move the test callers.** With the Edit tool and `replace_all`, in each of tests/test_search.cpp, tests/test_rules.cpp and tests/test_preview_view.cpp, make these three replacements (a file that lacks a string is skipped):
  - `run_search(graph, DepthMode::Scores, 0, std::nullopt, false)` becomes `run_search(graph, EngineOptions{})`
  - `run_search(graph, DepthMode::Scores, 0, std::nullopt)` becomes `run_search(graph, EngineOptions{})`
  - `run_search(graph, DepthMode::Scores, 1, std::nullopt)` becomes `run_search(graph, EngineOptions{DepthMode::Scores, 1})`

  Then `Get-ChildItem tests -Recurse -Include *.cpp | Select-String 'run_search\(graph, DepthMode'` must print nothing.

- [ ] **Step 6: Run it and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="run_search*","search*","legacy*"`. Expected: `Status: SUCCESS!`.

- [ ] **Step 7: Commit.**

```bash
git add src/search/engine.h src/search/engine.cpp src/search/pather.cpp tests/test_search.cpp tests/test_rules.cpp tests/test_preview_view.cpp
git commit -m "Pass run_search its knobs as one EngineOptions value

Task: Task 2: Engine cleanup and interfaces
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 8: Write the failing test for the activation walk.** Add this case to tests/test_model.cpp, right after the TEST_CASE "Path pathstring and pathstring_verbose":

```cpp
// walk_activations reads a path's activations in place: its own, then the
// tail it shares with its parent, in that order, and copies nothing.
TEST_CASE("Path::walk_activations: own activations then the variant tail, in place") {
    Path p;
    Activation a1, a2, t1;
    a1.skips = 0;
    a2.skips = 1;
    t1.skips = 2;
    p.activations = {a1, a2};
    p.variant_tail = {t1};

    const ActivationWalk walk = p.walk_activations();
    REQUIRE(walk.size() == 3);
    CHECK_FALSE(walk.empty());
    // In place: each element is the very object in the path, not a copy.
    CHECK(&walk[0] == &p.activations[0]);
    CHECK(&walk[1] == &p.activations[1]);
    CHECK(&walk[2] == &p.variant_tail[0]);
    CHECK(&walk.front() == &p.activations[0]);
    CHECK(&walk.back() == &p.variant_tail[0]);

    // A range-for visits the same objects in the same order.
    std::vector<const Activation*> seen;
    for (const Activation& a : walk) seen.push_back(&a);
    CHECK(seen == std::vector<const Activation*>{&p.activations[0], &p.activations[1],
                                                 &p.variant_tail[0]});

    // The same sequence the copying all_activations() hands out.
    const std::vector<Activation> copied = p.all_activations();
    REQUIRE(copied.size() == walk.size());
    for (size_t i = 0; i < copied.size(); ++i) CHECK(*copied[i].skips == *walk[i].skips);

    // Nothing on either side.
    Path none;
    const ActivationWalk nothing = none.walk_activations();
    CHECK(nothing.empty());
    CHECK(nothing.begin() == nothing.end());
}
```

- [ ] **Step 9: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error, because `ActivationWalk` does not exist.

- [ ] **Step 10: Add the walk.** In src/core/model.h, add `#include <cstddef>` and `#include <iterator>` to the includes. Right after the comment block that ends `// derives nothing: it just hands back the stored deact_tick.` (before `// ---- Path ----`), add:

```cpp
// A read-only walk over a path's activations: its own, then the variant tail
// it shares with its parent -- the order all_activations() copies them in.
// It holds pointers into the Path and copies nothing, so it is valid only
// while that Path is alive and unchanged.
class ActivationWalk {
public:
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Activation;
        using difference_type = std::ptrdiff_t;
        using pointer = const Activation*;
        using reference = const Activation&;

        iterator(const std::vector<Activation>* own, const std::vector<Activation>* tail,
                 size_t i)
            : own_(own), tail_(tail), i_(i) {}
        reference operator*() const {
            return i_ < own_->size() ? (*own_)[i_] : (*tail_)[i_ - own_->size()];
        }
        pointer operator->() const { return &**this; }
        iterator& operator++() {
            ++i_;
            return *this;
        }
        iterator operator++(int) {
            iterator old = *this;
            ++i_;
            return old;
        }
        bool operator==(const iterator& o) const { return i_ == o.i_; }
        bool operator!=(const iterator& o) const { return i_ != o.i_; }

    private:
        const std::vector<Activation>* own_;
        const std::vector<Activation>* tail_;
        size_t i_;
    };

    ActivationWalk(const std::vector<Activation>& own, const std::vector<Activation>& tail)
        : own_(&own), tail_(&tail) {}

    size_t size() const { return own_->size() + tail_->size(); }
    bool empty() const { return size() == 0; }
    const Activation& operator[](size_t i) const {
        return i < own_->size() ? (*own_)[i] : (*tail_)[i - own_->size()];
    }
    const Activation& front() const { return (*this)[0]; }
    const Activation& back() const { return (*this)[size() - 1]; }
    iterator begin() const { return iterator(own_, tail_, 0); }
    iterator end() const { return iterator(own_, tail_, size()); }

private:
    const std::vector<Activation>* own_;
    const std::vector<Activation>* tail_;
};
```

In `struct Path`, right after `std::vector<Activation> all_activations() const;`, add:

```cpp
    // The same activations, read in place. Prefer this unless the caller
    // really needs its own copy.
    ActivationWalk walk_activations() const { return ActivationWalk(activations, variant_tail); }
```

- [ ] **Step 11: Switch the read-only callers.** These callers only read, so each one moves to the walk. In src/core/model.cpp:
  - In `Path::pathstring`, `std::vector<Activation> acts = all_activations();` becomes `const ActivationWalk acts = walk_activations();`.
  - In `Path::pathstring_verbose`, the same line inside `if (has_activations())` becomes `const ActivationWalk acts = walk_activations();`.
  - In `Path::difficulty`, `for (const Activation& act : all_activations()) {` becomes `for (const Activation& act : walk_activations()) {`.
  - In `Path::is_allzero`, `std::vector<Activation> acts = all_activations();` becomes `const ActivationWalk acts = walk_activations();`.
  - In `Path::prepare_variants`, replace:

```cpp
    std::vector<Activation> mine = all_activations();
    for (Path& v : variants) {
        int vp = v.var_point.value_or(0);
        v.variant_tail.assign(mine.begin() + vp, mine.end());
```

  with:

```cpp
    const ActivationWalk mine = walk_activations();
    for (Path& v : variants) {
        const size_t vp = static_cast<size_t>(v.var_point.value_or(0));
        v.variant_tail.clear();
        for (size_t i = vp; i < mine.size(); ++i) v.variant_tail.push_back(mine[i]);
```

  In src/store/record_store.cpp, inside `summarize_path`, `std::vector<Activation> acts = path.all_activations();` becomes `const ActivationWalk acts = path.walk_activations();`.

  In src/search/pather.cpp, inside `search_target`, `const std::vector<Activation> acts = p.all_activations();` becomes `const ActivationWalk acts = p.walk_activations();`.

  In src/core/replay.cpp: in `windows_for_path`, `for (const Activation& act : path.all_activations()) {` becomes `for (const Activation& act : path.walk_activations()) {`; in `replay_stored_path`, `out.activations = path.all_activations().size();` becomes `out.activations = path.walk_activations().size();`; in `paths_json`, `for (const Activation& act : p->all_activations()) {` becomes `for (const Activation& act : p->walk_activations()) {`. In src/core/replay.h, the field comment `// path.all_activations().size()` becomes `// path.walk_activations().size()`.

  In tools/replay.cpp, in the self-check failure printout, `const std::vector<Activation> acts = p->all_activations();` becomes `const ActivationWalk acts = p->walk_activations();`.

  Leave src/app/path_view.cpp, src/app/preview_view.cpp and src/app/report.cpp on `all_activations()`: T11 and T5 switch those.

- [ ] **Step 12: Run it and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, the walk case included.

- [ ] **Step 13: Commit.**

```bash
git add src/core/model.h src/core/model.cpp src/store/record_store.cpp src/search/pather.cpp src/core/replay.h src/core/replay.cpp tools/replay.cpp tests/test_model.cpp
git commit -m "Walk a path's activations in place instead of copying them

Task: Task 2: Engine cleanup and interfaces
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 14: Write the gate test for stored scales.** This test is a proof, not a failing test: it must pass on today's code. In tests/test_store.cpp, add `#include "core/squeeze_rating.h"` to the includes, and add this case right after the TEST_CASE "records round-trip through RecordStore across the corpus and config matrix":

```cpp
// rate_activation reads the stored transfer scales only. That is safe because
// a Ready record's stored scales equal a live recompute: a Ready row was
// written by this build, which stamps the scales with
// frontend_transfer_scales at copy-out, and the store hands back every input
// that function reads. This pins it through a real store round trip, for
// every activation of every path, all-0 paths included.
TEST_CASE("stored transfer scales equal a live recompute after a store round trip") {
    const std::vector<Config> configs = {
        {"cap4", 4, DepthMode::Scores, 4, std::nullopt},
        {"cap4.ms10", 4, DepthMode::Scores, 4, 10.0},
        {"auto", std::nullopt, DepthMode::Scores, 4, std::nullopt},
    };
    RecordStore store(":memory:");
    int acts = 0, mismatches = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;

        for (const Config& cfg : configs) {
            std::optional<HydraRecord> record;
            try {
                SearchSettings settings;
                settings.sp_cap = cfg.cap;
                settings.depth_mode = cfg.dmode;
                settings.depth_value = cfg.dvalue;
                settings.ms_filter = cfg.ms;
                record = analyze_chart(song, settings);
            } catch (const ChartFileError&) {
                continue;
            }
            const CapQuery cap = CapQuery::at(*record->sp_cap);
            const std::string hyhash = path + "|scales|" + cfg.key;
            store.add_song(hyhash, "Title", "Artist", "Charter", song);
            store.add_record(RecordKey{hyhash, "mode", cap}, *record);

            const RecordLookup lookup = store.get_record(RecordKey{hyhash, "mode", cap});
            REQUIRE(lookup.status == RecordStatus::Ready);
            REQUIRE(lookup.timing.has_value());

            std::vector<const Path*> all = lookup.record->all_paths();
            for (const Path* p : lookup.record->all_allzero_paths()) all.push_back(p);
            for (const Path* p : all) {
                for (const Activation& act : p->walk_activations()) {
                    ++acts;
                    const std::optional<ActTransferScales> live =
                        frontend_transfer_scales(act, *lookup.timing);
                    const bool same = live && live->pre.early == act.transfer_pre.early &&
                                      live->pre.late == act.transfer_pre.late &&
                                      live->post.early == act.transfer_post.early &&
                                      live->post.late == act.transfer_post.late;
                    if (!same && ++mismatches <= 8)
                        CHECK_MESSAGE(false, path << " [" << cfg.key << "] activation at tick "
                                                  << act.timecode->ticks());
                }
            }
        }
    }

    CHECK(mismatches == 0);
    REQUIRE(acts > 0);
    MESSAGE("compared " << acts << " stored activations with a live recompute");
}
```

- [ ] **Step 15: Run the gate.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="stored transfer scales equal a live recompute after a store round trip"`. Expected: `Status: SUCCESS!`. If any activation differs, stop here and report the MESSAGE lines. Do not change `rate_activation`.

- [ ] **Step 16: Write the failing test for stored-only rating.** In tests/test_squeeze_rating.cpp, delete the whole TEST_CASE "rate_activation: a live timing overrides the stored scales" and put this one in its place:

```cpp
TEST_CASE("rate_activation: the stored scales are the only scales") {
    // A flat chart would recompute identity scales, but the search stored
    // 0.5s. The rating reads what the search stored and never recomputes.
    Activation act;
    act.timecode = Timecode::raw(0);
    act.sp_meter = 2;
    act.deact_tick = 7680;
    act.transfer_pre = TransferScale{0.5, 0.5};
    act.transfer_post = TransferScale{0.5, 0.5};

    BackendSqueeze row;
    row.timecode = Timecode::raw(7728);
    row.offset_ms = 50.0;
    act.backends.push_back(row);

    ActivationRating r = rate_activation(act, 85.0);
    CHECK(r.scales.post.late == doctest::Approx(0.5));
    CHECK(r.scales.pre.early == doctest::Approx(0.5));
    CHECK(r.late_warns);
}
```

  Then, with the Edit tool and `replace_all` on tests/test_squeeze_rating.cpp, replace `, nullptr, 85.0` with `, 85.0`. That moves every other `rate_activation(x, nullptr, 85.0...)` call to the new signature.

- [ ] **Step 17: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors in tests/test_squeeze_rating.cpp, because `rate_activation` still takes a `const SongTiming*` second.

- [ ] **Step 18: Make `rate_activation` read the stored scales.** In src/core/squeeze_rating.cpp, replace the head of `rate_activation`:

```cpp
ActivationRating rate_activation(const Activation& act,
                                 const SongTiming* timing,
                                 double hit_window_ms,
                                 double backend_leeway_ms) {
    ActivationRating out;

    std::optional<ActTransferScales> scales;
    if (timing) scales = frontend_transfer_scales(act, *timing);
    // No timing at hand (no songmeta row): the record stores the scales the
    // search computed (1.0 on old blobs).
    if (!scales) scales = ActTransferScales{act.transfer_pre, act.transfer_post};
    out.scales = *scales;
```

  with:

```cpp
ActivationRating rate_activation(const Activation& act,
                                 double hit_window_ms,
                                 double backend_leeway_ms) {
    ActivationRating out;

    // The scales the search stamped on the record. A stored fact is read,
    // never re-derived (ADRs 0011, 0013, 0014); every Ready record has them.
    out.scales = ActTransferScales{act.transfer_pre, act.transfer_post};
```

  In the rest of that function, replace each of the six `scales->` with `out.scales.` (they read `scales->post.late`, `scales->post.early`, `scales->pre.early` and `scales->pre.late`).

  In src/core/squeeze_rating.h, replace the declaration:

```cpp
ActivationRating rate_activation(
    const Activation& act, const SongTiming* timing,
    double hit_window_ms = kDefaultHitWindowMs,
    double backend_leeway_ms = core::default_rules().backend_leeway_ms);
```

  with:

```cpp
ActivationRating rate_activation(
    const Activation& act,
    double hit_window_ms = kDefaultHitWindowMs,
    double backend_leeway_ms = core::default_rules().backend_leeway_ms);
```

  In the comment above it, replace `rows itself (act.display_backends()), so the caller renders and nothing
// more. `timing` may be null (no songmeta row): the stored scales are used
// then. Backend rows` with `rows itself (act.display_backends()), so the caller renders and nothing
// more. It reads the scales the search stored on the activation. Backend rows`. In `struct ActivationRating`, replace the comment on `scales`:

```cpp
    // Resolved scales: recomputed live from `timing` when one is at hand,
    // else the record's stored (blob v3) values — 1.0 flat-tempo identities
    // on older blobs.
```

  with:

```cpp
    // The scales the search stored on the activation (transfer_pre/post).
```

  In src/app/path_view.cpp, inside `build_activations`, replace:

```cpp
        ActivationRating rate =
            rate_activation(act, timing, W, rules.backend_leeway_ms);
```

  with:

```cpp
        ActivationRating rate = rate_activation(act, W, rules.backend_leeway_ms);
```

  (`timing` is still used further down, for the overfill line's measure.)

- [ ] **Step 19: Run it and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="rate_activation*","stored transfer scales*","build_activations*"`. Expected: `Status: SUCCESS!`. If a `build_activations` case fails, it relied on the live recompute: stop and report which one.

- [ ] **Step 20: Commit.**

```bash
git add src/core/squeeze_rating.h src/core/squeeze_rating.cpp src/app/path_view.cpp tests/test_squeeze_rating.cpp tests/test_store.cpp
git commit -m "Rate activations from the stored transfer scales only

Task: Task 2: Engine cleanup and interfaces
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 21: Write the gate test for the 4-bar branch.** This is also a proof that must pass on today's code. In tests/test_search.cpp, add this case right after the "run_search: EngineOptions carries each knob to the engine" case:

```cpp
// analyze_chart used to run Clone Hero's 4 bars down its own branch, with the
// graph built a flat 4 bars tall. Every other fixed cap builds the graph only
// as tall as the song has phrases (graph_build_cap). Both give the same
// answer: a song with p phrases never holds more than p bars, so a p-bar
// ceiling clamps nothing a 4-bar ceiling would not. This pins it byte for
// byte through the store's own writer before the branches fold into one.
TEST_CASE("a 4-bar graph built at the song's phrase count stores the same paths") {
    std::vector<Song> songs;
    // Hand-built, three phrases, one of them collected mid-SP.
    songs.push_back(build_tail_song({{0, true, false},
                                     {768, true, false},
                                     {1536},
                                     {2304, false, true},
                                     {3072},
                                     {3840, true, false},
                                     {4608},
                                     {5376},
                                     {6144},
                                     {6720},
                                     {6816}}));
    for (const std::string& path : corpus::chart_paths()) {
        Song s = load_songpath(path, true, true);
        if (!s.is_empty() && s.sp_phrase_count() < kCloneHeroSpCap)
            songs.push_back(std::move(s));
    }

    int compared = 0;
    for (const Song& song : songs) {
        const int build_cap = graph_build_cap(kCloneHeroSpCap, song.sp_phrase_count());
        REQUIRE(build_cap < kCloneHeroSpCap);

        ScoreGraph g_tall(song, kCloneHeroSpCap);
        ScoreGraph g_built(song, build_cap);
        EngineOptions options;
        options.depth_value = 4;
        HydraRecord tall, built;
        tall.sp_cap = kCloneHeroSpCap;
        built.sp_cap = kCloneHeroSpCap;
        tall.paths = run_search(g_tall, options);
        built.paths = run_search(g_built, options);
        tall.allzero_paths = search_allzero(g_tall);
        built.allzero_paths = search_allzero(g_built);

        const store::FlatRecord a = store::flatten_record(tall);
        const store::FlatRecord b = store::flatten_record(built);
        bool same = a.structure == b.structure && a.nodes.size() == b.nodes.size();
        for (size_t i = 0; same && i < a.nodes.size(); ++i)
            same = a.nodes[i].payload == b.nodes[i].payload;
        CHECK_MESSAGE(same, "a song with " << song.sp_phrase_count() << " phrases");
        ++compared;
    }
    MESSAGE("compared " << compared << " songs with fewer than 4 phrases");
}
```

- [ ] **Step 22: Run the gate.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="a 4-bar graph built*"`. Expected: `Status: SUCCESS!`. If any song differs, stop this part: keep the test, skip Step 23, note the failing phrase count in the final report and go on to Step 25.

- [ ] **Step 23: Fold the 4-bar branch.** In src/search/pather.cpp, inside `analyze_chart`, delete this block:

```cpp
    // Clone Hero's 4-bar rule is the classic single pass, kept exactly as it
    // always was so a fresh 4-bar record matches every stored one.
    if (sp_cap == kCloneHeroSpCap) {
        HydraRecord record =
            analyze_at_cap(song, kCloneHeroSpCap, depth_mode, depth_value, ms_filter,
                           std::nullopt, settings.legacy_fill_deadline, settings.rules,
                           /*want_allzero=*/true, on_progress);
        record.rules_fingerprint = settings.rules.fingerprint();
        return record;
    }
```

  and replace the comment above the remaining `if (sp_cap.has_value()) {`:

```cpp
    // Any other fixed cap runs a single pass at that ceiling. The graph is
    // still only built as tall as the song has phrases to bank -- no run can
    // exceed that -- so a huge cap on a short song stays cheap and exact. A
    // fixed cap is the user's explicit choice, so Auto's time budget doesn't
    // apply to it.
```

  with:

```cpp
    // A fixed cap, Clone Hero's 4 bars included, runs a single pass at that
    // ceiling. The graph is only built as tall as the song has phrases to
    // bank -- no run can exceed that -- so a huge cap on a short song stays
    // cheap, and a 4-bar graph built lower stores the same bytes (test "a
    // 4-bar graph built at the song's phrase count stores the same paths").
    // A fixed cap is the user's explicit choice, so Auto's time budget
    // doesn't apply to it.
```

- [ ] **Step 24: Commit.**

```bash
git add src/search/pather.cpp tests/test_search.cpp
git commit -m "Run the 4-bar cap through the same branch as every fixed cap

Task: Task 2: Engine cleanup and interfaces
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

  (If Step 22 failed, commit only tests/test_search.cpp with the message "Pin whether a 4-bar graph built lower stores the same paths" and say so in the report.)

- [ ] **Step 25: Delete the dead engine members.** Nothing reads these, so there is no test to write first. The proof is the build, the full suite, the grep and the batch diff in Step 28.

  In src/search/graph.h, delete these lines:

```cpp
    ScoreGraphNode* sp_start() const { return sp_start_; }
    int length() const { return length_; }
```

```cpp
    FillDeadlineRule fill_rule() const { return rule_; }
```

```cpp
    ScoreGraphNode* sp_start_ = nullptr;
    int length_ = 0;
```

```cpp
    bool head_time_set_ = false;
```

  In src/search/graph.cpp, delete `    sp_start_ = sp_track_head_;` from the constructor, `    head_time_set_ = true;` from `set_head_time`, and `    length_ += 1;` (with the blank line after it) from `advance_tracks`.

  In src/search/engine.cpp, delete these lines from `struct OutAct`:

```cpp
    // Reserved for the squeezed-out phrase; rebuild() stamps the Activation's
    // sqout_tick from the deact edge, so this stays NO_TIME.
    int64_t sqout_tick;
```

  and delete `        oa.sqout_tick = NO_TIME;` from `Engine::emit_acts`.

- [ ] **Step 26: Replace `FrontendSqueeze` with a points field, and stop copying extension maps onto the base track.** In src/core/model.h, delete:

```cpp
struct FrontendSqueeze {
    Chord chord;
    int points = 0;
    bool operator==(const FrontendSqueeze& o) const {
        return chord == o.chord && points == o.points;
    }
};
```

  In src/search/graph.h, replace `    std::optional<FrontendSqueeze> frontend;` with:

```cpp
    // The frontend chord's SP points: set only on activation edges, 0 elsewhere.
    int frontend_points = 0;
```

  and replace the declaration

```cpp
    ScoreGraphEdge* add_act_edge(const Chord& frontend_chord,
                                 int frontend_points,
                                 int64_t fill_length_ticks);
```

  with `    ScoreGraphEdge* add_act_edge(int frontend_points, int64_t fill_length_ticks);`. Also replace the `sp_times` comment

```cpp
    // (sp_timecode, extension_map) per SP phrase collected on this advance edge.
    // The extension map is from_tick -> where that end moves (and whether the
    // cap pinned it there); the engine scans it by key.
```

  with:

```cpp
    // (sp_timecode, extension_map) per SP phrase collected on this advance edge.
    // The extension map is from_tick -> where that end moves (and whether the
    // cap pinned it there); the engine scans it by key. Only the SP track moves
    // an SP end, so base-track edges leave the map empty and read the times.
```

  In src/search/graph.cpp, replace the definition head and body line:

```cpp
ScoreGraphEdge* ScoreGraph::add_act_edge(const Chord& frontend_chord,
                                         int frontend_points,
                                         int64_t fill_length_ticks) {
    ScoreGraphEdge* act_edge = new_edge();
    act_edge->dest = sp_track_head_;

    act_edge->frontend = FrontendSqueeze{frontend_chord, frontend_points};
```

  with:

```cpp
ScoreGraphEdge* ScoreGraph::add_act_edge(int frontend_points, int64_t fill_length_ticks) {
    ScoreGraphEdge* act_edge = new_edge();
    act_edge->dest = sp_track_head_;

    act_edge->frontend_points = frontend_points;
```

  In `ScoreGraph::build`, replace:

```cpp
            ScoreGraphEdge* act_edge = add_act_edge(
                timestamp.chord, sg.sp, *timestamp.activation_length);
```

  with:

```cpp
            ScoreGraphEdge* act_edge = add_act_edge(sg.sp, *timestamp.activation_length);
```

  and replace:

```cpp
            proto_base_edge_->sp_times.push_back({timestamp.timecode, ext_map});
            proto_sp_edge_->sp_times.push_back({timestamp.timecode, ext_map});
```

  with:

```cpp
            proto_base_edge_->sp_times.push_back({timestamp.timecode, {}});
            proto_sp_edge_->sp_times.push_back({timestamp.timecode, std::move(ext_map)});
```

  In src/search/engine.cpp, in `Engine::edge`, replace `        v.frontend_points = o->frontend.has_value() ? o->frontend->points : 0;` with `        v.frontend_points = o->frontend_points;`. In `rebuild`, replace `            act.frontend_points = node->branch_edge->frontend->points;` with `            act.frontend_points = node->branch_edge->frontend_points;`.

- [ ] **Step 27: Delete the dead helpers and parameters.**

  `cymbal_flip`: in src/core/model.h delete `NoteCymbalType cymbal_flip(NoteCymbalType t);`, and in src/core/model.cpp delete the four-line `cymbal_flip` definition.

  `allows_dynamics`: in src/core/model.h delete `bool allows_dynamics(NoteColor c);`. In src/core/model.cpp delete:

```cpp
// Every lane, kick included: Clone Hero prices a velocity-1 kick as a ghost
// and a velocity-127 kick as an accent, the same rule the pads use.
bool allows_dynamics(NoteColor) { return true; }
```

  and in `ChordNote::str` replace:

```cpp
    std::vector<std::string> mods;
    if (allows_dynamics(colortype)) {
        switch (dynamictype) {
            case NoteDynamicType::Normal: break;
            case NoteDynamicType::Ghost: mods.push_back("Ghost"); break;
            case NoteDynamicType::Accent: mods.push_back("Accent"); break;
        }
    }
```

  with:

```cpp
    // Every lane carries dynamics, the kick included (ADR 0012).
    std::vector<std::string> mods;
    switch (dynamictype) {
        case NoteDynamicType::Normal: break;
        case NoteDynamicType::Ghost: mods.push_back("Ghost"); break;
        case NoteDynamicType::Accent: mods.push_back("Accent"); break;
    }
```

  In tests/test_model.cpp, delete `    CHECK(allows_dynamics(NoteColor::Kick));` from the TEST_CASE "a ghost or accent kick scores double, like a pad". The TEST_CASE "ChordNote::str shows the kick's dynamic and its 2x flag" keeps pinning the kick's "Ghost" text.

  `MeasureIndex`'s `tick_r`: in src/core/timing.h replace `    MeasureIndex(const std::map<int64_t, int64_t>& tpm_map, int64_t tick_r);` with `    explicit MeasureIndex(const std::map<int64_t, int64_t>& tpm_map);`. In src/core/timing.cpp replace:

```cpp
MeasureIndex::MeasureIndex(const std::map<int64_t, int64_t>& tpm_map,
                           int64_t tick_r) {
    (void)tick_r;  // The meter index does not use resolution; kept for parity.
```

  with:

```cpp
MeasureIndex::MeasureIndex(const std::map<int64_t, int64_t>& tpm_map) {
```

  and in the `SongTiming` constructor's initializer list replace `mbt_(tpm_map, tick_r)` with `mbt_(tpm_map)`. In tests/test_timing.cpp replace `hydra::MeasureIndex(tpm, 480)` with `hydra::MeasureIndex(tpm)` and `hydra::MeasureIndex mi(tpm, 480);` with `hydra::MeasureIndex mi(tpm);`.

  `windows_for_path`'s `Song`: in src/core/replay.h replace `std::vector<ReplayWindow> windows_for_path(const Path& path, const Song& song);` with `std::vector<ReplayWindow> windows_for_path(const Path& path);`. In src/core/replay.cpp replace:

```cpp
// The song is no longer consulted: the deact node comes off the record, so
// there is nothing left to rebuild from the chart. The parameter stays so
// callers read the same, and so a future window rule can use it.
std::vector<ReplayWindow> windows_for_path(const Path& path, const Song&) {
```

  with:

```cpp
// The deact node comes off the record, so the chart is not consulted.
std::vector<ReplayWindow> windows_for_path(const Path& path) {
```

  and in `replay_stored_path` replace `    out.windows = windows_for_path(path, song);` with `    out.windows = windows_for_path(path);`. In tests/test_replay.cpp replace `windows_for_path(*p, song)` with `windows_for_path(*p)`.

- [ ] **Step 28: Run everything.** Run:

```powershell
.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe
.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all
.\build_cpp.ps1 -Target hydra_replay
Get-ChildItem src,tests,tools -Recurse -Include *.cpp,*.h | Select-String -Pattern 'sp_start\(\)|fill_rule\(\)|head_time_set_|cymbal_flip|allows_dynamics|FrontendSqueeze|oa\.sqout_tick|\blength_\b|run_search\(graph, DepthMode'
```

  Expected: `Status: SUCCESS!`, every GUI test passes, hydra_replay builds, and the grep prints nothing. Then build `hydra_batch` and run the brief's score-neutral batch diff. Expected: it prints nothing.

- [ ] **Step 29: Commit.**

```bash
git add src/search/graph.h src/search/graph.cpp src/search/engine.cpp src/core/model.h src/core/model.cpp src/core/timing.h src/core/timing.cpp src/core/replay.h src/core/replay.cpp tests/test_model.cpp tests/test_timing.cpp tests/test_replay.cpp
git commit -m "Delete engine members and parameters nothing reads

Task: Task 2: Engine cleanup and interfaces
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/search/engine.h","src/search/engine.cpp","src/search/graph.h","src/search/graph.cpp","src/search/pather.cpp","src/core/model.h","src/core/model.cpp","src/core/squeeze_rating.h","src/core/squeeze_rating.cpp","src/core/timing.h","src/core/timing.cpp","src/core/replay.h","src/core/replay.cpp","src/app/path_view.cpp","src/store/record_store.cpp","tools/replay.cpp","tests/test_search.cpp","tests/test_store.cpp","tests/test_model.cpp","tests/test_squeeze_rating.cpp","tests/test_rules.cpp","tests/test_preview_view.cpp","tests/test_timing.cpp","tests/test_replay.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe","acceptanceCriteria":["The stored-scales round-trip test passes before rate_activation changes and at the end.","The 4-bar graph test passes before the branch is removed.","The EngineOptions, walk_activations and stored-only rate_activation tests pass.","The full hydra_tests run prints Status: SUCCESS!","hydra_uitest --all passes.","The dead-name grep prints nothing.","The score-neutral batch diff prints nothing."],"modelTier":"standard"}
```

---

### Task 3: Analysis batch runner

The batch runner analyzes many charts at once on a small thread pool. Think of a kitchen. Cooks (worker threads) each take the next ticket and put the finished plate on the pass. One runner (the calling thread) carries plates out as they land. There are three problems with it today.

First, cancel can hang. When the batch is cancelled, the cooks stop taking tickets and go home. If they all leave while the runner is waiting at an empty pass, nobody tells the runner, so it waits forever. The library scan has the same pool and guards against this with a "last cook out rings the bell" counter. The batch copy never got that guard. The scan's guard also has a small gap of its own: the last cook drops the counter without holding the pass's lock, so the bell can ring in the instant between the runner checking the pass and starting to wait. This task writes the pool once, in a new header `src/app/work_pool.h`, with the bell rung under the lock, and both the scan and the batch use it.

Second, cancel doesn't stop a search that is already running. The batch hands the search no progress callback, so a cancel waits for up to 8 searches to finish, and each can run to the 120-second Auto budget. The single-chart Analyze button already stops mid-search: its progress callback throws when cancel is set, and the throw unwinds the search. The batch now passes the same kind of callback. The thrown type, `AnalysisCancelled`, moves from library_jobs.cpp into app/analysis.h so both jobs share it. A chart stopped this way is neither a result nor a failure, so it never shows up in the failure list.

Third, `run_batch` takes the chart mode, the lens (the ms limit and score range a result is filed under) and the search settings as three separate arguments. Nothing checks they came from the same settings. They now travel as one bundle, `BatchRun`, which `Settings::batch_run()` fills from one `Settings`. The four trailing callbacks and the cancel flag also become one `BatchCallbacks` struct, the way the scan already takes `ScanCallbacks`. That struct is also where a test can put a fake analyzer.

What the user sees: nothing, except that Cancel on "Analyze library" and closing Hydra during a batch now return within a second or two instead of waiting for running searches.

**Depends on:** nothing.
**Expected overlaps:** Task 4 edits src/cli/batch.cpp too. Task 3 changes only the three lines that build `analysis` and `chartmode`, and the `run_batch(...)` call block. Task 4 changes only the stamp block, the `discover_charts` line and the header comment. The hunks are separated by unchanged lines, so git merges them in either order, and Task 4 never calls `run_batch`. Task 2 owns src/search/pather.cpp, including the 4-bar branch merge in `analyze_chart`; this task does not touch pather.h or pather.cpp. Task 6 edits src/ui/app_state.cpp elsewhere (window teardown); this task edits only `AppState::start_batch`. Task 9 (wave 2) will change the `has_record` pre-skip loop at the top of `run_batch`; it must build on this task's version. Task 16 (wave 3) dedupes `lower`/`ends_with_ci` in analysis.cpp, which this task doesn't touch.
**Goal:** One cancel-safe thread pool serves the scan and the batch, a cancelled batch stops running searches within seconds, `run_batch` takes one settings bundle.
**Files:**
- Create: `src/app/work_pool.h`
- Modify: `src/app/analysis.h`, `src/app/analysis.cpp`, `src/app/config.h`, `src/app/config.cpp`, `src/ui/library_jobs.h`, `src/ui/library_jobs.cpp`, `src/ui/app_state.cpp`, `src/cli/batch.cpp`
- Test: `tests/test_analysis.cpp`, `tests/test_config.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="run_work_pool*"` passes: 500 cancelled runs all return within the 30 s watchdog, and every started item reaches the consumer.
- [ ] `hydra_tests.exe -tc="run_batch: cancel stops running searches within seconds"` passes: `run_batch` returns in under 5 s (60 s without the fix), exactly 4 charts started, 0 errors, 0 results, 0 stored rows.
- [ ] `hydra_tests.exe -tc="run_batch: a cancelled real search is neither a result nor a failure"` passes.
- [ ] `hydra_tests.exe -tc="batch_run bundles*"` and `-tc="run_batch files results under the lens it is given"` pass.
- [ ] The whole `hydra_tests.exe` run ends `Status: SUCCESS!`, and `hydra_uitest.exe --all` passes (its `batch-modal-drift` test drives a real `BatchJob`).
- [ ] `Select-String -Path src\app\analysis.cpp -Pattern 'std::vector<std::thread>'` prints nothing (both hand-written pools are gone).
- [ ] The score-neutral batch compare from the global rules prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="run_work_pool*,run_batch*,batch_run*,discover_charts*,rescan cache*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests.** In tests/test_analysis.cpp, add these includes beside the existing ones:

```cpp
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <stdexcept>
#include <thread>

#include "app/work_pool.h"
```

Replace the existing case "run_batch files results under the lens it is given" (the whole TEST_CASE) with:

```cpp
TEST_CASE("run_batch files results under the lens it is given") {
    std::string chart;
    for (const std::string& p : corpus::chart_paths()) {
        if (!hydra::load_songpath(p, true, true).is_empty()) { chart = p; break; }
    }
    REQUIRE(!chart.empty());

    BatchRun run;
    run.chartmode = "lens-test";
    run.lens = hydra::store::Lens::from(std::optional<int>(10), 0, 10);
    run.settings.depth_mode = hydra::DepthMode::Scores;
    run.settings.depth_value = 10;
    run.settings.ms_filter = 10.0;

    ScanItem item;
    item.md5 = hash_chart_file(chart);
    item.title = "t";
    item.notespath = chart;
    hydra::store::RecordStore store(":memory:");
    run_batch({item}, run, store, /*redo=*/false, 1);

    const hydra::store::CapQuery cap =
        hydra::store::CapQuery::from_setting(run.settings.sp_cap);
    CHECK(store.has_record(hydra::store::RecordKey{item.md5, "lens-test", cap, run.lens}));
}
```

Then add these cases after it:

```cpp
TEST_CASE("run_work_pool hands every item to the consumer once") {
    std::vector<int> seen(100, 0);
    run_work_pool<size_t>(
        100, 8, nullptr, [](size_t i) { return i; }, [&](size_t&& i) { ++seen[i]; });
    for (int n : seen) CHECK(n == 1);
}

TEST_CASE("run_work_pool: a cancel mid-run never strands the consumer") {
    // Hundreds of short runs, each cancelled from inside the work. The old
    // batch pool could leave its consumer waiting forever when every worker
    // saw the cancel between items, so a hang is the failure this guards. The
    // watchdog turns a hang into a failed check instead of a stuck suite.
    std::promise<bool> finished;
    std::future<bool> outcome = finished.get_future();
    std::thread([p = std::move(finished)]() mutable {
        bool every_item_consumed = true;
        for (int round = 0; round < 500; ++round) {
            std::atomic<bool> cancel{false};
            std::atomic<int> worked{0};
            int consumed = 0;
            run_work_pool<int>(
                64, 8, &cancel,
                [&](size_t i) {
                    ++worked;
                    if (i == 3) cancel = true;
                    return static_cast<int>(i);
                },
                [&](int&&) { ++consumed; });
            // Every item a worker started reached the consumer.
            if (consumed != worked.load()) every_item_consumed = false;
        }
        p.set_value(every_item_consumed);
    }).detach();

    REQUIRE_MESSAGE(outcome.wait_for(std::chrono::seconds(30)) == std::future_status::ready,
                    "run_work_pool never returned: its consumer was left waiting");
    CHECK(outcome.get());
}

namespace {

// Items for a fake analyzer: nothing is read from disk.
std::vector<ScanItem> fake_items(int n) {
    std::vector<ScanItem> items;
    for (int i = 0; i < n; ++i) {
        ScanItem item;
        item.md5 = "fake" + std::to_string(i);
        item.title = "fake " + std::to_string(i);
        item.notespath = "fake_" + std::to_string(i) + ".chart";
        items.push_back(item);
    }
    return items;
}

}  // namespace

TEST_CASE("run_batch: cancel stops running searches within seconds") {
    // A stand-in for a heavy chart: it runs for a minute, reporting progress
    // every millisecond the way the engine's sweep does. Only a progress
    // callback that throws on cancel can stop it early.
    std::atomic<int> started{0};
    BatchCallbacks callbacks;
    callbacks.analyze = [&started](const std::string&, const AnalysisSettings&,
                                   const std::function<void(float)>& on_progress)
        -> AnalysisResult {
        ++started;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < until) {
            if (on_progress) on_progress(0.5f);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("the fake search ran its full minute");
    };
    std::atomic<bool> cancel{false};
    callbacks.cancel = &cancel;
    int errors = 0, results = 0;
    callbacks.on_error = [&errors](const std::string&, const std::string&) { ++errors; };
    callbacks.on_result = [&results](const ScanItem&, const hydra::store::PreparedRow&) {
        ++results;
    };

    // Press cancel once all four workers are inside a search.
    std::thread canceller([&] {
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (started.load() < 4 && std::chrono::steady_clock::now() < give_up)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cancel = true;
    });

    hydra::store::RecordStore store(":memory:");
    BatchRun run;
    run.chartmode = "cancel-test";
    const auto t0 = std::chrono::steady_clock::now();
    run_batch(fake_items(16), run, store, /*redo=*/false, /*worker_count=*/4, callbacks);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    canceller.join();

    CHECK(started.load() == 4);  // no chart started after the cancel
    CHECK(seconds < 5.0);        // without the cancel check this takes 60 s
    CHECK(errors == 0);          // a stopped search is not a failure...
    CHECK(results == 0);         // ...and not a result
    CHECK(store.counts().second == 0);
}

TEST_CASE("run_batch: a cancelled real search is neither a result nor a failure") {
    std::string chart;
    for (const std::string& p : corpus::chart_paths()) {
        if (!hydra::load_songpath(p, true, true).is_empty()) { chart = p; break; }
    }
    REQUIRE(!chart.empty());

    ScanItem item;
    item.md5 = hash_chart_file(chart);
    item.title = "t";
    item.notespath = chart;

    std::atomic<bool> cancel{false};
    BatchCallbacks callbacks;
    callbacks.cancel = &cancel;
    // The real analysis, with cancel pressed at the search's first progress
    // tick. The throw has to unwind the engine, the pather and the all-0 pass
    // without any of them catching it.
    callbacks.analyze = [&cancel](const std::string& path, const AnalysisSettings& s,
                                  const std::function<void(float)>& on_progress) {
        return analyze_chart_file(path, s, [&](float f) {
            cancel = true;
            on_progress(f);
        });
    };
    int errors = 0, results = 0;
    callbacks.on_error = [&errors](const std::string&, const std::string&) { ++errors; };
    callbacks.on_result = [&results](const ScanItem&, const hydra::store::PreparedRow&) {
        ++results;
    };

    hydra::store::RecordStore store(":memory:");
    BatchRun run;
    run.chartmode = "cancel-test";
    run_batch({item}, run, store, /*redo=*/false, 1, callbacks);

    CHECK(errors == 0);
    CHECK(results == 0);
    CHECK(store.counts().second == 0);
}
```

In tests/test_config.cpp, add after the case "record_key carries the chartmode, the SP cap and the lens":

```cpp
TEST_CASE("batch_run bundles one Settings' chartmode, lens and search settings") {
    Settings s;
    s.view_difficulty = "Hard";
    s.view_prodrums = false;
    s.mslimit_enabled = false;
    s.depth_mode = 1;
    s.depth_value = 5000;
    s.sp_cap = std::nullopt;

    const hydra::app::BatchRun run = s.batch_run();
    CHECK(run.chartmode == s.chartmode_key());
    CHECK(run.lens == s.lens());
    CHECK(run.settings.difficulty == hydra::Difficulty::Hard);
    CHECK(run.settings.prodrums == false);
    CHECK(!run.settings.ms_filter.has_value());
    CHECK(run.settings.depth_mode == hydra::DepthMode::Points);
    CHECK(run.settings.depth_value == 5000);
    CHECK(!run.settings.sp_cap.has_value());
}
```

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors. `app/work_pool.h` does not exist, and `BatchRun`, `BatchCallbacks` and `Settings::batch_run` are not declared.

- [ ] **Step 3: Write the shared pool.** Create src/app/work_pool.h:

```cpp
// A small thread pool shared by the library scan (hashing chart files) and
// the batch runner (analyzing charts).
//
// Picture a kitchen pass. Cooks (worker threads) each take the next ticket,
// cook it, and set the plate on the pass. One runner (the calling thread)
// carries plates out in the order they land. When the kitchen closes early,
// the last cook to leave rings the bell, so the runner never stands at an
// empty pass waiting for a plate nobody is cooking.

#ifndef HYDRA_APP_WORK_POOL_H
#define HYDRA_APP_WORK_POOL_H

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace hydra::app {

// Runs work(i) for every i in [0, count) on up to `worker_count` threads, and
// hands each result to consume(Result&&) on the calling thread in the order
// the results finish. consume never runs on a worker, so it may write the
// store or touch UI state.
//
// Once *cancel is set, workers stop taking new items. Items already running
// finish (work decides how fast) and still reach consume, so consume sees
// every item that was started and nothing else. The call returns once every
// worker has left. The last one out wakes the consumer while holding the
// lock, so the wake-up cannot slip in between the consumer's check and its
// wait.
//
// If consume throws, workers stop taking items, the pool is joined, and the
// exception propagates. work must not throw: catch inside it. Result must be
// default-constructible and movable.
template <typename Result, typename Work, typename Consume>
void run_work_pool(size_t count, int worker_count, const std::atomic<bool>* cancel,
                   Work&& work, Consume&& consume) {
    if (count == 0) return;

    std::mutex mu;
    std::condition_variable cv;
    std::deque<Result> finished;  // guarded by mu
    std::atomic<size_t> next{0};
    std::atomic<bool> stop{false};

    const size_t nworkers =
        std::min(count, static_cast<size_t>(std::max(1, worker_count)));
    size_t live = nworkers;  // workers still running; guarded by mu

    std::vector<std::thread> pool;
    pool.reserve(nworkers);
    for (size_t w = 0; w < nworkers; ++w) {
        pool.emplace_back([&] {
            for (;;) {
                if (stop.load() || (cancel && cancel->load())) break;
                const size_t i = next.fetch_add(1);
                if (i >= count) break;
                Result r = work(i);
                {
                    std::lock_guard<std::mutex> lock(mu);
                    finished.push_back(std::move(r));
                }
                cv.notify_one();
            }
            std::lock_guard<std::mutex> lock(mu);
            if (--live == 0) cv.notify_one();
        });
    }

    try {
        for (;;) {
            Result r;
            {
                std::unique_lock<std::mutex> lock(mu);
                cv.wait(lock, [&] { return !finished.empty() || live == 0; });
                if (finished.empty()) break;  // every worker has left
                r = std::move(finished.front());
                finished.pop_front();
            }
            consume(std::move(r));
        }
    } catch (...) {
        stop.store(true);
        for (std::thread& t : pool) t.join();
        throw;
    }
    for (std::thread& t : pool) t.join();
}

}  // namespace hydra::app

#endif  // HYDRA_APP_WORK_POOL_H
```

- [ ] **Step 4: Declare the bundle, the callbacks and the shared cancel type.** In src/app/analysis.h, replace this block (from `// Loads and analyzes one chart file` to the end of the `run_batch` declaration):

```cpp
// Loads and analyzes one chart file (.mid/.chart/.sng/.srb), producing a record and
// the song's timing (for the store's songmeta row).
struct AnalysisResult {
    HydraRecord record;
    Song song;  // carries tick_resolution/tpm_changes/bpm_changes for add_song
};
// on_progress, if set, is called from the calling thread with a monotonic 0..1
// fraction as the search sweeps the chart — for a single-chart progress bar.
AnalysisResult analyze_chart_file(const std::string& filepath,
                                  const AnalysisSettings& settings,
                                  const std::function<void(float)>& on_progress = {});

// One unit of batch work: analyze a ScanItem's chart and store the result
// under `chartmode`. Skips charts that already have a record unless `redo`.
struct BatchProgress {
    int completed = 0;
    int total = 0;
    std::string current_title;
};

// The default batch pool size: one core is left for the UI (or shell) and for
// whatever else the machine is doing; capped so peak memory (a discography
// chart can reach hundreds of MB) stays bounded.
int batch_worker_count();

// Runs analyze_chart_file + store::prepare_row for every item across a
// std::thread pool, writing results into `store` from the calling thread (a
// RecordStore is safe to call from any one thread at a time, but SQLite
// writes are serialized here to keep the store simple).
//
// on_progress, on_error and on_result, if set, are invoked from the calling
// thread only (never from a worker) as each result comes back — safe to touch
// UI state. on_result fires after the row is written to the store, with the
// row it wrote (the batch CLI prints score/bestpath from it).
// `lens` is the store key the caller's settings file results under: pass `Settings::lens()`.
void run_batch(const std::vector<ScanItem>& items, const std::string& chartmode,
               const store::Lens& lens,
               const AnalysisSettings& settings, store::RecordStore& store, bool redo,
               int worker_count,
               const std::function<void(const BatchProgress&)>& on_progress = nullptr,
               const std::function<void(const std::string& title, const std::string& error)>&
                   on_error = nullptr,
               const std::function<void(const ScanItem&, const store::PreparedRow&)>&
                   on_result = nullptr,
               const std::atomic<bool>* cancel = nullptr);
```

with:

```cpp
// Loads and analyzes one chart file (.mid/.chart/.sng/.srb), producing a record and
// the song's timing (for the store's songmeta row).
struct AnalysisResult {
    HydraRecord record;
    Song song;  // carries tick_resolution/tpm_changes/bpm_changes for add_song
};
// on_progress, if set, is called from the calling thread with a monotonic 0..1
// fraction as the search sweeps the chart — for a single-chart progress bar.
// It may throw AnalysisCancelled to stop the search.
AnalysisResult analyze_chart_file(const std::string& filepath,
                                  const AnalysisSettings& settings,
                                  const std::function<void(float)>& on_progress = {});

// Thrown out of a search's progress callback to stop a cancelled analysis.
// It does not derive from std::exception, so no catch (const std::exception&)
// on the way out (the all-0 pass in search/pather.cpp has one) swallows it.
// The single-chart Analyze job and run_batch both stop searches with it.
struct AnalysisCancelled {};

// Analyzes one chart file. analyze_chart_file is the real one; run_batch
// takes another only from a test.
using ChartAnalyzer = std::function<AnalysisResult(
    const std::string& path, const AnalysisSettings& settings,
    const std::function<void(float)>& on_progress)>;

// How far a batch run has got.
struct BatchProgress {
    int completed = 0;
    int total = 0;
    std::string current_title;
};

// The default batch pool size: one core is left for the UI (or shell) and for
// whatever else the machine is doing; capped so peak memory (a discography
// chart can reach hundreds of MB) stays bounded.
int batch_worker_count();

// Everything one batch run is: the search settings, and the chart mode and
// lens its results are filed under. Settings::batch_run() fills all three
// from one Settings, so they cannot disagree. Building one by hand is for
// tests.
struct BatchRun {
    std::string chartmode;
    store::Lens lens;
    AnalysisSettings settings;
};

// Progress, result and cancel hooks for run_batch. The three callbacks fire
// on the calling thread only (never a worker), so they may touch UI state.
struct BatchCallbacks {
    std::function<void(const BatchProgress&)> on_progress;
    std::function<void(const std::string& title, const std::string& error)> on_error;
    // Fires after the row is written to the store, with the row it wrote (the
    // batch CLI prints score and best path from it).
    std::function<void(const ScanItem&, const store::PreparedRow&)> on_result;
    // Setting *cancel stops the run. No new chart starts, and a running search
    // stops at its next progress tick. A stopped chart is neither a result nor
    // a failure, and nothing more is written once the cancel is seen.
    const std::atomic<bool>* cancel = nullptr;
    // What analyzes one chart. Empty means analyze_chart_file.
    ChartAnalyzer analyze;
};

// Runs the analysis + store::prepare_row for every item on a
// batch_worker_count()-sized pool (app/work_pool.h), writing results into
// `store` from the calling thread only. Skips a chart that already has a
// record under `run` unless `redo`.
void run_batch(const std::vector<ScanItem>& items, const BatchRun& run,
               store::RecordStore& store, bool redo, int worker_count,
               const BatchCallbacks& callbacks = {});
```

- [ ] **Step 5: Move the scan onto the pool.** In src/app/analysis.cpp, add `#include "app/work_pool.h"` after `#include "app/dynamics_breakdown.h"`. Then replace the stage-2 block:

```cpp
    std::vector<std::optional<ScanItem>> results(pending.size());
    if (total > 0 && !(cancel && cancel->load())) {
        struct ReadNote {
            bool cached = false;
            std::string error;
        };
        std::mutex q_mu;
        std::condition_variable q_cv;
        std::deque<ReadNote> notes_q;
        std::atomic<size_t> next{0};
        std::atomic<int> workers_live{0};

        int nworkers = std::max(1, std::min(batch_worker_count(), total));
        std::vector<std::thread> pool;
        pool.reserve(static_cast<size_t>(nworkers));
        workers_live.store(nworkers);
        for (int w = 0; w < nworkers; ++w) {
            pool.emplace_back([&]() {
                // One CNG provider per worker, reused across every file it
                // hashes. Created lazily so an all-cache-hits rescan never
                // touches CNG at all.
                std::optional<Md5Provider> md5;

                for (;;) {
                    size_t i = next.fetch_add(1);
                    if (i >= pending.size()) break;
                    if (cancel && cancel->load()) break;

                    const PendingChart& pc = pending[i];
                    ReadNote note;
                    try {
```

(everything from here down to the matching `catch` stays as it is) and the tail after that `catch`:

```cpp
                    } catch (const std::exception& e) {
                        note.error = e.what();
                    }

                    {
                        std::lock_guard<std::mutex> lock(q_mu);
                        notes_q.push_back(std::move(note));
                    }
                    q_cv.notify_one();
                }

                // Last worker out wakes the consumer even if the queue is
                // empty (cancel can leave claimed items unpushed; the
                // consumer must not wait for them forever).
                if (workers_live.fetch_sub(1) == 1) q_cv.notify_one();
            });
        }

        // Consume on the calling thread: progress/error callbacks fire here
        // only, mirroring run_batch's worker/consumer split.
        int done = 0, cached_count = 0;
        for (;;) {
            ReadNote note;
            {
                std::unique_lock<std::mutex> lock(q_mu);
                q_cv.wait(lock, [&] {
                    return !notes_q.empty() || workers_live.load() == 0;
                });
                if (notes_q.empty()) break;  // workers gone, nothing left
                note = std::move(notes_q.front());
                notes_q.pop_front();
            }

            ++done;
            if (note.cached) ++cached_count;
            if (!note.error.empty()) errors.push_back(std::move(note.error));
            if (callbacks.on_charts) callbacks.on_charts(done, total, cached_count);
            if (done == total) break;
        }

        for (std::thread& t : pool) t.join();
    }
```

The head becomes:

```cpp
    std::vector<std::optional<ScanItem>> results(pending.size());
    if (total > 0 && !(cancel && cancel->load())) {
        struct ReadNote {
            bool cached = false;
            std::string error;
        };
        int done = 0, cached_count = 0;
        run_work_pool<ReadNote>(
            pending.size(), batch_worker_count(), cancel,
            [&](size_t i) {
                // One CNG provider per worker thread, reused across every file
                // it hashes and closed when the worker exits. Created lazily
                // so an all-cache-hits rescan never touches CNG at all.
                thread_local std::optional<Md5Provider> md5;

                const PendingChart& pc = pending[i];
                ReadNote note;
                try {
```

The unchanged `try` body follows (re-indent it four spaces less; not a single token changes). The tail becomes:

```cpp
                } catch (const std::exception& e) {
                    note.error = e.what();
                }
                return note;
            },
            // Progress and error callbacks fire here, on the calling thread
            // only, as they do for run_batch.
            [&](ReadNote&& note) {
                ++done;
                if (note.cached) ++cached_count;
                if (!note.error.empty()) errors.push_back(std::move(note.error));
                if (callbacks.on_charts) callbacks.on_charts(done, total, cached_count);
            });
    }
```

- [ ] **Step 6: Rewrite the batch runner on the pool.** In src/app/analysis.cpp, replace everything from the `namespace {` that opens `struct WorkResult` to the closing brace of `run_batch`:

```cpp
namespace {

struct WorkResult {
    ScanItem item;
    std::optional<store::PreparedRow> row;
    std::optional<AnalysisResult> analysis;
    std::string error;
};

}  // namespace

void run_batch(const std::vector<ScanItem>& items, const std::string& chartmode,
              const store::Lens& lens,
              const AnalysisSettings& settings, store::RecordStore& store, bool redo,
              int worker_count,
              const std::function<void(const BatchProgress&)>& on_progress,
              const std::function<void(const std::string&, const std::string&)>& on_error,
              const std::function<void(const ScanItem&, const store::PreparedRow&)>& on_result,
              const std::atomic<bool>* cancel) {
```

(and the rest of that function body, down to `    for (std::thread& t : pool) t.join();` and its closing `}`) with:

```cpp
namespace {

struct WorkResult {
    ScanItem item;
    std::optional<store::PreparedRow> row;
    std::optional<AnalysisResult> analysis;
    std::string error;
    // The search stopped at a cancel: neither a result nor a failure.
    bool cancelled = false;
};

}  // namespace

void run_batch(const std::vector<ScanItem>& items, const BatchRun& run,
               store::RecordStore& store, bool redo, int worker_count,
               const BatchCallbacks& callbacks) {
    const AnalysisSettings& settings = run.settings;
    const std::atomic<bool>* cancel = callbacks.cancel;

    // "Already has a result" means a current-version record at the cap this
    // run would produce (Auto: any record above 4 bars) AND under this run's
    // ms limit and score range, so stale rows, other caps' rows and other
    // settings' rows are re-run rather than skipped.
    const store::CapQuery cap = store::CapQuery::from_setting(settings.sp_cap);
    std::vector<const ScanItem*> todo;
    for (const ScanItem& item : items) {
        if (!redo &&
            store.has_record(store::RecordKey{item.md5, run.chartmode, cap, run.lens}))
            continue;
        todo.push_back(&item);
    }

    BatchProgress progress;
    progress.total = static_cast<int>(todo.size());
    if (callbacks.on_progress) callbacks.on_progress(progress);
    if (todo.empty()) return;

    // A running search checks for cancel in its progress callback. Throwing
    // AnalysisCancelled there unwinds it at the next tick (the engine reports
    // every half percent of the chart), the way the single-chart Analyze
    // button stops. With no cancel flag there is nothing to check, so the
    // search gets no callback at all, exactly as before.
    std::function<void(float)> check_cancel;
    if (cancel)
        check_cancel = [cancel](float) {
            if (cancel->load(std::memory_order_relaxed)) throw AnalysisCancelled{};
        };
    const ChartAnalyzer analyze =
        callbacks.analyze ? callbacks.analyze : ChartAnalyzer(analyze_chart_file);

    int completed = 0;
    run_work_pool<WorkResult>(
        todo.size(), worker_count, cancel,
        [&](size_t i) {
            const ScanItem* item = todo[i];
            WorkResult wr;
            wr.item = *item;
            try {
                AnalysisResult ar = analyze(item->notespath, settings, check_cancel);
                wr.row = store::prepare_row(
                    store::RecordKey{item->md5, run.chartmode, cap, run.lens}, ar.record);
                wr.analysis = std::move(ar);
            } catch (const AnalysisCancelled&) {
                wr.cancelled = true;
            } catch (const std::exception& e) {
                wr.error = e.what();
            }
            return wr;
        },
        [&](WorkResult&& wr) {
            // Once cancel is seen nothing more is written or reported, as
            // before. A search stopped part-way is not a failure, and a result
            // that finished alongside the cancel is dropped.
            if (wr.cancelled || (cancel && cancel->load())) return;

            ++completed;
            if (!wr.error.empty()) {
                if (callbacks.on_error) callbacks.on_error(wr.item.title, wr.error);
            } else {
                store.add_song(wr.item.md5, wr.item.title, wr.item.artist, wr.item.charter,
                               wr.analysis->song);
                store.add_row(*wr.row);
                store_dynamics_from_analysis(store, wr.item.md5, wr.analysis->song,
                                             settings.bass2x, settings.difficulty,
                                             settings.prodrums);
                if (callbacks.on_result) callbacks.on_result(wr.item, *wr.row);
            }

            progress.completed = completed;
            progress.current_title = wr.item.title;
            if (callbacks.on_progress) callbacks.on_progress(progress);
        });
}
```

One small difference from today, stated so no one is surprised: today the result being written at the moment cancel is first noticed still gets written. Now a result consumed after cancel is set is dropped. Either way the run stops; nothing already written is touched.

- [ ] **Step 7: Build the bundle from Settings.** In src/app/config.h, after the `record_key` declaration:

```cpp
    store::RecordKey record_key(const std::string& hyhash) const;
```

add:

```cpp

    // Everything a batch run under these settings needs: the search settings,
    // and the chartmode and lens its results are filed under. All three come
    // from this one Settings, so they cannot disagree.
    BatchRun batch_run() const;
```

In src/app/config.cpp, after:

```cpp
store::RecordKey Settings::record_key(const std::string& hyhash) const {
    return store::RecordKey{hyhash, chartmode_key(), cap_query(), lens()};
}
```

add:

```cpp

BatchRun Settings::batch_run() const {
    return BatchRun{chartmode_key(), lens(), to_analysis_settings()};
}
```

- [ ] **Step 8: Move the GUI's batch job onto the bundle.** In src/ui/library_jobs.h, change:

```cpp
    BatchJob(std::optional<std::string> search, std::string chartmode, store::Lens lens,
             app::AnalysisSettings settings, store::RecordStore& store, bool redo);
```

to:

```cpp
    BatchJob(std::optional<std::string> search, app::BatchRun run,
             store::RecordStore& store, bool redo);
```

and the members:

```cpp
    std::string chartmode_;
    store::Lens lens_;
    app::AnalysisSettings settings_;
```

to:

```cpp
    app::BatchRun run_;
```

In src/ui/library_jobs.cpp, change the constructor:

```cpp
BatchJob::BatchJob(std::optional<std::string> search, std::string chartmode,
                   store::Lens lens, app::AnalysisSettings settings,
                   store::RecordStore& store, bool redo)
    : search_(std::move(search)),
      chartmode_(std::move(chartmode)),
      lens_(std::move(lens)),
      settings_(std::move(settings)),
      store_(store),
      redo_(redo),
      workers_(app::batch_worker_count()) {}
```

to:

```cpp
BatchJob::BatchJob(std::optional<std::string> search, app::BatchRun run,
                   store::RecordStore& store, bool redo)
    : search_(std::move(search)),
      run_(std::move(run)),
      store_(store),
      redo_(redo),
      workers_(app::batch_worker_count()) {}
```

and the call in `BatchJob::run()`:

```cpp
    app::run_batch(
        items_, chartmode_, lens_, settings_, store_, redo_, workers_,
        [this, &total_known](const app::BatchProgress& p) {
            std::lock_guard<std::mutex> lock(mu_);
            snap_.total = p.total;
            snap_.completed = p.completed;
            snap_.current_title = p.current_title;
            if (!total_known) {
                snap_.skipped = static_cast<int>(items_.size()) - p.total;
                total_known = true;
            }
        },
        [this](const std::string& title, const std::string& error) {
            std::lock_guard<std::mutex> lock(mu_);
            ++snap_.failed;
            snap_.failures.push_back(title + ": " + error);
        },
        /*on_result=*/nullptr, &cancel_);
```

to:

```cpp
    app::BatchCallbacks callbacks;
    callbacks.on_progress = [this, &total_known](const app::BatchProgress& p) {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.total = p.total;
        snap_.completed = p.completed;
        snap_.current_title = p.current_title;
        if (!total_known) {
            snap_.skipped = static_cast<int>(items_.size()) - p.total;
            total_known = true;
        }
    };
    callbacks.on_error = [this](const std::string& title, const std::string& error) {
        std::lock_guard<std::mutex> lock(mu_);
        ++snap_.failed;
        snap_.failures.push_back(title + ": " + error);
    };
    callbacks.cancel = &cancel_;
    app::run_batch(items_, run_, store_, redo_, workers_, callbacks);
```

Delete the private cancel type, which now lives in app/analysis.h:

```cpp
namespace {
// Thrown out of the search's progress callback to abort a cancelled analysis
// — the same unwind path pather.cpp's CapBudgetExceeded takes, so the engine
// is already known to survive it.
struct AnalysisCancelled {};
}  // namespace

```

and in `AnalyzeJob::start()` change the two uses:

```cpp
                            if (cancel_.load(std::memory_order_relaxed))
                                throw AnalysisCancelled{};
```

```cpp
                } catch (const AnalysisCancelled&) {
```

to:

```cpp
                            if (cancel_.load(std::memory_order_relaxed))
                                throw app::AnalysisCancelled{};
```

```cpp
                } catch (const app::AnalysisCancelled&) {
```

In src/ui/app_state.cpp, `AppState::start_batch`, change:

```cpp
    batch_job = std::make_unique<BatchJob>(search_opt, settings.chartmode_key(),
                                           settings.lens(),
                                           settings.to_analysis_settings(), *store, redo);
```

to:

```cpp
    batch_job = std::make_unique<BatchJob>(search_opt, settings.batch_run(), *store, redo);
```

- [ ] **Step 9: Move the batch CLI onto the bundle.** In src/cli/batch.cpp, change:

```cpp
    hydra::app::AnalysisSettings analysis = settings.to_analysis_settings();
    analysis.legacy_fill_deadline = legacy_fills;
    std::string chartmode = settings.chartmode_key();
```

to:

```cpp
    hydra::app::BatchRun run = settings.batch_run();
    run.settings.legacy_fill_deadline = legacy_fills;
    const hydra::app::AnalysisSettings& analysis = run.settings;
    const std::string& chartmode = run.chartmode;
```

(The two references keep every later line that reads `analysis` or `chartmode` unchanged, which keeps Task 4's hunks clean.) Then change the call block:

```cpp
    hydra::app::run_batch(
        scanitems, chartmode, settings.lens(), analysis, store, redo,
        hydra::app::batch_worker_count(),
        [&](const hydra::app::BatchProgress& p) {
            if (!total_known) {
                total = p.total;
                skipped = static_cast<int>(scanitems.size()) - p.total;
                total_known = true;
            }
        },
        [&](const std::string& title, const std::string& error) {
            ++failed;
            ++done;
            failures.push_back(title + ": " + error);
            std::printf("[%d/%d] FAILED %s: %s\n", done, total, title.c_str(),
                        error.c_str());
        },
        [&](const hydra::app::ScanItem& item, const hydra::store::PreparedRow& row) {
            ++analyzed;
            ++done;
            std::string label = item.artist + " - " + item.title;
            std::string score =
                row.summary.score ? hydra::group_thousands(*row.summary.score) : "-";
            std::printf("[%d/%d] %10s  %s %s\n", done, total, score.c_str(),
                        clip_utf8(label, 52).c_str(), clip_utf8(row.bestpath, 36).c_str());
            std::fflush(stdout);
        });
```

to:

```cpp
    hydra::app::BatchCallbacks callbacks;
    callbacks.on_progress = [&](const hydra::app::BatchProgress& p) {
        if (!total_known) {
            total = p.total;
            skipped = static_cast<int>(scanitems.size()) - p.total;
            total_known = true;
        }
    };
    callbacks.on_error = [&](const std::string& title, const std::string& error) {
        ++failed;
        ++done;
        failures.push_back(title + ": " + error);
        std::printf("[%d/%d] FAILED %s: %s\n", done, total, title.c_str(),
                    error.c_str());
    };
    callbacks.on_result = [&](const hydra::app::ScanItem& item,
                              const hydra::store::PreparedRow& row) {
        ++analyzed;
        ++done;
        std::string label = item.artist + " - " + item.title;
        std::string score =
            row.summary.score ? hydra::group_thousands(*row.summary.score) : "-";
        std::printf("[%d/%d] %10s  %s %s\n", done, total, score.c_str(),
                    clip_utf8(label, 52).c_str(), clip_utf8(row.bestpath, 36).c_str());
        std::fflush(stdout);
    };
    hydra::app::run_batch(scanitems, run, store, redo, hydra::app::batch_worker_count(),
                          callbacks);
```

The CLI passes no cancel flag (Ctrl+C ends the process), so its searches still run with no progress callback, exactly as today.

- [ ] **Step 10: Run everything and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="run_work_pool*,run_batch*,batch_run*,discover_charts*,rescan cache*"`. Expected: `Status: SUCCESS!`. Then run the whole `.\build-cpp\Release\hydra_tests.exe` (expected `Status: SUCCESS!`), then `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all` (expected: every test passes, `batch-modal-drift` included). Build `hydra_batch` with `.\build_cpp.ps1 -Target hydra_batch` and run the score-neutral compare from the global rules. Expected: it prints nothing. Run the grep from the acceptance criteria; it prints nothing.

- [ ] **Step 11: Commit.**

```bash
git add src/app/work_pool.h src/app/analysis.h src/app/analysis.cpp src/app/config.h src/app/config.cpp src/ui/library_jobs.h src/ui/library_jobs.cpp src/ui/app_state.cpp src/cli/batch.cpp tests/test_analysis.cpp tests/test_config.cpp
git commit -m "Share one cancel-safe pool between scan and batch; stop searches on cancel

Task: Task 3: Analysis batch runner
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/app/work_pool.h","src/app/analysis.h","src/app/analysis.cpp","src/app/config.h","src/app/config.cpp","src/ui/library_jobs.h","src/ui/library_jobs.cpp","src/ui/app_state.cpp","src/cli/batch.cpp","tests/test_analysis.cpp","tests/test_config.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe -tc=\"run_work_pool*,run_batch*,batch_run*,discover_charts*,rescan cache*\"","acceptanceCriteria":["`run_work_pool*` passes: 500 cancelled runs return within the 30 s watchdog and every started item is consumed","`run_batch: cancel stops running searches within seconds` passes: under 5 s, 4 started, 0 errors, 0 results, 0 rows","`run_batch: a cancelled real search is neither a result nor a failure` passes","`batch_run bundles*` and `run_batch files results under the lens it is given` pass","full hydra_tests and hydra_uitest --all pass","`Select-String -Path src\\app\\analysis.cpp -Pattern 'std::vector<std::thread>'` prints nothing","score-neutral batch compare prints nothing"],"modelTier":"standard"}
```

---

### Task 4: CLI tools

There are four problems in the command-line tools.

First, `hydra_batch --reindex` relabels a Clone Hero 1.0 database as 1.1. Some background: Clone Hero 1.1 changed when drum fills appear, and `--legacy-fills` scores by the old 1.0 rule instead. The rule is not stored on each result, so a 1.0 run needs its own database (docs/adr/0010). To keep them apart, hydra_batch writes an "engine mode" stamp (`ch10` or `ch11`) into each database, and hydra_fillcompare reads it. Today that stamp is written from the `--legacy-fills` flag before the `--reindex` early return. So `--reindex` on a 1.0 database without the flag stamps it `ch11`. A normal run into a 1.0 database also quietly mixes both rules in one file and then stamps it `ch11`. The fix, per the orchestrator's call: `--reindex` never stamps, and a run whose flag disagrees with the file's stamp exits with code 2 and a message, before writing anything. My own call for the one gap the stamp leaves: a database with results but no stamp was written before stamping existed, by the normal rule, so it counts as `ch11`. That is the reading hydra_fillcompare already gives an unstamped file. A database with no results and no stamp is new and takes the run's stamp.

Second, `hydra_batch` re-hashes the whole library on every run. It calls the scan with no cache, although the database it just opened holds the GUI's last scan: each chart's file sizes and modified times, with the hash and song.ini fields read from them. The GUI reuses those rows for any chart whose files haven't changed. hydra_batch now reads the same cache. It only reads it. Writing the library back would change what the GUI's library shows after a `hydra_batch <one folder>` run, which is a user-visible change nobody asked for. So a separate `--db` file that the GUI never scanned has no cache and still hashes in full, as today.

Third, `core/replay.h` pulls the whole JSON library into core. Every file that includes the replay header compiles it, the Preview's among them. Only hydra_replay and tests/test_replay use the three JSON functions (`windows_from_json`, `score_json`, `paths_json`). They move unchanged into `tools/replay_json.h` and `tools/replay_json.cpp`, built as a small library that hydra_replay and hydra_tests link.

Fourth, the three shipped CLI programs have no tests, and the README's command-line section leaves out `hydra_fillcompare`, `--rules` and `--legacy-fills`. The tests run the real built programs. Here is why that route and not moving each `main()` into testable functions: the bugs above live in the order of operations inside `main()` (which flag is read, when the stamp is written, which scan is called, what exit code comes back). Running the real exe tests exactly that, with no refactor. The repo's CMake supports it directly: `add_dependencies` makes building hydra_tests build the three tools first, and `$<TARGET_FILE:...>` hands the tests each exe's path. Each test copies the exes into a fresh temp folder, because the tools read hydra_settings.ini and hydra_rules.ini from their own folder. A folder with neither gives the app's defaults, whatever the developer's build folder holds.

How this stays mergeable with Task 3: this task never changes `run_batch` or app/analysis.h, and its tests never call `run_batch`. In src/cli/batch.cpp it touches only the header comment, the stamp block and the `discover_charts` line. Task 3 touches only the three lines that build `analysis` and `chartmode`, and the `run_batch(...)` call block. Unchanged lines separate every pair of hunks, so git merges the two in either order.

What the user sees: `hydra_batch` refuses, with exit code 2 and a message saying which flag to use, a run whose fill rule disagrees with the database's stamp. That behavior is the orchestrator's call. `hydra_batch --reindex` no longer changes the stamp. `hydra_batch` against Hydra's own database skips re-hashing charts the GUI already scanned. The README lists all three tools and their flags. Nothing else changes.

**Depends on:** nothing.
**Expected overlaps:** Task 3 edits src/cli/batch.cpp (see the paragraph above). Task 14 edits CMakeLists.txt (the `/utf-8` flags, the uitest option, and building hydra_bench/hydra_replay by default). This task edits no existing CMake line; it appends one block at the end of the file, so if Task 14 also appends there, the merger keeps both blocks. Task 14 may edit README.md's build and "Developer tools" sections; this task edits only "Command line tools". Task 2 edits src/core/replay.h and replay.cpp (dropping `windows_for_path`'s unused `Song` parameter) and its callers in tests/test_replay.cpp. This task deletes the JSON block below `windows_for_path` and adds one include line to the test, so the hunks don't touch. Task 12 (wave 2) drops unused stored fields; if it drops `Activation::sp_meter`, `skips` or `chord`, it must also edit tools/replay_json.cpp, which reads them for the dump. Task 5 reworks the report page; test_cli checks only the exit code, the "Wrote" line and that the song's name is in the page. If Task 5 makes the page read script or style files from beside the exe at run time, `CliSandbox` must copy them too. Task 1 may remove the hydra_uncapped.db import from `open_store` (user decision 5); the tests don't depend on it.
**Goal:** `hydra_batch` keeps each database's fill-rule stamp honest and reads the GUI's scan cache, core no longer includes the JSON library, the three CLI tools have end-to-end tests, and the README documents every tool and flag.
**Files:**
- Create: `tools/replay_json.h`, `tools/replay_json.cpp`, `tests/test_cli.cpp`
- Modify: `src/cli/batch.cpp`, `src/core/replay.h`, `src/core/replay.cpp`, `tools/replay.cpp`, `CMakeLists.txt`, `README.md`, `docs/adr/0010-legacy-fill-deadline-is-a-cli-only-mode.md`
- Test: `tests/test_cli.cpp`, `tests/test_replay.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="hydra_batch --reindex keeps a legacy database's stamp"` passes (the stamp stays `ch10`).
- [ ] `hydra_tests.exe -tc="hydra_batch refuses a run whose fill rule disagrees with the database"` passes: exit code 2 both ways, stamp unchanged, record count unchanged.
- [ ] `hydra_tests.exe -tc="hydra_batch treats an unstamped database with records as Clone Hero 1.1"` passes.
- [ ] `hydra_tests.exe -tc="hydra_batch reuses the GUI's scan cache"` passes: the output shows the cached title, not the song.ini one.
- [ ] `hydra_tests.exe -tc="hydra_batch*,hydra_report*,hydra_fillcompare*"` passes (all eight CLI cases).
- [ ] `hydra_tests.exe -tc="*path JSON*,paths_json*"` passes with the JSON functions living in tools/.
- [ ] `Select-String -Path src\core\*.h,src\core\*.cpp -Pattern 'nlohmann|json.hpp'` prints nothing.
- [ ] `.\build_cpp.ps1 -Target hydra_replay` builds, and `.\build-cpp\Release\hydra_replay.exe selfcheck` exits 0 with `FAIL 0` in its last line.
- [ ] README.md's "Command line tools" section names `hydra_fillcompare`, `--rules` and `--legacy-fills`: `Select-String -Path README.md -Pattern 'hydra_fillcompare --old','--legacy-fills','--rules'` prints at least one line for each.
- [ ] The whole `hydra_tests.exe` run ends `Status: SUCCESS!`, and the score-neutral batch compare prints nothing (a fresh database has no scan cache, so the output must not move).

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="hydra_batch*,hydra_report*,hydra_fillcompare*,*path JSON*,paths_json*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing CLI tests.** Create tests/test_cli.cpp:

```cpp
// End-to-end tests for the three console tools (src/cli/). Each test copies
// the built exes into a fresh temp folder and runs them there, because the
// tools read hydra_settings.ini and hydra_rules.ini from their own folder: a
// folder with neither gives the app's defaults, whatever the developer's build
// folder holds. The exe paths come from CMakeLists.txt, which also builds the
// tools before the tests.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "app/config.h"
#include "core/winstr.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/graph.h"
#include "store/record_store.h"

#if !defined(HYDRA_BATCH_EXE) || !defined(HYDRA_REPORT_EXE) || \
    !defined(HYDRA_FILLCOMPARE_EXE)
#error "the CLI exe paths must be defined (see CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace {

const std::string kCh10 = hydra::engine_mode_stamp(hydra::FillDeadlineRule::Ch10);
const std::string kCh11 = hydra::engine_mode_stamp(hydra::FillDeadlineRule::Ch11);

struct RunResult {
    int exit_code = -1;
    std::string output;  // stdout and stderr, interleaved
};

// Runs `exe` with `args`, waits for it, and captures everything it prints.
RunResult run_exe(const fs::path& exe, const std::vector<std::string>& args) {
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    for (const std::string& a : args) cmd += L" \"" + hydra::utf8_to_wide(a) + L"\"";

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    REQUIRE(CreatePipe(&read_end, &write_end, &sa, 0));
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write_end);  // the child holds its own copy
    REQUIRE_MESSAGE(started, "could not start " << exe.u8string());

    RunResult r;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(read_end, buf, sizeof(buf), &got, nullptr) && got > 0)
        r.output.append(buf, got);
    CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    r.exit_code = static_cast<int>(code);
    return r;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The smallest corpus .chart that has drum notes and at least two Star Power
// phrases: every tool gets a real path to work with, and each run is quick.
std::string small_chart() {
    static const std::string chosen = [] {
        std::string best;
        uintmax_t best_size = UINTMAX_MAX;
        for (const std::string& p : corpus::chart_paths()) {
            if (p.size() < 6 || p.compare(p.size() - 6, 6, ".chart") != 0) continue;
            const uintmax_t size = fs::file_size(fs::u8path(p));
            if (size >= best_size) continue;
            const hydra::Song song = hydra::load_songpath(p, true, true);
            if (song.is_empty() || song.sp_phrase_count() < 2) continue;
            best = p;
            best_size = size;
        }
        return best;
    }();
    return chosen;
}

// A scratch folder holding a copy of each tool and one chart folder with a
// known song name. Removed again when the test ends.
struct CliSandbox {
    fs::path dir;
    fs::path batch, report, fillcompare;
    fs::path songs;  // holds one chart folder, "fixture"

    explicit CliSandbox(const char* name) {
        dir = fs::temp_directory_path() /
              ("hydra_cli_" + std::to_string(GetCurrentProcessId()) + "_" + name);
        fs::remove_all(dir);
        fs::create_directories(dir);
        batch = copy_tool(HYDRA_BATCH_EXE);
        report = copy_tool(HYDRA_REPORT_EXE);
        fillcompare = copy_tool(HYDRA_FILLCOMPARE_EXE);

        const std::string chart = small_chart();
        REQUIRE(!chart.empty());
        songs = dir / "songs";
        fs::create_directories(songs / "fixture");
        fs::copy_file(fs::u8path(chart), songs / "fixture" / "notes.chart");
        std::ofstream ini(songs / "fixture" / "song.ini", std::ios::binary);
        ini << "[song]\nname = CLI Fixture\nartist = Tester\ncharter = Nobody\n";
    }
    ~CliSandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path copy_tool(const char* built) {
        const fs::path from = fs::u8path(built).make_preferred();
        const fs::path to = dir / from.filename();
        fs::copy_file(from, to);
        return to;
    }
    std::string db(const char* name) const { return (dir / name).u8string(); }
    std::string folder() const { return songs.u8string(); }
};

}  // namespace

TEST_CASE("hydra_batch stamps a new database with the rule it ran under") {
    CliSandbox box("stamp");
    const std::string normal = box.db("ch11.db");
    RunResult r = run_exe(box.batch, {"--db", normal, box.folder()});
    INFO(r.output);
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.output, "Tester - CLI Fixture"));
    {
        hydra::store::RecordStore store(normal);
        CHECK(store.engine_mode() == std::optional<std::string>(kCh11));
        CHECK(store.counts().second == 1);
    }

    const std::string legacy = box.db("ch10.db");
    r = run_exe(box.batch, {"--legacy-fills", "--db", legacy, box.folder()});
    INFO(r.output);
    REQUIRE(r.exit_code == 0);
    hydra::store::RecordStore store(legacy);
    CHECK(store.engine_mode() == std::optional<std::string>(kCh10));
}

TEST_CASE("hydra_batch --legacy-fills refuses the tool's own hydra.db") {
    // The sandboxed exe's default database is hydra.db beside it: the file the
    // app itself would read.
    CliSandbox box("owndb");
    RunResult r = run_exe(box.batch, {"--legacy-fills", box.folder()});
    INFO(r.output);
    CHECK(r.exit_code == 2);
    CHECK(contains(r.output, "--db legacy.db"));
}

TEST_CASE("hydra_batch --reindex keeps a legacy database's stamp") {
    CliSandbox box("reindex");
    const std::string legacy = box.db("ch10.db");
    REQUIRE(run_exe(box.batch, {"--legacy-fills", "--db", legacy, box.folder()})
                .exit_code == 0);

    RunResult r = run_exe(box.batch, {"--reindex", "--db", legacy});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "Reindexed 1 records."));
    hydra::store::RecordStore store(legacy);
    CHECK(store.engine_mode() == std::optional<std::string>(kCh10));
}

TEST_CASE("hydra_batch refuses a run whose fill rule disagrees with the database") {
    CliSandbox box("mismatch");

    // A normal run into a 1.0 file.
    const std::string legacy = box.db("ch10.db");
    REQUIRE(run_exe(box.batch, {"--legacy-fills", "--db", legacy, box.folder()})
                .exit_code == 0);
    RunResult r = run_exe(box.batch, {"--redo", "--db", legacy, box.folder()});
    INFO(r.output);
    CHECK(r.exit_code == 2);
    CHECK(contains(r.output, "Add --legacy-fills"));
    {
        hydra::store::RecordStore store(legacy);
        CHECK(store.engine_mode() == std::optional<std::string>(kCh10));
        CHECK(store.counts().second == 1);
    }

    // A legacy run into a 1.1 file.
    const std::string normal = box.db("ch11.db");
    REQUIRE(run_exe(box.batch, {"--db", normal, box.folder()}).exit_code == 0);
    r = run_exe(box.batch, {"--legacy-fills", "--redo", "--db", normal, box.folder()});
    INFO(r.output);
    CHECK(r.exit_code == 2);
    CHECK(contains(r.output, "Drop --legacy-fills"));
    hydra::store::RecordStore store(normal);
    CHECK(store.engine_mode() == std::optional<std::string>(kCh11));
}

TEST_CASE("hydra_batch treats an unstamped database with records as Clone Hero 1.1") {
    // A file written before hydra_batch stamped: one normal result, no stamp.
    CliSandbox box("unstamped");
    const std::string db = box.db("old.db");
    {
        const hydra::app::Settings settings{};  // the defaults the sandboxed exe reads
        const std::string chart = (box.songs / "fixture" / "notes.chart").u8string();
        const std::string md5 = hydra::app::hash_chart_file(chart);
        hydra::app::AnalysisResult ar =
            hydra::app::analyze_chart_file(chart, settings.to_analysis_settings());
        hydra::store::RecordStore store(db);
        store.add_song(md5, "CLI Fixture", "Tester", "Nobody", ar.song);
        store.add_row(hydra::store::prepare_row(settings.record_key(md5), ar.record));
        REQUIRE(!store.engine_mode().has_value());
    }

    RunResult r = run_exe(box.batch, {"--legacy-fills", "--db", db, box.folder()});
    INFO(r.output);
    CHECK(r.exit_code == 2);
    hydra::store::RecordStore store(db);
    CHECK(!store.engine_mode().has_value());
}

TEST_CASE("hydra_batch reuses the GUI's scan cache") {
    CliSandbox box("cache");
    const std::string db = box.db("cached.db");
    {
        auto [items, errors] = hydra::app::discover_charts({box.folder()});
        REQUIRE(items.size() == 1);
        // The row a GUI scan writes, with only its title changed. The file
        // sizes and times still match, so a scan that reads the cache takes
        // this title; one that re-reads song.ini gets "CLI Fixture".
        hydra::store::RecordStore store(db);
        store.rebuild_chart_library({{items[0].md5, "Title From Cache", items[0].artist,
                                      items[0].charter, items[0].notespath,
                                      items[0].rootfolder, items[0].sig}});
    }

    RunResult r = run_exe(box.batch, {"--db", db, box.folder()});
    INFO(r.output);
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.output, "Tester - Title From Cache"));
    CHECK(!contains(r.output, "CLI Fixture"));
}

TEST_CASE("hydra_report writes a page for a filled database and says so for an empty one") {
    CliSandbox box("report");
    const std::string db = box.db("report.db");
    REQUIRE(run_exe(box.batch, {"--db", db, box.folder()}).exit_code == 0);

    const fs::path page = box.dir / "out" / "paths.html";
    RunResult r =
        run_exe(box.report, {"--db", db, "--out", page.u8string(), "--no-open"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "Wrote "));
    REQUIRE(fs::exists(page));
    std::ifstream f(page, std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    CHECK(contains(html, "CLI Fixture"));

    RunResult empty = run_exe(box.report, {"--db", box.db("empty.db"), "--out",
                                           (box.dir / "empty.html").u8string(),
                                           "--no-open"});
    INFO(empty.output);
    CHECK(empty.exit_code == 1);
    CHECK(contains(empty.output, "No records stored yet"));

    CHECK(run_exe(box.report, {"--bogus"}).exit_code == 2);
}

TEST_CASE("hydra_fillcompare compares a 1.0 and a 1.1 database") {
    CliSandbox box("fillcompare");
    const std::string ch10 = box.db("ch10.db"), ch11 = box.db("ch11.db");
    REQUIRE(run_exe(box.batch, {"--legacy-fills", "--db", ch10, box.folder()})
                .exit_code == 0);
    REQUIRE(run_exe(box.batch, {"--db", ch11, box.folder()}).exit_code == 0);

    const fs::path page = box.dir / "compare.html";
    RunResult r = run_exe(box.fillcompare, {"--old", ch10, "--new", ch11, "--out",
                                            page.u8string(), "--no-open"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "Compared 1 charts"));
    CHECK(!contains(r.output, "Warning"));
    CHECK(fs::exists(page));

    // Swapped files: each stamp disagrees with the side it was passed as,
    // which warns but still runs.
    RunResult swapped = run_exe(box.fillcompare,
                                {"--old", ch11, "--new", ch10, "--out",
                                 (box.dir / "swapped.html").u8string(), "--no-open"});
    INFO(swapped.output);
    CHECK(swapped.exit_code == 0);
    CHECK(contains(swapped.output, "is stamped engine_mode=" + kCh11 + ", not " + kCh10));

    CHECK(run_exe(box.fillcompare, {"--old", ch10}).exit_code == 2);
}
```

Append this block to the very end of CMakeLists.txt:

```cmake

# ---- CLI end-to-end tests ------------------------------------------------
# tests/test_cli.cpp runs the three console tools as real processes, so
# building the tests builds the tools first and tells the tests where they are.
target_sources(hydra_tests PRIVATE tests/test_cli.cpp)
add_dependencies(hydra_tests hydra_batch hydra_report hydra_fillcompare)
target_compile_definitions(hydra_tests PRIVATE
    HYDRA_BATCH_EXE="$<TARGET_FILE:hydra_batch>"
    HYDRA_REPORT_EXE="$<TARGET_FILE:hydra_report>"
    HYDRA_FILLCOMPARE_EXE="$<TARGET_FILE:hydra_fillcompare>"
)
```

- [ ] **Step 2: Run them and watch four fail.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="hydra_batch*,hydra_report*,hydra_fillcompare*"`. Expected: four cases fail and four pass. "--reindex keeps a legacy database's stamp" fails because the stamp reads `ch11`. "refuses a run whose fill rule disagrees" and "treats an unstamped database with records" fail because the exit code is 0, not 2. "reuses the GUI's scan cache" fails because the output shows "CLI Fixture". The stamp, own-hydra.db, report and fillcompare cases pass already; they pin today's behavior.

- [ ] **Step 3: Stamp only analysis runs, and refuse a mismatch.** In src/cli/batch.cpp, change:

```cpp
    std::unique_ptr<hydra::store::RecordStore> store_ptr = hydra::app::open_store(db, settings.rules.fingerprint());
    hydra::store::RecordStore& store = *store_ptr;

    // Stamp the file with the rule this run used, every run, so hydra_fillcompare
    // can tell a 1.0 database from a 1.1 one.
    store.set_engine_mode(hydra::engine_mode_stamp(
        legacy_fills ? hydra::FillDeadlineRule::Ch10 : hydra::FillDeadlineRule::Ch11));

    if (reindex_only) {
        std::printf("Rebuilding sort columns from stored records...\n");
        int n = store.reindex();
        std::printf("Reindexed %d records.\n", n);
        return 0;
    }
```

to:

```cpp
    std::unique_ptr<hydra::store::RecordStore> store_ptr = hydra::app::open_store(db, settings.rules.fingerprint());
    hydra::store::RecordStore& store = *store_ptr;

    // Reindexing only re-reads stored rows. It scores nothing, so it must not
    // relabel the file: a Clone Hero 1.0 database stays stamped ch10.
    if (reindex_only) {
        std::printf("Rebuilding sort columns from stored records...\n");
        int n = store.reindex();
        std::printf("Reindexed %d records.\n", n);
        return 0;
    }

    // One database holds one fill rule (docs/adr/0010). A file with results
    // but no stamp was written before hydra_batch stamped, by the normal rule;
    // hydra_fillcompare reads it the same way. A file with neither is new.
    const char* ch10 = hydra::engine_mode_stamp(hydra::FillDeadlineRule::Ch10);
    const char* ch11 = hydra::engine_mode_stamp(hydra::FillDeadlineRule::Ch11);
    const std::string run_mode = legacy_fills ? ch10 : ch11;
    std::optional<std::string> file_mode = store.engine_mode();
    if (!file_mode && store.counts().second > 0) file_mode = ch11;
    if (file_mode && *file_mode != run_mode) {
        auto rule_name = [&](const std::string& mode) {
            return mode == ch10 ? "Clone Hero 1.0" : mode == ch11 ? "Clone Hero 1.1"
                                                                  : "unknown";
        };
        std::fprintf(stderr,
                     "This database holds results scored by the %s fill rule "
                     "(engine_mode=%s):\n  %s\n"
                     "This run scores by the %s rule. The rule is not stored on each "
                     "result, so the two cannot share a file.\n"
                     "%s --legacy-fills to write into this database, or give this run "
                     "its own database with --db.\n",
                     rule_name(*file_mode), file_mode->c_str(), db.c_str(),
                     rule_name(run_mode), legacy_fills ? "Drop" : "Add");
        return 2;
    }
    // Stamp the file with the rule this run used, so hydra_fillcompare can tell
    // a 1.0 database from a 1.1 one.
    store.set_engine_mode(run_mode);
```

Change the header comment:

```cpp
// --legacy-fills needs its own --db: the rule is not recorded on a row, so
// 1.0 and 1.1 results must not share a file (docs/adr/0010). Compare two such
// databases with hydra_fillcompare.
```

to:

```cpp
// --legacy-fills needs its own --db: the rule is not recorded on a row, so
// 1.0 and 1.1 results must not share a file (docs/adr/0010). Each run stamps
// its database with the rule it used, and a run whose rule disagrees with an
// existing stamp exits 2 without writing. --reindex never stamps. Compare two
// such databases with hydra_fillcompare.
```

- [ ] **Step 4: Scan through the GUI's cache.** In src/cli/batch.cpp, change:

```cpp
    std::printf("\nDiscovering charts...\n");
    auto [scanitems, folder_errors] = hydra::app::discover_charts(folders);
```

to:

```cpp
    std::printf("\nDiscovering charts...\n");
    // The GUI's last library scan, if this database has one. A chart whose
    // files are unchanged (size and modified time) reuses its hash and song
    // fields instead of being read again. hydra_batch only reads this cache;
    // it never rewrites the GUI's library.
    hydra::store::ChartLibraryCache cache;
    try {
        cache = store.chart_library_cache();
    } catch (const std::exception&) {
        // No cache is only a slower scan.
    }
    auto [scanitems, folder_errors] = hydra::app::discover_charts(
        folders, hydra::app::ScanCallbacks{}, cache.empty() ? nullptr : &cache);
```

- [ ] **Step 5: Run the CLI tests and watch them pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="hydra_batch*,hydra_report*,hydra_fillcompare*"`. Expected: `Status: SUCCESS!` with all eight cases. Note the elapsed time the suite prints for these cases in the report; it is new test time Task 17 will see.

- [ ] **Step 6: Point the replay tests at the new header.** In tests/test_replay.cpp, change:

```cpp
#include "core/replay.h"
```

to:

```cpp
#include "core/replay.h"
#include "replay_json.h"
```

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error, `replay_json.h` not found.

- [ ] **Step 7: Move the JSON functions to tools/.** Create tools/replay_json.h:

```cpp
// The JSON shapes hydra_replay writes and reads: the "paths" array of a `dump`
// or `target` file, one score split, and the windows read back out of a path.
//
// These used to live in core/replay.h, which pulled the whole JSON library
// into every file that includes the replay, the Preview's among them. Only
// hydra_replay and tests/test_replay use them, so they live beside the tool.

#ifndef HYDRA_TOOLS_REPLAY_JSON_H
#define HYDRA_TOOLS_REPLAY_JSON_H

#include <vector>

#include "json.hpp"

#include "core/replay.h"

namespace hydra {

// The same windows as windows_for_path, read out of a `dump` or `target` JSON
// file instead of a live record. `path` is one entry of that file's top-level
// "paths" array; each of its "activations" carries act_tick, deact_tick,
// sqout_tick (-1 or absent when there is none, or in a dump from before v6),
// and a "sqinouts" list whose SqOut entry holds the squeeze-out's offset in
// ms. A window with an offset but no sqout_tick must go through
// resolve_sqout_note before it is replayed.
//
// This exists because the only other way to hand a path to `hydra_replay
// score` was to retype it as an "act:deact,..." string, and that string used
// to drop the squeeze-out offset. Without the offset the squeezed phrase note
// is doubled as if it were still inside Star Power, so the score comes out
// high. Reading the file keeps every field.
//
// Throws std::runtime_error when the JSON is not that shape: no "activations"
// array, an activation missing act_tick or deact_tick, or a deact_tick of -1,
// which is how a dump writes "this record has no deactivation node" and means
// the path cannot be replayed faithfully.
std::vector<ReplayWindow> windows_from_json(const nlohmann::json& path);

// One score split as JSON, one key per kReplayScoreFields entry. The dump's
// per-path "score" object and the `score` command's totals both use it.
nlohmann::json score_json(const ReplayScore& s);

// The "paths" array of a hydra_replay dump: one object per path in `all`, in
// order, with the score split and every activation's ticks, SP meter, skips,
// chord and squeezes. windows_from_json reads one element of it back, and
// fcvideo reads the rest.
nlohmann::json paths_json(const std::vector<const Path*>& all, const SongTiming& timing);

}  // namespace hydra

#endif  // HYDRA_TOOLS_REPLAY_JSON_H
```

Create tools/replay_json.cpp. Its body is the three functions cut from src/core/replay.cpp in this step, byte for byte:

```cpp
#include "replay_json.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "core/timing.h"  // sp_bars_to_measures

namespace hydra {

std::vector<ReplayWindow> windows_from_json(const nlohmann::json& path) {
    if (!path.is_object() || !path.contains("activations") ||
        !path["activations"].is_array())
        throw std::runtime_error("this path has no \"activations\" array");

    std::vector<ReplayWindow> out;
    int index = 0;
    for (const nlohmann::json& act : path["activations"]) {
        const std::string where = "activation " + std::to_string(index++);
        if (!act.is_object() || !act.contains("act_tick") ||
            !act.contains("deact_tick") || !act["act_tick"].is_number() ||
            !act["deact_tick"].is_number())
            throw std::runtime_error(where +
                                     " has no act_tick/deact_tick number");

        ReplayWindow w;
        w.act_tick = act["act_tick"].get<int64_t>();
        w.deact_tick = act["deact_tick"].get<int64_t>();
        // -1 is how the dump writes a missing value. A window with no
        // deactivation node cannot be replayed, and guessing one would print a
        // wrong score with no hint why.
        if (w.act_tick < 0)
            throw std::runtime_error(where + " has no activation tick");
        if (w.deact_tick < 0)
            throw std::runtime_error(
                where +
                " has no deactivation node; the record it came from predates "
                "the field, so this path cannot be replayed");
        if (w.deact_tick < w.act_tick)
            throw std::runtime_error(where +
                                     " deactivates before it activates");

        // -1 (or no key, from a dump written before v6) means "not stamped".
        // The caller resolves a bare offset with resolve_sqout_note.
        if (act.contains("sqout_tick") && act["sqout_tick"].is_number() &&
            act["sqout_tick"].get<int64_t>() >= 0)
            w.sqout_tick = act["sqout_tick"].get<int64_t>();

        if (act.contains("sqinouts") && act["sqinouts"].is_array()) {
            for (const nlohmann::json& sq : act["sqinouts"]) {
                if (!sq.is_object()) continue;
                if (sq.value("kind", std::string()) != "SqOut") continue;
                if (!sq.contains("offset_ms") || !sq["offset_ms"].is_number())
                    throw std::runtime_error(where +
                                             " has a SqOut with no offset_ms");
                w.sqout_offset_ms = sq["offset_ms"].get<double>();
            }
        }
        out.push_back(w);
    }
    return out;
}

nlohmann::json score_json(const ReplayScore& s) {
    nlohmann::json j = nlohmann::json::object();
    for (const ReplayScoreField& f : kReplayScoreFields) j[f.name] = s.*(f.member);
    return j;
}

// The path list `dump` and `target` both print. One shape, so anything that
// reads dump's JSON reads target's too.
nlohmann::json paths_json(const std::vector<const Path*>& all, const SongTiming& timing) {
    nlohmann::json paths = nlohmann::json::array();
    int index = 0;
    for (const Path* p : all) {
        nlohmann::json acts = nlohmann::json::array();
        for (const Activation& act : p->all_activations()) {
            nlohmann::json sq = nlohmann::json::array();
            for (const SPSqueeze& s2 : act.sqinouts)
                sq.push_back(nlohmann::json{{"kind", s2.type_name()},
                                            {"offset_ms", s2.offset()}});

            const int64_t act_tick = act.timecode ? act.timecode->ticks() : -1;
            const std::optional<int64_t>& d = act.deact_tick;
            int64_t nominal = -1;
            if (act.timecode && act.sp_meter)
                nominal = timing
                              .plusmeasure(*act.timecode, sp_bars_to_measures(*act.sp_meter))
                              .ticks();

            acts.push_back(nlohmann::json{
                {"act_tick", act_tick},
                {"deact_tick", d ? *d : -1},
                {"sqout_tick", act.sqout_tick ? *act.sqout_tick : -1},
                {"nominal_deact_tick", nominal},
                {"sp_meter", act.sp_meter ? *act.sp_meter : -1},
                {"skips", act.skips ? *act.skips : -1},
                {"chord_code", act.chord ? act.chord->code() : std::string()},
                {"sqinouts", sq},
            });
        }
        paths.push_back(nlohmann::json{
            {"index", index++},
            {"pathstring", p->pathstring()},
            {"total", p->totalscore()},
            {"score", score_json(score_of(*p))},
            {"activations", acts},
        });
    }
    return paths;
}

}  // namespace hydra
```

In src/core/replay.cpp, delete the three definitions just copied: everything from the line `std::vector<ReplayWindow> windows_from_json(const nlohmann::json& path) {` down to and including the closing `}` of `paths_json` (the line after `    return paths;`). `replay_stored_path` above them and `resolve_sqout_note` below them stay.

In src/core/replay.h, delete the include block:

```cpp
// third_party/json is not on hydra_core's include path — only the tools and
// the test harness list that directory. This is the one file in core that
// reads JSON, so it reaches the header by relative path instead of the whole
// library growing an include directory for it.
#include "../../third_party/json/json.hpp"

```

and delete the three declarations with their comments, from `// The same windows, read out of a `dump` or `target` JSON file instead of a` down to and including `nlohmann::json paths_json(const std::vector<const Path*>& all, const SongTiming& timing);` (they now live in tools/replay_json.h). In the same header, change the comment line

```cpp
// The six score categories, in the order hydra_replay's JSON and its check
// output print them. One list, so the names and the fields cannot drift.
```

to:

```cpp
// The six score categories, in the order hydra_replay's JSON
// (tools/replay_json.h) and its check output print them. One list, so the
// names and the fields cannot drift.
```

In tools/replay.cpp, change:

```cpp
#include "core/replay.h"
#include "core/squeeze_rating.h"
```

to:

```cpp
#include "core/replay.h"
#include "core/squeeze_rating.h"
#include "replay_json.h"
```

Append this block to the end of CMakeLists.txt, after the block from Step 1:

```cmake

# ---- hydra_replay_json: the dump/target JSON shapes (tools only) ----------
# hydra_replay writes and reads these, and tests/test_replay pins them. The
# app never reads a path as JSON, so this stays out of hydra_core, and core/
# stays free of the JSON library.
add_library(hydra_replay_json STATIC tools/replay_json.cpp)
target_include_directories(hydra_replay_json PUBLIC tools third_party/json)
target_link_libraries(hydra_replay_json PUBLIC hydra_core PRIVATE hydra_warnings)
target_link_libraries(hydra_replay PRIVATE hydra_replay_json)
target_link_libraries(hydra_tests PRIVATE hydra_replay_json)
```

- [ ] **Step 8: Run the replay side and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="replay*,*path JSON*,paths_json*,targeted search*"`. Expected: `Status: SUCCESS!`. Run `Select-String -Path src\core\*.h,src\core\*.cpp -Pattern 'nlohmann|json.hpp'`. Expected: nothing printed. Run `.\build_cpp.ps1 -Target hydra_replay; .\build-cpp\Release\hydra_replay.exe selfcheck`. Expected: exit code 0 and a last line ending `FAIL 0 (0 chart(s) skipped)` or with the same skipped count the Task 0 build prints.

- [ ] **Step 9: Document the tools.** In README.md, replace the whole "Command line tools" section, from `## Command line tools` down to the line before `## Building from source`:

````markdown
## Command line tools

Two console tools ship next to the app and share its settings and database
(they read the same `hydra_settings.ini` / `hydra.db` beside the executable):

```
hydra_batch                    Analyze every chart folder from the app's settings
hydra_batch <folder> [...]     ...or specific folders instead
hydra_batch --redo             Re-analyze charts already stored
hydra_batch --reindex          Only rebuild sort columns, no analysis
hydra_batch --db <path>        Target a specific database

hydra_report                   Sortable HTML report of stored paths (top 5 per chart)
hydra_report --paths 20        Top 20 per chart
hydra_report --all-paths       Everything stored
hydra_report --out report.html
hydra_report --no-open         Write the file without opening the browser
```

Both read the app's settings file, so they analyze and report at the same
chart mode and SP cap the app is set to.

````

with:

````markdown
## Command line tools

Three console tools ship next to the app. They share its settings, rules and
database: they read the same `hydra_settings.ini`, `hydra_rules.ini` and
`hydra.db` beside the executable.

```
hydra_batch                    Analyze every chart folder from the app's settings
hydra_batch <folder> [...]     ...or specific folders instead
hydra_batch --redo             Re-analyze charts already stored
hydra_batch --reindex          Only rebuild sort columns, no analysis
hydra_batch --db <path>        Target a specific database
hydra_batch --rules <path>     Take the rule choices from this file, not hydra_rules.ini
hydra_batch --legacy-fills     Score fills by Clone Hero 1.0's rule (needs its own --db)

hydra_report                   Sortable HTML report of stored paths (top 5 per chart)
hydra_report --paths 20        Top 20 per chart
hydra_report --all-paths       Everything stored
hydra_report --out report.html
hydra_report --db <path>       Report on a specific database
hydra_report --rules <path>    Judge records against the rules in this file
hydra_report --no-open         Write the file without opening the browser

hydra_fillcompare --old <ch10.db> --new <ch11.db>
                               Compare Clone Hero 1.0 and 1.1 fill results, chart by chart
hydra_fillcompare ... --out fill_compare.html
hydra_fillcompare ... --rules <path>
hydra_fillcompare ... --no-open
```

All three read the app's settings file, so they analyze, report and compare
at the same chart mode, SP cap, timing limit and score range the app is set to.

Clone Hero 1.1 changed when a drum fill appears. `--legacy-fills` scores by
the older 1.0 rule instead. The rule is not stored on each result, so a 1.0
run needs its own database, and hydra_batch refuses to write one into Hydra's
own `hydra.db`. Each database is stamped with the rule that filled it, and
hydra_batch refuses (exit code 2) a run whose rule disagrees with the stamp.
`--reindex` never changes the stamp. To see what the rule change did, fill two
databases and compare them:

```
hydra_batch --legacy-fills --db ch10.db
hydra_batch --db ch11.db
hydra_fillcompare --old ch10.db --new ch11.db
```

````

In docs/adr/0010-legacy-fill-deadline-is-a-cli-only-mode.md, after the paragraph that ends:

```markdown
`--db`, and `hydra_batch` refuses to run `--legacy-fills` against the database
the app itself reads.
```

add:

```markdown

Each database carries a stamp of the rule that filled it. `hydra_batch`
refuses (exit code 2) a run whose rule disagrees with that stamp, so one file
never mixes the two, and `--reindex` never changes it. A file with results but
no stamp was written before stamping existed, by the 1.1 rule, and counts as
1.1.
```

- [ ] **Step 10: Run everything.** Run the whole `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`. Build `hydra_batch` with `.\build_cpp.ps1 -Target hydra_batch` and run the score-neutral compare from the global rules. Expected: it prints nothing. Its database is fresh, so it has no scan cache, and the output cannot move. Run the README grep from the acceptance criteria. Expected: a line for each of the three patterns.

- [ ] **Step 11: Commit.**

```bash
git add src/cli/batch.cpp src/core/replay.h src/core/replay.cpp tools/replay_json.h tools/replay_json.cpp tools/replay.cpp tests/test_cli.cpp tests/test_replay.cpp CMakeLists.txt README.md docs/adr/0010-legacy-fill-deadline-is-a-cli-only-mode.md
git commit -m "Keep hydra_batch's fill-rule stamp honest, scan through the cache, test the CLIs

Task: Task 4: CLI tools
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/cli/batch.cpp","src/core/replay.h","src/core/replay.cpp","tools/replay_json.h","tools/replay_json.cpp","tools/replay.cpp","tests/test_cli.cpp","tests/test_replay.cpp","CMakeLists.txt","README.md","docs/adr/0010-legacy-fill-deadline-is-a-cli-only-mode.md"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe -tc=\"hydra_batch*,hydra_report*,hydra_fillcompare*,*path JSON*,paths_json*\"","acceptanceCriteria":["`hydra_batch --reindex keeps a legacy database's stamp` passes","`hydra_batch refuses a run whose fill rule disagrees with the database` passes: exit 2 both ways, stamp and count unchanged","`hydra_batch treats an unstamped database with records as Clone Hero 1.1` passes","`hydra_batch reuses the GUI's scan cache` passes","all eight CLI cases pass","`*path JSON*,paths_json*` pass with the JSON functions in tools/","`Select-String -Path src\\core\\*.h,src\\core\\*.cpp -Pattern 'nlohmann|json.hpp'` prints nothing","hydra_replay builds and `selfcheck` exits 0 with FAIL 0","README names hydra_fillcompare, --rules and --legacy-fills","full hydra_tests passes and the score-neutral compare prints nothing"],"modelTier":"standard"}
```

---

### Task 5: Report pages share one stylesheet and one script, and the path report shows each row's mode

Hydra writes three report pages: the path report, the dmleaderboards comparison and the fill-spawn comparison. Each one carries its own copy of the same table script and nearly the same stylesheet. ADR 0002 said the path report had to match the old `hydra_report.py` page byte for byte, and that was the only stated reason for the copies. That Python script was deleted in the C++ cutover (95f22b0), and no test compares page bytes. This task retires ADR 0002 and gives the three pages one stylesheet and one script. Each page keeps only its own title, its body markup, and a small `PAGE` object (a plain JavaScript settings object) that names its columns, its filters, how a row is drawn and which stats sit above the table.

The path report mixes every chart mode into one table without saying which row is which. A song analyzed at Expert Pro Drums and at Hard shows two rank-1 rows that look like duplicates. Each row already carries its mode in the page's data; the page just never shows it. This task adds a Mode column after Charter, and every row stays. The subtitle's "N records across M songs" counts the whole database today. It will count the records that have rows on the page and the songs those records belong to.

The rest is cleanup. `ReportRow::delta` is written into the page data, but the page never reads it, so it goes. `py_repr` formats every number in the page data, so it stays, but it moves out of report.h, where it was "exposed for tests" that never call it. `count_chart_chords` is only called by its own test, and its comment names a "how big is this chart" display that doesn't exist, so the function and its test go. The report built the timing-tier table (the Normal/Hard/Extreme bands) once per row; it now builds it once per report. The two comparison pages each built the same "records by chart hash" index; they now share `report::records_by_hash`.

Audit check: `py_repr` is not dead, as said above. report_files.cpp needs no change here; its per-frame `report_file_exists()` call from the library window belongs to T7.

What the user sees: the path report gains a Mode column after Charter, and its subtitle counts the records on the page instead of the whole database. Everything else on all three pages looks and behaves the same. The task proves it by rendering each page in headless Edge (a browser with no window) before and after the change, driving every sort, header click, filter and search, and comparing what a reader would see, down to computed colors and fonts.

**Depends on:** nothing.

**Expected overlaps:** T3 edits src/app/analysis.cpp and tests/test_analysis.cpp for the batch runner; this task only deletes `count_chart_chords` (analysis.h, analysis.cpp) and its one test case. T11 edits `collect_rows` in src/app/report.cpp to stop copying activations (the `path->all_activations()` loop); this task edits the lines around that loop (tier table, `delta`, `hyhash`) and leaves the loop itself alone. T16 later renames `lower_hex` inside `report::records_by_hash` and replaces the trim at the end of `report::plain`. T14 moves `/utf-8` onto every target; the em dash in the subtitle string is left as it is here. T1 changes how song names are stored (decision 1); the report reads `meta.ref_name` the same way either way. This task adds ADR 0016; if another task merged first with an ADR 0016, the merger renumbers this one.

**Goal:** One stylesheet and one script serve all three report pages, the path report gains a Mode column and a subtitle that counts what it shows, and the dead report code is gone.

**Files:**
- Create: `docs/adr/0016-report-pages-share-one-stylesheet-and-script.md`
- Modify: `src/app/html_page.h`, `src/app/html_page.cpp`, `src/app/report.h`, `src/app/report.cpp`, `src/app/dm_report.h`, `src/app/dm_report.cpp`, `src/app/fill_report.h`, `src/app/fill_report.cpp`, `src/app/analysis.h`, `src/app/analysis.cpp`, `docs/adr/0002-sortable-page-fragments-canon-from-path-report.md`
- Test: `tests/test_report.cpp`, `tests/test_fill_report.cpp`, `tests/test_analysis.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="the three report pages share one stylesheet and one script"` passes.
- [ ] `hydra_tests.exe -tc="path report shows each row's chart mode*"` passes, including its check that the page data has no `"delta":` key.
- [ ] `hydra_tests.exe -tc="report lists only the wanted cap and names it"` passes with its new checks: `records == 1`, `songs == 1`, and the subtitle text `1 records across 1 songs`.
- [ ] `hydra_tests.exe -tc="tier_for over a built table*"` and `hydra_tests.exe -tc="records_by_hash*"` pass.
- [ ] The full `hydra_tests.exe` run ends with `Status: SUCCESS!`.
- [ ] `page_check.py compare` prints `SAME` for dm, fill and real_fill, and prints the Mode values followed by `SAME` for paths and real_paths (Step 11).
- [ ] `hydra_uitest.exe --test settings-and-reports` passes.
- [ ] `git grep -nE "kSortable|byte-exact|count_chart_chords" -- src tests` prints nothing, and `git grep -n "py_repr" -- src/app/report.h tests` prints nothing.
- [ ] `git grep -n "timing_tiers(" -- src/app/report.cpp` prints exactly three lines: the forwarding `tier_for`, `collect_rows` and `build_html`.

**Verify:** `.\build_cpp.ps1; .\build-cpp\Release\hydra_tests.exe` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Record the pages as they are today.** This step comes before the failing tests, because the "before" pages must be built from today's code. First add a sample-page writer to the end of tests/test_report.cpp. It is skipped in normal runs and only runs on request. It builds one small page of each kind from fixed rows, so the dm page (which has no command-line tool) gets checked too. Add these includes to the include block of tests/test_report.cpp, after `#include <atomic>`:

```cpp
#include <cstdlib>
```

and after `#include "app/analysis.h"`:

```cpp
#include "app/dm_report.h"
#include "app/fill_report.h"
```

Then append this at the end of the file:

```cpp
// Not an invariant: writes one small page of each kind into the folder named
// by HYDRA_PAGE_SAMPLES, built from fixed rows, so a page change can be
// checked in a real browser before and after (docs/adr/0016). Run it with
//   hydra_tests.exe --no-skip -tc="report pages: write samples*"
TEST_CASE("report pages: write samples for the browser check" * doctest::skip()) {
    const char* dir = std::getenv("HYDRA_PAGE_SAMPLES");
    REQUIRE(dir != nullptr);
    const std::filesystem::path out = std::filesystem::u8path(dir);
    std::filesystem::create_directories(out);

    std::vector<report::ReportRow> paths;
    auto add_path = [&](const std::string& song, const char* mode, int rank,
                        const std::string& path, int64_t score, std::optional<double> ms,
                        std::optional<double> efill) {
        report::ReportRow r;
        r.song = song;
        r.artist = "Artist of " + song;
        r.charter = "Charter & Co";
        r.mode = mode;
        r.rank = rank;
        r.path = path;
        r.score = score;
        r.acts = 3 + rank;
        r.skip = rank - 1;
        r.ms = ms;
        auto [tier, tok] = report::tier_for(ms, 85.0);
        r.tier = tier;
        r.tok = tok;
        r.efill = efill;
        r.mult = 2.345 + rank;
        r.sqin = rank;
        r.sqout = 2 - rank % 2;
        r.notes = 1200 + rank;
        paths.push_back(r);
    };
    std::string long_path;
    for (int i = 0; i < 80; ++i) long_path += "1-E2+ ";
    add_path("Song A", "Expert Pro Drums, 2x Bass", 1, "1-E2+ 0-E1", 123456, 12.5, -3.25);
    add_path("Song A", "Expert Pro Drums, 2x Bass", 2, "1-E2 0-E1-", 123000, 48.0, std::nullopt);
    add_path("Song A", "Hard Drums, 1x Bass", 1, "0 0 1", 98000, std::nullopt, std::nullopt);
    add_path("Song B", "Expert Pro Drums, 2x Bass", 1, long_path, 250000, 171.0, 4.5);
    add_path("Song B", "Expert Pro Drums, 2x Bass", 2, "2 1-E3", 249500, 1.5, 0.0);
    add_path("Song C", "Expert Drums, 1x Bass", 1, "1 1 1", 77000, 90.0, std::nullopt);

    std::vector<dm_report::DmReportRow> dm;
    auto add_dm = [&](const char* song, int64_t actual, std::optional<int64_t> optimal,
                      const char* status, bool fc, std::optional<int> rank) {
        dm_report::DmReportRow r;
        r.song = song;
        r.artist = "Artist";
        r.charter = "Charter";
        r.identifier = "hash";
        r.actual = actual;
        r.optimal = optimal;
        if (optimal) r.delta = *optimal - actual;
        if (optimal && *optimal > 0)
            r.pct = static_cast<double>(actual) / static_cast<double>(*optimal) * 100.0;
        r.is_fc = fc;
        r.percent = fc ? 100 : 97;
        r.speed = 100;
        r.rank = rank;
        r.posted = "2026-09-20T12:34:56Z";
        r.status = status;
        dm.push_back(r);
    };
    add_dm("Song A", 120000, 123456, "matched", true, 3);
    add_dm("Song B", 251000, 250000, "above optimal", false, 1);
    add_dm("Song C", 90000, std::nullopt, "unmatched", false, std::nullopt);

    std::vector<fill_report::FillCompareRow> fill;
    auto add_fill = [&](const char* song, std::optional<int64_t> old_score,
                        std::optional<int64_t> new_score, const char* status) {
        fill_report::FillCompareRow r;
        r.song = song;
        r.artist = "Artist";
        r.charter = "Charter";
        r.hyhash = song;
        r.old_score = old_score;
        r.new_score = new_score;
        if (old_score && new_score) r.delta = *new_score - *old_score;
        if (old_score) { r.old_path = "1-E2 0"; r.old_acts = 2; }
        if (new_score) { r.new_path = "1-E2 0-E1"; r.new_acts = 3; }
        r.notes = 900;
        r.status = status;
        fill.push_back(r);
    };
    add_fill("Song A", 100000, 100500, "1.1 higher");
    add_fill("Song B", 100000, 99000, "1.0 higher");
    add_fill("Song C", 100000, 100000, "same");
    add_fill("Song D", 100000, std::nullopt, "only 1.0");
    add_fill("Song E", std::nullopt, 100000, "only 1.1");

    write_report_file(out / "paths.html",
                      report::build_html(paths, "Sample subtitle", "Sample footer", 85.0));
    write_report_file(out / "dm.html",
                      dm_report::build_dm_html(dm, "Sample subtitle", "Sample footer"));
    write_report_file(out / "fill.html",
                      fill_report::build_fill_html(fill, "Sample subtitle", "Sample footer"));
}
```

Next, write the browser probe to `$env:TEMP\hydra_t5\page_check.py` with the Write tool. It is a throwaway tool for this task and is not committed. It has been run against today's path and fill pages: two renders of the same page compare `SAME`.

```python
"""Record what a reader sees on a Hydra report page, and compare two recordings.

    python page_check.py render <page.html> <out.json>
    python page_check.py compare <before.json> <after.json> [--drop-column NAME]

render opens the page in headless Edge with a probe script appended. The probe
waits for the page's first draw, then drives every sort order, every header
click, every filter option, each checkbox and the search box. It records the
visible text, the cell and row classes, the tooltips and the computed styles.

compare prints SAME when the two recordings match. With --drop-column, that
column is removed from the second recording first, and its distinct values
are printed so you can see what the new column shows.
"""
import html
import json
import pathlib
import re
import subprocess
import sys
import tempfile

EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"

PROBE = r"""
<script>
requestAnimationFrame(() => setTimeout(() => {
  const KEYS = ['color', 'backgroundColor', 'fontSize', 'fontWeight', 'fontFamily',
                'maxWidth', 'textAlign', 'boxShadow', 'borderTopColor', 'borderLeftColor',
                'position', 'overflowX', 'textOverflow', 'whiteSpace', 'paddingLeft'];
  const style = e => { const s = getComputedStyle(e); return KEYS.map(k => s[k]).join(' | '); };
  const names = () => COLS.map(c => c.t);
  const cell = td => ({cls: td.className, text: td.textContent, title: td.title, style: style(td),
                       chip: td.firstElementChild
                         ? td.firstElementChild.className + ' | ' + style(td.firstElementChild) : ''});
  const snap = () => {
    const n = names();
    const empty = document.getElementById('empty');
    return {
      count: document.getElementById('count').textContent,
      empty: [empty.hidden, empty.textContent],
      stats: [...document.querySelectorAll('.stat')].map(s => [s.children[0].textContent, s.children[1].textContent]),
      heads: [...document.querySelectorAll('#head th')].map((th, i) => ({
        name: n[i], text: th.textContent, sort: th.getAttribute('aria-sort'),
        cls: th.className, title: th.title, style: style(th)})),
      rows: [...document.querySelectorAll('#body tr')].slice(0, 200).map(tr => ({
        cls: tr.className,
        cells: Object.fromEntries([...tr.children].map((td, i) => [n[i], cell(td)]))})),
    };
  };
  const fire = (el, ev) => el.dispatchEvent(new Event(ev));
  const sortby = document.getElementById('sortby');
  const sortdir = document.getElementById('sortdir');
  const k0 = sortKey, d0 = sortDir;

  const out = {title: document.title};
  // Text-bearing chrome: its words and its look.
  out.chrome = [...document.querySelectorAll(
      'h1, .sub, footer, .sorter label, #sortdir, #q, .controls select:not(#sortby), .toggle, .count')]
    .map(e => ({tag: e.tagName, id: e.id,
                text: e.tagName === 'SELECT' ? [...e.options].map(o => o.value + '=' + o.textContent).join(';') : e.textContent,
                placeholder: e.placeholder || '', style: style(e)}));
  // Containers: their look only (their text is the table, recorded below).
  out.boxes = [...document.querySelectorAll('body, .wrap, header, .stats, .controls, .tablewrap, table, .stat, .stat-k, .stat-v')]
    .map(e => ({tag: e.tagName, style: style(e)}));
  out.sortby = [...sortby.options].map(o => o.textContent);
  out.initial = snap();

  out.sorts = [];
  for (const c of COLS) for (const d of [1, -1]) {
    setSort(c.k, d);
    out.sorts.push({key: c.t, dir: d, button: sortdir.textContent, snap: snap()});
  }
  setSort(k0, d0);

  out.clicks = [];
  document.querySelectorAll('#head th').forEach((th, i) => {
    const name = names()[i];
    th.click();
    const first = [sortby.value, sortdir.textContent];
    th.click();
    out.clicks.push({name, first, second: [sortby.value, sortdir.textContent]});
  });
  setSort(k0, d0);

  out.filters = [];
  for (const sel of document.querySelectorAll('.controls select:not(#sortby)')) {
    for (const o of [...sel.options]) {
      sel.value = o.value; fire(sel, 'change');
      out.filters.push({control: sel.id, value: o.value, snap: snap()});
    }
    sel.value = ''; fire(sel, 'change');
  }
  for (const box of document.querySelectorAll('.controls input[type=checkbox]')) {
    box.checked = !box.checked; fire(box, 'change');
    out.filters.push({control: box.id, value: box.checked, snap: snap()});
    box.checked = !box.checked; fire(box, 'change');
  }
  const q = document.getElementById('q');
  for (const text of ['a', 'Song B', 'E2', 'no such chart zz']) {
    q.value = text; fire(q, 'input');
    out.filters.push({control: 'q', value: text, snap: snap()});
  }
  q.value = ''; fire(q, 'input');

  const pre = document.createElement('pre');
  pre.id = 'proof';
  pre.textContent = JSON.stringify(out);
  document.body.appendChild(pre);
}, 50));
</script>
"""


def render(page, out):
    src = pathlib.Path(page).read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
        probe_page = pathlib.Path(tmp) / "probe.html"
        probe_page.write_text(src + PROBE, encoding="utf-8")
        run = subprocess.run(
            [EDGE, "--headless=new", "--disable-gpu", "--no-first-run",
             f"--user-data-dir={tmp}\\profile", "--window-size=1600,1000",
             "--virtual-time-budget=20000", "--dump-dom", probe_page.as_uri()],
            capture_output=True, text=True, encoding="utf-8", timeout=180)
    m = re.search(r'<pre id="proof">(.*?)</pre>', run.stdout, re.S)
    if not m:
        sys.exit(f"{page}: the probe never reported (the page script may have thrown)")
    data = json.loads(html.unescape(m.group(1)))
    pathlib.Path(out).write_text(json.dumps(data, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"{page}: recorded {len(data['initial']['rows'])} rows, "
          f"{len(data['sorts'])} sorts, {len(data['filters'])} filter states")


def drop_column(rec, name):
    """Removes one column from a recording everywhere it shows; returns its values."""
    values = set()

    def strip(s):
        s["heads"] = [h for h in s["heads"] if h["name"] != name]
        for r in s["rows"]:
            c = r["cells"].pop(name, None)
            if c is not None:
                values.add(c["text"])

    strip(rec["initial"])
    rec["sorts"] = [x for x in rec["sorts"] if x["key"] != name]
    for x in rec["sorts"]:
        strip(x["snap"])
    for x in rec["filters"]:
        strip(x["snap"])
    rec["sortby"] = [t for t in rec["sortby"] if t != name]
    rec["clicks"] = [c for c in rec["clicks"] if c["name"] != name]
    return sorted(values)


def diffs(a, b, where="", out=None):
    out = [] if out is None else out
    if type(a) is not type(b):
        out.append(f"{where}: {a!r} != {b!r}")
    elif isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                out.append(f"{where}.{k}: only in {'after' if k in b else 'before'}")
            else:
                diffs(a[k], b[k], f"{where}.{k}", out)
    elif isinstance(a, list):
        if len(a) != len(b):
            out.append(f"{where}: {len(a)} items before, {len(b)} after")
        for i, (x, y) in enumerate(zip(a, b)):
            diffs(x, y, f"{where}[{i}]", out)
    elif a != b:
        out.append(f"{where}: {a!r} != {b!r}")
    return out


def compare(before, after, drop=None):
    a = json.loads(pathlib.Path(before).read_text(encoding="utf-8"))
    b = json.loads(pathlib.Path(after).read_text(encoding="utf-8"))
    if drop:
        print(f"dropped column {drop}: values {drop_column(b, drop)}")
    found = diffs(a, b)
    if not found:
        print("SAME")
        return 0
    for line in found[:25]:
        print(line)
    print(f"{len(found)} differences")
    return 1


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "render":
        render(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 4 and sys.argv[1] == "compare":
        drop = sys.argv[5] if len(sys.argv) >= 6 and sys.argv[4] == "--drop-column" else None
        sys.exit(compare(sys.argv[2], sys.argv[3], drop))
    else:
        sys.exit(__doc__)
```

Now build everything and record five pages: the three samples, plus a real path report and a real fill comparison built from the test corpus. The corpus batch takes under a second.

```powershell
$out = "$env:TEMP\hydra_t5"
New-Item -ItemType Directory -Force "$out\before", "$out\after" | Out-Null
.\build_cpp.ps1
$env:HYDRA_PAGE_SAMPLES = "$out\before"
.\build-cpp\Release\hydra_tests.exe --no-skip -tc="report pages: write samples*"
.\build-cpp\Release\hydra_batch.exe --db "$out\ch11.db" testdata\input
.\build-cpp\Release\hydra_batch.exe --legacy-fills --db "$out\ch10.db" testdata\input
.\build-cpp\Release\hydra_report.exe --db "$out\ch11.db" --out "$out\before\real_paths.html" --no-open
.\build-cpp\Release\hydra_fillcompare.exe --old "$out\ch10.db" --new "$out\ch11.db" --out "$out\before\real_fill.html" --no-open
foreach ($p in "paths", "dm", "fill", "real_paths", "real_fill") {
    C:\Python314\python.exe "$out\page_check.py" render "$out\before\$p.html" "$out\before\$p.json"
}
```

Expected: five `recorded N rows` lines (real_paths shows 97 rows, one per corpus chart, because "Best path only" starts ticked).

- [ ] **Step 2: Write the failing tests.** In tests/test_report.cpp, add `#include "app/html_page.h"` after `#include "app/fill_report.h"` and `#include <unordered_map>` after `#include <string>`. In the test case "report lists only the wanted cap and names it", right after:

```cpp
    CHECK(four.html.find("SP cap 4 bars") != std::string::npos);
```

add:

```cpp
    // The subtitle counts what the page lists: the one record at 4 bars, not
    // the 8-bar record the database also holds for the same chart.
    CHECK(four.records == 1);
    CHECK(four.songs == 1);
    CHECK(four.html.find("1 records across 1 songs") != std::string::npos);
```

Then add these cases before the sample-page writer:

```cpp
TEST_CASE("path report shows each row's chart mode in its own column") {
    report::ReportRow a;
    a.song = "Song A";
    a.mode = "Expert Pro Drums, 2x Bass";
    a.path = "1";
    a.tier = "None";
    a.tok = "tn";
    report::ReportRow b = a;
    b.mode = "Hard Drums, 1x Bass";
    const std::string html = report::build_html({a, b}, "sub", "foot", 85.0);

    CHECK(html.find("{k:'mode',") != std::string::npos);
    CHECK(html.find("t:'Mode'") != std::string::npos);
    CHECK(html.find("['dim trunc mode', r.mode]") != std::string::npos);
    CHECK(html.find("\"mode\":\"Expert Pro Drums, 2x Bass\"") != std::string::npos);
    CHECK(html.find("\"mode\":\"Hard Drums, 1x Bass\"") != std::string::npos);
    // The page never read each row's gap to the best path, so the payload no
    // longer carries it.
    CHECK(html.find("\"delta\":") == std::string::npos);
}

TEST_CASE("the three report pages share one stylesheet and one script") {
    const std::string paths = report::build_html({}, "sub", "foot", 85.0);
    const std::string dm = dm_report::build_dm_html({}, "sub", "foot");
    const std::string fill = fill_report::build_fill_html({}, "sub", "foot");
    for (const std::string* page : {&paths, &dm, &fill}) {
        CHECK(page->find(html::kReportCss) != std::string::npos);
        CHECK(page->find(html::kReportJs) != std::string::npos);
        // Rules no page used, and the theme switch nothing ever sets, are gone.
        CHECK(page->find(".delta {") == std::string::npos);
        CHECK(page->find(".rank {") == std::string::npos);
        CHECK(page->find("data-theme") == std::string::npos);
    }
    CHECK(paths.find("<div class=\"wrap\">") != std::string::npos);
    CHECK(dm.find("<div class=\"wrap dm\">") != std::string::npos);
    CHECK(fill.find("<div class=\"wrap fill\">") != std::string::npos);
}

TEST_CASE("tier_for over a built table matches the window form") {
    const std::vector<TimingTier> tiers = timing_tiers(85.0);
    const std::vector<std::optional<double>> samples = {
        std::nullopt, 0.0, 1.9, 2.0, 42.5, 127.4, 169.9, 170.0, 500.0};
    for (const std::optional<double>& ms : samples)
        CHECK(report::tier_for(ms, tiers) == report::tier_for(ms, 85.0));
}

TEST_CASE("records_by_hash keys every listed record by its lower-case hash") {
    store::RecordStore store(":memory:");
    AnalysisSettings settings;
    settings.depth_value = 0;
    settings.time_budget_s = std::nullopt;
    bool added = false;
    for (const std::string& path : corpus::chart_paths()) {
        try {
            AnalysisResult result = analyze_chart_file(path, settings);
            if (result.song.is_empty() || result.record.paths.empty()) continue;
            store.add_song("ABCDEF0123", "Title", "Artist", "Charter", result.song);
            store.add_record(
                store::RecordKey{"ABCDEF0123", "mode", store::CapQuery::at(4)},
                result.record);
            added = true;
            break;
        } catch (const std::exception&) {
            continue;
        }
    }
    REQUIRE(added);

    const std::unordered_map<std::string, store::RecordListing> by_hash =
        report::records_by_hash(store, "mode", store::CapQuery::at(4), store::Lens{});
    REQUIRE(by_hash.size() == 1);
    REQUIRE(by_hash.count("abcdef0123") == 1);
    CHECK(by_hash.at("abcdef0123").hyhash == "ABCDEF0123");
    CHECK(report::records_by_hash(store, "other mode", store::CapQuery::at(4),
                                  store::Lens{})
              .empty());
}
```

In tests/test_fill_report.cpp, the page no longer spells its first sort as a `let`. Change:

```cpp
    CHECK(html.find("let sortKey = 'delta', sortDir = -1;") != std::string::npos);
```

to:

```cpp
    CHECK(html.find("sortKey: 'delta',") != std::string::npos);
```

In tests/test_analysis.cpp, delete the whole test case that starts `TEST_CASE("count_chart_chords matches the song parser's code tally") {` and ends with `MESSAGE("checked " << checked << " charts");` and its closing `}` (today's lines 37 to 58).

- [ ] **Step 3: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors in tests/test_report.cpp, because `hydra::app::html` has no `kReportCss` or `kReportJs`, `report` has no `records_by_hash`, and no `tier_for` overload takes a `std::vector<TimingTier>`.

- [ ] **Step 4: One stylesheet and one script in html_page.** In src/app/html_page.h, change the top comment:

```cpp
// Shared plumbing for the HTML report pages (app/report.cpp and
// app/dm_report.cpp): template substitution and the escaping helpers both
// pages embed their row data with.
```

to:

```cpp
// Shared plumbing for the three HTML report pages (app/report.cpp,
// app/dm_report.cpp, app/fill_report.cpp): the one stylesheet and script they
// all use, template substitution, and the escaping helpers they embed their
// row data with.
```

Then replace everything from `// ---- sortable page fragments ----` down to (not including) `}  // namespace hydra::app::html` with:

```cpp
// ---- the shared report page -----------------------------------------------
// All three report pages are one stylesheet and one script wrapped around each
// page's own title, body markup and PAGE settings (docs/adr/0016). Both stay
// ASCII: this file compiles into hydra_core, so a glyph goes in as an HTML
// entity or a \uXXXX JavaScript escape.
extern const char* const kReportCss;     // every rule the three pages use
extern const char* const kReportJsHead;  // the data tag, DATA, DASH, fmt, fmtMs
extern const char* const kReportJs;      // sorting, filtering, drawing, first render

// One page's template: the shared head, stylesheet and script around the
// page's <title> text, its body markup and its `const PAGE = {...};` script.
// The result still carries __SUBTITLE__, __FOOTER__ and __DATA__ for
// render_page to fill.
std::string page_template(const char* title, const char* body, const char* page_js);

```

In src/app/html_page.cpp, replace everything from `// ---- sortable page fragments ----` down to (not including) `}  // namespace hydra::app::html` with the code below. The CSS is today's `kSortableCss*` text in the same order, with three changes: the two `:root[data-theme=...]` blocks are gone (nothing sets `data-theme`), the per-page column and chip rules are merged in, and the comparison pages' different widths are scoped by a class on `.wrap`. The script is today's `kSortableJsSorter` and `kSortableJsBoot` plus the table code the three pages each carried, with the per-page parts read from `PAGE`.

```cpp
// ---- the shared report page -----------------------------------------------

namespace {

const char* const kHead = R"frag(<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
)frag";

}  // namespace

const char* const kReportCss = R"css(:root {
  color-scheme: light dark;
  --paper: #faf9f7;
  --surface: #ffffff;
  --raised: #f2f0ec;
  --ink: #15171d;
  --muted: #6a6e79;
  --rule: #e3e1db;
  --sp: #b07d0a;
  --sp-soft: #f6e7c2;
  --t0: #2c7a5e; --t1: #9a7a1e; --t2: #b85f2c; --t3: #b23c3c; --t4: #8e3070; --t5: #5b3fa8;
  --tn: #9aa0ab;
  --shadow: 0 1px 2px rgba(20,22,28,.06), 0 8px 24px rgba(20,22,28,.05);
}
@media (prefers-color-scheme: dark) {
  :root {
    --paper: #101219; --surface: #171a22; --raised: #1e222c;
    --ink: #e9e7e2; --muted: #8f95a1; --rule: #282d39;
    --sp: #f0b429; --sp-soft: #3a2e12;
    --t0: #4fbf94; --t1: #e0b13f; --t2: #f0894e; --t3: #f2686b; --t4: #e07ac0; --t5: #a78bfa;
    --tn: #5c626e;
    --shadow: 0 1px 2px rgba(0,0,0,.4), 0 8px 24px rgba(0,0,0,.3);
  }
}

* { box-sizing: border-box; }
body {
  margin: 0;
  background: var(--paper);
  color: var(--ink);
  font-family: ui-sans-serif, system-ui, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  font-size: 14px;
  line-height: 1.5;
}
.mono, td.num, .path, .stat-v {
  font-family: ui-monospace, "Cascadia Mono", "Consolas", "SF Mono", Menlo, monospace;
  font-variant-numeric: tabular-nums;
}

.wrap { max-width: 1760px; margin: 0 auto; padding: 28px 20px 64px; display: flex; flex-direction: column; gap: 20px; }

header { display: flex; flex-direction: column; gap: 6px; }
h1 { margin: 0; font-size: 20px; font-weight: 650; letter-spacing: -.01em; }
h1 .accent { color: var(--sp); }
.sub { color: var(--muted); font-size: 13px; }

.stats { display: flex; flex-wrap: wrap; gap: 10px; }
.stat {
  background: var(--surface); border: 1px solid var(--rule); border-radius: 8px;
  padding: 10px 14px; min-width: 116px; box-shadow: var(--shadow);
}
.stat-k { font-size: 10px; text-transform: uppercase; letter-spacing: .09em; color: var(--muted); }
.stat-v { font-size: 19px; font-weight: 600; margin-top: 3px; }

.controls { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
input[type="search"], select, button {
  font: inherit; color: var(--ink); background: var(--surface);
  border: 1px solid var(--rule); border-radius: 7px; padding: 8px 11px;
}
input[type="search"] { min-width: 220px; flex: 1 1 220px; }
button { cursor: pointer; }
button:hover, select:hover { border-color: var(--sp); }

/* Sorting is the point of these pages, so it gets a control of its own rather
   than living only on column headers -- with a dozen or more columns, the ones
   worth sorting by are usually scrolled off the right-hand side. */
.sorter { display: inline-flex; align-items: center; gap: 6px; }
.sorter label { color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: .08em; }
#sortdir { min-width: 108px; text-align: left; }
input:focus-visible, select:focus-visible, th:focus-visible, button:focus-visible {
  outline: 2px solid var(--sp); outline-offset: 2px;
}
.toggle { display: inline-flex; align-items: center; gap: 7px; color: var(--muted); cursor: pointer; user-select: none; }
.count { color: var(--muted); font-size: 13px; margin-left: auto; }

.tablewrap {
  overflow-x: auto; background: var(--surface);
  border: 1px solid var(--rule); border-radius: 10px; box-shadow: var(--shadow);
  /* Always show the horizontal bar: the numeric columns live off to the
     right, and a scroller you cannot see is a scroller nobody uses. */
  scrollbar-color: var(--muted) transparent;
}
.tablewrap::-webkit-scrollbar { height: 12px; }
.tablewrap::-webkit-scrollbar-thumb { background: var(--rule); border-radius: 6px; }
.tablewrap::-webkit-scrollbar-thumb:hover { background: var(--muted); }
table { border-collapse: separate; border-spacing: 0; width: 100%; }
thead th {
  position: sticky; top: 0; z-index: 2;
  background: var(--raised); color: var(--muted);
  font-size: 10px; text-transform: uppercase; letter-spacing: .08em; font-weight: 600;
  text-align: left; padding: 9px 10px; white-space: nowrap;
  border-bottom: 1px solid var(--rule); cursor: pointer;
}
thead th.num, td.num { text-align: right; }
thead th:hover { color: var(--ink); background: var(--surface); }
/* Every header carries its affordance, not just the active one. */
thead th .arrow { opacity: .35; margin-left: 4px; }
thead th[aria-sort] { color: var(--ink); }
thead th[aria-sort] .arrow { opacity: 1; color: var(--sp); }
tbody td { padding: 7px 10px; border-bottom: 1px solid var(--rule); white-space: nowrap; }
tbody tr:last-child td { border-bottom: 0; }
tbody tr:hover td { background: var(--raised); }
tbody tr.best td:first-child { box-shadow: inset 3px 0 0 var(--sp); }

/* Keep the song visible while reading the numbers off to the right. */
thead th:first-child { left: 0; z-index: 4; }
tbody td:first-child { position: sticky; left: 0; z-index: 1; background: var(--surface); }
tbody tr:hover td:first-child { background: var(--raised); }

/* Every text column is capped. Left to size themselves, a full-discography
   path string (hundreds of activations) or a charter credit carrying Clone
   Hero colour markup stretches its column to thousands of pixels and pushes
   the numbers off the far right of the page. Hover for the full value; the
   title attribute carries it. */
td.trunc { overflow: hidden; text-overflow: ellipsis; }
.song { font-weight: 550; max-width: 240px; overflow: hidden; text-overflow: ellipsis; }
td.artist { max-width: 150px; }
td.charter { max-width: 150px; }
td.mode { max-width: 190px; }
td.path { max-width: 230px; }
.dim { color: var(--muted); }
.path { color: var(--ink); }
.pos { color: var(--t0); font-weight: 600; }
.neg { color: var(--t3); }
/* The two comparison pages size a few columns their own way. */
.dm .song { max-width: 260px; }
.dm td.artist { max-width: 170px; }
.fill td.charter { max-width: 130px; }
.fill td.path { max-width: 200px; font-size: 12px; }

.chip {
  display: inline-block; padding: 1px 7px; border-radius: 999px;
  font-size: 11px; font-weight: 600; letter-spacing: .01em;
  border: 1px solid currentColor;
}
.t0{color:var(--t0)} .t1{color:var(--t1)} .t2{color:var(--t2)}
.t3{color:var(--t3)} .t4{color:var(--t4)} .t5{color:var(--t5)} .tn{color:var(--tn); border-color:transparent}
.s-matched{color:var(--t0)} .s-above{color:var(--t1)} .s-unmatched{color:var(--tn); border-color:transparent}
/* "1.1 higher" is the interesting, rare case, so it gets the strong green;
   "1.0 higher" (the common drop) is red, ties are neutral, and the two
   one-sided statuses are muted so they read as missing data, not a result. */
.s-newhigh{color:var(--t0); border-color:var(--t0)} .s-oldhigh{color:var(--t3)} .s-same{color:var(--tn)} .s-only{color:var(--muted); border-color:transparent}

.empty { padding: 40px; text-align: center; color: var(--muted); }
footer { color: var(--muted); font-size: 12px; }
)css";

const char* const kReportJsHead = R"js(<script id="data" type="application/json">__DATA__</script>
<script>
const DATA = JSON.parse(document.getElementById('data').textContent);
const DASH = '\u2014';
const fmt = n => n === null || n === undefined ? DASH : n.toLocaleString();
const fmtMs = n => n === null || n === undefined ? DASH : n.toFixed(1);

)js";

const char* const kReportJs = R"js(
// Everything below is the same on every report page. The page's own PAGE
// (above) names its columns, which rows its filters keep, how a row is drawn,
// and which stats sit above the table.
const ROWS = PAGE.rows;
const COLS = PAGE.cols;
let sortKey = PAGE.sortKey, sortDir = PAGE.sortDir;

function visible() {
  const q = document.getElementById('q').value.trim().toLowerCase();
  return ROWS.filter(PAGE.filter(q));
}

function render() {
  const rows = visible();
  const dir = sortDir;
  rows.sort((a, b) => {
    let x = a[sortKey], y = b[sortKey];
    // Nulls always sort to the bottom, whichever direction is active.
    if (x === null || x === undefined) return 1;
    if (y === null || y === undefined) return -1;
    if (typeof x === 'string') return dir * x.localeCompare(y);
    return dir * (x - y);
  });

  document.querySelectorAll('#head th').forEach((th, i) => {
    const c = COLS[i];
    if (c.k === sortKey) th.setAttribute('aria-sort', dir === 1 ? 'ascending' : 'descending');
    else th.removeAttribute('aria-sort');
    // Inactive columns keep a dim double arrow, so it is obvious every one
    // of them can be sorted.
    th.querySelector('.arrow').textContent =
      c.k === sortKey ? (dir === 1 ? '\u2191' : '\u2193') : '\u21c5';
  });

  const body = document.getElementById('body');
  body.textContent = '';
  const frag = document.createDocumentFragment();

  for (const r of rows) {
    const tr = document.createElement('tr');
    const rowCls = PAGE.rowClass ? PAGE.rowClass(r) : '';
    if (rowCls) tr.className = rowCls;

    // A cell is [class, text], or [chip class, text, 'chip'] for a coloured
    // pill such as the timing tier or a status.
    for (const [cls, val, kind] of PAGE.cells(r)) {
      const td = document.createElement('td');
      if (kind === 'chip') {
        const chip = document.createElement('span');
        chip.className = cls;
        chip.textContent = val;
        td.appendChild(chip);
      } else {
        td.className = cls;
        td.textContent = val;
        // Truncated cells still have to be readable somehow.
        if (cls.includes('trunc') && val) td.title = val;
      }
      tr.appendChild(td);
    }
    frag.appendChild(tr);
  }
  body.appendChild(frag);

  const empty = document.getElementById('empty');
  empty.textContent = 'Nothing matches those filters.';
  empty.hidden = rows.length > 0;
  document.getElementById('count').textContent =
    rows.length.toLocaleString() + ' of ' + ROWS.length.toLocaleString() + ' ' + PAGE.noun;

  const el = document.getElementById('stats');
  el.textContent = '';
  for (const [k, v] of PAGE.stats(rows)) {
    const d = document.createElement('div');
    d.className = 'stat';
    const kk = document.createElement('div'); kk.className = 'stat-k'; kk.textContent = k;
    const vv = document.createElement('div'); vv.className = 'stat-v'; vv.textContent = v;
    d.append(kk, vv);
    el.appendChild(d);
  }
}

const sortby = document.getElementById('sortby');
const sortdir = document.getElementById('sortdir');

COLS.forEach(c => {
  const opt = document.createElement('option');
  opt.value = c.k;
  opt.textContent = c.t;
  sortby.appendChild(opt);
});

function setSort(key, dir) {
  sortKey = key;
  sortDir = dir;
  sortby.value = key;
  const numeric = COLS.find(c => c.k === key).num;
  sortdir.textContent = dir === -1
    ? (numeric ? '\u2193 Highest' : '\u2193 Z \u2192 A')
    : (numeric ? '\u2191 Lowest' : '\u2191 A \u2192 Z');
  render();
}

sortby.addEventListener('change', () => {
  // A fresh column starts the way that column is usually wanted: biggest
  // number first, but names from the top.
  setSort(sortby.value, COLS.find(c => c.k === sortby.value).num ? -1 : 1);
});
sortdir.addEventListener('click', () => setSort(sortKey, -sortDir));

const head = document.getElementById('head');
COLS.forEach(c => {
  const th = document.createElement('th');
  th.textContent = c.t;
  th.tabIndex = 0;
  th.title = 'Sort by ' + c.t;
  if (c.num) th.className = 'num';
  const arrow = document.createElement('span');
  arrow.className = 'arrow';
  th.appendChild(arrow);
  const activate = () => {
    if (sortKey === c.k) setSort(c.k, -sortDir);
    else setSort(c.k, c.num ? -1 : 1);
  };
  th.addEventListener('click', activate);
  th.addEventListener('keydown', e => {
    if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); activate(); }
  });
  head.appendChild(th);
});

for (const [id, ev] of PAGE.controls)
  document.getElementById(id).addEventListener(ev, render);

// Building tens of thousands of rows takes a moment, and doing it inline
// leaves the window blank until it finishes - which reads as a broken page.
// Let the shell paint first, placeholder and all, then fill the table.
requestAnimationFrame(() => setTimeout(() => setSort(sortKey, sortDir), 0));
</script>
)js";

std::string page_template(const char* title, const char* body, const char* page_js) {
    std::string page = kHead;
    page += "<title>";
    page += title;
    page += "</title>\n<style>\n";
    page += kReportCss;
    page += "</style>\n\n";
    page += body;
    page += kReportJsHead;
    page += page_js;
    page += kReportJs;
    return page;
}

```

- [ ] **Step 5: The path report.** In src/app/report.h, add these includes after `#include <optional>` and after `#include "store/record_store.h"` respectively:

```cpp
#include <unordered_map>
#include <utility>
```

```cpp
#include "core/squeeze_rating.h"
```

Change the start of `struct ReportRow`:

```cpp
// One table row. Field order is the JSON key order the page's script reads;
// keep it stable so old and new report files stay comparable.
struct ReportRow {
```

to:

```cpp
// One table row. Field order is the JSON key order the page's script reads,
// except `hyhash`, which the page never sees.
struct ReportRow {
```

Delete the line `    int64_t delta = 0;`. After `    int notes = 0;` add:

```cpp
    // The chart this row belongs to. Not written to the page: generate_report
    // counts the distinct charts on the page with it.
    std::string hyhash;
```

After the existing `tier_for` declaration (the one ending `double hit_window_ms = kDefaultHitWindowMs);`) add:

```cpp
// The same label, read from an already-built timing_tiers table, so a caller
// labeling many rows builds the table once.
std::pair<std::string, std::string> tier_for(const std::optional<double>& ms,
                                             const std::vector<TimingTier>& tiers);

// Every listed record for one chart mode, cap and lens, keyed by its chart
// hash in lower case. Both comparison pages join on this.
std::unordered_map<std::string, store::RecordListing> records_by_hash(
    store::RecordStore& store, const std::string& chartmode, const store::CapQuery& cap,
    const store::Lens& lens);
```

Change the `GeneratedReport` struct:

```cpp
struct GeneratedReport {
    std::string html;  // empty when the store held no reportable rows
    int64_t songs = 0;
    int64_t records = 0;
    int64_t rows = 0;
};
```

to:

```cpp
struct GeneratedReport {
    std::string html;  // empty when the store held no reportable rows
    int64_t songs = 0;    // distinct charts with rows on the page
    int64_t records = 0;  // records with rows on the page (one rank-1 row each)
    int64_t rows = 0;
};
```

Delete these lines at the end of report.h:

```cpp
// repr(float) / json.dumps float formatting (shortest round-trip). Exposed
// for tests.
std::string py_repr(double v);

```

In src/app/report.cpp, add `#include <unordered_set>` after `#include <filesystem>` and `#include "core/strutil.h"` after `#include "core/squeeze_rating.h"`. Then replace everything from the line `namespace {` (right after `using html::json_escape_into;`) down to and including the closing `}` of `py_repr` with:

```cpp
namespace {

// The path report's own pieces. The stylesheet and the script that sorts,
// filters and draws the table are shared with the other two report pages
// (html::page_template, docs/adr/0016).
const char* const kTitle = "Hydra Path Index";

const char* const kBody = R"page(<div class="wrap">
  <header>
    <h1>Hydra <span class="accent">Path Index</span></h1>
    <div class="sub">__SUBTITLE__</div>
  </header>

  <div class="stats" id="stats"></div>

  <div class="controls">
    <span class="sorter">
      <label for="sortby">Sort by</label>
      <select id="sortby"></select>
      <button id="sortdir" type="button" title="Switch between highest-first and lowest-first"></button>
    </span>
    <input type="search" id="q" placeholder="Search song, artist, charter, or path notation">
    <select id="tier">
      <option value="">All timing tiers</option>
    </select>
    <label class="toggle"><input type="checkbox" id="bestonly" checked> Best path only</label>
    <span class="count" id="count"></span>
  </div>

  <div class="tablewrap">
    <table>
      <thead><tr id="head"></tr></thead>
      <tbody id="body"></tbody>
    </table>
    <div class="empty" id="empty">Reading paths&hellip;</div>
  </div>

  <footer>__FOOTER__</footer>
</div>

)page";

// The payload is {hit_window, tiers, rows}. The tier dropdown and the
// "Past N ms" tile read the tier table, so they always match the bands the
// rows were labeled with.
const char* const kPageJs = R"page(const BEYOND = Math.max(...DATA.tiers.filter(t => t.cutoff !== null).map(t => t.cutoff));

// The tier dropdown mirrors the bands the rows were labeled with.
{
  const sel = document.getElementById('tier');
  for (const t of DATA.tiers) {
    const o = document.createElement('option');
    o.value = t.name;
    o.textContent = t.name === 'Beyond' ? 'Beyond ' + BEYOND + ' ms'
                  : t.name === 'None' ? 'No squeezes'
                  : t.name;
    sel.appendChild(o);
  }
}

const PAGE = {
  rows: DATA.rows,
  noun: 'paths',
  sortKey: 'score',
  sortDir: -1,
  cols: [
    {k:'song',    t:'Song',     num:false},
    {k:'artist',  t:'Artist',   num:false},
    {k:'charter', t:'Charter',  num:false},
    {k:'mode',    t:'Mode',     num:false},
    {k:'path',    t:'Path',     num:false},
    {k:'score',   t:'Score',    num:true},
    {k:'acts',    t:'Acts',     num:true},
    {k:'skip',    t:'Max skip', num:true},
    {k:'ms',      t:'Hardest ms', num:true},
    {k:'tier',    t:'Timing',   num:false},
    {k:'efill',   t:'Cal fill', num:true},
    {k:'mult',    t:'Avg mult', num:true},
    {k:'sqin',    t:'SqIn',     num:true},
    {k:'sqout',   t:'SqOut',    num:true},
    {k:'notes',   t:'Notes',    num:true},
  ],
  controls: [['q', 'input'], ['tier', 'change'], ['bestonly', 'change']],
  filter(q) {
    const tier = document.getElementById('tier').value;
    const bestOnly = document.getElementById('bestonly').checked;
    return r => {
      if (bestOnly && r.rank !== 1) return false;
      if (tier && r.tier !== tier) return false;
      if (!q) return true;
      return (r.song + ' ' + r.artist + ' ' + r.charter + ' ' + r.path).toLowerCase().includes(q);
    };
  },
  rowClass: r => r.rank === 1 ? 'best' : '',
  cells: r => [
    ['song trunc', r.song],
    ['dim trunc artist', r.artist],
    ['dim trunc charter', r.charter],
    ['dim trunc mode', r.mode],
    ['path mono trunc', r.path],
    ['num', fmt(r.score)],
    ['num', r.acts],
    ['num', r.skip],
    ['num', fmtMs(r.ms)],
    ['chip ' + r.tok, r.tier, 'chip'],
    ['num', fmtMs(r.efill)],
    ['num', r.mult.toFixed(3)],
    ['num', r.sqin],
    ['num', r.sqout],
    ['num', fmt(r.notes)],
  ],
  stats(rows) {
    const best = rows.filter(r => r.rank === 1);
    const withMs = rows.filter(r => r.ms !== null && r.ms !== undefined);
    const tightest = withMs.length ? Math.max(...withMs.map(r => r.ms)) : null;
    const maxSkip = rows.length ? Math.max(...rows.map(r => r.skip)) : 0;
    const beyond = rows.filter(r => r.ms !== null && r.ms >= BEYOND).length;
    return [
      ['Charts', new Set(best.map(r => r.song + r.artist)).size.toLocaleString()],
      ['Paths shown', rows.length.toLocaleString()],
      ['Tightest squeeze', tightest === null ? DASH : tightest.toFixed(1) + ' ms'],
      ['Past ' + BEYOND + ' ms', beyond.toLocaleString()],
      ['Highest skip', maxSkip],
    ];
  },
};
)page";

// The page shell, built once on first use.
const std::string& page_template() {
    static const std::string page = html::page_template(kTitle, kBody, kPageJs);
    return page;
}

// repr(float) / json.dumps float formatting for the page payload.
std::string py_repr(double v) {
    // std::to_chars with no precision produces the shortest string that
    // round-trips -- the same contract as CPython's float repr. The one
    // cosmetic difference: Python prints integral floats as "140.0" where
    // to_chars gives "140".
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    std::string s(buf, res.ptr);
    if (s.find_first_of(".eE") == std::string::npos &&
        s.find_first_of("0123456789") != std::string::npos)
        s += ".0";
    return s;
}

}  // namespace
```

Replace the `tier_for` definition:

```cpp
std::pair<std::string, std::string> tier_for(const std::optional<double>& ms,
                                             double hit_window_ms) {
    // The ladder itself lives in core/squeeze_rating.h (timing_tiers) so
    // these labels and the page's embedded tier table cannot drift apart.
    // The two open bands are the table's last two entries: "Beyond", then
    // the "None" (no squeeze) entry.
    const std::vector<TimingTier> tiers = timing_tiers(hit_window_ms);
    const TimingTier& none = tiers.back();
```

with:

```cpp
std::pair<std::string, std::string> tier_for(const std::optional<double>& ms,
                                             double hit_window_ms) {
    // The ladder itself lives in core/squeeze_rating.h (timing_tiers) so
    // these labels and the page's embedded tier table cannot drift apart.
    return tier_for(ms, timing_tiers(hit_window_ms));
}

std::pair<std::string, std::string> tier_for(const std::optional<double>& ms,
                                             const std::vector<TimingTier>& tiers) {
    // The two open bands are the table's last two entries: "Beyond", then
    // the "None" (no squeeze) entry.
    const TimingTier& none = tiers.back();
```

(The rest of that function body stays as it is.) After the end of `tier_for`, add:

```cpp
std::unordered_map<std::string, store::RecordListing> records_by_hash(
    store::RecordStore& store, const std::string& chartmode, const store::CapQuery& cap,
    const store::Lens& lens) {
    std::unordered_map<std::string, store::RecordListing> by_hash;
    for (store::RecordListing& r : store.list_records(chartmode, cap, lens,
                                                       store::SortColumn::Score,
                                                       /*descending=*/true))
        by_hash.emplace(lower_hex(r.hyhash), std::move(r));
    return by_hash;
}
```

In `collect_rows`, change:

```cpp
    std::vector<ReportRow> rows;

    store.for_each_blob(std::nullopt, cap, lens,
```

to:

```cpp
    std::vector<ReportRow> rows;
    // Built once for the whole report, not once per row.
    const std::vector<TimingTier> tiers = timing_tiers(hit_window_ms);

    store.for_each_blob(std::nullopt, cap, lens,
```

Delete the line `        int64_t best_score = paths.empty() ? 0 : paths[0]->totalscore();`. Change `            auto [label, token] = tier_for(s.hardest_ms, hit_window_ms);` to `            auto [label, token] = tier_for(s.hardest_ms, tiers);`. Delete `            row.delta = *s.score - best_score;`. After `            row.notes = *s.notecount;` add `            row.hyhash = meta.hyhash;`.

In `build_html`, delete the line `        data += ",\"delta\":" + std::to_string(r.delta);`.

In `generate_report`, change:

```cpp
    GeneratedReport out;
    auto [songs, records] = store.counts();
    out.songs = songs;
    out.records = records;

    const double w = static_cast<double>(options.hit_window_ms);
```

to:

```cpp
    GeneratedReport out;
    const double w = static_cast<double>(options.hit_window_ms);
```

and change:

```cpp
    out.rows = static_cast<int64_t>(rows.size());
    if (rows.empty()) return out;
```

to:

```cpp
    out.rows = static_cast<int64_t>(rows.size());
    if (rows.empty()) return out;
    // The subtitle counts what the page lists: every record on it has exactly
    // one rank-1 row, and its songs are the distinct charts among the rows.
    std::unordered_set<std::string> songs;
    for (const ReportRow& r : rows) {
        if (r.rank == 1) ++out.records;
        songs.insert(r.hyhash);
    }
    out.songs = static_cast<int64_t>(songs.size());
```

and change the subtitle line `    std::string subtitle = group_thousands(records) + " records across " +` / `                           group_thousands(songs) + " songs — " + shown + " — " + cap_label;` to use `out.records` and `out.songs`:

```cpp
    std::string subtitle = group_thousands(out.records) + " records across " +
                           group_thousands(out.songs) + " songs — " + shown + " — " + cap_label;
```

- [ ] **Step 6: The dmleaderboards page.** In src/app/dm_report.h, change the top comment's first sentences:

```cpp
// Comparison report: one dmleaderboards user's actual scores against Hydra's
// computed optimal for the same charts. A parallel of app/report.h (kept
// separate so report.h's byte-exact parity test is never disturbed): it joins
// the fetched scores to stored records by chart-file MD5 (leaderboard
// `identifier` == Hydra `hyhash`) and emits a self-contained sortable HTML page.
```

to:

```cpp
// Comparison report: one dmleaderboards user's actual scores against Hydra's
// computed optimal for the same charts. It joins the fetched scores to stored
// records by chart-file MD5 (leaderboard `identifier` == Hydra `hyhash`) and
// emits a self-contained sortable HTML page on the shared report shell.
```

In src/app/dm_report.cpp, delete `#include "core/strutil.h"  // lower_hex`. Replace everything from `// The per-page pieces of the comparison page;` down to and including the closing `}` of `page_template()` with:

```cpp
// The comparison page's own pieces. The stylesheet and the script that sorts,
// filters and draws the table are shared with the other report pages
// (html::page_template, docs/adr/0016). __SUBTITLE__/__FOOTER__/__DATA__ are
// filled by build_dm_html.
const char* const kTitle = "Hydra vs dmleaderboards";

const char* const kBody = R"page(<div class="wrap dm">
  <header>
    <h1>Hydra <span class="accent">vs dmleaderboards</span></h1>
    <div class="sub">__SUBTITLE__</div>
  </header>

  <div class="stats" id="stats"></div>

  <div class="controls">
    <span class="sorter">
      <label for="sortby">Sort by</label>
      <select id="sortby"></select>
      <button id="sortdir" type="button" title="Switch between highest-first and lowest-first"></button>
    </span>
    <input type="search" id="q" placeholder="Search song, artist, or charter">
    <select id="status">
      <option value="">All charts</option>
      <option value="matched">Matched</option>
      <option value="above optimal">Above optimal</option>
      <option value="unmatched">Unmatched (not in library)</option>
    </select>
    <span class="count" id="count"></span>
  </div>

  <div class="tablewrap">
    <table>
      <thead><tr id="head"></tr></thead>
      <tbody id="body"></tbody>
    </table>
    <div class="empty" id="empty">Joining scores&hellip;</div>
  </div>

  <footer>__FOOTER__</footer>
</div>

)page";

const char* const kPageJs = R"page(const STATUS_CLASS = {'matched':'s-matched', 'above optimal':'s-above', 'unmatched':'s-unmatched'};

const PAGE = {
  rows: DATA,
  noun: 'scores',
  sortKey: 'delta',
  sortDir: -1,
  cols: [
    {k:'song',    t:'Song',      num:false},
    {k:'artist',  t:'Artist',    num:false},
    {k:'charter', t:'Charter',   num:false},
    {k:'actual',  t:'Actual',    num:true},
    {k:'optimal', t:'Hydra opt', num:true},
    {k:'delta',   t:'Points left', num:true},
    {k:'pct',     t:'% of opt',  num:true},
    {k:'fc',      t:'FC',        num:true},
    {k:'percent', t:'Percent',   num:true},
    {k:'speed',   t:'Speed',     num:true},
    {k:'rank',    t:'Rank',      num:true},
    {k:'posted',  t:'Posted',    num:false},
    {k:'status',  t:'Status',    num:false},
  ],
  controls: [['q', 'input'], ['status', 'change']],
  filter(q) {
    const status = document.getElementById('status').value;
    return r => {
      if (status && r.status !== status) return false;
      if (!q) return true;
      return (r.song + ' ' + r.artist + ' ' + r.charter).toLowerCase().includes(q);
    };
  },
  cells(r) {
    const noDelta = r.delta === null || r.delta === undefined;
    const deltaCls = noDelta ? 'num dim' : (r.delta < 0 ? 'num neg' : 'num');
    const deltaTxt = noDelta ? DASH
                   : (r.delta < 0 ? '+' + (-r.delta).toLocaleString() + ' over' : fmt(r.delta));
    return [
      ['song trunc', r.song],
      ['dim trunc artist', r.artist],
      ['dim trunc charter', r.charter],
      ['num', fmt(r.actual)],
      ['num', fmt(r.optimal)],
      [deltaCls, deltaTxt],
      ['num', r.pct === null || r.pct === undefined ? DASH : r.pct.toFixed(2) + '%'],
      ['num', r.fc ? '\u2713' : DASH],
      ['num', r.percent + '%'],
      ['num', r.speed + '%'],
      ['num', r.rank === null || r.rank === undefined ? DASH : '#' + r.rank],
      ['dim', r.posted ? r.posted.slice(0, 10) : DASH],
      ['chip ' + (STATUS_CLASS[r.status] || 's-unmatched'), r.status, 'chip'],
    ];
  },
  stats(rows) {
    const matched = rows.filter(r => r.status === 'matched');
    const above = rows.filter(r => r.status === 'above optimal');
    const unmatched = rows.filter(r => r.status === 'unmatched');
    const withPct = rows.filter(r => r.pct !== null && r.pct !== undefined);
    const avgPct = withPct.length
      ? (withPct.reduce((a, r) => a + r.pct, 0) / withPct.length).toFixed(2) + '%' : DASH;
    const left = matched.reduce((a, r) => a + (r.delta > 0 ? r.delta : 0), 0);
    return [
      ['Scores', rows.length.toLocaleString()],
      ['Matched', matched.length.toLocaleString()],
      ['Above optimal', above.length.toLocaleString()],
      ['Unmatched', unmatched.length.toLocaleString()],
      ['Avg % of optimal', avgPct],
      ['Points left on table', left.toLocaleString()],
    ];
  },
};
)page";

// The page shell, built once on first use.
const std::string& page_template() {
    static const std::string page = html::page_template(kTitle, kBody, kPageJs);
    return page;
}
```

In `collect_dm_rows`, change:

```cpp
    std::unordered_map<std::string, store::RecordListing> by_hash;
    for (store::RecordListing& r :
         store.list_records(chartmode, store::CapQuery::at(kCloneHeroSpCap), lens,
                            store::SortColumn::Score, /*descending=*/true)) {
        by_hash.emplace(lower_hex(r.hyhash), std::move(r));
    }
```

to:

```cpp
    const std::unordered_map<std::string, store::RecordListing> by_hash =
        report::records_by_hash(store, chartmode, store::CapQuery::at(kCloneHeroSpCap), lens);
```

- [ ] **Step 7: The fill-spawn page.** In src/app/fill_report.h, change:

```cpp
// Comparison report: the same charts scored under Clone Hero 1.0's fill-spawn
// rule against Clone Hero 1.1's. A parallel of app/dm_report.h, kept separate
// so app/report.h's byte-exact parity test is never disturbed.
```

to:

```cpp
// Comparison report: the same charts scored under Clone Hero 1.0's fill-spawn
// rule against Clone Hero 1.1's, on the shared report shell like
// app/dm_report.h.
```

In src/app/fill_report.cpp, delete `#include "core/strutil.h"  // lower_hex`. Replace everything from `// The per-page pieces of this comparison page;` down to and including the closing `}` of `index_by_hash` (just before `}  // namespace`) with:

```cpp
// This comparison page's own pieces. The stylesheet and the script that sorts,
// filters and draws the table are shared with the other report pages
// (html::page_template, docs/adr/0016). __SUBTITLE__/__FOOTER__/__DATA__ are
// filled by build_fill_html.
//
// Every literal here is ASCII: this file compiles into hydra_core, which is
// not built with /utf-8. Glyphs the page needs go in as HTML entities (markup)
// or \uXXXX escapes (JavaScript).
const char* const kTitle = "Fill spawn comparison &mdash; CH 1.0 vs CH 1.1";

const char* const kBody = R"page(<div class="wrap fill">
  <header>
    <h1>Fill spawn <span class="accent">CH 1.0 vs CH 1.1</span></h1>
    <div class="sub">__SUBTITLE__</div>
  </header>

  <div class="stats" id="stats"></div>

  <div class="controls">
    <span class="sorter">
      <label for="sortby">Sort by</label>
      <select id="sortby"></select>
      <button id="sortdir" type="button" title="Switch between highest-first and lowest-first"></button>
    </span>
    <input type="search" id="q" placeholder="Search song, artist, or charter">
    <select id="status">
      <option value="">All charts</option>
      <option value="1.1 higher">1.1 higher</option>
      <option value="1.0 higher">1.0 higher</option>
      <option value="same">Same score</option>
      <option value="only 1.0">Only in 1.0 db</option>
      <option value="only 1.1">Only in 1.1 db</option>
    </select>
    <span class="count" id="count"></span>
  </div>

  <div class="tablewrap">
    <table>
      <thead><tr id="head"></tr></thead>
      <tbody id="body"></tbody>
    </table>
    <div class="empty" id="empty">Joining databases&hellip;</div>
  </div>

  <footer>__FOOTER__</footer>
</div>

)page";

const char* const kPageJs = R"page(const STATUS_CLASS = {'1.1 higher':'s-newhigh', '1.0 higher':'s-oldhigh',
                      'same':'s-same', 'only 1.0':'s-only', 'only 1.1':'s-only'};

const PAGE = {
  rows: DATA,
  noun: 'charts',
  sortKey: 'delta',
  sortDir: -1,
  cols: [
    {k:'song',    t:'Song',        num:false},
    {k:'artist',  t:'Artist',      num:false},
    {k:'charter', t:'Charter',     num:false},
    {k:'s10',     t:'CH 1.0',      num:true},
    {k:'s11',     t:'CH 1.1',      num:true},
    {k:'delta',   t:'Delta',       num:true},
    {k:'p10',     t:'CH 1.0 path', num:false},
    {k:'p11',     t:'CH 1.1 path', num:false},
    {k:'acts',    t:'Acts',        num:true},
    {k:'notes',   t:'Notes',       num:true},
    {k:'status',  t:'Status',      num:false},
  ],
  controls: [['q', 'input'], ['status', 'change']],
  filter(q) {
    const status = document.getElementById('status').value;
    return r => {
      if (status && r.status !== status) return false;
      if (!q) return true;
      return (r.song + ' ' + r.artist + ' ' + r.charter).toLowerCase().includes(q);
    };
  },
  cells(r) {
    const hasDelta = r.delta !== null && r.delta !== undefined;
    const deltaCls = !hasDelta ? 'num dim' : (r.delta > 0 ? 'num pos'
                   : (r.delta < 0 ? 'num neg' : 'num dim'));
    const deltaTxt = !hasDelta ? DASH
                   : (r.delta > 0 ? '+' + r.delta.toLocaleString() : fmt(r.delta));
    return [
      ['song trunc', r.song],
      ['dim trunc artist', r.artist],
      ['dim trunc charter', r.charter],
      ['num', fmt(r.s10)],
      ['num', fmt(r.s11)],
      [deltaCls, deltaTxt],
      ['path trunc', r.p10 || DASH],
      ['path trunc', r.p11 || DASH],
      ['num', r.acts_txt],
      ['num', fmt(r.notes)],
      ['chip ' + (STATUS_CLASS[r.status] || 's-only'), r.status, 'chip'],
    ];
  },
  stats(rows) {
    const n = s => rows.filter(r => r.status === s).length;
    const gains = rows.filter(r => r.delta > 0).reduce((a, r) => a + r.delta, 0);
    const losses = rows.filter(r => r.delta < 0).reduce((a, r) => a - r.delta, 0);
    return [
      ['Charts', rows.length.toLocaleString()],
      ['1.1 higher', n('1.1 higher').toLocaleString()],
      ['1.0 higher', n('1.0 higher').toLocaleString()],
      ['Same', n('same').toLocaleString()],
      ['Only one side', (n('only 1.0') + n('only 1.1')).toLocaleString()],
      ['Points gained in 1.1', gains.toLocaleString()],
      ['Points lost in 1.1', losses.toLocaleString()],
    ];
  },
};
)page";

// The page shell, built once on first use.
const std::string& page_template() {
    static const std::string page = html::page_template(kTitle, kBody, kPageJs);
    return page;
}
```

In `collect_fill_rows`, change:

```cpp
    std::unordered_map<std::string, store::RecordListing> old_by_hash =
        index_by_hash(old_store, chartmode, cap, lens);
    std::unordered_map<std::string, store::RecordListing> new_by_hash =
        index_by_hash(new_store, chartmode, cap, lens);
```

to:

```cpp
    const std::unordered_map<std::string, store::RecordListing> old_by_hash =
        report::records_by_hash(old_store, chartmode, cap, lens);
    const std::unordered_map<std::string, store::RecordListing> new_by_hash =
        report::records_by_hash(new_store, chartmode, cap, lens);
```

- [ ] **Step 8: Delete `count_chart_chords`.** In src/app/analysis.h, delete:

```cpp
// Chord counts by code, for the "how big is this chart" display. Dispatches
// via load_songpath, so it takes any supported chart type
// (.mid/.chart/.sng/.srb).
std::map<std::string, int> count_chart_chords(const std::string& filepath);

```

In src/app/analysis.cpp, delete:

```cpp
std::map<std::string, int> count_chart_chords(const std::string& filepath) {
    Song song = load_songpath(filepath, true, true);
    std::map<std::string, int> counts;
    for (const SongTimestamp& ts : song.sequence) ++counts[ts.chord.code()];
    return counts;
}

```

- [ ] **Step 9: Retire ADR 0002 and write ADR 0016.** In docs/adr/0002-sortable-page-fragments-canon-from-path-report.md, add this line right under the title, followed by a blank line:

```markdown
_Superseded by ADR 0016 (2026-09): `hydra_report.py` is gone, nothing pins the page bytes, and the three report pages now share one stylesheet and one script._
```

Create docs/adr/0016-report-pages-share-one-stylesheet-and-script.md:

```markdown
# Report pages share one stylesheet and one script

ADR 0002 pinned the path report's page byte for byte to `hydra_report.py`'s
PAGE string. That script was deleted in the C++ cutover (95f22b0), and no test
compared page bytes after that. The pin was still the only stated reason for
three near-identical page scripts, CSS rules that matched nothing on some
pages, and two copies of the "records by chart hash" index.

## The decision

The path report, the dmleaderboards comparison and the fill-spawn comparison
are one stylesheet and one script around each page's own title, body markup
and a small `PAGE` object. `PAGE` names the columns, the first sort, which rows
the filters keep, how a row is drawn and which stats sit above the table.
Everything else is shared: sorting, the header arrows, drawing rows, the count
line, the stats tiles and the deferred first render.

The stylesheet (`html::kReportCss`) and the script (`html::kReportJs`) live in
`app/html_page.cpp`. They stay ASCII because hydra_core is not built with
/utf-8; a glyph is an HTML entity or a `\uXXXX` escape. The comparison pages
size a few columns differently, scoped by a class on `.wrap`. The comparison
pages look records up through one `report::records_by_hash`.

Page bytes are not pinned. A page change is checked in a real browser instead:
`hydra_tests --no-skip -tc="report pages: write samples*"` writes one sample
page of each kind, and rendering those in headless Edge before and after the
change shows whether a reader would see any difference.

## What this costs

A new column or filter is a `PAGE` edit, not a script copy. A behaviour that
does not fit `PAGE` has to be added to the shared script, where every page gets
it.

This supersedes ADR 0002.
```

- [ ] **Step 10: Run it and watch it pass.** Run `.\build_cpp.ps1; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, with the new cases included.

- [ ] **Step 11: Prove the pages look and behave the same.** Rebuild the five pages with the new code, from the same databases, and compare:

```powershell
$out = "$env:TEMP\hydra_t5"
$env:HYDRA_PAGE_SAMPLES = "$out\after"
.\build-cpp\Release\hydra_tests.exe --no-skip -tc="report pages: write samples*"
.\build-cpp\Release\hydra_report.exe --db "$out\ch11.db" --out "$out\after\real_paths.html" --no-open
.\build-cpp\Release\hydra_fillcompare.exe --old "$out\ch10.db" --new "$out\ch11.db" --out "$out\after\real_fill.html" --no-open
foreach ($p in "paths", "dm", "fill", "real_paths", "real_fill") {
    C:\Python314\python.exe "$out\page_check.py" render "$out\after\$p.html" "$out\after\$p.json"
}
foreach ($p in "dm", "fill", "real_fill") {
    C:\Python314\python.exe "$out\page_check.py" compare "$out\before\$p.json" "$out\after\$p.json"
}
foreach ($p in "paths", "real_paths") {
    C:\Python314\python.exe "$out\page_check.py" compare "$out\before\$p.json" "$out\after\$p.json" --drop-column Mode
}
```

Expected: `SAME` three times, then for each path page one `dropped column Mode: values [...]` line followed by `SAME`. The sample page's values are `['Expert Drums, 1x Bass', 'Expert Pro Drums, 2x Bass', 'Hard Drums, 1x Bass']`. The real page's values are the chart mode the batch ran under. If any compare prints differences, the change is not done: fix the page code, never the probe.

- [ ] **Step 12: Check the report buttons in the GUI.** Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test settings-and-reports`. Expected: the test passes.

- [ ] **Step 13: Commit.**

```bash
git add src/app/html_page.h src/app/html_page.cpp src/app/report.h src/app/report.cpp src/app/dm_report.h src/app/dm_report.cpp src/app/fill_report.h src/app/fill_report.cpp src/app/analysis.h src/app/analysis.cpp docs/adr/0002-sortable-page-fragments-canon-from-path-report.md docs/adr/0016-report-pages-share-one-stylesheet-and-script.md tests/test_report.cpp tests/test_fill_report.cpp tests/test_analysis.cpp
git commit -m "Share one stylesheet and script across the report pages; add the Mode column

The path report shows each row's chart mode, and its subtitle counts the
records on the page. ADR 0002 is retired (ADR 0016). ReportRow::delta,
count_chart_chords and py_repr's export are gone, the tier table is built
once per report, and both comparison pages use report::records_by_hash.
Headless-Edge recordings of all five pages match before and after, apart
from the new column.

Task: Task 5: Report pages share one stylesheet and one script, and the path report shows each row's mode
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/app/html_page.h","src/app/html_page.cpp","src/app/report.h","src/app/report.cpp","src/app/dm_report.h","src/app/dm_report.cpp","src/app/fill_report.h","src/app/fill_report.cpp","src/app/analysis.h","src/app/analysis.cpp","docs/adr/0002-sortable-page-fragments-canon-from-path-report.md","docs/adr/0016-report-pages-share-one-stylesheet-and-script.md","tests/test_report.cpp","tests/test_fill_report.cpp","tests/test_analysis.cpp"],"verifyCommand":".\\build_cpp.ps1; .\\build-cpp\\Release\\hydra_tests.exe","acceptanceCriteria":["`hydra_tests.exe -tc=\"the three report pages share one stylesheet and one script\"` passes.","`hydra_tests.exe -tc=\"path report shows each row's chart mode*\"` passes, including its check that the page data has no `\"delta\":` key.","`hydra_tests.exe -tc=\"report lists only the wanted cap and names it\"` passes with records == 1, songs == 1 and the subtitle text `1 records across 1 songs`.","`hydra_tests.exe -tc=\"tier_for over a built table*\"` and `-tc=\"records_by_hash*\"` pass.","The full hydra_tests run ends with `Status: SUCCESS!`.","page_check.py compare prints SAME for dm, fill and real_fill, and the Mode values followed by SAME for paths and real_paths.","`hydra_uitest.exe --test settings-and-reports` passes.","`git grep -nE \"kSortable|byte-exact|count_chart_chords\" -- src tests` prints nothing, and `git grep -n \"py_repr\" -- src/app/report.h tests` prints nothing.","`git grep -n \"timing_tiers(\" -- src/app/report.cpp` prints exactly three lines."],"modelTier":"standard"}
```

---

### Task 6: The details window closes cleanly and the Preview survives a PC with no audio device

Four things go wrong around the Song Details window today. On a PC with no audio output, the Preview says "Preview failed" and draws nothing. The controller catches the device error and stores it in `error_`, the same field a real failure uses, and the panel treats any error as fatal. Closing the window while a Preview or Dynamics load is running freezes the app, because closing joins the load's thread on the UI thread and neither job ever looks at its cancel flag. The teardown that should run on close lives in three copies (two branches of `render_details_modal` and the same reset in `AppState::select`), and the "Rescan library" button skips all three: it sets `show_details = false`, the modal then returns before any teardown, and the Preview keeps playing. The Dynamics job is also collected only while its tab shows, so a count that finishes on another tab is thrown away at close and parsed again next time. Finally, the window asks the disk whether the chart file exists on every frame.

This task fixes all four. The device error becomes a separate audio warning. The panel shows one muted-audio line and keeps drawing; that wording and behaviour are the orchestrator's call. Both load jobs stop at their cancel flag between steps. One `AppState::close_details()` runs on the window's open-to-closed edge, whatever closed it, and replaces the three copies. A finished Dynamics count is stored every frame, whichever tab shows. The chart-file check runs when the window opens and then every two seconds.

It also fixes non-ASCII install folders. The Preview's art (`render/file_util.cpp`), the icons (`icons.cpp`) and the settings INI (`app/config.cpp`) open UTF-8 paths through narrow ANSI calls. So a folder like `C:\Users\Zoë\…` breaks all three. The audit named the first two; the settings INI is the same bug, so it is added here. `render/file_util` is deleted, and everything reads through `core/winstr`.

Last, it deletes the dead code the audit found in this area. That is PreviewController's `open_key()`, `scrubbing()` and `volume()` getters, `PreviewRenderer::Impl::have_state`, `RenderParams` (its one field, `speed`, is always 1), the ImGui viewports block in `main.cpp`, `ConfigDpiScaleViewports`, and `kDisabledTextColor`.

Audit check: the three teardown copies are real. They are the `!visible` and `!open` branches of `render_details_modal` plus the Preview and Dynamics reset in `AppState::select`. The Dynamics job has one heavy step, the parse, which is a single call that cannot stop midway. So its cancel checks sit before and after the parse, and a close during the parse still waits for that one parse (which reads no audio). The Preview job gets checks between reading, each stem, and mixing, so a close waits for one stem's decode at most.

What the user sees: on a PC with no audio device, the Preview draws the highway under one warning line, "No audio device found; the preview is muted.", instead of "Preview failed". Closing the window during a load no longer freezes. Nothing else changes.

**Depends on:** nothing.

**Expected overlaps:** T7 edits the same `src/ui/app_state.h` (it adds `LibraryViewState`, `edit_settings`, `flush_settings`, `report_file_shown` and a lookup cache next to this task's `close_details`, `reap_dynamics` and `selected_file_ok`) and `src/ui/app_state.cpp` (T7 rewrites `commit_settings` and `refresh_page`; this task rewrites `select` and `update_dynamics`). In `src/ui/details_view.cpp`, both edit `render_controls`: T7 changes the three number boxes' calls, this task changes the `file_ok` line and the Rescan comment. In `src/ui/app_shell.cpp`, T7 adds one line to `run_frame` and this task deletes one line in `setup_imgui`. Both append cases to `tests/test_app_state.cpp` and tests plus register entries to `tests/ui/uitest_tests.cpp`; T18 appends to that file too. T8 rewrites `audio::decode_and_mix`; this task relies on its progress callback being called outside the per-stem `try`, and T8 must keep that. T14 edits `CMakeLists.txt` (it drops icons.cpp's `/W1` line); this task removes `src/render/file_util.cpp` from `hydra_render` and adds two test files to `hydra_tests`. T10 may edit `Settings` in `src/app/config.cpp`; this task changes only the two stream opens in `load_file` and `save_file`. T11 later changes `PreviewController::open`'s signature and so edits this task's new test file.

**Goal:** closing the details window never freezes or leaks, the Preview draws without an audio device, and every GUI file open works from a non-ASCII folder.

**Files:**
- Create: `tests/test_preview_controller.cpp`, `tests/test_utf8_paths.cpp`
- Modify: `src/ui/job_base.h`, `src/ui/preview_load_job.cpp`, `src/ui/dynamics_load_job.h`, `src/ui/dynamics_load_job.cpp`, `src/ui/preview_controller.h`, `src/ui/preview_controller.cpp`, `src/ui/app_state.h`, `src/ui/app_state.cpp`, `src/ui/details_view.cpp`, `src/ui/app_shell.cpp`, `src/ui/main.cpp`, `src/ui/theme.h`, `src/ui/icons.cpp`, `src/render/preview_renderer.h`, `src/render/preview_renderer.cpp`, `src/core/winstr.h`, `src/core/winstr.cpp`, `src/app/config.cpp`, `CMakeLists.txt`
- Delete: `src/render/file_util.h`, `src/render/file_util.cpp`
- Test: `tests/test_app_state.cpp`, `tests/test_preview_renderer.cpp`, `tests/test_preview_golden.cpp`, `tests/test_preview_config.cpp`, `tests/test_obj_loader.cpp`, `tests/ui/uitest_tests.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="a cancelled*"` passes both cases: a load cancelled before it starts finishes with `ok() == false` and `error() == "cancelled"`, and the Preview load never leaves its Reading step.
- [ ] `hydra_tests.exe -tc="with no audio device*"` passes: the load finishes with no error, `audio_warning()` holds the device's message, and the transport plays.
- [ ] `hydra_tests.exe -tc="close_details*"` and `-tc="the chart-file check*"` pass.
- [ ] `hydra_tests.exe -tc="*non-ASCII*"` passes all three cases (the text reader, the settings INI, the Preview renderer).
- [ ] `hydra_uitest.exe --test details-close-teardown` passes, and `hydra_uitest.exe --all` passes.
- [ ] `Get-ChildItem src -Recurse -Include *.cpp,*.h | Select-String -Pattern 'file_util|have_state|RenderParams|ConfigDpiScaleViewports|ViewportsEnable|kDisabledTextColor|open_key\(\)|bool scrubbing\(\)|int volume\(\)'` prints nothing.
- [ ] `Select-String -Path src\ui\icons.cpp,src\render\*.cpp,src\app\config.cpp -Pattern 'std::fopen|std::ifstream f\(path|std::ofstream f\(path'` prints nothing.
- [ ] The score-neutral batch comparison prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe; .\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all` → `Status: SUCCESS!`, then `[PASS]` on every GUI test.

**Steps:**

- [ ] **Step 1: Write the failing tests.**

Create `tests/test_preview_controller.cpp`:

```cpp
// Tests for the details window's two background loads and the Preview
// controller's no-audio fallback. The loads are real threads on real corpus
// charts; nothing here opens a window or a real audio device.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "audio/device.h"
#include "core/winstr.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "store/record_store.h"
#include "ui/dynamics_load_job.h"
#include "ui/preview_controller.h"
#include "ui/preview_load_job.h"

#ifndef HYDRA_TESTDATA_DIR
#error "HYDRA_TESTDATA_DIR must be defined (see CMakeLists.txt)"
#endif

using hydra::Difficulty;
using hydra::store::ChartLibraryEntry;
using hydra::ui::DynamicsLoadJob;
using hydra::ui::PreviewController;
using hydra::ui::PreviewLoadJob;

namespace {

// Wait up to 60 s for a job's worker to finish.
template <class Job>
void wait_finished(const Job& job) {
    for (int i = 0; i < 1200 && !job.finished(); ++i) Sleep(50);
    REQUIRE(job.finished());
}

ChartLibraryEntry entry_for(const std::string& notespath) {
    ChartLibraryEntry e;
    e.md5 = "prevctl";
    e.title = "Preview controller test";
    e.notespath = notespath;
    return e;
}

void copy_file_utf8(const std::string& from, const std::string& to) {
    std::vector<uint8_t> bytes = hydra::read_file_bytes(from);
    std::FILE* f = hydra::fopen_utf8(to, L"wb");
    REQUIRE(f != nullptr);
    if (!bytes.empty()) std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

// A chart folder that has audio: a corpus .chart plus the test sine as
// song.ogg. The GUI test library has no audio at all, so the no-device path
// can only be reached here.
std::string chart_with_audio() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"hydra_prevctl_" +
                       std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::string d = hydra::wide_to_utf8(dir);
    copy_file_utf8(corpus::first_chart_with_suffix(".chart"), d + "\\notes.chart");
    copy_file_utf8(std::string(HYDRA_TESTDATA_DIR) + "/audio/sine220.ogg", d + "\\song.ogg");
    return d + "\\notes.chart";
}

}  // namespace

// Closing the details window joins the load's thread on the UI thread. A
// job that ignored its cancel flag ran its whole parse, decode and mix first.
TEST_CASE("a cancelled Preview load stops before decoding") {
    PreviewLoadJob job(entry_for(corpus::first_chart_with_suffix(".chart")), true, true,
                       Difficulty::Expert, std::nullopt, 4);
    job.cancel();  // the window closed before the worker got going
    job.start();
    wait_finished(job);
    CHECK_FALSE(job.ok());
    CHECK(job.error() == "cancelled");
    CHECK(job.progress().step == PreviewLoadJob::Step::Reading);
}

TEST_CASE("a cancelled Dynamics load stops before counting") {
    DynamicsLoadJob job(entry_for(corpus::first_chart_with_suffix(".chart")), true,
                        Difficulty::Expert);
    job.cancel();
    job.start();
    wait_finished(job);
    CHECK_FALSE(job.ok());
    CHECK(job.error() == "cancelled");
}

// No output device is a warning, not a failure: the chart loads, the clock
// plays, and only the sound is missing.
TEST_CASE("with no audio device the Preview still loads, muted, with a warning") {
    PreviewController pc(nullptr, nullptr);
    pc.set_audio_device_factory(
        [](int, int, PreviewController::AudioSource)
            -> std::unique_ptr<hydra::audio::PreviewAudioDevice> {
            throw std::runtime_error("PreviewAudioDevice: ma_device_init failed");
        });
    pc.open(entry_for(chart_with_audio()), true, true, Difficulty::Expert, nullptr, 4);
    for (int i = 0; i < 1200 && pc.loading(); ++i) {
        pc.poll();
        Sleep(50);
    }
    REQUIRE_FALSE(pc.loading());

    CHECK(pc.has_audio());        // the stem decoded; only the device failed
    CHECK_FALSE(pc.has_error());  // so no "Preview failed"
    CHECK(pc.audio_warning() == "PreviewAudioDevice: ma_device_init failed");
    CHECK(pc.length_ms() > 0.0);  // the scene is there to draw
    pc.play();
    CHECK(pc.playing());          // the clock runs without a device

    pc.close();
    CHECK(pc.audio_warning().empty());
}
```

Create `tests/test_utf8_paths.cpp`:

```cpp
// Non-ASCII install folders. Hydra runs from wherever the user put it, and a
// folder like C:\Users\Zoë\... is a UTF-8 path in our std::strings. The narrow
// CRT and std::fstream calls read such a path through the ANSI code page and
// miss the file, so every reader goes through core/winstr's wide calls.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "app/config.h"
#include "core/winstr.h"
#include "render/preview_renderer.h"
#include "warp_util.h"

#ifndef HYDRA_ASSET_DIR
#error "HYDRA_ASSET_DIR must be defined (see CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace {

// A fresh %TEMP%\hydra_Zoë_<tag>_<pid> folder, as a UTF-8 string.
std::string non_ascii_dir(const char* tag) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    fs::path dir = fs::path(tmp) / (std::wstring(L"hydra_Zo\u00EB_") +
                                    hydra::utf8_to_wide(tag) + L"_" +
                                    std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return hydra::wide_to_utf8(dir.wstring());
}

void remove_dir(const std::string& utf8_dir) {
    std::error_code ec;
    fs::remove_all(fs::path(hydra::utf8_to_wide(utf8_dir)), ec);
}

}  // namespace

TEST_CASE("read_file_text reads a file under a non-ASCII folder") {
    const std::string dir = non_ascii_dir("text");
    const std::string file = dir + "\\note.txt";
    std::FILE* f = hydra::fopen_utf8(file, L"wb");
    REQUIRE(f != nullptr);
    std::fputs("hello\r\nworld", f);
    std::fclose(f);

    CHECK(hydra::read_file_text(file) == "hello\r\nworld");  // bytes as they are
    CHECK_THROWS_AS(hydra::read_file_text(dir + "\\missing.txt"), std::runtime_error);
    remove_dir(dir);
}

TEST_CASE("the settings INI saves and loads under a non-ASCII folder") {
    const std::string dir = non_ascii_dir("ini");
    const std::string ini = dir + "\\hydra_settings.ini";
    hydra::app::Settings s;
    s.depth_value = 7;
    s.dm_last_user = "111";
    REQUIRE(s.save_file(ini));
    CHECK(hydra::file_exists_utf8(ini));

    hydra::app::Settings back = hydra::app::Settings::load_file(ini);
    CHECK(back.depth_value == 7);
    CHECK(back.dm_last_user == "111");
    remove_dir(dir);
}

TEST_CASE("the Preview renderer loads its assets from a non-ASCII folder (WARP)") {
    Microsoft::WRL::ComPtr<ID3D11Device> dev;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    REQUIRE(warp::make_device(dev, ctx));
    const std::string dir = non_ascii_dir("assets");
    fs::copy(fs::u8path(HYDRA_ASSET_DIR), fs::path(hydra::utf8_to_wide(dir)),
             fs::copy_options::recursive | fs::copy_options::overwrite_existing);

    // Before the fix this threw "PreviewRenderer: missing ...\3d-config.json".
    hydra::render::PreviewRenderer r(dev.Get(), ctx.Get(), dir);
    CHECK(r.config().hydra.msaa == 4);
    remove_dir(dir);
}
```

In `tests/test_app_state.cpp`, add `#include "corpus_util.h"` right after `#include "core/winstr.h"`, and append these two cases at the end of the file:

```cpp
// A Dynamics count that finished while another tab showed is stored when the
// window closes, not thrown away. Before, only the Dynamics tab collected the
// job, so the next open parsed the chart again.
TEST_CASE("close_details keeps a Dynamics count that finished on another tab") {
    ScratchPaths paths("appstate_dyn_close");
    std::unique_ptr<AppState> app = app_on(paths);
    app->selected->notespath = corpus::first_chart_with_suffix(".chart");
    app->show_details = true;

    app->update_dynamics();  // the Dynamics tab was shown once: the parse starts
    REQUIRE(app->dynamics_job != nullptr);
    for (int i = 0; i < 1200 && !app->dynamics_job->finished(); ++i) Sleep(50);
    REQUIRE(app->dynamics_job->finished());
    REQUIRE(app->dynamics_job->ok());

    // The user went back to Paths, so update_dynamics never ran again.
    app->close_details();

    CHECK_FALSE(app->show_details);
    CHECK(app->dynamics_job == nullptr);
    CHECK_FALSE(app->dynamics_result.has_value());
    const hydra::store::DynamicsKey key = hydra::app::dynamics_store_key(
        library_entry(0).md5, app->settings.difficulty(), app->settings.view_prodrums);
    CHECK(hydra::app::load_stored_dynamics(*app->store, key).has_value());
}

// The "Song file not found" check asks the disk when the window opens and
// then every two seconds, not on every frame.
TEST_CASE("the chart-file check runs on open and then every two seconds") {
    ScratchPaths paths("appstate_fileok");
    std::unique_ptr<AppState> app = app_on(paths);
    const std::string chart = temp_path("fileok_chart", ".chart");
    { std::ofstream f(chart); f << "[Song]\n"; }
    app->selected->notespath = chart;

    CHECK(app->selected_file_ok(10.0));        // first look
    std::remove(chart.c_str());
    CHECK(app->selected_file_ok(11.0));        // one second later: not asked again
    CHECK_FALSE(app->selected_file_ok(12.5));  // two seconds on: asked, and gone

    { std::ofstream f(chart); f << "[Song]\n"; }
    app->close_details();                      // the next open looks at once
    CHECK(app->selected_file_ok(12.6));
    std::remove(chart.c_str());
}
```

In `tests/ui/uitest_tests.cpp`, add this test after `test_squeezed_out_uncounted`, and add `{"details-close-teardown", test_details_close_teardown},` as the last entry of the table in `register_tests`:

```cpp
// Hiding the details window by any route tears it down. The "Rescan library"
// button used to set show_details = false directly, which skipped the
// teardown: the Preview kept its audio device and kept playing.
void test_details_close_teardown(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    ctx->ItemClick("**/Play");
    IM_CHECK(h.app->preview->playing());

    // Exactly what the Rescan library button does. The button itself only
    // shows when the chart file is missing, which never happens here.
    h.app->request_scan = true;
    h.app->show_details = false;
    ctx->Yield(3);
    IM_CHECK(!h.app->preview->active());
    IM_CHECK(!h.app->preview->playing());

    // The rescan the button asked for runs; finish it so the app is idle.
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->scan_job && h.app->scan_job->snapshot().finished;
    }, 60));
    ctx->SetRef("//Scanning charts");
    ctx->ItemClick("Continue");
    ctx->Yield(2);
}
```

In `CMakeLists.txt`, in the `add_executable(hydra_tests` list, change:

```cmake
    tests/test_app_state.cpp
)
```

to:

```cmake
    tests/test_app_state.cpp
    tests/test_preview_controller.cpp
    tests/test_utf8_paths.cpp
)
```

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors, because `PreviewController` has no `set_audio_device_factory` or `AudioSource`, `hydra::read_file_text` does not exist, and `AppState` has no `close_details` or `selected_file_ok`. Then run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test details-close-teardown`. Expected: `[FAIL]` on `!h.app->preview->active()`. If the non-ASCII cases later pass before Step 6, check `[System.Text.Encoding]::Default.CodePage`: on a PC set to the system-wide UTF-8 code page (65001) the narrow calls happen to work, and the cases only prove the fix on 1252.

- [ ] **Step 3: Let the load jobs stop at their cancel flag.** In `src/ui/job_base.h`, add `#include <exception>` after `#include <atomic>`. Then, right before `class JobBase {`, add:

```cpp
// Thrown by JobBase::throw_if_cancelled() between a job's steps. run_guarded
// turns it into a failed run whose error() reads "cancelled".
struct JobCancelled : std::exception {
    const char* what() const noexcept override { return "cancelled"; }
};
```

In the same file, change:

```cpp
    void shutdown() {
        cancel_.store(true);
        if (thread_.joinable()) thread_.join();
    }
```

to:

```cpp
    void shutdown() {
        cancel_.store(true);
        if (thread_.joinable()) thread_.join();
    }
    // Stops the job here when cancel() was called. The UI thread joins a
    // cancelled job, so a long job gives up between its steps instead of
    // running to the end while the window waits.
    void throw_if_cancelled() const {
        if (cancel_.load()) throw JobCancelled{};
    }
```

In `src/ui/preview_load_job.cpp`, replace the body of `PreviewLoadJob::run()`:

```cpp
void PreviewLoadJob::run() {
    run_guarded([this] {
        // Re-parse the chart and locate its audio (the note stream and stems
        // are never stored), then decode + mix to one 48 kHz stereo buffer.
        step_.store(Step::Reading);
        app::PreviewSource source =
            app::resolve_preview_source(entry_.notespath, pro_, bass2x_, difficulty_, rules_);
        // A chart with no charting at this difficulty would otherwise build an
        // empty scene and the tab would show a blank highway with no reason
        // given. Throwing here surfaces it as "Preview failed: ...", the same
        // wording analysis uses.
        if (source.song.is_empty())
            throw ChartFileError(no_notes_message(difficulty_, pro_));
        step_.store(Step::Decoding);
        audio::DecodedAudio mixed = audio::decode_and_mix(
            source.stems, /*out_rate=*/48000, /*out_channels=*/2, [this](int done, int total) {
                stems_total_.store(total);
                stems_done_.store(done);
                if (done == total) step_.store(Step::Mixing);
            });
        double offset_ms = source.audio_offset_ms;
```

with:

```cpp
void PreviewLoadJob::run() {
    run_guarded([this] {
        // Re-parse the chart and locate its audio (the note stream and stems
        // are never stored), then decode + mix to one 48 kHz stereo buffer.
        // Closing the details window joins this thread on the UI thread, so
        // the job looks at its cancel flag between steps and between stems.
        step_.store(Step::Reading);
        throw_if_cancelled();
        app::PreviewSource source =
            app::resolve_preview_source(entry_.notespath, pro_, bass2x_, difficulty_, rules_);
        // A chart with no charting at this difficulty would otherwise build an
        // empty scene and the tab would show a blank highway with no reason
        // given. Throwing here surfaces it as "Preview failed: ...", the same
        // wording analysis uses.
        if (source.song.is_empty())
            throw ChartFileError(no_notes_message(difficulty_, pro_));
        throw_if_cancelled();
        step_.store(Step::Decoding);
        audio::DecodedAudio mixed = audio::decode_and_mix(
            source.stems, /*out_rate=*/48000, /*out_channels=*/2, [this](int done, int total) {
                stems_total_.store(total);
                stems_done_.store(done);
                if (done == total) step_.store(Step::Mixing);
                // Called before the first stem and after each one, outside the
                // decoder's per-stem catch, so a cancel stops the load here.
                throw_if_cancelled();
            });
        throw_if_cancelled();
        double offset_ms = source.audio_offset_ms;
```

In `src/ui/dynamics_load_job.cpp`, change:

```cpp
    run_guarded([this] {
        Song song = load_songpath(entry_.notespath, pro_, app::kDynamicsParseBass2x,
                                  difficulty_);
        result_ = app::count_dynamics(song);
        return true;
    });
```

to:

```cpp
    run_guarded([this] {
        // Closing the details window joins this thread on the UI thread. The
        // parse is one call and cannot stop midway, so the job looks at its
        // cancel flag before and after it.
        throw_if_cancelled();
        Song song = load_songpath(entry_.notespath, pro_, app::kDynamicsParseBass2x,
                                  difficulty_);
        throw_if_cancelled();
        result_ = app::count_dynamics(song);
        return true;
    });
```

In `src/ui/dynamics_load_job.h`, change:

```cpp
    const std::string& key() const { return key_; }
```

to:

```cpp
    const std::string& key() const { return key_; }

    // What the job was started for, so a finished count is stored under the
    // chart and settings it was counted for, not whatever is selected now.
    const store::ChartLibraryEntry& entry() const { return entry_; }
    bool pro() const { return pro_; }
    Difficulty difficulty() const { return difficulty_; }
```

- [ ] **Step 4: Make a missing audio device a warning.** In `src/ui/preview_controller.h`, add `#include <cstdint>` and `#include <functional>` after `#include <memory>`. Change:

```cpp
    bool has_error() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
```

to:

```cpp
    bool has_error() const { return !error_.empty(); }
    const std::string& error() const { return error_; }

    // Set when the audio output device would not open. Not an error: the
    // chart still loads, draws and plays on the clock, just muted. The panel
    // shows one warning line and keeps drawing.
    bool has_audio_warning() const { return !audio_warning_.empty(); }
    const std::string& audio_warning() const { return audio_warning_; }

    // What opens the output device. Empty (the default) opens the real one; a
    // test installs a factory that throws, standing in for a PC with no audio
    // device.
    using AudioSource = std::function<int64_t(float* out, int64_t frames)>;
    using AudioDeviceFactory = std::function<std::unique_ptr<hydra::audio::PreviewAudioDevice>(
        int channels, int sample_rate, AudioSource source)>;
    void set_audio_device_factory(AudioDeviceFactory factory) {
        device_factory_ = std::move(factory);
    }
```

In the same file's private section, change:

```cpp
    bool active_ = false;
    std::string open_key_;
    std::string error_;
};
```

to:

```cpp
    bool active_ = false;
    std::string open_key_;
    std::string error_;
    std::string audio_warning_;
    AudioDeviceFactory device_factory_;
};
```

In `src/ui/preview_controller.cpp`, inside `poll()`, change:

```cpp
        if (transport_.has_audio()) {
            try {
                audio_device_ = std::make_unique<audio::PreviewAudioDevice>(
                    transport_.channels(), transport_.sample_rate(),
                    [this](float* out, int64_t frames) {
                        return transport_.read_frames(out, frames);
                    });
                audio_device_->start();
            } catch (const std::exception& e) {
                error_ = e.what();  // no device: still previewable, just muted
            }
        }
```

to:

```cpp
        if (transport_.has_audio()) {
            AudioSource source = [this](float* out, int64_t frames) {
                return transport_.read_frames(out, frames);
            };
            try {
                audio_device_ =
                    device_factory_
                        ? device_factory_(transport_.channels(), transport_.sample_rate(),
                                          source)
                        : std::make_unique<audio::PreviewAudioDevice>(
                              transport_.channels(), transport_.sample_rate(), source);
                audio_device_->start();
            } catch (const std::exception& e) {
                // No device: still previewable, just muted. A warning, not
                // error_, which the panel treats as fatal.
                audio_device_.reset();
                audio_warning_ = e.what();
            }
        }
```

In `close()` in the same file, change:

```cpp
    open_key_.clear();
    error_.clear();
}
```

to:

```cpp
    open_key_.clear();
    error_.clear();
    audio_warning_.clear();
}
```

In `src/ui/details_view.cpp`, inside `render_preview_panel`, change:

```cpp
        ImGui::ProgressBar(lp.fraction, ImVec2(-1.0f, 0.0f), overlay);
        return;
    }

    // Transport row: back 5 s, back 5 ticks, play/pause, forward 5 ticks,
```

to:

```cpp
        ImGui::ProgressBar(lp.fraction, ImVec2(-1.0f, 0.0f), overlay);
        return;
    }

    // No audio output device: the chart previews muted. One line says so and
    // the highway below draws as usual; the device's own message is a hover away.
    if (pc->has_audio_warning()) {
        ImGui::TextColored(kWarningColor, "No audio device found; the preview is muted.");
        hint(pc->audio_warning().c_str());
    }

    // Transport row: back 5 s, back 5 ticks, play/pause, forward 5 ticks,
```

- [ ] **Step 5: One teardown on the close edge, Dynamics reaped every frame, the file check on a timer.** In `src/ui/app_state.h`, inside `struct DetailsViewState`, change:

```cpp
    GenerationWatcher analyze_watcher;
    double done_at = -1.0;
    bool stored = false;
    std::string store_error;
};
```

to:

```cpp
    GenerationWatcher analyze_watcher;
    double done_at = -1.0;
    bool stored = false;
    std::string store_error;
    // The chart file's presence (the "Song file not found" line), as of the
    // last look. Looked at when the window opens and then every
    // AppState::kFileCheckSeconds, not every frame: on a sleeping or network
    // drive one look can stall a frame. -1 = look now.
    bool file_ok = true;
    double file_checked_at = -1.0;
};
```

In the same file, change:

```cpp
    // Selection / details modal.
    std::optional<store::ChartLibraryEntry> selected;
    bool show_details = false;
    void select(const store::ChartLibraryEntry& entry);
```

to:

```cpp
    // Selection / details modal.
    std::optional<store::ChartLibraryEntry> selected;
    bool show_details = false;
    void select(const store::ChartLibraryEntry& entry);

    // Everything that must stop when the Song Details window closes. It runs
    // once, on the window's open-to-closed edge, whatever closed it: the X,
    // the Rescan library button, or a new selection. An unfinished analysis
    // is cancelled, the Preview stops and lets go of its audio device, a
    // finished Dynamics count is kept and an unfinished one is cancelled.
    // Safe to call when already closed.
    void close_details();

    // Whether the selected chart's file exists, as of the last look; looks
    // again once `now` (seconds) is kFileCheckSeconds past it.
    bool selected_file_ok(double now);
    static constexpr double kFileCheckSeconds = 2.0;
```

In the same file, change:

```cpp
    void update_dynamics();
```

to:

```cpp
    void update_dynamics();

    // Stores a finished Dynamics parse and drops its job. The details window
    // calls it every frame, whichever tab shows; a parse that finished while
    // another tab was up used to be thrown away at close.
    void reap_dynamics();
```

In `src/ui/app_state.cpp`, add `#include "core/winstr.h"` after `#include "app/rules_file.h"`. Replace `AppState::select`:

```cpp
void AppState::select(const store::ChartLibraryEntry& entry) {
    // Tear down any preview for the previous chart: its audio device must stop
    // before a new chart's is opened, and the highway must not keep playing the
    // old song.
    if (preview) preview->close();
    // Drop the dynamics cache: the new chart needs its own parse.
    if (dynamics_job) { dynamics_job->cancel(); dynamics_job.reset(); }
    dynamics_result.reset();
    dynamics_key.clear();
    dynamics_store_error.clear();
    selected = entry;
    show_details = true;
    refresh_viewed_record();
}
```

with:

```cpp
void AppState::select(const store::ChartLibraryEntry& entry) {
    // The previous chart's window, torn down the one way. A row can only be
    // clicked while the window is closed, when this already ran, so it is a
    // no-op in the app; it matters for callers that select directly.
    close_details();
    selected = entry;
    show_details = true;
    refresh_viewed_record();
}

void AppState::close_details() {
    show_details = false;
    // With the window gone there is nowhere to show an analysis' progress,
    // and the search would keep burning CPU (up to the Auto budget) unseen.
    // The main window reaps the job once the cancel lands.
    if (analyze_job && !analyze_job->finished()) analyze_job->cancel();
    // The audio device must stop, and the GPU and decode work must not keep
    // running behind a hidden window.
    if (preview) preview->close();
    // Keep a count that already finished; cancel one still parsing.
    reap_dynamics();
    if (dynamics_job) {
        dynamics_job->cancel();
        dynamics_job.reset();
    }
    dynamics_result.reset();
    dynamics_key.clear();
    dynamics_store_error.clear();
    // The next open looks at the chart file at once.
    details_ui.file_checked_at = -1.0;
}

bool AppState::selected_file_ok(double now) {
    if (!selected) return false;
    if (details_ui.file_checked_at < 0.0 ||
        now - details_ui.file_checked_at >= kFileCheckSeconds) {
        details_ui.file_ok = file_exists_utf8(selected->notespath);
        details_ui.file_checked_at = now;
    }
    return details_ui.file_ok;
}
```

In the same file, inside `update_dynamics`, replace the reap block:

```cpp
    // Reap a finished job.
    if (dynamics_job && dynamics_job->finished()) {
        if (dynamics_job->ok()) {
            dynamics_result = dynamics_job->take_result();
            dynamics_key = dynamics_job->key();
            // Persist to the store so the next open is instant.
            try {
                app::save_dynamics(*store, app::dynamics_store_key(selected->md5, diff, pro),
                                   *dynamics_result);
                dynamics_store_error.clear();
            } catch (const std::exception& e) {
                dynamics_store_error =
                    std::string("Counted, but saving failed: ") + e.what();
            }
            dynamics_job.reset();
        }
        // On failure, keep the job around so we can read its error().
    }
}
```

with:

```cpp
    reap_dynamics();
}

void AppState::reap_dynamics() {
    if (!dynamics_job || !dynamics_job->finished()) return;
    // On failure, keep the job around so the tab can read its error().
    if (!dynamics_job->ok()) return;
    dynamics_result = dynamics_job->take_result();
    dynamics_key = dynamics_job->key();
    // Persist under what the job counted, so the next open is instant.
    try {
        app::save_dynamics(*store,
                           app::dynamics_store_key(dynamics_job->entry().md5,
                                                   dynamics_job->difficulty(),
                                                   dynamics_job->pro()),
                           *dynamics_result);
        dynamics_store_error.clear();
    } catch (const std::exception& e) {
        dynamics_store_error = std::string("Counted, but saving failed: ") + e.what();
    }
    dynamics_job.reset();
}
```

In `src/ui/details_view.cpp`, inside `render_controls`, change:

```cpp
    bool file_ok = file_exists_utf8(app.selected->notespath);
```

to:

```cpp
    bool file_ok = app.selected_file_ok(ImGui::GetTime());
```

In the same function, change:

```cpp
        if (ImGui::SmallButton("Rescan library")) {
            app.request_scan = true;  // the main window starts the scan
            app.show_details = false;
            ImGui::CloseCurrentPopup();
        }
```

to:

```cpp
        if (ImGui::SmallButton("Rescan library")) {
            app.request_scan = true;   // the main window starts the scan
            app.show_details = false;  // close_details() runs on the next frame's edge
            ImGui::CloseCurrentPopup();
        }
```

In the same file, replace the start of `render_details_modal`, from its first line through the `if (!open) { ... }` block:

```cpp
void render_details_modal(AppState& app) {
    // Job lifecycle first, every frame -- even with the modal closed or a
    // different tab in front.
    update_analyze_job(app);

    // All of this modal's own state lives on AppState (see DetailsViewState):
    // a static here would outlive the AppState it describes.
    bool& prev_open = app.details_ui.prev_open;
    const Path*& selected_path = app.details_ui.selected_path;
    GenerationWatcher& record_watcher = app.details_ui.record_watcher;

    if (app.show_details && !prev_open) ImGui::OpenPopup("SongDetails");
    prev_open = app.show_details;
    if (!app.show_details) return;
```

(unchanged sizing and title lines follow, down to `bool visible = ...`), then everything from the long comment `// \`open\` is checked unconditionally below` through:

```cpp
    if (!open) {
        app.show_details = false;
        cancel_running_analysis();
        close_preview();
        close_dynamics();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
```

The new start of the function is:

```cpp
void render_details_modal(AppState& app) {
    // Job lifecycles first, every frame -- even with the modal closed or a
    // different tab in front.
    update_analyze_job(app);
    app.reap_dynamics();

    // All of this modal's own state lives on AppState (see DetailsViewState):
    // a static here would outlive the AppState it describes.
    bool& prev_open = app.details_ui.prev_open;
    const Path*& selected_path = app.details_ui.selected_path;
    GenerationWatcher& record_watcher = app.details_ui.record_watcher;

    if (app.show_details && !prev_open) ImGui::OpenPopup("SongDetails");
    // The one teardown, on the open-to-closed edge, whatever closed the
    // window: its X, the Rescan library button, or the resync below.
    if (!app.show_details && prev_open) app.close_details();
    prev_open = app.show_details;
    if (!app.show_details) return;
```

and the part after `bool visible = ImGui::BeginPopupModal(...)` becomes:

```cpp
    // `open` is checked unconditionally below (not only when `visible` is
    // true) because the popup's close button can make BeginPopupModal itself
    // start returning false on/after the closing frame, without ever handing
    // back a true-but-open-false frame to react to. Either way show_details
    // goes false, and the next frame's edge above runs close_details().
    if (!visible) {
        // We know app.show_details was true when we called OpenPopup above,
        // so if ImGui says the popup isn't showing, our state has drifted
        // from ImGui's -- resync.
        app.show_details = false;
        return;
    }

    if (!open) {
        app.show_details = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
```

The three lambdas `cancel_running_analysis`, `close_preview` and `close_dynamics` are gone with it.

- [ ] **Step 6: Read every GUI file through core/winstr.** In `src/core/winstr.h`, change:

```cpp
// The whole file's bytes; throws std::runtime_error when the open fails.
std::vector<uint8_t> read_file_bytes(const std::string& utf8_path);
```

to:

```cpp
// The whole file's bytes; throws std::runtime_error when the open fails.
std::vector<uint8_t> read_file_bytes(const std::string& utf8_path);

// The whole file as text, bytes as they are (no newline translation); throws
// std::runtime_error when the open fails.
std::string read_file_text(const std::string& utf8_path);
```

In `src/core/winstr.cpp`, add after `read_file_bytes`:

```cpp
std::string read_file_text(const std::string& utf8_path) {
    std::vector<uint8_t> bytes = read_file_bytes(utf8_path);
    return std::string(bytes.begin(), bytes.end());
}
```

In `src/render/preview_renderer.cpp`, change `#include "render/file_util.h"` to `#include "core/winstr.h"`. In its anonymous namespace, right after the `check` function, add:

```cpp
// A whole asset file, empty when it is missing or unreadable, so each loader
// below can name the asset in its own message. Reads through core/winstr, so
// an install folder like C:\Users\Zoë\... works.
std::vector<uint8_t> asset_bytes(const std::string& utf8_path) {
    try {
        return hydra::read_file_bytes(utf8_path);
    } catch (const std::exception&) {
        return {};
    }
}

std::string asset_text(const std::string& utf8_path) {
    std::vector<uint8_t> bytes = asset_bytes(utf8_path);
    return std::string(bytes.begin(), bytes.end());
}
```

Then replace the five reads. `std::vector<uint8_t> bytes = read_file_bytes(asset_dir + "\\textures\\" + file);` becomes `std::vector<uint8_t> bytes = asset_bytes(asset_dir + "\\textures\\" + file);`. `std::string text = read_file_text(asset_dir + "\\models\\" + file);` becomes `std::string text = asset_text(asset_dir + "\\models\\" + file);`. `std::string cfg_text = read_file_text(asset_dir + "\\3d-config.json");` becomes `std::string cfg_text = asset_text(asset_dir + "\\3d-config.json");`. The two shader reads `read_file_text(asset_dir + "\\shaders\\object.hlsl")` and `read_file_text(asset_dir + "\\shaders\\fade.hlsl")` become `asset_text(...)` with the same arguments.

In `src/ui/icons.cpp`, add `#include "core/winstr.h"` after `#include "app/config.h"`, and change:

```cpp
ImTextureID load_png_texture(ID3D11Device* device, const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        return 0;
    }
    std::vector<uint8_t> buf((size_t)size);
    size_t read = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (read != buf.size()) return 0;
```

to:

```cpp
ImTextureID load_png_texture(ID3D11Device* device, const std::string& path) {
    // Through core/winstr, so an install folder like C:\Users\Zoë\... works.
    std::vector<uint8_t> buf;
    try {
        buf = hydra::read_file_bytes(path);
    } catch (const std::exception&) {
        return 0;
    }
    if (buf.empty()) return 0;
```

and in `load_icons`, drop the four `.c_str()` calls, for example `load_png_texture(device, dir + "icon_record_32.png")`.

In `src/app/config.cpp`, inside `Settings::load_file`, change `std::ifstream f(path);` to `std::ifstream f(utf8_to_wide(path));`. Inside `Settings::save_file`, change `std::ofstream f(path, std::ios::trunc);` to `std::ofstream f(utf8_to_wide(path), std::ios::trunc);`. MSVC's streams take a wide path, and `core/winstr.h` is already included there.

Delete the old reader and take it out of the build:

```powershell
git rm src/render/file_util.h src/render/file_util.cpp
```

In `CMakeLists.txt`, in `add_library(hydra_render STATIC`, delete the line `    src/render/file_util.cpp`.

Point the three tests that used it at core/winstr. In `tests/test_preview_config.cpp` and `tests/test_obj_loader.cpp`, change `#include "render/file_util.h"` to `#include "core/winstr.h"`, and change each `read_file_text(` call to `hydra::read_file_text(` (one call in test_preview_config.cpp, three in test_obj_loader.cpp). In `tests/test_preview_golden.cpp`, change `#include "render/file_util.h"` to `#include "core/winstr.h"`, and change:

```cpp
    std::string spec_text = read_file_text(spec_path);
    if (spec_text.empty()) {
        MESSAGE("golden fixture absent (" << spec_path << "): skipped");
        return;
    }
```

to:

```cpp
    if (!file_exists_utf8(spec_path)) {
        MESSAGE("golden fixture absent (" << spec_path << "): skipped");
        return;
    }
    std::string spec_text = read_file_text(spec_path);
```

(`using namespace hydra;` at the top of that file makes both names resolve; the `read_file_bytes` call further down now resolves to `hydra::read_file_bytes`, which throws on a missing file instead of returning empty. The fixture is checked in, so that is fine.)

- [ ] **Step 7: Delete the dead code.** In `src/ui/preview_controller.h`, delete these three lines:

```cpp
    const std::string& open_key() const { return open_key_; }
```

```cpp
    bool scrubbing() const { return scrubbing_; }
```

```cpp
    int volume() const { return volume_pct_; }
```

and delete the member `    render::RenderParams params_;`. In `src/ui/preview_controller.cpp`, change `renderer_->render(transport_.tick(), params_);` to `renderer_->render(transport_.tick());`.

In `src/render/preview_renderer.h`, delete:

```cpp
struct RenderParams {
    double speed = 1.0;  // fixed at 1 today; Onyx's playback-speed knob
};

```

and change `void render(double now_ms, const RenderParams& params);` to `void render(double now_ms);`.

In `src/render/preview_renderer.cpp`, delete `    bool have_state = false;` from `Impl`, delete `    impl_->have_state = true;` from `set_scene`, and change:

```cpp
void PreviewRenderer::render(double now_ms, const RenderParams& params) {
    Impl& d = *impl_;
    if (!d.final_rtv) return;
    ID3D11DeviceContext* ctx = d.context;
    const PreviewConfig& cfg = d.cfg;

    // ---- scene pass: the highway into the (multisampled) track target ----
    std::vector<DrawCommand> cmds =
        build_highway_draws(d.state, cfg, now_ms / 1000.0, params.speed);
```

to:

```cpp
void PreviewRenderer::render(double now_ms) {
    Impl& d = *impl_;
    if (!d.final_rtv) return;
    ID3D11DeviceContext* ctx = d.context;
    const PreviewConfig& cfg = d.cfg;

    // ---- scene pass: the highway into the (multisampled) track target ----
    // Onyx's draw code takes a playback speed; Hydra always plays at 1x.
    constexpr double kPlaybackSpeed = 1.0;
    std::vector<DrawCommand> cmds =
        build_highway_draws(d.state, cfg, now_ms / 1000.0, kPlaybackSpeed);
```

In `tests/test_preview_renderer.cpp`, change each of the six `r.render(1000.0, RenderParams{});` / `r.render(0.0, RenderParams{});` calls to `r.render(1000.0);` / `r.render(0.0);`. In `tests/test_preview_golden.cpp`, change `r.render(time_ms, RenderParams{});` to `r.render(time_ms);`.

In `src/ui/main.cpp`, delete the line `    ImGuiIO& io = ImGui::GetIO();` (its only reader is the block below) and delete:

```cpp
        // Draw and present the additional platform windows (viewports).
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

```

In `src/ui/app_shell.cpp`, delete `    io.ConfigDpiScaleViewports = true;`.

In `src/ui/theme.h`, delete `inline const ImVec4 kDisabledTextColor{50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f};`. In `src/ui/details_view.cpp`, inside `dynamics_table_row`, change the comment:

```cpp
    // kDisabledTextColor is tuned for text on a teal button; on the dark panel
    // it disappears. The style's own disabled text grey reads fine here.
```

to:

```cpp
    // The style's own disabled text grey: it reads on the dark panel.
```

- [ ] **Step 8: Run everything and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, the new cases included. Run `.\build_cpp.ps1 -Target Hydra` (it must still build with the viewports block gone). Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test `[PASS]`, `details-close-teardown` included. Run the two greps from the acceptance list; both print nothing. Run the score-neutral batch comparison; it prints nothing.

- [ ] **Step 9: Commit.**

```bash
git add tests/test_preview_controller.cpp tests/test_utf8_paths.cpp src/ui/job_base.h src/ui/preview_load_job.cpp src/ui/dynamics_load_job.h src/ui/dynamics_load_job.cpp src/ui/preview_controller.h src/ui/preview_controller.cpp src/ui/app_state.h src/ui/app_state.cpp src/ui/details_view.cpp src/ui/app_shell.cpp src/ui/main.cpp src/ui/theme.h src/ui/icons.cpp src/render/preview_renderer.h src/render/preview_renderer.cpp src/core/winstr.h src/core/winstr.cpp src/app/config.cpp CMakeLists.txt tests/test_app_state.cpp tests/test_preview_renderer.cpp tests/test_preview_golden.cpp tests/test_preview_config.cpp tests/test_obj_loader.cpp tests/ui/uitest_tests.cpp
git commit -m "Close the details window cleanly and preview without an audio device

Task: Task 6: The details window closes cleanly and the Preview survives a PC with no audio device
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

(The two deleted files were staged by `git rm` in Step 6.)

---

### Task 7: The library view keeps its state on AppState, and the number boxes stop hammering the disk

Today `library_view.cpp` keeps its own state in function statics: the status line's fade timer, the folder-removal confirm, the search box's text and edit time, and the dmleaderboards name filter. A static outlives the AppState it describes. The GUI test runner builds a fresh AppState per test, so the next test inherits the last one's search text and filter; the dynamics-stored test already works around this. The statics are also the only reason `Generation` starts each counter at a process-unique value. This task moves them into a `LibraryViewState` owned by AppState, the way `DetailsViewState` did, and drops that trick.

Second, the score-range, ms-limit and SP-cap boxes in the details window. Holding a +/- button changes the value every frame, and each change runs `commit_settings`. That rewrites the INI, re-queries the library page (a count, a list and one summary per row) and decodes the chart's whole record again. User decision 3 keeps the boxes live but makes them cheap. The value still applies at every step. The INI is written once the edit ends, meaning once no widget is active. A lookup already made is parked and comes back without a decode, the way a browser keeps a page you just left. Only the per-row summary query reruns, because the page's rows and counts do not depend on the cap or the lens (the ms limit and score range a result ran under).

Third, the main window asks the disk whether the report file exists on every frame. It now looks every two seconds, and at once when a report job finishes.

Audit check: confirmed. There are seven static variables across four functions: `generation` and `shown_at` in `render_status_line`, `confirm_remove` in `render_folder_manager`, `buf`, `synced` and `edited_at` in `render_search_box`, and `filter` in `render_dm_picker_modal`. Once they move, every `GenerationWatcher` in the app belongs to an AppState, so the process-unique counter start is no longer needed.

What the user sees: nothing. The boxes behave as today, and the INI is saved when you let go of the button or leave the box.

**Depends on:** nothing.

**Expected overlaps:** T6 edits the same `src/ui/app_state.h` (it adds `close_details`, `reap_dynamics`, `selected_file_ok` and two `DetailsViewState` fields), `src/ui/app_state.cpp` (it rewrites `select` and `update_dynamics`; this task also adds one line to `select` and one to `store_finished_analysis`), `src/ui/details_view.cpp` (`render_controls`: T6 changes the `file_ok` line and the Rescan comment, this task changes the three number boxes' calls), `src/ui/app_shell.cpp` (T6 deletes a line in `setup_imgui`, this task adds one to `run_frame`), `tests/test_app_state.cpp` and `tests/ui/uitest_tests.cpp` (both append cases and register entries). T18 appends to `tests/ui/uitest_tests.cpp` too. T9 turns the per-row summary query into one page query: that loop moves here into the new `AppState::refresh_summaries()`, so T9 edits `refresh_summaries` rather than `refresh_page`. This task does not touch the store. T11 adds fields to `DetailsViewState` next to this task's changes.

**Goal:** the library view's state dies with its AppState, and a held number box costs one summary query per step, with no INI write and no repeat decode.

**Files:**
- Modify: `src/ui/generation.h`, `src/ui/app_state.h`, `src/ui/app_state.cpp`, `src/ui/library_view.cpp`, `src/ui/details_view.cpp`, `src/ui/app_shell.cpp`
- Test: `tests/test_app_state.cpp`, `tests/ui/uitest_tests.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="number boxes*"`, `-tc="stepping a number box*"` and `-tc="the report-file check*"` pass.
- [ ] `hydra_tests.exe -tc="commit_settings*"` still passes (all four existing cases).
- [ ] `hydra_uitest.exe --test library-state-per-app` passes, and `--all` passes.
- [ ] `Select-String -Path src\ui\library_view.cpp -Pattern '^\s+static '` prints nothing.
- [ ] `Select-String -Path src\ui\generation.h -Pattern 'next_start'` prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*settings*,*number box*,*report-file*"; .\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all` → `Status: SUCCESS!`, then `[PASS]` on every GUI test.

**Steps:**

- [ ] **Step 1: Write the failing tests.** In `tests/test_app_state.cpp`, add `#include <filesystem>` after `#include <cstdio>` and `#include "app/report_files.h"` after `#include "app/dynamics_breakdown.h"`. Append:

```cpp
// The number boxes apply each step at once (the shown record follows live)
// but leave the INI until the edit ends: holding +/- used to rewrite the
// file every frame.
TEST_CASE("number boxes apply at once but write the INI only on flush") {
    ScratchPaths paths("appstate_flush");
    std::unique_ptr<AppState> app = app_on(paths);
    const int seeded_depth = app->settings.depth_value;

    app->settings.depth_value = seeded_depth + 3;
    app->edit_settings();
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);  // applied live
    CHECK(Settings::load_file(paths.ini).depth_value == seeded_depth);  // not saved yet

    app->flush_settings();  // the edit ended
    CHECK(Settings::load_file(paths.ini).depth_value == seeded_depth + 3);
}

// Stepping away and back re-shows a lookup already made, without asking the
// store again (a big record's decode is the expensive part).
TEST_CASE("stepping a number box back reuses the lookup it already made") {
    ScratchPaths paths("appstate_parked");
    std::unique_ptr<AppState> app = app_on(paths);

    app->settings.sp_cap = 8;
    app->edit_settings();
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);
    app->settings.sp_cap = kSeededCap;
    app->edit_settings();
    CHECK(app->viewed.status == RecordStatus::Ready);

    // A cap-8 record appears behind the cache's back. Stepping to 8 shows the
    // parked "not analyzed" answer: proof the store was not asked again.
    HydraRecord at8;
    at8.sp_cap = 8;
    at8.ms_limit = Settings{}.mslimit_value;
    app->store->add_record(
        RecordKey{library_entry(0).md5, kChartMode, CapQuery::at(8), Settings{}.lens()}, at8);
    app->settings.sp_cap = 8;
    app->edit_settings();
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);

    // A new selection drops every parked lookup, so the store is asked again.
    app->select(library_entry(0));
    CHECK(app->viewed.status == RecordStatus::Ready);
}

// The main window's "Open path report" button looks for the file every two
// seconds, not on every frame.
TEST_CASE("the report-file check is cached for two seconds") {
    ScratchPaths paths("appstate_report");
    std::unique_ptr<AppState> app = app_on(paths);
    const std::filesystem::path report(hydra::app::report_html_path());
    std::error_code ec;
    std::filesystem::remove(report, ec);

    CHECK_FALSE(app->report_file_shown(10.0));
    hydra::app::write_report_file(report, "<html></html>");
    CHECK_FALSE(app->report_file_shown(11.0));  // one second later: not asked
    CHECK(app->report_file_shown(12.5));        // two seconds on: asked, found
    std::filesystem::remove(report, ec);
}
```

In `tests/ui/uitest_tests.cpp`, add this test after `test_squeezed_out_uncounted` and add `{"library-state-per-app", test_library_state_per_app},` at the end of the table in `register_tests`:

```cpp
// The library view's own state (the search text, the dmleaderboards filter,
// the status fade, the folder confirm) belongs to the AppState. When it lived
// in function statics, the next test's fresh app still showed the last test's
// search text and filter.
void test_library_state_per_app(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    ctx->ItemInputValue("##search", "zzqx");
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == "zzqx"; }, 5));
    ctx->ItemClick("Compare dmleaderboards user...");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->dm_users.empty(); }, 10));
    ctx->SetRef("//Compare dmleaderboards user");
    ctx->ItemInputValue("##dmfilter", "zzqx");
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("alice") == std::string::npos);
    ctx->ItemClick("Close");
    ctx->Yield(2);

    // A fresh app: both boxes start empty. ImGui's text log shows an input
    // box's contents, so leftover text would be on screen.
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    IM_CHECK(h.app->search.empty());
    IM_CHECK(visible_text(h).find("zzqx") == std::string::npos);
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Compare dmleaderboards user...");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->dm_users.empty(); }, 10));
    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("alice") != std::string::npos;
    }, 5));
    IM_CHECK(visible_text(h).find("zzqx") == std::string::npos);
    ctx->SetRef("//Compare dmleaderboards user");
    ctx->ItemClick("Close");
    ctx->Yield(2);
}
```

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors, because `AppState` has no `edit_settings`, `flush_settings` or `report_file_shown`. Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test library-state-per-app`. Expected: `[FAIL]` at the first `find("zzqx") == npos` after the reset, because the static search buffer still holds `zzqx`.

- [ ] **Step 3: Drop the process-unique counter start.** Replace the whole `struct Generation` in `src/ui/generation.h`:

```cpp
struct Generation {
    // Each counter starts at a process-unique value. The views' watchers are
    // function statics that outlive any one AppState (the UI test runner
    // builds a fresh AppState per test); with every counter restarting at 0,
    // a watcher that had seen the old app's value 1 would treat the new app's
    // first bump to 1 as "already seen" and silently drop that event.
    int n = next_start();
    void bump() { ++n; }

private:
    static int next_start() {
        static int next = 0;
        int start = next;
        next += 1 << 20;
        return start;
    }
};
```

with:

```cpp
// Every watcher lives on the same AppState as the counter it watches
// (DetailsViewState, LibraryViewState), so a plain count from 0 is enough.
struct Generation {
    int n = 0;
    void bump() { ++n; }
};
```

- [ ] **Step 4: Add LibraryViewState and the cheap-edit calls to AppState.** In `src/ui/app_state.h`, right after the closing `};` of `struct DetailsViewState`, add:

```cpp
// Per-frame UI state of the main window's library view. Owned here, not as
// statics in the draw code, for the same reason as DetailsViewState: a static
// outlives this AppState, so the UI test runner's next app inherited the last
// test's search text and dmleaderboards filter.
struct LibraryViewState {
    // The status line's fade. The watcher starts at "already seen" for this
    // app's counter (0), so app startup does not start a fade.
    GenerationWatcher status_watcher{/*seen=*/0};
    double status_shown_at = -1.0;
    // The folder waiting on the "Remove folder?" confirm.
    std::optional<size_t> confirm_remove;
    // The search box's text, whether it was filled from `search` yet, and
    // when it was last typed in (the library re-queries 0.25 s after).
    char search_buf[256] = "";
    bool search_synced = false;
    double search_edited_at = -1.0;
    // The dmleaderboards picker's name filter.
    char dm_filter[128] = "";
    // Whether the path report file exists, as of the last look.
    bool report_exists = false;
    double report_checked_at = -1.0;  // -1 = look now
};
```

In the same file, change:

```cpp
    // The details modal's own per-frame state (see DetailsViewState above).
    DetailsViewState details_ui;
```

to:

```cpp
    // The details modal's own per-frame state (see DetailsViewState above).
    DetailsViewState details_ui;
    // The library view's own per-frame state (see LibraryViewState above).
    LibraryViewState library_ui;

    // Whether the path report file exists, as of the last look; looks again
    // once `now` (seconds) is kReportCheckSeconds past it, or at once after
    // library_ui.report_checked_at is reset to -1.
    bool report_file_shown(double now);
    static constexpr double kReportCheckSeconds = 2.0;
```

Replace the `commit_settings` declaration and its comment:

```cpp
    // The one way to finish a settings change: write the INI (with a status
    // message when it can't be written — a silent failure made changes look
    // persisted when they weren't) and then refresh whatever the change
    // invalidated.
    //
    // This is the single place that knows which settings change a record's
    // identity — the chart mode, the SP cap, and the lens (the ms limit and
    // the score range a result ran under). Every widget just mutates
    // `settings` and calls this, so none of them can forget a refresh. That
    // forgetting is exactly the bug class here: the 1.5.1 SP-cap crash came
    // from this path, and the Pro Drums / 2x Bass checkboxes used to refresh
    // the library page while leaving `viewed` pointing at the old record.
    void commit_settings();
```

with:

```cpp
    // The one way to finish a settings change: write the INI (with a status
    // message when it can't be written — a silent failure made changes look
    // persisted when they weren't) and then refresh whatever the change
    // invalidated.
    //
    // This is the single place that knows which settings change a record's
    // identity — the chart mode, the SP cap, and the lens (the ms limit and
    // the score range a result ran under). Every widget just mutates
    // `settings` and calls this, so none of them can forget a refresh. That
    // forgetting is exactly the bug class here: the 1.5.1 SP-cap crash came
    // from this path, and the Pro Drums / 2x Bass checkboxes used to refresh
    // the library page while leaving `viewed` pointing at the old record.
    void commit_settings();

    // The number boxes' form of commit_settings: the same refresh at once,
    // but the INI waits for flush_settings. A held +/- button changes the
    // value every frame, and each change used to rewrite the file.
    void edit_settings();
    // Writes the INI if an edit_settings change is not saved yet. run_frame
    // calls it once no widget is active, which is when an edit has ended.
    void flush_settings();
```

In the private section, change:

```cpp
    std::string committed_chartmode_;
    store::CapQuery committed_cap_;
    store::Lens committed_lens_;
};
```

to:

```cpp
    std::string committed_chartmode_;
    store::CapQuery committed_cap_;
    store::Lens committed_lens_;

    // An edit_settings change the INI does not have yet.
    bool settings_unsaved_ = false;
    void save_settings();
    // The refresh half of commit_settings.
    void apply_settings();
    // Re-reads each row's Best Path summary for the current page only.
    void refresh_summaries();

    // The number boxes step through settings one value at a time, and each
    // step used to decode the chart's record again. `viewed_key_` is what
    // `viewed` answers; lookups the boxes stepped away from are parked here
    // and come back without asking the store. Cleared whenever a record may
    // have changed under them: a new selection or a stored analysis.
    std::optional<store::RecordKey> viewed_key_;
    std::vector<std::pair<store::RecordKey, store::RecordLookup>> parked_lookups_;
    static constexpr size_t kParkedLookups = 16;
    // Shows the lookup for the current settings: parked if seen, read otherwise.
    void show_record_for_settings();
```

Add `#include <utility>` after `#include <string>` at the top of the header.

- [ ] **Step 5: Implement it in app_state.cpp.** In `src/ui/app_state.cpp`, add `#include "app/report_files.h"` after `#include "app/rules_file.h"`. Change the destructor:

```cpp
AppState::~AppState() = default;
```

to:

```cpp
AppState::~AppState() { flush_settings(); }  // an edit in progress still lands
```

Split `refresh_page`. Change:

```cpp
    current_page.rows =
        store->list_chart_library(search_opt, table_viewpage * rows_per_page, rows_per_page);

    // Resolve each row's Best Path summary once here instead of per row per
    // frame in the render loop (a SQLite query at 60fps x 200 rows, on the
    // render thread, against the same mutex the batch workers hold).
    current_page.summaries.clear();
    current_page.summaries.reserve(current_page.rows.size());
    for (const store::ChartLibraryEntry& row : current_page.rows) {
        store::SummaryLookup summary = store->get_summary(settings.record_key(row.md5));
        LibraryPage::RowSummary rs;
        rs.state = summary.status;
        rs.bestpath = std::move(summary.bestpath);
        current_page.summaries.push_back(std::move(rs));
    }

    library_total =
        search.empty() ? current_page.total_count : store->chart_library_count(std::nullopt);
}
```

to:

```cpp
    current_page.rows =
        store->list_chart_library(search_opt, table_viewpage * rows_per_page, rows_per_page);
    refresh_summaries();

    library_total =
        search.empty() ? current_page.total_count : store->chart_library_count(std::nullopt);
}

void AppState::refresh_summaries() {
    // Resolve each row's Best Path summary once here instead of per row per
    // frame in the render loop (a SQLite query at 60fps x 200 rows, on the
    // render thread, against the same mutex the batch workers hold).
    current_page.summaries.clear();
    current_page.summaries.reserve(current_page.rows.size());
    for (const store::ChartLibraryEntry& row : current_page.rows) {
        store::SummaryLookup summary = store->get_summary(settings.record_key(row.md5));
        LibraryPage::RowSummary rs;
        rs.state = summary.status;
        rs.bestpath = std::move(summary.bestpath);
        current_page.summaries.push_back(std::move(rs));
    }
}
```

In `select`, add `parked_lookups_.clear();  // a new chart: nothing parked applies` right before `selected = entry;`. In `store_finished_analysis`, add `parked_lookups_.clear();  // a record just changed` right before `refresh_viewed_record();`.

Replace `refresh_viewed_record`:

```cpp
void AppState::refresh_viewed_record() {
    if (!selected) {
        viewed = store::RecordLookup{};
        return;
    }
    viewed = store->get_record(settings.record_key(selected->md5));
    record_generation.bump();
}
```

with:

```cpp
void AppState::refresh_viewed_record() {
    if (!selected) {
        viewed = store::RecordLookup{};
        viewed_key_.reset();
        return;
    }
    store::RecordKey key = settings.record_key(selected->md5);
    viewed = store->get_record(key);
    viewed_key_ = std::move(key);
    record_generation.bump();
}

void AppState::show_record_for_settings() {
    if (!selected) {
        refresh_viewed_record();
        return;
    }
    store::RecordKey key = settings.record_key(selected->md5);
    if (viewed_key_ && *viewed_key_ == key) return;

    std::optional<store::RecordLookup> found;
    for (auto it = parked_lookups_.begin(); it != parked_lookups_.end(); ++it) {
        if (it->first == key) {
            found = std::move(it->second);
            parked_lookups_.erase(it);
            break;
        }
    }
    // Park what is showing now. Moves, not copies: the decoded record changes
    // hands without being copied.
    if (viewed_key_) {
        parked_lookups_.emplace_back(std::move(*viewed_key_), std::move(viewed));
        if (parked_lookups_.size() > kParkedLookups)
            parked_lookups_.erase(parked_lookups_.begin());
    }
    if (found) {
        viewed = std::move(*found);
        viewed_key_ = std::move(key);
        record_generation.bump();  // selected_path must re-sync, as after a read
    } else {
        refresh_viewed_record();
    }
}

bool AppState::report_file_shown(double now) {
    if (library_ui.report_checked_at < 0.0 ||
        now - library_ui.report_checked_at >= kReportCheckSeconds) {
        library_ui.report_exists = app::report_file_exists();
        library_ui.report_checked_at = now;
    }
    return library_ui.report_exists;
}
```

Replace `commit_settings`:

```cpp
void AppState::commit_settings() {
    if (!settings.save())
        set_status("Settings could not be saved — " + app::ini_path() +
                   " is not writable.");

    // Which record a chart shows is (chart, chart mode, SP cap, lens). When
    // any of the last three moves, every cached lookup is answering the old
    // question and has to be re-asked.
    std::string chartmode = settings.chartmode_key();
    store::CapQuery cap = settings.cap_query();
    store::Lens lens = settings.lens();
    if (chartmode != committed_chartmode_) {
        // A different chart mode is a different library listing, so the user
        // starts over at page one.
        table_viewpage = 0;
        refresh_page();
        refresh_viewed_record();
    } else if (cap != committed_cap_ || lens != committed_lens_) {
        // The cap box and the search controls live in the details modal.
        // Resetting the page here would yank the library out from under a
        // user who never touched it.
        refresh_page();
        refresh_viewed_record();
    }
    committed_chartmode_ = std::move(chartmode);
    committed_cap_ = cap;
    committed_lens_ = lens;
}
```

with:

```cpp
void AppState::commit_settings() {
    save_settings();
    apply_settings();
}

void AppState::edit_settings() {
    settings_unsaved_ = true;
    apply_settings();
}

void AppState::flush_settings() {
    if (settings_unsaved_) save_settings();
}

void AppState::save_settings() {
    settings_unsaved_ = false;
    if (!settings.save())
        set_status("Settings could not be saved — " + app::ini_path() +
                   " is not writable.");
}

void AppState::apply_settings() {
    // Which record a chart shows is (chart, chart mode, SP cap, lens). When
    // any of the last three moves, every cached lookup is answering the old
    // question and has to be re-asked.
    std::string chartmode = settings.chartmode_key();
    store::CapQuery cap = settings.cap_query();
    store::Lens lens = settings.lens();
    if (chartmode != committed_chartmode_) {
        // A different chart mode is a different library listing, so the user
        // starts over at page one.
        table_viewpage = 0;
        refresh_page();
        refresh_viewed_record();
    } else if (cap != committed_cap_ || lens != committed_lens_) {
        // The cap box and the search controls live in the details modal.
        // Resetting the page here would yank the library out from under a
        // user who never touched it. The page's rows and counts do not depend
        // on the cap or lens, so only the Best Path summaries are asked again.
        refresh_summaries();
        show_record_for_settings();
    }
    committed_chartmode_ = std::move(chartmode);
    committed_cap_ = cap;
    committed_lens_ = lens;
}
```

(The em dash in the status string is copied from the file as it is; T14 moves `/utf-8` onto every target.)

- [ ] **Step 6: Point library_view.cpp at LibraryViewState.** In `src/ui/library_view.cpp`, in `render_status_line`, change:

```cpp
    // seen starts at 0 (the first AppState's initial counter value), not the
    // watcher's default -1: app startup must not count as a change and start
    // a fade. A later AppState in the same process (UI test runner) starts
    // higher and does register once, which the empty-message check below
    // turns into a no-op.
    static GenerationWatcher generation{/*seen=*/0};
    static double shown_at = -1.0;
```

to:

```cpp
    // Lives on the AppState (LibraryViewState); the watcher starts at "seen"
    // for this app's counter, so startup does not start a fade.
    GenerationWatcher& generation = app.library_ui.status_watcher;
    double& shown_at = app.library_ui.status_shown_at;
```

In `render_folder_manager`, change `    static std::optional<size_t> confirm_remove;` to `    std::optional<size_t>& confirm_remove = app.library_ui.confirm_remove;`.

In `render_search_box`, change:

```cpp
    static char buf[256] = "";
    static bool synced = false;
    static double edited_at = -1.0;
```

to:

```cpp
    char (&buf)[256] = app.library_ui.search_buf;
    bool& synced = app.library_ui.search_synced;
    double& edited_at = app.library_ui.search_edited_at;
```

In `render_dm_picker_modal`, change `    static char filter[128] = "";` to `    char (&filter)[128] = app.library_ui.dm_filter;`.

In `render_main_window`, change:

```cpp
    if (!app.batch_job && app.report_job && app.report_job->finished()) {
```

to:

```cpp
    if (!app.batch_job && app.report_job && app.report_job->finished()) {
        app.library_ui.report_checked_at = -1.0;  // a new report: look at once
```

and change:

```cpp
    } else if (app::report_file_exists()) {
```

to:

```cpp
    } else if (app.report_file_shown(ImGui::GetTime())) {
```

- [ ] **Step 7: The three number boxes edit instead of commit; run_frame flushes.** In `src/ui/details_view.cpp`, inside `render_controls`, change the `##depthvalue` block:

```cpp
    if (ImGui::InputInt("##depthvalue", &app.settings.depth_value)) {
        if (app.settings.depth_value < 0) app.settings.depth_value = 0;
        app.commit_settings();
    }
```

to:

```cpp
    // The three number boxes apply every step live but save the INI once the
    // edit ends (AppState::edit_settings, flushed by run_frame).
    if (ImGui::InputInt("##depthvalue", &app.settings.depth_value)) {
        if (app.settings.depth_value < 0) app.settings.depth_value = 0;
        app.edit_settings();
    }
```

the `##mslimitvalue` block:

```cpp
    if (ImGui::InputInt("##mslimitvalue", &app.settings.mslimit_value)) {
        app.settings.mslimit_value = std::clamp(app.settings.mslimit_value, -500, 500);
        app.commit_settings();
    }
```

to:

```cpp
    if (ImGui::InputInt("##mslimitvalue", &app.settings.mslimit_value)) {
        app.settings.mslimit_value = std::clamp(app.settings.mslimit_value, -500, 500);
        app.edit_settings();
    }
```

and the `##spcapvalue` block:

```cpp
        if (ImGui::InputInt("##spcapvalue", &last_cap)) {
            if (last_cap < 1) last_cap = 1;
            app.settings.sp_cap = last_cap;
            app.commit_settings();
        }
```

to:

```cpp
        if (ImGui::InputInt("##spcapvalue", &last_cap)) {
            if (last_cap < 1) last_cap = 1;
            app.settings.sp_cap = last_cap;
            app.edit_settings();
        }
```

The checkboxes, the scores/points combo and the backend-limit box keep `commit_settings()`: they change once per click.

In `src/ui/app_shell.cpp`, inside `run_frame`, change:

```cpp
    render_main_window(app);
    render_details_modal(app);
```

to:

```cpp
    render_main_window(app);
    render_details_modal(app);
    // The number boxes apply edits live but leave the INI until the edit
    // ends (AppState::edit_settings). An edit has ended once no widget is
    // active: the +/- button is released, or the text box lost focus.
    if (!ImGui::IsAnyItemActive()) app.flush_settings();
```

- [ ] **Step 8: Let the analyze uitest wait for the INI.** In `tests/ui/uitest_tests.cpp`, inside `test_analyze`, the INI now lands a frame after the box lets go. Change:

```cpp
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).sp_cap == 8);
```

to:

```cpp
    IM_CHECK(wait_until(ctx, [&] {
        return hydra::app::Settings::load_file(h.ini_path).sp_cap == 8;
    }, 5));
```

- [ ] **Step 9: Run everything and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, with the three new cases and the four `commit_settings*` cases. Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test `[PASS]`, `library-state-per-app`, `analyze` and `cap-switch` included. Run the two greps from the acceptance list; both print nothing.

- [ ] **Step 10: Commit.**

```bash
git add src/ui/generation.h src/ui/app_state.h src/ui/app_state.cpp src/ui/library_view.cpp src/ui/details_view.cpp src/ui/app_shell.cpp tests/test_app_state.cpp tests/ui/uitest_tests.cpp
git commit -m "Keep the library view's state on AppState and save number boxes on edit end

Task: Task 7: The library view keeps its state on AppState, and the number boxes stop hammering the disk
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: The Preview mixes one stem at a time and applies the delay to .sng and .srb

Loading a Preview briefly needs about twice the audio memory it should. `decode_and_mix` in src/audio/mixer.cpp first decodes every stem into a `std::vector<DecodedAudio> decoded`. Then `mix_stems` converts every one of them into a second vector, `converted`, and only then sums. So every decoded stem and every converted copy are alive at once. For five 5-minute stems that peaks near 1.2 GB. This task converts and adds one stem at a time, then drops it. At most one decoded stem and its converted copy now sit beside the growing mix.

The sum must not change by a single bit. Floating-point addition depends on order, so the new code keeps the old order exactly. Each output sample still starts at 0.0f and adds stem 0, then stem 1, and so on. A new test rebuilds today's convert-everything-then-sum mix from the public API and compares the two byte for byte.

The Opus decoder has the same kind of waste on a smaller scale. `decode_ogg_opus` in src/audio/decode.cpp builds a fresh `std::vector<float> tmp` of 5,760 frames for every packet. That is one heap allocation every 20 ms of audio. This task allocates it once, when the channel count is known, and reuses it. A pinned fingerprint of the decoded fixture proves the output is unchanged.

The task also carries user decision 4. Today `resolve_preview_source` in src/app/preview_source.cpp sets `audio_offset_ms` only for folder charts, so a .sng or .srb chart plays with offset 0. After this task a .sng uses the `delay` in its own metadata block, and falls back to its chart's Offset. A .srb has no delay field (parse/srb.h lists its eight strings, and none is a delay), so it uses its chart's Offset. The rule itself stays in `preview_audio_offset_ms`, unchanged: a delay of 0 counts as unset.

Last, three audio functions nobody calls go away: `PreviewAudioDevice::stop()`, `PreviewAudioDevice::running()` and `audio::headless()`. The destructor already stops the device, and the GUI test harness only ever calls `set_headless`.

What the user sees: a .sng or .srb chart whose delay or Offset is not zero now plays its music in sync in the Preview, the way Clone Hero plays it (decision 4). Everything else is unchanged, and loading a Preview takes less memory.

**Depends on:** nothing.

**Expected overlaps:** Task 14 deletes the `#define MA_NO_ENCODING` / `#define MA_NO_GENERATION` lines at the top of src/audio/mixer.cpp and src/audio/decode.cpp; this task edits function bodies further down in both files, so keep both sides. Task 6 may touch the Preview load path (src/ui/preview_load_job.cpp, src/ui/preview_controller.cpp) for the audio-error and muted-audio items; this task does not edit those files. Task 16 (wave 3) later replaces the local `to_lower` / `ends_with_ci` in preview_source.cpp.

**Goal:** Mix stems one at a time with bit-identical output, reuse one Opus decode buffer, apply the delay/Offset rule to .sng and .srb, and delete three dead audio functions.

**Files:**
- Modify: `src/audio/mixer.cpp` (`convert_stem`, `mix_stems`, `decode_and_mix`)
- Modify: `src/audio/mixer.h` (header comment)
- Modify: `src/audio/decode.cpp` (`decode_ogg_opus`)
- Modify: `src/audio/device.h`, `src/audio/device.cpp` (delete `stop`, `running`, `headless`)
- Modify: `src/app/preview_source.h` (`PreviewSource::audio_offset_ms` comment, new `sng_delay_ms`)
- Modify: `src/app/preview_source.cpp` (`parse_delay_ms`, `sng_audio_from`, `sng_delay_ms`, `read_ini_delay_ms`, `extract_sng_audio`, `resolve_preview_source`)
- Test: `tests/test_audio_mixer.cpp`, `tests/test_audio_decode.cpp`, `tests/test_preview_source.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="mix_stems matches the convert-all-then-sum mix bit for bit"` and `-tc="decode_and_mix matches decoding every stem then mixing"` pass after the rewrite.
- [ ] `hydra_tests.exe -tc="decode_audio: Ogg-Opus output is pinned bit for bit"` passes with the fingerprint captured from the unchanged code.
- [ ] `hydra_tests.exe -tc="sng_delay_ms*"`, `-tc="resolve_preview_source: a .sng's metadata delay*"` and `-tc="resolve_preview_source: a .srb uses its chart's Offset"` pass.
- [ ] `Select-String src\audio\mixer.cpp -Pattern 'std::vector<DecodedAudio> decoded'` prints nothing.
- [ ] `Select-String src\audio\decode.cpp -Pattern 'std::vector<float> tmp'` prints nothing.
- [ ] `Select-String src,tests -Recurse -Pattern 'PreviewAudioDevice::stop|PreviewAudioDevice::running|bool headless\(\)|audio::headless\(\)'` prints nothing.
- [ ] The full `hydra_tests.exe` run ends `Status: SUCCESS!`, and `hydra_uitest.exe --all` passes.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*mix*,*Opus*,*sng*,*srb*,*delay*,*Offset*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the mixer guard tests.** They pass today, on purpose: they pin today's output so the rewrite cannot drift. In tests/test_audio_mixer.cpp add `#include <algorithm>` and `#include <cstring>` to the includes. Add these helpers inside the anonymous namespace, after `estimate_freq_hz`:

```cpp
// Today's mixer, rebuilt from the public API: convert every stem on its own
// (a one-stem mix is 0.0f plus the converted stem), keep every converted copy,
// then sum them in stem order into a zeroed buffer as long as the longest.
// The one-at-a-time mixer must match it bit for bit.
DecodedAudio reference_mix(const std::vector<DecodedAudio>& stems, int rate,
                           int channels) {
    std::vector<std::vector<float>> converted;
    std::size_t longest = 0;
    for (const DecodedAudio& s : stems) {
        converted.push_back(mix_stems({s}, rate, channels).samples);
        longest = std::max(longest, converted.back().size());
    }
    DecodedAudio out;
    out.sample_rate = rate;
    out.channels = channels;
    out.samples.assign(longest, 0.0f);
    for (const std::vector<float>& c : converted)
        for (std::size_t i = 0; i < c.size(); ++i) out.samples[i] += c[i];
    return out;
}

// Same format and the same float bits, sample for sample.
bool same_bits(const DecodedAudio& a, const DecodedAudio& b) {
    if (a.sample_rate != b.sample_rate || a.channels != b.channels) return false;
    if (a.samples.size() != b.samples.size()) return false;
    return a.samples.empty() ||
           std::memcmp(a.samples.data(), b.samples.data(),
                       a.samples.size() * sizeof(float)) == 0;
}
```

Append these cases at the end of the file:

```cpp
TEST_CASE("mix_stems matches the convert-all-then-sum mix bit for bit") {
    // Mixed rates, channel counts and lengths, so resampling, upmixing and
    // the grow-with-silence path all run. The middle stem is the longest.
    DecodedAudio a = synth_tone(300.0, 24000, 0.5);   // mono 24 kHz, short
    DecodedAudio b = synth_tone(440.0, 44100, 1.2);   // mono 44.1 kHz, longest
    DecodedAudio c = make_pcm(std::vector<float>(48000 * 2, 0.25f), 2, 48000);
    const std::vector<DecodedAudio> stems = {a, b, c};

    CHECK(same_bits(mix_stems(stems, 48000, 2), reference_mix(stems, 48000, 2)));
    CHECK(same_bits(mix_stems(stems, 44100, 1), reference_mix(stems, 44100, 1)));
}

TEST_CASE("decode_and_mix matches decoding every stem then mixing") {
    hydra::app::PreviewAudioStem ogg;
    ogg.label = "song";
    ogg.path = std::string(HYDRA_TESTDATA_DIR) + "/audio/sine220.ogg";
    hydra::app::PreviewAudioStem mp3;
    mp3.label = "drums";
    mp3.bytes = read_fixture("sine220.mp3");
    hydra::app::PreviewAudioStem opus;
    opus.label = "guitar";
    opus.bytes = read_fixture("sine220.opus");
    hydra::app::PreviewAudioStem junk;
    junk.label = "broken";
    junk.bytes = {'n', 'o', 't', ' ', 'a', 'u', 'd', 'i', 'o'};
    const std::vector<hydra::app::PreviewAudioStem> stems = {ogg, junk, mp3, opus};

    std::vector<DecodedAudio> decoded;
    for (const hydra::app::PreviewAudioStem& s : stems) {
        try {
            decoded.push_back(decode_stem(s));
        } catch (const std::exception&) {
        }
    }
    REQUIRE(decoded.size() == 3);

    CHECK(same_bits(decode_and_mix(stems, 48000, 2), mix_stems(decoded, 48000, 2)));
}
```

- [ ] **Step 2: Write the Opus fingerprint test.** In tests/test_audio_decode.cpp append:

```cpp
// FNV-1a over the decoded float bytes. It pins the Opus decoder's exact output,
// so reusing one decode buffer cannot change a single sample. If libopus is
// ever upgraded, re-capture both numbers from a build of the old code first.
TEST_CASE("decode_audio: Ogg-Opus output is pinned bit for bit") {
    constexpr int64_t kPinnedFrames = 0;   // captured in Step 3
    constexpr uint64_t kPinnedHash = 0;    // captured in Step 3

    const DecodedAudio a = decode_audio(read_fixture("sine220.opus"));
    uint64_t h = 1469598103934665603ull;
    const auto* p = reinterpret_cast<const uint8_t*>(a.samples.data());
    for (size_t i = 0; i < a.samples.size() * sizeof(float); ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    MESSAGE("opus fingerprint: frames=" << a.frames() << " hash=" << h);
    CHECK(a.frames() == kPinnedFrames);
    CHECK(h == kPinnedHash);
}
```

- [ ] **Step 3: Capture the fingerprint from today's code.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="decode_audio: Ogg-Opus output is pinned*" -s`. Expected: the case fails and prints `opus fingerprint: frames=<F> hash=<H>`. Put F and H into `kPinnedFrames` and `kPinnedHash` (write H with a `ull` suffix). Run it again. Expected: `Status: SUCCESS!`. Also run `-tc="mix_stems matches*,decode_and_mix matches*"`. Expected: `Status: SUCCESS!` on the unchanged mixer. Both captures happen before any source edit.

- [ ] **Step 4: Write the failing .sng/.srb offset tests.** In tests/test_preview_source.cpp add `#include <utility>` to the includes. Give `make_sng` an optional metadata list. Change its signature and its `meta` line from:

```cpp
std::vector<uint8_t> make_sng(const std::vector<SngFile>& files) {
    uint8_t mask[16];
    for (int i = 0; i < 16; ++i) mask[i] = static_cast<uint8_t>(i * 13 + 7);
    std::vector<uint8_t> meta(20, 0xAB);
```

to:

```cpp
std::vector<uint8_t> make_sng(
    const std::vector<SngFile>& files,
    const std::vector<std::pair<std::string, std::string>>& metadata = {}) {
    uint8_t mask[16];
    for (int i = 0; i < 16; ++i) mask[i] = static_cast<uint8_t>(i * 13 + 7);
    // No pairs: the old 20 junk bytes, which read as no metadata at all, so
    // every existing fixture stays byte-identical. With pairs: a real block,
    // u64 count then (u32 length + bytes) key and value strings.
    std::vector<uint8_t> meta(20, 0xAB);
    if (!metadata.empty()) {
        meta.clear();
        push_u64(meta, metadata.size());
        for (const auto& [key, value] : metadata) {
            push_u32(meta, static_cast<uint32_t>(key.size()));
            meta.insert(meta.end(), key.begin(), key.end());
            push_u32(meta, static_cast<uint32_t>(value.size()));
            meta.insert(meta.end(), value.begin(), value.end());
        }
    }
```

Add this helper to the anonymous namespace, right after `make_srb`:

```cpp
// multidiff's chart with an Offset line in its [Song] section.
std::vector<uint8_t> chart_with_offset(const std::string& seconds) {
    std::string s = multidiff::chart_text();
    const std::string anchor = "  Resolution = 192\n";
    const size_t at = s.find(anchor);
    REQUIRE(at != std::string::npos);
    s.insert(at + anchor.size(), "  Offset = " + seconds + "\n");
    return bytes_of(s);
}
```

Append these cases at the end of the file:

```cpp
TEST_CASE("sng_delay_ms reads the delay key in any case") {
    const std::vector<uint8_t> notes = multidiff::chart_bytes();
    const std::vector<uint8_t> upper = make_sng({{"notes.chart", notes}}, {{"DELAY", "-120"}});
    REQUIRE(sng_delay_ms(upper).has_value());
    CHECK(*sng_delay_ms(upper) == doctest::Approx(-120.0));

    CHECK_FALSE(sng_delay_ms(make_sng({{"notes.chart", notes}}, {{"delay", "soon"}})).has_value());
    CHECK_FALSE(sng_delay_ms(make_sng({{"notes.chart", notes}}, {{"name", "X"}})).has_value());
    CHECK_FALSE(sng_delay_ms(make_sng({{"notes.chart", notes}})).has_value());
}

TEST_CASE("resolve_preview_source: a .sng's metadata delay replaces the chart Offset") {
    const std::vector<uint8_t> notes = chart_with_offset("0.25");

    const std::string with_delay = fixture_dir() + "\\delay500.sng";
    write_bytes(with_delay, make_sng({{"notes.chart", notes}}, {{"name", "X"}, {"delay", "500"}}));
    CHECK(resolve_preview_source(with_delay, true, true).audio_offset_ms ==
          doctest::Approx(500.0));

    // A delay of 0 counts as unset (the Lunaris rule), whatever the key's case.
    const std::string zero_delay = fixture_dir() + "\\delay0.sng";
    write_bytes(zero_delay, make_sng({{"notes.chart", notes}}, {{"Delay", "0"}}));
    CHECK(resolve_preview_source(zero_delay, true, true).audio_offset_ms ==
          doctest::Approx(250.0));

    // No delay key at all: the chart's Offset applies.
    const std::string no_delay = fixture_dir() + "\\nodelay.sng";
    write_bytes(no_delay, make_sng({{"notes.chart", notes}}, {{"name", "X"}}));
    CHECK(resolve_preview_source(no_delay, true, true).audio_offset_ms ==
          doctest::Approx(250.0));
}

TEST_CASE("resolve_preview_source: a .srb uses its chart's Offset") {
    const std::string path = fixture_dir() + "\\offset.srb";
    write_bytes(path, make_srb(chart_with_offset("0.25"), {}, "notes.chart"));
    CHECK(resolve_preview_source(path, true, true).audio_offset_ms ==
          doctest::Approx(250.0));
}
```

- [ ] **Step 5: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error, because `sng_delay_ms` is not declared. (Comment out the `sng_delay_ms` case for a moment to see the other two fail at run time: both report `audio_offset_ms` 0 where 500 or 250 is expected. Restore it.)

- [ ] **Step 6: Mix one stem at a time.** In src/audio/mixer.cpp add `#include <utility>` to the includes. Replace `convert_stem`'s first lines:

```cpp
std::vector<float> convert_stem(const DecodedAudio& s, int out_rate,
                                int out_channels) {
    if (s.channels <= 0 || s.samples.empty()) return {};
    if (s.sample_rate == out_rate && s.channels == out_channels)
        return s.samples;
```

with:

```cpp
// Takes the stem by value so a caller that owns it can move it in; a stem
// already in the output format then hands over its samples without a copy.
std::vector<float> convert_stem(DecodedAudio s, int out_rate,
                                int out_channels) {
    if (s.channels <= 0 || s.samples.empty()) return {};
    if (s.sample_rate == out_rate && s.channels == out_channels)
        return std::move(s.samples);
```

Right after `convert_stem`, still inside the anonymous namespace, add:

```cpp
// Add one converted stem into the running mix, growing the mix with silence
// when this stem is longer. Every sample still starts at 0.0f and adds the
// stems in order, the same order as converting them all first, so the sum is
// bit-identical to the old mixer.
void add_into(std::vector<float>& mix, const std::vector<float>& stem) {
    if (stem.size() > mix.size()) mix.resize(stem.size(), 0.0f);
    for (std::size_t i = 0; i < stem.size(); ++i) mix[i] += stem[i];
}
```

Replace `mix_stems` and `decode_and_mix`:

```cpp
DecodedAudio mix_stems(const std::vector<DecodedAudio>& stems, int out_rate,
                       int out_channels) {
    DecodedAudio out;
    out.sample_rate = out_rate;
    out.channels = out_channels;

    std::vector<std::vector<float>> converted;
    converted.reserve(stems.size());
    std::size_t longest = 0;
    for (const DecodedAudio& s : stems) {
        converted.push_back(convert_stem(s, out_rate, out_channels));
        longest = std::max(longest, converted.back().size());
    }

    out.samples.assign(longest, 0.0f);
    for (const std::vector<float>& c : converted)
        for (std::size_t i = 0; i < c.size(); ++i) out.samples[i] += c[i];

    return out;
}

DecodedAudio decode_and_mix(const std::vector<app::PreviewAudioStem>& stems,
                            int out_rate, int out_channels,
                            const DecodeProgress& on_progress) {
    const int total = static_cast<int>(stems.size());
    if (on_progress) on_progress(0, total);
    std::vector<DecodedAudio> decoded;
    decoded.reserve(stems.size());
    int done = 0;
    for (const app::PreviewAudioStem& s : stems) {
        try {
            decoded.push_back(decode_stem(s));
        } catch (const std::exception&) {
            // Skip a stem we cannot decode; the rest of the chart still plays.
        }
        if (on_progress) on_progress(++done, total);
    }
    return mix_stems(decoded, out_rate, out_channels);
}
```

with:

```cpp
DecodedAudio mix_stems(const std::vector<DecodedAudio>& stems, int out_rate,
                       int out_channels) {
    DecodedAudio out;
    out.sample_rate = out_rate;
    out.channels = out_channels;
    for (const DecodedAudio& s : stems)
        add_into(out.samples, convert_stem(s, out_rate, out_channels));
    return out;
}

DecodedAudio decode_and_mix(const std::vector<app::PreviewAudioStem>& stems,
                            int out_rate, int out_channels,
                            const DecodeProgress& on_progress) {
    DecodedAudio out;
    out.sample_rate = out_rate;
    out.channels = out_channels;
    const int total = static_cast<int>(stems.size());
    if (on_progress) on_progress(0, total);
    int done = 0;
    for (const app::PreviewAudioStem& s : stems) {
        // Decode, convert and add this stem, then let both copies go before
        // the next one decodes: at most one stem is ever held beside the mix.
        DecodedAudio decoded;
        bool ok = false;
        try {
            decoded = decode_stem(s);
            ok = true;
        } catch (const std::exception&) {
            // Skip a stem we cannot decode; the rest of the chart still plays.
        }
        if (ok) add_into(out.samples, convert_stem(std::move(decoded), out_rate, out_channels));
        if (on_progress) on_progress(++done, total);
    }
    return out;
}
```

In src/audio/mixer.h, change the header sentence:

```cpp
// their own sample rates and channel counts (see audio/decode.h), so the mixer
// converts each to a common output format, then adds them sample for sample.
```

to:

```cpp
// their own sample rates and channel counts (see audio/decode.h), so the mixer
// converts each to a common output format and adds it into the mix, one stem
// at a time: only one decoded stem and its converted copy are alive beside the
// mix, never all of them.
```

- [ ] **Step 7: Reuse one Opus decode buffer.** In src/audio/decode.cpp, `decode_ogg_opus`, change:

```cpp
        const int kMaxFrame = 5760;  // 120 ms at 48 kHz, the largest Opus packet
```

to:

```cpp
        const int kMaxFrame = 5760;  // 120 ms at 48 kHz, the largest Opus packet
        // One decode buffer for the whole stream, sized once the OpusHead
        // gives the channel count. Every packet decodes into it.
        std::vector<float> pcm;
```

After `out.channels = channels;` add:

```cpp
                    pcm.resize(static_cast<std::size_t>(kMaxFrame) * channels);
```

Replace the packet branch's buffer and its uses:

```cpp
                    std::vector<float> tmp(static_cast<std::size_t>(kMaxFrame) *
                                           channels);
                    int n = opus_decode_float(dec, op.packet,
                                              static_cast<opus_int32>(op.bytes),
                                              tmp.data(), kMaxFrame, 0);
```

with:

```cpp
                    int n = opus_decode_float(dec, op.packet,
                                              static_cast<opus_int32>(op.bytes),
                                              pcm.data(), kMaxFrame, 0);
```

and:

```cpp
                        tmp.begin() + static_cast<std::size_t>(start) * channels,
                        tmp.begin() + static_cast<std::size_t>(n) * channels);
```

with:

```cpp
                        pcm.begin() + static_cast<std::size_t>(start) * channels,
                        pcm.begin() + static_cast<std::size_t>(n) * channels);
```

A packet arriving before the OpusHead cannot reach this branch: `packet_index == 0` always takes the OpusHead branch, which throws unless it sizes `pcm`.

- [ ] **Step 8: Apply the delay/Offset rule to .sng and .srb.** In src/app/preview_source.h, replace:

```cpp
    // Where chart time 0 sits in the audio: audio_ms = chart_ms +
    // audio_offset_ms. From song.ini delay (ms) and .chart Offset (s), as
    // Clone Hero applies them. 0 for .sng and .srb.
    double audio_offset_ms = 0.0;
```

with:

```cpp
    // Where chart time 0 sits in the audio: audio_ms = chart_ms +
    // audio_offset_ms. From the delay (ms) and the .chart Offset (s), as
    // Clone Hero applies them. A folder chart's delay comes from its song.ini
    // and a .sng's from its metadata block. A .srb has no delay field, so
    // only its chart's Offset counts.
    double audio_offset_ms = 0.0;
```

After the `read_ini_delay_ms` declaration add:

```cpp
// A .sng container's `delay` metadata in milliseconds, the key matched in any
// case (the last one wins, like the library scan's metadata read), or nullopt
// when it is missing or not a number.
std::optional<double> sng_delay_ms(const std::vector<uint8_t>& sng_bytes);
```

In src/app/preview_source.cpp, add to the anonymous namespace, just before `constexpr double kOffsetDirection`:

```cpp
// A delay string in milliseconds, or nullopt when it is not wholly a number.
// Shared by song.ini's delay and a .sng's metadata delay.
std::optional<double> parse_delay_ms(const std::string& text) {
    try {
        size_t used = 0;
        const double ms = std::stod(text, &used);
        if (used != text.size()) return std::nullopt;
        return ms;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// The audio entries of an already-read .sng, XOR-demasked.
std::vector<PreviewAudioStem> sng_audio_from(const std::vector<uint8_t>& buf) {
    std::vector<PreviewAudioStem> stems;
    for (const SngFileEntry& e : sng_read_file_table(buf)) {
        if (!is_audio_filename(e.name)) continue;
        std::optional<std::vector<uint8_t>> bytes = sng_decode_file(buf, e);
        if (!bytes) continue;  // corrupt entry
        PreviewAudioStem s;
        s.label = stem_of(e.name);
        s.bytes = std::move(*bytes);
        stems.push_back(std::move(s));
    }
    return stems;
}
```

`is_audio_filename` is declared in the header, so it is visible here. Replace `extract_sng_audio`'s body:

```cpp
std::vector<PreviewAudioStem> extract_sng_audio(const std::string& path) {
    std::vector<PreviewAudioStem> stems;
    std::vector<uint8_t> buf = read_file_bytes(path);
    for (const SngFileEntry& e : sng_read_file_table(buf)) {
        if (!is_audio_filename(e.name)) continue;
        std::optional<std::vector<uint8_t>> bytes = sng_decode_file(buf, e);
        if (!bytes) continue;  // corrupt entry
        PreviewAudioStem s;
        s.label = stem_of(e.name);
        s.bytes = std::move(*bytes);
        stems.push_back(std::move(s));
    }
    return stems;
}
```

with:

```cpp
std::vector<PreviewAudioStem> extract_sng_audio(const std::string& path) {
    return sng_audio_from(read_file_bytes(path));
}

std::optional<double> sng_delay_ms(const std::vector<uint8_t>& sng_bytes) {
    std::optional<double> delay;
    for (const auto& [key, value] : sng_read_metadata(sng_bytes))
        if (to_lower(key) == "delay") delay = parse_delay_ms(value);
    return delay;
}
```

In `read_ini_delay_ms`, replace:

```cpp
    const auto it = ini.find("delay");
    if (it == ini.end()) return std::nullopt;
    try {
        size_t used = 0;
        const double ms = std::stod(it->second, &used);
        if (used != it->second.size()) return std::nullopt;
        return ms;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
```

with:

```cpp
    const auto it = ini.find("delay");
    if (it == ini.end()) return std::nullopt;
    return parse_delay_ms(it->second);
}
```

In `resolve_preview_source`, replace:

```cpp
    if (ends_with_ci(notespath, ".sng"))
        src.stems = extract_sng_audio(notespath);
    else if (ends_with_ci(notespath, ".srb")) {
        src.stems = extract_srb_audio(notespath);
        // If decryption fails (wrong key, corrupt file, etc.) fall back to
        // loose audio files beside the .srb, same as a folder chart.
        if (src.stems.empty()) src.stems = find_loose_audio(dir_name(notespath));
    } else {
```

with:

```cpp
    if (ends_with_ci(notespath, ".sng")) {
        // One read serves both the audio and the metadata delay.
        const std::vector<uint8_t> buf = read_file_bytes(notespath);
        src.stems = sng_audio_from(buf);
        src.audio_offset_ms = preview_audio_offset_ms(sng_delay_ms(buf), src.song.chart_offset_s);
    } else if (ends_with_ci(notespath, ".srb")) {
        src.stems = extract_srb_audio(notespath);
        // If decryption fails (wrong key, corrupt file, etc.) fall back to
        // loose audio files beside the .srb, same as a folder chart.
        if (src.stems.empty()) src.stems = find_loose_audio(dir_name(notespath));
        // A .srb's metadata has no delay field (parse/srb.h), so only the
        // chart's Offset counts.
        src.audio_offset_ms = preview_audio_offset_ms(std::nullopt, src.song.chart_offset_s);
    } else {
```

- [ ] **Step 9: Delete the dead device functions.** In src/audio/device.h, change:

```cpp
// Process-wide switch for harnesses with no sound card (the GUI test runner):
// when set, PreviewAudioDevice opens nothing and start()/stop() only track
// state, so Play/Pause stays testable without touching a real device.
void set_headless(bool headless);
bool headless();
```

to:

```cpp
// Process-wide switch for harnesses with no sound card (the GUI test runner):
// when set, PreviewAudioDevice opens nothing and start() only tracks state,
// so Play/Pause stays testable without touching a real device.
void set_headless(bool headless);
```

and delete these two lines from the class:

```cpp
    void stop();
    bool running() const;
```

In src/audio/device.cpp delete `bool headless() { return g_headless.load(); }`, and delete the whole `PreviewAudioDevice::stop()` function and the `PreviewAudioDevice::running()` line. The destructor keeps its own `ma_device_stop` call, so nothing else changes.

- [ ] **Step 10: Run everything and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, including the two mixer guards and the pinned Opus fingerprint from Step 3. Then run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test passes (the harness still calls `set_headless`, which stays). Run the three `Select-String` checks from the Acceptance Criteria. Expected: each prints nothing.

- [ ] **Step 11: Commit.**

```bash
git add src/audio/mixer.cpp src/audio/mixer.h src/audio/decode.cpp src/audio/device.h src/audio/device.cpp src/app/preview_source.h src/app/preview_source.cpp tests/test_audio_mixer.cpp tests/test_audio_decode.cpp tests/test_preview_source.cpp
git commit -m "Mix Preview stems one at a time and apply the delay to .sng and .srb

The mixer now converts and adds each stem, then drops it, so a load no
longer holds every decoded stem and every converted copy at once. The sum
is bit-identical (guard tests rebuild the old mixer). The Opus decoder
reuses one buffer. A .sng takes its metadata delay, else the chart
Offset; a .srb takes the chart Offset (user decision 4). Removes the
uncalled PreviewAudioDevice::stop/running and audio::headless.

Task: Task 8: The Preview mixes one stem at a time and applies the delay to .sng and .srb
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/audio/mixer.cpp","src/audio/mixer.h","src/audio/decode.cpp","src/audio/device.h","src/audio/device.cpp","src/app/preview_source.h","src/app/preview_source.cpp","tests/test_audio_mixer.cpp","tests/test_audio_decode.cpp","tests/test_preview_source.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe -tc=\"*mix*,*Opus*,*sng*,*srb*,*delay*,*Offset*\"","acceptanceCriteria":["mixer guard tests pass bit for bit","Opus fingerprint pinned from the unchanged code passes","sng_delay_ms and the .sng/.srb offset cases pass","no std::vector<DecodedAudio> decoded in mixer.cpp","no per-packet std::vector<float> tmp in decode.cpp","no stop/running/headless() left","full hydra_tests and hydra_uitest --all pass"],"modelTier":"standard"}
```

---

### Task 9: Saving and listing records costs fewer statements, commits and disk syncs

Saving one analyzed chart is slow for five reasons, and each one is small. `add_row` compiles two SQL statements for every path node it writes, when one compile per chart would do. Each chart is saved in three separate commits: the song row (`add_song`), the result (`add_row`) and the dynamics count (`put_dynamics`). No journal mode is set, so every commit pays several full disk syncs. `reindex` commits once per record, which is 18,000 commits on a full library. Before a batch starts, `run_batch` calls `has_record` once per library chart, which compiles 18,000 statements.

Reading is slow in one place. Each library page runs `get_summary` once per visible row, and the `charts` table has no index on its name column, which the page sorts by.

The fixes, one line each. `add_row` compiles its two node statements once per chart. A new `save_analysis` writes the song, the result and the dynamics count in one transaction, and both the batch and the Analyze button use it. The store opens in WAL mode with `synchronous=NORMAL` (the orchestrator's call, not a user decision). `reindex` runs in one transaction with each statement compiled once. A new `analyzed_hashes` answers "which charts are already analyzed?" in one query, and `run_batch` uses it. A new `get_summaries` answers a whole library page in one query, `get_summary` becomes a one-row call of it, and `charts` gets an index on `name`.

WAL, in one line: a commit appends to a side file (`hydra.db-wal`) instead of rewriting pages in place. With `synchronous=NORMAL`, it syncs to disk only at checkpoints, not at every commit. A power cut can lose the last few commits but never corrupts the file, and readers stop blocking the writer (https://www.sqlite.org/wal.html, sections 2 and "Performance Considerations"; https://www.sqlite.org/pragma.html#pragma_synchronous). WAL has one side effect this task must handle. Until a checkpoint, recent commits live in `hydra.db-wal`, so a plain copy of `hydra.db` misses them. `hydra_replay dump` makes exactly such a copy (`snapshot_db` in tools/replay.cpp). This task switches it to SQLite's backup API, which reads through SQLite and gets one consistent snapshot.

What the user sees: nothing, except that saving, reindexing and paging the library are faster. A `hydra.db-wal` and a `hydra.db-shm` file appear next to `hydra.db` while Hydra runs; SQLite removes them when the last connection closes.

**Depends on:** Task 1 (this task is written against Task 1's `add_row`, `add_song`, `reindex`, `get_summary` and `rollback_if_open`, and its tests use Task 1's `exec_on_file` helper).

**Expected overlaps:** Task 3 edits `run_batch` in `src/app/analysis.cpp` (cancel and progress); this task changes its skip loop, `WorkResult` and the consumer's save call. Task 7 and the number-box work (decision 3) edit `AppState::refresh_page` in `src/ui/app_state.cpp`; this task changes its summary loop. Task 4 edits `tools/replay.cpp`; this task changes only `snapshot_db`. Task 10 changes the type of the last parameter of `bind_ready_params`, which this task's new `bind_analyzed_filter` also takes.

**Goal:** A chart saves in one transaction with each statement compiled once, the store runs in WAL mode, reindex commits once, and the batch skip check and the library page each take one query.

**Files:**
- Modify: `src/store/record_store.h`, `src/store/record_store.cpp`
- Modify: `src/app/dynamics_breakdown.h`, `src/app/dynamics_breakdown.cpp`
- Modify: `src/app/analysis.cpp` (`run_batch`)
- Modify: `src/ui/app_state.cpp` (`refresh_page`, `store_finished_analysis`)
- Modify: `tools/replay.cpp` (`snapshot_db`)
- Test: `tests/test_store.cpp`, `tests/test_dynamics_store.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="a file store runs in WAL mode with an index on chart names"` passes.
- [ ] `hydra_tests.exe -tc="save_analysis writes the song, the result and the count together"` passes.
- [ ] `hydra_tests.exe -tc="a save_analysis that fails leaves nothing behind"` passes.
- [ ] `hydra_tests.exe -tc="analyzed_hashes names exactly the charts has_record would skip"` passes.
- [ ] `hydra_tests.exe -tc="get_summaries answers a page the same as get_summary row by row"` passes.
- [ ] `hydra_tests.exe -tc="dynamics_entry_from_analysis counts only when the parse kept 2x kicks"` passes.
- [ ] The whole suite, `hydra_tests.exe`, ends `Status: SUCCESS!`, and `hydra_uitest.exe --all` passes.
- [ ] `hydra_replay.exe dump` on the measurement database prints `(read from a snapshot at` and exits 0.
- [ ] The measurement step reports before and after seconds for a full `hydra_batch` run, a second run that skips everything, and `--reindex`, all on the same machine with nothing else building or testing. The skip pass and the reindex are no slower after.
- [ ] The score-neutral proof prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests.** In `tests/test_store.cpp`, add `#include <unordered_set>` next to `#include <unordered_map>`, and append:

```cpp
// ---- store speed (2026-09-26 audit, Task 9) --------------------------------

TEST_CASE("a file store runs in WAL mode with an index on chart names") {
    // WAL: a commit appends to a log instead of rewriting the file in place,
    // so it costs far fewer disk syncs (https://www.sqlite.org/wal.html).
    // The journal mode is stored in the file, so a fresh connection sees it.
    const std::string path = temp_db("wal");
    std::remove(path.c_str());
    { RecordStore store(path); }
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA journal_mode", -1, &s, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(s) == SQLITE_ROW);
    const std::string mode = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
    sqlite3_finalize(s);
    sqlite3_close(db);
    CHECK(mode == "wal");
    CHECK(scalar(path, "SELECT COUNT(*) FROM sqlite_master"
                       " WHERE type='index' AND name='charts_by_name'") == 1);
    std::remove(path.c_str());
}

TEST_CASE("save_analysis writes the song, the result and the count together") {
    RecordStore store(":memory:");
    const RecordKey key{"h", "mode", CapQuery::at(4)};
    const DynamicsEntry count{DynamicsKey{"h", "Expert", true}, {1, 2, 3}, 7};
    store.save_analysis("h", "Song", "Artist", "Charter", fixture().song,
                        prepare_row(key, at_cap(4)), count);
    CHECK(store.counts() == std::pair<int64_t, int64_t>{1, 1});
    CHECK(store.get_record(key).status == RecordStatus::Ready);
    CHECK(store.get_dynamics(count.key, 7) == std::optional<std::vector<uint8_t>>(count.blob));

    // No count (the parse dropped the 2x kicks): the song and result still land.
    const RecordKey key2{"h2", "mode", CapQuery::at(4)};
    store.save_analysis("h2", "Song 2", "Artist", "Charter", fixture().song,
                        prepare_row(key2, at_cap(4)), std::nullopt);
    CHECK(store.get_record(key2).status == RecordStatus::Ready);
    CHECK(store.counts().first == 2);
}

TEST_CASE("a save_analysis that fails leaves nothing behind") {
    // A trigger that refuses one chart's result makes the save fail after
    // the song row went in. One transaction means the song row goes back out.
    const std::string path = temp_db("save_fail");
    std::remove(path.c_str());
    { RecordStore store(path); }
    exec_on_file(path,
                 "CREATE TRIGGER refuse_boom BEFORE INSERT ON results WHEN NEW.hyhash = 'boom'"
                 " BEGIN SELECT RAISE(ABORT, 'boom'); END;");
    {
        RecordStore store(path);
        const RecordKey boom{"boom", "mode", CapQuery::at(4)};
        CHECK_THROWS(store.save_analysis("boom", "Song", "Artist", "Charter", fixture().song,
                                         prepare_row(boom, at_cap(4)), std::nullopt));
        CHECK(store.counts().first == 0);
        // The store is still usable: no transaction was left open.
        const RecordKey ok{"ok", "mode", CapQuery::at(4)};
        store.save_analysis("ok", "Song", "Artist", "Charter", fixture().song,
                            prepare_row(ok, at_cap(4)), std::nullopt);
        CHECK(store.get_record(ok).status == RecordStatus::Ready);
    }
    std::remove(path.c_str());
}

TEST_CASE("analyzed_hashes names exactly the charts has_record would skip") {
    RecordStore store(":memory:");
    const std::vector<const char*> charts = {"ready", "stale", "other_lens", "other_cap",
                                             "other_mode"};
    for (const char* h : charts) store.add_song(h, h, "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"ready", "mode", CapQuery::at(4)}, at_cap(4));
    PreparedRow stale = prepare_row(RecordKey{"stale", "mode", CapQuery::at(4)}, at_cap(4));
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    store.add_record(RecordKey{"other_lens", "mode", CapQuery::at(4), kLensB}, at_cap(4));
    store.add_record(RecordKey{"other_cap", "mode", CapQuery::at(8)}, at_cap(8));
    store.add_record(RecordKey{"other_mode", "other", CapQuery::at(4)}, at_cap(4));

    for (const CapQuery& cap : {CapQuery::at(4), CapQuery::at(8), CapQuery::automatic()}) {
        const std::unordered_set<std::string> got = store.analyzed_hashes("mode", cap, Lens{});
        for (const char* h : charts) {
            INFO(h);
            CHECK((got.count(h) == 1) == store.has_record(RecordKey{h, "mode", cap}));
        }
    }
    CHECK(store.analyzed_hashes("mode", CapQuery::at(4), Lens{}) ==
          std::unordered_set<std::string>{"ready"});
}

TEST_CASE("get_summaries answers a page the same as get_summary row by row") {
    RecordStore store(":memory:");
    for (const char* h : {"ready", "stale", "none", "two_caps"})
        store.add_song(h, h, "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"ready", "mode", CapQuery::at(4)}, at_cap(4));
    PreparedRow stale = prepare_row(RecordKey{"stale", "mode", CapQuery::at(4)}, at_cap(4));
    stale.hyversion = "0.0.0";
    store.add_row(stale);
    store.add_record(RecordKey{"two_caps", "mode", CapQuery::at(32)}, at_cap(32));
    store.add_record(RecordKey{"two_caps", "mode", CapQuery::at(16)}, at_cap(16));

    // "ready" twice: a page can list the same chart from two folders.
    const std::vector<std::string> page = {"ready", "stale", "none", "two_caps", "ready"};
    for (const CapQuery& cap : {CapQuery::at(4), CapQuery::automatic()}) {
        const std::vector<SummaryLookup> got = store.get_summaries(page, "mode", cap, Lens{});
        REQUIRE(got.size() == page.size());
        for (size_t i = 0; i < page.size(); ++i) {
            INFO(page[i]);
            // Compared with get_record, not get_summary, which this task
            // rebuilds on top of get_summaries. Every Ready row here holds
            // the fixture's paths, so its best path is the fixture's.
            const RecordLookup one = store.get_record(RecordKey{page[i], "mode", cap});
            CHECK(got[i].status == one.status);
            CHECK(got[i].bestpath == (one.status == RecordStatus::Ready
                                          ? fixture().record.best_path().pathstring()
                                          : std::string()));
        }
    }
    CHECK(store.get_summaries({}, "mode", CapQuery::at(4), Lens{}).empty());
}
```

In `tests/test_dynamics_store.cpp`, replace the whole test case `TEST_CASE("store_dynamics_from_analysis stores only when the parse kept 2x kicks")` with:

```cpp
TEST_CASE("dynamics_entry_from_analysis counts only when the parse kept 2x kicks") {
    hydra::Song song = hydra::load_songbytes_mid(
        testmidi::smf(testmidi::concat({testmidi::track_name("PART DRUMS"), testmidi::set_tempo(),
                                        testmidi::note_on(96, 100), testmidi::end_of_track()})),
        true, true);

    CHECK_FALSE(dynamics_entry_from_analysis("nokicks", song, /*bass2x=*/false,
                                             hydra::Difficulty::Expert, true)
                    .has_value());

    const std::optional<DynamicsEntry> entry = dynamics_entry_from_analysis(
        "withkicks", song, /*bass2x=*/true, hydra::Difficulty::Expert, true);
    REQUIRE(entry.has_value());
    CHECK(entry->key.md5 == "withkicks");
    CHECK(entry->key.difficulty == "Expert");
    CHECK(entry->key.pro);
    CHECK(entry->count_version == kDynamicsCountVersion);
    auto bd = decode_dynamics(entry->blob);
    REQUIRE(bd.has_value());
    CHECK(bd->row(DynamicsRow::Kick).all() == 1);
}
```

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors, because `DynamicsEntry`, `save_analysis`, `analyzed_hashes`, `get_summaries` and `dynamics_entry_from_analysis` do not exist.

- [ ] **Step 3: Record the before numbers.** The source is still unchanged, so build the tools from it now: `.\build_cpp.ps1 -Target hydra_batch`. Make sure no other build or test runs on this machine, then run:

```powershell
$out = "$env:TEMP\hydra_t9"; New-Item -ItemType Directory -Force $out | Out-Null
Remove-Item "$out\before.db*" -ErrorAction SilentlyContinue
$full  = Measure-Command { .\build-cpp\Release\hydra_batch.exe --db "$out\before.db" testdata\input | Out-Null }
$skip  = Measure-Command { .\build-cpp\Release\hydra_batch.exe --db "$out\before.db" testdata\input | Out-Null }
$reidx = Measure-Command { .\build-cpp\Release\hydra_batch.exe --db "$out\before.db" --reindex | Out-Null }
"before: full {0:N1}s  skip-pass {1:N1}s  reindex {2:N2}s" -f $full.TotalSeconds, $skip.TotalSeconds, $reidx.TotalSeconds | Tee-Object "$out\before.txt"
```

The full run is mostly search time, so expect it to move little. The skip pass (every chart already analyzed) and the reindex are where the store's own cost shows.

- [ ] **Step 4: WAL, synchronous=NORMAL and the name index.** In `src/store/record_store.cpp`, in the constructor, right after the `if (sqlite3_open(...) != SQLITE_OK) { ... }` block and before the first `exec(`, add:

```cpp
    // WAL journal mode (orchestrator's call, 2026-09-26 audit plan): a commit
    // appends to hydra.db-wal instead of rewriting pages in place, and with
    // synchronous=NORMAL it syncs to disk only at checkpoints, not on every
    // commit. A power cut can lose the last few commits but never corrupts
    // the file, and readers stop blocking the writer
    // (https://www.sqlite.org/wal.html). journal_mode is stored in the file;
    // synchronous is per connection, so both are set on every open. A
    // ":memory:" store answers "memory" and is unaffected.
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
```

Right after Task 1's `if (!has_column("charts", "sig")) exec("ALTER TABLE charts ADD COLUMN sig TEXT");`, add:

```cpp
    // The library page sorts by name (list_chart_library's ORDER BY name).
    exec("CREATE INDEX IF NOT EXISTS charts_by_name ON charts (name)");
```

- [ ] **Step 5: One compile per statement per chart, and one transaction per chart.** Replace the whole of `void RecordStore::add_row(const PreparedRow& row)`, as Task 1 left it, with:

```cpp
void RecordStore::add_row(const PreparedRow& row) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    exec("BEGIN");
    try {
        write_row(row);
        exec("COMMIT");
    } catch (...) {
        rollback_if_open(db_);
        throw;
    }
}

void RecordStore::write_row(const PreparedRow& row) {
    // Deleting a result means deleting its refs first, always: the refs are
    // what keep its paths alive, and the final sweep collects whatever they
    // stopped pointing at. The caller holds the lock and an open transaction,
    // so a failure anywhere leaves the store exactly as it was.
    auto run = [&](Stmt& s, const char* what) {
        if (sqlite3_step(s) != SQLITE_DONE)
            throw std::runtime_error(std::string("add_row ") + what + " failed: " +
                                     sqlite3_errmsg(db_));
    };
    // Deletes the results a subquery names, and their refs. `where` is a
    // fragment over `results`, bound by `bind`.
    auto purge = [&](const std::string& where,
                     const std::function<void(sqlite3_stmt*)>& bind, const char* what) {
        std::string refs = "DELETE FROM path_refs WHERE result_id IN"
                           " (SELECT result_id FROM results WHERE " + where + ")";
        Stmt r = prepare(db_, refs.c_str());
        bind(r);
        run(r, what);

        std::string rows = "DELETE FROM results WHERE " + where;
        Stmt d = prepare(db_, rows.c_str());
        bind(d);
        run(d, what);
    };

    // (1) Anything this chart+mode holds that this build cannot read --
    //     another Hydra version's stamp (which includes every row an old
    //     migration left), an older path layout, a result analyzed under
    //     other rules -- is superseded by a write here. The test is against
    //     what is current, not against this row: a test writing a
    //     deliberately old-stamped row must not take the real rows with it,
    //     and this runs before the insert so the new row is untouched.
    purge("hyhash=? AND chartmode=? AND NOT " + std::string(kRowReadySql),
          [&](sqlite3_stmt* s) {
              bind_text(s, 1, row.hyhash);
              bind_text(s, 2, row.chartmode);
              bind_ready_params(s, 3, rules_fingerprint_);
          },
          "unreadable purge");

    // (2) The row this one replaces, deleted explicitly rather than by
    //     INSERT OR REPLACE: the refs bookkeeping has to be ours, and the
    //     re-insert must take a fresh result_id so Auto sees it as newest.
    purge("hyhash=? AND chartmode=? AND sp_cap=? AND ms_enabled=? AND ms_value=?"
          " AND depth_mode=? AND depth_value=?",
          [&](sqlite3_stmt* s) {
              bind_text(s, 1, row.hyhash);
              bind_text(s, 2, row.chartmode);
              sqlite3_bind_int(s, 3, row.sp_cap);
              bind_lens(s, 4, row.lens);
          },
          "replace purge");

    // (3) The result, then its paths (shared, so first writer wins) and the
    //     refs that tie the two together.
    {
        Stmt s = prepare(db_,
            (std::string("INSERT INTO results "
                         "(hyhash, chartmode, hyversion, sp_cap, ms_enabled, ms_value,"
                         " depth_mode, depth_value, bestpath, structure, ") +
             kSummaryColumnList + ") VALUES (?,?,?,?,?,?,?,?,?,?, ?,?,?,?,?,?,?,?,?)")
                .c_str());
        bind_text(s, 1, row.hyhash);
        bind_text(s, 2, row.chartmode);
        bind_text(s, 3, row.hyversion);
        sqlite3_bind_int(s, 4, row.sp_cap);
        bind_lens(s, 5, row.lens);
        bind_text(s, 9, row.bestpath);
        bind_blob(s, 10, row.structure);
        bind_summary(s, 11, row.summary);
        run(s, "insert");
    }
    const int64_t result_id = sqlite3_last_insert_rowid(db_);

    {
        // Compiled once per row, not once per node, and bound once with what
        // every node shares. Bindings survive a reset, so each node only
        // rebinds its own hash and payload.
        Stmt path_insert = prepare(db_,
            "INSERT OR IGNORE INTO paths (hyhash, chartmode, phash, payload)"
            " VALUES (?,?,?,?)");
        Stmt ref_insert = prepare(db_,
            "INSERT OR IGNORE INTO path_refs (result_id, hyhash, chartmode, phash)"
            " VALUES (?,?,?,?)");
        bind_text(path_insert, 1, row.hyhash);
        bind_text(path_insert, 2, row.chartmode);
        sqlite3_bind_int64(ref_insert, 1, result_id);
        bind_text(ref_insert, 2, row.hyhash);
        bind_text(ref_insert, 3, row.chartmode);
        for (const StoredPathNode& node : row.nodes) {
            bind_text(path_insert, 3, node.hash);
            bind_blob(path_insert, 4, node.payload);
            run(path_insert, "path insert");
            sqlite3_reset(path_insert);
            bind_text(ref_insert, 4, node.hash);
            run(ref_insert, "path ref insert");
            sqlite3_reset(ref_insert);
        }
    }

    // (4) Whatever the replaced row was the last owner of.
    {
        Stmt s = prepare(db_,
            "DELETE FROM paths WHERE hyhash=? AND chartmode=? AND phash NOT IN"
            " (SELECT phash FROM path_refs WHERE hyhash=? AND chartmode=?)");
        bind_text(s, 1, row.hyhash);
        bind_text(s, 2, row.chartmode);
        bind_text(s, 3, row.hyhash);
        bind_text(s, 4, row.chartmode);
        run(s, "path gc");
    }
}
```

Replace the whole of `void RecordStore::add_song(...)`, as Task 1 left it, with:

```cpp
void RecordStore::add_song(const std::string& hyhash, const std::string& ref_name,
                           const std::string& ref_artist, const std::string& ref_charter,
                           const Song& song) {
    // Encoded before the lock: the lock covers sqlite calls only.
    const std::vector<uint8_t> tempomap = encode_tempomap(song);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    upsert_song(hyhash, ref_name, ref_artist, ref_charter, tempomap);
}

void RecordStore::upsert_song(const std::string& hyhash, const std::string& ref_name,
                              const std::string& ref_artist, const std::string& ref_charter,
                              const std::vector<uint8_t>& tempomap) {
    // A chart already registered takes the names this call carries. They
    // come from the scan, so a fixed song.ini reaches the reports on the next
    // analysis (user decision 2026-09-26). The tempo map is keyed by the same
    // content hash, so it cannot have changed and is left alone.
    Stmt s = prepare(db_,
        "INSERT INTO songmeta (hyhash, ref_name, ref_artist, ref_charter, tempomap) "
        "VALUES (?,?,?,?,?) "
        "ON CONFLICT(hyhash) DO UPDATE SET ref_name = excluded.ref_name, "
        "ref_artist = excluded.ref_artist, ref_charter = excluded.ref_charter");
    bind_text(s, 1, hyhash);
    bind_text(s, 2, ref_name);
    bind_text(s, 3, ref_artist);
    bind_text(s, 4, ref_charter);
    bind_blob(s, 5, tempomap);
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string("add_song failed: ") + sqlite3_errmsg(db_));
}
```

Replace the whole of `void RecordStore::put_dynamics(...)` with:

```cpp
void RecordStore::put_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob,
                               int count_version) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    insert_dynamics(key, blob, count_version);
}

void RecordStore::insert_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob,
                                  int count_version) {
    Stmt s = prepare(db_,
        "INSERT OR REPLACE INTO dynamics (md5, difficulty, pro, blob, count_version)"
        " VALUES (?,?,?,?,?)");
    bind_text(s, 1, key.md5);
    bind_text(s, 2, key.difficulty);
    sqlite3_bind_int(s, 3, key.pro ? 1 : 0);
    bind_blob(s, 4, blob);
    sqlite3_bind_int(s, 5, count_version);
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string("put_dynamics failed: ") + sqlite3_errmsg(db_));
}

void RecordStore::save_analysis(const std::string& hyhash, const std::string& ref_name,
                                const std::string& ref_artist, const std::string& ref_charter,
                                const Song& song, const PreparedRow& row,
                                const std::optional<DynamicsEntry>& dynamics) {
    const std::vector<uint8_t> tempomap = encode_tempomap(song);  // not a sqlite call
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    exec("BEGIN");
    try {
        upsert_song(hyhash, ref_name, ref_artist, ref_charter, tempomap);
        write_row(row);
        if (dynamics) {
            // Best effort, inside the same transaction: a failed count write
            // is undone on its own and never costs the result.
            exec("SAVEPOINT dynamics");
            try {
                insert_dynamics(dynamics->key, dynamics->blob, dynamics->count_version);
                exec("RELEASE dynamics");
            } catch (const std::exception&) {
                exec("ROLLBACK TO dynamics");
                exec("RELEASE dynamics");
            }
        }
        exec("COMMIT");
    } catch (...) {
        rollback_if_open(db_);
        throw;
    }
}
```

In `src/store/record_store.h`, add `#include <unordered_set>` after `#include <unordered_map>`. Right after `struct DynamicsKey { ... };`, add:

```cpp
// One dynamics count ready to store: its key, its encoded blob and its count
// stamp (app::kDynamicsCountVersion). Built off the store by
// app::dynamics_entry_from_analysis, saved by RecordStore::save_analysis.
struct DynamicsEntry {
    DynamicsKey key;
    std::vector<uint8_t> blob;
    int count_version = 0;
};
```

In the public writing section, right after `void add_row(const PreparedRow& row);`, add:

```cpp
    // One analyzed chart, saved in one transaction: the song's row (as
    // add_song), the result (as add_row) and, when given, its dynamics count
    // (as put_dynamics). A failure in the first two rolls all of it back. A
    // failed dynamics write is dropped on its own and never blocks the result.
    void save_analysis(const std::string& hyhash, const std::string& ref_name,
                       const std::string& ref_artist, const std::string& ref_charter,
                       const Song& song, const PreparedRow& row,
                       const std::optional<DynamicsEntry>& dynamics);
```

In the private section, after Task 1's `read_tempomap` declaration, add:

```cpp
    // The bodies of add_song, add_row and put_dynamics. The caller holds the
    // lock; write_row also needs an open transaction.
    void upsert_song(const std::string& hyhash, const std::string& ref_name,
                     const std::string& ref_artist, const std::string& ref_charter,
                     const std::vector<uint8_t>& tempomap);
    void write_row(const PreparedRow& row);
    void insert_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob,
                         int count_version);
```

In `src/app/dynamics_breakdown.h`, replace:

```cpp
// After an analysis, store its dynamics counts as a free by-product (the
// chart is already parsed). Stores only when the analysis parsed with bass2x
// on: with it off the parse dropped the 2x kicks and the counts would be
// incomplete. Best effort: a failed save is swallowed so it can never block
// the analysis record.
void store_dynamics_from_analysis(store::RecordStore& store, const std::string& md5,
                                  const Song& song, bool bass2x, Difficulty difficulty, bool pro);
```

with:

```cpp
// After an analysis, its dynamics count as a free by-product (the chart is
// already parsed), ready for RecordStore::save_analysis. nullopt when the
// analysis parsed with bass2x off (the parse dropped the 2x kicks and the
// counts would be incomplete) or the count fails: best effort, never a reason
// to lose the analysis record.
std::optional<store::DynamicsEntry> dynamics_entry_from_analysis(
    const std::string& md5, const Song& song, bool bass2x, Difficulty difficulty, bool pro);
```

In `src/app/dynamics_breakdown.cpp`, replace:

```cpp
void store_dynamics_from_analysis(store::RecordStore& store, const std::string& md5,
                                  const Song& song, bool bass2x, Difficulty difficulty, bool pro) {
    if (!bass2x) return;  // the 2x kicks were dropped; the counts would be incomplete
    try {
        save_dynamics(store, dynamics_store_key(md5, difficulty, pro), count_dynamics(song));
    } catch (...) {
        // Best effort: never block the analysis record.
    }
}
```

with:

```cpp
std::optional<store::DynamicsEntry> dynamics_entry_from_analysis(
    const std::string& md5, const Song& song, bool bass2x, Difficulty difficulty, bool pro) {
    if (!bass2x) return std::nullopt;  // the 2x kicks were dropped; the counts would be incomplete
    try {
        return store::DynamicsEntry{dynamics_store_key(md5, difficulty, pro),
                                    encode_dynamics(count_dynamics(song)), kDynamicsCountVersion};
    } catch (...) {
        // Best effort: never block the analysis record.
        return std::nullopt;
    }
}
```

In `src/app/analysis.cpp`, in `struct WorkResult`, after `std::optional<AnalysisResult> analysis;`, add:

```cpp
    // Counted on the worker, so the consumer only writes.
    std::optional<store::DynamicsEntry> dynamics;
```

In the worker loop, replace:

```cpp
                    wr.row = store::prepare_row(
                        store::RecordKey{item->md5, chartmode, cap, lens}, ar.record);
                    wr.analysis = std::move(ar);
```

with:

```cpp
                    wr.row = store::prepare_row(
                        store::RecordKey{item->md5, chartmode, cap, lens}, ar.record);
                    wr.dynamics = dynamics_entry_from_analysis(
                        item->md5, ar.song, settings.bass2x, settings.difficulty,
                        settings.prodrums);
                    wr.analysis = std::move(ar);
```

In the consumer, replace:

```cpp
            store.add_song(wr.item.md5, wr.item.title, wr.item.artist, wr.item.charter,
                           wr.analysis->song);
            store.add_row(*wr.row);
            store_dynamics_from_analysis(store, wr.item.md5, wr.analysis->song,
                                         settings.bass2x, settings.difficulty,
                                         settings.prodrums);
            if (on_result) on_result(wr.item, *wr.row);
```

with:

```cpp
            store.save_analysis(wr.item.md5, wr.item.title, wr.item.artist, wr.item.charter,
                                wr.analysis->song, *wr.row, wr.dynamics);
            if (on_result) on_result(wr.item, *wr.row);
```

In `src/ui/app_state.cpp`, in `AppState::store_finished_analysis`, replace:

```cpp
        store->add_song(song.md5, song.title, song.artist, song.charter,
                        result.song);
        store->add_record(analyze_job->key(), result.record);
        // Key the dynamics by the settings the job snapshotted, not the
        // current ones: the user may have moved the difficulty box since it
        // started.
        const app::AnalysisSettings& as = analyze_job->settings();
        app::store_dynamics_from_analysis(*store, song.md5, result.song, as.bass2x,
                                          as.difficulty, as.prodrums);
```

with:

```cpp
        // Key the dynamics by the settings the job snapshotted, not the
        // current ones: the user may have moved the difficulty box since it
        // started.
        const app::AnalysisSettings& as = analyze_job->settings();
        store->save_analysis(song.md5, song.title, song.artist, song.charter, result.song,
                             store::prepare_row(analyze_job->key(), result.record),
                             app::dynamics_entry_from_analysis(song.md5, result.song,
                                                               as.bass2x, as.difficulty,
                                                               as.prodrums));
```

- [ ] **Step 6: reindex commits once.** Replace the whole of `int RecordStore::reindex()`, as Task 1 left it, with:

```cpp
int RecordStore::reindex() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    struct Row {
        int64_t result_id;
        std::string hyversion;
        std::vector<uint8_t> structure;
    };
    std::vector<Row> rows;
    {
        Stmt s = prepare(db_, "SELECT result_id, hyversion, structure"
                              " FROM results ORDER BY result_id");
        while (sqlite3_step(s) == SQLITE_ROW)
            rows.push_back({sqlite3_column_int64(s, 0), column_text(s, 1), column_blob(s, 2)});
    }

    // One transaction for the whole pass, and each statement compiled once.
    // This used to commit once per record: 18,000 commits on a full library.
    exec("BEGIN");
    try {
        Stmt nodes_stmt = prepare(db_, kLoadNodesSql);
        Stmt update = prepare(db_,
            "UPDATE results SET score=?,actcount=?,maxskip=?,hardest_ms=?,avgmult=?,"
            "notecount=?,sqin_count=?,sqout_count=?,pathcount=? WHERE result_id=?");
        int done = 0;
        for (const Row& row : rows) {
            // A stale row gets empty summaries: its stored bytes are not this
            // build's to read, so there is nothing to recompute from.
            PathSummary summary;
            if (rank_row(row.hyversion, row.structure, row.result_id, rules_fingerprint_)
                    .ready()) {
                const std::unordered_map<std::string, std::vector<uint8_t>> nodes =
                    load_nodes(nodes_stmt, row.result_id);
                summary = summarize_record(rebuild_record(
                    row.structure,
                    [&nodes](const std::string& hash) -> const std::vector<uint8_t>* {
                        auto it = nodes.find(hash);
                        return it == nodes.end() ? nullptr : &it->second;
                    }));
            }
            ResetOnExit reset{update};
            bind_summary(update, 1, summary);
            sqlite3_bind_int64(update, 10, row.result_id);
            if (sqlite3_step(update) != SQLITE_DONE)
                throw std::runtime_error(std::string("reindex failed: ") + sqlite3_errmsg(db_));
            ++done;
        }
        exec("COMMIT");
        return done;
    } catch (...) {
        rollback_if_open(db_);
        throw;
    }
}
```

- [ ] **Step 7: One query for the batch's skip check.** In `src/store/record_store.cpp`, in the anonymous namespace, right after Task 1's `rollback_if_open`, add:

```cpp
// The rows that make a chart "already analyzed" for a batch run: a readable
// row under exactly this chart mode, cap and lens. Never another lens's row,
// or a batch would skip charts whose stored answer came from a different
// question. has_record and analyzed_hashes share it so the two cannot drift.
// Binds, from `idx`: the chart mode, the ready parameters, the lens, then an
// exact cap when there is one.
std::string analyzed_filter(const CapQuery& cap) {
    std::string sql =
        "chartmode=? AND " + std::string(kRowReadySql) + " AND " + lens_match("");
    if (cap.exact) sql += " AND sp_cap=?";
    else sql += " AND sp_cap>" + std::to_string(kCloneHeroSpCap);
    return sql;
}
int bind_analyzed_filter(sqlite3_stmt* s, int idx, const std::string& chartmode,
                         const CapQuery& cap, const Lens& lens, uint64_t rules_fingerprint) {
    bind_text(s, idx++, chartmode);
    idx = bind_ready_params(s, idx, rules_fingerprint);
    idx = bind_lens(s, idx, lens);
    if (cap.exact) sqlite3_bind_int(s, idx++, *cap.exact);
    return idx;
}
```

Replace the whole of `bool RecordStore::has_record(const RecordKey& key)` with:

```cpp
bool RecordStore::has_record(const RecordKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string sql =
        "SELECT 1 FROM results WHERE hyhash=? AND " + analyzed_filter(key.cap) + " LIMIT 1";
    Stmt s = prepare(db_, sql.c_str());
    bind_text(s, 1, key.hyhash);
    bind_analyzed_filter(s, 2, key.chartmode, key.cap, key.lens, rules_fingerprint_);
    return sqlite3_step(s) == SQLITE_ROW;
}

std::unordered_set<std::string> RecordStore::analyzed_hashes(const std::string& chartmode,
                                                             const CapQuery& cap,
                                                             const Lens& lens) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string sql = "SELECT DISTINCT hyhash FROM results WHERE " + analyzed_filter(cap);
    Stmt s = prepare(db_, sql.c_str());
    bind_analyzed_filter(s, 1, chartmode, cap, lens, rules_fingerprint_);
    std::unordered_set<std::string> out;
    while (sqlite3_step(s) == SQLITE_ROW) out.insert(column_text(s, 0));
    return out;
}
```

In `src/store/record_store.h`, right after the `has_record` declaration, add:

```cpp
    // Every chart has_record would say yes to, for one chart mode, cap and
    // lens, in one query: the batch's skip list for the whole library.
    std::unordered_set<std::string> analyzed_hashes(const std::string& chartmode,
                                                    const CapQuery& cap, const Lens& lens);
```

In `src/app/analysis.cpp`, add `#include <unordered_set>` after `#include <tuple>`, and in `run_batch` replace:

```cpp
    const store::CapQuery cap = store::CapQuery::from_setting(settings.sp_cap);
    std::vector<const ScanItem*> todo;
    for (const ScanItem& item : items) {
        if (!redo && store.has_record(store::RecordKey{item.md5, chartmode, cap, lens}))
            continue;
        todo.push_back(&item);
    }
```

with:

```cpp
    const store::CapQuery cap = store::CapQuery::from_setting(settings.sp_cap);
    // One query for the whole library, not one per chart.
    const std::unordered_set<std::string> analyzed =
        redo ? std::unordered_set<std::string>{} : store.analyzed_hashes(chartmode, cap, lens);
    std::vector<const ScanItem*> todo;
    for (const ScanItem& item : items) {
        if (analyzed.count(item.md5)) continue;
        todo.push_back(&item);
    }
```

- [ ] **Step 8: One query for a library page.** In `src/store/record_store.cpp`, replace the whole of `SummaryLookup RecordStore::get_summary(const RecordKey& key)`, as Task 1 left it, with:

```cpp
SummaryLookup RecordStore::get_summary(const RecordKey& key) {
    return get_summaries({key.hyhash}, key.chartmode, key.cap, key.lens).front();
}

std::vector<SummaryLookup> RecordStore::get_summaries(const std::vector<std::string>& hyhashes,
                                                      const std::string& chartmode,
                                                      const CapQuery& cap, const Lens& lens) {
    std::vector<SummaryLookup> out(hyhashes.size());
    if (hyhashes.empty()) return out;

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string sql =
        "SELECT hyhash, hyversion, bestpath, result_id, substr(structure,1,12)"
        " FROM results WHERE chartmode=? AND hyhash IN (";
    for (size_t i = 0; i < hyhashes.size(); ++i) sql += i ? ",?" : "?";
    sql += ")";
    append_candidate_filter(sql, "", cap);
    Stmt s = prepare(db_, sql.c_str());
    int idx = 1;
    bind_text(s, idx++, chartmode);
    for (const std::string& h : hyhashes) bind_text(s, idx++, h);
    bind_candidate_filter(s, idx, cap, lens);

    WinnerPicker picker;
    std::vector<std::pair<std::string, std::string>> offered;  // hyhash, bestpath
    while (sqlite3_step(s) == SQLITE_ROW) {
        std::string hyhash = column_text(s, 0);
        picker.offer(hyhash, chartmode,
                     rank_row(column_text(s, 1), column_blob(s, 4),
                              sqlite3_column_int64(s, 3), rules_fingerprint_));
        offered.emplace_back(std::move(hyhash), column_text(s, 2));
    }

    // One answer per chart, then handed to every position that asked for it
    // (a page can list one chart twice, from two folders). A stale winner is
    // reported as Stale, not hidden: the library's status column has to tell
    // "analyzed by another build" apart from "never analyzed".
    const std::vector<bool> won = picker.winners();
    std::unordered_map<std::string, SummaryLookup> by_hash;
    for (size_t i = 0; i < offered.size(); ++i) {
        if (!won[i]) continue;
        SummaryLookup& answer = by_hash[offered[i].first];
        if (picker.rank(i).ready()) {
            answer.status = RecordStatus::Ready;
            answer.bestpath = offered[i].second;
        } else {
            answer.status = RecordStatus::Stale;
        }
    }
    for (size_t i = 0; i < hyhashes.size(); ++i) {
        auto it = by_hash.find(hyhashes[i]);
        if (it != by_hash.end()) out[i] = it->second;
    }
    return out;
}
```

In `src/store/record_store.h`, right after the `get_summary` declaration, add:

```cpp
    // get_summary for many charts at once, in one query: one answer per
    // entry of `hyhashes`, in the same order (a repeated hash gets the same
    // answer twice). What a library page asks for.
    std::vector<SummaryLookup> get_summaries(const std::vector<std::string>& hyhashes,
                                             const std::string& chartmode,
                                             const CapQuery& cap, const Lens& lens);
```

In `src/ui/app_state.cpp`, in `AppState::refresh_page`, replace:

```cpp
    current_page.summaries.clear();
    current_page.summaries.reserve(current_page.rows.size());
    for (const store::ChartLibraryEntry& row : current_page.rows) {
        store::SummaryLookup summary = store->get_summary(settings.record_key(row.md5));
        LibraryPage::RowSummary rs;
        rs.state = summary.status;
        rs.bestpath = std::move(summary.bestpath);
        current_page.summaries.push_back(std::move(rs));
    }
```

with:

```cpp
    std::vector<std::string> hashes;
    hashes.reserve(current_page.rows.size());
    for (const store::ChartLibraryEntry& row : current_page.rows) hashes.push_back(row.md5);
    std::vector<store::SummaryLookup> lookups = store->get_summaries(
        hashes, settings.chartmode_key(), settings.cap_query(), settings.lens());
    current_page.summaries.clear();
    current_page.summaries.reserve(lookups.size());
    for (store::SummaryLookup& summary : lookups) {
        LibraryPage::RowSummary rs;
        rs.state = summary.status;
        rs.bestpath = std::move(summary.bestpath);
        current_page.summaries.push_back(std::move(rs));
    }
```

`Settings::record_key` is exactly `RecordKey{hyhash, chartmode_key(), cap_query(), lens()}` (src/app/config.cpp), so the page asks the same question as before.

- [ ] **Step 9: hydra_replay snapshots through SQLite.** In `tools/replay.cpp`, add `#include <sqlite3.h>` after `#include <process.h>` (hydra_core links sqlite3 publicly, so the header is on the include path). Replace:

```cpp
    std::ifstream in(src, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read database: " + src);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error(
            "cannot make a snapshot of the database (cannot write " + dst +
            "); refusing to open the live database " + src);
    out << in.rdbuf();
    if (!out)
        throw std::runtime_error(
            "cannot make a snapshot of the database (copy to " + dst +
            " failed); refusing to open the live database " + src);
    out.close();
```

with:

```cpp
    // SQLite's own backup, not a file copy. The database runs in WAL mode, so
    // recent commits can sit in the -wal file beside it until a checkpoint,
    // and a copy of the main file alone would miss them. The backup reads
    // through SQLite and gets one consistent snapshot, whatever the app is
    // doing (https://www.sqlite.org/backup.html).
    sqlite3* from = nullptr;
    if (sqlite3_open_v2(src.c_str(), &from, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(from);
        throw std::runtime_error("cannot read database: " + src);
    }
    std::remove(dst.c_str());
    sqlite3* to = nullptr;
    if (sqlite3_open(dst.c_str(), &to) != SQLITE_OK) {
        sqlite3_close(to);
        sqlite3_close(from);
        throw std::runtime_error(
            "cannot make a snapshot of the database (cannot write " + dst +
            "); refusing to open the live database " + src);
    }
    sqlite3_backup* backup = sqlite3_backup_init(to, "main", from, "main");
    const int rc = backup ? sqlite3_backup_step(backup, -1) : SQLITE_ERROR;
    sqlite3_backup_finish(backup);
    sqlite3_close(to);
    sqlite3_close(from);
    if (rc != SQLITE_DONE)
        throw std::runtime_error(
            "cannot make a snapshot of the database (copy to " + dst +
            " failed); refusing to open the live database " + src);
```

If nothing else in `tools/replay.cpp` uses `std::ifstream` or `std::ofstream` after this, leave `#include <fstream>` anyway; other modes write files with it (check with `Select-String -Path tools\replay.cpp -Pattern 'fstream'`).

- [ ] **Step 10: Run the tests and watch them pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, including the six new cases. Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test passes.

- [ ] **Step 11: Record the after numbers and prove the scores unchanged.** With nothing else running, build and time the same three runs:

```powershell
.\build_cpp.ps1 -Target hydra_batch; .\build_cpp.ps1 -Target hydra_replay
$out = "$env:TEMP\hydra_t9"
Remove-Item "$out\after.db*" -ErrorAction SilentlyContinue
$full  = Measure-Command { .\build-cpp\Release\hydra_batch.exe --db "$out\after.db" testdata\input | Out-Null }
$skip  = Measure-Command { .\build-cpp\Release\hydra_batch.exe --db "$out\after.db" testdata\input | Out-Null }
$reidx = Measure-Command { .\build-cpp\Release\hydra_batch.exe --db "$out\after.db" --reindex | Out-Null }
"after:  full {0:N1}s  skip-pass {1:N1}s  reindex {2:N2}s" -f $full.TotalSeconds, $skip.TotalSeconds, $reidx.TotalSeconds | Tee-Object "$out\after.txt"
Get-Content "$out\before.txt", "$out\after.txt"
.\build-cpp\Release\hydra_replay.exe dump --chart "testdata\input\common\IB24\T1\Allister - Overrated\notes.mid" --db "$out\after.db"
```

Expected: the skip pass and the reindex are no slower than before (report both lines in the task summary); the `hydra_replay` output starts with `(read from a snapshot at` and the command exits 0. If the skip pass or the reindex is slower, stop and report instead of committing. Then run the score-neutral proof with a fresh database; expected: `Compare-Object` prints nothing.

- [ ] **Step 12: Commit.**

```bash
git add src/store/record_store.h src/store/record_store.cpp src/app/dynamics_breakdown.h src/app/dynamics_breakdown.cpp src/app/analysis.cpp src/ui/app_state.cpp tools/replay.cpp tests/test_store.cpp tests/test_dynamics_store.cpp
git commit -m "Store: WAL, one transaction per chart, one query per page and per batch skip list

Task: Task 9: Saving and listing records costs fewer statements, commits and disk syncs
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/store/record_store.h","src/store/record_store.cpp","src/app/dynamics_breakdown.h","src/app/dynamics_breakdown.cpp","src/app/analysis.cpp","src/ui/app_state.cpp","tools/replay.cpp","tests/test_store.cpp","tests/test_dynamics_store.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe","acceptanceCriteria":["The six named test cases pass","hydra_tests.exe ends Status: SUCCESS! and hydra_uitest.exe --all passes","hydra_replay dump reads a WAL database through a snapshot","Before/after seconds for full batch, skip pass and reindex are reported; skip pass and reindex are no slower","Score-neutral proof prints nothing"],"modelTier":"standard"}
```

---

### Task 10: The Auto budget leaves the rules fingerprint, and the Auto ladder marks only Auto runs Stale

A stored record carries a fingerprint of the rules it ran under: a 64-bit hash of every field in `core::Rules`. The store compares it with the fingerprint of the rules in force, and any difference reads Stale (ADR 0014). Three things are wrong with that today.

The Auto time budget is in the fingerprint. It is a wall-clock limit, so it cannot make a result repeatable anyway, and decision 7 takes it out. The budget also has two homes. The search reads `SearchSettings::time_budget_s`, while the fingerprint hashes `Rules::auto_budget_s`. So a run with no budget at all still stamps "120 s". After this task, `Rules::auto_budget_s` is the only home. It becomes `std::optional<double>`, where nullopt means "run every rung to the end", and `SearchSettings::time_budget_s` is deleted.

The Auto ladder is in the fingerprint of every record, so editing it marks fixed-cap records Stale too, though a fixed-cap run never climbs the ladder. Decision 7 says the ladder marks only Auto records Stale. So a record now carries one of two fingerprints. A fixed-cap run is stamped with `Rules::fingerprint()`, which covers every rule except the ladder and the budget. An Auto run is stamped with the new `Rules::auto_fingerprint()`, which is that plus the ladder. The store accepts either one. A small value, `core::RulesStamp`, carries the pair from the rules to the store, and `RulesStamp::none()` keeps today's "bad rules file, nothing is Ready" gate. The SQL Ready rule compares the stored head against both with `IN (?, ?)`.

The fingerprint is also rebuilt far too often. `HydraRecord`'s default member initializer calls `core::default_rules().fingerprint()`, which formats nine lines of text and hashes them. `rebuild_record` builds a `HydraRecord`, so this runs once for every record decoded. The fix is `core::default_stamp()`, computed once on first use.

What the user sees: editing `auto_budget_s` no longer makes anything Stale. Editing `auto_cap_ladder` marks only results that Auto made as Stale. The UserGuide says both. One more thing, which the merger must know: the fingerprint's text changes, so every stored record reads Stale once after this lands. Task 12's record-format bump asks the user for the same one re-analysis (decision 8), so the two ship in one release and the user re-analyzes once.

**Depends on:** Task 1 (the store code quoted below is Task 1's) and Task 2 (it may merge `analyze_chart`'s two fixed-cap branches; this task then edits the one that remains).

**Expected overlaps:** Task 9 adds `bind_analyzed_filter` in `src/store/record_store.cpp`, which takes the same fingerprint parameter this task retypes; Step 5 covers it. Task 2 edits `analyze_chart` in `src/search/pather.cpp`. Task 4 edits `src/cli/batch.cpp`, `src/cli/report.cpp`, `src/cli/fillcompare.cpp` and `tools/replay.cpp`; this task changes one `open_store`/`RecordStore` line in each. Task 12 edits `HydraRecord` in `src/core/model.h`; this task changes only the `rules_fingerprint` initializer.

**Goal:** The time budget is in no fingerprint and has one home, a ladder edit stales only Auto records, and the default fingerprint is computed once per process.

**Files:**
- Modify: `src/core/rules.h`, `src/core/rules.cpp`
- Modify: `src/core/model.h` (`HydraRecord::rules_fingerprint`)
- Modify: `src/app/rules_file.cpp`
- Modify: `src/search/pather.h`, `src/search/pather.cpp`
- Modify: `src/app/config.h`, `src/app/config.cpp`
- Modify: `src/store/record_store.h`, `src/store/record_store.cpp`
- Modify: `src/ui/app_state.cpp`, `src/cli/batch.cpp`, `src/cli/report.cpp`, `src/cli/fillcompare.cpp`
- Modify: `tools/bench.cpp`, `tools/replay.cpp`
- Modify: `docs/adr/0014-squeeze-out-and-collected-phrases-and-rules-are-stored.md`, `docs/UserGuide.md`
- Test: `tests/test_rules.cpp`, `tests/test_store.cpp`, `tests/test_config.cpp`, `tests/test_report.cpp`, `tests/test_app_state.cpp` (one comment)

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="rules: the time budget is in neither fingerprint"` passes.
- [ ] `hydra_tests.exe -tc="rules: the Auto ladder is in the Auto fingerprint only"` passes.
- [ ] `hydra_tests.exe -tc="rules: the default stamp is built once and matches a fresh record"` passes.
- [ ] `hydra_tests.exe -tc="rules: an Auto run is stamped with the ladder, a fixed cap without it"` passes.
- [ ] `hydra_tests.exe -tc="editing the Auto ladder marks only Auto runs Stale"` passes.
- [ ] The whole suite, `hydra_tests.exe`, ends `Status: SUCCESS!`, and `hydra_uitest.exe --all` passes.
- [ ] `Get-ChildItem -Recurse src,tests,tools -Include *.cpp,*.h | Select-String -Pattern 'time_budget_s'` prints nothing.
- [ ] `Get-ChildItem -Recurse src,tests,tools -Include *.cpp,*.h | Select-String -Pattern 'default_rules\(\)\.fingerprint\(\)'` prints only lines in `tests/`.
- [ ] `.\build_cpp.ps1 -Target hydra_bench; .\build_cpp.ps1 -Target hydra_replay` both build.
- [ ] The score-neutral proof prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests.** Append to `tests/test_rules.cpp`:

```cpp
// ---- the fingerprint's scope (docs/adr/0014, amended 2026-09-26) ----------

TEST_CASE("rules: the time budget is in neither fingerprint") {
    // A wall-clock limit can't make a result repeatable, so changing it must
    // never make a stored record Stale (user decision 7).
    core::Rules r = core::default_rules();
    const uint64_t fixed = r.fingerprint();
    const uint64_t autocap = r.auto_fingerprint();
    r.auto_budget_s = 30.0;
    CHECK(r.fingerprint() == fixed);
    CHECK(r.auto_fingerprint() == autocap);
    r.auto_budget_s = std::nullopt;
    CHECK(r.fingerprint() == fixed);
    CHECK(r.auto_fingerprint() == autocap);
}

TEST_CASE("rules: the Auto ladder is in the Auto fingerprint only") {
    core::Rules r = core::default_rules();
    const uint64_t fixed = r.fingerprint();
    const uint64_t autocap = r.auto_fingerprint();
    CHECK(fixed != autocap);

    r.auto_cap_ladder = {8, 24};
    CHECK(r.fingerprint() == fixed);
    CHECK(r.auto_fingerprint() != autocap);

    // Every other rule is in both.
    core::Rules ties = core::default_rules();
    ties.max_tied_paths = 2;
    CHECK(ties.fingerprint() != fixed);
    CHECK(ties.auto_fingerprint() != autocap);
}

TEST_CASE("rules: the default stamp is built once and matches a fresh record") {
    // HydraRecord's default fingerprint used to re-hash the default rules for
    // every record built, which includes every record decoded.
    CHECK(&core::default_stamp() == &core::default_stamp());
    CHECK(core::default_stamp().fixed == core::default_rules().fingerprint());
    CHECK(core::default_stamp().autocap == core::default_rules().auto_fingerprint());
    CHECK(HydraRecord{}.rules_fingerprint == core::default_stamp().fixed);
    CHECK(core::RulesStamp::none().fixed == core::kNoRulesFingerprint);
    CHECK(core::RulesStamp::none().autocap == core::kNoRulesFingerprint);
}

TEST_CASE("rules: an Auto run is stamped with the ladder, a fixed cap without it") {
    SearchSettings settings;
    settings.rules.auto_cap_ladder = {8};
    settings.rules.auto_budget_s = std::nullopt;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        settings.sp_cap = std::nullopt;
        CHECK(analyze_chart(song, settings).rules_fingerprint ==
              settings.rules.auto_fingerprint());
        settings.sp_cap = 8;
        CHECK(analyze_chart(song, settings).rules_fingerprint == settings.rules.fingerprint());
        settings.sp_cap = 4;
        CHECK(analyze_chart(song, settings).rules_fingerprint == settings.rules.fingerprint());
        break;
    }
}
```

In the same file, in `TEST_CASE("rules: no rules value has the no-rules fingerprint")`, replace:

```cpp
    CHECK(core::default_rules().fingerprint() != core::kNoRulesFingerprint);
    core::Rules other = core::default_rules();
    other.max_tied_paths = 2;
    CHECK(other.fingerprint() != core::kNoRulesFingerprint);
```

with:

```cpp
    CHECK(core::default_rules().fingerprint() != core::kNoRulesFingerprint);
    CHECK(core::default_rules().auto_fingerprint() != core::kNoRulesFingerprint);
    core::Rules other = core::default_rules();
    other.max_tied_paths = 2;
    CHECK(other.fingerprint() != core::kNoRulesFingerprint);
    CHECK(other.auto_fingerprint() != core::kNoRulesFingerprint);
```

In `TEST_CASE("rules: the Auto ladder and budget come from the rules")`, replace:

```cpp
    app::Settings s;
    s.sp_cap = std::nullopt;
    s.rules.auto_budget_s = 30.0;
    CHECK(s.to_analysis_settings().time_budget_s == std::optional<double>(30.0));
    s.sp_cap = 4;
    CHECK_FALSE(s.to_analysis_settings().time_budget_s.has_value());
}
```

with:

```cpp
    // The budget has one home, the rules, and rides along with them.
    app::Settings s;
    s.sp_cap = std::nullopt;
    s.rules.auto_budget_s = 30.0;
    CHECK(s.to_analysis_settings().rules.auto_budget_s == std::optional<double>(30.0));
}
```

Append to `tests/test_store.cpp`:

```cpp
// ---- rules fingerprint scope (2026-09-26 audit, Task 10) -------------------

TEST_CASE("editing the Auto ladder marks only Auto runs Stale") {
    // User decision 7: the ladder only changes what an Auto run does, so a
    // fixed-cap row stays Ready when it changes, and the budget changes
    // nothing at all.
    core::Rules taller = core::default_rules();
    taller.auto_cap_ladder = {16, 32, 64, 128, 256, 512, 1024};
    const RecordKey fixed{"h", "fixed", CapQuery::at(32)};
    const RecordKey autorun{"h", "auto", CapQuery::automatic()};
    const std::string db = temp_db("ladder");
    std::remove(db.c_str());
    {
        RecordStore store(db);
        store.add_song("h", "Song", "Artist", "Charter", fixture().song);
        store.add_record(fixed, at_cap(32));  // the fixture ran at a fixed cap
        HydraRecord auto_run = at_cap(32);
        auto_run.rules_fingerprint = core::default_rules().auto_fingerprint();
        store.add_record(autorun, auto_run);
        CHECK(store.get_record(fixed).status == RecordStatus::Ready);
        CHECK(store.get_record(autorun).status == RecordStatus::Ready);
    }
    {
        RecordStore store(db, core::RulesStamp::of(taller));
        CHECK(store.get_record(fixed).status == RecordStatus::Ready);
        CHECK(store.has_record(fixed));
        const RecordLookup a = store.get_record(autorun);
        CHECK(a.status == RecordStatus::Stale);
        CHECK(a.stale_rules);
        CHECK_FALSE(a.stale_build);
        CHECK_FALSE(store.has_record(autorun));
    }
    {
        core::Rules quicker = core::default_rules();
        quicker.auto_budget_s = 5.0;
        RecordStore store(db, core::RulesStamp::of(quicker));
        CHECK(store.get_record(fixed).status == RecordStatus::Ready);
        CHECK(store.get_record(autorun).status == RecordStatus::Ready);
    }
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(db), ec);
}
```

In `TEST_CASE("a row analyzed under other rules reads Stale until the rules match again")`, replace `RecordStore store(db, other.fingerprint());` with `RecordStore store(db, core::RulesStamp::of(other));`, and replace `RecordStore store(db, core::kNoRulesFingerprint);` with `RecordStore store(db, core::RulesStamp::none());`. In `TEST_CASE("records round-trip through RecordStore across the corpus and config matrix")`, replace:

```cpp
                settings.ms_filter = cfg.ms;
                record = analyze_chart(song, settings);
```

with:

```cpp
                settings.ms_filter = cfg.ms;
                // No budget: every Auto rung runs to the end, so the result
                // never depends on how busy the machine is.
                settings.rules.auto_budget_s = std::nullopt;
                record = analyze_chart(song, settings);
```

In `tests/test_config.cpp`, in `TEST_CASE("to_analysis_settings maps the cap and its Auto budget")`, replace:

```cpp
    // A fixed cap: single run, no time budget.
    s.sp_cap = 16;
    AnalysisSettings a = s.to_analysis_settings();
    CHECK(a.depth_mode == hydra::DepthMode::Points);
    CHECK(a.depth_value == 5000);
    CHECK(a.ms_filter == 20.0);
    CHECK(a.sp_cap == 16);
    CHECK_FALSE(a.time_budget_s.has_value());

    // Auto: the ladder and its budget apply.
    s.sp_cap = std::nullopt;
    a = s.to_analysis_settings();
    CHECK_FALSE(a.sp_cap.has_value());
    CHECK(a.time_budget_s.has_value());
```

with:

```cpp
    // A fixed cap: a single run. The rules still carry the Auto budget; only
    // an Auto run reads it (analyze_chart).
    s.sp_cap = 16;
    AnalysisSettings a = s.to_analysis_settings();
    CHECK(a.depth_mode == hydra::DepthMode::Points);
    CHECK(a.depth_value == 5000);
    CHECK(a.ms_filter == 20.0);
    CHECK(a.sp_cap == 16);

    // Auto: the ladder and its budget come along in the rules.
    s.sp_cap = std::nullopt;
    a = s.to_analysis_settings();
    CHECK_FALSE(a.sp_cap.has_value());
    CHECK(a.rules.auto_budget_s == s.rules.auto_budget_s);
```

In `tests/test_report.cpp`, replace both occurrences of `    settings.time_budget_s = std::nullopt;` with `    settings.rules.auto_budget_s = std::nullopt;`. In `tests/test_app_state.cpp`, replace `    // Bad file: the store is gated on kNoRulesFingerprint, so the same` with `    // Bad file: the store is gated on RulesStamp::none(), so the same`.

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors, because `auto_fingerprint`, `RulesStamp` and `default_stamp` do not exist, `auto_budget_s` cannot take `std::nullopt`, and `RecordStore` takes no `RulesStamp`.

- [ ] **Step 3: Two fingerprints, a stamp, one budget home.** In `src/core/rules.h`, replace:

```cpp
// used, so an absent file changes nothing. A stored record carries
// fingerprint() of the rules it ran under (docs/adr/0014).
```

with:

```cpp
// used, so an absent file changes nothing. A stored record carries a
// fingerprint of the rules it ran under (docs/adr/0014): fingerprint() for a
// fixed-cap run, auto_fingerprint() for an Auto run.
```

Add `#include <optional>` after `#include <cstdint>`. Replace:

```cpp
    // Auto cap: the SP ceilings tried in order, and the seconds before a slow
    // rung is abandoned.
    std::vector<int> auto_cap_ladder{16, 32, 64, 128, 256, 512};
    double auto_budget_s = 120.0;
```

with:

```cpp
    // Auto cap: the SP ceilings tried in order, and the seconds before a slow
    // rung is abandoned. The search reads the budget here and nowhere else;
    // nullopt runs every rung to the end (what tests use, so their results
    // stay deterministic). A fixed-cap run reads neither.
    std::vector<int> auto_cap_ladder{16, 32, 64, 128, 256, 512};
    std::optional<double> auto_budget_s = 120.0;
```

Replace:

```cpp
    // A 64-bit hash of every field above. Equal rules give equal fingerprints
    // in every build; any changed field gives a different one. Never
    // kNoRulesFingerprint.
    uint64_t fingerprint() const;
};

// The defaults above, as one shared value.
const Rules& default_rules();
```

with:

```cpp
    // A 64-bit hash of every field that can change a fixed-cap run's answer:
    // every field above except auto_cap_ladder and auto_budget_s. Equal rules
    // give equal fingerprints in every build; any changed field gives a
    // different one. Never kNoRulesFingerprint.
    uint64_t fingerprint() const;
    // fingerprint()'s fields plus auto_cap_ladder: what an Auto run is
    // stamped with, since only an Auto run climbs the ladder. The budget is
    // in neither: a wall-clock limit can't make a result repeatable anyway.
    // Never kNoRulesFingerprint.
    uint64_t auto_fingerprint() const;
};

// The defaults above, as one shared value.
const Rules& default_rules();

// The two fingerprints a store accepts as "these rules": a fixed-cap run's
// and an Auto run's (docs/adr/0014, amended 2026-09-26). none() accepts
// nothing, so a store gated on it (a bad hydra_rules.ini) reads no row as
// Ready.
struct RulesStamp {
    uint64_t fixed = kNoRulesFingerprint;
    uint64_t autocap = kNoRulesFingerprint;
    static RulesStamp of(const Rules& rules) {
        return RulesStamp{rules.fingerprint(), rules.auto_fingerprint()};
    }
    static RulesStamp none() { return RulesStamp{}; }
};

// default_rules()'s stamp, computed once on first use.
const RulesStamp& default_stamp();
```

In `src/core/rules.cpp`, replace everything from `uint64_t Rules::fingerprint() const {` through the end of `const Rules& default_rules() { ... }` with:

```cpp
namespace {

// Every field that can change a fixed-cap run's answer, one line each. The
// Auto ladder and the Auto budget are not here: a fixed-cap run never climbs
// the ladder, and the budget is a wall-clock limit no fingerprint can make
// repeatable (docs/adr/0014, amended 2026-09-26).
std::string fixed_cap_text(const Rules& r) {
    std::string text;
    add_line(text, "backend_leeway_ms", r.backend_leeway_ms);
    text += r.sqout_rule == SqOutRule::WholeChord ? "sqout_rule=whole_chord\n"
                                                  : "sqout_rule=first_note\n";
    add_line(text, "max_tied_paths", r.max_tied_paths);
    add_line(text, "fill_cooldown_measures", r.fill_cooldown_measures);
    add_line(text, "fill_max_distance_beats", r.fill_max_distance_beats);
    add_line(text, "fill_length_measures", r.fill_length_measures);
    add_line(text, "fill_land_slop_beats", r.fill_land_slop_beats);
    return text;
}

// 0 is reserved for "no usable rules"; a hash that lands on it moves off.
uint64_t hash_rules_text(const std::string& text) {
    const uint64_t h = fnv1a64(text);
    return h == kNoRulesFingerprint ? 1 : h;
}

}  // namespace

uint64_t Rules::fingerprint() const { return hash_rules_text(fixed_cap_text(*this)); }

uint64_t Rules::auto_fingerprint() const {
    std::string text = fixed_cap_text(*this);
    text += "auto_cap_ladder=";
    for (int cap : auto_cap_ladder) text += std::to_string(cap) + ",";
    text += "\n";
    return hash_rules_text(text);
}

const Rules& default_rules() {
    static const Rules rules;
    return rules;
}

const RulesStamp& default_stamp() {
    static const RulesStamp stamp = RulesStamp::of(default_rules());
    return stamp;
}
```

In `src/app/rules_file.cpp`, replace:

```cpp
        else if (key == "auto_budget_s") {
            r.auto_budget_s = to_double(where, key, v, 0.0);
            if (r.auto_budget_s == 0.0) bad(where, key, v, "above zero");
        }
```

with:

```cpp
        else if (key == "auto_budget_s") {
            const double budget = to_double(where, key, v, 0.0);
            if (budget == 0.0) bad(where, key, v, "above zero");
            r.auto_budget_s = budget;
        }
```

In `src/core/model.h`, replace:

```cpp
    // core::Rules::fingerprint() of the rules the search ran under (blob v6,
    // path structure v4). A record built in memory starts with the default
    // rules' fingerprint; analyze_chart stamps the real one. An older blob
    // reads back core::kNoRulesFingerprint, which matches no rules, so it can
    // never pass as current.
    uint64_t rules_fingerprint = core::default_rules().fingerprint();
```

with:

```cpp
    // The fingerprint of the rules the search ran under (blob v6, path
    // structure v4): Rules::fingerprint() for a fixed-cap run,
    // Rules::auto_fingerprint() for an Auto run. A record built in memory
    // starts with the default rules' fixed-cap fingerprint, computed once
    // (core::default_stamp), not once per record decoded; analyze_chart
    // stamps the real one. An older blob reads back core::kNoRulesFingerprint,
    // which matches no rules, so it can never pass as current.
    uint64_t rules_fingerprint = core::default_stamp().fixed;
```

- [ ] **Step 4: The search reads the budget from the rules and stamps the right fingerprint.** In `src/search/pather.h`, delete:

```cpp
    // Auto only: seconds before a too-slow ladder rung is abandoned
    // (auto_budget_s in hydra_rules.ini). nullopt runs every rung to completion.
    std::optional<double> time_budget_s;
```

and replace `// self-settling ladder (settings.time_budget_s applies only there). Throws` with `// self-settling ladder (settings.rules.auto_budget_s applies only there). Throws`. In `src/search/pather.cpp`, in `analyze_chart`, replace:

```cpp
    HydraRecord record = analyze_auto_cap(song, depth_mode, depth_value, ms_filter,
                                          settings.legacy_fill_deadline, settings.rules,
                                          /*want_allzero=*/true, on_progress,
                                          settings.time_budget_s);
    record.rules_fingerprint = settings.rules.fingerprint();
    return record;
```

with:

```cpp
    HydraRecord record = analyze_auto_cap(song, depth_mode, depth_value, ms_filter,
                                          settings.legacy_fill_deadline, settings.rules,
                                          /*want_allzero=*/true, on_progress,
                                          settings.rules.auto_budget_s);
    // An Auto run climbed the ladder, so its answer depends on it too.
    record.rules_fingerprint = settings.rules.auto_fingerprint();
    return record;
```

Leave the fixed-cap branches' `record.rules_fingerprint = settings.rules.fingerprint();` as they are (after Task 2 there may be one such line instead of two). In `src/app/config.cpp`, in `Settings::to_analysis_settings`, delete:

```cpp
    // Bound the Auto ladder so a heavy chart can't hang the app for minutes.
    // The budget is auto_budget_s in hydra_rules.ini (the user's choice). A
    // fixed cap is a single run and needs no budget.
    s.time_budget_s = sp_cap ? std::nullopt : std::optional<double>(rules.auto_budget_s);
```

The budget now arrives through `s.rules = rules;` on the next line.

- [ ] **Step 5: The store accepts either fingerprint.** In `src/store/record_store.cpp`, replace Task 1's `structure_is_current`:

```cpp
// Is this row's stored path tree in the layout this build reads, analyzed
// under the rules this process runs? Takes the whole blob or just the
// substr(structure,1,12) a query selected.
bool structure_is_current(const std::vector<uint8_t>& structure_head,
                          uint64_t rules_fingerprint) {
    if (structure_head.size() < kStructureHeadBytes) return false;
    const std::vector<uint8_t> want = structure_head_for(rules_fingerprint);
    return std::equal(want.begin(), want.end(), structure_head.begin());
}
```

with:

```cpp
// Is this row's stored path tree in the layout this build reads, analyzed
// under the rules this process runs, as a fixed-cap run or as an Auto run?
// Takes the whole blob or just the substr(structure,1,12) a query selected.
bool structure_is_current(const std::vector<uint8_t>& structure_head,
                          const core::RulesStamp& rules) {
    if (structure_head.size() < kStructureHeadBytes) return false;
    for (uint64_t fingerprint : {rules.fixed, rules.autocap}) {
        const std::vector<uint8_t> want = structure_head_for(fingerprint);
        if (std::equal(want.begin(), want.end(), structure_head.begin())) return true;
    }
    return false;
}
```

Replace Task 1's `rank_row` signature line `                   int64_t result_id, uint64_t rules_fingerprint) {` and its body's `structure_is_current(structure_head, rules_fingerprint)` so the function reads:

```cpp
Candidate rank_row(const std::string& hyversion, const std::vector<uint8_t>& structure_head,
                   int64_t result_id, const core::RulesStamp& rules) {
    return Candidate{hyversion == current_record_version(),
                     structure_is_current(structure_head, rules), result_id};
}
```

Replace Task 1's `stale_reasons` with:

```cpp
StaleReasons stale_reasons(const std::string& hyversion,
                           const std::vector<uint8_t>& structure_head,
                           const core::RulesStamp& rules) {
    // The first four bytes are the layout; either fingerprint's head has the
    // same four, so the fixed one serves to read them.
    const std::vector<uint8_t> want = structure_head_for(rules.fixed);
    const bool layout_current =
        structure_head.size() >= kStructureHeadBytes &&
        std::equal(want.begin(), want.begin() + 4, structure_head.begin());
    StaleReasons why;
    why.build = hyversion != current_record_version() || !layout_current;
    why.rules = layout_current && !structure_is_current(structure_head, rules);
    return why;
}
```

Replace Task 1's SQL rule and its binder:

```cpp
// Two bound parameters: the current version text, then the 12-byte structure
// head (format + rules fingerprint). bind_ready_params binds them and returns
// the next free index.
constexpr const char* kRowReadySql = "(hyversion = ? AND substr(structure,1,12) = ?)";

int bind_ready_params(sqlite3_stmt* s, int idx, uint64_t rules_fingerprint) {
    bind_text(s, idx, current_record_version());
    bind_blob(s, idx + 1, structure_head_for(rules_fingerprint));
    return idx + 2;
}
```

with:

```cpp
// Three bound parameters: the current version text, then the two 12-byte
// structure heads this store accepts (format + the fixed-cap fingerprint,
// format + the Auto fingerprint). bind_ready_params binds them and returns
// the next free index.
constexpr const char* kRowReadySql =
    "(hyversion = ? AND substr(structure,1,12) IN (?, ?))";

int bind_ready_params(sqlite3_stmt* s, int idx, const core::RulesStamp& rules) {
    bind_text(s, idx, current_record_version());
    bind_blob(s, idx + 1, structure_head_for(rules.fixed));
    bind_blob(s, idx + 2, structure_head_for(rules.autocap));
    return idx + 3;
}
```

If Task 9 has merged, also change `bind_analyzed_filter`'s last parameter from `uint64_t rules_fingerprint` to `const core::RulesStamp& rules`, and its `bind_ready_params(s, idx, rules_fingerprint)` call to `bind_ready_params(s, idx, rules)`.

Replace the constructor's first two lines:

```cpp
RecordStore::RecordStore(const std::string& dbpath, uint64_t rules_fingerprint)
    : rules_fingerprint_(rules_fingerprint) {
```

with:

```cpp
RecordStore::RecordStore(const std::string& dbpath, core::RulesStamp rules_fingerprint)
    : rules_fingerprint_(rules_fingerprint) {
```

Every other use of `rules_fingerprint_` in the file passes it straight to one of the four functions above, so none of those lines change. In `src/store/record_store.h`, replace:

```cpp
    // rules_fingerprint: core::Rules::fingerprint() of the rules this process
    // runs under. A row stamped with any other fingerprint reads Stale.
    // core::kNoRulesFingerprint (a bad hydra_rules.ini) makes every row Stale.
    explicit RecordStore(const std::string& dbpath,
                         uint64_t rules_fingerprint = core::default_rules().fingerprint());
```

with:

```cpp
    // rules_fingerprint: core::RulesStamp::of() the rules this process runs
    // under. A row stamped with neither of its two fingerprints reads Stale.
    // core::RulesStamp::none() (a bad hydra_rules.ini) makes every row Stale.
    explicit RecordStore(const std::string& dbpath,
                         core::RulesStamp rules_fingerprint = core::default_stamp());
```

and replace:

```cpp
    // The fingerprint of the rules this process runs under; a row stamped
    // with any other reads Stale.
    uint64_t rules_fingerprint_;
```

with:

```cpp
    // The two fingerprints of the rules this process runs under (fixed-cap
    // and Auto); a row stamped with neither reads Stale. Computed once, when
    // the store opens.
    core::RulesStamp rules_fingerprint_;
```

- [ ] **Step 6: Every caller hands the store a stamp.** In `src/app/config.h`, replace:

```cpp
std::unique_ptr<store::RecordStore> open_store(
    const std::string& db, uint64_t rules_fingerprint = core::default_rules().fingerprint());
```

with:

```cpp
std::unique_ptr<store::RecordStore> open_store(
    const std::string& db, core::RulesStamp rules = core::default_stamp());
```

and in the comment above it replace `// Opens the store at `db`. rules_fingerprint gates Ready: a row analyzed` with `// Opens the store at `db`. `rules` gates Ready: a row analyzed`. In `src/app/config.cpp`, replace Task 1's:

```cpp
std::unique_ptr<store::RecordStore> open_store(const std::string& db,
                                               uint64_t rules_fingerprint) {
    return std::make_unique<store::RecordStore>(db, rules_fingerprint);
}
```

with:

```cpp
std::unique_ptr<store::RecordStore> open_store(const std::string& db,
                                               core::RulesStamp rules) {
    return std::make_unique<store::RecordStore>(db, rules);
}
```

In `src/ui/app_state.cpp`, replace:

```cpp
// A bad rules file gates the store on kNoRulesFingerprint, so no row reads
// Ready under the defaults the settings still hold.
AppState::AppState(StartupSettings start)
    : AppState(start.settings,
               app::open_store(app::db_path(),
                               start.rules_error.empty()
                                   ? start.settings.rules.fingerprint()
                                   : core::kNoRulesFingerprint)) {
```

with:

```cpp
// A bad rules file gates the store on RulesStamp::none(), so no row reads
// Ready under the defaults the settings still hold.
AppState::AppState(StartupSettings start)
    : AppState(start.settings,
               app::open_store(app::db_path(),
                               start.rules_error.empty()
                                   ? core::RulesStamp::of(start.settings.rules)
                                   : core::RulesStamp::none())) {
```

In `src/cli/batch.cpp` and `src/cli/report.cpp`, replace `settings.rules.fingerprint()` in the `open_store(` line with `hydra::core::RulesStamp::of(settings.rules)`. In `src/cli/fillcompare.cpp`, do the same on both `open_store(` lines. In `tools/replay.cpp`, replace `store::RecordStore store(snapshot_path, s.rules.fingerprint());` with `store::RecordStore store(snapshot_path, core::RulesStamp::of(s.rules));`. In `tools/bench.cpp`, replace `store::RecordStore store(":memory:", rules.fingerprint());` with `store::RecordStore store(":memory:", core::RulesStamp::of(rules));`, replace `store = std::make_unique<store::RecordStore>(dbpath, g_rules.fingerprint());` with `store = std::make_unique<store::RecordStore>(dbpath, core::RulesStamp::of(g_rules));`, and replace `store::RecordStore db(dbpath, g_rules.fingerprint());` with `store::RecordStore db(dbpath, core::RulesStamp::of(g_rules));`. In `corpus_bench` in the same file, replace:

```cpp
        settings.rules = g_rules;
```

with:

```cpp
        settings.rules = g_rules;
        // No budget, as before: every Auto rung runs to the end, so the
        // timing is of the search and not of a wall-clock cut-off.
        settings.rules.auto_budget_s = std::nullopt;
```

- [ ] **Step 7: Amend ADR 0014 and the UserGuide.** Append to `docs/adr/0014-squeeze-out-and-collected-phrases-and-rules-are-stored.md`:

```
## Amendment, 2026-09-26: the Auto budget and the Auto ladder

The first version put every rules field into one fingerprint, so any edit to
hydra_rules.ini made the whole library Stale. Two fields were over-reach
(user decision 7 of the 2026-09-26 audit plan).

The Auto time budget is a wall-clock limit. Two runs under the same budget
can settle on different rungs on a busy machine, so the fingerprint could
never promise a repeatable answer for it. It is in no fingerprint now. It
also had two homes: the search read `SearchSettings::time_budget_s` while the
fingerprint hashed `Rules::auto_budget_s`, so a run with no budget still
stamped 120 s. `Rules::auto_budget_s` is now the only home, and nullopt means
no budget.

The Auto ladder only changes what an Auto run does. A record now carries one
of two fingerprints: `Rules::fingerprint()` (every rule except the ladder and
the budget) for a fixed-cap run, and `Rules::auto_fingerprint()` (that plus
the ladder) for an Auto run. The store accepts either (`core::RulesStamp`),
in C++ (`structure_is_current`) and in SQL (`kRowReadySql`, now
`IN (?, ?)`). A ladder edit marks only Auto runs Stale.

The fingerprint's text changed, so every stored record reads Stale once more
after this lands. It ships with the record-format bump of the same plan,
which asks for the same one re-analysis.
```

In `docs/UserGuide.md`, replace:

```
- **`auto_budget_s`** (default `120`): how many seconds Auto may spend on one chart before it stops climbing the ladder.
```

with:

```
- **`auto_budget_s`** (default `120`): how many seconds Auto may spend on one chart before it stops climbing the ladder. Changing it never marks a result stale.
```

and replace:

```
Every analysis result remembers the rules it was made with. After you change the file, results made under the old rules show **`(Stale)`** until you re-analyze them. Switching the rules back brings those results back.
```

with:

```
Every analysis result remembers the rules it was made with. After you change the file, results made under the old rules show **`(Stale)`** until you re-analyze them. Switching the rules back brings those results back. Two keys are exceptions. `auto_cap_ladder` only changes what Auto does, so editing it marks only results Auto made as stale; results at a fixed SP cap stay. `auto_budget_s` never marks anything stale, because a time limit can't make a result repeatable anyway.
```

- [ ] **Step 8: Run everything and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, with the five new cases included. Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all`; expected: every test passes. Build `hydra_bench` and `hydra_replay`; expected: both build. Run the two greps from the acceptance list; expected: the first prints nothing, the second prints only `tests/` lines. Run the score-neutral proof; expected: `Compare-Object` prints nothing.

- [ ] **Step 9: Commit.**

```bash
git add src/core/rules.h src/core/rules.cpp src/core/model.h src/app/rules_file.cpp src/search/pather.h src/search/pather.cpp src/app/config.h src/app/config.cpp src/store/record_store.h src/store/record_store.cpp src/ui/app_state.cpp src/cli/batch.cpp src/cli/report.cpp src/cli/fillcompare.cpp tools/bench.cpp tools/replay.cpp docs/adr/0014-squeeze-out-and-collected-phrases-and-rules-are-stored.md docs/UserGuide.md tests/test_rules.cpp tests/test_store.cpp tests/test_config.cpp tests/test_report.cpp tests/test_app_state.cpp
git commit -m "Rules: budget leaves the fingerprint, the Auto ladder stales only Auto runs

Task: Task 10: The Auto budget leaves the rules fingerprint, and the Auto ladder marks only Auto runs Stale
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/core/rules.h","src/core/rules.cpp","src/core/model.h","src/app/rules_file.cpp","src/search/pather.h","src/search/pather.cpp","src/app/config.h","src/app/config.cpp","src/store/record_store.h","src/store/record_store.cpp","src/ui/app_state.cpp","src/cli/batch.cpp","src/cli/report.cpp","src/cli/fillcompare.cpp","tools/bench.cpp","tools/replay.cpp","docs/adr/0014-squeeze-out-and-collected-phrases-and-rules-are-stored.md","docs/UserGuide.md","tests/test_rules.cpp","tests/test_store.cpp","tests/test_config.cpp","tests/test_report.cpp","tests/test_app_state.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe","acceptanceCriteria":["The five named test cases pass","hydra_tests.exe ends Status: SUCCESS! and hydra_uitest.exe --all passes","No time_budget_s left in src, tests, tools","default_rules().fingerprint() appears only in tests","hydra_bench and hydra_replay build","Score-neutral proof prints nothing"],"modelTier":"standard"}
```

---

### Task 11: The Paths tab and the Preview stop rebuilding every frame

At 60 frames a second, the Paths tab rebuilds everything it shows. Each frame it builds the path list, every row's pathstring and ms cell, every activation's rating, the multiplier squeezes, the score breakdown and the stored-result lines. It also builds the selected path's verbose string on the chance that Ctrl+C is pressed. The Preview tab builds its overlay key (that same verbose string) every frame. The highway renderer copies every visible note, each with its own vector, every frame. And picking another path while the Preview is open rebuilds the Preview scene on the UI thread, which hitches the window on a big chart. Under all of this, each `Path::all_activations()` call deep-copies every activation with all its vectors, and the library report makes three to four such copies per path.

This task caches the built views and rebuilds them only when their inputs move. Think of it like a printed menu: you reprint it when the dishes change, not every time a customer looks. The Paths tab's views live in a new `app::PathsTabCache` keyed on the record's generation, the selected path and the two display settings the ratings read. Ctrl+C builds the verbose string only when pressed. The Preview's overlay key is built only when the selected path or the record changes. The highway renderer reads a window over its own notes instead of copying them. A path switch builds the new scene on a background job, and the old overlay stays up until the new one is ready. Last, the loops that walked a path's activations through `all_activations()` in the report, the Paths tab, the Preview scene and the store's path summary switch to the non-copying walk T2 adds.

The rules (`hydra_rules.ini`) are not part of the cache key, because they only change when Hydra restarts.

What the user sees: nothing, except that switching paths with the Preview open no longer hitches; the new overlay lands a few frames later instead.

**Depends on:** T2 (the non-copying walk over a path's activations), T5 (it edits report.cpp's row loop first), T6 (it rewrites `render_details_modal`, `render_preview_panel` and `PreviewController::poll`, and creates `tests/test_preview_controller.cpp`).

**Expected overlaps:** T12 runs in the same wave and changes where the multiplier squeezes are stored. Both edit `build_multsqueezes` callers: this task moves the call from `render_multsqueeze_section` into `PathsTabCache::details`, and T12 changes what `build_multsqueezes` takes. Whichever merges second passes T12's argument in `PathsTabCache::details`. T9 and T12 both edit `src/store/record_store.cpp`; this task changes only the first two lines of `summarize_path`. T5 already edited `collect_rows` in `src/app/report.cpp`; this task changes only its `all_activations()` loop. If T2's own commit already switched any of the four loops named in Step 7, skip that one.

**Goal:** a Paths or Preview frame with nothing changed builds no view, no string and no activation copy.

**Files:**
- Modify: `src/app/path_view.h`, `src/app/path_view.cpp`, `src/ui/app_state.h`, `src/ui/details_view.cpp`, `src/ui/preview_controller.h`, `src/ui/preview_controller.cpp`, `src/ui/preview_load_job.h`, `src/ui/preview_load_job.cpp`, `src/render/track_state.h`, `src/render/track_state.cpp`, `src/render/highway_draw.cpp`, `src/app/report.cpp`, `src/app/preview_view.cpp`, `src/store/record_store.cpp`
- Test: `tests/test_path_view.cpp`, `tests/test_track_state.cpp`, `tests/test_preview_controller.cpp`, `tests/ui/uitest_tests.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="PathsTabCache*"` passes: 5 frames build the list once, a new record generation rebuilds it, and the details rebuild only on a path, generation, hit-window or backend-limit change.
- [ ] `hydra_tests.exe -tc="PathsTabCache: 600*"` passes and prints `600 rebuilt frames: A ms; 600 cached frames: B ms` with B under a tenth of A. Record both numbers in the task report.
- [ ] `hydra_tests.exe -tc="window: a view*"` passes: an instant in the window is the state's own object, at the same address.
- [ ] `hydra_tests.exe -tc="switching paths builds the new overlay off the UI thread"` passes.
- [ ] `hydra_uitest.exe --all` passes, `preview-path-overlay`, `preview-controls` and `analyze` included.
- [ ] `Select-String -Path src\app\report.cpp,src\app\path_view.cpp,src\app\preview_view.cpp,src\store\record_store.cpp -Pattern 'all_activations\(\)'` prints nothing.
- [ ] `Select-String -Path src\ui\details_view.cpp -Pattern 'pathstring_verbose'` prints exactly two lines: the Copy button and the Ctrl+C branch.
- [ ] The score-neutral batch comparison prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe; .\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all` → `Status: SUCCESS!`, then `[PASS]` on every GUI test.

**Steps:**

- [ ] **Step 1: Write the failing tests.** In `tests/test_path_view.cpp`, add `#include <chrono>` after `#include <map>`, and append:

```cpp
TEST_CASE("PathsTabCache: views are built once and rebuilt only when their inputs move") {
    const AnalysisResult& ar = analyzed();
    const HydraRecord& rec = ar.record;
    const SongTiming& timing = ar.song.timing();
    PathsTabCache cache;

    // The list: once per record generation, however many frames ask.
    for (int frame = 0; frame < 5; ++frame) cache.list(rec, 7);
    CHECK(cache.list_builds() == 1);
    const PathListView& list = cache.list(rec, 8);  // the record was re-read
    CHECK(cache.list_builds() == 2);

    // Every listed row carries the path's own label and ms cell.
    for (const PathGroupView& g : list.groups)
        for (const Path* p : g.paths) {
            CHECK(cache.row(p).label == p->pathstring());
            CHECK(cache.row(p).cell.ms == build_path_row(*p).ms);
        }

    // The details: once per (path, record, hit window, backend limit).
    const Path& best = rec.best_path();
    for (int frame = 0; frame < 5; ++frame)
        cache.details(best, rec, 8, &timing, 70.0, std::nullopt, core::default_rules());
    CHECK(cache.details_builds() == 1);
    cache.details(best, rec, 8, &timing, 71.0, std::nullopt, core::default_rules());
    CHECK(cache.details_builds() == 2);
    cache.details(best, rec, 8, &timing, 71.0, 30.0, core::default_rules());
    CHECK(cache.details_builds() == 3);
    cache.details(best, rec, 9, &timing, 71.0, 30.0, core::default_rules());
    CHECK(cache.details_builds() == 4);
    std::vector<const Path*> all = rec.all_paths();
    if (all.size() > 1) {
        cache.details(*all[1], rec, 9, &timing, 71.0, 30.0, core::default_rules());
        CHECK(cache.details_builds() == 5);
    }

    // What the cache hands back is what a fresh build gives.
    const PathsTabCache::Details& d =
        cache.details(best, rec, 9, &timing, 71.0, 30.0, core::default_rules());
    CHECK(d.breakdown == build_score_breakdown(best));
    CHECK(d.squeezes.size() == build_multsqueezes(best).size());
    CHECK(d.activations.acts.size() ==
          build_activations(best, rec, &timing, 71.0, 30.0).acts.size());

    // The stored-result lines: once per record generation.
    store::RecordLookup lookup;
    lookup.status = store::RecordStatus::Ready;
    lookup.record = rec;
    for (int frame = 0; frame < 5; ++frame) cache.status(lookup, 9);
    CHECK(cache.status_builds() == 1);
    CHECK(cache.status(lookup, 9).lines == build_record_status(lookup).lines);
}

TEST_CASE("PathsTabCache: 600 cached frames cost far less than 600 rebuilds") {
    using clock = std::chrono::steady_clock;
    const AnalysisResult& ar = analyzed();
    const HydraRecord& rec = ar.record;
    const SongTiming& timing = ar.song.timing();
    const Path& best = rec.best_path();

    // What a Paths frame did before: every view, every row label.
    const clock::time_point t0 = clock::now();
    for (int frame = 0; frame < 600; ++frame) {
        PathListView list = build_path_list(rec);
        for (const Path* p : rec.all_paths()) {
            std::string label = p->pathstring();
            PathRowView row = build_path_row(*p);
        }
        std::vector<MultSqueezeView> sq = build_multsqueezes(best);
        ActivationsView acts = build_activations(best, rec, &timing, 70.0);
        std::vector<std::string> bd = build_score_breakdown(best);
    }
    const clock::time_point t1 = clock::now();
    PathsTabCache cache;
    for (int frame = 0; frame < 600; ++frame) {
        cache.list(rec, 1);
        cache.details(best, rec, 1, &timing, 70.0, std::nullopt, core::default_rules());
    }
    const clock::time_point t2 = clock::now();

    const double rebuilt_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double cached_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    MESSAGE("600 rebuilt frames: " << rebuilt_ms << " ms; 600 cached frames: " << cached_ms
                                   << " ms");
    CHECK(cached_ms * 10.0 < rebuilt_ms);
}
```

In `tests/test_track_state.cpp`, change the three `std::vector<TrackInstant> ... = st.window(...)` declarations in "window: strict bounds, and a synthesized instant when empty" (`w`, `empty`, `before`) and the two in "make_toggle_bounds: covers [near, far], merges equal neighbours" (`w`, `mid`) to `TrackWindow` (for example `TrackWindow w = st.window(1.0, 3.0);`). Then append:

```cpp
// The renderer asks for this window every frame. It used to be a vector of
// copies, each instant with its own vector of notes.
TEST_CASE("window: a view into the state, not a copy") {
    PreviewScene scene = timed_scene();
    scene.notes = {note(1000.0, PreviewLane::Red), note(2000.0, PreviewLane::Red),
                   note(3000.0, PreviewLane::Red)};
    TrackState st = build_track_state(scene, TrackStateOptions{});

    TrackWindow w = st.window(0.5, 3.5);
    const TrackInstant* in_state = find(st.instants(), 2.0);
    REQUIRE(in_state != nullptr);
    const TrackInstant* in_window = nullptr;
    for (const TrackInstant& i : w)
        if (i.t == doctest::Approx(2.0)) in_window = &i;
    CHECK(in_window == in_state);

    // Walking it backwards reaches the same objects.
    CHECK(&*w.rbegin() == &w[w.size() - 1]);
}
```

In `tests/test_preview_controller.cpp` (created by T6), add `#include "app/analysis.h"` and `#include "app/preview_view.h"` to the includes. In "with no audio device the Preview still loads, muted, with a warning", change `pc.open(entry_for(chart_with_audio()), true, true, Difficulty::Expert, nullptr, 4);` to `pc.open(entry_for(chart_with_audio()), true, true, Difficulty::Expert, nullptr, "", 4);`. Append:

```cpp
// Picking another path with the Preview open used to rebuild the scene inside
// open(), on the UI thread. It now builds on a job and lands on a later poll().
TEST_CASE("switching paths builds the new overlay off the UI thread") {
    using namespace hydra;
    using namespace hydra::app;
    AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 2;
    settings.ms_filter = 10.0;
    std::string chart;
    std::optional<AnalysisResult> analyzed;
    for (const std::string& p : corpus::chart_paths()) {
        try {
            AnalysisResult r = analyze_chart_file(p, settings);
            if (!r.record.paths.empty()) {
                chart = p;
                analyzed.emplace(std::move(r));
                break;
            }
        } catch (const std::exception&) {
        }
    }
    REQUIRE(analyzed.has_value());
    const Path& best = analyzed->record.best_path();
    const std::string best_key = path_overlay_key(&best);

    PreviewController pc(nullptr, nullptr);
    pc.open(entry_for(chart), true, true, Difficulty::Expert, nullptr, "", 4);
    for (int i = 0; i < 1200 && pc.loading(); ++i) {
        pc.poll();
        Sleep(50);
    }
    REQUIRE_FALSE(pc.loading());
    const std::string before = pc.overlay_path_key();

    // open() returns at once: the old overlay is still up, and nothing reloads.
    pc.open(entry_for(chart), true, true, Difficulty::Expert, &best, best_key, 4);
    CHECK(pc.overlay_path_key() == before);
    CHECK_FALSE(pc.loading());

    // The new overlay lands on a later poll.
    for (int i = 0; i < 1200 && pc.overlay_path_key().rfind(best_key, 0) != 0; ++i) {
        pc.poll();
        Sleep(10);
    }
    CHECK(pc.overlay_path_key().rfind(best_key, 0) == 0);
}
```

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors, because `PathsTabCache` and `TrackWindow` do not exist and `PreviewController::open` takes no path key.

- [ ] **Step 3: The Paths tab cache.** In `src/app/path_view.h`, add `#include <unordered_map>` after `#include <string>`, and add before the closing `}  // namespace hydra::app`:

```cpp
// ---- the Paths tab's views, kept between frames -----------------------------

// The Paths tab's views, built once and kept until what they show changes.
// The tab used to rebuild all of them every frame (60 times a second): the
// list, every row's pathstring, every activation's rating. Each view here is
// rebuilt only when its inputs move: the record (by its generation number),
// the selected path, or the two display settings the ratings read. The rules
// are left out of the key because they only change when Hydra restarts.
// Pointers inside point into the record, like build_path_list's.
class PathsTabCache {
public:
    struct Row {
        std::string label;  // the path's pathstring
        PathRowView cell;   // the right-aligned ms cell
    };
    struct Details {
        std::vector<MultSqueezeView> squeezes;
        ActivationsView activations;
        std::vector<std::string> breakdown;
    };

    // The stored-result panel's lines for `lookup`.
    const RecordStatusView& status(const store::RecordLookup& lookup, int record_generation);
    // The path list for `record`, with every listed path's row.
    const PathListView& list(const HydraRecord& record, int record_generation);
    // The row of a path in the last list() (the all-0 section included).
    const Row& row(const Path* path) const;
    // The selected path's squeezes, activations and score breakdown.
    const Details& details(const Path& path, const HydraRecord& record, int record_generation,
                           const SongTiming* timing, double hit_window_ms,
                           std::optional<double> backend_limit_ms, const core::Rules& rules);

    // How many times each view was built; for tests.
    int status_builds() const { return status_builds_; }
    int list_builds() const { return list_builds_; }
    int details_builds() const { return details_builds_; }

private:
    int status_generation_ = -1;
    RecordStatusView status_;
    int status_builds_ = 0;

    int list_generation_ = -1;
    PathListView list_;
    std::unordered_map<const Path*, Row> rows_;
    int list_builds_ = 0;

    int details_generation_ = -1;
    const Path* details_path_ = nullptr;
    double details_hit_window_ms_ = 0.0;
    std::optional<double> details_backend_limit_ms_;
    Details details_;
    int details_builds_ = 0;
};
```

In `src/app/path_view.cpp`, add at the end of the namespace:

```cpp
const RecordStatusView& PathsTabCache::status(const store::RecordLookup& lookup,
                                               int record_generation) {
    if (record_generation != status_generation_) {
        status_ = build_record_status(lookup);
        status_generation_ = record_generation;
        ++status_builds_;
    }
    return status_;
}

const PathListView& PathsTabCache::list(const HydraRecord& record, int record_generation) {
    if (record_generation != list_generation_) {
        list_ = build_path_list(record);
        rows_.clear();
        auto add_row = [this](const Path* p) {
            rows_[p] = Row{p->pathstring(), build_path_row(*p)};
        };
        for (const PathGroupView& g : list_.groups)
            for (const Path* p : g.paths) add_row(p);
        for (const Path* p : list_.allzero) add_row(p);
        list_generation_ = record_generation;
        ++list_builds_;
    }
    return list_;
}

const PathsTabCache::Row& PathsTabCache::row(const Path* path) const {
    return rows_.at(path);
}

const PathsTabCache::Details& PathsTabCache::details(
    const Path& path, const HydraRecord& record, int record_generation,
    const SongTiming* timing, double hit_window_ms, std::optional<double> backend_limit_ms,
    const core::Rules& rules) {
    if (record_generation != details_generation_ || &path != details_path_ ||
        hit_window_ms != details_hit_window_ms_ ||
        backend_limit_ms != details_backend_limit_ms_) {
        details_.squeezes = build_multsqueezes(path);
        details_.activations =
            build_activations(path, record, timing, hit_window_ms, backend_limit_ms, rules);
        details_.breakdown = build_score_breakdown(path);
        details_generation_ = record_generation;
        details_path_ = &path;
        details_hit_window_ms_ = hit_window_ms;
        details_backend_limit_ms_ = backend_limit_ms;
        ++details_builds_;
    }
    return details_;
}
```

In `src/ui/app_state.h`, add `#include "app/path_view.h"` after `#include "app/dynamics_breakdown.h"`, and inside `struct DetailsViewState`, after `std::string store_error;`, add:

```cpp
    // The Paths tab's built views, kept between frames (app::PathsTabCache).
    app::PathsTabCache paths_tab;
    // The Preview overlay's key for selected_path (app::path_overlay_key),
    // built when the selection or the record changes instead of every frame.
    std::string overlay_key;
    const Path* overlay_key_path = nullptr;
    int overlay_key_generation = -1;
```

- [ ] **Step 4: Draw the Paths tab from the cache.** In `src/ui/details_view.cpp`, in `render_record_status`, change `    app::RecordStatusView status = app::build_record_status(app.viewed);` to `    const app::RecordStatusView& status = app.details_ui.paths_tab.status(app.viewed, app.record_generation.n);`.

Change `render_multsqueeze_section`'s first lines:

```cpp
void render_multsqueeze_section(const Path* path) {
    if (begin_section("Multiplier squeezes")) {
        std::vector<app::MultSqueezeView> squeezes = app::build_multsqueezes(*path);
        if (squeezes.empty()) {
```

to:

```cpp
void render_multsqueeze_section(const std::vector<app::MultSqueezeView>& squeezes) {
    if (begin_section("Multiplier squeezes")) {
        if (squeezes.empty()) {
```

and in the same function change the comment `// Keyed on the index, not the element address: the view-model` / `// vector is rebuilt per frame, so its addresses are unstable.` to `// Keyed on the index, not the element address: the cached vector` / `// is rebuilt whenever the path changes, so its addresses move.`

Change `render_activations_section`'s signature and first lines:

```cpp
void render_activations_section(const Path* path, const HydraRecord& record,
                                const SongTiming* timing,
                                const Settings& settings) {
    if (begin_section("Activations")) {
        app::ActivationsView view = app::build_activations(
            *path, record, timing,
            static_cast<double>(settings.hit_window_ms),
            settings.backend_limit(), settings.rules);

        if (view.acts.empty()) ImGui::TextDisabled("None.");
```

to:

```cpp
void render_activations_section(const app::ActivationsView& view) {
    if (begin_section("Activations")) {
        if (view.acts.empty()) ImGui::TextDisabled("None.");
```

Change `render_score_breakdown_section`:

```cpp
void render_score_breakdown_section(const Path* path) {
    if (begin_section("Score breakdown")) {
        for (const std::string& line : app::build_score_breakdown(*path))
            ImGui::TextUnformatted(line.c_str());
```

to:

```cpp
void render_score_breakdown_section(const std::vector<std::string>& breakdown) {
    if (begin_section("Score breakdown")) {
        for (const std::string& line : breakdown) ImGui::TextUnformatted(line.c_str());
```

Change `render_path_details`:

```cpp
void render_path_details(const Path* path, const HydraRecord& record,
                         const SongTiming* timing, const Settings& settings,
                         double& copied_at) {
```

to:

```cpp
void render_path_details(const Path* path, const app::PathsTabCache::Details& details,
                         double& copied_at) {
```

and its last three lines:

```cpp
    render_multsqueeze_section(path);
    render_activations_section(path, record, timing, settings);
    render_score_breakdown_section(path);
```

to:

```cpp
    render_multsqueeze_section(details.squeezes);
    render_activations_section(details.activations);
    render_score_breakdown_section(details.breakdown);
```

(The Copy button still builds `pathstring_verbose()` only when clicked.)

Change `render_path_row`:

```cpp
void render_path_row(const Path* p, const Path*& selected_path) {
```

to:

```cpp
void render_path_row(const Path* p, const app::PathsTabCache::Row& row,
                     const Path*& selected_path) {
```

and inside it change:

```cpp
        if (row_selectable(p->pathstring().c_str(), p == selected_path))
            selected_path = p;

        app::PathRowView row = app::build_path_row(*p);
        if (!row.ms.empty()) {
            ImGui::TableSetColumnIndex(1);
            if (row.warn) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
            ImGui::TextUnformatted(row.ms.c_str());
            if (row.warn) ImGui::PopStyleColor();
        }
```

to:

```cpp
        if (row_selectable(row.label.c_str(), p == selected_path))
            selected_path = p;

        if (!row.cell.ms.empty()) {
            ImGui::TableSetColumnIndex(1);
            if (row.cell.warn) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
            ImGui::TextUnformatted(row.cell.ms.c_str());
            if (row.cell.warn) ImGui::PopStyleColor();
        }
```

In `render_path_panel`, change:

```cpp
    ImGui::BeginChild("pathlist", ImVec2(px(600), 0), ImGuiChildFlags_Borders);
    app::PathListView list = app::build_path_list(*app.viewed.record);
```

to:

```cpp
    app::PathsTabCache& cache = app.details_ui.paths_tab;
    ImGui::BeginChild("pathlist", ImVec2(px(600), 0), ImGuiChildFlags_Borders);
    const app::PathListView& list = cache.list(*app.viewed.record, app.record_generation.n);
```

change both `render_path_row(p, selected_path);` calls to `render_path_row(p, cache.row(p), selected_path);`, and change:

```cpp
    if (selected_path)
        render_path_details(selected_path, *app.viewed.record,
                           app.viewed.timing ? &*app.viewed.timing : nullptr,
                           app.settings, app.details_ui.copied_at);
```

to:

```cpp
    if (selected_path) {
        const app::PathsTabCache::Details& details = cache.details(
            *selected_path, *app.viewed.record, app.record_generation.n,
            app.viewed.timing ? &*app.viewed.timing : nullptr,
            static_cast<double>(app.settings.hit_window_ms), app.settings.backend_limit(),
            app.settings.rules);
        render_path_details(selected_path, details, app.details_ui.copied_at);
    }
```

In `render_details_modal`, change:

```cpp
    std::string copytext = selected_path ? selected_path->pathstring_verbose() : "";
    if (!copytext.empty() && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
        ImGui::SetClipboardText(copytext.c_str());
```

to:

```cpp
    // Built only when the chord is pressed, not every frame just in case.
    if (selected_path && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
        ImGui::SetClipboardText(selected_path->pathstring_verbose().c_str());
```

- [ ] **Step 5: Build the Preview overlay key once, and the new scene on a job.** In `src/ui/details_view.cpp`, inside `render_preview_panel`, change:

```cpp
    pc->open(*app.selected, app.settings.view_prodrums, app.settings.effective_bass2x(),
             app.settings.difficulty(), selected_path, sp_cap, app.settings.rules);
```

to:

```cpp
    // The overlay key is the path's verbose string: rebuilt when the
    // selection or the record changes, not every frame.
    DetailsViewState& ui = app.details_ui;
    if (selected_path != ui.overlay_key_path ||
        app.record_generation.n != ui.overlay_key_generation) {
        ui.overlay_key = hydra::app::path_overlay_key(selected_path);
        ui.overlay_key_path = selected_path;
        ui.overlay_key_generation = app.record_generation.n;
    }
    pc->open(*app.selected, app.settings.view_prodrums, app.settings.effective_bass2x(),
             app.settings.difficulty(), selected_path, ui.overlay_key, sp_cap,
             app.settings.rules);
```

In `src/ui/preview_load_job.h`, add `#include <memory>` after `#include <atomic>`, and before the closing `}  // namespace hydra::ui` add:

```cpp
// Rebuilds the Preview scene for a new path overlay off the UI thread. The
// song is shared with the controller, read-only, so nothing is re-parsed and
// the audio is left alone. `key` is the overlay key the scene is built for.
class PreviewSceneJob : public ResultJobBase {
public:
    PreviewSceneJob(std::shared_ptr<const Song> song, std::optional<Path> path, int sp_cap,
                    core::Rules rules, std::string key);
    ~PreviewSceneJob() { shutdown(); }

    void start();
    // Valid once finished() && ok(); moves the scene out (call once).
    app::PreviewScene take_scene();
    const std::string& key() const { return key_; }

private:
    void run();

    std::shared_ptr<const Song> song_;
    std::optional<Path> path_;
    int sp_cap_;
    core::Rules rules_;
    std::string key_;
    std::optional<app::PreviewScene> scene_;
};
```

In `src/ui/preview_load_job.cpp`, add before the closing `}  // namespace hydra::ui`:

```cpp
PreviewSceneJob::PreviewSceneJob(std::shared_ptr<const Song> song, std::optional<Path> path,
                                 int sp_cap, core::Rules rules, std::string key)
    : song_(std::move(song)),
      path_(std::move(path)),
      sp_cap_(sp_cap),
      rules_(std::move(rules)),
      key_(std::move(key)) {}

void PreviewSceneJob::start() { spawn([this] { run(); }); }

void PreviewSceneJob::run() {
    run_guarded([this] {
        throw_if_cancelled();  // a newer selection already replaced this one
        scene_ = app::build_preview_scene(*song_, path_ ? &*path_ : nullptr, sp_cap_, rules_);
        return true;
    });
}

app::PreviewScene PreviewSceneJob::take_scene() { return std::move(*scene_); }
```

In `src/ui/preview_controller.h`, add `#include <vector>` after `#include <string>`, forward-declare `class PreviewSceneJob;` next to `class PreviewLoadJob;`, and change the `open` declaration and its comment:

```cpp
    // (Re)start the preview for `entry`. Called every frame the Preview tab is
    // shown. `path` (may be null) supplies the path overlay; it is copied, so
    // the caller's Path need not outlive the call. Already open for the same
    // chart and the same path and SP cap: a no-op. Same chart, different path
    // or a changed `sp_cap`: the overlay is swapped in place off the retained
    // song — no re-parse, no audio re-decode, playback position untouched.
    void open(const store::ChartLibraryEntry& entry, bool pro, bool bass2x,
              Difficulty difficulty, const Path* path, int sp_cap,
              const core::Rules& rules = core::default_rules());
```

to:

```cpp
    // (Re)start the preview for `entry`. Called every frame the Preview tab is
    // shown. `path` (may be null) supplies the path overlay; it is copied, so
    // the caller's Path need not outlive the call. `path_key` is
    // app::path_overlay_key(path), which the caller builds once per selection
    // (it is too heavy to build per frame). Already open for the same chart,
    // path key and SP cap: a no-op. Same chart, different path or cap: the new
    // overlay is built on a background job off the retained song and swapped
    // in by a later poll() — no re-parse, no audio re-decode, playback
    // position untouched; the old overlay stays up until then.
    void open(const store::ChartLibraryEntry& entry, bool pro, bool bass2x,
              Difficulty difficulty, const Path* path, const std::string& path_key,
              int sp_cap, const core::Rules& rules = core::default_rules());
```

In its private section, change:

```cpp
    std::optional<Song> song_;
    std::optional<Path> path_;
    std::string path_key_;        // key of path_ + sp_cap_
```

to:

```cpp
    std::shared_ptr<const Song> song_;  // shared read-only with scene jobs
    std::optional<Path> path_;
    std::string requested_path_key_;  // the path half of path_key_, as open() got it
    std::string path_key_;        // key of path_ + sp_cap_
    // The overlay being built for a new selection, and replaced ones still
    // finishing (dropped by poll() once done, so replacing one never joins
    // its thread on the UI thread).
    std::unique_ptr<PreviewSceneJob> scene_job_;
    std::vector<std::unique_ptr<PreviewSceneJob>> retired_scene_jobs_;
    void start_scene_job();
```

In `src/ui/preview_controller.cpp`, add `#include <algorithm>` after `#include <cstdint>`. Change the `overlay_key` helper:

```cpp
std::string overlay_key(const Path* path, int sp_cap) {
    return hydra::app::path_overlay_key(path) + "|cap" + std::to_string(sp_cap);
}
```

to:

```cpp
std::string overlay_key(const std::string& path_key, int sp_cap) {
    return path_key + "|cap" + std::to_string(sp_cap);
}
```

Replace the start of `open` through the end of its same-chart branch:

```cpp
void PreviewController::open(const store::ChartLibraryEntry& entry, bool pro,
                             bool bass2x, Difficulty difficulty,
                             const Path* path, int sp_cap,
                             const core::Rules& rules) {
    rules_ = rules;
    if (active_ && open_key_ == entry.md5) {
        std::string key = overlay_key(path, sp_cap);
        if (key == path_key_) return;  // same chart, same overlay: nothing to do
        path_ = path ? std::optional<Path>(*path) : std::nullopt;
        sp_cap_ = sp_cap;
        path_key_ = std::move(key);
        // Mid-load the job is building its own scene; poll() reconciles. Once
        // the song is here the overlay is rebuilt on the spot, which leaves the
        // audio and the playhead alone.
        if (!job_ && song_ && !song_->is_empty()) {
            scene_ = hydra::app::build_preview_scene(*song_, path, sp_cap_, rules_);
            scene_path_key_ = path_key_;
            scene_dirty_ = true;
        }
        return;
    }
    close();

    open_key_ = entry.md5;
    active_ = true;
    error_.clear();
    pro_ = pro;
    sp_cap_ = sp_cap;

    path_ = path ? std::optional<Path>(*path) : std::nullopt;
    path_key_ = overlay_key(path, sp_cap_);
```

with:

```cpp
void PreviewController::open(const store::ChartLibraryEntry& entry, bool pro,
                             bool bass2x, Difficulty difficulty,
                             const Path* path, const std::string& path_key,
                             int sp_cap, const core::Rules& rules) {
    rules_ = rules;
    if (active_ && open_key_ == entry.md5) {
        // Same chart, same overlay: nothing to do, and nothing built.
        if (sp_cap == sp_cap_ && path_key == requested_path_key_) return;
        path_ = path ? std::optional<Path>(*path) : std::nullopt;
        sp_cap_ = sp_cap;
        requested_path_key_ = path_key;
        path_key_ = overlay_key(path_key, sp_cap);
        // Mid-load the load job is building its own scene; poll() reconciles.
        // Once the song is here, the new overlay builds on a job and poll()
        // swaps it in, which leaves the audio and the playhead alone.
        if (!job_ && song_ && !song_->is_empty()) start_scene_job();
        return;
    }
    close();

    open_key_ = entry.md5;
    active_ = true;
    error_.clear();
    pro_ = pro;
    sp_cap_ = sp_cap;

    path_ = path ? std::optional<Path>(*path) : std::nullopt;
    requested_path_key_ = path_key;
    path_key_ = overlay_key(path_key, sp_cap_);
```

Add after `open`:

```cpp
void PreviewController::start_scene_job() {
    if (scene_job_) {
        scene_job_->cancel();
        retired_scene_jobs_.push_back(std::move(scene_job_));
    }
    scene_job_ = std::make_unique<PreviewSceneJob>(song_, path_, sp_cap_, rules_, path_key_);
    scene_job_->start();
}
```

In `close()`, change:

```cpp
    job_.reset();  // ResultJobBase's shutdown() joins the worker
```

to:

```cpp
    job_.reset();  // ResultJobBase's shutdown() joins the worker
    scene_job_.reset();
    retired_scene_jobs_.clear();
    requested_path_key_.clear();
```

In `poll()`, change its first line and the song handoff:

```cpp
void PreviewController::poll() {
    if (!job_ || !job_->finished()) return;
```

to:

```cpp
void PreviewController::poll() {
    // Replaced overlay builds that have finished can go.
    retired_scene_jobs_.erase(
        std::remove_if(retired_scene_jobs_.begin(), retired_scene_jobs_.end(),
                       [](const std::unique_ptr<PreviewSceneJob>& j) { return j->finished(); }),
        retired_scene_jobs_.end());
    // A new selection's overlay is ready: swap it in.
    if (scene_job_ && scene_job_->finished()) {
        if (scene_job_->ok()) {
            scene_ = scene_job_->take_scene();
            scene_path_key_ = scene_job_->key();
            scene_dirty_ = true;
        } else if (error_.empty()) {
            error_ = scene_job_->error();
        }
        scene_job_.reset();
    }

    if (!job_ || !job_->finished()) return;
```

and change `        song_ = std::move(result.song);` to `        song_ = std::make_shared<const Song>(std::move(result.song));`. At the end of `poll()`, change:

```cpp
    if (ok && scene_path_key_ != path_key_) {
        scene_ = hydra::app::build_preview_scene(*song_, path_ ? &*path_ : nullptr, sp_cap_,
                                                 rules_);
        scene_path_key_ = path_key_;
        scene_dirty_ = true;
    }
```

to:

```cpp
    if (ok && scene_path_key_ != path_key_) start_scene_job();
```

In `tests/ui/uitest_tests.cpp`, inside `test_preview_path_overlay`, the swap now lands a few frames later. Change:

```cpp
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
    IM_CHECK(!h.app->preview->loading());  // swapped in place, not reloaded
    IM_CHECK_FLOAT_NEAR_EQ(h.app->preview->position_ms(), held, 1.0);
    IM_CHECK_EQ(h.app->preview->overlay_path_key().rfind(other_key, 0), (size_t)0);
```

to:

```cpp
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
    IM_CHECK(!h.app->preview->loading());  // swapped in place, not reloaded
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->preview->overlay_path_key().rfind(other_key, 0) == 0;
    }, 10));
    IM_CHECK_FLOAT_NEAR_EQ(h.app->preview->position_ms(), held, 1.0);
```

and change:

```cpp
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
    IM_CHECK(!h.app->preview->loading());
    IM_CHECK_STR_EQ(h.app->preview->overlay_path_key().c_str(), first_overlay.c_str());
```

to:

```cpp
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
    IM_CHECK(!h.app->preview->loading());
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->preview->overlay_path_key() == first_overlay;
    }, 10));
```

- [ ] **Step 6: The highway reads a window, not copies.** In `src/render/track_state.h`, add `#include <cstddef>` and `#include <iterator>` after `#include <optional>`. Right before `class TrackState {`, add:

```cpp
// The instants one frame draws, near < t < far, as a view into the track
// state: nothing is copied. When no instant falls inside, it holds one
// synthesized instant at the window's midpoint instead (Onyx's zoomMap
// fallback). Valid while the TrackState it came from is alive and unchanged.
class TrackWindow {
public:
    TrackWindow(const TrackInstant* first, const TrackInstant* last)
        : first_(first), last_(last) {}
    explicit TrackWindow(TrackInstant synthesized) : synth_(std::move(synthesized)) {}

    const TrackInstant* begin() const { return synth_ ? &*synth_ : first_; }
    const TrackInstant* end() const { return synth_ ? &*synth_ + 1 : last_; }
    std::reverse_iterator<const TrackInstant*> rbegin() const {
        return std::reverse_iterator<const TrackInstant*>(end());
    }
    std::reverse_iterator<const TrackInstant*> rend() const {
        return std::reverse_iterator<const TrackInstant*>(begin());
    }
    size_t size() const { return static_cast<size_t>(end() - begin()); }
    bool empty() const { return size() == 0; }
    const TrackInstant& front() const { return *begin(); }
    const TrackInstant& operator[](size_t i) const { return begin()[i]; }

private:
    const TrackInstant* first_ = nullptr;
    const TrackInstant* last_ = nullptr;
    std::optional<TrackInstant> synth_;
};
```

In the same file, change:

```cpp
    std::vector<TrackInstant> window(double near_s, double far_s) const;

    // Onyx's makeToggleBounds over one span field: consecutive spans covering
    // [near, far] with that field on or off, adjacent equal states merged.
    std::vector<ToggleSpan> make_toggle_bounds(const std::vector<TrackInstant>& win,
```

to:

```cpp
    TrackWindow window(double near_s, double far_s) const;

    // Onyx's makeToggleBounds over one span field: consecutive spans covering
    // [near, far] with that field on or off, adjacent equal states merged.
    std::vector<ToggleSpan> make_toggle_bounds(const TrackWindow& win,
```

In `src/render/track_state.cpp`, replace `TrackState::window`:

```cpp
std::vector<TrackInstant> TrackState::window(double near_s, double far_s) const {
    std::vector<TrackInstant> out;
    auto lo = std::upper_bound(instants_.begin(), instants_.end(), near_s,
                               [](double t, const TrackInstant& i) { return t < i.t; });
    for (auto it = lo; it != instants_.end() && it->t < far_s; ++it) out.push_back(*it);
    // Onyx writes the synthesized key as `t1 + (t2 + t1) / 2`, which lands
    // past t2; it never reads that key, only the state (its neighbours'
    // before/after). We place it at the midpoint so the state is evaluated
    // strictly inside the window, which is the same state Onyx carries.
    if (out.empty()) out.push_back(synthesize((near_s + far_s) / 2.0));
    return out;
}
```

with:

```cpp
TrackWindow TrackState::window(double near_s, double far_s) const {
    auto lo = std::upper_bound(instants_.begin(), instants_.end(), near_s,
                               [](double t, const TrackInstant& i) { return t < i.t; });
    auto hi = lo;
    while (hi != instants_.end() && hi->t < far_s) ++hi;
    // Onyx writes the synthesized key as `t1 + (t2 + t1) / 2`, which lands
    // past t2; it never reads that key, only the state (its neighbours'
    // before/after). We place it at the midpoint so the state is evaluated
    // strictly inside the window, which is the same state Onyx carries.
    if (lo == hi) return TrackWindow(synthesize((near_s + far_s) / 2.0));
    const TrackInstant* base = instants_.data();
    return TrackWindow(base + (lo - instants_.begin()), base + (hi - instants_.begin()));
}
```

and change `std::vector<ToggleSpan> TrackState::make_toggle_bounds(const std::vector<TrackInstant>& win,` to `std::vector<ToggleSpan> TrackState::make_toggle_bounds(const TrackWindow& win,`. Its body only uses `empty()`, `front()` and a range-for, which `TrackWindow` has.

In `src/render/highway_draw.cpp`, change `    const std::vector<TrackInstant> win = state.window(near_t, far_t);` to `    const TrackWindow win = state.window(near_t, far_t);`. The range-for loops and the two `win.rbegin()` / `win.rend()` loops compile unchanged.

- [ ] **Step 7: Walk activations without copying.** T2 adds a way to visit a path's activations in order (the activations, then the variant tail, as `all_activations()` returns them) without copying them. T2 names it `Path::walk_activations()`, which returns an `ActivationWalk` (a read-only range with `size()`, `empty()`, `front()`, `back()`, `operator[]` and range-for). Use it at these four sites, in the form `const ActivationWalk acts = path.walk_activations();`. Keep each loop body and the order exactly as they are.

In `src/app/report.cpp`, inside `collect_rows`:

```cpp
            for (const Activation& a : path->all_activations())
                if (a.e_offset.has_value() && a.skips.has_value()) {
```

In `src/app/path_view.cpp`, inside `build_activations`:

```cpp
    for (const Activation& act : path.all_activations()) {
```

In `src/app/preview_view.cpp`, inside `build_preview_scene`:

```cpp
        for (const Activation& a : path->all_activations()) {
```

In `src/store/record_store.cpp`, `summarize_path` copies the list once and uses it twice, for the count and for the loop:

```cpp
    std::vector<Activation> acts = path.all_activations();

    s.score = path.totalscore();
    s.actcount = static_cast<int>(acts.size());
```

Here, count the activations as `path.activations.size() + path.variant_tail.size()` (the same count `all_activations()` returned, with no copy), and turn `for (const Activation& a : acts) {` into `for (const Activation& a : path.walk_activations()) {`. The other `all_activations()` copies each report row makes (`pathstring()` and `difficulty()`) live in core/model.cpp, which T2 switches onto the walk itself.

- [ ] **Step 8: Run everything and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`. Run `.\build-cpp\Release\hydra_tests.exe -tc="PathsTabCache: 600*" -s` and copy the `600 rebuilt frames ... 600 cached frames ...` line into the task report (measured with no other build or test running). Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test `[PASS]`. Run the two greps from the acceptance list. Run the score-neutral batch comparison; it prints nothing.

- [ ] **Step 9: Commit.**

```bash
git add src/app/path_view.h src/app/path_view.cpp src/ui/app_state.h src/ui/details_view.cpp src/ui/preview_controller.h src/ui/preview_controller.cpp src/ui/preview_load_job.h src/ui/preview_load_job.cpp src/render/track_state.h src/render/track_state.cpp src/render/highway_draw.cpp src/app/report.cpp src/app/preview_view.cpp src/store/record_store.cpp tests/test_path_view.cpp tests/test_track_state.cpp tests/test_preview_controller.cpp tests/ui/uitest_tests.cpp
git commit -m "Cache the Paths tab's views and build Preview overlays off the UI thread

Task: Task 11: The Paths tab and the Preview stop rebuilding every frame
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Record format v7

A stored record carries a lot of bytes nobody needs. The whole-record blob format (`write_record`, `read_record`, `write_path`, `read_path`, and every version-below-6 branch in path_binary.cpp) is reachable only from tests: production writes and reads only the structure-plus-nodes format. Its header comment also says "Version 1 blobs are still read", which has been false since ADR 0015 rejected every old chord code. All of that goes, and path_binary folds into path_codec.cpp, its only remaining user.

Inside the kept format, four things change. `Path::skipped_accents` and `skipped_ghosts` are never set to anything but 0, so they go, along with the two path-view warnings that read them. Each variant node stores its own six score totals, note count and leftover SP, but `prepare_variants` overwrites all eight from the parent on every load. Now only a root path's totals are stored, in the record's structure blob. The multiplier squeezes depend on the combo alone, never on the path, yet every path, variant and node carries its own copy, and the graph keeps a second copy on the SP track that nothing reads. Now the graph finds them once, the record holds one list, and the structure blob stores it once. Last, six `Activation` fields that the search always sets (`skips`, `timecode`, `chord`, `sp_meter`, `frontend_points`, `e_offset`) stop being `std::optional`, a Python-era leftover. Their stored presence bytes go with them.

The node and structure format versions go from 5 to 6. The Ready rule already compares the structure version, so every older result reads Stale and the library needs one re-analysis (decision 8). This task calls the new layout "record format v7" because it follows blob format 6. In code there is no 7: the blob version constant is deleted, and the two codec versions become 6. ADR 0016 records the layout.

What the user sees: every analyzed chart shows as out of date until it is re-analyzed once. After that, every number, label and path string is the same as before. The two "skipped (unhittable)" warnings could never appear, so nothing visible goes.

**Depends on:** Task 1 (it removes the pre-1.7 migrations, decision 5) and Task 2.

**Expected overlaps:** T11 runs in the same wave and edits src/app/path_view.cpp, src/app/preview_view.cpp and src/ui/details_view.cpp. This task touches `build_multsqueezes`, the footer of `build_activations`, and the `measurestr`, `sp_meter` and `rowstr` lines of `build_activations` in path_view.cpp; the overlay loop in `build_preview_scene` and `path_overlay_key` in preview_view.cpp; `render_multsqueeze_section`, `render_path_details` and the Ctrl+C copy line in details_view.cpp. T9 runs in the same wave and edits src/store/record_store.cpp; this task touches only `summarize_path`'s `maxskip` line. T5 (wave 1) will already have edited the `efill` loop in src/app/report.cpp; this task edits that loop again.

**Goal:** Store each record once per chart fact: no whole-record blob, no dead per-path fields, root totals and multiplier squeezes once per record, and the six always-set activation fields as plain values, with node and structure formats at 6.

**Files:**
- Create: `docs/adr/0016-record-format-stores-chart-facts-once.md`, `tests/record_bytes.h`
- Delete: `src/store/path_binary.h`, `src/store/path_binary.cpp`
- Modify: `CMakeLists.txt`, `src/core/model.h`, `src/core/model.cpp`, `src/core/squeeze_rating.h`, `src/core/squeeze_rating.cpp`, `src/core/replay.cpp`
- Modify: `src/search/graph.h`, `src/search/graph.cpp`, `src/search/engine.cpp`, `src/search/pather.cpp`
- Modify: `src/store/serialize.h`, `src/store/serialize.cpp`, `src/store/path_codec.h`, `src/store/path_codec.cpp`, `src/store/record_store.cpp`
- Modify: `src/app/path_view.h`, `src/app/path_view.cpp`, `src/app/preview_view.cpp`, `src/app/report.cpp`, `src/ui/details_view.cpp`, `tools/replay.cpp`
- Test: `tests/test_path_codec.cpp`, `tests/test_store.cpp`, `tests/test_search.cpp`, `tests/test_model.cpp`, `tests/test_path_view.cpp`, `tests/test_squeeze_rating.cpp`, `tests/test_replay.cpp`, `tests/test_preview_view.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="path codec*"` passes, including "path codec: a node carries activations only, never totals", "path codec: root totals ride in the structure, once per root" and "path codec: the multiplier squeezes are stored once per record".
- [ ] `hydra_tests.exe -tc="a row in the 1.8.1 path layout (structure format 5) reads Stale"` passes.
- [ ] `hydra_tests.exe -tc="the graph finds the chart's multiplier squeezes once, in chart order"` passes.
- [ ] The full `hydra_tests.exe` run prints `Status: SUCCESS!`, and `hydra_uitest.exe --all` passes.
- [ ] This prints nothing: `Get-ChildItem src,tests,tools -Recurse -Include *.cpp,*.h | Select-String -Pattern 'write_record|read_record|write_path\b|read_path\b|kBlobFormatVersion|path_binary|skipped_accents|skipped_ghosts|collect_multsqueezes|peek_sp_cap'`
- [ ] The score-neutral batch diff (brief recipe) prints nothing.
- [ ] `docs/adr/0016-record-format-stores-chart-facts-once.md` exists.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` -> `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Add the test equality helper.** The deleted `write_record` was the tests' equality proxy. Its replacement goes through the production writer. Create tests/record_bytes.h:

```cpp
// A record's bytes through the store's own writer: the structure blob, then
// every node payload in first-seen order. Two records that give the same
// bytes store the same thing, so tests use this as their equality proxy now
// that the whole-record blob format is gone (docs/adr/0016).

#ifndef HYDRA_TESTS_RECORD_BYTES_H
#define HYDRA_TESTS_RECORD_BYTES_H

#include <cstdint>
#include <vector>

#include "core/model.h"
#include "store/path_codec.h"

inline std::vector<uint8_t> record_bytes(const hydra::HydraRecord& record) {
    const hydra::store::FlatRecord flat = hydra::store::flatten_record(record);
    std::vector<uint8_t> out = flat.structure;
    for (const hydra::store::StoredPathNode& n : flat.nodes)
        out.insert(out.end(), n.payload.begin(), n.payload.end());
    return out;
}

#endif  // HYDRA_TESTS_RECORD_BYTES_H
```

- [ ] **Step 2: Write the failing codec tests.** In tests/test_path_codec.cpp:
  - Replace `#include "store/path_binary.h"` with `#include "record_bytes.h"`.
  - Delete the TEST_CASE "path codec: a rebuilt record is byte-identical through the record serializer" and put this in its place:

```cpp
TEST_CASE("path codec: a rebuilt record flattens to the same bytes") {
    const HydraRecord& rec = fixture().record;
    REQUIRE_FALSE(rec.paths.empty());
    REQUIRE_FALSE(rec.allzero_paths.empty());
    REQUIRE(rec.all_paths().size() > rec.paths.size());

    HydraRecord back = rebuild_record(flatten_record(rec));
    CHECK(record_bytes(back) == record_bytes(rec));

    CHECK(back.ms_limit == rec.ms_limit);
    CHECK(back.sp_cap == rec.sp_cap);
    CHECK(back.sp_cap_converged == rec.sp_cap_converged);
    CHECK(back.rules_fingerprint == rec.rules_fingerprint);
    CHECK(back.multsqueezes == rec.multsqueezes);

    CHECK(back.all_paths().size() == rec.all_paths().size());
    CHECK(back.all_allzero_paths().size() == rec.all_allzero_paths().size());
    CHECK(pathstrings(back.all_paths()) == pathstrings(rec.all_paths()));
    CHECK(pathstrings(back.all_allzero_paths()) == pathstrings(rec.all_allzero_paths()));
    CHECK(diff_summary(summarize_record(back), summarize_record(rec)) == "");

    // Every path, variants included, carries its root's totals again after
    // prepare_variants pushes them down.
    const std::vector<const Path*> want = rec.all_paths();
    const std::vector<const Path*> got = back.all_paths();
    for (size_t i = 0; i < want.size(); ++i) {
        CHECK(got[i]->totalscore() == want[i]->totalscore());
        CHECK(got[i]->notecount == want[i]->notecount);
        CHECK(got[i]->leftover_sp == want[i]->leftover_sp);
        CHECK(got[i]->tied_pathcount() == want[i]->tied_pathcount());
    }

    const ActivationWalk acts = back.best_path().walk_activations();
    REQUIRE_FALSE(acts.empty());
    CHECK(acts.front().deact_tick == rec.best_path().walk_activations().front().deact_tick);

    // Raw ticks until restored; after the restore the strings still agree.
    restore_timecodes(back, fixture().song.timing());
    CHECK(pathstrings(back.all_paths()) == pathstrings(rec.all_paths()));
    CHECK(record_bytes(back) == record_bytes(rec));
}

// A node is a path's activations and nothing else. Totals live with the
// root in the structure blob, and the squeezes live on the record.
TEST_CASE("path codec: a node carries activations only, never totals") {
    const Path& root = fixture().record.best_path();
    Path changed = root;
    changed.score_base += 1;
    changed.score_sp += 7;
    changed.notecount += 1;
    changed.leftover_sp += 1;
    CHECK(encode_path_node(changed) == encode_path_node(root));

    REQUIRE_FALSE(changed.activations.empty());
    changed.activations.front().skips += 1;
    CHECK(encode_path_node(changed) != encode_path_node(root));
}

TEST_CASE("path codec: root totals ride in the structure, once per root") {
    HydraRecord rec = fixture().record;
    const FlatRecord before = flatten_record(rec);
    rec.paths.front().score_base += 1;
    rec.paths.front().notecount += 2;
    rec.paths.front().leftover_sp += 3;
    const FlatRecord after = flatten_record(rec);

    CHECK(after.structure != before.structure);
    REQUIRE(after.nodes.size() == before.nodes.size());
    for (size_t i = 0; i < after.nodes.size(); ++i)
        CHECK(after.nodes[i].hash == before.nodes[i].hash);

    const HydraRecord back = rebuild_record(after);
    CHECK(back.paths.front().score_base == rec.paths.front().score_base);
    CHECK(back.paths.front().notecount == rec.paths.front().notecount);
    CHECK(back.paths.front().leftover_sp == rec.paths.front().leftover_sp);
}

TEST_CASE("path codec: the multiplier squeezes are stored once per record") {
    HydraRecord rec = fixture().record;
    Chord c;
    c.add_note(NoteColor::Red);
    c.add_note(NoteColor::Yellow);
    c.apply_cymbal(NoteColor::Yellow);
    rec.multsqueezes = {MultSqueeze(c, 8), MultSqueeze(c, 18)};
    HydraRecord none = rec;
    none.multsqueezes.clear();

    const FlatRecord with = flatten_record(rec);
    const FlatRecord without = flatten_record(none);
    // Each squeeze costs its chord code (a 4-byte length plus 5 characters)
    // and its 4-byte combo, once, however many paths the record holds.
    CHECK(with.structure.size() == without.structure.size() + 2 * 13);
    REQUIRE(with.nodes.size() == without.nodes.size());
    for (size_t i = 0; i < with.nodes.size(); ++i)
        CHECK(with.nodes[i].payload == without.nodes[i].payload);
    CHECK(rebuild_record(with).multsqueezes == rec.multsqueezes);
}
```

  - In the TEST_CASE "path codec: a record with no paths round-trips", replace `    CHECK(write_record(back) == write_record(read_record(write_record(empty))));` with `    CHECK(record_bytes(back) == record_bytes(empty));`.
  - In the TEST_CASE "path codec: node payloads are flat and content-addressed", replace:

```cpp
    CHECK(node.activations.size() == root.activations.size());
    CHECK(node.multsqueezes.size() == root.multsqueezes.size());
    CHECK(node.score_base == root.score_base);
    CHECK(node.notecount == root.notecount);
    CHECK(node.leftover_sp == root.leftover_sp);
    CHECK(node.skipped_ghosts == root.skipped_ghosts);
    CHECK(node.skipped_accents == root.skipped_accents);
```

  with `    CHECK(node.activations.size() == root.activations.size());`.
  - Replace the whole TEST_CASE "path codec: a version-1 node is rejected" (and the comment block above it) with:

```cpp
// Only the current node layout is read. A node from an older layout is
// reachable only through an older structure, which the store never decodes.
TEST_CASE("path codec: a node in an older layout is rejected") {
    std::vector<uint8_t> old = encode_path_node(fixture().record.best_path());
    old[0] = 5;  // the 1.8.1 node layout
    CHECK_THROWS_AS(decode_path_node(old), SerializeError);
}
```

  - In the TEST_CASE "path codec: a missing node or a bad structure blob throws", after the `past4` block add:

```cpp
    // Version 5 is the 1.8.1 layout: per-path squeezes and totals in nodes.
    FlatRecord past5 = flat;
    past5.structure[0] = 5;
    CHECK_THROWS_AS(rebuild_record(past5), SerializeError);
```

  and change `    // The current version is 5, and the unmodified flat record -- still at` to `    // The current version is 6, and the unmodified flat record -- still at`, and `    CHECK(kPathStructureFormatVersion == 5);` to `    CHECK(kPathStructureFormatVersion == 6);`.

- [ ] **Step 3: Write the failing store, graph and model tests.** In tests/test_store.cpp, add `#include "record_bytes.h"`, and add this case right after the TEST_CASE "a row in an older path format is Stale even when this build stamped it":

```cpp
TEST_CASE("a row in the 1.8.1 path layout (structure format 5) reads Stale") {
    RecordStore store(":memory:");
    store.add_song("old", "Song", "Artist", "Charter", fixture().song);
    PreparedRow row = prepare_row(RecordKey{"old", "mode", CapQuery::at(4)}, at_cap(4));
    REQUIRE(row.structure.size() >= 4);
    row.structure[0] = 5;
    store.add_row(row);

    const RecordLookup lookup = store.get_record(RecordKey{"old", "mode", CapQuery::at(4)});
    CHECK(lookup.status == RecordStatus::Stale);
    CHECK(lookup.stale_build);
    CHECK_FALSE(lookup.record.has_value());
}
```

  In the same file, delete the four TEST_CASEs that exercise the whole-record blob: "record blob: v3 carries transfer scales, v1/v2 still read", "record blob: a v3 write drops deact_tick, a v4 write keeps it", "record blob: a v4 write drops clamp_tick, a v5 write keeps it", and "record blob: a v5 write drops sqout_tick and collected_phrase_ticks, a v6 write keeps them" (with the comment blocks above each). In the TEST_CASE "replacing one lens's result leaves the other's bytes untouched", replace both `write_record(` calls with `record_bytes(`.

  In tests/test_search.cpp, add this case after the TEST_CASE "search invariants hold across the corpus and config knobs":

```cpp
// A multiplier squeeze depends on the combo alone, and a full-combo path
// never breaks combo, so the list is one fact about the chart. The graph
// finds it once; analyze_chart hands that one list to the record.
TEST_CASE("the graph finds the chart's multiplier squeezes once, in chart order") {
    int charts = 0, with_squeezes = 0, analyzed = 0;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        ++charts;

        // An independent spelling: every chord, at the combo before it.
        std::vector<MultSqueeze> want;
        int combo = 0;
        for (const SongTimestamp& ts : song.sequence) {
            try {
                want.push_back(MultSqueeze(ts.chord, combo));
            } catch (const std::invalid_argument&) {
            }
            combo += ts.chord.count();
        }

        ScoreGraph graph(song, 4);
        CHECK_MESSAGE(graph.multsqueezes() == want, path);
        if (want.empty()) continue;
        ++with_squeezes;

        if (analyzed < 3) {
            SearchSettings cfg;
            cfg.sp_cap = 4;
            cfg.depth_value = 0;
            CHECK_MESSAGE(analyze_chart(song, cfg).multsqueezes == want, path);
            ++analyzed;
        }
    }
    REQUIRE(charts > 0);
    CHECK(with_squeezes > 0);
}
```

  In tests/test_model.cpp, in the TEST_CASE "Path pathstring and pathstring_verbose", replace:

```cpp
    CHECK(p.pathstring_verbose() ==
          "(No mult squeezes.) | 1 | Score: 100,000");
```

  with:

```cpp
    CHECK(p.pathstring_verbose({}) ==
          "(No mult squeezes.) | 1 | Score: 100,000");
    Chord c;
    c.add_note(NoteColor::Red);
    c.add_note(NoteColor::Yellow);
    c.apply_cymbal(NoteColor::Yellow);
    CHECK(p.pathstring_verbose({MultSqueeze(c, 8)}) == "2x | 1 | Score: 100,000");
```

  and replace `    CHECK(empty.pathstring_verbose() ==` with `    CHECK(empty.pathstring_verbose({}) ==`.

  In tests/test_path_view.cpp, replace the body of the TEST_CASE "build_multsqueezes: one labeled entry per squeeze":

```cpp
    const Path& best = analyzed().record.best_path();
    std::vector<MultSqueezeView> v = build_multsqueezes(best);
    CHECK(v.size() == best.multsqueezes.size());
    for (size_t i = 0; i < v.size(); ++i) {
        CHECK(v[i].label.find(" pts):   ") != std::string::npos);
        CHECK(v[i].howto == best.multsqueezes[i].howto());
    }
```

  with:

```cpp
    const HydraRecord& rec = analyzed().record;
    std::vector<MultSqueezeView> v = build_multsqueezes(rec);
    CHECK(v.size() == rec.multsqueezes.size());
    for (size_t i = 0; i < v.size(); ++i) {
        CHECK(v[i].label.find(" pts):   ") != std::string::npos);
        CHECK(v[i].howto == rec.multsqueezes[i].howto());
    }
```

- [ ] **Step 4: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors, because `HydraRecord::multsqueezes`, `ScoreGraph::multsqueezes()`, the one-argument `pathstring_verbose` and `build_multsqueezes(const HydraRecord&)` do not exist yet.

- [ ] **Step 5: Change the model.** In src/core/model.h, inside `struct Activation`, replace:

```cpp
    std::optional<int> skips;
    std::optional<Timecode> timecode;
    std::optional<Chord> chord;
    std::optional<int> sp_meter;
    std::optional<int> frontend_points;
```

  with:

```cpp
    // The search sets these six on every activation it makes, so they are
    // plain values (record format v7, docs/adr/0016).
    int skips = 0;
    Timecode timecode;
    Chord chord;
    int sp_meter = 0;
    int frontend_points = 0;
```

  and replace `    std::optional<double> e_offset;` with `    double e_offset = 0.0;`.

  Inside `struct Path`, delete `    std::vector<MultSqueeze> multsqueezes;`, `    int skipped_ghosts = 0;` and `    int skipped_accents = 0;`. Replace `    std::string pathstring_verbose() const;` with:

```cpp
    // The Ctrl+C string: the chart's multiplier squeezes (from the record,
    // HydraRecord::multsqueezes), the verbose activations, the score.
    std::string pathstring_verbose(const std::vector<MultSqueeze>& multsqueezes) const;
```

  In the `is_allzero` comment, replace `    // skips == 0. False for a path with no activations, and for a stale record
    // whose activations carry no skip count.` with `    // skips == 0. False for a path with no activations.`.

  Inside `struct HydraRecord`, right after `    std::vector<Path> paths;`, add:

```cpp
    // The chart's multiplier squeezes, in chart order. They depend on the
    // combo alone, never on the path, so a record holds one list rather than
    // one per path (docs/adr/0016).
    std::vector<MultSqueeze> multsqueezes;
```

  In src/core/model.cpp:
  - `Activation::is_e_critical` returns `e_offset < kCalibrationFillWindowMs;`.
  - `Activation::is_E0` returns `is_e0(e_offset, skips);`.
  - In `Activation::e_difficulty`, `calibration_fill_difficulty(*e_offset)` becomes `calibration_fill_difficulty(e_offset)`.
  - In `Activation::notationstr`, `std::to_string(*skips)` becomes `std::to_string(skips)`.
  - In `Path::is_allzero`, `if (act.skips.value_or(-1) != 0) return false;` becomes `if (act.skips != 0) return false;`.
  - `Path::pathstring_verbose() const {` becomes `Path::pathstring_verbose(const std::vector<MultSqueeze>& multsqueezes) const {`; its body already reads a name `multsqueezes`, which is now the parameter.
  - `Path::prepare_variants` needs no change: it already copies the six totals, `notecount` and `leftover_sp` from parent to variant.

- [ ] **Step 6: Find the squeezes once, in the graph.** In src/search/graph.h, delete `    std::vector<MultSqueeze> multsqueezes;` from `struct ScoreGraphEdge`. In `class ScoreGraph`'s public part, after `const SongTiming& timing() const { return song_.timing(); }`, add:

```cpp
    // The chart's multiplier squeezes, in chart order. One list: the combo
    // that decides them never depends on the path.
    const std::vector<MultSqueeze>& multsqueezes() const { return multsqueezes_; }
```

  and in the private members, after `int combo_ = 0;`, add `    std::vector<MultSqueeze> multsqueezes_;`.

  In src/search/graph.cpp replace:

```cpp
void ScoreGraph::store_multsqueeze(const MultSqueeze& msq) {
    proto_base_edge_->multsqueezes.push_back(msq);
    proto_sp_edge_->multsqueezes.push_back(msq);
}
```

  with:

```cpp
void ScoreGraph::store_multsqueeze(const MultSqueeze& msq) {
    multsqueezes_.push_back(msq);
}
```

  In src/search/pather.cpp, inside `read`, after `    record.ms_limit = ms_filter;`, add `    record.multsqueezes = graph.multsqueezes();`.

- [ ] **Step 7: Stop the engine carrying copies.** In src/search/engine.cpp:
  - In `struct Variant`, delete `    int32_t sc[6];`, `    int32_t notecount;`, `    int32_t leftover_sp;`, `    int32_t skipped_accents;` and `    int32_t skipped_ghosts;`.
  - In `struct Path` (the engine's), delete `    int32_t skipped_accents;` and `    int32_t skipped_ghosts;`.
  - In `struct OutPath`, `    int32_t notecount, leftover_sp, skipped_accents, skipped_ghosts;` becomes `    int32_t notecount, leftover_sp;`.
  - In `Engine::reduce_group`, delete these five lines from the variant it builds:

```cpp
            for (int32_t k = 0; k < 6; ++k) v.sc[k] = p.sc[k];
            v.notecount = p.notecount;
            v.leftover_sp = p.sp;
            v.skipped_accents = p.skipped_accents;
            v.skipped_ghosts = p.skipped_ghosts;
```

  - In `Engine::emit_variant`, replace:

```cpp
        OutPath op;
        op.score_base = var.sc[0];
        op.score_combo = var.sc[1];
        op.score_sp = var.sc[2];
        op.score_solo = var.sc[3];
        op.score_accents = var.sc[4];
        op.score_ghosts = var.sc[5];
        op.notecount = var.notecount;
        op.leftover_sp = var.leftover_sp;
        op.skipped_accents = var.skipped_accents;
        op.skipped_ghosts = var.skipped_ghosts;
        op.var_point = var.var_point;
```

  with:

```cpp
        // A variant ties its parent's score, and prepare_variants copies the
        // parent's totals, note count and leftover SP onto it, so the engine
        // hands none of its own (docs/adr/0016).
        OutPath op{};
        op.var_point = var.var_point;
```

  - In `Engine::emit_path`, delete `    op.skipped_accents = p.skipped_accents;` and `    op.skipped_ghosts = p.skipped_ghosts;`.
  - Delete the whole `collect_multsqueezes` function.
  - In `rebuild`, delete the parameter line `                           const std::vector<MultSqueeze>& multsqueezes,`, and delete `        path.skipped_accents = op.skipped_accents;`, `        path.skipped_ghosts = op.skipped_ghosts;` and `        path.multsqueezes = multsqueezes;`. Replace `            act.chord = node->chord;` with:

```cpp
            // An activation node is a chart note, so it always carries the
            // chord hit there.
            act.chord = node->chord.value();
```

  - In `run_search`, the `rebuild(...)` call drops its `collect_multsqueezes(graph), ` argument.

- [ ] **Step 8: Rewrite the codec.** Delete src/store/path_binary.h and src/store/path_binary.cpp (`git rm src/store/path_binary.h src/store/path_binary.cpp`), and delete the line `    src/store/path_binary.cpp` from CMakeLists.txt.

  In src/store/path_codec.cpp, replace `#include "store/path_binary.h"` with nothing (delete the line). Inside the anonymous namespace, right before `// ---- structure blob ----`, add:

```cpp
// ---- node and structure pieces (record format v7, docs/adr/0016) ------------

// One activation. The six fields the search always sets carry no presence
// byte; the three ticks that can be missing keep theirs.
void write_activation(BinaryWriter& w, const Activation& act) {
    w.i32(act.skips);
    w.i64(act.timecode.ticks());
    w.str(act.chord.code());
    w.i32(act.sp_meter);
    w.i32(act.frontend_points);

    // Only the rows the details view shows (display_backends). Backends are
    // a large, display-only part of a record, so the rest are dropped.
    const std::vector<BackendSqueeze> backends = act.display_backends();
    w.u32(static_cast<uint32_t>(backends.size()));
    for (const BackendSqueeze& b : backends) {
        w.i64(b.timecode.ticks());
        w.str(b.chord.code());
        w.i32(b.points);
        w.i32(b.sqout_points);
        w.boolean(b.is_sp);
        w.opt_f64(b.offset_ms);
    }

    w.u32(static_cast<uint32_t>(act.sqinouts.size()));
    for (const SPSqueeze& sq : act.sqinouts) {
        w.u8(sq.kind == SqueezeKind::SqIn ? 0 : 1);
        w.f64(sq.offset_ms);
    }

    w.f64(act.e_offset);
    w.f64(act.transfer_pre.early);
    w.f64(act.transfer_pre.late);
    w.f64(act.transfer_post.early);
    w.f64(act.transfer_post.late);
    w.opt_i64(act.deact_tick);
    w.opt_i64(act.clamp_tick);
    w.opt_i64(act.sqout_tick);
    w.u32(static_cast<uint32_t>(act.collected_phrase_ticks.size()));
    for (int64_t t : act.collected_phrase_ticks) w.i64(t);
}

Activation read_activation(BinaryReader& r) {
    Activation act;
    act.skips = r.i32();
    act.timecode = Timecode::raw(r.i64());
    act.chord = Chord::from_code(r.str());
    act.sp_meter = r.i32();
    act.frontend_points = r.i32();

    const uint32_t nbackends = r.u32();
    act.backends.reserve(nbackends);
    for (uint32_t i = 0; i < nbackends; ++i) {
        BackendSqueeze b;
        b.timecode = Timecode::raw(r.i64());
        b.chord = Chord::from_code(r.str());
        b.points = r.i32();
        b.sqout_points = r.i32();
        b.is_sp = r.boolean();
        b.offset_ms = r.opt_f64();
        act.backends.push_back(std::move(b));
    }

    const uint32_t nsq = r.u32();
    act.sqinouts.reserve(nsq);
    for (uint32_t i = 0; i < nsq; ++i) {
        const SqueezeKind kind = r.u8() == 0 ? SqueezeKind::SqIn : SqueezeKind::SqOut;
        const double offset = r.f64();
        act.sqinouts.push_back(SPSqueeze{kind, offset});
    }

    act.e_offset = r.f64();
    act.transfer_pre.early = r.f64();
    act.transfer_pre.late = r.f64();
    act.transfer_post.early = r.f64();
    act.transfer_post.late = r.f64();
    act.deact_tick = r.opt_i64();
    act.clamp_tick = r.opt_i64();
    act.sqout_tick = r.opt_i64();
    const uint32_t n = r.u32();
    act.collected_phrase_ticks.reserve(n);
    for (uint32_t i = 0; i < n; ++i) act.collected_phrase_ticks.push_back(r.i64());
    return act;
}

// A root path's own totals: the six score categories, the chart's note count
// and the SP left at the end. A variant's copies are overwritten from its
// parent by prepare_variants on every load, so only roots store them.
void write_root_totals(BinaryWriter& w, const Path& p) {
    w.i64(p.score_base);
    w.i64(p.score_combo);
    w.i64(p.score_sp);
    w.i64(p.score_solo);
    w.i64(p.score_accents);
    w.i64(p.score_ghosts);
    w.i32(p.notecount);
    w.i32(p.leftover_sp);
}

void read_root_totals(BinaryReader& r, Path& p) {
    p.score_base = r.i64();
    p.score_combo = r.i64();
    p.score_sp = r.i64();
    p.score_solo = r.i64();
    p.score_accents = r.i64();
    p.score_ghosts = r.i64();
    p.notecount = r.i32();
    p.leftover_sp = r.i32();
}
```

  Replace `encode_path_node` and `decode_path_node` with:

```cpp
std::vector<uint8_t> encode_path_node(const Path& path) {
    BinaryWriter w;
    w.u32(kPathNodeFormatVersion);
    w.u32(static_cast<uint32_t>(path.activations.size()));
    for (const Activation& a : path.activations) write_activation(w, a);
    return std::move(w.bytes);
}

Path decode_path_node(const std::vector<uint8_t>& payload) {
    BinaryReader r(payload);
    // Only the current node version is readable. An older node is reachable
    // only through an older structure, and the store never decodes one of
    // those (row_is_ready in record_store.cpp).
    if (r.u32() != kPathNodeFormatVersion)
        throw SerializeError("unsupported path node format version");

    Path path;
    const uint32_t nact = r.u32();
    path.activations.reserve(nact);
    for (uint32_t i = 0; i < nact; ++i) path.activations.push_back(read_activation(r));
    return path;
}
```

  In `flatten_record`, replace:

```cpp
    w.boolean(record.sp_cap_converged);

    w.u32(static_cast<uint32_t>(record.paths.size()));
    for (const Path& p : record.paths) write_tree_entry(w, p, flat, seen);

    w.u32(static_cast<uint32_t>(record.allzero_paths.size()));
    for (const Path& p : record.allzero_paths) write_tree_entry(w, p, flat, seen);
```

  with:

```cpp
    w.boolean(record.sp_cap_converged);

    w.u32(static_cast<uint32_t>(record.multsqueezes.size()));
    for (const MultSqueeze& m : record.multsqueezes) {
        w.str(m.chord().code());
        w.i32(m.combo());
    }

    w.u32(static_cast<uint32_t>(record.paths.size()));
    for (const Path& p : record.paths) {
        write_tree_entry(w, p, flat, seen);
        write_root_totals(w, p);
    }

    w.u32(static_cast<uint32_t>(record.allzero_paths.size()));
    for (const Path& p : record.allzero_paths) {
        write_tree_entry(w, p, flat, seen);
        write_root_totals(w, p);
    }
```

  In `rebuild_record` (the lookup overload), replace:

```cpp
    record.sp_cap_converged = r.boolean();

    uint32_t nroots = r.u32();
    record.paths.reserve(nroots);
    for (uint32_t i = 0; i < nroots; ++i)
        record.paths.push_back(read_tree_entry(r, lookup));

    uint32_t nzero = r.u32();
    record.allzero_paths.reserve(nzero);
    for (uint32_t i = 0; i < nzero; ++i)
        record.allzero_paths.push_back(read_tree_entry(r, lookup));
```

  with:

```cpp
    record.sp_cap_converged = r.boolean();

    const uint32_t nmsq = r.u32();
    record.multsqueezes.reserve(nmsq);
    for (uint32_t i = 0; i < nmsq; ++i) {
        Chord chord = Chord::from_code(r.str());
        const int combo = r.i32();
        record.multsqueezes.push_back(MultSqueeze(std::move(chord), combo));
    }

    const uint32_t nroots = r.u32();
    record.paths.reserve(nroots);
    for (uint32_t i = 0; i < nroots; ++i) {
        Path p = read_tree_entry(r, lookup);
        read_root_totals(r, p);
        record.paths.push_back(std::move(p));
    }

    const uint32_t nzero = r.u32();
    record.allzero_paths.reserve(nzero);
    for (uint32_t i = 0; i < nzero; ++i) {
        Path p = read_tree_entry(r, lookup);
        read_root_totals(r, p);
        record.allzero_paths.push_back(std::move(p));
    }
```

  and replace its closing comment `    // The same post-passes read_record runs, in the same order. tied_count is
    // a pure function of the variant tree, so it is recounted rather than
    // stored; recount_tied_paths() walks the whole subtree, matching
    // read_path's per-node call. prepare_variants() then pushes each parent's
    // score fields down and rebuilds variant_tail.` with `    // tied_count is a pure function of the variant tree, so it is recounted
    // rather than stored. prepare_variants() then pushes each root's totals
    // down to its variants and rebuilds variant_tail.`.

  In src/store/path_codec.h, replace the paragraph starting `// Flat storage is safe because tree-contextual data is rebuilt on load, not` (four lines, ending `// alone, which is why those two really are per-node data and are stored.`) with:

```cpp
// Flat storage is safe because tree-contextual data is rebuilt on load, not
// read from a node. A node holds a path's own activations and nothing else.
// A root's totals (six score categories, note count, leftover SP) sit next to
// it in the structure blob, and Path::prepare_variants() copies them onto
// each variant and rebuilds variant_tail from var_point. The chart's
// multiplier squeezes are stored once, in the structure (docs/adr/0016).
```

  Replace the two version comments and constants:

```cpp
constexpr uint32_t kPathNodeFormatVersion = 5;
```

  becomes (and append to the comment above it the sentence `// Version 6 (record format v7, docs/adr/0016) is the activations alone:
// no squeezes, no totals, and no presence byte on the six always-set fields.`):

```cpp
constexpr uint32_t kPathNodeFormatVersion = 6;
```

  and `constexpr uint32_t kPathStructureFormatVersion = 5;` becomes `constexpr uint32_t kPathStructureFormatVersion = 6;`, with `// Version 6 adds the record's multiplier squeezes after the header and each
// root's totals after its tree entry, and references node format 6.` appended to its comment. Also change `// read_record's — call restore_timecodes() with the song's SongTiming.` to `// call restore_timecodes() with the song's SongTiming.`, and in the `rebuild_record` declaration comment replace `then runs the same post-passes read_record
// runs (recount_tied_paths, then prepare_variants), in the same order.` with `then runs recount_tied_paths and prepare_variants.`.

- [ ] **Step 9: Delete the whole-record blob.** In src/store/serialize.cpp, delete `#include "store/path_binary.h"`, the anonymous-namespace `write_path` and `read_path` (with the comment block above them), `write_record`, both `read_record` overloads, and `peek_sp_cap`. In `restore_activation`, replace `    if (act.timecode) act.timecode = timing.timecode(act.timecode->ticks());` with `    act.timecode = timing.timecode(act.timecode.ticks());`.

  Before deleting `peek_sp_cap`, run `Get-ChildItem src,tests -Recurse -Include *.cpp,*.h | Select-String 'peek_sp_cap|write_legacy_db|write_v1_db'`. It must list only serialize.h and serialize.cpp. If record_store.cpp still calls `peek_sp_cap`, or tests/test_store.cpp still has the `write_legacy_db`/`write_v1_db` helpers (they call `write_record`), the pre-1.7 migrations are still there: stop and report that decision 5's task has not landed.

  Also in src/store/path_codec.h, in the node-version comment, replace `// Only version 5 is written, and only version 5 is decoded.` with `// Only version 6 is written, and only version 6 is decoded.`.

  In src/store/serialize.h, replace the header comment (lines 1 to 12) with:

```cpp
// Small binary primitives for the store's own formats -- the path codec
// (store/path_codec.h) and the songmeta tempo-map blob (record_store.cpp) --
// plus restore_timecodes. A decoded record's Activation/BackendSqueeze
// timecodes carry only raw ticks (Timecode::raw), because a stored path has
// no tempo map of its own. restore_timecodes() with the song's SongTiming
// (from songmeta) resolves them into full Timecodes.
```

  and delete the `kBlobFormatVersion` comment and constant, and the declarations of `write_record`, `peek_sp_cap` and both `read_record`s with their comments. Change the `restore_timecodes` comment's `Call once after
// read_record, using` to `Call once after
// decoding, using`.

- [ ] **Step 10: Move every reader of the six fields to plain values.** Build `hydra_tests`; the compiler names each site. The rule for each: `*act.x` becomes `act.x`, `act.x->m` becomes `act.x.m`, `act.x.value_or(d)` becomes `act.x`, and a `has_value()` or truthiness guard on one of the six is dropped while its body stays. These sites need more than that rule, so change them exactly:
  - src/core/squeeze_rating.cpp, in `frontend_transfer_scales`: replace `    if (!act.timecode || !act.sp_meter || !act.deact_tick) return std::nullopt;` with `    if (!act.deact_tick) return std::nullopt;`, and `    int64_t act_tick = act.timecode->ticks();` with `    int64_t act_tick = act.timecode.ticks();`. In src/core/squeeze_rating.h, the comment `// Display-only; nullopt when the
// activation has no timecode, no sp_meter, or no deact_tick (stale record).` becomes `// Display-only; nullopt when the
// activation has no deact_tick.`.
  - src/store/record_store.cpp, in `summarize_path`: `        if (a.skips && *a.skips > maxskip) maxskip = *a.skips;` becomes `        if (a.skips > maxskip) maxskip = a.skips;`.
  - src/app/report.cpp, in the row loop: the `if (a.e_offset.has_value() && a.skips.has_value()) {` guard and its closing brace go, keeping the three lines inside.
  - src/app/path_view.cpp, in `build_activations`: `        std::string meas = act.timecode ? measurestr(*act.timecode) : "";` becomes `        std::string meas = measurestr(act.timecode);`; `act.sp_meter.value_or(0)` becomes `act.sp_meter`; and `            "Frontend: " + (act.chord ? act.chord->rowstr() : std::string("None"));` becomes `            "Frontend: " + act.chord.rowstr();`.
  - src/app/preview_view.cpp, in `build_preview_scene`: delete `            if (!a.timecode.has_value()) continue;`; `a.timecode->ticks()` becomes `a.timecode.ticks()`; `a.sp_meter.value_or(0)` becomes `a.sp_meter`; `a.skips.value_or(0)` becomes `a.skips`; and `            if (a.chord.has_value() && !a.chord->notes().empty()) {` becomes `            if (a.chord.count() > 0) {` with `a.chord->activation_note()` becoming `a.chord.activation_note()`.
  - src/core/replay.cpp: in `windows_for_path`, `        if (!act.timecode || !act.deact_tick) continue;` becomes `        if (!act.deact_tick) continue;` and `act.timecode->ticks()` becomes `act.timecode.ticks()`. In `paths_json`, replace:

```cpp
            const int64_t act_tick = act.timecode ? act.timecode->ticks() : -1;
            const std::optional<int64_t>& d = act.deact_tick;
            int64_t nominal = -1;
            if (act.timecode && act.sp_meter)
                nominal = timing
                              .plusmeasure(*act.timecode, sp_bars_to_measures(*act.sp_meter))
                              .ticks();
```

   with:

```cpp
            const int64_t act_tick = act.timecode.ticks();
            const std::optional<int64_t>& d = act.deact_tick;
            const int64_t nominal =
                timing.plusmeasure(act.timecode, sp_bars_to_measures(act.sp_meter)).ticks();
```

   and in the JSON object `{"sp_meter", act.sp_meter ? *act.sp_meter : -1}` becomes `{"sp_meter", act.sp_meter}`, `{"skips", act.skips ? *act.skips : -1}` becomes `{"skips", act.skips}`, and `{"chord_code", act.chord ? act.chord->code() : std::string()}` becomes `{"chord_code", act.chord.code()}`.
  - tools/replay.cpp, in the self-check failure printout: replace:

```cpp
                    const int64_t act_tick =
                        act.timecode ? act.timecode->ticks() : -1;
                    int64_t nominal = -1;
                    if (act.timecode && act.sp_meter)
                        nominal = timing
                                      .plusmeasure(*act.timecode,
                                                   sp_bars_to_measures(*act.sp_meter))
                                      .ticks();
```

   with:

```cpp
                    const int64_t act_tick = act.timecode.ticks();
                    const int64_t nominal =
                        timing.plusmeasure(act.timecode, sp_bars_to_measures(act.sp_meter))
                            .ticks();
```

   and in the `std::printf` arguments `act.sp_meter.value_or(-1)` becomes `act.sp_meter` and `act.skips.value_or(-1)` becomes `act.skips`.
  - src/search/pather.cpp, in `search_target`: `            if (!acts[i].timecode || acts[i].timecode->ticks() != ticks[i])` becomes `            if (acts[i].timecode.ticks() != ticks[i])`.

  In the tests (tests/test_search.cpp, test_squeeze_rating.cpp, test_preview_view.cpp, test_store.cpp, test_replay.cpp, test_model.cpp), apply the same rule. A `REQUIRE(act.x.has_value());` line on one of the six is deleted. In tests/test_squeeze_rating.cpp, delete this sub-case, which tests a state that no longer exists:

```cpp
    // Missing timecode or sp_meter (stale record): no scale.
    Activation bare;
    bare.sp_meter = 2;
    CHECK(!frontend_transfer_scales(bare, st).has_value());
    bare.timecode = st.timecode(0);
    bare.sp_meter.reset();
    CHECK(!frontend_transfer_scales(bare, st).has_value());
```

  A hand-built test `Activation` now starts with `e_offset = 0.0`, which is E-critical. If a test fails only because its fixture never set `e_offset`, set `e_offset = 300.0` on that fixture (the value the existing tests use for "not E-critical"). Any other new failure means a real change: stop and report it.

- [ ] **Step 11: Show one squeeze list and drop the dead warnings.** In src/app/path_view.h, replace `std::vector<MultSqueezeView> build_multsqueezes(const Path& path);` with `std::vector<MultSqueezeView> build_multsqueezes(const HydraRecord& record);`. In src/app/path_view.cpp replace:

```cpp
std::vector<MultSqueezeView> build_multsqueezes(const Path& path) {
    std::vector<MultSqueezeView> out;
    out.reserve(path.multsqueezes.size());
    for (const MultSqueeze& msq : path.multsqueezes) {
```

  with:

```cpp
std::vector<MultSqueezeView> build_multsqueezes(const HydraRecord& record) {
    std::vector<MultSqueezeView> out;
    out.reserve(record.multsqueezes.size());
    for (const MultSqueeze& msq : record.multsqueezes) {
```

  and at the end of `build_activations` delete:

```cpp
    if (path.skipped_accents > 0)
        view.footer.push_back(
            {"This path has " + std::to_string(path.skipped_accents) +
                 " skipped (unhittable) accent" +
                 (path.skipped_accents == 1 ? "" : "s") + "!",
             true});
    if (path.skipped_ghosts > 0)
        view.footer.push_back(
            {"This path has " + std::to_string(path.skipped_ghosts) +
                 " skipped (unhittable) ghost" +
                 (path.skipped_ghosts == 1 ? "" : "s") + "!",
             true});
```

  In src/ui/details_view.cpp: `void render_multsqueeze_section(const Path* path) {` becomes `void render_multsqueeze_section(const HydraRecord& record) {`, and inside it `app::build_multsqueezes(*path)` becomes `app::build_multsqueezes(record)`. In `render_path_details`, `ImGui::SetClipboardText(path->pathstring_verbose().c_str());` becomes `ImGui::SetClipboardText(path->pathstring_verbose(record.multsqueezes).c_str());` and `    render_multsqueeze_section(path);` becomes `    render_multsqueeze_section(record);`. For the Ctrl+C copy, replace:

```cpp
    std::string copytext = selected_path ? selected_path->pathstring_verbose() : "";
```

  with:

```cpp
    // selected_path is set only while the viewed record is Ready.
    std::string copytext =
        selected_path ? selected_path->pathstring_verbose(app.viewed.record->multsqueezes) : "";
```

  In src/app/preview_view.cpp replace:

```cpp
std::string path_overlay_key(const Path* path) {
    if (path == nullptr) return {};
    return path->pathstring_verbose() + "|" + std::to_string(path->totalscore());
}
```

  with:

```cpp
std::string path_overlay_key(const Path* path) {
    if (path == nullptr) return {};
    // The key only has to tell paths of one chart apart (the controller
    // compares it after matching the chart), and a chart's multiplier
    // squeezes are the same for every path, so they are left out.
    return path->pathstring_verbose({}) + "|" + std::to_string(path->totalscore());
}
```

- [ ] **Step 12: Write ADR 0016.** Create docs/adr/0016-record-format-stores-chart-facts-once.md:

```markdown
# The record format stores each chart fact once

A stored record is a structure blob (the path tree's shape) plus
content-addressed path nodes (ADR 0009). Until 1.8.1 each node carried its
own multiplier squeezes, six score totals, note count, leftover SP and two
skipped-note counts, beside its activations. Most of that was never read.

## The decision

A node holds a path's activations and nothing else.

The multiplier squeezes depend on the combo alone, and a full-combo path
never breaks combo. So they are one fact about the chart, not about a path.
The search graph finds them once, the record holds one list
(`HydraRecord::multsqueezes`), and the structure blob stores it once, right
after the record's header.

A root path's totals (the six score categories, the note count and the
leftover SP) are stored next to that root in the structure blob. A variant's
totals are not stored at all: `Path::prepare_variants` copies them from the
parent on every load, as it always did.

The two skipped-note counts are gone. Nothing ever set them to anything but
zero, so the two warnings that read them could never show.

The six activation fields the search always sets (skips, timecode, chord,
SP meter, frontend points, calibration-fill offset) are plain values, with
no presence byte. The deactivation node, the cap-clamp tick and the
squeeze-out tick can legitimately be missing, so they keep theirs.

The whole-record blob format that `write_record` and `read_record` spoke is
deleted. Only tests used it, and it could not read a real old blob since
ADR 0015 anyway.

## What this costs

The path node format and the structure format both go from 5 to 6. We call
the result record format v7, since it follows blob format 6; there is no
constant named 7. Every result analyzed before this change reads Stale, and
the library needs one re-analysis. Every number, label and path string it
shows afterwards is unchanged.
```

- [ ] **Step 13: Run everything.** Run:

```powershell
.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe
.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all
.\build_cpp.ps1 -Target hydra_replay; .\build_cpp.ps1 -Target hydra_bench
Get-ChildItem src,tests,tools -Recurse -Include *.cpp,*.h | Select-String -Pattern 'write_record|read_record|write_path\b|read_path\b|kBlobFormatVersion|path_binary|skipped_accents|skipped_ghosts|collect_multsqueezes'
```

  Expected: `Status: SUCCESS!`, every GUI test passes, both tools build, and the grep prints nothing. Then build `hydra_batch` and run the brief's score-neutral batch diff. Expected: it prints nothing.

- [ ] **Step 14: Commit.**

```bash
git add CMakeLists.txt docs/adr/0016-record-format-stores-chart-facts-once.md tests/record_bytes.h src/core/model.h src/core/model.cpp src/core/squeeze_rating.h src/core/squeeze_rating.cpp src/core/replay.cpp src/search/graph.h src/search/graph.cpp src/search/engine.cpp src/search/pather.cpp src/store/serialize.h src/store/serialize.cpp src/store/path_codec.h src/store/path_codec.cpp src/store/record_store.cpp src/app/path_view.h src/app/path_view.cpp src/app/preview_view.cpp src/app/report.cpp src/ui/details_view.cpp tools/replay.cpp tests/test_path_codec.cpp tests/test_store.cpp tests/test_search.cpp tests/test_model.cpp tests/test_path_view.cpp tests/test_squeeze_rating.cpp tests/test_replay.cpp tests/test_preview_view.cpp
git commit -m "Record format v7: store each chart fact once

Task: Task 12: Record format v7
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

  (`git rm` in Step 8 already staged the two deleted path_binary files.)

```json:metadata
{"files":["docs/adr/0016-record-format-stores-chart-facts-once.md","tests/record_bytes.h","src/store/path_binary.h","src/store/path_binary.cpp","CMakeLists.txt","src/core/model.h","src/core/model.cpp","src/core/squeeze_rating.h","src/core/squeeze_rating.cpp","src/core/replay.cpp","src/search/graph.h","src/search/graph.cpp","src/search/engine.cpp","src/search/pather.cpp","src/store/serialize.h","src/store/serialize.cpp","src/store/path_codec.h","src/store/path_codec.cpp","src/store/record_store.cpp","src/app/path_view.h","src/app/path_view.cpp","src/app/preview_view.cpp","src/app/report.cpp","src/ui/details_view.cpp","tools/replay.cpp","tests/test_path_codec.cpp","tests/test_store.cpp","tests/test_search.cpp","tests/test_model.cpp","tests/test_path_view.cpp","tests/test_squeeze_rating.cpp","tests/test_replay.cpp","tests/test_preview_view.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe","acceptanceCriteria":["path codec tests pass, including the three new layout tests.","A structure-format-5 row reads Stale.","The graph's multiplier squeezes equal an independent walk of the chart.","Full hydra_tests and hydra_uitest --all pass.","The deleted-names grep prints nothing.","The score-neutral batch diff prints nothing.","ADR 0016 exists."],"modelTier":"standard"}
```

---

### Task 13: Engine speed

The search's hot loop reads a node or an edge through `Engine::node()` and `Engine::edge()`. Each call builds a small view from scratch, and each view does one or two `unordered_map` lookups to turn pointers back into indices. That is 10 to 16 hash lookups per path per step, across the whole live frontier. The views never change during a search, so `enumerate()` builds every one of them once, into two plain arrays, and the hot loop just indexes them.

Building the graph also throws and catches an exception for nearly every chord. `ScoreGraph::build` constructs a `MultSqueeze` for each chord and catches the failure, and failure is the normal case. It happens once per chord per graph, and an Auto analysis builds up to seven graphs. A plain `MultSqueeze::applies()` check answers the same question without throwing.

Both changes are timed with `hydra_bench` against the build just before them and against the Task 0 baseline. The batch diff must stay identical.

What the user sees: nothing, except analysis finishing sooner.

**Depends on:** Task 12.

**Expected overlaps:** T16 and T17 run in the same wave. Neither is expected to touch src/search/engine.cpp or src/search/graph.cpp. T16 (string helper dedupe) may touch src/core/model.cpp; this task adds `MultSqueeze::applies` and rewrites `MultSqueeze::validate` there.

**Goal:** Replace the per-read view building with two arrays built once, and replace the per-chord throw with an `applies()` check, with identical results and no slower bench.

**Files:**
- Modify: `src/search/engine.cpp`, `src/search/graph.cpp`, `src/core/model.h`, `src/core/model.cpp`
- Test: `tests/test_model.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="MultSqueeze::applies answers exactly when the constructor accepts"` passes.
- [ ] The full `hydra_tests.exe` run prints `Status: SUCCESS!`.
- [ ] The score-neutral batch diff (brief recipe) prints nothing.
- [ ] `hydra_bench.exe` (no argument) reports "cap4 d4" and "auto d4" each at or below the same lines measured on this task's parent commit, and the commit message lists before, after and the Task 0 baseline for both.
- [ ] This prints nothing: `Select-String -Path src\search\engine.cpp -Pattern 'en_\.node_of|en_\.edge_of|en\.node_idx|en\.edge_idx'` (the engine no longer reaches the pointer-to-index maps; only `enumerate` uses them, as locals).

**Verify:** `.\build_cpp.ps1 -Target hydra_bench; .\build-cpp\Release\hydra_bench.exe` -> two lines, "cap4 d4" and "auto d4", each at or below the parent commit's.

**Steps:**

- [ ] **Step 1: Measure the parent commit.** With no other build or test running, run `.\build_cpp.ps1 -Target hydra_bench; .\build-cpp\Release\hydra_bench.exe | Tee-Object "$env:TEMP\t13_before.txt"`. Keep the two lines. Also read `$env:TEMP\hydra_audit_base\bench.txt` for the Task 0 numbers.

- [ ] **Step 2: Write the failing test.** Add this case to tests/test_model.cpp, right after the TEST_CASE "MultSqueeze accepts exactly the 2- and 3-note chords that straddle a multiplier step":

```cpp
// The graph asks applies() of every chord instead of catching a throw.
// It must answer exactly as the constructor decides, for every shape.
TEST_CASE("MultSqueeze::applies answers exactly when the constructor accepts") {
    const NoteColor order[] = {NoteColor::Red, NoteColor::Yellow, NoteColor::Kick,
                               NoteColor::Blue, NoteColor::Green};
    for (int n = 0; n <= 5; ++n) {
        for (bool cymbal : {false, true}) {
            Chord c;
            for (int i = 0; i < n; ++i) c.add_note(order[i]);
            if (cymbal && n >= 2) c.apply_cymbal(NoteColor::Yellow);
            for (int combo = 0; combo < 40; ++combo) {
                bool constructed = true;
                try {
                    MultSqueeze ms(c, combo);
                } catch (const std::invalid_argument&) {
                    constructed = false;
                }
                CHECK_MESSAGE(MultSqueeze::applies(c, combo) == constructed,
                              "n=" << n << " cymbal=" << cymbal << " combo=" << combo);
            }
        }
    }
}
```

- [ ] **Step 3: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error, because `MultSqueeze` has no member `applies`.

- [ ] **Step 4: Add `applies()`.** In src/core/model.h, inside `class MultSqueeze`'s public part, after the constructor, add:

```cpp
    // Whether a chord hit at this combo is a multiplier squeeze: exactly the
    // cases the constructor accepts, answered without throwing. The graph
    // asks this of every chord, and "no" is the usual answer.
    static bool applies(const Chord& chord, int combo);
```

  In src/core/model.cpp, replace the whole `MultSqueeze::validate` definition:

```cpp
void MultSqueeze::validate() const {
    switch (combo_) {
        case 7: case 8: case 17: case 18: case 27: case 28: break;
        default:
            throw std::invalid_argument("Invalid MultSqueeze combo");
    }
    int mod = (chord_.count() + combo_) % 10;
    if (mod != 0 && mod != 1)
        throw std::invalid_argument("Invalid MultSqueeze chord length");

    std::vector<ChordNote> notes = chord_.notes();
    bool all_same = true;
    for (const ChordNote& n : notes)
        if (n.basescore() != notes[0].basescore()) {
            all_same = false;
            break;
        }
    if (all_same)
        throw std::invalid_argument("MultSqueeze chord has no squeezable notes");
}
```

  with:

```cpp
bool MultSqueeze::applies(const Chord& chord, int combo) {
    switch (combo) {
        case 7: case 8: case 17: case 18: case 27: case 28: break;
        default: return false;
    }
    const int mod = (chord.count() + combo) % 10;
    if (mod != 0 && mod != 1) return false;

    // A chord whose notes are all worth the same has nothing to squeeze.
    const std::vector<ChordNote> notes = chord.notes();
    for (const ChordNote& n : notes)
        if (n.basescore() != notes[0].basescore()) return true;
    return false;
}

void MultSqueeze::validate() const {
    if (!applies(chord_, combo_))
        throw std::invalid_argument("not a multiplier squeeze at this combo");
}
```

  The long comment above `validate` ("A multiplier squeeze is a chord whose notes straddle...") now describes `applies`: leave it where it is, directly above `applies`.

- [ ] **Step 5: Stop throwing in the graph.** In src/search/graph.cpp, inside `ScoreGraph::build`, replace:

```cpp
        try {
            MultSqueeze msq(timestamp.chord, combo_);
            store_multsqueeze(msq);
        } catch (const std::invalid_argument&) {
        }
```

  with:

```cpp
        if (MultSqueeze::applies(timestamp.chord, combo_))
            store_multsqueeze(MultSqueeze(timestamp.chord, combo_));
```

- [ ] **Step 6: Build the views once.** In src/search/engine.cpp, move the two structs `NodeView` and `EdgeView` up to just before `struct Enum`. Their comment (it starts "Cheap value-views over one node/edge") changes its last sentence, "Built per access from the objects.", to "enumerate() builds one per node and edge, once." Replace `struct Enum` with:

```cpp
struct Enum {
    std::vector<const ScoreGraphNode*> nodes;
    std::vector<const ScoreGraphEdge*> edges;
    // One view per node and per edge, in index order, built once at the end
    // of enumerate(): the hot loop indexes these instead of rebuilding a view
    // through hash-map lookups on every read.
    std::vector<NodeView> node_views;
    std::vector<EdgeView> edge_views;
    int32_t start = -1;
};
```

  In `enumerate`, the two maps become locals: add these two lines right after `    Enum en;`:

```cpp
    std::unordered_map<const ScoreGraphNode*, int32_t> node_idx;
    std::unordered_map<const ScoreGraphEdge*, int32_t> edge_idx;
```

  and in the `nid` and `eid` lambdas replace `en.node_idx` with `node_idx` and `en.edge_idx` with `edge_idx`. Then replace the function's last line, `    return en;`, with:

```cpp
    // Every reachable pointer is indexed now, so each view resolves fully.
    auto node_of = [&](const ScoreGraphNode* n) -> int32_t {
        return n == nullptr ? -1 : node_idx.at(n);
    };
    auto edge_of = [&](const ScoreGraphEdge* e) -> int32_t {
        return e == nullptr ? -1 : edge_idx.at(e);
    };

    en.node_views.reserve(en.nodes.size());
    for (const ScoreGraphNode* o : en.nodes) {
        NodeView v;
        v.tick = o->timecode.ticks();
        v.adv_edge = edge_of(o->adv_edge);
        v.branch_edge = edge_of(o->branch_edge);
        v.is_sp = o->is_sp ? 1 : 0;
        en.node_views.push_back(v);
    }

    en.edge_views.reserve(en.edges.size());
    for (const ScoreGraphEdge* o : en.edges) {
        EdgeView v;
        v.dest = node_of(o->dest);
        v.notecount = (int32_t)o->notecount;
        v.basescore = (int32_t)o->basescore;
        v.comboscore = (int32_t)o->comboscore;
        v.spscore = (int32_t)o->spscore;
        v.soloscore = (int32_t)o->soloscore;
        v.accentscore = (int32_t)o->accentscore;
        v.ghostscore = (int32_t)o->ghostscore;
        v.frontend_points = o->frontend_points;
        v.late_sqin_count = o->late_sqin_count;
        v.activation_fill_deadline_ms = o->activation_fill_deadline_ms.value_or(0.0);
        v.sqinout_timing = o->sqinout_timing.value_or(0.0);
        v.sqinout_time = o->sqinout_time ? o->sqinout_time->ticks() : NO_TIME;
        v.sqout_time = o->sqout_time ? o->sqout_time->ticks() : NO_TIME;
        v.sqin_time = o->sqin_time ? o->sqin_time->ticks() : NO_TIME;
        en.edge_views.push_back(v);
    }
    return en;
```

  In `class Engine`, replace the two view builders (as Task 2 left them):

```cpp
    NodeView node(int32_t i) const {
        const ScoreGraphNode* o = en_.nodes[(size_t)i];
        NodeView v;
        v.tick = o->timecode.ticks();
        v.adv_edge = en_.edge_of(o->adv_edge);
        v.branch_edge = en_.edge_of(o->branch_edge);
        v.is_sp = o->is_sp ? 1 : 0;
        return v;
    }
    EdgeView edge(int32_t i) const {
        const ScoreGraphEdge* o = en_.edges[(size_t)i];
        EdgeView v;
        v.dest = en_.node_of(o->dest);
        v.notecount = (int32_t)o->notecount;
        v.basescore = (int32_t)o->basescore;
        v.comboscore = (int32_t)o->comboscore;
        v.spscore = (int32_t)o->spscore;
        v.soloscore = (int32_t)o->soloscore;
        v.accentscore = (int32_t)o->accentscore;
        v.ghostscore = (int32_t)o->ghostscore;
        v.frontend_points = o->frontend_points;
        v.late_sqin_count = o->late_sqin_count;
        v.activation_fill_deadline_ms =
            o->activation_fill_deadline_ms.value_or(0.0);
        v.sqinout_timing = o->sqinout_timing.value_or(0.0);
        v.sqinout_time = o->sqinout_time ? o->sqinout_time->ticks() : NO_TIME;
        v.sqout_time = o->sqout_time ? o->sqout_time->ticks() : NO_TIME;
        v.sqin_time = o->sqin_time ? o->sqin_time->ticks() : NO_TIME;
        return v;
    }
```

  with:

```cpp
    const NodeView& node(int32_t i) const { return en_.node_views[(size_t)i]; }
    const EdgeView& edge(int32_t i) const { return en_.edge_views[(size_t)i]; }
```

  The callers keep their `const NodeView n = node(...)` and `const EdgeView e = edge(...)` lines; copying a view out of the array is cheap and needs no edit.

- [ ] **Step 7: Run it and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, the new `applies` case included. Then run the brief's score-neutral batch diff. Expected: it prints nothing.

- [ ] **Step 8: Measure.** With no other build or test running, run `.\build_cpp.ps1 -Target hydra_bench; .\build-cpp\Release\hydra_bench.exe | Tee-Object "$env:TEMP\t13_after.txt"`. Compare "cap4 d4" and "auto d4" with Step 1. If either is slower than before, stop and report the three sets of numbers (Task 0, before, after) instead of committing.

- [ ] **Step 9: Commit.** Put the measured numbers in the message body.

```bash
git add src/search/engine.cpp src/search/graph.cpp src/core/model.h src/core/model.cpp tests/test_model.cpp
git commit -m "Build the engine's node and edge views once; ask MultSqueeze::applies

hydra_bench (best of 3), Task 0 / before / after:
  cap4 d4: <t0>s / <before>s / <after>s
  auto d4: <t0>s / <before>s / <after>s

Task: Task 13: Engine speed
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

  Replace the six `<...>` fields with the measured numbers before committing.

```json:metadata
{"files":["src/search/engine.cpp","src/search/graph.cpp","src/core/model.h","src/core/model.cpp","tests/test_model.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_bench; .\\build-cpp\\Release\\hydra_bench.exe","acceptanceCriteria":["MultSqueeze::applies test passes.","Full hydra_tests passes.","The score-neutral batch diff prints nothing.","hydra_bench cap4 d4 and auto d4 are each at or below the parent commit's, with numbers in the commit message."],"modelTier":"standard"}
```

---

### Task 14: The build ships no tests, compiles each file once, and loses its leftovers

The release exe carries the GUI test suite and the repo's folder paths. CMakeLists.txt always adds the three GUI test files to `Hydra` and always defines `HYDRA_UITEST_ATTACHED`, `HYDRA_INPUT_DIR`, `HYDRA_ASSET_DIR` and `HYDRA_RESOURCE_DIR` for it. So every installed Hydra.exe holds strings like `C:/Users/Patrick/Downloads/Hydra/hydra-test/testdata/input`. This task makes attached testing a CMake option. It stays on for dev builds in build-cpp. A new `ship` preset builds in its own folder, build-ship, with it off, and the installer builds from that preset. The installer script also refuses to package an exe that still holds those paths. One limit stays: third_party/imgui/imconfig.h turns the Test Engine's hooks on for every ImGui build, so the engine's core library still links. Only Hydra's tests and paths leave.

The same three GUI test files also compile twice today, once for `Hydra` and once for `hydra_uitest`. They move into one static library, `hydra_uitest_harness`, which both link. A static library is an archive of compiled objects that the linker pulls from.

Then a set of build leftovers, each small:

`SQLITE_ENABLE_FTS5=0` turns full-text search on, not off. SQLite only asks whether the macro is defined. sqlite.org/compile.html says the value does not matter ("the following compilation switches all have the same effect" for 0 and 1), and the vendored sqlite3.c tests it with `#ifdef SQLITE_ENABLE_FTS5`. The line goes. A test checks `sqlite3_compileoption_used("ENABLE_FTS5")` is 0.

`icons.cpp` gets `/W1` and a `third_party/stb` include path. Neither is needed since icons decode through `image/decode.h`. Removing `/W1` puts the file under `/W4`, where its narrow `std::fopen` raises warning C4996. It switches to `hydra::fopen_utf8`, which also lets the icons load from a folder with non-ASCII characters.

`shell32` is linked by hydra_core, hydra_ui and Hydra. Only hydra_core needs it (report_files.cpp calls `ShellExecuteW`), and ImGui also pulls it in with its own `#pragma comment(lib, "shell32")`. The hydra_ui and Hydra copies go.

`/utf-8` is set only on hydra_dm. Every source file under src, tests and tools is valid UTF-8 today (checked: 88 files hold non-ASCII bytes, none invalid). Without the flag MSVC reads them in the system code page, which only works on an English Windows by luck. The flag moves onto every Hydra target through the `hydra_warnings` interface target, C++ only, so the resource compiler never sees it.

miniaudio compiles its high-level engine, node graph, resource manager and every audio backend. Hydra uses only the device, the data converter and the decoders. A search of src, tests and tools finds no `ma_engine`, `ma_sound`, `ma_node` or `ma_resource_manager`. miniaudio's own option table (miniaudio.h, around line 584) names the switches: `MA_NO_ENGINE`, `MA_NO_NODE_GRAPH`, `MA_NO_RESOURCE_MANAGER`, and `MA_ENABLE_ONLY_SPECIFIC_BACKENDS` with `MA_ENABLE_WASAPI`. They become PUBLIC definitions on the miniaudio target, so every file that includes miniaudio.h sees the same set. The hand-copied `#define`s in miniaudio.c, decode.cpp and mixer.cpp go. The comment in miniaudio.c also claims miniaudio's decoders are compiled out; they are not (decode.cpp uses `ma_decoder` for WAV, MP3 and FLAC), so it is corrected.

Link-time optimization (LTO) lets the compiler optimize across .cpp files at link time instead of one file at a time. CMake calls it `INTERPROCEDURAL_OPTIMIZATION`. This task turns it on for hydra_core, which holds the search engine, and the programs that link it, in Release only. It stays only if hydra_bench shows at least a 3% gain; otherwise the step is dropped and the numbers go in the commit message.

The rest is housekeeping. The two presets share build-cpp with different generators, so switching breaks configure; `vs2022` gets build-cpp-vs2022. `Find-CMake` is copied in build_cpp.ps1 and build_installer.ps1; it moves to one shared script. `hydra_bench` and `hydra_replay` are excluded from the default build and can rot unseen; build_cpp.ps1 now builds them in a plain run. Stale comments go: the CMake header's target list, "until it's wired in" for libopus, and the ADR citations. docs/archive/CPP_PORT_PLAN.md is deleted (nothing links to it; git history keeps it). src/image/decode.h cites "docs/adr/0007 and the note in render/mesh.h"; neither exists (git log shows no 0007 ever), so the citation goes.

What the user sees: nothing. The installed Hydra.exe no longer answers `--uitest`, which no user runs.

**Depends on:** nothing.

**Expected overlaps:** Task 8 edits src/audio/mixer.cpp and src/audio/decode.cpp function bodies; this task deletes only their miniaudio `#define` lines near the top. Tasks 1 and 9 may append to tests/test_store.cpp; this task appends one case at the end. Task 4 edits README.md's command-line section and may add test files to the `hydra_tests` list; this task edits README's "Building from source" and "Developer tools" paragraphs and different CMake blocks. Task 18 adds GUI tests: new code in tests/ui/uitest_tests.cpp needs no CMake change, but a new tests/ui source file now goes into `hydra_uitest_harness`, not the old two lists; it may also edit docs/agents/ui-testing.md (this task adds one sentence to the attached-mode section). Whichever task owns the audit's non-ASCII-path item may also change `load_png_texture` in src/ui/icons.cpp; the change there is the same one-line switch to `fopen_utf8`, so keep one copy. Task 2 or any task that adds a source file edits CMakeLists.txt target lists. Task 13 measures engine speed with hydra_bench after this lands, so its baseline includes LTO if LTO is kept.

**Goal:** Ship an exe with no test harness or repo paths, compile the GUI tests once, and clear the build leftovers, with every score unchanged.

**Files:**
- Create: `tools/find_cmake.ps1`, `tests/test_audio_device.cpp`
- Modify: `CMakeLists.txt`, `CMakePresets.json`, `build_cpp.ps1`, `installer/build_installer.ps1`, `.gitignore`, `README.md`, `docs/agents/ui-testing.md`
- Modify: `third_party/miniaudio/miniaudio.c`, `src/audio/decode.cpp`, `src/audio/mixer.cpp` (miniaudio `#define` lines only)
- Modify: `src/ui/icons.cpp`, `src/image/decode.h`
- Delete: `docs/archive/CPP_PORT_PLAN.md`
- Test: `tests/test_store.cpp`, `tests/test_audio_device.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="the vendored SQLite is built without FTS5"` passes (it fails before the change).
- [ ] `hydra_tests.exe -tc="PreviewAudioDevice opens and starts the default output device"` passes with miniaudio trimmed to WASAPI.
- [ ] After `.\build_cpp.ps1 -Preset ship -Target Hydra`, the ship-exe path check in Step 12 prints nothing, and the same check on `build-cpp\Release\Hydra.exe` prints all four strings (the dev exe keeps attached mode).
- [ ] `Select-String build-cpp\Hydra.vcxproj -Pattern 'uitest_harness.cpp|uitest_tests.cpp|uitest_script.cpp'` prints nothing.
- [ ] `Select-String build-cpp\*.vcxproj -Pattern '/utf-8' -List | ForEach-Object Filename` lists hydra_core, hydra_audio, hydra_render, hydra_dm, hydra_ui, hydra_image, hydra_uitest_harness, Hydra, hydra_batch, hydra_report, hydra_fillcompare, hydra_tests and hydra_uitest.
- [ ] miniaudio.lib holds `ma_device_init` and none of `ma_engine_init`, `ma_node_graph_init`, `ma_resource_manager_init` (Step 8 check), and its size before and after is in the commit message.
- [ ] The build log of `.\build_cpp.ps1 -Configure` shows no warning from icons.cpp (`Select-String` on the log for `icons.cpp.*warning` prints nothing).
- [ ] `.\build_cpp.ps1` with no -Target builds hydra_bench.exe and hydra_replay.exe; `.\build_cpp.ps1 -Preset vs2022 -Configure` configures build-cpp-vs2022 and leaves build-cpp untouched.
- [ ] `Select-String build_cpp.ps1, installer\build_installer.ps1 -Pattern 'function Find-CMake'` prints nothing.
- [ ] `Test-Path docs\archive\CPP_PORT_PLAN.md` is False; `Select-String src\image\decode.h, CMakeLists.txt -Pattern 'adr/0007|render/mesh.h|adr/0008\)'` prints nothing.
- [ ] hydra_bench before/after numbers for LTO are in the commit message, and LTO is kept only at a gain of 3% or more.
- [ ] The global score-neutral batch proof prints nothing; full `hydra_tests.exe` ends `Status: SUCCESS!`; `hydra_uitest.exe --all` passes.

**Verify:** `.\build_cpp.ps1 -Configure; .\build-cpp\Release\hydra_tests.exe -tc="the vendored SQLite*,PreviewAudioDevice*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing FTS5 test.** Append to tests/test_store.cpp (it already includes `<sqlite3.h>`):

```cpp
// SQLite switches a feature on when its SQLITE_ENABLE_* macro is defined at
// all, whatever its value (sqlite.org/compile.html), so the old
// "SQLITE_ENABLE_FTS5=0" compiled full-text search in. Hydra never uses it.
TEST_CASE("the vendored SQLite is built without FTS5") {
    CHECK(sqlite3_compileoption_used("ENABLE_FTS5") == 0);
}
```

- [ ] **Step 2: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="the vendored SQLite*"`. Expected: the CHECK fails with `1 == 0`.

- [ ] **Step 3: Add the device guard test.** It passes today. It guards Step 8: if the WASAPI-only miniaudio could not open a device, the Preview would go silent. It opens the machine's default output device, so it needs one; every dev machine running hydra_tests has one. Create tests/test_audio_device.cpp:

```cpp
// Tests for audio/device: the one place Hydra opens a real output device.
// miniaudio is built with only its WASAPI backend (CMakeLists.txt), so this
// proves that backend still opens and starts the default device. It needs a
// machine with an audio output, which every Hydra dev machine has.

#include "doctest.h"

#include <algorithm>
#include <cstdint>
#include <memory>

#include "audio/device.h"

using namespace hydra::audio;

TEST_CASE("PreviewAudioDevice opens and starts the default output device") {
    set_headless(false);  // the real device, whatever an earlier case set
    std::unique_ptr<PreviewAudioDevice> device;
    REQUIRE_NOTHROW(device = std::make_unique<PreviewAudioDevice>(
                        2, 48000, [](float* out, int64_t frames) {
                            std::fill(out, out + frames * 2, 0.0f);
                            return int64_t{0};
                        }));
    device->start();
    device.reset();  // stops the device and waits for the callback first
}
```

In CMakeLists.txt add `    tests/test_audio_device.cpp` to `add_executable(hydra_tests ...)` right after `    tests/test_audio_player.cpp`. Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="PreviewAudioDevice*"`. Expected: `Status: SUCCESS!` before any other change.

- [ ] **Step 4: Record the "before" numbers.** With no other build or test running:

```powershell
$m = "$env:TEMP\hydra_t14"; New-Item -ItemType Directory -Force $m | Out-Null
(Get-Item build-cpp\Release\miniaudio.lib).Length | Set-Content "$m\miniaudio_before.txt"
.\build_cpp.ps1 -Target hydra_bench
.\build-cpp\Release\hydra_bench.exe | Select-String 'best of 3' | Set-Content "$m\bench_before_1.txt"
.\build-cpp\Release\hydra_bench.exe | Select-String 'best of 3' | Set-Content "$m\bench_before_2.txt"
```

Expected: two files of `<config> : <seconds>s (best of 3)` lines.

- [ ] **Step 5: CMakeLists.txt, the leftovers.** Make these edits.

Replace the header lines 1 to 9:

```cmake
# Hydra — Windows-only C++ build. Produces:
#   * Hydra.exe                       — the GUI
#   * hydra_batch / hydra_report      — the command line tools
#   * hydra_tests                     — the doctest harness
#   * a zip release via CPack (build_cpp.ps1 -Package)
#
# Third-party code (ImGui, SQLite, miniz, doctest, json, stb) is vendored under
# third_party/ and built here so there is nothing to install but a compiler +
# CMake.
```

with:

```cmake
# Hydra — Windows-only C++ build. Produces:
#   * Hydra.exe                       — the GUI
#   * hydra_batch / hydra_report / hydra_fillcompare — the command line tools
#   * hydra_tests                     — the doctest harness
#   * hydra_uitest                    — the headless GUI test runner
#   * hydra_bench / hydra_replay      — developer tools (build_cpp.ps1 builds them)
#   * a zip release via CPack (build_cpp.ps1 -Package)
#
# Third-party code (ImGui and its Test Engine, SQLite, miniz, doctest, json,
# stb, miniaudio, libogg, libopus) is vendored under third_party/ and built
# here, so there is nothing to install but a compiler + CMake.
```

Replace:

```cmake
add_library(hydra_warnings INTERFACE)
target_compile_options(hydra_warnings INTERFACE /W4 /EHsc)
```

with:

```cmake
# /utf-8: our sources are UTF-8, and several hold non-ASCII text in string
# literals (em dashes, arrows). Without it MSVC reads them in the system code
# page, which works on an English Windows only by luck. C++ only, so the
# resource compiler never sees it.
add_library(hydra_warnings INTERFACE)
target_compile_options(hydra_warnings INTERFACE
    /W4 /EHsc $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
```

Delete the line `target_compile_definitions(sqlite3 PRIVATE SQLITE_ENABLE_FTS5=0)`.

Replace:

```cmake
# EXCLUDE_FROM_ALL: each of these builds only when a Preview module links it,
# so a build that doesn't reach the Preview is byte-for-byte unaffected and
# the heavy libopus compile stays off the default path until it's wired in.
#
# miniaudio drives the output device, resampling, and mixing; each audio
# format is decoded to PCM by dedicated code (stb_vorbis for OGG, libopus +
# libogg for OPUS, miniaudio's own dr_libs for MP3/WAV/FLAC).
add_library(miniaudio STATIC EXCLUDE_FROM_ALL third_party/miniaudio/miniaudio.c)
target_include_directories(miniaudio PUBLIC third_party/miniaudio)
target_compile_options(miniaudio PRIVATE /W1)
```

with:

```cmake
# EXCLUDE_FROM_ALL: each of these builds only when a target that links it is
# built. hydra_audio links all four, so the CLI tools never compile them.
#
# miniaudio drives the output device and converts sample rates and channel
# counts; each audio format is decoded to PCM by dedicated code (stb_vorbis
# for OGG, libopus + libogg for OPUS, miniaudio's own dr_libs for MP3/WAV/FLAC).
add_library(miniaudio STATIC EXCLUDE_FROM_ALL third_party/miniaudio/miniaudio.c)
target_include_directories(miniaudio PUBLIC third_party/miniaudio)
target_compile_options(miniaudio PRIVATE /W1)
# Compile out what Hydra never calls (option names from miniaudio.h's own
# table): encoders, generators, the high-level engine, its node graph and
# resource manager, and every backend but WASAPI. PUBLIC so every file that
# includes miniaudio.h sees the same declarations as the implementation.
target_compile_definitions(miniaudio PUBLIC
    MA_NO_ENCODING
    MA_NO_GENERATION
    MA_NO_ENGINE
    MA_NO_NODE_GRAPH
    MA_NO_RESOURCE_MANAGER
    MA_ENABLE_ONLY_SPECIFIC_BACKENDS
    MA_ENABLE_WASAPI
)
```

In the hydra_image block, replace:

```cmake
# renderer's textures decode images. So the STB_IMAGE_IMPLEMENTATION lives
# here behind image/decode.h and both link this one lib (docs/adr/0008).
add_library(hydra_image STATIC src/image/decode.cpp)
target_include_directories(hydra_image PUBLIC src PRIVATE third_party/stb)
# stb_image.h is third-party; keep it out of the /W4 sweep like icons.cpp.
target_compile_options(hydra_image PRIVATE /W1 /EHsc)
```

with:

```cmake
# renderer's textures decode images. So the STB_IMAGE_IMPLEMENTATION lives
# here behind image/decode.h and both link this one lib.
add_library(hydra_image STATIC src/image/decode.cpp)
target_include_directories(hydra_image PUBLIC src PRIVATE third_party/stb)
# stb_image.h is third-party; keep it out of the /W4 sweep. /utf-8 as for
# every Hydra target (see hydra_warnings).
target_compile_options(hydra_image PRIVATE /W1 /EHsc /utf-8)
```

In the hydra_dm block, replace:

```cmake
# comparison page. third_party/json parses the leaderboard API responses.
# /utf-8: dm_report.cpp's HTML template embeds non-ASCII glyphs (arrows, em
# dash) in string literals; without this MSVC reads the source in the system
# code page and can mangle them.
```

with:

```cmake
# comparison page. third_party/json parses the leaderboard API responses.
```

and delete the line `target_compile_options(hydra_dm PRIVATE /utf-8)` (hydra_warnings now carries it).

In the hydra_ui block, delete:

```cmake
# stb_image.h is third-party; keep icons.cpp out of our /W4 sweep like the
# other vendored libraries above.
set_source_files_properties(src/ui/icons.cpp PROPERTIES COMPILE_OPTIONS "/W1")
target_include_directories(hydra_ui PRIVATE third_party/stb)
```

and replace:

```cmake
# shell32/ole32: the folder-picker dialog (opening reports moved down to
# hydra_core's report_files.cpp with the rest of the report-file handling);
# hydra_image: icons.cpp decodes its PNGs through the shared stb TU.
target_link_libraries(hydra_ui PRIVATE ole32 shell32 hydra_image hydra_warnings)
```

with:

```cmake
# ole32: the folder-picker dialog (COM's IFileOpenDialog). shell32 is not
# needed here: hydra_core links it for report_files.cpp's ShellExecuteW.
# hydra_image: icons.cpp decodes its PNGs through the shared stb TU.
target_link_libraries(hydra_ui PRIVATE ole32 hydra_image hydra_warnings)
```

Change `target_link_libraries(Hydra PRIVATE hydra_ui d3d11 dxgi d3dcompiler shell32 hydra_warnings)` to `target_link_libraries(Hydra PRIVATE hydra_ui d3d11 dxgi d3dcompiler hydra_warnings)`.

- [ ] **Step 6: CMakeLists.txt, the GUI tests compile once and leave the ship build.** Right before the line `# ---- Hydra.exe: the GUI shell ---...`, add:

```cmake
# Attached GUI testing (`Hydra.exe --uitest ...`, docs/agents/ui-testing.md)
# builds the GUI tests into the exe. On for dev builds; the "ship" preset (the
# installer's build) turns it off, so the shipped exe carries no tests and no
# repo paths.
option(HYDRA_UITEST_ATTACHED
       "Build Hydra.exe with the attached GUI test harness (off for the installer)" ON)

# ---- hydra_uitest_harness: the GUI tests, compiled once --------------------
# The harness, the script runner and the tests. hydra_uitest always links
# them; Hydra.exe links them when HYDRA_UITEST_ATTACHED is on. The repo paths
# the tests need are compiled in here and nowhere else.
add_library(hydra_uitest_harness STATIC
    tests/ui/uitest_harness.cpp
    tests/ui/uitest_script.cpp
    tests/ui/uitest_tests.cpp
)
target_include_directories(hydra_uitest_harness PUBLIC tests/ui)
target_link_libraries(hydra_uitest_harness PUBLIC hydra_ui PRIVATE hydra_warnings)
target_compile_definitions(hydra_uitest_harness PRIVATE
    HYDRA_INPUT_DIR="${CMAKE_SOURCE_DIR}/testdata/input"
    HYDRA_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/preview"
    HYDRA_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resource"
)
```

Replace:

```cmake
# Attached GUI testing (`Hydra.exe --uitest ...`, docs/agents/ui-testing.md):
# the exe carries the test harness + tests so a test can run inside the real
# window at watchable speed.
target_sources(Hydra PRIVATE
    tests/ui/uitest_harness.cpp
    tests/ui/uitest_script.cpp
    tests/ui/uitest_tests.cpp
)
target_include_directories(Hydra PRIVATE tests/ui)
target_compile_definitions(Hydra PRIVATE
    HYDRA_UITEST_ATTACHED
    HYDRA_INPUT_DIR="${CMAKE_SOURCE_DIR}/testdata/input"
    HYDRA_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/preview"
    HYDRA_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resource"
)
```

with:

```cmake
# Attached GUI testing: a test runs inside the real window at watchable speed.
# main.cpp compiles the --uitest path only under HYDRA_UITEST_ATTACHED.
if(HYDRA_UITEST_ATTACHED)
    target_link_libraries(Hydra PRIVATE hydra_uitest_harness)
    target_compile_definitions(Hydra PRIVATE HYDRA_UITEST_ATTACHED)
endif()
```

Replace the hydra_uitest target:

```cmake
add_executable(hydra_uitest
    tests/ui/uitest_main.cpp
    tests/ui/uitest_harness.cpp
    tests/ui/uitest_script.cpp
    tests/ui/uitest_tests.cpp
)
target_link_libraries(hydra_uitest PRIVATE hydra_ui d3d11 dxgi d3dcompiler hydra_warnings)
target_compile_definitions(hydra_uitest PRIVATE
    HYDRA_INPUT_DIR="${CMAKE_SOURCE_DIR}/testdata/input"
    HYDRA_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/preview"
    HYDRA_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resource"
)
```

with:

```cmake
add_executable(hydra_uitest tests/ui/uitest_main.cpp)
target_link_libraries(hydra_uitest PRIVATE
    hydra_uitest_harness d3d11 dxgi d3dcompiler hydra_warnings)
```

(Only uitest_harness.cpp reads the three `HYDRA_*_DIR` macros; uitest_main.cpp does not.)

- [ ] **Step 7: The source files.** In third_party/miniaudio/miniaudio.c replace the whole file with:

```c
/* Single implementation translation unit for miniaudio.
 * Hydra uses miniaudio for the output device (WASAPI only), sample-rate and
 * channel conversion, and its dr_libs decoders for WAV, MP3 and FLAC. OGG
 * goes to stb_vorbis and OPUS to libopus instead. The MA_NO_* and
 * MA_ENABLE_* switches are PUBLIC definitions on the miniaudio target in
 * CMakeLists.txt, so this file and every includer see the same set. */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
```

In src/audio/decode.cpp replace:

```cpp
// Match the macros the miniaudio implementation TU (third_party/miniaudio.c) was
// compiled with, so the declarations here agree with those definitions.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"
```

with:

```cpp
// miniaudio's configuration macros come from the miniaudio target
// (CMakeLists.txt), the same set its implementation TU is compiled with.
#include "miniaudio.h"
```

In src/audio/mixer.cpp replace:

```cpp
// Match the macros the miniaudio implementation TU was compiled with.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"
```

with:

```cpp
// miniaudio's configuration macros come from the miniaudio target.
#include "miniaudio.h"
```

In src/ui/icons.cpp add `#include "core/winstr.h"` after `#include "app/config.h"`, and change:

```cpp
    FILE* f = std::fopen(path, "rb");
```

to:

```cpp
    // UTF-8 path through the wide API, so an install folder with non-ASCII
    // characters still finds its icons.
    FILE* f = hydra::fopen_utf8(path, L"rb");
```

In src/image/decode.h replace:

```cpp
// both link this tiny library instead of each defining their own copy (see
// docs/adr/0007 and the note in render/mesh.h).
```

with:

```cpp
// both link this tiny library instead of each defining their own copy.
```

- [ ] **Step 8: Rebuild, run the new tests, check miniaudio.** Run `.\build_cpp.ps1 -Configure *>&1 | Tee-Object "$env:TEMP\hydra_t14\build.log"`. Then:

```powershell
Select-String "$env:TEMP\hydra_t14\build.log" -Pattern 'icons\.cpp.*warning'
.\build-cpp\Release\hydra_tests.exe -tc="the vendored SQLite*,PreviewAudioDevice*,decode_audio*,*mix*"
$lib = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes("build-cpp\Release\miniaudio.lib"))
'ma_device_init','ma_engine_init','ma_node_graph_init','ma_resource_manager_init' | ForEach-Object { "{0} {1}" -f $_, $lib.Contains($_) }
"miniaudio.lib bytes: before $(Get-Content $env:TEMP\hydra_t14\miniaudio_before.txt), after $((Get-Item build-cpp\Release\miniaudio.lib).Length)"
```

Expected: the first `Select-String` prints nothing; tests end `Status: SUCCESS!` (FTS5 now 0, the device still opens); the symbol lines read `ma_device_init True` and the other three `False`; the after size is smaller. If icons.cpp shows any other /W4 warning, fix that line in icons.cpp and rebuild.

- [ ] **Step 9: Presets, the shared Find-CMake and build_cpp.ps1.** Replace CMakePresets.json with:

```json
{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
  "configurePresets": [
    {
      "name": "default",
      "displayName": "MSVC x64 (Visual Studio 18 2026)",
      "generator": "Visual Studio 18 2026",
      "architecture": "x64",
      "binaryDir": "${sourceDir}/build-cpp"
    },
    {
      "name": "vs2022",
      "displayName": "MSVC x64 (Visual Studio 17 2022)",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "binaryDir": "${sourceDir}/build-cpp-vs2022"
    },
    {
      "name": "ship",
      "displayName": "Installer build (no attached GUI tests)",
      "inherits": "default",
      "binaryDir": "${sourceDir}/build-ship",
      "cacheVariables": { "HYDRA_UITEST_ATTACHED": { "type": "BOOL", "value": "OFF" } }
    }
  ],
  "buildPresets": [
    {
      "name": "default",
      "configurePreset": "default",
      "configuration": "Release"
    },
    {
      "name": "vs2022",
      "configurePreset": "vs2022",
      "configuration": "Release"
    },
    {
      "name": "ship",
      "configurePreset": "ship",
      "configuration": "Release"
    }
  ]
}
```

Create tools/find_cmake.ps1 with the function moved verbatim from build_cpp.ps1:

```powershell
# Find cmake.exe: PATH first, else the copy bundled with Visual Studio.
# Shared by build_cpp.ps1 and installer\build_installer.ps1. Dot-source it:
#   . (Join-Path <repo> "tools\find_cmake.ps1"); $cmake = Find-CMake

function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        # -latest only reports instances in a "complete" state; a VS with a
        # pending update reports nothing there but still shows under -all.
        if (-not $vs) {
            $vs = & $vswhere -all -prerelease -products * -property installationPath
        }
        foreach ($path in @($vs)) {
            $candidate = Join-Path $path "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    throw "cmake.exe not found (not on PATH and no Visual Studio C++ install located)."
}
```

Replace build_cpp.ps1 with:

```powershell
# Configure and build the Hydra C++ port.
#
#   .\build_cpp.ps1              # configure (if needed) + build Release, with
#                                # hydra_bench and hydra_replay
#   .\build_cpp.ps1 -Configure   # force a reconfigure first
#   .\build_cpp.ps1 -Target hydra_tests
#   .\build_cpp.ps1 -Preset ship # the installer's build, in build-ship\
#   .\build_cpp.ps1 -Package     # build, then zip a release (CPack)
#
# CMake and MSVC ship with Visual Studio, so this finds the VS-bundled cmake.exe
# via vswhere rather than requiring cmake on PATH.

param(
    [switch]$Configure,
    [switch]$Package,
    [string]$Target = "",
    [string]$Config = "Release",
    # default: dev build in build-cpp. vs2022: the VS 2022 generator, in its
    # own folder. ship: the installer's build (no attached GUI tests).
    [ValidateSet("default", "vs2022", "ship")]
    [string]$Preset = "default"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

. (Join-Path $root "tools\find_cmake.ps1")
$cmake = Find-CMake
Write-Host "Using cmake: $cmake"

# Must match each preset's binaryDir in CMakePresets.json.
$buildDirs = @{ default = "build-cpp"; vs2022 = "build-cpp-vs2022"; ship = "build-ship" }
$buildDir = Join-Path $root $buildDirs[$Preset]
if ($Configure -or -not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    & $cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
}

$buildArgs = @("--build", "--preset", $Preset, "--config", $Config)
if ($Target -ne "") {
    $buildArgs += @("--target", $Target)
} else {
    # hydra_bench and hydra_replay are EXCLUDE_FROM_ALL (not shipped), so a
    # plain build names them too; otherwise they could break unnoticed.
    $buildArgs += @("--target", "ALL_BUILD", "hydra_bench", "hydra_replay")
}
& $cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host "Build succeeded. Artifacts in $buildDir\$Config\"

if ($Package) {
    # cpack.exe sits next to cmake.exe in the VS bundle.
    $cpack = Join-Path (Split-Path $cmake) "cpack.exe"
    Push-Location $buildDir
    try {
        & $cpack -G ZIP -C $Config
        if ($LASTEXITCODE -ne 0) { throw "cpack failed" }
    } finally {
        Pop-Location
    }
    Write-Host "Package written to $buildDir\package\"
}
```

- [ ] **Step 10: The installer builds the ship preset and guards the exe.** In installer/build_installer.ps1, replace the header lines:

```powershell
# The staging step uses `cmake --install`, never a glob of build-cpp\Release:
# that folder accumulates dev hydra*.db files (hundreds of MB) that must never
# ship. The install() rules in CMakeLists.txt define the exact ship list.
```

with:

```powershell
# It builds the "ship" preset in build-ship\, where Hydra.exe has no attached
# GUI tests and so no repo paths. The staging step uses `cmake --install`,
# never a glob of a Release folder: dev folders accumulate hydra*.db files
# (hundreds of MB) that must never ship. The install() rules in
# CMakeLists.txt define the exact ship list.
```

Delete the whole `function Find-CMake { ... }` block and the comment line above it (`# Same lookup as build_cpp.ps1: prefer PATH, else the VS-bundled cmake.`), and put in its place:

```powershell
. (Join-Path $repo "tools\find_cmake.ps1")
```

Replace:

```powershell
# 1. Build Release.
if (-not $SkipBuild) {
    & (Join-Path $repo "build_cpp.ps1")
}
```

with:

```powershell
# 1. Build Release with the ship preset.
if (-not $SkipBuild) {
    & (Join-Path $repo "build_cpp.ps1") -Preset ship
}
$build = Join-Path $repo "build-ship"
```

Replace:

```powershell
$stage = Join-Path $repo "build-cpp\stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
& $cmake --install (Join-Path $repo "build-cpp") --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }
```

with:

```powershell
$stage = Join-Path $build "stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
& $cmake --install $build --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }
```

After the existing user-data guard (`if ($leaked) { throw ... }`) add:

```powershell
# Guard the other invariant: the shipped exe holds no repo paths. They come
# only from the attached GUI tests, which the ship preset leaves out.
$srcDir = $repo -replace '\\', '/'
$exeText = [Text.Encoding]::GetEncoding(28591).GetString(
    [IO.File]::ReadAllBytes((Join-Path $stage "Hydra.exe")))
foreach ($p in "$srcDir/testdata/input", "$srcDir/assets/preview", "$srcDir/resource") {
    if ($exeText.Contains($p)) { throw "Hydra.exe holds the repo path $p; build it with -Preset ship" }
}
```

The installer's output folder (hydra.iss `OutputDir`, build-cpp\installer) does not change.

- [ ] **Step 11: .gitignore, README and ui-testing.md.** In .gitignore, change:

```
# C++ build tree (CMake) and IDE state.
build-cpp/
```

to:

```
# C++ build trees (CMake presets: default, vs2022, ship) and IDE state.
build-cpp/
build-cpp-vs2022/
build-ship/
```

In README.md replace:

```
.\build_cpp.ps1              # configure + build everything (Release)
.\build_cpp.ps1 -Package     # ...then zip a release (build-cpp\package\)
```

with:

```
.\build_cpp.ps1              # configure + build everything (Release), dev tools too
.\build_cpp.ps1 -Preset ship -Package   # zip a release without the GUI tests
```

and replace:

```
It builds Release, stages the ship list via `cmake --install` (so stray user
```

with:

```
It builds Release with the `ship` preset (in `build-ship\`, without the
attached GUI tests), stages the ship list via `cmake --install` (so stray user
```

and replace:

```
Two more console programs live in `tools/` and are built on demand
(`.\build_cpp.ps1 -Target hydra_bench`), not shipped. `hydra_bench` times the
```

with:

```
Two more console programs live in `tools/`. A plain `.\build_cpp.ps1` builds
them; they are not shipped. `hydra_bench` times the
```

In docs/agents/ui-testing.md, after the paragraph that begins "Runs the same test inside the real window at human speed", add:

```
Attached mode exists only in dev builds (`build-cpp`). The installer builds with the `ship` preset, which leaves the GUI tests out, so an installed Hydra.exe ignores `--uitest`.
```

- [ ] **Step 12: Prove the ship exe is clean and the dev exe is not.** Run:

```powershell
.\build_cpp.ps1 -Preset ship -Target Hydra
$src = (Resolve-Path .).Path -replace '\\', '/'
foreach ($exe in 'build-ship\Release\Hydra.exe', 'build-cpp\Release\Hydra.exe') {
    "== $exe"
    $t = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($exe))
    "$src/testdata/input", "$src/assets/preview", "$src/resource", "squeezed_out_uncounted" |
        Where-Object { $t.Contains($_) }
}
```

Expected: nothing under `== build-ship\Release\Hydra.exe`; all four strings under `== build-cpp\Release\Hydra.exe`. Then run `Select-String build-cpp\Hydra.vcxproj -Pattern 'uitest_harness.cpp|uitest_tests.cpp|uitest_script.cpp'` (expected: nothing) and the `/utf-8` listing from the Acceptance Criteria (expected: the thirteen targets). Run `.\build_cpp.ps1 -Preset vs2022 -Configure` only if Visual Studio 2022 is installed; expected: it configures build-cpp-vs2022, and `build-cpp\CMakeCache.txt` still names `Visual Studio 18 2026`.

- [ ] **Step 13: LTO, kept only if it pays.** Append to the end of CMakeLists.txt:

```cmake
# ---- link-time optimization for the search engine --------------------------
# hydra_core holds the search engine (src/search). LTO lets the compiler
# optimize across its .cpp files at link time. Release only, so other
# configs link at normal speed. Kept because hydra_bench measured a gain
# (numbers in the commit that added it).
include(CheckIPOSupported)
check_ipo_supported(RESULT hydra_ipo_ok OUTPUT hydra_ipo_error LANGUAGES CXX)
if(hydra_ipo_ok)
    foreach(t IN ITEMS hydra_core Hydra hydra_batch hydra_report hydra_fillcompare
                       hydra_tests hydra_uitest hydra_bench hydra_replay)
        set_property(TARGET ${t} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endforeach()
else()
    message(STATUS "LTO not available: ${hydra_ipo_error}")
endif()
```

With nothing else running:

```powershell
$m = "$env:TEMP\hydra_t14"
.\build_cpp.ps1 -Configure -Target hydra_bench
.\build-cpp\Release\hydra_bench.exe | Select-String 'best of 3' | Set-Content "$m\bench_after_1.txt"
.\build-cpp\Release\hydra_bench.exe | Select-String 'best of 3' | Set-Content "$m\bench_after_2.txt"
function Sum-Bench($f) { (Get-Content $f | ForEach-Object { [double]([regex]::Match($_, ':\s+([\d.]+)s').Groups[1].Value) } | Measure-Object -Sum).Sum }
$before = [math]::Min((Sum-Bench "$m\bench_before_1.txt"), (Sum-Bench "$m\bench_before_2.txt"))
$after = [math]::Min((Sum-Bench "$m\bench_after_1.txt"), (Sum-Bench "$m\bench_after_2.txt"))
"before {0:N2}s after {1:N2}s change {2:P1}" -f $before, $after, (($after - $before) / $before)
```

Expected: one line with both totals. If the change is -3.0% or better (faster), keep the block. Otherwise delete the block, run `.\build_cpp.ps1 -Configure`, and say "LTO dropped" in the commit message. Either way, the before/after line goes in the commit message.

- [ ] **Step 14: Delete the stale plan.** Run `git rm docs/archive/CPP_PORT_PLAN.md`. Nothing in the repo links to it (`git grep CPP_PORT_PLAN` prints nothing), and git history keeps it.

- [ ] **Step 15: Full proof.** Run `.\build_cpp.ps1 -Configure`. Expected: hydra_bench.exe and hydra_replay.exe appear in build-cpp\Release. Run `.\build-cpp\Release\hydra_tests.exe` (expected `Status: SUCCESS!`), `.\build-cpp\Release\hydra_uitest.exe --all` (expected: all pass), the global score-neutral batch proof (expected: prints nothing), and `Select-String build_cpp.ps1, installer\build_installer.ps1 -Pattern 'function Find-CMake'` (expected: nothing). If Inno Setup is installed, also run `.\installer\build_installer.ps1`; expected: it finishes with "Installer written to ...", which means the new exe guard passed.

- [ ] **Step 16: Commit.**

```bash
git add CMakeLists.txt CMakePresets.json build_cpp.ps1 tools/find_cmake.ps1 installer/build_installer.ps1 .gitignore README.md docs/agents/ui-testing.md third_party/miniaudio/miniaudio.c src/audio/decode.cpp src/audio/mixer.cpp src/ui/icons.cpp src/image/decode.h tests/test_store.cpp tests/test_audio_device.cpp docs/archive/CPP_PORT_PLAN.md
git commit -m "Ship Hydra.exe without its GUI tests and clear build leftovers

HYDRA_UITEST_ATTACHED is now a CMake option: on in build-cpp, off in
the new ship preset (build-ship) that the installer builds, which also
refuses an exe holding repo paths. The GUI test files compile once, in
hydra_uitest_harness. FTS5 is really off now; /utf-8 covers every Hydra
target; miniaudio drops its engine, node graph, resource manager and
non-WASAPI backends (miniaudio.lib <before> -> <after> bytes); shell32
leaves hydra_ui and Hydra; icons.cpp builds at /W4 via fopen_utf8. The
vs2022 preset gets its own folder, Find-CMake is shared, and a plain
build_cpp.ps1 builds hydra_bench and hydra_replay. LTO on hydra_core:
hydra_bench before <X>s after <Y>s (<kept | dropped>). Removes the stale
CPP_PORT_PLAN and the nonexistent ADR 0007 citation.

Task: Task 14: The build ships no tests, compiles each file once, and loses its leftovers
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Replace `<before>`, `<after>`, `<X>`, `<Y>` and `<kept | dropped>` with the measured values from Steps 8 and 13 before committing.

```json:metadata
{"files":["CMakeLists.txt","CMakePresets.json","build_cpp.ps1","tools/find_cmake.ps1","installer/build_installer.ps1",".gitignore","README.md","docs/agents/ui-testing.md","third_party/miniaudio/miniaudio.c","src/audio/decode.cpp","src/audio/mixer.cpp","src/ui/icons.cpp","src/image/decode.h","tests/test_store.cpp","tests/test_audio_device.cpp","docs/archive/CPP_PORT_PLAN.md"],"verifyCommand":".\\build_cpp.ps1 -Configure; .\\build-cpp\\Release\\hydra_tests.exe -tc=\"the vendored SQLite*,PreviewAudioDevice*\"","acceptanceCriteria":["FTS5 test passes","device test passes on WASAPI-only miniaudio","ship exe holds no repo paths, dev exe keeps attached mode","GUI test files compile once","/utf-8 on every Hydra target","miniaudio.lib trimmed","no icons.cpp warnings","plain build makes hydra_bench and hydra_replay; vs2022 has its own folder","one Find-CMake","CPP_PORT_PLAN and ADR 0007 citation gone","LTO kept only at >=3% gain","batch proof, hydra_tests and hydra_uitest --all pass"],"modelTier":"standard"}
```

---

### Task 15: The vendored opus and ogg lose the files the build never opens

third_party/opus and third_party/ogg carry whole upstream release trees. The build compiles two static libraries from them and ignores most of the rest. Candidates for deletion: opus's deep-learning code in dnn/ (only compiled with deep PLC, DRED or OSCE, all off), its doc/, its three tests/ folders (opus/tests, celt/tests, silk/tests), meson/ and every meson build file, and its autotools scripts. In ogg: doc/, win32/ (a Visual Studio 2015 project and an ogg.def used only for shared builds, which Hydra forces off) and the autotools scripts. That is 262 tracked files, not the audit's "about 220".

Some files look like autotools junk but CMake reads them at configure time, so they stay. opus's cmake/OpusSources.cmake reads every *.mk list and also opus's Makefile.am (for the program and test source lists). cmake/OpusFunctions.cmake reads configure.ac, and cmake/OpusPackageVersion.cmake reads package_version. opus's CMakeLists.txt configures opus.pc.in. ogg's CMakeLists.txt reads configure.ac and configures ogg.pc.in and include/ogg/config_types.h.in. The .mk header lists name headers under celt/arm, celt/mips, celt/x86, silk/fixed and friends, and CMake adds those headers to the target, so a missing one fails the configure step. All of those stay.

The deletion is proved, not assumed. A scratch build compiles opus and ogg with `/showIncludes`, which makes the compiler print every header it opens. That list is joined with every file CMake put into the two projects and every file its configure step depends on. Not one deletion candidate may appear in it. Then the files go, and a clean configure plus a full build and the audio tests pass.

What the user sees: nothing.

**Depends on:** nothing.

**Expected overlaps:** none. Task 14 edits third_party/miniaudio/miniaudio.c, a different folder.

**Goal:** Delete the 262 opus and ogg files the build never opens, proven by a compiler include list, with a clean build still passing.

**Files:**
- Delete: 262 tracked files under `third_party/opus` and `third_party/ogg`, listed exactly by the Step 1 script (whole folders: opus/dnn, opus/doc, opus/tests, opus/celt/tests, opus/silk/tests, opus/meson, opus/m4, ogg/doc, ogg/win32, ogg/m4; single files as named in the script)
- Test: the Step 1, Step 3 and Step 6 checks (no source test file; this task changes no code)

**Acceptance Criteria:**
- [ ] Step 1 prints `candidates: 262` before the deletion.
- [ ] Step 3's intersection check prints `used files: <N>` and no paths.
- [ ] After deletion, `git ls-files third_party/opus third_party/ogg | Measure-Object | ForEach-Object Count` prints 347 (475 + 134 - 262).
- [ ] A clean configure (build-cpp removed first) and `.\build_cpp.ps1` succeed.
- [ ] `hydra_tests.exe -tc="decode_audio*,decode_stem*,decode_and_mix*"` ends `Status: SUCCESS!`, and the full `hydra_tests.exe` ends `Status: SUCCESS!`.

**Verify:** `git ls-files third_party/opus third_party/ogg | Measure-Object | ForEach-Object Count` → `347`

**Steps:**

- [ ] **Step 1: List the candidates.** From the repo root, run `.\build_cpp.ps1 -Target opus; .\build_cpp.ps1 -Target ogg` so build-cpp is configured. Then:

```powershell
$out = "$env:TEMP\hydra_t15"; New-Item -ItemType Directory -Force $out | Out-Null
$dirs = @(
    'third_party/opus/dnn/', 'third_party/opus/doc/', 'third_party/opus/tests/',
    'third_party/opus/celt/tests/', 'third_party/opus/silk/tests/',
    'third_party/opus/meson/', 'third_party/opus/m4/',
    'third_party/ogg/doc/', 'third_party/ogg/win32/', 'third_party/ogg/m4/')
$files = @(
    # opus: meson build files outside the folders above
    'opus/meson.build', 'opus/meson_options.txt', 'opus/celt/meson.build',
    'opus/silk/meson.build', 'opus/src/meson.build', 'opus/include/meson.build',
    # opus: autotools output and scripts, the autoheader template, the
    # uninstalled pkg-config template, and two hand makefiles for other
    # platforms. Makefile.am, configure.ac, package_version, opus.pc.in and
    # every *.mk stay: CMake reads them.
    'opus/aclocal.m4', 'opus/compile', 'opus/config.guess', 'opus/config.sub',
    'opus/configure', 'opus/depcomp', 'opus/install-sh', 'opus/ltmain.sh',
    'opus/missing', 'opus/test-driver', 'opus/Makefile.in', 'opus/config.h.in',
    'opus/opus.m4', 'opus/opus-uninstalled.pc.in', 'opus/Makefile.mips', 'opus/Makefile.unix',
    # ogg: autotools output and scripts, every Makefile.am/.in (ogg's CMake
    # reads none), the RPM spec, the autoheader template. configure.ac,
    # ogg.pc.in and include/ogg/config_types.h.in stay: CMake reads them.
    'ogg/aclocal.m4', 'ogg/compile', 'ogg/config.guess', 'ogg/config.sub',
    'ogg/configure', 'ogg/depcomp', 'ogg/install-sh', 'ogg/ltmain.sh', 'ogg/missing',
    'ogg/Makefile.am', 'ogg/Makefile.in', 'ogg/include/Makefile.am', 'ogg/include/Makefile.in',
    'ogg/include/ogg/Makefile.am', 'ogg/include/ogg/Makefile.in',
    'ogg/src/Makefile.am', 'ogg/src/Makefile.in', 'ogg/config.h.in',
    'ogg/libogg.spec', 'ogg/libogg.spec.in', 'ogg/ogg.m4', 'ogg/ogg-uninstalled.pc.in'
) | ForEach-Object { "third_party/$_" }
$tracked = git ls-files third_party/opus third_party/ogg
$cand = $tracked | Where-Object { $f = $_; ($files -contains $f) -or ($dirs | Where-Object { $f.StartsWith($_) }) }
$cand | Set-Content "$out\candidates.txt"
"candidates: $($cand.Count)"
$files | Where-Object { $tracked -notcontains $_ }
```

Expected: `candidates: 262`, and nothing after it (every named file is tracked).

- [ ] **Step 2: Build opus and ogg in a scratch folder with `/showIncludes`.** The scratch folder keeps the real build-cpp untouched. It reuses the cmake and generator build-cpp was configured with, and CMake's default C flags plus `/showIncludes`:

```powershell
$out = "$env:TEMP\hydra_t15"
$cache = Get-Content build-cpp\CMakeCache.txt
$cmake = ($cache | Select-String '^CMAKE_COMMAND:INTERNAL=(.*)$').Matches[0].Groups[1].Value
$gen = ($cache | Select-String '^CMAKE_GENERATOR:INTERNAL=(.*)$').Matches[0].Groups[1].Value
$probe = "$out\probe"
if (Test-Path $probe) { Remove-Item -Recurse -Force $probe }
& $cmake -S . -B $probe -G $gen -A x64 "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /showIncludes" *> "$out\probe_configure.log"
& $cmake --build $probe --config Release --target opus ogg -- /v:n *> "$out\probe_build.log"
"exit $LASTEXITCODE"
(Select-String "$out\probe_build.log" -Pattern 'Note: including file:').Count
```

Expected: `exit 0` and a count in the thousands. A count of 0 means MSBuild hid the compiler output; rerun the build line with `/v:d` in place of `/v:n`.

- [ ] **Step 3: Prove no candidate is used.** This joins four sources: headers the compiler opened, every file CMake put in the two projects (sources plus listed headers), every input CMake's configure step tracks, and the files CMake reads with `file(STRINGS)` / `file(READ)`, which it does not track:

```powershell
$out = "$env:TEMP\hydra_t15"; $probe = "$out\probe"
$root = (Resolve-Path .).Path
function Norm($p) { ($p.Trim() -replace '/', '\').ToLowerInvariant() }
$used = [System.Collections.Generic.HashSet[string]]::new()
Select-String "$out\probe_build.log" -Pattern 'Note: including file:\s+(.+)$' |
    ForEach-Object { [void]$used.Add((Norm $_.Matches[0].Groups[1].Value)) }
Get-ChildItem $probe -Recurse -Include opus.vcxproj, ogg.vcxproj | ForEach-Object {
    Select-String $_.FullName -Pattern 'Include="([^"]+)"' -AllMatches |
        ForEach-Object { $_.Matches } | ForEach-Object { [void]$used.Add((Norm $_.Groups[1].Value)) } }
Get-ChildItem $probe -Recurse -Filter generate.stamp.depend | Get-Content |
    ForEach-Object { [void]$used.Add((Norm $_)) }
foreach ($f in 'third_party/opus/configure.ac', 'third_party/opus/package_version',
               'third_party/opus/Makefile.am', 'third_party/ogg/configure.ac') {
    [void]$used.Add((Norm (Join-Path $root $f))) }
Get-ChildItem third_party/opus -Filter *.mk | ForEach-Object { [void]$used.Add((Norm $_.FullName)) }
"used files: $($used.Count)"
Get-Content "$out\candidates.txt" | Where-Object { $used.Contains((Norm (Join-Path $root $_))) }
```

Expected: `used files: <N>` and no paths after it. If any path prints, the build opens that file: remove its line from `$out\candidates.txt`, keep the file, and name it in the commit message.

- [ ] **Step 4: Delete the candidates.** Run:

```powershell
git rm -q --pathspec-from-file="$env:TEMP\hydra_t15\candidates.txt"
git ls-files third_party/opus third_party/ogg | Measure-Object | ForEach-Object Count
Test-Path third_party/opus/dnn, third_party/ogg/doc
```

Expected: `347` (or 347 plus the number of files Step 3 kept), then `False` twice.

- [ ] **Step 5: Clean configure and full build.** Run `Remove-Item -Recurse -Force build-cpp; .\build_cpp.ps1`. Expected: `Build succeeded.` A missing header named by a .mk list would stop the configure step with "Cannot find source file"; it must not appear.

- [ ] **Step 6: Run the audio tests and the full suite.** Run `.\build-cpp\Release\hydra_tests.exe -tc="decode_audio*,decode_stem*,decode_and_mix*"`, then `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!` both times. Delete the scratch folder: `Remove-Item -Recurse -Force "$env:TEMP\hydra_t15"`.

- [ ] **Step 7: Commit.**

```bash
git commit -m "Drop the vendored opus and ogg files the build never opens

262 files: opus dnn/, doc/, three tests/ folders, meson files and
autotools scripts; ogg doc/, win32/ and autotools scripts. A scratch
build with /showIncludes, joined with CMake's project sources and its
configure inputs, opened none of them. Makefile.am, configure.ac,
package_version, the .pc.in templates and every .mk list stay because
CMake reads them. Clean configure, full build and hydra_tests pass.

Task: Task 15: The vendored opus and ogg lose the files the build never opens
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

(`git rm` in Step 4 already staged exactly the deleted files, so no `git add` is needed. If Step 3 kept any file, change "262" in the message to the real count and name the kept files.)

```json:metadata
{"files":["third_party/opus","third_party/ogg"],"verifyCommand":"git ls-files third_party/opus third_party/ogg | Measure-Object | ForEach-Object Count","acceptanceCriteria":["Step 1 prints candidates: 262","Step 3 intersection prints no paths","347 tracked files remain","clean configure and full build succeed","audio tests and full hydra_tests pass"],"modelTier":"mechanical"}
```

---

### Task 16: One owner for the small string helpers

Hydra keeps several copies of three tiny string jobs: lowercasing ASCII text, trimming whitespace off both ends of a line, and checking a file extension without caring about case. The audit counted three lowercase helpers, two trims and two case-blind suffix checks. There are more.

For lowercasing there are five named helpers and two inline loops. They are `lower` in analysis.cpp, `ascii_lower` in chart_files.cpp, `to_lower` in preview_source.cpp, `lower_hex` in core/strutil (it lowercases any text, despite its name), and `ascii_casefold` in song.cpp, which nothing calls. The inline loops are in batch.cpp's `same_file` and in library_view.cpp's user filter.

For trimming there are six copies. config.cpp, rules_file.cpp and tests/ui/uitest_script.cpp each carry the same `trim`. song.cpp has `strip`, report.cpp's `plain` ends with its own trim, and tools/replay.cpp has `trimmed`.

For suffix checks, preview_source.cpp has an `ends_with_ci` that is used. analysis.cpp has another `ends_with_ci` that has had no caller since the chart-file names moved into chart_files.cpp (commit 6984716). Plain case-sensitive suffix checks sit in chart_files.cpp (after lowercasing the whole path first), record_store.cpp, tests/test_song.cpp, tests/corpus_util.h and tests/test_midi.cpp.

This task gives each job one owner in src/core/strutil: `to_lower_ascii`, `trim`, `ends_with` and `ends_with_ci`. Every copy is deleted and every caller switches. The two helpers nobody calls are simply deleted. `lower_hex` goes too; its callers use `to_lower_ascii`.

Behaviour stays the same. Every lowercase copy already changed only A to Z, because no code calls `setlocale`, so `std::tolower` only maps ASCII here. The shared `trim` strips space, tab, CR, LF, vertical tab and form feed, like song.cpp's `strip` and `plain` did. The config, rules and uitest-script copies didn't strip vertical tab or form feed, and replay's `trimmed` stripped only spaces and tabs. Those characters don't appear in Hydra's INI files or in a hydra_replay argument in practice, so nothing a user sees changes. Two look-alikes stay on purpose. The trims inside `read_song_ini_keys` (analysis.cpp) strip only spaces and tabs by design, and changing them could change a stored song name. `difficulty_from_name` in song.cpp does a case-blind comparison, not a copy of these helpers.

What the user sees: nothing.

**Depends on:** T5 (it moves the hash index into `report::records_by_hash`, so dm_report.cpp and fill_report.cpp no longer call `lower_hex`). The wave 1 tasks that edit files named here (T1 record_store.cpp, T3 analysis.cpp, T4 batch.cpp, T6 preview_source.cpp, T7 library_view.cpp) are merged before wave 3 by construction.

**Expected overlaps:** T17 also edits tests/corpus_util.h (it appends the cache functions; this task changes only the body of `first_chart_with_suffix`), tests/test_song.cpp (T17 changes two loops' parse lines; this task deletes the file's local `ends_with`) and the `hydra_tests` source list in CMakeLists.txt (T17 adds tests/test_corpus_cache.cpp; this task adds tests/test_strutil.cpp). T7 moves library_view.cpp's function statics into a state struct, so the DM filter buffer may have a new name there; apply the change below on top of whatever name it has.

**Goal:** Lowercasing, trimming and suffix checks each have one implementation, in src/core/strutil, and every copy is gone.

**Files:**
- Create: `tests/test_strutil.cpp`
- Modify: `src/core/strutil.h`, `src/core/strutil.cpp`, `src/app/analysis.cpp`, `src/app/preview_source.cpp`, `src/parse/chart_files.cpp`, `src/parse/song.cpp`, `src/app/config.cpp`, `src/app/rules_file.cpp`, `src/app/report.cpp`, `src/net/dmbot_client.cpp`, `src/cli/batch.cpp`, `src/ui/library_view.cpp`, `src/store/record_store.cpp` (only if `import_legacy_uncapped` still exists), `tools/replay.cpp`, `tests/ui/uitest_script.cpp`, `tests/test_song.cpp`, `tests/test_midi.cpp`, `tests/corpus_util.h`, `CMakeLists.txt`
- Test: `tests/test_strutil.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="strutil*"` passes (three cases).
- [ ] The full `hydra_tests.exe` run ends with `Status: SUCCESS!`, and `hydra_uitest.exe --all` passes.
- [ ] `.\build_cpp.ps1 -Target hydra_replay` builds with no errors.
- [ ] `git grep -nE "(std::string|bool) (lower|ascii_lower|to_lower|ascii_casefold|lower_hex|strip|trim|trimmed|ends_with|ends_with_ci)\(" -- src tests tools` prints only lines in src/core/strutil.h and src/core/strutil.cpp.
- [ ] `git grep -n "std::tolower" -- src tests tools` prints only src/app/report.cpp (the `<color>` tag match in `plain`) and the two lines of `difficulty_from_name` in src/parse/song.cpp.
- [ ] `git grep -n "lower_hex" -- src tests tools` prints nothing.
- [ ] The score-neutral batch recipe prints nothing.

**Verify:** `.\build_cpp.ps1; .\build-cpp\Release\hydra_tests.exe -tc="strutil*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test.** Create tests/test_strutil.cpp:

```cpp
// The one owner of Hydra's small string helpers (core/strutil). Every caller
// that used to carry its own lowercase, trim or suffix check reads these.

#include "doctest.h"

#include <string>

#include "core/strutil.h"

using namespace hydra;

TEST_CASE("strutil: to_lower_ascii lowers A-Z and leaves every other byte") {
    CHECK(to_lower_ascii("Notes.MID") == "notes.mid");
    CHECK(to_lower_ascii("AbCdEF0123") == "abcdef0123");
    // UTF-8 bytes pass through untouched: "ÉTÉ" becomes "ÉtÉ".
    CHECK(to_lower_ascii("\xC3\x89T\xC3\x89") == "\xC3\x89t\xC3\x89");
    CHECK(to_lower_ascii("") == "");
}

TEST_CASE("strutil: trim strips ASCII whitespace from both ends only") {
    CHECK(trim("  key = value \r\n") == "key = value");
    CHECK(trim("\t\v\fx y\f\v\t") == "x y");
    CHECK(trim(" \t\r\n") == "");
    CHECK(trim("") == "");
    CHECK(trim("inner  space") == "inner  space");
}

TEST_CASE("strutil: ends_with is exact and ends_with_ci ignores ASCII case") {
    CHECK(ends_with("song.mid", ".mid"));
    CHECK(ends_with(".mid", ".mid"));
    CHECK_FALSE(ends_with("song.MID", ".mid"));
    CHECK_FALSE(ends_with("mid", ".mid"));
    CHECK(ends_with_ci("SONG.Mid", ".mid"));
    CHECK(ends_with_ci("track.OPUS", ".opus"));
    CHECK_FALSE(ends_with_ci("track.opus.bak", ".opus"));
    CHECK_FALSE(ends_with_ci("s", ".sng"));
    CHECK(ends_with_ci("x", ""));
}
```

In CMakeLists.txt, in the `add_executable(hydra_tests` list, add `    tests/test_strutil.cpp` after `    tests/test_main.cpp`.

- [ ] **Step 2: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors in tests/test_strutil.cpp, because `to_lower_ascii`, `trim`, `ends_with` and `ends_with_ci` are not declared in core/strutil.h.

- [ ] **Step 3: Write the one owner.** Replace the whole of src/core/strutil.h with:

```cpp
// Small, generic string helpers with no other natural home. Every caller in
// Hydra uses these rather than its own copy.

#ifndef HYDRA_CORE_STRUTIL_H
#define HYDRA_CORE_STRUTIL_H

#include <string>
#include <string_view>

namespace hydra {

// ASCII-only lowercase: A-Z become a-z and every other byte is left alone, so
// UTF-8 text passes through intact. Chart hashes, file names and ini keys all
// go through here.
std::string to_lower_ascii(std::string_view s);

// Strips ASCII whitespace (space, tab, CR, LF, vertical tab, form feed) from
// both ends. Inner whitespace is kept.
std::string trim(std::string_view s);

// Whether s ends with suffix, byte for byte.
bool ends_with(std::string_view s, std::string_view suffix);

// Whether s ends with suffix, ignoring ASCII case (".MID" matches ".mid").
bool ends_with_ci(std::string_view s, std::string_view suffix);

}  // namespace hydra

#endif  // HYDRA_CORE_STRUTIL_H
```

Replace the whole of src/core/strutil.cpp with:

```cpp
#include "core/strutil.h"

namespace hydra {

namespace {

constexpr std::string_view kSpace = " \t\r\n\v\f";

char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

std::string to_lower_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower_ascii(c);
    return out;
}

std::string trim(std::string_view s) {
    const size_t a = s.find_first_not_of(kSpace);
    if (a == std::string_view::npos) return std::string();
    const size_t b = s.find_last_not_of(kSpace);
    return std::string(s.substr(a, b - a + 1));
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    const std::string_view tail = s.substr(s.size() - suffix.size());
    for (size_t i = 0; i < suffix.size(); ++i)
        if (lower_ascii(tail[i]) != lower_ascii(suffix[i])) return false;
    return true;
}

}  // namespace hydra
```

- [ ] **Step 4: Switch every caller.** Each file below gets `#include "core/strutil.h"` in its project-include block (alphabetical order where the block is sorted), then the edit shown. Code is quoted as it is on disk today; if an earlier task changed the same lines, apply the change on top.

src/app/analysis.cpp: delete both helpers:

```cpp
std::string lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Case-insensitive suffix check without the per-call allocations the old
// lowercase-both-strings version paid on every file in every folder.
bool ends_with_ci(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char a = static_cast<unsigned char>(s[s.size() - n + i]);
        unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

```

Then change the three calls: `        const std::string key = lower(raw_key);` becomes `        const std::string key = to_lower_ascii(raw_key);`, `            std::string section = lower(line.substr(1, line.size() - 2));` becomes `            std::string section = to_lower_ascii(line.substr(1, line.size() - 2));`, and `        std::string key = lower(line.substr(0, eq));` becomes `        std::string key = to_lower_ascii(line.substr(0, eq));`.

src/app/preview_source.cpp: delete:

```cpp
std::string to_lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool ends_with_ci(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return to_lower(s.substr(s.size() - suffix.size())) == to_lower(suffix);
}

```

and change `        if (to_lower(stem) == "preview") continue;` to `        if (to_lower_ascii(stem) == "preview") continue;`. The `ends_with_ci(...)` calls stay as written; they now reach `hydra::ends_with_ci`.

src/parse/chart_files.cpp: replace everything from `namespace {` through the end of `is_song_ini` with:

```cpp
ChartFormat chart_format_of(std::string_view path) {
    if (ends_with_ci(path, ".mid")) return ChartFormat::Mid;
    if (ends_with_ci(path, ".chart")) return ChartFormat::Chart;
    if (ends_with_ci(path, ".sng")) return ChartFormat::Sng;
    if (ends_with_ci(path, ".srb")) return ChartFormat::Srb;
    return ChartFormat::None;
}

ChartFormat notes_file_format(std::string_view filename) {
    const std::string low = to_lower_ascii(filename);
    if (low == "notes.mid") return ChartFormat::Mid;
    if (low == "notes.chart") return ChartFormat::Chart;
    return ChartFormat::None;
}

bool is_song_ini(std::string_view filename) { return to_lower_ascii(filename) == "song.ini"; }
```

src/parse/song.cpp: delete `strip`:

```cpp
std::string strip(const std::string& s) {
    const char* ws = " \t\r\n\v\f";
    size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

```

and delete `ascii_casefold`, which has no caller:

```cpp
std::string ascii_casefold(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    return out;
}

```

Then replace every `strip(` with `trim(` in src/parse/song.cpp (Edit with replace_all; six calls), and change the comment `    // Split into lines on '\n' (a trailing '\r' is removed by strip).` to `    // Split into lines on '\n' (a trailing '\r' is removed by trim).`.

src/app/config.cpp and src/app/rules_file.cpp: in each, delete the local copy:

```cpp
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

```

In config.cpp that leaves `namespace {` followed directly by `}  // namespace`; delete that now-empty pair too. The `trim(...)` calls stay and reach `hydra::trim`.

src/app/report.cpp: in `plain`, change:

```cpp
    // .strip()
    size_t a = out.find_first_not_of(" \t\r\n\f\v");
    if (a == std::string::npos) return "";
    size_t b = out.find_last_not_of(" \t\r\n\f\v");
    return out.substr(a, b - a + 1);
```

to:

```cpp
    return trim(out);
```

and in `records_by_hash`, change `        by_hash.emplace(lower_hex(r.hyhash), std::move(r));` to `        by_hash.emplace(to_lower_ascii(r.hyhash), std::move(r));`.

src/net/dmbot_client.cpp: change `#include "core/strutil.h"  // lower_hex` to `#include "core/strutil.h"  // to_lower_ascii`, and `    s.identifier = lower_hex(jstr(entry, "identifier"));` to `    s.identifier = to_lower_ascii(jstr(entry, "identifier"));`.

src/cli/batch.cpp: in `same_file`, change:

```cpp
        std::string s = ec ? p : canon.u8string();
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
```

to:

```cpp
        return hydra::to_lower_ascii(ec ? p : canon.u8string());
```

src/ui/library_view.cpp: change:

```cpp
    std::string needle = filter;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
```

to:

```cpp
    const std::string needle = to_lower_ascii(filter);
```

and:

```cpp
            if (!needle.empty()) {
                std::string name = u.username;
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return (char)std::tolower(c); });
                if (name.find(needle) == std::string::npos) continue;
            }
```

to:

```cpp
            if (!needle.empty() &&
                to_lower_ascii(u.username).find(needle) == std::string::npos)
                continue;
```

src/store/record_store.cpp: if the file still has `import_legacy_uncapped` (a wave 1 task may have deleted it with the Uncapped import), delete the local copy below; its one call, `ends_with(fix.version, suffix)`, then reaches `hydra::ends_with`. If the import is gone, the local copy has no caller: delete it anyway, and skip the include.

```cpp
bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

```

tools/replay.cpp: delete:

```cpp
std::string trimmed(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

```

and replace every `trimmed(` with `trim(` in the file (Edit with replace_all; five calls).

tests/ui/uitest_script.cpp: replace the local copy:

```cpp
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
```

with:

```cpp
using hydra::trim;
```

tests/test_song.cpp: delete the local copy (the calls reach `hydra::ends_with` through the file's `using namespace hydra;`):

```cpp
bool ends_with(const std::string& s, const char* suffix) {
    std::string suf(suffix);
    return s.size() >= suf.size() &&
           s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

```

tests/corpus_util.h: in `first_chart_with_suffix`, change:

```cpp
        if (p.size() > suffix.size() &&
            p.compare(p.size() - suffix.size(), suffix.size(), suffix) == 0)
            return p;
```

to:

```cpp
        if (hydra::ends_with(p, suffix)) return p;
```

tests/test_midi.cpp: change:

```cpp
        if (path.size() < 4 || path.compare(path.size() - 4, 4, ".mid") != 0)
            continue;
```

to:

```cpp
        if (!hydra::ends_with(path, ".mid")) continue;
```

- [ ] **Step 5: Run it and watch it pass.** Run `.\build_cpp.ps1; .\build_cpp.ps1 -Target hydra_replay; .\build-cpp\Release\hydra_tests.exe`. Expected: both builds succeed and the tests end `Status: SUCCESS!`. Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all`; expected: every GUI test passes. Run the two greps from the acceptance criteria and the score-neutral batch recipe with `$out = "$env:TEMP\hydra_t16"`; expected: the greps print only the lines named there, and the batch compare prints nothing.

- [ ] **Step 6: Commit.**

```bash
git add src/core/strutil.h src/core/strutil.cpp src/app/analysis.cpp src/app/preview_source.cpp src/parse/chart_files.cpp src/parse/song.cpp src/app/config.cpp src/app/rules_file.cpp src/app/report.cpp src/net/dmbot_client.cpp src/cli/batch.cpp src/ui/library_view.cpp src/store/record_store.cpp tools/replay.cpp tests/ui/uitest_script.cpp tests/test_song.cpp tests/test_midi.cpp tests/corpus_util.h tests/test_strutil.cpp CMakeLists.txt
git commit -m "Give lowercase, trim and suffix checks one owner in core/strutil

Five lowercase helpers, six trims and every suffix check now call
to_lower_ascii, trim, ends_with or ends_with_ci. analysis.cpp's
ends_with_ci and song.cpp's ascii_casefold had no caller and are gone.

Task: Task 16: One owner for the small string helpers
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/core/strutil.h","src/core/strutil.cpp","src/app/analysis.cpp","src/app/preview_source.cpp","src/parse/chart_files.cpp","src/parse/song.cpp","src/app/config.cpp","src/app/rules_file.cpp","src/app/report.cpp","src/net/dmbot_client.cpp","src/cli/batch.cpp","src/ui/library_view.cpp","src/store/record_store.cpp","tools/replay.cpp","tests/ui/uitest_script.cpp","tests/test_song.cpp","tests/test_midi.cpp","tests/corpus_util.h","tests/test_strutil.cpp","CMakeLists.txt"],"verifyCommand":".\\build_cpp.ps1; .\\build-cpp\\Release\\hydra_tests.exe -tc=\"strutil*\"","acceptanceCriteria":["`hydra_tests.exe -tc=\"strutil*\"` passes (three cases).","The full hydra_tests run ends with `Status: SUCCESS!`, and `hydra_uitest.exe --all` passes.","`.\\build_cpp.ps1 -Target hydra_replay` builds with no errors.","The helper-definition git grep prints only lines in src/core/strutil.h and src/core/strutil.cpp.","`git grep -n \"std::tolower\" -- src tests tools` prints only src/app/report.cpp and the two difficulty_from_name lines in src/parse/song.cpp.","`git grep -n \"lower_hex\" -- src tests tools` prints nothing.","The score-neutral batch recipe prints nothing."],"modelTier":"mechanical"}
```

---

### Task 17: The test suite parses and analyzes each corpus chart once per settings

Many test cases walk the whole chart corpus (the 97 charts under testdata/input) and parse or analyze every chart. Several of them repeat each other's work exactly. The store round-trip test analyzes every chart under ten settings, and three of those ten are the same three the search-invariants test runs. Two search tests analyze every chart at the engine's default settings. The replay tests analyze every chart at the GUI's defaults. About a dozen loops parse every chart the same way.

This task adds two cached calls to tests/corpus_util.h. `corpus::song()` parses a chart once per set of load options. `corpus::analyzed()` analyzes a chart once per set of search settings. Think of it as a shared scratchpad for the test run: the first test to ask does the work, and every later test with the same question reads the answer. A failure is cached too, so a loop's try/catch sees the same exception it sees today. Seventeen loops switch to the cached calls.

Audit check: the whole suite takes 17.8 s today (measured 2026-09-26 on this machine: 381 cases, 43,235 assertions). The store round-trip test alone is 7.2 s. The work that can be shared adds up to about 4 to 5 s, so expect roughly 13 to 14 s afterwards, not a dramatic cut. The audit's "35 loops" is 33 `corpus::chart_paths()` loops today. The other 16 stay as they are, for one of four reasons. Some stop at the first usable chart and already keep their answer in a static (the fixtures in test_path_view, test_preview_view, test_path_codec, test_store, test_fill_report). Some need the full `AnalysisResult` for `add_song` and stop after a few charts (the `fill_store` helpers in test_report and test_dm_report). Some need a specific loader (`load_songpath_mid`, `MidiFile::from_file`). And one runs under non-default rules (test_rules' Auto-ladder case).

What the user sees: nothing. Only the test suite changes.

**Depends on:** nothing beyond the waves before it. The before and after numbers are both taken on this task's own base, because earlier tasks add and remove test cases (T5 deletes the count_chart_chords case, for one). Task 0's tests_time.txt is printed for reference only.

**Expected overlaps:** T16 also edits tests/corpus_util.h (`first_chart_with_suffix`'s body; this task only appends), tests/test_song.cpp (T16 deletes the local `ends_with`; this task changes two parse lines) and the `hydra_tests` list in CMakeLists.txt (T16 adds tests/test_strutil.cpp; this task adds tests/test_corpus_cache.cpp). T13 makes the engine faster, which also shortens the suite; run this task's before and after measurements on the same base so T13's gain isn't counted here.

**Goal:** Corpus loops that ask for the same parse or analysis share one cached answer, the suite gets faster, and no assertion is lost.

**Files:**
- Create: `tests/test_corpus_cache.cpp`
- Modify: `tests/corpus_util.h`, `tests/test_search.cpp`, `tests/test_store.cpp`, `tests/test_replay.cpp`, `tests/test_song.cpp`, `tests/test_timing.cpp`, `tests/test_rules.cpp`, `CMakeLists.txt`
- Test: `tests/test_corpus_cache.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="corpus cache*"` passes (two cases).
- [ ] The full `hydra_tests.exe` run ends with `Status: SUCCESS!`.
- [ ] No assertion is lost: the after run's assertion total, minus the assertions of `-tc="corpus cache*"` alone, equals the before total; and the after test-case total is the before total plus 2.
- [ ] The median of three after runs is lower than the median of three before runs, both measured on this machine with nothing else building or testing. Both numbers, and Task 0's tests_time.txt value, go in the commit message. If the saving is under 1 s, stop and report the numbers instead of committing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Measure the suite as it is.** This comes before the failing test because the baseline must be today's code.

```powershell
$out = "$env:TEMP\hydra_t18"
New-Item -ItemType Directory -Force $out | Out-Null
.\build_cpp.ps1 -Target hydra_tests
$times = 1..3 | ForEach-Object { (Measure-Command { .\build-cpp\Release\hydra_tests.exe *> "$out\before_run.txt" }).TotalSeconds }
$median = ($times | Sort-Object)[1]
"before median s: $median (runs: $($times -join ', '))" | Tee-Object "$out\before.txt"
Select-String -Path "$out\before_run.txt" -Pattern '^\[doctest\] (test cases|assertions):' | ForEach-Object Line | Tee-Object -Append "$out\before.txt"
Get-Content "$env:TEMP\hydra_audit_base\tests_time.txt"
```

Expected: three runs of roughly 17 s each (less if T13 already landed), then the `test cases` and `assertions` lines.

- [ ] **Step 2: Write the failing test.** Create tests/test_corpus_cache.cpp:

```cpp
// tests/corpus_util.h caches corpus parses and analyses for the whole run.
// These checks pin what the corpus loops rely on: one answer per chart and
// settings, the same answer a direct call gives, and a failure that repeats
// instead of turning into a silent empty result.

#include "doctest.h"

#include <string>

#include "core/model.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/pather.h"

using namespace hydra;

TEST_CASE("corpus cache: one parse and one analysis per chart and settings") {
    std::string path;
    for (const std::string& p : corpus::chart_paths())
        if (!corpus::song(p, true, true).is_empty()) { path = p; break; }
    REQUIRE(!path.empty());

    const Song& a = corpus::song(path, true, true);
    CHECK(&corpus::song(path, true, true) == &a);
    CHECK(&corpus::song(path, true, true, Difficulty::Hard) != &a);

    SearchSettings cfg;
    cfg.sp_cap = 4;
    cfg.depth_value = 0;
    const HydraRecord& r = corpus::analyzed(path, cfg);
    CHECK(&corpus::analyzed(path, cfg) == &r);

    // The cached record is the one a direct call produces.
    const HydraRecord direct = analyze_chart(load_songpath(path, true, true), cfg);
    REQUIRE(r.paths.size() == direct.paths.size());
    REQUIRE(!r.paths.empty());
    CHECK(r.best_path().pathstring() == direct.best_path().pathstring());
    CHECK(r.best_path().totalscore() == direct.best_path().totalscore());

    // Plain SearchSettings and the matching AnalysisSettings share one entry.
    app::AnalysisSettings same;
    static_cast<SearchSettings&>(same) = cfg;
    CHECK(&corpus::analyzed(path, same) == &r);

    // Different settings are a different entry.
    cfg.depth_value = 1;
    CHECK(&corpus::analyzed(path, cfg) != &r);
}

TEST_CASE("corpus cache: a failure is thrown again on every call") {
    // A chart with no Hard charting parses to an empty song, and analyzing an
    // empty song throws ChartFileError. The second call must throw too.
    std::string no_hard;
    for (const std::string& p : corpus::chart_paths())
        if (corpus::song(p, true, true, Difficulty::Hard).is_empty()) { no_hard = p; break; }
    REQUIRE(!no_hard.empty());

    app::AnalysisSettings hard;
    hard.difficulty = Difficulty::Hard;
    CHECK_THROWS_AS(corpus::analyzed(no_hard, hard), ChartFileError);
    CHECK_THROWS_AS(corpus::analyzed(no_hard, hard), ChartFileError);
}
```

In CMakeLists.txt, in the `add_executable(hydra_tests` list, add `    tests/test_corpus_cache.cpp` after `    tests/test_main.cpp`.

- [ ] **Step 3: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors in tests/test_corpus_cache.cpp, because `corpus` has no member `song` or `analyzed`.

- [ ] **Step 4: Add the cache.** In tests/corpus_util.h, change the include block:

```cpp
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "json.hpp"
```

to:

```cpp
#include <algorithm>
#include <exception>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "core/rules.h"
#include "json.hpp"
#include "parse/song.h"
#include "search/pather.h"
```

Then, right before the closing `}  // namespace corpus`, add:

```cpp
// ---- cached corpus work ---------------------------------------------------
// Many corpus loops parse the same chart, or analyze it at the same settings,
// as a loop in another test case. These two calls do each piece of work once
// per test run and hand every later caller the same answer. A failure is
// cached too: every call for that key throws the same exception again, so a
// loop's try/catch behaves exactly as it did around the direct call.
// Both return const references into caches that live for the whole run; a
// test that needs to change the Song or the record copies it first.

namespace detail {

template <class T>
struct Outcome {
    std::optional<T> value;
    std::exception_ptr error;
};

// Every SearchSettings field that can change a record. auto_budget_s is
// listed on its own as well as through the fingerprint, so the key stays
// complete whether or not the fingerprint includes it.
inline void add_settings(std::ostringstream& k, const hydra::SearchSettings& s) {
    auto opt = [&k](const auto& o) {
        if (o) k << *o;
        else k << "none";
        k << '|';
    };
    opt(s.sp_cap);
    k << static_cast<int>(s.depth_mode) << '|' << s.depth_value << '|';
    opt(s.ms_filter);
    opt(s.time_budget_s);
    k << s.legacy_fill_deadline << '|' << s.rules.fingerprint() << '|'
      << s.rules.auto_budget_s;
}

}  // namespace detail

// The Song for one corpus chart, parsed once per run for these load options
// (the arguments load_songpath takes).
inline const hydra::Song& song(const std::string& path, bool pro, bool bass2x,
                               hydra::Difficulty difficulty = hydra::Difficulty::Expert,
                               const hydra::core::Rules& rules = hydra::core::default_rules()) {
    static std::map<std::string, detail::Outcome<hydra::Song>> cache;
    std::ostringstream key;
    key.precision(17);
    key << path << '|' << pro << '|' << bass2x << '|' << static_cast<int>(difficulty) << '|'
        << rules.fingerprint();
    auto [it, fresh] = cache.try_emplace(key.str());
    detail::Outcome<hydra::Song>& o = it->second;
    if (fresh) {
        try {
            o.value.emplace(hydra::load_songpath(path, pro, bass2x, difficulty, rules));
        } catch (...) {
            o.error = std::current_exception();
        }
    }
    if (o.error) std::rethrow_exception(o.error);
    return *o.value;
}

// One corpus chart analyzed under `settings`, once per run: the record
// analyze_chart_file would return (parsed with settings.prodrums, bass2x,
// difficulty and rules, then analyze_chart). Throws what that would throw.
inline const hydra::HydraRecord& analyzed(const std::string& path,
                                          const hydra::app::AnalysisSettings& settings) {
    static std::map<std::string, detail::Outcome<hydra::HydraRecord>> cache;
    std::ostringstream key;
    key.precision(17);
    key << path << '|' << settings.prodrums << '|' << settings.bass2x << '|'
        << static_cast<int>(settings.difficulty) << '|';
    detail::add_settings(key, settings);
    auto [it, fresh] = cache.try_emplace(key.str());
    detail::Outcome<hydra::HydraRecord>& o = it->second;
    if (fresh) {
        try {
            const hydra::Song& s = song(path, settings.prodrums, settings.bass2x,
                                        settings.difficulty, settings.rules);
            o.value.emplace(hydra::analyze_chart(s, settings));
        } catch (...) {
            o.error = std::current_exception();
        }
    }
    if (o.error) std::rethrow_exception(o.error);
    return *o.value;
}

// The same for a loop that holds plain SearchSettings and parses with
// load_songpath(path, true, true): pro drums, 2x bass, Expert.
inline const hydra::HydraRecord& analyzed(const std::string& path,
                                          const hydra::SearchSettings& settings) {
    hydra::app::AnalysisSettings a;
    static_cast<hydra::SearchSettings&>(a) = settings;
    return analyzed(path, a);
}
```

Only loops whose settings use the default rules switch below. For those, parsing with `settings.rules` is the same parse the loop does today.

- [ ] **Step 5: Switch the loops in tests/test_search.cpp.** Six loops.

"search invariants hold across the corpus and config knobs": change `        Song song = load_songpath(path, true, true);` to `        const Song& song = corpus::song(path, true, true);`, and change:

```cpp
            HydraRecord shallow = analyze_chart(song, cfg);
            cfg.depth_value = 200;
            HydraRecord deep = analyze_chart(song, cfg);
            cfg.ms_filter = 20.0;
            HydraRecord filtered = analyze_chart(song, cfg);
```

to:

```cpp
            const HydraRecord& shallow = corpus::analyzed(path, cfg);
            cfg.depth_value = 200;
            const HydraRecord& deep = corpus::analyzed(path, cfg);
            cfg.ms_filter = 20.0;
            const HydraRecord& filtered = corpus::analyzed(path, cfg);
```

"legacy fill deadline analyzes a chart end to end": change `        Song song = load_songpath(path, true, true);` to `        const Song& song = corpus::song(path, true, true);`. Its `analyze_chart(song, cfg)` stays (it needs its `std::optional` and stops after three charts).

"stored transfer scales match the display-layer recomputation" and "no activation keeps backends past its squeezed-out note": in each, change `        Song song = load_songpath(path, true, true);` to `        const Song& song = corpus::song(path, true, true);`, change `        std::optional<HydraRecord> record;` to `        const HydraRecord* record = nullptr;`, and change `            record = analyze_chart(song, cfg);` to `            record = &corpus::analyzed(path, cfg);`. The `record->` uses after that read the same through a pointer.

"search_allzero returns only all-0 paths inside the 0 ms limit" and "collected phrases: the corpus agrees with the squeezes and the SP end": in each, change `        Song song = load_songpath(path, true, true);` to `        const Song& song = corpus::song(path, true, true);`. `ScoreGraph` takes a `const Song&`, so nothing else changes.

- [ ] **Step 6: Switch the store round-trip loop.** In tests/test_store.cpp, "records round-trip through RecordStore across the corpus and config matrix": change `        Song song = load_songpath(path, true, true);` to `        const Song& song = corpus::song(path, true, true);`, change `            std::optional<HydraRecord> record;` to `            const HydraRecord* record = nullptr;`, and change `                record = analyze_chart(song, settings);` to `                record = &corpus::analyzed(path, settings);`.

- [ ] **Step 7: Switch the replay loops.** In tests/test_replay.cpp, five loops read the GUI defaults (`cfg` or `app::Settings().to_analysis_settings()`).

"replay reproduces the engine's score for every corpus path", "targeted search reproduces every corpus path" and "targeted search rejects a tick that is not a fill": in each, change `        Song song = load_songpath(path, cfg.prodrums, cfg.bass2x, cfg.difficulty);` to `        const Song& song = corpus::song(path, cfg.prodrums, cfg.bass2x, cfg.difficulty);`, and change `        HydraRecord rec = analyze_chart(song, cfg);` to `        const HydraRecord& rec = corpus::analyzed(path, cfg);`.

"windows read from a path JSON match the ones read from the record" and "paths_json writes every field the dump readers use": in each, change `        Song song = load_songpath(chart, cfg.prodrums, cfg.bass2x, cfg.difficulty);` to `        const Song& song = corpus::song(chart, cfg.prodrums, cfg.bass2x, cfg.difficulty);`, and change `        HydraRecord rec = analyze_chart(song, cfg);` to `        const HydraRecord& rec = corpus::analyzed(chart, cfg);`.

- [ ] **Step 8: Switch the parse-only loops.** In tests/test_song.cpp, "song parse holds its invariants over the corpus": change `        Song song = load_songpath(path, true, true);` to `        const Song& song = corpus::song(path, true, true);`. "song parse holds its invariants at Hard too": change `        Song song = load_songpath(path, true, true, Difficulty::Hard);` to `        const Song& song = corpus::song(path, true, true, Difficulty::Hard);`.

In tests/test_timing.cpp, change `        hydra::Song song = hydra::load_songpath(path, true, true);` to `        const hydra::Song& song = corpus::song(path, true, true);`.

In tests/test_rules.cpp, "rules: the leeway changes what the engine counts" and "rules: max_tied_paths caps the tied paths the engine keeps": in each, change `        Song song = load_songpath(path, true, true);` to `        const Song& song = corpus::song(path, true, true);`. These loops pass custom rules to `ScoreGraph`, not to the parse, so the cached default-rules parse is the same Song they parse today.

- [ ] **Step 9: Run it, measure it and check the assertion count.**

```powershell
$out = "$env:TEMP\hydra_t18"
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe -tc="corpus cache*" *> "$out\cache_run.txt"
Get-Content "$out\cache_run.txt" -Tail 4
$times = 1..3 | ForEach-Object { (Measure-Command { .\build-cpp\Release\hydra_tests.exe *> "$out\after_run.txt" }).TotalSeconds }
$median = ($times | Sort-Object)[1]
"after median s: $median (runs: $($times -join ', '))" | Tee-Object "$out\after.txt"
Select-String -Path "$out\after_run.txt" -Pattern '^\[doctest\] (test cases|assertions):|Status:' | ForEach-Object Line | Tee-Object -Append "$out\after.txt"
Get-Content "$out\before.txt"
```

Expected: the cache cases pass; the full run ends `Status: SUCCESS!`; after-assertions minus the cache run's assertions equals the before total; after test cases equal before plus 2; the after median is lower, around 4 s lower on this machine. If an assertion total is short, a switched loop now skips work it used to do: find it by comparing `hydra_tests.exe -tc="<case>"` per switched case against the base build, and fix the loop. If the saving is under 1 s, stop and report the numbers.

- [ ] **Step 10: Commit.** Put the measured numbers in the message.

```bash
git add tests/corpus_util.h tests/test_corpus_cache.cpp tests/test_search.cpp tests/test_store.cpp tests/test_replay.cpp tests/test_song.cpp tests/test_timing.cpp tests/test_rules.cpp CMakeLists.txt
git commit -m "Parse and analyze each corpus chart once per settings in the tests

corpus::song and corpus::analyzed cache the work 17 corpus loops used to
repeat. hydra_tests wall time, median of 3: <before> s -> <after> s
(Task 0 baseline: <tests_time.txt value>). Assertions: <before total> before,
<after total> after including <cache test count> from the new cache tests.

Task: Task 17: The test suite parses and analyzes each corpus chart once per settings
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

The four `<...>` numbers in the message body are filled from `$out\before.txt`, `$out\after.txt`, `$out\cache_run.txt` and Task 0's tests_time.txt.

```json:metadata
{"files":["tests/corpus_util.h","tests/test_corpus_cache.cpp","tests/test_search.cpp","tests/test_store.cpp","tests/test_replay.cpp","tests/test_song.cpp","tests/test_timing.cpp","tests/test_rules.cpp","CMakeLists.txt"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe","acceptanceCriteria":["`hydra_tests.exe -tc=\"corpus cache*\"` passes (two cases).","The full hydra_tests run ends with `Status: SUCCESS!`.","After assertion total minus the corpus-cache cases' assertions equals the before total; after test cases equal before plus 2.","Median of three after runs is lower than the median of three before runs; both numbers and Task 0's tests_time.txt value are in the commit message; under 1 s saved means stop and report."],"modelTier":"standard"}
```

---

### Task 18: GUI tests for the DM compare, the report buttons and the view settings

The audit says the GUI tests cover six flows and leave out the DM compare, the settings and the report buttons. That is only partly true. The existing `settings-and-reports` test already toggles 2x Bass, clicks the batch modal's "Open path report", and runs one DM compare against the canned API. What no test touches is the rest of those flows. For the DM compare, that is the two refusals (SP cap not 4, difficulty not Expert), the name filter, "Open report again", "Compare another" and "Close". For the reports, it is the main window's "Open path report" button, the "Open automatically" box, and "redo existing". For the view settings, it is Pro Drums, the page arrows, the "Analyze library" confirm's Cancel, and removing a song folder through its confirm popup.

This task adds three GUI tests for those gaps. They pin behaviour that works today, so they pass on first run. That is the point of a coverage task: the next change that breaks one of these flows fails a test instead of reaching the user.

What the user sees: nothing.

**Depends on:** nothing.

**Expected overlaps:** T6 and T7 also add tests and register entries to `tests/ui/uitest_tests.cpp`; each appends after `test_squeezed_out_uncounted` and at the end of the table, so the merger keeps all of them. Until T7 merges, the dmleaderboards filter is a function static that survives between tests, so `dm-compare-flow` clears it first.

**Goal:** every button in the DM compare, report and view-settings flows is clicked by at least one GUI test.

**Files:**
- Test: `tests/ui/uitest_tests.cpp`

**Acceptance Criteria:**
- [ ] `hydra_uitest.exe --test dm-compare-flow --test report-buttons --test view-settings` prints `[PASS]` three times.
- [ ] `hydra_uitest.exe --all` passes.
- [ ] `Select-String -Path tests\ui\uitest_tests.cpp -Pattern 'Open report again|Compare another|##dmfilter|Open automatically|redo existing|Pro Drums|##pageright|Remove folder\?'` prints at least one line for each of the eight labels.

**Verify:** `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test dm-compare-flow --test report-buttons --test view-settings` → `[PASS]` three times, exit code 0.

**Steps:**

- [ ] **Step 1: Write the tests.** In `tests/ui/uitest_tests.cpp`, add these three tests after `test_squeezed_out_uncounted`:

```cpp
// The dmleaderboards comparison, end to end: the two refusals, the name
// filter, and every button of the finished report.
void test_dm_compare_flow(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");

    // The ladder plays by Clone Hero's rules at Expert. Any other cap or
    // difficulty refuses with a status line and opens nothing.
    h.app->settings.sp_cap = 8;
    h.app->commit_settings();
    ctx->ItemClick("Compare dmleaderboards user...");
    ctx->Yield(2);
    IM_CHECK(!h.app->dm_picker_open);
    IM_CHECK(visible_text(h).find("needs SP cap 4") != std::string::npos);
    h.app->settings.sp_cap = 4;
    h.app->commit_settings();

    ctx->ComboClick("##difficulty/Hard");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.view_difficulty == "Hard"; }, 5));
    ctx->ItemClick("Compare dmleaderboards user...");
    ctx->Yield(2);
    IM_CHECK(!h.app->dm_picker_open);
    IM_CHECK(visible_text(h).find("needs Expert difficulty") != std::string::npos);
    ctx->ComboClick("##difficulty/Expert");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.view_difficulty == "Expert"; }, 5));

    // The picker opens on the canned ladder; the filter narrows it as you
    // type, ignoring case.
    ctx->ItemClick("Compare dmleaderboards user...");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->dm_users.empty(); }, 10));
    ctx->SetRef("//Compare dmleaderboards user");
    ctx->ItemInputValue("##dmfilter", "");  // an earlier test may have left text
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find("alice") != std::string::npos; }, 5));
    ctx->ItemInputValue("##dmfilter", "bob");
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("alice") == std::string::npos);
    ctx->ItemInputValue("##dmfilter", "ALI");
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("alice") != std::string::npos);

    // Pick alice: the report builds, the choice is remembered, and with
    // auto-open off nothing opens until asked.
    ctx->ItemClick("**/###111");
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->dm_report_job && h.app->dm_report_job->finished();
    }, 60));
    IM_CHECK(h.app->dm_report_job->ok());
    IM_CHECK_STR_EQ(h.app->settings.dm_last_user.c_str(), "111");
    IM_CHECK_STR_EQ(hydra::app::Settings::load_file(h.ini_path).dm_last_user.c_str(), "111");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)0);
    ctx->ItemClick("Open report again");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);
    IM_CHECK(h.opened_urls[0] == hydra::app::dm_report_html_path());

    // Compare another: back to the list, the finished report dropped.
    ctx->ItemClick("Compare another");
    ctx->Yield(2);
    IM_CHECK(h.app->dm_report_job == nullptr);
    IM_CHECK(h.app->dm_picker_open);
    IM_CHECK(visible_text(h).find("Pick a player") != std::string::npos);

    // Close: the picker goes away.
    ctx->ItemClick("Close");
    ctx->Yield(2);
    IM_CHECK(!h.app->dm_picker_open);
}

// The path report's buttons: the main window's "Open path report" appears
// once a report exists, "Open automatically" persists and then opens the
// next report by itself, and "redo existing" re-analyzes a stored chart.
void test_report_buttons(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    IM_CHECK(!ctx->ItemExists("Open path report"));  // no report built yet

    // Batch just the first chart (the search narrows the batch).
    std::string title = h.app->current_page.rows[0].title;
    ctx->ItemInputValue("##search", title.c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == title; }, 5));
    auto run_batch = [&] {
        char label[96];
        std::snprintf(label, sizeof(label), "Analyze search (%lld)",
                      (long long)h.app->current_page.total_count);
        ctx->SetRef("//Hydra");
        ctx->ItemClick(label);
        ctx->SetRef("//Analyzing");
        ctx->ItemClick("Start");
        return wait_until(ctx, [&] {
                   return h.app->batch_job && h.app->batch_job->snapshot().finished;
               }, 300) &&
               wait_until(ctx, [&] {
                   return h.app->report_job && h.app->report_job->finished();
               }, 60);
    };
    IM_CHECK(run_batch());
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)0);  // auto-open is off

    // Tick "Open automatically" in the finished modal: it persists at once.
    ctx->ItemClick("Open automatically");
    IM_CHECK(h.app->settings.auto_open_report);
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).auto_open_report);
    ctx->ItemClick("Continue");
    ctx->Yield(2);

    // The main window now offers the report, and opens it on a click.
    ctx->SetRef("//Hydra");
    IM_CHECK(wait_until(ctx, [&] { return ctx->ItemExists("Open path report"); }, 5));
    ctx->ItemClick("Open path report");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);
    IM_CHECK(h.opened_urls[0] == hydra::app::report_html_path());

    // "redo existing" re-analyzes the stored chart, and the confirm says so.
    ctx->ItemCheck("redo existing");
    IM_CHECK(h.app->batch_redo);
    char label[96];
    std::snprintf(label, sizeof(label), "Analyze search (%lld)",
                  (long long)h.app->current_page.total_count);
    ctx->ItemClick(label);
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("will be re-analyzed") != std::string::npos);
    ctx->SetRef("//Analyzing");
    ctx->ItemClick("Cancel");
    ctx->Yield(2);
    IM_CHECK(run_batch());
    IM_CHECK_EQ(h.app->batch_job->snapshot().skipped, 0);  // nothing skipped: redone

    // With auto-open on, the new report opened by itself.
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)2);
    ctx->ItemClick("Continue");
    ctx->Yield(2);
    ctx->SetRef("//Hydra");
    ctx->ItemUncheck("redo existing");
}

// The View row and the library's own controls: Pro Drums, the page arrows,
// backing out of "Analyze library", and removing a song folder.
void test_view_settings(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");

    // The page arrows move one page each way.
    IM_CHECK(h.app->current_page.total_count > h.app->rows_per_page);  // 2+ pages
    IM_CHECK_EQ(h.app->table_viewpage, 0);
    ctx->ItemClick("##pageright");
    IM_CHECK_EQ(h.app->table_viewpage, 1);
    ctx->ItemClick("##pageleft");
    IM_CHECK_EQ(h.app->table_viewpage, 0);

    // Pro Drums off is a different chart mode: persisted at once, and the
    // library starts over at page one.
    ctx->ItemClick("##pageright");
    IM_CHECK_EQ(h.app->table_viewpage, 1);
    IM_CHECK(h.app->settings.view_prodrums);
    ctx->ItemClick("Pro Drums");
    IM_CHECK(!h.app->settings.view_prodrums);
    IM_CHECK(!hydra::app::Settings::load_file(h.ini_path).view_prodrums);
    IM_CHECK(h.app->settings.chartmode_key().find("Pro Drums") == std::string::npos);
    IM_CHECK_EQ(h.app->table_viewpage, 0);
    ctx->ItemClick("Pro Drums");
    IM_CHECK(h.app->settings.view_prodrums);

    // "Analyze library" asks first; Cancel starts nothing.
    ctx->ItemClick("Analyze library");
    ctx->SetRef("//Analyzing");
    IM_CHECK(visible_text(h).find("will be skipped") != std::string::npos);
    ctx->ItemClick("Cancel");
    ctx->Yield(2);
    IM_CHECK(h.app->batch_job == nullptr);
    IM_CHECK(!h.app->batch_confirm_pending);

    // Removing a song folder goes through a confirm. Cancel keeps it;
    // Remove drops it, persists that, and turns the scan button off.
    ctx->SetRef("//Hydra");
    IM_CHECK_EQ(h.app->settings.chartfolders.size(), (size_t)1);
    ctx->ItemClick("Manage folders... (1)");
    ctx->SetRef("//Song folders");
    ctx->ItemClick("**/X");
    ctx->SetRef("//Remove folder?");
    ctx->ItemClick("Cancel");
    ctx->Yield(2);
    IM_CHECK_EQ(h.app->settings.chartfolders.size(), (size_t)1);
    ctx->SetRef("//Song folders");
    ctx->ItemClick("**/X");
    ctx->SetRef("//Remove folder?");
    ctx->ItemClick("Remove");
    ctx->Yield(2);
    IM_CHECK(h.app->settings.chartfolders.empty());
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).chartfolders.empty());
    IM_CHECK(!h.app->settings.is_rescan);
    ctx->SetRef("//Song folders");
    ctx->ItemClick("Close");
    ctx->Yield(2);
    ctx->SetRef("//Hydra");
    IM_CHECK((ctx->ItemInfo("Scan charts").ItemFlags & ImGuiItemFlags_Disabled) != 0);
}
```

and add these three entries at the end of the table in `register_tests`:

```cpp
        {"dm-compare-flow", test_dm_compare_flow},
        {"report-buttons", test_report_buttons},
        {"view-settings", test_view_settings},
```

- [ ] **Step 2: Run them.** Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test dm-compare-flow --test report-buttons --test view-settings`. Expected: `[PASS]` three times, because these flows work today. If one fails, read the engine log it prints: it names the failing check. A failing check here is either a real bug (stop and report it, with the log) or a label that differs from the one quoted; in the second case run `.\build-cpp\Release\hydra_uitest.exe --script` with a file holding `dump` for the window named in the log, read the real label, and fix the test's label only.

- [ ] **Step 3: Prove each test can fail.** A test that cannot fail proves nothing. In `src/ui/library_view.cpp`, temporarily change `if (ImGui::Button("Open report again")) app::open_dm_report_in_browser();` to `if (ImGui::Button("Open report again")) {}`, rebuild `hydra_uitest` and run `--test dm-compare-flow`. Expected: `[FAIL]` on `h.opened_urls.size() == 1`. Restore the line with `git checkout -- src/ui/library_view.cpp` (it is the only change in that file) and rebuild. Then run `.\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test `[PASS]`.

- [ ] **Step 4: Commit.**

```bash
git add tests/ui/uitest_tests.cpp
git commit -m "Cover the DM compare, report buttons and view settings with GUI tests

Task: Task 18: GUI tests for the DM compare, the report buttons and the view settings
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 19: ch_probe fixes

Four things are wrong in the ch_probe tool today.

First, the audit says `tests/test_process.py` still passes milliseconds after `process.py` switched to seconds. It is no longer true on disk. The file already has the seconds-based tests as an uncommitted change (modified 09:36 today, origin unknown), and all 134 tests pass. So this task only proves it still holds once Task 0 has committed it.

Second, the debugger never turns off Windows' kill-on-exit rule. A debugger thread that exits takes every process it is attached to down with it, unless it first calls `DebugSetProcessKillOnExit(FALSE)` ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-debugsetprocesskillonexit): "If this parameter is TRUE, the thread terminates all attached processes on exit (note that this is the default)"). So a Python crash or Ctrl+C while attached kills Clone Hero. The call must come after `DebugActiveProcess`, because the page says the thread needs a debugging connection first.

Kill-on-exit off is not enough on its own. An armed breakpoint is a 0xCC byte (the x86 trap instruction) written into the game's code. If we detach with one still in place, or while a thread is halfway through stepping over one, the game hits a trap nobody catches and crashes. So `stop()` changes too. It removes every breakpoint first. Then it keeps answering debug events for up to a second, until no thread is still mid-step. It handles a hit that was already queued on a removed breakpoint by moving that thread's instruction pointer back one byte onto the restored instruction. Only then does it detach. A hard kill of Python (Task Manager) still leaves the bytes behind, and nothing can fix that.

While in there: `_Win32` uses `ctypes.wintypes` but the file only does `import ctypes`. That works today only because another module happened to import `ctypes.wintypes` first. The file gets its own import.

Third, `experiments/play_chart.py`'s .chart reader ignores the cymbal markers and the 2x kick. It maps note numbers straight to lanes (`NOTE_TO_LANE = {0: 4, 1: 1, 2: 2, 3: 3, 4: 0, 5: 0}`) and drops everything else. On a pro-drums .chart, notes 66, 67 and 68 mark the yellow, blue and green gem on the same tick as a cymbal. Hydra's own parser reads them the same way (`case 66: ... op_cymbal(NoteColor::Yellow)` in src/parse/song.cpp). Dropping them means every cymbal is pressed as a tom. Note 32 is the 2x kick (`case 32: ... op_2x()`), and it is dropped too. The MIDI path was fixed on 2026-09-25 with a helper that resolves one gem to one lane. The .chart path gets the same kind of helper, with the .chart rule: a gem is a tom unless its cymbal marker is on the same tick. That is the reverse of MIDI, where a gem is a cymbal unless its tom marker is there.

What the user sees: nothing in Hydra. At the game, `play_chart.py` now presses cymbal keys for cymbals and plays 2x kicks on .chart songs. `.mid` songs play exactly as before.

**Depends on:** Task 0 (it commits the pending `process.py`, `test_process.py` and experiments files).
**Expected overlaps:** Task 20 edits the same three files afterwards: `debugger.py` (it deletes `set_hw_data_breakpoint` and the `DR7_*` constants, and leaves this task's `attach`, `stop`, `_handle_exception`, `_drain` and `_rewind_rip` alone), `experiments/play_chart.py` (it swaps this task's lane numbers for `Lane` names and deletes `find_active_engine`), and `tests/test_debugger.py`. Nothing else in wave 1 touches `tools/ch_probe`.
**Goal:** Clone Hero survives the probe's debugger going away, and play_chart presses the right keys for .chart cymbals and 2x kicks.

**Files:**
- Modify: `tools/ch_probe/debugger.py` (imports, `BreakpointTable.addresses`, `_Win32.__init__`, `Debugger.__init__`, `attach`, `stop`, new `_drain` and `_rewind_rip`, `_handle_exception`)
- Modify: `tools/ch_probe/experiments/play_chart.py` (the `NOTE_TO_LANE` block, `parse_chart`)
- Create: `tools/ch_probe/tests/test_play_chart.py`
- Test: `tools/ch_probe/tests/test_debugger.py`, `tools/ch_probe/tests/test_process.py` (no change; its six seconds-based `CheckNormalConstantsTest` cases and three `VerifyTargetsTest` cases must pass)

**Acceptance Criteria:**
- [ ] `python -m pytest tools/ch_probe/tests/test_process.py -q` passes with 0 failed (Audit check: already true on disk before this task).
- [ ] `python -m pytest tools/ch_probe/tests/test_play_chart.py -q` passes. A tiny pro-drums .chart gives yellow tom, yellow, blue and green cymbal, 2x kick as kick, red plus kick, and green tom, one lane per gem.
- [ ] `python -m pytest tools/ch_probe/tests/test_debugger.py -q` passes, including `test_attach_turns_kill_on_exit_off_right_after_attaching`, `test_attach_detaches_if_kill_on_exit_cannot_be_turned_off`, `test_single_step_after_stop_does_not_rearm` and `test_queued_hit_on_a_removed_breakpoint_rewinds_and_continues`.
- [ ] `python -m pytest tools/ch_probe/tests -q` shows 0 failed.
- [ ] `Select-String -Path tools\ch_probe\experiments\play_chart.py -Pattern 'NOTE_TO_LANE'` prints nothing.

**Verify:** `python -m pytest tools/ch_probe/tests -q` -> a last line of `N passed`, with no `failed`.

**Steps:**

- [ ] **Step 1: Prove the process tests pass as they are.** Run `python -m pytest tools/ch_probe/tests/test_process.py -q`. Expected: `24 passed`. If any fail, stop and report. The audit expected three failures, but the fix is already on disk as an uncommitted change of unknown origin, and this task must not rewrite it.

- [ ] **Step 2: Write the failing .chart test.** Create `tools/ch_probe/tests/test_play_chart.py`:

```python
"""play_chart.py's .chart path turns each tick's notes into input lanes.

On a pro-drums .chart, notes 66/67/68 on the same tick as a yellow, blue or
green gem make that gem a CYMBAL; without its marker the gem is a tom. Note 32
is the 2x kick. One gem is one key: pressing a tom and a cymbal key for one
gem is an overhit. These tests feed a tiny .chart through parse_chart.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe.experiments import play_chart  # noqa: E402

# Resolution 192 at 120 BPM: 192 ticks = one beat = 0.5 s.
_CHART = """[Song]
{
  Resolution = 192
}
[SyncTrack]
{
  0 = TS 4
  0 = B 120000
}
[ExpertDrums]
{
  0 = N 2 0
  192 = N 2 0
  192 = N 66 0
  384 = N 3 0
  384 = N 67 0
  576 = N 4 0
  576 = N 68 0
  768 = N 32 0
  960 = N 0 0
  960 = N 1 0
  1152 = N 4 0
  1152 = N 37 0
}
"""


def _parse():
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "notes.chart")
        with open(path, "w", encoding="utf-8") as f:
            f.write(_CHART)
        return play_chart.parse_chart(path)


class ChartPathTest(unittest.TestCase):
    def test_each_tick_gets_one_lane_per_gem(self):
        resolution, _tempos, notes = _parse()
        self.assertEqual(resolution, 192)
        self.assertEqual([(n.tick, list(n.lanes)) for n in notes], [
            (0, [2]),       # yellow tom (J)
            (192, [5]),     # yellow cymbal (U)
            (384, [6]),     # blue cymbal (Y)
            (576, [7]),     # green cymbal (T)
            (768, [4]),     # 2x kick presses the kick (L)
            (960, [1, 4]),  # red + kick
            (1152, [0]),    # green tom (A); the accent marker 37 presses nothing
        ])

    def test_times_follow_the_tempo(self):
        _resolution, _tempos, notes = _parse()
        self.assertAlmostEqual(notes[1].time_s, 0.5)
        self.assertAlmostEqual(notes[-1].time_s, 3.0)

    def test_lane_helper(self):
        self.assertEqual(play_chart.chart_notes_to_lanes([2, 66]), [5])
        self.assertEqual(play_chart.chart_notes_to_lanes([3]), [3])
        self.assertEqual(play_chart.chart_notes_to_lanes([66]), [])
        self.assertEqual(play_chart.chart_notes_to_lanes([0, 32]), [4])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Write the failing debugger tests.** Add these to `tools/ch_probe/tests/test_debugger.py`, before `if __name__ == "__main__":`:

```python
def _exception_event(code, *, tid, addr=0):
    """A DEBUG_EVENT carrying one exception, built by hand (no debuggee)."""
    ev = debugger.DEBUG_EVENT()
    ev.dwDebugEventCode = debugger.EXCEPTION_DEBUG_EVENT
    ev.dwThreadId = tid
    rec = ev.u.Exception.ExceptionRecord
    rec.ExceptionCode = code
    rec.ExceptionAddress = addr
    return ev


class KillOnExitTests(unittest.TestCase):
    """attach() must turn Windows' kill-on-exit off right after attaching, so
    a Python crash or Ctrl+C detaches from Clone Hero instead of killing it."""

    def setUp(self):
        self.calls = []
        self.kill_ok = True
        calls, test = self.calls, self

        class FakeK32:
            def DebugActiveProcess(self, pid):
                calls.append(("DebugActiveProcess", pid))
                return 1

            def DebugSetProcessKillOnExit(self, kill):
                calls.append(("DebugSetProcessKillOnExit", kill))
                return 1 if test.kill_ok else 0

            def DebugActiveProcessStop(self, pid):
                calls.append(("DebugActiveProcessStop", pid))
                return 1

            def OpenProcess(self, access, inherit, pid):
                calls.append(("OpenProcess", pid))
                return 0x1234

        class FakeWin32:
            def __init__(self):
                self.k32 = FakeK32()

        self._real_win32 = debugger._Win32
        debugger._Win32 = FakeWin32

    def tearDown(self):
        debugger._Win32 = self._real_win32

    def test_attach_turns_kill_on_exit_off_right_after_attaching(self):
        Debugger().attach(4242)
        self.assertEqual(self.calls[:2], [
            ("DebugActiveProcess", 4242),
            ("DebugSetProcessKillOnExit", False),
        ])

    def test_attach_detaches_if_kill_on_exit_cannot_be_turned_off(self):
        self.kill_ok = False
        with self.assertRaises(OSError):
            Debugger().attach(4242)
        self.assertIn(("DebugActiveProcessStop", 4242), self.calls)
        self.assertNotIn(("OpenProcess", 4242), self.calls)


class SafeDetachTests(unittest.TestCase):
    """stop() removes the breakpoints, then answers events still queued for
    them. Those late events must never re-plant a 0xCC or reach the game."""

    def test_single_step_after_stop_does_not_rearm(self):
        dbg = Debugger()
        dbg._pending_rearm[7] = 0x1000   # thread 7 was mid-step; bp since removed
        ev = _exception_event(debugger.EXCEPTION_SINGLE_STEP, tid=7)
        self.assertEqual(dbg._handle_exception(ev), debugger.DBG_CONTINUE)
        self.assertEqual(dbg._pending_rearm, {})
        self.assertFalse(dbg._table.has(0x1000))

    def test_queued_hit_on_a_removed_breakpoint_rewinds_and_continues(self):
        dbg = Debugger()
        dbg._seen_initial = True
        dbg._removed.add(0x1000)
        rewound = []
        dbg._rewind_rip = rewound.append
        ev = _exception_event(debugger.EXCEPTION_BREAKPOINT, tid=7, addr=0x1000)
        self.assertEqual(dbg._handle_exception(ev), debugger.DBG_CONTINUE)
        self.assertEqual(rewound, [7])

    def test_table_lists_every_registered_address(self):
        mem = FakeMemory({0x10: 0x55, 0x20: 0x66})
        table = BreakpointTable(mem.read, mem.write)
        table.add(0x10)
        table.add(0x20)
        table.arm(0x10)
        self.assertEqual(sorted(table.addresses()), [0x10, 0x20])
```

- [ ] **Step 4: Run them and watch them fail.** Run `python -m pytest tools/ch_probe/tests/test_play_chart.py tools/ch_probe/tests/test_debugger.py -q`. Expected failures: `test_each_tick_gets_one_lane_per_gem` (tick 192 gives `[2]` and tick 768 is missing) and `test_lane_helper` (AttributeError: no `chart_notes_to_lanes`). `test_times_follow_the_tempo` already passes; it guards the tempo maths through the change. The first kill-on-exit test fails because the second call is `OpenProcess`, not `DebugSetProcessKillOnExit`. The second fails because nothing raises. `test_single_step_after_stop_does_not_rearm` fails with KeyError from `BreakpointTable.arm`. The removed-breakpoint test fails with AttributeError: no `_removed`. The table test fails with AttributeError: no `addresses`.

- [ ] **Step 5: Fix the .chart path.** In `tools/ch_probe/experiments/play_chart.py`, replace:

```python
# .chart note -> input lane. Note 66 = cymbal flag, skip it.
NOTE_TO_LANE = {0: 4, 1: 1, 2: 2, 3: 3, 4: 0, 5: 0}
```

with:

```python
# .chart drum notes: 0 kick, 1 red, 2 yellow, 3 blue, 4 green (5 is green on a
# 5-lane chart), 32 the 2x kick. On pro drums, 66/67/68 are CYMBAL markers for
# yellow/blue/green. This is the reverse of the MIDI rule below: in a .chart a
# gem is a tom unless its marker is on the same tick; in a MIDI a gem is a
# cymbal unless its tom marker is.
CHART_DRUM_NOTES = frozenset({0, 1, 2, 3, 4, 5, 32, 66, 67, 68})

# gem note -> (tom lane, its cymbal-marker note, cymbal lane)
_CHART_CYMBAL_MARKER = {
    2: (2, 66, 5),   # Yellow: tom J / cymbal U
    3: (3, 67, 6),   # Blue:   tom K / cymbal Y
    4: (0, 68, 7),   # Green:  tom A / cymbal T
}


def chart_notes_to_lanes(chart_notes) -> list:
    """Turn the .chart note numbers at one tick into input lanes.

    One gem -> one lane, like midi_notes_to_lanes. The kick (0) and the 2x
    kick (32) both press the kick key. A marker with no gem presses nothing.
    """
    s = set(chart_notes)
    lanes = []
    if 0 in s or 32 in s:
        lanes.append(4)   # Kick (L)
    if 1 in s:
        lanes.append(1)   # Red (S)
    for gem_note, (tom_lane, marker, cym_lane) in _CHART_CYMBAL_MARKER.items():
        if gem_note in s:
            lanes.append(cym_lane if marker in s else tom_lane)
    if 5 in s:
        lanes.append(0)   # 5-lane green: pressed as before (A)
    return sorted(set(lanes))
```

In `parse_chart`, replace:

```python
    raw_notes: dict[int, list[int]] = {}
    drums_match = re.search(r"\[ExpertDrums\]\s*\{([^}]*)\}", text, re.DOTALL)
    if drums_match:
        for line in drums_match.group(1).strip().split("\n"):
            nm = re.match(r"\s*(\d+)\s*=\s*N\s+(\d+)\s+\d+", line)
            if nm:
                tick = int(nm.group(1))
                note_num = int(nm.group(2))
                if note_num in NOTE_TO_LANE:
                    raw_notes.setdefault(tick, []).append(NOTE_TO_LANE[note_num])

    notes = []
    for tick in sorted(raw_notes.keys()):
        lanes = sorted(set(raw_notes[tick]))
        notes.append(NoteEvent(tick=tick, lanes=lanes))
```

with:

```python
    # Collect the raw .chart note numbers per tick, then resolve each tick to
    # lanes, so a cymbal marker turns its gem into a cymbal instead of being
    # dropped.
    raw_notes: dict[int, list[int]] = {}
    drums_match = re.search(r"\[ExpertDrums\]\s*\{([^}]*)\}", text, re.DOTALL)
    if drums_match:
        for line in drums_match.group(1).strip().split("\n"):
            nm = re.match(r"\s*(\d+)\s*=\s*N\s+(\d+)\s+\d+", line)
            if nm:
                tick = int(nm.group(1))
                note_num = int(nm.group(2))
                if note_num in CHART_DRUM_NOTES:
                    raw_notes.setdefault(tick, []).append(note_num)

    notes = []
    for tick in sorted(raw_notes.keys()):
        lanes = chart_notes_to_lanes(raw_notes[tick])
        if lanes:
            notes.append(NoteEvent(tick=tick, lanes=lanes))
```

- [ ] **Step 6: Import `ctypes.wintypes` properly.** In `tools/ch_probe/debugger.py`, replace:

```python
import ctypes
import struct
from typing import Callable, Dict, Optional
```

with:

```python
import ctypes
import ctypes.wintypes  # _Win32 uses it; don't rely on another module importing it
import struct
import time
from typing import Callable, Dict, Optional
```

- [ ] **Step 7: Let the table list its addresses.** In `BreakpointTable`, right after the `remove` method, add:

```python
    def addresses(self) -> list:
        """Every registered breakpoint address, armed or not."""
        return list(self._bps)
```

- [ ] **Step 8: Bind the kill-on-exit call.** In `_Win32.__init__`, right after the `DebugActiveProcessStop` restype line, add:

```python
        k32.DebugSetProcessKillOnExit.argtypes = [wintypes.BOOL]
        k32.DebugSetProcessKillOnExit.restype = wintypes.BOOL
```

- [ ] **Step 9: Remember removed breakpoints.** In `Debugger.__init__`, replace:

```python
        self._seen_initial = False   # swallow the one system breakpoint on attach
        self._stopped = False
```

with:

```python
        self._seen_initial = False   # swallow the one system breakpoint on attach
        self._stopped = False
        # Breakpoints stop() has removed. A hit on one may already be queued.
        self._removed: set = set()
```

- [ ] **Step 10: Turn kill-on-exit off in `attach`.** Replace:

```python
    def attach(self, pid: int) -> None:
        """Attach to a running process by id. LIVE-ONLY."""
        self._win32 = _Win32()
        self._pid = pid
        if not self._win32.k32.DebugActiveProcess(pid):
            raise ctypes.WinError(ctypes.get_last_error())
        self._proc_handle = self._win32.k32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
```

with:

```python
    def attach(self, pid: int) -> None:
        """Attach to a running process by id. LIVE-ONLY.

        Right after attaching, turn kill-on-exit off. Windows' default kills
        every process a debugger thread is attached to when that thread exits,
        so a Python crash or Ctrl+C would take Clone Hero down too. With it
        off, the thread detaches instead. The call needs the debugging
        connection to exist first, so it comes after DebugActiveProcess:
        https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-debugsetprocesskillonexit
        """
        self._win32 = _Win32()
        self._pid = pid
        k32 = self._win32.k32
        if not k32.DebugActiveProcess(pid):
            raise ctypes.WinError(ctypes.get_last_error())
        if not k32.DebugSetProcessKillOnExit(False):
            err = ctypes.get_last_error()
            k32.DebugActiveProcessStop(pid)   # never stay attached with kill-on-exit on
            raise ctypes.WinError(err)
        self._proc_handle = k32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
```

- [ ] **Step 11: Detach safely in `stop`.** Replace:

```python
    def stop(self) -> None:
        """Detach and let the process run free. LIVE-ONLY."""
        self._stopped = True
        if self._win32 and self._pid is not None:
            # Restore every patched byte before we walk away.
            for addr in list(self._table.armed_originals().keys()):
                try:
                    self._table.disarm(addr)
                except Exception:
                    pass
            self._win32.k32.DebugActiveProcessStop(self._pid)
```

with:

```python
    def stop(self) -> None:
        """Detach and let the process run free. LIVE-ONLY.

        Call it from the thread that attached; only that thread gets debug
        events. Order matters, because a 0xCC or a single-step trap left for
        the game after we detach would crash it:
          1. Put every patched byte back and forget the breakpoints.
          2. Answer queued debug events until none is left and no thread is
             still mid-step over a breakpoint (at most one second).
          3. Detach.
        """
        self._stopped = True
        if self._win32 and self._pid is not None:
            for addr in self._table.addresses():
                try:
                    self._table.remove(addr)
                except Exception:
                    pass
                self._removed.add(addr)
            self._drain(timeout_s=1.0)
            self._win32.k32.DebugActiveProcessStop(self._pid)
```

- [ ] **Step 12: Add the drain and the rewind.** Right after `stop`, add:

```python
    def _drain(self, timeout_s: float) -> None:
        """Answer queued debug events until the queue is empty and no thread
        is mid-step, or until timeout_s passes. LIVE-ONLY."""
        ev = DEBUG_EVENT()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if not self._win32.k32.WaitForDebugEvent(ctypes.byref(ev), 50):
                if not self._pending_rearm:
                    return
                continue
            status = DBG_CONTINUE
            if ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT:
                status = self._handle_exception(ev)
            self._win32.k32.ContinueDebugEvent(
                ev.dwProcessId, ev.dwThreadId, status)

    def _rewind_rip(self, tid: int) -> None:
        """Move one thread's rip back one byte, onto the instruction our 0xCC
        had replaced (its real byte is back now). LIVE-ONLY."""
        handle = self._win32.k32.OpenThread(THREAD_ALL_ACCESS, False, tid)
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            ctx = CONTEXT()
            ctx.ContextFlags = CONTEXT_ALL
            if not self._win32.k32.GetThreadContext(handle, ctypes.byref(ctx)):
                raise ctypes.WinError(ctypes.get_last_error())
            ctx.Rip = adjust_rip_after_int3(ctx.Rip)
            if not self._win32.k32.SetThreadContext(handle, ctypes.byref(ctx)):
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            self._win32.k32.CloseHandle(handle)
```

- [ ] **Step 13: Handle late events in `_handle_exception`.** Replace:

```python
        if code == EXCEPTION_BREAKPOINT:
            if self._table.is_armed(addr):
                self._on_our_breakpoint(addr, tid)
                return DBG_CONTINUE
            if not self._seen_initial:
```

with:

```python
        if code == EXCEPTION_BREAKPOINT:
            if self._table.is_armed(addr):
                self._on_our_breakpoint(addr, tid)
                return DBG_CONTINUE
            if addr in self._removed:
                # A thread reached one of our 0xCC bytes just before stop()
                # put the real byte back; its event was already queued. Step
                # rip back onto the restored instruction so it runs normally.
                self._rewind_rip(tid)
                return DBG_CONTINUE
            if not self._seen_initial:
```

and replace:

```python
            if addr_to_rearm is not None:
                # We stepped over the restored instruction; put 0xCC back.
                self._table.arm(addr_to_rearm)
                return DBG_CONTINUE
```

with:

```python
            if addr_to_rearm is not None:
                # We stepped over the restored instruction; put 0xCC back,
                # unless stop() has removed that breakpoint meanwhile.
                if self._table.has(addr_to_rearm):
                    self._table.arm(addr_to_rearm)
                return DBG_CONTINUE
```

- [ ] **Step 14: Run them and watch them pass.** Run `python -m pytest tools/ch_probe/tests -q`. Expected: `N passed`, no `failed`, where N is the Task 0 count plus 8. Then run `Select-String -Path tools\ch_probe\experiments\play_chart.py -Pattern 'NOTE_TO_LANE'`. Expected: nothing printed.

- [ ] **Step 15: Commit.**

```bash
git add tools/ch_probe/debugger.py tools/ch_probe/experiments/play_chart.py tools/ch_probe/tests/test_debugger.py tools/ch_probe/tests/test_play_chart.py
git commit -m "ch_probe: detach from Clone Hero safely; play .chart cymbals and 2x kicks

Task: Task 19: ch_probe fixes
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["tools/ch_probe/debugger.py","tools/ch_probe/experiments/play_chart.py","tools/ch_probe/tests/test_debugger.py","tools/ch_probe/tests/test_play_chart.py"],"verifyCommand":"python -m pytest tools/ch_probe/tests -q","acceptanceCriteria":["test_process.py passes with 0 failed (already fixed on disk)","test_play_chart.py passes: one lane per gem, cymbal markers and 2x kick honoured","test_debugger.py passes including the kill-on-exit and safe-detach tests","the whole ch_probe suite shows 0 failed","NOTE_TO_LANE no longer appears in play_chart.py"],"modelTier":"standard"}
```

---

### Task 20: ch_probe restructure

`play_chart.py` is the one ch_probe script proven at the game. It auto-played "Slipping" on 2026-09-25 and hit the notes. But it gets there by going around the tool's seven-piece design, so the unit tests cover code the working tool never runs. It finds the engine with its own copy of a memory scan instead of `EngineModel`. It reads the clock and score at offsets it defines itself. It presses keys through `InputDriver._send_key`, a private method. Other scripts copied the same pieces. There are ten copies of "find the engine" (find_engine, find_clock, find_clock2, find_clock3, hit_detect, send_inputs, reactive_inputs, poll_windows, play_chart and live), and five copies of the key table (input_driver, play_chart's `LANE_NAMES`, pad_flash_test, key_delivery_test, and hit_detect's hand-listed bindings).

This task moves the working pieces into tracked, tested modules and makes play_chart import them. The logic moves unchanged. Think of it like moving a recipe from a sticky note into the cookbook: same steps, one copy.

Here is what moves. The memory scan and the "whose clock moves" check go into a new `engine_finder.py`. Clone Hero leaves old engine objects frozen on the heap after a restart, so the live engine is the one whose clock changes between two reads. The key table becomes one `Lane` list in `input_driver.py`, with the short names play_chart prints. The chord press becomes a public `InputDriver.press_chord`, which is play_chart's exact press: all keys down, 3 ms hold, all up. `EngineModel` learns to take an engine pointer found by the scan (`use_object`) and to read the score. It then reads the clock at +0x100, the field play_chart proved.

Offset 0x100 has three names today. constants.py calls it `OFF_HIT_TIME`, play_chart calls it `OFF_SONG_CLOCK`, and live.py calls it `OFF_CLOCK`. The live game settled what it is, because play_chart plays whole songs off it. So it becomes `constants.OFF_SONG_CLOCK`, the only name. The real hit-time candidate, +0x2e0, moves from live.py into constants.py as `OFF_HIT_TIME`. It is marked "not yet confirmed live", because step 4 of the hit-window plan checks it. `engine.song_clock` stops reading its guessed 0x1A0, and the test that locked the guess in now checks 0x100.

"Lane" now means one thing. In input_driver, lane 0 is the green pad and lane 4 is the kick. In probe_chart and constants, "lane 0" meant the .chart note number 0, which is the kick. So chart code now says "note" (`PROBE_CHART_NOTE_KICK`, `probe_chart(..., note=...)`), and "lane" always means an input lane. This also fixes a live bug in active_probe, which pressed lane 0 (green, the A key) for a kick chart.

Then the dead code goes. The superseded experiments go: find_clock.py and find_clock2.py (find_clock3 and then play_chart's proven clock replaced them), reactive_inputs.py and send_inputs.py (play_chart replaced both). `ocr.read_accuracy_ms` goes, with the two screen-capture helpers only it used. `set_hw_data_breakpoint` goes too. It is deleted, not implemented, for two reasons. A hardware breakpoint has to be set on every thread that might write the field, and Unity creates threads all the time. And nothing needs it, because the int3 route works and the passive probe reads +0x20 at the next formula call (Task 21). The constants nothing reads go as well. The Ghidra facts that no code reads yet stay, because constants.py is the one place addresses live: `GHIDRA_IMAGE_BASE`, `RVA_BASE_ENGINE_CTOR_DRUMS` and `RVA_NOTE_PROCESSING`.

hit_detect.py, find_clock3.py and poll_windows.py stay. watch_window.py says it is "poll_windows.py plus the song clock", and walk_edges.py measures hits the way hit_detect tried to. But neither new script has run at the game yet, so deleting the old ones would be premature. They only get their imports repointed, the private key call renamed, and hit_detect's 0x100 constant replaced.

The README gains the play_chart route. The spec's "not yet implemented" status is updated, and so are its stale `scratch_ms/` path, its swapped normal-mode labels and its "+0x100 = hit time" line.

What the user sees: nothing. play_chart, walk_edges and watch_window read the same bytes, press the same keys with the same timing, and print the same lines. pad_flash_test's watch hints read "GREEN", "KICK", "YELLOW CYMBAL" instead of the hand-written phrases. key_delivery_test's labels read "A (Grn)" instead of "A (Green)".

**Depends on:** Task 19.
**Expected overlaps:** Task 21 rewrites `experiments/passive_probe.py` and `experiments/active_probe.py` (this task touches only active_probe's `PROBE_LANE` lines and its `generate_probe_chart` call). Task 21 also adds `probe_note_ticks` to `probe_chart.py` (this task renames `lane` to `note` there) and adds `rsp` to `interfaces.ThreadContext`. Task 0 edits `constants.py`'s module docstring (the scratch_ms path); this task edits only its offset, expectation and probe-layout blocks. Task 19's `debugger.py` changes stay as they are.
**Goal:** play_chart runs on tracked, tested modules, with one engine finder, one key table, one name per offset and one meaning of "lane".

**Files:**
- Create: `tools/ch_probe/engine_finder.py`, `tools/ch_probe/tests/test_engine_finder.py`
- Modify: `tools/ch_probe/constants.py`, `tools/ch_probe/engine.py`, `tools/ch_probe/process.py` (a `handle` property), `tools/ch_probe/input_driver.py`, `tools/ch_probe/probe_chart.py`, `tools/ch_probe/interfaces.py`, `tools/ch_probe/debugger.py`, `tools/ch_probe/ocr.py`, `tools/ch_probe/README.md`, `docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md`
- Modify (experiments): `play_chart.py`, `live.py`, `walk_edges.py`, `watch_window.py`, `find_engine.py`, `hit_detect.py`, `find_clock3.py`, `poll_windows.py`, `pad_flash_test.py`, `key_delivery_test.py`, `active_probe.py`
- Delete: `tools/ch_probe/experiments/find_clock.py`, `find_clock2.py`, `reactive_inputs.py`, `send_inputs.py`
- Test: `tests/test_engine.py`, `tests/test_input_driver.py`, `tests/test_debugger.py`, `tests/test_analysis.py`, `tests/test_hit_window_scripts.py`, `tests/test_play_chart.py`

**Acceptance Criteria:**
- [ ] `python -m pytest tools/ch_probe/tests -q` shows 0 failed, including every case in `test_engine_finder.py` and the new `TestSongClock`, `TestScoreAndFoundObject`, `TestKeyTable`, `TestPressChord` and `SharedPiecesTest` cases.
- [ ] `Get-ChildItem tools\ch_probe -Recurse -Filter *.py | Select-String -Pattern 'OFF_\w*\s*=\s*0x(100|1A0|2E0)\b'` prints exactly two lines, both in constants.py: `OFF_SONG_CLOCK = 0x100` and `OFF_HIT_TIME = 0x2E0`.
- [ ] `Get-ChildItem tools\ch_probe -Recurse -Filter *.py | Where-Object FullName -notmatch '\\tests\\' | Select-String -Pattern '_send_key|set_hw_data_breakpoint|read_accuracy_ms|_PLACEHOLDER_SONG_CLOCK|PROBE_LANE_KICK|DRUM_LANE_KICK|find_active_engine|def find_live_engine'` prints exactly one line: `engine_finder.py` with `def find_live_engine(`. The tests are left out on purpose, because they name the removed things to prove they are gone.
- [ ] `Get-ChildItem tools\ch_probe -Recurse -Filter *.py | Select-String -Pattern 'def scan_for_engine'` prints exactly one line, in engine_finder.py.
- [ ] `Test-Path tools\ch_probe\experiments\find_clock.py, tools\ch_probe\experiments\find_clock2.py, tools\ch_probe\experiments\reactive_inputs.py, tools\ch_probe\experiments\send_inputs.py` prints `False` four times.
- [ ] `Select-String -Path docs\superpowers\specs\2026-09-17-ch-dynamic-input-probe.md -Pattern 'not yet implemented|scratch_ms/'` prints nothing.

**Verify:** `python -m pytest tools/ch_probe/tests -q` -> `N passed`, no `failed`.

**Steps:**

- [ ] **Step 1: Write the failing engine-finder tests.** Create `tools/ch_probe/tests/test_engine_finder.py`:

```python
"""Tests for engine_finder: the memory scan's match maths and the "whose clock
moves" check that picks the live engine.

No game: a fake process answers reads from dicts, and a fake scan hands back
the candidate addresses a real scan would find.
"""

from __future__ import annotations

import io
import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants as C  # noqa: E402
from tools.ch_probe import engine_finder as F  # noqa: E402

BASE = 0x180000000
W = C.OFF_TOTAL_WINDOW
K = C.OFF_SONG_CLOCK


class FakeProc:
    """Doubles from a dict. A clock address holds a list of values, read one
    per call; the last value repeats."""

    def __init__(self, doubles=None, clocks=None, raw=None):
        self.module_base = BASE
        self._doubles = dict(doubles or {})
        self._clocks = {a: list(v) for a, v in (clocks or {}).items()}
        self._raw = dict(raw or {})

    def resolve(self, rva):
        return BASE + rva

    def read(self, addr, size):
        return self._raw[addr][:size]

    def read_double(self, addr):
        seq = self._clocks.get(addr)
        if seq is not None:
            return seq.pop(0) if len(seq) > 1 else seq[0]
        if addr in self._doubles:
            return self._doubles[addr]
        raise OSError(f"nothing mapped at {addr:#x}")


class HitsInRegionTest(unittest.TestCase):
    def test_each_match_backs_up_to_the_object_start(self):
        pattern = b"B" * 8 + b"F" * 8
        data = b"\0" * 0x40 + pattern + b"\0" * 0x20 + pattern
        self.assertEqual(F.hits_in_region(0x5000, data, pattern),
                         [0x5000 + 0x40 - 0x30, 0x5000 + 0x70 - 0x30])


class FindLiveEngineTest(unittest.TestCase):
    def test_picks_the_engine_whose_clock_moves(self):
        frozen, live, in_module, empty = 0x10000, 0x20000, BASE + 0x100, 0x30000
        proc = FakeProc(
            doubles={frozen + W: 0.17, live + W: 0.17, in_module + W: 0.17,
                     empty + W: 0.0},
            clocks={frozen + K: [21.95], live + K: [1.0, 1.1],
                    in_module + K: [0.0, 5.0]})
        sleeps = []

        def scan(p, back, front):
            return [frozen, in_module, empty, live], 0, 0

        got = F.find_live_engine(proc, [(b"b", b"f")], scan=scan,
                                 sleep=sleeps.append, out=io.StringIO())
        self.assertEqual(got, live)
        self.assertEqual(sleeps, [0.12])

    def test_keeps_waiting_until_a_clock_moves(self):
        live = 0x20000
        proc = FakeProc(doubles={live + W: 0.17},
                        clocks={live + K: [1.0, 1.0, 1.0, 1.2]})
        sleeps, out = [], io.StringIO()
        got = F.find_live_engine(proc, [(b"b", b"f")],
                                 scan=lambda p, b, f: ([live], 0, 0),
                                 sleep=sleeps.append, out=out)
        self.assertEqual(got, live)
        self.assertEqual(sleeps, [0.12, 0.4, 0.12])
        self.assertEqual(out.getvalue(), ".")

    def test_a_candidate_two_patterns_find_is_checked_once_in_scan_order(self):
        a, b = 0x10000, 0x20000
        proc = FakeProc(doubles={a + W: 0.17, b + W: 0.17},
                        clocks={a + K: [1.0, 1.1], b + K: [2.0, 2.1]})
        answers = iter([([a, b], 0, 0), ([b, a], 0, 0)])
        got = F.find_live_engine(proc, [(b"n", b"n"), (b"p", b"p")],
                                 scan=lambda p, back, front: next(answers),
                                 sleep=lambda s: None, out=io.StringIO())
        self.assertEqual(got, a)


class PatternsTest(unittest.TestCase):
    def test_all_patterns_tries_the_precision_pair_both_ways(self):
        raw = {
            BASE + C.RVA_CONST_NORMAL_BACK: b"NB" * 4,
            BASE + C.RVA_CONST_NORMAL_FRONT: b"NF" * 4,
            BASE + C.RVA_CONST_PRECISION_BACK: b"PB" * 4,
            BASE + C.RVA_CONST_PRECISION_FRONT: b"PF" * 4,
        }
        proc = FakeProc(raw=raw)
        self.assertEqual(F.normal_pattern(proc), (b"NB" * 4, b"NF" * 4))
        self.assertEqual(F.all_patterns(proc), [
            (b"NB" * 4, b"NF" * 4), (b"PB" * 4, b"PF" * 4), (b"PF" * 4, b"PB" * 4)])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Write the failing engine tests.** In `tools/ch_probe/tests/test_engine.py`, delete the `set_hw_data_breakpoint` method from `FakeDebugger`:

```python
    def set_hw_data_breakpoint(self, addr, size=8):
        pass

```

Replace:

```python
    def test_hit_time_reads_off_0x100(self):
        eng = self._engine_with_fields(hit_time=12.34)
        self.assertEqual(eng.hit_time(), 12.34)
```

with:

```python
    def test_hit_time_reads_off_0x2e0(self):
        # +0x100 is the song clock (proven live); the hit-time candidate is
        # +0x2e0, still to be confirmed at the game.
        self.assertEqual(C.OFF_HIT_TIME, 0x2E0)
        eng = self._engine_with_fields(hit_time=12.34)
        self.assertEqual(eng.hit_time(), 12.34)
```

Replace the whole `TestSongClock` class:

```python
class TestSongClock(unittest.TestCase):
    def test_reads_the_placeholder_field(self):
        # The song-clock source is unconfirmed; we only check the method reads
        # a double off the object at the documented placeholder offset. The
        # value itself cannot be trusted until pinned live.
        offset = engine._PLACEHOLDER_SONG_CLOCK_OFFSET
        doubles = {OBJ + offset: 3.5}
        eng, proc, dbg = make_engine(object_ptr=OBJ, doubles=doubles)
        eng.capture_object()
        self.assertEqual(eng.song_clock(), 3.5)
```

with:

```python
class TestSongClock(unittest.TestCase):
    def test_reads_the_proven_clock_at_0x100(self):
        # play_chart.py plays whole songs off this field (2026-09-25), so the
        # song clock is +0x100, in seconds.
        self.assertEqual(C.OFF_SONG_CLOCK, 0x100)
        doubles = {OBJ + C.OFF_SONG_CLOCK: 3.5}
        eng, proc, dbg = make_engine(object_ptr=OBJ, doubles=doubles)
        eng.capture_object()
        self.assertEqual(eng.song_clock(), 3.5)

    def test_no_placeholder_offset_is_left(self):
        self.assertFalse(hasattr(engine, "_PLACEHOLDER_SONG_CLOCK_OFFSET"))


class TestScoreAndFoundObject(unittest.TestCase):
    def test_score_reads_u32_off_0x94(self):
        eng, proc, dbg = make_engine(object_ptr=OBJ,
                                     dwords={OBJ + C.OFF_SCORE: 4200})
        eng.capture_object()
        self.assertEqual(eng.score(), 4200)

    def test_use_object_takes_a_scanned_pointer_without_a_debugger(self):
        proc = FakeProcess(doubles={OBJ + C.OFF_SONG_CLOCK: 1.25})
        eng = engine.EngineModel(proc)
        self.assertEqual(eng.use_object(OBJ), OBJ)
        self.assertEqual(eng.object_ptr, OBJ)
        self.assertEqual(eng.song_clock(), 1.25)

    def test_capture_without_a_debugger_raises(self):
        eng = engine.EngineModel(FakeProcess())
        with self.assertRaises(RuntimeError):
            eng.capture_object()
```

- [ ] **Step 3: Write the failing key-table tests.** In `tools/ch_probe/tests/test_input_driver.py`, replace:

```python
from tools.ch_probe.input_driver import DEFAULT_BINDINGS, InputDriver
```

with:

```python
from tools.ch_probe.input_driver import DEFAULT_BINDINGS, LANE_NAMES, InputDriver, Lane
```

Replace `driver._send_key = lambda vk, key_up: presses.append((vk, key_up))` with `driver.send_key = lambda vk, key_up: presses.append((vk, key_up))`, `real_send = driver._send_key` with `real_send = driver.send_key`, and `driver._send_key = spy` with `driver.send_key = spy`. Then add before `if __name__ == "__main__":`:

```python
class TestKeyTable(unittest.TestCase):
    """One key table. A lane number means the same key everywhere."""

    def test_lanes_follow_the_bind_screen(self):
        keys = {lane: chr(DEFAULT_BINDINGS[lane]) for lane in Lane}
        self.assertEqual(keys, {
            Lane.GREEN: "A", Lane.RED: "S", Lane.YELLOW: "J", Lane.BLUE: "K",
            Lane.KICK: "L", Lane.YELLOW_CYMBAL: "U", Lane.BLUE_CYMBAL: "Y",
            Lane.GREEN_CYMBAL: "T"})

    def test_lane_numbers_are_unchanged(self):
        self.assertEqual([int(lane) for lane in Lane], list(range(8)))
        self.assertEqual(Lane.KICK, 4)

    def test_every_lane_has_a_short_name(self):
        self.assertEqual(set(LANE_NAMES), set(Lane))
        self.assertEqual(LANE_NAMES[Lane.KICK], "Kick")


class TestPressChord(unittest.TestCase):
    """The press play_chart proved at the game: all down, hold, all up."""

    def test_all_down_then_all_up(self):
        driver, presses = _make_driver()
        sent = driver.press_chord([Lane.RED, Lane.KICK],
                                  sleep=lambda s: presses.append(("sleep", s)))
        red, kick = DEFAULT_BINDINGS[Lane.RED], DEFAULT_BINDINGS[Lane.KICK]
        self.assertEqual(presses, [(red, False), (kick, False), ("sleep", 0.003),
                                   (red, True), (kick, True)])
        self.assertEqual(sent, [red, kick])

    def test_unbound_lane_is_skipped(self):
        driver = InputDriver(bindings={Lane.KICK: 0x4C})
        presses = []
        driver.send_key = lambda vk, key_up: presses.append((vk, key_up))
        driver.press_chord([Lane.GREEN, Lane.KICK], sleep=lambda s: None)
        self.assertEqual(presses, [(0x4C, False), (0x4C, True)])
```

- [ ] **Step 4: Write the failing shared-pieces and naming tests.** Append to `tools/ch_probe/tests/test_play_chart.py`, before `if __name__ == "__main__":`:

```python
class SharedPiecesTest(unittest.TestCase):
    """play_chart runs on the tracked modules, not private copies."""

    def test_no_private_copies_are_left(self):
        for name in ("find_active_engine", "OFF_SONG_CLOCK", "OFF_SCORE",
                     "NOTE_TO_LANE", "scan_for_engine"):
            self.assertFalse(hasattr(play_chart, name), name)

    def test_lane_names_come_from_the_key_table(self):
        from tools.ch_probe import input_driver
        self.assertIs(play_chart.LANE_NAMES, input_driver.LANE_NAMES)
```

In `tools/ch_probe/tests/test_analysis.py`, replace:

```python
    def test_probe_chart_spacings_and_lane_come_from_constants(self):
        self.assertEqual(tuple(probe_chart.DEFAULT_SPACINGS_MS), tuple(C.PROBE_SPACINGS_MS))
        self.assertEqual(probe_chart.DRUM_LANE_KICK, C.PROBE_LANE_KICK)
```

with:

```python
    def test_probe_chart_spacings_and_note_come_from_constants(self):
        self.assertEqual(tuple(probe_chart.DEFAULT_SPACINGS_MS), tuple(C.PROBE_SPACINGS_MS))
        self.assertEqual(probe_chart.DRUM_NOTE_KICK, C.PROBE_CHART_NOTE_KICK)
        self.assertEqual(C.PROBE_CHART_NOTE_KICK, 0)   # a .chart note, not an input lane
```

In `tools/ch_probe/tests/test_hit_window_scripts.py`, replace:

```python
from tools.ch_probe import probe_songs as P  # noqa: E402
```

with:

```python
from tools.ch_probe import constants as C  # noqa: E402
from tools.ch_probe import probe_songs as P  # noqa: E402
```

and replace the whole `LiveSnapshotTest` class:

```python
class LiveSnapshotTest(unittest.TestCase):
    def test_decode_reads_each_field_at_its_offset(self):
        raw = bytearray(live.SNAPSHOT_SIZE)
        struct.pack_into("<d", raw, live.OFF_WINDOW, 0.17143)
        struct.pack_into("<I", raw, live.OFF_SCORE, 1234)
        struct.pack_into("<d", raw, live.OFF_CLOCK, 12.5)
        struct.pack_into("<I", raw, live.OFF_FLAGS, 0x1000)
        struct.pack_into("<d", raw, live.OFF_HIT_TIME, 12.49)
        s = live.decode_snapshot(bytes(raw))
        self.assertAlmostEqual(s.window_ms, 171.43)
        self.assertEqual(s.score, 1234)
        self.assertEqual(s.clock_s, 12.5)
        self.assertEqual(s.hit_time_s, 12.49)
        self.assertTrue(s.precision)
```

with:

```python
class LiveSnapshotTest(unittest.TestCase):
    def test_decode_reads_each_field_at_its_offset(self):
        raw = bytearray(live.SNAPSHOT_SIZE)
        struct.pack_into("<d", raw, C.OFF_TOTAL_WINDOW, 0.17143)
        struct.pack_into("<I", raw, C.OFF_SCORE, 1234)
        struct.pack_into("<d", raw, C.OFF_SONG_CLOCK, 12.5)
        struct.pack_into("<I", raw, C.OFF_FLAGS, 0x1000)
        struct.pack_into("<d", raw, C.OFF_HIT_TIME, 12.49)
        s = live.decode_snapshot(bytes(raw))
        self.assertAlmostEqual(s.window_ms, 171.43)
        self.assertEqual(s.score, 1234)
        self.assertEqual(s.clock_s, 12.5)
        self.assertEqual(s.hit_time_s, 12.49)
        self.assertTrue(s.precision)

    def test_live_keeps_no_offsets_or_finder_of_its_own(self):
        for name in ("OFF_WINDOW", "OFF_SCORE", "OFF_CLOCK", "OFF_FLAGS",
                     "OFF_HIT_TIME", "find_live_engine"):
            self.assertFalse(hasattr(live, name), name)
```

In `tools/ch_probe/tests/test_debugger.py`, delete the whole `test_hw_data_breakpoint_flags_itself_live_only` method:

```python
    def test_hw_data_breakpoint_flags_itself_live_only(self):
        # It must not silently pretend to work before attach; it is unverified.
        dbg = Debugger()
        with self.assertRaises(RuntimeError):
            dbg.set_hw_data_breakpoint(0x140002000)

```

and in `test_debugger_constructs_without_attaching`, replace:

```python
        for name in ("attach", "set_breakpoint", "clear_breakpoint",
                     "set_hw_data_breakpoint", "read", "write", "run", "stop"):
            self.assertTrue(callable(getattr(dbg, name)), name)
```

with:

```python
        for name in ("attach", "set_breakpoint", "clear_breakpoint",
                     "read", "write", "run", "stop"):
            self.assertTrue(callable(getattr(dbg, name)), name)
        self.assertFalse(hasattr(dbg, "set_hw_data_breakpoint"))
```

- [ ] **Step 5: Run them and watch them fail.** Run `python -m pytest tools/ch_probe/tests -q`. Expected: collection errors for `test_engine_finder.py` (ModuleNotFoundError: `tools.ch_probe.engine_finder`), `test_input_driver.py` (ImportError: `LANE_NAMES`, `Lane`) and `test_hit_window_scripts.py`. After that come AttributeErrors on `C.OFF_SONG_CLOCK`, `C.OFF_SCORE`, `C.PROBE_CHART_NOTE_KICK`, `eng.score`, `eng.use_object` and `probe_chart.DRUM_NOTE_KICK`. `test_hit_time_reads_off_0x2e0` fails (0x100 != 0x2E0), `test_no_private_copies_are_left` fails on `find_active_engine`, and the hw-breakpoint `assertFalse` fails.

- [ ] **Step 6: One name per offset, and the unused constants go.** In `tools/ch_probe/constants.py`, replace:

```python
# Hit time (double): engine timestamp captured at the moment of a hit.
OFF_HIT_TIME = 0x100
```

with:

```python
# Song clock (double, seconds). Proven live on 2026-09-25: play_chart.py reads
# it continuously and plays whole songs on time. The Ghidra notes called this
# field "hit time"; the running game shows it is the song clock.
OFF_SONG_CLOCK = 0x100

# Score (u32). Rises only when a note is hit (proven by play_chart.py).
OFF_SCORE = 0x94

# Hit-time candidate (double, seconds). The code reading says the game copies
# the song clock here on a hit. NOT yet confirmed live: step 4 of
# docs/superpowers/plans/2026-09-25-hit-window-testing.md checks it.
OFF_HIT_TIME = 0x2E0
```

Replace:

```python
# Expected values in SECONDS (the game's native unit for these constants).
EXPECT_NORMAL_BACK_S = 0.085
EXPECT_NORMAL_FRONT_S = 0.0375
EXPECT_PRECISION_BACK_S = 0.040
EXPECT_PRECISION_FRONT_S = 0.025

# Legacy ms names still used by the passive probe's clamp verdict.
EXPECT_NORMAL_BACK_MS = 85.0
EXPECT_NORMAL_FRONT_MS = 37.5
EXPECT_PRECISION_BACK_MS = 40.0
EXPECT_PRECISION_FRONT_MS = 25.0
EXPECT_DIVISOR = 1000.0

# The linear coefficient the originating session read straight from .rdata
# (resolved double 0.0110924370). Not stored as an RVA in the spec, kept here
# as a known-good value the formula constants should reproduce.
KNOWN_LINEAR_COEFFICIENT = 0.0110924370
```

with:

```python
# Expected values in SECONDS (the game's native unit for these constants).
EXPECT_NORMAL_BACK_S = 0.085
EXPECT_NORMAL_FRONT_S = 0.0375

# The back-window edges in ms, which the passive probe's clamp verdict uses.
EXPECT_NORMAL_BACK_MS = 85.0
EXPECT_PRECISION_BACK_MS = 40.0
```

Replace:

```python
# ---- probe chart layout -------------------------------------------------
# Note-pair spacings the probe chart lays out, and the lane it uses (the kick,
# lane 0). probe_chart.py and experiments/active_probe.py both read these.
PROBE_SPACINGS_MS: tuple[float, ...] = (
    30, 50, 100, 150, 180, 185, 190, 195, 205, 211, 220, 240, 300,
)
PROBE_LANE_KICK = 0
```

with:

```python
# ---- probe chart layout -------------------------------------------------
# Note-pair spacings the probe chart lays out, and the .chart NOTE number it
# writes (0 is the kick in a .chart). This is a chart note, not an input lane:
# input lanes are input_driver.Lane, where the kick is Lane.KICK (4).
# probe_chart.py and experiments/active_probe.py both read these.
PROBE_SPACINGS_MS: tuple[float, ...] = (
    30, 50, 100, 150, 180, 185, 190, 195, 205, 211, 220, 240, 300,
)
PROBE_CHART_NOTE_KICK = 0
```

- [ ] **Step 7: Give `Process` a public handle.** In `tools/ch_probe/process.py`, right after `Process.__init__`, add:

```python
    @property
    def handle(self) -> Optional[int]:
        """The raw OS process handle, or None for a Process built in a test.
        engine_finder's memory scan needs it for VirtualQueryEx."""
        return self._handle
```

- [ ] **Step 8: Create `tools/ch_probe/engine_finder.py`.** The scan is `find_engine.scan_for_engine` moved as it is, with its inner match loop lifted into `hits_in_region` so it can be tested. `find_live_engine` is play_chart's `find_active_engine` loop. The only change is that it takes a list of patterns, which is how live.py searched both scoring modes. The pattern lists are live.py's `_engine_patterns`, split in two.

```python
"""Find the live DrumsEngine object by reading memory; no debugger needed.

Plain version: the engine object keeps copies of the two window constants side
by side at +0x30 and +0x38. So we search the game's writable memory for those
16 bytes, and every match is an engine-shaped object. Clone Hero leaves old
engine objects on the heap after a restart, frozen at their last clock, so the
live one is the only candidate whose song clock moves between two reads.

This is the route play_chart.py proved at the game on 2026-09-25. The memory
scan moved here from experiments/find_engine.py; the "whose clock moves" check
from play_chart.py and experiments/live.py. Behaviour is unchanged.
"""

from __future__ import annotations

import ctypes
import sys
import time
from typing import Callable, List, Optional, Sequence, TextIO, Tuple

from . import constants as C

Pattern = Tuple[bytes, bytes]   # (back-window bytes, front-window bytes)

# Matches this far past the module base are GameAssembly.dll's own .rdata
# copies of the constants, not engine objects.
MODULE_SPAN = 0x4000000


# --- the memory scan ---------------------------------------------------------

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", ctypes.c_ulong),
        ("RegionSize", ctypes.c_size_t),
        ("State", ctypes.c_ulong),
        ("Protect", ctypes.c_ulong),
        ("Type", ctypes.c_ulong),
    ]


MEM_COMMIT = 0x1000
PAGE_READWRITE = 0x04
PAGE_WRITECOPY = 0x08
PAGE_EXECUTE_READWRITE = 0x40
PAGE_EXECUTE_WRITECOPY = 0x80


def is_rw(protect: int) -> bool:
    return protect in (
        PAGE_READWRITE, PAGE_WRITECOPY,
        PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY,
    )


def hits_in_region(base: int, data: bytes, pattern: bytes) -> List[int]:
    """Object addresses for every match of `pattern` in one region's bytes.
    The pattern sits at +0x30 in the object, so each object starts 0x30
    before its match."""
    hits: List[int] = []
    offset = 0
    while True:
        pos = data.find(pattern, offset)
        if pos == -1:
            return hits
        hits.append(base + pos - C.OFF_BACK_WINDOW)
        offset = pos + 1


def scan_for_engine(proc, back_bytes: bytes, front_bytes: bytes):
    """Scan committed RW memory for +0x30 = back, +0x38 = front. LIVE-ONLY.
    Returns (object addresses, regions scanned, bytes scanned)."""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    handle = ctypes.c_void_p(proc.handle)
    mbi = MEMORY_BASIC_INFORMATION()
    mbi_size = ctypes.sizeof(mbi)
    pattern = back_bytes + front_bytes  # 16 contiguous bytes

    addr = 0
    hits: List[int] = []
    regions_scanned = 0
    bytes_scanned = 0
    max_addr = 0x7FFFFFFFFFFF  # user-mode limit on 64-bit Windows

    while addr < max_addr:
        ret = k32.VirtualQueryEx(
            handle, ctypes.c_void_p(addr), ctypes.byref(mbi), mbi_size
        )
        if ret == 0:
            break

        if mbi.State == MEM_COMMIT and is_rw(mbi.Protect) and mbi.RegionSize > 0:
            base = mbi.BaseAddress or 0
            size = mbi.RegionSize
            # Skip tiny regions and absurdly large ones
            if 0x100 <= size <= 256 * 1024 * 1024:
                try:
                    data = proc.read(base, size)
                    regions_scanned += 1
                    bytes_scanned += len(data)
                    hits.extend(hits_in_region(base, data, pattern))
                except OSError:
                    pass  # unreadable region

        next_addr = (mbi.BaseAddress or 0) + mbi.RegionSize
        if next_addr <= addr:
            break
        addr = next_addr

    return hits, regions_scanned, bytes_scanned


# --- which window constants to look for ---------------------------------------

def _raw(proc, rva: int) -> bytes:
    return proc.read(proc.resolve(rva), 8)


def normal_pattern(proc) -> Pattern:
    """The pair a normal-mode engine holds at +0x30/+0x38."""
    return (_raw(proc, C.RVA_CONST_NORMAL_BACK), _raw(proc, C.RVA_CONST_NORMAL_FRONT))


def all_patterns(proc) -> List[Pattern]:
    """Normal mode, plus the precision pair in both orders. The precision
    labels are not confirmed live, so both orders are searched."""
    p1 = _raw(proc, C.RVA_CONST_PRECISION_BACK)
    p2 = _raw(proc, C.RVA_CONST_PRECISION_FRONT)
    return [normal_pattern(proc), (p1, p2), (p2, p1)]


# --- whose clock moves ----------------------------------------------------------

def find_live_engine(
    proc,
    patterns: Sequence[Pattern],
    *,
    scan: Callable = scan_for_engine,
    sleep: Callable[[float], None] = time.sleep,
    out: Optional[TextIO] = None,
) -> int:
    """Return the engine object whose song clock is advancing. Blocks, printing
    a dot per try, until a song is playing.

    Clone Hero keeps several engine-shaped objects on the heap, and restarting
    a song allocates a NEW one while the old one stays frozen at its final
    time. So read every candidate's clock twice, a moment apart, and return
    the first one that changed.
    """
    out = sys.stdout if out is None else out
    module_end = proc.module_base + MODULE_SPAN
    while True:
        candidates: dict = {}   # ordered, so scan order decides ties
        for back, front in patterns:
            hits, _, _ = scan(proc, back, front)
            for h in hits:
                if not (proc.module_base <= h < module_end):
                    candidates[h] = None

        first = {}
        for e in candidates:
            try:
                if proc.read_double(e + C.OFF_TOTAL_WINDOW) < 0.001:
                    continue
                first[e] = proc.read_double(e + C.OFF_SONG_CLOCK)
            except OSError:
                pass

        sleep(0.12)

        for e, c0 in first.items():
            try:
                c1 = proc.read_double(e + C.OFF_SONG_CLOCK)
            except OSError:
                continue
            if abs(c1 - c0) > 1e-6:   # clock moved -> this is the live song
                return e

        out.write(".")
        out.flush()
        sleep(0.4)
```

- [ ] **Step 9: Let `EngineModel` use a scanned pointer, the proven clock and the score.** In `tools/ch_probe/engine.py`, delete the whole placeholder block:

```python
# --- song clock placeholder --------------------------------------------------
#
# OPEN QUESTION (from the spec): where the current song time actually lives is
# not yet known. The spec lists "song clock source" as a thing the next session
# must pin down against the running game. Until someone reads it live, this is a
# guess: a double at this byte offset off the engine object.
#
# This offset is NOT in constants.py on purpose -- constants.py holds only
# addresses that were confirmed in the Ghidra dumps, and this one wasn't. When
# the field is pinned live, move it into constants.py and delete this block.
_PLACEHOLDER_SONG_CLOCK_OFFSET = 0x1A0  # UNCONFIRMED -- must be verified live.

```

Replace:

```python
    def __init__(self, process: "ProcessHandle", debugger: "Debugger") -> None:
        self._process = process
        self._debugger = debugger
```

with:

```python
    def __init__(self, process: "ProcessHandle",
                 debugger: Optional["Debugger"] = None) -> None:
        self._process = process
        # Only capture_object() needs a debugger. A runner that finds the
        # engine by memory scan (engine_finder) passes none.
        self._debugger = debugger
```

In `capture_object`, replace:

```python
        addr = self._process.resolve(C.RVA_DRUMS_ENGINE_CTOR)

        def _on_ctor(debugger: "Debugger", ctx: "ThreadContext") -> None:
```

with:

```python
        if self._debugger is None:
            raise RuntimeError(
                "capture_object needs a debugger; use use_object() with a "
                "pointer from engine_finder instead")
        addr = self._process.resolve(C.RVA_DRUMS_ENGINE_CTOR)

        def _on_ctor(debugger: "Debugger", ctx: "ThreadContext") -> None:
```

Right after `capture_object`'s `return self.object_ptr`, add:

```python
    def use_object(self, object_ptr: int) -> int:
        """Adopt an engine object found another way: the memory scan in
        engine_finder, the route play_chart.py proved live. Returns it."""
        self.object_ptr = object_ptr
        return object_ptr
```

Replace:

```python
    def hit_time(self) -> float:
        """Engine timestamp captured at the moment of a hit."""
        return self._process.read_double(self._addr(C.OFF_HIT_TIME))
```

with:

```python
    def hit_time(self) -> float:
        """Song time of the last hit, in seconds, from +0x2e0. The code reading
        says the game copies the clock here on a hit; not yet confirmed live."""
        return self._process.read_double(self._addr(C.OFF_HIT_TIME))

    def score(self) -> int:
        """The game score. It rises only when a note is hit, which is how
        play_chart.py tells a hit from a miss."""
        return self._process.read_u32(self._addr(C.OFF_SCORE))
```

Replace the whole `song_clock` method:

```python
    def song_clock(self) -> float:
        """Current song time in seconds.

        LIVE-ONLY SEAM: the real source of the song clock is an open question in
        the spec and has not been confirmed against the running game. This reads
        a placeholder field (see _PLACEHOLDER_SONG_CLOCK_OFFSET above) so the
        method exists and the callers can be written, but the number it returns
        cannot be trusted until the field is pinned live.
        """
        return self._process.read_double(self._addr(_PLACEHOLDER_SONG_CLOCK_OFFSET))
```

with:

```python
    def song_clock(self) -> float:
        """Current song time in seconds, from +0x100. Proven live on
        2026-09-25: play_chart.py plays whole songs off this field. The game
        writes it once per frame, so a read can be a few ms stale."""
        return self._process.read_double(self._addr(C.OFF_SONG_CLOCK))
```

In the `total_window`, `back_window` and `front_window` docstrings, change "(ms)" to "(seconds)". The game stores them in seconds (constants.py, `EXPECT_NORMAL_BACK_S`).

- [ ] **Step 10: One key table, one meaning of "lane", a public chord press.** In `tools/ch_probe/input_driver.py`, replace:

```python
from typing import Callable, Dict, Optional
```

with:

```python
from enum import IntEnum
from typing import Callable, Dict, Iterable, List, Optional
```

Replace everything from `# --- Placeholder drum-lane key bindings ---` through the closing `}` of `DEFAULT_BINDINGS`:

```python
# --- Placeholder drum-lane key bindings --------------------------------------
#
# OPEN QUESTION (see the spec's "Drum key bindings"): Clone Hero's drum lane
# keys are user-configurable and were NOT captured. The map below is a guess so
# the module imports and its logic can be tested. It is almost certainly wrong
# for any given install.
#
# Before a real active-probe run you MUST either read the real bindings out of
# the game's config or call set_binding() for each lane with a key you have
# confirmed. Do not trust these defaults silently.
#
# Lane numbering follows the probe-chart generator's `lane` argument. The
# values are Windows virtual-key codes.
#
# These are taken directly from the game's own Controller Remap screen
# (Options -> Controls, Player1 drums), which is the authoritative source:
#   Green=A  Red=S  Yellow=J  Blue=K  Orange/Kick=L  2X Kick=O
#   Yellow Cymbal=U  Blue Cymbal=Y  Green Cymbal=T
# Every drum lane is bound to the action of its own color; Orange is the kick.
DEFAULT_BINDINGS: Dict[int, int] = {
    0: 0x41,  # 'A'   -- Green pad      (bind screen 2026-09-25)
    1: 0x53,  # 'S'   -- Red pad        (bind screen 2026-09-25)
    2: 0x4A,  # 'J'   -- Yellow pad     (bind screen 2026-09-25)
    3: 0x4B,  # 'K'   -- Blue pad       (bind screen 2026-09-25)
    4: 0x4C,  # 'L'   -- Orange / Kick  (bind screen 2026-09-25)
    5: 0x55,  # 'U'   -- Yellow Cymbal  (bind screen 2026-09-25)
    6: 0x59,  # 'Y'   -- Blue Cymbal    (bind screen 2026-09-25)
    7: 0x54,  # 'T'   -- Green Cymbal   (bind screen 2026-09-25)
}
```

with:

```python
# --- The one key table ---------------------------------------------------------


class Lane(IntEnum):
    """One input lane per bound key. The number means the same thing everywhere
    in ch_probe: 0 is the green pad, 4 is the kick. A .chart numbers its notes
    differently (note 0 is the kick), so chart code says "note", never "lane"."""

    GREEN = 0
    RED = 1
    YELLOW = 2
    BLUE = 3
    KICK = 4
    YELLOW_CYMBAL = 5
    BLUE_CYMBAL = 6
    GREEN_CYMBAL = 7


# Windows virtual-key codes, read off the game's own Controller Remap screen
# (Options -> Controls, Player1 drums) on 2026-09-25:
#   Green=A  Red=S  Yellow=J  Blue=K  Orange/Kick=L  2X Kick=O
#   Yellow Cymbal=U  Blue Cymbal=Y  Green Cymbal=T
# play_chart.py hits notes with these. A different install may rebind them;
# set_binding() overrides one lane.
DEFAULT_BINDINGS: Dict[int, int] = {
    Lane.GREEN: 0x41,          # 'A'
    Lane.RED: 0x53,            # 'S'
    Lane.YELLOW: 0x4A,         # 'J'
    Lane.BLUE: 0x4B,           # 'K'
    Lane.KICK: 0x4C,           # 'L' (orange)
    Lane.YELLOW_CYMBAL: 0x55,  # 'U'
    Lane.BLUE_CYMBAL: 0x59,    # 'Y'
    Lane.GREEN_CYMBAL: 0x54,   # 'T'
}

# Short names for printed logs (moved from play_chart.py).
LANE_NAMES: Dict[int, str] = {
    Lane.GREEN: "Grn", Lane.RED: "Red", Lane.YELLOW: "Yel", Lane.BLUE: "Blu",
    Lane.KICK: "Kick", Lane.YELLOW_CYMBAL: "YCym", Lane.BLUE_CYMBAL: "BCym",
    Lane.GREEN_CYMBAL: "GCym",
}
```

Right after the `tap` method, add:

```python
    def press_chord(self, lanes: Iterable[int], *, hold_s: float = 0.003,
                    sleep: Callable[[float], None] = time.sleep) -> List[int]:
        """Press every lane's key down, hold `hold_s`, then release them all.

        This is the press play_chart.py proved at the game: a chord's keys go
        down together so the game sees one chord. A lane with no binding is
        skipped, not guessed. Returns the keys pressed.
        """
        vks: List[int] = []
        for lane in lanes:
            try:
                vk = self.get_binding(lane)
            except KeyError:
                continue
            vks.append(vk)
            self.send_key(vk, key_up=False)
        sleep(hold_s)
        for vk in vks:
            self.send_key(vk, key_up=True)
        return vks
```

`send_key` is today's `_send_key`, made public by the rename in Step 13. It stays the one place that calls SendInput.

- [ ] **Step 11: Chart code says "note".** In `tools/ch_probe/probe_chart.py`, replace:

```python
# Drum note numbers in the .chart format. 0 is the kick. 1-4 are the four
# colored pads (red, yellow, blue, green). We default to the kick because it is
# a single lane with no cymbal-vs-tom ambiguity, which keeps the probe clean.
DRUM_LANE_KICK = C.PROBE_LANE_KICK
```

with:

```python
# Drum note numbers in the .chart format. 0 is the kick. 1-4 are the four
# colored pads (red, yellow, blue, green). We default to the kick because it
# has no cymbal-vs-tom ambiguity, which keeps the probe clean. These are chart
# NOTE numbers; the key that plays note 0 is input_driver.Lane.KICK (4).
DRUM_NOTE_KICK = C.PROBE_CHART_NOTE_KICK
```

In `_expert_drums_section`, rename the parameter: change `def _expert_drums_section(note_ticks: Sequence[int], lane: int) -> str:` to `def _expert_drums_section(note_ticks: Sequence[int], note: int) -> str:`, change `"""The [ExpertDrums] section. One line per note: `<tick> = N <lane> 0`.` to `"""The [ExpertDrums] section. One line per note: `<tick> = N <note> 0`.`, and change `lines.append(f"  {tick} = N {lane} 0")` to `lines.append(f"  {tick} = N {note} 0")`. In `build_probe_chart_text`, change `lane: int = DRUM_LANE_KICK,` to `note: int = DRUM_NOTE_KICK,` and `+ _expert_drums_section(note_ticks, lane)` to `+ _expert_drums_section(note_ticks, note)`. In `generate_probe_chart`, change `lane: int = 0,` to `note: int = DRUM_NOTE_KICK,` and `spacings_ms, resolution=resolution, bpm=bpm, lane=lane` to `spacings_ms, resolution=resolution, bpm=bpm, note=note`.

In `tools/ch_probe/experiments/active_probe.py`, replace:

```python
from tools.ch_probe.input_driver import InputDriver  # noqa: E402
```

with:

```python
from tools.ch_probe.input_driver import InputDriver, Lane  # noqa: E402
```

and replace:

```python
# The drum lane the probe chart writes its notes on. Kept in one place so the
# input driver and the chart generator agree.
PROBE_LANE = constants.PROBE_LANE_KICK
```

with:

```python
# The .chart note the probe chart writes (the kick), and the input lane that
# presses it. They are different numbers: chart note 0 is the kick; input lane
# 4 is the kick key.
PROBE_NOTE = constants.PROBE_CHART_NOTE_KICK
PROBE_LANE = Lane.KICK
```

and change `generate_probe_chart(list(spacings_ms), chart_path, lane=PROBE_LANE)` to `generate_probe_chart(list(spacings_ms), chart_path, note=PROBE_NOTE)`. Task 21 rewrites the rest of this file.

- [ ] **Step 12: play_chart imports the shared pieces.** In `tools/ch_probe/experiments/play_chart.py`, replace:

```python
from tools.ch_probe import constants
from tools.ch_probe.process import open_process
from tools.ch_probe.input_driver import InputDriver
from tools.ch_probe.experiments.find_engine import scan_for_engine

OFF_SONG_CLOCK = 0x100  # double, seconds
OFF_SCORE = 0x94        # u32, game score (monotonically increasing)

```

with:

```python
from tools.ch_probe import engine_finder
from tools.ch_probe.engine import EngineModel
from tools.ch_probe.process import open_process
from tools.ch_probe.input_driver import LANE_NAMES, InputDriver, Lane

```

Delete the `LANE_NAMES` block:

```python
LANE_NAMES = {0: "Grn", 1: "Red", 2: "Yel", 3: "Blu", 4: "Kick",
              5: "YCym", 6: "BCym", 7: "GCym"}
```

Replace Task 19's `_CHART_CYMBAL_MARKER` table and `chart_notes_to_lanes` body with the same logic spelled in lane names:

```python
# gem note -> (tom lane, its cymbal-marker note, cymbal lane)
_CHART_CYMBAL_MARKER = {
    2: (Lane.YELLOW, 66, Lane.YELLOW_CYMBAL),
    3: (Lane.BLUE, 67, Lane.BLUE_CYMBAL),
    4: (Lane.GREEN, 68, Lane.GREEN_CYMBAL),
}


def chart_notes_to_lanes(chart_notes) -> list:
    """Turn the .chart note numbers at one tick into input lanes.

    One gem -> one lane, like midi_notes_to_lanes. The kick (0) and the 2x
    kick (32) both press the kick key. A marker with no gem presses nothing.
    """
    s = set(chart_notes)
    lanes = []
    if 0 in s or 32 in s:
        lanes.append(Lane.KICK)
    if 1 in s:
        lanes.append(Lane.RED)
    for gem_note, (tom_lane, marker, cym_lane) in _CHART_CYMBAL_MARKER.items():
        if gem_note in s:
            lanes.append(cym_lane if marker in s else tom_lane)
    if 5 in s:
        lanes.append(Lane.GREEN)   # 5-lane green: pressed as before (A)
    return sorted(set(lanes))
```

Replace:

```python
_CYMBAL_UPGRADE = {
    98: (2, 110, 5),   # Yellow: tom J / cymbal U
    99: (3, 111, 6),   # Blue:   tom K / cymbal Y
    100: (0, 112, 7),  # Green:  tom A / cymbal T
}
```

with:

```python
_CYMBAL_UPGRADE = {
    98: (Lane.YELLOW, 110, Lane.YELLOW_CYMBAL),
    99: (Lane.BLUE, 111, Lane.BLUE_CYMBAL),
    100: (Lane.GREEN, 112, Lane.GREEN_CYMBAL),
}
```

and in `midi_notes_to_lanes`, change `lanes.append(4)   # Kick (L). 95 = 2x-kick pedal, 96 = normal kick;` to `lanes.append(Lane.KICK)   # 95 = 2x-kick pedal, 96 = normal kick;`, and `lanes.append(1)   # Red (S)` to `lanes.append(Lane.RED)`.

Delete the whole `find_active_engine` function, from `def find_active_engine(proc):` through its final `time.sleep(0.4)`.

In `main`, replace:

```python
    print("  Waiting for active engine (start/unpause the song)...")
    engine_ptr = find_active_engine(proc)
    clock_now = proc.read_double(engine_ptr + OFF_SONG_CLOCK)
    print(f"  Engine at {engine_ptr:#x}, song clock = {clock_now:.2f}s")
```

with:

```python
    print("  Waiting for active engine (start/unpause the song)...")
    engine = EngineModel(proc)
    engine.use_object(engine_finder.find_live_engine(
        proc, [engine_finder.normal_pattern(proc)]))
    engine_ptr = engine.object_ptr
    clock_now = engine.song_clock()
    print(f"  Engine at {engine_ptr:#x}, song clock = {clock_now:.2f}s")
```

Replace:

```python
    def read_clock():
        return proc.read_double(engine_ptr + OFF_SONG_CLOCK)

    def read_score():
        return proc.read_u32(engine_ptr + OFF_SCORE)
```

with:

```python
    def read_clock():
        return engine.song_clock()

    def read_score():
        return engine.score()
```

Replace:

```python
            # Send input for this note's lanes — press all down, hold, release
            vks = []
            for lane in note.lanes:
                try:
                    vk = driver.get_binding(lane)
                    vks.append(vk)
                    driver._send_key(vk, key_up=False)
                except KeyError:
                    pass

            time.sleep(0.003)
            for vk in vks:
                driver._send_key(vk, key_up=True)
```

with:

```python
            # Send input for this note's lanes — press all down, hold, release
            driver.press_chord(note.lanes)
```

play_chart still searches only the normal-mode pair, exactly as before (`normal_pattern`).

- [ ] **Step 13: Rename the private key call everywhere.** Run from the repo root in PowerShell:

```powershell
Get-ChildItem tools\ch_probe -Recurse -Filter *.py | Where-Object FullName -notmatch '__pycache__' | ForEach-Object { $t = [IO.File]::ReadAllText($_.FullName); if ($t -match '_send_key') { [IO.File]::WriteAllText($_.FullName, ($t -replace '\b_send_key\b', 'send_key')) } }
```

Check: `Get-ChildItem tools\ch_probe -Recurse -Filter *.py | Select-String '_send_key'` prints nothing. This renames the definition in input_driver.py, and the calls in hit_detect.py, pad_flash_test.py, key_delivery_test.py and walk_edges.py (Step 14 then replaces walk_edges' pair).

- [ ] **Step 14: The hit-window scripts use the shared pieces.** Replace the whole of `tools/ch_probe/experiments/live.py` with:

```python
"""Shared live-game pieces for the hit-window experiments.

watch_window.py and walk_edges.py both read the same engine fields and load a
probe song's manifest. They share this file so the two scripts agree on how a
sample is read. Every offset comes from constants.py. Finding the engine is
engine_finder.find_live_engine with engine_finder.all_patterns, which also
finds an engine in precision mode.
"""

from __future__ import annotations

import json
import os
import struct
import sys
from dataclasses import dataclass

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants as C

# One read covers every watched field, so a sample is a consistent snapshot.
# The hit-time candidate at +0x2e0 is the furthest field out.
SNAPSHOT_SIZE = C.OFF_HIT_TIME + 8

PROBE_ROOT = r"C:\Clone Hero\songs\Hydra Probe"


@dataclass(frozen=True)
class Snapshot:
    window_ms: float
    clock_s: float
    score: int
    hit_time_s: float
    flags: int

    @property
    def precision(self) -> bool:
        return bool(self.flags & C.PRECISION_MODE_BIT)


def decode_snapshot(raw: bytes) -> Snapshot:
    """Turn SNAPSHOT_SIZE bytes read from the engine base into a Snapshot."""
    def dbl(off: int) -> float:
        return struct.unpack_from("<d", raw, off)[0]

    def u32(off: int) -> int:
        return struct.unpack_from("<I", raw, off)[0]

    return Snapshot(
        window_ms=dbl(C.OFF_TOTAL_WINDOW) * 1000.0,
        clock_s=dbl(C.OFF_SONG_CLOCK),
        score=u32(C.OFF_SCORE),
        hit_time_s=dbl(C.OFF_HIT_TIME),
        flags=u32(C.OFF_FLAGS),
    )


def read_snapshot(proc, engine: int) -> Snapshot:
    return decode_snapshot(proc.read(engine, SNAPSHOT_SIZE))


def load_manifest(song_dir: str) -> dict:
    with open(os.path.join(song_dir, "manifest.json"), encoding="utf-8") as f:
        return json.load(f)
```

In `tools/ch_probe/experiments/walk_edges.py`, replace:

```python
from tools.ch_probe.process import open_process
from tools.ch_probe.input_driver import InputDriver
from tools.ch_probe.experiments import live

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
KICK_LANE = 4          # InputDriver lane for the kick (L), as in play_chart.py
```

with:

```python
from tools.ch_probe import constants as C, engine_finder
from tools.ch_probe.process import open_process
from tools.ch_probe.input_driver import InputDriver, Lane
from tools.ch_probe.experiments import live

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
KICK_LANE = Lane.KICK
```

Change `engine = live.find_live_engine(proc)` to `engine = engine_finder.find_live_engine(proc, engine_finder.all_patterns(proc))`. Replace:

```python
    driver = InputDriver()
    vk = driver.get_binding(KICK_LANE)
```

with `    driver = InputDriver()`. Change `clock = SongClock(lambda: proc.read_double(engine + live.OFF_CLOCK))` to `clock = SongClock(lambda: proc.read_double(engine + C.OFF_SONG_CLOCK))`. Replace (the calls carry Step 13's new name):

```python
            driver.send_key(vk, key_up=False)
            time.sleep(0.003)
            driver.send_key(vk, key_up=True)
```

with:

```python
            driver.press_chord([KICK_LANE])
```

In `tools/ch_probe/experiments/watch_window.py`, replace:

```python
from tools.ch_probe.process import open_process
from tools.ch_probe.experiments import live
```

with:

```python
from tools.ch_probe import constants as C, engine_finder
from tools.ch_probe.process import open_process
from tools.ch_probe.experiments import live
```

and replace:

```python
    engine = live.find_live_engine(proc)
    snap = live.read_snapshot(proc, engine)
    back = proc.read_double(engine + 0x30) * 1000
    front = proc.read_double(engine + 0x38) * 1000
```

with:

```python
    engine = engine_finder.find_live_engine(proc, engine_finder.all_patterns(proc))
    snap = live.read_snapshot(proc, engine)
    back = proc.read_double(engine + C.OFF_BACK_WINDOW) * 1000
    front = proc.read_double(engine + C.OFF_FRONT_WINDOW) * 1000
```

- [ ] **Step 15: The kept diagnostics import the moved scan.** In `tools/ch_probe/experiments/find_engine.py`, delete the line `import ctypes`. Then cut the moved code (from `# VirtualQueryEx plumbing` through `return hits, regions_scanned, bytes_scanned`) and put an import in its place:

```powershell
python -c "p='tools/ch_probe/experiments/find_engine.py'; L=open(p,encoding='utf-8').read().split('\n'); s=L.index('# VirtualQueryEx plumbing'); e=L.index('    return hits, regions_scanned, bytes_scanned'); L[s:e+1]=['from tools.ch_probe.engine_finder import scan_for_engine  # moved there 2026-09']; open(p,'w',encoding='utf-8',newline='\n').write('\n'.join(L))"
```

Check: `python -c "import tools.ch_probe.experiments.find_engine as f, tools.ch_probe.engine_finder as e; print(f.scan_for_engine is e.scan_for_engine)"` prints `True`.

In `hit_detect.py`, `find_clock3.py` and `poll_windows.py`, change `from tools.ch_probe.experiments.find_engine import scan_for_engine` to `from tools.ch_probe.engine_finder import scan_for_engine`. In `poll_windows.py`, also delete the comment line above it, `# Import the scanner from find_engine`.

In `hit_detect.py`, delete the line `OFF_HIT_TIME = 0x100    # hit timestamp (double)`, and replace:

```python
    def read_hit_time():
        return proc.read_double(engine_ptr + OFF_HIT_TIME)
```

with:

```python
    def read_hit_time():
        # This script read +0x100 as a hit time; it is the song clock
        # (constants.OFF_SONG_CLOCK). Kept as it ran on 2026-09-25.
        return proc.read_double(engine_ptr + constants.OFF_SONG_CLOCK)
```

- [ ] **Step 16: The two key-test scripts read the one key table.** In `tools/ch_probe/experiments/key_delivery_test.py`, replace:

```python
from tools.ch_probe.input_driver import InputDriver

# The eight drum keys, in lane order, as (label, virtual-key code).
KEYS = [
    ("A (Green)", 0x41),
    ("S (Red)", 0x53),
    ("J (Yellow)", 0x4A),
    ("K (Blue)", 0x4B),
    ("L (Kick)", 0x4C),
    ("U (Y-Cym)", 0x55),
    ("Y (B-Cym)", 0x59),
    ("T (G-Cym)", 0x54),
]
```

with:

```python
from tools.ch_probe.input_driver import DEFAULT_BINDINGS, LANE_NAMES, InputDriver

# The eight drum keys, in lane order, as (label, virtual-key code), from the
# one key table in input_driver.py.
KEYS = [(f"{chr(vk)} ({LANE_NAMES[lane]})", vk) for lane, vk in DEFAULT_BINDINGS.items()]
```

In `tools/ch_probe/experiments/pad_flash_test.py`, replace:

```python
from tools.ch_probe.input_driver import InputDriver

# Drum keys in lane order: (label, virtual-key code, which pad to watch).
KEYS = [
    ("A", 0x41, "GREEN pad"),
    ("S", 0x53, "RED pad"),
    ("J", 0x4A, "YELLOW pad"),
    ("K", 0x4B, "BLUE pad"),
    ("L", 0x4C, "KICK bar (flash across the lane)"),
    ("U", 0x55, "YELLOW cymbal"),
    ("Y", 0x59, "BLUE cymbal"),
    ("T", 0x54, "GREEN cymbal"),
]
```

with:

```python
from tools.ch_probe.input_driver import DEFAULT_BINDINGS, InputDriver, Lane

# Drum keys in lane order: (label, virtual-key code, which pad to watch), from
# the one key table in input_driver.py.
KEYS = [(chr(DEFAULT_BINDINGS[lane]), DEFAULT_BINDINGS[lane], lane.name.replace("_", " "))
        for lane in Lane]
```

- [ ] **Step 17: Delete the dead code.** Delete the superseded experiments with `git rm tools/ch_probe/experiments/find_clock.py tools/ch_probe/experiments/find_clock2.py tools/ch_probe/experiments/reactive_inputs.py tools/ch_probe/experiments/send_inputs.py`. Before running it, check that nothing imports them: `Get-ChildItem tools\ch_probe -Recurse -Filter *.py | Select-String 'import.*(find_clock\b|find_clock2|reactive_inputs|send_inputs)'` must print nothing.

Cut `ocr.py` back to its parser. Only `read_accuracy_ms` called `_capture_crop` and `_ocr_bgra`, and nothing calls `read_accuracy_ms`:

```powershell
python -c "p='tools/ch_probe/ocr.py'; L=open(p,encoding='utf-8').read().split('\n'); i=L.index('def read_accuracy_ms(crop_region: Tuple[int, int, int, int]) -> Optional[float]:'); L=L[:i-1]; open(p,'w',encoding='utf-8',newline='\n').write('\n'.join(L))"
```

Then, in `ocr.py`'s docstring, replace:

```python
Two parts:

  * parse_accuracy_text -- pure text parsing. Given whatever text the OCR
    produced, pull out the signed millisecond number. No screen, no game; fully
    unit-tested below.
  * read_accuracy_ms -- the live path. Grab a screen rectangle, run Windows'
    built-in OCR on it, then hand the text to the parser. The screen grab and
    the OCR call can only run against a real display, so they are isolated
    behind small seams and cannot be tested without a screen.

OCR (optical character recognition) = turning a picture of text into a string.
We use the OCR engine built into Windows (Windows.Media.Ocr), reached through
the winrt package. That package is imported lazily inside the function, so just
importing this module never fails on a machine that lacks it.
"""

from __future__ import annotations

import ctypes
import re
from ctypes import wintypes
from typing import Optional, Tuple
```

with:

```python
Only the pure parser remains: parse_accuracy_text pulls the signed millisecond
number out of whatever text an OCR produced. The screen-capture half (the
function that ran Windows' built-in OCR on a screen crop) had no caller and
was deleted in 2026-09. Git history has it if the spec's OCR
cross-check (build-order step 4) is ever run.
"""

from __future__ import annotations

import re
from typing import Optional
```

Check: `python -c "import tools.ch_probe.ocr as o; print(hasattr(o, 'read_accuracy_ms'), o.parse_accuracy_text('Accuracy: -8 ms'))"` prints `False -8.0`.

In `tools/ch_probe/debugger.py`, delete:

```python
# Debug-register bits for a hardware data breakpoint (Dr7).
DR7_L0 = 0x1          # local enable for slot 0
DR7_RW0_WRITE = 0x1   # break on data write, in the slot-0 condition field
DR7_LEN0_8 = 0x2      # 8-byte length, in the slot-0 length field

```

and delete the whole `set_hw_data_breakpoint` method, from `    def set_hw_data_breakpoint(self, addr: int, size: int = 8) -> None:` through `            "use an int3 breakpoint instead")` and the blank line after it.

In `tools/ch_probe/interfaces.py`, replace:

```python
# WaitForDebugEvent engine. Sets int3 (0xCC) software breakpoints, catches
# them, exposes registers and memory, restores/single-steps to continue. Can
# also set a hardware data breakpoint via the debug registers.
```

with:

```python
# WaitForDebugEvent engine. Sets int3 (0xCC) software breakpoints, catches
# them, exposes registers and memory, restores/single-steps to continue.
```

Delete from the `Debugger` Protocol:

```python
    def set_hw_data_breakpoint(self, addr: int, size: int = 8) -> None:
        """Optional: watch a write to a data address (e.g. self+0x20) via the
        debug registers instead of sampling it."""
        ...

```

In the `EngineModel` Protocol, replace:

```python
    def capture_object(self) -> int:
        """Breakpoint the constructor, grab rcx, remember it. Returns the ptr."""
        ...

    def total_window(self) -> float:
        """self+0x20, the field the passive probe watches (ms)."""
        ...
```

with:

```python
    def capture_object(self) -> int:
        """Breakpoint the constructor, grab rcx, remember it. Returns the ptr."""
        ...

    def use_object(self, object_ptr: int) -> int:
        """Adopt a pointer found by engine_finder's memory scan."""
        ...

    def score(self) -> int:
        """self+0x94, rises only on a hit."""
        ...

    def total_window(self) -> float:
        """self+0x20, the field the passive probe watches (seconds)."""
        ...
```

and replace:

```python
    def song_clock(self) -> float:
        """Current song time (seconds). Source to be pinned live; may read a
        known clock field or a timer call."""
        ...
```

with:

```python
    def song_clock(self) -> float:
        """Current song time (seconds), self+0x100, proven live."""
        ...
```

In `generate_probe_chart`'s signature there, change `lane: int = 0,` to `note: int = 0,`. In the `InputDriver` Protocol, right after `tap`, add:

```python
    def send_key(self, vk: int, key_up: bool) -> None:
        """One key event through SendInput (the only OS call)."""
        ...

    def press_chord(self, lanes: Sequence[int], *, hold_s: float = 0.003) -> list:
        """All lanes' keys down, hold, all up."""
        ...
```

Replace the OCR section at the end:

```python
# --- ocr.py : the OCR cross-check -------------------------------------------
#
# Reads the on-screen "Accuracy: X ms" from a screen crop with WinRT OCR.
# Lowest-priority piece: it only validates that the memory delta matches the
# printed number. There is NO in-repo OCR code to reuse; build against the
# WinRT Windows.Media.Ocr API.

def read_accuracy_ms(crop_region: tuple[int, int, int, int]) -> Optional[float]:
    """Function in ocr.py. Screenshot the given (left, top, right, bottom)
    screen rectangle, OCR it, parse a signed millisecond number out of an
    "Accuracy: X ms" string. Returns None if nothing parseable is found."""
    ...
```

with:

```python
# --- ocr.py : the OCR cross-check -------------------------------------------
#
# Only the text parser remains (parse_accuracy_text). The screen-capture path
# had no caller and was deleted in 2026-09; git history has it.
```

- [ ] **Step 18: README and spec.** Replace the whole of `tools/ch_probe/README.md` with:

````markdown
# ch_probe: measuring Clone Hero's real drum hit window

## What this is

We took Clone Hero's drum hit window apart with Ghidra. Static analysis
answered everything except one question: does the window top out at 85 ms, or
does it rise to about 89.5 ms at moderate note spacings? The code that might
clamp the number is reached through a function pointer the decompiler can't
follow. So the only way to know is to watch the running game.

This tool does that, in Python, off to the side of Hydra's C++ build. The full
design and every reverse-engineering fact is in
[`docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md`](../../docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md).

## The one idea it rests on

Windows can't deliver a keystroke at a precise millisecond. So don't trust the
input's timing; trust what the engine recorded about it. Every test input
becomes a fact: "at a measured offset of X ms, this note was hit or missed."

## The route that works at the game today

`experiments/play_chart.py` auto-plays a chart and hits the notes (proven on
"Slipping", 2026-09-25). It needs no debugger. It:

1. opens the game and checks the two window constants (`process.py`);
2. finds the live engine object by memory scan (`engine_finder.py`): the
   engine keeps the window constants at +0x30/+0x38, and the live one is the
   only candidate whose song clock moves;
3. reads the song clock (+0x100) and the score (+0x94) through `EngineModel`
   (`engine.py`);
4. presses each chord with `InputDriver.press_chord` (`input_driver.py`),
   using the one key table, `Lane`.

A hit is a score that rose. `experiments/walk_edges.py` and
`experiments/watch_window.py` (the hit-window plan,
docs/superpowers/plans/2026-09-25-hit-window-testing.md) run on the same
pieces.

## The pieces

- `constants.py`: every address, offset and constant. The one place the
  numbers live.
- `interfaces.py`: the API contract each module meets.
- `process.py`: opens the game, turns Ghidra RVAs into live addresses, reads
  memory, and refuses to run if the constants don't match the build.
- `engine_finder.py`: finds the live engine object without a debugger.
- `engine.py`: the meaning layer. Named reads: window, clock, score, flags,
  constants. It takes an engine pointer from `engine_finder` (`use_object`)
  or catches the constructor with the debugger (`capture_object`).
- `input_driver.py`: the key table (`Lane`, `DEFAULT_BINDINGS`) and SendInput.
  "Lane" always means an input lane: 0 is green, 4 is the kick. A .chart
  numbers notes differently (note 0 is the kick), so chart code says "note".
- `debugger.py`: the Win32 debug loop with int3 breakpoints. Attaching turns
  Windows' kill-on-exit off, and `stop()` removes every breakpoint before
  detaching, so the game survives the tool going away.
- `probe_chart.py`, `probe_songs.py`: write probe charts and playable probe
  song folders.
- `ocr.py`: parses "Accuracy: X ms" text (the screen capture was removed).
- `experiments/`: the runners. `passive_probe.py` and `active_probe.py` use
  the debugger; the others don't.

## What runs here, and what needs the game

The unit tests in `tests/` cover the pure logic and every seam through fakes.
Anything that reads a live process needs Clone Hero running. Run the tests from
the repo root:

```bash
python -m pytest tools/ch_probe/tests -q
```

## Why bother

Hydra's model uses a flat 85 ms hit window. The point of this work is to
decide whether that should become the per-note curve the game really uses.
````

In `docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md`, replace:

```markdown
**Status:** design approved, not yet implemented
```

with:

```markdown
**Status:** built. The debugger-free route (`play_chart.py`) plays songs at the game since 2026-09-25. The passive and active probes and the hit-window scripts are built but not yet run at the game.
```

Replace:

```markdown
Everything below is cross-checked against the actual Ghidra dumps in
`scratch_ms/` (`ghidra_decompile_output.txt`, `ghidra_decompile_all.txt`). The
```

with:

```markdown
Everything below is cross-checked against the actual Ghidra dumps in
`C:\Users\Patrick\Downloads\Hydra\hydra-data\scratch_ms\` (moved out of the repo
on 2026-09-26; `ghidra_decompile_output.txt`, `ghidra_decompile_all.txt`). The
```

Replace:

```markdown
- `+0x100` — hit time: the engine's timestamp captured at the moment of a hit.
```

with:

```markdown
- `+0x100` — the song clock, in seconds. The Ghidra reading called it the hit
  time; the live game (play_chart.py, 2026-09-25) shows it is the running
  clock. The hit-time candidate is `+0x2e0` (`constants.OFF_HIT_TIME`, not yet
  confirmed live).
```

Replace:

```markdown
- Normal mode: back = `DAT_1831406c8`, front = `DAT_1831406e0`.
```

with:

```markdown
- Normal mode: back = `DAT_1831406e0` (0.085 s), front = `DAT_1831406c8`
  (0.0375 s). Corrected 2026-09-25 from a live read; this line first had them
  swapped.
```

Replace:

```markdown
milestone: if `DAT_1831406c8` reads 85.0 and `DAT_1831406e0` reads 37.5, your
address math is correct and you can trust everything downstream.
```

with:

```markdown
milestone: if `DAT_1831406e0` reads 0.085 and `DAT_1831406c8` reads 0.0375 (the
game stores seconds), your address math is correct and you can trust
everything downstream.
```

Replace:

```markdown
- Ghidra dumps: `scratch_ms/ghidra_decompile_output.txt` (constructor and
  note-processing, line refs above) and `scratch_ms/ghidra_decompile_all.txt`
```

with:

```markdown
- Ghidra dumps, in `C:\Users\Patrick\Downloads\Hydra\hydra-data\scratch_ms\`:
  `ghidra_decompile_output.txt` (constructor and note-processing, line refs
  above) and `ghidra_decompile_all.txt`
```

- [ ] **Step 19: Run everything and watch it pass.** Run `python -m pytest tools/ch_probe/tests -q`. Expected: `N passed`, no `failed`. Then run the five checks from the Acceptance Criteria. Each must print exactly what it says there.

- [ ] **Step 20: Commit.**

```bash
git add tools/ch_probe/engine_finder.py tools/ch_probe/constants.py tools/ch_probe/engine.py tools/ch_probe/process.py tools/ch_probe/input_driver.py tools/ch_probe/probe_chart.py tools/ch_probe/interfaces.py tools/ch_probe/debugger.py tools/ch_probe/ocr.py tools/ch_probe/README.md docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md tools/ch_probe/experiments/play_chart.py tools/ch_probe/experiments/live.py tools/ch_probe/experiments/walk_edges.py tools/ch_probe/experiments/watch_window.py tools/ch_probe/experiments/find_engine.py tools/ch_probe/experiments/hit_detect.py tools/ch_probe/experiments/find_clock3.py tools/ch_probe/experiments/poll_windows.py tools/ch_probe/experiments/pad_flash_test.py tools/ch_probe/experiments/key_delivery_test.py tools/ch_probe/experiments/active_probe.py tools/ch_probe/tests/test_engine_finder.py tools/ch_probe/tests/test_engine.py tools/ch_probe/tests/test_input_driver.py tools/ch_probe/tests/test_debugger.py tools/ch_probe/tests/test_analysis.py tools/ch_probe/tests/test_hit_window_scripts.py tools/ch_probe/tests/test_play_chart.py
git commit -m "ch_probe: move play_chart's working pieces into tested modules

One engine finder, one key table, one name per offset, one meaning of
lane; song_clock reads the proven +0x100; superseded experiments and dead
code removed.

Task: Task 20: ch_probe restructure
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

The four deleted experiment files are already staged by `git rm` in Step 17.

```json:metadata
{"files":["tools/ch_probe/engine_finder.py","tools/ch_probe/constants.py","tools/ch_probe/engine.py","tools/ch_probe/process.py","tools/ch_probe/input_driver.py","tools/ch_probe/probe_chart.py","tools/ch_probe/interfaces.py","tools/ch_probe/debugger.py","tools/ch_probe/ocr.py","tools/ch_probe/README.md","docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md","tools/ch_probe/experiments/play_chart.py","tools/ch_probe/experiments/live.py","tools/ch_probe/experiments/walk_edges.py","tools/ch_probe/experiments/watch_window.py","tools/ch_probe/experiments/find_engine.py","tools/ch_probe/experiments/hit_detect.py","tools/ch_probe/experiments/find_clock3.py","tools/ch_probe/experiments/poll_windows.py","tools/ch_probe/experiments/pad_flash_test.py","tools/ch_probe/experiments/key_delivery_test.py","tools/ch_probe/experiments/active_probe.py","tools/ch_probe/experiments/find_clock.py","tools/ch_probe/experiments/find_clock2.py","tools/ch_probe/experiments/reactive_inputs.py","tools/ch_probe/experiments/send_inputs.py","tools/ch_probe/tests/test_engine_finder.py","tools/ch_probe/tests/test_engine.py","tools/ch_probe/tests/test_input_driver.py","tools/ch_probe/tests/test_debugger.py","tools/ch_probe/tests/test_analysis.py","tools/ch_probe/tests/test_hit_window_scripts.py","tools/ch_probe/tests/test_play_chart.py"],"verifyCommand":"python -m pytest tools/ch_probe/tests -q","acceptanceCriteria":["whole ch_probe suite 0 failed incl. test_engine_finder and the new engine/input/shared-pieces cases","only constants.py defines OFF_* = 0x100 / 0x2E0; no 0x1A0","no _send_key, set_hw_data_breakpoint, read_accuracy_ms, placeholder clock, PROBE_LANE_KICK, DRUM_LANE_KICK, find_active_engine; find_live_engine defined only in engine_finder","scan_for_engine defined only in engine_finder","the four superseded experiments are gone","spec has no 'not yet implemented' and no scratch_ms/ path"],"modelTier":"complex"}
```

---

### Task 21: ch_probe runners

Both tracked probe runners crash on their first real step today. `passive_probe.py` and `active_probe.py` build a `Debugger()` and set breakpoints on it, but never call `attach`. `set_breakpoint` then reads game memory through a debugger with no process handle and fails. Decision 11 says fix them, not delete them. So this task makes them attach and run, on Task 20's modules.

Attaching is not enough on its own, so this is the order each runner now follows. It finds the live engine by memory scan while the song plays, the route play_chart proved. It attaches, and Task 19's attach turns kill-on-exit off. It plants its breakpoints and pumps debug events on the same thread, because Windows only hands debug events to the thread that attached. It always calls `stop()` in a `finally` block, even on Ctrl+C, so the breakpoints come out before it detaches.

Audit check: reading the two files turned up five more defects that would make a run crash, freeze the game or record nonsense. They are fixed here too.

The passive probe reads xmm0 at the formula's first instruction. xmm0 only holds the result when the formula returns, so every "raw" value it logs is left over from the previous call. The fix plants a second breakpoint at the formula's return address. At the first instruction that address sits at [rsp], the top of the stack. The stored value (+0x20) is read at the next formula call, after the caller has stored the previous result.

The active probe sets a breakpoint and then sends keys without ever pumping debug events. The first hit-check breakpoint would freeze the game for good. The fix runs the keys on a second thread while the main thread pumps.

Its key binding call always raises (`_read_lane_binding` looks for a `read_config_binding` that doesn't exist). The fix uses the bind-screen key table, `Lane.KICK`.

It aims every input at "now" (`target_time = clock()`) instead of at a note. The fix aims at the note times of a probe song it writes itself.

It calls a note a hit when `note_count() > 0` and records a timestamp as the delta. The fix uses the proven signals, which are walk_edges.py's rules. A hit is a score that rose. The measured offset is the +0x2e0 hit time minus the note time when that field changed, and otherwise the estimated send time.

The active probe's song is probe_chart's pairs layout: one pair per (spacing, offset). It is written at 480 ticks per beat and 125 BPM, where one tick is one millisecond. It lands as a playable folder, with a song.ini and a silent song.ogg from probe_songs, in `C:\Clone Hero\songs\Hydra Probe\Active Probe`. The first kick of each pair is pressed on time and the second is pressed late by the planned offset.

The hit-check breakpoint now counts how often the game ran its hit check during each input. That count is the evidence that the debugger route reaches the hit decision.

One scale question stays open until the probe runs. The game keeps times in seconds, so the rows are converted to milliseconds. +0x20 holds the whole window (twice the back window at construction), but nobody knows yet whether the formula returns one side or the whole. So the passive probe prints its first ten rows and a clamp verdict against both edges, labelled, rather than guessing.

What the user sees: nothing in Hydra. At the game, both runners now run end to end and leave Clone Hero running.

**Depends on:** Task 20 (`engine_finder`, `EngineModel.use_object`/`score`/`hit_time`, `Lane`, `press_chord`, `PROBE_CHART_NOTE_KICK`, probe_chart's `note` parameter) and Task 19 (kill-on-exit off, safe `stop()`).
**Expected overlaps:** none in wave 3. This task replaces the whole of `experiments/passive_probe.py` and `experiments/active_probe.py`. It adds `probe_note_ticks` to `probe_chart.py` and `rsp` to `interfaces.ThreadContext`, on top of Task 20's edits there.
**Goal:** passive_probe and active_probe attach, run and detach cleanly at the game, and record real data.

> **USER-ORDERED GATE — NON-SKIPPABLE.** This task was requested by the user in the current conversation. It MUST NOT be closed by walking around it, by declaring it "verified inline", or by substituting a cheaper check. Close only after every item in `acceptanceCriteria` has been re-validated independently, with output captured.

**Files:**
- Modify (full replacement): `tools/ch_probe/experiments/passive_probe.py`, `tools/ch_probe/experiments/active_probe.py`
- Modify: `tools/ch_probe/probe_chart.py` (new `probe_note_ticks`, used by `build_probe_chart_text`), `tools/ch_probe/interfaces.py` (`ThreadContext.rsp`)
- Create: `tools/ch_probe/tests/test_runners.py`
- Test: `tools/ch_probe/tests/test_probe_chart.py`

**Acceptance Criteria:**
- [ ] `python -m pytest tools/ch_probe/tests/test_runners.py -q` passes. With a fake debugger, both runners call `attach(pid)` before any `set_breakpoint`, the passive breakpoint is at `RVA_WINDOW_FORMULA` and the active one at `RVA_HIT_CHECK`, and `stop` is the last call, even when the debug loop raises KeyboardInterrupt.
- [ ] `test_runners.py::PassiveCollectorTest` passes. The raw value comes from xmm0 at the return site, the stored value is read at the next call, and the return breakpoint is planted once per call site.
- [ ] `test_probe_chart.py::test_probe_note_ticks_are_the_written_ticks` passes.
- [ ] `python -m pytest tools/ch_probe/tests -q` shows 0 failed.
- [ ] User gate (below) passes at the game.

**Verify:** `python -m pytest tools/ch_probe/tests -q` -> `N passed`, no `failed`.

**Steps:**

- [ ] **Step 1: Write the failing runner tests.** Create `tools/ch_probe/tests/test_runners.py`:

```python
"""The probe runners attach before any breakpoint and always detach.

No game: a fake process, a fake debugger that logs every call and refuses a
breakpoint before attach, and a fake engine finder.
"""

from __future__ import annotations

import contextlib
import io
import os
import struct
import sys
import tempfile
import time
import unittest
from unittest import mock

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants as C  # noqa: E402
from tools.ch_probe.debugger import ThreadContext  # noqa: E402
from tools.ch_probe.experiments import active_probe, passive_probe  # noqa: E402

BASE = 0x180000000
OBJ = 0x1234000


class FakeProcess:
    pid = 4242
    module_base = BASE

    def __init__(self, log):
        self.log = log

    def verify_targets(self):
        self.log.append("verify")

    def resolve(self, rva):
        return BASE + rva

    def read_double(self, addr):
        return 0.0

    def read_u32(self, addr):
        return 0

    def read_const_double(self, rva):
        return 0.0


class FakeDebugger:
    def __init__(self, log, raise_in_run=None):
        self.log = log
        self.attached = False
        self.raise_in_run = raise_in_run

    def attach(self, pid):
        self.attached = True
        self.log.append(("attach", pid))

    def set_breakpoint(self, addr, callback):
        if not self.attached:
            raise AssertionError("breakpoint set before attach")
        self.log.append(("set_breakpoint", addr))

    def run(self, until=None):
        self.log.append("run")
        if self.raise_in_run is not None:
            raise self.raise_in_run
        deadline = time.monotonic() + 2.0
        while until is not None and not until() and time.monotonic() < deadline:
            time.sleep(0.001)

    def stop(self):
        self.log.append("stop")


def _names(log):
    return [e[0] if isinstance(e, tuple) else e for e in log]


class PassiveRunnerTest(unittest.TestCase):
    def _run(self, log, dbg):
        with tempfile.TemporaryDirectory() as d, \
                mock.patch.object(passive_probe, "RESULTS_DIR", d), \
                contextlib.redirect_stdout(io.StringIO()):
            passive_probe.run_passive_probe(
                duration_s=0.0,
                open_proc=lambda name: FakeProcess(log),
                make_debugger=lambda: dbg,
                find_engine=lambda proc: OBJ)

    def test_attach_comes_before_any_breakpoint(self):
        log = []
        self._run(log, FakeDebugger(log))
        self.assertEqual(_names(log), ["verify", "attach", "set_breakpoint", "run", "stop"])
        self.assertEqual(log[1], ("attach", 4242))
        self.assertEqual(log[2], ("set_breakpoint", BASE + C.RVA_WINDOW_FORMULA))

    def test_detaches_when_the_loop_is_interrupted(self):
        log = []
        with self.assertRaises(KeyboardInterrupt):
            self._run(log, FakeDebugger(log, raise_in_run=KeyboardInterrupt()))
        self.assertEqual(log[-1], "stop")


class FakeEngine:
    def __init__(self, clocks, windows):
        self._clocks = list(clocks)
        self._windows = list(windows)

    def song_clock(self):
        return self._clocks.pop(0)

    def total_window(self):
        return self._windows.pop(0)


class FakeStackDebugger:
    """Answers the [rsp] read with a fixed return address."""

    RET = 0x7000

    def __init__(self):
        self.planted = []

    def read(self, addr, size):
        return struct.pack("<Q", self.RET)

    def set_breakpoint(self, addr, callback):
        self.planted.append(addr)


def _ctx(xmm0=0.0):
    return ThreadContext({"rsp": 0x9000}, struct.pack("<d", xmm0) + b"\0" * 8)


class PassiveCollectorTest(unittest.TestCase):
    def test_raw_at_return_stored_at_next_call(self):
        engine = FakeEngine(clocks=[10.0, 10.2, 10.4], windows=[0.170, 0.172])
        dbg = FakeStackDebugger()
        col = passive_probe.PassiveCollector(engine)
        col.on_formula_entry(dbg, _ctx())
        col.on_formula_return(dbg, _ctx(0.0895))
        col.on_formula_entry(dbg, _ctx())
        col.on_formula_return(dbg, _ctx(0.0850))
        col.on_formula_entry(dbg, _ctx())
        rows = col.rows
        self.assertEqual(len(rows), 2)
        for got, want in zip(rows, [(0.0, 89.5, 170.0), (200.0, 85.0, 172.0)]):
            for g, w in zip(got, want):
                self.assertAlmostEqual(g, w, places=6)
        self.assertEqual(dbg.planted, [FakeStackDebugger.RET])

    def test_a_result_with_no_next_call_is_not_logged(self):
        engine = FakeEngine(clocks=[10.0], windows=[])
        col = passive_probe.PassiveCollector(engine)
        col.on_formula_entry(FakeStackDebugger(), _ctx())
        col.on_formula_return(FakeStackDebugger(), _ctx(0.09))
        self.assertEqual(col.rows, [])


class ActiveRunnerTest(unittest.TestCase):
    def _run(self, log, dbg):
        def drive(engine, driver, collector, plan, stop, focus):
            log.append("drive")

        with tempfile.TemporaryDirectory() as d, \
                mock.patch.object(active_probe, "RESULTS_DIR", d), \
                contextlib.redirect_stdout(io.StringIO()):
            active_probe.run_active_probe(
                spacings_ms=[211], offsets_ms=[70],
                open_proc=lambda name: FakeProcess(log),
                make_debugger=lambda: dbg,
                find_engine=lambda proc: OBJ,
                write_song=lambda root, plan: "fake-song",
                make_driver=lambda: None,
                find_window=lambda: 0,
                drive=drive)

    def test_attach_comes_before_the_breakpoint_and_the_inputs(self):
        log = []
        self._run(log, FakeDebugger(log))
        names = _names(log)
        self.assertLess(names.index("attach"), names.index("set_breakpoint"))
        self.assertLess(names.index("set_breakpoint"), names.index("drive"))
        self.assertIn(("set_breakpoint", BASE + C.RVA_HIT_CHECK), log)
        self.assertEqual(names[-1], "stop")

    def test_detaches_when_the_loop_is_interrupted(self):
        log = []
        with self.assertRaises(KeyboardInterrupt):
            self._run(log, FakeDebugger(log, raise_in_run=KeyboardInterrupt()))
        # The input thread may still log "drive" after "stop", so check
        # presence and order rather than the last entry.
        names = _names(log)
        self.assertIn("stop", names)
        self.assertLess(names.index("run"), names.index("stop"))


class ActivePlanTest(unittest.TestCase):
    def test_one_pair_per_spacing_and_offset(self):
        plan = active_probe.plan_inputs([211, 30], [70, 100])
        self.assertEqual([(p.spacing_ms, p.offset_ms) for p in plan],
                         [(211, 70), (211, 100), (30, 70), (30, 100)])
        for p in plan:
            self.assertEqual(p.second_ms - p.first_ms, p.spacing_ms)
        self.assertEqual(plan[0].first_ms, 3840.0)   # 2 bars of lead-in, 1 tick = 1 ms

    def test_hit_checks_are_counted_only_during_an_input(self):
        col = active_probe.ActiveCollector()
        col.on_hit_check(None, None)
        col.current_index = 3
        col.on_hit_check(None, None)
        col.on_hit_check(None, None)
        self.assertEqual(col.hit_check_calls, {3: 2})


if __name__ == "__main__":
    unittest.main()
```

Add this to `tools/ch_probe/tests/test_probe_chart.py`, inside its main test class (after its last test method), and add `probe_note_ticks` to its `from tools.ch_probe.probe_chart import (...)` list:

```python
    def test_probe_note_ticks_are_the_written_ticks(self):
        # 480 ticks per beat at 125 BPM: one tick is one millisecond. Lead-in
        # is two 4-beat bars (3840), each pair is followed by a 4-bar pad (7680).
        ticks = probe_note_ticks([211, 30], resolution=480, bpm=125.0)
        self.assertEqual(ticks, [3840, 4051, 11731, 11761])
        text = build_probe_chart_text([211, 30], resolution=480, bpm=125.0)
        self.assertEqual(parse_drum_ticks(text), ticks)
```

- [ ] **Step 2: Run them and watch them fail.** Run `python -m pytest tools/ch_probe/tests/test_runners.py tools/ch_probe/tests/test_probe_chart.py -q`. Expected: `TypeError: run_passive_probe() got an unexpected keyword argument 'open_proc'` (and the same for `run_active_probe`). There is an AttributeError for `on_formula_entry`, `plan_inputs` and `ActiveCollector()` taking no engine, and an ImportError for `probe_note_ticks`.

- [ ] **Step 3: Split out the note ticks in probe_chart.** In `tools/ch_probe/probe_chart.py`, replace the body of `build_probe_chart_text` from `    pad_ticks = _PAD_WHOLE_NOTES * resolution * 4` through `        cursor = second + pad_ticks` with:

```python
    note_ticks = probe_note_ticks(spacings_ms, resolution=resolution, bpm=bpm)
```

and add, right above `build_probe_chart_text`:

```python
def probe_note_ticks(
    spacings_ms: Sequence[float], *, resolution: int = 192, bpm: float = 120.0
) -> list[int]:
    """The tick of every note the probe chart writes, in order: two per
    spacing, with a wide silent pad after each pair. The active probe reads
    its note times from here, so they always match the written chart."""
    pad_ticks = _PAD_WHOLE_NOTES * resolution * 4
    cursor = _LEAD_IN_WHOLE_NOTES * resolution * 4

    note_ticks: list[int] = []
    for ms in spacings_ms:
        gap = ms_to_ticks(ms, resolution, bpm)
        first = cursor
        second = cursor + gap
        note_ticks.append(first)
        note_ticks.append(second)
        # Next pair starts a full pad past this pair's second note.
        cursor = second + pad_ticks
    return note_ticks
```

- [ ] **Step 4: Expose rsp in the contract.** In `tools/ch_probe/interfaces.py`, in `class ThreadContext(Protocol)`, replace:

```python
    rip: int
    rcx: int
```

with:

```python
    rip: int
    rcx: int
    rsp: int   # at a function's first instruction, [rsp] is its return address
```

- [ ] **Step 5: Replace `tools/ch_probe/experiments/passive_probe.py`** with:

```python
"""Experiment 1: the passive probe. LIVE-ONLY orchestration.

What it does, in one line: watch a real song play, and for every note read the
raw window the formula computed next to the window the engine actually stored,
so we can see whether the stored value is clamped.

How it runs:

1. Wait for a song to be playing and find the live engine by memory scan
   (engine_finder, the route play_chart.py proved at the game).
2. Attach the debugger. Debugger.attach turns kill-on-exit off, so a crash
   here detaches instead of killing Clone Hero.
3. Breakpoint the formula's first instruction. There the return address sits
   at [rsp], so plant a second breakpoint on it (once per call site). When the
   formula returns, its result is in xmm0: that is the raw window.
4. At the formula's next call the caller has stored the previous result, so
   read +0x20 then: that is the stored window for the previous note.
5. Consecutive calls' song clocks give the spacing.
6. After the chosen time, detach (always, even on Ctrl+C), write the rows and
   print the clamp verdict.

The game keeps times in seconds; rows are in milliseconds. +0x20 holds the
whole window, but whether the formula returns one side or the whole is not
known until this runs. So the first rows are printed and the verdict is given
against both edges.

    python -m tools.ch_probe.experiments.passive_probe --seconds 20
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import struct
import sys
import time
from typing import Callable, List, Optional, Tuple

# Make `tools.ch_probe...` importable when this file is run directly, not just
# under `python -m`. experiments/ is three levels below the repo root.
_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants, engine_finder  # noqa: E402
from tools.ch_probe.experiments import analysis  # noqa: E402
from tools.ch_probe.process import open_process  # noqa: E402
from tools.ch_probe.debugger import Debugger  # noqa: E402
from tools.ch_probe.engine import EngineModel  # noqa: E402


# Where result files land. A sibling `results/` folder next to this script.
RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")

# One collected note, in ms: spacing since the previous call, the formula's
# raw result, and the +0x20 value the caller stored for it.
PassiveRow = Tuple[float, float, float]


def find_any_mode_engine(process) -> int:
    """The live engine in either scoring mode."""
    return engine_finder.find_live_engine(process, engine_finder.all_patterns(process))


class PassiveCollector:
    """Pairs each formula result with the window the caller stored for it.

    The callbacks run inside the debug loop. They only read memory and plant
    breakpoints, so the game is held for as short a time as possible.
    """

    def __init__(self, engine: EngineModel) -> None:
        self._engine = engine
        self._rows: List[PassiveRow] = []
        self._last_note_time: Optional[float] = None
        self._spacing_ms = 0.0
        self._pending: Optional[Tuple[float, float]] = None  # (spacing, raw) awaiting stored
        self.return_sites: set = set()

    @property
    def rows(self) -> List[PassiveRow]:
        return list(self._rows)

    def on_formula_entry(self, debugger, thread_context) -> None:
        """Breakpoint callback on the formula's first instruction. LIVE-ONLY seam.

        Three jobs. The previous call's result has been stored by now, so read
        +0x20 and finish that row. The return address is at [rsp]; plant a
        breakpoint there once per call site so the result can be read in xmm0.
        And note the song clock, so consecutive calls give a spacing.
        """
        self._finish_pending()
        ret = struct.unpack("<Q", debugger.read(thread_context.rsp, 8))[0]
        if ret not in self.return_sites:
            self.return_sites.add(ret)
            debugger.set_breakpoint(ret, self.on_formula_return)
        now = self._engine.song_clock()
        self._spacing_ms = (0.0 if self._last_note_time is None
                            else (now - self._last_note_time) * 1000.0)
        self._last_note_time = now

    def on_formula_return(self, debugger, thread_context) -> None:
        """Breakpoint callback where the formula returns. Its result (seconds)
        is in xmm0."""
        self._pending = (self._spacing_ms, thread_context.xmm0_double() * 1000.0)

    def _finish_pending(self) -> None:
        if self._pending is None:
            return
        spacing_ms, raw_ms = self._pending
        self._rows.append((spacing_ms, raw_ms, self._engine.total_window() * 1000.0))
        self._pending = None


def run_passive_probe(
    *,
    duration_s: float = 60.0,
    process_name: str = constants.PROCESS_NAME,
    out_stub: str = "passive",
    open_proc: Callable = open_process,
    make_debugger: Callable = Debugger,
    find_engine: Callable = find_any_mode_engine,
    now: Callable[[], float] = time.monotonic,
) -> List[analysis.ClampResult]:
    """Find the engine, attach, collect for `duration_s`, detach, report.

    LIVE-ONLY orchestration: start a chart with a wide spread of note spacings
    first. Rows go to results/<out_stub>.csv and .json. Returns the verdicts
    against one side's edge and against the whole window's.
    """
    process = open_proc(process_name)
    # Milestone 1: refuse to run if the address pipeline does not match the
    # build. This raises rather than reading garbage.
    process.verify_targets()

    engine = EngineModel(process)
    print("Waiting for a song to play (start or unpause it)...")
    engine.use_object(find_engine(process))
    print(f"  Engine at {engine.object_ptr:#x}")

    collector = PassiveCollector(engine)
    debugger = make_debugger()
    debugger.attach(process.pid)
    print("  Debugger attached (kill-on-exit off).")
    try:
        debugger.set_breakpoint(process.resolve(constants.RVA_WINDOW_FORMULA),
                                collector.on_formula_entry)
        deadline = now() + duration_s
        debugger.run(until=lambda: now() >= deadline)
    finally:
        debugger.stop()
        print("  Detached.")

    rows = collector.rows
    _write_rows(rows, out_stub)
    _print_first_rows(rows)

    # Judge against the edge of the mode actually being probed: precision
    # mode's back window is 40 ms, not normal mode's 85.
    back_ms = (constants.EXPECT_PRECISION_BACK_MS if engine.precision_mode()
               else constants.EXPECT_NORMAL_BACK_MS)
    verdicts = []
    for label, cap_ms in (("one side", back_ms), ("whole window", 2 * back_ms)):
        verdict = analysis.clamp_verdict(rows, cap_ms=cap_ms)
        _print_verdict(verdict, label)
        verdicts.append(verdict)
    return verdicts


def _write_rows(rows: List[PassiveRow], stub: str) -> Tuple[str, str]:
    """Write the collected rows to CSV and JSON. Pure file I/O, no game."""
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, f"{stub}.csv")
    json_path = os.path.join(RESULTS_DIR, f"{stub}.json")

    with open(csv_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["spacing_ms", "raw_formula_ms", "stored_window_ms"])
        for spacing, raw, stored in rows:
            writer.writerow([spacing, raw, stored])

    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(
            [
                {"spacing_ms": s, "raw_formula_ms": r, "stored_window_ms": w}
                for s, r, w in rows
            ],
            handle,
            indent=2,
        )
    return csv_path, json_path


def _print_first_rows(rows: List[PassiveRow], n: int = 10) -> None:
    """Show the scale: is raw one side of the window, or the whole of it?"""
    print(f"Collected {len(rows)} notes. First {min(n, len(rows))}, in ms "
          "(spacing, raw formula, stored +0x20):")
    for spacing, raw, stored in rows[:n]:
        print(f"  {spacing:8.1f}  {raw:9.3f}  {stored:9.3f}")


def _print_verdict(verdict: analysis.ClampResult, label: str) -> None:
    """Say the answer in plain English, for one reading of the scale."""
    head = f"Against the {label} edge ({verdict.cap_ms:.0f} ms): "
    if verdict.verdict == analysis.CLAMP_ABSENT:
        print(head + "no clamp. The stored window followed the raw formula past "
              f"the edge ({verdict.tracked_fraction:.0%} of {verdict.n_above} notes).")
    elif verdict.verdict == analysis.CLAMP_PRESENT:
        print(head + "clamp. The stored window stayed pinned at the edge while "
              f"the raw formula rose above it ({verdict.flat_fraction:.0%} of "
              f"{verdict.n_above} notes).")
    else:
        print(head + "inconclusive. No note pushed the raw window past this edge, "
              "or the evidence split.")


def main(argv: Optional[List[str]] = None) -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seconds", type=float, default=60.0, help="how long to watch")
    args = ap.parse_args(argv)
    run_passive_probe(duration_s=args.seconds)


if __name__ == "__main__":
    main()
```

- [ ] **Step 6: Replace `tools/ch_probe/experiments/active_probe.py`** with:

```python
"""Experiment 2: the active probe. LIVE-ONLY orchestration.

What it does, in one line: play a song of isolated kick pairs, press the second
kick of each pair late by a planned offset, and record whether the engine
counted it -- so the line between hits and misses shows the window the game
enforces at each spacing.

How it runs:

1. Write the probe song into Clone Hero's songs folder: probe_chart's pairs
   layout, one pair per (spacing, offset), at 480 ticks per beat and 125 BPM
   so one tick is one millisecond, with a silent song.ogg.
2. Wait for that song to be playing, and find the live engine by memory scan.
3. Attach the debugger (kill-on-exit off) and breakpoint the hit check. The
   debug loop runs on this thread, because Windows only delivers debug events
   to the thread that attached.
4. A second thread presses the keys: the first kick of each pair on time, the
   second at note + offset. Hit = the score rose (proven by play_chart.py).
   Measured offset = the +0x2e0 hit time minus the note when that field
   changed, else the estimated send time (walk_edges.py's rule).
5. Detach (always, even on Ctrl+C), write the rows, and summarise per spacing.

The hit-check breakpoint counts how often the game ran its hit check during
each input: evidence that the debugger reaches the hit decision.

    python -m tools.ch_probe.experiments.active_probe --spacings 211 --offsets 70,100
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import json
import os
import sys
import threading
import time
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional, Sequence, Tuple

# Make `tools.ch_probe...` importable when run directly. experiments/ is three
# levels below the repo root.
_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.ch_probe import constants, engine_finder, probe_chart, probe_songs  # noqa: E402
from tools.ch_probe.experiments import analysis  # noqa: E402
from tools.ch_probe.experiments.walk_edges import SongClock  # noqa: E402
from tools.ch_probe.process import open_process  # noqa: E402
from tools.ch_probe.debugger import Debugger  # noqa: E402
from tools.ch_probe.engine import EngineModel  # noqa: E402
from tools.ch_probe.input_driver import InputDriver, Lane  # noqa: E402


RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
SONG_NAME = "Active Probe"

# Dense near the 180-220 ms region where the clamp decision happens.
DEFAULT_SPACINGS_MS = constants.PROBE_SPACINGS_MS
# A sweep from clearly inside to clearly outside the ~85 ms edge.
DEFAULT_OFFSETS_MS = (70, 75, 80, 82, 84, 86, 88, 90, 95, 100)
SETTLE_MS = 250   # read the result this long after the note (as walk_edges.py)

# One collected input: its spacing, the measured offset (ms), and hit or miss.
ActiveRow = Tuple[float, float, bool]


@dataclass(frozen=True)
class PlannedInput:
    index: int
    spacing_ms: float
    offset_ms: float
    first_ms: float    # the pair's first kick, pressed on time
    second_ms: float   # the pair's second kick, pressed at second_ms + offset_ms


def plan_inputs(spacings_ms: Sequence[float],
                offsets_ms: Sequence[float]) -> List[PlannedInput]:
    """One note pair per (spacing, offset), in chart order. At probe_songs'
    480 ticks per beat and 125 BPM a tick is one millisecond, so the chart's
    note ticks are the note times in ms."""
    order = [(s, o) for s in spacings_ms for o in offsets_ms]
    ticks = probe_chart.probe_note_ticks(
        [s for s, _ in order], resolution=probe_songs.RESOLUTION, bpm=probe_songs.BPM)
    return [PlannedInput(i, float(s), float(o), float(ticks[2 * i]), float(ticks[2 * i + 1]))
            for i, (s, o) in enumerate(order)]


def write_probe_song(root: str, plan: Sequence[PlannedInput]) -> str:
    """Write the playable probe song folder: notes.chart, song.ini, song.ogg."""
    folder = os.path.join(root, SONG_NAME)
    os.makedirs(folder, exist_ok=True)
    text = probe_chart.build_probe_chart_text(
        [p.spacing_ms for p in plan], resolution=probe_songs.RESOLUTION,
        bpm=probe_songs.BPM, note=constants.PROBE_CHART_NOTE_KICK)
    length_ms = int(plan[-1].second_ms) + probe_songs.SILENCE_MS
    full_name = f"Hydra Probe - {SONG_NAME}"
    with open(os.path.join(folder, "notes.chart"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    with open(os.path.join(folder, "song.ini"), "w", encoding="utf-8", newline="\n") as f:
        f.write(probe_songs.song_ini(full_name, length_ms))
    probe_songs.write_silent_ogg(os.path.join(folder, "song.ogg"), length_ms)
    return folder


class ActiveCollector:
    """Rows from the input thread; hit-check counts from the debug loop."""

    def __init__(self) -> None:
        self._rows: List[ActiveRow] = []
        self.current_index: Optional[int] = None   # the input in flight
        self.hit_check_calls: Dict[int, int] = {}

    @property
    def rows(self) -> List[ActiveRow]:
        return list(self._rows)

    def add_row(self, row: ActiveRow) -> None:
        self._rows.append(row)

    def on_hit_check(self, debugger, thread_context) -> None:
        """Breakpoint callback at the hit check: count calls per input."""
        i = self.current_index
        if i is not None:
            self.hit_check_calls[i] = self.hit_check_calls.get(i, 0) + 1


def drive_inputs(engine: EngineModel, driver: InputDriver, collector: ActiveCollector,
                 plan: Sequence[PlannedInput], stop: threading.Event,
                 focus: Callable[[], None]) -> None:
    """Press the planned kicks against the song clock. LIVE-ONLY; runs on the
    input thread while the main thread pumps debug events."""
    clock = SongClock(engine.song_clock)
    raw_s, _ = clock.read()
    last_raw, last_move = raw_s, time.perf_counter()

    def wait_until(t_ms: float) -> float:
        """Poll until the clock estimate reaches t_ms; return it in ms."""
        nonlocal last_raw, last_move
        while True:
            if stop.is_set():
                raise RuntimeError("stopped")
            raw, est = clock.read()
            now = time.perf_counter()
            if raw < last_raw - 1.0:
                raise RuntimeError(f"clock jumped back ({last_raw:.2f} -> {raw:.2f} s)")
            if raw != last_raw:
                last_raw, last_move = raw, now
            elif now - last_move > 5.0:
                raise RuntimeError(f"clock frozen at {raw:.2f} s (song quit or paused)")
            ahead = t_ms / 1000 - est
            if ahead <= 0:
                return est * 1000
            if ahead > 0.04:
                time.sleep(min(ahead - 0.03, 0.5))

    todo = [p for p in plan if p.first_ms > raw_s * 1000 + 150]
    print(f"  {len(todo)}/{len(plan)} pairs still ahead of the clock.")
    print(f"  {'#':>4}  {'spacing':>7}  {'plan':>5}  {'measured':>8}  result")
    for p in todo:
        focus()
        wait_until(p.first_ms)
        driver.press_chord([Lane.KICK])                  # first kick, on time
        sent_ms = wait_until(p.second_ms + p.offset_ms)
        before_score, before_hit = engine.score(), engine.hit_time()
        collector.current_index = p.index
        driver.press_chord([Lane.KICK])                  # second kick, late
        wait_until(max(p.second_ms, p.second_ms + p.offset_ms) + SETTLE_MS)
        after_score, after_hit = engine.score(), engine.hit_time()
        collector.current_index = None
        hit = after_score > before_score
        if after_hit != before_hit:
            measured = after_hit * 1000 - p.second_ms
        else:
            measured = sent_ms - p.second_ms
        collector.add_row((p.spacing_ms, measured, hit))
        print(f"  {p.index + 1:4d}  {p.spacing_ms:7.0f}  {p.offset_ms:+5.0f}  "
              f"{measured:+8.1f}  {'HIT' if hit else 'miss'}")


def find_any_mode_engine(process) -> int:
    """The live engine in either scoring mode."""
    return engine_finder.find_live_engine(process, engine_finder.all_patterns(process))


def find_game_window() -> int:
    return ctypes.windll.user32.FindWindowW(None, "Clone Hero") or 0


def run_active_probe(
    *,
    spacings_ms: Sequence[float] = DEFAULT_SPACINGS_MS,
    offsets_ms: Sequence[float] = DEFAULT_OFFSETS_MS,
    process_name: str = constants.PROCESS_NAME,
    song_root: str = probe_songs.DEFAULT_OUT,
    out_stub: str = "active",
    open_proc: Callable = open_process,
    make_debugger: Callable = Debugger,
    find_engine: Callable = find_any_mode_engine,
    write_song: Callable = write_probe_song,
    make_driver: Callable = InputDriver,
    find_window: Callable[[], int] = find_game_window,
    drive: Callable = drive_inputs,
) -> List[analysis.SpacingEdge]:
    """Write the song, find the engine, attach, drive the inputs, detach,
    report. LIVE-ONLY orchestration. Rows go to results/<out_stub>.csv/.json."""
    plan = plan_inputs(spacings_ms, offsets_ms)
    folder = write_song(song_root, plan)
    print(f"Wrote {folder} ({len(plan)} pairs). Rescan songs in Clone Hero, "
          "then play it on Expert drums.")

    process = open_proc(process_name)
    process.verify_targets()  # milestone 1: refuse a build mismatch.
    engine = EngineModel(process)
    print("  Waiting for the song to play...")
    engine.use_object(find_engine(process))
    print(f"  Engine at {engine.object_ptr:#x}")

    collector = ActiveCollector()
    driver = make_driver()
    hwnd = find_window()

    def focus() -> None:
        if hwnd:
            ctypes.windll.user32.SetForegroundWindow(hwnd)

    stop = threading.Event()
    done = threading.Event()
    failures: List[BaseException] = []

    def worker() -> None:
        try:
            drive(engine, driver, collector, plan, stop, focus)
        except BaseException as e:  # reported below, after detaching
            failures.append(e)
        finally:
            done.set()

    debugger = make_debugger()
    debugger.attach(process.pid)
    print("  Debugger attached (kill-on-exit off).")
    try:
        debugger.set_breakpoint(process.resolve(constants.RVA_HIT_CHECK),
                                collector.on_hit_check)
        threading.Thread(target=worker, name="active-probe-input", daemon=True).start()
        debugger.run(until=done.is_set)
    finally:
        stop.set()
        debugger.stop()
        print("  Detached.")

    if failures and str(failures[0]) != "stopped":
        print(f"  Input thread stopped early: {failures[0]}")
    rows = collector.rows
    _write_rows(rows, out_stub)
    calls = sum(collector.hit_check_calls.values())
    print(f"  Hit-check breakpoint fired {calls} times over "
          f"{len(collector.hit_check_calls)} of {len(rows)} inputs.")

    formula_constants = analysis.normal_formula_constants(engine.constants())
    summary = analysis.summarize_active(rows, formula_constants=formula_constants)
    _print_summary(summary)
    return summary


def _write_rows(rows: List[ActiveRow], stub: str) -> Tuple[str, str]:
    """Write collected rows to CSV and JSON. Pure file I/O, no game."""
    os.makedirs(RESULTS_DIR, exist_ok=True)
    csv_path = os.path.join(RESULTS_DIR, f"{stub}.csv")
    json_path = os.path.join(RESULTS_DIR, f"{stub}.json")

    with open(csv_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["spacing_ms", "measured_delta_ms", "hit"])
        for spacing, delta, hit in rows:
            writer.writerow([spacing, delta, int(hit)])

    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(
            [
                {"spacing_ms": s, "measured_delta_ms": d, "hit": bool(h)}
                for s, d, h in rows
            ],
            handle,
            indent=2,
        )
    return csv_path, json_path


def _print_summary(summary: List[analysis.SpacingEdge]) -> None:
    """Say the per-spacing result in plain English."""
    if not summary:
        print("No inputs were collected. Was the probe song playing?")
        return
    print("spacing(ms)  measured_edge(ms)  predicted_edge(ms)  errors")
    for row in summary:
        measured = "n/a" if row.measured_edge_ms is None else f"{row.measured_edge_ms:8.2f}"
        predicted = "n/a" if row.predicted_edge_ms is None else f"{row.predicted_edge_ms:8.2f}"
        print(f"{row.spacing_ms:10.1f}  {measured:>16}  {predicted:>17}  {row.errors:6d}")


def _ms_list(text: str) -> List[float]:
    return [float(x) for x in text.split(",") if x.strip()]


def main(argv: Optional[List[str]] = None) -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--spacings", type=_ms_list,
                    default=list(DEFAULT_SPACINGS_MS), help="e.g. 211,300")
    ap.add_argument("--offsets", type=_ms_list,
                    default=list(DEFAULT_OFFSETS_MS), help="late ms, e.g. 70,100")
    ap.add_argument("--song-root", default=probe_songs.DEFAULT_OUT)
    args = ap.parse_args(argv)
    run_active_probe(spacings_ms=args.spacings, offsets_ms=args.offsets,
                     song_root=args.song_root)


if __name__ == "__main__":
    main()
```

- [ ] **Step 7: Run them and watch them pass.** Run `python -m pytest tools/ch_probe/tests -q`. Expected: `N passed`, no `failed`.

- [ ] **Step 8: Commit.**

```bash
git add tools/ch_probe/experiments/passive_probe.py tools/ch_probe/experiments/active_probe.py tools/ch_probe/probe_chart.py tools/ch_probe/interfaces.py tools/ch_probe/tests/test_runners.py tools/ch_probe/tests/test_probe_chart.py
git commit -m "ch_probe: the passive and active probes attach, run and detach cleanly

Task: Task 21: ch_probe runners
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 9: User gate at the game.** The executor stops here and hands the user these steps. The gate checks that the tools run and leave Clone Hero alive. The hit and miss numbers are the experiment's own results, and they do not decide this gate.

Before you start: Clone Hero v1.1.0.6142 is running in normal scoring mode (not precision). Open PowerShell at the repo root as the same Windows user that runs the game.

Passive probe, normal run:
1. If "Hydra Probe - Window Map" is not in your library yet, run `python -m tools.ch_probe.probe_songs` and rescan songs in Clone Hero.
2. Start "Hydra Probe - Window Map" on Expert drums and let it play.
3. Run `python -m tools.ch_probe.experiments.passive_probe --seconds 20`.
4. Pass means all of these: it prints `Engine at 0x…`, then `Debugger attached (kill-on-exit off).`, and about 20 seconds later `Detached.`. Then comes `Collected N notes.` with N of at least 5, a table whose raw column is not all zeros, and two `Against the … edge` lines. `tools\ch_probe\experiments\results\passive.csv` exists. And Clone Hero kept playing the whole time and is still running afterwards. A short stutter while attached is fine; a freeze or crash is a fail.

Passive probe, Ctrl+C:
5. Restart the song. Run `python -m tools.ch_probe.experiments.passive_probe --seconds 120`, and press Ctrl+C about 5 seconds after `Debugger attached` appears.
6. Pass: `Detached.` is printed before the KeyboardInterrupt traceback, and Clone Hero keeps playing.

Active probe:
7. Run `python -m tools.ch_probe.experiments.active_probe --spacings 211 --offsets 70,100`. It prints `Wrote C:\Clone Hero\songs\Hydra Probe\Active Probe (2 pairs)`, then waits, printing dots.
8. In Clone Hero, rescan songs, start "Hydra Probe - Active Probe" on Expert drums, and click back into the game window. Don't touch the keyboard; the song is about 15 seconds long.
9. Pass means all of these: it prints `Debugger attached (kill-on-exit off).`, two input lines (`1  211  +70 …` and `2  211  +100 …`, each ending HIT or miss), and `Detached.`. `Hit-check breakpoint fired N times` shows N greater than 0. `tools\ch_probe\experiments\results\active.csv` has 2 data rows. And Clone Hero is still running afterwards.

If a step fails, paste the whole terminal output back. If the game froze, also say which step it froze at.

- [ ] **Step 10: Ask about the three older diagnostics.** Task 20 kept `hit_detect.py`, `find_clock3.py` and `poll_windows.py`, because their replacements (`watch_window.py`, which is poll_windows plus the song clock, and `walk_edges.py`, which measures hits the way hit_detect tried to) had not run at the game when this plan was written. After the gate above, ask the user one question: have `watch_window.py` and `walk_edges.py` now worked at the game? On a yes, delete the three older scripts with `git rm tools/ch_probe/experiments/hit_detect.py tools/ch_probe/experiments/find_clock3.py tools/ch_probe/experiments/poll_windows.py`, run `python -m pytest tools/ch_probe/tests -q` (0 failed), and commit with the four trailer lines. On a no, keep them, and put one line in the task report naming what still has to run at the game first.

```json:metadata
{"files":["tools/ch_probe/experiments/passive_probe.py","tools/ch_probe/experiments/active_probe.py","tools/ch_probe/probe_chart.py","tools/ch_probe/interfaces.py","tools/ch_probe/tests/test_runners.py","tools/ch_probe/tests/test_probe_chart.py"],"verifyCommand":"python -m pytest tools/ch_probe/tests -q","acceptanceCriteria":["test_runners.py passes: attach before any breakpoint, breakpoints at RVA_WINDOW_FORMULA / RVA_HIT_CHECK, stop last even on KeyboardInterrupt","PassiveCollectorTest passes: raw from xmm0 at the return site, stored read at the next call, return breakpoint planted once","test_probe_note_ticks_are_the_written_ticks passes","whole ch_probe suite 0 failed","user gate at the game passes"],"modelTier":"standard","userGate":true}
```

---

### Task 22: Final check of the merged whole (main session)

Each wave was already tested after its merge. This task checks the finished result once more as a whole, and re-runs every speed number on a quiet machine, because the fleet's own timings were taken while other builds were running. It also builds the installer and proves the shipped exe no longer carries the GUI tests or your folder paths.

What the user sees: a short report in chat with the before and after numbers, and a release note for the record-format change.

**Depends on:** every other task merged. **Expected overlaps:** none.

**Goal:** Every test passes on the merged `hydra-test`, the scores are unchanged, each speed task's gain is confirmed on a quiet machine, and the installer exe is clean.

**Files:**
- Create: `docs/handoffs/2026-09-26-audit-fixes-release-note.md`
- Modify: none

**Acceptance Criteria:**
- [ ] `hydra_tests.exe` ends with `Status: SUCCESS!`, and its test-case count is at least the Task 0 count minus the tests the plan deleted on purpose (each deletion is named in its task).
- [ ] `hydra_uitest.exe --all` passes every test, including the new ones from T6, T7 and T18.
- [ ] `python -m pytest tools/ch_probe/tests -q` passes.
- [ ] The batch diff against `$env:TEMP\hydra_audit_base\batch_sorted.txt` prints nothing.
- [ ] `hydra_replay selfcheck` over testdata\input reports no mismatch.
- [ ] Batch time, reindex time, `hydra_bench` and test-suite time are each re-measured on a quiet machine and written next to the Task 0 numbers.
- [ ] The installer build's `Hydra.exe` contains none of the strings `hydra-test\testdata`, `hydra-test\assets`, `hydra-test\resource` or `uitest`.
- [ ] `git worktree list` shows only the main checkout, and no `audit/T*` branch is left.

**Verify:** `.\build-cpp\Release\hydra_tests.exe; .\build-cpp\Release\hydra_uitest.exe --all` → `Status: SUCCESS!` and every uitest `[PASS]`.

**Steps:**

- [ ] **Step 1: Clean build of everything.**

```powershell
Remove-Item -Recurse -Force .\build-cpp
.\build_cpp.ps1
.\build_cpp.ps1 -Target hydra_uitest
```

After T14, `build_cpp.ps1` also builds hydra_bench and hydra_replay. Check they exist: `Test-Path .\build-cpp\Release\hydra_bench.exe, .\build-cpp\Release\hydra_replay.exe` prints True twice.

- [ ] **Step 2: All tests.**

```powershell
.\build-cpp\Release\hydra_tests.exe | Tee-Object "$env:TEMP\hydra_audit_base\final_tests.txt"
.\build-cpp\Release\hydra_uitest.exe --all | Tee-Object "$env:TEMP\hydra_audit_base\final_uitest.txt"
python -m pytest tools/ch_probe/tests -q
```

- [ ] **Step 3: Scores unchanged.** Run the batch-diff recipe from the Global Constraints with `$out = "$env:TEMP\hydra_final"`. Then run `.\build-cpp\Release\hydra_replay.exe selfcheck --db "$env:TEMP\hydra_final\after.db"` (check its usage line first) and confirm it reports no mismatch.

- [ ] **Step 4: Speed on a quiet machine.** Close every other build and test. Wait until `tasklist` shows no `cl.exe`, `link.exe`, `MSBuild.exe` or `hydra_*` for 60 seconds. Then run the same four timings as Task 0 step 8 (batch, reindex, hydra_bench, hydra_tests), each twice, and keep the faster run. Write a table with the Task 0 number, the new number and the change. If any is slower than Task 0 by more than 3%, find the task responsible from its own report and tell the user before going on.

- [ ] **Step 5: The installer exe is clean.**

```powershell
.\installer\build_installer.ps1
$exe = Get-ChildItem .\build-cpp -Recurse -Filter Hydra.exe | Where-Object FullName -match 'installer|stage' | Select-Object -First 1
foreach ($s in 'hydra-test\testdata','hydra-test\assets','hydra-test\resource','uitest') {
  if (Select-String -LiteralPath $exe.FullName -Pattern ([regex]::Escape($s)) -SimpleMatch -Quiet) { "FOUND $s" }
}
```

It must print nothing. T14 names the exact staging path; use it if the pattern above finds a different exe.

- [ ] **Step 6: Release note.** Write docs/handoffs/2026-09-26-audit-fixes-release-note.md in plain English. It says that every record reads Stale once and needs re-analysis (the format change from T12). It lists the visible changes from "What you'll see when it's done". It says that a 1.5 or 1.6 database now reads "Not analyzed", and that the database now uses WAL, so a `hydra.db-wal` and a `hydra.db-shm` file sit next to it while Hydra runs.

- [ ] **Step 7: Tidy up.** `git worktree list`; remove any leftover `wt-T*` with `git worktree remove`, and delete merged `audit/T*` branches with `git branch -d`. Never use `-D` on a branch that is not merged; ask the user instead.

- [ ] **Step 8: Commit and report.**

```powershell
git add docs/handoffs/2026-09-26-audit-fixes-release-note.md
git commit -m "Release note for the audit fixes

Task: Task 22 - final check
Agent: main session
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Report to the user in chat: the test counts, the batch diff result, the speed table, and anything a task reported and did not fix.

---

## Appendix: the wave workflow script

The main session runs this once per wave with `args` set to the wave. For wave 1: `{"base": "<Task 0 commit>", "tasks": [1,2,3,4,5,6,7,8,14,15,18,19]}`. For wave 2: `{"base": "<merged head>", "tasks": [9,10,11,12,20]}`. For wave 3: `{"base": "<merged head>", "tasks": [13,16,17,21]}`. The script returns one line per task: merged-ready, or needs the main session, with the reviewer's last findings.

Each implementer makes its own worktree with plain `git worktree add`, not the Workflow tool's automatic worktree. That way the branch has a known name the main session can merge. The small gate at the top of the script caps how many implementers build at once.

```js
export const meta = {
  name: 'audit-fix-wave',
  description: 'One wave of the 2026-09-26 audit-fix plan: implement each task in its own worktree, review, follow up on failure',
  phases: [{ title: 'Implement' }, { title: 'Review' }, { title: 'Follow up' }],
}

const PLAN = 'C:\\Users\\Patrick\\Downloads\\Hydra\\hydra-test\\docs\\superpowers\\plans\\2026-09-26-codebase-audit-fixes.md'
const WT = n => `C:\\Users\\Patrick\\Downloads\\Hydra\\wt-T${n}`
const STATUS = 'Append one line to C:\\Users\\Patrick\\.claude\\hooks\\state\\status\\<your agent id>.md every 10 tool calls or 5 minutes, form `HH:MM done ... | next: ...` (helper: & "C:/Users/Patrick/.claude/hooks/status_append.ps1" <agent id> "<line>").'
const RULES = 'Work only inside your worktree. Edit only the files your task lists, plus test files it creates; never delete, move or rewrite anything else. Never hand off to a background job; poll in the foreground and finish in this turn. Never amend, rebase or reset. Source edits via the Edit tool. If your session has no Agent tool, do delegated work yourself and say so.'

// At most six implementers build at once: twelve full C++ builds at the same time starve each other.
let free = 6; const waiting = []
const take = () => free > 0 ? (free--, Promise.resolve()) : new Promise(r => waiting.push(r))
const give = () => { const next = waiting.shift(); if (next) next(); else free++ }

const REVIEW = {
  type: 'object',
  properties: {
    pass: { type: 'boolean' },
    scope_ok: { type: 'boolean' },
    findings: { type: 'array', items: { type: 'string' } },
  },
  required: ['pass', 'scope_ok', 'findings'],
}

const implement = n => `Task ${n} of 22: implement "Task ${n}" from the plan at ${PLAN}.
First make your worktree: git -C C:\\Users\\Patrick\\Downloads\\Hydra\\hydra-test worktree add ${WT(n)} -b audit/T${n} ${args.base}
Then cd into ${WT(n)}. Read the plan's Global Constraints and your task section, and do every step in order, test first. Commit on audit/T${n} with the four trailer lines. Speed measurements wait for a quiet machine as the Global Constraints say.
${RULES}
Delegation: send mechanical work (running builds and tests, bulk renames) to a sonnet or haiku worker if you have the Agent tool; do the judgment yourself.
${STATUS}
Reply with: the commit hashes, each acceptance check with its observed output, and anything you could not do.`

const review = (n, report) => `Task ${n} of 22: review audit/T${n} in ${WT(n)} against "Task ${n}" in ${PLAN}.
Scope gate first: run git -C ${WT(n)} diff --stat ${args.base}..audit/T${n}. Any file outside the task's Files list (test files it creates excepted) sets scope_ok=false and pass=false.
Then re-run the task's Verify command and every acceptance check yourself in the worktree, and read the diff against the task text and the plan's user decisions. Do not trust the implementer's report; it follows for reference only.
Read-only: never edit, commit or build outside ${WT(n)}.
${STATUS}
Implementer report:
${report}`

const followup = (n, findings) => `Task ${n} of 22: follow-up on audit/T${n} in ${WT(n)}. The reviewer found these problems with "Task ${n}" from ${PLAN}:
${findings.map(f => '- ' + f).join('\n')}
Fix them in the worktree, re-run the task's Verify command, and commit with the four trailer lines.
${RULES}
${STATUS}
Reply with the new commit hashes and the Verify output.`

const results = await pipeline(
  args.tasks,
  async n => {
    await take()
    try { return await agent(implement(n), { label: `T${n} implement`, phase: 'Implement', model: 'opus' }) }
    finally { give() }
  },
  async (report, n) => {
    let r = await agent(review(n, report), { label: `T${n} review`, phase: 'Review', model: 'sonnet', schema: REVIEW })
    // serial because: each follow-up edits the same worktree the next review reads.
    for (let round = 1; r && !r.pass && round <= 2; round++) {
      const fix = await agent(followup(n, r.findings), { label: `T${n} followup ${round}`, phase: 'Follow up', model: 'opus' })
      r = await agent(review(n, fix), { label: `T${n} review ${round + 1}`, phase: 'Review', model: 'sonnet', schema: REVIEW })
    }
    return { task: n, ready: !!(r && r.pass), findings: r ? r.findings : ['review agent failed'] }
  },
)

for (const r of results.filter(Boolean)) log(`T${r.task}: ${r.ready ? 'ready to merge' : 'needs the main session'}`)
return results
```

The script above is a template: at execution, the main session checks it against the Workflow tool's current rules before launching. It never merges; the main session does that after the run, as "Running it as a workflow" describes.
