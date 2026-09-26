# Preview Score Box and Finer Time Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Preview tab gets a running score box and four new transport buttons: back or forward 5 seconds (Left and Right arrows), and back or forward one chart tick (comma and period).

**Architecture:** The score is not computed anew. It comes from `replay_path` (src/core/replay.h), which is already proven equal to the engine's totals for every corpus path. The replay runs once whenever the Preview scene is built, and the box reads the last chord at or before the playhead. The step math lives in the view model (src/app/preview_view), where it is unit-tested. The controller forwards to the transport, and the details panel only draws buttons and reads keys.

**Tech Stack:** C++20, CMake via build_cpp.ps1, Dear ImGui 1.93 WIP, doctest (hydra_tests), hydra_uitest for GUI checks.

**Spec:** The design and the clickable mockup agreed in chat on 2026-09-25, restated in "What we're building" below. There is no separate spec file.

## What we're building

The score box sits under the time box, at the highway's top-left, in the same see-through dark panel. It has two lines. The top line is the score in large type, like `12,345`. The bottom line is the multiplier and combo in the time box's size, like `x4 · combo 42`, in light grey.

The score is what the game's counter would show at the playhead. A solo's bonus is held back until the solo's last note and then lands all at once, the way Clone Hero shows it. The replay already tracks this as `cum_onscreen_total`.

The multiplier doubles while the playhead is inside a Star Power window. That window runs from the activation to its deactivation node, which is the same stretch the highway tints.

The box only appears when there is an analyzed path. It says "Score unavailable" if the path can't be replayed faithfully. That happens when an older record lacks a deactivation node (stored since blob v4) or a squeeze-out tick (stored since blob v6), or when the replay's total doesn't equal the path's stored total. A wrong number is worse than no number, so the box never shows one it can't vouch for.

The transport row becomes `-5s`, `< Tick`, `Play`, `Tick >`, `+5s`, then the scrubber, readout and volume as today. Hovering a button names its key.

A 5-second jump keeps playing if it was playing, and stops at the song's start or end. A tick step pauses first, then moves exactly one chart tick using the song's own tempo map. "One tick" is measured from the tick the time box shows, so each press changes the displayed `[measure:beat:tick]` by exactly one. Holding any of the four keys repeats.

The keys only act while the Preview tab is showing and no text field has the keyboard. The Preview claims Left and Right while it shows, so ImGui's keyboard navigation doesn't also move focus or nudge the scrubber.

## Global Constraints

The engine is the single source of truth. The score, multiplier and combo all come from `replay_path`'s own fields. No task re-derives a score, a multiplier or a combo in the view, the controller or the panel. That is why Task 1 adds `combo_after` to the replay instead of letting the view add up note counts.

Nothing that exists today changes how it looks or behaves, apart from the transport row gaining four buttons. The scrubber shrinks to make room. Its position must not drift while playing, and the existing `layout-drift` GUI test must keep passing unchanged.

The working tree has uncommitted files that belong to the user: `docs/cap-clamped-squeeze-frontend-anchor.md`, the scratch `.txt` files at the repo root, `docs/audit/`, `docs/handoffs/`, `scratch_ms/`, `srb_audio_dump/` and `srb_songs/`. Never stage, revert, stash or delete them. Stage only the files a task names.

Every commit message ends with four lines: `Task: <task name>`, `Agent: <executor>`, `Session: <session>`, and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never amend, rebase or reset.

Build from the repo root in PowerShell. Unit tests: `.\build_cpp.ps1 -Target hydra_tests`, then `.\build-cpp\Release\hydra_tests.exe`, optionally with `-tc="<name>"`. GUI tests: `.\build_cpp.ps1 -Target hydra_uitest`, then `.\build-cpp\Release\hydra_uitest.exe --test <name>` or `--all`. GUI checks go through hydra_uitest (docs/agents/ui-testing.md), never screenshots.

Code blocks quote the code as it is on disk today. If an earlier task already changed the same lines, apply the change on top and keep what the earlier task added.

**User decisions (already made):**
- The tick step is exactly one chart tick ("Chart tick (as asked)").
- The score box shows score, multiplier and combo ("Score + multiplier + combo").
- The solo bonus counts like the game: the whole bonus lands on the solo's last note ("Like the game").
- With no analyzed path, the score box is hidden ("Hide the box").
- The design in chat was accepted as shown: box under the time box, same translucent style, "Score unavailable" on a mismatch, button labels `-5s` `< Tick` `Play` `Tick >` `+5s` with tooltips naming the key, 5 s jumps keep the play state, tick steps pause first.

---

## How the tasks fit together

There are five tasks, run in order. Task 1 gives the replay the one number it was missing, the combo after each chord. Task 2 builds the score track and the score box's text in the view model. Task 3 adds the tick-step math beside it. Task 4 wires both into the controller and the background load, and tests them through the real app. Task 5 adds the buttons, the keys and the drawn box to the panel, and tests them by clicking and key presses.

---

### Task 1: The replay reports the combo after each chord

Today each replayed chord carries `combo_before`, the combo going into the chord. The score box wants the combo the game shows once the chord is hit. The replay already computes that number (`combo += ts.chord.count();` in src/core/replay.cpp) and then throws it away. This task keeps it as a new field, `combo_after`, so the view never has to add note counts itself.

What the user sees: nothing yet.

**Goal:** `ReplayChord` gains `int combo_after`, set to the combo right after the chord is hit.

**Files:**
- Modify: `src/core/replay.h` (struct `ReplayChord`)
- Modify: `src/core/replay.cpp` (`replay_path`, right after `combo += ts.chord.count();`)
- Test: `tests/test_replay.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="replay: combo_after*"` passes: for chords of 1, 2 and 3 notes, `combo_after` reads 1, 3 and 6, and each chord's `combo_before` equals the previous chord's `combo_after`.
- [ ] `hydra_tests.exe -tc="replay reproduces the engine's score for every corpus path"` still passes.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="replay*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test.** Add this case to tests/test_replay.cpp, after the first TEST_CASE:

```cpp
// The combo the game's counter shows once a chord is hit. The Preview's score
// box reads it straight off the replay rather than adding note counts itself.
TEST_CASE("replay: combo_after is the combo once the chord is hit") {
    Song song(480);
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    const std::vector<std::vector<NoteColor>> chords = {
        {NoteColor::Red},
        {NoteColor::Red, NoteColor::Yellow},
        {NoteColor::Kick, NoteColor::Blue, NoteColor::Green}};
    int64_t tick = 0;
    for (const std::vector<NoteColor>& colors : chords) {
        SongTimestamp ts;
        ts.timecode = song.timecode(tick);
        for (NoteColor c : colors) ts.chord.add_note(c);
        song.sequence.push_back(std::move(ts));
        tick += 480;
    }

    const ReplayResult r = replay_path(song, {});
    REQUIRE(r.chords.size() == 3);
    CHECK(r.chords[0].combo_before == 0);
    CHECK(r.chords[0].combo_after == 1);
    CHECK(r.chords[1].combo_before == 1);
    CHECK(r.chords[1].combo_after == 3);
    CHECK(r.chords[2].combo_before == 3);
    CHECK(r.chords[2].combo_after == 6);
}
```

- [ ] **Step 2: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error, because `ReplayChord` has no member `combo_after`.

- [ ] **Step 3: Add the field.** In src/core/replay.h, inside `struct ReplayChord`, right after `int combo_before = 0;`:

```cpp
    // The combo once this chord is hit: combo_before plus the chord's notes.
    // What the game's combo counter shows after the chord.
    int combo_after = 0;
```

In src/core/replay.cpp, inside `replay_path`, change:

```cpp
        combo += ts.chord.count();
```

to:

```cpp
        combo += ts.chord.count();
        row.combo_after = combo;
```

- [ ] **Step 4: Run it and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="replay*"`. Expected: `Status: SUCCESS!`, with the corpus replay case included.

- [ ] **Step 5: Commit.**

```bash
git add src/core/replay.h src/core/replay.cpp tests/test_replay.cpp
git commit -m "Keep the combo after each replayed chord

Task: Task 1: The replay reports the combo after each chord
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/core/replay.h","src/core/replay.cpp","tests/test_replay.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe -tc=\"replay*\"","acceptanceCriteria":["`hydra_tests.exe -tc=\"replay: combo_after*\"` passes: for chords of 1, 2 and 3 notes, `combo_after` reads 1, 3 and 6, and each chord's `combo_before` equals the previous chord's `combo_after`.","`hydra_tests.exe -tc=\"replay reproduces the engine's score for every corpus path\"` still passes."],"modelTier":"mechanical"}
```

---

### Task 2: The Preview scene carries the running score

Today the Preview scene knows the notes, the Star Power windows and the meter, but not the score. This task runs `replay_path` once when the scene is built, keeps one small step per chord, and adds `build_score_box`, which turns the step at the playhead into the box's two lines of text.

The replay is trusted only when it provably matches the path. Every activation must have become a Star Power window, and the six category totals must equal the path's stored ones. Otherwise the box says "Score unavailable". With no path at all, the box is hidden.

`build_preview_scene` also gains a `rules` parameter, because the replay prices backend rows and squeeze-outs with the user's `hydra_rules.ini` rules. It defaults to `core::default_rules()`, so every existing caller still compiles. Task 4 passes the real rules.

What the user sees: nothing yet. The panel starts drawing the box in Task 5.

**Goal:** `PreviewScene` gains a `PreviewScore` (None, Unavailable or Ready, plus one step per chord), and `build_score_box(scene, now_ms)` returns the box's text at the playhead.

**Files:**
- Modify: `src/app/preview_view.h` (new `PreviewScoreStep`, `PreviewScore`, `PreviewScoreBox`, `build_score_box`; `PreviewScene::score`; the `rules` parameter on `build_preview_scene`)
- Modify: `src/app/preview_view.cpp` (a `build_score` helper, one call in `build_preview_scene`, and `build_score_box`)
- Test: `tests/test_preview_view.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="score box: no path hides the box"` passes: state None and `shown == false`.
- [ ] `hydra_tests.exe -tc="score box: a solo's bonus lands on its last note"` passes: mid-solo the total leaves out the solo bonus, and on the solo's last chord it includes all of it.
- [ ] `hydra_tests.exe -tc="score box: before the first note*"` passes: `"0"` and `"x1 · combo 0"`.
- [ ] `hydra_tests.exe -tc="score box: Star Power doubles the multiplier*"` passes: `"x1 · combo 5"` before the activation, `"x2 · combo 7"` inside the window, `"x2 · combo 15"` after it.
- [ ] `hydra_tests.exe -tc="score box: a path the replay can't reproduce*"` passes: a wrong stored score, and an activation with no deactivation node, both read `"Score unavailable"` with an empty detail line.
- [ ] `hydra_tests.exe -tc="score box: the analyzed chart ends on the path's total"` passes on the corpus chart.
- [ ] The whole suite `hydra_tests.exe` passes, with no existing test edited.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="score box*,build_preview_scene*,build_time_box*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests.** In tests/test_preview_view.cpp, add `#include "core/replay.h"` and `#include "core/model.h"` beside the other includes. Add this helper inside the anonymous namespace, after `analyzed()`:

```cpp
// A path whose stored score is what the replay prices it at, so the scene
// trusts it. The corpus case proves the replay against the engine; these
// hand-built fixtures only test the Preview's plumbing.
Path priced_path(const Song& song, std::vector<Activation> acts) {
    Path p;
    p.activations = std::move(acts);
    const ReplayScore s = replay_path(song, windows_for_path(p, song)).final;
    p.score_base = s.base;
    p.score_combo = s.combo;
    p.score_sp = s.sp;
    p.score_solo = s.solo;
    p.score_accents = s.accent;
    p.score_ghosts = s.ghost;
    return p;
}

// The detail line's separator: a middle dot, U+00B7, in UTF-8.
const std::string kDot = "\xC2\xB7";
```

Then add these cases at the end of the file:

```cpp
TEST_CASE("score box: no path hides the box") {
    Song song = make_hand_song();
    PreviewScene scene = build_preview_scene(song, nullptr);
    CHECK(scene.score.state == PreviewScore::State::None);
    CHECK(scene.score.steps.empty());
    CHECK_FALSE(build_score_box(scene, 600.0).shown);
}

TEST_CASE("score box: a solo's bonus lands on its last note") {
    // Hand song: chords at 0, 250, 500 and 750 ms. The solo is the chords at
    // 250 and 500 ms, so its bonus is withheld at 250 and paid at 500.
    Song song = make_hand_song();
    Path path = priced_path(song, {});
    PreviewScene scene = build_preview_scene(song, &path);
    REQUIRE(scene.score.state == PreviewScore::State::Ready);
    REQUIRE(scene.score.steps.size() == 4);

    const ReplayResult r = replay_path(song, {});
    REQUIRE(r.chords[1].points.solo > 0);
    CHECK(scene.score.steps[1].total == r.chords[1].cum.total() - r.chords[1].points.solo);
    CHECK(scene.score.steps[2].total == r.chords[2].cum.total());
    CHECK(scene.score.steps[3].total == path.totalscore());

    // Between the solo's two chords the box shows the withheld total.
    PreviewScoreBox mid = build_score_box(scene, 400.0);
    CHECK(mid.shown);
    CHECK(mid.available);
    CHECK(mid.score == group_thousands(scene.score.steps[1].total));
    CHECK(mid.detail == "x1 " + kDot + " combo 2");

    // A chord exactly at the playhead counts as hit.
    PreviewScoreBox on = build_score_box(scene, scene.score.steps[2].ms);
    CHECK(on.score == group_thousands(scene.score.steps[2].total));
    CHECK(on.detail == "x1 " + kDot + " combo 3");
}

TEST_CASE("score box: before the first note nothing is hit yet") {
    // The fill song's first note is at tick 480, 500 ms.
    Song song = make_fill_song();
    Path path = priced_path(song, {});
    PreviewScene scene = build_preview_scene(song, &path);
    REQUIRE(scene.score.state == PreviewScore::State::Ready);
    PreviewScoreBox box = build_score_box(scene, 100.0);
    CHECK(box.shown);
    CHECK(box.available);
    CHECK(box.score == "0");
    CHECK(box.detail == "x1 " + kDot + " combo 0");
}

TEST_CASE("score box: Star Power doubles the multiplier inside the active window") {
    // A Red note every 500 ms (tick 480 steps). One bar of SP activated at
    // tick 2400 (2500 ms) runs two measures, to tick 6240 (6500 ms).
    Song song = make_sp_song({1920}, 9600);
    Path path = priced_path(song, {sp_act_at(song, 2400, 1)});
    PreviewScene scene = build_preview_scene(song, &path);
    REQUIRE(scene.score.state == PreviewScore::State::Ready);
    REQUIRE(scene.activations.size() == 1);
    REQUIRE(scene.activations[0].has_sp_end);
    CHECK(scene.activations[0].sp_end_ms == doctest::Approx(6500.0));

    // 2000 ms: five notes hit, before the activation.
    CHECK(build_score_box(scene, 2000.0).detail == "x1 " + kDot + " combo 5");
    // 3000 ms: seven notes hit, inside the window, so x1 doubles to x2.
    CHECK(build_score_box(scene, 3000.0).detail == "x2 " + kDot + " combo 7");
    // 7000 ms: fifteen notes hit, past the window: the plain x2 of combo 15.
    CHECK(build_score_box(scene, 7000.0).detail == "x2 " + kDot + " combo 15");
}

TEST_CASE("score box: a path the replay can't reproduce says so") {
    Song song = make_hand_song();

    // The stored score disagrees with the replay by one point.
    Path wrong = priced_path(song, {});
    wrong.score_base += 1;
    PreviewScene a = build_preview_scene(song, &wrong);
    CHECK(a.score.state == PreviewScore::State::Unavailable);
    PreviewScoreBox box = build_score_box(a, 600.0);
    CHECK(box.shown);
    CHECK_FALSE(box.available);
    CHECK(box.score == "Score unavailable");
    CHECK(box.detail.empty());

    // An activation with no deactivation node (a record from before blob v4)
    // yields no window, so the replay can't stand for the path.
    Path old;
    old.activations.push_back(act_at(song, 720, 0));
    PreviewScene b = build_preview_scene(song, &old);
    CHECK(b.score.state == PreviewScore::State::Unavailable);
    CHECK(build_score_box(b, 600.0).score == "Score unavailable");
}

TEST_CASE("score box: the analyzed chart ends on the path's total") {
    const AnalysisResult& r = analyzed();
    const Path& best = r.record.best_path();
    PreviewScene scene = build_preview_scene(r.song, &best);
    REQUIRE(scene.score.state == PreviewScore::State::Ready);
    REQUIRE(scene.score.steps.size() == r.song.sequence.size());
    CHECK(scene.score.steps.back().total == best.totalscore());
    for (size_t i = 1; i < scene.score.steps.size(); ++i) {
        CHECK(scene.score.steps[i].ms >= scene.score.steps[i - 1].ms);
        CHECK(scene.score.steps[i].combo > scene.score.steps[i - 1].combo);
    }
    PreviewScoreBox end = build_score_box(scene, scene.song_length_ms);
    CHECK(end.score == group_thousands(best.totalscore()));
}
```

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors naming `PreviewScore`, `build_score_box` and `PreviewScene::score`.

- [ ] **Step 3: Add the types.** In src/app/preview_view.h, add `#include "core/rules.h"` after `#include "core/model.h"`. Add these types after `SpMeterCurve`, before `PreviewScene`:

```cpp
// One step of the running score: what the game's counter reads from the
// moment this chord is hit until the next one. Copied from core/replay.h,
// which is proven equal to the engine's own totals for every corpus path.
struct PreviewScoreStep {
    double ms = 0.0;      // the chord's onset
    int64_t total = 0;    // on-screen total: a solo's bonus lands on its last chord
    int multiplier = 1;   // combo multiplier once the chord is hit, before SP doubles it
    int combo = 0;        // notes hit so far, this chord included
};

// The running score for the path the overlay shows.
//   None        — no path: the chart is not analyzed, so there is no score.
//   Unavailable — the path cannot be replayed faithfully (an activation
//                 without a stored deactivation node or squeeze-out tick),
//                 or the replay's total is not the stored one. The box says
//                 so rather than show a number nobody can vouch for.
//   Ready       — `steps` holds one entry per chord, in time order.
struct PreviewScore {
    enum class State { None, Unavailable, Ready };
    State state = State::None;
    std::vector<PreviewScoreStep> steps;
};
```

Inside `struct PreviewScene`, right after `SpMeterCurve sp_meter;` and its comment, add:

```cpp
    // The running score the score box reads. None when built without a path.
    PreviewScore score;
```

After `build_time_box`'s declaration, add:

```cpp
// The score box the Preview draws under the time box, at `now_ms`. `score`
// is the total with thousands separators ("12,345"), and `detail` is
// "x<multiplier> · combo <n>". The multiplier doubles while `now_ms` is
// inside an activation's Star Power window (activation to deact node, the
// span the highway tints). Hidden (`shown` false) when the scene has no
// path; "Score unavailable" with an empty `detail` when the replay could not
// be trusted.
struct PreviewScoreBox {
    bool shown = false;
    bool available = false;
    std::string score;
    std::string detail;
};

PreviewScoreBox build_score_box(const PreviewScene& scene, double now_ms);
```

Change the `build_preview_scene` declaration and add a line to its comment:

```cpp
// `rules` prices the running score (the replay reads the backend leeway and
// the squeeze-out rule from it); nothing else in the scene depends on it.
PreviewScene build_preview_scene(const Song& song, const Path* path,
                                 int sp_cap = kCloneHeroSpCap,
                                 const core::Rules& rules = core::default_rules());
```

- [ ] **Step 4: Build the score.** In src/app/preview_view.cpp, add `#include <exception>` with the standard includes and `#include "core/replay.h"` after `#include "core/squeeze_rating.h"`. In the anonymous namespace that ends just before `build_preview_scene` (the one holding `build_sp_meter_curve`), add:

```cpp
// The running score, from the same replay hydra_replay uses. Trusted only
// when the replay provably stands for the path: every activation became a
// window, and all six category totals equal the stored ones.
PreviewScore build_score(const Song& song, const Path* path, const core::Rules& rules) {
    PreviewScore score;
    if (path == nullptr) return score;  // None
    score.state = PreviewScore::State::Unavailable;
    try {
        std::vector<ReplayWindow> windows = windows_for_path(*path, song);
        if (windows.size() != path->all_activations().size()) return score;
        const ReplayResult r = replay_path(song, std::move(windows), rules);
        if (!(r.final == score_of(*path))) return score;
        score.steps.reserve(r.chords.size());
        for (const ReplayChord& c : r.chords)
            score.steps.push_back({c.ms, c.cum_onscreen_total, c.multiplier_after, c.combo_after});
    } catch (const std::exception&) {
        score.steps.clear();
        return score;  // Unavailable
    }
    score.state = PreviewScore::State::Ready;
    return score;
}
```

Change the definition's first line to match the header:

```cpp
PreviewScene build_preview_scene(const Song& song, const Path* path, int sp_cap,
                                 const core::Rules& rules) {
```

And just before its last two lines (`scene.sp_meter = build_sp_meter_curve(scene, timing, sp_cap); return scene;`), add:

```cpp
    scene.score = build_score(song, path, rules);
```

- [ ] **Step 5: Build the box.** In src/app/preview_view.cpp, after `build_time_box`'s definition, add:

```cpp
PreviewScoreBox build_score_box(const PreviewScene& scene, double now_ms) {
    PreviewScoreBox box;
    if (scene.score.state == PreviewScore::State::None) return box;
    box.shown = true;
    if (scene.score.state == PreviewScore::State::Unavailable) {
        box.score = "Score unavailable";
        return box;
    }
    box.available = true;

    // The last chord at or before the playhead. Before the first chord
    // nothing is hit yet: 0, x1, combo 0.
    const std::vector<PreviewScoreStep>& steps = scene.score.steps;
    auto it = std::upper_bound(steps.begin(), steps.end(), now_ms,
                               [](double v, const PreviewScoreStep& s) { return v < s.ms; });
    PreviewScoreStep at;
    if (it != steps.begin()) at = *(it - 1);

    // Star Power doubles the multiplier across the window the highway tints.
    int multiplier = at.multiplier;
    for (const PreviewActivation& a : scene.activations)
        if (a.has_sp_end && now_ms >= a.ms && now_ms <= a.sp_end_ms) {
            multiplier *= 2;
            break;
        }

    box.score = group_thousands(at.total);
    char buf[64];
    std::snprintf(buf, sizeof buf, "x%d \xC2\xB7 combo %d", multiplier, at.combo);
    box.detail = buf;
    return box;
}
```

- [ ] **Step 6: Run them and watch them pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="score box*,build_preview_scene*,build_time_box*"`. Expected: `Status: SUCCESS!`. Then run the whole suite, `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`.

- [ ] **Step 7: Commit.**

```bash
git add src/app/preview_view.h src/app/preview_view.cpp tests/test_preview_view.cpp
git commit -m "Give the Preview scene the running score from the replay

Task: Task 2: The Preview scene carries the running score
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/app/preview_view.h","src/app/preview_view.cpp","tests/test_preview_view.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe -tc=\"score box*,build_preview_scene*,build_time_box*\"","acceptanceCriteria":["`hydra_tests.exe -tc=\"score box: no path hides the box\"` passes: state None and `shown == false`.","`hydra_tests.exe -tc=\"score box: a solo's bonus lands on its last note\"` passes: mid-solo the total leaves out the solo bonus, and on the solo's last chord it includes all of it.","`hydra_tests.exe -tc=\"score box: before the first note*\"` passes: `\"0\"` and `\"x1 · combo 0\"`.","`hydra_tests.exe -tc=\"score box: Star Power doubles the multiplier*\"` passes: `\"x1 · combo 5\"` before the activation, `\"x2 · combo 7\"` inside the window, `\"x2 · combo 15\"` after it.","`hydra_tests.exe -tc=\"score box: a path the replay can't reproduce*\"` passes: a wrong stored score, and an activation with no deactivation node, both read `\"Score unavailable\"` with an empty detail line.","`hydra_tests.exe -tc=\"score box: the analyzed chart ends on the path's total\"` passes on the corpus chart.","The whole suite `hydra_tests.exe` passes, with no existing test edited."],"modelTier":"standard"}
```

---

### Task 3: The view model knows where one tick away is

The tick buttons need "the time one tick before or after the playhead". The time box already names the playhead's tick: it asks the song's own tempo map for the tick at that time and rounds to the nearest whole tick (`tick_at` in src/app/preview_view.cpp). This task adds `step_tick_ms`, which starts from that same rounded tick, adds the step, stops at tick 0, and asks the tempo map for that tick's time. Because both use the same rounding, each press changes the time box's tick by exactly one, even across a tempo change.

What the user sees: nothing yet.

**Goal:** `step_tick_ms(scene, now_ms, delta_ticks)` returns the ms of the tick `delta_ticks` away from the tick the time box shows, never before tick 0.

**Files:**
- Modify: `src/app/preview_view.h` (declaration)
- Modify: `src/app/preview_view.cpp` (definition, after `build_score_box`, where `tick_at` is visible)
- Test: `tests/test_preview_view.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="step_tick_ms*"` passes: from the time box's own example (1300 ms, `[1:3:288]`) one step each way reads `[1:3:289]` and `[1:3:287]`; a time between ticks steps from the rounded tick; stepping back from 0 stays at 0; across a 120 to 240 BPM change the two neighbours of the change tick land on the tempo map's own times; a scene with no timing returns the time unchanged.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="step_tick_ms*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests.** Add at the end of tests/test_preview_view.cpp:

```cpp
TEST_CASE("step_tick_ms: one tick from the tick the time box shows") {
    // 120 BPM, 480 ticks per quarter: a tick is 500/480 ms.
    Song song = make_hand_song();
    PreviewScene scene = build_preview_scene(song, nullptr);
    const MsIndex& ms = song.timing().ms_index();

    // 1300 ms is the time box's own example: tick 1248, "[1:3:288]".
    const double fwd = step_tick_ms(scene, 1300.0, 1);
    const double back = step_tick_ms(scene, 1300.0, -1);
    CHECK(fwd == doctest::Approx(ms.at(1249)));
    CHECK(back == doctest::Approx(ms.at(1247)));
    CHECK(build_time_box(scene, fwd, 5000.0).measure_beat == "[1:3:289] / [3:3:000]");
    CHECK(build_time_box(scene, back, 5000.0).measure_beat == "[1:3:287] / [3:3:000]");

    // Between ticks it steps from the rounded tick: 1300.6 ms rounds to 1249.
    CHECK(step_tick_ms(scene, 1300.6, 1) == doctest::Approx(ms.at(1250)));

    // Never before tick 0.
    CHECK(step_tick_ms(scene, 0.0, -1) == doctest::Approx(0.0));
    CHECK(step_tick_ms(scene, 0.5, -3) == doctest::Approx(0.0));
}

TEST_CASE("step_tick_ms: a tempo change moves the tick length with it") {
    // 120 BPM until tick 1920 (2000 ms), then 240 BPM: a tick shrinks from
    // 500/480 ms to 250/480 ms.
    Song song = make_sp_song({}, 3840, {{1920, 240.0}});
    PreviewScene scene = build_preview_scene(song, nullptr);
    const MsIndex& ms = song.timing().ms_index();
    REQUIRE(ms.at(1920) == doctest::Approx(2000.0));

    CHECK(step_tick_ms(scene, 2000.0, 1) == doctest::Approx(ms.at(1921)));
    CHECK(step_tick_ms(scene, 2000.0, -1) == doctest::Approx(ms.at(1919)));
    CHECK(step_tick_ms(scene, 2000.0, 1) - 2000.0 == doctest::Approx(250.0 / 480.0));
    CHECK(2000.0 - step_tick_ms(scene, 2000.0, -1) == doctest::Approx(500.0 / 480.0));
}

TEST_CASE("step_tick_ms: a scene with no song leaves the time alone") {
    PreviewScene empty;
    CHECK(step_tick_ms(empty, 1234.5, 1) == doctest::Approx(1234.5));
}
```

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a compile error naming `step_tick_ms`.

- [ ] **Step 3: Declare it.** In src/app/preview_view.h, after `build_score_box`'s declaration:

```cpp
// The playhead `delta_ticks` chart ticks from `now_ms`, for the Preview's
// tick-step buttons. It starts from the tick the time box shows (the tempo
// map's tick at `now_ms`, rounded to the nearest), so each step changes the
// displayed tick by exactly `delta_ticks`. Never before tick 0. The ms comes
// from the song's own tempo map, so a step follows tempo changes. A scene
// with no timing returns `now_ms` unchanged.
double step_tick_ms(const PreviewScene& scene, double now_ms, int delta_ticks);
```

- [ ] **Step 4: Define it.** In src/app/preview_view.cpp, after `build_score_box`:

```cpp
double step_tick_ms(const PreviewScene& scene, double now_ms, int delta_ticks) {
    if (!scene.timing) return now_ms;
    int64_t target = tick_at(*scene.timing, now_ms < 0.0 ? 0.0 : now_ms) + delta_ticks;
    if (target < 0) target = 0;
    return scene.timing->ms_index().at(target);
}
```

- [ ] **Step 5: Run them and watch them pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="step_tick_ms*"`. Expected: `Status: SUCCESS!`.

- [ ] **Step 6: Commit.**

```bash
git add src/app/preview_view.h src/app/preview_view.cpp tests/test_preview_view.cpp
git commit -m "Add one-tick stepping to the Preview view model

Task: Task 3: The view model knows where one tick away is
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/app/preview_view.h","src/app/preview_view.cpp","tests/test_preview_view.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_tests; .\\build-cpp\\Release\\hydra_tests.exe -tc=\"step_tick_ms*\"","acceptanceCriteria":["`hydra_tests.exe -tc=\"step_tick_ms*\"` passes: from the time box's own example (1300 ms, `[1:3:288]`) one step each way reads `[1:3:289]` and `[1:3:287]`; a time between ticks steps from the rounded tick; stepping back from 0 stays at 0; across a 120 to 240 BPM change the two neighbours of the change tick land on the tempo map's own times; a scene with no timing returns the time unchanged."],"modelTier":"mechanical"}
```

---

### Task 4: The controller jumps, steps and reports the score

The panel talks to the Preview through `PreviewController` (src/ui/preview_controller.h). This task gives it three calls. `jump_ms` moves the playhead by a number of ms and leaves play or pause alone. `step_ticks` pauses and then moves by whole ticks. `score_box` returns the box's text at the playhead.

The controller also has to hand the user's rules to the scene. Today it builds the scene in three places, and none of them passes rules. The background load job (src/ui/preview_load_job.cpp) already holds them, so it just passes them on. The controller keeps its own copy, updated on every `open()`, and passes it to its two in-place rebuilds. Without this, a user with a custom `hydra_rules.ini` would see "Score unavailable" on every path, because the replay would price it under the default rules.

The transport already clamps a seek to the song's ends, so the controller doesn't clamp again.

What the user sees: nothing yet. The buttons come in Task 5. This task's GUI test drives the controller directly inside the real app.

**Goal:** `PreviewController` gains `jump_ms`, `step_ticks` and `score_box`, and every scene build passes the user's rules.

**Files:**
- Modify: `src/ui/preview_controller.h` (three methods, one `core::Rules rules_` member)
- Modify: `src/ui/preview_controller.cpp` (store the rules in `open()`, pass them at both rebuilds, define the three methods)
- Modify: `src/ui/preview_load_job.cpp` (pass `rules_` to `build_preview_scene`)
- Test: `tests/ui/uitest_tests.cpp` (new test `preview-controls`, registered in `register_tests`)

**Acceptance Criteria:**
- [ ] `hydra_uitest.exe --test preview-controls` prints `[PASS]`. It checks five things. With no analysis the score box is hidden. After analysis it shows, and at the song's end it reads the selected path's total with thousands separators. `jump_ms` of ±5000 moves the playhead by exactly that and stops at 0 and at the length. A jump while playing keeps playing. `step_ticks(1)` pauses, and `step_ticks(1)` then `step_ticks(-1)` returns the time box to the same `[measure:beat:tick]`.
- [ ] `hydra_uitest.exe --test preview`, `--test preview-path-overlay` and `--test layout-drift` still print `[PASS]`.
- [ ] `git grep -n "build_preview_scene(" -- src` shows the load job and both controller calls passing a rules argument.

**Verify:** `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test preview-controls --test preview --test preview-path-overlay --test layout-drift` → four `[PASS]` lines

**Steps:**

- [ ] **Step 1: Write the failing GUI test.** In tests/ui/uitest_tests.cpp, add `#include "core/model.h"` beside the other includes if it is not already there (for `hydra::group_thousands`). Add this test after `test_preview_path_overlay`:

```cpp
// The Preview's finer time controls and running score, driven through the
// controller inside the real app. preview-buttons-keys drives the same
// through the panel's buttons and keys.
void test_preview_controls(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;  // chart 0, not analyzed yet
    auto& pc = *h.app->preview;

    // No analyzed path: no score box.
    IM_CHECK(!pc.score_box().shown);

    // Analyze from the Preview tab, then visit Paths and come back so the
    // overlay (and with it the score) is rebuilt from the new record's path.
    ctx->ItemClick("**/Analyze paths!");
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    // Wait for the reload to finish and the score box to appear, rather than
    // assume a fixed number of frames is enough.
    IM_CHECK(wait_until(ctx, [&] { return !pc.loading() && pc.score_box().shown; }, 60));

    // At the song's end the box reads the selected path's total.
    IM_CHECK(pc.length_ms() > 12000.0);
    pc.seek_ms(pc.length_ms());
    hydra::app::PreviewScoreBox end = pc.score_box();
    IM_CHECK(end.shown);
    IM_CHECK(end.available);
    const hydra::Path* shown = h.app->viewed.record->all_paths().front();
    IM_CHECK_STR_EQ(end.score.c_str(), hydra::group_thousands(shown->totalscore()).c_str());

    // 5 s jumps, clamped to the song's ends.
    pc.seek_ms(10000.0);
    pc.jump_ms(5000.0);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 15000.0, 0.5);
    pc.jump_ms(-5000.0);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 10000.0, 0.5);
    pc.jump_ms(-60000.0);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 0.0, 0.5);
    pc.jump_ms(pc.length_ms() + 60000.0);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), pc.length_ms(), 0.5);

    // A jump while playing keeps playing.
    pc.seek_ms(10000.0);
    pc.play();
    IM_CHECK(pc.playing());
    pc.jump_ms(5000.0);
    IM_CHECK(pc.playing());

    // A tick step pauses, and one step each way returns to the same tick.
    pc.step_ticks(0);  // pause and snap onto the displayed tick
    IM_CHECK(!pc.playing());
    const double t0 = pc.position_ms();
    const std::string mb0 = pc.time_box().measure_beat;
    pc.step_ticks(1);
    IM_CHECK(pc.position_ms() > t0);
    IM_CHECK(pc.position_ms() - t0 < 20.0);  // one tick, at any real tempo
    IM_CHECK(pc.time_box().measure_beat != mb0);
    pc.step_ticks(-1);
    IM_CHECK_STR_EQ(pc.time_box().measure_beat.c_str(), mb0.c_str());
}
```

Register it in `register_tests`, after the `preview-path-overlay` entry:

```cpp
        {"preview-controls", test_preview_controls},
```

- [ ] **Step 2: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_uitest`. Expected: compile errors naming `score_box`, `jump_ms` and `step_ticks`.

- [ ] **Step 3: Declare the calls and the rules member.** In src/ui/preview_controller.h, after `void seek_ms(double ms);`:

```cpp
    // Move the playhead by `delta_ms` (the -5s/+5s buttons, Left/Right).
    // Playing stays playing; the transport stops it at the song's ends.
    void jump_ms(double delta_ms);
    // Pause, then move the playhead `delta_ticks` chart ticks from the tick
    // the time box shows (the < Tick / Tick > buttons, comma and period).
    // A step of 0 snaps onto the displayed tick.
    void step_ticks(int delta_ticks);
```

After `hydra::app::PreviewTimeBox time_box() const;`:

```cpp
    // The score box the panel draws under the time box.
    hydra::app::PreviewScoreBox score_box() const;
```

In the private section, after `int sp_cap_ = kCloneHeroSpCap;`:

```cpp
    // The rules the running score is priced under: the user's
    // hydra_rules.ini, as the panel passes it to every open().
    core::Rules rules_ = core::default_rules();
```

- [ ] **Step 4: Store and pass the rules.** In src/ui/preview_controller.cpp, make the first line of `PreviewController::open`'s body:

```cpp
    rules_ = rules;
```

Change the in-place rebuild inside `open()` from:

```cpp
            scene_ = hydra::app::build_preview_scene(*song_, path, sp_cap_);
```

to:

```cpp
            scene_ = hydra::app::build_preview_scene(*song_, path, sp_cap_, rules_);
```

Change the rebuild in `poll()` from:

```cpp
        scene_ = hydra::app::build_preview_scene(*song_, path_ ? &*path_ : nullptr, sp_cap_);
```

to:

```cpp
        scene_ = hydra::app::build_preview_scene(*song_, path_ ? &*path_ : nullptr, sp_cap_,
                                                 rules_);
```

In src/ui/preview_load_job.cpp, change:

```cpp
        app::PreviewScene scene = app::build_preview_scene(source.song, path, sp_cap_);
```

to:

```cpp
        app::PreviewScene scene = app::build_preview_scene(source.song, path, sp_cap_, rules_);
```

- [ ] **Step 5: Define the three calls.** In src/ui/preview_controller.cpp, after `PreviewController::seek_ms`:

```cpp
void PreviewController::jump_ms(double delta_ms) {
    if (!active_ || job_) return;  // nothing loaded yet
    transport_.seek_ms(transport_.now_ms() + delta_ms);
}

void PreviewController::step_ticks(int delta_ticks) {
    if (!active_ || job_) return;
    transport_.pause();
    transport_.seek_ms(hydra::app::step_tick_ms(scene_, transport_.now_ms(), delta_ticks));
}
```

After `PreviewController::time_box`:

```cpp
hydra::app::PreviewScoreBox PreviewController::score_box() const {
    return hydra::app::build_score_box(scene_, transport_.now_ms());
}
```

- [ ] **Step 6: Run it and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test preview-controls --test preview --test preview-path-overlay --test layout-drift`. Expected: four `[PASS]` lines. If `preview-controls` fails at the score check because the Preview did not pick up a path after the Paths round trip, stop and report. Don't weaken the check.

- [ ] **Step 7: Check the call sites.** Run `git grep -n "build_preview_scene(" -- src`. Expected: the load job and both controller lines end with a rules argument (`rules_`). The only other hit is the definition in src/app/preview_view.cpp.

- [ ] **Step 8: Commit.**

```bash
git add src/ui/preview_controller.h src/ui/preview_controller.cpp src/ui/preview_load_job.cpp tests/ui/uitest_tests.cpp
git commit -m "Let the Preview controller jump, step ticks and report the score

Task: Task 4: The controller jumps, steps and reports the score
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/ui/preview_controller.h","src/ui/preview_controller.cpp","src/ui/preview_load_job.cpp","tests/ui/uitest_tests.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_uitest; .\\build-cpp\\Release\\hydra_uitest.exe --test preview-controls --test preview --test preview-path-overlay --test layout-drift","acceptanceCriteria":["`hydra_uitest.exe --test preview-controls` prints `[PASS]`. It checks five things. With no analysis the score box is hidden. After analysis it shows, and at the song's end it reads the selected path's total with thousands separators. `jump_ms` of ±5000 moves the playhead by exactly that and stops at 0 and at the length. A jump while playing keeps playing. `step_ticks(1)` pauses, and `step_ticks(1)` then `step_ticks(-1)` returns the time box to the same `[measure:beat:tick]`.","`hydra_uitest.exe --test preview`, `--test preview-path-overlay` and `--test layout-drift` still print `[PASS]`.","`git grep -n \"build_preview_scene(\" -- src` shows the load job and both controller calls passing a rules argument."],"modelTier":"standard"}
```

---

### Task 5: Buttons, keys and the drawn score box

This is the part the user sees. The Preview panel (`render_preview_panel` in src/ui/details_view.cpp) gets four buttons around Play/Pause, reads the four keys, and draws the score box under the time box.

The buttons use plain `ImGui::Button` with fixed labels, so their widths never change and the scrubber after them can't drift. The scrubber's width is already computed from the space left on the row, so it shrinks to make room without any change.

The keys need one detail. Keyboard navigation is on (app_shell.cpp), and it reads the arrow keys only when nobody owns them. So the panel claims Left and Right every frame while it shows (`ImGui::SetKeyOwner`) and reads them with its own owner ID. The claim made in one frame is still in force during the next frame's navigation update, so even the first press lands in the Preview and not in navigation. Comma and period aren't navigation keys, so they are read the ordinary way. Nothing is claimed or read while a text field has the keyboard (`io.WantTextInput`).

The box is drawn with the time box's own font, panel colour and margins. The score is at 1.8 times the time box's text size, and the detail line is at the time box's size in light grey. "Score unavailable" is drawn at the time box's size.

What the user sees: the five-button transport row with key hints on hover, working arrow, comma and period keys, and the score box once the song is analyzed.

**Goal:** The Preview panel shows `-5s`, `< Tick`, `Play`, `Tick >` and `+5s` with key hints, obeys Left, Right, comma and period (with repeat), and draws the score box under the time box.

**Files:**
- Modify: `src/ui/details_view.cpp` (`render_preview_panel`, plus `#include "imgui_internal.h"` for `SetKeyOwner` and the owner-aware `IsKeyPressed`)
- Test: `tests/ui/uitest_tests.cpp` (new test `preview-buttons-keys`, registered in `register_tests`)

**Acceptance Criteria:**
- [ ] `hydra_uitest.exe --test preview-buttons-keys` prints `[PASS]`. Clicking `+5s` and `-5s`, and pressing Right and Left, each move the playhead by exactly ±5000 ms. Clicking `Tick >` while playing pauses. Clicking `< Tick`, then pressing period and comma, moves the time box's tick forward one and back to where it was.
- [ ] `hydra_uitest.exe --test layout-drift` still prints `[PASS]`, with the test unedited.
- [ ] `hydra_uitest.exe --all` prints `[PASS]` for every test.

**Verify:** `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all` → every line `[PASS]`

**Steps:**

- [ ] **Step 1: Write the failing GUI test.** In tests/ui/uitest_tests.cpp, after `test_preview_controls`:

```cpp
// The Preview's new transport buttons and keys: -5s / +5s and Left / Right
// jump 5 s, < Tick / Tick > and comma / period step one chart tick.
void test_preview_buttons_keys(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    auto& pc = *h.app->preview;
    IM_CHECK(!pc.playing());
    IM_CHECK(pc.length_ms() > 16000.0);

    pc.seek_ms(10000.0);
    ctx->ItemClick("**/+5s");
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 15000.0, 0.5);
    ctx->ItemClick("**/-5s");
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 10000.0, 0.5);

    // The arrows jump exactly 5 s. Had keyboard navigation also taken the
    // arrow and nudged the scrubber, the playhead would be off by the nudge.
    ctx->KeyPress(ImGuiKey_RightArrow);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 15000.0, 0.5);
    ctx->KeyPress(ImGuiKey_LeftArrow);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), 10000.0, 0.5);

    // A tick step pauses.
    ctx->ItemClick("**/Play");
    IM_CHECK(pc.playing());
    ctx->ItemClick("**/Tick >");
    IM_CHECK(!pc.playing());

    // After a step the playhead sits on a tick; period then comma returns
    // the time box to it.
    ctx->ItemClick(("**/" + escape_ref("< Tick")).c_str());
    const double t0 = pc.position_ms();
    const std::string mb0 = pc.time_box().measure_beat;
    ctx->KeyPress(ImGuiKey_Period);
    IM_CHECK(pc.position_ms() > t0);
    IM_CHECK(pc.position_ms() - t0 < 20.0);
    IM_CHECK(pc.time_box().measure_beat != mb0);
    ctx->KeyPress(ImGuiKey_Comma);
    IM_CHECK_STR_EQ(pc.time_box().measure_beat.c_str(), mb0.c_str());
}
```

Register it after `preview-controls`:

```cpp
        {"preview-buttons-keys", test_preview_buttons_keys},
```

- [ ] **Step 2: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test preview-buttons-keys`. Expected: `[FAIL]`, because the item `+5s` is not found.

- [ ] **Step 3: Add the include.** In src/ui/details_view.cpp, after `#include "imgui.h"`:

```cpp
#include "imgui_internal.h"  // SetKeyOwner, owner-aware IsKeyPressed
```

- [ ] **Step 4: Add the buttons.** In `render_preview_panel`, replace:

```cpp
    // Transport row: play/pause, a scrubber, and the time readout. Every
    // piece whose text changes while playing sits in a fixed slot (see
    // widgets.h): otherwise the Vol slider walked under a held mouse as the
    // readout's digits changed width.
    const float play_w = std::max(button_slot_width("Play"), button_slot_width("Pause"));
    if (button_in_slot(pc->playing() ? "Pause" : "Play", play_w)) pc->toggle();
    ImGui::SameLine();
```

with:

```cpp
    // Transport row: back 5 s, back a tick, play/pause, forward a tick,
    // forward 5 s, a scrubber, and the time readout. Every piece whose text
    // changes while playing sits in a fixed slot (see widgets.h): otherwise
    // the Vol slider walked under a held mouse as the readout's digits
    // changed width. The four step buttons have fixed labels, so they need
    // no slot.
    if (ImGui::Button("-5s")) pc->jump_ms(-5000.0);
    hint("Back 5 seconds (Left arrow)");
    ImGui::SameLine();
    if (ImGui::Button("< Tick")) pc->step_ticks(-1);
    hint("Back one tick (Comma)");
    ImGui::SameLine();
    const float play_w = std::max(button_slot_width("Play"), button_slot_width("Pause"));
    if (button_in_slot(pc->playing() ? "Pause" : "Play", play_w)) pc->toggle();
    ImGui::SameLine();
    if (ImGui::Button("Tick >")) pc->step_ticks(1);
    hint("Forward one tick (Period)");
    ImGui::SameLine();
    if (ImGui::Button("+5s")) pc->jump_ms(5000.0);
    hint("Forward 5 seconds (Right arrow)");
    ImGui::SameLine();
```

- [ ] **Step 5: Read the keys.** Right after the volume block (the line `if (ImGui::IsItemDeactivatedAfterEdit()) app.commit_settings();`), add:

```cpp
    // Keys: Left/Right jump 5 s, comma/period step one tick; a held key
    // repeats. Not while a text field has the keyboard. Keyboard navigation
    // (on in app_shell.cpp) reads the arrows only when nobody owns them, so
    // the Preview claims Left and Right every frame it shows; a claim made
    // this frame still holds during next frame's navigation update, so even
    // the first press lands here rather than moving focus or nudging the
    // scrubber.
    if (!ImGui::GetIO().WantTextInput) {
        const ImGuiID keys_owner = ImGui::GetID("##preview_keys");
        ImGui::SetKeyOwner(ImGuiKey_LeftArrow, keys_owner);
        ImGui::SetKeyOwner(ImGuiKey_RightArrow, keys_owner);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, ImGuiInputFlags_Repeat, keys_owner))
            pc->jump_ms(-5000.0);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, ImGuiInputFlags_Repeat, keys_owner))
            pc->jump_ms(5000.0);
        if (ImGui::IsKeyPressed(ImGuiKey_Comma, true)) pc->step_ticks(-1);
        if (ImGui::IsKeyPressed(ImGuiKey_Period, true)) pc->step_ticks(1);
    }
```

- [ ] **Step 6: Draw the score box.** In the same function, right after the time box's text loop (the `for` that calls `dl->AddText(... lines[i])`) and before the Star Power meter comment, add:

```cpp
        // The score box, under the time box in the same panel style: the
        // running score in large type, then "x<mult> · combo <n>" in light
        // grey. Absent until the chart is analyzed; "Score unavailable" (at
        // the time box's size) when the path can't be replayed to its stored
        // score. Right corners rounded, since it sits against the left edge.
        hydra::app::PreviewScoreBox score = pc->score_box();
        if (score.shown) {
            const float score_size = score.available ? size * 1.8f : size;
            const float score_h = score_size * 1.2f;
            const float gap = px(6.0f);
            const float score_w =
                font->CalcTextSizeA(score_size, FLT_MAX, 0.0f, score.score.c_str()).x;
            const float detail_w =
                score.detail.empty()
                    ? 0.0f
                    : font->CalcTextSizeA(size, FLT_MAX, 0.0f, score.detail.c_str()).x;
            const float lines_h = score_h + (score.detail.empty() ? 0.0f : line_h);
            ImVec2 s_min(origin.x, box_max.y + gap);
            ImVec2 s_max(origin.x + margin + std::max(score_w, detail_w) + pad * 2.0f,
                         s_min.y + pad + lines_h + pad);
            dl->AddRectFilled(s_min, s_max, IM_COL32(0, 0, 0, 128), px(6.0f),
                              ImDrawFlags_RoundCornersRight);
            dl->AddText(font, score_size, ImVec2(origin.x + margin, s_min.y + pad),
                        IM_COL32(255, 255, 255, 255), score.score.c_str());
            if (!score.detail.empty())
                dl->AddText(font, size, ImVec2(origin.x + margin, s_min.y + pad + score_h),
                            IM_COL32(200, 200, 200, 255), score.detail.c_str());
        }
```

- [ ] **Step 7: Run it and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test preview-buttons-keys --test layout-drift`. Expected: two `[PASS]` lines. If `preview-buttons-keys` fails on an arrow-key check, the navigation claim isn't working. Stop and report what the playhead read. Don't loosen the 0.5 ms tolerance.

- [ ] **Step 8: Run everything.** Run `.\build-cpp\Release\hydra_uitest.exe --all`, then `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: every uitest line `[PASS]`, and `Status: SUCCESS!`.

- [ ] **Step 9: Commit.**

```bash
git add src/ui/details_view.cpp tests/ui/uitest_tests.cpp
git commit -m "Add 5 s and one-tick buttons, their keys, and the score box to the Preview

Task: Task 5: Buttons, keys and the drawn score box
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

```json:metadata
{"files":["src/ui/details_view.cpp","tests/ui/uitest_tests.cpp"],"verifyCommand":".\\build_cpp.ps1 -Target hydra_uitest; .\\build-cpp\\Release\\hydra_uitest.exe --all","acceptanceCriteria":["`hydra_uitest.exe --test preview-buttons-keys` prints `[PASS]`. Clicking `+5s` and `-5s`, and pressing Right and Left, each move the playhead by exactly ±5000 ms. Clicking `Tick >` while playing pauses. Clicking `< Tick`, then pressing period and comma, moves the time box's tick forward one and back to where it was.","`hydra_uitest.exe --test layout-drift` still prints `[PASS]`, with the test unedited.","`hydra_uitest.exe --all` prints `[PASS]` for every test."],"modelTier":"standard"}
```

---

## After the last task

The drawn box itself (its size, colour and position) can't be read back as text, so no automated test covers its looks. When the tasks are merged, the user opens an analyzed song's Preview and checks the box against the mockup. The executor doesn't take screenshots to check it.
