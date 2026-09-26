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

### View Options (Difficulty / Pro Drums / 2x Bass)
Path/scoring analysis depends on difficulty, whether it's Pro Drums, and whether 2x bass is enabled. When you analyze a song, that analysis result is for that particular combination of options and it'll only be visible when that combination is selected. Pick the difficulty from the dropdown: Expert, Hard, Medium or Easy. 2x Bass only exists on Expert, so it is greyed out on the other three. The dmleaderboards comparison also needs Expert, because the leaderboard only carries Expert scores.

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
- **`(Stale)`** — analyzed by an older Hydra version, or under different rules in `hydra_rules.ini`; re-analyze to refresh it. Switching the rules back brings the old results back.

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
A summary of the saved analysis for this song under the current view options and SP cap: the best score, how many paths were kept, and the settings the analysis ran with (path limit and SP cap; an Auto run shows the cap it settled on). If the song hasn't been analyzed at this cap — or the record is stale — that's shown here instead.

### Analysis Controls (upper right)
Look here to generate paths for a song (and a lot of scoring info too).

#### Score range
A global setting for how many extra paths below optimal you'd like Hydra to keep during analysis. These paths are worse, but usually only by a little bit. Only a few are really necessary, in case the optimal path is unusually difficult.

Note that the more extra paths are allowed, the longer analysis will take, though a few should be no problem.

There are two depth modes. You can keep some extra paths based on a certain number of `scores` (i.e. "the next best score under optimal") or a certain amount of `points` (i.e. "paths that are within 2000 points of optimal").

#### Path limit
When enabled, extra paths are only kept if their hardest required squeeze is within this many milliseconds — useful if you want alternates you can realistically hit. Lower (or negative) values demand more slack. The limit a record was analyzed with is shown above its path list.

Note: the limit compares raw squeeze milliseconds, measured at the SP end. It does not account for frontend timing scaling (see the note under Backends below), so where an activation shows a scale warning, a kept path can be somewhat harder to execute than its listed milliseconds suggest.

#### Backend limit
This is a display-only filter for the Backends tables in Path Details, not something the analyzer uses. When enabled, a backend row only shows if its timing is within plus/minus this many milliseconds — except a note the path squeezes out, which always shows no matter how far out it is. Off (the default) shows everything the analyzer stored, which reaches out to ±500ms. Because it only changes what's displayed, flipping it never triggers a re-analysis.

#### SP cap
The Star Power meter ceiling the analysis runs under, in bars. 4 is Clone Hero's rule and the default; leave it there for paths you intend to play. Any other number is a what-if whose scores are not achievable in game. Tick **Auto** to let Hydra raise the cap until the score stops improving, which approximates no ceiling at all (slower: up to two minutes per chart).

Results are kept per cap. Changing the cap switches which record the app shows, and a chart is only analyzed again when it has no record at the current cap. The path report follows the current cap; the leaderboard comparison only runs at 4 bars.

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

If the last activation's Star Power runs past the end of the chart, the table lists the notes before that SP end, so every timing is negative.

Perform a backend squeeze by hitting the `0ms` note early, so that it lands during Star Power.

The backends have a (made up by me) rating that just conveys how difficult it would be to fit that note into Star Power. If you're interested in double backend squeezes, look here for notes that are in the `3ms` to `85ms` range (the upper edge follows the hit-window setting). Or even higher if you're crazy. Whether these double backends are actually possible depends on some details that aren't considered by Hydra yet...

One important caveat: Star Power length is measured in measures, not milliseconds. If the SP end falls where measures last a different amount of time than at the activation point (a different time signature and/or tempo), frontend timing only partially transfers to the SP end — hitting the activation 50ms late might move the SP end only 25ms. When this matters, the activation details show a scale warning (e.g. `x0.51`), and affected backend rows show an effective timing (`eff.`) that puts the real difficulty back on the nominal two-hit scale (twice the hit window). The warning appears whenever the scaling is material to a listed squeeze — even a ratio within a fraction of a percent of 1.0 shows up when a large gap makes it decide success. Late and early frontend hits can even scale differently, when the activation or the SP end sits exactly on a signature or tempo change.

Sometimes a phrase you collect partway through Star Power fills the meter
all the way to the SP cap (the most bars of SP you can hold at once).
When that happens, the activation details show an overfill warning. It
means the note that filled the meter — not the activation — is now the one
whose early or late timing moves the SP end. The squeeze numbers below the
warning are unaffected by this; only which note you'd need to move to change
them has shifted.

One INI setting feeds these displays (`hydra_settings.ini`, no UI control yet): `hit_window_ms` (default 85 — the registrable Clone Hero Pro Drums window per side).

#### Score breakdown

The score that this path should get, following the same categories that Clone Hero uses in its results screen.

The score includes multiplier squeezes, frontend squeezes, and backend squeezes, so if you follow the path but your score has a bit less Star Power score than this readout, one of those was probably missed.

Double squeezes only count in this score when the backend note lands inside a small leeway past the Star Power end. The leeway is `3ms` by default. You can change it with `backend_leeway_ms` in `hydra_rules.ini` (see below).

### Dynamics tab

The third tab in the details screen counts the chart's ghost and accent notes. Ghosts and accents are the soft and hard hits that score double in Clone Hero.

The Pads table shows, for each pad (and each cymbal separately under Pro Drums), how many notes are ghosts, accents and normal hits. The Kicks section does the same for kicks, with 2x kicks on their own row and a line saying how many of the kick notes are 2x. When 2x Bass is off, the 2x kick row stays visible but greyed out and is left out of the totals.

The Chart box says whether the chart has dynamics turned on. A MIDI chart has to opt in; without that flag Clone Hero ignores the velocity markings, so Hydra reports the counts but notes that the game will not apply them.

Counts are worked out the first time you open the tab and saved in the library database, so the tab opens instantly after that. Analyzing a song with 2x Bass on also saves its counts as a by-product.

## Scoring rules (`hydra_rules.ini`)

A few of Hydra's rules are judgment calls, not facts read from Clone Hero. You can change them in `hydra_rules.ini`, a plain text file next to Hydra.exe. The app and every command line tool read the same file.

The file is optional. A missing file, or a missing line, means the default below. Each line is `key = value`. A line starting with `#` is a comment. There are no `[section]` headers.

```ini
# Hydra's defaults
backend_leeway_ms = 3.0
sqout_rule = first_note
max_tied_paths = 4
auto_cap_ladder = 16,32,64,128,256,512
auto_budget_s = 120
fill_cooldown_measures = 4
fill_max_distance_beats = 0.5
fill_length_measures = 0.5
fill_land_slop_beats = 0.03125
```

What each line does:

- **`backend_leeway_ms`** (default `3.0`): how far past the Star Power end a backend note can land and still count in the score.
- **`sqout_rule`** (default `first_note`): what a squeeze-out costs. `first_note` removes the Star Power doubling from one note of the chord (the lowest-value one). `whole_chord` removes it from every note in the chord.
- **`max_tied_paths`** (default `4`): how many paths Hydra keeps when several reach the same score. More paths means longer lists and slower analysis.
- **`auto_cap_ladder`** (default `16,32,64,128,256,512`): the SP caps Auto tries, in rising order, until the score stops changing. Separate them with commas.
- **`auto_budget_s`** (default `120`): how many seconds Auto may spend on one chart before it stops climbing the ladder.
- **`fill_cooldown_measures`** (default `4`): for charts with no authored fills, how many measures must pass after an activation point before Hydra places the next one.
- **`fill_max_distance_beats`** (default `0.5`): for charts with no authored fills, how far from a measure line a note can sit and still get a fill.
- **`fill_length_measures`** (default `0.5`): for charts with no authored fills, how long each fill Hydra places is, in measures.
- **`fill_land_slop_beats`** (default `0.03125`, a 32nd of a beat): how close a fill's end must be to a note for the fill to count. This one applies to authored fills too.

A value Hydra can't read, or a key it doesn't know, is an error that names the key. The command line tools print the error and stop with exit code 2. The app still opens and shows the error, but Analyze stays off until you fix the file and restart Hydra. Hydra never analyzes on the defaults behind your back.

Every analysis result remembers the rules it was made with. After you change the file, results made under the old rules show **`(Stale)`** until you re-analyze them. Switching the rules back brings those results back.

## Command line tools

Two console programs ship alongside the app and share its settings and library:

- **`hydra_batch`** — runs the same batch analysis as `Analyze library`, printing one line per chart. Flags: `--redo` (re-analyze existing results), `--reindex`, `--db <path>`, `--rules <path>`, `--legacy-fills`. `--legacy-fills` prices charts under Clone Hero 1.0's fill rule instead of 1.1's. It is a command-line-only mode, and it refuses to write the app's own database, so give it its own `--db`.
- **`hydra_report`** — rebuilds the HTML path index from stored results. Flags: `--paths N`, `--all-paths`, `--out <path>`, `--no-open`, `--db <path>`, `--rules <path>`.

Both use the chart mode and SP cap from the app's settings file. Both read the scoring rules from `hydra_rules.ini` next to Hydra.exe, or from the file `--rules` names. If that file has an error, they print it and stop with exit code 2.
