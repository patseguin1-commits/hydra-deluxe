# Codebase audit, 2026-09-25

This is a read-only audit of the whole repo as of 1.8.1 (ac6bc13). Six agents each took one area:

- the search engine and core rules
- the record store and file readers
- the analysis, reports and CLI
- the GUI, Preview and audio
- the build, tests, vendored code and docs
- the `ch_probe` Python tool

Before calling code dead, each agent had to search the whole repo for callers. Code that only tests call is marked "test-only", not dead. I re-checked the headline claims against the source myself: the Preview's audio error, the Uncapped import filter, the SQLite FTS5 flag and the test engine built into Hydra.exe. All four hold. Nothing here repeats an item from the 2026-09-24 derivation audit unless it is still unfixed; most of that audit's items are confirmed fixed.

The findings fall into four groups: bugs to fix first, dead code, slow spots and design problems. A final section lists the changes that would alter what you see, which need your yes before anyone touches them.

---

## 1. Real bugs: fix these first

**The Preview shows nothing on a PC with no audio device.** When the audio device fails to open, the controller stores the reason in its one error field. The comment there says "still previewable, just muted". But the Preview panel treats any error as fatal. It prints "Preview failed" and returns, so the highway never draws. The fix is a separate audio-warning field that shows one line and keeps drawing. See [preview_controller.cpp:120](src/ui/preview_controller.cpp:120) and [details_view.cpp:609](src/ui/details_view.cpp:609).

**A failed library rebuild empties the library.** `rebuild_chart_library` drops and recreates the `charts` table *before* it opens its transaction. If an insert then fails, the rollback leaves an empty table. That also loses the rescan cache, so the next scan re-hashes every chart. A throw between BEGIN and COMMIT also leaves the transaction open. The fix is to put BEGIN first (SQLite table changes are transactional too) and roll back on any exception. See [record_store.cpp:1549](src/store/record_store.cpp:1549).

**Batch cancel can hang, and never interrupts a running search.** The batch thread pool copies the scan's pool but not its hang fix. Workers can all exit on cancel without waking the consumer thread, and the scan guards against that with a "last worker out wakes the consumer" counter. The window is narrow, but when it hits, closing the app hangs. Separately, the batch passes no progress callback into the search. So cancel waits for up to 8 in-flight searches, each of which can run to the 120-second Auto budget. The single-chart Analyze button already cancels mid-search by throwing from that callback, and the batch should do the same. See [analysis.cpp:633](src/app/analysis.cpp:633).

**Closing the details window while a Preview is loading freezes the UI.** Neither the Preview load job nor the Dynamics load job ever checks its cancel flag. Closing the window joins their thread on the UI thread, so the app sits frozen until the whole parse, decode and mix finishes. That takes seconds on big charts. See [preview_load_job.cpp:26](src/ui/preview_load_job.cpp:26).

**`hydra_batch --reindex` relabels a legacy-fills database as 1.1.** The engine-mode stamp is written from the `--legacy-fills` flag before the `--reindex` early return. So reindexing a CH 1.0 database without the flag re-stamps it "ch11". A normal run into a legacy database also mixes both fill rules, then stamps it ch11. The fix is to refuse a run whose flag disagrees with the existing stamp, and not stamp at all on reindex. See [batch.cpp:139](src/cli/batch.cpp:139).

**Two lifecycle leaks of the known "state lives in render code" kind.** The Dynamics job is only collected while its tab is on screen. A parse that finishes while you're on another tab is never saved. Closing the window then throws the result away, so the chart gets parsed again next time. The "Rescan library" button hides the window in a way that skips the teardown block, which leaves the Preview playing and jobs running. That button is only reachable when the chart file has gone missing. The fix is one `AppState::close_details()` that runs on the open-to-closed edge. That would also replace three copies of the teardown code. See [details_view.cpp:211](src/ui/details_view.cpp:211) and [details_view.cpp:873](src/ui/details_view.cpp:873).

**The Preview's art and the icons fail if Hydra is installed under a non-ASCII path.** `render/file_util.cpp` and `icons.cpp` open files with narrow ANSI calls, but the path they're given is UTF-8. So a user whose folder is something like `C:\Users\Zoë\…` gets "missing 3d-config.json". Program Files installs are fine. `core/winstr` already has UTF-8-safe readers, so `render/file_util` can simply be deleted.

**`get_record` decodes the whole record while holding the store lock.** The header says the lock covers SQLite calls only. The UI thread calls this function, so a big record blocks the batch writer, the same class of problem as the batch-finish freeze fixed earlier. The fix is to narrow the lock to the SELECT and decode after releasing it. See [record_store.cpp:1108](src/store/record_store.cpp:1108).

**The release build ships the whole GUI test suite and your folder paths.** `HYDRA_UITEST_ATTACHED` is always on for `Hydra`. So the shipped exe contains the test engine, every UI test and the absolute paths `C:\Users\Patrick\Downloads\Hydra\hydra-test\testdata\input`, `…\assets\preview` and `…\resource`. The fix is a CMake option that the installer build turns off. See [CMakeLists.txt:288](CMakeLists.txt:288).

**About 2.6 GB of untracked song data sits in the repo root with nothing ignoring it.** One `git add -A` would commit it:

- `srb_songs/` (2.4 GB of extracted songs with audio)
- `srb_audio_dump/` (169 MB of WAVs)
- `libvpx_err.txt` (a 50 MB ffmpeg log)

The other loose files are old benchmark logs, and several are empty: the `e*.txt`, `err_*.txt`, `d*.txt`, `fixtureB.json` files and `scratch_ms/`. They should move out of the repo or be deleted, with `.gitignore` guards added.

**`ch_probe` bugs:**

- The uncommitted change in `process.py` switched the constants check to seconds. `tests/test_process.py` still passes milliseconds, so three tests now fail.
- Both tracked probe runners (`passive_probe.py`, `active_probe.py`) set breakpoints without ever attaching the debugger, so they crash on the first call.
- The debugger never calls `DebugSetProcessKillOnExit(FALSE)`. A Python crash or Ctrl+C while attached therefore kills Clone Hero.
- `play_chart.py`'s .chart path ignores the cymbal markers and the 2x kick note. On a pro-drums .chart, every yellow, blue and green cymbal is played as a tom and 2x kicks are skipped. This is the same bug class you already fixed on the MIDI path.

---

## 2. Dead code that can go

**The Uncapped-edition import can never copy a row.** It runs on every store open. It only matches rows stamped with *this* build's version plus ".uncapped", something like "1.8.1.uncapped", which no real old file can contain. Its test fakes exactly that stamp. Deleting the import, its call in `open_store` and its test changes nothing a user sees. See [record_store.cpp:794](src/store/record_store.cpp:794) and [config.cpp:53](src/app/config.cpp:53).

**The whole-record blob format is test-only.** Production writes and reads only the structure-plus-nodes format. That leaves `write_record`, `read_record`, `write_path`/`read_path` and every version-below-6 branch in `path_binary` reachable only from tests. The two-argument `read_record` has no caller at all. The header comment that says "Version 1 blobs are still read" is also false. They can't be read since ADR 0015, because every old chord code is rejected now. `drop_stale_records`, `Lens::sentinel()` and `is_sentinel()` are test-only too.

**Engine fields nobody writes or reads:**

- `Path::skipped_accents` and `skipped_ghosts` are never set to anything but 0. So the two "skipped accent/ghost" warnings in the path view can never appear, and every stored node carries 8 wasted bytes.
- `Variant::sc[6]`, `notecount` and `leftover_sp` are copied and stored, then overwritten by `prepare_variants`.
- `ScoreGraph::length()`, `sp_start()` and `fill_rule()` have no callers.
- `head_time_set_` and `OutAct::sqout_tick` are written but never read.
- `FrontendSqueeze::chord` is never read; only `.points` is.
- The base-track copies of the `sp_times` extension maps are never read.
- `cymbal_flip()` has no callers.
- `allows_dynamics()` always returns true since ADR 0012.
- `MeasureIndex`'s `tick_r` parameter and `windows_for_path`'s `Song` parameter are unused.

Removing the stored fields needs a node-format bump, so batch that with the next one.

**Dead UI and audio code.** `PreviewController::open_key()`, `scrubbing()` and `volume()` have no callers. Neither do `PreviewAudioDevice::stop()`, `running()`, `audio::headless()` or `kDisabledTextColor`. `PreviewRenderer::Impl::have_state` is written and never read, and `RenderParams::speed` is only ever 1. The ImGui viewports block in `main.cpp` and `ConfigDpiScaleViewports` are dead because the viewports flag is never set.

**Dead report fields.** `ReportRow::delta` goes into the page JSON, but the page never reads it. `py_repr` says "exposed for tests", yet no test calls it. `count_chart_chords` is test-only, and its comment names a display that doesn't exist.

**ADR 0002's byte-parity pin is gone.** Several comments say the report page is pinned byte-for-byte to `hydra_report.py`. That file was deleted in the C++ cutover (95f22b0), and no test pins page bytes. The pin is the only stated reason for three near-identical page scripts, unused CSS and two copies of the by-hash index. Retiring the ADR frees all of that to be shared.

**Build leftovers.**

- `SQLITE_ENABLE_FTS5=0` was meant to turn full-text search off, but it turns it on. SQLite only checks whether the macro is defined, so FTS5 is compiled into every binary. Delete the line ([CMakeLists.txt:73](CMakeLists.txt:73)).
- `icons.cpp` still gets `/W1` and a `third_party/stb` include, and neither is needed any more. The first hides it from `/W4` warnings.
- `shell32` is linked twice and needed by neither target.
- Several CMake comments describe libopus as "not wired in" or leave out the newer targets.

**Vendored code: about 220 files that the build never touches.** In opus that means:

- `dnn/`: 78 files, compiled only with deep PLC/DRED/OSCE, which are all off
- `doc/` and the three `tests/` folders
- `meson/` and the meson build files
- the autotools scripts

In ogg it means `doc/` (87 files), `win32/` and the autotools scripts. A few opus files look like junk but must stay: `configure.ac`, `package_version`, all the `*.mk` source lists, and the arm/mips/fixed folders, because the `.mk` lists name their headers.

**Docs.** `docs/archive/CPP_PORT_PLAN.md` is historical and nothing links to it, so git history is enough. The ch_probe spec still says "not yet implemented". The README's command-line section leaves out `hydra_fillcompare`, `--rules` and `--legacy-fills`. ADR 0007 never existed (0006 and 0008 landed in the same commit), yet `image/decode.h` cites it. The two "retired Python-era" lines in `.gitignore` point at folders that no longer exist.

**`ch_probe`.**

- Delete `find_clock.py`, `find_clock2.py`, `reactive_inputs.py` and `send_inputs.py`, because later scripts superseded them.
- Rewrite or delete `passive_probe.py` and `active_probe.py`, since neither can run.
- `ocr.read_accuracy_ms` has no caller, and `set_hw_data_breakpoint` always raises. Several constants are unused.
- The README still describes only the debugger route.

---

## 3. Slow spots

**The search's hot loop does hash-map lookups it doesn't need.** Every time the engine reads a node or an edge, it rebuilds a small view by looking pointers up in `unordered_map`s. That's 10 to 16 lookups per path per step, across the whole live frontier. Two plain arrays built once in `enumerate()` would replace them. This is the most promising speed item in the engine; measure it with `hydra_bench`. See [engine.cpp:301](src/search/engine.cpp:301).

**Building the graph throws and catches an exception for nearly every chord.** The graph tries to make a `MultSqueeze` for every chord and catches the failure. That failure is the normal case, and it happens up to seven times per Auto analysis (once per ladder rung). A plain `applies()` check would do the same job. See [graph.cpp:115](src/search/graph.cpp:115).

**The Paths tab rebuilds everything every frame.** At 60 frames a second, the Paths tab:

- rebuilds the path list, every row and every activation rating
- rebuilds the multiplier squeezes, the score breakdown and the status line
- calls `pathstring()` over and over, and builds `pathstring_verbose()` just in case Ctrl+C is pressed

The Preview tab builds its overlay key string every frame too. Each rebuild goes through `Path::all_activations()`, which deep-copies every activation with all its vectors, and the library report makes 3–4 such copies per path. Two fixes cover this. First, cache the built views and rebuild only when the record, path or settings change. Second, give `Path` a walk that doesn't copy.

**The UI thread touches the disk and the database every frame.** The library window checks whether the report file exists on every frame. The details window checks whether the chart file exists on every frame. On a sleeping or network drive, either can stall a frame. Holding a +/- button on the score-range, ms-limit or SP-cap boxes is worse. Every frame, it rewrites the INI, re-queries the library page one row at a time and decodes the whole record again.

**Saving a result is slow per chart.** For every path node, `add_row` re-compiles two SQL statements. Each chart is also saved in three separate commits. No `journal_mode` or `synchronous` pragma is set anywhere, so every commit pays full disk syncs. Separately, `reindex` does one commit per record, which comes to 18,000 on a full library. The fixes are to prepare the statements once, use one transaction per chart and consider WAL mode (a journal mode that makes commits cheaper).

**`hydra_batch` re-hashes the whole library on every run.** It calls the no-cache chart scan, even though the store is open and the scan cache exists. So every run hashes every chart file in full, including `.sng` files with their audio. The GUI already uses the cache. See [batch.cpp:177](src/cli/batch.cpp:177).

**More repeated queries.** Each library page runs one summary query per row on the UI thread, and the `charts` table has no index on name. Before a batch starts, `has_record` runs once per library chart, which is 18,000 statement compiles.

**Loading a Preview briefly needs about twice the audio memory.** The mixer keeps every decoded stem and every converted copy alive until the sum is done. For five 5-minute stems, that peaks near 1.2 GB. The Opus decoder also allocates a new buffer for every packet. The fix is to add one stem at a time and drop it.

**Smaller items:**

- The highway renderer deep-copies every visible note on every frame.
- Switching paths rebuilds the Preview scene synchronously on the UI thread.
- The rules fingerprint is rebuilt for every record decoded.
- The report rebuilds its timing-tier table for every row.

**The build.** The three GUI test files are compiled twice, once for `Hydra` and once for `hydra_uitest`. There's no link-time optimization on the search engine. miniaudio compiles its engine, node graph and every non-WASAPI backend, none of which Hydra uses. The test suite has 35 loops over the corpus that often re-analyze the same chart with the same settings. A cached `corpus::analyzed()` would probably cut test time a lot, but nobody has measured it.

---

## 4. Design problems

**Stored-value rules that still have a live fallback.** `rate_activation` recomputes the transfer scales live when timing is available, and reads the stored values only otherwise. That is the "re-derive when you can" pattern that ADRs 0011, 0013 and 0014 removed everywhere else. Every Ready record has the stored values, so it should always read them.

**The multiplier squeezes are one fact about the whole chart, copied onto every path.** The combo counter never depends on the path. Yet every path, every variant and every stored node carries its own copy of the list, and the graph also keeps a second copy on the SP track that nothing reads. The list should be computed once and stored on the record.

**The rules fingerprint and the Auto time budget disagree.** Editing the Auto ladder or the time budget marks fixed-cap records stale, though neither can change them. The budget is also stored in two places: the search reads one and the fingerprint stamps the other. So a run with no budget still stamps "120 s". ADR 0014 chose "every edit makes everything stale" on purpose. The budget part of that looks like over-reach, though, because a wall-clock limit can't be reproduced anyway.

**The "which row wins" logic exists four times** in the record store, across `get_summary`, `get_record`, `for_each_blob` and `list_records`. The Ready rule is also spelled once more in SQL. One helper should own it.

**Interfaces that invite mistakes.**

- `run_search` takes three booleans in a row plus a raw pointer, so two swapped flags would still compile.
- `run_batch` takes the chart mode, the lens and the search settings as separate arguments, and nothing checks that they agree.
- `analyze_chart` has two fixed-cap branches that do the same job.
- Six fields that the engine always sets are still `std::optional`, a Python-era leftover. Some callers check them and some don't.

**Layering.** `core/replay.h` pulls a JSON library into core, but only `hydra_replay` and the tests use the JSON functions. They belong in `tools/`. `library_view.cpp` keeps its state in function statics: the search box text, the confirm dialog and the DM filter. Those outlive each test's `AppState`, and they're the only reason `Generation` needs its process-wide counter trick. They should move into a `LibraryViewState`, the way `DetailsViewState` did.

**Encoding.** Only `hydra_dm` is built with `/utf-8`. But `report.cpp`, `html_page.cpp`, `app_state.cpp` and `library_view.cpp` all have em dashes in string literals. That works on an English Windows only by luck, and it would garble on a CJK code page. The fix is to move `/utf-8` onto every Hydra target.

**Small duplicates.** There are three lowercase helpers, two `trim`s and two `ends_with_ci`s. `build_cpp.ps1` and `build_installer.ps1` carry identical copies of `Find-CMake`. Both presets share one build folder with different generators, so switching between them breaks the configure step.

**Repo.** Git tracks `claude.md` in lowercase, while the file on disk is `CLAUDE.md`. This only works because Windows ignores case. On GitHub or Linux, tools looking for `CLAUDE.md` won't find it. `hydra_bench` and `hydra_replay` are excluded from the default build, so they can break without anyone noticing. `build_cpp.ps1` should build them too. The three CLI programs have no tests. Of the GUI's features, only 6 flows are covered, and the DM compare, settings and report buttons are not.

**`ch_probe` structure.** `play_chart.py` works, but it bypasses the seven-piece design. It calls a private input method directly, skips `EngineModel` and the debugger, and finds the engine by scanning memory. So the unit tests cover code paths the working tool never runs. Some logic is copied across scripts:

- the "find the live engine" search: 8 copies
- the key table: 5 copies

Offset `0x100` has three different names across the scripts. `engine.song_clock` still reads a guessed offset (`0x1A0`), and a test locks that guess in. "Lane 0" means kick in one module and green in another. The working pieces should move into tracked modules: the engine scan, the "whose clock moves" check, a public chord press and one key table.

---

## 5. Changes that need your yes (they alter what you see)

1. **Song names in reports.** Today, a song's name is frozen at its first analysis, so fixing song.ini never reaches the reports. Changing the insert to an upsert (update the row if it already exists) would make report names follow song.ini.
2. **Chart modes in the path report.** The report mixes every chart mode into one table without saying which row is which. For example, a song analyzed as Expert Pro and as Hard shows two rank-1 rows. You could filter to the current mode, or add a Mode column. The subtitle's "N records across M songs" also counts the whole database, not what's on the page.
3. **Number boxes commit when you leave the box**, not on every step. This removes the per-frame database work, but the record would update after the edit instead of live.
4. **Audio offset for `.sng` charts.** The Preview applies the chart's offset only to folder charts and gives `.sng` charts 0. Including them would change what you hear.
5. **Dropping the pre-1.7 database migrations.** Users upgrading from 1.5.x or 1.6.x would see "Not analyzed" instead of "Stale". Either way, their old rows are unusable since 1.8.1.
6. **Retiring ADR 0002**, so the three report pages can share one script.
7. **Taking the time budget out of the rules fingerprint**, so editing it no longer marks every record stale.

---

## Suggested order

1. **Bugs.** Start with the bug list in section 1: the Preview audio error, the library rebuild, batch cancel, the load-job cancel flag, the reindex stamp and the lock scope. Each is small and each one is a real failure.
2. **Repo hygiene.** Ignore and move the 2.6 GB of song data, fix `claude.md`'s case, fix the FTS5 line and turn the UI tests off for release builds.
3. **Easy deletions.** Next come the no-risk deletions: the Uncapped import, the test-only blob format, the dead UI getters and the vendored opus/ogg extras.
4. **Speed work,** each item measured with `hydra_bench` or a timed batch: array-indexed views in the search loop, no exception per chord, cached Paths-tab views, prepared statements plus one transaction per chart, and the scan cache in `hydra_batch`.
5. **Format-bump cleanup.** Hold the engine-field removals (skipped accents, variant scores, multiplier squeezes per path) until the next node-format bump, and do them together.
