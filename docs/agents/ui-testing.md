# GUI testing: drive the real UI by label, read back text

Hydra's UI is Dear ImGui. The Dear ImGui Test Engine (vendored at `third_party/imgui_test_engine`) can find a widget by its label, click it, type into it, and read what is on screen. `hydra_uitest` runs the whole UI with no window on a software GPU, so an agent can verify a GUI change in about a second and get **plain text** back — not screenshots.

Use this instead of launching `Hydra.exe` and taking screenshots. Reach for a screenshot only when something looks visually wrong.

## Build and run

```bash
.\build_cpp.ps1 -Target hydra_uitest
```

```bash
.\build-cpp\Release\hydra_uitest.exe --all
```

| Command | What it does |
|---|---|
| `hydra_uitest --all` | run every checked-in test |
| `hydra_uitest --test scan` | run one test (repeatable) |
| `hydra_uitest --list` | list the tests |
| `hydra_uitest --script file.txt` | run a command file (see below) |
| `--keep-temp` | keep the scratch folder (DB, INI, report HTML) and print its path |
| `--shots <dir>` | where `screenshot` files go (default: the scratch folder) |

Output is `[PASS]`/`[FAIL]` per test. A failed test prints the engine's log, which names the check that failed (`uitest_tests.cpp:29`) and every action before it. Exit code 0 only when everything passed. `ctest` runs it too.

Each test starts from scratch: a temp folder with a settings INI (song folder = `testdata/input`, 97 charts; "open report automatically" off; search depth 2) and an empty DB. No browser opens, no sound card is touched, and the dmleaderboards API is canned (one user, `alice`, id `111`, whose one score is the first library chart). The seams are `set_open_in_browser`, `audio::set_headless`, `net::set_fetcher`, and `app::set_path_overrides`.

## Command files (no rebuild)

A command file is one verb per line. Example:

```
click Scan charts
wait-idle
click //Scanning charts/Continue
state
type ##search | Polyphia
wait 0.2
text
dump Hydra
screenshot lib.png
```

| Verb | Meaning |
|---|---|
| `window <ref>` | set the window the following refs are relative to (default `//Hydra`) |
| `click <ref>` | click a widget |
| `check <ref>` / `uncheck <ref>` | set a checkbox |
| `type <ref> \| <text>` | put text into an input |
| `wait <seconds>` | let frames run |
| `wait-idle` | wait until every background job (scan, analyze, report, DM) has finished |
| `wait-text <substring>` | wait until the text appears on screen |
| `expect-text <substring>` / `expect-not-text <substring>` | assert on screen text now |
| `text` | print everything on screen as text |
| `state` | print the key app state (library count, rows, selection, jobs, preview, status) |
| `dump [window]` | print the widget tree: label, id, rect, checked/disabled/opened/inputable |
| `screenshot <file.png>` | save the frame |
| `timeout <seconds>` | change the wait-text / wait-idle limit (default 30) |
| `#` | comment |

Lines print as they run; a failing line prints `!! failed at line N`.

## Refs (how to name a widget)

- A bare label is looked up in the current window: `Scan charts`.
- `//Window/Label` is absolute: `//Scanning charts/Continue`.
- `**/Label` searches every child window too — use it for anything inside a modal's panels or a table: `**/Analyze paths!`, `**/Play`, `**/Open path report`.
- `//$FOCUSED` is the focused window (the Song Details modal after a row click).
- `##id` labels work as written: `##search`, `##DetailsTabs/Preview`, `##scrub`.
- `###` labels are addressed by the part after `###`: DM picker rows are `**/###<discord id>`.
- Library rows: `**/<title>`; escape `/` and `#` in the title with a backslash.

Label cheat-sheet (main window `Hydra`): `Manage folders... (N)`, `Scan charts` / `Refresh scan`, `Analyze library` / `Analyze search (N)`, `redo existing`, `Compare dmleaderboards user...`, `Pro Drums`, `2x Bass`, `##search`, `Open path report` / `Building path report...`, `##pageleft`, `##pageright`.
Modals: `Scanning charts` (`Continue`, `Cancel`), `Analyzing` (`Start`, `Cancel`, `Continue`, `Open path report`, `Open automatically`), `Compare dmleaderboards user` (`##dmfilter`, `Close`, `Open report again`, `Compare another`), `Song folders`.
Song Details (`//$FOCUSED`): tabs `##DetailsTabs/Paths` and `##DetailsTabs/Preview`; `Analyze paths!`, `Copy path string`, `Play` / `Pause`, `##scrub`, `##volume`. There is no Close button — the modal closes via its title-bar X.

When unsure, `dump` the window and read the labels off it.

## Watching it run: attached mode

```bash
.\build-cpp\Release\Hydra.exe --uitest scan
```

Runs the same test inside the real window at human speed, with the Test Engine's own panel showing. Accepts a test name, `all`, or a command-file path. Results and any `text`/`state`/`dump` output go to `hydra_uitest.log` next to the exe (`--uitest-log <file>` to change); the window stays open afterwards so the end state can be inspected. Attached mode also runs on the scratch library, never on the real `hydra.db` — the tests wipe their DB at start.

## Adding a C++ test

Tests live in `tests/ui/uitest_tests.cpp`. Template:

```cpp
void test_thing(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);                 // fresh scratch library
    scan_library(ctx);            // helper: scan + Continue
    if (ctx->IsError()) return;   // helpers only return from themselves
    ctx->ItemClick("**/Analyze paths!");
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(visible_text(h).find("Done!") != std::string::npos);
}
// then add {"thing", test_thing} to the table in register_tests().
```

Rules of thumb: wait on app state (`h.app->…`) with `wait_until`, never on frame counts — jobs are real threads. `wait_until` yields one extra frame after its condition holds, so `visible_text` reflects it. An `IM_CHECK` inside a helper only returns from the helper; check `ctx->IsError()` after calling one.

## Layout

- `tests/ui/uitest_harness.{h,cpp}` — WARP device, offscreen target, engine setup, scratch files, seams, `wait_until`, `dump_*`, `screenshot`.
- `tests/ui/uitest_script.cpp` — the command-file interpreter.
- `tests/ui/uitest_tests.cpp` — the checked-in tests.
- `tests/ui/uitest_main.cpp` — the CLI.
- `src/ui/app_shell.{h,cpp}` — `setup_imgui` / `run_frame`, shared by `Hydra.exe` and the runner. `run_frame` can capture every string ImGui drew (`FrameText`), which is what `text`/`wait-text` read.
