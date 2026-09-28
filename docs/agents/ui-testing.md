# GUI testing: drive the real UI by label, read back text

Hydra's UI is Dear ImGui. The Dear ImGui Test Engine (vendored at `third_party/imgui_test_engine`) can find a widget by its label, click it, type into it, and read what is on screen. `hydra_uitest` runs the whole UI with no window on a software GPU, so an agent can verify a GUI change in about a second and get **plain text** back — not screenshots.

Use this instead of launching `Hydra.exe` and taking screenshots. Reach for a screenshot only when something looks visually wrong.

## Build and run

```bash
.\build_cpp.ps1 -Target hydra_uitest
```

```bash
.\build-cpp\Release\hydra_uitest.exe --all --jobs 4
```

| Command | What it does |
|---|---|
| `hydra_uitest --all` | run every checked-in test, one after another |
| `hydra_uitest --all --jobs <n>` | run every test in its own process, at most n at once |
| `hydra_uitest --test scan` | run one test (repeatable) |
| `hydra_uitest --list` | list the tests |
| `hydra_uitest --script file.txt` | run a command file (see below) |
| `--keep-temp` | keep the scratch folder (DB, INI, report HTML) and print its path |
| `--shots <dir>` | where `screenshot` files go (default: the scratch folder) |

Output is `[PASS]`/`[FAIL]` per test. A failed test prints the engine's log. The log names the check that failed (like `uitest_library.cpp:120`) and every action before it. The exit code is 0 only when everything passed. `ctest` runs it too.

**Running tests in parallel.** `--jobs <n>` starts each test as its own `hydra_uitest --test <name>` process, at most n at once. Each process has its own scratch folder and a fresh ImGui context, so no test sees another's leftovers. The results still print in the usual order. The full suite takes about 14 s at `--jobs 4`, against about 22 s one after another. `--jobs` runs named tests only, not scripts.

A few tests race a real batch against a count of frames. On a machine busy with other test processes, each frame is slow enough for the batch to finish first. Those tests are listed in `kRunAlone` in `tests/ui/uitest_main.cpp` (today `batch-strip-drift` and `batch-pause-stop`). `--jobs` starts them last, one at a time, with nothing else running. A new test that counts frames while a job runs belongs on that list.

Each test starts from scratch: a temp folder with a settings INI (song folder = `testdata/input`, 97 charts; "open report automatically" off; search depth 2) and an empty DB. No browser opens, no sound card is touched, and the dmleaderboards API is canned (one user, `alice`, id `111`, whose one score is the first library chart). Reports land in the scratch folder, never in the real Documents folder. The seams are `set_open_in_browser`, `audio::set_headless`, `net::set_fetcher`, and `app::set_path_overrides`.

## Command files (no rebuild)

A command file is one verb per line. Example:

```
click Scan library
wait-idle
click //Scanning charts/Continue
state
type **/##search | Burnout
wait 0.2
click **/Burnout
click **/Analyze this song
timeout 300
wait-idle
wait 0.5
click **/##DetailsTabs/Stars
wait 0.2
text
dump Hydra
screenshot stars.png
```

| Verb | Meaning |
|---|---|
| `window <ref>` | set the window the following refs are relative to (default `//Hydra`) |
| `click <ref>` | click a widget |
| `check <ref>` / `uncheck <ref>` | set a checkbox |
| `type <ref> \| <text>` | put text into an input |
| `wait <seconds>` | let frames run |
| `wait-idle` | wait until every background job (scan, analyze, batch, report, DM, Preview loading) has finished |
| `wait-text <substring>` | wait until the text appears on screen |
| `expect-text <substring>` / `expect-not-text <substring>` | assert on screen text now |
| `text` | print everything on screen as text |
| `state` | print the key app state (library count, rows, selection, jobs, preview, status) |
| `dump [window]` | print the widget tree of a window and its child windows: label, id, rect, checked/disabled/opened/inputable |
| `screenshot <file.png>` | save the frame |
| `timeout <seconds>` | change the wait-text / wait-idle limit (default 30) |
| `#` | comment |

Lines print as they run; a failing line prints `!! failed at line N`.

`wait-idle` counts a finished analysis as idle, but the song panel shows `Done!` in place of the tabs until the app clears the job a moment later. So put a short `wait` after it before reading a tab. A tab click also needs a frame or two before its text shows.

## Refs (how to name a widget)

- A bare label is looked up in the current window: `Scan library`.
- `//Window/Label` is absolute: `//Scanning charts/Continue`, `//Analyze library/Start analyzing`.
- `**/Label` searches every child window too. Most of the screen now lives in child windows (the settings bar, the library, the song panel, the batch strips), so use it for anything below the toolbar: `**/##search`, `**/Analyze this song`, `**/Expand all`, `**/Pause`.
- `##id` labels work as written: `**/##spcap`, `**/##DetailsTabs/Preview`, `**/##scrub`.
- `###` labels are addressed by the part after `###`: DM picker rows are `**/###<discord id>`.
- Library rows: `**/<title>`; escape `/` and `#` in the title with a backslash.
- Child window names are mangled (`Hydra/##songpanel_BBB91B00`), so a C++ test points at one with `ctx->WindowInfo("//Hydra/##songpanel").Window`, the way `set_panel_ref` does.

## Label cheat-sheet

These are the labels the merged app draws. Every GUI test finds widgets by them. The `...` is three ASCII dots.

| Area | Labels |
|---|---|
| Toolbar (window `Hydra`) | `Manage folders... (N)`, `Scan library`, `Analyze library...` or `Analyze search (N)...` while searching, `Compare with dmleaderboards...`, `Open path report` / `Building path report...` |
| Status line | a problem stays until `X##dismissstatus` |
| Settings bar, child `##settingsbar` | combo `##difficulty`, checkboxes `Pro Drums` and `2x Bass`, inputs `##spcap` and `##depthvalue`, combo `##depthmode`, checkbox `Path limit##mslimit`, input `##mslimitvalue`. While locked it shows `Stop the batch to change these.` (batch) or `Settings are locked while this song analyzes.` (one song) |
| Library, child `##library` | search input `##search`, clear button `X##clearsearch`, chips `All (N)##chipall`, `Not analyzed (N)##chipnew`, `Stale (N)##chipstale`, `Analyzed (N)##chipdone`, table `##librarytable` with columns `Title`, `Artist`, `Charter`, `Folder`, `Best path`. A Best path cell reads `Not analyzed`, `Stale`, or `<score>  <path>`. Under a search: `Clear search` |
| Song panel, child `##songpanel` | `Hide library` / `Show library`, `<##prevsong`, `>##nextsong`, `X##closepanel` (Escape does the same), `Analyze this song` (not analyzed) or `Re-analyze` (analyzed or stale), tab bar `##DetailsTabs` with tabs `Paths`, `Preview`, `Dynamics`, `Stars` |
| Paths tab | path buttons `##path<i>` (0-based), `Expand all` / `Collapse all`, activation rows `##act<i>` (1-based), links `Show in Preview >##showact<i>`, folds `Backend timings##act<i>`, `Multiplier squeeze##mult`, `Score breakdown##breakdown`, button `Copy path` (flashes `Copied!`), checkbox `Hide backend rows beyond##backendlimit`, input `##backendlimitvalue` |
| Preview tab | text `Showing` beside the combo `##previewpath`, `< Act##prevact`, `Act >##nextact`, transport `-5s`, `< 5 Ticks`, `Play` / `Pause`, `5 Ticks >`, `+5s`, sliders `##volume` and `##scrub` |
| Batch confirm, popup `Analyze library` | checkbox `Also re-analyze charts that already have a result##redo`, buttons `Start analyzing` and `Cancel` |
| Running batch, child `##batchstrip` | `Pause` / `Resume`, `Stop` |
| Finished batch, child `##batchdone` | `Open report`, `Show in folder`, `X##dismissdone`, checkbox `Open automatically` |
| Other modals | `Scanning charts` (`Continue`, `Cancel`); `Song folders` (`Add folder...`, `Scan now`, `Close`); `Compare dmleaderboards user` (`##dmfilter`, `Close`, `Open report` / `Open report again`, `Compare another`, `Back to list`) |

When unsure, `dump Hydra` and read the labels off it. The dump cuts long labels short (the test engine keeps about 30 characters), so `Hide backend rows beyond##backendlimit` prints as `Hide backend rows beyond##backe`. Combos print with an empty label; take their `##id` from the source.

## Watching it run: attached mode

```bash
.\build-cpp\Release\Hydra.exe --uitest scan
```

Runs the same test inside the real window at human speed, with the Test Engine's own panel showing. Accepts a test name, `all`, or a command-file path. Results and any `text`/`state`/`dump` output go to `hydra_uitest.log` next to the exe (`--uitest-log <file>` to change); the window stays open afterwards so the end state can be inspected. Attached mode also runs on the scratch library, never on the real `hydra.db` — the tests wipe their DB at start.

Attached mode exists only in dev builds (`build-cpp`). The installer builds with the `ship` preset, which leaves the GUI tests out, so an installed Hydra.exe ignores `--uitest`.

## Adding a C++ test

Tests are split by area into four files: `uitest_library.cpp`, `uitest_details.cpp`, `uitest_preview.cpp` and `uitest_batch_reports.cpp`. Each file ends with an entry table (`library_tests()`, `details_tests()`, and so on). Write the test in the file for its area, then add `{"thing", test_thing}` to that file's table. `uitest_paths.cpp` holds the Paths tab tests and registers them itself, from `register_paths_tests`.

`register_tests` in `uitest_tests.cpp` walks the four tables. Its `kRunOrder` list keeps the order the tests had before the split, because one ImGui context carries tab and input state from one test to the next under `--all`. A test that isn't on the list runs after the listed ones, in its file's order, so a new test needs no line there. A renamed test must be renamed in `kRunOrder` too, or the runner stops with an error.

Template:

```cpp
void test_thing(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);                 // fresh scratch library
    scan_library(ctx);            // helper: Scan library + Continue
    if (ctx->IsError()) return;   // helpers only return from themselves
    open_details(ctx, 0);         // click row 0, wait for the song panel, land on Paths
    if (ctx->IsError()) return;
    analyze_open_song(ctx);       // Analyze this song, wait for a Ready record
    if (ctx->IsError()) return;
    ctx->ItemClick("##DetailsTabs/Stars");  // the ref points at the panel now
    IM_CHECK(visible_text(h).find("Base score") != std::string::npos);
}
```

The shared helpers live in `uitest_harness.{h,cpp}`. `open_titled(ctx, search, title)` searches and opens a song by title. `set_panel_ref(ctx)` points refs at the song panel. `analyze_button_ref(h)` gives `**/Analyze this song` or `**/Re-analyze`, whichever is showing. `open_preview(ctx)` opens row 0 on the Preview tab and waits for it to load.

Rules of thumb:

- Wait on app state (`h.app->…`) with `wait_until`, never on frame counts. Jobs are real threads.
- `wait_until` yields one extra frame after its condition holds, so `visible_text` reflects it.
- `wait_until` returns false at once when the test has already failed (`ctx->IsError()`). A click that found no item no longer sits out the whole timeout.
- An `IM_CHECK` inside a helper only returns from the helper. Check `ctx->IsError()` after calling one.
- The Dynamics tab reads stored counts from the database first. A second open of the same chart shows them with no background job.

## Layout

- `tests/ui/uitest_harness.{h,cpp}` — WARP device, offscreen target, engine setup, scratch files, seams, `wait_until`, `dump_*`, `screenshot`, and the shared helpers every area file calls.
- `tests/ui/uitest_script.cpp` — the command-file interpreter.
- `tests/ui/uitest_tests.cpp` — `register_tests` and its `kRunOrder` list.
- `tests/ui/uitest_library.cpp`, `uitest_details.cpp`, `uitest_preview.cpp`, `uitest_batch_reports.cpp` — the checked-in tests by area, each with its entry table.
- `tests/ui/uitest_paths.cpp` — the Paths tab tests.
- `tests/ui/uitest_main.cpp` — the CLI, `--jobs` and `kRunAlone`.
- `src/ui/app_shell.{h,cpp}` — `setup_imgui` / `run_frame`, shared by `Hydra.exe` and the runner. `run_frame` can capture every string ImGui drew (`FrameText`), which is what `text`/`wait-text` read.
