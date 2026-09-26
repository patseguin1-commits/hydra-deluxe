# Hydra
Score optimizer / path viewer for Clone Hero drums!
 
Featuring a song browsing UI to make information convenient to access even for large song libraries.

<br><p align="center"><img src="/resource/icon_app.png" width="200"></p>

## Essential Info

* [Path Notation Quick Reference](https://github.com/DragonDelgar/hydra/wiki/Path-Notation-Quick-Reference)
* [Optimal Checklist](https://github.com/DragonDelgar/hydra/wiki/Optimal-Checklist)
* [Hydra User Guide](https://github.com/DragonDelgar/hydra/wiki/Hydra-User-Guide)

Hungry for more info? Check out [the wiki](https://github.com/DragonDelgar/hydra/wiki).

## Quick-start guide

1. Download the installer (`Hydra-<version>-setup.exe`) from the [latest release](https://github.com/DragonDelgar/hydra/releases) and run it. It installs to `C:\Program Files\Hydra`, adds Start Menu shortcuts, and installs the Microsoft VC++ runtime if your PC doesn't have it. The installer isn't code-signed, so Windows SmartScreen may warn — choose "More info" → "Run anyway".
2. Run Hydra from the Start Menu.
3. Click `Add folder...` and then pick your Clone Hero songs folder (or whichever folder contains the songs you want to add). Hydra reads `.mid`, `.chart`, `.sng`, and `.srb` charts.
4. Click `Scan charts`.
5. Once it's done, songs should appear in a table. Search for or find the page of the song you want to get the path for, then click on the song.
6. Click the `Analyze paths!` button.
7. Once it's done, paths should appear. The first path is optimal. There may be other paths tied for optimal, listed under the same score. Below that are some of the next-highest scores and their paths, which could come in handy if the optimal path is too annoying or difficult.
8. Click a path on the left side to show its details on the right side.
9. You can return to browsing songs by X-ing out of the Song Details window.

### Where your data lives

Hydra keeps its records database, settings, and generated reports next to
Hydra.exe — for an installed copy that's `C:\Program Files\Hydra` (the
installer makes that folder writable for regular users). Uninstalling keeps
your `hydra*.db` / `hydra*_settings.ini` there; delete the folder manually if
you really want them gone. Moving from a zip install? Copy your old
`hydra*.db`, `hydra*_settings.ini`, and `hydra*_ui.ini` into
`C:\Program Files\Hydra` and your library and records come with you.

## Command line tools

Two console tools ship next to the app and share its settings and database
(they read the same `hydra_settings.ini` / `hydra.db` beside the executable):

```
hydra_batch                    Analyze every chart folder from the app's settings
hydra_batch <folder> [...]     ...or specific folders instead
hydra_batch --redo             Re-analyze charts already stored
hydra_batch --reindex          Only rebuild sort columns, no analysis
hydra_batch --db <path>        Target a specific database

hydra_report                   Sortable HTML report of stored paths (top 5 per chart)
hydra_report --paths 20        Top 20 per chart
hydra_report --all-paths       Everything stored
hydra_report --out report.html
hydra_report --no-open         Write the file without opening the browser
```

Both read the app's settings file, so they analyze and report at the same
chart mode and SP cap the app is set to.

## Building from source

Hydra is a native Windows app: C++17, built with CMake and MSVC (Visual
Studio's "Desktop development with C++" workload is all it needs — the build
script finds the VS-bundled CMake itself). Third-party code (Dear ImGui,
SQLite, miniz, doctest, nlohmann/json, stb_image) is vendored under
`third_party/`.

```
.\build_cpp.ps1              # configure + build everything (Release)
.\build_cpp.ps1 -Package     # ...then zip a release (build-cpp\package\)
.\build_cpp.ps1 -Target hydra_tests
.\build-cpp\Release\hydra_tests.exe    # run the test suite
```

To build the Windows installer (needs Inno Setup 6:
`winget install -e --id JRSoftware.InnoSetup`):

```
.\installer\build_installer.ps1        # -> build-cpp\installer\Hydra-<ver>-setup.exe
```

It builds Release, stages the ship list via `cmake --install` (so stray user
data in the build tree can never leak into a release), downloads and caches
the VC++ redistributable, and compiles `installer\hydra.iss`.

The tests run against the checked-in chart corpus under `testdata/input/`;
nothing else is needed. `hydra_tests` asserts structural invariants and
lossless round-trips over that corpus.

### Developer tools

Two more console programs live in `tools/` and are built on demand
(`.\build_cpp.ps1 -Target hydra_bench`), not shipped. `hydra_bench` times the
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

The **SP cap** setting (in a song's details, next to the timing limit) lets you
change that number, to answer what the paths would be if the meter held more.
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

### "Auto": how "no ceiling" is actually reached

Searching with no ceiling at all is the honest way to ask the question and the
wrong way to answer it. A path holding a different number of bars is a
different path and nothing merges them, so cost climbs about 2.5x every time
the ceiling doubles. On a discography, "no ceiling" means every bar count up to
several hundred, and the search doesn't finish.

It doesn't need to. What a chart can do with SP is limited by the music, not by
the meter: past some ceiling the optimizer runs out of things to spend it on
and the score stops moving. So the **Auto** cap raises the ceiling — 16, 32,
64, … — until two runs in a row agree, and reports that score along with the
ceiling it settled on. When you pick Auto and a chart already has a record
above 4 bars, that record is reused instead of running the ladder again.

Two agreeing runs are strong evidence, not proof. A chart that runs out of
ladder, or out of time (the 120s ladder budget), says so in the path details
instead of quietly passing for a finished answer.

Measured with the retired 1.3.1 build (Expert Pro Drums 2x, depth 4); the
current build is faster, so read these as an upper bound and a shape, not
exact numbers:

| chart | 4 bars | Auto | settled at |
|---|---|---|---|
| Hail The Sun — Discography (961 SP phrases) | 3.7s | 36.4s | 64 bars |
| Rise Against — Discography | 6.1s | 38.1s | 64 bars |
| blink-182 — Discography (1,732 SP phrases) | 7.3s | 87.5s | 128 bars |
| Endless Setlist I (4.4 hours of music) | 7.7s | 98.8s | 128 bars |

## Acknowledgements
- Boddy, Beud, and Nick (BongOfDestiny) for active beta testing
- Boddy for reference footage / images shown in the Hydra Wiki
- Drummer's Monthly for being an awesome CH drums community, and for testing some of the first paths ever made by Hydra
- Smidge for all the love and support
