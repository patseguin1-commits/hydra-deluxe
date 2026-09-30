# Hydra
Score optimizer / path viewer for Clone Hero drums!
 
Featuring a song browsing UI to make information convenient to access even for large song libraries.

<br><p align="center"><img src="/resource/icon_app.png" width="200"></p>

This is a separate version of [DragonDelgar's Hydra](https://github.com/DragonDelgar/hydra). It started from the public v1.3.1 in August 2026 and was rewritten in C++. It has grown a lot since. [How this version differs from the public Hydra](docs/differences-from-public-hydra.md) covers every change, and why scores can differ between the two.

## What Hydra does

Hydra reads your Clone Hero drum charts and finds the Star Power paths that give the best score. It keeps a few paths just below optimal too, in case the best one is awkward to play.

For each path it tells you how to play it. Each activation says where it is, which chord to activate on, and which squeezes it needs. Each squeeze is a sentence saying which note to hit early or late, by how much, and what it's worth. A score breakdown matches the categories on Clone Hero's results screen.

The **Preview** plays the chart as a 3D note highway with the song's audio. It draws the path on it, with a running score and a Star Power meter. The **Dynamics** tab counts ghost and accent notes. The **Stars** tab shows the score each star needs.

Hydra can analyze your whole library in the background and build a sortable HTML report of every song's paths. It can also compare a player's [dmleaderboards](https://dmleaderboards.com) scores with your optimals.

It reads `.mid`, `.chart`, `.sng` and `.srb` charts, on any difficulty, with or without Pro Drums and 2x Bass.

## Essential Info

* [Hydra User Guide](docs/UserGuide.md): every button and tab, in the order you meet them.
* [How this version differs from the public Hydra](docs/differences-from-public-hydra.md)

The public Hydra wiki explains the playing mechanics. They apply to this version too:

* [Path Notation Quick Reference](https://github.com/DragonDelgar/hydra/wiki/Path-Notation-Quick-Reference)
* [Optimal Checklist](https://github.com/DragonDelgar/hydra/wiki/Optimal-Checklist)
* [Squeezes and early fills](https://github.com/DragonDelgar/hydra/wiki): the wiki's "Detailed Mechanics" pages.

The wiki's own user guide describes the public app's older screen, not this one.

## Quick-start guide

1. Download the installer (`Hydra-<version>-setup.exe`) from the [latest release](https://github.com/patseguin1-commits/hydra-test/releases/latest) and run it. It installs to `C:\Program Files\Hydra`, adds Start Menu shortcuts, and installs the Microsoft VC++ runtime if your PC doesn't have it. The installer isn't code-signed, so Windows SmartScreen may warn — choose "More info" → "Run anyway".
2. Run Hydra from the Start Menu.
3. Click `Manage folders...`, then `Add folder...`, and pick your Clone Hero songs folder (or whichever folder contains the songs you want to add). Hydra reads `.mid`, `.chart`, `.sng`, and `.srb` charts.
4. Click `Scan library`.
5. Once it's done, songs should appear in a table. Type in the search box to find the song you want to get the path for, then click on the song. Its panel opens beside the library.
6. Click the `Analyze this song` button.
7. Once it's done, paths should appear. The first path is optimal. There may be other paths tied for optimal, listed under the same score. Below that are some of the next-highest scores and their paths, which could come in handy if the optimal path is too annoying or difficult.
8. Click a path on the left side of the panel to show its activations on the right side.
9. Use `<` and `>` to step to the previous or next song, or close the panel with its `X` (or `Escape`) to go back to browsing.

### Where your data lives

Hydra keeps its records database and settings next to Hydra.exe — for an
installed copy that's `C:\Program Files\Hydra` (the installer makes that
folder writable for regular users). Reports (the path report and the
leaderboard comparison) are saved in your `Documents\Hydra` folder instead.
Uninstalling keeps your `hydra*.db` / `hydra*_settings.ini` there; delete the
folder manually if you really want them gone. Moving from a zip install? Copy
your old `hydra*.db`, `hydra*_settings.ini`, and `hydra*_ui.ini` into
`C:\Program Files\Hydra` and your library and records come with you.

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

Clone Hero 1.1 changed when a drum fill appears. `--legacy-fills` scores by
the older 1.0 rule instead. The rule is not stored on each result, so a 1.0
run needs its own database, and hydra_batch refuses to write one into Hydra's
own `hydra.db`. Each database is stamped with the rule that filled it, and
hydra_batch refuses (exit code 2) a run whose rule disagrees with the stamp.
`--reindex` never changes the stamp. To see what the rule change did, fill two
databases and compare them:

```
hydra_batch --legacy-fills --db ch10.db
hydra_batch --db ch11.db
hydra_fillcompare --old ch10.db --new ch11.db
```

## Building from source

Hydra is a native Windows app: C++17, built with CMake and MSVC (Visual
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
.\installer\build_installer.ps1        # -> build-cpp\installer\Hydra-<ver>-setup.exe
```

It builds Release with the `ship` preset (in `build-ship\`, without the
attached GUI tests), stages the ship list via `cmake --install` (so stray user
data in the build tree can never leak into a release), downloads and caches
the VC++ redistributable, and compiles `installer\hydra.iss`.

The tests run against the checked-in chart corpus under `testdata/input/`;
nothing else is needed. `hydra_tests` asserts structural invariants and
lossless round-trips over that corpus.

### Developer tools

Two more console programs live in `tools/`. A plain `.\build_cpp.ps1` builds
them; they are not shipped. `hydra_bench` times the
analysis path — parse, search and database write, separately, per chart — so a
change to the engine can be measured instead of guessed at.

`hydra_replay` answers "what is *my* path worth?". Give it a chart and a list
of activation windows in ticks. It walks the chart chord by chord. For each
chord it prints the chord's own score, the running totals, and whether the
chord fell under Star Power — all as JSON, so it can be diffed or graphed.
`hydra_replay dump` reads the windows straight out of a stored record, so you
can start from a path Hydra already found and change one activation.
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

## The SP cap

Every path Hydra finds rests on one rule taken from Clone Hero: the Star Power
meter holds 4 bars and no more, so a phrase collected on a full meter is
thrown away. That rule is why banking SP has a ceiling, why the longest
activation is 8 measures, and why a phrase collected late in an activation can
be worth nothing at all.

The **SP cap** setting (in the Analysis settings bar on the main screen) lets
you change that number, to answer what the paths would be if the meter held more.
With a higher cap an activation runs 2 measures per bar spent up to that
ceiling, and a phrase collected during SP is worth its full 2 measures more
often. **Scores at any cap other than 4 are not achievable in Clone Hero.**
They are a what-if for seeing how much the cap costs and where, not paths to
play.

Results are kept per cap. A chart's 4-bar record and its 64-bar record sit
side by side in the same library; changing the cap just changes which one the
app shows, and a chart only gets analyzed again when it has no record at the
current cap. The path report and the leaderboard comparison follow the same
rule (the comparison only runs at 4 bars, because that is what the leaderboard
plays).

Before 1.6 this shipped as a second program, Hydra Uncapped, with its own
`hydra_uncapped.db`. Hydra does not read that file; analyze those charts again
at the cap you want.

Earlier versions also had an automatic cap setting. It kept raising the cap
until the score stopped changing. It has been removed: pick a number instead.
Results saved under the automatic setting are deleted the first time the new
version opens the database.

## Acknowledgements
- DragonDelgar, who wrote Hydra and whose public version this one grew from
- Onyx, whose drum previewer the Preview's highway is ported from
- Boddy, Beud, and Nick (BongOfDestiny) for active beta testing
- Boddy for reference footage / images shown in the Hydra Wiki
- Drummer's Monthly for being an awesome CH drums community, and for testing some of the first paths ever made by Hydra
- Smidge for all the love and support
