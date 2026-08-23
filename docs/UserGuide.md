# Hydra User Guide

Here's a run-through of the Hydra UI. Screens are presented, then explained from top to bottom.

> Note: the screenshots below are from the pre-1.4 UI. The layout is the
> same, but details and styling have moved on.

## Main Screen

View your song library and quickly reference optimal paths for songs.

<p align="center"><img src="/docs/images/app_library.PNG"></p>

### Manage folders...
Opens the song folders window, where you add and remove the folders Hydra scans.

These will probably be your Clone Hero song folder(s). Hydra will find all charts in subfolders. You can add more than one folder; each one has a red `X` to remove it (with a confirmation, since removing a folder changes what the next scan finds).

Hydra recognizes `notes.mid`/`notes.chart` + `song.ini` folders, `.sng` archives, and the `.srb` bundles Clone Hero's built-in setlist ships with — so adding the game's own `Clone Hero_Data\StreamingAssets\songs` folder brings in the default songs too.

### Scan charts / Refresh scan
Starts a scan and updates what songs will be viewable in Hydra.

If the folders are unchanged since your last scan, the button will say `Refresh scan`.

Currently, songs require a valid ini file to show up. There may be a case where a song is playable in Clone Hero yet Hydra calls it invalid, but this should be very rare.

If a song fails, it'll be skipped. The progress window will list any problems when the scan completes. Once the scan is done, all successful songs will appear on the main screen!

Songs that were scanned remain visible until the next scan, even if you did something to those chart files. If you add, remove, or edit songs, be sure to re-scan.

Re-scans are fast: charts that haven't changed since the last scan are recognized by their file sizes and timestamps and don't need to be read again — only new or modified charts are. A scan can also be cancelled mid-way, which leaves the previous library untouched.

### Analyze library / Analyze search (N)
Analyzes every chart in the library in one batch, using all but one of your CPU cores. If the search box is filtered, the button becomes `Analyze search (N)` and only analyzes the matching charts.

A confirmation shows how many charts are about to be analyzed before anything starts — a full library can take a while — and the run can be cancelled at any time. Charts that already have a stored result are skipped unless `redo existing` is checked.

When a batch finishes, Hydra builds the **path index report**: a sortable, searchable HTML page of every analyzed chart's paths, squeeze timings, and scores. Use `Open path report` in the finished dialog (or the same button next to the search box any time after) to open it in your browser; check `Open automatically` if you'd rather it always opens itself.

### View Options (Pro Drums / 2x Bass)
Path/scoring analysis depends on difficulty, whether it's Pro Drums, and whether 2x bass is enabled. When you analyze a song, that analysis result is for that particular combination of options and it'll only be visible when that combination is selected. (Only Expert difficulty is supported right now, so it's shown as a fixed label.)

For example, if you analyzed a song with 2x Bass enabled, but want to see what it would be with 1x bass, simply uncheck 2x Bass and analyze the song again. Whenever you re-check 2x Bass, _that_ analysis will come back.

### Search
Filter the song library by an input string. Songs whose title, artist, or charter match will be shown. `Ctrl+F` jumps to the search box.

### Compare dmleaderboards user
Compares a [dmleaderboards.com](https://dmleaderboards.com) player's posted scores against your library's stored optimals. Pick a user from the searchable ladder (the last pick is remembered), and Hydra fetches their scores, joins them to your analyzed charts by chart hash, and opens a sortable HTML comparison page: actual score, Hydra's optimal, the points left, and a status per row.

`above optimal` rows are expected, not errors: Hydra's optimal intentionally excludes several score backends, and many leaderboard scores were set on older Clone Hero versions whose fill spawning allowed totals that are impossible now. Only charts you have analyzed (in the current view options) can be matched — analyze your library first for a full comparison.

The first request after a while can take tens of seconds; the leaderboard's backend has to wake up.

### Library table
The list of songs from the latest scan, filtered by the search box. The **Best Path** column shows each song's status for the current view options:

- **`(New...)`** — not analyzed yet.
- A gold path string — the optimal path, for quick reference.
- **`(Stale)`** — analyzed by an older Hydra version (or the other edition); re-analyze to refresh it.

Hover the cell for an explanation. Click on a song's row to go to the details screen for that song. Column widths can be resized and are remembered between sessions.

### Page left/right
Down by the bottom are arrow buttons to move between pages of your song library.

## Song Details Screen

Clicking into a song will lead to this screen. This screen displays analysis for the current View Options (Pro Drums / 2x Bass).

<p align="center"><img src="/docs/images/app_songdetails_analyzed.PNG"></p>

### Song Metadata (upper left)
Information pulled from the chart's ini (or the archive's embedded metadata, for `.sng`/`.srb`) during the latest scan: Title, Artist, Charter.

Lastly, a hash/checksum (the big hexadecimal number) is also shown here; identical chart files will have the same value. However, this comparison is only good within Hydra. Clone Hero and other apps have their own versions of this and they probably don't align.

### Stored result (upper middle)
A summary of the saved analysis for this song under the current view options and SP cap: the best score, how many paths were kept, and the settings the analysis ran with (timing limit and SP cap; an Auto run shows the cap it settled on). If the song hasn't been analyzed at this cap — or the record is stale — that's shown here instead.

### Analysis Controls (upper right)
Look here to generate paths for a song (and a lot of scoring info too).

#### Score range
A global setting for how many extra paths below optimal you'd like Hydra to keep during analysis. These paths are worse, but usually only by a little bit. Only a few are really necessary, in case the optimal path is unusually difficult.

Note that the more extra paths are allowed, the longer analysis will take, though a few should be no problem.

There are two depth modes. You can keep some extra paths based on a certain number of `scores` (i.e. "the next best score under optimal") or a certain amount of `points` (i.e. "paths that are within 2000 points of optimal").

#### Limit timings
When enabled, extra paths are only kept if their hardest required squeeze is within this many milliseconds — useful if you want alternates you can realistically hit. Lower (or negative) values demand more slack. The limit a record was analyzed with is shown above its path list.

Note: the limit compares raw squeeze milliseconds, measured at the SP end. It does not account for frontend timing scaling (see the note under Backends below), so where an activation shows a scale warning, a kept path can be somewhat harder to execute than its listed milliseconds suggest.

#### SP cap
The Star Power meter ceiling the analysis runs under, in bars. 4 is Clone Hero's rule and the default; leave it there for paths you intend to play. Any other number is a what-if whose scores are not achievable in game. Tick **Auto** to let Hydra raise the cap until the score stops improving, which approximates no ceiling at all (slower: up to two minutes per chart).

Results are kept per cap. Changing the cap switches which record the app shows, and a chart is only analyzed again when it has no record at the current cap. The path report follows the current cap; the leaderboard comparison only runs at 4 bars.

If you used the pre-1.6 Hydra Uncapped app, its records are copied into the main library the first time 1.6 opens. The old `hydra_uncapped.db` is left untouched.

#### Analyze button
Smash this button to analyze the song and generate paths. The result will be saved and pulled up again whenever you check on this song in the future. Long analyses show a progress bar and can be cancelled; closing the window also cancels them.

If the chart's file has moved or been deleted since the last scan, the button is disabled and a `Rescan library` shortcut appears instead.

### Path List (lower left)
A list of the paths that were found, organized by score. Click on a path to view that path's details in the panel to the right.

### Path Details (lower right)
When it comes to getting an optimal score, following the optimal path is only part of the story. The rest is (sort of) explained here, though it's sort of an info dump at the moment.
But here's a walkthrough.
<p align="center"><img src="/docs/images/app_songdetails_details.PNG"></p>

#### Copy path string
Copies the selected path's notation to the clipboard, ready to paste anywhere. `Ctrl+C` does the same for the currently selected path.

#### Multiplier squeezes
When building up combo at the start of a song, sometimes the combo multiplier goes up on a multi-note chord. Since individual notes are scored exactly when they're hit, in this situation you can control which notes are scored on the higher multiplier by hitting those notes *later*. If those notes are also more valuable (cymbal and dynamic notes), then this can result in slightly more points.

If you've ever seen FC scores that seem to be identical but one is +15, this is what happened there.

This details panel will list out these multiplier squeezes if they happen in this song. `2x` means it happens when hitting 2x multiplier (10 notes into the song), and so on.

#### Activations

A list of activations in this path. The number from the path notation is shown as well as how many bars of SP you'll have at that activation. Measure number is also shown, if you happen to be referencing an image or some other view of the chart.

If an activation has a [calibration fill](https://github.com/DragonDelgar/hydra?tab=readme-ov-file#calibration-fills-e) (the `E` notation), the timing of that calibration fill will be listed. Usually this will be `0ms`. The more negative the value, the more early you have to hit to make the calibration fill show up.

Frontend: The chord that the activation is on. If it's a multi-note chord, perform a frontend squeeze by hitting the activation note first, so that the other notes are scored with the Star Power multiplier.

Backends: The notes surrounding the end of Star Power for this activation. There is probably a note at `0ms`, which is exactly when SP ends; the others are the notes right before and right after.

Perform a backend squeeze by hitting the `0ms` note early, so that it lands during Star Power.

The backends have a (made up by me) rating that just conveys how difficult it would be to fit that note into Star Power. If you're interested in double backend squeezes, look here for notes that are in the `3ms` to `85ms` range (the upper edge follows the hit-window setting). Or even higher if you're crazy. Whether these double backends are actually possible depends on some details that aren't considered by Hydra yet...

One important caveat: Star Power length is measured in measures, not milliseconds. If the SP end falls where measures last a different amount of time than at the activation point (a different time signature and/or tempo), frontend timing only partially transfers to the SP end — hitting the activation 50ms late might move the SP end only 25ms. When this matters, the activation details show a scale warning (e.g. `x0.51`), and affected backend rows show an effective timing (`eff.`) that puts the real difficulty back on the nominal two-hit scale (twice the hit window). The warning appears whenever the scaling is material to a listed squeeze — even a ratio within a fraction of a percent of 1.0 shows up when a large gap makes it decide success. Late and early frontend hits can even scale differently, when the activation or the SP end sits exactly on a signature or tempo change.

One INI setting feeds these displays (`hydra_settings.ini`, no UI control yet): `hit_window_ms` (default 85 — the registrable Clone Hero Pro Drums window per side).

#### Score breakdown

The score that this path should get, following the same categories that Clone Hero uses in its results screen.

The score includes multiplier squeezes, frontend squeezes, and backend squeezes, so if you follow the path but your score has a bit less Star Power score than this readout, one of those was probably missed.

Double squeezes are currently not considered in this scoring unless they're `2ms` or less, there's a slight margin.

## Command line tools

Two console programs ship alongside the app and share its settings and library:

- **`hydra_batch`** — runs the same batch analysis as `Analyze library`, printing one line per chart. Flags: `--redo` (re-analyze existing results), `--reindex`, `--db <path>`.
- **`hydra_report`** — rebuilds the HTML path index from stored results. Flags: `--paths N`, `--all-paths`, `--out <path>`, `--no-open`, `--db <path>`.

Both use the chart mode and SP cap from the app's settings file.
