# How Hydra Deluxe differs from the public Hydra

The public Hydra is DragonDelgar's, at [DragonDelgar/hydra](https://github.com/DragonDelgar/hydra). It is a Python app. Its latest release is v1.3.1, from August 7, 2026.

Hydra Deluxe started from that v1.3.1 and has been developed on its own since, from 1.4.1 up to 2.0.0. Up to 1.9.7 it was also called Hydra; since 2.0.0 it is Hydra Deluxe, so the two are easy to tell apart. Nothing flows between the two automatically. The public version has kept changing too (see [What the public version has](#what-the-public-version-has-that-this-one-doesnt)).

Both do the same job. They read Clone Hero drum charts and find the Star Power paths that give the best score. The path notation is the same, and the mechanics pages on the [public wiki](https://github.com/DragonDelgar/hydra/wiki) (squeezes, early fills, the optimal checklist) apply to both.

## At a glance

| | Public Hydra 1.3.1 | Hydra Deluxe 2.0.0 |
|---|---|---|
| Written in | Python | C++, a native Windows app |
| Install | Unzip anywhere, run `Hydra.exe` | Installer into `C:\Program Files\Hydra` |
| Where results live | `records.json` | `hydra.db`, a SQLite database |
| Re-analyzing a song | Replaces the old result | Keeps one result per settings combination |
| Chart files | `.mid`, `.chart`, `.sng` | Also `.srb`, Clone Hero's built-in setlist |
| Library | Paged table, search by title or artist | One scrolling, sortable table with search filters |
| Song details | A separate window | A panel beside the library, with four tabs |
| Watching a path play | Not available | 3D note highway with the song's audio |
| SP cap | Always 4 bars | A setting; 4 is the default |
| Ghost and accent kicks | Scored as normal kicks | Scored double, like the pads |
| Hit window | 70 ms | 85 ms |
| Squeeze-in/out search window | 140 ms | 500 ms |
| Early fill window | 70 ms | 60 ms |
| Rules you can change | None | `hydra_rules.ini` |
| Analyzing many songs | `hydra_runfolder.py` script | In-app batch, plus `hydra_batch` |
| Reports | JSON from the runfolder script | HTML path report and leaderboard comparison |

The sections below explain each difference.

## Under the hood

**C++ instead of Python.** The whole app was ported to C++ in 1.4.1, and the Python code was then deleted. The port was checked against the Python version chart by chart before the switch. On the test charts, one analysis at the 4-bar cap takes under a tenth of a second.

**A database instead of a JSON file.** Results are saved in `hydra.db`. Saving one result no longer rewrites every other result, so a big library stays fast.

**Every result remembers its settings.** A result is saved together with the settings it ran under: difficulty, Pro Drums, 2x Bass, SP cap, score range and path limit. Change a setting and the library shows the results for the new one. Change it back and the old results return without analyzing again. The public version keeps one result per chart mode and replaces it when you re-analyze.

**Results know when they're out of date.** Each result carries a stamp of the analysis that made it. When an update changes the analysis, older results show as `Stale` so you know to re-analyze. A release that only changes the screen keeps your library as it is.

## What you see

**The library** is one table you scroll and sort by any column. The search box takes quoted phrases and ignores accents and colour tags. It also takes filters: `artist:`, `charter:`, `folder:`, `title:`, `stars:7` and `squeeze<=20`. Chips above the table show how many songs are not analyzed, stale, or analyzed.

**The song panel** opens beside the library instead of in its own window. `<` and `>` step through the list without closing it. `Hide library` gives the song the whole window.

**The Paths tab** shows each path's hardest timing right beside it. Each activation is one line on a timeline across the song. Each squeeze is a plain sentence saying which note to hit early or late, by how much, and what it's worth. It also flags two things the public version doesn't. The first is when a tempo or time-signature change makes an early or late activation move the SP end by more than the hit itself. The second is when a phrase fills the meter to the cap, so a different note controls where SP ends. The squeeze-free alternative is labelled **Best all-0 path**.

**The Preview tab** is new. It plays the chart as a 3D note highway in time with the song's audio, including the audio inside `.srb` bundles. It draws the chosen path on the highway: the Star Power windows, the fills you'd see, a running score, an SP meter and an SP drain timer. The highway itself is a port of Onyx's drum previewer.

**The Dynamics tab** is new. It counts ghost and accent notes per pad and for kicks. It also says whether the chart has dynamics turned on, since Clone Hero ignores them in a MIDI chart that doesn't opt in.

**The Stars tab** is new. It shows the exact score each star from 1 to 7 needs on that chart, and what the full solo bonus adds on top.

**Analyzing the whole library** happens in the app. It runs in the background on all but one CPU core, with Pause and Stop, and you can keep browsing while it runs. When it finishes it builds a sortable HTML report of every song's paths.

**Compare with dmleaderboards** is new. It fetches a player's posted scores from dmleaderboards.com and lines them up against your optimals.

## Numbers that can differ

The two versions can give different optimal scores for the same chart. There are two reasons.

**Ghost and accent kicks.** Clone Hero scores a ghost or accent kick double, the same as on the pads. Public Hydra 1.3.1 scored every kick as a normal hit, so its optimal was too low on charts with kick dynamics. Onyxite's "Won't Get Fooled Again (O)" is the chart that proved it.

**Squeeze-ins and squeeze-outs further from the SP end.** This version looks for them up to 500 ms from the SP end. The public version stops at 140 ms, so it can miss a squeeze that a strong player can hit.

Two changes affect only the display, never the path search. The hit window is 85 ms per side, which is Clone Hero's Pro Drums window. It sets the squeeze ratings and the report's difficulty tiers. The early fill window is 60 ms. Across 18,773 analyzed charts, no best path needed an early fill between 60 and 85 ms, so this changed no result.

## Things only this version can do

**Try a different SP cap.** Clone Hero's meter holds 4 bars. This version lets you set another number to see what the cap costs a chart. Scores at any cap other than 4 can't be reached in the game.

**Change the judgment calls.** A few rules are Hydra Deluxe's own choices, not facts read from Clone Hero. Examples are the backend leeway, what a squeeze-out costs, and how many tied paths to keep. `hydra_rules.ini` lets you change them. The [user guide](UserGuide.md#scoring-rules-hydra_rulesini) lists every key.

**Score by Clone Hero 1.0's fill rule.** Clone Hero 1.1 changed when a drum fill appears. The `1.0 fills` setting analyzes under the old 1.0 rule, and keeps those results beside the 1.1 ones. `hydra_batch --legacy-fills` does the same from the command line, and `hydra_fillcompare` shows every chart where the two rules disagree. It's for pricing runs recorded on 1.0.

## What the public version has that this one doesn't

**The runfolder script.** Public Hydra's `hydra_runfolder.py` analyzes a folder and writes JSON, and its newer code also writes CSV. This version has `hydra_batch` and an HTML report instead. It has no JSON or CSV export.

**A portable zip.** Public releases are a zip you can run from anywhere. This version's releases are an installer only.

**Changes made after v1.3.1.** The public version's main branch has kept moving since the split. It added dynamic kicks and handling for Star Power that ends after the song's final note. This version made its own fixes for both (1.7.9 and 1.7.3). The two sets of fixes were written separately, and nobody has checked that they give the same numbers.

**The public wiki and releases.** Public Hydra has DragonDelgar's wiki, releases and community around it. This version is private.

## Moving from the public version

This version doesn't read `records.json`, so your old results don't carry over. Add your song folders, scan, and analyze again. `Analyze library...` does the whole library in one go.

## Release history since the split

| Version | What it added |
|---|---|
| 1.4.1 | The C++ port, a SQLite database, batch analysis and the path report |
| 1.5.0 | The installer, the 85 ms hit window, timing-scale warnings on squeezes |
| 1.6.0 | The Preview, and the SP cap as a setting |
| 1.7.0 | One saved result per settings combination, and the Preview follows the chosen path |
| 1.7.3 | Backends for a last activation that runs past the end of the chart |
| 1.7.6 | The Clone Hero 1.0 fill rule (command line only), and the Preview's SP meter |
| 1.7.7 | The exact SP end saved with each result |
| 1.7.9 | Ghost and accent kicks, the overfill warning, the 60 ms early fill window, `.srb` audio |
| 1.7.10 | The Dynamics tab |
| 1.8.0 | `hydra_rules.ini`, and a running score and step controls in the Preview |
| 1.8.1 | Charts with three- or four-pad chords |
| 1.8.2 | The SP drain box, and much faster analysis and saving |
| 1.8.3 | The Stars tab |
| 1.8.4 | Results stay current across releases that don't change the analysis |
| 1.9.0 | The redesigned screen: song panel, sortable library, search filters, timeline |
| 1.9.1 to 1.9.6 | Polish: early-fill badges, clearer squeeze tips, every timing scale shown |
| 1.9.7 | The `1.0 fills` setting in the app |
| 2.0.0 | The new name, Hydra Deluxe |

Each release's full notes are on the [releases page](https://github.com/splax77/hydra-deluxe/releases).
