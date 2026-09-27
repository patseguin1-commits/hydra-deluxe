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

- [ ] **Step 2: Act on the answer.** If the box covers the highway, change `d_max` in the drawing code from Task 2 to anchor top-right: `ImVec2 d_min(gauge_min.x - d_gap - d_w - pad * 2.0f, origin.y + v_margin)` and `ImVec2 d_max(gauge_min.x - d_gap, d_min.y + line_h * 3.0f + pad * 2.0f)`. Then run Task 2's Verify again and commit with the same trailers. If it doesn't cover the highway, nothing changes.
