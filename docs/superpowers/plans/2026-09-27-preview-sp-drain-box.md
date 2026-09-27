# Preview SP Drain Box Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show, beside the Preview's SP gauge, how fast Star Power drains at the playhead.

**Architecture:** A pure function, `build_drain_box(scene, now_ms)`, turns the Preview scene into the box's three text lines. It sits next to `build_score_box` in `src/app/preview_view.cpp`, so it is tested without a window. The controller hands it the playhead, the same way it does for the score box. `details_view.cpp` draws the three lines next to the gauge. Every number comes from the path's record or from timing functions the engine already uses. Nothing re-derives Star Power.

**Tech Stack:** C++20, Dear ImGui, doctest (`hydra_tests`), Dear ImGui Test Engine (`hydra_uitest`), CMake via `build_cpp.ps1`.

**Spec:** `docs/superpowers/specs/2026-09-27-preview-sp-drain-box-design.md`

## Global Constraints

The box's text is fixed. The header is `SP drain` while the path has SP running, and `SP drain (if activated)` otherwise. The rate line is `1 bar / X.X s`. The detail line is `empties in X.X s` while active and `full meter X.X s` otherwise. Every number has one decimal place, in seconds.

The sources are fixed as well:

- **Whether SP is running:** a `PreviewActivation` with `has_sp_end`, where `ms <= now < sp_end_ms`.
- **"empties in":** `sp_end_ms - now`.
- **The rate:** `kMeasuresPerSpBar * SongTiming::ms_per_measure_at(now_tick)`.
- **"full meter":** `SongTiming::plusmeasure(timecode(now_tick), sp_bars_to_measures(scene.sp_meter.cap))`, minus the ms of `now_tick`.
- **`now_tick`:** the existing `tick_at` helper in `preview_view.cpp`.

The code must not read the gauge's curve (`SpMeterCurve` segments) for any value, and must not call `SongTiming::sp_end_ms`. The box shows only when the gauge shows. That means `scene.timing` is set and `scene.sp_meter.segments` is not empty.

Colors match the existing overlay. Active header and detail are SP gold `IM_COL32(255, 204, 51, 255)`. Idle header and detail are light grey `IM_COL32(200, 200, 200, 255)`. The rate line is white. The panel is `IM_COL32(0, 0, 0, 128)`, the same as the time box. The gauge, the time box and the score box do not change.

**User decisions (already made):**
- "show rate if activated" — when SP isn't running, the box stays visible in grey as "SP drain (if activated)".
- "just the drain box" — no drain-speed strip on the scrubber.
- "verify that this new module will only pull information from existing code instead re-deriving starpower" — the sources listed above.
- Spec approved 2026-09-27, including "full meter" that follows upcoming tempo changes, and "empties in" replacing "full meter" while active.

---

## Files

`src/app/preview_view.h` and `src/app/preview_view.cpp` gain `PreviewDrainBox` and `build_drain_box`. This is the only place the numbers are worked out.

`tests/test_preview_view.cpp` gains the unit tests. Its `make_sp_song` fixture gets one optional meter-change argument for the 7/8 test.

`src/ui/preview_controller.h` and `src/ui/preview_controller.cpp` gain a one-line `drain_box()` accessor.

`src/ui/details_view.cpp` draws the box inside the existing SP-gauge block.

`tests/ui/uitest_tests.cpp` gains one GUI test, `preview-drain-box`.

---

### Task 1: The drain box view-model, with unit tests

**Goal:** `build_drain_box(scene, now_ms)` returns the box's visibility, active flag and three lines, with every value taken from the record or the engine's timing calls.

**Files:**
- Modify: `src/app/preview_view.h` (new struct and declaration, after `build_score_box`'s declaration)
- Modify: `src/app/preview_view.cpp` (definition, after `build_score_box`'s definition, where `tick_at` is visible)
- Test: `tests/test_preview_view.cpp` (optional `extra_tpm` on `make_sp_song`, and six new `TEST_CASE`s)

**Acceptance Criteria:**
- [ ] A default-built scene and a scene with no SP phrases and no path both give `shown == false`.
- [ ] At 120 BPM in 4/4 with no path, 2000 ms reads `SP drain (if activated)`, `1 bar / 4.0 s`, `full meter 16.0 s`.
- [ ] With 60 BPM from tick 5760 (6000 ms), 5990 ms reads `1 bar / 4.0 s` and 6000 ms reads `1 bar / 8.0 s`. "full meter" at 2000 ms reads `full meter 28.0 s`, equal to the `plusmeasure` result.
- [ ] With 7/8 (1680 ticks per measure) from tick 3840, 2000 ms reads `1 bar / 4.0 s` and 4000 ms reads `1 bar / 3.5 s`.
- [ ] Inside a stored window from 4000 to 12000 ms, 4000 ms is active with `SP drain` and `empties in 8.0 s`. 9000 ms reads `empties in 3.0 s`. 3999 ms and 12000 ms are idle.
- [ ] With a stored end that a mid-SP collection pushed out to 16000 ms, 12000 ms is active with `empties in 4.0 s`.
- [ ] An activation with no stored end leaves 5000 ms idle, with a `full meter` detail.
- [ ] The whole `hydra_tests` suite reports `Status: SUCCESS!`.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="drain box*"` → `Status: SUCCESS!`, then `.\build-cpp\Release\hydra_tests.exe` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Give the SP fixture an optional meter change.** In `tests/test_preview_view.cpp`, `make_sp_song` builds a 120 BPM 4/4 song. Add one trailing parameter so the 7/8 test can reuse it. Existing callers don't change. Replace the function's signature and its timing lines:

```cpp
Song make_sp_song(const std::vector<int64_t>& phrase_ends, int64_t last_tick,
                  const std::map<int64_t, double>& extra_bpm = {},
                  int64_t step = 480,
                  const std::map<int64_t, int64_t>& extra_tpm = {}) {
    Song song(480);
    song.bpm_changes[0] = 120.0;
    for (const auto& kv : extra_bpm) song.bpm_changes[kv.first] = kv.second;
    for (const auto& kv : extra_tpm) song.tpm_changes[kv.first] = kv.second;
    song.build_timing();
```

Also add one sentence to the comment above it: "`extra_tpm` adds meter changes (ticks per measure) the same way."

- [ ] **Step 2: Write the failing tests.** Add these at the end of `tests/test_preview_view.cpp`, before any closing namespace. Every fixture is 480 ticks per quarter at 120 BPM in 4/4. A measure is 1920 ticks and 2000 ms, so one bar of SP (two measures) lasts 4000 ms.

```cpp
// ---- SP drain box -------------------------------------------------------

TEST_CASE("drain box: hidden without an SP gauge") {
    CHECK_FALSE(build_drain_box(PreviewScene{}, 1000.0).shown);
    // Notes but no SP phrase and no path: no gauge, so no box.
    Song song = make_sp_song({}, /*last_tick=*/3840);
    PreviewScene scene = build_preview_scene(song, nullptr);
    REQUIRE(scene.sp_meter.segments.empty());
    CHECK_FALSE(build_drain_box(scene, 1000.0).shown);
}

TEST_CASE("drain box: idle at a steady tempo reads the rate and a full meter") {
    Song song = make_sp_song({960}, /*last_tick=*/13440);
    PreviewScene scene = build_preview_scene(song, nullptr);

    PreviewDrainBox box = build_drain_box(scene, 2000.0);
    CHECK(box.shown);
    CHECK_FALSE(box.active);
    CHECK(box.header == "SP drain (if activated)");
    CHECK(box.rate == "1 bar / 4.0 s");
    // Cap 4: eight measures, 16000 ms.
    CHECK(box.detail == "full meter 16.0 s");
}

TEST_CASE("drain box: the rate switches exactly at a tempo change") {
    // 120 BPM until tick 5760 (6000 ms), then 60 BPM: a bar goes 4 s -> 8 s.
    Song song = make_sp_song({960}, /*last_tick=*/13440, {{5760, 60.0}});
    PreviewScene scene = build_preview_scene(song, nullptr);

    CHECK(build_drain_box(scene, 5990.0).rate == "1 bar / 4.0 s");
    CHECK(build_drain_box(scene, 6000.0).rate == "1 bar / 8.0 s");

    // A full meter from tick 1920 (2000 ms): two measures at 120 BPM reach
    // the change at 6000 ms, then six measures at 4000 ms each end at 30000.
    PreviewDrainBox box = build_drain_box(scene, 2000.0);
    CHECK(box.detail == "full meter 28.0 s");
    // ...which is exactly the engine's own SP-end call.
    const SongTiming& t = song.timing();
    const Timecode start = t.timecode(1920);
    const Timecode end = t.plusmeasure(start, sp_bars_to_measures(4));
    CHECK(end.ms() - start.ms() == doctest::Approx(28000.0));
}

TEST_CASE("drain box: a 7/8 section drains faster at the same BPM") {
    // 7/8 from tick 3840 (4000 ms, a barline): 1680 ticks = 1750 ms a
    // measure, so a bar lasts 3500 ms instead of 4000.
    Song song = make_sp_song({960}, /*last_tick=*/13440, {}, 480, {{3840, 1680}});
    PreviewScene scene = build_preview_scene(song, nullptr);

    CHECK(build_drain_box(scene, 2000.0).rate == "1 bar / 4.0 s");
    CHECK(build_drain_box(scene, 4000.0).rate == "1 bar / 3.5 s");
}

TEST_CASE("drain box: active inside the stored SP window, idle outside it") {
    // Two bars at tick 3840 (4000 ms): the stored end is tick 11520 (12000 ms).
    Song song = make_sp_song({960}, /*last_tick=*/13440);
    Path path;
    path.activations = {sp_act_at(song, 3840, /*sp_meter=*/2)};
    PreviewScene scene = build_preview_scene(song, &path);

    PreviewDrainBox before = build_drain_box(scene, 3999.0);
    CHECK_FALSE(before.active);
    CHECK(before.header == "SP drain (if activated)");

    PreviewDrainBox at = build_drain_box(scene, 4000.0);
    CHECK(at.active);
    CHECK(at.header == "SP drain");
    CHECK(at.rate == "1 bar / 4.0 s");
    CHECK(at.detail == "empties in 8.0 s");
    CHECK(build_drain_box(scene, 9000.0).detail == "empties in 3.0 s");

    // At the stored end SP is over, as the gauge reads that boundary too.
    PreviewDrainBox after = build_drain_box(scene, 12000.0);
    CHECK_FALSE(after.active);
    CHECK(after.detail.rfind("full meter ", 0) == 0);
}

TEST_CASE("drain box: empties in reads the stored end, not a recount") {
    // A phrase collected mid-SP pushes the stored end two measures past the
    // plain act + 4 measures: 16000 ms, not 12000. Only the record knows.
    Song song = make_sp_song({960, 5760}, /*last_tick=*/17280);
    Path path;
    Activation act = sp_act_at(song, 3840, /*sp_meter=*/2);
    act.deact_tick = 3840 + 6 * 1920;
    act.collected_phrase_ticks = {5760};
    path.activations = {act};
    PreviewScene scene = build_preview_scene(song, &path);

    PreviewDrainBox box = build_drain_box(scene, 12000.0);
    CHECK(box.active);
    CHECK(box.detail == "empties in 4.0 s");
}

TEST_CASE("drain box: an activation with no stored end stays idle") {
    // A pre-v4 record: no deact node, so nothing says SP is running.
    Song song = make_sp_song({960, 1920}, /*last_tick=*/9600);
    Path path;
    path.activations = {act_at(song, 3840, /*skips=*/0)};
    PreviewScene scene = build_preview_scene(song, &path);
    REQUIRE_FALSE(scene.activations[0].has_sp_end);

    PreviewDrainBox box = build_drain_box(scene, 5000.0);
    CHECK(box.shown);
    CHECK_FALSE(box.active);
    CHECK(box.detail.rfind("full meter ", 0) == 0);
}
```

- [ ] **Step 3: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors naming `PreviewDrainBox` and `build_drain_box`.

- [ ] **Step 4: Declare the box.** In `src/app/preview_view.h`, add this right after the `build_score_box` declaration:

```cpp
// The Star Power drain box the Preview draws beside the SP gauge, at `now_ms`.
// `rate` is how long one bar of SP lasts at the playhead ("1 bar / 4.0 s"):
// kMeasuresPerSpBar measures at the local measure length the song's timing
// gives (SongTiming::ms_per_measure_at). The box is `active` when the path has
// SP running at the playhead: an activation at or before it whose stored deact
// node is after it. Then `detail` is "empties in X s", that stored end minus
// the playhead. Otherwise `detail` is "full meter X s": how long the meter's
// cap would last if activated at the playhead's tick, from the engine's own
// SongTiming::plusmeasure. Hidden (`shown` false) when the scene has no SP
// gauge or no timing. Nothing here re-derives Star Power: it reads the record
// and the timing calls the engine itself makes.
struct PreviewDrainBox {
    bool shown = false;
    bool active = false;
    std::string header;  // "SP drain" or "SP drain (if activated)"
    std::string rate;    // "1 bar / 4.0 s"
    std::string detail;  // "empties in 7.3 s" or "full meter 16.0 s"
};

PreviewDrainBox build_drain_box(const PreviewScene& scene, double now_ms);
```

- [ ] **Step 5: Build the box.** In `src/app/preview_view.cpp`, add this right after `build_score_box`'s definition. It sits outside the anonymous namespace, below it, so `tick_at` is visible.

```cpp
PreviewDrainBox build_drain_box(const PreviewScene& scene, double now_ms) {
    PreviewDrainBox box;
    if (!scene.timing || scene.sp_meter.segments.empty()) return box;
    box.shown = true;

    const SongTiming& timing = *scene.timing;
    const double now = now_ms < 0.0 ? 0.0 : now_ms;
    const int64_t now_tick = tick_at(timing, now);

    // The rule's own constant at the local measure length: on a tempo or
    // meter change the new section is read, as the time box's BPM line does.
    char buf[64];
    const double bar_ms =
        static_cast<double>(kMeasuresPerSpBar) * timing.ms_per_measure_at(now_tick);
    std::snprintf(buf, sizeof buf, "1 bar / %.1f s", bar_ms / 1000.0);
    box.rate = buf;

    // SP is running when the playhead sits in an activation's stored window.
    // A record with no deact node cannot say, so it reads idle.
    const PreviewActivation* running = nullptr;
    for (const PreviewActivation& a : scene.activations) {
        if (a.has_sp_end && a.ms <= now && now < a.sp_end_ms) {
            running = &a;
            break;
        }
    }

    if (running != nullptr) {
        box.active = true;
        box.header = "SP drain";
        std::snprintf(buf, sizeof buf, "empties in %.1f s",
                      (running->sp_end_ms - now) / 1000.0);
    } else {
        box.header = "SP drain (if activated)";
        // The engine's own SP-end call, from the playhead's tick.
        const Timecode start = timing.timecode(now_tick);
        const Timecode end =
            timing.plusmeasure(start, sp_bars_to_measures(scene.sp_meter.cap));
        std::snprintf(buf, sizeof buf, "full meter %.1f s",
                      (end.ms() - start.ms()) / 1000.0);
    }
    box.detail = buf;
    return box;
}
```

- [ ] **Step 6: Run them and watch them pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="drain box*"`. Expected: `Status: SUCCESS!`. Then run the whole suite, `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`.

- [ ] **Step 7: Commit.**

```bash
git add src/app/preview_view.h src/app/preview_view.cpp tests/test_preview_view.cpp
git commit -m "Preview: SP drain box view-model

build_drain_box reads the drain speed at the playhead from the SP rule's
constant and the song's measure length, 'empties in' from the record's
stored deact node, and 'full meter' from the engine's own plusmeasure.

Task: Task 1: drain box view-model
Agent: <your agent name>
Session: <your session id>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Draw the box on the Preview, with a GUI test

**Goal:** The Preview draws the drain box bottom-right, just left of the SP gauge. A `hydra_uitest` test shows it wired to the playhead and to the viewed path.

**Files:**
- Modify: `src/ui/preview_controller.h` (accessor after `score_box()`, around line 152)
- Modify: `src/ui/preview_controller.cpp` (definition after `score_box()`, around line 265)
- Modify: `src/ui/details_view.cpp` (inside the SP-gauge block, around lines 793-823)
- Test: `tests/ui/uitest_tests.cpp` (new `test_preview_drain_box`, registered as `preview-drain-box`)

**Acceptance Criteria:**
- [ ] `PreviewController::drain_box()` returns `build_drain_box(scene_, transport_.now_ms())`.
- [ ] On chart 0, once analyzed, playhead 0 gives a shown, idle box. The header is `SP drain (if activated)`, the rate starts with `1 bar / `, and the detail starts with `full meter `.
- [ ] Walking the playhead in 50 ms steps finds a time where the box is active, with header `SP drain` and a detail starting with `empties in `.
- [ ] The test saves `drain-box-active.png`. In it, the box sits left of the gauge, the header and detail are gold, and the box doesn't cover the gauge.
- [ ] `hydra_uitest --all` reports every test `[PASS]`, and `hydra_tests` still reports `Status: SUCCESS!`.

**Verify:** `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test preview-drain-box --keep-temp` → `[PASS] preview-drain-box` plus a `screenshot:` line with the PNG's path, then `.\build-cpp\Release\hydra_uitest.exe --all` → every test `[PASS]`

**Steps:**

- [ ] **Step 1: Write the failing GUI test.** In `tests/ui/uitest_tests.cpp`, add this after `test_preview_controls`:

```cpp
// The SP drain box beside the gauge. Its values are build_drain_box's, pinned
// by the unit tests; this checks the panel feeds it the playhead and the
// viewed path, and saves a frame of the active box to look at.
void test_preview_drain_box(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;  // chart 0, not analyzed yet
    auto& pc = *h.app->preview;

    // Analyze, then visit Paths and come back so the overlay is rebuilt from
    // the new record's path (as preview-controls does).
    ctx->ItemClick("**/Analyze paths!");
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.record.has_value());
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    IM_CHECK(wait_until(ctx, [&] { return !pc.loading() && pc.score_box().shown; }, 60));
    IM_CHECK(pc.sp_meter_has_curve());

    // Before anything is banked or spent: the idle box.
    pc.seek_ms(0.0);
    hydra::app::PreviewDrainBox idle = pc.drain_box();
    IM_CHECK(idle.shown);
    IM_CHECK(!idle.active);
    IM_CHECK_STR_EQ(idle.header.c_str(), "SP drain (if activated)");
    IM_CHECK(idle.rate.rfind("1 bar / ", 0) == 0);
    IM_CHECK(idle.detail.rfind("full meter ", 0) == 0);

    // Somewhere the path has SP running. Walk the playhead to find it, so the
    // test needs no timing of its own.
    double active_ms = -1.0;
    for (double t = 0.0; t <= pc.length_ms() && active_ms < 0.0; t += 50.0) {
        pc.seek_ms(t);
        if (pc.drain_box().active) active_ms = t;
    }
    IM_CHECK(active_ms >= 0.0);
    pc.seek_ms(active_ms);
    hydra::app::PreviewDrainBox on = pc.drain_box();
    IM_CHECK_STR_EQ(on.header.c_str(), "SP drain");
    IM_CHECK(on.rate.rfind("1 bar / ", 0) == 0);
    IM_CHECK(on.detail.rfind("empties in ", 0) == 0);

    // A frame of the active box, for a person to look at.
    ctx->Yield(2);
    IM_CHECK(screenshot(ctx, "drain-box-active.png"));
}
```

In `register_tests()`, add this after the `{"preview-controls", test_preview_controls},` line:

```cpp
        {"preview-drain-box", test_preview_drain_box},
```

- [ ] **Step 2: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_uitest`. Expected: a compile error that `PreviewController` has no member `drain_box`.

- [ ] **Step 3: Add the accessor.** In `src/ui/preview_controller.h`, add this after the `score_box()` declaration:

```cpp
    // The SP drain box the panel draws beside the gauge.
    hydra::app::PreviewDrainBox drain_box() const;
```

In `src/ui/preview_controller.cpp`, add this after `score_box()`'s definition:

```cpp
hydra::app::PreviewDrainBox PreviewController::drain_box() const {
    return hydra::app::build_drain_box(scene_, transport_.now_ms());
}
```

- [ ] **Step 4: Draw it.** In `src/ui/details_view.cpp`, the SP gauge is drawn inside `if (pc->sp_meter_has_curve()) { ... }`. That block defines `img_max`, `v_margin` and `gauge_min`, and it has an inner `if (gauge_max.y > gauge_min.y) { ... }` that draws the gauge. Add the code below after that inner `if` closes, still inside the outer block. `font`, `size`, `line_h` and `pad` are already in scope from the time box above.

```cpp
            // The SP drain box, bottom-right just left of the gauge, in the
            // time box's panel style and right-aligned: how long a bar of SP
            // lasts at the playhead, then "empties in" (gold, SP running on
            // the path) or "full meter" (grey, if activated here). Every
            // number is build_drain_box's.
            hydra::app::PreviewDrainBox drain = pc->drain_box();
            if (drain.shown) {
                const char* d_lines[3] = {drain.header.c_str(), drain.rate.c_str(),
                                          drain.detail.c_str()};
                float d_w = 0.0f;
                for (const char* l : d_lines)
                    d_w = std::max(d_w, font->CalcTextSizeA(size, FLT_MAX, 0.0f, l).x);
                const float d_gap = px(6.0f);
                ImVec2 d_max(gauge_min.x - d_gap, img_max.y - v_margin);
                ImVec2 d_min(d_max.x - d_w - pad * 2.0f, d_max.y - line_h * 3.0f - pad * 2.0f);
                dl->AddRectFilled(d_min, d_max, IM_COL32(0, 0, 0, 128), px(6.0f));
                const ImU32 accent = drain.active ? IM_COL32(255, 204, 51, 255)  // SP gold
                                                  : IM_COL32(200, 200, 200, 255);
                const ImU32 colors[3] = {accent, IM_COL32(255, 255, 255, 255), accent};
                for (int i = 0; i < 3; ++i) {
                    const float lw = font->CalcTextSizeA(size, FLT_MAX, 0.0f, d_lines[i]).x;
                    dl->AddText(font, size,
                                ImVec2(d_max.x - pad - lw, d_min.y + pad + line_h * static_cast<float>(i)),
                                colors[i], d_lines[i]);
                }
            }
```

- [ ] **Step 5: Run it and watch it pass.** Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test preview-drain-box --keep-temp`. Expected: `[PASS] preview-drain-box`, and a `screenshot:` line naming the PNG.

- [ ] **Step 6: Look at the frame.** Open the PNG from the `screenshot:` line with the Read tool. Check that the box sits bottom-right, left of the gold gauge, with a gold header and detail and a white rate line. The box must not overlap the gauge. If it overlaps the gauge, stop and report with the screenshot. Don't adjust the layout on your own.

- [ ] **Step 7: Run everything.** Run `.\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test `[PASS]`. Then run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`.

- [ ] **Step 8: Commit.**

```bash
git add src/ui/preview_controller.h src/ui/preview_controller.cpp src/ui/details_view.cpp tests/ui/uitest_tests.cpp
git commit -m "Preview: draw the SP drain box beside the gauge

The box sits bottom-right, left of the SP gauge, in the time box's panel
style: gold while the path has SP running, grey 'if activated' elsewhere.
preview-drain-box checks it follows the playhead and the viewed path.

Task: Task 2: draw the drain box
Agent: <your agent name>
Session: <your session id>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The user looks at it in the real app

**Goal:** The user confirms the box reads right while playing a real chart, and that it doesn't cover the highway in a narrower window.

**Files:**
- none (a look at the running app; nothing is edited unless the user asks)

**Acceptance Criteria:**
- [ ] The user has opened an analyzed chart's Preview in `build-cpp\Release\Hydra.exe` and played through at least one activation.
- [ ] The user has said whether the box covers the highway after they narrow the Song Details window.
- [ ] If they say it covers the highway, the box is moved to the top-right beside the top of the gauge, as the spec says, and Task 2's checks are run again.

**Verify:** The user's reply in chat.

**Steps:**

- [ ] **Step 1: Hand it over.** Tell the user the build is at `build-cpp\Release\Hydra.exe`. Ask them to open an analyzed chart's Preview, play through an activation, and then narrow the Song Details window. Ask two things: does the speed change where they expect, and does the box ever sit on the highway?

- [x] **Step 2: Act on the answer.** Done differently: the user's look led to the revision below (Tasks 4 to 7), which replaces the simple top-right move with scaling plus the move.

---

## Revision after the first look (2026-09-27)

The user ran Tasks 1 and 2 on "One [Metallica]" (Periphery) and asked for three changes. The spec's section "Revision after the first look" is the source for everything below.

**Revision constraints (binding on Tasks 4 to 7):**
- "full meter X s" is `cap * kMeasuresPerSpBar * ms_per_measure_at(now_tick)`. It jumps exactly when the rate line does. `build_drain_box` no longer calls `plusmeasure`.
- The time box's new line reads exactly `Time signature: N/D`, for example `Time signature: 6/4`. It sits directly under the BPM line. It shows the signature in force at the playhead's tick, as the chart wrote it, and `Time signature: 4/4` before any.
- `Song::timesig_changes` is written only by `apply_timesig`, the same call that writes `tpm_changes`. The engine keeps reading only `tpm_changes`, and nothing stored in the database changes.
- The time box, score box and drain box share one scale from `render::overlay_scale`. It is 1 whenever there's room and never below `kOverlayMinScale` (0.6). The gauge is not scaled.
- The drain box sits top-right: its top is level with the gauge's top (`origin.y + v_margin`), and its right edge is `d_gap` left of the gauge.
- The track height has one home, `render::track_height`, used by both `PreviewRenderer::resize` and the overlay layout.

**User decisions (revision):**
- "yes do the snap" — "full meter" jumps with the section.
- "go with the recommended fix" — the boxes stop shrinking at the readable minimum and overlap below it.
- "go with recommended" — the drain box moves top-right and scales with the others.
- "make it say time signature" — the label is "Time signature:".
- "add time signature as a new line under BPM".

Task 4 and Task 5 can run in parallel. Task 6 starts from Task 5's branch, because it redraws the time box that Task 5 adds a line to. Task 7 is the user's.

---

### Task 4: "full meter" snaps with the section

**Goal:** `build_drain_box`'s idle detail becomes the cap times the bar time at the playhead, so it jumps at the exact tick the rate line jumps.

**Files:**
- Modify: `src/app/preview_view.h` (the `PreviewDrainBox` comment)
- Modify: `src/app/preview_view.cpp` (the idle branch of `build_drain_box`)
- Test: `tests/test_preview_view.cpp` (the two existing drain box tests named below; no new test cases, and nothing added at the end of the file)

**Acceptance Criteria:**
- [ ] With 60 BPM from tick 5760 (6000 ms), 2000 ms and 5990 ms read `full meter 16.0 s`, and 6000 ms reads `full meter 32.0 s`.
- [ ] With 7/8 from tick 3840, 4000 ms reads `full meter 14.0 s`.
- [ ] `build_drain_box` contains no call to `plusmeasure`.
- [ ] The whole `hydra_tests` suite reports `Status: SUCCESS!`.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="drain box*"` → `Status: SUCCESS!`, then `.\build-cpp\Release\hydra_tests.exe` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Change the tests first.** In `tests/test_preview_view.cpp`, in `TEST_CASE("drain box: the rate switches exactly at a tempo change")`, replace everything from the comment `// A full meter from tick 1920 (2000 ms)` to the end of that test case with:

```cpp
    // "full meter" is the cap at the bar time in force, so it jumps with the
    // rate, at the change and not before: 4 x 4 s, then 4 x 8 s.
    CHECK(build_drain_box(scene, 2000.0).detail == "full meter 16.0 s");
    CHECK(build_drain_box(scene, 5990.0).detail == "full meter 16.0 s");
    CHECK(build_drain_box(scene, 6000.0).detail == "full meter 32.0 s");
}
```

In `TEST_CASE("drain box: a 7/8 section drains faster at the same BPM")`, add this line after the `4000.0` rate check:

```cpp
    CHECK(build_drain_box(scene, 4000.0).detail == "full meter 14.0 s");
```

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="drain box*"`. Expected: the 2000 ms check fails with `full meter 28.0 s`, and the 5990 ms check fails too.

- [ ] **Step 3: Snap the detail.** In `src/app/preview_view.cpp`, in `build_drain_box`, replace the `else` branch with:

```cpp
    } else {
        box.header = "SP drain (if activated)";
        // The cap at the bar time in force here, so it jumps exactly when the
        // rate line does rather than blending in the sections ahead.
        std::snprintf(buf, sizeof buf, "full meter %.1f s",
                      bar_ms * static_cast<double>(scene.sp_meter.cap) / 1000.0);
    }
```

In `src/app/preview_view.h`, in the comment above `struct PreviewDrainBox`, replace the sentences starting at "Otherwise `detail` is" up to "SongTiming::plusmeasure." with:

```cpp
// Otherwise `detail` is "full meter X s": the meter's cap in bars at that same
// bar time, so it jumps exactly when the rate does.
```

Also change the last sentence of that comment to: "Nothing here re-derives Star Power: it reads the record and the song's own timing."

- [ ] **Step 4: Run them and watch them pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="drain box*"`. Expected: `Status: SUCCESS!`. Then run `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`.

- [ ] **Step 5: Commit** on branch `drain/T4`, with the message "Preview: full meter snaps with the section" and a body line: "It is now the cap at the bar time in force, so it jumps with the rate instead of blending in the next eight measures." Trailers: `Task: Task 4: full meter snaps`, `Agent: <your agent name>`, `Session: <your session id>`, `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

---

### Task 5: Time signature line in the time box

**Goal:** The parser keeps each chart time signature as written, and the time box shows `Time signature: N/D` under BPM.

**Files:**
- Modify: `src/parse/song.h` (`Song::timesig_changes`, and its default in the constructor)
- Modify: `src/parse/song.cpp` (`apply_timesig`)
- Modify: `src/app/preview_view.h` (`PreviewTimeSig`, `PreviewScene::time_sigs`, `PreviewTimeBox::time_sig`)
- Modify: `src/app/preview_view.cpp` (fill `time_sigs` in `build_preview_scene`; the line in `build_time_box`)
- Modify: `src/ui/details_view.cpp` (draw the line under BPM)
- Test: `tests/test_song.cpp` (one new test case, right after `TEST_CASE(".chart: the note on the solo end tick is in the solo")`)
- Test: `tests/test_preview_view.cpp` (one new test case, right after `TEST_CASE("build_time_box: a mid-measure meter change follows the engine")`; nothing added at the end of the file)

**Acceptance Criteria:**
- [ ] A `.chart` with `TS 4`, `TS 6 3` and `TS 3` keeps (4,4), (6,8) and (3,4), while `tpm_changes` holds 576 for both 6/8 and 3/4.
- [ ] A new `Song` has `timesig_changes` = {0: (4,4)}.
- [ ] `build_time_box(...).time_sig` reads `Time signature: 4/4`, then `Time signature: 6/8` from tick 1920, then `Time signature: 3/4` from tick 3360. A default-built scene reads `Time signature: 4/4`.
- [ ] The time box draws the line directly under the BPM line.
- [ ] `hydra_tests` reports `Status: SUCCESS!` and `hydra_uitest --all` reports every test `[PASS]`.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*time signature*"` → `Status: SUCCESS!`; `.\build-cpp\Release\hydra_tests.exe` → `Status: SUCCESS!`; `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all` → every test `[PASS]`

**Steps:**

- [ ] **Step 1: Write the failing tests.** In `tests/test_song.cpp`, right after `TEST_CASE(".chart: the note on the solo end tick is in the solo")`, add:

```cpp
// The parser keeps each time signature as the chart wrote it, beside the
// measure length the engine reads. The length alone cannot tell 6/8 from 3/4.
TEST_CASE(".chart: time signatures are kept as written") {
    const std::string text =
        "[Song]\n{\n  Resolution = 192\n}\n"
        "[SyncTrack]\n{\n"
        "  0 = TS 4\n  0 = B 120000\n"
        "  768 = TS 6 3\n"
        "  1344 = TS 3\n"
        "}\n"
        "[ExpertDrums]\n{\n  0 = N 0 0\n  1536 = N 1 0\n}\n";
    const std::vector<uint8_t> data(text.begin(), text.end());
    Song song = load_songbytes_chart(data, true, true);

    // 6/8 and 3/4 are both 576 ticks at 192 per quarter note.
    CHECK(song.tpm_changes.at(768) == 576);
    CHECK(song.tpm_changes.at(1344) == 576);
    CHECK(song.timesig_changes.at(0) == std::make_pair(4, 4));
    CHECK(song.timesig_changes.at(768) == std::make_pair(6, 8));
    CHECK(song.timesig_changes.at(1344) == std::make_pair(3, 4));

    // A chart with no signature at all reads the default, 4/4 from tick 0.
    Song blank(480);
    CHECK(blank.timesig_changes.size() == 1);
    CHECK(blank.timesig_changes.at(0) == std::make_pair(4, 4));
}
```

In `tests/test_preview_view.cpp`, right after `TEST_CASE("build_time_box: a mid-measure meter change follows the engine")`, add:

```cpp
TEST_CASE("build_time_box: the time signature in force, as the chart wrote it") {
    // 120 BPM, 480 ticks per quarter. 6/8 from tick 1920 (2000 ms), then 3/4
    // from tick 3360 (3500 ms). Both are 1440 ticks a measure, so only the
    // stored signature tells them apart.
    Song song(480);
    song.bpm_changes[0] = 120.0;
    song.tpm_changes[1920] = 1440;
    song.timesig_changes[1920] = {6, 8};
    song.tpm_changes[3360] = 1440;
    song.timesig_changes[3360] = {3, 4};
    song.build_timing();
    {
        Chord c;
        c.add_note(NoteColor::Red);
        SongTimestamp ts;
        ts.timecode = song.timecode(4800);
        ts.chord = std::move(c);
        song.sequence.push_back(std::move(ts));
    }
    PreviewScene scene = build_preview_scene(song, nullptr);

    CHECK(build_time_box(scene, 0.0, 5000.0).time_sig == "Time signature: 4/4");
    CHECK(build_time_box(scene, 1999.0, 5000.0).time_sig == "Time signature: 4/4");
    CHECK(build_time_box(scene, 2000.0, 5000.0).time_sig == "Time signature: 6/8");
    CHECK(build_time_box(scene, 3500.0, 5000.0).time_sig == "Time signature: 3/4");
    // A scene built from nothing reads the chart default.
    CHECK(build_time_box(PreviewScene{}, 0.0, 0.0).time_sig == "Time signature: 4/4");
}
```

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: compile errors naming `timesig_changes` and `time_sig`.

- [ ] **Step 3: Keep the signature in the song.** In `src/parse/song.h`, in `class Song`, change the constructor body to also set the default signature:

```cpp
    explicit Song(int64_t resolution) : tick_resolution_(resolution) {
        tpm_changes[0] = resolution * 4;
        timesig_changes[0] = {4, 4};
    }
```

After the `std::map<int64_t, double> bpm_changes;` line, add:

```cpp
    // The chart's own time signatures, tick -> (numerator, denominator),
    // recorded by the same parser call that writes tpm_changes. Display only
    // (the Preview's time box): the engine reads tpm_changes, and a length
    // alone cannot tell 6/8 from 3/4.
    std::map<int64_t, std::pair<int, int>> timesig_changes;
```

Add `#include <utility>` to song.h's includes if it is not already there. In `src/parse/song.cpp`, replace `apply_timesig` with:

```cpp
// A time signature: ticks per measure = resolution * 4 * num / den. The
// signature itself is kept too, for display.
void apply_timesig(Song& song, int64_t tick, int numerator, int denominator) {
    song.tpm_changes[tick] = song.tick_resolution() * static_cast<int64_t>(numerator) * 4 /
                             static_cast<int64_t>(denominator);
    song.timesig_changes[tick] = {numerator, denominator};
}
```

- [ ] **Step 4: Carry it into the scene and the time box.** In `src/app/preview_view.h`, after `struct PreviewMeter { ... };`, add:

```cpp
// A time signature as the chart wrote it, for the time box's line.
struct PreviewTimeSig {
    int64_t tick = 0;
    int numerator = 4;
    int denominator = 4;
};
```

In `struct PreviewScene`, after the `meters` member, add:

```cpp
    std::vector<PreviewTimeSig> time_sigs; // the chart's own signatures, tick order
```

In `struct PreviewTimeBox`, after `std::string bpm;`, add `std::string time_sig;`. In the comment above that struct, change "the BPM in force, and the practice section in force" to "the BPM in force, the time signature in force ("Time signature: 6/4"), and the practice section in force", and change "the box is three lines tall then" to "the box is four lines tall then".

In `src/app/preview_view.cpp`, in `build_preview_scene`, right after the block that fills `scene.meters`, add:

```cpp
    for (const auto& [tick, sig] : song.timesig_changes)
        scene.time_sigs.push_back({tick, sig.first, sig.second});
```

In `build_time_box`, right after `box.bpm = buf;`, add:

```cpp
    // The time signature in force at the playhead's tick, as the chart wrote
    // it; 4/4 before any, the chart default.
    int ts_num = 4, ts_den = 4;
    for (const PreviewTimeSig& t : scene.time_sigs) {
        if (t.tick > now_tick) break;
        ts_num = t.numerator;
        ts_den = t.denominator;
    }
    std::snprintf(buf, sizeof buf, "Time signature: %d/%d", ts_num, ts_den);
    box.time_sig = buf;
```

- [ ] **Step 5: Draw the line.** In `src/ui/details_view.cpp`, in the time box code, change `const char* lines[4];` to `const char* lines[5];`, and right after `lines[line_count++] = box.bpm.c_str();` add:

```cpp
        lines[line_count++] = box.time_sig.c_str();
```

In the comment above it, change "BPM, and the practice section" to "BPM, the time signature, and the practice section".

- [ ] **Step 6: Run everything.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="*time signature*"`. Expected: `Status: SUCCESS!`. Then `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`. Then `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test `[PASS]`.

- [ ] **Step 7: Commit** on branch `drain/T5`, with the message "Preview: time signature line in the time box" and a body line: "The parser keeps each signature as written beside the measure length (which cannot tell 6/8 from 3/4); the time box shows the one in force under BPM." Trailers: `Task: Task 5: time signature line`, `Agent: <your agent name>`, `Session: <your session id>`, `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

---

### Task 6: The boxes scale to fit beside the highway; the drain box moves top-right

**Goal:** The time, score and drain boxes share one scale that keeps them beside the highway, and the drain box sits top-right. The scale is worked out from the renderer's own camera.

**Files:**
- Modify: `src/render/highway_draw.h` and `src/render/highway_draw.cpp` (new `track_height`)
- Modify: `src/render/preview_renderer.cpp` (`resize` uses `track_height`)
- Create: `src/render/overlay_layout.h` and `src/render/overlay_layout.cpp` (`project_to_image`, `highway_span_at`, `OverlayBoxes`, `overlay_scale`)
- Modify: `CMakeLists.txt` (add `src/render/overlay_layout.cpp` to `hydra_render`, and `tests/test_overlay_layout.cpp` to the test list)
- Modify: `src/ui/preview_controller.h` (`set_overlay_scale` / `overlay_scale`)
- Modify: `src/ui/details_view.cpp` (the overlay block is measured, fitted, then drawn)
- Test: `tests/test_overlay_layout.cpp` (new)
- Test: `tests/ui/uitest_tests.cpp` (new `test_preview_overlay_fit`, registered as `preview-overlay-fit`)

**Acceptance Criteria:**
- [ ] `track_height` returns min(height, round(width × ratio)), at least 1, and `PreviewRenderer::resize` calls it instead of its own formula.
- [ ] `highway_span_at` passes through the projected railing corners, is symmetric about the image centre, is wider lower down, and keeps its width when only the image width changes.
- [ ] `overlay_scale` is 1 with room, `kOverlayMinScale` in a very narrow image, never falls as the image widens, and whenever it is between the two the boxes clear the highway.
- [ ] In the GUI test, the default harness display gives scale 1. A 640 px display gives a scale below 1 and at least 0.6, and saves `overlay-fit-narrow.png`.
- [ ] In the screenshots, the drain box is top-right beside the top of the gauge, and the time box shows the time signature line.
- [ ] `hydra_tests` reports `Status: SUCCESS!` and `hydra_uitest --all` reports every test `[PASS]`.

**Verify:** `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="track_height*,highway_span_at*,overlay_scale*"` → `Status: SUCCESS!`; `.\build-cpp\Release\hydra_tests.exe` → `Status: SUCCESS!`; `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test preview-overlay-fit --test preview-drain-box --keep-temp` → both `[PASS]` and two `screenshot:` lines; `.\build-cpp\Release\hydra_uitest.exe --all` → every test `[PASS]`

**Steps:**

- [ ] **Step 0: Start from Task 5.** This task's branch starts from `drain/T5`, not from the plan commit: `git checkout -B drain/T6 drain/T5`. It needs Task 5's `time_sig` line.

- [ ] **Step 1: Write the failing unit tests.** Create `tests/test_overlay_layout.cpp`:

```cpp
// Tests for render/overlay_layout: where the highway lands on screen, and the
// scale that keeps the Preview's text boxes beside it. Device-free.

#include "doctest.h"

#include "render/highway_draw.h"
#include "render/overlay_layout.h"
#include "render/preview_config.h"

using namespace hydra::render;

TEST_CASE("track_height: the width ratio, capped by the image height") {
    PreviewConfig cfg;  // height_width_ratio 1.1666...
    CHECK(track_height(cfg, 1000, 300) == 300);  // height-limited: the usual case
    CHECK(track_height(cfg, 300, 1000) == 350);  // 300 * 1.1666 = 350
    CHECK(track_height(cfg, 0, 0) == 1);
}

TEST_CASE("highway_span_at: passes through the projected railing corners") {
    PreviewConfig cfg;
    const PreviewConfig::Track& T = cfg.track;
    const int w = 1200, h = 400;
    const float xr = T.x_right + T.railing_x_width;
    const float xl = T.x_left - T.railing_x_width;
    // The strike line lies between the railing's two ends, so both edges run
    // through its corners at their rows (or outside them, the wider line winning).
    const ImagePoint r = project_to_image(cfg, w, h, {xr, T.railing_y_top, T.z_now});
    const ImagePoint l = project_to_image(cfg, w, h, {xl, T.railing_y_top, T.z_now});
    CHECK(highway_span_at(cfg, w, h, r.y).right >= r.x - 0.01f);
    CHECK(highway_span_at(cfg, w, h, l.y).left <= l.x + 0.01f);
    // A point on a lane lies inside the span at its own row.
    const ImagePoint lane = project_to_image(cfg, w, h, {0.9f, T.y, -6.0f});
    const HighwaySpan s = highway_span_at(cfg, w, h, lane.y);
    CHECK(lane.x < s.right);
    CHECK(lane.x > s.left);
}

TEST_CASE("highway_span_at: symmetric, and wider nearer the camera") {
    PreviewConfig cfg;
    const int w = 1200, h = 400;
    const HighwaySpan top = highway_span_at(cfg, w, h, 60.0f);
    const HighwaySpan bottom = highway_span_at(cfg, w, h, 390.0f);
    CHECK(top.left + top.right == doctest::Approx(static_cast<double>(w)).epsilon(0.001));
    CHECK(bottom.left + bottom.right == doctest::Approx(static_cast<double>(w)).epsilon(0.001));
    CHECK(bottom.right - bottom.left > top.right - top.left);
}

TEST_CASE("highway_span_at: a narrower image leaves the highway's size alone") {
    // The track height follows the image height here, so the highway is the
    // same size; a narrower image only trims the empty sides.
    PreviewConfig cfg;
    const HighwaySpan wide = highway_span_at(cfg, 1200, 400, 390.0f);
    const HighwaySpan narrow = highway_span_at(cfg, 600, 400, 390.0f);
    CHECK(narrow.right - narrow.left ==
          doctest::Approx(static_cast<double>(wide.right - wide.left)).epsilon(0.01));
}

TEST_CASE("overlay_scale: full size with room, floored when narrow, clear in between") {
    PreviewConfig cfg;
    const int h = 390;
    auto boxes = [](int w) {
        OverlayBoxes b;
        b.left_w = 265.0f;
        b.left_h = 150.0f;
        b.right_w = 200.0f;
        b.right_h = 70.0f;
        b.right_top = 10.0f;
        b.right_edge = static_cast<float>(w) - 30.0f;
        b.gap = 6.0f;
        return b;
    };
    CHECK(overlay_scale(cfg, 1200, h, boxes(1200)) == 1.0f);
    CHECK(overlay_scale(cfg, 150, h, boxes(150)) == doctest::Approx(kOverlayMinScale));

    float prev = 0.0f;
    bool saw_partial = false;
    for (int w = 150; w <= 1200; w += 10) {
        const OverlayBoxes b = boxes(w);
        const float s = overlay_scale(cfg, w, h, b);
        CHECK(s >= prev - 1e-6f);  // never falls as the image widens
        prev = s;
        if (s > kOverlayMinScale && s < 1.0f) {
            saw_partial = true;
            const float left_room = highway_span_at(cfg, w, h, b.left_h).left - b.gap;
            const float right_room =
                b.right_edge - highway_span_at(cfg, w, h, b.right_top + b.right_h).right - b.gap;
            CHECK(b.left_w * s <= left_room + 0.01f);
            CHECK(b.right_w * s <= right_room + 0.01f);
        }
    }
    CHECK(saw_partial);
    // No boxes: nothing to fit.
    CHECK(overlay_scale(cfg, 300, h, OverlayBoxes{}) == 1.0f);
}
```

In `CMakeLists.txt`, add `    tests/test_overlay_layout.cpp` on the line after `    tests/test_highway_draw.cpp`, and add `    src/render/overlay_layout.cpp` on the line after `    src/render/highway_draw.cpp` in `add_library(hydra_render STATIC ...)`.

- [ ] **Step 2: Run them and watch them fail.** Run `.\build_cpp.ps1 -Target hydra_tests`. Expected: a failure naming the missing `render/overlay_layout.h` (CMake may also complain that `src/render/overlay_layout.cpp` does not exist yet).

- [ ] **Step 3: One home for the track height.** In `src/render/highway_draw.h`, after the `make_camera` declaration, add:

```cpp
// The track rectangle's height in a width x height preview: Onyx lays out one
// highway min(height, width * height_width_ratio) tall, anchored at the
// bottom. The renderer sizes its scene target with this and the overlay
// layout projects through it, so the two cannot disagree. At least 1.
int track_height(const PreviewConfig& cfg, int width, int height);
```

In `src/render/highway_draw.cpp`, after `make_camera`'s definition, add:

```cpp
int track_height(const PreviewConfig& cfg, int width, int height) {
    const int w = std::max(1, width);
    const int h = std::max(1, height);
    const int t = std::min(h, static_cast<int>(std::lround(w * cfg.view.height_width_ratio)));
    return std::max(1, t);
}
```

Add `#include <algorithm>` and `#include <cmath>` to highway_draw.cpp if they are not already there. In `src/render/preview_renderer.cpp`, in `PreviewRenderer::resize`, replace the two `d.track_h = ...` lines with:

```cpp
    d.track_h = track_height(d.cfg, d.width, d.height);
```

- [ ] **Step 4: The layout module.** Create `src/render/overlay_layout.h`:

```cpp
// Where the highway lands on the Preview image, and the scale that keeps the
// Preview's text boxes (the time and score boxes top-left, the SP drain box
// top-right) beside it rather than on it. Device-free: the same camera and
// track height the renderer draws with, projected on the CPU.
//
// The highway's size follows the track height, not the image width, so a
// narrower window leaves less room at its sides. The boxes shrink into that
// room, never past kOverlayMinScale (below it the text stops being readable,
// so they overlap instead) and never above 1 (their configured size).

#ifndef HYDRA_RENDER_OVERLAY_LAYOUT_H
#define HYDRA_RENDER_OVERLAY_LAYOUT_H

#include "render/preview_config.h"

namespace hydra::render {

// A point on the image, in pixels from its top-left corner (x right, y down).
struct ImagePoint {
    float x = 0.0f;
    float y = 0.0f;
};

// Where world point `p` lands in a width x height Preview image, through the
// renderer's own camera, with the track rectangle anchored at the bottom.
ImagePoint project_to_image(const PreviewConfig& cfg, int width, int height, const Vec3& p);

// The highway's outer edges (the railings' outsides) at image row `y`. Each
// railing edge is a straight line on screen, read off its two projected ends;
// the railing's top and bottom give two lines and the wider one is returned.
// Above the far end the far end's edges are used: nothing is drawn there.
struct HighwaySpan {
    float left = 0.0f;
    float right = 0.0f;
};
HighwaySpan highway_span_at(const PreviewConfig& cfg, int width, int height, float y);

// The boxes to fit, at scale 1, in image pixels. A width of 0 means absent.
struct OverlayBoxes {
    float left_w = 0.0f;      // the top-left column (time box, score box under it)
    float left_h = 0.0f;      //   from the image's top edge
    float right_w = 0.0f;     // the SP drain box
    float right_h = 0.0f;
    float right_edge = 0.0f;  // x of the drain box's right edge, left of the gauge
    float right_top = 0.0f;   // y of the drain box's top edge
    float gap = 0.0f;         // clearance kept from the highway's edge
};

inline constexpr float kOverlayMinScale = 0.6f;

// One scale for all the boxes: the largest in [min_scale, 1] at which each box
// clears the highway at its lowest row (where the highway is widest within
// it). Rows are measured at scale 1, so a shrunken box is only more clear.
float overlay_scale(const PreviewConfig& cfg, int width, int height, const OverlayBoxes& boxes,
                    float min_scale = kOverlayMinScale);

}  // namespace hydra::render

#endif  // HYDRA_RENDER_OVERLAY_LAYOUT_H
```

Create `src/render/overlay_layout.cpp`:

```cpp
// See overlay_layout.h.

#include "render/overlay_layout.h"

#include <algorithm>
#include <cmath>

#include <DirectXMath.h>

#include "render/highway_draw.h"

using namespace DirectX;

namespace hydra::render {

ImagePoint project_to_image(const PreviewConfig& cfg, int width, int height, const Vec3& p) {
    const int w = std::max(1, width);
    const int h = std::max(1, height);
    const int th = track_height(cfg, w, h);
    const HighwayCamera cam = make_camera(cfg, static_cast<float>(w) / static_cast<float>(th));
    const XMMATRIX view_proj = XMLoadFloat4x4(&cam.view) * XMLoadFloat4x4(&cam.proj);
    const XMVECTOR ndc = XMVector3TransformCoord(XMVectorSet(p.x, p.y, p.z, 1.0f), view_proj);
    ImagePoint out;
    out.x = (XMVectorGetX(ndc) + 1.0f) * 0.5f * static_cast<float>(w);
    // The track rectangle hugs the bottom of the image.
    out.y = static_cast<float>(h - th) + (1.0f - XMVectorGetY(ndc)) * 0.5f * static_cast<float>(th);
    return out;
}

HighwaySpan highway_span_at(const PreviewConfig& cfg, int width, int height, float y) {
    const PreviewConfig::Track& T = cfg.track;
    // x at row y on the screen line through a railing edge's two ends.
    auto x_at = [&](float wx, float wy) {
        const ImagePoint a = project_to_image(cfg, width, height, {wx, wy, T.z_past});
        const ImagePoint b = project_to_image(cfg, width, height, {wx, wy, T.z_future});
        if (std::fabs(a.y - b.y) < 1e-3f) return a.x;
        const float row = std::max(y, b.y);  // above the far end: the far end's edge
        return a.x + (row - a.y) * (b.x - a.x) / (b.y - a.y);
    };
    const float xl = T.x_left - T.railing_x_width;
    const float xr = T.x_right + T.railing_x_width;
    HighwaySpan s;
    s.left = std::min(x_at(xl, T.railing_y_top), x_at(xl, T.railing_y_bottom));
    s.right = std::max(x_at(xr, T.railing_y_top), x_at(xr, T.railing_y_bottom));
    return s;
}

float overlay_scale(const PreviewConfig& cfg, int width, int height, const OverlayBoxes& boxes,
                    float min_scale) {
    float scale = 1.0f;
    if (boxes.left_w > 0.0f) {
        const float room = highway_span_at(cfg, width, height, boxes.left_h).left - boxes.gap;
        scale = std::min(scale, room / boxes.left_w);
    }
    if (boxes.right_w > 0.0f) {
        const float bottom = boxes.right_top + boxes.right_h;
        const float room =
            boxes.right_edge - highway_span_at(cfg, width, height, bottom).right - boxes.gap;
        scale = std::min(scale, room / boxes.right_w);
    }
    return std::clamp(scale, min_scale, 1.0f);
}

}  // namespace hydra::render
```

- [ ] **Step 5: Run the unit tests and watch them pass.** Run `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe -tc="track_height*,highway_span_at*,overlay_scale*"`. Expected: `Status: SUCCESS!`. Then run `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`. That includes the golden render test, which shows `resize` still sizes the track the same way.

- [ ] **Step 6: Write the failing GUI test.** In `tests/ui/uitest_tests.cpp`, add `#include "render/overlay_layout.h"` after `#include "core/model.h"`. After `test_preview_drain_box`, add:

```cpp
// Sets the harness display width for one test and puts it back however the
// test ends, so a failed check cannot leave later tests on a narrow display.
struct DisplayWidth {
    Harness& h;
    int saved;
    DisplayWidth(Harness& harness_, int width) : h(harness_), saved(harness_.width) {
        h.width = width;
    }
    ~DisplayWidth() { h.width = saved; }
};

// The Preview's text boxes shrink to sit beside the highway in a narrow
// window and keep their size in a normal one. The Song Details modal follows
// the display size, so narrowing the display narrows the Preview.
void test_preview_overlay_fit(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_preview(ctx)) return;
    auto& pc = *h.app->preview;

    ctx->Yield(3);
    IM_CHECK_EQ(pc.overlay_scale(), 1.0f);  // the default display: full size
    IM_CHECK(screenshot(ctx, "overlay-fit-wide.png"));

    DisplayWidth narrow(h, 640);
    ctx->Yield(5);
    IM_CHECK(pc.overlay_scale() < 1.0f);
    IM_CHECK(pc.overlay_scale() >= hydra::render::kOverlayMinScale);
    IM_CHECK(screenshot(ctx, "overlay-fit-narrow.png"));
}
```

In `register_tests()`, add `        {"preview-overlay-fit", test_preview_overlay_fit},` after the `preview-drain-box` line.

- [ ] **Step 7: Run it and watch it fail.** Run `.\build_cpp.ps1 -Target hydra_uitest`. Expected: a compile error that `PreviewController` has no member `overlay_scale`.

- [ ] **Step 8: Let the controller hold the last drawn scale.** In `src/ui/preview_controller.h`, after the `drain_box()` declaration, add:

```cpp
    // The text overlays' scale the panel last drew at (1 = full size). The
    // panel sets it each frame; only the GUI test reads it back.
    void set_overlay_scale(float scale) { overlay_scale_ = scale; }
    float overlay_scale() const { return overlay_scale_; }
```

In its `private:` section, add `    float overlay_scale_ = 1.0f;`.

- [ ] **Step 9: Measure, fit, then draw.** In `src/ui/details_view.cpp`, add `#include "render/overlay_layout.h"` after `#include "imgui_internal.h"  // SetKeyOwner, owner-aware IsKeyPressed`. Then replace everything inside `if (srv != nullptr && w > 0 && h > 0) { ... }`, from the line after `ImGui::Image(...)` to the brace that closes that `if`, with:

```cpp

        // The text overlays: the time box and score box top-left, the SP drain
        // box top-right beside the gauge. They share one scale, fitted by
        // render::overlay_scale so they sit beside the highway: their
        // configured size whenever there is room, smaller in a narrow window,
        // never below kOverlayMinScale (under that they overlap rather than
        // become unreadable). Everything is measured at scale 1 first, then
        // drawn at the fitted scale. The gauge keeps its size.
        ImFont* font = g_mono_font ? g_mono_font : ImGui::GetFont();
        const render::PreviewConfig& pcfg = pc->preview_config();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetItemRectMin();
        const ImVec2 img_max = ImGui::GetItemRectMax();
        auto text_width = [font](float sz, const char* s) {
            return font->CalcTextSizeA(sz, FLT_MAX, 0.0f, s).x;
        };

        // The time box's lines, the way Onyx draws its own (top-left,
        // monospace, on a translucent dark panel): time / length,
        // [measure:beat:tick] for both, BPM, the time signature, and the
        // practice section (absent on charts that have none).
        hydra::app::PreviewTimeBox box = pc->time_box();
        const char* lines[5];
        int line_count = 0;
        lines[line_count++] = box.timestamp.c_str();
        lines[line_count++] = box.measure_beat.c_str();
        lines[line_count++] = box.bpm.c_str();
        lines[line_count++] = box.time_sig.c_str();
        if (!box.section.empty()) lines[line_count++] = box.section.c_str();
        hydra::app::PreviewScoreBox score = pc->score_box();
        const bool has_gauge = pc->sp_meter_has_curve();
        hydra::app::PreviewDrainBox drain = pc->drain_box();
        const bool drain_drawn = has_gauge && drain.shown;
        const char* d_lines[3] = {drain.header.c_str(), drain.rate.c_str(),
                                  drain.detail.c_str()};

        // The gauge's geometry, which is not scaled.
        const float bar_w = px(14.0f);
        const float inset = px(10.0f);
        const float v_margin = px(10.0f);
        const float d_gap = px(6.0f);  // between the drain box and the gauge
        const float gauge_left = img_max.x - inset - bar_w;

        // Scale-1 sizes, and the extents the fit needs (image pixels).
        const float size1 = px(pcfg.text.time_box_size);
        const float margin1 = px(pcfg.text.time_box_margin);
        const float pad1 = px(8.0f);
        const float gap1 = px(6.0f);
        const float line_h1 = size1 * 1.25f;
        float time_text_w1 = 0.0f;
        for (int i = 0; i < line_count; ++i)
            time_text_w1 = std::max(time_text_w1, text_width(size1, lines[i]));
        render::OverlayBoxes fit;
        fit.left_w = margin1 + time_text_w1 + pad1 * 2.0f;
        fit.left_h = margin1 + line_h1 * static_cast<float>(line_count) + pad1;
        if (score.shown) {
            const float score_size1 = score.available ? size1 * 1.8f : size1;
            float score_w1 = text_width(score_size1, score.score.c_str());
            if (!score.detail.empty())
                score_w1 = std::max(score_w1, text_width(size1, score.detail.c_str()));
            const float score_lines_h1 =
                score_size1 * 1.2f + (score.detail.empty() ? 0.0f : line_h1);
            fit.left_w = std::max(fit.left_w, margin1 + score_w1 + pad1 * 2.0f);
            fit.left_h += gap1 + pad1 + score_lines_h1 + pad1;
        }
        if (drain_drawn) {
            float drain_w1 = 0.0f;
            for (const char* l : d_lines) drain_w1 = std::max(drain_w1, text_width(size1, l));
            fit.right_w = drain_w1 + pad1 * 2.0f;
            fit.right_h = line_h1 * 3.0f + pad1 * 2.0f;
            fit.right_edge = gauge_left - d_gap - origin.x;
            fit.right_top = v_margin;
        }
        fit.gap = gap1;
        const float scale = render::overlay_scale(pcfg, w, h, fit);
        pc->set_overlay_scale(scale);

        const float size = size1 * scale;
        const float margin = margin1 * scale;
        const float pad = pad1 * scale;
        const float gap = gap1 * scale;
        const float line_h = size * 1.25f;
        const float corner = px(6.0f) * scale;

        // The time box.
        float text_w = 0.0f;
        for (int i = 0; i < line_count; ++i) text_w = std::max(text_w, text_width(size, lines[i]));
        ImVec2 box_min(origin.x, origin.y);
        ImVec2 box_max(origin.x + margin + text_w + pad * 2.0f,
                       origin.y + margin + line_h * static_cast<float>(line_count) + pad);
        dl->AddRectFilled(box_min, box_max, IM_COL32(0, 0, 0, 128), corner,
                          ImDrawFlags_RoundCornersBottomRight);
        for (int i = 0; i < line_count; ++i)
            dl->AddText(font, size, ImVec2(origin.x + margin, origin.y + margin + line_h * i),
                        IM_COL32(255, 255, 255, 255), lines[i]);

        // The score box, under the time box in the same panel style: the
        // running score in large type, then "x<mult> · combo <n>" in light
        // grey. Absent until the chart is analyzed; "Score unavailable" (at
        // the time box's size) when the path can't be replayed to its stored
        // score. Right corners rounded, since it sits against the left edge.
        if (score.shown) {
            const float score_size = score.available ? size * 1.8f : size;
            const float score_h = score_size * 1.2f;
            const float score_w = text_width(score_size, score.score.c_str());
            const float detail_w =
                score.detail.empty() ? 0.0f : text_width(size, score.detail.c_str());
            const float lines_h = score_h + (score.detail.empty() ? 0.0f : line_h);
            ImVec2 s_min(origin.x, box_max.y + gap);
            ImVec2 s_max(origin.x + margin + std::max(score_w, detail_w) + pad * 2.0f,
                         s_min.y + pad + lines_h + pad);
            dl->AddRectFilled(s_min, s_max, IM_COL32(0, 0, 0, 128), corner,
                              ImDrawFlags_RoundCornersRight);
            dl->AddText(font, score_size, ImVec2(origin.x + margin, s_min.y + pad),
                        IM_COL32(255, 255, 255, 255), score.score.c_str());
            if (!score.detail.empty())
                dl->AddText(font, size, ImVec2(origin.x + margin, s_min.y + pad + score_h),
                            IM_COL32(200, 200, 200, 255), score.detail.c_str());
        }

        // The Star Power meter: a gauge down the image's right edge, filling
        // bottom-up as phrases are collected and draining while SP is active.
        // Hydra's own overlay, like the time box above -- not part of the Onyx
        // render. The value is the view-model's curve read at the playhead, so
        // it is anchored to the same engine truth the path overlay is.
        if (has_gauge) {
            ImVec2 gauge_min(gauge_left, origin.y + v_margin);
            ImVec2 gauge_max(img_max.x - inset, img_max.y - v_margin);
            if (gauge_max.y > gauge_min.y) {
                dl->AddRectFilled(gauge_min, gauge_max, IM_COL32(0, 0, 0, 128), px(4.0f));

                const int cap = std::max(1, pc->sp_meter_cap());
                float fill = static_cast<float>(pc->sp_meter_bars()) / static_cast<float>(cap);
                fill = fill < 0.0f ? 0.0f : (fill > 1.0f ? 1.0f : fill);

                const float fill_pad = px(2.0f);
                ImVec2 in_min(gauge_min.x + fill_pad, gauge_min.y + fill_pad);
                ImVec2 in_max(gauge_max.x - fill_pad, gauge_max.y - fill_pad);
                const float in_h = in_max.y - in_min.y;
                if (in_h > 0.0f && fill > 0.0f)
                    dl->AddRectFilled(ImVec2(in_min.x, in_max.y - in_h * fill), in_max,
                                      IM_COL32(255, 204, 51, 230));  // Star Power gold
                // One line per whole-bar boundary, over the fill, so a glance
                // reads how many bars are banked and not just how full it is.
                for (int b = 1; b < cap; ++b) {
                    const float y = in_max.y - in_h * (static_cast<float>(b) /
                                                       static_cast<float>(cap));
                    dl->AddLine(ImVec2(in_min.x, y), ImVec2(in_max.x, y),
                                IM_COL32(0, 0, 0, 160), px(1.0f));
                }
            }
        }

        // The SP drain box, top-right just left of the gauge and level with
        // its top, where the highway is narrowest; right-aligned in the time
        // box's panel style. How long a bar of SP lasts at the playhead, then
        // "empties in" (gold, SP running on the path) or "full meter" (grey,
        // if activated here). Every number is build_drain_box's.
        if (drain_drawn) {
            float d_w = 0.0f;
            for (const char* l : d_lines) d_w = std::max(d_w, text_width(size, l));
            ImVec2 d_min(gauge_left - d_gap - d_w - pad * 2.0f, origin.y + v_margin);
            ImVec2 d_max(gauge_left - d_gap, d_min.y + line_h * 3.0f + pad * 2.0f);
            dl->AddRectFilled(d_min, d_max, IM_COL32(0, 0, 0, 128), corner);
            const ImU32 accent = drain.active ? IM_COL32(255, 204, 51, 255)  // SP gold
                                              : IM_COL32(200, 200, 200, 255);
            const ImU32 colors[3] = {accent, IM_COL32(255, 255, 255, 255), accent};
            for (int i = 0; i < 3; ++i) {
                const float lw = text_width(size, d_lines[i]);
                dl->AddText(font, size,
                            ImVec2(d_max.x - pad - lw, d_min.y + pad + line_h * static_cast<float>(i)),
                            colors[i], d_lines[i]);
            }
        }
    }
```

The last line above (`    }`) is the brace that closes `if (srv != nullptr && w > 0 && h > 0)`. Check that the function's own closing brace still follows it.

- [ ] **Step 10: Run the GUI tests and look.** Run `.\build_cpp.ps1 -Target hydra_uitest; .\build-cpp\Release\hydra_uitest.exe --test preview-overlay-fit --test preview-drain-box --keep-temp`. Expected: both `[PASS]`, with `screenshot:` lines for `overlay-fit-wide.png`, `overlay-fit-narrow.png` and `drain-box-active.png`. Open all three with the Read tool. Describe:
  - the drain box's position (top-right, left of the gauge's top)
  - whether any box covers the highway in the narrow frame, and by roughly how much
  - whether the time box shows the `Time signature:` line under BPM
  - whether the text in the narrow frame is readable

  If the narrow frame shows the text is unreadable, or a box sits on the lanes by more than a few pixels, stop and report with the paths. Don't tune the numbers yourself.

- [ ] **Step 11: Run everything.** Run `.\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test `[PASS]`. Then `.\build_cpp.ps1 -Target hydra_tests; .\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`.

- [ ] **Step 12: Commit** on branch `drain/T6`, with the message "Preview: text boxes scale to fit beside the highway" and a body line: "The time, score and drain boxes share one scale fitted to the room beside the highway (projected through the renderer's own camera), floored at 60%; the drain box moves top-right. track_height now has one home." Trailers: `Task: Task 6: overlays fit beside the highway`, `Agent: <your agent name>`, `Session: <your session id>`, `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

---

### Task 7: The user looks again

**Goal:** The user confirms the revision in the real app: "full meter" jumps with the section, the time signature line reads right, and the boxes sit beside the highway in a narrow window.

**Files:**
- none (a look at the running app; nothing is edited unless the user asks)

**Acceptance Criteria:**
- [ ] `build-cpp\Release\Hydra.exe` is rebuilt with `-Target Hydra` from the merged revision before the handoff, and its timestamp is checked.
- [ ] The user has played "One [Metallica]" or another chart with time signature changes, and has said whether the rate and "full meter" jump at the section changes.
- [ ] The user has said whether the boxes sit clear of the highway, and are readable, in a narrow window.

**Verify:** The user's reply in chat.

**Steps:**

- [ ] **Step 1: Rebuild the app itself.** Run `.\build_cpp.ps1 -Target Hydra`, then check that `build-cpp\Release\Hydra.exe`'s LastWriteTime is from after the merge. The test targets do not rebuild it.

- [ ] **Step 2: Hand it over.** Ask the user to play a chart with time signature changes, watch the drain box and the new time box line, and narrow the window. Ask: do the numbers jump at each change, and do the boxes stay clear of the highway and readable?
