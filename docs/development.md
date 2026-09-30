# Hydra Deluxe for developers and power users

This page holds everything the README leaves out: the command line tools in
full, how to build from source, and the developer tools. For using the app,
see the [User Guide](UserGuide.md).

## Command line tools

Three console tools ship next to the app. They share its settings, rules and
database: they read the same `hydra_settings.ini`, `hydra_rules.ini` and
`hydra.db` beside the executable.

```
hydra_batch                    Analyze every chart folder from the app's settings
hydra_batch <folder> [...]     ...or specific folders instead
hydra_batch --redo             Re-analyze charts already stored
hydra_batch --reindex          Only rebuild sort columns, no analysis
hydra_batch --db <path>        Target a specific database
hydra_batch --rules <path>     Take the rule choices from this file, not hydra_rules.ini
hydra_batch --legacy-fills     Score fills by Clone Hero 1.0's rule (needs its own --db)

hydra_report                   Sortable HTML report of stored paths (top 5 per chart)
hydra_report --paths 20        Top 20 per chart
hydra_report --all-paths       Everything stored
hydra_report --out report.html
hydra_report --db <path>       Report on a specific database
hydra_report --rules <path>    Judge records against the rules in this file
hydra_report --no-open         Write the file without opening the browser

hydra_fillcompare --old <ch10.db> --new <ch11.db>
                               Compare Clone Hero 1.0 and 1.1 fill results, chart by chart
hydra_fillcompare ... --out fill_compare.html
hydra_fillcompare ... --rules <path>
hydra_fillcompare ... --no-open
```

All three read the app's settings file, so they analyze, report and compare
at the same chart mode, SP cap, timing limit and score range the app is set to.
The fill rule is the exception, below. All three read the scoring rules from
`hydra_rules.ini` next to Hydra.exe, or from the file `--rules` names. If that
file has an error, they print it and stop with exit code 2.

Clone Hero 1.1 changed when a drum fill appears. The app's **1.0 fills**
setting scores by the older 1.0 rule instead. Each result remembers which rule
made it, so a song's 1.0 and 1.1 results sit side by side in `hydra.db`.

`hydra_batch --legacy-fills` does the same from the command line. It ignores
the app's 1.0 fills setting and goes by the flag alone. It still refuses to
write into the app's own `hydra.db`, so give it its own `--db`. Each database it
fills is stamped with the rule, and hydra_batch refuses (exit code 2) a run
whose rule disagrees with the stamp. `--reindex` never changes the stamp.
`hydra_report` on a database stamped 1.0 reports its 1.0 results.

To see what the rule change did, compare the two. `hydra_fillcompare` reads
the 1.0 results from `--old` and the 1.1 results from `--new`. They can be two
files, or the app's own database twice:

```
hydra_batch --legacy-fills --db ch10.db
hydra_batch --db ch11.db
hydra_fillcompare --old ch10.db --new ch11.db

hydra_fillcompare --old hydra.db --new hydra.db
```

## Building from source

Hydra Deluxe is a native Windows app: C++17, built with CMake and MSVC (Visual
Studio's "Desktop development with C++" workload is all it needs — the build
script finds the VS-bundled CMake itself). Third-party code (Dear ImGui,
SQLite, miniz, doctest, nlohmann/json, stb_image) is vendored under
`third_party/`.

```
.\build_cpp.ps1              # configure + build everything (Release), dev tools too
.\build_cpp.ps1 -Preset ship -Package   # zip a release without the GUI tests
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe    # run the test suite
```

To build the Windows installer (needs Inno Setup 6:
`winget install -e --id JRSoftware.InnoSetup`):

```
.\installer\build_installer.ps1        # -> build-cpp\installer\HydraDeluxe-<ver>-setup.exe
```

It builds Release with the `ship` preset (in `build-ship\`, without the
attached GUI tests), stages the ship list via `cmake --install` (so stray user
data in the build tree can never leak into a release), downloads and caches
the VC++ redistributable, and compiles `installer\hydra.iss`.

The tests run against the checked-in chart corpus under `testdata/input/`;
nothing else is needed. `hydra_tests` asserts structural invariants and
lossless round-trips over that corpus. GUI changes are checked headlessly with
`hydra_uitest`; see [agents/ui-testing.md](agents/ui-testing.md).

The program's name on screen is "Hydra Deluxe", but its files keep their older
names: `Hydra.exe`, `hydra.db`, `hydra_settings.ini`, `hydra_ui.ini`, the
`C:\Program Files\Hydra` install folder and the `Documents\Hydra` report
folder. That way an upgrade from an earlier Hydra finds the user's records
where they were.

## Developer tools

Two more console programs live in `tools/`. A plain `.\build_cpp.ps1` builds
them; they are not shipped. `hydra_bench` times the
analysis path — parse, search and database write, separately, per chart — so a
change to the engine can be measured instead of guessed at.

`hydra_replay` answers "what is *my* path worth?". Give it a chart and a list
of activation windows in ticks. It walks the chart chord by chord. For each
chord it prints the chord's own score, the running totals, and whether the
chord fell under Star Power — all as JSON, so it can be diffed or graphed.
`hydra_replay dump` reads the windows straight out of a stored record, so you
can start from a path the app already found and change one activation.
`hydra_replay score --path <file>` prices a path straight out of the JSON
`dump` or `target` wrote, so nothing has to be retyped and nothing is lost on
the way — in particular the squeeze-out offsets, which a hand-typed window
list drops and which are worth real points. When a window ends on the note
that closes a Star Power phrase and carries no squeeze-out offset, `score`
says so instead of guessing: that score is right if the player did not squeeze
that note out, and a little high if they did.
`hydra_replay target` prices a path the search never kept. Give it the
activation ticks and the engine is made to activate at exactly those fills and
nowhere else; back come that path's squeeze variants with their windows, meter
and skips stamped the engine's way. The search folds equal-scoring paths into
one another, so a real player's path is often not in any record no matter how
deep the search; this is how you get its number anyway.
`hydra_replay selfcheck` is what keeps the numbers honest: it re-analyzes
every corpus chart, replays every path the engine found, and fails if the
replay's six score categories disagree with the engine's own by a single
point.

## Other developer notes in this folder

`adr/` records design decisions. `agents/` holds instructions for coding
agents. `audit/`, `handoffs/` and `superpowers/` are working notes, plans and
specs from past changes. `srb-format.md` and
`cap-clamped-squeeze-frontend-anchor.md` are technical write-ups. None of these
are needed to use the app.
