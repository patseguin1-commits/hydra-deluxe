# Hydra C++ Port — Execution Prompt

> Copy-paste this into a fresh Claude session to drive the port. The full
> reasoning, phase gates, and file-level detail live in
> [`CPP_PORT_PLAN.md`](CPP_PORT_PLAN.md) — read it alongside this brief.

---

**Project: port Hydra from Python to C++ (Windows-only, single codebase).**

Hydra is a Clone Hero drums score/path optimizer: it scans a chart library,
parses `.mid`/`.chart`/`.sng`, builds a two-track scoring graph, runs a BFS +
activation-DP search for the optimal Star Power path, stores records, and shows
them in a desktop GUI (currently DearPyGui) with batch/report CLIs and an
"Uncapped" edition. ~14K lines of Python in `hydra/` + top-level scripts, plus
~2K lines of already-working C++ in `native/` (the scoring core + the full
search, reached over a ctypes C-ABI). Read `README.md`, `OPTIMIZATION_PROMPT.md`,
`hydra/hypath.py`, `hydra/hydata.py`, `native/hydra_search.h`, and
`hydra/hynative.py` first, then `docs/CPP_PORT_PLAN.md`.

**Locked decisions:** UI = Dear ImGui (Win32 + DX11 backend); persistence =
redesigned native format (old `.db` files need not open; re-scan is fine);
platform = Windows-only; strategy = greenfield C++ built module-by-module in
dependency order, each gated against a frozen Python golden oracle, then delete
Python at cutover. Build with CMake + MSVC, C++17. Reuse `native/hydra_score.cpp`
and `native/hydra_search.cpp` as-is — they're the hardest, most-tested logic —
and dissolve the ctypes/flatten/marshal boundary rather than re-deriving the
search.

**Work the phases in order (see the plan file); do not start a phase until the
prior phase's parity gate is green:**

0. **Scaffolding:** CMake app, blank ImGui window, vendored deps (ImGui, SQLite3,
   doctest, nlohmann/json), fix the `CMakeLists.txt` bug that omits
   `hydra_search.cpp`, and build+commit the **golden-output generator**
   (`tools/gen_golden.py`, thin wrapper over `hyutil`/`hypath`, run with
   `HYDRA_NO_NATIVE=1`) over `test/input/`.
1. **Foundation:** `hymisc`→timing, `hyencode`→chord tables, `hymidi`→MIDI reader.
2. **Model + parsers:** `hydata`→model, `hysong`→song/chart/sng parsers.
3. **Algorithm:** `ScoreGraph` builder wired into the existing native search; keep
   both engines; **adversarial verification** over the whole corpus.
4. **Persistence + orchestration:** `hystore` (new binary format), `hyutil`/`hybatch`
   (discovery, cap ladder, native `std::thread` pool).
5. **GUI:** rebuild `hydra_app.py` in immediate-mode ImGui, decomposed by view.
6. **CLIs + cutover:** `hydra_batch`, `hydra_report` (HTML), Uncapped edition as a
   build flag, CMake packaging, then delete the Python tree and rewrite the docs.

**Parity is the spine.** After every phase, diff C++ output against the frozen
golden JSON over `test/input/` until the diff is empty. Path strings and
per-category scores must be byte-identical; watch sort stability, tie-breaking,
and float→string formatting. Port `test_regression`, `test_search_parity`,
`test_uncapped`, `dp_parity_check`, and `test_midi_parity` as C++ doctest tests.

**Use subagents aggressively** where modules are independent — one agent per leaf
module in Phase 1, one per parser in Phase 2, one per UI view in Phase 5 — and
use independent adversarial verifiers in Phase 3 to hunt for any chart where the
C++ result diverges from the oracle. Keep the Python tree frozen and untouched as
the reference until the final cutover.
