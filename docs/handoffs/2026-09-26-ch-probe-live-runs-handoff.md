# Handoff: running the ch_probe scripts at the game

These scripts have only been tested against fakes. This handoff covers running them against the real game. There are two jobs.

The first job is the last gate of Task 21 in the audit-fixes plan (docs/superpowers/plans/2026-09-26-codebase-audit-fixes.md). It checks that `passive_probe` and `active_probe` attach to Clone Hero, run, detach, and leave the game running. The gate is about the tools surviving. The hit and miss numbers they print are the experiment's results, and they don't decide whether the gate passes.

The second job is the hit-window runs with `watch_window` and `walk_edges` (docs/superpowers/plans/2026-09-25-hit-window-testing.md). Once those two have worked at the game, three older scripts they replace can be deleted.

Neither job touches Hydra. Nothing here changes the app, its database or its scores.

## Before you start

Clone Hero v1.1.0.6142 must be running, in normal scoring mode, not precision. Every script checks the game build first. On any other build it stops with an error before reading anything else.

Open PowerShell at the repo root (`C:\Users\Patrick\Downloads\Hydra\hydra-test`). Run it as the same Windows user that runs the game. The debugger can't attach to a game started by another user.

ffmpeg must be on PATH, because the probe songs need a silent song.ogg. Yours is at `C:\ytdl\ffmpeg`.

Install the two probe songs once:

```powershell
python -m tools.ch_probe.probe_songs
```

This writes "Hydra Probe - Window Map" and "Hydra Probe - Edge Walk" into `C:\Clone Hero\songs\Hydra Probe`. Rescan songs in Clone Hero afterwards so they show up. Window Map needs no key presses: it only shows how the window moves with note spacing. Edge Walk is 120 kicks one second apart, for the scripts that press keys.

All result files go to `tools\ch_probe\experiments\results\`. Git doesn't ignore that folder, so the files show up as untracked. Leave them there for me to read, and don't commit them. `passive.csv` and `active.csv` are overwritten on every run. The watch_window and walk_edges files get a timestamp in their names.

Every script that finds the game prints `Waiting for the song to play` and then a dot about every half second until a song is actually playing. It finds the game by watching for a song clock that moves. A paused song or the song menu just keeps the dots coming. That is normal, not a hang.

## Part 1: passive probe (Task 21 gate)

This one attaches the debugger and reads the window the game computes for each note. It presses no keys.

Normal run:

1. Start "Hydra Probe - Window Map" on Expert drums and let it play.
2. Run:

   ```powershell
   python -m tools.ch_probe.experiments.passive_probe --seconds 20
   ```

3. It passes when all of these happen:
   - It prints `Engine at 0x…`, then `Debugger attached (kill-on-exit off).`, then `Detached.` about 20 seconds later.
   - It prints `Collected N notes.` with N of 5 or more, and a table whose middle column (the raw formula value) is not all zeros.
   - It prints two lines that start `Against the one side edge` and `Against the whole window edge`.
   - `tools\ch_probe\experiments\results\passive.csv` exists.
   - Clone Hero kept playing the whole time and is still running afterwards. A short stutter while the debugger is attached is fine. A freeze or a crash is a fail.

Ctrl+C run. This checks that the script cleans up when it is interrupted:

4. Restart the song.
5. Run the same command with `--seconds 120`. About 5 seconds after `Debugger attached` appears, press Ctrl+C in PowerShell.
6. It passes when `Detached.` prints before the KeyboardInterrupt traceback and Clone Hero keeps playing.

## Part 2: active probe (Task 21 gate)

This one writes its own short song and then presses the kick key for you. For each note pair it presses the first kick on time and the second kick late by a set amount. Then it records whether the game counted it.

The order matters here: start the script first, then the song. The first note comes about 3.8 seconds into the song. The script skips any pair it can't reach in time. If you start the song first, the script may find the game too late and skip the first pair.

1. Run:

   ```powershell
   python -m tools.ch_probe.experiments.active_probe --spacings 211 --offsets 70,100
   ```

   It prints `Wrote C:\Clone Hero\songs\Hydra Probe\Active Probe (2 pairs). Rescan songs in Clone Hero, then play it on Expert drums.` Then it waits, printing dots.
2. In Clone Hero, rescan songs and start "Hydra Probe - Active Probe" on Expert drums. Click back into the game window. After that, keep your hands off the keyboard. The script brings the game window to the front before each pair, and the song is only about 15 seconds long.
3. It passes when all of these happen:
   - It prints `Debugger attached (kill-on-exit off).` and `2/2 pairs still ahead of the clock.`
   - It prints two input rows, one for `211  +70` and one for `211 +100`, each ending in HIT or miss. Whether each one hits doesn't matter for the gate.
   - It prints `Detached.`, then `Hit-check breakpoint fired N times` with N greater than 0. That count shows the debugger really reaches the game's hit decision.
   - `tools\ch_probe\experiments\results\active.csv` has 2 data rows under its header.
   - Clone Hero is still running afterwards.

If it says `1/2 pairs` or `0/2 pairs`, you started the song before the script found the game. Quit the song and run it again, script first.

## Part 3: watch_window (hit-window steps 1 to 4)

This one uses no debugger and presses no keys. It reads the game's window value against the song clock while a song plays. After the song ends, it lines the values up with the notes from the song's manifest.

1. Run it, then start "Hydra Probe - Window Map" (or start the song first; it waits either way):

   ```powershell
   python tools\ch_probe\experiments\watch_window.py "Window Map"
   ```

2. It prints a line each time the window value changes. It stops by itself a couple of seconds after the last note, or when the clock stops moving for 8 seconds. Ctrl+C stops it early and it still reports.
3. At the end it prints the report, and its last line is `Samples written to …\watch_window_window_map_normal_<time>.csv`.

We don't yet know whether the window updates when nobody presses keys. If the run prints almost no window changes, run it again while `play_chart.py` hits every note. Use a second PowerShell window for play_chart:

```powershell
python tools\ch_probe\experiments\play_chart.py "C:\Clone Hero\songs\Hydra Probe\Window Map"
```

Start both scripts, then start the song.

Step 4 of the hit-window plan does the same thing on Edge Walk: `play_chart.py` hitting the notes and `watch_window.py "Edge Walk"` watching. Its report says whether the game's hit-time field changes once per hit and sits close to the note time.

## Part 4: walk_edges (hit-window step 5)

This one presses the kick itself, like play_chart, but most notes get a planned offset instead of being on time. That is how it finds where hits turn into misses on each side.

1. Run it, then start "Hydra Probe - Edge Walk" on Expert drums and keep your hands off the keyboard:

   ```powershell
   python tools\ch_probe\experiments\walk_edges.py
   ```

   By default it walks the late side from +80 to +92 ms and the early side from −80 to −92 ms, in 1 ms steps, 3 notes per step. Every leftover note is pressed on time.
2. The first 3 notes are on time and must show HIT. If they miss, the setup is broken (wrong key, or the game didn't have focus), and the rest of the run means nothing. Stop it and tell me.
3. At the end it prints a summary and `Rows written to …\walk_edges_<time>.csv`.

If one side misses at every step, its edge is below 80 ms. Run again with that side walked lower. For example, for the late side:

```powershell
python tools\ch_probe\experiments\walk_edges.py --late 68:80 --early none
```

## When something goes wrong

Paste the whole terminal output back to me, not a summary of it. If the game froze, also say which step it froze at and whether the Python window was still open.

## What I do after you report

For Task 21, tell me whether Parts 1 and 2 passed. That closes the task.

For the cleanup, tell me whether `watch_window.py` and `walk_edges.py` worked at the game. If they did, I'll delete the three older scripts they replace (`hit_detect.py`, `find_clock3.py` and `poll_windows.py`), rerun the ch_probe tests and commit. If either one failed, the old scripts stay until it works.

I'll read the result files as the experiment's data. That analysis is separate from both checks above.
