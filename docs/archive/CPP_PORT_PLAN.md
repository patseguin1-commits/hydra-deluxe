# Hydra: Full Port from Python to C++

> **Historical document.** The port completed and the Python tree was deleted
> at commit `95f22b0`; the golden parity corpus this plan describes was
> removed after the cutover. Kept for provenance only — file paths, phases,
> and instructions below no longer apply. Python-era sources survive on the
> frozen `python-oracle` git tag.

> Reference plan for the C++ port. Written 2026-08-14.

## Context

Hydra is a score optimizer / path viewer for Clone Hero drums. It scans a chart
library, parses `.mid`/`.chart`/`.sng` charts, builds a two-track scoring graph,
runs a search (BFS + activation DP) for the optimal Star Power activation path,
stores results, and presents them in a desktop GUI. It also ships batch/report
CLIs and an "Uncapped" what-if edition.

Today it is ~14K lines of Python plus ~2K lines of C++. The C++ is a *hot-loop
accelerator* reached over a ctypes/C-ABI boundary: it already implements the two
hardest pieces — per-chord scoring (`hy_category_scores`) and the **entire**
search in both forms (`hy_search` BFS + `hy_dp_search` activation DP). The Python
side builds the graph, flattens it to arrays, calls across the boundary, and
rebuilds objects from the returned decision log.

**The problem this solves:** maintaining two codebases (Python + a C++ shim) is a
tax on every change and a source of drift (ABI versioning, mirrored structs, the
flatten/marshal/rebuild dance). The goal is **one codebase in C++** — abandon
Python entirely, delete the ctypes seam, and let the scoring/search core operate
on native objects directly instead of marshalled arrays.

**Decisions locked (with the user):**
- **UI:** Dear ImGui (the C++ library DearPyGui already wraps) with a **Win32 +
  DirectX 11** backend. Widget vocabulary maps closely from the current UI.
- **Persistence:** **redesigned** native format (not the old SQLite JSON+zlib
  blob layout). Existing user `.db` files are not required to open; a re-scan is
  acceptable (an optional importer is a stretch goal, not a gate).
- **Platforms:** **Windows-only.** Matches today's reality (MSVC, `.dll`,
  Windows packaging). Cross-platform is out of scope.
- **Strategy:** **Greenfield C++ app, built module-by-module in dependency
  order, each gated against Python golden outputs; delete Python once at parity.**
  Python remains the oracle until the final cutover.

## End-state architecture

A single native Windows executable (`Hydra.exe`, `HydraUncapped.exe`) built with
**CMake + MSVC (C++17)**. No Python, no ctypes, no `hyflat`/`hynative` boundary,
no ABI version. Third-party libraries, all vendored/fetched:
- **Dear ImGui** (+ Win32/DX11 backends) — GUI
- **SQLite3** amalgamation — chart library + records store (schema redesigned)
- **doctest** (or Catch2) — the C++ test harness
- **nlohmann/json** (header-only) — *only* for the parity harness (reading the
  golden JSON that Python emits); not used at runtime
- Windows API for clipboard (replaces `pyperclip`) and threads (replaces
  `multiprocessing`/`concurrent.futures`)

Proposed source layout:
```
src/
  core/     hymisc→timing, hyencode→chord tables, hydata→model
  parse/    hymidi→midi reader, hysong→chart parsers (mid/chart/sng)
  search/   ScoreGraph builder + hydra_search.cpp/hydra_score.cpp (folded in)
  store/    RecordStore (SQLite, new format)
  app/      analysis orchestration, batch thread pool, discovery
  ui/       Dear ImGui views (library, details, analyze modal, settings)
  cli/      hydra_batch, hydra_report
third_party/  imgui/, sqlite/, doctest/, json/
tests/     C++ unit + parity tests
tools/     golden-output generator (thin Python wrapper over the old tree)
```

## The parity spine (most important part)

The old code carries a large, ready-made oracle: `test/test_regression.py` (97
generated path-string assertions), `test_search_parity.py`, `test_uncapped.py`,
`test_midi_parity.py`, `dp_parity_check.py`, and the `test/input/` corpus. Because
persistence is being redesigned, we can't diff DBs; instead:

1. **Freeze the Python tree** on a tag/branch as the golden oracle. Do not modify
   it during the port.
2. Write a **golden-output generator** (`tools/gen_golden.py`, a thin wrapper over
   the existing `hyutil`/`hypath`): for every chart in `test/input/`, emit a
   deterministic JSON of parsed events, chord codes, and full analysis output
   (every path's `pathstring_verbose()`, per-category scores, summaries) for both
   editions and a range of SP caps / depths.
3. Every C++ phase ends by **diffing its output against the frozen golden JSON**
   over the whole corpus. A phase is not "done" until its diff is empty.
4. Keep `HYDRA_NO_NATIVE` parity flags in mind: the golden must be generated from
   the **pure-Python** path (`HYDRA_NO_NATIVE=1`) so the oracle is the reference
   semantics, not the current C++ (which the port must also match anyway).

This turns "did the port stay correct?" into a mechanical, automatable check at
every step, and is what makes heavy agent parallelism safe.

> **Post-cutover note:** the port is complete and the C++ engine is now the
> oracle; `golden/` is regenerated by `tools/gen_golden.cpp` (the
> `hydra_gen_golden` target) rather than the Python-era generators. The
> calibration-fill window (`kCalibrationFillWindowMs` in `src/core/model.h`)
> was widened to ±250ms in 1.4.0 and reverted to the original ±70ms in 1.4.1,
> so analysis semantics again match the frozen Python oracle — but the golden's
> `store.hyversion` stamps track the current version, so the golden stays
> self-generated. The `python-oracle` tag remains the frozen **1.3.1** oracle;
> parity tests are regression protection against the C++ engine's own history,
> plus the independent DP-vs-BFS cross-check.

## Phases

Each phase is independently verifiable against the golden oracle. Encourage
subagents where modules are independent; require adversarial verification on the
algorithm core.

### Phase 0 — Scaffolding, build, and the oracle
**Goal:** a compiling empty C++ app + the parity harness, before any real port.
- CMake project (MSVC, C++17, `/O2 /W4 /EHsc`), targeting `Hydra.exe`. Fold in
  `native/hydra_score.{h,cpp}` and `native/hydra_search.{h,cpp}` as-is (they
  compile today). **Fix the known `CMakeLists.txt` bug**: it lists only
  `hydra_score.cpp`, omitting `hydra_search.cpp`.
- Vendor Dear ImGui + Win32/DX11 backend; stand up a blank window that renders.
- Vendor SQLite3, doctest, nlohmann/json.
- Build `tools/gen_golden.py` and generate the golden corpus once. Commit it.
- **Agents:** low parallelism (foundation). One focused pass.
- **Gate:** `Hydra.exe` opens a blank ImGui window; `gen_golden.py` produces a
  stable, committed golden set; existing `native/*` compiles under the new CMake.

### Phase 1 — Foundation modules (leaf, no internal deps)
**Goal:** timing math, chord tables, MIDI reader in C++.
- `hymisc.py` → `core/timing`: `Timecode` (compares/hashes on `int64` ticks only),
  `TempoMap`, `MsIndex`/`MeasureIndex`, `to_multiplier`, edition constants
  (`apply_edition`). ~500 lines, self-contained.
- `hyencode.py` → `core/chord_tables`: mechanical translation of the large
  `CHORD_ENCODE` lookup dicts. Consider code-generating the tables from the Python
  source rather than hand-typing.
- `hymidi.py` → `parse/midi`: the custom byte-walking MIDI reader (stdlib `struct`
  only → trivial in C++). Mirror the minimal `MidiFile`/`Message`/`MetaMessage`
  shape.
- **Agents:** high parallelism — three genuinely independent modules; one agent
  each, verified separately.
- **Gate:** unit tests for timing/tempo edge cases; `parse/midi` output diffs
  clean against the `test_midi_parity` corpus (byte-for-byte event streams).

### Phase 2 — Domain model + chart parsing
**Goal:** the note/chord/path/record model and the parsers that fill it.
- `hydata.py` → `core/model`: `Chord`/`ChordNote`, `NoteColor`/dynamics/cymbal
  enums, `Path`/`Activation`/squeeze types (`SqIn`/`SqOut`/`MultSqueeze`/…),
  `HydraRecord`. **Note:** `json_save`/`json_load` are being *replaced* by the new
  binary serialization (Phase 4) — but the model's *string* forms
  (`pathstring`, `pathstring_verbose`, `code`/`from_code`) must match Python
  exactly, since those are what the golden diff checks.
- `hysong.py` → `parse/song`: `Song`/`SongTimestamp`, the `MidiParser` (note
  numbers 95–100 drums, 120 activation, velocity 1/127 dynamics; the `op_*`
  dispatch) and the `ChartParser` for `.chart` text. `.sng` archive loading.
- **Agents:** medium — model and parsers are separable but the parsers depend on
  the model; sequence model first, then parsers (can still fan out mid/chart/sng).
- **Gate:** parse the whole corpus; diff `Song` event streams and every chord's
  `code` against golden. `.sng`, `.chart`, `.mid` all covered.

### Phase 3 — Core algorithm (the heart, highest risk)
**Goal:** native `ScoreGraph` feeding the already-ported search, boundary dissolved.
- `hypath.py` → `search/graph` + `search/pather`: port `ScoreGraph`
  construction (`compute_bounds`, the `store_*score` accumulators, act/deact
  edges, `advance_tracks`), and the `category_scores` per-chord function (already
  in `hydra_score.cpp` — reuse directly).
- **Dissolve the ctypes seam:** the existing `hydra_search.cpp` consumes the flat
  arrays that `hyflat.py` produced. Now that the graph is native, either (a) build
  the flat form in C++ (smallest change — reuse `hydra_search.cpp` almost
  verbatim) or (b) let the search read the graph objects directly. Recommend (a)
  first for a fast parity win, then simplify to (b) as cleanup. Delete
  `hyflat`/`hynative`/ABI machinery.
- Keep **both** engines (`hy_search` BFS and `hy_dp_search` DP) and their
  cross-check (`dp_parity_check` becomes a C++ test).
- **Agents:** the graph builder can be one agent; verification must be
  **adversarial** — spawn independent verifiers to hunt for any chart where the
  C++ path string, score breakdown, path count, or ordering diverges from golden.
  This is the phase to over-invest in testing.
- **Gate:** empty diff against golden for `test_regression` + `test_search_parity`
  + `test_uncapped` corpora, capped and uncapped, across depths, both editions.
  Byte-identical path strings and per-category scores. DP == BFS best score.

### Phase 4 — Persistence + orchestration
**Goal:** the store (new format) and the scan→analyze→save pipeline.
- `hystore.py` → `store/record_store`: SQLite via the C API. **Redesign the
  format**: keep the `songmeta`/`records` table split and the denormalized summary
  columns (they drive sortable listings — cheap and worth keeping), but replace
  the JSON+zlib blob with a **versioned binary serialization** of the record
  (hand-rolled writer/reader or a header-only lib; include a format-version tag).
  Port `summarize_path`/`summarize_record`, `reindex`, `drop_stale_records`,
  `list_records`.
- `hyutil.py`/`hybatch.py` → `app/analysis`: `discover_charts`, `ScanItem`,
  `analyze_chart_*`, `_analyze_at_cap`, the `_analyze_uncapped` SP-cap ladder with
  its time budget + cancel path, `count_chart_chords`. Replace the
  `multiprocessing`/`concurrent.futures` worker pool with a native
  `std::thread` pool (no pickling, shared memory — simpler in C++).
- **Agents:** medium — store and orchestration are separable.
- **Gate:** scan a folder headlessly, analyze, persist, reload, and diff the
  reloaded records + summary columns against golden. Uncapped ladder settles at
  the same cap/score as Python.

### Phase 5 — GUI (Dear ImGui)
**Goal:** the desktop app, feature-parity with `hydra_app.py`.
- `hydra_app.py` (1,784 lines, ~470 DearPyGui calls) → `ui/`. Rebuild in
  immediate-mode ImGui: main window/viewport + render loop; **library view**
  (paginated, searchable table); **song details** panel; **analyze modal** with a
  background worker + per-frame progress repaint (the `BatchJob` behavior);
  **path viewer** (list on the left, details on the right); **settings**
  (difficulty/pro/2x, edition), persisted to an ini-equivalent; fonts (the two
  TTFs in `resource/`, loaded natively by ImGui), icons, themes; clipboard copy of
  path strings via Win32.
- DearPyGui is retained-mode-ish (tag-addressed); ImGui is immediate-mode — this
  is a real rewrite of control flow, not a transliteration. Decompose by **view**.
- **Agents:** high parallelism — one agent per view (library, details, analyze
  modal, settings), integrated against the shared store from Phase 4.
- **Gate:** manual/visual parity (the workflow in README steps 3–9: add folder,
  scan, browse, analyze, inspect paths) plus the same store backing and correct
  progress/cancel behavior. Verify by running `Hydra.exe` (screenshot tooling does
  not apply to a native window).

### Phase 6 — CLIs, packaging, cutover
**Goal:** the command-line tools, a shippable build, and Python deletion.
- `hydra_batch.py` → `cli/batch`; `hydra_report.py` → `cli/report` (the
  self-contained sortable **HTML report** generator — string-building, port
  directly and diff the HTML against Python output).
- **Uncapped edition:** it's just `hymisc.apply_edition` constants + separate
  db/ini — implement as a build flag / runtime switch producing
  `HydraUncapped.exe`, carried through every phase, not a separate port.
- **Packaging:** replace PyInstaller (`build.py`, `HydraTest.spec`,
  `HydraUncapped.spec`) with a CMake `install`/CPack step (or a simple zip of the
  `.exe` + `resource/`). No Python runtime to bundle.
- **Cutover:** once all gates are green, delete the entire Python tree (`hydra/`,
  top-level scripts, `test/`, `.venv`, specs), keep the frozen oracle on a tag for
  posterity, and rewrite `README.md`/`docs/` for the C++ build.
- **Agents:** low — mostly mechanical + the final delete.
- **Gate:** `hydra_report` HTML diffs clean; both `.exe`s build via CMake; a
  real-library end-to-end run matches Python scores.

## Cross-cutting notes
- **Reuse, don't rewrite, the algorithm core.** `hydra_score.cpp` and
  `hydra_search.cpp` (~1,900 lines) already encode the hardest, most-tested logic
  and pass parity today. Phase 3 is mostly *wiring native objects into them* and
  deleting the marshalling — not re-deriving the search.
- **The flat-array design was a boundary artifact.** With one language it can be
  simplified away (Phase 3 option b), removing `hyflat`, `hynative`, the mirrored
  ctypes structs, and the whole ABI-version discipline.
- **Determinism is load-bearing.** The golden diff only works if ordering,
  tie-breaking, and float formatting match. Preserve stable sorts and the exact
  scoring arithmetic; watch float→string formatting in `pathstring_verbose` and
  `hardest_ms`.
- **Keep the Python oracle untouched and reproducible** for the entire port.

## Risks
- **Algorithm parity drift (Phase 3)** — highest. Mitigation: adversarial
  multi-agent verification over the full corpus; both engines cross-check.
- **Float/format mismatches** breaking the golden diff on otherwise-correct logic.
  Mitigation: pin formatting rules early; diff a small chart first.
- **GUI rewrite scope (Phase 5)** — immediate-mode is a paradigm shift.
  Mitigation: decompose by view; the store/analysis layer is already proven by
  Phase 4, so the UI is "only" presentation.
- **`.sng` archive parsing** correctness — verify against corpus samples early.

## Verification (end to end)
- Per-phase: empty diff against the frozen golden JSON over `test/input/`.
- Algorithm: port `test_regression`, `test_search_parity`, `test_uncapped`,
  `dp_parity_check`, `test_midi_parity` as C++ (doctest) tests reading the same
  corpus and golden.
- App: run `Hydra.exe`, follow README steps 3–9 on a real Clone Hero library;
  confirm scores/paths match a Python run of the same charts.
- Uncapped: confirm the SP-cap ladder settles at the same cap and score as Python
  for the reference charts (Hail The Sun, Rise Against, blink-182 discographies).
