# Layout fixes: nothing cut off, nothing drawn on top of anything

The user approved all ten fixes on 2026-09-27 ("fix everything"). They come from two searches of the redesigned interface. The first was a window sweep: the `layout-sweep` GUI test in `tests/ui/uitest_details.cpp` flags any window whose content is wider than its room. The second was a read-only review of the UI code; its screenshots are in the session scratchpad `ui` folder. Four tasks run in parallel, one worktree each, and the main session merges them in order L1, L2, L3, L4.

## Rules for every task

Change layout only. No wording, label, number or behaviour changes beyond what your task says. Every widget label and ID the GUI tests use stays the same. The only exception is a new ID your own new test needs.

A fix must hold at every window size and on long real-world data, not only on the test library. The user's library has 19,863 charts: long titles, 40+ activation paths, 7-digit scores, measures past 1,000. At a 1,280 px window the song panel always sits at its 820 px minimum, so check there. The GUI harness draws at 1,280 px, and `hydra::ui::remember_library_share(0.99f)` followed by `h.app->library_ui.panel_was_open = false` gives the narrowest panel. A share of 0.01 gives the widest panel.

Test geometry, not text. The text log (`visible_text`) records a clipped string in full, so it cannot see a cut-off. Use item or window rectangles instead: `ctx->ItemInfo(...).RectFull`, `ImGuiWindow::ContentSize` against `ContentRegionRect`, or a pure layout function with unit tests.

`layout-sweep` fails on the base commit, with `Hydra/##settingsbar` overflowing (content 1,293 px, room 1,248 px). That line goes away when L1 merges. Until then a reviewer accepts `layout-sweep` failing only with `##settingsbar` lines; any other `OVERFLOW` line is a real failure. Every other test must pass.

Build from the worktree root with `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 > build.log 2>&1`, then again with `-Target hydra_uitest`. Unit tests are `.\build-cpp\Release\hydra_tests.exe`. GUI tests are `.\build-cpp\Release\hydra_uitest.exe --all --jobs 4` (see `docs/agents/ui-testing.md`).

## Task L1: The settings bar, the filter chips, library titles and the Score range box

Files: `src/ui/settings_bar.cpp`, `src/ui/library_table.cpp`, `src/ui/app_state.h` (only the `LibraryViewState` struct, only if you need to remember widths between frames), `tests/ui/uitest_library.cpp`.

The Analysis settings bar is one row of fixed pieces, so at 1,280 px the "ms" after Path limit is cut off. Treat its four groups as blocks: Difficulty with Pro Drums and 2x Bass, then SP cap, then Score range, then Path limit. The lock message ("Stop the batch to change these.") is a fifth block. A block stays on the current line when it fits and starts a new line when it doesn't; a block that starts a line has no separator before it. A block's width is only known after it is drawn, so measure it on one frame and use it on the next (keep it in `LibraryViewState`, not in a static). On a wide window the bar stays one line, exactly as today.

The Score range number box shows only four digits. Make it wide enough for six, step buttons included.

The four filter chips (`render_chips`) are a plain `SameLine` chain. The last chip, "Analyzed (N)", is cut at the library's edge at its narrowest (320 px) or with big counts. A chip that doesn't fit starts a new line.

The Title column is the only library column that cuts text mid-letter; Artist, Charter, Folder and Best path end in "…". Make a long title end in "…" too, keeping its hover tooltip, the row's click, selection, keyboard and search-highlight behaviour, and whatever the tests use to find a row.

Tests in `uitest_library.cpp`. At 1,280 px with the library at its narrowest, no piece of the settings bar runs past its window and the chips stay inside the library. With the library widest, the bar is one line. Score range holds a six-digit value without cutting it. A long title in a narrow Title column ends inside its cell.

Verify: `layout-sweep` passes, along with every other GUI and unit test.

## Task L2: The Paths tab's backend table and activation rows

Files: `src/ui/paths_tab.cpp`, `tests/ui/uitest_paths.cpp`, and a new pure header plus unit test if you want one for the row layout (add the test to `CMakeLists.txt`'s `hydra_tests` list).

The backend table (`render_backend_table`) gives Timing, Chord and Points 80 px each, so the Rating column is left with about 230 px. The rating that matters most, "Insane SqOut <-- squeezed out (-260)", is cut to "Insane SqOut <-- sque" on Burnout's activation 1 at the narrowest panel. Size those three columns to their widest content, and let the Rating text wrap inside its cell so it is never cut off.

On an activation row (`render_activation_row`), the measure is drawn at +104 px and the bars at +200 px. A measure of 11 characters or more, like "m1024.1.120", runs into "3 bars". Place the bars after the widest measure in this path's list, with a gap, never earlier than today's +200 px. Check that the badge on the right still never overlaps the bars at the narrowest panel.

Tests in `uitest_paths.cpp`. On Burnout at the narrowest panel, the squeezed-out rating text sits inside its table cell. The rows' measure, bars and badge don't overlap; a pure function with a unit test fed an 11-character measure is fine for this.

## Task L3: The Preview's next-activation box, its text size, the path picker and the error line

Files: `src/ui/preview_tab.cpp`, `src/render/overlay_layout.h`, `src/render/overlay_layout.cpp`, their unit tests, `src/ui/preview_controller.h/.cpp` only if the per-path longest line needs to live there, and `tests/ui/uitest_preview.cpp`.

Two faults share one cause. First, the "Next: activation" box at the bottom left widens over the red lane on a chord like "[Kick - YellowCym - GreenCym]" (Burnout, activation 2). Second, the time, score and drain boxes all shrink by about 12% at the same moment and grow back later, because the shared text scale counts the next box's current width. `overlay_scale` checks room only at the height of the top boxes, where the highway is narrow, but the next box sits at the bottom, where the highway is widest. Make the fit check use the room where each box is actually drawn. Pick the scale once per path, from the widest line any of its activations' next boxes would show (plus the other boxes), so it never changes during playback. The scale must still reach 1.0 when there is room: `preview-overlay-fit` expects 1.0 at the library's narrowest.

The path picker (`render_path_picker`) now sizes its box to the longest path, capped at the line. A 40-activation path plus "  (optimal)" is about 100 characters, more than the line holds, so the end still vanishes under the arrow. When the shown label doesn't fit, draw it ending in "…" and put the full label in a hover tooltip. The open list keeps its full labels.

The "Preview failed: …" detail line doesn't wrap, so a long error with a file path is cut off. Wrap it.

Tests. A unit test on the overlay layout shows a long next line no longer covers the lane at the bottom, and the scale is the same whatever the current next line is. In `uitest_preview.cpp`, `overlay_scale()` on Burnout is identical at activations 1, 2 and 3 and after the last one, and the picker's label ellipsis works; a pure helper with a unit test for that is fine.

## Task L4: The Dynamics tab and long error messages

Files: `src/ui/dynamics_tab.cpp`, `src/ui/details_panel.cpp` (the analyze-error lines only), `src/ui/library_dialogs.cpp` (the batch failure detail lines only), `tests/ui/uitest_details.cpp`, and `tests/ui/uitest_batch_reports.cpp` if you test the batch lines.

The Dynamics tab's right box gets about 37% of the panel. The line "Dynamics enabled: no (markings ignored by Clone Hero)" is cut off in it on any MIDI chart without `[ENABLE_CHART_DYNAMICS]`, for example Karnivool - Themata. Make that line, and any other sentence in that box that can run long, wrap inside the box.

Error detail lines don't wrap, so a long message is cut off at the panel edge: "Dynamics failed: …" in the Dynamics tab, the analyze error in the song panel, and the batch failure details in the finished strip. Wrap each one.

Tests. Extend `layout-sweep` in `uitest_details.cpp` with Themata ("themata", title "Themata") so its Dynamics tab is checked at both panel widths. Add a check that a long error message wraps inside its window, for any one of the wrapped lines the harness can reach.
