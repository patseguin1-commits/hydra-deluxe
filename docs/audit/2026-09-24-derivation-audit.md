# Derivation audit, 2026-09-24

This audit looked for places where Hydra works out the same fact twice, places where those copies disagree, and rules nobody wrote a reason for. It was read-only. No source, test or build file was changed. Nothing below has been fixed. Every fix waits for your yes.

## How it was done

Seven agents each read one group of modules and listed candidates. The groups were the named leads, `src/core`, `src/search`, `src/app` with `src/cli`, the UI with render and audio, parse with store, and tests with tools and docs. They produced 97 candidates. I merged the ones that described the same problem, which left 66. Four different agents then checked each of the 66 against the code. A candidate survived only if its verifier found every copy where it was claimed. For a drift claim, the verifier also had to show the input where the copies disagree. 65 survived in full or in part. One was refuted. One more problem turned up during verification. I checked that one myself against the code before including it.

Some claims were proven by running tools. Those runs used `build-cpp/Release/hydra_replay.exe`, built 2026-09-09. That binary is older than the latest source. The finder that made the runs checked that none of the later commits touch the lines involved. The Round and Round and Wake runs were done by the finder. The verifier could not find those two charts on disk, so it re-checked those two claims by reading the code instead. Everything else marked "from the code" was reasoned from the source, not run.

"Not found where we looked" means exactly that. A grep that finds nothing does not prove a thing is absent.

## The short version

The drift you reported is real and it is the most visible one. The details table prices a squeezed-out backend row by a rule the engine does not use. The Preview's SP gauge has the same kind of problem. It guesses how many phrases the activation collected instead of asking the engine, and it guesses wrong when the cap clamps. Two displays also show the same stored number in two different ways. They are the average multiplier and the calibration-fill offset.

Most duplicates agree today. The biggest group is "is this the squeezed-out note", which is answered in five places by two different methods. The fix pattern is the same one ADR 0011 used for the deact node: have the engine stamp the fact on the record, and let everyone else read it.

---

## Drift: copies that disagree today

These are ranked by how visible the disagreement is to you.

### 1. The squeezed-out backend row shows points the score never counted

The question is: how much SP score does a squeezed-out backend row add to the path? Three places answer it. The engine answers it in `create_deactivated_path` (`src/search/engine.cpp`, around line 573). If the row is at or before the SP end, it keeps the reduced value. If it is less than 3 ms after the end (`kBackendLeewayMs`), it adds the reduced value. Anything later adds nothing. The replay has a hand-written copy of the same rule in `replay_path` (`src/core/replay.cpp`, around line 101), and it agrees with the engine. The details table in `build_activations` (`src/app/path_view.cpp`, around line 230) never looks at the offset. It always prints the reduced value and "full minus reduced" as a loss.

They disagree for any squeezed-out row 3 ms or more past the SP end. The finder proved it on Ratt, Round and Round, at cap 4 and 10 ms. The second activation (69120 to 76800) squeezes out the chord at tick 77280, which sits at +479.999 ms. The engine and `hydra_replay score --path` both count that chord as 0, and the replay total (499615) matches the engine. The table prints 260 and "(-200)". The fourth activation has the same pattern at +483.868 ms.

What you see is the report that started this audit: a row that claims 260 banked and 200 lost, on a note worth nothing. (An earlier draft also listed the late-SqOut warning that `rate_activation` can raise for this row as a second symptom. That was wrong. The comment at `src/core/squeeze_rating.cpp` lines 90 to 95 explains that warning on purpose: a late frontend hit can drag the SP end back over the free row and undo the squeeze-out. That risk is real whatever the row is worth.)

The owner should be one core function: "what is this backend row worth on this path". It would return 0, the reduced value or the full value, using the engine's three cases. The engine, the replay and the table would all call it. Today the engine owns the rule only as inline branches, so nothing else can call it.

### 2. The Preview SP gauge shows a phantom banked bar after a cap-clamped activation

The question is: how many SP phrases did this activation collect while it was running? The engine knows the answer. `extend_deacts` (`src/search/graph.cpp`, line 262) extends the SP end by 2 measures per phrase and clamps it at the cap. The Preview does not ask. `build_sp_meter_curve` (`src/app/preview_view.cpp`, line 134) guesses the count from how far the deact node sits past the activation, as (measures past activation ÷ 2) minus the starting bars, rounded.

That guess is right when no clamp happens. It is wrong when the clamps together cut more than one measure off the plain extensions. The verifier used the engine's own test fixture, "SP cap overfill: a second clamp in the same window replaces clamp_tick" (`tests/test_search.cpp`, line 632). It has cap 2, an activation at tick 2304 with 2 bars, and phrases at 3072 and 3840. The engine collects both phrases and ends at 6912. The Preview's formula gives (9 − 3) ÷ 2 − 2 = 1 phrase. This part was worked by hand from the code, not run.

What you see in the Preview: the gauge misses the refill at 3840, drains too early, and then shows one bar still banked after SP ends. The path really has an empty meter there. The wrong bar stays until the next activation, where the curve snaps back to the engine's value. CONTEXT.md ("SP meter gauge") describes the guess but not the clamp case.

The owner should be the search. At copy-out, next to `deact_tick` and `clamp_tick`, it should stamp how many phrases (or which ticks) it collected. The gauge should read that. `clamp_tick` alone is not enough, because it only names the last clamping note.

### 3. The calibration fill shows opposite signs in the same panel

The question is: how early or late do you hit the calibration fill? The value is stored once, as `e_offset` on the activation. Two displays print it with opposite signs. The details line in `build_activations` (`src/app/path_view.cpp`, line 104) prints the raw field. The activation header just above it (line 95) prints `Activation::difficulty()`, which flips the sign. So does the report's efill column (`src/app/report.cpp`, line 383) and the verbose notation (`src/core/model.cpp`, line 432).

For an activation with `e_offset` of −12.3 ms, the details panel reads "Calibration fill: -12.3ms (required)" on one line. The header reads 12.3ms and the report reads 12.3. You cannot tell from the panel which way to hit it.

`report.h` (lines 34 to 38) documents the report's convention: positive means hit early. Nothing explains why the details line prints the raw value. The owner should be `Activation::e_difficulty` or one formatter that both views call. Which sign convention to show is your call.

### 4. The average multiplier differs by 0.001 between Song Details and the report

The question is: what is this path's average multiplier, as displayed? The value itself has one owner, `Path::avg_mult` (`src/core/model.cpp`, line 594). The two displays format it differently. Song Details (`build_score_breakdown`, `src/app/path_view.cpp`, line 294) cuts it off after three decimals. The report (`collect_rows`, `src/app/report.cpp`, line 387) rounds it with `py_round3`.

For 2.3456, Song Details shows 2.345x and the report shows 2.346. For 2.0005, they show 2.000 and 2.001. Both conventions are justified in comments as "Python parity", and the Python is gone. The owner should be one formatter in `src/app` that both call. Round or cut off is your choice.

### 5. An "SP overfilled" warning can appear on a row the table calls Standard

The question is: does this plain backend row need a deliberate squeeze, or does it count on its own? The label and the engine use the 3 ms leeway. `BackendSqueeze::summarystr` (`src/core/model.cpp`, line 326) says "Standard" under 3 ms, and the engine counts the row for free. The rating uses a different edge. `rate_activation` (`src/core/squeeze_rating.cpp`, lines 119 and 166) treats any plain row past `kDifficultMs` (2 ms) as one a late hit decides.

Take a cap-clamped activation with one plain backend row at +2.5 ms. The table labels it Standard and the engine counts it. The rating sets `cap_clamped`, so the table shows the "SP overfilled" warning because of a row it just called automatic. This was worked from the code, not run. Only rows between 2 and 3 ms are affected.

The owner should be the same core function as finding 1, or a single "counted without a squeeze" test that the label, the rating and the engine all call.

### 6. A loose chart whose notes file is capitalized would not appear in the library

The question is: which file in this folder is the chart? Three places answer it. The scan in `discover_charts` (`src/app/analysis.cpp`, line 397) compares names exactly, so it wants "notes.mid" and "song.ini" in lower case. The `.sng` loader (`load_songpath_sng`, `src/parse/song.cpp`, line 1077) ignores case. So does the extension check in `load_songpath` (line 1140). All three prefer notes.mid over notes.chart.

A loose folder with "Notes.mid" or "Song.ini" would be skipped by the scan, even though the parser would read it fine. The verifier did not check whether any real library folder has a capitalized name, or what Clone Hero does with one. The owner should be one helper in `src/parse` that owns both the name match and the mid-over-chart preference.

### 7. The Preview highway can draw one note after an SP phrase as an SP note

The question is: is this note inside an SP phrase, fill or solo? The parser answers it by tick. The renderer (`src/render/track_state.cpp`, line 20, used at `src/render/highway_draw.cpp`, line 320) answers it by time. It extends each span's end by 0.5 ms. The comment says that margin never reaches the next note.

That is not true for every chart. At 480 ticks per beat and 300 BPM, one tick is 0.417 ms. A chord one tick after the phrase's last chord would draw with the SP look, though the parser says it is outside. At 192 ticks per beat this needs a tempo above 625 BPM. No real chart was checked. The owner should be the tick span the scene already carries.

### 8. The User Guide says 2 ms where the engine uses 3 ms

`docs/UserGuide.md` line 164 says a double squeeze counts only at 2 ms or less. The engine counts anything under 3 ms (`kBackendLeewayMs`, `src/core/model.h`, line 66). The guide's own line 144 agrees with the engine. A reader checking a score against the guide will find 2 to 3 ms rows counted that the guide says are excluded. The code is the owner. The guide should say "under 3 ms".

### 9. The User Guide says only Expert is supported

`docs/UserGuide.md` line 42 says only Expert is supported and shows as a fixed label. The library has a four-entry difficulty dropdown (`src/ui/library_view.cpp`, line 222). The guide hides a feature you have. The code comment at `library_view.cpp` line 197 has the accurate caveat: only the Auto ladder is Expert-only.

### 10. The User Guide still mentions "the other edition" and omits `--legacy-fills`

`docs/UserGuide.md` line 61 still says "(or the other edition)", though the editions were merged. The flag list at line 180 leaves out `--legacy-fills`, which `hydra_batch` accepts (`src/cli/batch.cpp`, line 92). ADR 0010 explains why that flag is CLI-only. A reader looks for an edition that doesn't exist and cannot discover the CH 1.0 fill mode.

### 11. `hydra_replay` JSON reports the multiplier one chord behind

The question is: what multiplier did this chord score at? `category_scores` (`src/core/scoring.cpp`, line 38) adds one to the combo before it looks up the multiplier. The replay (`src/core/replay.cpp`, line 83) looks it up again without adding one. It writes that value into the JSON (`tools/replay.cpp`, line 414).

This was proven with a run on Evans Blue, Beg (`testdata/input/common/IB24/T1`), with no SP. Chord 8 has 9 notes of combo before it. The JSON says multiplier 1, but its combo points are 50, which is the 2x rate. Every chord that crosses 10, 20 or 30 shows the old multiplier while being paid at the new one. The owner should be `category_scores`: it should report the multiplier it applied, and the replay should copy it.

### 12. `hydra_replay --acts` misprices a squeeze-out when the offset is typed by hand

The question is: which chord is the squeezed-out note? The engine answers by exact tick (`be_tick == e.sqinout_time`, `src/search/engine.cpp`, line 578). The replay only has the SqOut offset in ms. It finds the note again with its own 0.01 ms tolerance, `kSameNoteMs` (`src/core/replay.cpp`, line 23).

The two agree when the offset comes straight from a record, because it is the same stored number. They disagree when you type it. The finder proved this on Hail The Sun, Wake, at cap 4 and 10 ms. The activation 2651040 to 2659680 has SqOut −93.75. Typing −93.73 instead adds 200 to the total, because the phrase note is no longer recognized. The GUI shows SqOut timings to one decimal, so a copied figure can be 0.05 ms off. No warning is printed. The owner should be a stored squeezed-out-note tick on the activation (see finding 16), so the replay compares ticks.

### 13. `hydra_replay`'s squeeze-out warning can name the wrong note

The replay's warning in `ambiguous_window_warnings` (`src/core/replay.cpp`, line 242) picks the last phrase note in the window as the one squeezed out. The graph (`add_deact_edge`, `src/search/graph.cpp`, line 371) picks the first one. They agree on the paths the code comment cites. They split in two edge cases, worked from the code and not run. With phrase notes at 400 ms and 100 ms before the SP end, the warning names the wrong note and understates the cost. With a phrase note exactly 500 ms before the end, the replay warns about a squeeze-out the graph could never build. This is a CLI hint, not a score. The owner should be the graph's rule or the stored tick.

### 14. `hydra_bench` claims to run the GUI default, but runs something else

`tools/bench.cpp` (line 52) labels its runs as the GUI default. It actually runs the Auto ladder with no time budget. A fresh GUI uses cap 4 (`src/app/config.h`, line 89), and when it does run Auto it applies a 120 s budget. Benchmark timings therefore measure a different, often slower, setup. The owner is `app::Settings`: bench should build its settings from `Settings().to_analysis_settings()` instead of copying fields. This was read from the code; bench was not run.

### 15. The ch_probe predicted window can never appear

This one turned up during verification, and I checked it myself. `EngineModel.constants()` (`tools/ch_probe/engine.py`, line 147) returns the formula constants under names like `normal_c1` and `precision_c1`. `_decode_formula_constants` (`tools/ch_probe/experiments/active_probe.py`, line 229) looks for plain `c1`, `c2`, `c3`, `c4` and `divisor`. The `c` keys never match, so it returns nothing. The active probe's summary therefore never shows the predicted window, and it cannot confirm or refute the formula. The unit test (`tools/ch_probe/tests/test_analysis.py`, line 173) feeds plain `c1` keys, which is why it passes. The owner should be one key list shared by `engine.py` and `active_probe.py`.

---

## Duplicates that agree today

None of these disagree now. Each is a second copy that could drift. They are ranked by what you would see if one did.

### 16. "Is this the squeezed-out note?" is answered in five places

The engine asks it twice by tick. Once when pricing (`create_deactivated_path`, line 577) and once when copying rows into the record (`src/search/engine.cpp`, line 1199). `Activation::is_sqout_backend` (`src/core/model.cpp`, line 549) asks it with a bare 0.01 ms literal. `Activation::display_backends` (line 558) asks "past the note?" with a second bare 0.01. The replay uses its own named `kSameNoteMs`. A test in `tests/test_search.cpp` (line 258) writes the rule out again instead of calling `display_backends`.

They agree because every offset is measured from the same stored node, and ms rises with ticks. If one copy changed, the details table could tag the wrong row. The store would also save the wrong trim, because `write_activation` (`src/store/path_binary.cpp`, line 17) stores whatever `display_backends` returns. The owner should be the engine: stamp the squeezed-out note's tick on the activation, the way ADR 0011 stamps `deact_tick`. Everything else compares ticks.

### 17. The E0 and squeeze difficulty rules are written in the engine and in the model

The engine's `act_difficulty` and `search_difficulty` (`src/search/engine.cpp`, lines 641 and 666) decide how hard an activation is, for the ms filter. `Activation::is_E0`, `e_difficulty`, `difficulty` and `is_difficult` (`src/core/model.cpp`, around lines 395 to 414) decide the same thing for the display. `build_path_row` (`src/app/path_view.cpp`, line 320) decides "is this path difficult" a third way. All three agree today. If they split, the ms limit would keep or drop paths by a different hardest-ms than Song Details and the report show. The owner should be one free function in `src/core/model` for the E0 test and one for squeeze difficulty. The engine can call a free function on its own arrays. A `Path::is_difficult` would serve the path list.

### 18. The SP length rule is restated in about six places

"2 measures per bar, +2 per collected phrase, capped at 2 × cap" lives in the graph (`extend_deacts`, `add_act_edge`, `add_deact_edge`) and the engine. It is also restated in the Preview curve (`src/app/preview_view.cpp`, lines 90, 155, 165), the transfer scale (`src/core/squeeze_rating.cpp`, lines 46 and 221), `tools/replay.cpp` (lines 186 and 787) and a test fixture. None has drifted. Several are labelled as nominal approximations. If the rule changed in the engine, the Preview gauge and the transfer scale would keep the old one. The owner should be a small core helper next to `plusmeasure`. The Preview should read engine-stamped collection points instead (see finding 2).

### 19. The squeezed-out chord's reduced value is computed twice

`ScoreGraph::build` (`src/search/graph.cpp`, line 129) and `replay_path` (`src/core/replay.cpp`, line 114) each subtract the reduction from the full value. Inside `category_scores` the reduction itself is also written twice (`src/core/scoring.cpp`, lines 68 and 84). They agree. The owner should be `category_scores`, exposing the reduced value once.

### 20. A note's point value is defined twice

`ChordNote::basescore` (`src/core/model.cpp`, line 88) gives 50 or 65, doubled for a dynamic note. `category_scores` (`src/core/scoring.cpp`, line 42) builds the same total from its own pieces. `basescore` drives the note sort order and multiplier-squeeze points. `category_scores` drives the score. They agree but share no code. The owner should be `basescore`.

### 21. The 500 ms squeeze window is two constants

`kSqueezeWindowMs` (`src/search/graph.h`, line 25) sets how far the graph looks for backends. `kBackendDisplayWindowMs` (`src/core/model.h`, line 76) sets what `display_backends` keeps. Both are 500.0 with the same strict edge. If the graph window grew alone, the store would silently drop the extra rows. The test at `tests/test_squeeze_rating.cpp` line 696 uses literal offsets, so it would not catch that. The owner should be one constant in `src/core/model.h`, since core sits below search.

### 22. The solo bonus is written twice

The graph (`src/search/graph.cpp`, line 111) and the replay (`src/core/replay.cpp`, line 128) each pay a literal 100 per solo note. If one changed, `hydra_replay` would disagree with the analyzed score. The owner should be one named constant in core.

### 23. The "Beyond 2× hit window" edge is written in four places

`timing_tiers` (`src/core/squeeze_rating.cpp`, line 183) owns the tier table, and the report page reads it. The Beyond edge is still written again in the page's tier dropdown, in its summary count and in the footer (`src/app/report.cpp`, lines 97, 221 and 489). If the tier table changed, the "Past N ms" tile and the footer would stay on the old edge. The page should count rows by their stored tier.

### 24. The nominal squeeze budget is recomputed in a tooltip

The backend-row tooltip (`src/app/path_view.cpp`, line 225) prints 2 × W. `squeeze_budget_ms(1.0, W)` (`src/core/squeeze_rating.cpp`, line 66) gives the same number. The tooltip should call it.

### 25. The default SP cap of 4 is a bare literal in four places

`kCloneHeroSpCap` exists (`src/core/model.h`, line 30), and nearby code uses it. The stragglers are `Settings::sp_cap` (`src/app/config.h`, line 89), two Preview defaults (`src/app/preview_view.h`, lines 147 and 219) and the "4 bars" hint on the Dynamics tab (`src/ui/details_view.cpp`, line 197). If the default changed, these would stay at 4.

### 26. The Dynamics cache key and its "only with 2x bass" rule are spelled in several places

The cache-key string is built twice (`src/ui/app_state.cpp`, line 103, and `src/ui/dynamics_load_job.cpp`, line 13). The two must match exactly, or the job restarts every frame. The `DynamicsKey` struct is built in four places. The rule that dynamics counts are only stored with 2x bass on appears three times (`app_state.cpp` 208, `analysis.cpp` 700, `dynamics_load_job.cpp` 11), with its reason written at two of them. The owner should be one helper in `src/app/dynamics_breakdown`.

### 27. The `.sng` layout is decoded in three places, and `.srb` shares an unnamed size cap

The `.sng` mask offset, mask length, metadata length and XOR key are written out in the parser (`src/parse/song.cpp`, line 1038), the Preview audio extractor (`src/app/preview_source.cpp`, line 172) and the metadata reader (`src/app/analysis.cpp`, line 264). Only the parser lacks bounds checks. The `.srb` notes-stream cap (`size_t{1} << 30`) is a bare literal three times (`song.cpp` 1121, `preview_source.cpp` 233 and 239). The owner should be a `parse/sng.h` module like the existing `parse/srb.h`, plus one named constant there.

### 28. The MIDI and .chart parsers each write the same three opcode bodies

Time signature, fill end and SP end are written twice in `src/parse/song.cpp` (MIDI at lines 297, 307, 314; .chart at 731, 740, 750). The bodies match. One small difference: the MIDI SP-end copy reads `sp_start_tick_` without checking it has a value. The file already keeps shared rules as free functions (`fill_lands_on_chord`), so those are the natural home.

### 29. MIDI variable-length numbers are parsed twice in one file

`read_varlen` (`src/parse/midi.cpp`, line 74) exists, and `parse_track` (line 198) has its own copy. They agree for every valid MIDI number and differ only on malformed files.

### 30. Two builders turn settings into a Lens

`Settings::lens` (`src/app/config.cpp`, line 191) and `lens_from` (`src/app/analysis.cpp`, line 589) both build the lens. A test pins them equal (`tests/test_config.cpp`, line 211). They could only differ with a hand-edited INI whose depth mode isn't 0 or 1. The batch should take the Lens from `Settings::lens`.

### 31. Smaller duplicates, grouped

These agree, and a split would be hard to notice or harmless.

The "every path" sentinel and the default path count are separate literals on the CLI and report sides (`src/cli/report.cpp` lines 34 and 41, `src/app/report.cpp` line 475, `src/app/report.h` line 82, `src/ui/library_jobs.cpp` line 222). They belong as named constants in `report.h`.

The build cap clamp ("no higher than the phrase count") is written twice in `src/search/pather.cpp` (lines 224 and 298). A fixed-cap run and an Auto rung at the same cap would build different graphs if one changed. One helper in `pather.cpp` fixes it.

The library dropdown lists the difficulty names itself (`src/ui/library_view.cpp`, line 222) instead of using `difficulty_name`. `tools/replay.cpp` also maps names and lists its six score fields three times.

The "No <diff> notes in this chart" message is copied from `analyze_chart_file` into `PreviewLoadJob::run` (`src/ui/preview_load_job.cpp`, line 35).

The Preview time box size (15 and 10) is written in `details_view.cpp` (line 678) and in `assets/preview/3d-config.json`, but the config value is never read. Editing the config does nothing.

The ".uncapped" suffix appears twice in `src/store/record_store.cpp` (lines 626 and 778).

The record-blob header is written by two codecs (`serialize.cpp` and `path_codec.cpp`). These are separate formats with their own versions, and a test checks that both round-trip identically. Low risk.

`tests/test_replay.cpp` (line 247) builds its own copy of the `paths_json` dump. The shared reader is tested, but the producer in `tools/replay.cpp` is not, so a dump-format change could break fcvideo with no failing test.

`tools/bench.cpp` types the chart-mode string by hand (line 89) and counts paths with its own walk (line 39). Both match today.

In ch_probe, the 85 ms back window is written in three files, and the probe spacings and kick lane in two. They match. The real risk is that a precision-mode probe would be judged against the normal 85 ms edge.

---

## Undocumented or unjustified assumptions

These are rules with no written reason, or a reason that points at deleted code. Ranked by effect on what you see.

### 32. The Preview assumes chart time 0 is audio time 0

The Preview seeks the audio to exactly the chart clock (`src/ui/preview_transport.cpp`, line 38). Nothing in `src/` reads the song.ini `delay` or the .chart `Offset`, not found where we looked. Real test charts set them. TEXTURES, Laments Of An Icarus has delay 689. Thornhill, Limbo has delay 1016. Moonlight Haze, Lunaris has Offset 0.25. On those charts the highway would run early or late against the music. Whether Hydra should honor these the way Clone Hero does is your call. Nobody has checked it against the game.

### 33. A squeeze-out removes only the first note's share

`category_scores` (`src/core/scoring.cpp`, line 66) says a squeezed-out chord loses only its lowest-value note's doubling. Every squeezed-out multi-note chord in every optimal path depends on this. No doc, ADR or comment gives a source. The comment itself calls it "quick and dirty". Whether it matches Clone Hero is unverified.

### 34. The 3 ms backend leeway has no stated source

`kBackendLeewayMs` (`src/core/model.h`, line 66) decides which rows score. Its comment explains why it is shared, but not where 3 ms comes from. The ±10 ms edges in `summarystr` are bare literals too. They only change label wording, and the User Guide says the rating is Hydra's own invention, so they matter less.

### 35. Auto-generated fill constants have no source

`Song::check_activations` (`src/parse/song.cpp`, lines 225, 226 and 235) uses fixed numbers to place fills in songs that have none authored. None of them has a stated source. If one differs from the game, those songs get wrong activation points.

### 36. At most 4 tied paths are kept per score

`MAX_TIED_PATHS` (`src/search/engine.cpp`, line 35) keeps 4 paths per score tie: the leader plus 3 variants. No reason is given for 4. A player's real path can be missing from every list because of it.

### 37. Tie grouping leaves out SP-ready time, with no written rule

`reduce_iteration_paths` (`src/search/engine.cpp`, line 854) groups paths by a key that does not include `sp_ready_ms`. The leader is whichever came first in the search. No comment says why. It is plausible, but not shown, that this merges away an easier calibration-fill variant.

### 38. Dynamics counts carry no parser version

The dynamics table (`src/store/record_store.cpp`, line 496) stores no Hydra or parser version. After an upgrade that changes parsing, results rows go stale, but the Dynamics tab would keep showing old counts.

### 39. Many rules are justified only by pointers to deleted Python

About 100 comments across about 30 files justify code with "Mirrors hydata…", "hydra_app.py…", "hymisc…", "hystore…" or "native/hydra_search.cpp". None of those files exist in the repo. Most are harmless notes on UI layout, like the font bindings in `details_view.cpp` and `library_view.cpp`. The ones that matter for scoring have no other reason written anywhere:

- the Auto cap ladder (`kSpCapLadder`, `src/search/pather.cpp`, line 16)
- the 120 s Auto budget (`src/app/config.cpp`, line 175; the User Guide mentions it but gives no reason)
- the backend trim written to the blob (`src/store/path_binary.cpp`, line 15)
- `SPSqueeze` equality comparing offset only, ignoring kind (`src/core/model.h`, line 163)
- `is_sqout_backend` and `display_backends` (`src/core/model.h`, lines 296 and 301)

The report page's byte parity with `hydra_report.py` is the exception. ADR 0002 covers it. ADR 0010 and CONTEXT.md cover the Ch10 deadline clamp, but not its res/16 pad (`src/search/graph.cpp`, line 58).

### 40. Smaller assumptions, grouped

`.chart` songs always have dynamics enabled (`src/parse/song.cpp`, line 1006), with no comment. The likely reason is that .chart ghosts and accents come from explicit flag notes, but it isn't written down. ADR 0012 covers only the MIDI side.

Solo end is handled in a different parser phase for .mid and .chart (`src/parse/song.cpp`, lines 449 and 838). It matches each format's usual convention, but a comment would stop someone "fixing" one to match the other.

The multiplier-squeeze combo set and its mod-10 rule (`src/core/model.cpp`, lines 338 and 359) have no stated reason.

`build_activations` uses a 0.005 tolerance four times to mean "looks the same at two decimals" (`src/app/path_view.cpp`, lines 123, 125, 136, 163). Only one use says why. The "10 ms" lever in the transfer-scale prose (lines 146, 154, 171) is an example amount with no reason.

Highway gem sizes (`src/render/highway_draw.cpp`, lines 338 and 341) are literals, despite the header's no-literals rule, and cite no Onyx source.

A song.ini with an empty `name =` shows blank in the library and "(unknown)" in the report (`src/app/analysis.cpp`, line 189, and `src/app/report.cpp`, line 368). The batch worker cap (`kBatchMaxWorkers`, `analysis.cpp` line 600) has no reason.

The engine reports progress every 0.5 percent (`src/search/engine.cpp`, line 1032), with no reason. This is cosmetic.

ch_probe's window functions default to exponent 2.0 (`tools/ch_probe/experiments/analysis.py`, lines 217 and 244). Its own constants file (line 118) warns not to assume that. The live path never reaches the default today, because of finding 15.

`sp_end_shift_ms` and its two siblings (`src/core/squeeze_rating.cpp`, lines 217 to 235) have no production caller, not found where we looked. Only tests use them. Their comment says so.

---

## Checked and dropped

The three blob format versions (`kBlobFormatVersion`, `kPathNodeFormatVersion`, `kPathStructureFormatVersion`) are consistent. Their coupling is documented in `path_codec.h` and `serialize.h`. It is enforced by comment only, not by a compile-time check. The CapQuery half of the Lens finding was a wrapper, since both sites call `CapQuery::from_setting`. The Preview gauge's extra `max(1, …)` floor reads the curve's own cap, so it cannot disagree. The chord-hash tables in `tools/gen_chord_tables.py` are a documented generator with a round-trip test. The Dynamics tab percentages have only one copy. The two path-identity keys (all-0 check versus Preview) answer different questions on purpose. The test that rebuilds the deact tick from backend offsets (`tests/test_search.cpp`, line 196) is an intentional cross-check. It feeds no display, so it fits ADR 0011. The Preview lighting literals are cited as Onyx values. One finder reported a test comment split in two by an inserted test (`tests/test_search.cpp`, around line 96). It is real, but it isn't a derivation problem.

---

## Proposed order of fixes

This is a proposal, not a plan in motion. Nothing starts without your yes. Several of these change what you see on screen, and those are marked. Each one needs its own yes on the exact new behavior.

**First, the backend-row value function (findings 1 and 5).** One core function prices a backend row, and the engine, replay, table and rating call it. This fixes your original report. It changes the display. Your two open questions from the handoff still stand. Should the engine's hot loop move onto the function (proven by a `hydra_batch` before/after diff), or only the replay and display? And what should the 0-point row say: "(uncounted)" or "(0 pts)"?

**Second, stamp the squeezed-out note's tick on the activation (findings 12, 13, 16).** This follows the ADR 0011 pattern and needs a blob version bump. The five "is this the note" tests then compare ticks, and the replay stops guessing from ms.

**Third, stamp the collected phrases per activation (findings 2 and 18).** The Preview gauge reads them instead of guessing. This changes the Preview display on cap-clamped charts. It can share the version bump with the second step.

**Fourth, two display questions for you (findings 3 and 4).** Which sign should the calibration fill show? Should the average multiplier be rounded or cut off? Once you pick, one formatter each.

**Fifth, the documentation and tool drifts (findings 8, 9, 10, 11, 14, 15).** Fix the User Guide's 2 ms, the difficulty dropdown, the edition and `--legacy-fills`. Make the replay JSON report the applied multiplier. Build bench's settings from `Settings`. Share ch_probe's key list. None of these changes Hydra's scores.

**Sixth, the mechanical single-owner moves (findings 17 and 19 to 31).** The E0 free function, one 500 ms constant, the solo bonus, the reduced value, `kCloneHeroSpCap`, the Beyond edge, the budget tooltip, the dynamics key helper, `parse/sng.h`, the shared opcodes, `read_varlen`, and the notes-file helper (finding 6). They are behavior-preserving, except the notes-file helper would start accepting capitalized names. That one needs your yes. The safety net for the rest is the test suite plus a `hydra_batch` diff.

**Last, write down the reasons (findings 32 to 40).** Several need facts only you or the game can supply. Examples: whether the Preview should honor song delay, where 3 ms and the first-note share come from, and why the ladder, the 120 s budget, the fill constants and the 4-path tie limit have their values. I'd ask you for each one rather than guess. Then each deleted-Python pointer gets replaced with the real reason, or with an ADR.
