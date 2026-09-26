# Derivation Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every rule the audit found computed in more than one place gets one owner, every display reads the engine's own facts, and the user's own arbitrary rules move into a hydra_rules.ini file.

**Architecture:** The engine stays the source of truth. Facts the display used to re-derive (which chord was squeezed out, which phrases an activation collected, what a backend row is worth, which multiplier a note got) are either stored in the record (format v6, ADR 0014) or computed by one shared function that the engine itself calls. The user's own rule choices live in one `core::Rules` value, loaded from hydra_rules.ini, fingerprinted into every record so a rules change reads Stale instead of showing wrong numbers.

**Tech Stack:** C++20, CMake via build_cpp.ps1, doctest (hydra_tests), hydra_uitest for GUI checks, SQLite record store, Python 3 for the separate video-tools repo.

**Spec:** docs/audit/2026-09-24-derivation-audit.md (findings 1 to 40). Background: docs/handoffs/2026-09-24-backend-scoring-drift.md, CONTEXT.md, docs/adr/0011 and 0013.

## How to read this plan

There are 19 tasks, run in number order. Each one starts with a plain paragraph saying what is wrong today, what changes, and what you will see. Then come the goal, the files, the acceptance checks, one verify command, and the steps. The steps are test-first: write the failing test, watch it fail, make the change, watch it pass, commit.

Tasks 1 and 2 lay the ground: the rules file and the new record format. Tasks 3 to 5 fix the backend row that started all this. Tasks 6 to 11 move each remaining display fact onto its one owner. Tasks 12 to 14 do the same for the parsers and the Preview. Tasks 15 to 18 fix the tools and the docs, and make the code say why each fixed number is what it is. Task 19 checks everything end to end.

Two tasks stop and wait for you. Task 17 starts with a check you do at the game, because nobody knows yet which way Clone Hero applies the audio delay. Tasks 2 and 3 stop and report if the engine gets slower than the agreed limit.

## Global Constraints

Code blocks in a task quote the code as it is on disk today. When an earlier task already changed those lines, apply the same change to the new lines, and keep what the earlier task added. Tasks 1, 8 and 9 all edit `category_scores` in src/core/scoring.cpp, in that order.

The working tree holds uncommitted edits of yours in src/app/path_view.cpp, tests/test_path_view.cpp, tests/test_search.cpp and docs/cap-clamped-squeeze-frontend-anchor.md. Never revert, stash or overwrite them. The tasks are written against those files as they are on disk.

Every commit message ends with these four lines: `Task: <task name>`, `Agent: <executor>`, `Session: <session>`, and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never amend, rebase or reset. Source edits use the Edit tool.

Build with `.\build_cpp.ps1 -Target <target>` from the repo root in PowerShell. Run the tests with `.\build-cpp\Release\hydra_tests.exe`, optionally filtered with `-tc="<name>"`. GUI checks go through hydra_uitest (docs/agents/ui-testing.md), never screenshots.

Every score-neutral task proves it with the same hydra_batch diff. Task 3 writes the baseline file `$env:TEMP\hydra_task3\batch_sorted.txt` before it touches any code, and that file must not be deleted until Task 19 is done. The recipe keeps only the per-chart `[n/N]` lines, strips the counter, and sorts them, because the database line and the elapsed-time line change on every run:

```powershell
.\build-cpp\Release\hydra_batch.exe --db "$out\after.db" testdata\input |
  Select-String '^\[\d+/\d+\] ' | ForEach-Object { $_.Line -replace '^\[\d+/\d+\] ', '' } |
  Sort-Object | Set-Content "$out\batch_sorted.txt"
Compare-Object (Get-Content "$env:TEMP\hydra_task3\batch_sorted.txt") (Get-Content "$out\batch_sorted.txt")
```

The hydra_rules.ini format is fixed by Task 1 and every task uses it. It is flat `key = value` lines with no section header. `#` starts a comment. The keys are the `core::Rules` field names. `auto_cap_ladder` is a comma-separated rising list, and `sqout_rule` is `first_note` or `whole_chord`. A missing file or key means today's value. A bad value or an unknown key is an error that names the key.

Speed limits: the shared backend-pricing function (Task 3) may cost at most 5% of search time, and the collected-phrase bookkeeping (Task 2) at most 3%. Past either limit the task stops and reports to you. No fallback gets built without asking.

**User decisions (already made):**

1. The squeezed-out row the engine counts as 0 shows 0 in Points with the tag "squeezed out (uncounted)".
2. The engine's own search loop also calls the shared backend-pricing function, proven by an identical hydra_batch diff and a timing check.
3. Calibration fill sign: positive means hit early, everywhere.
4. The average multiplier is rounded (py_round3) everywhere, so Song Details changes.
5. Storing the squeezed-out tick and collected phrases is a new record format. Old records read Stale until re-analyzed, with no guessing fallback.
6. hydra_replay's JSON reports the multiplier category_scores actually applied: a per-note multiplier, and a `multiplier_after` on each chord.
7. For 2-3 ms plain backend rows the 3 ms leeway wins everywhere, in the rating and the cap_clamped flag too.
8. The library scan accepts any case for notes.mid, notes.chart and song.ini, through one helper shared with the parser.
9. Preview audio delay and Offset: first a live check at the game, then the Preview honors them.
10. A song with no usable name shows "(unknown)" everywhere: an empty `name =`, a missing name key, and an empty .srb or .sng name.
11. The Preview time box size is read from assets/preview/3d-config.json. Hydra keeps its own font for it.
12. Dynamics count rows get a stamp, a hand-bumped parser counter constant, so older counts are recomputed on next view.
13. The 3 ms leeway, the squeeze-out rule, the fill constants (including the 1/32-beat landing slop), 4 tied paths, the Auto cap ladder and its 120 s budget are your own choices and become configurable in hydra_rules.ini.
14. The file is a new hydra_rules.ini next to the exe, read by the GUI and every CLI tool through `--rules`.
15. Records store the rules fingerprint. A mismatch reads Stale until re-analyzed, and records come back when the rules are switched back.
16. The squeeze-out rule offers `first_note` (today) and `whole_chord` (every note in the chord loses its SP doubling).
17. Fix both repos: hydra_replay and video-tools' fcvideo (multiplier_steps, dynamics pricing), with check_refs staying green. The six uncommitted video-tools edits are committed as-is first.
18. A bad value or unknown key in hydra_rules.ini: the CLIs exit with code 2. The GUI opens, shows the error, and keeps Analyze disabled until the file is fixed.
19. Every backend row the engine does not count shows 0 points and no highlight, including plain "Hard (uncounted)" and "Insane (uncounted)". The label text stays.
20. A typed SqOut offset landing on a chord the engine would never squeeze out is refused, with a message naming that chord and the one the engine would pick.
21. The three test-only squeeze_rating helpers (sp_end_shift_ms, required_frontend_ms, exact_even_split_ms) and their tests are deleted.
22. The 8 batch workers, the Ch10 1/16-beat pad and the combo set 7/8/17/18/27/28 stay fixed, each with a code comment saying why (the combo set's reason is checked against to_multiplier first).
23. Speed limits: 5% for the backend function, 3% for the phrase bookkeeping. Past either, stop and report.
24. `view_difficulty` in the settings INI matches any case.
25. The MIDI reader follows mido on an over-long meta message, but keeps loading a track that is cut off mid-event, as it does today.
26. video-tools' dynamics pricing is fixed too: a mis-hit ghost or accent costs its own note's real bonus, from a new per-note field in the replay JSON.
27. `hydra_replay dump` names the actual reason a record is Stale: a different Hydra build, different rules, or both.
28. video-tools names each mis-hit ghost or accent from its own note (a per-note `dynamic` field), not from the chord. Only the loss text changes, and only on chords that mix a ghost and an accent.

## What others do

Clone Hero's own documentation defines the two audio-offset fields Task 17 honors. The [song.ini guide](https://wiki.clonehero.net/books/guides-and-tutorials/page/songini-guide) describes `delay` in milliseconds. [scan-chart's readme](https://github.com/Geomitron/scan-chart/blob/master/readme.md), the scanner behind Chorus/Encore, lists both `delay` and the .chart `Offset`. Neither says which wins when both are set, which is why Task 17 starts at the game.

[CHOpt](https://github.com/GenericMadScientist/CHOpt), the best-known Clone Hero path optimizer, exposes its judgement calls as user options (squeeze %, early whammy %, lazy whammy, whammy delay, video lag) instead of hard-coding them. That is the same move as hydra_rules.ini.

[ccache](https://ccache.dev/manual/latest.html) keys every cached result on a hash that includes the compiler options, so a changed option can never serve a stale result. The rules fingerprint in each record does the same job for Hydra's store.

---


---

### Task 1: Rules config

Today nine of the user's own rule choices are hard-coded in six files. The 3 ms backend leeway is `kBackendLeewayMs` in core/model.h, and three other files read it. The squeeze-out rule ("only the chord's first note loses its SP doubling") is an `if (i == 0)`, written twice in core/scoring.cpp. The tied-path limit `MAX_TIED_PATHS = 4` sits at the top of search/engine.cpp. The Auto ladder `kSpCapLadder` sits at the top of search/pather.cpp. The 120 s Auto budget is a literal in `Settings::to_analysis_settings` (app/config.cpp). The last four live in parse/song.cpp. Three are locals inside `Song::check_activations`: the fill cooldown (4 measures), how far from a downbeat a generated fill may land (half a beat), and the generated fill's length (half a measure). The fourth is the 1/32-beat slop `fill_lands_on_chord` uses to place authored fills.

This task gathers all nine into one `core::Rules` value. The GUI and every CLI tool read it from a new `hydra_rules.ini` next to the exe. A missing file or a missing key means today's value, so with no file every result stays identical. The sqout rule gains a second choice, `whole_chord`, where every note in the squeezed-out chord loses its SP doubling.

A bad value or an unknown key is an error that names the key. The CLIs print it and exit with code 2. The GUI still opens, shows the error on the main screen, and keeps every Analyze button disabled until the file is fixed and Hydra restarted. It never falls back to the default rules for analysis.

What the user sees: nothing, until they write a `hydra_rules.ini`. With a good file, the chosen rules drive every analysis and the Preview's fills. With a bad file, the main screen shows a red line naming the key, and analysis is off. The fingerprint that makes records from other rules read Stale is Task 2.

The file format is flat `key = value` lines with no section header. A `#` starts a comment, anywhere on a line. The keys are the `core::Rules` field names. `auto_cap_ladder` is a comma-separated rising list, and `sqout_rule` is `first_note` or `whole_chord`.

Two notes on names. Every Hydra source file lives inside `namespace hydra`, so the new header opens `namespace hydra::core`; code inside `hydra` still spells it `core::Rules`. And because `check_activations` has three generated-fill constants, not two, `Rules` gains one field beyond the shared interface: `double fill_length_measures = 0.5`.

**Goal:** One `core::Rules` value, loaded from `hydra_rules.ini` by the GUI and every CLI, drives the leeway, the sqout rule, the tied-path limit, the Auto ladder and budget, and the four fill values, with defaults equal to today's values, and a bad file blocks analysis instead of running the defaults.

**Files:** Creates src/core/rules.h and rules.cpp, src/app/rules_file.h and rules_file.cpp, and tests/test_rules.cpp. Changes the engine side in src/core/model.h and model.cpp, src/core/scoring.h and scoring.cpp, src/core/replay.h and replay.cpp, src/parse/song.h and song.cpp, src/search/graph.h and graph.cpp, src/search/engine.cpp, src/search/pather.h and pather.cpp, src/app/config.h and config.cpp, and src/app/analysis.cpp. Changes the GUI in src/ui/app_state.h and app_state.cpp, src/ui/library_view.cpp, src/ui/details_view.cpp, src/app/preview_source.h and preview_source.cpp, src/ui/preview_load_job.h and preview_load_job.cpp, and src/ui/preview_controller.h and preview_controller.cpp. Changes the CLIs in src/cli/batch.cpp, src/cli/report.cpp, src/cli/fillcompare.cpp, tools/replay.cpp and tools/bench.cpp. Adds cases to tests/test_app_state.cpp, a GUI test to tests/ui/uitest_tests.cpp with its harness support in tests/ui/uitest_harness.h and uitest_harness.cpp, and adds the new sources to CMakeLists.txt.

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="rules*"` passes: defaults, missing and empty file, full file, inline comments, bad values, unknown key, the no-rules fingerprint, and each non-default behavior.
- [ ] `hydra_tests.exe -tc="*hydra_rules.ini*"` passes: a bad file sets `rules_error` naming the key, blocks `start_analyze` and `start_batch`, and no file leaves analysis on.
- [ ] `hydra_uitest.exe --test rules-error` prints `[PASS]`: the error text is on screen, and both "Analyze library" and "Analyze paths!" are disabled.
- [ ] The whole suite `hydra_tests.exe` passes, and `hydra_uitest.exe --all` passes, with no existing test edited.
- [ ] The sorted `[n/N]` lines of `hydra_batch` on testdata\input, before and after this task, with no `hydra_rules.ini` present, have zero differences.
- [ ] `hydra_batch --rules <file holding "max_tied_paths = 0">` exits with code 2 and prints a line containing `max_tied_paths`. `hydra_replay`, `hydra_bench`, `hydra_report` and `hydra_fillcompare` do the same.
- [ ] `git grep -n -E "kBackendLeewayMs|MAX_TIED_PATHS|kSpCapLadder|ACT_COOLDOWN_MEASURES" -- src tools` prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="rules*,*hydra_rules.ini*"` -> `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Record today's batch output, before any edit.**

This is the "before" half of the proof that the defaults change nothing. It runs on the untouched tree, so no second checkout is needed.

```powershell
.\build_cpp.ps1 -Target hydra_batch
Test-Path .\build-cpp\Release\hydra_rules.ini   # must print False
$base = "$env:TEMP\hydra_task1"
New-Item -ItemType Directory -Force $base | Out-Null
Remove-Item "$base\before.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_batch.exe --db "$base\before.db" .\testdata\input > "$base\before_batch.txt"
Get-Content "$base\before_batch.txt" |
    Where-Object { $_ -match '^\[\d+/\d+\]' } |
    ForEach-Object { $_ -replace '^\[\d+/\d+\]\s*', '' } |
    Sort-Object | Set-Content "$base\before_sorted.txt"
```

The filter keeps only the per-chart lines (score, title, best path, or FAILED) and strips the counter, because workers finish in any order.

- [ ] **Step 2: Write the failing tests for the Rules value and the loader.**

Create tests/test_rules.cpp and add it to the test source list in CMakeLists.txt, next to `tests/test_config.cpp`.

```cpp
// Tests for core::Rules and hydra_rules.ini (app/rules_file.h).
//
// Every rule the user chose by hand lives in one Rules value. Defaults must
// equal the values Hydra always used, and each non-default value must
// actually change the behavior it names.

#include "doctest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "app/config.h"
#include "app/rules_file.h"
#include "core/model.h"
#include "core/rules.h"
#include "core/scoring.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/engine.h"
#include "search/graph.h"
#include "search/pather.h"

using namespace hydra;

namespace {

std::filesystem::path write_rules(const char* tag, const std::string& text) {
    std::filesystem::path p =
        std::filesystem::temp_directory_path() / (std::string("hydra_rules_") + tag + ".ini");
    std::ofstream f(p, std::ios::trunc);
    f << text;
    return p;
}

// One measure per note on a 4/4 120 BPM song with no authored fills, so
// check_activations has to generate them.
Song fill_song() {
    Song song(192);
    song.tpm_changes[0] = 768;
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (int64_t m = 0; m < 16; ++m) {
        SongTimestamp ts;
        ts.timecode = song.timecode(m * 768);
        ts.chord.add_note(NoteColor::Red);
        song.sequence.push_back(ts);
    }
    return song;
}

int fill_count(const Song& song) {
    int n = 0;
    for (const SongTimestamp& ts : song.sequence)
        if (ts.has_activation()) ++n;
    return n;
}

}  // namespace

TEST_CASE("rules: defaults are the values Hydra always used") {
    const core::Rules& r = core::default_rules();
    CHECK(r.backend_leeway_ms == 3.0);
    CHECK(r.sqout_rule == core::SqOutRule::FirstNote);
    CHECK(r.max_tied_paths == 4);
    CHECK(r.auto_cap_ladder == std::vector<int>{16, 32, 64, 128, 256, 512});
    CHECK(r.auto_budget_s == 120.0);
    CHECK(r.fill_cooldown_measures == 4);
    CHECK(r.fill_max_distance_beats == 0.5);
    CHECK(r.fill_length_measures == 0.5);
    CHECK(r.fill_land_slop_beats == 1.0 / 32);
}

TEST_CASE("rules: a missing file and an empty file both load the defaults") {
    const uint64_t fp = core::default_rules().fingerprint();
    CHECK(app::load_rules_file(std::filesystem::temp_directory_path() /
                               "hydra_rules_does_not_exist.ini")
              .fingerprint() == fp);
    CHECK(app::load_rules_file(write_rules("empty", "# nothing here\n\n")).fingerprint() == fp);
}

TEST_CASE("rules: every key in the file is read") {
    core::Rules r = app::load_rules_file(write_rules("full",
        "backend_leeway_ms = 5\n"
        "sqout_rule = whole_chord\n"
        "max_tied_paths = 2\n"
        "auto_cap_ladder = 8, 24\n"
        "auto_budget_s = 30\n"
        "fill_cooldown_measures = 2\n"
        "fill_max_distance_beats = 0.25\n"
        "fill_length_measures = 0.25\n"
        "fill_land_slop_beats = 0.125\n"));
    CHECK(r.backend_leeway_ms == 5.0);
    CHECK(r.sqout_rule == core::SqOutRule::WholeChord);
    CHECK(r.max_tied_paths == 2);
    CHECK(r.auto_cap_ladder == std::vector<int>{8, 24});
    CHECK(r.auto_budget_s == 30.0);
    CHECK(r.fill_cooldown_measures == 2);
    CHECK(r.fill_max_distance_beats == 0.25);
    CHECK(r.fill_length_measures == 0.25);
    CHECK(r.fill_land_slop_beats == 0.125);
    CHECK(r.fingerprint() != core::default_rules().fingerprint());
}

TEST_CASE("rules: a # starts a comment anywhere on a line") {
    core::Rules r = app::load_rules_file(write_rules("comments",
        "# my rules\n"
        "max_tied_paths = 2   # fewer ties\n"
        "sqout_rule = whole_chord#no space before the comment\n"));
    CHECK(r.max_tied_paths == 2);
    CHECK(r.sqout_rule == core::SqOutRule::WholeChord);
}

TEST_CASE("rules: a bad value or an unknown key is an error that names the key") {
    auto message_for = [](const char* tag, const std::string& text) -> std::string {
        try {
            app::load_rules_file(write_rules(tag, text));
        } catch (const app::RulesFileError& e) {
            return e.what();
        }
        return "";
    };
    CHECK(message_for("bad1", "max_tied_paths = 0\n").find("max_tied_paths") != std::string::npos);
    CHECK(message_for("bad2", "sqout_rule = every_note\n").find("sqout_rule") != std::string::npos);
    CHECK(message_for("bad3", "backend_leeway_ms = fast\n").find("backend_leeway_ms") !=
          std::string::npos);
    CHECK(message_for("bad4", "auto_cap_ladder = 32, 16\n").find("auto_cap_ladder") !=
          std::string::npos);
    // A typo is an unknown key, never a silent default.
    CHECK(message_for("bad5", "max_tied_path = 4\n").find("max_tied_path") != std::string::npos);
}

TEST_CASE("rules: no rules value has the no-rules fingerprint") {
    // Task 2 opens the store with kNoRulesFingerprint when the file is bad,
    // so no stored row can read Ready. That only works if no real rules
    // value ever hashes to it.
    CHECK(core::default_rules().fingerprint() != core::kNoRulesFingerprint);
    core::Rules other = core::default_rules();
    other.max_tied_paths = 2;
    CHECK(other.fingerprint() != core::kNoRulesFingerprint);
}

TEST_CASE("rules: whole_chord takes every note's SP doubling on a squeeze-out") {
    Chord chord;
    chord.add_note(NoteColor::Red);
    chord.add_note(NoteColor::Blue);
    // Combo 0, so both notes sit at a 1x multiplier and are worth 50 each.
    CHECK(category_scores(chord, 0, nullptr, core::SqOutRule::FirstNote).sqout_reduction == 50);
    CHECK(category_scores(chord, 0, nullptr, core::SqOutRule::WholeChord).sqout_reduction == 100);

    std::vector<CategoryScores> per_note;
    category_scores(chord, 0, &per_note, core::SqOutRule::WholeChord);
    REQUIRE(per_note.size() == 2);
    CHECK(per_note[0].sqout_reduction == 50);
    CHECK(per_note[1].sqout_reduction == 50);
}

TEST_CASE("rules: the leeway moves the Standard edge of a backend rating") {
    BackendSqueeze b;
    b.is_sp = false;
    b.offset_ms = 4.0;
    CHECK(b.summarystr(kDefaultHitWindowMs) == "Hard (uncounted)");
    CHECK(b.summarystr(kDefaultHitWindowMs, 5.0) == "Standard");
}

TEST_CASE("rules: the leeway changes what the engine counts") {
    // A wider leeway can only add backend points and a zero leeway can only
    // remove them. Somewhere in the corpus at least one chart must move.
    core::Rules none = core::default_rules();
    none.backend_leeway_ms = 0.0;
    core::Rules wide = core::default_rules();
    wide.backend_leeway_ms = 50.0;

    bool any_moved = false;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        auto best = [&](const core::Rules& r) {
            ScoreGraph graph(song, 4, FillDeadlineRule::Ch11, r);
            return run_search(graph, DepthMode::Scores, 0, std::nullopt).front().totalscore();
        };
        const int64_t s_none = best(none);
        const int64_t s_default = best(core::default_rules());
        const int64_t s_wide = best(wide);
        CHECK(s_none <= s_default);
        CHECK(s_default <= s_wide);
        if (s_none != s_default || s_default != s_wide) {
            any_moved = true;
            break;
        }
    }
    CHECK(any_moved);
}

TEST_CASE("rules: max_tied_paths caps the tied paths the engine keeps") {
    core::Rules one = core::default_rules();
    one.max_tied_paths = 1;
    int charts = 0;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        ScoreGraph graph(song, 4, FillDeadlineRule::Ch11, one);
        std::vector<Path> paths = run_search(graph, DepthMode::Scores, 0, std::nullopt);
        REQUIRE(!paths.empty());
        CHECK(paths.front().tied_pathcount() == 1);
        if (++charts == 5) break;
    }
    CHECK(charts > 0);
}

TEST_CASE("rules: the Auto ladder and budget come from the rules") {
    SearchSettings settings;
    settings.sp_cap = std::nullopt;
    settings.rules.auto_cap_ladder = {8};
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        HydraRecord record = analyze_chart(song, settings);
        REQUIRE(record.sp_cap.has_value());
        CHECK(*record.sp_cap == 8);
        break;
    }

    app::Settings s;
    s.sp_cap = std::nullopt;
    s.rules.auto_budget_s = 30.0;
    CHECK(s.to_analysis_settings().time_budget_s == std::optional<double>(30.0));
    s.sp_cap = 4;
    CHECK_FALSE(s.to_analysis_settings().time_budget_s.has_value());
}

TEST_CASE("rules: the generated-fill values come from the rules") {
    Song by_default = fill_song();
    by_default.check_activations();
    const int default_fills = fill_count(by_default);
    REQUIRE(default_fills > 0);
    for (const SongTimestamp& ts : by_default.sequence)
        if (ts.has_activation()) CHECK(*ts.activation_length == 384);

    core::Rules tight = core::default_rules();
    tight.fill_cooldown_measures = 2;
    tight.fill_length_measures = 0.25;
    Song with_tight = fill_song();
    with_tight.check_activations(tight);
    CHECK(fill_count(with_tight) > default_fills);
    for (const SongTimestamp& ts : with_tight.sequence)
        if (ts.has_activation()) CHECK(*ts.activation_length == 192);
}
```

- [ ] **Step 3: Write the failing GUI-state tests.**

A bad rules file must block analysis in the app's state, not only on the buttons. So there are two tests: a doctest on `AppState`, and a hydra_uitest check that drives the real screen.

In tests/test_app_state.cpp, add `#include <fstream>` to the includes. `ScratchPaths` also points the rules file at a scratch path, so no test ever reads a real `hydra_rules.ini`. Replace the whole struct with

```cpp
struct ScratchPaths {
    hydra::app::PathOverrides previous;
    std::string ini;
    std::string db;
    std::string rules;

    explicit ScratchPaths(const char* tag)
        : previous(hydra::app::path_overrides()),
          ini(temp_path(tag, ".ini")),
          db(temp_path(tag, ".db")),
          rules(temp_path(tag, "_rules.ini")) {
        std::remove(ini.c_str());
        std::remove(db.c_str());
        std::remove(rules.c_str());
        hydra::app::PathOverrides overrides = previous;
        overrides.ini_path = ini;
        overrides.db_path = db;
        overrides.rules_path = rules;
        hydra::app::set_path_overrides(overrides);
    }

    ~ScratchPaths() {
        hydra::app::set_path_overrides(previous);
        std::remove(ini.c_str());
        std::remove(db.c_str());
        std::remove(rules.c_str());
    }
};
```

Then add at the end of the file:

```cpp
TEST_CASE("a bad hydra_rules.ini names the key and keeps analysis off") {
    ScratchPaths paths("appstate_badrules");
    {
        std::ofstream f(paths.rules);
        f << "max_tied_paths = 0\n";
    }
    seeded_store(paths.db).reset();  // the library and one record, on disk

    // The startup constructor: settings INI, rules file and database from
    // the (scratch) paths, exactly as Hydra.exe starts.
    AppState app;
    CHECK(app.rules_error.find("max_tied_paths") != std::string::npos);
    CHECK(app.analysis_blocked());

    // The buttons are disabled, and the state refuses too, so no other
    // caller can start an analysis on the wrong rules.
    app.selected = library_entry(0);
    app.start_analyze();
    CHECK(app.analyze_job == nullptr);
    app.start_batch(false);
    CHECK(app.batch_job == nullptr);
}

TEST_CASE("no hydra_rules.ini leaves analysis on") {
    ScratchPaths paths("appstate_norules");
    seeded_store(paths.db).reset();
    AppState app;
    CHECK(app.rules_error.empty());
    CHECK_FALSE(app.analysis_blocked());
}
```

In tests/ui/uitest_harness.h, add `std::string rules_path;` to `Harness` right after `std::string ini_path;`, and replace `void reset_app(Harness& h);` with

```cpp
// rules_text: the scratch hydra_rules.ini's contents. Empty (the default)
// means no rules file, so every other test runs today's rules.
void reset_app(Harness& h, const std::string& rules_text = "");
```

In tests/ui/uitest_harness.cpp `init_scratch`, add `h.rules_path = h.temp_dir + "\\hydra_rules.ini";` after the `h.ini_path` line, and `po.rules_path = h.rules_path;` after `po.ini_path = h.ini_path;`. Change the definition's head to `void reset_app(Harness& h, const std::string& rules_text) {`. Right after the block that writes the settings INI (the one ending `f << "depth_value=2\n";  // keep analyses short` and its closing brace), add

```cpp
    if (rules_text.empty()) {
        fs::remove(fs::u8path(h.rules_path), ec);
    } else {
        std::ofstream f(fs::u8path(h.rules_path), std::ios::trunc);
        f << rules_text;
    }
```

In tests/ui/uitest_tests.cpp, add this test before the closing `}  // namespace` of the anonymous namespace:

```cpp
// A bad hydra_rules.ini: the app still opens and scans, the error naming the
// key stays on screen, and both Analyze buttons are disabled.
void test_rules_error(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h, "max_tied_paths = 0\n");
    IM_CHECK(h.app->analysis_blocked());
    IM_CHECK(h.app->rules_error.find("max_tied_paths") != std::string::npos);
    scan_library(ctx);
    if (ctx->IsError()) return;

    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("analysis is off") != std::string::npos;
    }, 5));
    IM_CHECK(visible_text(h).find("max_tied_paths") != std::string::npos);
    IM_CHECK((ctx->ItemInfo("Analyze library").ItemFlags & ImGuiItemFlags_Disabled) != 0);

    open_details(ctx, 0);
    if (ctx->IsError()) return;
    IM_CHECK((ctx->ItemInfo("**/Analyze paths!").ItemFlags & ImGuiItemFlags_Disabled) != 0);
    // The state refuses as well as the button.
    h.app->start_analyze();
    IM_CHECK(h.app->analyze_job == nullptr);
}
```

and add `{"rules-error", test_rules_error},` to the `entries` table in `register_tests`, after `{"dynamics-stored", test_dynamics_stored},`.

- [ ] **Step 4: Build and watch it fail.**

Run `.\build_cpp.ps1 -Target hydra_tests`. It fails to compile, because `core/rules.h`, `app/rules_file.h`, `PathOverrides::rules_path`, `SearchSettings::rules`, `Settings::rules`, `AppState::rules_error`, `AppState::analysis_blocked`, the new `category_scores` and `summarystr` parameters, the new `ScoreGraph` parameter and `check_activations(const core::Rules&)` do not exist yet.

- [ ] **Step 5: Create the Rules value.**

Create src/core/rules.h:

```cpp
// The user's own rule choices, in one place. Every value here is a judgment
// call, not a fact about Clone Hero, so it is configurable through
// hydra_rules.ini (app/rules_file.h). The defaults are the values Hydra always
// used, so an absent file changes nothing. A stored record carries
// fingerprint() of the rules it ran under (docs/adr/0014).

#ifndef HYDRA_CORE_RULES_H
#define HYDRA_CORE_RULES_H

#include <cstdint>
#include <vector>

namespace hydra::core {

// Which notes of a squeezed-out chord lose their SP doubling.
//   FirstNote  -- only the first note in base-sorted order (Hydra's rule so far).
//   WholeChord -- every note in the chord.
enum class SqOutRule { FirstNote, WholeChord };

// The fingerprint of "no usable rules". Rules::fingerprint() never returns
// it, so a store gated on it (a bad hydra_rules.ini) reads no row as Ready.
constexpr uint64_t kNoRulesFingerprint = 0;

struct Rules {
    // A backend note this close after the SP end still scores under SP.
    double backend_leeway_ms = 3.0;
    SqOutRule sqout_rule = SqOutRule::FirstNote;
    // Tied paths the engine folds into one leader before it drops the rest.
    int max_tied_paths = 4;
    // Auto cap: the SP ceilings tried in order, and the seconds before a slow
    // rung is abandoned.
    std::vector<int> auto_cap_ladder{16, 32, 64, 128, 256, 512};
    double auto_budget_s = 120.0;
    // Generated fills (Song::check_activations): the fewest measures between
    // two fills, how far from a downbeat the chosen chord may sit, and the
    // fill's length.
    int fill_cooldown_measures = 4;
    double fill_max_distance_beats = 0.5;
    double fill_length_measures = 0.5;
    // Authored-fill placement (fill_lands_on_chord, both parsers): how close
    // the next chord must be to the fill end to count as the chord the fill
    // lands on.
    double fill_land_slop_beats = 1.0 / 32;

    // A 64-bit hash of every field above. Equal rules give equal fingerprints
    // in every build; any changed field gives a different one. Never
    // kNoRulesFingerprint.
    uint64_t fingerprint() const;
};

// The defaults above, as one shared value.
const Rules& default_rules();

}  // namespace hydra::core

#endif  // HYDRA_CORE_RULES_H
```

Create src/core/rules.cpp. The fingerprint hashes a canonical text form, so it does not depend on struct padding or compiler layout.

```cpp
#include "core/rules.h"

#include <cstdio>
#include <string>

namespace hydra::core {

namespace {

void add_line(std::string& out, const char* key, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s=%.17g\n", key, v);
    out += buf;
}

uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

}  // namespace

uint64_t Rules::fingerprint() const {
    std::string text;
    add_line(text, "backend_leeway_ms", backend_leeway_ms);
    text += sqout_rule == SqOutRule::WholeChord ? "sqout_rule=whole_chord\n"
                                                : "sqout_rule=first_note\n";
    add_line(text, "max_tied_paths", max_tied_paths);
    text += "auto_cap_ladder=";
    for (int cap : auto_cap_ladder) text += std::to_string(cap) + ",";
    text += "\n";
    add_line(text, "auto_budget_s", auto_budget_s);
    add_line(text, "fill_cooldown_measures", fill_cooldown_measures);
    add_line(text, "fill_max_distance_beats", fill_max_distance_beats);
    add_line(text, "fill_length_measures", fill_length_measures);
    add_line(text, "fill_land_slop_beats", fill_land_slop_beats);
    const uint64_t h = fnv1a64(text);
    // 0 is reserved for "no usable rules"; a hash that lands on it moves off.
    return h == kNoRulesFingerprint ? 1 : h;
}

const Rules& default_rules() {
    static const Rules rules;
    return rules;
}

}  // namespace hydra::core
```

Add `src/core/rules.cpp` to the library source list in CMakeLists.txt, next to `src/core/scoring.cpp`.

- [ ] **Step 6: Create the loader.**

Create src/app/rules_file.h:

```cpp
// hydra_rules.ini: the user's rule choices (core/rules.h) as flat
// "key = value" lines. No [section] header. A # starts a comment anywhere on
// a line. A missing file or key keeps the default. A bad value or an unknown
// key is an error that names the key, so a typo can never silently run the
// defaults.

#ifndef HYDRA_APP_RULES_FILE_H
#define HYDRA_APP_RULES_FILE_H

#include <filesystem>
#include <stdexcept>

#include "core/rules.h"

namespace hydra::app {

struct RulesFileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

core::Rules load_rules_file(const std::filesystem::path& path);

// exe_dir()\hydra_rules.ini, or PathOverrides::rules_path when set.
std::filesystem::path default_rules_path();

}  // namespace hydra::app

#endif  // HYDRA_APP_RULES_FILE_H
```

Create src/app/rules_file.cpp:

```cpp
#include "app/rules_file.h"

#include <fstream>
#include <sstream>
#include <string>

#include "app/config.h"

namespace hydra::app {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

[[noreturn]] void bad(const std::string& path, const std::string& key,
                      const std::string& value, const char* want) {
    throw RulesFileError(path + ": " + key + " = \"" + value + "\" is not " + want);
}

double to_double(const std::string& path, const std::string& key, const std::string& v,
                 double min_inclusive) {
    size_t used = 0;
    double d = 0;
    try { d = std::stod(v, &used); } catch (...) { bad(path, key, v, "a number"); }
    if (used != v.size()) bad(path, key, v, "a number");
    if (d < min_inclusive) bad(path, key, v, "in range");
    return d;
}

int to_int(const std::string& path, const std::string& key, const std::string& v,
           int min_inclusive) {
    size_t used = 0;
    int i = 0;
    try { i = std::stoi(v, &used); } catch (...) { bad(path, key, v, "a whole number"); }
    if (used != v.size()) bad(path, key, v, "a whole number");
    if (i < min_inclusive) bad(path, key, v, "in range");
    return i;
}

}  // namespace

core::Rules load_rules_file(const std::filesystem::path& path) {
    core::Rules r;
    std::ifstream f(path);
    if (!f) return r;  // no file: today's rules
    const std::string where = path.u8string();

    std::string line;
    while (std::getline(f, line)) {
        // Everything from a # on is a comment, whole-line or trailing.
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            throw RulesFileError(where + ": \"" + line + "\" is not key = value");
        const std::string key = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));

        if (key == "backend_leeway_ms") r.backend_leeway_ms = to_double(where, key, v, 0.0);
        else if (key == "sqout_rule") {
            if (v == "first_note") r.sqout_rule = core::SqOutRule::FirstNote;
            else if (v == "whole_chord") r.sqout_rule = core::SqOutRule::WholeChord;
            else bad(where, key, v, "first_note or whole_chord");
        }
        else if (key == "max_tied_paths") r.max_tied_paths = to_int(where, key, v, 1);
        else if (key == "auto_cap_ladder") {
            r.auto_cap_ladder.clear();
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                int cap = to_int(where, key, trim(item), 1);
                if (!r.auto_cap_ladder.empty() && cap <= r.auto_cap_ladder.back())
                    bad(where, key, v, "a rising list of caps");
                r.auto_cap_ladder.push_back(cap);
            }
            if (r.auto_cap_ladder.empty()) bad(where, key, v, "a rising list of caps");
        }
        else if (key == "auto_budget_s") {
            r.auto_budget_s = to_double(where, key, v, 0.0);
            if (r.auto_budget_s == 0.0) bad(where, key, v, "above zero");
        }
        else if (key == "fill_cooldown_measures") r.fill_cooldown_measures = to_int(where, key, v, 1);
        else if (key == "fill_max_distance_beats") r.fill_max_distance_beats = to_double(where, key, v, 0.0);
        else if (key == "fill_length_measures") {
            r.fill_length_measures = to_double(where, key, v, 0.0);
            if (r.fill_length_measures == 0.0) bad(where, key, v, "above zero");
        }
        else if (key == "fill_land_slop_beats") r.fill_land_slop_beats = to_double(where, key, v, 0.0);
        else throw RulesFileError(where + ": unknown key \"" + key + "\"");
    }
    return r;
}

std::filesystem::path default_rules_path() {
    if (!path_overrides().rules_path.empty())
        return std::filesystem::u8path(path_overrides().rules_path);
    return std::filesystem::u8path(exe_dir()) / "hydra_rules.ini";
}

}  // namespace hydra::app
```

Add `src/app/rules_file.cpp` to the library source list next to `src/app/config.cpp`.

In src/app/config.h, `PathOverrides` gains a fourth field so tests and hydra_uitest can point at a scratch rules file. Replace

```cpp
struct PathOverrides {
    std::string db_path;
    std::string ini_path;
    std::string asset_dir;
};
```

with

```cpp
struct PathOverrides {
    std::string db_path;
    std::string ini_path;
    std::string asset_dir;
    std::string rules_path;  // hydra_rules.ini (app/rules_file.h)
};
```

- [ ] **Step 7: Carry the rules through the settings.**

In src/search/pather.h, add `#include "core/rules.h"` and a last field to `SearchSettings`, after `bool legacy_fill_deadline = false;`:

```cpp
    // The user's rule choices (hydra_rules.ini). Defaults are today's rules.
    core::Rules rules = core::default_rules();
```

`app::AnalysisSettings` derives from `SearchSettings`, so it gets the field too.

In src/app/config.h, add `#include "core/rules.h"` and a field to `Settings`, after `std::string dm_last_user;`:

```cpp
    // The rules this process runs under, loaded from hydra_rules.ini at
    // startup. Never written to hydra_settings.ini: the rules file is the
    // user's to edit, the app only reads it.
    core::Rules rules = core::default_rules();
```

`save_file` does not change, so the rules never leak into hydra_settings.ini.

In src/app/config.cpp `to_analysis_settings`, replace

```cpp
    s.time_budget_s = sp_cap ? std::nullopt : std::optional<double>(120.0);
    return s;
```

with

```cpp
    s.time_budget_s = sp_cap ? std::nullopt : std::optional<double>(rules.auto_budget_s);
    s.rules = rules;
    return s;
```

- [ ] **Step 8: Make the sqout rule a parameter of category_scores.**

This is the first of three tasks that edit `category_scores` (Tasks 8 and 9 follow). It quotes today's code.

In src/core/scoring.h, add `#include "core/rules.h"` and replace

```cpp
CategoryScores category_scores(const Chord& chord, int combo,
                                std::vector<CategoryScores>* per_note = nullptr);
```

with

```cpp
// sqout_rule decides which notes' SP doubling sqout_reduction takes: only
// note 0 (FirstNote, Hydra's rule so far) or every note (WholeChord).
CategoryScores category_scores(const Chord& chord, int combo,
                                std::vector<CategoryScores>* per_note = nullptr,
                                core::SqOutRule sqout_rule = core::SqOutRule::FirstNote);
```

In src/core/scoring.cpp, change the definition's signature to match. Then replace

```cpp
        // Quick and dirty SqOut calculation -- first note only.
        if (i == 0) {
            sqout_reduction =
                (basevalue + cymb) * combo_multiplier * (is_dynamic ? 2 : 1);
        }
```

with

```cpp
        // SqOut: the notes that lose their SP doubling. FirstNote keeps the
        // original quick calculation (note 0 only); WholeChord takes every note.
        const bool loses_sp = i == 0 || sqout_rule == core::SqOutRule::WholeChord;
        const int note_sqout =
            loses_sp ? (basevalue + cymb) * combo_multiplier * (is_dynamic ? 2 : 1) : 0;
        sqout_reduction += note_sqout;
```

and replace

```cpp
            note_scores.sqout_reduction =
                (i == 0) ? (basevalue + cymb) * combo_multiplier *
                               (is_dynamic ? 2 : 1)
                         : 0;
```

with

```cpp
            note_scores.sqout_reduction = note_sqout;
```

With FirstNote the sum has one non-zero term, so the value is unchanged.

- [ ] **Step 9: Give the graph its rules.**

In src/search/graph.h, add `#include "core/rules.h"` and replace the constructor declaration

```cpp
    ScoreGraph(const Song& song, std::optional<int> sp_meter_cap,
               FillDeadlineRule rule = FillDeadlineRule::Ch11);
```

with

```cpp
    // rules: the user's rule choices; the graph prices squeeze-outs by
    // rules.sqout_rule and hands the rest to the engine through rules().
    ScoreGraph(const Song& song, std::optional<int> sp_meter_cap,
               FillDeadlineRule rule = FillDeadlineRule::Ch11,
               const core::Rules& rules = core::default_rules());
```

Add the accessor `const core::Rules& rules() const { return rules_; }` next to `fill_rule()`, and the member `core::Rules rules_;` right after `FillDeadlineRule rule_ = FillDeadlineRule::Ch11;`. In src/search/graph.cpp, the constructor definition takes the same fourth parameter and its initializer list gains `rules_(rules)`.

In `ScoreGraph::build` (graph.cpp line 113), replace

```cpp
        CategoryScores sg = category_scores(timestamp.chord, combo_);
```

with

```cpp
        CategoryScores sg = category_scores(timestamp.chord, combo_, nullptr, rules_.sqout_rule);
```

- [ ] **Step 10: Give the engine the leeway and the tied-path limit.**

In src/search/engine.cpp, delete `const int32_t MAX_TIED_PATHS = 4;` (line 35). Replace the constructor head

```cpp
    Engine(const Enum& en, bool has_sp_cap, int32_t sp_cap, DepthMode depth_mode,
           int32_t depth_value, bool has_ms_filter, double ms_filter,
           bool no_skips, bool hard_ms_filter,
           const std::vector<int64_t>* target_act_ticks = nullptr)
        : en_(en),
```

with

```cpp
    Engine(const Enum& en, bool has_sp_cap, int32_t sp_cap, DepthMode depth_mode,
           int32_t depth_value, bool has_ms_filter, double ms_filter,
           bool no_skips, bool hard_ms_filter, double backend_leeway_ms,
           int32_t max_tied_paths,
           const std::vector<int64_t>* target_act_ticks = nullptr)
        : en_(en),
          backend_leeway_ms_(backend_leeway_ms),
          max_tied_paths_(max_tied_paths),
```

and add the members right after `const Enum& en_;`:

```cpp
    // hydra_rules.ini: the backend leeway edge and the tied-path fold limit.
    double backend_leeway_ms_;
    int32_t max_tied_paths_;
```

In `create_deactivated_path`, replace

```cpp
        const bool is_leeway = be_offset > 0 && be_offset < kBackendLeewayMs;
```

with

```cpp
        const bool is_leeway = be_offset > 0 && be_offset < backend_leeway_ms_;
```

Task 3 later moves this line into the shared backend-pricing function; it will pass `backend_leeway_ms_` through. In `reduce_group`, replace `if (leader.tied_count + p.tied_count <= MAX_TIED_PATHS) {` with `if (leader.tied_count + p.tied_count <= max_tied_paths_) {`.

In `run_search`, replace

```cpp
    Engine engine(en, has_cap, cap, depth_mode, depth_value,
                  ms_filter.has_value(), ms_filter.value_or(0.0),
                  no_skips, hard_ms_filter, target_act_ticks);
```

with

```cpp
    Engine engine(en, has_cap, cap, depth_mode, depth_value,
                  ms_filter.has_value(), ms_filter.value_or(0.0),
                  no_skips, hard_ms_filter, graph.rules().backend_leeway_ms,
                  static_cast<int32_t>(graph.rules().max_tied_paths),
                  target_act_ticks);
```

The engine reads its rules off the graph, so `run_search`'s own signature, and its callers, do not change.

- [ ] **Step 11: Thread the rules through the pather.**

In src/search/pather.cpp, delete `const int kSpCapLadder[] = {16, 32, 64, 128, 256, 512};` (line 17). `analyze_at_cap` and `analyze_auto_cap` each gain a parameter `const core::Rules& rules` right after `bool legacy_fills`. Every `ScoreGraph` they build passes it. In `analyze_at_cap`, replace

```cpp
    ScoreGraph graph(song, cap,
                     legacy_fills ? FillDeadlineRule::Ch10
                                  : FillDeadlineRule::Ch11);
```

with

```cpp
    ScoreGraph graph(song, cap,
                     legacy_fills ? FillDeadlineRule::Ch10
                                  : FillDeadlineRule::Ch11,
                     rules);
```

Make the same change to the settled-rung graph in `analyze_auto_cap` (line 263), passing `rules`, and to the graph `search_target` builds (line 113), passing `settings.rules`.

In `analyze_auto_cap`, replace `const int ladder_n = static_cast<int>(std::size(kSpCapLadder));` with `const int ladder_n = static_cast<int>(rules.auto_cap_ladder.size());`, and `for (int sp_cap : kSpCapLadder) {` with `for (int sp_cap : rules.auto_cap_ladder) {`. The rung call becomes `analyze_at_cap(song, sp_cap, depth_mode, depth_value, ms_filter, build_cap, legacy_fills, rules, /*want_allzero=*/false, wrapped);`.

The three calls in `analyze_chart` pass `settings.rules` right after `settings.legacy_fill_deadline`. The comment at pather.cpp line 73 says "up to MAX_TIED_PATHS"; change it to "up to Rules::max_tied_paths".

- [ ] **Step 12: Make the four fill values rules.**

In src/parse/song.h, add `#include "core/rules.h"`. Replace `void check_activations();` with `void check_activations(const core::Rules& rules = core::default_rules());`. Give each of the seven loaders a trailing `const core::Rules& rules = core::default_rules()` parameter after `Difficulty difficulty = Difficulty::Expert`.

In src/parse/song.cpp, `Song::check_activations` takes the parameter. Replace

```cpp
    const int64_t ACT_COOLDOWN_MEASURES = 4;
    const int64_t MAX_DISTANCE = tick_resolution_ / 2;
```

with

```cpp
    const int64_t cooldown_measures = rules.fill_cooldown_measures;
    const int64_t max_distance =
        static_cast<int64_t>(tick_resolution_ * rules.fill_max_distance_beats);
```

Rename the two uses below to match: `measure < *last_act_measure + cooldown_measures` and `cell.bestdist <= max_distance`. Replace `sequence[*cell.best].activation_length = cell.pre_tpm / 2;` with

```cpp
            sequence[*cell.best].activation_length =
                static_cast<int64_t>(cell.pre_tpm * rules.fill_length_measures);
```

These give the same integers as today. `tick_resolution_ * 0.5` and `pre_tpm * 0.5` truncate exactly as `/ 2` does for positive values.

`fill_lands_on_chord` gains a trailing `double slop_beats` parameter. Replace `return nextchord_dist <= song.tick_resolution() / 32 &&` with `return nextchord_dist <= static_cast<int64_t>(song.tick_resolution() * slop_beats) &&`. 1/32 is exact in binary, so the default gives today's integer.

`MidiParser` and `ChartParser` each gain a constructor `explicit MidiParser(const core::Rules& rules) : rules_(rules) {}` (and the same for `ChartParser`) and a member `const core::Rules& rules_;`. Their two `fill_lands_on_chord(*song_, *fill_end_tick_, tick)` calls (lines 518 and 924) pass `rules_.fill_land_slop_beats` as the new last argument. Their two `song.check_activations();` calls (lines 598 and 1007) become `song.check_activations(rules_);`. The loaders construct `MidiParser(rules)` and `ChartParser(rules)` in place of `MidiParser()` and `ChartParser()`. `load_songpath` passes `rules` to the four loaders it dispatches to (lines 1147-1150), and the .sng and .srb loaders pass it on to the parser they build.

In src/app/analysis.cpp `analyze_chart_file`, replace

```cpp
    Song song =
        load_songpath(filepath, settings.prodrums, settings.bass2x, settings.difficulty);
```

with

```cpp
    Song song = load_songpath(filepath, settings.prodrums, settings.bass2x,
                              settings.difficulty, settings.rules);
```

- [ ] **Step 13: Give the display and the replay the leeway and the sqout rule.**

In src/core/model.h, delete `constexpr double kBackendLeewayMs = 3.0;` and its comment (lines 62-66). Add `#include "core/rules.h"`. Replace

```cpp
    std::string summarystr(double hit_window_ms = kDefaultHitWindowMs) const;
```

with

```cpp
    // leeway_ms: the backend leeway edge (Rules::backend_leeway_ms); a
    // non-SP row under it rates "Standard".
    std::string summarystr(double hit_window_ms = kDefaultHitWindowMs,
                           double leeway_ms = core::default_rules().backend_leeway_ms) const;
```

In src/core/model.cpp, the definition takes `double leeway_ms`, and `if (off < kBackendLeewayMs) return "Standard";` becomes `if (off < leeway_ms) return "Standard";`. The one caller, `row.rating = bsq.summarystr(W);` in src/app/path_view.cpp line 232, is not touched here. Decision 7's rating change (Tasks 3-5) rewrites it and passes the leeway from the settings' rules. Until then the default argument keeps today's 3 ms.

In src/core/replay.h, replace `ReplayResult replay_path(const Song& song, std::vector<ReplayWindow> windows);` with

```cpp
ReplayResult replay_path(const Song& song, std::vector<ReplayWindow> windows,
                         const core::Rules& rules = core::default_rules());
```

In src/core/replay.cpp, the definition takes the parameter. Line 67 becomes `const CategoryScores sg = category_scores(ts.chord, combo, &per_note, rules.sqout_rule);`. Line 105 replaces `row.ms < w.deact_ms + kBackendLeewayMs;` with `row.ms < w.deact_ms + rules.backend_leeway_ms;`. The replay.h comment at line 15 names the leeway; change it to say "within Rules::backend_leeway_ms".

- [ ] **Step 14: Load the file in every CLI.**

Each CLI takes `--rules <path>`. Without it, the CLI reads `hydra_rules.ini` next to its exe. A bad file prints the loader's message, which names the key, and exits with code 2.

src/cli/batch.cpp. Add `#include "app/rules_file.h"` after `#include "app/config.h"`. Add a header comment line after the `--legacy-fills` line:

```cpp
//     hydra_batch --rules <path>     # rule choices from this file, not the exe's hydra_rules.ini
```

Add `std::optional<std::string> rulespath;` right after `std::optional<std::string> dbpath;`. In the argument loop, after `else if (arg == "--db" && i + 1 < argc) dbpath = argv[++i];`, add

```cpp
        else if (arg == "--rules" && i + 1 < argc) rulespath = argv[++i];
```

Then replace

```cpp
    hydra::app::Settings settings = hydra::app::Settings::load();
    hydra::app::AnalysisSettings analysis = settings.to_analysis_settings();
```

with

```cpp
    hydra::app::Settings settings = hydra::app::Settings::load();
    try {
        settings.rules = hydra::app::load_rules_file(
            rulespath ? std::filesystem::u8path(*rulespath) : hydra::app::default_rules_path());
    } catch (const hydra::app::RulesFileError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
    hydra::app::AnalysisSettings analysis = settings.to_analysis_settings();
```

The header printout does not change, so batch output stays byte-comparable with the Step 1 baseline.

src/cli/report.cpp. Add `#include "app/rules_file.h"` after `#include "app/config.h"`, and the header comment line `//     hydra_report --rules <path>    # the rules the records must match (Task 2)` after the `--db` line. Add `std::optional<std::string> rulespath;` after `std::optional<std::string> dbpath;`. After `else if (arg == "--db" && i + 1 < argc) dbpath = argv[++i];`, add

```cpp
        else if (arg == "--rules" && i + 1 < argc) rulespath = argv[++i];
```

Right after `hydra::app::Settings settings = hydra::app::Settings::load();`, add

```cpp
    try {
        settings.rules = hydra::app::load_rules_file(
            rulespath ? std::filesystem::u8path(*rulespath) : hydra::app::default_rules_path());
    } catch (const hydra::app::RulesFileError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
```

hydra_report never analyzes. It loads the rules so a bad file fails loudly, and so Task 2 can open the store with their fingerprint.

src/cli/fillcompare.cpp. Add `#include "app/rules_file.h"` after `#include "app/config.h"`, and a header comment line after the `--no-open` example:

```cpp
//     hydra_fillcompare --old ch10.db --new ch11.db --rules hydra_rules.ini
```

Add `std::optional<std::string> rulespath;` right after `bool open_when_done = true;`. Replace the argument loop

```cpp
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--old" && i + 1 < argc) old_path = argv[++i];
        else if (arg == "--new" && i + 1 < argc) new_path = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out = argv[++i];
        else if (arg == "--no-open") open_when_done = false;
        else {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return 2;
        }
    }

    if (!old_path || !new_path) {
        std::fprintf(stderr,
            "Usage: hydra_fillcompare --old <ch10.db> --new <ch11.db> "
            "[--out fill_compare.html] [--no-open]\n");
        return 2;
    }

    hydra::app::Settings settings = hydra::app::Settings::load();
```

with

```cpp
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--old" && i + 1 < argc) old_path = argv[++i];
        else if (arg == "--new" && i + 1 < argc) new_path = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out = argv[++i];
        else if (arg == "--rules" && i + 1 < argc) rulespath = argv[++i];
        else if (arg == "--no-open") open_when_done = false;
        else {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return 2;
        }
    }

    if (!old_path || !new_path) {
        std::fprintf(stderr,
            "Usage: hydra_fillcompare --old <ch10.db> --new <ch11.db> "
            "[--out fill_compare.html] [--no-open] [--rules hydra_rules.ini]\n");
        return 2;
    }

    hydra::app::Settings settings = hydra::app::Settings::load();
    try {
        settings.rules = hydra::app::load_rules_file(
            rulespath ? std::filesystem::u8path(*rulespath) : hydra::app::default_rules_path());
    } catch (const hydra::app::RulesFileError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
```

Both databases hold results of the same rules question, so one rules value serves both stores (Task 2).

tools/replay.cpp. Add `#include "app/rules_file.h"` after `#include "app/config.h"`. `struct Args` gains two fields after `bool legacy_fills = false;`:

```cpp
    std::string rules_path;  // --rules; empty = hydra_rules.ini next to the exe
    core::Rules rules;       // loaded once in main, before any command runs
```

In `usage()`, add this line right before `"JSON is printed compact by default; --pretty indents it.\n"`:

```cpp
        "Every command takes --rules <file>: the rule choices to price under\n"
        "(default: hydra_rules.ini next to the exe). A bad file exits with 2.\n"
```

In main's option chain, after `else if (k == "--db") a.db = next();`, add `else if (k == "--rules") a.rules_path = next();`. Right after the option loop's closing brace, before `try {`, add

```cpp
    try {
        a.rules = app::load_rules_file(a.rules_path.empty()
                                           ? app::default_rules_path()
                                           : std::filesystem::u8path(a.rules_path));
    } catch (const app::RulesFileError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
```

In `settings_from`, add `s.rules = a.rules;` right before `return s;`. Every command builds its settings through `settings_from`, so `to_analysis_settings()` carries the rules into `analyze_chart` and `search_target`. The four chart loads pass the rules too. At lines 365-366 and 643-644, replace

```cpp
    Song song = load_songpath(a.chart, s.view_prodrums, s.effective_bass2x(),
                              s.difficulty());
```

with

```cpp
    Song song = load_songpath(a.chart, s.view_prodrums, s.effective_bass2x(),
                              s.difficulty(), s.rules);
```

At lines 522-523, replace `s.effective_bass2x(), s.difficulty());` with `s.effective_bass2x(), s.difficulty(), s.rules);`. At lines 581-582, make the same change. At line 375, replace `const ReplayResult r = replay_path(song, windows);` with `const ReplayResult r = replay_path(song, windows, s.rules);`.

`check_chart` is the selfcheck worker. It builds its own settings, so it takes the rules as a parameter. Replace

```cpp
void check_chart(const std::string& path, Tally* tally) {
    // The GUI's defaults, straight from app::Settings rather than five
    // hand-written literals.
    const app::AnalysisSettings cfg = app::Settings().to_analysis_settings();

    std::optional<Song> song_opt;
    try {
        song_opt.emplace(load_songpath(path, cfg.prodrums, cfg.bass2x, cfg.difficulty));
```

with

```cpp
void check_chart(const std::string& path, const core::Rules& rules, Tally* tally) {
    // The GUI's defaults, straight from app::Settings rather than five
    // hand-written literals, under the rules this run loaded.
    app::Settings defaults;
    defaults.rules = rules;
    const app::AnalysisSettings cfg = defaults.to_analysis_settings();

    std::optional<Song> song_opt;
    try {
        song_opt.emplace(
            load_songpath(path, cfg.prodrums, cfg.bass2x, cfg.difficulty, cfg.rules));
```

At line 739, replace `const ReplayResult r = replay_path(song, windows);` with `const ReplayResult r = replay_path(song, windows, rules);`. In `cmd_selfcheck`, the two calls become `check_chart(a.chart, a.rules, &tally);` and `check_chart(p, a.rules, &tally);`. The engine and the replay then price under the same rules, so selfcheck stays a fair comparison under any rules file.

tools/bench.cpp has no general argument loop. Its `main` picks a mode from fixed argv positions (`--dump-db`, `--scan`, a folder, or nothing). So `--rules <path>` is taken out of argv first, and the mode checks see the argv they always did. Add `#include <filesystem>` to the standard includes and `#include "app/rules_file.h"` after `#include "app/analysis.h"`. Add, after `using clk = std::chrono::steady_clock;`:

```cpp
// hydra_rules.ini (or --rules <path>): the rule choices every mode runs under.
static core::Rules g_rules;
```

Replace

```cpp
int main(int argc, char** argv) {
    if (argc > 3 && std::string(argv[1]) == "--dump-db") {
```

with

```cpp
int main(int argc, char** argv) {
    // --rules <path> may sit anywhere; take it out so the positional mode
    // checks below see the same argv they always did.
    std::vector<char*> args;
    std::string rules_path;
    for (int i = 0; i < argc; ++i) {
        if (i > 0 && std::string(argv[i]) == "--rules" && i + 1 < argc) {
            rules_path = argv[++i];
            continue;
        }
        args.push_back(argv[i]);
    }
    try {
        g_rules = app::load_rules_file(rules_path.empty()
                                           ? app::default_rules_path()
                                           : std::filesystem::u8path(rules_path));
    } catch (const app::RulesFileError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
    argc = static_cast<int>(args.size());
    argv = args.data();

    if (argc > 3 && std::string(argv[1]) == "--dump-db") {
```

The two chart loads, `load_songpath(it.notespath, true, true)` (line 68) and `load_songpath(path, true, true)` (line 193), become `load_songpath(it.notespath, true, true, Difficulty::Expert, g_rules)` and `load_songpath(path, true, true, Difficulty::Expert, g_rules)`. Both `SearchSettings settings;` blocks (lines 78-83 and 200-204) gain `settings.rules = g_rules;` after their last field.

- [ ] **Step 15: Load the file in the GUI, and block analysis on a bad file.**

The GUI never analyzes on rules the user did not choose. A bad file leaves the app usable for browsing, scanning and previewing, but every analysis entry point is off.

In src/ui/app_state.h, add `#include "core/rules.h"`. Declare, in `hydra::ui` right above `class AppState`:

```cpp
// What the app reads before it opens the store: the settings, with
// hydra_rules.ini already loaded, and the loader's error if the file was bad.
struct StartupSettings {
    app::Settings settings;
    std::string rules_error;
};
```

In `AppState`'s public section, after `std::unique_ptr<store::RecordStore> store;`, add

```cpp
    // Set at startup when hydra_rules.ini is bad (the loader's message, which
    // names the key). While set, analysis is off: the Analyze buttons are
    // disabled and start_batch/start_analyze do nothing. It clears only on a
    // restart with a fixed file; there is no fallback to the default rules.
    std::string rules_error;
    bool analysis_blocked() const { return !rules_error.empty(); }
```

In the private section, add `explicit AppState(StartupSettings start);` before `ID3D11Device* render_device_ = nullptr;`.

In src/ui/app_state.cpp, add `#include "app/rules_file.h"` and replace

```cpp
AppState::AppState() : AppState(Settings::load(), app::open_store(app::db_path())) {}
```

with

```cpp
namespace {

// Settings plus hydra_rules.ini. A bad file is not fatal: the app still
// opens so the user can read the error, but analysis stays off.
StartupSettings load_startup_settings() {
    StartupSettings start{Settings::load(), {}};
    try {
        start.settings.rules = app::load_rules_file(app::default_rules_path());
    } catch (const app::RulesFileError& e) {
        start.rules_error = e.what();
    }
    return start;
}

}  // namespace

AppState::AppState() : AppState(load_startup_settings()) {}

AppState::AppState(StartupSettings start)
    : AppState(start.settings, app::open_store(app::db_path())) {
    rules_error = std::move(start.rules_error);
}
```

Task 2 adds the fingerprint argument to that `open_store` call.

In `start_batch`, add `if (analysis_blocked()) return;` as the first line. In `start_analyze`, add `if (analysis_blocked()) return;` as the first line.

In src/ui/library_view.cpp `render_actions_row`, replace

```cpp
    begin_disabled_button(analyzable == 0);
    if (button_in_slot(label, analyze_w)) {
```

with

```cpp
    // Also off while hydra_rules.ini is bad: no analysis on rules the user
    // did not choose.
    const bool analyze_off = analyzable == 0 || app.analysis_blocked();
    begin_disabled_button(analyze_off);
    if (button_in_slot(label, analyze_w)) {
```

and `end_disabled_button(analyzable == 0);` with `end_disabled_button(analyze_off);`. Then replace

```cpp
    render_status_line(app, /*same_line=*/true);
    render_folder_manager(app);
```

with

```cpp
    render_status_line(app, /*same_line=*/true);
    // A bad rules file is not a passing message: it stays on screen, under
    // the action row, for as long as analysis is off.
    if (app.analysis_blocked()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarningColor,
                           "hydra_rules.ini has an error, so analysis is off until the "
                           "file is fixed and Hydra is restarted.");
        ImGui::TextColored(kWarningColor, "%s", app.rules_error.c_str());
        ImGui::PopTextWrapPos();
    }
    render_folder_manager(app);
```

The fading status line is not used for this, because it disappears after 6 seconds.

In src/ui/details_view.cpp, replace

```cpp
    bool analyze_disabled = !file_ok || (app.analyze_job && !app.analyze_job->finished());
```

with

```cpp
    if (app.analysis_blocked())
        ImGui::TextColored(kWarningColor,
                           "Analysis is off until hydra_rules.ini is fixed and Hydra is "
                           "restarted.");
    bool analyze_disabled = app.analysis_blocked() || !file_ok ||
                            (app.analyze_job && !app.analyze_job->finished());
```

The Preview re-parses the chart, and the fill rules decide where its fills sit. So the Preview gets the rules too, and its fills match the analyzed ones. In src/app/preview_source.h, add `#include "core/rules.h"` and replace

```cpp
PreviewSource resolve_preview_source(const std::string& notespath, bool pro,
                                     bool bass2x,
                                     Difficulty difficulty = Difficulty::Expert);
```

with

```cpp
PreviewSource resolve_preview_source(const std::string& notespath, bool pro,
                                     bool bass2x,
                                     Difficulty difficulty = Difficulty::Expert,
                                     const core::Rules& rules = core::default_rules());
```

and extend its comment: "rules places the fills the same way analysis does." In src/app/preview_source.cpp, the definition takes `const core::Rules& rules`, and `load_songpath(notespath, pro, bass2x, difficulty)` becomes `load_songpath(notespath, pro, bass2x, difficulty, rules)`.

In src/ui/preview_load_job.h, the constructor becomes

```cpp
    PreviewLoadJob(store::ChartLibraryEntry entry, bool pro, bool bass2x,
                   Difficulty difficulty, std::optional<Path> path, int sp_cap,
                   core::Rules rules = core::default_rules());
```

and the member `core::Rules rules_;  // copied: the job outlives the caller's settings` goes after `int sp_cap_;`. In src/ui/preview_load_job.cpp, the definition takes `core::Rules rules`, the initializer list gains `rules_(std::move(rules))` after `sp_cap_(sp_cap)`, and the `resolve_preview_source(entry_.notespath, pro_, bass2x_, difficulty_)` call gains `, rules_`.

In src/ui/preview_controller.h, `open` becomes

```cpp
    void open(const store::ChartLibraryEntry& entry, bool pro, bool bass2x,
              Difficulty difficulty, const Path* path, int sp_cap,
              const core::Rules& rules = core::default_rules());
```

In src/ui/preview_controller.cpp, the definition takes the same parameter, and `std::make_unique<PreviewLoadJob>(entry, pro, bass2x, difficulty, path_, sp_cap_)` becomes `std::make_unique<PreviewLoadJob>(entry, pro, bass2x, difficulty, path_, sp_cap_, rules)`. The caller in src/ui/details_view.cpp,

```cpp
    pc->open(*app.selected, app.settings.view_prodrums, app.settings.effective_bass2x(),
             app.settings.difficulty(), selected_path, sp_cap);
```

becomes

```cpp
    pc->open(*app.selected, app.settings.view_prodrums, app.settings.effective_bass2x(),
             app.settings.difficulty(), selected_path, sp_cap, app.settings.rules);
```

Under a bad rules file, `app.settings.rules` holds the defaults, so the Preview draws default fills. That is display only, not analysis, and the error line is already on screen. The Dynamics tab's parse (`DynamicsLoadJob`) is left alone: it counts ghosts and accents per pad, which fills do not change.

- [ ] **Step 16: Build and run the new tests.**

```powershell
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe -tc="rules*,*hydra_rules.ini*"
.\build_cpp.ps1 -Target hydra_uitest
.\build-cpp\Release\hydra_uitest.exe --test rules-error
```

Expect `Status: SUCCESS!` from the first, and `[PASS]` for rules-error from the second.

- [ ] **Step 17: Prove the defaults change nothing, and that every CLI refuses a bad file.**

Run the whole suite, `.\build-cpp\Release\hydra_tests.exe`, and expect `Status: SUCCESS!`. Run `.\build-cpp\Release\hydra_uitest.exe --all` and expect every test `[PASS]`.

Then repeat Step 1's batch run on this build and compare:

```powershell
.\build_cpp.ps1 -Target hydra_batch
Test-Path .\build-cpp\Release\hydra_rules.ini   # must print False
$base = "$env:TEMP\hydra_task1"
Remove-Item "$base\after.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_batch.exe --db "$base\after.db" .\testdata\input > "$base\after_batch.txt"
Get-Content "$base\after_batch.txt" |
    Where-Object { $_ -match '^\[\d+/\d+\]' } |
    ForEach-Object { $_ -replace '^\[\d+/\d+\]\s*', '' } |
    Sort-Object | Set-Content "$base\after_sorted.txt"
Compare-Object (Get-Content "$base\before_sorted.txt") (Get-Content "$base\after_sorted.txt")
```

`Compare-Object` must print nothing. Any line it prints is a chart whose score or best path moved, and the task is not done.

Last, check the refusal in every CLI:

```powershell
.\build_cpp.ps1 -Target hydra_replay; .\build_cpp.ps1 -Target hydra_bench
$bad = "$env:TEMP\hydra_task1\bad_rules.ini"
Set-Content $bad "max_tied_paths = 0"
.\build-cpp\Release\hydra_batch.exe --rules $bad --db "$env:TEMP\hydra_task1\bad.db"; $LASTEXITCODE
.\build-cpp\Release\hydra_report.exe --rules $bad --no-open; $LASTEXITCODE
.\build-cpp\Release\hydra_fillcompare.exe --old a.db --new b.db --rules $bad --no-open; $LASTEXITCODE
.\build-cpp\Release\hydra_replay.exe selfcheck --rules $bad; $LASTEXITCODE
.\build-cpp\Release\hydra_bench.exe --rules $bad; $LASTEXITCODE
```

Each prints a line ending `max_tied_paths = "0" is not in range`, then `2`. None of them opens a database first, so no scratch file is created.

- [ ] **Step 18: Commit.**

```
git add src/core/rules.h src/core/rules.cpp src/app/rules_file.h src/app/rules_file.cpp tests/test_rules.cpp CMakeLists.txt src/core/model.h src/core/model.cpp src/core/scoring.h src/core/scoring.cpp src/core/replay.h src/core/replay.cpp src/parse/song.h src/parse/song.cpp src/search/graph.h src/search/graph.cpp src/search/engine.cpp src/search/pather.h src/search/pather.cpp src/app/config.h src/app/config.cpp src/app/analysis.cpp src/app/preview_source.h src/app/preview_source.cpp src/ui/app_state.h src/ui/app_state.cpp src/ui/library_view.cpp src/ui/details_view.cpp src/ui/preview_load_job.h src/ui/preview_load_job.cpp src/ui/preview_controller.h src/ui/preview_controller.cpp src/cli/batch.cpp src/cli/report.cpp src/cli/fillcompare.cpp tools/replay.cpp tools/bench.cpp tests/test_app_state.cpp tests/ui/uitest_harness.h tests/ui/uitest_harness.cpp tests/ui/uitest_tests.cpp
git commit -m "Read the user's rule choices from hydra_rules.ini

The backend leeway, the squeeze-out rule (first_note or whole_chord), the
tied-path limit, the Auto cap ladder and budget, and the four fill values
now come from one core::Rules value. The GUI and every CLI load it from
hydra_rules.ini next to the exe; the CLIs also take --rules <path>. A
missing file or key means today's value, so results are unchanged without
a file (hydra_batch before/after: zero differences).

A bad value or unknown key names the key. The CLIs exit with 2. The GUI
opens, shows the error, and keeps analysis off until the file is fixed and
Hydra restarted; it never analyzes on the default rules instead.

Task: Rules config
Agent: <executor>
Session: <session>

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 2: Record format v6

Today a stored activation cannot say which phrase it squeezed out, or which SP phrases it collected while active. The engine knows both during the search, then throws them away. The display layer then has to guess, which is the re-derivation the audit keeps flagging. A record also cannot say which rules it was analyzed under, so a record from before a `hydra_rules.ini` edit would still read Ready.

This task bumps the record format to v6, the way v5 did in commit 396082a (ADR 0013). Each `Activation` gains `sqout_tick` and `collected_phrase_ticks`, stamped by the engine at copy-out. Each record gains `rules_fingerprint`. A row whose fingerprint differs from the running rules reads Stale through the same gate an old format already uses.

What the user sees: every record stored before this build reads Stale until it is re-analyzed, with no guessing fallback (decision 5). After the user edits `hydra_rules.ini`, every record from the old rules reads Stale too (decision 15). Switching the rules back makes those records Ready again, because they are still in the database. When the rules file is bad, the GUI opens its store with a fingerprint no rules can have, so no row reads Ready under the wrong rules (decision 18). `hydra_replay dump` now says why a row is Stale. A row from another build keeps today's line, "was written by Hydra X, not this build". A row from this build under other rules says "was analyzed with different rules (hydra_rules.ini changed)". A row that is stale for both reasons prints both lines.

A note on the "record header". The store never saves the nested `write_record` blob. What it saves is the flat structure blob that `flatten_record` writes (src/store/path_codec.cpp), plus content-addressed path nodes. So the fingerprint goes into the structure blob, right after its format version. `write_record` also gains it, because tests and the equality proxy still use that blob.

How the collected phrases reach copy-out. `extend_deacts` (graph.cpp) runs at graph build time and records, per SP phrase on each advance edge, where every pending SP end moves. The engine already walks that list in `advance()`, one phrase at a time, when a path on the SP track crosses the edge. That walk is the one place that knows "this path collected this phrase". So the engine records each phrase tick there, in a small linked arena like the squeeze arena (`SqNode`), and hands the chain to copy-out the same way `sq_tail` travels. A squeeze-out drops the phrase at `sqinout_time` and every later one, so `create_deactivated_path` trims the chain back to before that tick.

Two kinds of phrase count as collected, because they are what the gauge really received. A late-SqIn phrase lands after the deactivation node, and `advance()` calls it "buffered"; it still counts. A phrase that arrives with the meter full extends the window only up to the cap; it still counts too, and `clamp_tick` separately says it was the pinning note.

The arena runs in the search's hottest loop, so this task has a speed limit. If `hydra_bench` shows either timed config more than 3% slower than before, the executor stops and reports the numbers to the user. No cheaper fallback is built unasked.

**Goal:** Records carry `sqout_tick`, `collected_phrase_ticks` and `rules_fingerprint`, stamped by the engine and the pather, and anything older, analyzed under other rules, or opened under a bad rules file reads Stale.

**Files:** Changes src/core/model.h, src/store/serialize.h and serialize.cpp, src/store/path_binary.cpp, src/store/path_codec.h and path_codec.cpp, src/store/record_store.h and record_store.cpp, src/search/engine.cpp, src/search/pather.cpp, src/app/config.h and config.cpp, src/ui/app_state.cpp, src/cli/batch.cpp, src/cli/report.cpp, src/cli/fillcompare.cpp, tools/replay.cpp and tools/bench.cpp. Adds cases to tests/test_store.cpp, tests/test_path_codec.cpp, tests/test_search.cpp and tests/test_app_state.cpp, and creates docs/adr/0014-squeeze-out-and-collected-phrases-and-rules-are-stored.md.

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="record blob: a v5 write drops*"` passes, and a v5 blob's fingerprint reads `core::kNoRulesFingerprint`.
- [ ] `hydra_tests.exe -tc="a row analyzed under other rules*"` passes.
- [ ] `hydra_tests.exe -tc="under a bad hydra_rules.ini no stored record reads Ready"` passes.
- [ ] `hydra_tests.exe -tc="a Stale lookup says why*"` passes: other rules set only `stale_rules`, another build sets only `stale_build`, both set both, and a Ready row sets neither.
- [ ] `hydra_tests.exe -tc="collected phrases*"` passes (three fixtures plus the corpus sweep).
- [ ] `hydra_tests.exe -tc="path codec: a missing node or a bad structure blob throws"` passes with `kPathStructureFormatVersion == 4` and a refused version-3 blob.
- [ ] The whole suite `hydra_tests.exe` passes.
- [ ] The sorted `[n/N]` lines of `hydra_batch` on testdata\input, before and after this task, have zero differences.
- [ ] The best-of-two `hydra_bench` times for "cap4 d4" and "auto d4" after this task are each at most 1.03 times the before times. Past that, the task stops and reports.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` -> `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Record today's batch output and timing, before any edit.**

This runs on Task 1's committed tree, so no second checkout is needed.

```powershell
.\build_cpp.ps1 -Target hydra_batch; .\build_cpp.ps1 -Target hydra_bench
Test-Path .\build-cpp\Release\hydra_rules.ini   # must print False
$base = "$env:TEMP\hydra_task2"
New-Item -ItemType Directory -Force $base | Out-Null
Remove-Item "$base\before.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_batch.exe --db "$base\before.db" .\testdata\input > "$base\before_batch.txt"
Get-Content "$base\before_batch.txt" |
    Where-Object { $_ -match '^\[\d+/\d+\]' } |
    ForEach-Object { $_ -replace '^\[\d+/\d+\]\s*', '' } |
    Sort-Object | Set-Content "$base\before_sorted.txt"
# Two timing runs, one after the other, nothing else running.
.\build-cpp\Release\hydra_bench.exe > "$base\bench_before_1.txt"
.\build-cpp\Release\hydra_bench.exe > "$base\bench_before_2.txt"
```

`hydra_bench` with no argument times the corpus at cap 4 and at Auto, depth 4, and prints lines such as `  cap4 d4 : 1.23s (best of 3)`. Those two lines are the timing gate in Step 14.

- [ ] **Step 2: Write the failing serialize test.**

Add to tests/test_store.cpp, after "record blob: a v4 write drops clamp_tick, a v5 write keeps it". Add `#include "core/rules.h"` at the top if it is not there.

```cpp
TEST_CASE("record blob: a v5 write drops sqout_tick and collected_phrase_ticks, a v6 write keeps them") {
    Activation act;
    act.timecode = Timecode::raw(960);
    act.deact_tick = 7680;
    act.sqout_tick = 7488;
    act.collected_phrase_ticks = {1920, 3840};
    Path path;
    path.activations.push_back(act);
    HydraRecord record;
    record.sp_cap = 4;
    record.rules_fingerprint = 0x0123456789abcdefull;
    record.paths.push_back(path);

    HydraRecord v6 = read_record(write_record(record, 6));
    const Activation& a6 = v6.paths.at(0).activations.at(0);
    REQUIRE(a6.sqout_tick.has_value());
    CHECK(*a6.sqout_tick == 7488);
    CHECK(a6.collected_phrase_ticks == std::vector<int64_t>{1920, 3840});
    CHECK(v6.rules_fingerprint == 0x0123456789abcdefull);

    // A v5 blob has none of the three. They read back empty, and the
    // fingerprint reads kNoRulesFingerprint, which no Rules value produces,
    // so an old record can never pass as analyzed under the current rules.
    HydraRecord v5 = read_record(write_record(record, 5));
    const Activation& a5 = v5.paths.at(0).activations.at(0);
    CHECK_FALSE(a5.sqout_tick.has_value());
    CHECK(a5.collected_phrase_ticks.empty());
    CHECK(v5.rules_fingerprint == core::kNoRulesFingerprint);
    // v5 still keeps what v5 always kept.
    REQUIRE(a5.deact_tick.has_value());
    CHECK(*a5.deact_tick == 7680);

    CHECK_THROWS_AS(write_record(record, kBlobFormatVersion + 1), SerializeError);
}
```

- [ ] **Step 3: Write the failing store test.**

Add to tests/test_store.cpp, after "a row in an older path format is Stale even when this build stamped it".

```cpp
TEST_CASE("a row analyzed under other rules reads Stale until the rules match again") {
    core::Rules other = core::default_rules();
    other.max_tied_paths = 2;
    const RecordKey key{"h", "mode", CapQuery::at(8)};

    // A store running the default rules sees a row stamped with other rules
    // as Stale everywhere a lookup can ask.
    {
        RecordStore store(":memory:");
        store.add_song("h", "Song", "Artist", "Charter", fixture().song);
        HydraRecord foreign = at_cap(8);
        foreign.rules_fingerprint = other.fingerprint();
        store.add_row(prepare_row(key, foreign));
        CHECK_FALSE(store.has_record(key));
        CHECK(store.get_record(key).status == RecordStatus::Stale);
        CHECK(store.get_summary(key).status == RecordStatus::Stale);
        CHECK(store.list_records(std::nullopt, CapQuery::at(8), Lens{}, SortColumn::Score, true)
                  .empty());
    }

    // The same row reads Ready again once the store runs those rules.
    const std::string db = temp_db("rules_fp");
    {
        RecordStore store(db);
        store.add_song("h", "Song", "Artist", "Charter", fixture().song);
        store.add_record(key, at_cap(8));
        CHECK(store.get_record(key).status == RecordStatus::Ready);
    }
    {
        RecordStore store(db, other.fingerprint());
        CHECK(store.get_record(key).status == RecordStatus::Stale);
        CHECK_FALSE(store.has_record(key));
    }
    {
        // A store gated on "no usable rules" (a bad hydra_rules.ini) reads
        // nothing as Ready.
        RecordStore store(db, core::kNoRulesFingerprint);
        CHECK(store.get_record(key).status == RecordStatus::Stale);
    }
    {
        RecordStore store(db);
        CHECK(store.get_record(key).status == RecordStatus::Ready);
    }
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(db), ec);
}

TEST_CASE("a Stale lookup says why: another build, other rules, or both") {
    core::Rules other = core::default_rules();
    other.max_tied_paths = 2;
    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", fixture().song);

    // This build, other rules.
    const RecordKey rules_key{"h", "rules", CapQuery::at(8)};
    HydraRecord foreign = at_cap(8);
    foreign.rules_fingerprint = other.fingerprint();
    store.add_row(prepare_row(rules_key, foreign));
    RecordLookup by_rules = store.get_record(rules_key);
    CHECK(by_rules.status == RecordStatus::Stale);
    CHECK(by_rules.stale_rules);
    CHECK_FALSE(by_rules.stale_build);

    // Another build, these rules.
    const RecordKey build_key{"h", "build", CapQuery::at(8)};
    PreparedRow old_build = prepare_row(build_key, at_cap(8));
    old_build.hyversion = "0.0.0";
    store.add_row(old_build);
    RecordLookup by_build = store.get_record(build_key);
    CHECK(by_build.status == RecordStatus::Stale);
    CHECK(by_build.stale_build);
    CHECK_FALSE(by_build.stale_rules);

    // Another build and other rules: both reasons.
    const RecordKey both_key{"h", "both", CapQuery::at(8)};
    PreparedRow both = prepare_row(both_key, foreign);
    both.hyversion = "0.0.0";
    store.add_row(both);
    RecordLookup by_both = store.get_record(both_key);
    CHECK(by_both.status == RecordStatus::Stale);
    CHECK(by_both.stale_build);
    CHECK(by_both.stale_rules);

    // A Ready row carries no reason.
    const RecordKey ready_key{"h", "ready", CapQuery::at(8)};
    store.add_record(ready_key, at_cap(8));
    RecordLookup ready = store.get_record(ready_key);
    CHECK(ready.status == RecordStatus::Ready);
    CHECK_FALSE(ready.stale_build);
    CHECK_FALSE(ready.stale_rules);
}
```

`hydra_replay dump` prints one line per reason these two flags report (Step 10), so this test pins which lines it prints.

- [ ] **Step 4: Write the failing GUI-state test.**

Add at the end of tests/test_app_state.cpp. It uses the `ScratchPaths` rules member and `#include <fstream>` from Task 1.

```cpp
TEST_CASE("under a bad hydra_rules.ini no stored record reads Ready") {
    ScratchPaths paths("appstate_badrules_stale");
    seeded_store(paths.db).reset();
    const RecordKey seeded{library_entry(0).md5, kChartMode, CapQuery::at(kSeededCap),
                           Settings{}.lens()};

    // Good (absent) rules file: the seeded record, written under the
    // default rules, is Ready.
    {
        AppState good;
        CHECK(good.store->get_summary(seeded).status == RecordStatus::Ready);
    }

    // Bad file: the store is gated on kNoRulesFingerprint, so the same
    // record reads Stale. Nothing is shown as Ready under rules the user
    // did not choose.
    {
        std::ofstream f(paths.rules);
        f << "max_tied_paths = 0\n";
    }
    AppState bad;
    REQUIRE(bad.analysis_blocked());
    CHECK(bad.store->get_summary(seeded).status == RecordStatus::Stale);
}
```

- [ ] **Step 5: Write the failing engine and path-codec tests.**

Add to tests/test_search.cpp, after the clamp tests (after the case at line 632):

```cpp
TEST_CASE("collected phrases: none when no phrase lands during the activation") {
    Song song = build_tail_song({{0, true, false}, {768, true, false}, {1536},
                                 {2304, false, true}, {3072}, {3840}, {4608},
                                 {5136}, {5280}});
    ScoreGraph graph(song, 4);
    const Activation& act = last_act(run_search(graph, DepthMode::Scores, 0, std::nullopt));
    CHECK(act.collected_phrase_ticks.empty());
    CHECK_FALSE(act.sqout_tick.has_value());
}

TEST_CASE("collected phrases: one phrase mid-activation is recorded") {
    // The fixture of "SP past the last note: a mid-activation phrase extends
    // the end": the phrase ending at 3840 is collected while SP is active.
    Song song = build_tail_song({{0, true, false}, {768, true, false}, {1536},
                                 {2304, false, true}, {3072}, {3840, true, false},
                                 {4608}, {5376}, {6144}, {6720}, {6816}});
    ScoreGraph graph(song, 4);
    const Activation& act = last_act(run_search(graph, DepthMode::Scores, 0, std::nullopt));
    CHECK(act.collected_phrase_ticks == std::vector<int64_t>{3840});
}

TEST_CASE("collected phrases: two phrases under a full meter are both recorded, in order") {
    // The cap-2 clamp fixture: both phrases are collected while active, and
    // the second is the note the cap pinned the end to. A clamped phrase
    // counts as collected.
    Song song = build_tail_song({{0, true, false}, {768, true, false},
                                 {2304, false, true}, {3072, true, false},
                                 {3840, true, false}, {4608}, {5376}, {6000},
                                 {6768}, {7500}});
    ScoreGraph graph(song, 2);
    std::vector<Path> paths = run_search(graph, DepthMode::Scores, 0, std::nullopt);
    REQUIRE(!paths.empty());
    const Activation& act = paths.front().activations.front();
    CHECK(act.collected_phrase_ticks == std::vector<int64_t>{3072, 3840});
    REQUIRE(act.clamp_tick.has_value());
    CHECK(act.collected_phrase_ticks.back() == *act.clamp_tick);
}

TEST_CASE("collected phrases: the corpus agrees with the squeezes and the SP end") {
    int sqouts_seen = 0;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        ScoreGraph graph(song, 4);
        for (const Path& p : run_search(graph, DepthMode::Scores, 1, std::nullopt)) {
            for (const Activation& act : p.all_activations()) {
                REQUIRE(act.timecode.has_value());
                bool took_sqout = false;
                for (const SPSqueeze& sq : act.sqinouts)
                    if (sq.kind == SqueezeKind::SqOut) took_sqout = true;
                // sqout_tick is set exactly when the activation squeezed out.
                CHECK(act.sqout_tick.has_value() == took_sqout);
                if (act.sqout_tick) ++sqouts_seen;

                int64_t prev = -1;
                for (int64_t t : act.collected_phrase_ticks) {
                    CHECK(t > prev);  // strictly ascending
                    CHECK(t >= act.timecode->ticks());
                    // A squeezed-out phrase, and anything after it, was
                    // never collected.
                    if (act.sqout_tick) CHECK(t < *act.sqout_tick);
                    prev = t;
                }
            }
        }
    }
    CHECK(sqouts_seen > 0);
}
```

The corpus sweep does not check `t <= deact_tick`. A late-SqIn phrase lands after the deactivation node and still counts as collected, so that bound would be wrong.

In tests/test_path_codec.cpp, in "path codec: a missing node or a bad structure blob throws", replace

```cpp
    // Versions 1 and 2 are real old versions, not just "some other number":
    // the structure format was bumped through 1 -> 2 -> 3, and the old
    // layouts are refused the same as any unknown one.
    FlatRecord past1 = flat;
    past1.structure[0] = 1;
    CHECK_THROWS_AS(rebuild_record(past1), SerializeError);

    FlatRecord past2 = flat;
    past2.structure[0] = 2;
    CHECK_THROWS_AS(rebuild_record(past2), SerializeError);

    // The current version is 3, and the unmodified flat record -- still at
    // that version -- round-trips through rebuild_record without throwing.
    CHECK(kPathStructureFormatVersion == 3);
    CHECK(flat.structure[0] == static_cast<uint8_t>(kPathStructureFormatVersion));
    HydraRecord rebuilt = rebuild_record(flat);
    CHECK(rebuilt.paths.size() == rec.paths.size());
```

with

```cpp
    // Versions 1, 2 and 3 are real old versions, not just "some other
    // number": the structure format was bumped through 1 -> 2 -> 3 -> 4, and
    // the old layouts are refused the same as any unknown one.
    FlatRecord past1 = flat;
    past1.structure[0] = 1;
    CHECK_THROWS_AS(rebuild_record(past1), SerializeError);

    FlatRecord past2 = flat;
    past2.structure[0] = 2;
    CHECK_THROWS_AS(rebuild_record(past2), SerializeError);

    FlatRecord past3 = flat;
    past3.structure[0] = 3;
    CHECK_THROWS_AS(rebuild_record(past3), SerializeError);

    // The current version is 4, and the unmodified flat record -- still at
    // that version -- round-trips through rebuild_record without throwing,
    // rules fingerprint included.
    CHECK(kPathStructureFormatVersion == 4);
    CHECK(flat.structure[0] == static_cast<uint8_t>(kPathStructureFormatVersion));
    HydraRecord rebuilt = rebuild_record(flat);
    CHECK(rebuilt.rules_fingerprint == rec.rules_fingerprint);
    CHECK(rebuilt.paths.size() == rec.paths.size());
```

- [ ] **Step 6: Build and watch it fail.**

Run `.\build_cpp.ps1 -Target hydra_tests`. It fails to compile: `Activation::sqout_tick`, `Activation::collected_phrase_ticks`, `HydraRecord::rules_fingerprint` and the two-argument `RecordStore` constructor do not exist yet.

- [ ] **Step 7: Add the fields to the model.**

In src/core/model.h, after the `clamp_tick` field of `Activation`, add

```cpp
    // The chart tick of the SP phrase chord this activation squeezed out:
    // the deact edge's sqinout_time when the path took the SqOut branch.
    // Stamped by the search at copy-out (blob v6). Unset when the activation
    // did not squeeze out, or on an older record. Nothing re-derives it.
    std::optional<int64_t> sqout_tick;

    // The ticks of the SP phrase-end chords this activation collected while
    // active, in chart order: every phrase the gauge received, a late-SqIn
    // phrase and a cap-clamped phrase included. A squeezed-out phrase is not
    // among them. The search records each one as its path crosses the phrase
    // (blob v6). Empty when none was collected, or on an older record.
    // Nothing re-derives it.
    std::vector<int64_t> collected_phrase_ticks;
```

In `HydraRecord`, after `bool sp_cap_converged = true;`, add

```cpp
    // core::Rules::fingerprint() of the rules the search ran under (blob v6,
    // path structure v4). A record built in memory starts with the default
    // rules' fingerprint; analyze_chart stamps the real one. An older blob
    // reads back core::kNoRulesFingerprint, which matches no rules, so it can
    // never pass as current.
    uint64_t rules_fingerprint = core::default_rules().fingerprint();
```

model.h already includes core/rules.h from Task 1.

- [ ] **Step 8: Write and read the new fields.**

In src/store/serialize.h, replace `constexpr uint32_t kBlobFormatVersion = 5;` with `constexpr uint32_t kBlobFormatVersion = 6;`, and add one line to its history comment: "v6: Activation.sqout_tick and collected_phrase_ticks; HydraRecord.rules_fingerprint (docs/adr/0014)."

In src/store/path_binary.cpp `write_activation`, after

```cpp
    if (version >= 5) w.opt_i64(act.clamp_tick);
```

add

```cpp
    // Format version 6 and later: the squeezed-out phrase and the phrases
    // collected while active. Older blobs read back unset and empty.
    if (version >= 6) {
        w.opt_i64(act.sqout_tick);
        w.u32(static_cast<uint32_t>(act.collected_phrase_ticks.size()));
        for (int64_t t : act.collected_phrase_ticks) w.i64(t);
    }
```

In `read_activation`, replace

```cpp
    // Pre-v5 blobs leave clamp_tick unset. Nothing fills it in.
    if (version >= 5) act.clamp_tick = r.opt_i64();
    return act;
```

with

```cpp
    // Pre-v5 blobs leave clamp_tick unset. Nothing fills it in.
    if (version >= 5) act.clamp_tick = r.opt_i64();
    // Pre-v6 blobs leave sqout_tick unset and collected_phrase_ticks empty.
    if (version >= 6) {
        act.sqout_tick = r.opt_i64();
        const uint32_t n = r.u32();
        act.collected_phrase_ticks.reserve(n);
        for (uint32_t i = 0; i < n; ++i) act.collected_phrase_ticks.push_back(r.i64());
    }
    return act;
```

In src/store/serialize.cpp `write_record`, right after `w.boolean(record.sp_cap_converged);`, add `if (version >= 6) w.u64(record.rules_fingerprint);`. In `read_record`, right after `record.sp_cap_converged = r.boolean();`, add

```cpp
    // Pre-v6 blobs never recorded their rules: kNoRulesFingerprint matches none.
    record.rules_fingerprint = version >= 6 ? r.u64() : core::kNoRulesFingerprint;
```

- [ ] **Step 9: Bump the node and structure formats.**

In src/store/path_codec.h, replace `kPathNodeFormatVersion = 3` with `4` and `kPathStructureFormatVersion = 3` with `4`. Add a history line to each comment: node v4 carries blob-v6 activations, and structure v4 puts the rules fingerprint right after the version.

In src/store/path_codec.cpp `flatten_record`, replace

```cpp
    w.u32(kPathStructureFormatVersion);
    w.opt_f64(record.ms_limit);
```

with

```cpp
    w.u32(kPathStructureFormatVersion);
    // Right after the version, so the store can compare version and rules
    // as one fixed 12-byte head in SQL (record_store.cpp kRowReadySql).
    w.u64(record.rules_fingerprint);
    w.opt_f64(record.ms_limit);
```

In `rebuild_record`, right after the version check, add `record.rules_fingerprint = r.u64();` before `record.ms_limit = r.opt_f64();`.

- [ ] **Step 10: Make the store gate on the rules, and open every store with a fingerprint.**

In src/store/record_store.h, add `#include "core/rules.h"`, and replace `explicit RecordStore(const std::string& dbpath);` with

```cpp
    // rules_fingerprint: core::Rules::fingerprint() of the rules this process
    // runs under. A row stamped with any other fingerprint reads Stale.
    // core::kNoRulesFingerprint (a bad hydra_rules.ini) makes every row Stale.
    explicit RecordStore(const std::string& dbpath,
                         uint64_t rules_fingerprint = core::default_rules().fingerprint());
```

Add the private member `uint64_t rules_fingerprint_;` next to `db_`.

In src/store/record_store.cpp, the version and the fingerprint become one 12-byte head. Replace `structure_is_current` and `current_structure_head` (lines 277-297) with

```cpp
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
```

`rank_row` and `row_is_ready` each gain a trailing `uint64_t rules_fingerprint` parameter, passed to `structure_is_current`. `Candidate::format` now means "this layout and these rules", so a row analyzed under other rules ranks exactly like an old-format row. Replace the SQL constants with

```cpp
constexpr const char* kRowReadySql =
    "(hyversion = ? AND ms_enabled != -1 AND substr(structure,1,12) = ?)";
constexpr const char* kRowNotReadySql =
    "(hyversion != ? OR ms_enabled = -1 OR substr(structure,1,12) != ?)";
```

and `bind_ready_params` with

```cpp
int bind_ready_params(sqlite3_stmt* s, int idx, uint64_t rules_fingerprint) {
    bind_text(s, idx, current_record_version());
    bind_blob(s, idx + 1, structure_head_for(rules_fingerprint));
    return idx + 2;
}
```

Every caller passes the store's `rules_fingerprint_`. That covers the four `bind_ready_params` calls (lines 936, 1153, 1340 and 1350) and the five `row_is_ready` calls (lines 1059, 1110, 1222, 1386 and 1461). The two SELECTs that read `substr(structure,1,4)` (lines 1027 and 1418) read `substr(structure,1,12)` instead. The comment above the SQL constants changes to say the second bound parameter is "the current structure format and rules fingerprint, 12 bytes".

The constructor at line 467 changes from `RecordStore::RecordStore(const std::string& dbpath) {` to

```cpp
RecordStore::RecordStore(const std::string& dbpath, uint64_t rules_fingerprint)
    : rules_fingerprint_(rules_fingerprint) {
```

A Stale lookup now also says why. In src/store/record_store.h, `RecordLookup` gains two fields after `std::string hyversion;`:

```cpp
    // Why a Stale row is Stale; both can be true, neither is when not Stale.
    bool stale_build = false;  // another Hydra build, an older path layout, or a migrated row
    bool stale_rules = false;  // this path layout, analyzed under other rules
```

In src/store/record_store.cpp, add this helper right after `row_is_ready`. It is the negation of `row_is_ready`, split into its two causes.

```cpp
// Why a row that is not Ready is Stale, for callers that explain it
// (hydra_replay dump). `build`: another Hydra build, an older path layout, or
// a migrated row. `rules`: this layout, analyzed under other rules. An older
// layout has no fingerprint to compare, so it is only ever `build`.
struct StaleReasons {
    bool build = false;
    bool rules = false;
};

StaleReasons stale_reasons(const std::string& hyversion, int ms_enabled,
                           const std::vector<uint8_t>& structure_head,
                           uint64_t rules_fingerprint) {
    const std::vector<uint8_t> want = structure_head_for(rules_fingerprint);
    const bool layout_current =
        structure_head.size() >= kStructureHeadBytes &&
        std::equal(want.begin(), want.begin() + 4, structure_head.begin());
    StaleReasons why;
    why.build = hyversion != current_record_version() || ms_enabled == -1 || !layout_current;
    why.rules = layout_current &&
                !std::equal(want.begin() + 4, want.end(), structure_head.begin() + 4);
    return why;
}
```

In `get_record`, replace

```cpp
        if (!row_is_ready(best->hyversion, best->ms_enabled, best->structure)) {
            out.status = RecordStatus::Stale;
            return out;
        }
```

with

```cpp
        if (!row_is_ready(best->hyversion, best->ms_enabled, best->structure,
                          rules_fingerprint_)) {
            out.status = RecordStatus::Stale;
            const StaleReasons why = stale_reasons(best->hyversion, best->ms_enabled,
                                                   best->structure, rules_fingerprint_);
            out.stale_build = why.build;
            out.stale_rules = why.rules;
            return out;
        }
```

In tools/replay.cpp `cmd_dump`, the Stale message names the real reason. Replace

```cpp
        } else {
            std::fprintf(stderr,
                         "Stale: the stored row was written by Hydra %s, not "
                         "this build; its paths cannot be read.%s\n",
                         lookup.hyversion.c_str(),
                         a.no_analyze ? " Re-analyze the chart."
                                      : " Analyzing the chart fresh instead.");
        }
```

with

```cpp
        } else {
            // One line per reason. The follow-up ("Re-analyze" or "Analyzing
            // fresh") goes on the last line printed.
            const char* next_step = a.no_analyze ? " Re-analyze the chart."
                                                 : " Analyzing the chart fresh instead.";
            if (lookup.stale_build)
                std::fprintf(stderr,
                             "Stale: the stored row was written by Hydra %s, not "
                             "this build; its paths cannot be read.%s\n",
                             lookup.hyversion.c_str(), lookup.stale_rules ? "" : next_step);
            if (lookup.stale_rules)
                std::fprintf(stderr,
                             "Stale: the stored row was analyzed with different rules "
                             "(hydra_rules.ini changed).%s\n",
                             next_step);
        }
```

So a row from another build prints today's line unchanged. A row from this build under other rules prints `Stale: the stored row was analyzed with different rules (hydra_rules.ini changed). Analyzing the chart fresh instead.` A row stale for both prints the build line without the follow-up, then the rules line with it.

In src/app/config.h, replace the `open_store` declaration with

```cpp
std::unique_ptr<store::RecordStore> open_store(
    const std::string& db, uint64_t rules_fingerprint = core::default_rules().fingerprint());
```

In src/app/config.cpp, replace

```cpp
std::unique_ptr<store::RecordStore> open_store(const std::string& db) {
    auto store = std::make_unique<store::RecordStore>(db);
```

with

```cpp
std::unique_ptr<store::RecordStore> open_store(const std::string& db,
                                               uint64_t rules_fingerprint) {
    auto store = std::make_unique<store::RecordStore>(db, rules_fingerprint);
```

Then every store open passes the loaded rules' fingerprint. There are eight sites.

In src/ui/app_state.cpp, Task 1's delegating constructor becomes

```cpp
AppState::AppState(StartupSettings start)
    : AppState(start.settings,
               app::open_store(app::db_path(),
                               start.rules_error.empty()
                                   ? start.settings.rules.fingerprint()
                                   : core::kNoRulesFingerprint)) {
    rules_error = std::move(start.rules_error);
}
```

A bad rules file gates the store on `kNoRulesFingerprint`, so no row reads Ready under the defaults the settings still hold.

In src/cli/batch.cpp, `hydra::app::open_store(db)` becomes `hydra::app::open_store(db, settings.rules.fingerprint())`. In src/cli/report.cpp, `hydra::app::open_store(db)` becomes `hydra::app::open_store(db, settings.rules.fingerprint())`, so the report lists only records made under the rules it was given. In src/cli/fillcompare.cpp, the two opens become `hydra::app::open_store(*old_path, settings.rules.fingerprint())` and `hydra::app::open_store(*new_path, settings.rules.fingerprint())`. In tools/replay.cpp, `store::RecordStore store(snapshot_path);` becomes `store::RecordStore store(snapshot_path, s.rules.fingerprint());`. In tools/bench.cpp, the three opens pass `g_rules.fingerprint()`: the in-memory store at line 60, `std::make_unique<store::RecordStore>(dbpath, g_rules.fingerprint())` at line 115, and `store::RecordStore db(dbpath, g_rules.fingerprint());` at line 224.

- [ ] **Step 11: Stamp the fingerprint on every analysis.**

In src/search/pather.cpp `analyze_chart`, each of the three `return analyze_...(...)` statements becomes `HydraRecord record = analyze_...(...);` and ends with

```cpp
    record.rules_fingerprint = settings.rules.fingerprint();
    return record;
```

- [ ] **Step 12: Record collected phrases in the engine.**

In src/search/engine.cpp, add a node type next to `SqNode`:

```cpp
// One collected SP phrase, linked to the one collected before it.
struct ColNode {
    int32_t prev;
    int64_t tick;
};
```

Five structs gain a field. `Act`, `Path` and `Variant` each gain `int32_t col_tail;` after `clamp_tick`. `OutAct` gains `int32_t col_begin, col_end;` and `int64_t sqout_tick;` after `clamp_tick`.

The engine gains four members. The arena `std::vector<ColNode> cols_;` goes next to `sqs_`. The output `std::vector<int64_t> out_cols_;` goes next to `out_sqs_`, with the accessor `const std::vector<int64_t>& out_cols() const { return out_cols_; }` next to `out_sqs()`. A scratch `std::vector<int32_t> col_scratch_;` goes next to `sq_scratch_`. It also gains two helpers next to `push_sq`:

```cpp
    int32_t push_col(int32_t prev, int64_t tick) {
        cols_.push_back(ColNode{prev, tick});
        return (int32_t)cols_.size() - 1;
    }
    // The chain with every phrase at or after `tick` dropped. Nodes are never
    // edited, so this only walks the tail pointer back.
    int32_t trim_cols(int32_t tail, int64_t tick) const {
        while (tail >= 0 && cols_[(size_t)tail].tick >= tick)
            tail = cols_[(size_t)tail].prev;
        return tail;
    }
```

Three places start an empty chain. `new_act` sets `a.col_tail = -1;` after `a.clamp_tick = NO_TIME;`. `run()` sets `root.col_tail = -1;` after `root.clamp_tick = NO_TIME;`, because the `memset` leaves it 0, which is a real index. `branch_activate` sets `c.col_tail = -1;` after `c.clamp_tick = NO_TIME;`, so each activation starts fresh.

In `advance`, in the SP-track loop, record every phrase the path crosses. Replace

```cpp
            for (int32_t i = 0; i < sp_n; ++i) {
                if (buffered > 0) {
```

with

```cpp
            for (int32_t i = 0; i < sp_n; ++i) {
                // Every phrase the gauge receives counts as collected: a
                // buffered (late-SqIn) phrase too, whose extension is already
                // in sp_end_time, and a phrase the cap clamps.
                p.col_tail = push_col(p.col_tail, eo->sp_times[(size_t)i].first.ticks());
                if (buffered > 0) {
```

In `create_deactivated_path`, replace

```cpp
        a.deact_edge = deact_edge;
        a.clamp_tick = p.clamp_tick;
        if (is_sq_out) {
            a.sq_tail = push_sq(a.sq_tail, SQ_OUT, e.sqinout_timing);
        }
```

with

```cpp
        a.deact_edge = deact_edge;
        a.clamp_tick = p.clamp_tick;
        // A squeeze-out gives back the phrase at sqinout_time and everything
        // after it, so the activation keeps only what came before.
        a.col_tail = is_sq_out ? trim_cols(p.col_tail, e.sqinout_time) : p.col_tail;
        if (is_sq_out) {
            a.sq_tail = push_sq(a.sq_tail, SQ_OUT, e.sqinout_timing);
        }
```

and add `c.col_tail = -1;` after `c.clamp_tick = NO_TIME;` in the same function.

In `reduce_group`, add `v.col_tail = p.col_tail;` after `v.clamp_tick = p.clamp_tick;`.

`emit_acts` gains an `int32_t col_tail` parameter after `clamp_tick`, in both the declaration and the definition. Inside the per-activation loop, after `oa.clamp_tick = a.clamp_tick;`, add

```cpp
        oa.sqout_tick = NO_TIME;
        emit_cols(a.col_tail, &oa.col_begin, &oa.col_end);
```

In the `if (last.deact_edge < 0)` block, after `last.clamp_tick = clamp_tick;`, add

```cpp
            // Still running at the song's end: its phrases are on the live path.
            emit_cols(col_tail, &last.col_begin, &last.col_end);
```

`emit_cols` is a new private member next to `emit_acts`:

```cpp
    void emit_cols(int32_t tail, int32_t* begin, int32_t* end) {
        col_scratch_.clear();
        for (int32_t c = tail; c >= 0; c = cols_[(size_t)c].prev) col_scratch_.push_back(c);
        *begin = (int32_t)out_cols_.size();
        for (size_t k = col_scratch_.size(); k-- > 0;)
            out_cols_.push_back(cols_[(size_t)col_scratch_[k]].tick);
        *end = (int32_t)out_cols_.size();
    }
```

The final-activation branch appends a second run to `out_cols_` and points the act at it. The first run it wrote for that act is always empty, since `a.col_tail` is -1 for a still-running act, so it is simply unused. `emit_variant` passes `var.col_tail` and `emit_path` passes `p.col_tail` as the new argument.

`rebuild` gains a `const std::vector<int64_t>& out_cols` parameter after `out_sqs`. After `if (oa.clamp_tick != NO_TIME) act.clamp_tick = oa.clamp_tick;`, add

```cpp
            // The phrases this activation collected while active, as the
            // search recorded them (blob v6).
            act.collected_phrase_ticks.assign(out_cols.begin() + oa.col_begin,
                                              out_cols.begin() + oa.col_end);
```

Inside the `took_sqout` block, right after `const int64_t sqout_tick = sqout_at->ticks();`, add `act.sqout_tick = sqout_tick;`.

In `run_search`, the rebuild call becomes `rebuild(en, engine.out_paths(), engine.out_acts(), engine.out_sqs(), engine.out_cols(), collect_multsqueezes(graph), graph.tail_backends(), graph.timing());`.

- [ ] **Step 13: Build and run the new tests.**

```powershell
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe -tc="record blob: a v5 write drops*,a row analyzed under other rules*,under a bad hydra_rules.ini*,a Stale lookup says why*,collected phrases*,path codec*"
```

Expect `Status: SUCCESS!`.

- [ ] **Step 14: Run the whole suite, prove the scores did not move, and check the speed limit.**

Run `.\build-cpp\Release\hydra_tests.exe` and expect `Status: SUCCESS!`. Then repeat Step 1 on this build and compare:

```powershell
.\build_cpp.ps1 -Target hydra_batch; .\build_cpp.ps1 -Target hydra_bench
$base = "$env:TEMP\hydra_task2"
Remove-Item "$base\after.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_batch.exe --db "$base\after.db" .\testdata\input > "$base\after_batch.txt"
Get-Content "$base\after_batch.txt" |
    Where-Object { $_ -match '^\[\d+/\d+\]' } |
    ForEach-Object { $_ -replace '^\[\d+/\d+\]\s*', '' } |
    Sort-Object | Set-Content "$base\after_sorted.txt"
Compare-Object (Get-Content "$base\before_sorted.txt") (Get-Content "$base\after_sorted.txt")

.\build-cpp\Release\hydra_bench.exe > "$base\bench_after_1.txt"
.\build-cpp\Release\hydra_bench.exe > "$base\bench_after_2.txt"
function Best($files, $config) {
    ($files | ForEach-Object {
        Get-Content $_ | Where-Object { $_ -match "^\s*$config\s*:\s*([\d.]+)s" } |
            ForEach-Object { [double]$Matches[1] }
    } | Measure-Object -Minimum).Minimum
}
foreach ($config in "cap4 d4", "auto d4") {
    $before = Best @("$base\bench_before_1.txt", "$base\bench_before_2.txt") $config
    $after = Best @("$base\bench_after_1.txt", "$base\bench_after_2.txt") $config
    "{0}: before {1:N2}s, after {2:N2}s, ratio {3:N3}" -f $config, $before, $after, ($after / $before)
}
```

`Compare-Object` must print nothing. Both ratios must be at most 1.030. If either ratio is above 1.030, stop here. Do not commit, and do not build a cheaper variant. Report both configs' before and after times to the user and wait for their call (decision 26).

- [ ] **Step 15: Write the ADR.**

Create docs/adr/0014-squeeze-out-and-collected-phrases-and-rules-are-stored.md:

```markdown
# The squeezed-out phrase, the collected phrases and the rules are stored

Two facts about an activation were only ever known inside the search. The
first is which SP phrase it squeezed out. The second is which phrases it
collected while Star Power was running. The display layer needed both, so it
rebuilt them from the chart. That is the same drift ADR 0011 and ADR 0013
closed for the deactivation node and the cap-clamp anchor.

A record also never said which rules it was analyzed under. Once the user's
rule choices moved into hydra_rules.ini, a record from before an edit would
have read Ready while holding answers to a different question.

## The decision

The engine stamps both facts at copy-out. `Activation::sqout_tick` is the deact
edge's `sqinout_time` when the path took the SqOut branch.
`Activation::collected_phrase_ticks` is every phrase tick the path crossed on
the SP track, recorded in `advance()` and trimmed back past the squeezed-out
phrase in `create_deactivated_path`. A late-SqIn phrase and a cap-clamped
phrase both count, because the gauge received them.

The pather stamps `HydraRecord::rules_fingerprint` with the fingerprint of the
rules the run used. The store writes it into the structure blob right after
the format version, so version and rules make one 12-byte head. That head is
what decides Ready, in C++ (`structure_is_current`) and in SQL
(`kRowReadySql`).

Blob format 6, path-node format 4 and path-structure format 4 carry the three
fields.

## No fallback

A record written before this build has none of the three fields. It reads
Stale until it is re-analyzed. Nothing reconstructs the squeezed-out phrase or
the collected phrases from the chart, and nothing assumes an old record ran
under the default rules.

A record analyzed under other rules also reads Stale. It is not deleted. When
the rules are switched back, it reads Ready again.

When hydra_rules.ini is bad, the GUI opens its store with
`core::kNoRulesFingerprint`, which no rules value hashes to. Every row reads
Stale until the file is fixed, so nothing is shown as Ready under rules the
user did not choose.

## What this costs

Every stored record reads Stale once, after the upgrade. Every edit to
hydra_rules.ini makes the whole library Stale under the new rules.

The search pays one arena push per phrase crossed on the SP track, per path.
The limit was 3% on hydra_bench's corpus timing (best of two runs each, cap 4
and Auto at depth 4). The change was only committed within that limit; a run
past it stops for the user's call, and no cheaper variant exists.
```

Step 14 printed the two measured ratios. Put them in the commit message body, on the line that says "hydra_bench within the 3% limit".

- [ ] **Step 16: Commit.**

```
git add src/core/model.h src/store/serialize.h src/store/serialize.cpp src/store/path_binary.cpp src/store/path_codec.h src/store/path_codec.cpp src/store/record_store.h src/store/record_store.cpp src/search/engine.cpp src/search/pather.cpp src/app/config.h src/app/config.cpp src/ui/app_state.cpp src/cli/batch.cpp src/cli/report.cpp src/cli/fillcompare.cpp tools/replay.cpp tools/bench.cpp tests/test_store.cpp tests/test_path_codec.cpp tests/test_search.cpp tests/test_app_state.cpp docs/adr/0014-squeeze-out-and-collected-phrases-and-rules-are-stored.md
git commit -m "Store the squeezed-out phrase, the collected phrases and the rules

Record format v6: each activation carries sqout_tick and
collected_phrase_ticks, stamped by the engine at copy-out, and each record
carries the fingerprint of the rules it ran under. The structure blob puts the
fingerprint right after its version, and a row whose 12-byte head does not
match this build and these rules reads Stale. Older records read Stale until
re-analyzed; nothing reconstructs the new fields (docs/adr/0014). Under a bad
hydra_rules.ini the GUI's store reads every row as Stale. hydra_replay dump
says whether a Stale row came from another build, other rules, or both.

hydra_batch before/after: zero differences. hydra_bench within the 3% limit.

Task: Record format v6
Agent: <executor>
Session: <session>

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: One function prices a backend row, and everyone calls it

Today three places decide separately what a backend row is worth. The search does it with inline branches in `create_deactivated_path` (src/search/engine.cpp). The replay has its own hand-written copy of the same window-plus-leeway rule (src/core/replay.cpp, the loop inside `replay_path`). The details table never checks where a row sits at all. It prints `points` for a plain row and `sqout_points` for a squeezed-out row, whatever the offset (src/app/path_view.cpp, `build_activations`). That is how Round and Round came to show `480.0 [ RY ] 260 Free SqOut <-- squeezed out (-200)` for a chord the engine counts as 0. This task adds one small function, `core::backend_row_value`, that answers "how much SP score does this row add on this path?" with the engine's exact cases. The search, the replay and the table all call it.

What the user sees (user decisions 1 and 19). Every backend row the engine does not count now shows `0` in the Points column and is not highlighted. A squeezed-out row past the leeway reads `0` with the tag `squeezed out (uncounted)`. A plain row labelled "Hard (uncounted)" or "Insane (uncounted)" reads `0`; its label is unchanged. The same rule covers a phrase-chord row that was not squeezed out and sits past the leeway (for example one labelled "Free SqOut"), because the engine does not count that row either. A squeezed-out row that really costs points keeps its `(-N)` tag and its warning colour, unchanged. Scores do not move; a before/after `hydra_batch` diff proves it.

Tasks 1 and 2 run first. Task 1 puts `Rules` in `namespace hydra::core` (src/core/rules.h), gives `app::Settings` a `rules` field, gives the engine a `backend_leeway_ms_` member it reads off `graph.rules()`, gives `replay_path` a `const core::Rules& rules` parameter, and deletes `kBackendLeewayMs`. Task 2 adds `Activation::sqout_tick`. The code this task quotes is the code as Task 1 leaves it. If an earlier task changed these lines differently, apply the same change to the new lines.

The engine's arithmetic, restated so the function is easy to check. The SP walk has already paid every row at or before the SP end (offset <= 0) at full value, so the deactivation edge only adds the difference. A row is counted with no squeeze when it sits at or before the SP end, or less than the leeway (3 ms by default) after it. On a squeeze-out, the squeezed chord itself keeps only `sqout_points`, and every chord after it is worth 0. Written as "what the row is worth" rather than "what the edge adds", that is four lines, and the edge adds `worth - already_paid`. I checked all nine engine cases (three offset bands times before/exact/after, plus the no-squeeze-out branch) against that subtraction; every one matches.

**Goal:** One inline function in src/core decides a backend row's SP value, and the engine, the replay and the details table all call it.

**Files:** Creates src/core/backend_value.h. Changes src/search/engine.cpp (`create_deactivated_path`), src/core/replay.h and replay.cpp (`replay_path`), src/app/path_view.h and path_view.cpp (`build_activations`), and src/ui/details_view.cpp (`render_activations_section`). Adds cases to tests/test_model.cpp, tests/test_path_view.cpp and tests/test_replay.cpp, updates one check in tests/test_path_view.cpp, and adds one headless GUI test to tests/ui/uitest_tests.cpp.

**Acceptance Criteria:**
- [ ] `backend_row_value: every engine case` passes, including the Round and Round numbers (offset 479.999, 460/260, the squeezed chord itself: value 0; the same chord at -5.0: value 260).
- [ ] `build_activations: a squeezed-out row past the leeway is worth 0` passes: Points `"0"`, rating `"Free SqOut <-- squeezed out (uncounted)"`, `warn == false`; the -5.0 row still reads `"260"`, `"Standard SqOut <-- squeezed out (-200)"` and `warn == true`.
- [ ] `build_activations: plain rows past the leeway show 0` passes: the rows at -40 and +2.5 ms read `"460"`; the rows at +20 and +120 ms read `"0"` with the labels `"Hard (uncounted)"` and `"Insane (uncounted)"`; the unsqueezed phrase chord at +150 ms reads `"0"`; none of the five warns.
- [ ] `build_activations: the backend limit hides far rows but never squeezed-out ones` passes with its +60 ms squeezed-out row now unhighlighted and tagged `(uncounted)`.
- [ ] `a squeezed-out chord past the leeway earns nothing` passes in tests/test_replay.cpp.
- [ ] `replay reproduces the engine's score for every corpus path` and `targeted search reproduces every corpus path` still pass.
- [ ] The whole suite passes: `.\build-cpp\Release\hydra_tests.exe` ends with `Status: SUCCESS!`.
- [ ] `$env:TEMP\hydra_task3\batch_sorted.txt` exists (the baseline Tasks 9, 10 and 19 diff against), and the after-change `batch_sorted.txt` is identical to it (`Compare-Object` prints nothing).
- [ ] The best-of-3 "search" seconds from `hydra_bench` on The Faceless - The Spiraling Void are no more than 5% above the baseline. Past 5% the task stops and reports to the user (user decision 26).
- [ ] `hydra_uitest --test squeezed_out_uncounted` prints `[PASS]`.
- [ ] The engine's inline branches are gone: `Select-String -Path src\search\engine.cpp -Pattern 'is_leeway|is_already_counted|is_exact_sqout'` finds nothing, and `Select-String -Path src\core\replay.cpp -Pattern 'const bool leeway'` finds nothing.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="backend_row_value*,build_activations*,a squeezed-out chord past the leeway*,replay reproduces*"` -> `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Record the score and timing baselines before touching any code**

The engine change must not move a single score. The only proof is a run from before the change, so this comes first. The folder `$env:TEMP\hydra_task3` is the Task 3 baseline folder. Its `batch_sorted.txt` is the file Tasks 9, 10 and 19 compare against, so do not delete it.

`hydra_batch` skips charts already stored, so each run gets its own fresh database. It prints one line per chart as `[done/total] score  artist - title bestpath`. Workers finish in any order, so the command keeps only those lines, strips the `[n/N]` counter and sorts. This is the same pipeline Tasks 9 and 10 use, character for character. hydra_batch reads difficulty, pro drums, depth and SP cap from the app's settings INI, so leave that INI alone between this run and the later ones. It also reads hydra_rules.ini next to the exe; there must not be one, so the run uses the default rules.

The timing chart is the largest chart file among those in testdata\input: The Faceless - The Spiraling Void's notes.chart, 373 KB. I picked it by sorting every notes.mid and notes.chart under testdata\input by size. The per-row pricing loop runs once per backend row on every deactivation edge, so the chart with the most notes gives it the most work.

```powershell
$d = "$env:TEMP\hydra_task3"
New-Item -ItemType Directory -Force $d | Out-Null
Remove-Item "$d\before.db" -ErrorAction SilentlyContinue
.\build_cpp.ps1 -Target hydra_batch
.\build_cpp.ps1 -Target hydra_bench
Test-Path .\build-cpp\Release\hydra_rules.ini
.\build-cpp\Release\hydra_batch.exe --db "$d\before.db" testdata\input |
  Select-String '^\[\d+/\d+\] ' | ForEach-Object { $_.Line -replace '^\[\d+/\d+\] ', '' } |
  Sort-Object | Set-Content "$d\batch_sorted.txt"
$chart = "testdata\input\common\Summer Blast _25 Setlist\Tier 7\The Faceless - The Spiraling Void"
1..3 | ForEach-Object { .\build-cpp\Release\hydra_bench.exe $chart } | Select-String 'search' | Set-Content "$d\bench_before.txt"
Get-Content "$d\bench_before.txt"
```

Expected: `Test-Path` prints `False`. batch_sorted.txt holds one line per chart in testdata\input. bench_before.txt holds three `parse ...s | search ...s | store ...s` lines. Write down the smallest search figure.

- [ ] **Step 2: Write the failing unit test for the function**

Add to tests/test_model.cpp, after the existing `#include "core/model.h"`:

```cpp
#include "core/backend_value.h"
#include "core/rules.h"
```

and at the end of the file:

```cpp
// The engine's three cases, as values. The deactivation edge in
// create_deactivated_path adds `value - already_paid`, where rows at or
// before the SP end were already paid in full by the SP walk.
TEST_CASE("backend_row_value: every engine case") {
    const double lw = core::default_rules().backend_leeway_ms;  // 3 ms
    using P = core::SqOutPosition;
    using core::backend_row_value;

    // No squeeze-out: full value inside SP or inside the leeway, else 0.
    CHECK(backend_row_value(-50.0, 460, 260, P::NoSqOut, lw) == 460);
    CHECK(backend_row_value(0.0, 460, 260, P::NoSqOut, lw) == 460);
    CHECK(backend_row_value(2.999, 460, 260, P::NoSqOut, lw) == 460);
    CHECK(backend_row_value(3.0, 460, 260, P::NoSqOut, lw) == 0);

    // Before the squeezed-out chord: same as no squeeze-out.
    CHECK(backend_row_value(-50.0, 460, 260, P::Before, lw) == 460);
    CHECK(backend_row_value(1.5, 460, 260, P::Before, lw) == 460);
    CHECK(backend_row_value(10.0, 460, 260, P::Before, lw) == 0);

    // The squeezed-out chord itself keeps only its reduced value, and only
    // where it would have been counted at all.
    CHECK(backend_row_value(-5.0, 460, 260, P::Exact, lw) == 260);
    CHECK(backend_row_value(1.5, 460, 260, P::Exact, lw) == 260);
    // Round and Round (Ratt), second activation: R+Y squeezed out
    // 479.999 ms past the SP end. The engine counts it as 0.
    CHECK(backend_row_value(479.999, 460, 260, P::Exact, lw) == 0);

    // After the squeezed-out chord nothing is under Star Power.
    CHECK(backend_row_value(-50.0, 460, 260, P::After, lw) == 0);
    CHECK(backend_row_value(1.5, 460, 260, P::After, lw) == 0);

    // The leeway is the user's rule (hydra_rules.ini), not a constant.
    CHECK(backend_row_value(5.0, 460, 260, P::NoSqOut, 10.0) == 460);
    CHECK(backend_row_value(5.0, 460, 260, P::NoSqOut, lw) == 0);

    // Position comes from ticks, the way the engine compares them.
    CHECK(core::sqout_position(100, std::nullopt) == P::NoSqOut);
    CHECK(core::sqout_position(99, 100) == P::Before);
    CHECK(core::sqout_position(100, 100) == P::Exact);
    CHECK(core::sqout_position(101, 100) == P::After);

    // Only rows at or before the SP end were paid by the SP walk.
    CHECK(core::paid_by_sp_walk(0.0));
    CHECK(core::paid_by_sp_walk(-0.5));
    CHECK_FALSE(core::paid_by_sp_walk(0.5));
}
```

- [ ] **Step 3: Write the failing display tests**

Add to tests/test_path_view.cpp, after `build_activations: the backend limit hides far rows but never squeezed-out ones`. The first test sets `act.sqout_tick` (Task 2's field) so Task 5 does not have to touch it again.

```cpp
TEST_CASE("build_activations: a squeezed-out row past the leeway is worth 0") {
    HydraRecord rec;  // only feeds the footer

    // Round and Round (Ratt), second activation: an R+Y chord squeezed out
    // 479.999 ms past the SP end. The engine never counts it, so the table
    // must not claim it banks 260 and loses 200, and must not highlight it.
    Activation far;
    far.skips = 0;
    far.e_offset = 300.0;  // not e-critical
    BackendSqueeze far_row;
    far_row.timecode = Timecode::raw(3256);
    far_row.points = 460;
    far_row.sqout_points = 260;
    far_row.is_sp = true;
    far_row.offset_ms = 479.999;
    far.backends.push_back(far_row);
    far.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 479.999});
    far.sqout_tick = 3256;

    Path p;
    p.activations.push_back(far);
    ActivationsView v = build_activations(p, rec, nullptr, 85.0);
    REQUIRE(v.acts.size() == 1);
    REQUIRE(v.acts[0].backends.size() == 1);
    CHECK(v.acts[0].backends[0].points == "0");
    CHECK(v.acts[0].backends[0].rating ==
          "Free SqOut <-- squeezed out (uncounted)");
    CHECK_FALSE(v.acts[0].backends[0].warn);

    // The same chord squeezed out 5 ms inside SP really costs 200, and
    // that row keeps its warning colour.
    Activation near = far;
    near.backends[0].offset_ms = -5.0;
    near.sqinouts[0].offset_ms = -5.0;
    Path q;
    q.activations.push_back(near);
    v = build_activations(q, rec, nullptr, 85.0);
    REQUIRE(v.acts[0].backends.size() == 1);
    CHECK(v.acts[0].backends[0].points == "260");
    CHECK(v.acts[0].backends[0].rating ==
          "Standard SqOut <-- squeezed out (-200)");
    CHECK(v.acts[0].backends[0].warn);
}

// User decision 19: every row the engine does not count reads 0 and is not
// highlighted, not only squeezed-out ones. Labels stay as they are.
TEST_CASE("build_activations: plain rows past the leeway show 0") {
    HydraRecord rec;  // only feeds the footer

    Activation act;
    act.skips = 0;
    act.e_offset = 300.0;  // not e-critical
    // Inside SP, inside the 3 ms leeway, just past it, far past it.
    const std::pair<int64_t, double> rows_at[] = {
        {100, -40.0}, {200, 2.5}, {300, 20.0}, {400, 120.0}};
    for (const auto& [tick, ms] : rows_at) {
        BackendSqueeze row;
        row.timecode = Timecode::raw(tick);
        row.points = 460;
        row.sqout_points = 260;
        row.offset_ms = ms;
        act.backends.push_back(row);
    }
    // A phrase chord past the leeway that this path did not squeeze out.
    BackendSqueeze phrase;
    phrase.timecode = Timecode::raw(500);
    phrase.points = 460;
    phrase.sqout_points = 260;
    phrase.is_sp = true;
    phrase.offset_ms = 150.0;
    act.backends.push_back(phrase);

    Path p;
    p.activations.push_back(act);
    ActivationsView v = build_activations(p, rec, nullptr, 85.0);
    REQUIRE(v.acts.size() == 1);
    const std::vector<BackendRowView>& b = v.acts[0].backends;
    REQUIRE(b.size() == 5);
    CHECK(b[0].points == "460");
    CHECK(b[0].rating == "Easy");
    CHECK(b[1].points == "460");
    CHECK(b[1].rating == "Standard");
    CHECK(b[2].points == "0");
    CHECK(b[2].rating == "Hard (uncounted)");
    CHECK(b[3].points == "0");
    CHECK(b[3].rating == "Insane (uncounted)");
    CHECK(b[4].points == "0");
    CHECK(b[4].rating == "Free SqOut");
    for (const BackendRowView& row : b) CHECK_FALSE(row.warn);
}
```

The existing test `build_activations: the backend limit hides far rows but never squeezed-out ones` has a squeezed-out row at +60 ms. That is past the leeway, so under decision 19 it is uncounted and no longer highlighted. Replace its last lines:

```cpp
    CHECK(limited[1].timing == "60.0");
    CHECK(limited[1].warn);
}
```

with:

```cpp
    CHECK(limited[1].timing == "60.0");
    // +60 ms is past the leeway: the engine never counted this chord, so the
    // row is tagged uncounted and not highlighted (user decision 19).
    CHECK(limited[1].rating.find("squeezed out (uncounted)") != std::string::npos);
    CHECK_FALSE(limited[1].warn);
}
```

- [ ] **Step 4: Run them and watch them fail**

```powershell
.\build_cpp.ps1 -Target hydra_tests
```

Expected: the build fails with `cannot open include file 'core/backend_value.h'`. Temporarily comment out the Step 2 include and test, rebuild, and run:

```powershell
.\build-cpp\Release\hydra_tests.exe -tc="build_activations*"
```

Expected: FAIL. The squeezed-out test fails on `points == "0"` (actual `"260"`), on the rating (actual `"Free SqOut <-- squeezed out (-200)"`) and on `CHECK_FALSE(...warn)`. The plain-row test fails on the three `"0"` checks (actual `"460"`). The backend-limit test fails on the `(uncounted)` check and on `CHECK_FALSE(limited[1].warn)`. Uncomment Step 2 afterwards.

- [ ] **Step 5: Create the shared function**

It takes the leeway as a plain `double`, because that is what Task 1 already hands the engine (`backend_leeway_ms_`) and `summarystr` (`leeway_ms`). Every caller passes `Rules::backend_leeway_ms`.

Create src/core/backend_value.h:

```cpp
// What one backend row is worth on one path: the single answer the search,
// the replay and the details table all read. Before this file each of the
// three wrote its own copy, and the table's copy ignored the row's offset
// (docs/audit/2026-09-24-derivation-audit.md, finding 1).
//
// Header-only and inline so the engine's per-edge loop pays no call.

#ifndef HYDRA_CORE_BACKEND_VALUE_H
#define HYDRA_CORE_BACKEND_VALUE_H

#include <cstdint>
#include <optional>

namespace hydra::core {

// Where a row sits against the activation's squeezed-out chord, compared by
// tick. NoSqOut when the activation did not squeeze out.
enum class SqOutPosition { NoSqOut, Before, Exact, After };

inline SqOutPosition sqout_position(int64_t row_tick,
                                    std::optional<int64_t> sqout_tick) {
    if (!sqout_tick) return SqOutPosition::NoSqOut;
    if (row_tick < *sqout_tick) return SqOutPosition::Before;
    if (row_tick == *sqout_tick) return SqOutPosition::Exact;
    return SqOutPosition::After;
}

// The SP walk pays every chord at or before the SP end (the window is
// inclusive at the deactivation node), before any backend pricing happens.
inline bool paid_by_sp_walk(double offset_ms) { return offset_ms <= 0.0; }

// Counted under Star Power with no squeeze: at or before the SP end, or
// less than the leeway (Rules::backend_leeway_ms) after it.
inline bool counted_without_squeeze(double offset_ms, double leeway_ms) {
    return paid_by_sp_walk(offset_ms) || offset_ms < leeway_ms;
}

// The SP score this row adds on this path. `points` is the row's full SP
// value, `sqout_points` what is left when it is the squeezed-out chord.
inline int backend_row_value(double offset_ms, int points, int sqout_points,
                             SqOutPosition pos, double leeway_ms) {
    if (pos == SqOutPosition::After) return 0;
    if (!counted_without_squeeze(offset_ms, leeway_ms)) return 0;
    return pos == SqOutPosition::Exact ? sqout_points : points;
}

}  // namespace hydra::core

#endif  // HYDRA_CORE_BACKEND_VALUE_H
```

- [ ] **Step 6: The details table prints the value**

In src/app/path_view.h, give `build_activations` the rules. Replace:

```cpp
ActivationsView build_activations(const Path& path, const HydraRecord& record,
                                  const SongTiming* timing,
                                  double hit_window_ms,
                                  std::optional<double> backend_limit_ms = std::nullopt);
```

with:

```cpp
ActivationsView build_activations(const Path& path, const HydraRecord& record,
                                  const SongTiming* timing,
                                  double hit_window_ms,
                                  std::optional<double> backend_limit_ms = std::nullopt,
                                  const core::Rules& rules = core::default_rules());
```

and add `#include "core/rules.h"` to its includes. In the same header, `BackendRowView`'s two comments become:

```cpp
    std::string points;   // what the engine paid for the row; 0 when uncounted
    std::string rating;   // summarystr + " (eff. ...)" + " <-- squeezed out (-N)" or " (uncounted)"
    bool warn = false;    // a squeezed-out row the engine counts (it costs points)
```

in place of:

```cpp
    std::string points;
    std::string rating;   // summarystr + " (eff. ...)" + " <-- squeezed out (-N)"
    bool warn = false;    // the squeezed-out row
```

Make the same signature change on the definition in src/app/path_view.cpp (without the default argument), and add `#include "core/backend_value.h"` there.

In src/app/path_view.cpp, replace:

```cpp
            row.points =
                std::to_string(br.squeezed_out ? bsq.sqout_points : bsq.points);
```

with:

```cpp
            // What the engine actually paid for this row on this path, from
            // the same function the search calls. display_backends already
            // dropped every row past the squeezed-out chord, so a row here is
            // either that chord or priced as if nothing was squeezed out.
            const double off = bsq.offset_ms.value_or(0.0);
            const bool counted =
                core::counted_without_squeeze(off, rules.backend_leeway_ms);
            const int value = core::backend_row_value(
                off, bsq.points, bsq.sqout_points,
                br.squeezed_out ? core::SqOutPosition::Exact
                                : core::SqOutPosition::NoSqOut,
                rules.backend_leeway_ms);
            row.points = std::to_string(value);
```

and replace:

```cpp
            if (br.squeezed_out) {
                char extra[48];
                std::snprintf(extra, sizeof(extra), " <-- squeezed out (-%d)",
                              bsq.points - bsq.sqout_points);
                row.rating += extra;
                row.warn = true;
            }
```

with:

```cpp
            if (br.squeezed_out) {
                // "(-N)" and the warning colour only when the squeeze-out
                // really costs points. A row the engine never counted costs
                // nothing either way (user decisions 1 and 19).
                char extra[48];
                if (counted) {
                    std::snprintf(extra, sizeof(extra),
                                  " <-- squeezed out (-%d)", bsq.points - value);
                    row.warn = true;
                } else {
                    std::snprintf(extra, sizeof(extra),
                                  " <-- squeezed out (uncounted)");
                }
                row.rating += extra;
            }
```

`counted` is used rather than `value == 0` so a counted squeeze-out whose leftover really is 0 still reads `(-N)`.

In src/ui/details_view.cpp, `render_activations_section`, replace:

```cpp
        app::ActivationsView view = app::build_activations(
            *path, record, timing,
            static_cast<double>(settings.hit_window_ms),
            settings.backend_limit());
```

with:

```cpp
        app::ActivationsView view = app::build_activations(
            *path, record, timing,
            static_cast<double>(settings.hit_window_ms),
            settings.backend_limit(), settings.rules);
```

`settings.rules` is the `core::Rules` Task 1 loads onto `app::Settings`.

- [ ] **Step 7: Run the unit and display tests and watch them pass**

```powershell
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe -tc="backend_row_value*,build_activations*"
```

Expected: `Status: SUCCESS!`.

- [ ] **Step 8: Pin the replay on the same shape, then switch the replay to the function**

Add to tests/test_replay.cpp, after `replay without Star Power scores no doubling at all`. It passes today: the replay already agrees with the engine here. It guards the rewrite below.

```cpp
// Round and Round's shape on a hand-built chart: a window whose squeeze-out
// sits on an R+Y phrase chord ~479 ms past the SP end. That chord is outside
// the window and outside the leeway, so it earns no doubling at all -- the
// squeeze-out changes nothing. The chord on the deactivation node is paid.
TEST_CASE("a squeezed-out chord past the leeway earns nothing") {
    // 4/4, 120 BPM, 192 ticks per beat: 768 ticks and 2000 ms per measure.
    Song song(192);
    song.tpm_changes[0] = 768;
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (int64_t tick : {0, 768, 1536, 2304, 3072, 3256}) {
        SongTimestamp ts;
        ts.timecode = song.timecode(tick);
        ts.chord.add_note(NoteColor::Red);
        ts.chord.add_note(NoteColor::Yellow);
        ts.flag_sp = tick == 3256;
        song.sequence.push_back(ts);
    }

    ReplayWindow w;
    w.act_tick = 0;
    w.deact_tick = 3072;
    w.sqout_offset_ms = song.timecode(3256).ms() - song.timecode(3072).ms();

    const ReplayResult r = replay_path(song, {w});
    REQUIRE(r.chords.size() == 6);
    CHECK(r.chords[4].in_sp);  // on the deactivation node: paid in full
    CHECK(r.chords[4].points.sp > 0);
    CHECK_FALSE(r.chords[5].in_sp);
    CHECK(r.chords[5].points.sp == 0);
}
```

Run it: `.\build-cpp\Release\hydra_tests.exe -tc="a squeezed-out chord past the leeway*"` -> passes.

`replay_path` already takes `const core::Rules& rules` (Task 1). In src/core/replay.h, comment rule 2 at the top ends with "(search/engine.cpp create_deactivated_path)". Change that to "(core/backend_value.h, the same function the search calls)".

In src/core/replay.cpp add `#include "core/backend_value.h"` and replace the loop, as Task 1 left it:

```cpp
        for (const Window& w : wins) {
            if (row.tick < w.act_tick) continue;
            const bool inclusive = row.tick <= w.deact_tick;
            const bool leeway = row.ms > w.deact_ms &&
                                row.ms < w.deact_ms + rules.backend_leeway_ms;
            if (!inclusive && !leeway) continue;

            if (w.has_sqout) {
                // Nothing past the squeezed-out note is under Star Power any
                // more; the note itself keeps all but its first hit's share.
                if (row.ms > w.sqout_ms + kSameNoteMs) continue;
                ++sp_claims;
                if (std::fabs(row.ms - w.sqout_ms) < kSameNoteMs) {
                    sp_points += sg.sp - sg.sqout_reduction;
                    continue;
                }
                sp_points += sg.sp;
                continue;
            }
            ++sp_claims;
            sp_points += sg.sp;
        }
```

with:

```cpp
        for (const Window& w : wins) {
            if (row.tick < w.act_tick) continue;
            // The row's offset from the SP end, as the graph measures it. A
            // chord on or before the deactivation node is inside the window
            // whatever its ms says.
            const double offset = row.tick <= w.deact_tick
                                      ? std::min(row.ms - w.deact_ms, 0.0)
                                      : row.ms - w.deact_ms;
            core::SqOutPosition pos = core::SqOutPosition::NoSqOut;
            if (w.has_sqout) {
                if (row.ms > w.sqout_ms + kSameNoteMs)
                    pos = core::SqOutPosition::After;
                else if (std::fabs(row.ms - w.sqout_ms) < kSameNoteMs)
                    pos = core::SqOutPosition::Exact;
                else
                    pos = core::SqOutPosition::Before;
            }
            if (pos == core::SqOutPosition::After ||
                !core::counted_without_squeeze(offset, rules.backend_leeway_ms))
                continue;
            ++sp_claims;
            sp_points += core::backend_row_value(
                offset, sg.sp, sg.sp - sg.sqout_reduction, pos,
                rules.backend_leeway_ms);
        }
```

The ms-based position test stays for now. Task 5 replaces it with the stored tick. Task 9 may later swap the `sg.sp - sg.sqout_reduction` argument for its own helper; the call stays.

Run: `.\build-cpp\Release\hydra_tests.exe -tc="*replay*"` -> `Status: SUCCESS!`.

- [ ] **Step 9: The engine calls the same function**

Task 1 already gave the `Engine` its `backend_leeway_ms_` member, read off `graph.rules()` in `run_search`, so no constructor change is needed here. In src/search/engine.cpp add `#include "core/backend_value.h"`. In `create_deactivated_path`, replace the loop as Task 1 left it:

```cpp
    int32_t sp_delta = 0;
    for (const BackendSqueeze& beo : eo->backends) {
        const int64_t be_tick = beo.timecode.ticks();
        const double be_offset = beo.offset_ms.value_or(0.0);
        const int32_t be_points = beo.points;
        const int32_t be_sqout_points = beo.sqout_points;

        const bool is_already_counted = be_offset <= 0;
        const bool is_leeway = be_offset > 0 && be_offset < backend_leeway_ms_;

        if (is_sq_out) {
            const bool is_before_sqout = be_tick < e.sqinout_time;
            const bool is_exact_sqout = be_tick == e.sqinout_time;
            const bool is_after_sqout = be_tick > e.sqinout_time;

            if (is_already_counted) {
                if (is_exact_sqout) {
                    sp_delta += -be_points + be_sqout_points;
                } else if (is_after_sqout) {
                    sp_delta += -be_points;
                }
            } else if (is_leeway) {
                if (is_before_sqout) {
                    sp_delta += be_points;
                } else if (is_exact_sqout) {
                    sp_delta += be_sqout_points;
                }
            }
        } else {
            if (is_leeway) sp_delta += be_points;
        }
    }
```

with:

```cpp
    // Each row adds what it is worth on this path minus what the SP walk
    // already paid for it. The walk pays rows at or before the SP end in
    // full; core::backend_row_value is the one place that says what a row
    // is worth, shared with the replay and the details table.
    const std::optional<int64_t> sqout_tick =
        is_sq_out ? std::optional<int64_t>(e.sqinout_time) : std::nullopt;
    int32_t sp_delta = 0;
    for (const BackendSqueeze& beo : eo->backends) {
        const double be_offset = beo.offset_ms.value_or(0.0);
        const core::SqOutPosition pos =
            core::sqout_position(beo.timecode.ticks(), sqout_tick);
        const int32_t already_paid =
            core::paid_by_sp_walk(be_offset) ? beo.points : 0;
        sp_delta += core::backend_row_value(be_offset, beo.points,
                                            beo.sqout_points, pos,
                                            backend_leeway_ms_) -
                    already_paid;
    }
```

- [ ] **Step 10: Run the whole suite**

```powershell
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe
```

Expected: `Status: SUCCESS!`. The two corpus replay tests are the fast proof that the engine and the replay still agree on every path.

- [ ] **Step 11: Prove the scores did not move and the search did not slow down**

```powershell
$d = "$env:TEMP\hydra_task3"
$a = "$d\after"
New-Item -ItemType Directory -Force $a | Out-Null
Remove-Item "$a\after.db" -ErrorAction SilentlyContinue
.\build_cpp.ps1 -Target hydra_batch
.\build_cpp.ps1 -Target hydra_bench
.\build-cpp\Release\hydra_batch.exe --db "$a\after.db" testdata\input |
  Select-String '^\[\d+/\d+\] ' | ForEach-Object { $_.Line -replace '^\[\d+/\d+\] ', '' } |
  Sort-Object | Set-Content "$a\batch_sorted.txt"
Compare-Object (Get-Content "$d\batch_sorted.txt") (Get-Content "$a\batch_sorted.txt")
$chart = "testdata\input\common\Summer Blast _25 Setlist\Tier 7\The Faceless - The Spiraling Void"
1..3 | ForEach-Object { .\build-cpp\Release\hydra_bench.exe $chart } | Select-String 'search' | Set-Content "$d\bench_after.txt"
Get-Content "$d\bench_before.txt", "$d\bench_after.txt"
```

Expected: `Compare-Object` prints nothing, and the two batch_sorted.txt files have the same line count. The smallest after "search" figure is no more than 1.05 times the smallest before figure.

If the diff prints anything, stop: the function is not the engine's rule, and the commit must not happen. If the search is more than 5% slower, stop too. Do not commit, do not build a workaround, and report both search figures to the user (user decision 26).

- [ ] **Step 12: Check the real details table headlessly**

Find a testdata chart whose best path, at the GUI test harness's settings (search depth 2, cap 4), has a squeezed-out row the engine does not count. Add this discovery test to tests/test_path_view.cpp:

```cpp
// Not an invariant: it names a chart the GUI test can open to see an
// uncounted squeezed-out row for real. Prints nothing when none exists.
TEST_CASE("find a chart with an uncounted squeezed-out row" * doctest::skip()) {
    AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 2;
    for (const std::string& path : corpus::chart_paths()) {
        AnalysisResult r;
        try {
            r = analyze_chart_file(path, settings);
        } catch (const std::exception&) {
            continue;
        }
        if (r.record.paths.empty()) continue;
        ActivationsView v = build_activations(r.record.best_path(), r.record,
                                              &r.song.timing(), 85.0);
        for (const ActivationDetailsView& av : v.acts)
            for (const BackendRowView& row : av.backends)
                if (row.rating.find("squeezed out (uncounted)") !=
                    std::string::npos) {
                    MESSAGE(path << " | " << av.header);
                    return;
                }
    }
    MESSAGE("no corpus chart has one at depth 2");
}
```

Run it on purpose (it is skipped by default):

```powershell
.\build-cpp\Release\hydra_tests.exe -tc="find a chart with an uncounted*" --no-skip
```

It prints a chart folder. Its song title (the library row's title, from that folder's song.ini `name`) is the search text for the GUI test below. It is a measured value, so the plan cannot name it ahead of time. Add this test to tests/ui/uitest_tests.cpp and register it as `{"squeezed_out_uncounted", test_squeezed_out_uncounted}` in `register_tests()`, putting the printed title where the `kTitle` string goes:

```cpp
// A squeezed-out row the engine never counted reads "0" and "(uncounted)"
// in the real details table, not the old "(-N)".
void test_squeezed_out_uncounted(ImGuiTestContext* ctx) {
    static const char* kTitle = "<the title Step 12's discovery test printed>";
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    ctx->ItemInputValue("##search", kTitle);
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == kTitle; }, 5));
    IM_CHECK(wait_until(ctx, [&] { return !h.app->current_page.rows.empty(); }, 5));
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    ctx->ItemClick("**/Analyze paths!");
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());
    // Activation headers are closed tree nodes; open every one.
    ctx->ItemOpenAll("**/Activations");
    ctx->Yield(2);
    const std::string text = visible_text(h);
    IM_CHECK(text.find("squeezed out (uncounted)") != std::string::npos);
}
```

If the discovery test prints "no corpus chart has one at depth 2", skip the GUI test and say so in the commit message; the display tests from Step 3 still pin the strings. If `ItemOpenAll` does not reach the headers, run a command file with `dump` on the Song Details window and click the header by the label it prints.

```powershell
.\build_cpp.ps1 -Target hydra_uitest
.\build-cpp\Release\hydra_uitest.exe --test squeezed_out_uncounted
```

Expected: `[PASS]`.

- [ ] **Step 13: Commit**

```powershell
git add src/core/backend_value.h src/search/engine.cpp src/core/replay.h src/core/replay.cpp src/app/path_view.h src/app/path_view.cpp src/ui/details_view.cpp tests/test_model.cpp tests/test_path_view.cpp tests/test_replay.cpp tests/ui/uitest_tests.cpp
git commit -m "Price every backend row through one function: engine, replay and details table" -m "Every backend row the engine does not count now reads 0 and is not highlighted; a squeezed-out one is tagged 'squeezed out (uncounted)' instead of claiming a (-N) loss. hydra_batch over testdata\input is identical before and after; the search time on The Spiraling Void is within 5%." -m "Task: Task 3: One function prices a backend row
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 4: One "counted without a squeeze" edge for the rating and the overfill flag

Today the rating and the engine disagree about plain backend rows between 2 and 3 ms past the SP end. The engine counts them: they are inside the 3 ms leeway. But `rate_activation` (src/core/squeeze_rating.cpp) treats any row past `kDifficultMs` (2 ms) as a late squeeze the frontend has to decide. So a 2.5 ms row can feed the late "Frontend timing scales" warning, and on a cap-clamped window it switches on the "SP overfilled" line. The row label in `BackendSqueeze::summarystr` (src/core/model.cpp) already uses the leeway, so today the same row reads "Standard" while the flag treats it as a squeeze. The user decided the leeway edge wins everywhere (user decision 7). This task makes the label, the late-row warning and the cap_clamped flag all ask `core::counted_without_squeeze` from Task 3, with the leeway from the rules. What the user sees: an activation whose only frontend-decided squeeze was a plain row 2-3 ms past the SP end no longer shows "SP overfilled", and that row no longer counts toward the late scale line. The late-SqOut warning on a free squeezed-out row stays exactly as it is; that branch is intentional.

Task 1 already gave `summarystr` a `double leeway_ms` parameter and deleted `kBackendLeewayMs`. This task gives `rate_activation` the same kind of parameter and routes both through the shared edge.

**Goal:** The row label, the late backend warning and the cap_clamped flag all use the same leeway edge the engine counts with.

**Files:** Changes src/core/model.cpp (`BackendSqueeze::summarystr`), src/core/squeeze_rating.h and squeeze_rating.cpp (`rate_activation`), and src/app/path_view.cpp (passes the leeway through). Adds a case to tests/test_squeeze_rating.cpp and fixes two comments there.

**Acceptance Criteria:**
- [ ] `rate_activation: a plain row inside the leeway is not a frontend squeeze` passes: a +2.5 ms plain row on a clamped activation gives `cap_clamped == false`, no late backend warn, and the label `"Standard"`; at +3.0 it gives `cap_clamped == true` and `"Hard (uncounted)"`.
- [ ] With a 2.0 ms leeway passed in, the same +2.5 ms row gives `cap_clamped == true` and `"Hard (uncounted)"`, so the edge really comes from the rules.
- [ ] `rate_activation: free squeezes read the opposite scale direction` still passes (the late-SqOut branch is untouched).
- [ ] `Get-ChildItem src,tests,tools -Recurse -File | Select-String -Pattern kBackendLeewayMs` finds nothing, and `Select-String -Path src\core\squeeze_rating.cpp -Pattern 'offset_ms > kDifficultMs'` finds nothing.
- [ ] The whole suite passes.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="rate_activation*"` -> `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test**

Add `#include "core/rules.h"` to tests/test_squeeze_rating.cpp after `#include "core/squeeze_rating.h"`, and this case after `rate_activation: cap_clamped flag`:

```cpp
// The engine counts a plain row less than the leeway past the SP end, so it
// is not a squeeze the frontend decides. The rating, the late-row warn and
// the overfill flag all use that same edge (user decision 7).
TEST_CASE("rate_activation: a plain row inside the leeway is not a frontend squeeze") {
    Activation act;
    act.skips = 0;
    act.e_offset = 300.0;  // not e-critical
    act.clamp_tick = 3072;
    // A late scale this small would make a row treated as late material:
    // 2.5 ms reads as 4.2 ms, past the 1 ms impact floor.
    act.transfer_post.late = 0.2;
    BackendSqueeze row;
    row.timecode = Timecode::raw(3080);
    row.offset_ms = 2.5;
    act.backends.push_back(row);

    ActivationRating r = rate_activation(act, nullptr, 85.0);
    CHECK_FALSE(r.cap_clamped);
    CHECK_FALSE(r.late_backend_warns);
    REQUIRE(r.backends.size() == 1);
    CHECK_FALSE(r.backends[0].effective_ms.has_value());
    CHECK(act.backends[0].summarystr(85.0) == "Standard");

    // At the leeway edge the row is uncounted, so the frontend decides it.
    act.backends[0].offset_ms = 3.0;
    r = rate_activation(act, nullptr, 85.0);
    CHECK(r.cap_clamped);
    CHECK(act.backends[0].summarystr(85.0) == "Hard (uncounted)");

    // The edge is the user's rule: a 2 ms leeway makes 2.5 ms uncounted.
    const double narrow = 2.0;
    REQUIRE(narrow != core::default_rules().backend_leeway_ms);
    act.backends[0].offset_ms = 2.5;
    r = rate_activation(act, nullptr, 85.0, narrow);
    CHECK(r.cap_clamped);
    CHECK(act.backends[0].summarystr(85.0, narrow) == "Hard (uncounted)");
}
```

- [ ] **Step 2: Run it and watch it fail**

```powershell
.\build_cpp.ps1 -Target hydra_tests
```

Expected: a compile error, because `rate_activation` does not take a leeway yet. Comment out the last block (the `narrow` part), rebuild, and run `.\build-cpp\Release\hydra_tests.exe -tc="rate_activation: a plain row inside*"`. Expected: FAIL on `CHECK_FALSE(r.cap_clamped)` and `CHECK_FALSE(r.late_backend_warns)`, because 2.5 is past `kDifficultMs`. Uncomment the block afterwards.

- [ ] **Step 3: The label reads the shared edge**

In src/core/model.cpp, add `#include "core/backend_value.h"`. In `BackendSqueeze::summarystr`, replace the line Task 1 left:

```cpp
    if (off < leeway_ms) return "Standard";
```

with:

```cpp
    // Counted by the engine with no squeeze: the same edge it prices with.
    if (core::counted_without_squeeze(off, leeway_ms)) return "Standard";
```

The two lines above it return first for every offset below -10, so this line only ever sees offsets from -10 up, and for those the result is the same as before. The point is that the label now names the engine's edge instead of restating it.

- [ ] **Step 4: The rating and the overfill flag read the rule**

In src/core/squeeze_rating.h, add `#include "core/rules.h"` and replace:

```cpp
ActivationRating rate_activation(const Activation& act,
                                 const SongTiming* timing,
                                 double hit_window_ms = kDefaultHitWindowMs);
```

with:

```cpp
// backend_leeway_ms: Rules::backend_leeway_ms. A plain row less than this
// past the SP end is counted by the engine, so it is not a late squeeze.
ActivationRating rate_activation(
    const Activation& act, const SongTiming* timing,
    double hit_window_ms = kDefaultHitWindowMs,
    double backend_leeway_ms = core::default_rules().backend_leeway_ms);
```

In the same header, the comment at the `cap_clamped` field that says `(squeezed_out, or offset > kDifficultMs)` becomes `(squeezed_out, or a plain row the engine does not count: offset at or past the leeway)`.

In src/core/squeeze_rating.cpp, add `#include "core/backend_value.h"`, change the definition's signature to match (without the defaults), and replace:

```cpp
            } else if (*bsq.offset_ms > kDifficultMs) {
```

with:

```cpp
            } else if (!core::counted_without_squeeze(*bsq.offset_ms,
                                                      backend_leeway_ms)) {
```

Leave the two branches above it alone. The `row.squeezed_out && *bsq.offset_ms > 0.0` branch is the intentional late-SqOut warning on a free squeezed-out row.

Replace:

```cpp
    // The cap-clamped flag fires when the activation has a clamp_tick AND at
    // least one squeeze the frontend decides: any SqIn/SqOut, or any backend
    // row that was squeezed out or sits past the difficult threshold.
```

with:

```cpp
    // The cap-clamped flag fires when the activation has a clamp_tick AND at
    // least one squeeze the frontend decides: any SqIn/SqOut, or any backend
    // row that was squeezed out or that the engine does not count (at or past
    // the leeway).
```

and replace:

```cpp
                if (br.squeezed_out ||
                    (br.row.offset_ms && *br.row.offset_ms > kDifficultMs)) {
```

with:

```cpp
                if (br.squeezed_out ||
                    (br.row.offset_ms &&
                     !core::counted_without_squeeze(*br.row.offset_ms,
                                                    backend_leeway_ms))) {
```

- [ ] **Step 5: The details table passes the leeway down**

In src/app/path_view.cpp (`build_activations` already has `rules` from Task 3), replace:

```cpp
        ActivationRating rate = rate_activation(act, timing, W);
```

with:

```cpp
        ActivationRating rate =
            rate_activation(act, timing, W, rules.backend_leeway_ms);
```

and replace:

```cpp
            row.rating = bsq.summarystr(W);
```

with:

```cpp
            row.rating = bsq.summarystr(W, rules.backend_leeway_ms);
```

- [ ] **Step 6: Fix the test comments that name the old floor**

Task 1 already deleted `kBackendLeewayMs` and rewrote the replay.h comment that named it. Two comments in tests/test_squeeze_rating.cpp still describe the old 2 ms floor. Change "A row at or under the difficult floor never engages the late scale." to "A row inside the backend leeway never engages the late scale.". In `rate_activation: cap_clamped flag`, change "inside the kDifficultMs floor, so it isn't a squeeze the frontend decides" to "inside the SP window, so the engine counts it and it isn't a squeeze the frontend decides".

```powershell
Get-ChildItem src,tests,tools -Recurse -File | Select-String -Pattern kBackendLeewayMs
Select-String -Path src\core\squeeze_rating.cpp -Pattern 'offset_ms > kDifficultMs'
```

Expected: no output from either.

- [ ] **Step 7: Run it and watch it pass**

```powershell
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe
```

Expected: `Status: SUCCESS!`.

- [ ] **Step 8: Commit**

```powershell
git add src/core/model.cpp src/core/squeeze_rating.h src/core/squeeze_rating.cpp src/app/path_view.cpp tests/test_squeeze_rating.cpp
git commit -m "Rate plain backend rows by the engine's leeway edge, not the 2 ms floor" -m "A plain row 2-3 ms past the SP end is counted by the engine, so it no longer trips the late scale line or the SP overfilled flag. The label, the warn and the flag all ask core::counted_without_squeeze with Rules::backend_leeway_ms." -m "Task: Task 4: One counted-without-a-squeeze edge
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 5: Find the squeezed-out chord by its stored tick

Today four places answer "is this the squeezed-out chord, or past it?" in four ways. The engine compares ticks, which is right. `Activation::is_sqout_backend` and `Activation::display_backends` (src/core/model.cpp) compare float offsets within a 0.01 ms epsilon. The replay has its own `kSameNoteMs = 0.01` (src/core/replay.cpp). The corpus test `no activation keeps backends past its squeezed-out note` (tests/test_search.cpp) uses a fourth 0.01 comparison. Task 2 stores the squeezed-out chord's tick on the activation (`Activation::sqout_tick`). This task makes every one of them compare ticks against it. Backend rows already carry their own tick (`BackendSqueeze::timecode`, written to the blob by path_binary.cpp), so no row field is needed. The engine itself needs no change: its copy-out trim and `create_deactivated_path` already compare ticks, against the same edge tick Task 2 stamps.

`hydra_replay` changes visibly in four ways. A path read from a dump file now carries `sqout_tick`, and scoring compares ticks. A hand-typed `--acts` SqOut offset (or an old dump without `sqout_tick`) is matched to the nearest phrase chord within 500 ms of the SP end, and the tool prints which chord it chose on stderr, with its tick and its real offset. If that chord is not the one the engine would squeeze out, the tool refuses: it prints an error naming the typed chord and the engine's chord, prices nothing, and exits 1 (user decision 23). The engine only ever squeezes out the first phrase chord strictly within 500 ms of the SP end, so any other chord is a squeeze-out the search can never produce. Last, the "this window may hide a squeeze-out" warning uses that same rule for which chord it names, rather than the last phrase chord inside the window. In the GUI nothing changes for fresh records. A record from before Task 2's format reads Stale (user decision 5), and until it is re-analyzed its squeezed-out row carries no tag, because nothing guesses which row it was.

**Goal:** The display, the replay, the tool and the tests all identify the squeezed-out chord by comparing ticks with `Activation::sqout_tick`, and the tool refuses a typed squeeze-out the engine could never make.

**Files:** Changes src/core/model.h and model.cpp (`is_sqout_backend`, `display_backends`), src/core/replay.h and replay.cpp (`ReplayWindow`, `replay_path`, `windows_for_path`, `windows_from_json`, `ambiguous_window_warnings`, and a new `resolve_sqout_note`), and tools/replay.cpp (`paths_json`, `cmd_score`, usage text). Updates hand-built fixtures in tests/test_model.cpp, tests/test_path_view.cpp and tests/test_squeeze_rating.cpp, and adds cases to tests/test_replay.cpp and tests/test_search.cpp.

**Acceptance Criteria:**
- [ ] `Select-String -Path src\core\model.cpp,src\core\replay.cpp,tests\test_search.cpp,tests\test_path_view.cpp,tests\test_replay.cpp,tests\test_model.cpp -Pattern '0\.01|kSameNoteMs'` finds nothing. (The search is limited to these files because unrelated tests use `.epsilon(0.01)`.)
- [ ] `a typed squeeze-out offset resolves to the phrase chord` passes: `-93.73` resolves to tick 3036 at `-93.75` ms, and the score equals the one priced with `sqout_tick = 3036` directly.
- [ ] `a typed squeeze-out on a chord the engine never squeezes out is refused` passes: with phrase chords at ticks 2928 and 3036, a typed `-93.73` throws exactly `window 0:3072: the SqOut offset -93.73 ms lands on the phrase chord at tick 3036 (-93.75 ms from the SP end), which the engine never squeezes out. The only chord it can squeeze out here is the first phrase chord within 500 ms of the SP end, at tick 2928 (-375.00 ms). Not priced.`, and a typed `-375.0` resolves to tick 2928.
- [ ] `the squeeze-out warning names the chord the graph would squeeze` passes: with phrase chords 375 ms and 125 ms before the SP end, the warning names tick 2928 (the 375 ms one); a phrase chord exactly 500 ms before gives no warning; a phrase chord 2.6 ms after the SP end (inside the leeway) gives one.
- [ ] `no activation keeps backends past its squeezed-out note` passes with tick comparisons and also finds exactly one row at `sqout_tick` on every squeeze-out activation.
- [ ] `replay reproduces the engine's score for every corpus path` still passes.
- [ ] `hydra_replay score <chart> --acts "A:D:<offset>"` prints a `note: window A:D SqOut <offset> ms -> phrase chord at tick T (<real offset> ms from the SP end)` line on stderr, for real ticks A, D and T from a dump, with T equal to the dump's `sqout_tick`.
- [ ] The whole suite passes.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="*squeeze*,*replay*,*sqout*,display_backends*,build_activations*"` -> `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing replay tests**

Add to tests/test_replay.cpp. The helper builds the same hand-made chart shape as Task 3's replay test.

```cpp
namespace {

// 4/4, 120 BPM, 192 ticks per beat: 768 ticks and 2000 ms per measure, so
// 36 ticks are exactly 93.75 ms and 192 ticks exactly 500 ms.
Song song_with(const std::vector<std::pair<int64_t, bool>>& chords) {
    Song song(192);
    song.tpm_changes[0] = 768;
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (const auto& [tick, phrase] : chords) {
        SongTimestamp ts;
        ts.timecode = song.timecode(tick);
        ts.chord.add_note(NoteColor::Red);
        ts.chord.add_note(NoteColor::Yellow);
        ts.flag_sp = phrase;
        song.sequence.push_back(ts);
    }
    return song;
}

}  // namespace

// A typed offset is only ever an approximation of a chord that sits on a
// tick. The tool resolves it to the phrase chord it means and says which.
TEST_CASE("a typed squeeze-out offset resolves to the phrase chord") {
    const Song song = song_with(
        {{0, false}, {768, false}, {1536, false}, {3036, true}, {3072, false}});

    ReplayWindow typed;
    typed.act_tick = 0;
    typed.deact_tick = 3072;
    typed.sqout_offset_ms = -93.73;  // what a person copies off a screen

    const SqOutNote n = resolve_sqout_note(song, typed);
    CHECK(n.tick == 3036);
    CHECK(n.offset_ms == doctest::Approx(-93.75));

    typed.sqout_tick = n.tick;
    ReplayWindow exact;
    exact.act_tick = 0;
    exact.deact_tick = 3072;
    exact.sqout_tick = 3036;
    CHECK(replay_path(song, {typed}).final == replay_path(song, {exact}).final);

    // No phrase chord within 500 ms of the SP end: nothing to resolve to.
    const Song bare = song_with({{0, false}, {3072, false}});
    CHECK_THROWS(resolve_sqout_note(bare, typed));

    // replay_path refuses an offset it was never told the chord for.
    ReplayWindow unresolved;
    unresolved.act_tick = 0;
    unresolved.deact_tick = 3072;
    unresolved.sqout_offset_ms = -93.75;
    CHECK_THROWS(replay_path(song, {unresolved}));
}

// The engine only ever squeezes out the first phrase chord strictly within
// 500 ms of the SP end. A typed offset that lands on a later one names a
// squeeze-out the search can never produce, so it is refused and nothing is
// priced (user decision 23).
TEST_CASE("a typed squeeze-out on a chord the engine never squeezes out is refused") {
    // Phrase chords 375 ms (tick 2928) and 93.75 ms (tick 3036) before D.
    const Song two = song_with({{0, false}, {768, false}, {2928, true},
                                {3036, true}, {3072, false}});

    ReplayWindow late;
    late.act_tick = 0;
    late.deact_tick = 3072;
    late.sqout_offset_ms = -93.73;
    CHECK_THROWS_WITH_AS(
        resolve_sqout_note(two, late),
        "window 0:3072: the SqOut offset -93.73 ms lands on the phrase chord "
        "at tick 3036 (-93.75 ms from the SP end), which the engine never "
        "squeezes out. The only chord it can squeeze out here is the first "
        "phrase chord within 500 ms of the SP end, at tick 2928 (-375.00 ms). "
        "Not priced.",
        std::runtime_error);

    // The engine's own chord is accepted.
    ReplayWindow first = late;
    first.sqout_offset_ms = -375.0;
    const SqOutNote n = resolve_sqout_note(two, first);
    CHECK(n.tick == 2928);
    CHECK(n.offset_ms == doctest::Approx(-375.0));
}

// The graph lets a deactivation squeeze out exactly one chord: the first
// phrase chord strictly within 500 ms of the SP end (graph.cpp
// add_deact_edge, then store_new_backend for chords after the end). The
// warning names that chord, and only when the window actually paid it.
TEST_CASE("the squeeze-out warning names the chord the graph would squeeze") {
    // Phrase chords 375 ms (tick 2928) and 125 ms (tick 3024) before D.
    const Song two = song_with({{0, false}, {768, false}, {2928, true},
                                {3024, true}, {3072, false}});
    ReplayWindow w;
    w.act_tick = 0;
    w.deact_tick = 3072;
    const ReplayResult r = replay_path(two, {w});
    const std::vector<std::string> warned = ambiguous_window_warnings(two, r, {w});
    REQUIRE(warned.size() == 1);
    CHECK(warned[0].find("tick 2928") != std::string::npos);

    // Exactly 500 ms before D is outside the graph's window: no warning.
    const Song edge = song_with({{0, false}, {768, false}, {2880, true},
                                 {3072, false}});
    const ReplayResult re = replay_path(edge, {w});
    CHECK(ambiguous_window_warnings(edge, re, {w}).empty());

    // A phrase chord one tick (2.6 ms) after D is inside the leeway, so the
    // window paid it and squeezing it out would change the score.
    const Song after = song_with({{0, false}, {768, false}, {3072, false},
                                  {3073, true}});
    const ReplayResult ra = replay_path(after, {w});
    const std::vector<std::string> late = ambiguous_window_warnings(after, ra, {w});
    REQUIRE(late.size() == 1);
    CHECK(late[0].find("tick 3073") != std::string::npos);
}
```

In the same file, update `a path JSON becomes windows with the squeeze-out offset intact`. Replace:

```cpp
        {"act_tick": 7680, "deact_tick": 11520,
         "sqinouts": [{"kind": "SqIn",  "offset_ms": 12.5},
                      {"kind": "SqOut", "offset_ms": 31.25}]}
```

with:

```cpp
        {"act_tick": 7680, "deact_tick": 11520, "sqout_tick": 11532,
         "sqinouts": [{"kind": "SqIn",  "offset_ms": 12.5},
                      {"kind": "SqOut", "offset_ms": 31.25}]}
```

and after `CHECK(*w[1].sqout_offset_ms == doctest::Approx(31.25));` add:

```cpp
    CHECK_FALSE(w[0].sqout_tick.has_value());
    REQUIRE(w[1].sqout_tick.has_value());
    CHECK(*w[1].sqout_tick == 11532);
```

In `windows read from a path JSON match the ones read from the record`, replace:

```cpp
                acts.push_back(json{
                    {"act_tick", act.timecode ? act.timecode->ticks() : -1},
                    {"deact_tick", act.deact_tick ? *act.deact_tick : -1},
                    {"sqinouts", sq}});
```

with:

```cpp
                acts.push_back(json{
                    {"act_tick", act.timecode ? act.timecode->ticks() : -1},
                    {"deact_tick", act.deact_tick ? *act.deact_tick : -1},
                    {"sqout_tick", act.sqout_tick ? *act.sqout_tick : -1},
                    {"sqinouts", sq}});
```

and after `CHECK(got[i].sqout_offset_ms == want[i].sqout_offset_ms);` add `CHECK(got[i].sqout_tick == want[i].sqout_tick);`.

In `a window ending on a phrase note with no offset is flagged`, the "just after" search uses `<=`. The graph's window is strict, so replace:

```cpp
            c.ms - phrase_note->ms <= kSqueezeWindowMs)
```

with:

```cpp
            c.ms - phrase_note->ms < kSqueezeWindowMs)
```

In Task 3's `a squeezed-out chord past the leeway earns nothing`, after the `w.sqout_offset_ms = ...` line add `w.sqout_tick = 3256;`.

- [ ] **Step 2: Tighten the corpus test to ticks**

In tests/test_search.cpp, `no activation keeps backends past its squeezed-out note`, replace:

```cpp
                std::optional<double> sqout;
                for (const SPSqueeze& sq : act.sqinouts)
                    if (sq.kind == SqueezeKind::SqOut &&
                        (!sqout || sq.offset() < *sqout))
                        sqout = sq.offset();
                if (!sqout) continue;
                ++sqout_acts;

                for (const BackendSqueeze& b : act.backends) {
                    if (b.offset_ms.value_or(0.0) > *sqout + 0.01) {
                        d = "backend past the sqout note";
                        break;
                    }
                }
```

with:

```cpp
                bool has_sqout = false;
                for (const SPSqueeze& sq : act.sqinouts)
                    if (sq.kind == SqueezeKind::SqOut) has_sqout = true;
                if (!has_sqout) continue;
                ++sqout_acts;

                // The engine stamps the squeezed-out chord's tick (record
                // v6); everything is compared by tick, never by ms.
                if (!act.sqout_tick) {
                    d = "squeeze-out activation missing sqout_tick";
                    break;
                }
                int on_sqout = 0;
                for (const BackendSqueeze& b : act.backends) {
                    if (b.timecode.ticks() > *act.sqout_tick) {
                        d = "backend past the sqout note";
                        break;
                    }
                    if (b.timecode.ticks() == *act.sqout_tick) ++on_sqout;
                }
                if (d.empty() && on_sqout != 1)
                    d = "not exactly one backend row on the sqout tick";
```

- [ ] **Step 3: Give the hand-built fixtures their ticks**

These three fixtures pair a SqOut with a row by matching offsets. After this task the match is by tick, so each row gets a tick and each activation its `sqout_tick`.

In tests/test_model.cpp, `display_backends drops rows beyond a squeeze out`, replace:

```cpp
    const std::vector<double> offsets = {-368.1, -184.0, 0.0, 184.0, 368.1};
    for (double off : offsets) {
        BackendSqueeze bsq;
        bsq.points = 50;
        bsq.offset_ms = off;
        a.backends.push_back(bsq);
    }
```

with:

```cpp
    const std::vector<double> offsets = {-368.1, -184.0, 0.0, 184.0, 368.1};
    int64_t tick = 100;  // chart order, one row per 100 ticks
    for (double off : offsets) {
        BackendSqueeze bsq;
        bsq.timecode = Timecode::raw(tick);
        tick += 100;
        bsq.points = 50;
        bsq.offset_ms = off;
        a.backends.push_back(bsq);
    }
```

and replace:

```cpp
    a.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -184.0});
```

with:

```cpp
    a.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -184.0});
    a.sqout_tick = 200;  // the -184.0 row
```

In tests/test_path_view.cpp, `build_activations: the backend limit hides far rows but never squeezed-out ones`, replace:

```cpp
    for (double ms : {-30.0, -100.0, 60.0}) {
        BackendSqueeze row;
        row.offset_ms = ms;
        act.backends.push_back(row);
    }
    // Matches the +60 row (is_sqout_backend compares offsets within 0.01), so
    // that row is the squeezed-out one. It has to be the last row in chart
    // order: nothing can be a backend past the note squeezed out of SP, and
    // display_backends drops any row that claims to be.
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 60.0});
```

with:

```cpp
    const std::pair<double, int64_t> rows_at[] = {{-30.0, 200}, {-100.0, 100},
                                                   {60.0, 300}};
    for (const auto& [ms, tick] : rows_at) {
        BackendSqueeze row;
        row.timecode = Timecode::raw(tick);
        row.offset_ms = ms;
        act.backends.push_back(row);
    }
    // sqout_tick names the +60 row (tick 300), so that row is the
    // squeezed-out one. It has to be the last row in chart order: nothing can
    // be a backend past the note squeezed out of SP, and display_backends
    // drops any row that claims to be.
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 60.0});
    act.sqout_tick = 300;
```

The checks Task 3 changed at the end of that test stay as Task 3 left them.

In tests/test_squeeze_rating.cpp, `rate_activation: free squeezes read the opposite scale direction`, replace:

```cpp
    BackendSqueeze out_row;
    out_row.offset_ms = 300.0;  // matches the sqout offset within 0.01
    freeout.backends.push_back(out_row);
```

with:

```cpp
    BackendSqueeze out_row;
    out_row.timecode = Timecode::raw(3187);
    out_row.offset_ms = 300.0;
    freeout.backends.push_back(out_row);
    freeout.sqout_tick = 3187;  // this row is the squeezed-out chord
```

and replace:

```cpp
    BackendSqueeze hard_row;
    hard_row.offset_ms = -50.0;
    hardout.backends.push_back(hard_row);
```

with:

```cpp
    BackendSqueeze hard_row;
    hard_row.timecode = Timecode::raw(3053);
    hard_row.offset_ms = -50.0;
    hardout.backends.push_back(hard_row);
    hardout.sqout_tick = 3053;
```

- [ ] **Step 4: Run them and watch them fail**

```powershell
.\build_cpp.ps1 -Target hydra_tests
```

Expected: compile errors for `ReplayWindow::sqout_tick`, `SqOutNote` and `resolve_sqout_note`. The fixture updates compile (Task 2 added `Activation::sqout_tick`) and pass even now, because offsets still match too.

- [ ] **Step 5: The model compares ticks**

In src/core/model.cpp, replace:

```cpp
bool Activation::is_sqout_backend(const BackendSqueeze& bsq) const {
    for (const SPSqueeze& sq : sqinouts) {
        if (sq.kind == SqueezeKind::SqOut &&
            std::fabs(bsq.offset_ms.value_or(0.0) - sq.offset()) < 0.01)
            return true;
    }
    return false;
}
```

with:

```cpp
// The engine stamps the squeezed-out chord's tick at copy-out (record v6).
// A record without it is Stale and is never guessed at.
bool Activation::is_sqout_backend(const BackendSqueeze& bsq) const {
    return sqout_tick.has_value() && bsq.timecode.ticks() == *sqout_tick;
}
```

and replace:

```cpp
    // Nothing past a squeezed-out note can be a backend: the sqout note is hit
    // after SP has ended, and every later note is hit after that one. The
    // engine's record build drops those rows now, but records stored before
    // that fix carry them inside their blobs, so the display guards again
    // here. Both a row's offset and sq.offset() are measured against the same
    // deactivation node, so comparing offsets is comparing chart order; 0.01
    // is the same epsilon is_sqout_backend uses to spot the sqout row itself,
    // which stays.
    auto is_beyond_sqout = [this](const BackendSqueeze& bsq) {
        for (const SPSqueeze& sq : sqinouts) {
            if (sq.kind == SqueezeKind::SqOut &&
                bsq.offset_ms.value_or(0.0) > sq.offset() + 0.01)
                return true;
        }
        return false;
    };
```

with:

```cpp
    // Nothing past a squeezed-out note can be a backend: the sqout note is hit
    // after SP has ended, and every later note is hit after that one. The
    // engine's copy-out already drops those rows; this keeps the display
    // honest for any list that still holds one. Chart order is tick order.
    auto is_beyond_sqout = [this](const BackendSqueeze& bsq) {
        return sqout_tick.has_value() && bsq.timecode.ticks() > *sqout_tick;
    };
```

In src/core/model.h, the comment above `is_sqout_backend` ends with "hydata.Activation.is_sqout_backend.", which points at Python code that no longer exists. Replace that comment with `// True for the row sitting on sqout_tick, the chord this activation squeezed out.`

- [ ] **Step 6: The replay compares ticks, and a typed offset is resolved or refused**

In src/core/replay.h, replace the `ReplayWindow` body's squeeze-out member:

```cpp
    // Set when the activation ends on a squeeze-out: the SqOut phrase note's
    // offset from D, in ms (the value stored on the activation's SPSqueeze).
    // The note is hit after Star Power has ended, so it and everything after
    // it inside the window lose their doubling, and the note itself keeps
    // only what its non-first hits are worth (CategoryScores::sqout_reduction
    // is the first hit's share). Unset for a plain deactivation.
    std::optional<double> sqout_offset_ms;
```

with:

```cpp
    // Set when the activation ends on a squeeze-out: the tick of the phrase
    // chord squeezed out (Activation::sqout_tick). That chord is hit after
    // Star Power has ended, so it and everything after it lose their
    // doubling, and the chord itself keeps only what its non-first hits are
    // worth (CategoryScores::sqout_reduction is the first hit's share).
    std::optional<int64_t> sqout_tick;

    // The same squeeze-out as an ms offset from D, for display, and as typed
    // by hand in `--acts`. replay_path reads only the tick; a window with an
    // offset and no tick must go through resolve_sqout_note first.
    std::optional<double> sqout_offset_ms;
```

Add after `ReplayWindow`:

```cpp
// The phrase chord a typed SqOut offset means.
struct SqOutNote {
    int64_t tick = 0;
    double offset_ms = 0.0;  // its real offset from D
};

// Resolve w.sqout_offset_ms to the phrase chord nearest D + offset, among
// the phrase chords strictly within kSqueezeWindowMs of D on either side.
// The engine only ever squeezes out the first of those, so when the nearest
// one is any other chord this refuses (user decision 23). Throws
// std::runtime_error, with a message naming both chords, in that case; also
// when there is no candidate, or when w has no offset.
SqOutNote resolve_sqout_note(const Song& song, const ReplayWindow& w);
```

Update the `replay_path` comment to add: "Throws std::invalid_argument for a window with a SqOut offset but no SqOut tick."

In src/core/replay.cpp, delete:

```cpp
// Two chart positions count as the same note when their ms agree this
// closely. The engine compares ticks; a replay only has the SqOut's offset in
// ms, and distinct chord ticks are never this close in real charts.
constexpr double kSameNoteMs = 0.01;
```

In the private `Window` struct replace:

```cpp
    bool has_sqout = false;
    double sqout_ms = 0.0;
```

with:

```cpp
    std::optional<int64_t> sqout_tick;
```

In `replay_path` replace:

```cpp
        if (w.sqout_offset_ms) {
            win.has_sqout = true;
            win.sqout_ms = win.deact_ms + *w.sqout_offset_ms;
        }
```

with:

```cpp
        if (w.sqout_offset_ms && !w.sqout_tick)
            throw std::invalid_argument(
                "window " + std::to_string(w.act_tick) + ":" +
                std::to_string(w.deact_tick) +
                " has a SqOut offset but no SqOut chord; resolve it with "
                "resolve_sqout_note first");
        win.sqout_tick = w.sqout_tick;
```

and in the loop Task 3 wrote, replace:

```cpp
            core::SqOutPosition pos = core::SqOutPosition::NoSqOut;
            if (w.has_sqout) {
                if (row.ms > w.sqout_ms + kSameNoteMs)
                    pos = core::SqOutPosition::After;
                else if (std::fabs(row.ms - w.sqout_ms) < kSameNoteMs)
                    pos = core::SqOutPosition::Exact;
                else
                    pos = core::SqOutPosition::Before;
            }
```

with:

```cpp
            const core::SqOutPosition pos =
                core::sqout_position(row.tick, w.sqout_tick);
```

In `windows_for_path`, replace:

```cpp
        for (const SPSqueeze& sq : act.sqinouts)
            if (sq.kind == SqueezeKind::SqOut) w.sqout_offset_ms = sq.offset();
        out.push_back(w);
```

with:

```cpp
        for (const SPSqueeze& sq : act.sqinouts)
            if (sq.kind == SqueezeKind::SqOut) w.sqout_offset_ms = sq.offset();
        // A squeeze-out with no stored chord tick is a record from before v6.
        // It is skipped like one with no deact node: never guessed at.
        if (w.sqout_offset_ms && !act.sqout_tick) continue;
        w.sqout_tick = act.sqout_tick;
        out.push_back(w);
```

In `windows_from_json`, after the `deact_tick` checks and before the `sqinouts` loop, add:

```cpp
        // -1 (or no key, from a dump written before v6) means "not stamped".
        // The caller resolves a bare offset with resolve_sqout_note.
        if (act.contains("sqout_tick") && act["sqout_tick"].is_number() &&
            act["sqout_tick"].get<int64_t>() >= 0)
            w.sqout_tick = act["sqout_tick"].get<int64_t>();
```

Add the shared candidate helper to the anonymous namespace at the top of replay.cpp (next to the `Window` struct), and `resolve_sqout_note` below `windows_from_json`:

```cpp
// The phrase chords the graph could squeeze out at deactivation node D: the
// ones strictly within kSqueezeWindowMs of D, in chart order. The graph takes
// the first of them (graph.cpp add_deact_edge for chords up to D, then
// store_new_backend for chords after it).
std::vector<const SongTimestamp*> sqout_candidates(const Song& song,
                                                   int64_t deact_tick) {
    const double d_ms = song.timing().timecode(deact_tick).ms();
    std::vector<const SongTimestamp*> out;
    for (const SongTimestamp& ts : song.sequence)
        if (ts.flag_sp && std::fabs(ts.timecode.ms() - d_ms) < kSqueezeWindowMs)
            out.push_back(&ts);
    return out;
}
```

```cpp
SqOutNote resolve_sqout_note(const Song& song, const ReplayWindow& w) {
    const std::string where = "window " + std::to_string(w.act_tick) + ":" +
                              std::to_string(w.deact_tick);
    if (!w.sqout_offset_ms)
        throw std::runtime_error(where + " has no SqOut offset to resolve");
    const double d_ms = song.timing().timecode(w.deact_tick).ms();
    const double want_ms = d_ms + *w.sqout_offset_ms;

    const std::vector<const SongTimestamp*> cands =
        sqout_candidates(song, w.deact_tick);
    const SongTimestamp* best = nullptr;
    for (const SongTimestamp* ts : cands)
        if (!best || std::fabs(ts->timecode.ms() - want_ms) <
                         std::fabs(best->timecode.ms() - want_ms))
            best = ts;

    char buf[512];
    if (!best) {
        std::snprintf(buf, sizeof(buf),
                      "%s has a SqOut offset of %.2f ms but no Star Power "
                      "phrase chord within %.0f ms of its SP end",
                      where.c_str(), *w.sqout_offset_ms, kSqueezeWindowMs);
        throw std::runtime_error(buf);
    }
    const SqOutNote typed{best->timecode.ticks(), best->timecode.ms() - d_ms};

    // The engine squeezes out only the first candidate. Anything else is a
    // squeeze-out the search can never produce: refuse, never price it.
    const SongTimestamp* engine = cands.front();
    if (best != engine) {
        std::snprintf(
            buf, sizeof(buf),
            "%s: the SqOut offset %.2f ms lands on the phrase chord at tick "
            "%lld (%.2f ms from the SP end), which the engine never squeezes "
            "out. The only chord it can squeeze out here is the first phrase "
            "chord within %.0f ms of the SP end, at tick %lld (%.2f ms). "
            "Not priced.",
            where.c_str(), *w.sqout_offset_ms, (long long)typed.tick,
            typed.offset_ms, kSqueezeWindowMs,
            (long long)engine->timecode.ticks(), engine->timecode.ms() - d_ms);
        throw std::runtime_error(buf);
    }
    return typed;
}
```

Replace the body of `ambiguous_window_warnings`'s loop, from `if (w.sqout_offset_ms) continue;  // the offset settles the question` through the `out.push_back(...)` that ends it, with:

```cpp
        // A squeeze-out offset or chord settles the question.
        if (w.sqout_offset_ms || w.sqout_tick) continue;

        // The one chord the graph could squeeze out at this D.
        const std::vector<const SongTimestamp*> cands =
            sqout_candidates(song, w.deact_tick);
        if (cands.empty()) continue;
        const int64_t tick = cands.front()->timecode.ticks();

        // Only a chord the window paid can make the score too high: one at or
        // before D, or inside the leeway after it.
        const ReplayChord* chord = nullptr;
        for (const ReplayChord& c : result.chords)
            if (c.tick == tick) chord = &c;
        if (!chord || !chord->in_sp) continue;

        const double deact_ms = timing.timecode(w.deact_tick).ms();
        std::string where = "on the Star Power phrase note at tick " +
                            std::to_string(tick);
        char gap[32];
        if (tick < w.deact_tick) {
            std::snprintf(gap, sizeof(gap), "%.2f", deact_ms - chord->ms);
            where = "just after the Star Power phrase note at tick " +
                    std::to_string(tick) + " (" + gap + " ms earlier)";
        } else if (tick > w.deact_tick) {
            std::snprintf(gap, sizeof(gap), "%.2f", chord->ms - deact_ms);
            where = "just before the Star Power phrase note at tick " +
                    std::to_string(tick) + " (" + gap + " ms later)";
        }

        out.push_back("window " + std::to_string(w.act_tick) + ":" +
                      std::to_string(w.deact_tick) + " ends " + where +
                      " with no squeeze-out offset; if the player squeezed it "
                      "out, this score is high by that note's first-hit share");
```

Update the comment above `ambiguous_window_warnings` in replay.h. The chord it names is "the first phrase chord strictly within kSqueezeWindowMs of the deactivation node, the one the graph would squeeze out", and it warns only when the window paid that chord.

- [ ] **Step 7: The tool writes the tick, says what it resolved, and refuses the impossible**

In tools/replay.cpp `paths_json`, replace:

```cpp
                {"deact_tick", d ? *d : -1},
```

with:

```cpp
                {"deact_tick", d ? *d : -1},
                {"sqout_tick", act.sqout_tick ? *act.sqout_tick : -1},
```

In `cmd_score`, replace the lines as Task 1 left them:

```cpp
    const std::vector<ReplayWindow> windows =
        a.path.empty() ? parse_acts(a.acts)
                       : windows_from_file(a.path, a.index);
    const ReplayResult r = replay_path(song, windows, s.rules);
```

with:

```cpp
    std::vector<ReplayWindow> windows =
        a.path.empty() ? parse_acts(a.acts)
                       : windows_from_file(a.path, a.index);
    // A typed offset (or a dump from before sqout_tick existed) names a chord
    // only approximately. Resolve it and say which chord was used, so a typo
    // cannot quietly price a different squeeze-out. resolve_sqout_note throws
    // for a chord the engine never squeezes out; main prints "error: ..." and
    // exits 1, so nothing is priced.
    for (ReplayWindow& w : windows) {
        if (!w.sqout_offset_ms || w.sqout_tick) continue;
        const SqOutNote n = resolve_sqout_note(song, w);
        std::fprintf(stderr,
                     "note: window %lld:%lld SqOut %.2f ms -> phrase chord at "
                     "tick %lld (%.2f ms from the SP end)\n",
                     (long long)w.act_tick, (long long)w.deact_tick,
                     *w.sqout_offset_ms, (long long)n.tick, n.offset_ms);
        w.sqout_tick = n.tick;
    }
    const ReplayResult r = replay_path(song, windows, s.rules);
```

`s` is the `app::Settings` `cmd_score` already builds with `settings_from(a)`, which Task 1 makes load `--rules`. In the `acts` JSON below, after the `sqout_offset_ms` line, add:

```cpp
        if (w.sqout_tick) one["sqout_tick"] = *w.sqout_tick;
```

In `usage()`, where the optional third `--acts` field is described, add one line: "A typed SqOut offset is matched to the nearest phrase chord within 500 ms of the SP end and the chord used is printed on stderr; a chord the engine would never squeeze out is refused."

- [ ] **Step 8: Run everything and watch it pass**

```powershell
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe
Select-String -Path src\core\model.cpp,src\core\replay.cpp,tests\test_search.cpp,tests\test_path_view.cpp,tests\test_replay.cpp,tests\test_model.cpp -Pattern '0\.01|kSameNoteMs'
```

Expected: `Status: SUCCESS!`, and the search prints nothing.

- [ ] **Step 9: Check the tool by hand on a real squeeze-out**

```powershell
.\build_cpp.ps1 -Target hydra_replay
$chart = (Get-ChildItem testdata\input -Recurse -Filter notes.mid | Select-Object -First 1).FullName
.\build-cpp\Release\hydra_replay.exe dump $chart --out "$env:TEMP\hydra_task5_dump.json"
```

Open the JSON and pick an activation whose `sqinouts` has a SqOut. Take its `act_tick`, `deact_tick`, the SqOut `offset_ms` and `sqout_tick`. Score it twice, once from the file and once typed with the offset rounded to two decimals:

```powershell
.\build-cpp\Release\hydra_replay.exe score $chart --path "$env:TEMP\hydra_task5_dump.json" --index 0
.\build-cpp\Release\hydra_replay.exe score $chart --acts "<act_tick>:<deact_tick>:<offset rounded to 2 dp>"
```

The angle-bracket values are the ones read off the dump in this step. Expected: the typed run prints `note: window ... -> phrase chord at tick <sqout_tick> ...` with the same tick the dump holds, and no `error:` line. If the dump has no SqOut on path 0, use `--index` on a path that has one; if no path in this chart has one, repeat on the next notes.mid until one does. If `dump`'s real argument names differ from these, run `hydra_replay` with no arguments and use the names its usage text prints. The refusal is pinned by the unit test in Step 1; a real chart with two phrase chords inside one 500 ms window is too rare to hunt for by hand.

- [ ] **Step 10: Commit**

```powershell
git add src/core/model.h src/core/model.cpp src/core/replay.h src/core/replay.cpp tools/replay.cpp tests/test_model.cpp tests/test_path_view.cpp tests/test_squeeze_rating.cpp tests/test_replay.cpp tests/test_search.cpp
git commit -m "Find the squeezed-out chord by its stored tick everywhere" -m "The display, the replay and the corpus test compared float offsets within 0.01 ms; all now compare ticks with Activation::sqout_tick. hydra_replay resolves a typed SqOut offset to the nearest phrase chord and prints which one, refuses a chord the engine never squeezes out, and its squeeze-out warning uses the graph's own rule (first phrase chord strictly within 500 ms of the SP end)." -m "Task: Task 5: Find the squeezed-out chord by its stored tick
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The Preview SP gauge reads the collected phrases from the record

Today the Preview's SP gauge guesses how many phrases an activation collected. It measures how far the deact node sits past the activation, halves that, and subtracts the banked bars. That guess is wrong whenever the SP cap clamps: a clamped collection adds less than two measures, so the leftover rounds to the wrong count. The engine fixture "SP cap overfill: a second clamp in the same window replaces clamp_tick" shows it. The engine collects two phrases there, but the gauge counts one, draws a straight drain after the first phrase, and then shows a bar after SP ends that was never banked. Task 2 makes the engine store the collected phrases on each activation (`Activation::collected_phrase_ticks`, record format v6). This task makes the gauge read that list and stop guessing. Late-SqIn phrases and cap-clamped phrases are in that list, because they are what the gauge really received, so the gauge refills at them too. A record written before v6 reads Stale and its blob is never decoded, so the gauge never meets an old record with an empty list. The user will see the gauge refill at every phrase SP really collected and read empty after the deact node on clamped activations. Unclamped activations draw the same as today.

**Goal:** `build_sp_meter_curve` takes the mid-SP collections from `collected_phrase_ticks` instead of inferring a count from the deact node.

**Files:** Changes `PreviewActivation` in src/app/preview_view.h (one new field, plus the comment on `PreviewScene::sp_meter`), changes `build_sp_meter_curve` and the overlay copy in `build_preview_scene` in src/app/preview_view.cpp, updates two existing cases and adds one new case in tests/test_preview_view.cpp, and rewrites the "SP meter gauge" entry in CONTEXT.md.

**Acceptance Criteria:**
- [ ] The new test "sp meter curve: two clamped collections refill twice and empty at the deact node" passes. It reads 1.5 bars just before 8000 ms and 2.0 at 8000 ms, 1.5 just before 10000 ms and 2.0 at 10000 ms, 1.0 at 14000 ms, and 0.0 at 18000 ms and 19000 ms.
- [ ] The same test fails on the code before this task: it reads about 1.6 just before 10000 ms and 1.0 at 19000 ms.
- [ ] Every existing "sp meter curve:" case in tests/test_preview_view.cpp still passes.
- [ ] `std::llround` no longer appears in `build_sp_meter_curve`.
- [ ] CONTEXT.md's "SP meter gauge" entry says the collections come from the record's list, not from the deact node's extension.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="sp meter curve*"` -> `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the regression test from the engine fixture.**

The engine fixture lives in tests/test_search.cpp, and its song builder `build_tail_song` does not set `sp_phrase_start`. The Preview only draws phrases that carry a start tick, so this test builds the same song with its own small builder that does set it. Add `#include "search/engine.h"` and `#include "search/graph.h"` to the include block of tests/test_preview_view.cpp. Then add this helper inside the file's anonymous namespace, after `check_curve_well_formed`:

```cpp
// The engine fixture from test_search.cpp ("SP cap overfill: a second clamp
// in the same window replaces clamp_tick"), rebuilt with each phrase's start
// tick set so the Preview draws the phrases. 192 ticks per beat, 4/4, 120
// BPM: a measure is 768 ticks and 2000 ms.
Song make_overfill_song() {
    struct N { int64_t tick; bool phrase; bool fill; };
    const std::vector<N> notes = {{0, true, false},    {768, true, false},
                                  {2304, false, true}, {3072, true, false},
                                  {3840, true, false}, {4608, false, false},
                                  {5376, false, false}, {6000, false, false},
                                  {6768, false, false}, {7500, false, false}};
    Song song(192);
    song.tpm_changes[0] = 768;
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (const N& n : notes) {
        SongTimestamp ts;
        ts.timecode = song.timecode(n.tick);
        ts.chord.add_note(NoteColor::Red);
        ts.flag_sp = n.phrase;
        if (n.phrase) ts.sp_phrase_start = n.tick;
        if (n.fill) ts.activation_length = 384;
        song.sequence.push_back(ts);
    }
    return song;
}
```

Add this case after "sp meter curve: a full bank that collects a phrase and stores no row":

```cpp
TEST_CASE("sp meter curve: two clamped collections refill twice and empty at the deact node") {
    // Cap 2. Two phrases bank 2 bars; the activation at tick 2304 (6000 ms)
    // spends them. The phrases at 3072 (8000 ms) and 3840 (10000 ms) are both
    // collected during SP, and each one clamps at the cap. The engine puts
    // the deact node at 6912 (18000 ms). The old gauge counted the
    // collections off the deact node: (9 - 3) / 2 - 2 = 1, so it drew one
    // refill and then showed a bar after SP ended that was never banked.
    Song song = make_overfill_song();
    ScoreGraph graph(song, 2);
    std::vector<Path> paths = run_search(graph, DepthMode::Scores, 0, std::nullopt);
    REQUIRE(!paths.empty());
    REQUIRE(paths.front().activations.size() == 1);
    const Activation& act = paths.front().activations.front();
    REQUIRE(act.timecode.has_value());
    REQUIRE(act.timecode->ticks() == 2304);
    REQUIRE(activation_deact_tick(act) == std::optional<int64_t>(6912));
    // Extra parentheses: the braced list's comma would split the macro.
    REQUIRE((act.collected_phrase_ticks == std::vector<int64_t>{3072, 3840}));

    PreviewScene scene = build_preview_scene(song, &paths.front(), /*sp_cap=*/2);
    const SpMeterCurve& c = scene.sp_meter;
    check_curve_well_formed(c);
    CHECK(c.cap == 2);

    // The activation snaps to the 2 bars the engine recorded.
    CHECK(sp_meter_bars_at(c, 6000.0) == doctest::Approx(2.0));
    // One measure of drain, then the first collection tops back up to the cap.
    CHECK(sp_meter_bars_at(c, 8000.0 - 1e-6) == doctest::Approx(1.5));
    CHECK(sp_meter_bars_at(c, 8000.0) == doctest::Approx(2.0));
    // Another measure of drain, then the second collection does the same.
    CHECK(sp_meter_bars_at(c, 10000.0 - 1e-6) == doctest::Approx(1.5));
    CHECK(sp_meter_bars_at(c, 10000.0) == doctest::Approx(2.0));
    // Four measures from 10000 ms burn the 2 bars exactly at the deact node.
    CHECK(sp_meter_bars_at(c, 14000.0) == doctest::Approx(1.0));
    CHECK(sp_meter_bars_at(c, 18000.0) == doctest::Approx(0.0));
    // Both window phrases were collected, so nothing banks when SP ends.
    CHECK(sp_meter_bars_at(c, 19000.0) == doctest::Approx(0.0));
}
```

- [ ] **Step 2: Make the two hand-built collection cases state their collection.**

Two existing cases set `deact_tick` by hand to show a phrase collected during SP. Once the gauge reads the list, they must also say which phrase was collected. In "sp meter curve: a phrase collected mid-activation jumps the meter a bar", replace

```cpp
    act.deact_tick = 3840 + 6 * 1920;  // 4 measures banked, 2 for the collection
```

with

```cpp
    act.deact_tick = 3840 + 6 * 1920;  // 4 measures banked, 2 for the collection
    act.collected_phrase_ticks = {5760};  // the engine's record of that collection
```

In "sp meter curve: a full bank that collects a phrase and stores no row", replace

```cpp
    act.deact_tick = deact;
```

with

```cpp
    act.deact_tick = deact;
    act.collected_phrase_ticks = {phrase_tick};
```

Also fix the fixture comment above `sp_act_at`. Replace "A fixture that collects one overwrites `deact_tick` itself." with "A fixture that collects one overwrites `deact_tick` and sets `collected_phrase_ticks` itself."

- [ ] **Step 3: Build and watch the new case fail.**

Run `.\build_cpp.ps1 -Target hydra_tests` and then `.\build-cpp\Release\hydra_tests.exe -tc="sp meter curve: two clamped collections*"`. Expect it to fail on the check just before 10000 ms (about 1.6 instead of 1.5) and on the check at 19000 ms (1.0 instead of 0.0). The two cases changed in Step 2 still pass, because the old code ignores the new field.

- [ ] **Step 4: Carry the list into the Preview.**

In src/app/preview_view.h, add this field to `PreviewActivation` after `sp_end_ms`:

```cpp
    // The ticks of the SP phrase-end chords this activation collected while
    // SP was running, in order, copied from the record (blob v6). Late-SqIn
    // and cap-clamped phrases are included: they are what the gauge really
    // received. The gauge refills at exactly these; a phrase in the window
    // that is not listed was squeezed out and banks when SP ends.
    std::vector<int64_t> collected_phrase_ticks;
```

In `build_preview_scene` in src/app/preview_view.cpp, after `pa.skips = a.skips.value_or(0);`, add:

```cpp
            pa.collected_phrase_ticks = a.collected_phrase_ticks;
```

- [ ] **Step 5: Replace the guess with the record's list.**

In `build_sp_meter_curve`, replace these current lines:

```cpp
        // How many of those SP actually collects: the deact node already says.
        // The engine anchors it 2 measures past the activation per banked bar,
        // plus 2 more for every phrase collected mid-SP, so the surplus
        // measures are the count. A squeezed-out phrase lies inside the window
        // on the highway but is hit late, just after SP ends, so it buys no
        // extension and takes no step here — it banks the moment the window
        // closes, for the next activation.
        const double act_measures =
            timing.measures_at_tick_f(static_cast<double>(act.tick));
        const double end_measures =
            timing.measures_at_tick_f(static_cast<double>(act.sp_end_tick));
        int64_t collected = std::llround((end_measures - act_measures) / 2.0 -
                                         static_cast<double>(act.sp_meter));
        collected = std::clamp<int64_t>(collected, 0,
                                        static_cast<int64_t>(window.size()));

        std::vector<DrainSplit> splits;
        for (int64_t i = 0; i < collected; ++i)
            splits.push_back({window[i]->end_tick, window[i]->end_ms, true});
```

with:

```cpp
        // Which phrases SP collects is engine truth: the record lists them,
        // late-SqIn and cap-clamped ones included. A phrase ending on the
        // activation note is already inside sp_meter, so only ticks strictly
        // inside the window step the drain (the same bounds the tempo and
        // meter splits below use). A window phrase the list does not name was
        // squeezed out: it is hit just after SP ends, so it takes no step here
        // and banks the moment the window closes, for the next activation.
        std::vector<DrainSplit> splits;
        for (int64_t t : act.collected_phrase_ticks)
            if (t > act.tick && t < act.sp_end_tick)
                splits.push_back({t, timing.ms_index().at(t), true});
        int64_t squeezed_out = 0;
        for (const PreviewSpan* p : window)
            if (std::find(act.collected_phrase_ticks.begin(),
                          act.collected_phrase_ticks.end(),
                          p->end_tick) == act.collected_phrase_ticks.end())
                ++squeezed_out;
```

Then replace the bank line after the drain:

```cpp
        bank = std::min(static_cast<double>(static_cast<int64_t>(window.size()) -
                                            collected),
                        cap);
```

with:

```cpp
        bank = std::min(static_cast<double>(squeezed_out), cap);
```

`<algorithm>` is already in the file's include block (line 6), so `std::find` needs no new include. Leave the other includes alone; removing includes is not part of this task.

- [ ] **Step 6: Build and run the gauge cases.**

Run `.\build_cpp.ps1 -Target hydra_tests` and then `.\build-cpp\Release\hydra_tests.exe -tc="sp meter curve*"`. Expect `[doctest] Status: SUCCESS!`. Then run the whole suite with `.\build-cpp\Release\hydra_tests.exe` and expect SUCCESS.

- [ ] **Step 7: Update the two descriptions.**

In src/app/preview_view.h, the `PreviewScene::sp_meter` comment currently ends with "Phrases collected mid-SP are counted from the deact node rather than from where they sit on the highway, so a squeezed-out phrase steps the gauge the moment SP ends, not during the drain." Replace that sentence with "Phrases collected mid-SP come from the record's own list (`collected_phrase_ticks`), so a squeezed-out phrase steps the gauge the moment SP ends, not during the drain."

In CONTEXT.md, the "SP meter gauge" entry currently says "Anchored to the record's per-activation bank and deact node, never re-derived — which phrases SP collects is counted off the deact node's own extension, so a squeezed-out phrase inside the window banks when SP ends rather than during the drain." Replace that sentence with "Anchored to the record's per-activation bank, deact node and list of collected phrases, never re-derived. Late-SqIn and cap-clamped phrases are in that list. A phrase inside the window that the record does not list was squeezed out, so it banks when SP ends rather than during the drain."

- [ ] **Step 8: Commit.**

```powershell
git add src/app/preview_view.h src/app/preview_view.cpp tests/test_preview_view.cpp CONTEXT.md
git commit -m @'
Read the SP gauge's mid-SP collections from the record

The gauge counted collections off the deact node's extension, which
miscounts when the SP cap clamps. It now reads
Activation::collected_phrase_ticks (blob v6).

Task: Task 6: The Preview SP gauge reads the collected phrases from the record
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@
```

### Task 7: One formatter each for the calibration fill and the average multiplier

Two displays show the same stored number in two different ways. The first is the calibration fill. The Song Details header and the report both show it as positive = hit early, but the Song Details "Calibration fill:" line prints the raw `e_offset`, which has the opposite sign. So one activation can read "12.3ms" in the header and "-12.3ms" on the line below it. The second is the average multiplier. The report rounds it to three decimals with `py_round3`, but Song Details cuts it off after three decimals, so 5/3 reads 1.667 in the report and 1.666 in Song Details. The user decided both: positive = hit early everywhere, so only the details line changes; and rounded everywhere, so Song Details changes. This task puts the rounding and the ms text in one small shared file that both screens use. The user will see the details line flip sign to match the header, and Song Details' average multiplier sometimes read 0.001 higher, matching the report.

**Goal:** The calibration-fill line and the average-multiplier line use the same value owner and the same formatting as the header and the report.

**Files:** Creates src/app/display_format.h and display_format.cpp (added to the hydra_core list in CMakeLists.txt), moves `py_round3` out of src/app/report.h and report.cpp into it, changes the header, the calibration line and `build_score_breakdown` in src/app/path_view.cpp, updates the comment on `ActivationDetailsView` in src/app/path_view.h, and changes and adds cases in tests/test_path_view.cpp.

**Acceptance Criteria:**
- [ ] `format_avg_mult(2.3456)` is "2.346", `format_avg_mult(2.0005)` is "2.001", and `format_avg_mult(1.0005)` is "1.000". The last two are what Python's `round(x, 3)` gives on those doubles, because 2.0005 is stored just above the half and 1.0005 just below it.
- [ ] An E0 activation with `e_offset` -12.3 has the header "E0    (2 SP)\t         \t   12.3ms" and the details line "Calibration fill: 12.3ms (required)".
- [ ] An E-critical, non-E0 activation with `e_offset` 20.0 has the details line "Calibration fill: -20.0ms (optional)".
- [ ] One existing expectation changes: "build_score_breakdown: exact lines, truncation not rounding" expected "Avg. Multiplier:      1.666x". The case is renamed "build_score_breakdown: exact lines, rounded like the report" and now expects "Avg. Multiplier:      1.667x". No other existing test pins the "Calibration fill" line or the header's ms text; a grep of tests/ for "Calibration fill" finds only the new case.
- [ ] The report's `mult` numbers are unchanged, because it calls the same rounding function it called before, under its new home.
- [ ] The hydra_uitest command file in Step 7 passes on Evans Blue, Beg, and its `text` output shows an "Avg. Multiplier:" line with exactly three decimals.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="*display format*,build_score_breakdown*,build_activations*"` -> `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests.**

Add `#include "app/display_format.h"` to the include block of tests/test_path_view.cpp. Replace the start of the existing case

```cpp
TEST_CASE("build_score_breakdown: exact lines, truncation not rounding") {
    Path p;
    p.score_base = 3;
    p.score_combo = 2;  // 5/3 = 1.6666... — a slice truncates, %.3f would round

    std::vector<std::string> lines = build_score_breakdown(p);
    REQUIRE(lines.size() == 8);
    // (str(avg_mult()) + "000")[:5]: "1.666", NOT the rounded "1.667".
    CHECK(lines[0] == "Avg. Multiplier:      1.666x");
```

with

```cpp
TEST_CASE("build_score_breakdown: exact lines, rounded like the report") {
    Path p;
    p.score_base = 3;
    p.score_combo = 2;  // 5/3 = 1.6666...

    std::vector<std::string> lines = build_score_breakdown(p);
    REQUIRE(lines.size() == 8);
    // Rounded to three places, the same as the report's mult column.
    CHECK(lines[0] == "Avg. Multiplier:      1.667x");
```

and leave the three `CHECK`s after it as they are. Then add these cases after it:

```cpp
TEST_CASE("display format: the average multiplier rounds the exact double") {
    // Python's round(x, 3) on the binary value, not on the decimal literal.
    CHECK(format_avg_mult(2.3456) == "2.346");  // a slice would give 2.345
    CHECK(format_avg_mult(2.0005) == "2.001");  // stored as 2.000500000000000167
    CHECK(format_avg_mult(1.0005) == "1.000");  // stored as 1.000499999999999945
    CHECK(py_round3(2.3456) == 2.346);
}

TEST_CASE("display format: ms text is one decimal and a unit") {
    CHECK(format_ms(12.3) == "12.3ms");
    CHECK(format_ms(-20.0) == "-20.0ms");
    CHECK(format_ms(0.0) == "0.0ms");
}

TEST_CASE("build_activations: the calibration fill reads positive = early on both lines") {
    HydraRecord rec;  // only feeds the footer
    auto view_of = [&rec](const Activation& act) {
        Path p;
        p.activations.push_back(act);
        ActivationsView v = build_activations(p, rec, nullptr, 85.0);
        REQUIRE(v.acts.size() == 1);
        return v.acts[0];
    };

    // E0: 12.3 ms early. The header already showed +12.3; the details line
    // used to print the raw offset, -12.3.
    Activation e0;
    e0.skips = 0;
    e0.sp_meter = 2;
    e0.e_offset = -12.3;
    ActivationDetailsView av = view_of(e0);
    CHECK(av.header == "E0    (2 SP)\t         \t   12.3ms");
    CHECK(av.calibration == "Calibration fill: 12.3ms (required)");

    // E-critical but not E0: no ms in the header, and the details line uses
    // the same sign rule, so 20 ms late reads negative.
    Activation e1;
    e1.skips = 1;
    e1.sp_meter = 2;
    e1.e_offset = 20.0;
    av = view_of(e1);
    CHECK(av.header == "E1    (2 SP)\t         ");
    CHECK(av.calibration == "Calibration fill: -20.0ms (optional)");
}
```

- [ ] **Step 2: Run them and watch them fail.**

Run `.\build_cpp.ps1 -Target hydra_tests`. It fails to compile, because app/display_format.h does not exist yet. That is the expected first failure.

- [ ] **Step 3: Create the shared formatter.**

Create src/app/display_format.h:

```cpp
// The display text for numbers that more than one screen shows. Each rule
// lives here once, so Song Details and the report cannot drift apart.

#ifndef HYDRA_APP_DISPLAY_FORMAT_H
#define HYDRA_APP_DISPLAY_FORMAT_H

#include <string>

namespace hydra::app {

// round(v, 3), matching Python's correctly-rounded decimal rounding.
double py_round3(double v);

// The average multiplier as every screen shows it: py_round3, three places.
std::string format_avg_mult(double v);

// A timing in ms, one decimal, with the unit: "12.3ms". The caller decides
// the sign; the calibration fill passes Activation::e_difficulty(true), which
// is positive when the fill is hit early.
std::string format_ms(double ms);

}  // namespace hydra::app

#endif  // HYDRA_APP_DISPLAY_FORMAT_H
```

Create src/app/display_format.cpp:

```cpp
#include "app/display_format.h"

#include <cstdio>
#include <cstdlib>

namespace hydra::app {

double py_round3(double v) {
    // Python's round(x, 3) rounds the exact binary value to 3 decimal places,
    // ties-to-even. MSVC's printf does the same correctly-rounded conversion,
    // so format-and-reparse reproduces it.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return std::strtod(buf, nullptr);
}

std::string format_avg_mult(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", py_round3(v));
    return buf;
}

std::string format_ms(double ms) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1fms", ms);
    return buf;
}

}  // namespace hydra::app
```

In CMakeLists.txt, in the hydra_core source list, add `src/app/display_format.cpp` on its own line after `src/app/dynamics_breakdown.cpp`.

- [ ] **Step 4: Move the report onto the shared rounding.**

In src/app/report.h, delete these two lines:

```cpp
// round(v, 3), matching Python's correctly-rounded decimal rounding.
double py_round3(double v);
```

In src/app/report.cpp, delete the `py_round3` definition (the function that starts `double py_round3(double v) {` and ends after `return std::strtod(buf, nullptr);`). Add `#include "app/display_format.h"` to its include block. The call site `row.mult = py_round3(*s.avgmult);` stays as written. report.cpp is inside `hydra::app::report`, so the unqualified name finds `hydra::app::py_round3`. The report's page script still formats that number with `toFixed(3)` and the efill number with `fmtMs`. Those are JavaScript in the page and keep working on the same values.

- [ ] **Step 5: Point Song Details at the shared formatter.**

In src/app/path_view.cpp, add `#include "app/display_format.h"` after `#include "app/path_view.h"`.

Replace the header's ms part:

```cpp
        if (auto ms = act.difficulty()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "\t%7.1fms", *ms);
            av.header += buf;
        }
```

with

```cpp
        if (auto ms = act.difficulty()) {
            // "%9s" of "12.3ms" is byte-identical to the old "%7.1fms".
            char buf[48];
            std::snprintf(buf, sizeof(buf), "\t%9s", format_ms(*ms).c_str());
            av.header += buf;
        }
```

Replace the calibration line:

```cpp
        if (act.is_e_critical()) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "Calibration fill: %.1fms (%s)",
                          *act.e_offset, act.is_E0() ? "required" : "optional");
            av.calibration = buf;
        }
```

with

```cpp
        if (act.is_e_critical()) {
            // Positive = hit early, the same sign as the header and the report:
            // e_difficulty(true) is -e_offset for every E-critical activation.
            av.calibration = "Calibration fill: " +
                             format_ms(*act.e_difficulty(/*verbose=*/true)) +
                             (act.is_E0() ? " (required)" : " (optional)");
        }
```

Replace the average line in `build_score_breakdown`:

```cpp
    // hydra_app.py:1044 formats the average as (str(avg_mult()) + "000")[:5]
    // -- a string slice, which TRUNCATES to three decimals rather than
    // rounding (%.3f would round). %.10f gives a long-enough decimal
    // expansion; slicing its first five chars reproduces Python exactly.
    char avgbuf[32];
    std::snprintf(avgbuf, sizeof(avgbuf), "%.10f", path.avg_mult());
    std::string avgs = (std::string(avgbuf) + "000").substr(0, 5);
    lines.push_back("Avg. Multiplier:      " + avgs + "x");
```

with

```cpp
    // Rounded, not truncated, so it matches the report's mult column.
    lines.push_back("Avg. Multiplier:      " + format_avg_mult(path.avg_mult()) + "x");
```

In src/app/path_view.h, change the comment on `ActivationDetailsView::header` from `// "%-6s(%d SP)\t%9s" (+ "\t%7.1fms" when difficult-rated)` to `// "%-6s(%d SP)\t%9s" (+ "\t" and format_ms right-aligned in 9 when difficulty-rated)`. Change the comment on `calibration` from `// "Calibration fill: ..."; empty when not E-critical` to `// "Calibration fill: " + format_ms(positive = early); empty when not E-critical`.

- [ ] **Step 6: Build and run.**

Run `.\build_cpp.ps1 -Target hydra_tests` and then `.\build-cpp\Release\hydra_tests.exe -tc="*display format*,build_score_breakdown*,build_activations*"`. Expect `[doctest] Status: SUCCESS!`. Then run the whole suite with `.\build-cpp\Release\hydra_tests.exe` and expect SUCCESS, which includes the report cases.

- [ ] **Step 7: Check the GUI line headlessly.**

The unit tests pin the exact text. This step only proves the real Song Details draws it. It uses a hydra_uitest command file (docs/agents/ui-testing.md), which runs on the scratch library (testdata/input) with an empty DB, so it never touches the user's hydra.db. Write this file to `$env:TEMP\task7_details.txt`:

```
# Scan the scratch library, open Evans Blue - Beg, analyze it, read Song Details.
click Scan charts
wait-idle
click //Scanning charts/Continue
type ##search | Beg
wait 0.2
click **/Beg
click **/Analyze paths!
wait-idle
click ##DetailsTabs/Paths
wait-text Avg. Multiplier:
expect-text Avg. Multiplier:
text
```

Then run:

```powershell
.\build_cpp.ps1 -Target hydra_uitest
.\build-cpp\Release\hydra_uitest.exe --script "$env:TEMP\task7_details.txt" *> "$env:TEMP\task7_details_out.txt"
Select-String -LiteralPath "$env:TEMP\task7_details_out.txt" -Pattern 'Avg\. Multiplier:\s+\d\.\d{3}x', 'Calibration fill:', '!! failed'
```

Expect one "Avg. Multiplier:" match with exactly three decimals and no "!! failed" line. If a "Calibration fill:" line shows, its ms must carry the same sign as the ms at the end of that activation's header line. If `click **/Beg` or the tab click fails, add a `dump` line before it and read the real label off the dump, as the doc says. Do not use screenshots.

- [ ] **Step 8: Commit.**

```powershell
git add src/app/display_format.h src/app/display_format.cpp CMakeLists.txt src/app/report.h src/app/report.cpp src/app/path_view.h src/app/path_view.cpp tests/test_path_view.cpp
git commit -m @'
Share one formatter for the calibration fill and the average multiplier

The details line now shows the calibration fill as positive = early,
like the header and the report. Song Details rounds the average
multiplier with py_round3, like the report, instead of truncating.

Task: Task 7: One formatter each for the calibration fill and the average multiplier
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@
```

### Task 8: hydra_replay reports the multiplier the score actually used

`category_scores` adds one to the combo before it looks up each note's multiplier. The replay then writes `to_multiplier(combo_before)` into its JSON, which skips that plus-one. So today a chord's `multiplier` field is really the multiplier the previous chord left behind. Every chord whose first note takes the combo to 10, 20 or 30 is reported at the old multiplier while its points are paid at the new one. The audit proved it on Evans Blue, Beg: chord 8 has `combo_before` 9 and reports multiplier 1, but its combo points are 50, the 2x rate. The user decided that hydra_replay reports what `category_scores` applied. Each note gets its own `multiplier`. Each chord gets `multiplier_after`, the multiplier once the whole chord is hit, which is what the game's disc shows from then on. The chord's own `multiplier` becomes what its first note was worth. Hydra's scores do not change, and the GUI shows nothing different.

The same JSON also priced a mis-hit ghost or accent wrong, and the user decided to fix that here too. The chord's `ghost` and `accent` points hold only the pad's 50 per dynamic note, before the multiplier. But `category_scores` pays a dynamic cymbal 15 more, and it pays all of it at that note's own multiplier. So a mis-hit ghost cymbal at 2x really loses (50 + 15) x 2 = 130, while video-tools subtracted 50 x 2 = 100. video-tools also subtracted the chord's whole bonus from every note in the chord, so it could never explain two mis-hit dynamic notes in one chord. Each note now carries `dynamics_bonus`, the points its own ghost or accent earned, filled in by `category_scores` from the same terms it adds to the totals.

The loss text also named the wrong kind on some chords. analyze.py picks its wording once per chord, "ghost ... hit too hard" whenever the chord has any ghost and "accent ... hit too soft" otherwise. So on a chord holding both a ghost and an accent, a mis-hit accent reads as a ghost hit too hard. The user decided each note carries its own kind: a per-note `dynamic` field, "ghost", "accent" or "none", copied from `ChordNote::dynamictype`, the one place a note's dynamic is stored. analyze.py then words each mis-hit from its own note. That changes only loss text, and only on chords that mix a ghost and an accent.

video-tools moves to the new fields in its own commit. `replay.py`'s `multiplier_steps` reads `multiplier_after` and takes the first chord where it rises. That picks exactly the chords the current `i - 1` code picks, because a chord's old field always equalled the previous chord's `multiplier_after`. So the disc training marks do not move. `analyze.py` prices each mis-hit note at its own `dynamics_bonus`. The loss count and loss total that check_refs judges come from the score steps on screen, not from this pricing, so they do not change. What changes is which losses the report can explain as a dynamics mis-hit. A loss on a dynamic cymbal, on a chord that straddles a multiplier step, or on a chord with two mis-hit dynamic notes used to find no matching notes. It now gets its note named ("ghost Yellow cymbal hit too hard") and its kind counted as "dynamics" (analyze.py line 756 decides the kind from that text). fcvideo's report keeps no dynamics total of its own: each loss carries `points`, `notes` and `kind`, and `loss_total` sums the points (fcvideo.py around line 2571). The one dynamics-loss number is in album.py, which adds up each song's loss points by `kind` (its `by_kind`, line 242) and prints them in the "by kind" column. A loss that moves from "other" to "dynamics" moves its points between those two totals there, so for runs with mis-hit ghosts or accents the album's "dynamics" figure goes up and "other" goes down by the same amount.

Before any of that, video-tools has uncommitted edits in six files. The user decided Step 1 commits them as they are, as their own commit, so this task builds on a clean tree.

**Goal:** hydra_replay JSON carries the multiplier `category_scores` applied (per note and after each chord) and each note's own dynamics bonus and dynamic kind, and video-tools reads those fields with check_refs still all green.

**Files:** In hydra-test it adds `dynamic_str` next to `color_str` in src/core/model.h and src/core/model.cpp, changes `CategoryScores` in src/core/scoring.h and `category_scores` in src/core/scoring.cpp, `ReplayNote` and `ReplayChord` in src/core/replay.h, `replay_path` in src/core/replay.cpp and the JSON writer in tools/replay.cpp, and adds cases to tests/test_replay.cpp. In video-tools it first commits the six pending files as they are. Then it changes `multiplier_steps` in replay.py; `_note_options`, `attribute` and `attribute_span` in analyze.py, where `dynamics_of` gives way to `_mishit_text`; and one sentence of README.md. It also creates multiplier_test.py.

**Acceptance Criteria:**
- [ ] The new tests "category_scores reports the multiplier each note was paid at", "category_scores prices a dynamic cymbal's bonus at its own note", "replay reports the multipliers category_scores applied", "replay copies each note's dynamics bonus", "replay names each note's dynamic" and "replay multipliers agree with the combo on every corpus chord" pass.
- [ ] The existing test "replay reproduces the engine's score for every corpus path" still passes, which shows no score moved.
- [ ] On Evans Blue, Beg, `hydra_replay score` writes chord 8 with `combo_before` 9, `multiplier` 2, `multiplier_after` 2 and 50 combo points, and chord 7 with `multiplier_after` 1.
- [ ] video-tools has a commit holding exactly the six pre-existing edits (README.md, album.py, analyze.py, fcvideo.py, refs.json, replay.py), made before any Task 8 edit there.
- [ ] `py multiplier_test.py` in video-tools ends with `OK` for its seven tests. One of them shows a mis-hit 2x ghost cymbal priced at a 130 loss, not 100. Another shows two mis-hit ghost pads in one chord named together, where the old code found nothing.
- [ ] On a chord that mixes a ghost Red pad and an accent Yellow cymbal, a mis-hit accent reads "accent Yellow cymbal hit too soft" (multiplier_test.py's mixed-chord test). The chord-level wording would have said "ghost Yellow cymbal hit too hard". The `dynamic` field changes loss text only, only on mixed chords, and no number anywhere.
- [ ] `py check_refs.py` in video-tools ends with "9 of 9 cases passed." both before and after the change, with every case line reading PASS and identical before and after. The nine cases, as the script prints them from the committed refs.json, are swim_to_the_moon, divine_inner_tension, new_age_filth, the_violation, the_violation_half_speed, desecration_day, colors_album_ch10, wont_get_fooled_again, and pursuit_of_happiness (that last one passes by being refused).
- [ ] Three commits exist: the pre-existing edits and the Task 8 change in video-tools, and the Task 8 change in hydra-test, each with the trailers.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="*multiplier*,*dynamic*,replay reproduces*"` -> `[doctest] Status: SUCCESS!`, then in C:\Users\Patrick\Downloads\Hydra\video-tools `py multiplier_test.py` -> `OK` and `py check_refs.py` -> `9 of 9 cases passed.`

**Steps:**

- [ ] **Step 1: Commit the pending video-tools edits as they are.**

video-tools has uncommitted edits that were there before this plan. Who made them is not recorded. The user decided they go in as their own commit first, unchanged. Check the tree:

```powershell
git -C C:\Users\Patrick\Downloads\Hydra\video-tools status --short
```

When this plan was written it printed exactly:

```
 M README.md
 M album.py
 M analyze.py
 M fcvideo.py
 M refs.json
 M replay.py
```

If the list differs in any way (another file, a missing file, an untracked `??` entry), stop and show the user the new list; do not commit an unexpected file. If it matches, commit those six paths and nothing else:

```powershell
git -C C:\Users\Patrick\Downloads\Hydra\video-tools add README.md album.py analyze.py fcvideo.py refs.json replay.py
git -C C:\Users\Patrick\Downloads\Hydra\video-tools commit -m @'
Commit the pre-existing working-tree edits before Task 8

These six files held uncommitted edits made before the Task 8 plan,
committed here unchanged. They include multiplier_steps marking the
chord before the rise (steps[m] = i - 1), the sqout_offset helper in
analyze.py, and the ninth reference case, wont_get_fooled_again
(3 losses, 800 points), in refs.json.

Task: Task 8: hydra_replay reports the multiplier the score actually used
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@
git -C C:\Users\Patrick\Downloads\Hydra\video-tools status --short
```

The last command must print nothing.

- [ ] **Step 2: Record check_refs before the change.**

check_refs.py runs fcvideo.py with no `--hydra-replay` flag, so it always uses `DEFAULT_EXE` in video-tools\replay.py. That is C:\Users\Patrick\Downloads\Hydra\hydra-test\build-cpp\Release\hydra_replay.exe. If you work in a git worktree, your build lands in the worktree's own build-cpp, so copy your built hydra_replay.exe over that path before each check_refs run. On the hydra-test commit before this task, run from the hydra-test root:

```powershell
.\build_cpp.ps1 -Target hydra_replay
Push-Location C:\Users\Patrick\Downloads\Hydra\video-tools
py check_refs.py *> "$env:TEMP\check_refs_before_task8.txt"
Pop-Location
Get-Content "$env:TEMP\check_refs_before_task8.txt" | Select-String 'PASS|FAIL|cases passed'
```

Expect a PASS line for each of the nine cases and "9 of 9 cases passed." This run uses the just-committed `i - 1` version of `multiplier_steps`. If any case fails here, stop and report it to the user: this task cannot show it kept check_refs green from a red start. replay.py caches each JSON in %TEMP% keyed by the exe's size and modified time, so a rebuilt exe makes it run the replay again by itself.

- [ ] **Step 3: Write the failing C++ tests.**

Add `#include "core/scoring.h"` to the include block of tests/test_replay.cpp. Then add this after the last case, "per-note sp points sum to the chord's sp points":

```cpp
namespace {

// One chord per beat at 120 BPM; chord i holds the colors listed at i.
Song make_chord_song(const std::vector<std::vector<NoteColor>>& chords) {
    Song song(480);
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (size_t i = 0; i < chords.size(); ++i) {
        SongTimestamp ts;
        ts.timecode = song.timecode(480 * static_cast<int64_t>(i));
        for (NoteColor c : chords[i]) ts.chord.add_note(c);
        song.sequence.push_back(ts);
    }
    return song;
}

// Adds a dynamic Yellow cymbal to `chord`.
void add_dynamic_cymbal(Chord& chord, NoteDynamicType dyn) {
    ChordNote& y = chord.add_note(NoteColor::Yellow);
    y.cymbaltype = NoteCymbalType::Cymbal;
    y.dynamictype = dyn;
}

}  // namespace

TEST_CASE("category_scores reports the multiplier each note was paid at") {
    Chord one;
    one.add_note(NoteColor::Red);
    CHECK(category_scores(one, 8).multiplier == 1);  // the 9th note: 1x
    CHECK(category_scores(one, 9).multiplier == 2);  // the 10th note: 2x
    CHECK(category_scores(one, 19).multiplier == 3);
    CHECK(category_scores(one, 29).multiplier == 4);
    CHECK(category_scores(one, 40).multiplier == 4);
    CHECK(category_scores(one, 9).multiplier_after == 2);

    // A two-note chord after 8 notes of combo straddles the step: its first
    // note is the 9th (1x) and its second the 10th (2x).
    Chord two;
    two.add_note(NoteColor::Red);
    two.add_note(NoteColor::Kick);
    std::vector<CategoryScores> per_note;
    const CategoryScores s = category_scores(two, 8, &per_note);
    CHECK(s.multiplier == 1);
    CHECK(s.multiplier_after == 2);
    CHECK(s.combo == 50);  // only the second note earns combo points
    REQUIRE(per_note.size() == 2);
    CHECK(per_note[0].multiplier == 1);
    CHECK(per_note[1].multiplier == 2);
}

TEST_CASE("category_scores prices a dynamic cymbal's bonus at its own note") {
    // A kick and a ghost cymbal after 9 notes of combo: both notes are at 2x.
    // The cymbal's dynamics are the pad's 50 plus the cymbal's 15, times 2.
    // The chord's ghost category holds only the raw 50.
    Chord chord;
    chord.add_note(NoteColor::Kick);
    add_dynamic_cymbal(chord, NoteDynamicType::Ghost);
    std::vector<CategoryScores> per_note;
    const CategoryScores s = category_scores(chord, 9, &per_note);
    CHECK(s.ghost == 50);

    const std::vector<ChordNote> order = chord.notes(true);
    REQUIRE(order.size() == 2);
    REQUIRE(per_note.size() == 2);
    for (size_t k = 0; k < order.size(); ++k) {
        if (order[k].colortype == NoteColor::Yellow) {
            CHECK(per_note[k].dynamics_bonus == 130);  // (50 + 15) x 2
            // Without its dynamics the cymbal pays a plain 2x cymbal: 130.
            CHECK(per_note[k].sp - per_note[k].dynamics_bonus == 130);
        } else {
            CHECK(per_note[k].dynamics_bonus == 0);
        }
    }
}

TEST_CASE("replay reports the multipliers category_scores applied") {
    // Eight Red singles, a Red+Kick chord that straddles 10, then eleven more
    // singles so a later chord crosses 20 on its own. The audit's Evans Blue,
    // Beg case is the single-note crossing: paid at 2x, reported as 1.
    std::vector<std::vector<NoteColor>> chords(8, {NoteColor::Red});
    chords.push_back({NoteColor::Red, NoteColor::Kick});
    for (int i = 0; i < 11; ++i) chords.push_back({NoteColor::Red});
    const ReplayResult r = replay_path(make_chord_song(chords), {});
    REQUIRE(r.chords.size() == 20);

    // The straddling chord: first note 1x, second note 2x.
    const ReplayChord& straddle = r.chords[8];
    CHECK(straddle.combo_before == 8);
    CHECK(straddle.multiplier == 1);
    CHECK(straddle.multiplier_after == 2);
    CHECK(straddle.points.combo == 50);
    REQUIRE(straddle.notes.size() == 2);
    CHECK(straddle.notes[0].multiplier == 1);
    CHECK(straddle.notes[1].multiplier == 2);

    // The chord before it leaves the disc at 1x.
    CHECK(r.chords[7].multiplier_after == 1);

    // A single note that is the 20th: paid at 3x. The old field said 2.
    const ReplayChord& third = r.chords[18];
    CHECK(third.combo_before == 19);
    CHECK(third.multiplier == 3);
    CHECK(third.multiplier_after == 3);
    CHECK(third.notes[0].multiplier == 3);
    CHECK(third.points.combo == 100);  // 50 base x (3 - 1)
    CHECK(r.chords[17].multiplier_after == 2);
}

TEST_CASE("replay copies each note's dynamics bonus") {
    Song song = make_chord_song({{NoteColor::Red}});
    add_dynamic_cymbal(song.sequence[0].chord, NoteDynamicType::Accent);
    const ReplayResult r = replay_path(song, {});
    REQUIRE(r.chords.size() == 1);
    REQUIRE(r.chords[0].notes.size() == 2);
    for (const ReplayNote& n : r.chords[0].notes)
        CHECK(n.dynamics_bonus == (n.color == NoteColor::Yellow ? 65 : 0));
    // The accent category holds only the pad part; the note field holds all.
    CHECK(r.chords[0].points.accent == 50);
}

TEST_CASE("replay names each note's dynamic") {
    // A kick, a ghost Red pad and an accent Yellow cymbal in one chord: each
    // note reports its own kind, so a mixed chord can be worded per note.
    Song song = make_chord_song({{NoteColor::Kick}});
    Chord& chord = song.sequence[0].chord;
    chord.add_note(NoteColor::Red).dynamictype = NoteDynamicType::Ghost;
    add_dynamic_cymbal(chord, NoteDynamicType::Accent);
    const ReplayResult r = replay_path(song, {});
    REQUIRE(r.chords.size() == 1);
    REQUIRE(r.chords[0].notes.size() == 3);
    for (const ReplayNote& n : r.chords[0].notes) {
        if (n.color == NoteColor::Red)
            CHECK(n.dynamic == NoteDynamicType::Ghost);
        else if (n.color == NoteColor::Yellow)
            CHECK(n.dynamic == NoteDynamicType::Accent);
        else
            CHECK(n.dynamic == NoteDynamicType::Normal);
    }
    CHECK(dynamic_str(NoteDynamicType::Ghost) == "ghost");
    CHECK(dynamic_str(NoteDynamicType::Accent) == "accent");
    CHECK(dynamic_str(NoteDynamicType::Normal) == "none");
}

TEST_CASE("replay multipliers agree with the combo on every corpus chord") {
    Song song = load_songpath(corpus::first_chart_with_suffix(".mid"), true, true);
    REQUIRE_FALSE(song.is_empty());
    const ReplayResult r = replay_path(song, {});

    for (size_t i = 0; i < r.chords.size(); ++i) {
        const ReplayChord& c = r.chords[i];
        REQUIRE_FALSE(c.notes.empty());
        // The chord's multiplier is its first note's; multiplier_after is its
        // last note's.
        CHECK(c.multiplier == c.notes.front().multiplier);
        CHECK(c.multiplier_after == c.notes.back().multiplier);
        CHECK(c.multiplier == to_multiplier(c.combo_before + 1));
        // What the disc shows after this chord is what the next chord starts
        // from: the old field's value on the next chord.
        if (i + 1 < r.chords.size())
            CHECK(c.multiplier_after == to_multiplier(r.chords[i + 1].combo_before));
        // A note's dynamics bonus is part of what it pays, never more.
        for (const ReplayNote& n : c.notes) {
            CHECK(n.dynamics_bonus >= 0);
            CHECK(n.dynamics_bonus < n.sp_points);
        }
    }
}
```

- [ ] **Step 4: Run them and watch them fail.**

Run `.\build_cpp.ps1 -Target hydra_tests`. It fails to compile, because `CategoryScores`, `ReplayNote` and `ReplayChord` have no `multiplier_after`, per-note `multiplier`, `dynamics_bonus` or `dynamic` members yet, and `dynamic_str` does not exist. That is the expected first failure.

- [ ] **Step 5: Make category_scores report what it applied.**

Task 1 changes `category_scores` before this task, and Task 9 changes it after. The lines this step anchors on are ones Task 1 leaves alone. If an earlier task already changed any of these lines, apply the same change to the new lines.

In src/core/scoring.h, replace

```cpp
struct CategoryScores {
    int base = 0;
    int combo = 0;
    int sp = 0;
    int accent = 0;
    int ghost = 0;
    int sqout_reduction = 0;
};
```

with

```cpp
struct CategoryScores {
    int base = 0;
    int combo = 0;
    int sp = 0;
    int accent = 0;
    int ghost = 0;
    int sqout_reduction = 0;
    // The combo multiplier applied to the chord's first note (base-sorted),
    // the same note the SqOut calculation reads. Per note, that note's own.
    int multiplier = 1;
    // The multiplier applied to the chord's last note: what the game's disc
    // shows once the whole chord is hit. Per note, the same as `multiplier`.
    int multiplier_after = 1;
    // Per note only (left 0 on the chord total): the points this note's
    // ghost or accent earns, multiplier included -- the pad's 50 plus the
    // cymbal's 15 when the note is a dynamic cymbal. A mis-hit dynamic note
    // loses exactly this, twice over inside Star Power. `accent` and `ghost`
    // above hold only the pad's 50, before the multiplier.
    int dynamics_bonus = 0;
};
```

If Task 1 already added fields to this struct, keep them and add only the three new ones with their comments.

In src/core/scoring.cpp, replace

```cpp
    int sqout_reduction = 0;
```

with

```cpp
    int sqout_reduction = 0;
    int first_multiplier = to_multiplier(combo);
    int last_multiplier = first_multiplier;
```

Replace

```cpp
        combo += 1;
        const int combo_multiplier = to_multiplier(combo);
        const int extra = combo_multiplier - 1;
```

with

```cpp
        combo += 1;
        const int combo_multiplier = to_multiplier(combo);
        const int extra = combo_multiplier - 1;
        if (i == 0) first_multiplier = combo_multiplier;
        last_multiplier = combo_multiplier;
```

After `note_scores.ghost = is_ghost ? basevalue : 0;` add

```cpp
            note_scores.multiplier = combo_multiplier;
            note_scores.multiplier_after = combo_multiplier;
            // The dynamics terms the totals above add for this note:
            // dynamic_note_* and combodynamic_note (the pad's basevalue) plus
            // dynamic_cymbal and combodynamic_cymbal (dyn_cymb), each paid
            // once at 1x and once more per extra.
            note_scores.dynamics_bonus =
                ((is_dynamic ? basevalue : 0) + dyn_cymb) * combo_multiplier;
```

After `out.sqout_reduction = sqout_reduction;` add

```cpp
    out.multiplier = first_multiplier;
    out.multiplier_after = last_multiplier;
```

The dynamics bonus is computed where every other dynamics term is, from the same `basevalue`, `dyn_cymb` and `combo_multiplier`, so there is one owner. For a dynamic cymbal it is (50 + 15) x multiplier: 65 at 1x, 130 at 2x. The starting value `to_multiplier(combo)` only matters for a chord with no notes, which never reaches the scorer. The engine's own call in src/search/graph.cpp passes no `per_note` and reads only the score fields, so its behavior does not change. The only new work on the engine's path is two integer copies per note, which is noise next to the dozen additions already there.

- [ ] **Step 6: Make the replay copy them.**

In src/core/replay.h, replace

```cpp
struct ReplayNote {
    NoteColor color = NoteColor::Kick;
    bool cymbal = false;
    int sp_points = 0;
};
```

with

```cpp
struct ReplayNote {
    NoteColor color = NoteColor::Kick;
    bool cymbal = false;
    int sp_points = 0;
    // The combo multiplier this note was paid at, as category_scores applied
    // it. Notes of one chord differ when the chord straddles 10, 20 or 30.
    int multiplier = 1;
    // What this note's ghost or accent earned, multiplier included
    // (CategoryScores::dynamics_bonus); 0 for a plain note. A mis-hit dynamic
    // note pays sp_points minus this.
    int dynamics_bonus = 0;
    // Ghost, accent or neither, copied from ChordNote::dynamictype.
    NoteDynamicType dynamic = NoteDynamicType::Normal;
};
```

In `ReplayChord`, replace

```cpp
    int combo_before = 0;
    int multiplier = 1;
```

with

```cpp
    int combo_before = 0;
    // What category_scores applied to the chord's first note (base-sorted).
    int multiplier = 1;
    // What category_scores applied to the chord's last note: the multiplier
    // the game's disc shows once this chord is hit.
    int multiplier_after = 1;
```

In src/core/replay.cpp, replace

```cpp
        row.multiplier = to_multiplier(combo);
```

with

```cpp
        row.multiplier = sg.multiplier;  // what category_scores applied
        row.multiplier_after = sg.multiplier_after;
```

and in the note loop just below it, replace

```cpp
            note.sp_points = k < per_note.size() ? per_note[k].sp : 0;
```

with

```cpp
            note.sp_points = k < per_note.size() ? per_note[k].sp : 0;
            note.multiplier = k < per_note.size() ? per_note[k].multiplier : 1;
            note.dynamics_bonus =
                k < per_note.size() ? per_note[k].dynamics_bonus : 0;
            note.dynamic = ordering[k].dynamictype;
```

The JSON needs the kind as text. In src/core/model.h, after

```cpp
std::string color_str(NoteColor c);         // "Kick"/"Red"/...
```

add

```cpp
std::string dynamic_str(NoteDynamicType t); // "none"/"ghost"/"accent"
```

and in src/core/model.cpp, after the closing brace of `color_str`, add

```cpp
std::string dynamic_str(NoteDynamicType t) {
    switch (t) {
        case NoteDynamicType::Normal: return "none";
        case NoteDynamicType::Ghost: return "ghost";
        case NoteDynamicType::Accent: return "accent";
    }
    return "none";
}
```

Task 1 changes the `category_scores` call a few lines above (line 67 today) to pass `rules.sqout_rule`. This step does not touch that call. If an earlier task already changed the lines quoted here, apply the same change to the new lines. If `to_multiplier` has no other use left in replay.cpp, leave its include alone; removing includes is not part of this task.

- [ ] **Step 7: Write the new fields into the JSON.**

In tools/replay.cpp, replace

```cpp
            notes.push_back(json{{"color", color_str(n.color)},
                                 {"cymbal", n.cymbal},
                                 {"sp_points", n.sp_points}});
```

with

```cpp
            notes.push_back(json{{"color", color_str(n.color)},
                                 {"cymbal", n.cymbal},
                                 {"sp_points", n.sp_points},
                                 {"multiplier", n.multiplier},
                                 {"dynamics_bonus", n.dynamics_bonus},
                                 {"dynamic", dynamic_str(n.dynamic)}});
```

and replace

```cpp
            {"multiplier", c.multiplier},
```

with

```cpp
            {"multiplier", c.multiplier},
            {"multiplier_after", c.multiplier_after},
```

- [ ] **Step 8: Build, run the tests, and check Beg.**

Run `.\build_cpp.ps1 -Target hydra_tests` and then `.\build-cpp\Release\hydra_tests.exe -tc="*multiplier*,*dynamic*,replay reproduces*"`. Expect `[doctest] Status: SUCCESS!`. Then run the whole suite with `.\build-cpp\Release\hydra_tests.exe` and expect SUCCESS.

Then build hydra_replay and check the audit's chart:

```powershell
.\build_cpp.ps1 -Target hydra_replay
$beg = "testdata\input\common\IB24\T1\Evans Blue - Beg [highfine]\notes.chart"
.\build-cpp\Release\hydra_replay.exe score --chart $beg --out "$env:TEMP\beg_task8.json"
$j = Get-Content -LiteralPath "$env:TEMP\beg_task8.json" -Raw | ConvertFrom-Json
$j.chords[7..8] | Select-Object index, combo_before, multiplier, multiplier_after, @{n='combo_points'; e={$_.points.combo}}
$j.chords[8].notes | Select-Object color, cymbal, sp_points, multiplier, dynamics_bonus, dynamic
```

Expect index 7 with `multiplier_after` 1, and index 8 with `combo_before` 9, `multiplier` 2, `multiplier_after` 2 and `combo_points` 50. Every note of chord 8 shows `multiplier` 2 and has `dynamics_bonus` and `dynamic` fields. If you work in a worktree, copy this hydra_replay.exe over C:\Users\Patrick\Downloads\Hydra\hydra-test\build-cpp\Release\hydra_replay.exe now, because check_refs uses that path.

- [ ] **Step 9: Commit in hydra-test.**

```powershell
git add src/core/model.h src/core/model.cpp src/core/scoring.h src/core/scoring.cpp src/core/replay.h src/core/replay.cpp tools/replay.cpp tests/test_replay.cpp
git commit -m @'
Report the multipliers and dynamics category_scores applied in hydra_replay

The replay looked the multiplier up from combo_before, one note short of
what category_scores used, so chords crossing 10, 20 or 30 showed the
old multiplier. category_scores now returns the first and last note's
multiplier, and per note its own multiplier and the points its ghost or
accent earned (the dynamic cymbal's 15 included). The JSON gains a
per-note multiplier, dynamics_bonus and dynamic (ghost, accent or
none) and a per-chord multiplier_after;
the chord's multiplier is its first note's. Scores are unchanged.

Task: Task 8: hydra_replay reports the multiplier the score actually used
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@
```

- [ ] **Step 10: Write the failing video-tools tests.**

Create C:\Users\Patrick\Downloads\Hydra\video-tools\multiplier_test.py. It needs no video and no engine run.

```python
"""Checks how video-tools reads hydra_replay's multipliers and dynamics.

    py multiplier_test.py

hydra_replay writes a per-note `multiplier` (what that note was paid at), a
per-note `dynamics_bonus` (what its ghost or accent earned, multiplier and
dynamic-cymbal part included), a per-note `dynamic` (ghost, accent or none)
and a per-chord `multiplier_after` (what the
disc shows once the chord is hit). These tests pin how
replay.multiplier_steps and analyze's dynamics pricing read them.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import analyze  # noqa: E402
import replay  # noqa: E402


def to_multiplier(combo):
    """The engine's combo -> multiplier rule (core::to_multiplier)."""
    if combo < 10:
        return 1
    if combo < 20:
        return 2
    if combo < 30:
        return 3
    return 4


def single_note_curve(count):
    """`count` one-note chords of a full combo, as hydra_replay writes them."""
    chords = []
    for i in range(count):
        paid = to_multiplier(i + 1)
        chords.append({"combo_before": i, "multiplier": paid,
                       "multiplier_after": paid})
    return {"chords": chords}


def note(color, cymbal, sp_points, multiplier, dynamics_bonus,
         dynamic="none"):
    return {"color": color, "cymbal": cymbal, "sp_points": sp_points,
            "multiplier": multiplier, "dynamics_bonus": dynamics_bonus,
            "dynamic": dynamic}


class MultiplierStepsTest(unittest.TestCase):
    def test_the_step_is_the_chord_that_crosses(self):
        # Chord 9 holds the 10th note, so its hit turns the disc to 2.
        steps = replay.multiplier_steps(single_note_curve(32))
        self.assertEqual(steps, {1: 0, 2: 9, 3: 19, 4: 29})

    def test_an_old_exe_is_named(self):
        curve = {"chords": [{"combo_before": 0, "multiplier": 1}]}
        with self.assertRaises(replay.ReplayError):
            replay.multiplier_steps(curve)


class DynamicsPriceTest(unittest.TestCase):
    def ghost_cymbal_chord(self, in_sp=False):
        # A kick and a ghost cymbal, both at 2x. The cymbal's dynamics are the
        # pad's 50 plus the cymbal's 15, times 2 = 130. The chord's `ghost`
        # category holds only the raw 50.
        return {"combo_before": 9, "multiplier": 2, "multiplier_after": 2,
                "in_sp": in_sp, "points": {"ghost": 50},
                "notes": [note("Kick", False, 100, 2, 0),
                          note("Yellow", True, 260, 2, 130, "ghost")]}

    def test_a_mixed_chord_words_each_mishit_from_its_own_note(self):
        # A ghost Red pad (100, bonus 50) and an accent Yellow cymbal (130,
        # bonus 65) at 1x. The accent was hit too soft: 100 + 65 = 165 on
        # screen. The chord-level wording picked "ghost" for any chord with
        # a ghost in it, and would have called this a ghost hit too hard.
        chord = {"combo_before": 0, "multiplier": 1, "multiplier_after": 1,
                 "in_sp": False, "points": {"ghost": 50, "accent": 50},
                 "notes": [note("Red", False, 100, 1, 50, "ghost"),
                           note("Yellow", True, 130, 1, 65, "accent")]}
        text, short, certain = analyze.attribute(chord, [165])
        self.assertEqual(text, "accent Yellow cymbal hit too soft")
        self.assertEqual(short, 65)
        self.assertTrue(certain)

    def test_a_mishit_ghost_cymbal_loses_65_per_multiplier(self):
        chord = self.ghost_cymbal_chord()
        cymbal = chord["notes"][1]
        # 260 - 130 = 130. The chord-level price said 260 - 50 x 2 = 160.
        self.assertEqual(analyze._note_options(cymbal, chord),
                         [(260, False, False), (130, False, True)])

    def test_inside_star_power_the_loss_doubles(self):
        chord = self.ghost_cymbal_chord(in_sp=True)
        cymbal = chord["notes"][1]
        self.assertEqual(analyze._note_options(cymbal, chord),
                         [(520, False, False), (260, True, False),
                          (260, False, True), (130, True, True)])

    def test_a_plain_note_has_nothing_to_lose(self):
        chord = self.ghost_cymbal_chord()
        kick = chord["notes"][0]
        self.assertEqual(analyze._note_options(kick, chord),
                         [(100, False, False)])

    def test_two_mishit_ghosts_in_one_chord_are_both_named(self):
        # Two ghost pads at 1x, both hit too hard: each pays 100 - 50 = 50,
        # and the screen took one 100-point step. The old code subtracted the
        # chord's whole bonus (100) from each note and allowed only one
        # mis-hit per chord, so it found no answer at all.
        chord = {"combo_before": 0, "multiplier": 1, "multiplier_after": 1,
                 "in_sp": False, "points": {"ghost": 100},
                 "notes": [note("Red", False, 100, 1, 50, "ghost"),
                           note("Blue", False, 100, 1, 50, "ghost")]}
        text, short, certain = analyze.attribute(chord, [100])
        self.assertEqual(text, "ghost Red hit too hard; ghost Blue hit too hard")
        self.assertEqual(short, 100)
        self.assertTrue(certain)


if __name__ == "__main__":
    unittest.main(verbosity=2)
```

Run it:

```powershell
Push-Location C:\Users\Patrick\Downloads\Hydra\video-tools
py multiplier_test.py
Pop-Location
```

Expect all seven to fail or error on the current code. The step test gets `{1: 0, 2: 8, 3: 18, 4: 28}` because it still reads `multiplier` and marks the chord before the rise. The old-exe test sees no error. The mixed-chord test finds no answer, because the old code takes the chord's ghost bonus off both notes. The four `_note_options` and `attribute` tests error or fail, because `_note_options` still takes a third argument and the old pricing finds no answer for the two ghosts.

- [ ] **Step 11: Move video-tools onto the new fields.**

In video-tools\replay.py, replace the whole `multiplier_steps` function (from `def multiplier_steps(curve):` down to its `return steps`) with:

```python
def multiplier_steps(curve):
    """The chords whose hit takes the displayed multiplier up to 2, 3 and 4.

    These are the moments the disc has to change, and they are what the disc
    detector and its gate are anchored on.

    A chord's `multiplier_after` is the multiplier once the whole chord is hit,
    which is what the disc shows from then on. So the step is the first chord
    whose `multiplier_after` reaches the new value: the note that took the
    combo over the line is inside that chord, and the whole chord is hit at one
    moment. (The chord's own `multiplier` is what its first note was worth, so
    on a chord that straddles a step it still reads the old value.)
    """
    steps = {}
    last = None
    for i, c in enumerate(curve["chords"]):
        if "multiplier_after" not in c:
            raise ReplayError(
                "this hydra_replay predates the multiplier_after field; "
                "rebuild it (the default one lives at %s)" % DEFAULT_EXE)
        m = c["multiplier_after"]
        if last is not None and m > last and m in (2, 3, 4) and m not in steps:
            steps[m] = i
        if m == 1 and last is None:
            steps.setdefault(1, i)
        last = m
    return steps
```

In video-tools\analyze.py, replace

```python
def dynamics_of(chord):
    """The chord's dynamics bonus in points, and which sort it is.

    A ghost note has to be hit softly and an accent hard. The engine reports the
    bonus before the multiplier, so the points at stake are that figure times
    the multiplier. Inside Star Power the note's own doubling covers the rest.
    """
    points = chord["points"]
    multiplier = int(chord.get("multiplier", 1)) or 1
    for field, phrase in (("ghost", "ghost %s hit too hard"),
                          ("accent", "accent %s hit too soft")):
        raw = points.get(field) or 0
        if raw:
            return raw * multiplier, phrase
    return 0, None
```

with

```python
MISHIT_PHRASES = {"ghost": "ghost %s hit too hard",
                  "accent": "accent %s hit too soft"}


def _mishit_text(note):
    """How a mishit on this note reads.

    A ghost note has to be hit softly and an accent hard. The kind comes from
    the note's own `dynamic` field, so a chord holding both a ghost and an
    accent names each one correctly. What the mishit cost is the note's own
    `dynamics_bonus`, which _note_options takes off.
    """
    return MISHIT_PHRASES[note["dynamic"]] % _note_name(note)
```

Nothing else calls `dynamics_of` once the callers below change.

Replace the whole `_note_options` function with

```python
def _note_options(note, chord):
    """Every amount one note could actually have paid.

    A note inside a Star Power window pays double; if the window had already
    ended, or had not started yet, it pays single. If the note carries a ghost
    or accent and the player mishit it, the note pays without its own
    `dynamics_bonus`. Returns (points, lost_the_double, lost_the_dynamics) for
    each possibility.
    """
    base = note["sp_points"]
    bonus = note["dynamics_bonus"]
    in_sp = bool(chord.get("in_sp"))
    out = []
    for lost_dyn in ((False, True) if bonus else (False,)):
        value = base - (bonus if lost_dyn else 0)
        if value <= 0:
            continue
        if in_sp:
            out.append((2 * value, False, lost_dyn))
            out.append((value, True, lost_dyn))
        else:
            out.append((value, False, lost_dyn))
    return out
```

In `attribute`, replace

```python
    dynamics, phrase = dynamics_of(chord)
    options = [_note_options(n, chord, dynamics) for n in chord["notes"]]
```

with

```python
    options = [_note_options(n, chord) for n in chord["notes"]]
```

and, in its solution loop, delete these two lines. Each note now loses only its own bonus, so two mis-hit dynamic notes in one chord are a real answer:

```python
        if sum(1 for o in combo if o[2]) > 1:
            continue              # only one note carries the chord's dynamics
```

In `attribute_span`, replace

```python
        dynamics, phrase = dynamics_of(chord)
        for ni, note in enumerate(chord["notes"]):
            options = _note_options(note, chord, dynamics)
            if not options:
                return None
            entries.append((ci, ni, options, phrase))
```

with

```python
        for ni, note in enumerate(chord["notes"]):
            options = _note_options(note, chord)
            if not options:
                return None
            entries.append((ci, ni, options))
```

Nothing reads the fourth slot of an entry, so dropping `phrase` from it is safe.

and, in its solution loop, delete the one-mis-hit-per-chord check, for the same reason:

```python
        seen = {}
        clash = False
        for entry, option in zip(entries, combo):
            if option[2]:
                seen[entry[0]] = seen.get(entry[0], 0) + 1
                if seen[entry[0]] > 1:
                    clash = True
                    break
        if clash:
            continue
```

Further down `attribute_span`, delete the line just after `chord = span[where]`:

```python
    _, phrase = dynamics_of(chord)
```

Both `render` helpers, the one in `attribute` and the one in `attribute_span`, hold the same line. In each, replace

```python
        mishit = [phrase % _note_name(chord["notes"][i]) for i, _, g in lost if g]
```

with

```python
        mishit = [_mishit_text(chord["notes"][i]) for i, _, g in lost if g]
```

In video-tools\README.md, replace "The engine reports that bonus per chord before the multiplier, so what was actually lost is that figure times the multiplier, doubled again inside Star Power." with "hydra_replay reports that bonus per note, with the note's own multiplier and a dynamic cymbal's extra 15 included, so a mis-hit ghost cymbal at 2x loses 130, doubled again inside Star Power."

- [ ] **Step 12: Run the video-tools tests and check_refs.**

```powershell
Push-Location C:\Users\Patrick\Downloads\Hydra\video-tools
py multiplier_test.py
py check_refs.py *> "$env:TEMP\check_refs_after_task8.txt"
Pop-Location
Get-Content "$env:TEMP\check_refs_after_task8.txt" | Select-String 'PASS|FAIL|cases passed'
Compare-Object (Get-Content "$env:TEMP\check_refs_before_task8.txt" | Select-String 'PASS|FAIL|cases passed' | ForEach-Object Line) (Get-Content "$env:TEMP\check_refs_after_task8.txt" | Select-String 'PASS|FAIL|cases passed' | ForEach-Object Line)
```

Expect `OK` from multiplier_test.py, "9 of 9 cases passed." from check_refs, and no output from Compare-Object. Each case line prints only its loss count and total, which come from the score steps, so they must not move. If any case line differs or fails, stop and report both outputs to the user; do not tune video-tools to make it pass.

Then show what the pricing fix did change. check_refs leaves each case's report in the folder its "reports go to" line names. For the before and after runs, list every loss whose kind or note text differs:

```powershell
$before = (Select-String -LiteralPath "$env:TEMP\check_refs_before_task8.txt" -Pattern 'reports go to (.+)$').Matches[0].Groups[1].Value.Trim()
$after  = (Select-String -LiteralPath "$env:TEMP\check_refs_after_task8.txt"  -Pattern 'reports go to (.+)$').Matches[0].Groups[1].Value.Trim()
Get-ChildItem -LiteralPath $after -Filter *.json | ForEach-Object {
    $a = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
    $b = Get-Content -LiteralPath (Join-Path $before $_.Name) -Raw | ConvertFrom-Json
    for ($i = 0; $i -lt $a.losses.Count; $i++) {
        $x = $b.losses[$i] | ConvertTo-Json -Compress -Depth 6
        $y = $a.losses[$i] | ConvertTo-Json -Compress -Depth 6
        if ($x -ne $y) { "{0} loss {1}`n  before: {2}`n  after:  {3}" -f $_.BaseName, $i, $x, $y }
    }
}
```

Report that list to the user as the visible effect of the fix. Each loss whose `kind` changed moves its `points` between the "other" and "dynamics" totals in album.py's "by kind" column; name those moves in the report too. An empty list is a valid result: it means no reference run had a mis-hit on a dynamic cymbal, a straddling chord, or two dynamic notes in one chord.

- [ ] **Step 13: Commit in video-tools.**

```powershell
git -C C:\Users\Patrick\Downloads\Hydra\video-tools add replay.py analyze.py README.md multiplier_test.py
git -C C:\Users\Patrick\Downloads\Hydra\video-tools commit -m @'
Read hydra_replay's applied multipliers and per-note dynamics

hydra_replay now writes a per-note multiplier and dynamics_bonus and a
per-chord multiplier_after. multiplier_steps takes the first chord whose
multiplier_after rises (the same chords as before). A mis-hit ghost or
accent is priced at its own note's dynamics_bonus: the note's own
multiplier, and 65 rather than 50 per multiplier on a dynamic cymbal.
Two mis-hit dynamic notes in one chord can now both be named, and
each mis-hit is worded from its own note's ghost or accent kind.
check_refs: all nine cases pass, unchanged.

Task: Task 8: hydra_replay reports the multiplier the score actually used
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@
```

---

### Task 9: Scoring facts get one owner each

Today the engine's price rules are written out more than once. After Task 1, `category_scores` in src/core/scoring.cpp computes the squeeze-out cut by hand as `(basevalue + cymb) * combo_multiplier * (is_dynamic ? 2 : 1)`. That is the same number `ChordNote::basescore()` already gives, times the multiplier. The note value itself (50, plus 15 for a cymbal, doubled for a dynamic) is written in `ChordNote::basescore` in src/core/model.cpp and again as `basevalue`/`cymbvalue` in scoring.cpp. The subtraction "the chord's SP value minus the cut" is spelled out in both src/search/graph.cpp and src/core/replay.cpp. The solo bonus of 100 per note sits in graph.cpp and replay.cpp. The 500 ms squeeze window has two names: `kSqueezeWindowMs` in src/search/graph.h and `kBackendDisplayWindowMs` in src/core/model.h. "One SP bar is two measures" is a bare `2` in about a dozen `plusmeasure` calls. This task gives each fact one named home and points every copy at it. Nothing visible changes.

**Goal:** Note value, the squeeze-out cut, the kept-SP subtraction, the solo bonus, the 500 ms window and the bars-to-measures rule each live in exactly one named place, and the engine's output does not change.

**Files:** Changes src/core/model.h, src/core/model.cpp, src/core/scoring.h, src/core/scoring.cpp, src/core/timing.h, src/core/replay.cpp, src/core/squeeze_rating.cpp, src/search/graph.h, src/search/graph.cpp, src/app/preview_view.cpp and tools/replay.cpp; adds cases to tests/test_model.cpp and tests/test_timing.cpp; rewrites one case in tests/test_squeeze_rating.cpp; tidies includes in tests/test_replay.cpp and tests/test_preview_view.cpp.

**Acceptance Criteria:**
- [ ] New test "note value: one owner for base, cymbal and dynamic points" passes.
- [ ] New test "category_scores: the squeeze-out cut is basescore at each note's multiplier" passes for both `first_note` and `whole_chord`, including a chord that crosses the 1x-to-2x boundary.
- [ ] New test "timing: one SP bar is two measures" passes.
- [ ] The rewritten test "display_backends: 500 ms window keeps everything the 500 ms search graph collects" passes and reads `kSqueezeWindowMs`.
- [ ] Task 1's test of the two squeeze-out rules and Task 8's multiplier tests still pass unchanged.
- [ ] `kBackendDisplayWindowMs` no longer exists anywhere in src/, tests/ or tools/, and `kSqueezeWindowMs` is defined only in src/core/model.h.
- [ ] A search of src/ for `sp - sg.sqout_reduction` and `sp - sqout_reduction` finds only the body of `CategoryScores::sqout_sp` in src/core/scoring.h.
- [ ] The full `hydra_tests` run is green.
- [ ] The hydra_batch diff against `$env:TEMP\hydra_task3\batch_sorted.txt` prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` -> `[doctest] Status: SUCCESS!`, then the batch diff in Step 11 -> no output.

**Steps:**

- [ ] **Step 1: Write the failing scoring tests**

At the top of tests/test_model.cpp, add `#include "core/scoring.h"` and `#include "core/timing.h"` under `#include "core/model.h"`, if they are not there already. Then add these two cases after "basescore matches ChordNote.basescore". The combo values include 8, so the second note of the chord lands on combo 10 and is worth 2x while the first is still 1x. That is the case where "one multiplier for the whole chord" would be wrong.

```cpp
TEST_CASE("note value: one owner for base, cymbal and dynamic points") {
    CHECK(kNoteBasePoints == 50);
    CHECK(kCymbalBonusPoints == 15);
    CHECK(kSoloBonusPerNote == 100);
    const ChordNote accent_cymbal{NoteColor::Yellow, NoteDynamicType::Accent,
                                  NoteCymbalType::Cymbal, false};
    CHECK(accent_cymbal.basescore() == (kNoteBasePoints + kCymbalBonusPoints) * 2);
    CHECK(ChordNote{NoteColor::Red}.basescore() == kNoteBasePoints);
}

TEST_CASE("category_scores: the squeeze-out cut is basescore at each note's multiplier") {
    Chord c;
    c.at(NoteColor::Red) = ChordNote{NoteColor::Red};
    c.at(NoteColor::Yellow) = ChordNote{NoteColor::Yellow, NoteDynamicType::Accent,
                                        NoteCymbalType::Cymbal, false};
    const std::vector<ChordNote> notes = c.notes(true);
    REQUIRE(notes.size() == 2);
    for (int combo : {0, 8, 9, 29, 45}) {
        CAPTURE(combo);
        // first_note (the default): only note 0 loses its SP doubling.
        std::vector<CategoryScores> per_note;
        const CategoryScores cs = category_scores(c, combo, &per_note);
        const int first_cut = notes[0].basescore() * to_multiplier(combo + 1);
        CHECK(cs.sqout_reduction == first_cut);
        REQUIRE(per_note.size() == 2);
        CHECK(per_note[0].sqout_reduction == first_cut);
        CHECK(per_note[1].sqout_reduction == 0);
        CHECK(cs.sqout_sp() == cs.sp - cs.sqout_reduction);

        // whole_chord: every note loses it, each at its own multiplier.
        std::vector<CategoryScores> whole_per_note;
        const CategoryScores whole =
            category_scores(c, combo, &whole_per_note, core::SqOutRule::WholeChord);
        int whole_cut = 0;
        for (size_t i = 0; i < notes.size(); ++i) {
            const int note_cut =
                notes[i].basescore() * to_multiplier(combo + 1 + static_cast<int>(i));
            REQUIRE(whole_per_note.size() == 2);
            CHECK(whole_per_note[i].sqout_reduction == note_cut);
            whole_cut += note_cut;
        }
        CHECK(whole.sqout_reduction == whole_cut);
        CHECK(whole.sqout_sp() == whole.sp - whole.sqout_reduction);
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build fails with `kNoteBasePoints`, `kCymbalBonusPoints` and `kSoloBonusPerNote` undeclared and `sqout_sp` not a member of `CategoryScores`.

- [ ] **Step 3: Give note value, the cut and the kept-SP subtraction one owner each**

In src/core/model.h, add this block directly above `// ---- ChordNote ----`:

```cpp
// ---- note value ----------------------------------------------------------
// What one note is worth before any multiplier. ChordNote::basescore and
// category_scores both read these, so the price has one home.
inline constexpr int kNoteBasePoints = 50;
inline constexpr int kCymbalBonusPoints = 15;
// Solo bonus: this many points per note hit inside a solo section.
inline constexpr int kSoloBonusPerNote = 100;
```

In src/core/model.cpp, `ChordNote::basescore` today reads:

```cpp
    int points = is_cymbal() ? 65 : 50;
    if (is_dynamic()) points *= 2;
    return points;
```

Replace it with:

```cpp
    int points = kNoteBasePoints + (is_cymbal() ? kCymbalBonusPoints : 0);
    if (is_dynamic()) points *= 2;
    return points;
```

In src/core/scoring.h, add one member function to `struct CategoryScores`, right after the `sqout_reduction` field. Task 8 put a `multiplier` field after it; leave that field where it is.

```cpp
    // The SP points this chord keeps when it is squeezed out: the chord's SP
    // value minus the lost doubling. The one place that subtraction lives.
    int sqout_sp() const { return sp - sqout_reduction; }
```

That member is the only thing added to the struct. It exists so the search graph and the replay walk stop spelling out `sp - sqout_reduction` themselves. Both callers switch to it below, and the acceptance search proves no copy is left.

In src/core/scoring.cpp, the note-value lines inside the loop read:

```cpp
        const int basevalue = 50;
        const int cymbvalue = 15;
```

Replace them with:

```cpp
        const int basevalue = kNoteBasePoints;
        const int cymbvalue = kCymbalBonusPoints;
```

After Task 1, the squeeze-out block reads:

```cpp
        // SqOut: the notes that lose their SP doubling. FirstNote keeps the
        // original quick calculation (note 0 only); WholeChord takes every note.
        const bool loses_sp = i == 0 || sqout_rule == core::SqOutRule::WholeChord;
        const int note_sqout =
            loses_sp ? (basevalue + cymb) * combo_multiplier * (is_dynamic ? 2 : 1) : 0;
        sqout_reduction += note_sqout;
```

Replace it with:

```cpp
        // SqOut: the notes that lose their SP doubling. FirstNote takes note 0
        // only; WholeChord takes every note. A lost doubling is the note's full
        // value at its own multiplier, and basescore() is that value.
        const bool loses_sp = i == 0 || sqout_rule == core::SqOutRule::WholeChord;
        const int note_sqout = loses_sp ? note.basescore() * combo_multiplier : 0;
        sqout_reduction += note_sqout;
```

`note.basescore()` is `(50 + cymbal 15) * (dynamic ? 2 : 1)`, so the product is the same number as before. If an earlier task already changed these lines, apply the same change to the new lines: the only edit is swapping the hand-written `(basevalue + cymb) * combo_multiplier * (is_dynamic ? 2 : 1)` for `note.basescore() * combo_multiplier`. Keep every line Task 8 added in this function exactly as it is: `first_multiplier`, the line that records the first note's multiplier, `note_scores.multiplier = combo_multiplier;` and `out.multiplier = first_multiplier;`. The per-note line `note_scores.sqout_reduction = note_sqout;` that Task 1 wrote stays too. It already reads the one value.

In src/search/graph.cpp, the backend store reads `store_new_backend(timestamp, sg.sp, sg.sp - sg.sqout_reduction);`. Change it to `store_new_backend(timestamp, sg.sp, sg.sqout_sp());`.

In src/core/replay.cpp, Task 3 routes each backend row through `core::backend_row_value`, passing `sg.sp - sg.sqout_reduction` as the squeezed-out chord's kept SP. Change that argument to `sg.sqout_sp()`. Task 5 may have moved the call; search replay.cpp for `sqout_reduction` and change every `sg.sp - sg.sqout_reduction` it finds. If an earlier task already changed these lines, apply the same change to the new lines.

For the solo bonus, graph.cpp reads `store_soloscore(100 * timestamp.chord.count());`. Change it to `store_soloscore(kSoloBonusPerNote * timestamp.chord.count());`. replay.cpp reads `row.points.solo = ts.flag_solo ? 100LL * ts.chord.count() : 0;`. Change it to:

```cpp
        row.points.solo =
            ts.flag_solo ? static_cast<int64_t>(kSoloBonusPerNote) * ts.chord.count() : 0;
```

- [ ] **Step 4: Run the scoring tests and watch them pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="note value*,category_scores*,basescore*,a ghost or accent kick*"`. Expected: all pass, including Task 1's and Task 8's own `category_scores` cases.

- [ ] **Step 5: Point the window test at the one window constant (fails first)**

The test "display_backends: 500 ms window keeps everything the 500 ms search graph collects" in tests/test_squeeze_rating.cpp hard-codes 499 and 520 and names `kBackendDisplayWindowMs` in a comment. Replace it so it reads the constant itself:

```cpp
TEST_CASE("display_backends: 500 ms window keeps everything the 500 ms search graph collects") {
    // The display window IS the search graph's squeeze window (one constant,
    // kSqueezeWindowMs), so nothing the graph gathers gets trimmed at
    // store/display time. 180 and -300 are the regression: both used to fall
    // outside the old +-170 window and get dropped.
    CHECK(kSqueezeWindowMs == 500.0);
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    SongTiming st(480, tpm, bpm);

    Activation act;
    act.timecode = st.timecode(0);
    act.sp_meter = 2;

    const double offsets[] = {0.0, 180.0, -300.0, kSqueezeWindowMs - 1.0,
                              kSqueezeWindowMs + 20.0};
    for (double off : offsets) {
        BackendSqueeze row;
        row.timecode = st.timecode(0);
        row.offset_ms = off;
        act.backends.push_back(row);
    }

    std::vector<BackendSqueeze> kept = act.display_backends();
    REQUIRE(kept.size() == 4);
    CHECK(kept[0].offset_ms == doctest::Approx(0.0));
    CHECK(kept[1].offset_ms == doctest::Approx(180.0));
    CHECK(kept[2].offset_ms == doctest::Approx(-300.0));
    CHECK(kept[3].offset_ms == doctest::Approx(kSqueezeWindowMs - 1.0));
}
```

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build fails with `kSqueezeWindowMs` undeclared. test_squeeze_rating.cpp includes only core/squeeze_rating.h, and the constant still lives in search/graph.h.

- [ ] **Step 6: Move the 500 ms window into model.h and delete the second name**

In src/core/model.h, these lines today read:

```cpp
// Backends within this window of the deactivation are stored and shown.
// 500 matches the graph's squeeze window, so nothing the engine collects
// is trimmed; the Backend limit setting narrows the display from here.
constexpr double kBackendDisplayWindowMs = 500.0;
```

Replace them with:

```cpp
// The squeeze horizon in ms. The search graph only looks this far from the SP
// end for a reachable squeeze, and backends within it of the deactivation are
// stored and shown, so nothing the engine collects is trimmed. The Backend
// limit setting narrows the display from here. hydra_batch prints it.
constexpr double kSqueezeWindowMs = 500.0;
```

In src/search/graph.h, delete these lines:

```cpp
// Reachable-squeeze horizon in ms. Exposed so the batch CLI can print it in
// its settings header.
constexpr double kSqueezeWindowMs = 500.0;
```

graph.h already includes core/model.h, so every file that reached the constant through graph.h still sees it. That includes src/cli/batch.cpp, which prints it, and the later tasks that use it through graph.h.

In src/search/graph.cpp, delete the alias line `constexpr double SQUEEZE_WINDOW_MS = kSqueezeWindowMs;  // exported in graph.h`. Then change its two uses (`< SQUEEZE_WINDOW_MS)` and `return head_time_offset(tc) < SQUEEZE_WINDOW_MS;`) to read `kSqueezeWindowMs`.

In src/core/model.cpp, the one use of `kBackendDisplayWindowMs` (in `display_backends`) becomes `kSqueezeWindowMs`.

src/core/replay.cpp includes search/graph.h only for this constant (a four-line include block with its comment). Replace that block with `#include "core/model.h"  // kSqueezeWindowMs, the engine's squeeze horizon`. If an earlier task added a real graph.h use to replay.cpp, keep the graph.h include and drop only its comment about the constant. In tests/test_replay.cpp, `#include "search/graph.h"  // kSqueezeWindowMs, the horizon the warning uses` becomes `#include "core/model.h"  // kSqueezeWindowMs, the horizon the warning uses`, under the same condition.

- [ ] **Step 7: Run it and watch it pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="display_backends*,replay*"`. Expected: all pass.

- [ ] **Step 8: Write the failing SP-length test**

Add to the end of tests/test_timing.cpp:

```cpp
TEST_CASE("timing: one SP bar is two measures") {
    CHECK(hydra::kMeasuresPerSpBar == 2);
    CHECK(hydra::sp_bars_to_measures(1) == 2);
    CHECK(hydra::sp_bars_to_measures(4) == 8);
    CHECK(hydra::sp_bars_to_measures(-1) == -2);  // one phrase back, as squeeze_rating uses it

    // 4/4 at 480 ticks per beat: a measure is 1920 ticks, so two bars of SP
    // run four measures, 7680 ticks.
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    hydra::SongTiming st(480, tpm, bpm);
    CHECK(st.plusmeasure(st.timecode(0), hydra::sp_bars_to_measures(2)).ticks() == 7680);
}
```

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build fails with `kMeasuresPerSpBar` and `sp_bars_to_measures` not members of `hydra`.

- [ ] **Step 9: Add the helper next to plusmeasure and use it everywhere SP length is measured**

In src/core/timing.h, inside `namespace hydra`, add this above the declaration that holds `plusmeasure`:

```cpp
// One bar of Star Power lasts two measures. Every "how long does this much SP
// run" call to plusmeasure goes through this, so the rule has one home.
inline constexpr int64_t kMeasuresPerSpBar = 2;
constexpr int64_t sp_bars_to_measures(int64_t bars) { return kMeasuresPerSpBar * bars; }
```

Then change each site that turns SP bars into measures. Each one computes "bars of SP times two measures", so the swap does not change any result. If an earlier task already changed these lines, apply the same change to the new lines.

In src/search/graph.cpp, one phrase's extension is written `plusmeasure(..., 2)` in five places. Change each literal `2` to `sp_bars_to_measures(1)`. The ceiling line `Timecode ceiling = plusmeasure(sp_timecode, 2 * (*sp_meter_cap_));` becomes `plusmeasure(sp_timecode, sp_bars_to_measures(*sp_meter_cap_))`. The activation end `plusmeasure(act_edge->dest->timecode, 2 * sp);` becomes `plusmeasure(act_edge->dest->timecode, sp_bars_to_measures(sp));`.

In src/core/squeeze_rating.cpp, `has_sqin ? timing.plusmeasure(timing.timecode(post_tick), -2).ticks() : post_tick;` changes its `-2` to `-sp_bars_to_measures(1)`. The line `const int64_t end_measures = 2 * static_cast<int64_t>(*act.sp_meter);` becomes `const int64_t end_measures = sp_bars_to_measures(*act.sp_meter);`.

In src/app/preview_view.cpp (`build_sp_meter_curve`), the meter is tracked as a double count of measures. `double remaining = 2.0 * static_cast<double>(act.sp_meter);` becomes `double remaining = static_cast<double>(sp_bars_to_measures(act.sp_meter));`. Both `remaining / 2.0` become `remaining / static_cast<double>(kMeasuresPerSpBar)`. `remaining = std::min(remaining + 2.0, 2.0 * cap);` becomes `remaining = std::min(remaining + static_cast<double>(kMeasuresPerSpBar), static_cast<double>(sp_bars_to_measures(cap)));`. If the `(end_measures - act_measures) / 2.0` lines still exist when this task runs (Task 2 may have replaced them with `collected_phrase_ticks`), change that `2.0` to `static_cast<double>(kMeasuresPerSpBar)` too.

In tools/replay.cpp, `paths_json` and the check path both compute the nominal end as `plusmeasure(*act.timecode, 2 * static_cast<int64_t>(*act.sp_meter))`. Change both to `plusmeasure(*act.timecode, sp_bars_to_measures(*act.sp_meter))`.

In tests/test_preview_view.cpp, `a.deact_tick = song.timing().plusmeasure(*a.timecode, 2 * static_cast<int64_t>(sp_meter)).ticks();` changes to use `sp_bars_to_measures(sp_meter)`.

Three places look similar but are not SP lengths, so leave them alone. In src/search/engine.cpp, one line clamps the meter to the cap in bars, and another is the two-bar activation minimum. In src/app/preview_view.cpp, `bank = std::min(bank + 1.0, cap);` counts bars, not measures.

- [ ] **Step 10: Run the full suite**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `[doctest] Status: SUCCESS!`.

- [ ] **Step 11: Prove the engine did not move**

```powershell
.\build_cpp.ps1 -Target hydra_batch
$out = "$env:TEMP\hydra_task9"; New-Item -ItemType Directory -Force $out | Out-Null
Remove-Item "$out\after.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_batch.exe --db "$out\after.db" testdata\input |
  Select-String '^\[\d+/\d+\] ' | ForEach-Object { $_.Line -replace '^\[\d+/\d+\] ', '' } |
  Sort-Object | Set-Content "$out\batch_sorted.txt"
Compare-Object (Get-Content "$env:TEMP\hydra_task3\batch_sorted.txt") (Get-Content "$out\batch_sorted.txt")
```

Expected: `Compare-Object` prints nothing. The header line `Squeeze win: 500 ms` still prints, now read from model.h.

- [ ] **Step 12: Commit**

```powershell
git add src/core/model.h src/core/model.cpp src/core/scoring.h src/core/scoring.cpp src/core/timing.h src/core/replay.cpp src/core/squeeze_rating.cpp src/search/graph.h src/search/graph.cpp src/app/preview_view.cpp tools/replay.cpp tests/test_model.cpp tests/test_timing.cpp tests/test_squeeze_rating.cpp tests/test_replay.cpp tests/test_preview_view.cpp
git commit -m "Give each scoring fact one owner

Note value, the squeeze-out cut, the kept-SP subtraction, the solo bonus,
the 500 ms squeeze window and the two-measures-per-SP-bar rule were each
written out in two or more places. Each now has one named home (model.h,
ChordNote::basescore, CategoryScores::sqout_sp, timing.h) and every copy
reads it. kBackendDisplayWindowMs is gone; it was the same 500 ms as the
graph's kSqueezeWindowMs. No score changes: the corpus hydra_batch output
is identical to the Task 3 baseline.

Task: Scoring facts get one owner each
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Difficulty and cap rules get one owner each

Today the rule for "how hard is this squeeze" is written twice. `SPSqueeze::difficulty` in src/core/model.h and the engine's `act_difficulty` in src/search/engine.cpp each spell out "SqIn is its offset, SqOut is its negated offset". The E0 test ("inside the calibration-fill window and no skips") is likewise in `Activation::is_E0` in src/core/model.cpp and again inline in `act_difficulty`. The Song Details path list picks its warning color by comparing `path.difficulty()` to `kDifficultMs` itself in src/app/path_view.cpp, instead of asking the path. The "don't build a graph taller than the song has phrases" clamp is written twice in src/search/pather.cpp. Clone Hero's 4-bar SP cap is a bare `4` in src/app/config.h, src/search/pather.h, src/app/preview_view.h (twice) and the hint text in src/ui/details_view.cpp. The combined squeeze budget in the backend tooltip is written `2.0 * W` instead of calling `squeeze_budget_ms`. This task gives each rule one function or constant. The engine's legality rule in engine.cpp is not touched. Nothing visible changes.

**Goal:** Squeeze difficulty, the E0 test, path difficulty, the graph build cap, the Clone Hero SP cap and the tooltip's budget each come from one function or constant, and the engine's output does not change.

**Files:** Changes src/core/model.h, src/core/model.cpp, src/search/engine.cpp, src/search/pather.h, src/search/pather.cpp, src/app/path_view.cpp, src/app/config.h, src/app/preview_view.h and src/ui/details_view.cpp; adds cases to tests/test_model.cpp, tests/test_search.cpp and tests/test_squeeze_rating.cpp.

**Acceptance Criteria:**
- [ ] New test "squeeze_difficulty and is_e0: one owner for the engine and the model" passes, including the `+0.0` sign check.
- [ ] New test "Path::is_difficult: past the difficult floor, not at it" passes.
- [ ] New test "graph_build_cap: never taller than the song's phrases, never below one" passes.
- [ ] New test "squeeze_budget_ms: identity scale is twice the hit window" passes.
- [ ] The existing test "build_path_row: right-aligned ms, warn past the difficult floor" still passes.
- [ ] A search of src/ for `sp_cap = 4` and `int cap = 4` finds nothing.
- [ ] The full `hydra_tests` run is green.
- [ ] The hydra_batch diff against `$env:TEMP\hydra_task3\batch_sorted.txt` prints nothing.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` -> `[doctest] Status: SUCCESS!`, then the batch diff in Step 9 -> no output.

**Steps:**

- [ ] **Step 1: Write the failing model tests**

Add to tests/test_model.cpp, after "squeeze symbols, timing, difficulty". Add `#include <cmath>` to its includes for `std::signbit`.

```cpp
TEST_CASE("squeeze_difficulty and is_e0: one owner for the engine and the model") {
    CHECK(squeeze_difficulty(/*is_sqin=*/true, 3.5) == 3.5);
    CHECK(squeeze_difficulty(/*is_sqin=*/false, -3.5) == 3.5);
    // A SqOut at exactly 0 is +0.0, never -0.0: the "-x + 0.0" idiom stays.
    CHECK_FALSE(std::signbit(squeeze_difficulty(false, 0.0)));
    CHECK(SPSqueeze{SqueezeKind::SqOut, -3.5}.difficulty() == squeeze_difficulty(false, -3.5));

    CHECK(is_e0(kCalibrationFillWindowMs - 0.1, 0));
    CHECK_FALSE(is_e0(kCalibrationFillWindowMs, 0));
    CHECK_FALSE(is_e0(10.0, 1));
    CHECK(calibration_fill_difficulty(-4.0) == 4.0);
    CHECK_FALSE(std::signbit(calibration_fill_difficulty(0.0)));

    Activation a;
    a.e_offset = 10.0;
    a.skips = 0;
    CHECK(a.is_E0() == is_e0(10.0, 0));
    REQUIRE(a.e_difficulty().has_value());
    CHECK(*a.e_difficulty() == calibration_fill_difficulty(10.0));
}

TEST_CASE("Path::is_difficult: past the difficult floor, not at it") {
    Path empty;
    CHECK_FALSE(empty.is_difficult());

    Activation a;
    a.skips = 0;
    a.e_offset = 300.0;  // not e-critical
    a.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -(kDifficultMs + 0.5)});
    Path hard;
    hard.activations.push_back(a);
    CHECK(hard.is_difficult());

    Path edge = hard;
    edge.activations[0].sqinouts[0].offset_ms = -kDifficultMs;
    CHECK_FALSE(edge.is_difficult());  // exactly at the floor is not past it
}
```

- [ ] **Step 2: Run it and watch it fail**

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build fails with `squeeze_difficulty`, `is_e0` and `calibration_fill_difficulty` undeclared and `is_difficult` not a member of `Path`.

- [ ] **Step 3: Add the free functions and route both callers through them**

In src/core/model.h, add directly above `struct SPSqueeze`:

```cpp
// How hard a squeeze is, in ms: a SqIn's offset, or a SqOut's offset negated.
// "-x + 0.0" turns -0.0 into +0.0 so a dead-on SqOut prints "0.0". The engine's
// act_difficulty and SPSqueeze::difficulty both call this.
inline double squeeze_difficulty(bool is_sqin, double offset_ms) {
    return is_sqin ? offset_ms : (-offset_ms + 0.0);
}

// E0: the calibration fill lands inside its window and nothing was skipped.
inline bool is_e0(double e_offset, int skips) {
    return e_offset < kCalibrationFillWindowMs && skips == 0;
}

// How hard an E0 activation's calibration fill is, in ms.
inline double calibration_fill_difficulty(double e_offset) { return -e_offset + 0.0; }
```

In `SPSqueeze`, `difficulty()` today reads:

```cpp
    double difficulty() const { return kind == SqueezeKind::SqIn ? offset_ms : (-offset_ms + 0.0); }
```

Replace it with:

```cpp
    double difficulty() const { return squeeze_difficulty(kind == SqueezeKind::SqIn, offset_ms); }
```

In `Path` (model.h, next to `difficulty()`), declare:

```cpp
    // True when the hardest squeeze or E0 fill is past kDifficultMs: the
    // warning color's rule, asked of the path instead of re-derived by callers.
    bool is_difficult() const;
```

In src/core/model.cpp, the E0 pair today reads:

```cpp
bool Activation::is_E0() const { return is_e_critical() && *skips == 0; }
std::optional<double> Activation::e_difficulty(bool verbose) const { if (is_E0() || verbose) return -*e_offset + 0.0; return std::nullopt; }
```

Replace it with:

```cpp
bool Activation::is_E0() const { return is_e0(*e_offset, *skips); }
std::optional<double> Activation::e_difficulty(bool verbose) const {
    if (is_E0() || verbose) return calibration_fill_difficulty(*e_offset);
    return std::nullopt;
}
```

Keep `is_e_critical()` as it is. Other code reads it on its own. After `Path::difficulty`, add:

```cpp
bool Path::is_difficult() const {
    const std::optional<double> d = difficulty();
    return d && *d > kDifficultMs;
}
```

In src/search/engine.cpp `act_difficulty`, the squeeze line reads:

```cpp
const double d = sqs_[(size_t)s].kind == SQ_IN ? sqs_[(size_t)s].offset : -sqs_[(size_t)s].offset + 0.0;
```

Replace it with:

```cpp
const double d = squeeze_difficulty(sqs_[(size_t)s].kind == SQ_IN, sqs_[(size_t)s].offset);
```

The E0 block reads `if (a.e_offset < kCalibrationFillWindowMs && a.skips == 0) { const double d = -a.e_offset + 0.0; ...`. Replace its test with `if (is_e0(a.e_offset, a.skips))` and the value with `const double d = calibration_fill_difficulty(a.e_offset);`. Leave the rest of the block alone. If an earlier task already changed these lines, apply the same change to the new lines. Leave the engine's legality rule untouched: it asks "can this fill be summoned at all", which is a different question from difficulty.

In src/app/path_view.cpp, `build_path_row` reads `row.warn = *diff > kDifficultMs;`. Replace it with `row.warn = path.is_difficult();`.

- [ ] **Step 4: Run it and watch it pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="squeeze_difficulty*,Path::is_difficult*,build_path_row*,squeeze symbols*"`. Expected: all pass.

- [ ] **Step 5: Write the failing build-cap and budget tests**

Add to the end of tests/test_search.cpp:

```cpp
TEST_CASE("graph_build_cap: never taller than the song's phrases, never below one") {
    CHECK(graph_build_cap(4, 10) == 4);   // the cap binds
    CHECK(graph_build_cap(32, 3) == 3);   // the song's phrases bind
    CHECK(graph_build_cap(8, 0) == 1);    // a phraseless song still builds one level
}
```

Add to the end of tests/test_squeeze_rating.cpp:

```cpp
TEST_CASE("squeeze_budget_ms: identity scale is twice the hit window") {
    // The backend tooltip's "not %.0fms" figure is this call, not 2.0 * W.
    CHECK(squeeze_budget_ms(1.0, 85.0) == 170.0);
    CHECK(squeeze_budget_ms(1.0, 40.0) == 80.0);
}
```

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build fails with `graph_build_cap` undeclared. The budget case compiles and passes already. It pins the value the tooltip change in Step 6 relies on.

- [ ] **Step 6: Add graph_build_cap, name the SP cap, and fix the tooltip**

In src/search/pather.h, inside `namespace hydra`, add:

```cpp
// How tall to build the search graph: the SP cap, but never more levels than
// the song has phrases to bank, and never fewer than one.
int graph_build_cap(int sp_cap, int sp_phrase_count);
```

In src/search/pather.cpp, add the definition:

```cpp
int graph_build_cap(int sp_cap, int sp_phrase_count) {
    return std::min(sp_cap, std::max(sp_phrase_count, 1));
}
```

The first clamp reads `int build_cap = std::min(sp_cap, std::max(sp_phrases, 1));`. Replace it with `int build_cap = graph_build_cap(sp_cap, sp_phrases);`. The second reads `int build_cap = std::min(*sp_cap, std::max(song.sp_phrase_count(), 1));`. Replace it with `int build_cap = graph_build_cap(*sp_cap, song.sp_phrase_count());`.

Now the Clone Hero cap. `kCloneHeroSpCap` already lives in src/core/model.h.

In src/app/config.h, `std::optional<int> sp_cap = 4;` becomes `std::optional<int> sp_cap = kCloneHeroSpCap;` (config.h reaches model.h through app/analysis.h).

In src/search/pather.h, `std::optional<int> sp_cap = 4;` becomes `std::optional<int> sp_cap = kCloneHeroSpCap;`.

In src/app/preview_view.h, `int cap = 4;  // the ceiling in bars: 4 in Clone Hero` becomes `int cap = kCloneHeroSpCap;  // the ceiling in bars`. `PreviewScene build_preview_scene(const Song& song, const Path* path, int sp_cap = 4);` changes its default to `int sp_cap = kCloneHeroSpCap`.

In src/ui/details_view.cpp, the hint reads:

```cpp
hint("4 bars is Clone Hero's rule. Higher caps are what-ifs; Auto raises the "
     "cap until the score stops improving.");
```

Replace it with:

```cpp
hint((std::to_string(kCloneHeroSpCap) +
      " bars is Clone Hero's rule. Higher caps are what-ifs; Auto raises the "
      "cap until the score stops improving.").c_str());
```

Add `#include <string>` to details_view.cpp's standard includes. The text stays "4 bars is Clone Hero's rule. ...".

Finally the tooltip. In src/app/path_view.cpp, the backend tooltip reads:

```cpp
                std::snprintf(tip, sizeof(tip),
                              "Effectively %.1fms on the normal %.0fms scale:\n"
                              "frontend timing scales x%.2f here, so the combined\n"
                              "squeeze budget is %.0fms, not %.0fms.",
                              *br.effective_ms, 2.0 * W, br.scale,
                              br.budget_ms, 2.0 * W);
```

Replace the two `2.0 * W` arguments with one named value:

```cpp
                // The budget at identity scale (x1.00): what the combined
                // budget would be with no frontend-timing scale.
                const double normal_budget = squeeze_budget_ms(1.0, W);
                std::snprintf(tip, sizeof(tip),
                              "Effectively %.1fms on the normal %.0fms scale:\n"
                              "frontend timing scales x%.2f here, so the combined\n"
                              "squeeze budget is %.0fms, not %.0fms.",
                              *br.effective_ms, normal_budget, br.scale,
                              br.budget_ms, normal_budget);
```

path_view.cpp already reaches `squeeze_budget_ms` through app/path_view.h, which includes core/squeeze_rating.h. If an earlier task already changed these lines, apply the same change to the new lines.

- [ ] **Step 7: Run it and watch it pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="graph_build_cap*,squeeze_budget_ms*,build_activations*"`. Expected: all pass. The cap-literal swaps have no failing test of their own. Their check is the search in the acceptance criteria plus the unchanged Settings default. The existing test_config cases that read `Settings().sp_cap` must still pass.

- [ ] **Step 8: Run the full suite and check the hint in the GUI**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `[doctest] Status: SUCCESS!`.

Then build the GUI test driver with `.\build_cpp.ps1 -Target hydra_uitest`. Write this script to `$env:TEMP\hydra_task10.txt`:

```
wait-idle
expect-text 4 bars is Clone Hero's rule.
```

Run `.\build-cpp\Release\hydra_uitest.exe --script $env:TEMP\hydra_task10.txt`. Expected: it passes. If the hint only appears after the Settings panel opens, run `dump Hydra` first, read the settings control's label off it, and put a `click <that label>` line before `expect-text`.

- [ ] **Step 9: Prove the engine did not move**

```powershell
.\build_cpp.ps1 -Target hydra_batch
$out = "$env:TEMP\hydra_task10"; New-Item -ItemType Directory -Force $out | Out-Null
Remove-Item "$out\after.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_batch.exe --db "$out\after.db" testdata\input |
  Select-String '^\[\d+/\d+\] ' | ForEach-Object { $_.Line -replace '^\[\d+/\d+\] ', '' } |
  Sort-Object | Set-Content "$out\batch_sorted.txt"
Compare-Object (Get-Content "$env:TEMP\hydra_task3\batch_sorted.txt") (Get-Content "$out\batch_sorted.txt")
```

Expected: `Compare-Object` prints nothing.

- [ ] **Step 10: Commit**

```powershell
git add src/core/model.h src/core/model.cpp src/search/engine.cpp src/search/pather.h src/search/pather.cpp src/app/path_view.cpp src/app/config.h src/app/preview_view.h src/ui/details_view.cpp tests/test_model.cpp tests/test_search.cpp tests/test_squeeze_rating.cpp
git commit -m "Give each difficulty and cap rule one owner

Squeeze difficulty and the E0 test were spelled out in both the engine and
the model; both now call squeeze_difficulty / is_e0 /
calibration_fill_difficulty. The path list asks Path::is_difficult instead
of comparing to kDifficultMs itself. The graph build clamp is
graph_build_cap. Clone Hero's 4-bar cap is kCloneHeroSpCap everywhere, and
the backend tooltip's normal budget is squeeze_budget_ms. The engine's
legality rule is untouched. Corpus hydra_batch output is identical to the
Task 3 baseline.

Task: Difficulty and cap rules get one owner each
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Report and library facts get one owner each

Today several app-level facts are written twice. The path report's "Beyond" edge is `HIT_WINDOW * 2` in the page script and `2 * options.hit_window_ms` in the footer, both in src/app/report.cpp. The real edge already lives in `timing_tiers` in src/core/squeeze_rating.cpp. The "every path" sentinel 1000000000, its label threshold 100000000 and the default of 5 paths are bare numbers in src/cli/report.cpp, src/app/report.cpp, src/app/report.h and src/ui/library_jobs.cpp. The library's difficulty dropdown keeps its own list of names, separate from `difficulty_name`. So do `Settings::difficulty()` and hydra_replay's `--difficulty` parser, and the two disagree on case. The "No Hard Pro Drums notes in this chart." message is built twice (src/app/analysis.cpp and src/ui/preview_load_job.cpp). `lens_from` in src/app/analysis.cpp re-derives the store lens that `Settings::lens()` already computes. hydra_replay lists the six score fields three times. And a song with no usable name gets three different titles: a blank for `name =`, "<unknown title>" for a missing name key or an empty .sng/.srb name, and "(unknown)" in the path report. This task gives each one a single owner.

**What the user sees:** two changes, both approved. First, a song with no usable name reads "(unknown)" everywhere: the library table, Song Details and all three reports (path report, fill comparison, DM comparison). That covers an empty `name =`, a song.ini with no name line, and a .sng or .srb whose embedded name is empty. Titles that read "<unknown title>" today change to "(unknown)" (decisions 10 and 22). Library rows pick this up on the next scan. Reports pick it up at once, because they map the old stored names when they read them. The test corpus has no such chart, so hydra_batch output and Task 19's final diff do not change. Second, a hand-edited settings INI with `view_difficulty=hard` (any case) now opens on Hard instead of falling back to Expert (decision 27). The MIDI reader is not touched and keeps following mido on broken files. The path report's text is unchanged ("Beyond 170 ms", "Past 170 ms", "past the 170 ms window" at the default window), because it now reads the same number from `timing_tiers`.

**Goal:** Each report, library and replay-tool fact above is read from one owner, a song with no usable name reads "(unknown)" everywhere, and the settings difficulty matches any case.

**Files:** Changes src/core/squeeze_rating.h and .cpp, src/app/report.h, src/app/report.cpp, src/cli/report.cpp, src/ui/library_jobs.h and .cpp, src/ui/library_view.cpp, src/ui/preview_load_job.cpp, src/ui/app_state.cpp, src/parse/song.h and .cpp, src/app/config.cpp, src/app/analysis.h and .cpp, src/cli/batch.cpp, src/app/fill_report.cpp, src/app/dm_report.cpp, src/core/replay.h and tools/replay.cpp; adds cases to tests/test_squeeze_rating.cpp, tests/test_report.cpp, tests/test_model.cpp, tests/test_config.cpp, tests/test_analysis.cpp, tests/test_srb.cpp, tests/test_fill_report.cpp, tests/test_dm_report.cpp and tests/test_replay.cpp; removes one case from tests/test_config.cpp.

**Acceptance Criteria:**
- [ ] New test "beyond_edge_ms: the last finite timing-tier cutoff" passes: 170.0 at an 85 ms window, 80.0 at 40.
- [ ] New test "report page reads the Beyond edge from the tier table" passes, and the existing "generate_report: one seam frames the page for every entry point" still finds "past the 170 ms window", "songs — top 5 paths per chart" and "songs — every path".
- [ ] New tests "difficulty names: one list, one spelling" and "difficulty_from_name: any case, nothing else" pass.
- [ ] New test "view_difficulty matches any case and loads as the real name" passes, and the existing "view_difficulty round-trips, and a junk value normalizes to Expert" still passes.
- [ ] New test "no_notes_message names the difficulty and the drum mode" passes.
- [ ] New test "run_batch files results under the lens it is given" passes, and `lens_from` no longer exists in src/ or tests/.
- [ ] New test "title_or_unknown: one fallback for a song with no usable name" passes.
- [ ] New tests "discover_charts: a song with no usable name reads (unknown)", "rescan cache: an old placeholder or blank title reads (unknown)" and "srb: an empty embedded name reads (unknown)" pass.
- [ ] New tests "collect_rows: a blank or old-placeholder song name reads (unknown)", "collect_fill_rows: a blank stored song name reads (unknown)" and "collect_dm_rows: a blank stored song name reads (unknown)" pass.
- [ ] A search of src/ for `<unknown title>` finds only the `kOldPlaceholder` line in src/parse/song.cpp.
- [ ] New test "replay score fields: one list in schema order" passes.
- [ ] `hydra_replay dump` JSON for one corpus chart is byte-identical before and after (Steps 1 and 19).
- [ ] The full `hydra_tests` run is green.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` -> `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Capture one replay dump before any edit**

This is the "before" half of the byte-identity check in Step 19. Run it on the build that Task 10 left, before touching any file.

```powershell
.\build_cpp.ps1 -Target hydra_replay
$out = "$env:TEMP\hydra_task11"; New-Item -ItemType Directory -Force $out | Out-Null
$chart = (Get-ChildItem testdata\input -Recurse -Filter notes.chart | Sort-Object FullName | Select-Object -First 1).FullName
Remove-Item "$out\before.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_replay.exe dump --chart $chart --db "$out\before.db" --out "$out\replay_before.json"
```

Expected: it prints `wrote ...replay_before.json (N bytes)`. The database is new and empty, so dump analyzes the chart fresh.

- [ ] **Step 2: Write the failing Beyond-edge tests**

Add to the end of tests/test_squeeze_rating.cpp:

```cpp
TEST_CASE("beyond_edge_ms: the last finite timing-tier cutoff") {
    CHECK(beyond_edge_ms(85.0) == 170.0);
    CHECK(beyond_edge_ms(40.0) == 80.0);
    // It is the tier table's own number, not a second formula.
    double last = 0.0;
    for (const TimingTier& t : timing_tiers(85.0))
        if (t.cutoff) last = *t.cutoff;
    CHECK(beyond_edge_ms(85.0) == last);
}
```

Add to tests/test_report.cpp, after "report payload carries the hit window and the tier table". Like that case, it needs no store, because an empty row list still embeds the page script:

```cpp
TEST_CASE("report page reads the Beyond edge from the tier table") {
    std::string html = report::build_html({}, "sub", "foot", /*hit_window_ms=*/85.0);
    CHECK(html.find("const BEYOND = Math.max(") != std::string::npos);
    CHECK(html.find("HIT_WINDOW * 2") == std::string::npos);
    CHECK(html.find("'Past ' + BEYOND + ' ms'") != std::string::npos);
}
```

- [ ] **Step 3: Run it and watch it fail**

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build fails with `beyond_edge_ms` undeclared. Once Step 4's header line exists, the report case fails on the `BEYOND` check.

- [ ] **Step 4: Read the Beyond edge from timing_tiers**

In src/core/squeeze_rating.h, next to `timing_tiers`, declare:

```cpp
// Where the report's "Beyond" tier starts: the last finite cutoff of
// timing_tiers. The footer and the page script both print this number.
double beyond_edge_ms(double hit_window_ms);
```

In src/core/squeeze_rating.cpp, after `timing_tiers`, add:

```cpp
double beyond_edge_ms(double hit_window_ms) {
    double edge = 0.0;
    for (const TimingTier& t : timing_tiers(hit_window_ms))
        if (t.cutoff) edge = std::max(edge, *t.cutoff);
    return edge;
}
```

Add `#include <algorithm>` if squeeze_rating.cpp does not already include it.

In src/app/report.cpp, the footer reads:

```cpp
    std::string footer = "Generated from " + dbname +
                         ". Timing tiers match Hydra's squeeze ratings; "
                         "'Beyond' is past the " +
                         std::to_string(2 * options.hit_window_ms) +
                         " ms window.";
```

Replace `std::to_string(2 * options.hit_window_ms)` with `std::to_string(static_cast<int64_t>(beyond_edge_ms(w)))`. `w` is already defined a few lines above in the same function. The printed text stays "170" at the default window.

In kDataJs, the line `const HIT_WINDOW = DATA.hit_window;` gets one line after it:

```js
const BEYOND = Math.max(...DATA.tiers.filter(t => t.cutoff !== null).map(t => t.cutoff));
```

The line `o.textContent = t.name === 'Beyond' ? 'Beyond ' + (HIT_WINDOW * 2) + ' ms'` changes `(HIT_WINDOW * 2)` to `BEYOND`.

In kPageJs, `const beyond = rows.filter(r => r.ms !== null && r.ms >= HIT_WINDOW * 2).length;` changes `HIT_WINDOW * 2` to `BEYOND`. `['Past ' + (HIT_WINDOW * 2) + ' ms', beyond.toLocaleString()],` becomes `['Past ' + BEYOND + ' ms', beyond.toLocaleString()],`.

JavaScript prints the number 170.0 as "170", so the page text is the same. `HIT_WINDOW` stays, because other page code still reads it.

About ADR 0002 and pinned fragments: only the two per-page script strings `kDataJs` and `kPageJs` in report.cpp change. The shared fragments in app/html_page (the `html::kSortable*` pieces that `page_template` concatenates) do not change. ADR 0002 pinned the page to hydra_report.py's PAGE, but that script is no longer in the repo, and no C++ test compares page bytes. test_report.cpp checks substrings only, and the substrings it checks today stay true.

- [ ] **Step 5: Run it and watch it pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="beyond_edge_ms*,report*,generate_report*"`. Expected: all pass.

- [ ] **Step 6: Name the report's path-count constants**

In src/app/report.h, inside `namespace hydra::app::report` and above the options struct, add:

```cpp
// The path report's default: the top 5 paths per chart.
inline constexpr int64_t kDefaultReportPaths = 5;
// "--all-paths" asks for this many, which no chart reaches.
inline constexpr int64_t kEveryPathSentinel = 1000000000;
// A max_paths above this is labeled "every path" in the subtitle.
inline constexpr int64_t kEveryPathLabelThreshold = 100000000;
```

The options struct's `int64_t max_paths = 5;` becomes `int64_t max_paths = kDefaultReportPaths;`.

In src/app/report.cpp, `options.max_paths > 100000000` becomes `options.max_paths > kEveryPathLabelThreshold`.

In src/cli/report.cpp, `int64_t max_paths = 5;` becomes `int64_t max_paths = hydra::app::report::kDefaultReportPaths;`. `if (arg == "--all-paths") max_paths = 1000000000;` becomes `if (arg == "--all-paths") max_paths = hydra::app::report::kEveryPathSentinel;`. If the file already has a `using namespace` for report, drop the prefix. Task 1 added `--rules` to this argument loop; leave that branch alone.

In src/ui/library_jobs.cpp, `options.max_paths = 5;` becomes `options.max_paths = app::report::kDefaultReportPaths;`.

In tests/test_report.cpp, "generate_report: one seam frames the page for every entry point" sets `options.max_paths = 5` and later `options.max_paths = 1000000000`. Change them to `report::kDefaultReportPaths` and `report::kEveryPathSentinel`. Do the same for the other `1000000000` in that file. The expected strings "songs — top 5 paths per chart" and "songs — every path" stay literal. They are the user-visible text being pinned.

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="generate_report*"`. Expected: passes.

- [ ] **Step 7: Write the failing difficulty-name and message tests**

Add `#include "parse/song.h"` and `#include <optional>` to tests/test_model.cpp's includes. Then add:

```cpp
TEST_CASE("difficulty names: one list, one spelling") {
    // The dropdown indexes this list by the enum's value, so order matters.
    REQUIRE(std::size(kAllDifficulties) == 4);
    for (size_t i = 0; i < std::size(kAllDifficulties); ++i)
        CHECK(static_cast<size_t>(kAllDifficulties[i]) == i);
    CHECK(std::string(difficulty_name(kAllDifficulties[0])) == "Expert");
    CHECK(std::string(difficulty_name(kAllDifficulties[3])) == "Easy");
}

TEST_CASE("difficulty_from_name: any case, nothing else") {
    CHECK(difficulty_from_name("Hard") == Difficulty::Hard);
    CHECK(difficulty_from_name("medium") == Difficulty::Medium);
    CHECK(difficulty_from_name("EASY") == Difficulty::Easy);
    CHECK(difficulty_from_name("eXpErT") == Difficulty::Expert);
    CHECK_FALSE(difficulty_from_name("Legendary").has_value());
    CHECK_FALSE(difficulty_from_name("Har").has_value());
    CHECK_FALSE(difficulty_from_name("").has_value());
}

TEST_CASE("no_notes_message names the difficulty and the drum mode") {
    CHECK(no_notes_message(Difficulty::Hard, true) == "No Hard Pro Drums notes in this chart.");
    CHECK(no_notes_message(Difficulty::Expert, false) == "No Expert Drums notes in this chart.");
}
```

Add to tests/test_config.cpp, after "view_difficulty round-trips, and a junk value normalizes to Expert":

```cpp
TEST_CASE("view_difficulty matches any case and loads as the real name") {
    const std::string path = temp_ini("difficulty_case");
    for (const char* word : {"hard", "HARD", "hArD"}) {
        CAPTURE(word);
        {
            std::ofstream f(path, std::ios::trunc);
            f << "view_difficulty=" << word << "\n";
        }
        Settings r = Settings::load_file(path);
        // What the app carries, and bakes into the store key, is the real name.
        CHECK(r.view_difficulty == "Hard");
        CHECK(r.difficulty() == hydra::Difficulty::Hard);
        CHECK(r.chartmode_key() == "Hard Pro Drums, 1x Bass");
    }
    std::remove(path.c_str());

    // The in-memory lookup matches any case too.
    Settings s;
    s.view_difficulty = "easy";
    CHECK(s.difficulty() == hydra::Difficulty::Easy);
}
```

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build fails with `kAllDifficulties`, `difficulty_from_name` and `no_notes_message` undeclared.

- [ ] **Step 8: One difficulty list, one name lookup, one message builder**

In src/parse/song.h, add `#include <string_view>` to the includes. After `const char* difficulty_name(Difficulty difficulty);`, add:

```cpp
// Every difficulty, in enum order. UI lists and name lookups walk this
// instead of keeping their own copy of the four names.
inline constexpr Difficulty kAllDifficulties[] = {Difficulty::Expert, Difficulty::Hard,
                                                  Difficulty::Medium, Difficulty::Easy};

// The difficulty whose name matches `name` in any case ("hard", "HARD" and
// "Hard" all give Hard). nullopt for anything else. The settings INI and
// hydra_replay's --difficulty both read names through this.
std::optional<Difficulty> difficulty_from_name(std::string_view name);

// "No Hard Pro Drums notes in this chart." The one wording for a chart that
// lacks the requested difficulty, used by analysis and the Preview loader.
std::string no_notes_message(Difficulty difficulty, bool prodrums);
```

In src/parse/song.cpp, add `#include <cctype>` if it is missing. After `difficulty_name`, add:

```cpp
std::optional<Difficulty> difficulty_from_name(std::string_view name) {
    for (Difficulty d : kAllDifficulties) {
        const std::string_view want = difficulty_name(d);
        if (name.size() != want.size()) continue;
        if (std::equal(name.begin(), name.end(), want.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            }))
            return d;
    }
    return std::nullopt;
}

std::string no_notes_message(Difficulty difficulty, bool prodrums) {
    return std::string("No ") + difficulty_name(difficulty) +
           (prodrums ? " Pro Drums" : " Drums") + " notes in this chart.";
}
```

Add `#include <algorithm>` to song.cpp for `std::equal` if it is missing.

In src/app/config.cpp, `Settings::difficulty()` reads:

```cpp
    if (view_difficulty == "Hard") return Difficulty::Hard;
    if (view_difficulty == "Medium") return Difficulty::Medium;
    if (view_difficulty == "Easy") return Difficulty::Easy;
    return Difficulty::Expert;
```

Replace it with:

```cpp
    return difficulty_from_name(view_difficulty).value_or(Difficulty::Expert);
```

`load_file` already ends with `s.view_difficulty = difficulty_name(s.difficulty());`. So a lowercase word in the INI loads as the real name, and `chartmode_key()` stays one of the four keys already in the store. A junk word still reads as Expert, as before. Update the comment above `Settings::difficulty()` in src/app/config.h to say the match ignores case.

In tools/replay.cpp, `settings_from` reads:

```cpp
    std::string diff = a.difficulty;
    for (char& c : diff) c = static_cast<char>(std::tolower((unsigned char)c));
    if (diff == "hard") s.view_difficulty = "Hard";
    else if (diff == "medium") s.view_difficulty = "Medium";
    else if (diff == "easy") s.view_difficulty = "Easy";
    else s.view_difficulty = "Expert";
```

Replace it with:

```cpp
    s.view_difficulty =
        difficulty_name(difficulty_from_name(a.difficulty).value_or(Difficulty::Expert));
```

Task 1 added rules loading to this tool and may have edited nearby lines. If an earlier task already changed these lines, apply the same change to the new lines. Behavior is unchanged: the tool already ignored case, and anything unknown still means Expert.

In src/app/analysis.cpp, the missing-difficulty throw reads:

```cpp
        throw ChartFileError(std::string("No ") + difficulty_name(settings.difficulty) +
                             (settings.prodrums ? " Pro Drums" : " Drums") +
                             " notes in this chart.");
```

Replace it with `throw ChartFileError(no_notes_message(settings.difficulty, settings.prodrums));`.

In src/ui/preview_load_job.cpp, the same string is built from `difficulty_` and `pro_`. Replace that argument with `no_notes_message(difficulty_, pro_)`. The file already reaches parse/song.h, because it calls `difficulty_name`.

In src/ui/library_view.cpp, the dropdown reads:

```cpp
const char* kDifficulties[] = {"Expert", "Hard", "Medium", "Easy"};
int difficulty_idx = static_cast<int>(app.settings.difficulty());
if (ImGui::Combo("##difficulty", &difficulty_idx, kDifficulties, IM_ARRAYSIZE(kDifficulties))) {
    app.settings.view_difficulty = kDifficulties[difficulty_idx];
    app.commit_settings();
}
```

Replace it with:

```cpp
const char* difficulty_names[std::size(kAllDifficulties)];
for (size_t i = 0; i < std::size(kAllDifficulties); ++i)
    difficulty_names[i] = difficulty_name(kAllDifficulties[i]);
int difficulty_idx = static_cast<int>(app.settings.difficulty());
if (ImGui::Combo("##difficulty", &difficulty_idx, difficulty_names,
                 IM_ARRAYSIZE(difficulty_names))) {
    app.settings.view_difficulty = difficulty_names[difficulty_idx];
    app.commit_settings();
}
```

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="difficulty*,no_notes_message*,view_difficulty*,chartmode*"`. Expected: all pass.

- [ ] **Step 9: Write the failing lens test**

Add to tests/test_analysis.cpp (it already includes store/record_store.h):

```cpp
TEST_CASE("run_batch files results under the lens it is given") {
    std::string chart;
    for (const std::string& p : corpus::chart_paths()) {
        if (!hydra::load_songpath(p, true, true).is_empty()) { chart = p; break; }
    }
    REQUIRE(!chart.empty());

    AnalysisSettings settings;
    settings.depth_mode = hydra::DepthMode::Scores;
    settings.depth_value = 10;
    settings.ms_filter = 10.0;
    const hydra::store::Lens lens =
        hydra::store::Lens::from(std::optional<int>(10), 0, 10);

    ScanItem item;
    item.md5 = hash_chart_file(chart);
    item.title = "t";
    item.notespath = chart;
    hydra::store::RecordStore store(":memory:");
    run_batch({item}, "lens-test", lens, settings, store, /*redo=*/false, 1);

    const hydra::store::CapQuery cap = hydra::store::CapQuery::from_setting(settings.sp_cap);
    CHECK(store.has_record(hydra::store::RecordKey{item.md5, "lens-test", cap, lens}));
}
```

If Task 1 made `RecordStore` take the rules fingerprint when it opens, open this store the same way the other test_analysis cases do after Task 1.

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build fails because `run_batch` takes no lens argument.

- [ ] **Step 10: Pass the lens in and delete lens_from**

In src/app/analysis.h, delete `lens_from` and its comment. Change the `run_batch` declaration to take the lens after `chartmode`:

```cpp
void run_batch(const std::vector<ScanItem>& items, const std::string& chartmode,
               const store::Lens& lens,
               const AnalysisSettings& settings, store::RecordStore& store, bool redo,
               int worker_count,
```

Every parameter after `worker_count` stays as it is. Add one line to the comment above it: "`lens` is the store key the caller's settings file results under: pass `Settings::lens()`."

In src/app/analysis.cpp, delete `lens_from`. Give the `run_batch` definition the same new parameter. Delete the line `const store::Lens lens = lens_from(settings);`. The parameter now supplies `lens` to the two places that read it.

In src/cli/batch.cpp, the call reads `scanitems, chartmode, analysis, store, redo,`. Change it to `scanitems, chartmode, settings.lens(), analysis, store, redo,`.

In src/ui/library_jobs.h, the `BatchJob` constructor gains a `store::Lens lens` parameter after `chartmode`. The class gains a `store::Lens lens_;` member after `chartmode_`. In src/ui/library_jobs.cpp, the constructor takes and stores it (`lens_(std::move(lens)),`). The `run_batch` call becomes `items_, chartmode_, lens_, settings_, store_, redo_, workers_,`.

In src/ui/app_state.cpp, the job is built like this:

```cpp
    batch_job = std::make_unique<BatchJob>(search_opt, settings.chartmode_key(),
                                           settings.to_analysis_settings(), *store, redo);
```

Change it to:

```cpp
    batch_job = std::make_unique<BatchJob>(search_opt, settings.chartmode_key(),
                                           settings.lens(),
                                           settings.to_analysis_settings(), *store, redo);
```

If an earlier task already changed these lines (Task 1 gates Analyze on the rules file here), apply the same change to the new lines.

In tests/test_config.cpp, delete the case "lens_from(AnalysisSettings) agrees with Settings::lens()". The agreement it pinned no longer needs pinning, because there is now only one lens computation.

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="run_batch*"`. Expected: passes. Then search src/ and tests/ for `lens_from`. Expected: nothing.

- [ ] **Step 11: Write the failing "(unknown)" tests for the scan**

Add to tests/test_model.cpp:

```cpp
TEST_CASE("title_or_unknown: one fallback for a song with no usable name") {
    CHECK(std::string(kUnknownTitle) == "(unknown)");
    CHECK(title_or_unknown("") == "(unknown)");
    // What the metadata readers wrote before this fallback existed.
    CHECK(title_or_unknown("<unknown title>") == "(unknown)");
    CHECK(title_or_unknown("Some Song") == "Some Song");
}
```

In tests/test_analysis.cpp, add `#include <cstdint>`, `#include <filesystem>` and `#include <utility>` to the includes. Add this helper in an anonymous namespace above the first case that uses it. It writes the smallest .sng the scan reads: the "SNGPKG" identifier padded to the 34-byte header, then the metadata block (a u64 count, then u32-length key and value pairs).

```cpp
namespace {

void write_sng_with_metadata(
    const std::filesystem::path& path,
    const std::vector<std::pair<std::string, std::string>>& metadata) {
    std::ofstream f(path, std::ios::binary);
    std::string header = "SNGPKG";
    header.resize(34, '\0');
    f.write(header.data(), static_cast<std::streamsize>(header.size()));
    auto put_le = [&f](uint64_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xFF));
    };
    put_le(metadata.size(), 8);
    for (const auto& [key, value] : metadata) {
        put_le(key.size(), 4);
        f.write(key.data(), static_cast<std::streamsize>(key.size()));
        put_le(value.size(), 4);
        f.write(value.data(), static_cast<std::streamsize>(value.size()));
    }
}

}  // namespace
```

Then add the two cases:

```cpp
TEST_CASE("discover_charts: a song with no usable name reads (unknown)") {
    namespace fs = std::filesystem;
    const std::string chart = corpus::first_chart_with_suffix(".chart");
    REQUIRE(!chart.empty());

    const fs::path root = fs::temp_directory_path() /
        ("hydra_unknown_title_" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(root);

    // An empty `name =` line.
    fs::create_directories(root / "empty_name");
    fs::copy_file(fs::u8path(chart), root / "empty_name" / "notes.chart");
    {
        std::ofstream ini(root / "empty_name" / "song.ini", std::ios::binary);
        ini << "[song]\nname =\nartist = Someone\n";
    }
    // No name line at all.
    fs::create_directories(root / "no_name");
    fs::copy_file(fs::u8path(chart), root / "no_name" / "notes.chart");
    {
        std::ofstream ini(root / "no_name" / "song.ini", std::ios::binary);
        ini << "[song]\nartist = Someone\n";
    }
    // A .sng whose embedded name is empty.
    write_sng_with_metadata(root / "blank.sng",
                            {{"name", ""}, {"artist", "Someone"}, {"charter", "C"}});

    auto [items, errors] = discover_charts({root.u8string()});
    fs::remove_all(root);
    CHECK(errors.empty());
    REQUIRE(items.size() == 3);
    for (const ScanItem& it : items) {
        CAPTURE(it.notespath);
        CHECK(it.title == hydra::kUnknownTitle);
        CHECK(it.artist == "Someone");
    }
}

TEST_CASE("rescan cache: an old placeholder or blank title reads (unknown)") {
    const std::string input = HYDRA_INPUT_DIR;
    auto [items, errors] = discover_charts({input});
    REQUIRE(!items.empty());

    // Library rows written before the fallback existed: a blank title from an
    // empty `name =`, or the old readers' "<unknown title>".
    hydra::store::RecordStore store(":memory:");
    std::vector<hydra::store::ChartLibraryEntry> entries;
    for (size_t i = 0; i < items.size(); ++i) {
        const ScanItem& it = items[i];
        entries.push_back({it.md5, i % 2 ? "<unknown title>" : "", it.artist, it.charter,
                           it.notespath, it.rootfolder, it.sig});
    }
    store.rebuild_chart_library(entries);
    hydra::store::ChartLibraryCache cache = store.chart_library_cache();

    auto [items2, errors2] = discover_charts({input}, ScanCallbacks{}, &cache);
    REQUIRE(items2.size() == items.size());
    for (const ScanItem& it : items2) CHECK(it.title == hydra::kUnknownTitle);
}
```

Add to the end of tests/test_srb.cpp. It uses its own subfolder, so "srb: discovery surfaces the embedded metadata" still sees exactly one item in `scan`:

```cpp
TEST_CASE("srb: an empty embedded name reads (unknown)") {
    std::string dir = fixture_dir() + "\\scan_blank";
    CreateDirectoryW(utf8_to_wide(dir).c_str(), nullptr);

    std::vector<uint8_t> notes = read_bytes(corpus_chart_path(".chart"));
    write_bytes(dir + "\\blank.srb",
                make_srb(make_metadata("notes.chart", "", "Scanned Artist",
                                       "Scanned Charter"),
                         notes));

    auto [items, errors] = hydra::app::discover_charts({dir});
    REQUIRE(errors.empty());
    REQUIRE(items.size() == 1);
    CHECK(items[0].title == kUnknownTitle);
    CHECK(items[0].artist == "Scanned Artist");
}
```

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build fails with `kUnknownTitle` and `title_or_unknown` undeclared.

- [ ] **Step 12: Write the failing "(unknown)" tests for the three reports**

Old songmeta rows keep their stored name, because `add_song` uses `INSERT OR IGNORE` (src/store/record_store.cpp). So each report must map a blank or old-placeholder name when it reads it. These three cases store such a name first and check the row.

Add to tests/test_report.cpp, after "report lists only the wanted cap and names it". It files records with the same key shape `fill_store` uses; if an earlier task changed that shape, use the new one here too.

```cpp
TEST_CASE("collect_rows: a blank or old-placeholder song name reads (unknown)") {
    store::RecordStore store(":memory:");
    AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 10;
    settings.sp_cap = 4;
    settings.time_budget_s = std::nullopt;

    // songmeta names written before the fallback existed.
    const std::vector<std::string> stored_names = {"", "<unknown title>"};
    size_t added = 0;
    for (const std::string& path : corpus::chart_paths()) {
        if (added == stored_names.size()) break;
        try {
            AnalysisResult result = analyze_chart_file(path, settings);
            if (result.song.is_empty() || result.record.paths.empty()) continue;
            const std::string hyhash = "u" + std::to_string(added);
            store.add_song(hyhash, stored_names[added], "Artist", "Charter", result.song);
            store.add_record(
                store::RecordKey{hyhash, "mode", store::CapQuery::from_setting(settings.sp_cap)},
                result.record);
            ++added;
        } catch (const std::exception&) {
            continue;
        }
    }
    REQUIRE(added == stored_names.size());

    std::vector<report::ReportRow> rows =
        report::collect_rows(store, /*max_paths=*/100, store::CapQuery::at(4), store::Lens{});
    REQUIRE(!rows.empty());
    for (const report::ReportRow& row : rows) CHECK(row.song == kUnknownTitle);
}
```

Add `#include "parse/song.h"` to tests/test_fill_report.cpp, then add:

```cpp
TEST_CASE("collect_fill_rows: a blank stored song name reads (unknown)") {
    store::RecordStore old_store(":memory:");
    store::RecordStore new_store(":memory:");

    // A blank songmeta name from before the fallback. add_song keeps the first
    // name it sees, so put()'s own "Song aa11" does not replace it.
    old_store.add_song(kBoth, "", "Test Artist", "Test Charter", sample_chart().song);
    new_store.add_song(kBoth, "", "Test Artist", "Test Charter", sample_chart().song);
    put(old_store, kBoth, 1000000, 3, "old-path");
    put(new_store, kBoth, 1000000, 3, "new-path");

    std::vector<FillCompareRow> rows = compare(old_store, new_store);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].song == kUnknownTitle);
}
```

In tests/test_dm_report.cpp, add `#include "parse/song.h"`. Give `fill_store` a name parameter with today's value as the default, so the existing calls do not change:

```cpp
int64_t fill_store(store::RecordStore& store, const std::string& name = "Stored Title") {
```

Inside it, `store.add_song(kHash, "Stored Title", "Stored Artist",` becomes `store.add_song(kHash, name, "Stored Artist",`. Then add:

```cpp
TEST_CASE("collect_dm_rows: a blank stored song name reads (unknown)") {
    store::RecordStore store(":memory:");
    const int64_t optimal = fill_store(store, "");

    // The leaderboard has no metadata for it, so the row falls back to the
    // matched record, whose stored name is blank.
    net::DmScore unknown_meta = make_score(kHash, optimal - 10);
    unknown_meta.known = false;

    std::vector<DmReportRow> rows = app::dm_report::collect_dm_rows(
        store, {unknown_meta}, kMode, store::Lens{});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].song == kUnknownTitle);
}
```

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build still fails on `kUnknownTitle`, as in Step 11.

- [ ] **Step 13: One fallback for a song with no usable name**

In src/parse/song.h, next to `no_notes_message`, add:

```cpp
// What a song with no usable name is called everywhere it is shown.
inline constexpr const char* kUnknownTitle = "(unknown)";

// The one fallback for a song's name. A blank name (an empty `name =`, a
// missing name key, an empty .sng/.srb name) and the "<unknown title>" the
// metadata readers wrote before this existed both become kUnknownTitle.
// Library rows and songmeta rows from older scans still hold those, so every
// place that shows a stored name reads it through this.
std::string title_or_unknown(std::string title);
```

In src/parse/song.cpp, after `no_notes_message`, add:

```cpp
std::string title_or_unknown(std::string title) {
    // The placeholder the metadata readers used before kUnknownTitle.
    static constexpr const char* kOldPlaceholder = "<unknown title>";
    if (title.empty() || title == kOldPlaceholder) return kUnknownTitle;
    return title;
}
```

In src/app/analysis.cpp, the three metadata readers (`read_metadata_ini`, `parse_sng_metadata` and `parse_srb_metadata`) each start with:

```cpp
    std::string title = "<unknown title>";
    std::string artist = "<unknown artist>";
    std::string charter = "<unknown charter>";
```

In all three, change only the first line, so the block reads:

```cpp
    // Empty = no usable name; discover_charts applies the one fallback.
    std::string title;
    std::string artist = "<unknown artist>";
    std::string charter = "<unknown charter>";
```

Artist and charter keep their defaults; decision 10 is about the song name only. In the comment above `parse_sng_metadata`, "missing keys keep their <unknown> defaults" becomes "a missing name stays empty and the other keys keep their <unknown> defaults".

Then the scan collects its results like this:

```cpp
    std::vector<ScanItem> scanitems;
    scanitems.reserve(results.size());
    for (std::optional<ScanItem>& r : results)
        if (r) scanitems.push_back(std::move(*r));
```

Replace it with:

```cpp
    std::vector<ScanItem> scanitems;
    scanitems.reserve(results.size());
    for (std::optional<ScanItem>& r : results) {
        if (!r) continue;
        // The one fallback, whichever source produced the title: a fresh
        // song.ini, .sng or .srb read, or the rescan cache holding an older
        // scan's blank or "<unknown title>".
        r->title = title_or_unknown(std::move(r->title));
        scanitems.push_back(std::move(*r));
    }
```

Every scanned title passes through this loop: fresh song.ini reads, .sng/.srb reads and cache hits. The library table is rebuilt from these items on every scan, so old rows read "(unknown)" after the next scan. New songmeta rows get "(unknown)" too, because `add_song` in `run_batch` is written from `item.title`.

Now the three report writers that read songmeta names.

In src/app/report.cpp, these two lines:

```cpp
            row.song = plain(meta.ref_name);
            if (row.song.empty()) row.song = "(unknown)";
```

become one:

```cpp
            row.song = title_or_unknown(plain(meta.ref_name));
```

In src/app/fill_report.cpp, `row.song = report::plain(id->ref_name);` becomes `row.song = title_or_unknown(report::plain(id->ref_name));`.

In src/app/dm_report.cpp, only the matched-record branch changes. `row.song = report::plain(rec->ref_name);` becomes `row.song = title_or_unknown(report::plain(rec->ref_name));`. The leaderboard's own names and its "Unknown Song: <hash>" placeholder are not Hydra's metadata, so they stay as they are.

Add `#include "parse/song.h"` to report.cpp, fill_report.cpp and dm_report.cpp if it is not already reached through their includes.

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="title_or_unknown*,discover_charts*,rescan cache*,srb*,collect_rows*,collect_fill_rows*,collect_dm_rows*"`. Expected: all pass. "discover_charts output matches the checked-in scan snapshot" still passes, because testdata/scan_snapshot.json has no blank or placeholder titles. Then search src/ for `<unknown title>`. Expected: only the `kOldPlaceholder` line in src/parse/song.cpp.

- [ ] **Step 14: Write the failing replay-field test**

Add to tests/test_replay.cpp:

```cpp
TEST_CASE("replay score fields: one list in schema order") {
    REQUIRE(std::size(kReplayScoreFields) == 6);
    const char* want[] = {"base", "combo", "sp", "solo", "accent", "ghost"};
    for (size_t i = 0; i < 6; ++i) CHECK(std::string(kReplayScoreFields[i].name) == want[i]);

    ReplayScore s;
    s.base = 1; s.combo = 2; s.sp = 3; s.solo = 4; s.accent = 5; s.ghost = 6;
    for (size_t i = 0; i < 6; ++i)
        CHECK(s.*(kReplayScoreFields[i].member) == static_cast<int64_t>(i + 1));
}
```

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: the build fails with `kReplayScoreFields` undeclared.

- [ ] **Step 15: One list of score fields in core/replay.h**

In src/core/replay.h, after `struct ReplayScore`, add:

```cpp
// The six score categories, in the order hydra_replay's JSON and its check
// output print them. One list, so the names and the fields cannot drift.
struct ReplayScoreField {
    const char* name;
    int64_t ReplayScore::*member;
};
inline constexpr ReplayScoreField kReplayScoreFields[] = {
    {"base", &ReplayScore::base},     {"combo", &ReplayScore::combo},
    {"sp", &ReplayScore::sp},         {"solo", &ReplayScore::solo},
    {"accent", &ReplayScore::accent}, {"ghost", &ReplayScore::ghost},
};
```

In tools/replay.cpp, `score_json` reads:

```cpp
json score_json(const ReplayScore& s) {
    return json{{"base", s.base}, {"combo", s.combo}, {"sp", s.sp},
                {"solo", s.solo}, {"accent", s.accent}, {"ghost", s.ghost}};
}
```

Replace it with:

```cpp
json score_json(const ReplayScore& s) {
    json j = json::object();
    for (const ReplayScoreField& f : kReplayScoreFields) j[f.name] = s.*(f.member);
    return j;
}
```

`paths_json` builds the same six keys by hand from the Path's `score_*` fields. Replace that `json{{"base", p->score_base}, ... {"ghost", p->score_ghosts}}` object with `score_json(score_of(*p))`. `score_of` in src/core/replay.cpp maps those six Path fields one to one. Task 5 added a `sqout_tick` key to this function; leave it alone.

Delete `kFieldNames`. In `check_chart`, the field comparison reads:

```cpp
            const int64_t got[6] = {r.final.base, r.final.combo, r.final.sp,
                                    r.final.solo, r.final.accent, r.final.ghost};
            const int64_t want_fields[6] = {want.base,   want.combo, want.sp,
                                            want.solo, want.accent, want.ghost};
            for (int f = 0; f < 6; ++f) {
                if (got[f] == want_fields[f]) continue;
                if (!diffs.empty()) diffs += ", ";
                diffs += std::string(kFieldNames[f]) + " " +
                         std::to_string(got[f]) + " vs " +
                         std::to_string(want_fields[f]) + " (" +
                         std::to_string(got[f] - want_fields[f]) + ")";
            }
```

Replace it with:

```cpp
            for (const ReplayScoreField& f : kReplayScoreFields) {
                const int64_t got = r.final.*(f.member);
                const int64_t wanted = want.*(f.member);
                if (got == wanted) continue;
                if (!diffs.empty()) diffs += ", ";
                diffs += std::string(f.name) + " " + std::to_string(got) + " vs " +
                         std::to_string(wanted) + " (" + std::to_string(got - wanted) + ")";
            }
```

The JSON bytes do not change. `json` here is `nlohmann::json`, which stores object keys sorted by name, so the key order was never the order they were written in.

- [ ] **Step 16: Run it and watch it pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="replay*"`. Expected: all pass.

- [ ] **Step 17: Run the full suite**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `[doctest] Status: SUCCESS!`.

- [ ] **Step 18: Check the difficulty dropdown in the GUI**

Build `.\build_cpp.ps1 -Target hydra_uitest` and write `$env:TEMP\hydra_task11.txt`:

```
wait-idle
click ##difficulty
expect-text Expert
expect-text Hard
expect-text Medium
expect-text Easy
```

Run `.\build-cpp\Release\hydra_uitest.exe --script $env:TEMP\hydra_task11.txt`. Expected: it passes, so the dropdown still lists the four names.

- [ ] **Step 19: Prove hydra_replay's output did not move**

```powershell
.\build_cpp.ps1 -Target hydra_replay
$out = "$env:TEMP\hydra_task11"
$chart = (Get-ChildItem testdata\input -Recurse -Filter notes.chart | Sort-Object FullName | Select-Object -First 1).FullName
Remove-Item "$out\after.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_replay.exe dump --chart $chart --db "$out\after.db" --out "$out\replay_after.json"
Compare-Object (Get-Content "$out\replay_before.json") (Get-Content "$out\replay_after.json")
```

Expected: `Compare-Object` prints nothing.

- [ ] **Step 20: Commit**

```powershell
git add src/core/squeeze_rating.h src/core/squeeze_rating.cpp src/app/report.h src/app/report.cpp src/cli/report.cpp src/ui/library_jobs.h src/ui/library_jobs.cpp src/ui/library_view.cpp src/ui/preview_load_job.cpp src/ui/app_state.cpp src/parse/song.h src/parse/song.cpp src/app/config.h src/app/config.cpp src/app/analysis.h src/app/analysis.cpp src/cli/batch.cpp src/app/fill_report.cpp src/app/dm_report.cpp src/core/replay.h tools/replay.cpp tests/test_squeeze_rating.cpp tests/test_report.cpp tests/test_model.cpp tests/test_config.cpp tests/test_analysis.cpp tests/test_srb.cpp tests/test_fill_report.cpp tests/test_dm_report.cpp tests/test_replay.cpp
git commit -m "Give each report and library fact one owner

The report's Beyond edge is read from timing_tiers (beyond_edge_ms) in the
footer and the page script. The every-path sentinel, its label threshold and
the default 5 paths are named in report.h. The difficulty dropdown, the
settings lookup and hydra_replay walk kAllDifficulties through
difficulty_from_name. no_notes_message builds the missing-difficulty error.
run_batch takes the caller's lens, so lens_from is gone. hydra_replay's six
score fields are one list.

Visible changes (user decisions 10, 22 and 27): a song with no usable name
(empty name =, no name key, empty .sng/.srb name) reads (unknown) in the
library, Song Details and all three reports, through one fallback,
title_or_unknown. Titles that read <unknown title> before now read
(unknown); library rows pick it up on the next scan, reports at once. A
lowercase view_difficulty in the settings INI now selects that difficulty
instead of Expert.

Task: Report and library facts get one owner each
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

---

### Task 12: Parser single owners

Four parser rules are each written in more than one place today, and the copies have drifted. The .sng layout is read three times: the note loader in song.cpp, the audio extractor in preview_source.cpp and the metadata reader in analysis.cpp. Only the audio extractor checks bounds, so a truncated .sng can make the note loader read past the end of its buffer. The ".srb streams stop at 1 GB" rule is a bare `size_t{1} << 30` in three places. The chart-file names are matched three ways: the library scan wants exact lower case, while the .sng loader and `load_songpath` ignore case. So a loose folder holding `Notes.mid` and `Song.ini` never shows up in the library, though the parser would read it fine. The MIDI and .chart parsers carry identical copies of the time-signature, fill-end and SP-end handlers. The MIDI SP-end copy reads its start tick without checking that one exists, so a stray SP note-off reads a stale or empty value. Finally, `parse_track` in midi.cpp decodes delta times with its own loop instead of `read_varlen`, and the two loops disagree on malformed input. This task gives each rule one owner. The chart-file-name owner is the new src/parse/chart_files.h; its `is_song_ini` is the one test for "is this song.ini" that Task 17 also uses (user decision 8). The MIDI reader keeps following mido on broken files (user decision 27). Delta times already do, and stay exactly as they are. The one place the reader strays from mido today is a meta or sysex length too big to be real. Hydra wraps it to a small number and reads the bytes after it as fresh events. mido refuses any message longer than 1,000,000 bytes and raises an error for the whole file. The new single read does what mido does. What the user sees: loose chart folders with capitalized file names (`Notes.mid`, `NOTES.CHART`, `Song.ini`) now appear in the library. A truncated .sng now fails with a clear error instead of risking a crash. A .mid with an impossible message length now fails to load with mido's error text instead of loading garbage. Nothing else changes.

**Goal:** Every .sng read, the .srb stream cap, the chart-file-name rule, the three shared parser handlers and the MIDI variable-length read each live in one place.

**Files:** Creates src/parse/sng.h, src/parse/sng.cpp, src/parse/chart_files.h, src/parse/chart_files.cpp and tests/test_sng.cpp. Changes src/parse/srb.h (new constant), src/parse/song.cpp (load_songpath, load_songpath_sng, load_songpath_srb and the shared handlers), src/parse/midi.cpp (read_varlen and parse_track), src/app/preview_source.cpp (extract_sng_audio and extract_srb_audio), src/app/analysis.cpp (parse_sng_metadata and the discover_charts loop) and CMakeLists.txt. Adds cases to tests/test_srb.cpp, tests/test_analysis.cpp, tests/test_song.cpp and tests/test_midi.cpp.

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="discover_charts finds a folder whose notes and ini names are capitalized"` passes.
- [ ] `hydra_tests.exe -tc="chart_files:*"` passes: `notes_file_format("NOTES.MID") == ChartFormat::Mid`, `chart_format_of("a\\b.SNG") == ChartFormat::Sng`, `is_song_ini("Song.INI")` is true.
- [ ] `hydra_tests.exe -tc="sng:*"` passes, including the truncated-file and overflowing-offset cases.
- [ ] `hydra_tests.exe -tc="mid: a stray SP note-off flags nothing"` passes.
- [ ] `hydra_tests.exe -tc="midi: a message longer than mido's 1,000,000-byte cap refuses the file, as mido does"` passes, and so does the existing corpus smoke case "midi: every corpus .mid reads with a sane structure".
- [ ] `size_t{1} << 30` no longer appears in src/parse/song.cpp or src/app/preview_source.cpp. `kSrbMaxStream` is used in all three places.
- [ ] The full `hydra_tests.exe` run passes with no new failures.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe` -> `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test for the .srb stream cap constant**

Add to the end of tests/test_srb.cpp:

```cpp
TEST_CASE("srb: one named cap bounds every inflated stream") {
    // Notes files inflate to well under 100 MB even for mega-charts; the cap
    // exists only to bound hostile input.
    CHECK(kSrbMaxStream == (size_t{1} << 30));
    CHECK(kSrbMaxStream > kSrbMaxMetadata);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error, `'kSrbMaxStream': undeclared identifier`.

- [ ] **Step 3: Add the constant and use it in the three places**

In src/parse/srb.h, right after the `kSrbMaxMetadata` line, add:

```cpp
// The cap on any stream after the metadata (the notes file, and the audio or
// art streams that follow it). A notes file inflates to well under a hundred
// MB even for mega-charts; 1 GB exists only to bound hostile input.
constexpr size_t kSrbMaxStream = size_t{1} << 30;
```

In src/parse/song.cpp, load_songpath_srb, replace:

```cpp
    // The notes file inflates to well under a hundred MB even for mega-charts;
    // a 1 GB ceiling only exists to bound hostile input.
    std::vector<uint8_t> notebytes = srb_inflate_stream(
        buf.data(), buf.size(), notes_offset, size_t{1} << 30, nullptr);
```

with:

```cpp
    std::vector<uint8_t> notebytes = srb_inflate_stream(
        buf.data(), buf.size(), notes_offset, kSrbMaxStream, nullptr);
```

In src/app/preview_source.cpp, extract_srb_audio, replace `size_t{1} << 30, &offset);  // stream 2: notes` with `kSrbMaxStream, &offset);  // stream 2: notes`, and replace `buf.data(), buf.size(), offset, size_t{1} << 30, &next);` with `buf.data(), buf.size(), offset, kSrbMaxStream, &next);`.

- [ ] **Step 4: Run it and watch it pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="srb:*"`. Expected: every srb case passes.

- [ ] **Step 5: Commit**

```
git add src/parse/srb.h src/parse/song.cpp src/app/preview_source.cpp tests/test_srb.cpp
git commit -m "Name the .srb stream cap once" -m "Task: Parser single owners
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Write the failing tests for the chart-file-name helper and the scan**

Create the helper's tests at the end of tests/test_song.cpp (add `#include "parse/chart_files.h"` to its includes):

```cpp
TEST_CASE("chart_files: loose-folder notes names match in any case") {
    CHECK(notes_file_format("notes.mid") == ChartFormat::Mid);
    CHECK(notes_file_format("NOTES.MID") == ChartFormat::Mid);
    CHECK(notes_file_format("Notes.Chart") == ChartFormat::Chart);
    CHECK(notes_file_format("notes.sng") == ChartFormat::None);
    CHECK(notes_file_format("mynotes.mid") == ChartFormat::None);
    CHECK(is_song_ini("song.ini"));
    CHECK(is_song_ini("Song.INI"));
    CHECK_FALSE(is_song_ini("song.ini.bak"));
}

TEST_CASE("chart_files: a path's format comes from its extension in any case") {
    CHECK(chart_format_of("C:\\songs\\a\\notes.mid") == ChartFormat::Mid);
    CHECK(chart_format_of("x.CHART") == ChartFormat::Chart);
    CHECK(chart_format_of("C:\\songs\\bundle.SNG") == ChartFormat::Sng);
    CHECK(chart_format_of("pack.Srb") == ChartFormat::Srb);
    CHECK(chart_format_of("notes.txt") == ChartFormat::None);
    CHECK(chart_format_of("mid") == ChartFormat::None);
}
```

Add to tests/test_analysis.cpp (add `#include "midi_util.h"` to its includes):

```cpp
namespace {

// A fresh folder under %TEMP% for one scan fixture.
std::string scan_fixture_dir(const char* name) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::string dir = hydra::wide_to_utf8(tmp) + "hydra_scan_case_" +
                      std::to_string(GetCurrentProcessId()) + "_" + name;
    CreateDirectoryW(hydra::utf8_to_wide(dir).c_str(), nullptr);
    return dir;
}

void write_fixture(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* f = hydra::fopen_utf8(path, L"wb");
    REQUIRE_MESSAGE(f != nullptr, "cannot write " << path);
    if (!bytes.empty()) std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

}  // namespace

TEST_CASE("discover_charts finds a folder whose notes and ini names are capitalized") {
    const std::string dir = scan_fixture_dir("caps");
    write_fixture(dir + "\\Notes.mid",
                  testmidi::smf(testmidi::concat({testmidi::track_name("PART DRUMS"),
                                                  testmidi::set_tempo(),
                                                  testmidi::note_on(96, 100),
                                                  testmidi::end_of_track()})));
    const std::string ini = "[song]\r\nname = Capital Case\r\nartist = Someone\r\n"
                            "charter = Someone Else\r\n";
    write_fixture(dir + "\\Song.ini", std::vector<uint8_t>(ini.begin(), ini.end()));

    auto [items, errors] = discover_charts({dir});
    REQUIRE(errors.empty());
    REQUIRE(items.size() == 1);
    CHECK(items[0].title == "Capital Case");
    CHECK(items[0].notespath == dir + "\\Notes.mid");
}
```

- [ ] **Step 7: Run them and watch them fail**

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error, `cannot open include file 'parse/chart_files.h'`. The scan case also fails once that compiles, because today's loop compares `e.name == "notes.mid"` exactly and finds nothing (`items.size() == 0`).

- [ ] **Step 8: Create the helper**

Create src/parse/chart_files.h:

```cpp
// Which file is a chart, decided once for the library scan and the parser.
//
// Clone Hero reads a loose folder's notes.mid / notes.chart / song.ini in any
// letter case, and so does Hydra: the scan (app/analysis.cpp discover_charts)
// and the loaders (parse/song.cpp) both ask these functions, so the two can
// never disagree about which files count.

#ifndef HYDRA_PARSE_CHART_FILES_H
#define HYDRA_PARSE_CHART_FILES_H

#include <string_view>

namespace hydra {

enum class ChartFormat { None, Mid, Chart, Sng, Srb };

// A path's chart format from its extension, any case: ".mid", ".chart",
// ".sng", ".srb". Anything else is None.
ChartFormat chart_format_of(std::string_view path);

// A loose folder's notes file by its exact name, any case: "notes.mid" is
// Mid, "notes.chart" is Chart, anything else is None.
ChartFormat notes_file_format(std::string_view filename);

// "song.ini", any case.
bool is_song_ini(std::string_view filename);

}  // namespace hydra

#endif  // HYDRA_PARSE_CHART_FILES_H
```

Create src/parse/chart_files.cpp:

```cpp
#include "parse/chart_files.h"

#include <string>

namespace hydra {

namespace {

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool ends_with(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

ChartFormat chart_format_of(std::string_view path) {
    const std::string low = ascii_lower(path);
    if (ends_with(low, ".mid")) return ChartFormat::Mid;
    if (ends_with(low, ".chart")) return ChartFormat::Chart;
    if (ends_with(low, ".sng")) return ChartFormat::Sng;
    if (ends_with(low, ".srb")) return ChartFormat::Srb;
    return ChartFormat::None;
}

ChartFormat notes_file_format(std::string_view filename) {
    const std::string low = ascii_lower(filename);
    if (low == "notes.mid") return ChartFormat::Mid;
    if (low == "notes.chart") return ChartFormat::Chart;
    return ChartFormat::None;
}

bool is_song_ini(std::string_view filename) { return ascii_lower(filename) == "song.ini"; }

}  // namespace hydra
```

In CMakeLists.txt, in the hydra_core source list, add `src/parse/chart_files.cpp` on the line after `src/parse/srb.cpp`.

- [ ] **Step 9: Point the scan and the loaders at the helper**

In src/app/analysis.cpp add `#include "parse/chart_files.h"` after `#include "parse/srb.h"`. In discover_charts replace:

```cpp
                if (e.is_dir) subdirs.push_back(&e);
                else if (e.name == "notes.mid") found_mid = &e;
                else if (e.name == "notes.chart") found_chart = &e;
                else if (e.name == "song.ini") found_ini = &e;
                else if (ends_with_ci(e.name, ".sng"))
                    found_archives.push_back({&e, ChartKind::Sng});
                else if (ends_with_ci(e.name, ".srb"))
                    found_archives.push_back({&e, ChartKind::Srb});
```

with:

```cpp
                if (e.is_dir) subdirs.push_back(&e);
                else if (notes_file_format(e.name) == ChartFormat::Mid) found_mid = &e;
                else if (notes_file_format(e.name) == ChartFormat::Chart) found_chart = &e;
                else if (is_song_ini(e.name)) found_ini = &e;
                else if (chart_format_of(e.name) == ChartFormat::Sng)
                    found_archives.push_back({&e, ChartKind::Sng});
                else if (chart_format_of(e.name) == ChartFormat::Srb)
                    found_archives.push_back({&e, ChartKind::Srb});
```

Task 11 also edits discover_charts (it adds the "(unknown)" title fallback where the scan collects its results). That is a different block from the loop above. If an earlier task already changed these loop lines, apply the same change to the new lines.

In src/parse/song.cpp add `#include "parse/chart_files.h"` after `#include "parse/midi.h"`. Task 1 has already given every loader a trailing `const core::Rules& rules` parameter and passes it to every nested loader call, so load_songpath reads like this when this task starts (today's code is the same without `, rules`). Replace:

```cpp
    std::string low = ascii_casefold(path);
    auto ends_with = [&low](const char* suf) {
        size_t n = std::strlen(suf);
        return low.size() >= n && low.compare(low.size() - n, n, suf) == 0;
    };
    if (ends_with(".mid")) return load_songpath_mid(path, pro, bass2x, difficulty, rules);
    if (ends_with(".chart")) return load_songpath_chart(path, pro, bass2x, difficulty, rules);
    if (ends_with(".sng")) return load_songpath_sng(path, pro, bass2x, difficulty, rules);
    if (ends_with(".srb")) return load_songpath_srb(path, pro, bass2x, difficulty, rules);
    throw std::runtime_error("unexpected chart type: " + path);
```

with:

```cpp
    switch (chart_format_of(path)) {
        case ChartFormat::Mid: return load_songpath_mid(path, pro, bass2x, difficulty, rules);
        case ChartFormat::Chart: return load_songpath_chart(path, pro, bass2x, difficulty, rules);
        case ChartFormat::Sng: return load_songpath_sng(path, pro, bass2x, difficulty, rules);
        case ChartFormat::Srb: return load_songpath_srb(path, pro, bass2x, difficulty, rules);
        case ChartFormat::None: break;
    }
    throw std::runtime_error("unexpected chart type: " + path);
```

If Task 1 named the parameter something other than `rules`, pass its name instead.

In load_songpath_srb replace:

```cpp
    std::string fn = ascii_casefold(md.notes_filename);
    auto fn_ends_with = [&fn](const char* suf) {
        size_t n = std::strlen(suf);
        return fn.size() >= n && fn.compare(fn.size() - n, n, suf) == 0;
    };
    bool is_mid;
    if (fn_ends_with(".mid"))
        is_mid = true;
    else if (fn_ends_with(".chart"))
        is_mid = false;
    else  // Unexpected filename: sniff the payload instead.
        is_mid = notebytes.size() >= 4 && std::memcmp(notebytes.data(), "MThd", 4) == 0;
```

with:

```cpp
    const ChartFormat named = chart_format_of(md.notes_filename);
    bool is_mid;
    if (named == ChartFormat::Mid)
        is_mid = true;
    else if (named == ChartFormat::Chart)
        is_mid = false;
    else  // Unexpected filename: sniff the payload instead.
        is_mid = notebytes.size() >= 4 && std::memcmp(notebytes.data(), "MThd", 4) == 0;
```

The .sng loop moves to the helper in Step 14, together with the new .sng reader.

- [ ] **Step 10: Run them and watch them pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="chart_files:*,discover_charts*,srb:*"`. Expected: all pass, including the checked-in scan snapshot case (the corpus uses lower-case names, so its output does not change).

- [ ] **Step 11: Commit**

```
git add src/parse/chart_files.h src/parse/chart_files.cpp src/parse/song.cpp src/app/analysis.cpp CMakeLists.txt tests/test_song.cpp tests/test_analysis.cpp
git commit -m "Accept chart file names in any case, decided in one place" -m "Task: Parser single owners
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 12: Write the failing tests for the .sng reader**

Create tests/test_sng.cpp:

```cpp
// Tests for parse/sng: the one reader of the .sng container layout, used by
// the note loader, the Preview's audio extractor and the library scan.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/winstr.h"
#include "midi_util.h"
#include "parse/sng.h"
#include "parse/song.h"

using namespace hydra;

namespace {

void push_u32(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void push_u64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// A .sng: 10 prefix bytes, the 16-byte XOR mask, the metadata block (pair
// count, then u32-length key and value strings), the file section (its
// length, the file count, then name/length/absolute-offset entries), then
// each file's bytes XOR-encoded from its own index 0.
std::vector<uint8_t> make_sng(
    const std::vector<std::pair<std::string, std::string>>& meta,
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::vector<uint8_t> out(10, 0x53);
    uint8_t mask[16];
    for (int i = 0; i < 16; ++i) mask[i] = static_cast<uint8_t>(0x30 + i * 7);
    out.insert(out.end(), mask, mask + 16);

    std::vector<uint8_t> md;
    push_u64(md, meta.size());
    for (const auto& [k, v] : meta) {
        push_u32(md, k.size());
        md.insert(md.end(), k.begin(), k.end());
        push_u32(md, v.size());
        md.insert(md.end(), v.begin(), v.end());
    }
    push_u64(out, md.size());
    out.insert(out.end(), md.begin(), md.end());

    size_t entries = 0;
    for (const auto& f : files) entries += 1 + f.first.size() + 16;
    uint64_t offset = out.size() + 16 + entries;
    push_u64(out, 8 + entries);
    push_u64(out, files.size());
    for (const auto& f : files) {
        out.push_back(static_cast<uint8_t>(f.first.size()));
        out.insert(out.end(), f.first.begin(), f.first.end());
        push_u64(out, f.second.size());
        push_u64(out, offset);
        offset += f.second.size();
    }
    for (const auto& f : files)
        for (size_t i = 0; i < f.second.size(); ++i)
            out.push_back(static_cast<uint8_t>(f.second[i] ^ mask[i % 16] ^ (i & 0xff)));
    return out;
}

std::vector<uint8_t> tiny_mid() {
    return testmidi::smf(testmidi::concat({testmidi::track_name("PART DRUMS"),
                                           testmidi::set_tempo(),
                                           testmidi::note_on(96, 100),
                                           {0x83, 0x60, 0x90, 97, 100},  // tick 480: red
                                           testmidi::end_of_track()}));
}

std::string sng_fixture_path(const char* name) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return wide_to_utf8(tmp) + "hydra_sng_" + std::to_string(GetCurrentProcessId()) + "_" + name;
}

void write_fixture(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* f = fopen_utf8(path, L"wb");
    REQUIRE_MESSAGE(f != nullptr, "cannot write " << path);
    if (!bytes.empty()) std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

}  // namespace

TEST_CASE("sng: metadata pairs read back in order") {
    std::vector<uint8_t> buf = make_sng({{"name", "Song"}, {"Artist", "Band"}}, {});
    auto pairs = sng_read_metadata(buf);
    REQUIRE(pairs.size() == 2);
    CHECK(pairs[0] == std::make_pair(std::string("name"), std::string("Song")));
    CHECK(pairs[1] == std::make_pair(std::string("Artist"), std::string("Band")));
}

TEST_CASE("sng: the file table and each file decode") {
    const std::vector<uint8_t> mid = tiny_mid();
    const std::vector<uint8_t> ogg = {'O', 'g', 'g', 'S', 1, 2, 3};
    std::vector<uint8_t> buf = make_sng({{"name", "Song"}}, {{"notes.mid", mid}, {"song.ogg", ogg}});
    auto table = sng_read_file_table(buf);
    REQUIRE(table.size() == 2);
    CHECK(table[0].name == "notes.mid");
    CHECK(table[1].name == "song.ogg");
    auto got_mid = sng_decode_file(buf, table[0]);
    auto got_ogg = sng_decode_file(buf, table[1]);
    REQUIRE(got_mid.has_value());
    REQUIRE(got_ogg.has_value());
    CHECK(*got_mid == mid);
    CHECK(*got_ogg == ogg);
}

TEST_CASE("sng: truncated input stops early instead of reading past the end") {
    std::vector<uint8_t> whole = make_sng({{"name", "Song"}}, {{"notes.mid", tiny_mid()}});
    CHECK(sng_read_metadata({1, 2, 3}).empty());
    CHECK(sng_read_file_table({1, 2, 3}).empty());

    // Cut inside the file table: the entry is dropped, nothing is read past the end.
    std::vector<uint8_t> cut(whole.begin(), whole.begin() + (whole.size() - tiny_mid().size() - 4));
    CHECK(sng_read_file_table(cut).empty());

    // An entry whose offset + length wraps around is refused.
    SngFileEntry bad;
    bad.name = "notes.mid";
    bad.offset = 40;
    bad.length = UINT64_MAX - 10;
    CHECK_FALSE(sng_decode_file(whole, bad).has_value());
}

TEST_CASE("sng: the note loader reads the chart through the shared reader") {
    const std::string path = sng_fixture_path("loader.sng");
    write_fixture(path, make_sng({{"name", "Song"}}, {{"song.ogg", {1, 2}}, {"NOTES.MID", tiny_mid()}}));
    Song direct = load_songbytes_mid(tiny_mid(), true, true);
    Song via_sng = load_songpath_sng(path, true, true);
    REQUIRE(via_sng.sequence.size() == direct.sequence.size());
    for (size_t i = 0; i < direct.sequence.size(); ++i)
        CHECK(via_sng.sequence[i].timecode.ticks() == direct.sequence[i].timecode.ticks());

    // A file too short to hold a table throws a clear error.
    const std::string tiny = sng_fixture_path("tiny.sng");
    write_fixture(tiny, {1, 2, 3});
    CHECK_THROWS_AS(load_songpath_sng(tiny, true, true), std::runtime_error);
}
```

In CMakeLists.txt, in the hydra_tests source list, add `tests/test_sng.cpp` on the line after `tests/test_song.cpp`.

- [ ] **Step 13: Run them and watch them fail**

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error, `cannot open include file 'parse/sng.h'`.

- [ ] **Step 14: Create the .sng reader and switch the three readers to it**

Create src/parse/sng.h:

```cpp
// .sng container reading: the one owner of the layout. The note loader
// (parse/song.cpp), the Preview's audio extractor (app/preview_source.cpp)
// and the library scan's metadata reader (app/analysis.cpp) all read through
// here.
//
//   offset 0..9    prefix (never needed for reading)
//   offset 10..25  a 16-byte XOR mask
//   offset 26      u64 LE: the metadata block's length
//   offset 34      the metadata block: u64 pair count, then pairs of
//                  (u32 LE length + bytes) key and value strings
//   after it       u64 LE: the file section's length (not needed: entries
//                  carry absolute offsets), u64 LE file count, then entries
//                  of u8 name length + name, u64 LE length, u64 LE offset
//
// A file's byte i is stored XORed with mask[i % 16] ^ (i & 0xff), counting i
// from the file's own start. Every read is bounds-checked: truncated input
// stops early and never reads past the buffer.

#ifndef HYDRA_PARSE_SNG_H
#define HYDRA_PARSE_SNG_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hydra {

constexpr size_t kSngXorMaskOffset = 10;
constexpr size_t kSngXorMaskSize = 16;
constexpr size_t kSngMetadataLenOffset = kSngXorMaskOffset + kSngXorMaskSize;  // 26
constexpr size_t kSngMetadataOffset = kSngMetadataLenOffset + 8;               // 34

struct SngFileEntry {
    std::string name;
    uint64_t length = 0;
    uint64_t offset = 0;  // absolute, from the start of the file
};

// The metadata pairs in file order, keys as stored (callers fold case).
// Stops at the first pair that does not fit.
std::vector<std::pair<std::string, std::string>> sng_read_metadata(const std::vector<uint8_t>& buf);

// The file table. Stops at the first entry that does not fit and returns the
// entries before it.
std::vector<SngFileEntry> sng_read_file_table(const std::vector<uint8_t>& buf);

// One file's decoded bytes, or nullopt when its range is outside the buffer.
std::optional<std::vector<uint8_t>> sng_decode_file(const std::vector<uint8_t>& buf,
                                                    const SngFileEntry& entry);

}  // namespace hydra

#endif  // HYDRA_PARSE_SNG_H
```

Create src/parse/sng.cpp:

```cpp
#include "parse/sng.h"

namespace hydra {

namespace {

// n bytes starting at pos lie inside buf (no overflow for any n).
bool fits(const std::vector<uint8_t>& buf, size_t pos, uint64_t n) {
    return pos <= buf.size() && n <= buf.size() - pos;
}

uint64_t u64_at(const std::vector<uint8_t>& buf, size_t pos) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(buf[pos + i]) << (8 * i);
    return v;
}

uint32_t u32_at(const std::vector<uint8_t>& buf, size_t pos) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(buf[pos + i]) << (8 * i);
    return v;
}

std::string string_at(const std::vector<uint8_t>& buf, size_t pos, size_t len) {
    return std::string(reinterpret_cast<const char*>(buf.data() + pos), len);
}

}  // namespace

std::vector<std::pair<std::string, std::string>> sng_read_metadata(const std::vector<uint8_t>& buf) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!fits(buf, kSngMetadataOffset, 8)) return out;
    size_t pos = kSngMetadataOffset;
    const uint64_t count = u64_at(buf, pos);
    pos += 8;
    for (uint64_t i = 0; i < count; ++i) {
        if (!fits(buf, pos, 4)) break;
        const uint32_t key_len = u32_at(buf, pos);
        pos += 4;
        if (!fits(buf, pos, key_len)) break;
        std::string key = string_at(buf, pos, key_len);
        pos += key_len;
        if (!fits(buf, pos, 4)) break;
        const uint32_t value_len = u32_at(buf, pos);
        pos += 4;
        if (!fits(buf, pos, value_len)) break;
        std::string value = string_at(buf, pos, value_len);
        pos += value_len;
        out.emplace_back(std::move(key), std::move(value));
    }
    return out;
}

std::vector<SngFileEntry> sng_read_file_table(const std::vector<uint8_t>& buf) {
    std::vector<SngFileEntry> out;
    if (!fits(buf, kSngMetadataLenOffset, 8)) return out;
    const uint64_t metadata_len = u64_at(buf, kSngMetadataLenOffset);
    if (!fits(buf, kSngMetadataOffset, metadata_len)) return out;
    size_t pos = kSngMetadataOffset + static_cast<size_t>(metadata_len);
    if (!fits(buf, pos, 16)) return out;
    pos += 8;  // the file section's length; entries carry absolute offsets
    const uint64_t count = u64_at(buf, pos);
    pos += 8;
    for (uint64_t i = 0; i < count; ++i) {
        if (!fits(buf, pos, 1)) break;
        const size_t name_len = buf[pos];
        pos += 1;
        if (!fits(buf, pos, name_len + 16)) break;
        SngFileEntry e;
        e.name = string_at(buf, pos, name_len);
        pos += name_len;
        e.length = u64_at(buf, pos);
        pos += 8;
        e.offset = u64_at(buf, pos);
        pos += 8;
        out.push_back(std::move(e));
    }
    return out;
}

std::optional<std::vector<uint8_t>> sng_decode_file(const std::vector<uint8_t>& buf,
                                                    const SngFileEntry& entry) {
    if (!fits(buf, kSngXorMaskOffset, kSngXorMaskSize)) return std::nullopt;
    if (entry.offset > buf.size() || entry.length > buf.size() - entry.offset) return std::nullopt;
    const uint8_t* mask = buf.data() + kSngXorMaskOffset;
    const size_t start = static_cast<size_t>(entry.offset);
    std::vector<uint8_t> out(static_cast<size_t>(entry.length));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<uint8_t>(buf[start + i] ^ mask[i % kSngXorMaskSize] ^ (i & 0xff));
    return out;
}

}  // namespace hydra
```

In CMakeLists.txt, in the hydra_core source list, add `src/parse/sng.cpp` on the line after `src/parse/chart_files.cpp`.

In src/parse/song.cpp add `#include "parse/sng.h"` after `#include "parse/midi.h"`, and replace the whole body of load_songpath_sng (from `std::vector<uint8_t> buf = read_file_bytes(path);` through its last `return load_songbytes_chart(...)`, lines 1040-1102 today; after Task 1 the two final calls also pass `rules`) with:

```cpp
    std::vector<uint8_t> buf = read_file_bytes(path);

    // A notes.mid wins over a notes.chart; among .chart entries the last one
    // listed wins (the order this loader has always used).
    const std::vector<SngFileEntry> entries = sng_read_file_table(buf);
    const SngFileEntry* notes = nullptr;
    ChartFormat format = ChartFormat::None;
    for (const SngFileEntry& e : entries) {
        const ChartFormat f = notes_file_format(e.name);
        if (f == ChartFormat::Mid) {
            notes = &e;
            format = f;
            break;
        }
        if (f == ChartFormat::Chart) {
            notes = &e;
            format = f;
        }
    }
    if (!notes) throw std::runtime_error("No chart files found in SNG file.");

    std::optional<std::vector<uint8_t>> notebytes = sng_decode_file(buf, *notes);
    if (!notebytes) throw std::runtime_error("Truncated SNG file.");
    if (format == ChartFormat::Mid)
        return load_songbytes_mid(*notebytes, pro, bass2x, difficulty, rules);
    return load_songbytes_chart(*notebytes, pro, bass2x, difficulty, rules);
```

In src/app/preview_source.cpp add `#include "parse/sng.h"` after `#include "parse/srb.h"`, and replace the whole body of extract_sng_audio (lines 173-217 today, from `std::vector<PreviewAudioStem> stems;` through `return stems;`) with:

```cpp
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
```

(`read_u64` in preview_source.cpp stays: extract_srb_audio still uses it at line 276.)

In src/app/analysis.cpp add `#include "parse/sng.h"` after `#include "parse/chart_files.h"`. In parse_sng_metadata this step changes only how the bytes are read. The three default lines (`std::string title = ...;` and the artist and charter lines) and the `return {title, artist, charter};` line stay exactly as Task 11 left them. Task 11 owns the "(unknown)" fallback for an empty or missing name (user decision 10+22), and this step must not undo it.

First delete the two lambdas at the top of the function (lines 249-258 today):

```cpp
    auto u64_at = [&buf](size_t pos) {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(buf[pos + i]) << (8 * i);
        return v;
    };
    auto u32_at = [&buf](size_t pos) {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(buf[pos + i]) << (8 * i);
        return v;
    };
```

Then replace the block after the three default lines, from `const size_t kMetadataCountOffset = 34;` through the closing brace of the `for (uint64_t i = 0; i < count; ++i)` loop (lines 264-289 today), with:

```cpp
    for (const auto& [raw_key, value] : sng_read_metadata(buf)) {
        const std::string key = lower(raw_key);
        if (key == "name") title = value;
        else if (key == "artist") artist = value;
        else if (key == "charter") charter = value;
    }
```

The three `if (key == ...)` lines are today's. If Task 11 changed them (for example to skip an empty value), copy Task 11's version of those three lines into this loop instead. The function still receives only the head bytes captured while hashing (`kSngHeadCapture`, 1 MB), and `sng_read_metadata` stops cleanly when a pair runs past them, just as the old bounds checks did.

- [ ] **Step 15: Run them and watch them pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="sng:*,*preview_source*,discover_charts*"`. Expected: all pass. The existing preview-source .sng cases must pass unchanged.

- [ ] **Step 16: Commit**

```
git add src/parse/sng.h src/parse/sng.cpp src/parse/song.cpp src/app/preview_source.cpp src/app/analysis.cpp CMakeLists.txt tests/test_sng.cpp
git commit -m "Read the .sng layout in one bounds-checked place" -m "Task: Parser single owners
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 17: Write the failing test for the stray SP note-off**

A chart with a stray SP note-off (a 116 note-off with no note-on before it) must flag nothing. Today the MIDI handler reads the start tick without checking it. After an earlier real phrase, the stale start from that phrase is still in the optional's storage, so on MSVC the chord before the stray note-off gets flagged. Add to the end of tests/test_song.cpp:

```cpp
TEST_CASE("mid: a stray SP note-off flags nothing") {
    std::vector<std::vector<uint8_t>> ev;
    ev.push_back(testmidi::track_name("PART DRUMS"));
    ev.push_back(testmidi::set_tempo());
    ev.push_back(testmidi::note_on(116, 100));       // tick 0:   SP phrase starts
    ev.push_back(testmidi::note_on(96, 100));        // tick 0:   kick, inside the phrase
    ev.push_back({0x81, 0x70, 0x80, 116, 0});        // tick 240: SP phrase ends
    ev.push_back({0x81, 0x70, 0x90, 96, 100});       // tick 480: kick, outside any phrase
    ev.push_back({0x81, 0x70, 0x80, 116, 0});        // tick 720: stray SP note-off
    ev.push_back(testmidi::end_of_track());
    Song song = load_songbytes_mid(testmidi::smf(testmidi::concat(ev)), true, true);

    REQUIRE(song.sequence.size() == 2);
    CHECK(song.sequence[0].flag_sp);
    CHECK_FALSE(song.sequence[1].flag_sp);
}
```

- [ ] **Step 18: Run it and watch it fail**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="mid: a stray SP note-off flags nothing"`. Expected: `CHECK_FALSE( song.sequence[1].flag_sp )` fails. (The read is undefined behaviour, so a different compiler could pass by luck. The test still pins the rule.)

- [ ] **Step 19: Move the three shared handlers to free functions**

Task 1 already changed `fill_lands_on_chord` (a slop parameter) and gave both parser classes a `rules_` member. Neither touches the handler bodies below, so the quotes here match the file as Task 1 leaves it. If an earlier task already changed these lines, apply the same change to the new lines.

In src/parse/song.cpp, inside the anonymous namespace near the top (after `fill_lands_on_chord`), add:

```cpp
// The parser handlers MIDI and .chart share. Each parser decides when to
// call them (its own event phases); what they do to the Song lives here once.

// A time signature: ticks per measure = resolution * 4 * num / den.
void apply_timesig(Song& song, int64_t tick, int numerator, int denominator) {
    song.tpm_changes[tick] = song.tick_resolution() * static_cast<int64_t>(numerator) * 4 /
                             static_cast<int64_t>(denominator);
}

// A fill ending: the last chord becomes an activation chord whose fill began
// at `starttick`.
void apply_fill_end(Song& song, int64_t starttick) {
    if (song.sequence.empty()) return;
    SongTimestamp& last = song.sequence.back();
    if (last.timecode.ticks() >= starttick)
        last.activation_length = last.timecode.ticks() - starttick;
}

// An SP phrase ending: the last chord closes the phrase that began at
// `starttick`, if it lies inside it.
void mark_sp_phrase_end(Song& song, int64_t starttick) {
    if (song.sequence.empty()) return;
    SongTimestamp& last = song.sequence.back();
    if (last.timecode.ticks() >= starttick) {
        last.flag_sp = true;
        last.sp_phrase_start = starttick;
    }
}
```

In MidiParser replace:

```cpp
    void op_timesig(int64_t tick, int numerator, int denominator) {
        song_->tpm_changes[tick] = song_->tick_resolution() *
                                   static_cast<int64_t>(numerator) * 4 /
                                   static_cast<int64_t>(denominator);
    }
```

with:

```cpp
    void op_timesig(int64_t tick, int numerator, int denominator) {
        apply_timesig(*song_, tick, numerator, denominator);
    }
```

and replace:

```cpp
    void op_apply_fill(int64_t starttick) {
        if (song_->sequence.empty()) return;
        SongTimestamp& last = song_->sequence.back();
        if (last.timecode.ticks() >= starttick)
            last.activation_length = last.timecode.ticks() - starttick;
    }
    void op_sp_start(int64_t tick) { sp_start_tick_ = tick; }
    void op_sp_end() {
        if (song_->sequence.empty()) {
            sp_start_tick_.reset();
            return;
        }
        if (song_->sequence.back().timecode.ticks() >= *sp_start_tick_) {
            song_->sequence.back().flag_sp = true;
            song_->sequence.back().sp_phrase_start = *sp_start_tick_;
        }
        sp_start_tick_.reset();
    }
```

with:

```cpp
    void op_apply_fill(int64_t starttick) { apply_fill_end(*song_, starttick); }
    void op_sp_start(int64_t tick) { sp_start_tick_ = tick; }
    void op_sp_end() {
        // A note-off with no phrase open (a stray 116 off) closes nothing.
        if (sp_start_tick_) mark_sp_phrase_end(*song_, *sp_start_tick_);
        sp_start_tick_.reset();
    }
```

In ChartParser replace the op_timesig body the same way (the text is identical to the MIDI one quoted above), and replace:

```cpp
    void op_fillend(int64_t starttick) {
        if (song_->sequence.empty()) return;
        SongTimestamp& last = song_->sequence.back();
        if (last.timecode.ticks() >= starttick)
            last.activation_length = last.timecode.ticks() - starttick;
    }
```

with:

```cpp
    void op_fillend(int64_t starttick) { apply_fill_end(*song_, starttick); }
```

and replace:

```cpp
    void op_sp_end(int64_t starttick) {
        if (song_->sequence.empty()) {
            sp_end_tick_.reset();
            return;
        }
        if (song_->sequence.back().timecode.ticks() >= starttick) {
            song_->sequence.back().flag_sp = true;
            song_->sequence.back().sp_phrase_start = starttick;
        }
        sp_end_tick_.reset();
    }
```

with:

```cpp
    void op_sp_end(int64_t starttick) {
        mark_sp_phrase_end(*song_, starttick);
        sp_end_tick_.reset();
    }
```

The .chart caller keeps `sp_start_tick_.value_or(0)`: a .chart phrase always carries its start and length on one line, so its start is never missing.

- [ ] **Step 20: Run it and watch it pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="mid:*,chart:*,*corpus*"`. Expected: all pass, including the corpus parity cases.

- [ ] **Step 21: Commit**

```
git add src/parse/song.cpp tests/test_song.cpp
git commit -m "Share the time-signature, fill-end and SP-end handlers; ignore a stray SP note-off" -m "Task: Parser single owners
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 22: Write the tests for the variable-length read**

The two decoders agree on every well-formed file. They differ only on a quantity of five or more bytes. `parse_track` accumulates in 64 bits, which matches mido (Hydra's reference reader, whose Python integers do not wrap). `read_varlen` wraps at 32 bits. So the single owner is `read_varlen` widened to 64 bits, and `parse_track` calls it. Delta times do not change.

Meta and sysex lengths also go through `read_varlen`, and user decision 27 says the reader keeps following mido on broken files. mido's own code (mido/midifiles/midifiles.py) reads a length with `read_variable_int`, then calls `read_bytes`, which raises for any size over `MAX_MESSAGE_LENGTH = 1000000`. Nothing catches that, so mido refuses the whole file. Hydra today wraps such a length at 32 bits and reads the following bytes as events. Widening alone would not match mido either: a 64-bit length added to `pos` can wrap the position. So the length read gets mido's cap and throws `MidiError` with mido's message text. A length at or under the cap behaves as today: the payload is clamped to what the track holds.

Add to the end of tests/test_midi.cpp:

```cpp
TEST_CASE("midi: a five-byte delta accumulates past 32 bits, as mido does") {
    std::vector<uint8_t> track = {
        0x90, 0x80, 0x80, 0x80, 0x00,  // delta 2^32 (malformed: five bytes)
        0x90, 0x60, 0x64,              // note_on note 96 vel 100
        0x00, 0xFF, 0x2F, 0x00,        // end of track
    };
    hydra::MidiFile mid(smf(track));
    json expected = json::array({ json::array({
        json::array({int64_t{1} << 32, "note_on", 96, 100}),
    }) });
    CHECK(event_view(mid) == expected);
}

TEST_CASE("midi: a message longer than mido's 1,000,000-byte cap refuses the file, as mido does") {
    // A text meta whose length is 2^32. Today it wraps to 0 and the note_on
    // after it is read as a real event.
    std::vector<uint8_t> meta = {
        0x00, 0xFF, 0x01, 0x90, 0x80, 0x80, 0x80, 0x00,  // text meta, length 2^32
        0x00, 0x90, 0x60, 0x64,                          // note_on note 96 vel 100
        0x00, 0xFF, 0x2F, 0x00,                          // end of track
    };
    CHECK_THROWS_AS((hydra::MidiFile(smf(meta))), hydra::MidiError);

    // A sysex one byte over the cap: 1,000,001 = 0xBD 0x84 0x41.
    std::vector<uint8_t> sysex = {
        0x00, 0xF0, 0xBD, 0x84, 0x41,
        0x00, 0xFF, 0x2F, 0x00,
    };
    CHECK_THROWS_AS((hydra::MidiFile(smf(sysex))), hydra::MidiError);

    // Exactly at the cap (1,000,000 = 0xBD 0x84 0x40) is not refused. The
    // track is shorter than that, so the payload is clamped as today.
    std::vector<uint8_t> at_cap = {
        0x00, 0xFF, 0x01, 0xBD, 0x84, 0x40, 'a', 'b',
    };
    CHECK_NOTHROW((hydra::MidiFile(smf(at_cap))));
}
```

- [ ] **Step 23: Run them and watch the second fail**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="midi: a*"`. Expected: the five-byte delta case passes today (it pins the behaviour the change must keep). The cap case fails at both `CHECK_THROWS_AS` lines: today the meta length wraps to 0 and the sysex simply runs off the end, so nothing throws. Its `CHECK_NOTHROW` line passes.

- [ ] **Step 24: Widen read_varlen, use it for deltas, and cap message lengths**

In src/parse/midi.cpp replace:

```cpp
// A variable-length quantity, 7 bits per byte. Advances pos.
uint32_t read_varlen(const uint8_t* data, size_t& pos, size_t end) {
    uint32_t value = 0;
```

with:

```cpp
// A variable-length quantity, 7 bits per byte. Advances pos. Accumulates in
// 64 bits so a malformed 5+-byte quantity reads as mido reads it (Python
// integers do not wrap) instead of wrapping at 32 bits.
uint64_t read_varlen(const uint8_t* data, size_t& pos, size_t end) {
    uint64_t value = 0;
```

Right after the closing brace of read_varlen (still inside the anonymous namespace), add:

```cpp
// mido's MAX_MESSAGE_LENGTH. mido's read_bytes raises for any meta or sysex
// message longer than this, and nothing catches it, so mido refuses the whole
// file. Hydra follows mido on broken files (user decision 27, 2026-09-24).
// The cap also keeps `pos += length` from wrapping the read position.
constexpr uint64_t kMaxMessageLength = 1000000;

// A meta or sysex length. Throws MidiError, with mido's wording, past the cap.
uint64_t read_message_length(const uint8_t* data, size_t& pos, size_t end) {
    const uint64_t length = read_varlen(data, pos, end);
    if (length > kMaxMessageLength)
        throw MidiError("Message length " + std::to_string(length) +
                        " exceeds maximum length " + std::to_string(kMaxMessageLength));
    return length;
}
```

In parse_track replace:

```cpp
        // Delta time.
        int64_t delta = 0;
        while (pos < end) {
            uint8_t b = data[pos++];
            delta = (delta << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        pending += delta;
```

with:

```cpp
        pending += static_cast<int64_t>(read_varlen(data, pos, end));  // delta time
```

Change the two meta/sysex length reads, both today reading `uint32_t length = read_varlen(data, pos, end);` (lines 227 and 249), to:

```cpp
            const uint64_t length = read_message_length(data, pos, end);
```

The code after them stays. The meta branch still clamps the payload with `std::min<size_t>(length, avail)`, and both branches still stop the loop once `pos` passes `end`. midi.cpp already includes `<string>` through midi.h (MidiError takes a `std::string`), so `std::to_string` needs no new include.

A truncated track whose lengths stay under the cap is not changed by this step. mido would raise `EOFError` there too, while Hydra keeps what it read. That tolerance is older than this plan and is listed under "Still open".

- [ ] **Step 25: Run them and watch them pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="midi:*,mid:*"`. Expected: all pass, including the corpus smoke case "midi: every corpus .mid reads with a sane structure" (no real chart has a message over 1,000,000 bytes).

- [ ] **Step 26: Run the whole suite**

Run `.\build-cpp\Release\hydra_tests.exe`. Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 27: Commit**

```
git add src/parse/midi.cpp tests/test_midi.cpp
git commit -m "Decode MIDI variable-length values in one place and cap message lengths, as mido does" -m "Task: Parser single owners
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 13: Dynamics keys and version stamp

The Dynamics tab caches per-pad ghost and accent counts. Two things are wrong today. First, the rules around that cache are copied. The in-memory cache key ("path|pro|Expert") is built in two files. The stored-row key (`DynamicsKey`) is built at four sites. The rule "store counts only when 2x bass was on, because otherwise the 2x kicks were dropped" is written twice, and its reason only once in full. Second, a stored row carries no stamp of the parser that counted it. When the parser's counting changes, old rows keep showing stale counts forever. This task puts the keys and the store-only-with-2x rule in one helper module.

It also stamps each dynamics row (user decision 12+21). The stamp is a number kept by hand in the code: `kDynamicsCountVersion`, in src/app/dynamics_breakdown.h, starting at 1. Think of it as an edition number printed on each saved count. Whoever changes the counting, or the parser that feeds it, raises the number by one. Every row printed under the old edition then reads as missing, and the tab recounts it in the background the next time it is viewed. The stamp is deliberately not the app version. A release that leaves counting alone keeps every stored row, so users are not made to recount for nothing. It also leaves out the hydra_rules.ini fingerprint, because the rules move activation marks, never which notes a chart has or how hard they are hit.

Rows saved before this task have no stamp. The new column gives them 0, so each is recounted once. That also covers Task 12's parser changes, so Task 12 bumps nothing. What the user sees: the first open of a chart's Dynamics tab after this lands briefly shows the "counting" state again, then the same numbers (or corrected ones where the parser changed). Later releases do this only when someone bumps the constant.

**Goal:** One module owns the dynamics cache key, the stored-row key and the 2x-bass rule. Every dynamics row carries the hand-bumped `kDynamicsCountVersion` stamp, and a row with any other stamp is recounted on next view.

**Files:** Changes src/app/dynamics_breakdown.h and .cpp (the stamp constant, five helpers and one flag), src/store/record_store.cpp (new count_version column, its migration, and the stamped put/get), src/store/record_store.h (the stamp parameter on put/get), src/ui/app_state.cpp, src/ui/dynamics_load_job.cpp and src/app/analysis.cpp (callers). Adds cases to tests/test_dynamics_store.cpp and tests/test_app_state.cpp.

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="dynamics keys come from one place"` passes.
- [ ] `hydra_tests.exe -tc="store_dynamics_from_analysis stores only when the parse kept 2x kicks"` passes.
- [ ] `hydra_tests.exe -tc="RecordStore dynamics rows from before the stamp read as missing"` passes.
- [ ] `hydra_tests.exe -tc="RecordStore dynamics rows with another count stamp read as missing"` passes.
- [ ] `hydra_tests.exe -tc="update_dynamics recounts a stored row with an older count stamp"` passes.
- [ ] `hydra_tests.exe -tc="update_dynamics uses a stored row with the current count stamp"` passes.
- [ ] The existing `RecordStore dynamics put/get` case passes, with only the stamp argument added to its calls.
- [ ] `difficulty_name(` no longer appears in src/ui/app_state.cpp, src/ui/dynamics_load_job.cpp or in the dynamics block of src/app/analysis.cpp.
- [ ] `kDynamicsCountVersion` is defined once, in src/app/dynamics_breakdown.h, and its comment says to bump it by hand when the counting or its parser changes.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*dynamics*,RecordStore*"` -> `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests for the helpers**

In tests/test_dynamics_store.cpp add `#include "midi_util.h"` and `#include "parse/song.h"` to the includes, then add at the end:

```cpp
TEST_CASE("dynamics keys come from one place") {
    CHECK(dynamics_cache_key("C:\\songs\\a\\notes.mid", true, hydra::Difficulty::Expert) ==
          "C:\\songs\\a\\notes.mid|pro|Expert");
    CHECK(dynamics_cache_key("x.chart", false, hydra::Difficulty::Hard) == "x.chart|std|Hard");

    DynamicsKey k = dynamics_store_key("abc123", hydra::Difficulty::Medium, true);
    CHECK(k.md5 == "abc123");
    CHECK(k.difficulty == "Medium");
    CHECK(k.pro);

    // The background count always parses with 2x kicks kept.
    CHECK(kDynamicsParseBass2x);
}

TEST_CASE("store_dynamics_from_analysis stores only when the parse kept 2x kicks") {
    TempFile tmp;
    RecordStore store(tmp.path);
    hydra::Song song = hydra::load_songbytes_mid(
        testmidi::smf(testmidi::concat({testmidi::track_name("PART DRUMS"), testmidi::set_tempo(),
                                        testmidi::note_on(96, 100), testmidi::end_of_track()})),
        true, true);

    store_dynamics_from_analysis(store, "nokicks", song, /*bass2x=*/false,
                                 hydra::Difficulty::Expert, true);
    CHECK_FALSE(store.get_dynamics(dynamics_store_key("nokicks", hydra::Difficulty::Expert, true))
                    .has_value());

    store_dynamics_from_analysis(store, "withkicks", song, /*bass2x=*/true,
                                 hydra::Difficulty::Expert, true);
    auto blob = store.get_dynamics(dynamics_store_key("withkicks", hydra::Difficulty::Expert, true));
    REQUIRE(blob.has_value());
    auto bd = decode_dynamics(*blob);
    REQUIRE(bd.has_value());
    CHECK(bd->row(DynamicsRow::Kick).all() == 1);
}
```

- [ ] **Step 2: Run them and watch them fail**

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors, `'dynamics_cache_key': identifier not found` (and the same for the other three names).

- [ ] **Step 3: Add the helpers**

In src/app/dynamics_breakdown.h replace:

```cpp
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hydra {

class Song;  // forward — defined in parse/song.h

namespace app {
```

with:

```cpp
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "parse/song.h"
#include "store/record_store.h"

namespace hydra {
namespace app {
```

and add before `}  // namespace app`:

```cpp
// ---- the cache rules, in one place ----------------------------------------

// The Dynamics tab's background count always parses with 2x kicks kept, so
// the "2x kick" row is known even while the "2x Bass" box is off.
constexpr bool kDynamicsParseBass2x = true;

// The in-memory key of one count: the chart file, the pro-drums view and the
// difficulty, as "path|pro|Expert" or "path|std|Hard".
std::string dynamics_cache_key(const std::string& notespath, bool pro, Difficulty difficulty);

// The stored-row key for one count.
store::DynamicsKey dynamics_store_key(const std::string& md5, Difficulty difficulty, bool pro);

// After an analysis, store its dynamics counts as a free by-product (the
// chart is already parsed). Stores only when the analysis parsed with bass2x
// on: with it off the parse dropped the 2x kicks and the counts would be
// incomplete. Best effort: a failed save is swallowed so it can never block
// the analysis record.
void store_dynamics_from_analysis(store::RecordStore& store, const std::string& md5,
                                  const Song& song, bool bass2x, Difficulty difficulty, bool pro);
```

In src/app/dynamics_breakdown.cpp add before `}  // namespace app`:

```cpp
// ---- the cache rules --------------------------------------------------------

std::string dynamics_cache_key(const std::string& notespath, bool pro, Difficulty difficulty) {
    return notespath + "|" + (pro ? "pro" : "std") + "|" + difficulty_name(difficulty);
}

store::DynamicsKey dynamics_store_key(const std::string& md5, Difficulty difficulty, bool pro) {
    return store::DynamicsKey{md5, difficulty_name(difficulty), pro};
}

void store_dynamics_from_analysis(store::RecordStore& store, const std::string& md5,
                                  const Song& song, bool bass2x, Difficulty difficulty, bool pro) {
    if (!bass2x) return;  // the 2x kicks were dropped; the counts would be incomplete
    try {
        store.put_dynamics(dynamics_store_key(md5, difficulty, pro),
                           encode_dynamics(count_dynamics(song)));
    } catch (...) {
        // Best effort: never block the analysis record.
    }
}
```

- [ ] **Step 4: Point the callers at the helpers**

The quotes below are today's code. Tasks 1 and 11 also edit src/app/analysis.cpp. If an earlier task already changed these lines, apply the same change to the new lines.

In src/ui/dynamics_load_job.cpp replace:

```cpp
    // Always parse with bass2x=true so the 2x row is known even when
    // the "2x Bass" box is off.
    key_ = entry_.notespath + "|" + (pro_ ? "pro" : "std") + "|" +
           difficulty_name(difficulty_);
```

with:

```cpp
    key_ = app::dynamics_cache_key(entry_.notespath, pro_, difficulty_);
```

and replace:

```cpp
        Song song = load_songpath(entry_.notespath, pro_,
                                  /*bass2x=*/true, difficulty_);
```

with:

```cpp
        Song song = load_songpath(entry_.notespath, pro_, app::kDynamicsParseBass2x,
                                  difficulty_);
```

Task 1 gave the loaders a trailing rules parameter. If an earlier task already added a rules argument to this call, keep it and change only the bass2x argument.

In src/ui/app_state.cpp, update_dynamics, replace:

```cpp
    std::string want_key = selected->notespath + "|" +
                           (pro ? "pro" : "std") + "|" +
                           difficulty_name(diff);
```

with:

```cpp
    std::string want_key = app::dynamics_cache_key(selected->notespath, pro, diff);
```

Replace both `store::DynamicsKey dk{selected->md5, difficulty_name(diff), pro};` lines (124 and 148 today) with `store::DynamicsKey dk = app::dynamics_store_key(selected->md5, diff, pro);`.

In store_finished_analysis replace:

```cpp
        // The analysis already parsed the chart, so store the dynamics
        // breakdown as a free by-product -- but only when bass2x was on,
        // because with bass2x off the parse dropped the 2x kicks and the
        // stored counts would be incomplete.
        // Key it by the settings the job snapshotted, not the current ones:
        // the user may have moved the difficulty box since it started.
        const app::AnalysisSettings& as = analyze_job->settings();
        if (as.bass2x) {
            try {
                auto bd = app::count_dynamics(result.song);
                auto blob = app::encode_dynamics(bd);
                store::DynamicsKey dk{song.md5, difficulty_name(as.difficulty),
                                      as.prodrums};
                store->put_dynamics(dk, blob);
            } catch (...) {
                // Best effort: a dynamics save failure must not block the
                // analysis record from being stored.
            }
        }
```

with:

```cpp
        // Key the dynamics by the settings the job snapshotted, not the
        // current ones: the user may have moved the difficulty box since it
        // started.
        const app::AnalysisSettings& as = analyze_job->settings();
        app::store_dynamics_from_analysis(*store, song.md5, result.song, as.bass2x,
                                          as.difficulty, as.prodrums);
```

In src/app/analysis.cpp, in the batch runner, replace:

```cpp
            // Store the dynamics breakdown as a free by-product: the chart
            // is already parsed, so counting costs almost nothing. Only when
            // bass2x is on, because with it off the 2x kicks were dropped.
            if (settings.bass2x) {
                try {
                    auto bd = count_dynamics(wr.analysis->song);
                    auto blob = encode_dynamics(bd);
                    store::DynamicsKey dk{wr.item.md5,
                                          difficulty_name(settings.difficulty),
                                          settings.prodrums};
                    store.put_dynamics(dk, blob);
                } catch (...) {
                    // Best effort: never block the analysis record.
                }
            }
```

with:

```cpp
            store_dynamics_from_analysis(store, wr.item.md5, wr.analysis->song,
                                         settings.bass2x, settings.difficulty,
                                         settings.prodrums);
```

- [ ] **Step 5: Run them and watch them pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*dynamics*"`. Expected: all pass.

- [ ] **Step 6: Commit**

```
git add src/app/dynamics_breakdown.h src/app/dynamics_breakdown.cpp src/ui/dynamics_load_job.cpp src/ui/app_state.cpp src/app/analysis.cpp tests/test_dynamics_store.cpp
git commit -m "Build the dynamics keys and apply the 2x-bass rule in one place" -m "Task: Dynamics keys and version stamp
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Write the failing tests for the count stamp**

The store takes the stamp as an argument, because the store layer does not know what the app's counting is. So every existing `put_dynamics` and `get_dynamics` call in tests/test_dynamics_store.cpp gets `, kDynamicsCountVersion` appended as its last argument. That is the 15 calls in the "RecordStore dynamics put/get" case and the 2 `get_dynamics` calls in Step 1's case. Nothing else in those cases changes.

Then add `#include <sqlite3.h>` to the includes (tests/test_store.cpp already includes it, so the path is set up), and add at the end:

```cpp
TEST_CASE("RecordStore dynamics rows from before the stamp read as missing") {
    TempFile tmp;
    {  // A file from before the stamp: the dynamics table has no count_version column.
        sqlite3* db = nullptr;
        REQUIRE(sqlite3_open(tmp.path.c_str(), &db) == SQLITE_OK);
        REQUIRE(sqlite3_exec(db,
                             "CREATE TABLE dynamics (md5 TEXT NOT NULL, difficulty TEXT NOT NULL,"
                             " pro INTEGER NOT NULL, blob BLOB NOT NULL,"
                             " PRIMARY KEY (md5, difficulty, pro));"
                             "INSERT INTO dynamics VALUES ('old', 'Expert', 0, x'01');",
                             nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
    }
    RecordStore store(tmp.path);
    DynamicsKey key{"old", "Expert", false};
    CHECK_FALSE(store.get_dynamics(key, kDynamicsCountVersion).has_value());  // count again
    CHECK(store.get_dynamics(key, 0).has_value());  // the old row is kept, stamped 0

    const std::vector<uint8_t> blob = encode_dynamics(make_full_breakdown(true));
    store.put_dynamics(key, blob, kDynamicsCountVersion);
    auto got = store.get_dynamics(key, kDynamicsCountVersion);
    REQUIRE(got.has_value());
    CHECK(*got == blob);
}

TEST_CASE("RecordStore dynamics rows with another count stamp read as missing") {
    TempFile tmp;
    RecordStore store(tmp.path);
    DynamicsKey key{"abc123", "Expert", false};
    const std::vector<uint8_t> blob = encode_dynamics(make_full_breakdown(true));

    store.put_dynamics(key, blob, 1);
    CHECK(store.get_dynamics(key, 1).has_value());
    CHECK_FALSE(store.get_dynamics(key, 2).has_value());  // someone bumped the counter

    // The recount under the new stamp replaces the row; the old stamp is gone.
    store.put_dynamics(key, blob, 2);
    CHECK(store.get_dynamics(key, 2).has_value());
    CHECK_FALSE(store.get_dynamics(key, 1).has_value());
}
```

These two show the store honours the stamp. The next two show the part the user sees: the Dynamics tab recounts a row with an older stamp. In tests/test_app_state.cpp add `#include "app/dynamics_breakdown.h"` to the includes, then add at the end:

```cpp
TEST_CASE("update_dynamics recounts a stored row with an older count stamp") {
    ScratchPaths paths("appstate_dyn_old");
    std::unique_ptr<AppState> app = app_on(paths);
    const hydra::store::DynamicsKey key = hydra::app::dynamics_store_key(
        library_entry(0).md5, app->settings.difficulty(), app->settings.view_prodrums);
    // A row counted before the last bump of the counter.
    app->store->put_dynamics(key, hydra::app::encode_dynamics(hydra::app::DynamicsBreakdown{}),
                             hydra::app::kDynamicsCountVersion - 1);

    app->update_dynamics();

    // The old row was not shown; a background recount started instead. (The
    // chart file does not exist, so the job itself fails. This test only
    // cares that the recount was started.)
    CHECK_FALSE(app->dynamics_result.has_value());
    CHECK(app->dynamics_job != nullptr);
}

TEST_CASE("update_dynamics uses a stored row with the current count stamp") {
    ScratchPaths paths("appstate_dyn_now");
    std::unique_ptr<AppState> app = app_on(paths);
    const hydra::store::DynamicsKey key = hydra::app::dynamics_store_key(
        library_entry(0).md5, app->settings.difficulty(), app->settings.view_prodrums);
    hydra::app::save_dynamics(*app->store, key, hydra::app::DynamicsBreakdown{});

    app->update_dynamics();

    CHECK(app->dynamics_result.has_value());
    CHECK(app->dynamics_job == nullptr);  // no recount
}
```

- [ ] **Step 8: Run them and watch them fail**

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors, `'kDynamicsCountVersion': undeclared identifier`, `'save_dynamics': is not a member of 'hydra::app'`, and `'hydra::store::RecordStore::put_dynamics': function does not take 3 arguments` (the same for `get_dynamics` with 2).

- [ ] **Step 9: Add the stamp**

In src/app/dynamics_breakdown.h, right after the `kDynamicsParseBass2x` constant that Step 3 added, add:

```cpp
// The stamp saved on every stored dynamics row. A row with any other stamp
// reads as missing, so the Dynamics tab recounts it in the background on the
// next view and saves it again under this stamp.
//
// BUMP THIS BY HAND (add 1) whenever the counting in count_dynamics, or the
// parser that feeds it, changes what any chart counts: src/parse/song.cpp
// (the .mid, .chart and .sng loaders), src/parse/midi.cpp and
// src/parse/srb.cpp. It is deliberately NOT the app version: a release that
// leaves counting alone keeps every stored row. It does not include the
// hydra_rules.ini fingerprint either: the rules move activation marks, never
// which notes a chart has or their velocities.
//
// 0 = rows saved before the stamp existed. 1 = the first stamp (Task 13).
inline constexpr int kDynamicsCountVersion = 1;

// The stored count for this key, or nullopt when there is none, it carries
// another stamp, or it fails to decode. The caller then recounts.
std::optional<DynamicsBreakdown> load_stored_dynamics(store::RecordStore& store,
                                                      const store::DynamicsKey& key);

// Saves a count under this key, stamped kDynamicsCountVersion. Throws on a
// store failure.
void save_dynamics(store::RecordStore& store, const store::DynamicsKey& key,
                   const DynamicsBreakdown& breakdown);
```

In src/app/dynamics_breakdown.cpp, after `dynamics_store_key`, add:

```cpp
std::optional<DynamicsBreakdown> load_stored_dynamics(store::RecordStore& store,
                                                      const store::DynamicsKey& key) {
    auto blob = store.get_dynamics(key, kDynamicsCountVersion);
    if (!blob) return std::nullopt;
    return decode_dynamics(*blob);
}

void save_dynamics(store::RecordStore& store, const store::DynamicsKey& key,
                   const DynamicsBreakdown& breakdown) {
    store.put_dynamics(key, encode_dynamics(breakdown), kDynamicsCountVersion);
}
```

and in `store_dynamics_from_analysis` replace:

```cpp
        store.put_dynamics(dynamics_store_key(md5, difficulty, pro),
                           encode_dynamics(count_dynamics(song)));
```

with:

```cpp
        save_dynamics(store, dynamics_store_key(md5, difficulty, pro), count_dynamics(song));
```

In src/store/record_store.cpp, in the constructor's table list, replace:

```cpp
        "CREATE TABLE IF NOT EXISTS dynamics ("
        "  md5        TEXT NOT NULL,"
        "  difficulty TEXT NOT NULL,"
        "  pro        INTEGER NOT NULL,"
        "  blob       BLOB NOT NULL,"
        "  PRIMARY KEY (md5, difficulty, pro)"
        ");");
```

with:

```cpp
        "CREATE TABLE IF NOT EXISTS dynamics ("
        "  md5           TEXT NOT NULL,"
        "  difficulty    TEXT NOT NULL,"
        "  pro           INTEGER NOT NULL,"
        "  blob          BLOB NOT NULL,"
        "  count_version INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (md5, difficulty, pro)"
        ");");
    // A dynamics table from before the stamp gets the column. Its rows read
    // 0, which matches no real stamp, so each is recounted once.
    if (!has_column("dynamics", "count_version"))
        exec("ALTER TABLE dynamics ADD COLUMN count_version INTEGER NOT NULL DEFAULT 0");
```

Task 2 also edits record_store.cpp. If an earlier task already changed these lines, apply the same change to the new lines.

In put_dynamics replace the signature, SQL and binds:

```cpp
void RecordStore::put_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_,
        "INSERT OR REPLACE INTO dynamics (md5, difficulty, pro, blob) VALUES (?,?,?,?)");
    bind_text(s, 1, key.md5);
    bind_text(s, 2, key.difficulty);
    sqlite3_bind_int(s, 3, key.pro ? 1 : 0);
    bind_blob(s, 4, blob);
```

with:

```cpp
void RecordStore::put_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob,
                               int count_version) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_,
        "INSERT OR REPLACE INTO dynamics (md5, difficulty, pro, blob, count_version)"
        " VALUES (?,?,?,?,?)");
    bind_text(s, 1, key.md5);
    bind_text(s, 2, key.difficulty);
    sqlite3_bind_int(s, 3, key.pro ? 1 : 0);
    bind_blob(s, 4, blob);
    sqlite3_bind_int(s, 5, count_version);
```

In get_dynamics replace:

```cpp
std::optional<std::vector<uint8_t>> RecordStore::get_dynamics(const DynamicsKey& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Stmt s = prepare(db_,
        "SELECT blob FROM dynamics WHERE md5=? AND difficulty=? AND pro=?");
    bind_text(s, 1, key.md5);
    bind_text(s, 2, key.difficulty);
    sqlite3_bind_int(s, 3, key.pro ? 1 : 0);
```

with:

```cpp
std::optional<std::vector<uint8_t>> RecordStore::get_dynamics(const DynamicsKey& key,
                                                               int count_version) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // A row with another stamp reads as missing, so the caller recounts it
    // and put_dynamics restamps it.
    Stmt s = prepare(db_,
        "SELECT blob FROM dynamics WHERE md5=? AND difficulty=? AND pro=? AND count_version=?");
    bind_text(s, 1, key.md5);
    bind_text(s, 2, key.difficulty);
    sqlite3_bind_int(s, 3, key.pro ? 1 : 0);
    sqlite3_bind_int(s, 4, count_version);
```

In src/store/record_store.h replace:

```cpp
    // Stores a dynamics-breakdown blob (INSERT OR REPLACE).
    void put_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob);
    // Returns the blob for this key, or nullopt when the row is missing.
    std::optional<std::vector<uint8_t>> get_dynamics(const DynamicsKey& key);
```

with:

```cpp
    // Stores a dynamics-breakdown blob (INSERT OR REPLACE) under the caller's
    // count stamp (app::kDynamicsCountVersion; go through app::save_dynamics).
    void put_dynamics(const DynamicsKey& key, const std::vector<uint8_t>& blob,
                      int count_version);
    // Returns the blob for this key, or nullopt when the row is missing or
    // carries another count stamp (the caller then recounts it).
    std::optional<std::vector<uint8_t>> get_dynamics(const DynamicsKey& key,
                                                     int count_version);
```

In src/ui/app_state.cpp, update_dynamics, replace the store lookup as Step 4 left it:

```cpp
    if (!dynamics_job) {
        store::DynamicsKey dk = app::dynamics_store_key(selected->md5, diff, pro);
        auto blob = store->get_dynamics(dk);
        if (blob) {
            auto decoded = app::decode_dynamics(*blob);
            if (decoded) {
                dynamics_result = std::move(*decoded);
                dynamics_key = want_key;
                return;
            }
            // Decode failure: fall through and re-parse.
        }
        // Store miss or decode failure: start the background job.
```

with:

```cpp
    if (!dynamics_job) {
        auto stored =
            app::load_stored_dynamics(*store, app::dynamics_store_key(selected->md5, diff, pro));
        if (stored) {
            dynamics_result = std::move(*stored);
            dynamics_key = want_key;
            return;
        }
        // Store miss, an older count stamp or a decode failure: start the
        // background job.
```

and in the reap replace:

```cpp
                store::DynamicsKey dk = app::dynamics_store_key(selected->md5, diff, pro);
                auto blob = app::encode_dynamics(*dynamics_result);
                store->put_dynamics(dk, blob);
```

with:

```cpp
                app::save_dynamics(*store, app::dynamics_store_key(selected->md5, diff, pro),
                                   *dynamics_result);
```

After this, `put_dynamics` and `get_dynamics` are called only from dynamics_breakdown.cpp and the tests.

- [ ] **Step 10: Run them and watch them pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*dynamics*,RecordStore*"`. Expected: all pass, including every existing store migration case.

- [ ] **Step 11: Commit**

```
git add src/app/dynamics_breakdown.h src/app/dynamics_breakdown.cpp src/store/record_store.cpp src/store/record_store.h src/ui/app_state.cpp tests/test_dynamics_store.cpp tests/test_app_state.cpp
git commit -m "Stamp dynamics rows with a hand-bumped count version; recount rows with another stamp" -m "Task: Dynamics keys and version stamp
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
### Task 14: Preview spans by tick, time box and gem sizes from their sources

The Preview decides "is this note inside an SP phrase, solo or fill" by time. It pushes each span's end half a millisecond past the span's last note. The comment says that margin never reaches the next note. It can: at 480 ticks per beat and 300 BPM one tick is 0.417 ms, so a chord one tick after a phrase's last chord draws with the SP look though the parser says it is outside. The fix makes the tick span the owner. The drawn edge now sits half a tick past the last note, worked out from the song's own timing, so it lies past that note and short of the next tick at any resolution and tempo. On ordinary charts nothing looks different. Two literals move to their sources too. The time box's text size and margin (15 and 10) are typed into details_view.cpp, while assets/preview/3d-config.json carries the same numbers under `text.time_box` and is never read. They will be read from the config (user decision 11). The font stays Hydra's own monospace font (`g_mono_font`), as the user decided, so the config's `text.time_box.font` key is left unread. The gem sizes in highway_draw.cpp (a ghost is 70% of its lane, a pad gem is half a unit) turn out to be code literals in Onyx itself, not keys in its 3d-config.yml. They become named constants that cite the Onyx source line. What the user sees: the one-tick-late chord draws plain; editing `text.time_box` in the config now changes the time box.

**Goal:** Span membership in the Preview comes from ticks, the time box size and margin come from 3d-config.json, and the gem sizes are named after their Onyx source.

**Files:** Changes src/render/track_state.cpp and track_state.h (tick-based span edges), src/render/preview_config.h and .cpp (a text section), src/ui/preview_controller.h and .cpp (a config accessor), src/ui/details_view.cpp (the time box) and src/render/highway_draw.cpp (named gem constants). Adds and updates cases in tests/test_track_state.cpp, tests/test_highway_draw.cpp and tests/test_preview_config.cpp.

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="build_track_state: a chord one tick after a phrase ends is not SP (480 res, 300 BPM)"` passes.
- [ ] `hydra_tests.exe -tc="build_highway_draws: a chord one tick after a phrase ends draws plain"` passes.
- [ ] `hydra_tests.exe -tc="build_track_state: spans need the song timing"` passes.
- [ ] `hydra_tests.exe -tc="load_preview_config reads the time box size and margin"` passes, and the shipped-file case checks `text.time_box_size == 15` and `text.time_box_margin == 10`.
- [ ] `kSpanEndEpsilonS` no longer exists. `px(15.0f)` and `px(10.0f)` no longer appear in the time box code in details_view.cpp.
- [ ] Every existing track_state and highway_draw case passes. Their expected times (1.0005, 0.2505, 2.0005, 3.0005) are unchanged, because their fixtures run at one tick per millisecond.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*track_state*,*highway*,*preview_config*,PreviewConfig*"` -> `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests for tick spans**

In tests/test_track_state.cpp add `#include <stdexcept>` to the includes. Add this helper inside the anonymous namespace, after `fill()`:

```cpp
// One tick per millisecond (60 BPM at 1000 ticks per beat), matching note()
// and span() above, so half a tick is 0.5 ms and the edges these cases expect
// (1.0005, 0.2505, ...) are the same as before spans moved to ticks.
PreviewScene timed_scene() {
    PreviewScene s;
    s.timing = SongTiming(1000, {{0, 4000}}, {{0, 60.0}});
    s.tick_resolution = 1000;
    return s;
}
```

Then add at the end of the file:

```cpp
TEST_CASE("build_track_state: a chord one tick after a phrase ends is not SP (480 res, 300 BPM)") {
    // One tick here is 0.417 ms, shorter than the old half-millisecond margin.
    SongTiming timing(480, {{0, 1920}}, {{0, 300.0}});
    auto at_tick = [&](int64_t tick, PreviewLane lane) {
        PreviewNote n;
        n.tick = tick;
        n.ms = timing.ms_index().at(tick);
        n.lane = lane;
        return n;
    };
    PreviewScene scene;
    scene.timing = timing;
    scene.tick_resolution = 480;
    scene.notes = {at_tick(0, PreviewLane::Red), at_tick(480, PreviewLane::Yellow),
                   at_tick(481, PreviewLane::Blue)};
    PreviewSpan phrase;
    phrase.start_tick = 0;
    phrase.end_tick = 480;
    phrase.start_ms = timing.ms_index().at(0);
    phrase.end_ms = timing.ms_index().at(480);
    scene.sp_phrases = {phrase};

    TrackState st = build_track_state(scene, TrackStateOptions{});
    const TrackInstant* last_in = find(st.instants(), timing.ms_index().at(480) / 1000.0);
    const TrackInstant* next = find(st.instants(), timing.ms_index().at(481) / 1000.0);
    REQUIRE(last_in);
    REQUIRE(next);
    CHECK(last_in->overdrive == Toggle::On);
    CHECK(next->overdrive == Toggle::Empty);
}

TEST_CASE("build_track_state: spans need the song timing") {
    PreviewScene scene;  // no timing
    scene.notes = {note(1000.0, PreviewLane::Red)};
    scene.sp_phrases = {span(1000.0, 1000.0)};
    CHECK_THROWS_AS(build_track_state(scene, TrackStateOptions{}), std::invalid_argument);

    // A scene with no spans still builds without timing.
    PreviewScene plain;
    plain.notes = {note(1000.0, PreviewLane::Red)};
    CHECK(build_track_state(plain, TrackStateOptions{}).instants().size() == 1);
}
```

In the existing cases that have spans, change `PreviewScene scene;` to `PreviewScene scene = timed_scene();`. Those are "build_track_state: gems, pro-off, and the phrase end note reads inside", "build_track_state: taken, offered and hidden fills toggle different spans", "build_track_state: beats land on instants; solo toggles", "window: strict bounds, and a synthesized instant when empty" and "make_toggle_bounds: covers [near, far], merges equal neighbours". In the first of those, change the comment `// Instants: 0.0, 0.5, 1.0, 1.0005 (phrase end + epsilon).` to `// Instants: 0.0, 0.5, 1.0, 1.0005 (phrase end + half a tick).`. In the last, change `// Touching solos: with the end epsilon they overlap, so the second's start` to `// Touching solos: with the half-tick end they overlap, so the second's start`.

In tests/test_highway_draw.cpp add the same `timed_scene()` helper inside its anonymous namespace, after `fill()`, and change `PreviewScene scene;` to `PreviewScene scene = timed_scene();` in the four cases whose scenes have solos, SP phrases or offered/taken fills: the one at line 145 (solos and an offered fill), "build_highway_draws: energy gems inside an SP phrase, tinted floor in an active window", "build_highway_draws: the taken fill lights its lane with the lit target", and the offered-fill case at line 410. Then add at the end:

```cpp
TEST_CASE("build_highway_draws: a chord one tick after a phrase ends draws plain") {
    PreviewConfig cfg;
    SongTiming timing(480, {{0, 1920}}, {{0, 300.0}});
    auto at_tick = [&](int64_t tick, PreviewLane lane) {
        PreviewNote n;
        n.tick = tick;
        n.ms = timing.ms_index().at(tick);
        n.lane = lane;
        return n;
    };
    PreviewScene scene;
    scene.timing = timing;
    scene.tick_resolution = 480;
    scene.notes = {at_tick(480, PreviewLane::Yellow), at_tick(481, PreviewLane::Blue)};
    PreviewSpan phrase;
    phrase.start_tick = 0;
    phrase.end_tick = 480;
    phrase.start_ms = 0.0;
    phrase.end_ms = timing.ms_index().at(480);
    scene.sp_phrases = {phrase};
    TrackState st = build_track_state(scene, TrackStateOptions{});
    std::vector<DrawCommand> cmds = build_highway_draws(st, cfg, 0.0, 1.0);

    int energy = 0, plain_blue = 0;
    for (const DrawCommand& c : cmds) {
        if (c.material.texture == TextureId::BoxEnergy) ++energy;
        if (c.material.texture == TextureId::BoxBlue) ++plain_blue;
    }
    CHECK(energy == 1);      // the yellow on the phrase's last tick
    CHECK(plain_blue == 1);  // the blue one tick later
}
```

- [ ] **Step 2: Run them and watch them fail**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*one tick after*,*spans need*"`. Expected: the two "one tick after" cases fail (`next->overdrive` is `On`, and `energy == 2` with `plain_blue == 0`). "spans need the song timing" fails because nothing throws.

- [ ] **Step 3: Make the span edge a tick edge**

In src/render/track_state.cpp add `#include <stdexcept>` after `#include <map>`. Replace:

```cpp
// Hydra marks an SP phrase (and a fill) by its last note's tick; Onyx's span
// reaches past that note. Half a millisecond keeps the note inside without
// ever reaching the next note.
constexpr double kSpanEndEpsilonS = 0.0005;
```

with:

```cpp
// Hydra marks an SP phrase, a solo or a fill by the tick of its last note;
// Onyx's span reaches past that note. The drawn edge sits half a tick after
// the last note: past it, and short of any note on the next tick, at every
// resolution and tempo. (A fixed half millisecond was not: at 480 ticks per
// beat and 300 BPM one tick is 0.417 ms.)
constexpr double kSpanEndTicks = 0.5;
```

Replace:

```cpp
    auto span_iv = [](const PreviewSpan& s, double end_eps) {
        return TrackState::Interval{s_of(s.start_ms), s_of(s.end_ms) + end_eps};
    };
    for (const PreviewSpan& s : scene.sp_phrases) st.overdrive_.push_back(span_iv(s, kSpanEndEpsilonS));
    for (const PreviewSpan& s : scene.solos) st.solo_.push_back(span_iv(s, kSpanEndEpsilonS));
    // A hidden fill is one the game never showed, so it draws nothing.
    for (const app::PreviewFill& f : scene.fills) {
        if (f.state == app::PreviewFillState::Offered)
            st.fill_.push_back(span_iv(f.span, kSpanEndEpsilonS));
        else if (f.state == app::PreviewFillState::Taken)
            st.fill_taken_.push_back(span_iv(f.span, kSpanEndEpsilonS));
    }
```

with:

```cpp
    // The end edge comes from the span's end tick through the song's own
    // timing (the display-only sub-tick lookup), never from its end ms.
    auto span_iv = [&scene](const PreviewSpan& s) {
        if (!scene.timing)
            throw std::invalid_argument("build_track_state: a scene with spans needs its song timing");
        const double end_ms =
            scene.timing->ms_index().ms_at_tick_f(static_cast<double>(s.end_tick) + kSpanEndTicks);
        return TrackState::Interval{s_of(s.start_ms), s_of(end_ms)};
    };
    for (const PreviewSpan& s : scene.sp_phrases) st.overdrive_.push_back(span_iv(s));
    for (const PreviewSpan& s : scene.solos) st.solo_.push_back(span_iv(s));
    // A hidden fill is one the game never showed, so it draws nothing.
    for (const app::PreviewFill& f : scene.fills) {
        if (f.state == app::PreviewFillState::Offered)
            st.fill_.push_back(span_iv(f.span));
        else if (f.state == app::PreviewFillState::Taken)
            st.fill_taken_.push_back(span_iv(f.span));
    }
```

and replace `st.fill_lane_.push_back({span_iv(f.span, kSpanEndEpsilonS), *pad});` with `st.fill_lane_.push_back({span_iv(f.span), *pad});`.

In src/render/track_state.h replace:

```cpp
// Build the timeline from a scene. SP phrases and fills are extended by half a
// millisecond past their last note so that note reads as inside (Hydra marks
// a phrase by its last note; Onyx's phrase extends past it). Active SP
```

with:

```cpp
// Build the timeline from a scene. SP phrases, solos and fills end half a tick
// past their last note (through scene.timing), so that note reads as inside
// and a note on the next tick reads as outside (Hydra marks a phrase by its
// last note; Onyx's phrase extends past it). A scene with spans must carry
// its timing; build_preview_scene always sets it, and a scene with spans but
// no timing throws std::invalid_argument. Active SP
```

(build_preview_scene sets `scene.timing` at src/app/preview_view.cpp line 248, so the renderer's call at src/render/preview_renderer.cpp line 378 always has it.)

- [ ] **Step 4: Run them and watch them pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*track_state*,*highway*,window:*,make_toggle_bounds:*,toggle_at:*"`. Expected: all pass, the old cases with their unchanged expected times included.

- [ ] **Step 5: Commit**

```
git add src/render/track_state.cpp src/render/track_state.h tests/test_track_state.cpp tests/test_highway_draw.cpp
git commit -m "Preview: end spans half a tick past their last note, not half a millisecond" -m "Task: Preview spans by tick, time box and gem sizes from their sources
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Write the failing tests for the time box config**

In tests/test_preview_config.cpp, at the end of `check_is_onyx` (after `check_light(c.gems.light, 0, 1, 0.2f);`), add:

```cpp
    CHECK(c.text.time_box_size == doctest::Approx(15));
    CHECK(c.text.time_box_margin == doctest::Approx(10));
```

and add at the end of the file:

```cpp
TEST_CASE("load_preview_config reads the time box size and margin") {
    PreviewConfig c =
        load_preview_config(R"({"text": {"time_box": {"font": "x.ttf", "size": 20, "margin": 12}}})");
    CHECK(c.text.time_box_size == doctest::Approx(20));
    CHECK(c.text.time_box_margin == doctest::Approx(12));
}
```

- [ ] **Step 7: Run them and watch them fail**

Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error, `'text': is not a member of 'hydra::render::PreviewConfig'`.

- [ ] **Step 8: Read text.time_box**

In src/render/preview_config.h, after the `} gems;` line, add:

```cpp
    // Onyx's text.time_box. Hydra draws the box in its own monospace font,
    // so the file's `font` key is not read; size and margin are.
    struct Text {
        float time_box_size = 15;
        float time_box_margin = 10;
    } text;
```

In src/render/preview_config.cpp, in load_preview_config, after `get_light(gems, "light", c.gems.light);`, add:

```cpp
    const json& time_box = sub(sub(root, "text"), "time_box");
    get_f(time_box, "size", c.text.time_box_size);
    get_f(time_box, "margin", c.text.time_box_margin);
```

In src/ui/preview_controller.h, after the `time_box()` declaration, add:

```cpp
    // The Preview's look, as read from 3d-config.json by the renderer. Before
    // the first render (no renderer yet) this is the struct's defaults, which
    // are Onyx's values.
    const render::PreviewConfig& preview_config() const;
```

In src/ui/preview_controller.cpp add:

```cpp
const render::PreviewConfig& PreviewController::preview_config() const {
    static const render::PreviewConfig kOnyxDefaults;
    return renderer_ ? renderer_->config() : kOnyxDefaults;
}
```

In src/ui/details_view.cpp replace:

```cpp
        const float size = px(15.0f);
        const float margin = px(10.0f);
```

with:

```cpp
        const render::PreviewConfig& pcfg = pc->preview_config();
        const float size = px(pcfg.text.time_box_size);
        const float margin = px(pcfg.text.time_box_margin);
```

- [ ] **Step 9: Run them and watch them pass**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*preview_config*,PreviewConfig*,the shipped 3d-config.json*"`. Expected: all pass. Then run `.\build_cpp.ps1 -Target hydra` to confirm the GUI compiles. The time box looks the same, since the shipped values are 15 and 10.

- [ ] **Step 10: Commit**

```
git add src/render/preview_config.h src/render/preview_config.cpp src/ui/preview_controller.h src/ui/preview_controller.cpp src/ui/details_view.cpp tests/test_preview_config.cpp
git commit -m "Preview: read the time box size and margin from 3d-config.json" -m "Task: Preview spans by tick, time box and gem sizes from their sources
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 11: Name the gem sizes after their Onyx source**

This is a rename with no change in behaviour, so there is no new failing test. The existing highway cases guard it. The numbers come from Onyx's `drawDrumPlay`, in `drawGem` (mtolly/onyx, haskell/packages/onyx-lib-game/src/Onyx/Game/Graphics.hs, lines 660-668 at commit 84d5e51): `adjustX v = xCenter + (v - xCenter) * 0.7` for ghosts, and `reference = 0.5 / 2` for every non-kick gem. They are code literals there, not keys in 3d-config.yml, so they belong beside the draw code, not in PreviewConfig. In src/render/highway_draw.cpp, inside the anonymous namespace near the top (next to `toggle_on`), add:

```cpp
// Gem sizes from Onyx's drawDrumPlay / drawGem (mtolly/onyx,
// haskell/packages/onyx-lib-game/src/Onyx/Game/Graphics.hs, lines 660-668 at
// commit 84d5e51). They are literals in Onyx's code, not keys in its
// 3d-config.yml, so they live here rather than in PreviewConfig.
constexpr float kGhostWidthScale = 0.7f;        // Onyx: (v - xCenter) * 0.7
constexpr float kPadGemHalfSize = 0.5f / 2.0f;  // Onyx: reference = 0.5 / 2
```

Replace:

```cpp
            if (g.velocity == Velocity::Ghost) {
                const float cx = x1 + (x2 - x1) * 0.5f;
                x1 = cx + (x1 - cx) * 0.7f;
                x2 = cx + (x2 - cx) * 0.7f;
            }
            const float ref = g.kick ? (x2 - x1) * 0.5f : 0.5f * 0.5f;
```

with:

```cpp
            if (g.velocity == Velocity::Ghost) {
                const float cx = x1 + (x2 - x1) * 0.5f;
                x1 = cx + (x1 - cx) * kGhostWidthScale;
                x2 = cx + (x2 - cx) * kGhostWidthScale;
            }
            // A kick's box is as deep and tall as half its width (Onyx:
            // (x2' - x1') / 2); a pad gem's is fixed.
            const float ref = g.kick ? (x2 - x1) * 0.5f : kPadGemHalfSize;
```

(The remaining `0.5f` factors are "the midpoint" and "half the width", not tuning numbers.)

- [ ] **Step 12: Run the suite**

Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `[doctest] Status: SUCCESS!`

- [ ] **Step 13: Commit**

```
git add src/render/highway_draw.cpp
git commit -m "Preview: name the gem sizes and cite their Onyx source" -m "Task: Preview spans by tick, time box and gem sizes from their sources
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15: Tools measure what they claim

Three developer tools no longer do what they say they do.

First, hydra_bench claims to time "the GUI default", but it builds its own settings. It runs Auto with no time budget, while the GUI default is SP cap 4. It types the chart-mode string by hand and counts paths with its own walk. Its folder mode also never reads `--rules`: Task 1 put that flag in the `--scan` loop only, while the loader calls it changes sit in folder mode and corpus mode.

Second, ch_probe's predicted hit window never appears. The engine hands back keys like `normal_c1`, the probe looks for `c1`, finds nothing, and silently drops the prediction. The same tool assumes the exponent is 2.0 in three places, although its own constants file says never to assume that. It also judges every probe against the normal 85 ms edge, even in precision mode.

Third, the `paths_json` dump that fcvideo reads is built inside tools/replay.cpp, where no test can reach it. The one test that checks it builds its own copy.

After this task, hydra_bench runs the real GUI default under the loaded rules. ch_probe reads every number from one place. The dump has a real test. Nothing in the app changes.

**Goal:** hydra_bench, ch_probe and the hydra_replay dump each use the production definition of what they measure, and a test pins each one.

**Files:** Changes tools/bench.cpp. Moves `paths_json` and `score_json` from tools/replay.cpp into src/core/replay.h and src/core/replay.cpp, and changes tests/test_replay.cpp. Changes tools/ch_probe/constants.py, engine.py, probe_chart.py, experiments/analysis.py, experiments/active_probe.py and experiments/passive_probe.py, plus tests/test_analysis.py.

**Acceptance Criteria:**
- [ ] `hydra_tests -tc="paths_json writes every field the dump readers use"` passes.
- [ ] `hydra_tests -tc="windows read from a path JSON match the ones read from the record"` passes and no longer builds its own JSON.
- [ ] tools/replay.cpp no longer defines `paths_json` or `score_json`. The `hydra_replay dump --out` file is byte-identical before and after on one corpus chart.
- [ ] `hydra_bench <folder>` prints `Settings: the GUI default (SP cap 4, score range 4, 10ms limit).`, and `hydra_bench <folder> --rules bad.ini` (holding `max_tied_paths = 0`) exits with code 2 and names `max_tied_paths`.
- [ ] The ch_probe suite reports `112 passed` (108 today, plus 4 new).
- [ ] `grep -n "2\.0" tools/ch_probe/experiments/analysis.py` shows no exponent default. `grep -n "85\.0" tools/ch_probe/experiments/*.py` shows no match.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="*paths_json*,windows read from a path JSON*"` -> `[doctest] Status: SUCCESS!`, then `C:\Users\Patrick\AppData\Local\Python\pythoncore-3.14-64\python.exe -m pytest tools/ch_probe/tests -q -p no:cacheprovider` -> `112 passed`

**Steps:**

- [ ] **Step 1: Save a before-copy of one dump, for the byte-identical check later.**

`dump` needs a database file that exists. It copies the file to a snapshot, and throws "cannot read database" when the source is missing. An empty 0-byte file works: SQLite treats it as an empty database, so dump finds no stored row and analyzes the chart fresh. `--out` matters too. Without it, dump's stdout also carries a "(read from a snapshot at ...)" line whose path holds the process id, so two runs never match.

```powershell
.\build_cpp.ps1 -Target hydra_replay
$chart = (Get-ChildItem testdata\input -Recurse -Filter notes.mid | Select-Object -First 1).FullName
New-Item -ItemType File -Force "$env:TEMP\task15_empty.db" | Out-Null
.\build-cpp\Release\hydra_replay.exe dump --chart $chart --db "$env:TEMP\task15_empty.db" --out "$env:TEMP\task15_dump_before.json"
```

Expected: two lines, `(read from a snapshot at ...)` and `wrote ...task15_dump_before.json (N bytes)`. The file's "source" field reads "analyzed". That is expected.

- [ ] **Step 2: Write a failing test that calls the dump producer directly.**

Add this case to tests/test_replay.cpp, below the "windows read from a path JSON" case. The activation keys include `sqout_tick`, which Task 5 added to the dump.

```cpp
// fcvideo and `hydra_replay score --path` read these fields out of a dump. A
// dump-format change that drops one has to fail here, not in the video tools.
TEST_CASE("paths_json writes every field the dump readers use") {
    const app::AnalysisSettings cfg = app::Settings().to_analysis_settings();

    bool checked = false;
    for (const std::string& chart : corpus::chart_paths()) {
        Song song = load_songpath(chart, cfg.prodrums, cfg.bass2x, cfg.difficulty);
        if (song.is_empty()) continue;
        HydraRecord rec = analyze_chart(song, cfg);
        const std::vector<const Path*> all = rec.all_paths();
        if (all.empty() || all[0]->all_activations().empty()) continue;

        const json dumped = paths_json(all, song.timing());
        REQUIRE(dumped.size() == all.size());

        const json& p0 = dumped[0];
        for (const char* k : {"index", "pathstring", "total", "score", "activations"})
            CHECK_MESSAGE(p0.contains(k), k);
        for (const char* k : {"base", "combo", "sp", "solo", "accent", "ghost"})
            CHECK_MESSAGE(p0["score"].contains(k), k);
        CHECK(p0["pathstring"].get<std::string>() == all[0]->pathstring());

        REQUIRE(!p0["activations"].empty());
        const json& a0 = p0["activations"][0];
        for (const char* k : {"act_tick", "deact_tick", "sqout_tick", "nominal_deact_tick",
                              "sp_meter", "skips", "chord_code", "sqinouts"})
            CHECK_MESSAGE(a0.contains(k), k);

        checked = true;
        break;  // one chart's first path is the whole contract
    }
    CHECK(checked);
}
```

- [ ] **Step 3: Build and watch it fail.**

```powershell
.\build_cpp.ps1 -Target hydra_tests
```

Expected: a compile error in tests/test_replay.cpp saying `paths_json` is not declared. The function only exists inside an anonymous namespace in tools/replay.cpp today.

- [ ] **Step 4: Move `paths_json` and `score_json` into core.**

`score_json` has to move too. After Task 11, `paths_json` builds each path's score with `score_json(score_of(*p))`, so the producer cannot live in core without it.

In src/core/replay.h, next to the existing `std::vector<ReplayWindow> windows_from_json(const nlohmann::json& path);` declaration, add both producers. The writer and the reader of the dump then sit side by side:

```cpp
// One score split as JSON, one key per kReplayScoreFields entry. The dump's
// per-path "score" object and the `score` command's totals both use it.
nlohmann::json score_json(const ReplayScore& s);

// The "paths" array of a hydra_replay dump: one object per path in `all`, in
// order, with the score split and every activation's ticks, SP meter, skips,
// chord and squeezes. windows_from_json reads one element of it back, and
// fcvideo reads the rest.
nlohmann::json paths_json(const std::vector<const Path*>& all, const SongTiming& timing);
```

Cut both function bodies out of the anonymous namespace in tools/replay.cpp: `json score_json(const ReplayScore& s)` and `json paths_json(const std::vector<const Path*>& all, const SongTiming& timing)`. Today they are lines 164 to 215. By now they carry Task 5's `sqout_tick` line and Task 11's field loop, so move them as they are at that point. Paste them into src/core/replay.cpp inside `namespace hydra`, after `windows_from_json`. Spell the type `nlohmann::json` where the tool used its `json` alias. src/core/replay.cpp already includes core/timing.h for `SongTiming::plusmeasure`.

The callers in tools/replay.cpp keep compiling unchanged. Those are the three `score_json(...)` calls in the score command and the two `paths_json(...)` calls in `emit_dump` and the target command. tools/replay.cpp includes core/replay.h and has `using namespace hydra;`. Delete the tool's copies completely: a same-named function left in the anonymous namespace would make every call ambiguous.

- [ ] **Step 5: Point the old test at the real producer.**

In tests/test_replay.cpp, the "windows read from a path JSON match the ones read from the record" case builds its own copy of the dump. Today that is lines 243 to 262. Task 5 added a `{"sqout_tick", ...}` line to the hand-built acts; the whole hand-built block goes anyway. The block starts at:

```cpp
        for (const Path* p : rec.all_paths()) {
            const std::vector<ReplayWindow> want = windows_for_path(*p, song);
            if (want.empty()) continue;

            // The part of dump's JSON that windows_from_json reads, built the
            // way tools/replay.cpp's paths_json builds it.
            json acts = json::array();
```

It runs through:

```cpp
            const std::vector<ReplayWindow> got =
                windows_from_json(json{{"activations", acts}});
```

Replace everything from the `for` line through that `got` statement with:

```cpp
        const std::vector<const Path*> all = rec.all_paths();
        const json dumped = paths_json(all, song.timing());
        REQUIRE(dumped.size() == all.size());
        for (size_t k = 0; k < all.size(); ++k) {
            const Path* p = all[k];
            const std::vector<ReplayWindow> want = windows_for_path(*p, song);
            if (want.empty()) continue;

            // The real dump producer, so a format change on either side fails.
            const std::vector<ReplayWindow> got = windows_from_json(dumped[k]);
```

The rest of the loop body stays as it is. That covers the size check, the per-window checks (including the `CHECK(got[i].sqout_tick == want[i].sqout_tick);` line Task 5 added) and `++checked`.

- [ ] **Step 6: Build and run both cases.**

```powershell
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe -tc="*paths_json*,windows read from a path JSON*"
```

Expected: `[doctest] Status: SUCCESS!`.

- [ ] **Step 7: Check the dump did not change by a byte.**

```powershell
.\build_cpp.ps1 -Target hydra_replay
.\build-cpp\Release\hydra_replay.exe dump --chart $chart --db "$env:TEMP\task15_empty.db" --out "$env:TEMP\task15_dump_after.json"
fc.exe /b "$env:TEMP\task15_dump_before.json" "$env:TEMP\task15_dump_after.json"
```

Expected: `FC: no differences encountered`. The empty database file is still 0 bytes, because dump only ever opens its snapshot.

- [ ] **Step 8: Commit the dump move.**

```powershell
git add src/core/replay.h src/core/replay.cpp tools/replay.cpp tests/test_replay.cpp
git commit -m "Move the replay dump producer into core so a test can reach it

tests/test_replay.cpp built its own copy of paths_json, so a dump-format
change could break fcvideo with no failing test (audit finding 31).
score_json moves with it because paths_json uses it.

Task: Tools measure what they claim
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 9: Make hydra_bench run the real GUI default under the loaded rules.**

hydra_bench has no unit test. Its check is its own output in Step 10. Tasks 1 and 2 already edited this file: `load_songpath` calls gained a rules argument, `SearchSettings` gained `rules`, and the store opens pass a fingerprint. If an earlier task already changed any line quoted below, apply the same change to the new line.

In tools/bench.cpp, add `#include "app/config.h"` after `#include "app/analysis.h"` (line 26). Also add `#include "app/rules_file.h"` and `#include <filesystem>` if Task 1 did not add them.

Delete `count_paths` (lines 39 to 50). `HydraRecord::all_paths()` already walks paths and variants the way the Details view counts them.

Replace the header comment's lines 6 to 9:

```cpp
// With a folder argument it discovers the folder's charts and prints a per-
// chart breakdown at the GUI's UNCAPPED DEFAULT settings (score range 4, 10ms
// limit) -- parse, search, and DB store timed separately, plus the capped
// number for reference:
```

with:

```cpp
// With a folder argument (and an optional --rules <path>) it discovers the
// folder's charts and prints a per-chart breakdown at the GUI's default
// settings, taken from app::Settings so the two cannot drift -- parse,
// search, and DB store timed separately:
```

Replace the comment at lines 52 and 53 and the signature at line 54 with:

```cpp
// Folder mode: the real GUI default (app::Settings) under `rules`, broken into
// the phases the app actually pays, per chart.
static void folder_breakdown(const std::string& folder, const core::Rules& rules) {
```

Replace line 58's printf with:

```cpp
    app::Settings gui;  // struct defaults are the GUI defaults
    gui.rules = rules;
    const app::AnalysisSettings settings = gui.to_analysis_settings();
    std::printf("Settings: the GUI default (SP cap %d, score range %d, %dms limit).\n\n",
                gui.sp_cap.value_or(-1), gui.depth_value, gui.mslimit_value);
```

The store open at line 60 now compiles as Task 2 wrote it, `store::RecordStore store(":memory:", rules.fingerprint());`, because `rules` is a parameter.

Replace the load at line 68 (by now `load_songpath(it.notespath, true, true, rules)` or similar) with:

```cpp
            song_opt.emplace(load_songpath(it.notespath, settings.prodrums, settings.bass2x,
                                           settings.difficulty, settings.rules));
```

Replace lines 76 to 84 with the following. That is the "Uncapped ladder" comment and the hand-built `SearchSettings settings;` block, including any `settings.rules = rules;` line Task 1 added, ending in `HydraRecord rec = analyze_chart(song, settings);`.

```cpp
        t = clk::now();
        HydraRecord rec = analyze_chart(song, settings);
```

Replace lines 89 to 91 (the `store::RecordKey{it.md5, "Expert Pro Drums, 2x Bass", store::CapQuery::automatic()}` key) with:

```cpp
        store.add_record(gui.record_key(it.md5), rec);
```

Replace `count_paths(rec)` at line 98 with `static_cast<int>(rec.all_paths().size())`.

In `main`, the folder branch (lines 258 and 259, `} else if (argc > 1) { folder_breakdown(argv[1]);`) becomes the block below. If Task 1 already loads the rules somewhere `main` can reach for this branch, keep its load and only pass `rules` on.

```cpp
    } else if (argc > 1) {
        std::string rulespath;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--rules" && i + 1 < argc) rulespath = argv[++i];
        }
        core::Rules rules;
        try {
            rules = app::load_rules_file(rulespath.empty()
                                             ? app::default_rules_path()
                                             : std::filesystem::path(rulespath));
        } catch (const app::RulesFileError& e) {
            std::fprintf(stderr, "%s\n", e.what());
            return 2;
        }
        folder_breakdown(argv[1], rules);
```

The `corpus_bench` configs ("cap4 d4", "auto d4") stay. They are named benchmark configs, not claims about the GUI.

- [ ] **Step 10: Build hydra_bench and check the header line and the rules error.**

```powershell
.\build_cpp.ps1 -Target hydra_bench
.\build-cpp\Release\hydra_bench.exe "testdata\input\common\Summer Blast _25 Setlist\Tier 2\Thornhill - Limbo"
Set-Content -Encoding utf8 "$env:TEMP\task15_bad_rules.ini" "max_tied_paths = 0"
.\build-cpp\Release\hydra_bench.exe "testdata\input\common\Summer Blast _25 Setlist\Tier 2\Thornhill - Limbo" --rules "$env:TEMP\task15_bad_rules.ini"; $LASTEXITCODE
```

Expected: the first run's second line reads `Settings: the GUI default (SP cap 4, score range 4, 10ms limit).`, and the chart prints `sp_cap 4`. The second run prints an error containing `max_tied_paths`, then `2`.

- [ ] **Step 11: Commit hydra_bench.**

```powershell
git add tools/bench.cpp
git commit -m "hydra_bench runs the GUI default from app::Settings

It claimed the GUI default but ran Auto with no budget, typed the chart
mode by hand and counted paths itself (audit findings 14 and 31). Folder
mode now reads --rules like the other tools.

Task: Tools measure what they claim
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 12: Write failing ch_probe tests.**

Add to tools/ch_probe/tests/test_analysis.py, after `TestSummarizeActive`:

```python
from tools.ch_probe import constants as C
from tools.ch_probe import probe_chart
from tools.ch_probe.engine import EngineModel


class _ConstProcess:
    """Just enough of a ProcessHandle for EngineModel.constants(): every RVA
    reads back as a distinct float, so a mis-mapped key shows up."""

    def read_const_double(self, rva):
        return float(rva % 100003) + 0.25


class TestNormalFormulaConstants(unittest.TestCase):
    def test_maps_the_real_engine_shape(self):
        decoded = EngineModel(_ConstProcess(), None).constants()
        got = analysis.normal_formula_constants(decoded)
        self.assertIsNotNone(got)
        for name in C.RVA_FORMULA_NORMAL:
            self.assertEqual(got[name], decoded[C.CONST_KEY_PREFIX_NORMAL + name])
        self.assertEqual(got["divisor"], decoded[C.CONST_KEY_DIVISOR])
        self.assertEqual(got["exponent"], decoded[C.CONST_KEY_EXPONENT])

    def test_missing_exponent_gives_no_prediction(self):
        decoded = EngineModel(_ConstProcess(), None).constants()
        del decoded[C.CONST_KEY_EXPONENT]
        self.assertIsNone(analysis.normal_formula_constants(decoded))


class TestProbeSettingsHaveOneHome(unittest.TestCase):
    def test_clamp_verdict_needs_an_explicit_edge(self):
        with self.assertRaises(TypeError):
            analysis.clamp_verdict([])

    def test_probe_chart_spacings_and_lane_come_from_constants(self):
        self.assertEqual(tuple(probe_chart.DEFAULT_SPACINGS_MS), tuple(C.PROBE_SPACINGS_MS))
        self.assertEqual(probe_chart.DRUM_LANE_KICK, C.PROBE_LANE_KICK)
```

- [ ] **Step 13: Run them and watch them fail.**

```powershell
C:\Users\Patrick\AppData\Local\Python\pythoncore-3.14-64\python.exe -m pytest tools/ch_probe/tests -q -p no:cacheprovider
```

Expected: 4 failures. Three are `AttributeError`s, for `normal_formula_constants`, `CONST_KEY_PREFIX_NORMAL` and `PROBE_SPACINGS_MS`. The fourth fails because `clamp_verdict([])` raises no `TypeError`.

- [ ] **Step 14: Give the key names, spacings and lane one home in constants.py.**

Append to tools/ch_probe/constants.py:

```python
# ---- keys of EngineModel.constants() ------------------------------------
# engine.py writes these and experiments/analysis.py reads them, so both
# sides spell each key from here.
CONST_KEY_DIVISOR = "divisor"
CONST_KEY_EXPONENT = "exponent"
CONST_KEY_PREFIX_NORMAL = "normal_"
CONST_KEY_PREFIX_PRECISION = "precision_"

# ---- probe chart layout -------------------------------------------------
# Note-pair spacings the probe chart lays out, and the lane it uses (the kick,
# lane 0). probe_chart.py and experiments/active_probe.py both read these.
```

Then move the tuple literal from experiments/active_probe.py lines 70 to 74 into constants.py verbatim, renamed `PROBE_SPACINGS_MS`. That is `DEFAULT_SPACINGS_MS: Tuple[float, ...] = (` and its values. Add `PROBE_LANE_KICK = 0` under it. If constants.py does not import `Tuple`, write the annotation as `tuple[float, ...]`.

- [ ] **Step 15: Make engine.py spell its keys from constants.**

In `EngineModel.constants()` (engine.py lines 162, 163, 169 and 172), make four replacements. `"divisor"` becomes `C.CONST_KEY_DIVISOR`. `"exponent"` becomes `C.CONST_KEY_EXPONENT`. `"normal_" + name` becomes `C.CONST_KEY_PREFIX_NORMAL + name`. `"precision_" + name` becomes `C.CONST_KEY_PREFIX_PRECISION + name`.

- [ ] **Step 16: Add the mapping helper and drop the exponent defaults in analysis.py.**

Add near the top of experiments/analysis.py, after the existing imports:

```python
from tools.ch_probe import constants as C
```

Add this function above `summarize_active`:

```python
def normal_formula_constants(decoded: Optional[dict]) -> Optional[dict]:
    """Map EngineModel.constants() output onto predicted_window_normal's
    inputs (c1..c4, divisor, exponent). Returns None when any of them is
    missing -- the exponent included, because the game's exponent is read
    live and never assumed."""
    if not decoded:
        return None
    needed = [C.CONST_KEY_PREFIX_NORMAL + n for n in C.RVA_FORMULA_NORMAL]
    needed += [C.CONST_KEY_DIVISOR, C.CONST_KEY_EXPONENT]
    if any(k not in decoded for k in needed):
        return None
    out = {n: float(decoded[C.CONST_KEY_PREFIX_NORMAL + n]) for n in C.RVA_FORMULA_NORMAL}
    out["divisor"] = float(decoded[C.CONST_KEY_DIVISOR])
    out["exponent"] = float(decoded[C.CONST_KEY_EXPONENT])
    return out
```

If `Optional` is not already imported from `typing` in analysis.py, add it to that import.

Change three signatures and one lookup. `predicted_window_normal(..., exponent: float = 2.0)` at line 209 becomes `exponent: float`, still keyword-only. `predicted_window_precision(..., exponent: float = 2.0)` at line 236 gets the same change. `clamp_verdict(rows, *, cap_ms: float = 85.0, ...)` at line 151 becomes `clamp_verdict(rows, *, cap_ms: float, ...)`. In `summarize_active`, line 312's `formula_constants.get("exponent", 2.0)` becomes `formula_constants["exponent"]`.

- [ ] **Step 17: Move the callers onto the shared definitions.**

In experiments/active_probe.py, delete `_decode_formula_constants` (lines 221 to 234). Line 170 becomes `formula_constants = analysis.normal_formula_constants(engine.constants())`. Delete the local `DEFAULT_SPACINGS_MS` tuple and `PROBE_LANE = 0`. Replace them with `DEFAULT_SPACINGS_MS = constants.PROBE_SPACINGS_MS` and `PROBE_LANE = constants.PROBE_LANE_KICK`. active_probe.py already imports `constants` from tools.ch_probe.

In tools/ch_probe/probe_chart.py, import constants the way engine.py does:

```python
try:
    from . import constants as C
except ImportError:
    import constants as C
```

Line 25's `DEFAULT_SPACINGS_MS = [...]` becomes `DEFAULT_SPACINGS_MS = list(C.PROBE_SPACINGS_MS)`. Line 32's `DRUM_LANE_KICK = 0` becomes `DRUM_LANE_KICK = C.PROBE_LANE_KICK`.

In experiments/passive_probe.py, add `from tools.ch_probe import constants` if it is not already imported. Line 140's `verdict = analysis.clamp_verdict(rows)` becomes:

```python
    # Judge against the edge of the mode actually being probed: precision
    # mode's back window is 40 ms, not normal mode's 85.
    cap_ms = (constants.EXPECT_PRECISION_BACK_MS if engine.precision_mode()
              else constants.EXPECT_NORMAL_BACK_MS)
    verdict = analysis.clamp_verdict(rows, cap_ms=cap_ms)
```

- [ ] **Step 18: Fix the existing tests that relied on the defaults.**

In tools/ch_probe/tests/test_analysis.py, the two `TestParabolaPredictor` calls at lines 133 to 135 and 149 to 151 gain `exponent=2.0`. Every existing `analysis.clamp_verdict(...)` call in the file gains `cap_ms=C.EXPECT_NORMAL_BACK_MS`. The `TestSummarizeActive` input at lines 172 to 175 stays in the predictor's own shape, because `summarize_active` still takes c1..c4 keys. `normal_formula_constants` is what converts the engine's shape now.

- [ ] **Step 19: Run the suite.**

```powershell
C:\Users\Patrick\AppData\Local\Python\pythoncore-3.14-64\python.exe -m pytest tools/ch_probe/tests -q -p no:cacheprovider
```

Expected: `112 passed`.

- [ ] **Step 20: Commit ch_probe.**

```powershell
git add tools/ch_probe/constants.py tools/ch_probe/engine.py tools/ch_probe/probe_chart.py tools/ch_probe/experiments/analysis.py tools/ch_probe/experiments/active_probe.py tools/ch_probe/experiments/passive_probe.py tools/ch_probe/tests/test_analysis.py
git commit -m "ch_probe reads its keys, exponent, window edge and spacings from one place

The engine wrote normal_c1 while the probe looked for c1, so the predicted
window never appeared (audit finding 15). The exponent is now required, the
passive verdict uses the probed mode's back edge, and spacings and lane live
in constants.py (audit finding 31).

Task: Tools measure what they claim
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 16: User Guide matches the app

The User Guide is wrong in four places. It says squeezes count "unless they're 2ms or less", but the engine's edge is 3 ms. It says only Expert is supported, but the app has a difficulty dropdown with all four difficulties. It mentions "the other edition", which no longer exists. And it lists hydra_batch's flags without `--legacy-fills`. The new `hydra_rules.ini` file also needs a section, so a user can find every rule they can change. After this task the guide matches the app. Nothing in the app changes.

This task runs after Task 1, which creates `hydra_rules.ini` loading, `default_rules_path()` and the `--rules` flag on all five tools, hydra_report included. hydra_report needs the flag because it opens the store with the rules' fingerprint (Task 2). The guide describes Task 1's file format. The file sits next to Hydra.exe. It holds flat `key = value` lines with no section header, and `#` starts a comment. The keys are the `core::Rules` field names. There are nine of them, because Task 1 adds `fill_length_measures` beside the eight rules decision 13 names.

**Goal:** Every sentence in docs/UserGuide.md that the audit found wrong is corrected, and the guide documents hydra_rules.ini.

**Files:** Changes docs/UserGuide.md only.

**Acceptance Criteria:**
- [ ] `Select-String docs\UserGuide.md -Pattern '2ms','Only Expert difficulty','other edition','\[rules\]'` prints nothing.
- [ ] `Select-String docs\UserGuide.md -Pattern '--legacy-fills','--rules','hydra_rules.ini'` finds each one.
- [ ] Every key `load_rules_file` accepts appears once in the new section, with the default `core::default_rules()` returns. That is nine keys.

**Verify:** `Select-String docs\UserGuide.md -Pattern '2ms','Only Expert difficulty','other edition','\[rules\]' | Measure-Object | % Count` -> `0`

**Steps:**

- [ ] **Step 1: Run the check and watch it fail.**

```powershell
Select-String docs\UserGuide.md -Pattern '2ms','Only Expert difficulty','other edition','\[rules\]' | Measure-Object | % Count
```

Expected: `3` (lines 42, 61 and 164).

- [ ] **Step 2: Fix the difficulty paragraph.**

Line 42 today:

```markdown
Path/scoring analysis depends on difficulty, whether it's Pro Drums, and whether 2x bass is enabled. When you analyze a song, that analysis result is for that particular combination of options and it'll only be visible when that combination is selected. (Only Expert difficulty is supported right now, so it's shown as a fixed label.)
```

Replace it with:

```markdown
Path/scoring analysis depends on difficulty, whether it's Pro Drums, and whether 2x bass is enabled. When you analyze a song, that analysis result is for that particular combination of options and it'll only be visible when that combination is selected. Pick the difficulty from the dropdown: Expert, Hard, Medium or Easy. 2x Bass only exists on Expert, so it is greyed out on the other three. The dmleaderboards comparison also needs Expert, because the leaderboard only carries Expert scores.
```

The heading on line 41, `### View Options (Pro Drums / 2x Bass)`, becomes `### View Options (Difficulty / Pro Drums / 2x Bass)`.

- [ ] **Step 3: Fix the Stale line.**

Line 61 today:

```markdown
- **`(Stale)`** — analyzed by an older Hydra version (or the other edition); re-analyze to refresh it.
```

Replace it with:

```markdown
- **`(Stale)`** — analyzed by an older Hydra version, or under different rules in `hydra_rules.ini`; re-analyze to refresh it. Switching the rules back brings the old results back.
```

- [ ] **Step 4: Fix the double-squeeze sentence.**

Line 164 today:

```markdown
Double squeezes are currently not considered in this scoring unless they're `2ms` or less, there's a slight margin.
```

Replace it with:

```markdown
Double squeezes only count in this score when the backend note lands inside a small leeway past the Star Power end. The leeway is `3ms` by default. You can change it with `backend_leeway_ms` in `hydra_rules.ini` (see below).
```

- [ ] **Step 5: Fix the command line section.**

Lines 180 and 183 today:

```markdown
- **`hydra_batch`** — runs the same batch analysis as `Analyze library`, printing one line per chart. Flags: `--redo` (re-analyze existing results), `--reindex`, `--db <path>`.
...
Both use the chart mode and SP cap from the app's settings file.
```

Replace line 180 with:

```markdown
- **`hydra_batch`** — runs the same batch analysis as `Analyze library`, printing one line per chart. Flags: `--redo` (re-analyze existing results), `--reindex`, `--db <path>`, `--rules <path>`, `--legacy-fills`. `--legacy-fills` prices charts under Clone Hero 1.0's fill rule instead of 1.1's. It is a command-line-only mode, and it refuses to write the app's own database, so give it its own `--db`.
```

Line 181 gains `--rules <path>` at the end of hydra_report's flag list. Replace line 183 with:

```markdown
Both use the chart mode and SP cap from the app's settings file. Both read the scoring rules from `hydra_rules.ini` next to Hydra.exe, or from the file `--rules` names. If that file has an error, they print it and stop with exit code 2.
```

- [ ] **Step 6: Add the hydra_rules.ini section.**

Insert this section right before `## Command line tools` (line 176). The outer fence below uses four backticks only so this plan can show the inner `ini` block. The guide itself gets the text between them.

````markdown
## Scoring rules (`hydra_rules.ini`)

A few of Hydra's rules are judgment calls, not facts read from Clone Hero. You can change them in `hydra_rules.ini`, a plain text file next to Hydra.exe. The app and every command line tool read the same file.

The file is optional. A missing file, or a missing line, means the default below. Each line is `key = value`. A line starting with `#` is a comment. There are no `[section]` headers.

```ini
# Hydra's defaults
backend_leeway_ms = 3.0
sqout_rule = first_note
max_tied_paths = 4
auto_cap_ladder = 16,32,64,128,256,512
auto_budget_s = 120
fill_cooldown_measures = 4
fill_max_distance_beats = 0.5
fill_length_measures = 0.5
fill_land_slop_beats = 0.03125
```

What each line does:

- **`backend_leeway_ms`** (default `3.0`): how far past the Star Power end a backend note can land and still count in the score.
- **`sqout_rule`** (default `first_note`): what a squeeze-out costs. `first_note` removes the Star Power doubling from one note of the chord (the lowest-value one). `whole_chord` removes it from every note in the chord.
- **`max_tied_paths`** (default `4`): how many paths Hydra keeps when several reach the same score. More paths means longer lists and slower analysis.
- **`auto_cap_ladder`** (default `16,32,64,128,256,512`): the SP caps Auto tries, in rising order, until the score stops changing. Separate them with commas.
- **`auto_budget_s`** (default `120`): how many seconds Auto may spend on one chart before it stops climbing the ladder.
- **`fill_cooldown_measures`** (default `4`): for charts with no authored fills, how many measures must pass after an activation point before Hydra places the next one.
- **`fill_max_distance_beats`** (default `0.5`): for charts with no authored fills, how far from a measure line a note can sit and still get a fill.
- **`fill_length_measures`** (default `0.5`): for charts with no authored fills, how long each fill Hydra places is, in measures.
- **`fill_land_slop_beats`** (default `0.03125`, a 32nd of a beat): how close a fill's end must be to a note for the fill to count. This one applies to authored fills too.

A value Hydra can't read, or a key it doesn't know, is an error that names the key. The command line tools print the error and stop with exit code 2. The app still opens and shows the error, but Analyze stays off until you fix the file and restart Hydra. Hydra never analyzes on the defaults behind your back.

Every analysis result remembers the rules it was made with. After you change the file, results made under the old rules show **`(Stale)`** until you re-analyze them. Switching the rules back brings those results back.
````

- [ ] **Step 7: Run the check.**

```powershell
Select-String docs\UserGuide.md -Pattern '2ms','Only Expert difficulty','other edition','\[rules\]' | Measure-Object | % Count
Select-String docs\UserGuide.md -Pattern '--legacy-fills','--rules','hydra_rules.ini' | Measure-Object | % Count
```

Expected: `0`, then a number of at least `3`. Then open src/app/rules_file.cpp. Confirm that each key `load_rules_file` parses appears once in the new section, with the same default as `core::default_rules()` in src/core/rules.h. There should be nine.

- [ ] **Step 8: Commit.**

```powershell
git add docs/UserGuide.md
git commit -m "User Guide: 3 ms leeway, difficulty dropdown, --legacy-fills, hydra_rules.ini

Fixes the three wrong sentences from audit findings 8-10 and documents
every key of the new rules file, its error handling and the Stale rule.

Task: User Guide matches the app
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 17: Preview honors song.ini delay and .chart Offset

**USER GATE.** The executor stops at Step 1 and writes no code until the user answers.

The Preview assumes the chart's time 0 is the audio's time 0. Many charts say otherwise. A song.ini `delay` (in milliseconds) or a .chart `Offset` (in seconds) moves the notes against the music. TEXTURES "Laments Of An Icarus" has delay 689 and Thornhill "Limbo" has delay 1016. On those charts the highway runs out of sync with the song today. After this task the Preview shifts the audio by the chart's offset, so notes line up with the music the way they do in Clone Hero. Charts with no offset don't change.

Nobody has checked which way the shift goes, or which value wins when a chart has both. The Clone Hero wiki says a positive `delay` "will make the chart start later". scan-chart says nothing about precedence. So Step 1 asks the user to check three things at the game. The first is Thornhill "Limbo", which has a nonzero delay (1016). It shows which way the shift goes. The second is Moonlight Haze "Lunaris" (notes.chart `Offset = 0.25`, song.ini `delay = 0`). It shows whether a delay of 0 still counts as "set". The third is a copy of Lunaris with `delay = 500`. It has both values nonzero, so it settles which rule Clone Hero uses: the two add (0.75 s), the delay wins (0.5 s), or the Offset wins (0.25 s). The executor makes that copy with PowerShell once the user gives their songs folder path. Only after all three answers does the executor write code.

Only loose charts are in scope: a song.ini beside a notes.mid or notes.chart, and the .chart `Offset`. A `delay` inside a .sng or .srb container is out of scope.

**Goal:** The Preview plays audio at `chart_ms + offset_ms`, with the offset taken from song.ini `delay` or .chart `Offset` the way Clone Hero applies them.

> **USER-ORDERED GATE — NON-SKIPPABLE.** This task was requested by the user in the current conversation. It MUST NOT be closed by walking around it, by declaring it "verified inline", or by substituting a cheaper check. Close only after every item in `acceptanceCriteria` has been re-validated independently, with output captured.

**Files:** Changes src/parse/song.h and song.cpp (read `[Song] Offset`). Changes src/app/analysis.h and analysis.cpp (one shared song.ini reader). Changes src/app/preview_source.h and preview_source.cpp (find song.ini with `is_song_ini`, read `delay`, carry `audio_offset_ms`). Changes src/audio/mixer.h and mixer.cpp (a front-padding helper). Changes src/ui/preview_load_job.h and .cpp, src/ui/preview_transport.h and .cpp, and src/ui/preview_controller.cpp. Adds cases to tests/test_song.cpp, tests/test_preview_source.cpp and tests/test_preview_transport.cpp.

**Acceptance Criteria:**
- [ ] The user has answered all three Step 1 checks: which way Limbo moves, whether Lunaris is shifted, and whether the delay-500 copy moved by 0.25 s, 0.5 s or 0.75 s. The answers are consistent (Step 1's table), and Step 7 uses the matching body.
- [ ] `hydra_tests -tc="*Offset*,*delay*,*audio offset*,pad_front_ms*"` passes.
- [ ] Every existing case in tests/test_preview_transport.cpp still passes unchanged, and the full suite passes.
- [ ] On Thornhill "Limbo" and Moonlight Haze "Lunaris", the user confirms by ear that the Preview's notes land on the music.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="*Offset*,*delay*,*audio offset*,pad_front_ms*,*transport*"` -> `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: USER GATE. Ask the user to check three charts at the game, then stop.**

The executor writes no code in this step. First it asks one question and waits:

> To settle how Clone Hero combines `delay` and `Offset`, I'd like to make a test copy of Lunaris in your Clone Hero songs folder. What is that folder's full path? I will only add a new folder. Your original Lunaris folder stays untouched.

The executor never guesses the path. When the user answers, it runs this, with `$songs` set to the path the user gave. The script finds the user's Lunaris folder and copies it to a new folder. It never writes into the original. In the copy's song.ini it sets `delay = 500`, and it adds " (delay 500 test)" to the song name so the copy is easy to find in Clone Hero.

```powershell
$songs = 'D:\path\the\user\gave'   # replace with the user's answer, exactly
$found = @(Get-ChildItem -LiteralPath $songs -Recurse -Directory |
    Where-Object { $_.Name -like '*Lunaris*' -and
                   (Test-Path -LiteralPath (Join-Path $_.FullName 'notes.chart')) })
$dst = Join-Path $songs 'Hydra delay test - Lunaris delay 500'
if ($found.Count -ne 1) {
    "Found $($found.Count) Lunaris folders; ask the user which one:"
    $found.FullName
} elseif (Test-Path -LiteralPath $dst) {
    "The test folder already exists: $dst"
} else {
    Copy-Item -LiteralPath $found[0].FullName -Destination $dst -Recurse
    $ini = Get-ChildItem -LiteralPath $dst -File | Where-Object { $_.Name -ieq 'song.ini' }
    $text = Get-Content -LiteralPath $ini.FullName -Raw
    if ($text -match '(?im)^[ \t]*delay[ \t]*=') {
        $text = $text -replace '(?im)^[ \t]*delay[ \t]*=[^\r\n]*', 'delay = 500'
    } else {
        $text = $text -replace '(?im)^[ \t]*\[song\][ \t]*$', "`$0`r`ndelay = 500"
    }
    $text = $text -replace '(?im)^([ \t]*name[ \t]*=[ \t]*)([^\r\n]*)', '${1}${2} (delay 500 test)'
    Set-Content -LiteralPath $ini.FullName -Value $text -NoNewline -Encoding utf8
    "Copied from: $($found[0].FullName)"
    "Copy at:     $dst"
    Select-String -LiteralPath $ini.FullName -Pattern '^[ \t]*(name|delay)[ \t]*='
    Select-String -LiteralPath (Join-Path $dst 'notes.chart') -Pattern '^[ \t]*Offset[ \t]*='
}
```

Expected: the "Copied from" and "Copy at" lines, then `name = Lunaris (delay 500 test)`, `delay = 500` and `Offset = 0.25`. If the script finds no Lunaris folder or more than one, it prints what it found. The executor then asks the user which folder to copy and runs the copy with that folder.

Then it sends the user this request and waits for all three answers:

> Please check three charts in Clone Hero and tell me what you see. Rescan your songs first so the test copy shows up.
>
> 1. Thornhill "Limbo" (its song.ini has `delay = 1016`). Do the notes come about one second *later* against the music than they should, or *earlier*?
> 2. Moonlight Haze "Lunaris" (your original: notes.chart has `Offset = 0.25`, song.ini has `delay = 0`). Are the notes about a quarter second off against the music? Or do they sit where they would with no offset at all?
> 3. "Lunaris (delay 500 test)", the copy I just made (`Offset = 0.25` and `delay = 500`). Compared with the audio, are the notes shifted by about 0.25 s, 0.5 s or 0.75 s?
>
> When you're done, you can delete the folder "Hydra delay test - Lunaris delay 500" from your songs folder. Nothing else was changed.

The answers set two things in Step 7.

Answer 1 sets the direction. "Later" means `audio_ms = chart_ms + offset_ms`, which is `kOffsetDirection = 1.0`. "Earlier" means `kOffsetDirection = -1.0`.

Answer 3 picks the rule, and answer 2 must agree with it. The table says which body Step 7 uses:

| Answer 3 (the copy) | Answer 2 (Lunaris) | Rule | Step 7 body |
|---|---|---|---|
| 0.75 s | a quarter second off | the two add | (a) add |
| 0.5 s | no offset at all | a song.ini delay line wins, even `delay = 0` | (b) delay wins, any delay line |
| 0.5 s | a quarter second off | a nonzero delay wins; `delay = 0` counts as unset | (b) delay wins, nonzero only |
| 0.25 s | a quarter second off | the Offset wins whenever the chart has one | (c) Offset wins |

Any other pair contradicts itself. One example is 0.75 s on the copy but no shift on Lunaris. In that case the executor stops and tells the user both answers; it does not pick a rule.

Steps 2 to 9 are written for "later" and rule (a). Step 2 says which expected values change for the other answers.

- [ ] **Step 2: Write failing parse and combiner tests.**

Add to tests/test_song.cpp:

```cpp
TEST_CASE(".chart: [Song] Offset is read in seconds") {
    std::string text = multidiff::chart_text();
    const std::string at = "  Resolution = 192\n";
    text.insert(text.find(at) + at.size(), "  Offset = 0.25\n");
    const std::vector<uint8_t> data(text.begin(), text.end());

    Song song = load_songbytes_chart(data, true, true);
    REQUIRE(song.chart_offset_s.has_value());
    CHECK(*song.chart_offset_s == doctest::Approx(0.25));

    Song plain = load_songbytes_chart(multidiff::chart_bytes(), true, true);
    CHECK_FALSE(plain.chart_offset_s.has_value());
}
```

Add to tests/test_preview_source.cpp. It uses the file's own helpers: `make_subdir` makes a fresh folder under %TEMP%, `write_bytes` writes through `hydra::fopen_utf8(path, L"wb")`, and `bytes_of` turns a string into bytes.

```cpp
TEST_CASE("read_ini_delay_ms reads song.ini delay in milliseconds") {
    const std::string dir = make_subdir("ini_delay");
    const std::string ini = dir + "\\song.ini";

    write_bytes(ini, bytes_of("[song]\nname = X\ndelay = 1016\n"));
    REQUIRE(read_ini_delay_ms(ini).has_value());
    CHECK(*read_ini_delay_ms(ini) == doctest::Approx(1016.0));

    write_bytes(ini, bytes_of("[Song]\r\nDelay = -250\r\n"));  // any case, CRLF
    REQUIRE(read_ini_delay_ms(ini).has_value());
    CHECK(*read_ini_delay_ms(ini) == doctest::Approx(-250.0));

    write_bytes(ini, bytes_of("[song]\nname = X\n"));
    CHECK_FALSE(read_ini_delay_ms(ini).has_value());

    write_bytes(ini, bytes_of("[song]\ndelay = soon\n"));
    CHECK_FALSE(read_ini_delay_ms(ini).has_value());

    CHECK_FALSE(read_ini_delay_ms(dir + "\\missing.ini").has_value());
}

TEST_CASE("preview_audio_offset_ms combines delay and Offset") {
    CHECK(preview_audio_offset_ms(std::nullopt, std::nullopt) == doctest::Approx(0.0));
    CHECK(preview_audio_offset_ms(1016.0, std::nullopt) == doctest::Approx(1016.0));
    CHECK(preview_audio_offset_ms(std::nullopt, 0.25) == doctest::Approx(250.0));
    // Lunaris: delay = 0 in song.ini, Offset = 0.25 in the chart.
    CHECK(preview_audio_offset_ms(0.0, 0.25) == doctest::Approx(250.0));
    // The delay-500 copy of Lunaris from Task 17 step 1: both set.
    CHECK(preview_audio_offset_ms(500.0, 0.25) == doctest::Approx(750.0));
}

TEST_CASE("resolve_preview_source reads delay from a song.ini in any case") {
    const std::string dir = make_subdir("ini_delay_case");
    write_bytes(dir + "\\notes.chart", multidiff::chart_bytes());
    write_bytes(dir + "\\Song.INI", bytes_of("[song]\ndelay = 500\n"));
    const PreviewSource src =
        resolve_preview_source(dir + "\\notes.chart", true, true, Difficulty::Expert);
    CHECK(src.audio_offset_ms == doctest::Approx(500.0));
}
```

Add `#include <optional>` to the file's includes.

The values above are for rule (a). The other rules change only the last two lines of the combiner case. Under rule (b) with any delay line winning, the Lunaris line expects `0.0` and the copy line expects `500.0`. Under rule (b) with nonzero only, the Lunaris line stays `250.0` and the copy line expects `500.0`. Under rule (c), the Lunaris line stays `250.0` and the copy line expects `250.0`. The other lines and the resolve case hold under every rule, because they set only one of the two values.

If the user answered "earlier", negate every nonzero expected value in the two offset cases after that: 1016 becomes -1016, 250 becomes -250, 500 becomes -500 and 750 becomes -750.

- [ ] **Step 3: Write failing transport and padding tests.**

Add to tests/test_preview_transport.cpp:

```cpp
TEST_CASE("an audio offset seeks the playhead ahead of the clock") {
    double t = 0.0;
    PreviewTransport transport([&] { return t; });

    auto* playhead = new Playhead(make_ramp(96000));  // 2000 ms
    transport.load(std::unique_ptr<Playhead>(playhead), 0.0, /*audio_offset_ms=*/500.0);
    // The audio past the chart's end still plays out: 2000 - 500.
    CHECK(transport.length_ms() == doctest::Approx(1500.0));

    transport.seek_ms(250.0);
    CHECK(transport.now_ms() == doctest::Approx(250.0));
    CHECK(playhead->position_ms() == doctest::Approx(750.0));

    transport.play();
    CHECK(playhead->position_ms() == doctest::Approx(750.0));
}

TEST_CASE("load with no audio offset behaves exactly as before") {
    PreviewTransport transport([] { return 0.0; });
    auto* playhead = new Playhead(make_ramp(48000));
    transport.load(std::unique_ptr<Playhead>(playhead), 0.0);
    transport.seek_ms(250.0);
    CHECK(playhead->position_ms() == doctest::Approx(250.0));
}

TEST_CASE("pad_front_ms adds silence before the first sample") {
    DecodedAudio a = make_ramp(4);
    hydra::audio::pad_front_ms(a, 1.0);  // 48 frames at 48 kHz
    REQUIRE(a.frames() == 52);
    CHECK(a.samples[0] == 0.0f);
    CHECK(a.samples[47 * 2 + 1] == 0.0f);
    CHECK(a.samples[48 * 2] == 0.0f);       // the ramp's frame 0, L = 0
    CHECK(a.samples[49 * 2] == 1.0f);       // the ramp's frame 1, L = 1
    CHECK(a.samples[49 * 2 + 1] == 1.5f);
}
```

Add `#include "audio/mixer.h"` to the file's includes.

- [ ] **Step 4: Build and watch them fail.**

```powershell
.\build_cpp.ps1 -Target hydra_tests
```

Expected: compile errors for `chart_offset_s`, `read_ini_delay_ms`, `preview_audio_offset_ms`, `audio_offset_ms`, `pad_front_ms` and the three-argument `load`.

- [ ] **Step 5: Read `[Song] Offset` in the .chart parser.**

Tasks 1, 9-11 and 12-14 also edit song.h and song.cpp. If an earlier task already changed the lines quoted here, apply the same change to the new lines.

In src/parse/song.h, add under `bool dynamics_enabled = false;` (line 77). song.h already includes `<optional>`.

```cpp
    // .chart [Song] Offset, in seconds, when the file sets one. Only the
    // Preview reads it (to line the audio up); scoring works in chart time.
    std::optional<double> chart_offset_s;
```

In `ChartParser::parse` (src/parse/song.cpp), `song_sec` is bound at line 949 and the `Song` object only exists from line 957. Add the read right after `song_ = &song;` (line 958):

```cpp
    // Offset is a decimal number of seconds. ChartDataEntry keeps a
    // non-integer value in property_str, so parse that.
    if (auto it = song_sec.prop_data.find("Offset");
        it != song_sec.prop_data.end() && !it->second.empty()) {
        const ChartDataEntry& e = it->second.at(0);
        if (e.property_int) {
            song.chart_offset_s = static_cast<double>(*e.property_int);
        } else if (e.property_str) {
            try {
                song.chart_offset_s = std::stod(*e.property_str);
            } catch (const std::exception&) {
                // An unreadable Offset is treated as absent, like CH's default 0.
            }
        }
    }
```

- [ ] **Step 6: Give song.ini one reader.**

src/app/analysis.cpp already reads song.ini for the library's name, artist and charter (`read_metadata_ini`, lines 183 to 232, in the anonymous namespace). The Preview must not grow a second INI parser. So the line loop moves into one public function, and both callers use it. Tasks 9-11 and 12-14 also edit analysis.cpp. If an earlier task already changed `read_metadata_ini`'s fallback values, keep that task's fallbacks; only the line loop moves.

In src/app/analysis.h, add `#include <map>` if missing, and declare inside `namespace hydra::app`:

```cpp
// The [song] section of a song.ini as lower-cased key -> value, with the
// value's leading blanks trimmed. A key seen twice keeps its last value.
// Section and key names match in any case, `;` and `#` start comments, and a
// UTF-8 BOM is skipped. Throws std::runtime_error when the file cannot be
// read. The library scan (name, artist, charter) and the Preview (delay) both
// read song.ini through here.
std::map<std::string, std::string> read_song_ini_keys(const std::string& path);
```

In src/app/analysis.cpp, define it right after the anonymous namespace closes (`}  // namespace`, line 352 today). The body is today's loop from `read_metadata_ini`, storing each key instead of picking three:

```cpp
std::map<std::string, std::string> read_song_ini_keys(const std::string& path) {
    std::vector<uint8_t> raw = read_file_bytes(path);
    size_t start = 0;
    if (raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) start = 3;
    std::string text(reinterpret_cast<const char*>(raw.data() + start), raw.size() - start);

    std::map<std::string, std::string> keys;
    bool in_song_section = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos
                                                                      : eol - pos);
        pos = (eol == std::string::npos) ? text.size() + 1 : eol + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t'))
            line.pop_back();
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        line = line.substr(a);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            std::string section = lower(line.substr(1, line.size() - 2));
            in_song_section = (section == "song");
            continue;
        }
        if (!in_song_section) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(line.substr(0, eq));
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        std::string value = line.substr(eq + 1);
        size_t vb = value.find_first_not_of(" \t");
        value = (vb == std::string::npos) ? std::string() : value.substr(vb);
        keys[key] = value;
    }
    return keys;
}
```

Then `read_metadata_ini`'s body becomes the following. The fallbacks are today's. Keep the earlier task's version of them if it changed them.

```cpp
std::tuple<std::string, std::string, std::string> read_metadata_ini(const std::string& path) {
    const std::map<std::string, std::string> ini = read_song_ini_keys(path);

    std::string title = "<unknown title>";
    std::string artist = "<unknown artist>";
    std::string charter = "<unknown charter>";
    if (auto it = ini.find("name"); it != ini.end()) title = it->second;
    if (auto it = ini.find("artist"); it != ini.end()) artist = it->second;
    // Only `charter` — Python's get_metadata_ini never reads the `frets`
    // alias, and some inis carry both with different values.
    if (auto it = ini.find("charter"); it != ini.end()) charter = it->second;
    return {title, artist, charter};
}
```

The comment block above it (lines 176 to 181) moves to `read_song_ini_keys`'s declaration, which now says the same thing. Delete it here. Behavior is unchanged: today's loop also let a repeated key's last value win.

- [ ] **Step 7: Find song.ini, read `delay`, and combine.**

Tasks 12-14 also edit preview_source.cpp (the .sng and .srb extractors). If an earlier task already changed the lines quoted here, apply the same change to the new lines.

In src/app/preview_source.h, add `#include <optional>` to the includes. Add to `struct PreviewSource` after `stems`:

```cpp
    // Where chart time 0 sits in the audio: audio_ms = chart_ms +
    // audio_offset_ms. From song.ini delay (ms) and .chart Offset (s), as
    // Clone Hero applies them. 0 for .sng and .srb.
    double audio_offset_ms = 0.0;
```

Under "---- pieces, exposed for testing and reuse", declare:

```cpp
// song.ini's `delay` in milliseconds, or nullopt when the file or key is
// missing or the value is not a number.
std::optional<double> read_ini_delay_ms(const std::string& ini_path);

// The Preview's audio offset in ms from the two values Clone Hero reads.
double preview_audio_offset_ms(std::optional<double> ini_delay_ms,
                               std::optional<double> chart_offset_s);
```

In src/app/preview_source.cpp, add `#include <map>`, `#include <optional>`, `#include "app/analysis.h"` and `#include "parse/chart_files.h"`. Add this to the anonymous namespace, below `find_loose_audio`. `is_song_ini` is Task 12's helper, the same one the library scan uses:

```cpp
// The folder's song.ini, matched in any case (Song.INI counts), or "" when
// there is none.
std::string find_song_ini(const std::string& folder) {
    std::wstring pattern = utf8_to_wide(folder + "\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string found;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::string name = wide_to_utf8(fd.cFileName);
        if (is_song_ini(name)) {
            found = folder + "\\" + name;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

// +1: a positive delay or Offset makes the notes come later than the music,
// so the audio runs ahead of chart time. Confirmed at the game (Task 17
// step 1, Thornhill "Limbo").
constexpr double kOffsetDirection = 1.0;
```

Below the anonymous namespace, define the two pieces:

```cpp
std::optional<double> read_ini_delay_ms(const std::string& ini_path) {
    std::map<std::string, std::string> ini;
    try {
        ini = read_song_ini_keys(ini_path);
    } catch (const std::exception&) {
        return std::nullopt;  // no song.ini, or unreadable: no delay
    }
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

Right below it, define `preview_audio_offset_ms` with exactly one of these three versions: the one Step 1's table picked. Each comment records the game check that chose it.

Rule (a), the two add (the copy moved by 0.75 s):

```cpp
// Clone Hero adds the chart's Offset to song.ini's delay: the delay = 500
// copy of Lunaris (Offset = 0.25) moved by 0.75 s at the game (Task 17 step 1).
double preview_audio_offset_ms(std::optional<double> ini_delay_ms,
                               std::optional<double> chart_offset_s) {
    return kOffsetDirection *
           (ini_delay_ms.value_or(0.0) + chart_offset_s.value_or(0.0) * 1000.0);
}
```

Rule (b), the delay wins (the copy moved by 0.5 s):

```cpp
// song.ini's delay replaces the chart's Offset: the delay = 500 copy of
// Lunaris (Offset = 0.25) moved by 0.5 s at the game (Task 17 step 1). The
// original Lunaris (delay = 0) showed no shift, so any delay line wins, even 0.
double preview_audio_offset_ms(std::optional<double> ini_delay_ms,
                               std::optional<double> chart_offset_s) {
    const bool delay_wins = ini_delay_ms.has_value();
    const double ms = delay_wins ? *ini_delay_ms : chart_offset_s.value_or(0.0) * 1000.0;
    return kOffsetDirection * ms;
}
```

If the original Lunaris was a quarter second off instead, a `delay = 0` counts as unset. Then the `delay_wins` line becomes `const bool delay_wins = ini_delay_ms.has_value() && *ini_delay_ms != 0.0;`. The comment's last sentence becomes "The original Lunaris (delay = 0) still moved by 0.25 s, so a delay of 0 counts as unset."

Rule (c), the Offset wins (the copy moved by 0.25 s):

```cpp
// The chart's Offset replaces song.ini's delay whenever the chart sets one:
// the delay = 500 copy of Lunaris (Offset = 0.25) moved by 0.25 s at the game
// (Task 17 step 1).
double preview_audio_offset_ms(std::optional<double> ini_delay_ms,
                               std::optional<double> chart_offset_s) {
    const double ms = chart_offset_s ? *chart_offset_s * 1000.0 : ini_delay_ms.value_or(0.0);
    return kOffsetDirection * ms;
}
```

If the user answered "earlier" in Step 1, set `kOffsetDirection = -1.0` and say "earlier" in its comment.

In `resolve_preview_source` (lines 299 to 312), the last branch reads the delay. Only the loose .mid/.chart branch does; .sng and .srb keep offset 0. The function becomes:

```cpp
PreviewSource resolve_preview_source(const std::string& notespath, bool pro,
                                     bool bass2x, Difficulty difficulty) {
    PreviewSource src{load_songpath(notespath, pro, bass2x, difficulty), {}};
    if (ends_with_ci(notespath, ".sng"))
        src.stems = extract_sng_audio(notespath);
    else if (ends_with_ci(notespath, ".srb")) {
        src.stems = extract_srb_audio(notespath);
        if (src.stems.empty()) src.stems = find_loose_audio(dir_name(notespath));
    } else {
        const std::string folder = dir_name(notespath);
        src.stems = find_loose_audio(folder);
        const std::string ini = find_song_ini(folder);
        const std::optional<double> delay =
            ini.empty() ? std::nullopt : read_ini_delay_ms(ini);
        src.audio_offset_ms = preview_audio_offset_ms(delay, src.song.chart_offset_s);
    }
    return src;
}
```

If Task 1 gave `load_songpath` a rules argument here, keep it as Task 1 wrote it.

- [ ] **Step 8: Add the silence helper.**

In src/audio/mixer.h, inside `namespace hydra::audio`, declare:

```cpp
// Prepend `ms` of silence (rounded to whole frames). A negative chart offset
// means the chart starts before the audio, and the playhead cannot seek below
// 0, so the load job pads the front instead and plays with offset 0.
void pad_front_ms(DecodedAudio& audio, double ms);
```

In src/audio/mixer.cpp, add `#include <cmath>` if missing, and define it:

```cpp
void pad_front_ms(DecodedAudio& audio, double ms) {
    if (ms <= 0.0 || audio.channels <= 0 || audio.sample_rate <= 0) return;
    const auto frames = static_cast<std::size_t>(std::llround(ms * audio.sample_rate / 1000.0));
    audio.samples.insert(audio.samples.begin(), frames * static_cast<std::size_t>(audio.channels), 0.0f);
}
```

- [ ] **Step 9: Carry the offset through the load job and into the transport.**

Tasks 9-11 also edit preview_load_job.cpp: they replace the "No ... notes" message with `no_notes_message(...)`. If an earlier task already changed the lines quoted here, apply the same change to the new lines.

In src/ui/preview_load_job.h, add `double audio_offset_ms = 0.0;` to `struct Result` after `audio::DecodedAudio mixed;`. In src/ui/preview_load_job.cpp, add this after the `decode_and_mix` call (lines 40 to 45) and before `step_.store(Step::Building);`:

```cpp
        double offset_ms = source.audio_offset_ms;
        if (offset_ms < 0.0) {
            audio::pad_front_ms(mixed, -offset_ms);
            offset_ms = 0.0;
        }
```

Add `#include "audio/mixer.h"` if the file does not include it. Line 49's `result_ = Result{std::move(scene), std::move(mixed), std::move(source.song)};` becomes:

```cpp
        result_ = Result{std::move(scene), std::move(mixed), offset_ms, std::move(source.song)};
```

In src/ui/preview_transport.h, change the `load` declaration (line 37) to:

```cpp
    // Load a chart's audio (may be null/empty for a chart with no audio), the
    // song length, and where chart time 0 sits in the audio (audio_ms =
    // chart_ms + audio_offset_ms, never negative; see PreviewLoadJob). The
    // length is the later of `last_note_ms` and the audio's end in chart time.
    // Resets the playhead to the offset, paused.
    void load(std::unique_ptr<audio::Playhead> playhead, double last_note_ms,
              double audio_offset_ms = 0.0);
```

Add `double audio_offset_ms_ = 0.0;` next to `length_ms_` (line 66). In src/ui/preview_transport.cpp, `load` (lines 11 to 24) becomes:

```cpp
void PreviewTransport::load(std::unique_ptr<audio::Playhead> playhead,
                            double last_note_ms, double audio_offset_ms) {
    std::lock_guard<std::mutex> lock(mu_);
    playhead_ = std::move(playhead);
    audio_offset_ms_ = audio_offset_ms;
    double audio_end = playhead_ ? playhead_->length_ms() - audio_offset_ms_ : 0.0;
    length_ms_ = (std::max)(last_note_ms, audio_end);
    if (playhead_) {
        playhead_->pause();
        playhead_->seek_ms(audio_offset_ms_);
        playhead_->set_gain(gain_);
    }
    clock_.pause();
    clock_.seek_ms(0.0);
}
```

`unload` also sets `audio_offset_ms_ = 0.0;` next to its `length_ms_ = 0.0;` (line 29). In `play`, line 38's `playhead_->seek_ms(clock_.now_ms());` becomes `playhead_->seek_ms(clock_.now_ms() + audio_offset_ms_);`. In `seek_ms`, line 63's `if (playhead_) playhead_->seek_ms(ms);` becomes `if (playhead_) playhead_->seek_ms(ms + audio_offset_ms_);`.

In src/ui/preview_controller.cpp, lines 104 and 105 become:

```cpp
        transport_.load(std::make_unique<audio::Playhead>(std::move(result.mixed)),
                        scene_.song_length_ms, result.audio_offset_ms);
```

- [ ] **Step 10: Build and run.**

```powershell
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe -tc="*Offset*,*delay*,*audio offset*,pad_front_ms*,*transport*"
.\build-cpp\Release\hydra_tests.exe
```

Expected: `[doctest] Status: SUCCESS!` both times. The existing transport cases pass unchanged. The full run proves the library's song.ini metadata still reads the same through `read_song_ini_keys`.

- [ ] **Step 11: Ask the user to confirm by ear.**

Build `hydra`. Ask the user to open the Preview on Thornhill "Limbo" and on Moonlight Haze "Lunaris" and say whether the notes land on the music. The testdata folders for these two charts hold no audio files. The only audio in testdata is three sine-tone decoder fixtures in testdata\audio. So the user checks with the copies in their own Clone Hero library. Do not commit until the user says yes.

- [ ] **Step 12: Commit.**

```powershell
git add src/parse/song.h src/parse/song.cpp src/app/analysis.h src/app/analysis.cpp src/app/preview_source.h src/app/preview_source.cpp src/audio/mixer.h src/audio/mixer.cpp src/ui/preview_load_job.h src/ui/preview_load_job.cpp src/ui/preview_transport.h src/ui/preview_transport.cpp src/ui/preview_controller.cpp tests/test_song.cpp tests/test_preview_source.cpp tests/test_preview_transport.cpp
git commit -m "Preview lines the audio up with song.ini delay and .chart Offset

The Preview assumed chart time 0 was audio time 0 (audit finding 32).
Direction and precedence were confirmed at the game on Limbo, Lunaris
and a delay = 500 copy of Lunaris.
song.ini is found in any case with is_song_ini and read by the one
song.ini reader the library scan also uses. .sng/.srb delay is out of
scope.

Task: Preview honors song.ini delay and .chart Offset
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 18: Rules say why

Many comments justify code by pointing at Python files that were deleted months ago, such as "Mirrors hydata…" or "hymisc.SP_CAP_LADDER". A reader can't follow those pointers. Today there are 56 of them in the core, search, store and parse code plus config.cpp, analysis.h and path_view.cpp. The ones that touch scoring are the real problem, because nothing else explains them. Several other rules have no comment at all.

This task does four things. It replaces each pointer with what the code actually does, or why. It gives the unexplained rules their reasons. That includes the three fixed choices the user decided to keep (decision 25): 8 batch workers, the Ch10 sixteenth-of-a-beat pad, and the multiplier-squeeze combo set. For the combo set, a test checks the drafted reason against `to_multiplier` first. The check proves the reason for 2- and 3-note chords only, so the comment says that and no more. The task also deletes the three test-only squeeze helpers (decision 24). Last, it measures one unwritten rule, "tie grouping ignores SP-ready time", in a throwaway worktree instead of guessing.

The rules Task 1 moved into `core::Rules` already carry their reason in src/core/rules.h: the user's own choice, configurable in hydra_rules.ini. This task adds one line there on how the file names them, and removes the old Python pointers at their former homes. Nothing visible changes, except that `SPSqueeze`'s unused equality goes away.

**Goal:** No comment in the core, search, store and parse code cites deleted Python. Every rule the audit listed states its real reason, or says plainly that the reason is unrecorded. The three test-only squeeze helpers are gone.

**Files:** Changes comments in src/core/model.h, model.cpp, rules.h, timing.h, timing.cpp, src/search/pather.cpp, pather.h, engine.cpp, graph.cpp, src/store/path_binary.cpp, serialize.h, record_store.h, record_store.cpp, src/parse/song.cpp, src/app/config.cpp, analysis.h, analysis.cpp and path_view.cpp. Deletes `SPSqueeze::operator==` and `operator!=`. Deletes `sp_end_shift_ms`, `required_frontend_ms` and `exact_even_split_ms` from src/core/squeeze_rating.h and .cpp, and their test from tests/test_squeeze_rating.cpp. Adds two solo-end cases to tests/test_song.cpp and one multiplier-squeeze case to tests/test_model.cpp. Runs a throwaway measurement in a scratch worktree that is never merged.

**Acceptance Criteria:**
- [ ] `Select-String -Path src\core\*,src\search\*,src\store\*,src\parse\*,src\app\config.cpp,src\app\analysis.*,src\app\path_view.cpp -Pattern 'hydata|hymisc|hystore|hyutil|native/hydra_search|hydra_app\.py'` prints nothing.
- [ ] `git grep -n "sp_end_shift_ms\|required_frontend_ms\|exact_even_split_ms" -- src tools tests` prints nothing.
- [ ] `hydra_tests -tc="*solo end*,*solo marker*,MultSqueeze accepts*"` passes, and the full suite passes.
- [ ] The tie-key measurement's three numbers (charts, score diffs, path-list diffs) are written into the engine.cpp comment at the grouping key.

**Verify:** `.\build-cpp\Release\hydra_tests.exe` -> `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Pin the solo-end rule with tests.**

The two parsers end a solo in different phases on purpose, and the comment in Step 8 will say so. A test stops anyone "fixing" one to match the other. `load_songbytes_mid` and `load_songbytes_chart` both take `(data, pro, bass2x, difficulty = Expert)`, plus the rules argument Task 1 added with a default. Add to tests/test_song.cpp:

```cpp
// In a .chart the `E soloend` event sits on the solo's last note, so that note
// is in the solo. The parser runs solo end after the notes at its tick.
TEST_CASE(".chart: the note on the solo end tick is in the solo") {
    const std::string text =
        "[Song]\n{\n  Resolution = 192\n}\n"
        "[SyncTrack]\n{\n  0 = TS 4\n  0 = B 120000\n}\n"
        "[ExpertDrums]\n{\n"
        "  0 = E solo\n  0 = N 1 0\n"
        "  192 = N 2 0\n"
        "  384 = N 3 0\n  384 = E soloend\n"
        "  576 = N 4 0\n"
        "}\n";
    const std::vector<uint8_t> data(text.begin(), text.end());
    Song song = load_songbytes_chart(data, true, true);
    REQUIRE(song.sequence.size() == 4);
    CHECK(song.sequence[2].flag_solo);
    CHECK_FALSE(song.sequence[3].flag_solo);
}

// In a .mid the solo is a held marker note (103); its note-off tick is where
// the marker stops covering, so a note on that tick is outside the solo.
TEST_CASE(".mid: the note on the solo marker's note-off tick is outside the solo") {
    using namespace testmidi;
    // Delta 480 (one beat at 480 tpqn) as a two-byte variable-length number.
    const std::vector<uint8_t> beat = {0x83, 0x60};
    auto at_beat = [&](std::vector<uint8_t> ev) {  // replace the leading 0 delta
        ev.erase(ev.begin());
        std::vector<uint8_t> out = beat;
        out.insert(out.end(), ev.begin(), ev.end());
        return out;
    };
    const std::vector<uint8_t> track = concat({
        track_name("PART DRUMS"), set_tempo(),
        note_on(103, 100), note_on(97, 100),   // tick 0: solo on, Red
        at_beat(note_on(98, 100)),             // tick 480: Yellow
        at_beat(note_on(103, 0)),              // tick 960: solo marker off
        note_on(99, 100),                      // tick 960: Blue
        end_of_track(),
    });
    Song song = load_songbytes_mid(smf(track), true, true);
    REQUIRE(song.sequence.size() == 3);
    CHECK(song.sequence[0].flag_solo);
    CHECK(song.sequence[1].flag_solo);
    CHECK_FALSE(song.sequence[2].flag_solo);
}
```

- [ ] **Step 2: Run them.**

```powershell
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe -tc="*solo end*,*solo marker*"
```

Expected: both pass on today's code. They pin current behavior; they are not a fix. If either fails, stop and report it, because then the audit's reading of the two phases is wrong.

- [ ] **Step 3: Measure the tie-grouping key in a scratch worktree.**

`reduce_iteration_paths` groups paths by SP meter, or by SP end time while SP is active. It leaves out `sp_ready_ms`. That value decides whether a later activation meets its calibration-fill window (`branch_activate` reads `e.activation_fill_deadline_ms - p.sp_ready_ms`). So merging two tied paths with different `sp_ready_ms` could hide a variant, or in theory change a score. This step measures it on the corpus. Nothing from it is merged.

```powershell
git worktree add --detach "$env:TEMP\hydra-tiekey" HEAD
Set-Location "$env:TEMP\hydra-tiekey"
.\build_cpp.ps1 -Target hydra_replay
New-Item -ItemType Directory -Force "$env:TEMP\tiekey\before","$env:TEMP\tiekey\after" | Out-Null
Copy-Item build-cpp\Release\hydra_replay.exe "$env:TEMP\tiekey\before\"
```

Both copied exes run on default rules, because no hydra_rules.ini sits beside them. `settings_from` in tools/replay.cpp uses the `app::Settings` struct defaults and never reads hydra_settings.ini, so both sides also run the same settings.

In the scratch worktree's src/search/engine.cpp, find this line in `reduce_iteration_paths` (line 857 today):

```cpp
        const uint64_t key = ((uint64_t)sp_value << 1) | (is_sp ? 1ull : 0ull);
```

Replace it with this measurement-only key, and add `#include <functional>` at the top of the file if it is missing:

```cpp
        uint64_t key = ((uint64_t)sp_value << 1) | (is_sp ? 1ull : 0ull);
        if (!is_complete && !is_sp && p.sp >= 2)
            key ^= std::hash<double>{}(p.sp_ready_ms) * 0x9E3779B97F4A7C15ull;
```

Then build it and dump every corpus chart with both exes. Each side gets its own empty 0-byte database, because dump needs an existing file (see Task 15 Step 1). `--out` keeps the snapshot notice out of the JSON files.

```powershell
.\build_cpp.ps1 -Target hydra_replay
Copy-Item build-cpp\Release\hydra_replay.exe "$env:TEMP\tiekey\after\"
foreach ($side in 'before','after') { New-Item -ItemType File -Force "$env:TEMP\tiekey\$side\empty.db" | Out-Null }
$charts = Get-ChildItem C:\Users\Patrick\Downloads\Hydra\hydra-test\testdata\input -Recurse -Include notes.mid,notes.chart,*.sng,*.srb
$i = 0
foreach ($c in $charts) {
  $i++
  foreach ($side in 'before','after') {
    & "$env:TEMP\tiekey\$side\hydra_replay.exe" dump --chart $c.FullName --db "$env:TEMP\tiekey\$side\empty.db" --out "$env:TEMP\tiekey\$side\$i.json" 2>$null | Out-Null
  }
}
$score = 0; $paths = 0; $missing = 0
foreach ($n in 1..$i) {
  $bf = "$env:TEMP\tiekey\before\$n.json"; $af = "$env:TEMP\tiekey\after\$n.json"
  if (-not (Test-Path $bf) -or -not (Test-Path $af)) { $missing++; continue }
  $b = Get-Content $bf -Raw | ConvertFrom-Json
  $a = Get-Content $af -Raw | ConvertFrom-Json
  if ($b.result.score -ne $a.result.score) { $score++; "score differs: $($charts[$n-1].FullName)" }
  if (($b.paths | ConvertTo-Json -Depth 20 -Compress) -ne ($a.paths | ConvertTo-Json -Depth 20 -Compress)) { $paths++; "paths differ: $($charts[$n-1].FullName)" }
}
"charts=$i scoreDiffs=$score pathListDiffs=$paths skipped=$missing"
```

A chart with no notes on Expert Pro Drums writes no JSON. It counts as skipped on both sides.

Then throw the worktree away:

```powershell
Set-Location C:\Users\Patrick\Downloads\Hydra\hydra-test
git worktree remove --force "$env:TEMP\hydra-tiekey"
```

What each outcome means:

If `scoreDiffs=0` and `pathListDiffs=0`, the grouping is harmless on this corpus. Step 8 writes that result into the comment.

If `scoreDiffs=0` but `pathListDiffs>0`, the grouping hides variants only. Step 8 writes the numbers into the comment. The executor also reports the listed charts to the user in its final message. Whether to keep the key as it is becomes the user's call; this task does not change the engine.

If `scoreDiffs>0`, stop and report it to the user. That would be an engine bug, and it is out of scope here.

- [ ] **Step 4: Delete `SPSqueeze`'s unused equality.**

model.h lines 163 and 164 say "Equality compares the offset only, exactly like SPSqueeze.__eq__". So a SqIn and a SqOut with the same offset compare equal. No caller of `SPSqueeze ==` or `!=` exists in src, tests or tools. Activation and Path define no `operator==` that would use it, and tests/test_replay.cpp compares `kind` and `offset_ms` field by field. So it cannot matter today, and the safe fix is to delete it. The build proves there is no caller.

Lines 163 and 164 become:

```cpp
// A SqIn (+) or SqOut (-): which way the note is squeezed across the SP end,
// and by how many ms.
```

Delete lines 183 and 184:

```cpp
    bool operator==(const SPSqueeze& o) const { return offset_ms == o.offset_ms; }
    bool operator!=(const SPSqueeze& o) const { return !(*this == o); }
```

Build `hydra_tests`, `hydra_replay`, `hydra_batch` and `hydra`. If any of them fails to compile on a comparison, restore the two lines, give `operator==` a kind check (`return kind == o.kind && offset_ms == o.offset_ms;`), and name the caller in the commit message.

- [ ] **Step 5: Delete the three test-only squeeze helpers (decision 24).**

`sp_end_shift_ms`, `required_frontend_ms` and `exact_even_split_ms` have no production caller. Only one test uses them. Their own comment says they skip the +2-measure extension that `frontend_transfer_scales` models. Decision 24 deletes them.

In src/core/squeeze_rating.h, delete the block that starts at the `// ---- exact squeeze solver ---` comment (line 149 today) and ends with the `exact_even_split_ms` declaration (line 174). Leave the closing `}  // namespace hydra`. Tasks 4 and 10 also add declarations to this header, next to `timing_tiers`. Delete by these boundaries, not by line number.

In src/core/squeeze_rating.cpp, delete from the `namespace {` that holds `kSolverMaxMs = 8000.0`, `kSolverToleranceMs` and `bisect_min` (line 188 today) through the end of `exact_even_split_ms` (line 240). That includes the comment at lines 214 to 216. Leave the closing `}  // namespace hydra`.

In tests/test_squeeze_rating.cpp, delete the whole `TEST_CASE("exact solver prices displacements across a tempo boundary")` (lines 592 to 640 today).

Build `hydra_tests`, then run the acceptance grep:

```powershell
.\build_cpp.ps1 -Target hydra_tests
git grep -n "sp_end_shift_ms\|required_frontend_ms\|exact_even_split_ms" -- src tools tests
```

Expected: the build succeeds and the grep prints nothing. docs/audit/2026-09-24-derivation-audit.md and docs/cap-clamped-squeeze-frontend-anchor.md still name the helpers. They are dated records, so they stay as written.

- [ ] **Step 6: Check the multiplier-squeeze combo set against `to_multiplier`, then explain it.**

`MultSqueeze::validate` (src/core/model.cpp line 338) accepts a chord only when two things hold. The combo before the chord must be 7, 8, 17, 18, 27 or 28. And `(chord.count() + combo) % 10` must be 0 or 1. `to_multiplier` (src/core/timing.cpp) steps up at combos 10, 20 and 30. Scoring adds 1 to the combo per note before it prices that note. So note i of a chord (from 1) scores at `to_multiplier(combo + i)`. A chord "straddles" a step when its first and last notes score at different multipliers.

The drafted reason was "these are the combos where a chord straddles a step". Working it through by hand gives this. For 2-note chords, the code accepts combos 8, 18 and 28, exactly the straddling ones. For 3-note chords it accepts 7, 8, 17, 18, 27 and 28, again exactly the straddling ones. For 4-note chords it accepts only 7, 17 and 27, although combos 6 and 8 (plus 16, 18, 26 and 28) straddle too. 5-note chords are never accepted, although they straddle from combos 5 to 8 and so on. The test below makes the machine check the 2- and 3-note half, and pins the 4-note behavior as it is.

Add `#include <stdexcept>` and `#include "core/timing.h"` to tests/test_model.cpp, and add:

```cpp
// A multiplier squeeze is a chord whose notes straddle a to_multiplier step.
// For 2- and 3-note chords, MultSqueeze accepts exactly the straddling
// combos. For 4-note chords it accepts only 7, 17 and 27; this pins that
// as-is (see the comment on MultSqueeze::validate).
TEST_CASE("MultSqueeze accepts exactly the 2- and 3-note chords that straddle a multiplier step") {
    const NoteColor order[] = {NoteColor::Red, NoteColor::Yellow, NoteColor::Kick,
                               NoteColor::Blue, NoteColor::Green};
    auto chord_of = [&](int n) {
        Chord c;
        for (int i = 0; i < n; ++i) c.add_note(order[i]);
        c.apply_cymbal(NoteColor::Yellow);  // a cymbal among pads: something to squeeze
        return c;
    };
    auto accepted = [](const Chord& c, int combo) {
        try {
            MultSqueeze ms(c, combo);
            return true;
        } catch (const std::invalid_argument&) {
            return false;
        }
    };
    for (int n = 2; n <= 3; ++n)
        for (int combo = 0; combo < 40; ++combo) {
            const bool straddles = to_multiplier(combo + 1) < to_multiplier(combo + n);
            CHECK_MESSAGE(accepted(chord_of(n), combo) == straddles,
                          "n=" << n << " combo=" << combo);
        }
    for (int combo = 0; combo < 40; ++combo)
        CHECK_MESSAGE(accepted(chord_of(4), combo) == (combo == 7 || combo == 17 || combo == 27),
                      "n=4 combo=" << combo);
}
```

Build and run it:

```powershell
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe -tc="MultSqueeze accepts*"
```

Expected: it passes on today's code. It pins behavior; it is not a fix. If the 2- and 3-note loop fails, the hand check above is wrong. In that case, write the comment below without its second sentence, so it only says what the code does and that the reason is unrecorded. Tell the user which combos disagreed.

Then put this comment above `MultSqueeze::validate` in src/core/model.cpp:

```cpp
// A multiplier squeeze is a chord whose notes straddle a to_multiplier step
// (combos 10, 20, 30): combo_ is the combo before the chord, and note i scores
// at to_multiplier(combo_ + i). For 2- and 3-note chords, the combo set below
// plus the mod-10 rule (the last note lands on the step or one past it)
// accept exactly the straddling chords; the test "MultSqueeze accepts exactly
// the 2- and 3-note chords that straddle a multiplier step" checks this
// against to_multiplier. For 4-note chords only combos 7, 17 and 27 are
// accepted, and 5-note chords never are, although both also straddle from
// other combos. Why the set stops there is not recorded; it is kept fixed.
```

- [ ] **Step 7: Replace every Python pointer.**

Each row below gives the file and today's line, the words to change, and the words that replace them. An earlier task may have moved a line or rewritten nearby text. So the executor finds each one by its words, not its number. Where a row says "delete", the rest of the sentence stays and is rewrapped. The rows cover all 56 hits of the acceptance grep today.

| File, line today | Today's words | New words |
|---|---|---|
| core/model.h 1 | `// Domain model — the C++ port of hydra/hydata.py.` | `// Domain model: charts, chords, squeezes, activations and paths.` |
| core/model.h 9-11 | `// The JSON save/load path from hydata.py is intentionally not ported — Phase 4` and the two lines after it, through `need them.` | `// Records are stored in the binary format of store/serialize.h. Chord::code` / `// and Chord::from_code stay because the parser and the chord tables need them.` |
| core/model.h 32 | `// A chart file that does not work, mirroring hymisc.ChartFileError. Chord and` | `// A chart file that does not work. Chord and` |
| core/model.h 295-296 | the two comment lines above `bool is_sqout_backend(`, whatever Task 5 left there | `// Is this backend the note squeezed out of SP? Compares against the` / `// sqout_tick the engine stored (record format v6), so no display re-derives it.` |
| core/model.h 299-302 | the comment lines above `std::vector<BackendSqueeze> display_backends() const;` | `// Backends worth keeping: those near the deactivation, plus whatever note` / `// is being squeezed out of SP however far out it lands. The details view` / `// shows exactly these, and path_binary stores only these, so a stored` / `// record shows the same rows as a fresh one.` |
| core/model.h 316 | `   // hydata's _activations` (trailing comment on `activations`) | delete the trailing comment |
| core/model.h 353-354 | `// Points-per-note-scored average, matching hydata.Path.avg_mult. 0.0 for a` / `// path with no scoring notes (avoids the ZeroDivisionError guard).` | `// Points per scored note, on average. 0.0 for a path with no scoring` / `// notes, instead of dividing by zero.` If the rounding task (decision 4) rewrote this comment, keep its wording and only delete `, matching hydata.Path.avg_mult`. |
| core/model.h 379 | `variants), mirroring hydata.HydraRecord.all_paths(). Pointers into` | `variants), each path before its variants. Pointers into` |
| core/model.cpp 606 | `// hydata.HydraRecord.all_paths(). Shared by all_paths() and all_allzero_paths()` (and `, mirroring` at the end of line 605) | line 605 ends `nested variants, each path`; line 606 becomes `// before its variants. Shared by all_paths() and all_allzero_paths()` |
| core/timing.h 1 | `// Timing math — the C++ port of hydra/hymisc.py's tick/ms/measure machinery.` | `// Timing math: converting between ticks, milliseconds and measures.` |
| core/timing.h 22 | `// combo -> score multiplier, matching hymisc.to_multiplier.` | `// combo -> score multiplier: x1 below 10, x2 below 20, x3 below 30, else x4.` |
| core/timing.h 25-26 | `Mirrors` / `// hymisc.MsIndex. Built from` | delete `Mirrors hymisc.MsIndex.`; the line reads `// Built from a bpm map (tick -> BPM) and the tick resolution.` |
| core/timing.h 55-56 | `Mirrors` / `// hymisc.MeasureIndex. Built from` | delete `Mirrors hymisc.MeasureIndex.`; the line reads `// Built from a tpm map (tick -> ticks-per-measure).` |
| core/timing.h 79 | `// ticks alone, like hymisc.Timecode.` | `// ticks alone.` |
| core/timing.h 87-89 | `Mirrors the transient state hystore._unpack leaves an Activation's timecode in before _restore_timecodes resolves it` | `This is the state a decoded Activation's timecode is in until restore_timecodes (store/serialize.h) resolves it` |
| core/timing.h 136-137 | `mirroring hymisc.Timecode._plusmeasure_uncached ` | delete (the parenthesis about partial measures stays) |
| core/timing.cpp 29 | `// tps = bpm * tick_r / 60, same order as hymisc.` | `// tps = bpm * tick_r / 60, in this order: reordering the float operations` / `// changes the last bit, and stored results were made with this order.` |
| core/timing.cpp 163 | `// Work in measures, exactly as hymisc does. int() truncates toward zero` | `// Work in measures. int() truncates toward zero` |
| search/engine.cpp 122 | `// ---- engine data structures (verbatim from native/hydra_search.cpp) ------` | `// ---- engine data structures ---------------------------------------------` |
| search/engine.cpp 179 | `rebuilt into hydata Paths locally.` | `rebuilt into core Paths (core/model.h) locally.` |
| search/engine.cpp 1110 | `// ---- rebuild the decision log into hydata Paths -------------------------` | `// ---- rebuild the decision log into core Paths (core/model.h) -------------` |
| search/pather.cpp 16 | `// Ceilings Auto tries, in order. Mirrors hymisc.SP_CAP_LADDER.` | delete the line; Task 1 moved the ladder to `Rules::auto_cap_ladder` |
| search/pather.cpp 20 | `the time budget, mirroring hyutil._CapBudgetExceeded / _deadline_callback.` | `the time budget (auto_budget_s in hydra_rules.ini).` |
| search/pather.cpp 154 | `// Mirrors hyutil._analyze_at_cap.` | delete the line; the comment above it already says what the function does |
| search/pather.cpp 198-199 | ` -- exactly like hyutil._analyze_uncapped (the` / `// Python-era name for this ladder).` | delete; the sentence ends `a result to report.` |
| search/pather.cpp 217 | `interrupted (hyutil._deadline_callback), rather than` | `interrupted, rather than` |
| search/pather.h 30 | `(hymisc.SP_CAP_TIME_BUDGET)` | `(auto_budget_s in hydra_rules.ini)` |
| search/pather.h 64 | `hydra::ChartFileError when the song has no notes, matching hyutil._analyze.` | `hydra::ChartFileError when the song has no notes.` |
| store/path_binary.cpp 15-16 | `// Trim to what's worth keeping, exactly as hystore._pack does before` / `// storing (backends are ~40% of a stock record and display-only).` | `// Store only the rows the details view shows (display_backends). Backends` / `// are ~40% of a record and display-only, so the rest are dropped.` |
| store/serialize.h 1-2 | `// Binary record serialization — replaces hystore.py's json+zlib blob with a` / `// versioned, hand-rolled binary format (see docs/CPP_PORT_PLAN.md Phase 4).` | `// Binary record serialization: a versioned, hand-rolled binary format.` |
| store/serialize.h 5-6 | `both walk the same Path tree shape as hydata.json_save /` / `// json_load (multsqueezes,` | `both walk the Path tree (multsqueezes,` |
| store/serialize.h 14 | `// them into full Timecodes, mirroring hystore._restore_timecodes.` | `// them into full Timecodes.` |
| store/record_store.h 1-2 | `— the C++ port of hydra/hystore.py,` / `// on a redesigned binary format (see docs/CPP_PORT_PLAN.md Phase 4).` | `, in the binary blob format of` / `// store/serialize.h.` (line 3 stays: old Python-era .db files are not read) |
| store/record_store.h 49-50 | `, matching` / `// hystore._SUMMARY_COLUMNS / summarize_path.` | delete; the sentence ends `best path.` |
| store/record_store.h 144 | ` Mirrors hystore.prepare_row.` | delete |
| store/record_store.h 167 | `// hymisc.RECORD_VERSION equivalent: the app version that produced a row.` | `// The app version that produced a row.` |
| store/record_store.h 210-213 | `Mirrors the` / `` `charts` table hydra_app.py's scan_library()/hyutil.ScanItem builds — that `` / `table lives in the GUI layer in Python (not hystore.py), so it wasn't part` / `of the Phase 4 port; it's added here since it belongs in the same db file.` | `It lives in the same db file as the records.` |
| store/record_store.h 260 | `, matching hystore.add_song.` | `.` |
| store/record_store.h 296-297 | `Mirrors the` / `// songmeta dict hystore.iter_blobs builds per row, plus the row's` | `: the song's` / `// metadata row, plus the row's` |
| store/record_store.h 314-315 | ` Mirrors` / `// hystore.iter_blobs: timecodes are NOT restored` | ` Timecodes are NOT restored` |
| store/record_store.h 379-380 | `, matching hydra_app.py's` / `// scan_library(): a scan` | `: a scan` |
| store/record_store.cpp 858 | `// column set, the same way hystore._add_missing_columns does.` | `// column set by adding the columns it lacks.` |
| store/record_store.cpp 1108-1109 | `, matching hydata.json_load's short-circuit on` / `// hyversion mismatch.` | delete; the sentence ends `decoded at all.` |
| app/config.cpp 174-176 | the comment above the `s.time_budget_s = ...` line Task 1 rewrote | `// Bound the Auto ladder so a heavy chart can't hang the app for minutes.` / `// The budget is auto_budget_s in hydra_rules.ini (the user's choice). A` / `// fixed cap is a single run and needs no budget.` |
| app/analysis.h 1-4 | ` — the C++ port of hydra/hyutil.py's discovery half` through `(one chart's` / `// pathing); this is the rest:` | `: search/pather.h paths one chart, and this` / `// file does the rest:` (line 5 continues `finding charts on disk, ...`) |
| app/analysis.h 30 | ` Mirrors hyutil.ScanItem.` | delete |
| app/analysis.h 58 | ` Mirrors hyutil.discover_charts.` | delete |
| app/analysis.h 104-105 | ` Mirrors` / `// hyutil.analyze_chart_file + hybatch.analyze_for_store's non-store half.` | delete; the sentence ends `(for the store's songmeta row).` |
| app/analysis.h 126-127 | ` Mirrors hydra_app.py's` / `// BATCH_MAX_WORKERS/batch_workercount.` | delete; the sentence ends `stays bounded.` (Step 8 puts the reason for 8 on the constant) |
| app/path_view.cpp 84 | `// Mirrors hydra_app.py:951-953 exactly, including the literal tabs:` | `// The row layout, including the literal tabs:` |
| app/path_view.cpp 256-257 | `Mirrors` / `// hydra_app.py:1017-1031 (warning-colored` | `// (warning-colored` (the sentence reads `Which SP ceiling this result was found under (warning-colored when ...`) |
| app/path_view.cpp 288 | `// hydra_app.py:1044 formats the average as` | `// The average prints as`. The rounding task (decision 4) rewrote this code; if its comment no longer has these words, only make sure `hydra_app.py` is gone. |
| app/path_view.cpp 298-299 | `reproduces the blank lines` / `// hydra_app.py:1046,1058 add; Python uses no separator between them.` | `puts a blank line above` / `// each; there is no other separator.` |
| app/path_view.cpp 330-331 | `, mirroring` / `// hydra_app.py's dpg.add_tree_node(label=f"{current_score:,}") grouping.` | `, labelled` / `// with the score in thousands-separated digits.` |

Also add one line to the header comment of src/core/rules.h, after `used, so an absent file changes nothing.`:

```cpp
// In hydra_rules.ini each field is set by its own name, e.g. `max_tied_paths = 4`.
```

That header already says every field is the user's own choice, configurable in hydra_rules.ini. The fields replaced `MAX_TIED_PATHS` in engine.cpp and `kSpCapLadder` in pather.cpp, which Task 1 deleted, so those two sites need no comment of their own.

- [ ] **Step 8: Give the unexplained rules their reasons.**

In src/search/engine.cpp, above `const bool is_sp = !is_complete && node(p.node).is_sp;` in `reduce_iteration_paths`, add the comment below. Fill in the three numbers from Step 3's last line. If Step 3 found path-list differences, add a last line: `// Those charts were reported to the user (Task 18).`

```cpp
        // Group by what decides the path's future: its SP meter, or its SP end
        // while active. sp_ready_ms is left out, although branch_activate reads
        // it for the calibration-fill window. Measured 2026-09 over the corpus
        // with sp_ready_ms added to the key: <scoreDiffs> score changes and
        // <pathListDiffs> variant-list changes across <charts> charts.
```

In src/search/engine.cpp, above `if (f >= progress_reported_ + 0.005f)` (line 1032 today):

```cpp
            // Report every half percent: fine enough for a smooth bar, and it
            // bounds the callback (which may throw to cancel) to 200 calls.
```

In src/app/analysis.cpp, replace `constexpr int kBatchMaxWorkers = 8;` (line 600 today) with:

```cpp
// 8 = a memory/throughput choice: enough threads to keep a modern CPU busy
// while capping peak memory (a discography chart can reach hundreds of MB per
// worker). Kept fixed; batch_worker_count in analysis.h leaves one core free.
constexpr int kBatchMaxWorkers = 8;
```

In src/search/graph.cpp, after the comment at lines 58 to 60 that says the Ch10 lead time was "ported operation-for-operation from Python Hydra 1.2 ... plus a sixteenth-of-a-beat pad", add:

```cpp
// The res/16 pad matches Python Hydra 1.2 output; kept fixed (see ADR 0010).
```

In src/parse/song.cpp, at `case 103:` in the MIDI note-off branch (line 449 today):

```cpp
                case 103:
                    // A MIDI solo marker covers ticks up to its note-off, not
                    // including it: end the solo before this tick's notes.
                    // Pinned by ".mid: the note on the solo marker's note-off
                    // tick is outside the solo".
```

In src/parse/song.cpp, above `if (e.solo_end) ...` (line 838 today):

```cpp
    // A .chart `E soloend` sits on the solo's last note: end the solo after
    // this tick's notes. Pinned by ".chart: the note on the solo end tick is
    // in the solo".
```

In src/parse/song.cpp, above `song.dynamics_enabled = true;` in the .chart parser (line 1006 today):

```cpp
    // .chart ghosts and accents are explicit per-note flags (N 34-37 accent,
    // N 40-43 ghost), applied unconditionally, so a .chart has no opt-in
    // marker like MIDI's [ENABLE_CHART_DYNAMICS]. Dynamics are always on; the
    // Dynamics tab reads this flag.
```

In src/app/path_view.cpp, line 123 (`if (std::abs(r - 1.0) < 0.005) return;  // renders as x1.00`) keeps its comment. Above line 124's loop, add `// 0.005 is half the last digit of %.2f: two scales that print the same are one claim.`. Line 136's `ends_agree` and line 163's comparison each get `// Same %.2f rounding rule as the claim dedupe above.`. Above line 143's first `snprintf`, add:

```cpp
            // 10 ms is an example step, not a rule: the SP-end shift is
            // linear in the frontend shift, so any amount gives the same scale.
```

- [ ] **Step 9: Run the grep check and the full suite.**

```powershell
Select-String -Path src\core\*,src\search\*,src\store\*,src\parse\*,src\app\config.cpp,src\app\analysis.*,src\app\path_view.cpp -Pattern 'hydata|hymisc|hystore|hyutil|native/hydra_search|hydra_app\.py'
git grep -n "sp_end_shift_ms\|required_frontend_ms\|exact_even_split_ms" -- src tools tests
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe
```

Expected: both greps print nothing, and the suite prints `[doctest] Status: SUCCESS!`. UI-only pointers in src/ui (details_view, library_view, theme, fonts, icons, app_shell, app_state, main.cpp) and src/app/config.h are outside this grep and stay. They describe layout and settings plumbing, not rules.

- [ ] **Step 10: Commit.**

```powershell
git add src/core/model.h src/core/model.cpp src/core/rules.h src/core/timing.h src/core/timing.cpp src/core/squeeze_rating.h src/core/squeeze_rating.cpp src/search/pather.cpp src/search/pather.h src/search/engine.cpp src/search/graph.cpp src/store/path_binary.cpp src/store/serialize.h src/store/record_store.h src/store/record_store.cpp src/parse/song.cpp src/app/config.cpp src/app/analysis.h src/app/analysis.cpp src/app/path_view.cpp tests/test_song.cpp tests/test_model.cpp tests/test_squeeze_rating.cpp
git commit -m "Say why each scoring rule is what it is

Replaces comments that cited deleted Python with what the code does,
pins the per-format solo-end phase with two tests, checks the
multiplier-squeeze combo set against to_multiplier (exact for 2- and
3-note chords; the 4- and 5-note limit is kept and marked unrecorded),
explains the 8 workers and the Ch10 pad, deletes SPSqueeze's unused
offset-only equality and the three test-only squeeze helpers, and records
the tie-key measurement (audit findings 37, 39 and 40; decisions 24, 25).

Task: Rules say why
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 19: Final check across both repos

Each task above proved its own piece. This task proves they hold together. It rebuilds everything, runs the whole test suite, repeats the hydra_batch diff against the Task 3 baseline, and runs video-tools' check_refs one last time. It also writes the release note that tells you every record will read Stale after the upgrade. Nothing new is built here. If any check fails, the task stops and reports; it does not patch.

**Goal:** Show that all 18 tasks together leave every score unchanged except where a decision said it should change, and write the upgrade note.

**Files:** Creates docs/handoffs/2026-09-24-derivation-fixes-release-note.md. Reads (never edits) `$env:TEMP\hydra_task3\batch_sorted.txt` and the video-tools repo.

**Acceptance Criteria:**
- [ ] `.\build-cpp\Release\hydra_tests.exe` ends with `[doctest] Status: SUCCESS!` and zero failed test cases.
- [ ] Hydra (the GUI), hydra_batch, hydra_replay, hydra_bench, hydra_report, hydra_fillcompare and hydra_uitest all build with no errors.
- [ ] The hydra_batch diff against `$env:TEMP\hydra_task3\batch_sorted.txt` shows no score or path change. The only lines allowed to differ are a title that became "(unknown)" (decision 10) or a chart the case-insensitive scan now finds (decision 8), and each such line is listed in the report with its reason.
- [ ] In video-tools, `py check_refs.py` passes every case listed in refs.json.
- [ ] `docs/handoffs/2026-09-24-derivation-fixes-release-note.md` exists with the text in Step 5.
- [ ] `git status --short` in hydra-test still shows your four uncommitted files modified, unchanged by this plan's commits except where a task said so.

**Verify:** `.\build-cpp\Release\hydra_tests.exe` -> `[doctest] Status: SUCCESS!`, then the Step 3 diff -> only lines explained by decisions 8 and 10.

**Steps:**

- [ ] **Step 1: Build every target from a clean configure**

```powershell
foreach ($t in 'hydra_tests','Hydra','hydra_batch','hydra_replay','hydra_bench','hydra_report','hydra_fillcompare','hydra_uitest') {
  .\build_cpp.ps1 -Target $t
  if ($LASTEXITCODE -ne 0) { "BUILD FAILED: $t"; break }
}
```

Expected: no `BUILD FAILED` line.

- [ ] **Step 2: Run the whole test suite**

```powershell
.\build-cpp\Release\hydra_tests.exe
```

Expected: `[doctest] Status: SUCCESS!`. Write down the test-case and assertion counts for the report.

- [ ] **Step 3: Repeat the batch diff against the Task 3 baseline**

The run must use today's default rules, so first confirm there is no hydra_rules.ini next to the exe.

```powershell
Test-Path .\build-cpp\Release\hydra_rules.ini
$out = "$env:TEMP\hydra_task19"
New-Item -ItemType Directory -Force $out | Out-Null
Remove-Item "$out\after.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_batch.exe --db "$out\after.db" testdata\input |
  Select-String '^\[\d+/\d+\] ' | ForEach-Object { $_.Line -replace '^\[\d+/\d+\] ', '' } |
  Sort-Object | Set-Content "$out\batch_sorted.txt"
Compare-Object (Get-Content "$env:TEMP\hydra_task3\batch_sorted.txt") (Get-Content "$out\batch_sorted.txt")
```

Expected: `Test-Path` prints `False`. For every pair of differing lines, the score and best path are identical and only the title reads "(unknown)". Any line with no partner must be a chart whose file name is not all lowercase (check with `Get-ChildItem -Recurse testdata\input | Where-Object { $_.Name -cmatch '^(?i:notes\.(mid|chart)|song\.ini)$' -and $_.Name -cne $_.Name.ToLower() }`). Any other difference: stop and report it.

- [ ] **Step 4: Run check_refs in video-tools**

check_refs uses C:\Users\Patrick\Downloads\Hydra\hydra-test\build-cpp\Release\hydra_replay.exe. If this plan ran in a worktree, copy the worktree's built hydra_replay.exe over that path first.

```powershell
Push-Location C:\Users\Patrick\Downloads\Hydra\video-tools
py check_refs.py *> "$env:TEMP\check_refs_task19.txt"
Get-Content "$env:TEMP\check_refs_task19.txt" -Tail 5
git status --short
Pop-Location
```

Expected: the summary line says every case passed, and the number matches the case count in refs.json. `git status --short` prints nothing (Task 8 committed everything).

- [ ] **Step 5: Write the release note**

Create docs/handoffs/2026-09-24-derivation-fixes-release-note.md with exactly this text:

```markdown
# Upgrade note: derivation fixes

Every saved result reads Stale after this upgrade. The record format changed, so old results cannot be shown until re-analyzed. Run Analyze on your library once. Nothing is lost; the old rows stay in the database.

What you will see change:

- A squeezed-out backend row the game does not count now shows 0 points with "(uncounted)", and so does any plain "Hard (uncounted)" or "Insane (uncounted)" row. None of them is highlighted.
- The calibration fill reads the same sign everywhere: positive means you hit early.
- The average multiplier is rounded everywhere, so Song Details can read 1.667x where it read 1.666x.
- A song with no name shows "(unknown)".
- The Preview's SP gauge fills from the phrases the path really collected, and the Preview honors the song's audio delay.
- The scan finds notes.mid, notes.chart and song.ini in any letter case.

New: hydra_rules.ini. Put it next to Hydra.exe to change the rules that used to be fixed: the 3 ms backend leeway, the squeeze-out rule, the fill constants, the number of tied paths, and the Auto cap ladder and budget. Any change makes every result read Stale until re-analyzed. Switching back brings the old results back. A mistake in the file stops Analyze and shows which key is wrong.
```

- [ ] **Step 6: Commit and report**

```powershell
git add docs/handoffs/2026-09-24-derivation-fixes-release-note.md
git commit -m "Add the upgrade note for the derivation fixes

Task: Final check across both repos
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Report the test counts from Step 2, every line from Step 3 with its reason, and the check_refs summary line from Step 4.
