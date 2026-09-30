# Hydra User Guide

This guide walks through Hydra's screen in the order you meet it. First the toolbar, then the analysis settings, then the library and its search. After that comes the song panel with its four tabs, then analyzing the whole library, and last the reports.

It covers this version of Hydra. The public Hydra by DragonDelgar has a different screen; see [how the two differ](differences-from-public-hydra.md). For what the path notation and squeezes mean, see the [public Hydra wiki](https://github.com/DragonDelgar/hydra/wiki).

## The main screen

Hydra has one main screen. The toolbar runs along the top. The **Analysis settings** bar sits under it. The library fills the rest of the window.

Click a song and the **song panel** opens beside the library. Drag the library's right edge to give either side more room. Hydra remembers the width.

When something goes wrong, a message appears under the toolbar. A plain notice fades by itself. A problem stays in orange until you dismiss it with its `X`.

### Toolbar

**`Manage folders... (N)`** opens the **Song folders** window. N is how many folders you have. Add your Clone Hero song folder (or any folder of charts) with `Add folder...`. Hydra finds every chart in its subfolders. Each folder has a red `X` to remove it, with a confirmation, because removing a folder changes what the next scan finds.

Hydra reads `notes.mid`/`notes.chart` + `song.ini` folders, `.sng` archives, and the `.srb` bundles Clone Hero's built-in setlist ships with. Adding the game's own `Clone Hero_Data\StreamingAssets\songs` folder brings in the default songs too.

**`Scan library`** reads your folders and updates the song list. A song needs a valid ini file to show up. A song that fails is skipped, and the scan window lists the problems when it finishes. Re-scans are fast: a chart whose file size and timestamp haven't changed is not read again. You can cancel a scan part-way; the old library stays as it was.

Songs stay in the list until the next scan, even if you changed their files. After you add, remove or edit songs, scan again.

**`Analyze library...`** analyzes many songs at once, in the background. While you are searching, the button reads **`Analyze search (N)...`** and only analyzes the N songs your search shows. See [Analyzing the whole library](#analyzing-the-whole-library).

**`Compare with dmleaderboards...`** compares a player's leaderboard scores with your stored results. See [Reports](#reports).

**`Open path report`** opens the last path report in your browser. It appears once a report exists.

## Analysis settings

The settings bar holds every setting that shapes an analysis. Each one applies to every song, not just the one you have open. A result is saved together with the settings it ran under. Change a setting and the library shows the results for the new settings. Change it back and the old results come back, with no re-analysis.

While a batch runs, the bar is locked and says `Stop the batch to change these.` While one song analyzes, it says `Settings are locked while this song analyzes.`

**Difficulty.** Which charted difficulty to analyze, path and preview: Expert, Hard, Medium or Easy.

**Pro Drums.** Analyze with cymbals and toms as separate notes, the way Clone Hero scores Pro Drums.

**2x Bass.** Include the chart's 2x kicks. 2x Bass only exists on Expert, so it is greyed out on the other difficulties.

For example, say you analyzed a song with 2x Bass on and want to see it with 1x bass. Untick 2x Bass and analyze again. Tick it again and the 2x result comes back.

**SP cap.** The most bars of Star Power the meter can hold during the analysis. 4 is Clone Hero's rule and the default. Leave it at 4 for paths you mean to play. Any other number is a what-if: its scores can't be reached in the game. Results are kept per cap, so a 4-bar result and a 16-bar result for the same song sit side by side. The leaderboard comparison only runs at 4 bars.

**Score range.** How many extra paths below optimal to keep. The dropdown picks the unit. `scores` keeps the next few distinct scores under optimal. `points` keeps every path within that many points of optimal. These paths are a little worse, but handy when the optimal path is awkward to play. More paths make the analysis slower.

**Path limit.** When ticked, an extra path is kept only when its hardest squeeze is within this many milliseconds. That keeps alternates you can realistically hit. Lower or negative values demand more slack. The limit compares raw squeeze milliseconds at the SP end. It does not account for frontend timing scaling (see the Paths tab), so a kept path can be a bit harder than its number suggests where an activation's scale line is orange.

## Searching the library

The library lists every song from the latest scan. Its columns are **Title**, **Artist**, **Charter**, **Folder** and **Best path**. Click a column heading to sort by it. Columns can be resized and hidden, and Hydra remembers them, the sort included. The columns always stay in this order. While the song panel is open, the Charter and Folder columns step aside to save room, and the folder shows under the title.

The **Best path** cell shows the song's state under the current analysis settings:

- `Not analyzed` means there is no result yet.
- A score and a path, like `378,315  3- 1 2`, is the optimal path.
- `Stale` means the result is out of date. Either another Hydra version made it, or it was made under different rules in `hydra_rules.ini`. Re-analyze to refresh it. If the cause was a rules edit, switching the rules back brings the result back.

Hover the cell for the same explanation.

The chips above the table filter by state: `All`, `Not analyzed`, `Stale` and `Analyzed`, each with its count. A chip with a count of 0 is greyed out.

### The search box

Type in the search box to narrow the list. `Ctrl+F` jumps to it. `Escape` or its `X` clears it. The heading above shows how many songs match, like `Library 5 of 97 charts`.

Searching ignores case and accents, so `ALLISTER` finds Allister and `beyonce` finds Beyoncé. Colour and style tags in song names are ignored too. The examples below come from Hydra's test library.

| You type | What it matches | Example |
|---|---|---|
| words | songs whose title, artist, charter or folder contain every word, in any order | `day green` finds Burnout by Green Day |
| `"quoted text"` | that exact phrase | `"dance gavin dance"` finds the band's 2 songs |
| `artist:` | the word or phrase in the artist only | `artist:rush` finds YYZ |
| `charter:` | the charter only | `charter:onyxite` finds 8 songs |
| `folder:` | the folder only | `folder:"tier 4"` finds the 5 songs in Tier 4 |
| `title:` | the title only | `title:everlong` finds Everlong |
| `stars:N` | songs whose optimal path earns exactly N stars (0 to 7) | `stars:7` finds Burnout once it is analyzed |
| `squeeze<=N` | songs whose optimal path's hardest squeeze is N ms or less | `squeeze<=200` keeps Burnout (163 ms); `squeeze<=20` drops it |

You can mix them: `charter:hoph2o stars:7`. `squeeze<N` works the same as `squeeze<=N`. A path with no squeeze at all passes any squeeze limit.

`stars:` and `squeeze<=` only look at current results. A song that is not analyzed, or whose result is Stale, never matches them. A filter Hydra can't read, like `stars:9`, shows a short note under the search box and is left out of the search.

Under the table, a search shows a `Clear search` button. When a match came from a column that is hidden, a note there says so, like `Matched on folder.`

## The song panel

Click a song to open its panel beside the library. The top shows the title, the artist and the charter. The `<` and `>` buttons step to the previous and next song in the list. The `X` button, or `Escape`, closes the panel.

**`Hide library`**, left of `<`, gives the song panel the whole window. It then reads `Show library`, which puts the library back at the width you left it. `<` and `>` still step through the list while it's hidden. Closing the panel always brings the library back. Hydra remembers the choice, so the next song opens the same way, even after a restart.

The button on the right analyzes the song. It reads `Analyze this song` when there is no result, and `Re-analyze` when there is one. A long analysis shows a progress bar and can be cancelled. Closing the panel does not stop it: the analysis finishes, is saved, and the library row updates.

Under the button is the headline: the best score, its path, and one line like `Optimal path · 7 stars`. A Stale result says why it is out of date.

If the song's file has moved or been deleted since the last scan, the panel says `Song file not found.` and offers a `Rescan library` button.

The panel has four tabs: **Paths**, **Preview**, **Dynamics** and **Stars**.

## Paths tab

The left side lists the paths the analysis kept, grouped by score. **Optimal** comes first. Then come the extra paths under a heading like **Within 2 scores**. Last is **Best all-0 path**: the best path that activates at the first chance every time, with no skips and no squeeze timing, and how far it falls below optimal. Click a path to show it on the right.

Each path shows its own hardest timing right after it, like `378,315 · 3- 1 2   163.0 ms`: the hardest squeeze or early fill that path needs. It turns orange past the difficult limit. A path that needs no timing shows nothing there.

The right side starts with a summary, like `Activations 3 · no SP left over`. Under it, a timeline runs from the first measure to the last, with a mark for each activation. For a result saved before Hydra 1.9, the timeline appears a moment after you open the song: Hydra reads the chart once for its length and remembers it. Nothing is re-analyzed.

Each activation is one row. It shows the activation's number, its notation, its measure (like `m32.1.0`), how many bars of SP you spend, and a badge for a squeeze, like `squeeze out 163 ms`. Click a row to open it, or use `Expand all` and `Collapse all`.

An open row shows:

- **Chord**: the chord you activate on. On a multi-note chord, do a frontend squeeze: hit the activation note first, so the other notes score with the Star Power multiplier.
- **`Show in Preview >`**: jumps the Preview tab to this activation.
- The early fill timing, when the activation has one (the `E` notation). Usually this is `0ms`. The more negative the value, the earlier you have to hit to make the fill show up.
- A plain sentence for each squeeze: which note to hit early or late, by how much, and what it's worth.
- **Backend timings**: the notes near the end of Star Power, with a short lead-in above the table.

A backend squeeze means hitting the note at the SP end early, so it lands inside Star Power. The note at `0ms` is exactly where SP ends; the others are the notes just before and after it. If the last activation's SP runs past the end of the chart, the table lists the notes before that end, so every timing is negative. The rating beside each note is a rough guide to how hard it is to fit into Star Power. For double backend squeezes, look for notes in the `3ms` to `85ms` range (the upper edge follows the hit-window setting).

Star Power length is measured in measures, not milliseconds. If the SP end falls where measures last a different time than at the activation (a tempo or time signature change), frontend timing only partly carries to the SP end. Hitting the activation 50 ms late might move the SP end only 25 ms. Whenever the scale isn't x1.00, the opened row shows it, early first: `Frontend timing scales x0.99 (early) / x4.45 (late) at the SP end.` Late and early hits can scale differently when the activation or the SP end sits right on a change. A side that is x1.00 is left out. If a SqIn moved the SP end, that earlier end gets its own clause. The line is orange when the scale changes a squeeze or backend figure, and those backend rows show an effective timing (`eff.`). Otherwise it is gray.

Sometimes a phrase collected during Star Power fills the meter up to the SP cap. Then the row shows an overfill warning. It means the note that filled the meter, not the activation, is now the one whose timing moves the SP end. The squeeze numbers are unaffected; only the note you would move has changed.

Below the activations:

- **`Multiplier squeeze`** lists the multiplier squeezes in the song. When the combo multiplier goes up on a multi-note chord, hitting the more valuable notes (cymbals, dynamics) a little later scores them on the higher multiplier. It's why two FCs of the same song can differ by 15 points. `2x` means it happens as the multiplier reaches 2x (10 notes in), and so on.
- **`Score breakdown`** shows the score this path should get, in the same categories as Clone Hero's results screen. It includes multiplier, frontend and backend squeezes. A double squeeze only counts when the backend note lands inside a small leeway past the SP end, `3ms` by default (`backend_leeway_ms` in `hydra_rules.ini`).
- **`Copy path`** copies the selected path's notation. `Ctrl+C` does the same and flashes `Copied!`.
- **`Hide backend rows beyond`** hides backend rows more than this many ms from the SP end. A note the path squeezes out always shows. This only changes the display. It is not an analysis setting, so changing it never re-analyzes anything.

One more setting feeds these displays: `hit_window_ms` in `hydra_settings.ini` (default 85, the Clone Hero Pro Drums hit window per side). It has no control on screen yet.

## Preview tab

The Preview plays the chart as a 3D note highway, in time with the song's audio. It draws the chosen path on the highway: the Star Power windows as a tinted floor, and the fills a player following the path would see.

**`Showing`** picks which path to draw. It lists the same paths as the Paths tab, in the same order. The all-0 path reads like `0 0 0 0  (best all-0)`.

**`< Act`** and **`Act >`** jump to the previous and next activation. The `[` and `]` keys do the same. `Show in Preview >` on the Paths tab lands here too.

The transport buttons are `-5s`, `< 5 Ticks`, `Play`/`Pause`, `5 Ticks >` and `+5s`, with a volume slider beside them. The keys:

- `Space` plays and pauses.
- `Left` and `Right` arrows jump 5 seconds.
- `,` (comma) and `.` (period) step 5 ticks.
- `[` and `]` jump between activations.

The same keys are drawn as keycaps in a bar under the highway, in the buttons' order. Each button's tooltip names its key too.

The scrubber under the highway shows where you are. Its gold marks are the path's activations.

The boxes on the highway:

- **Time box** (top left): the measure you're at and the song's last measure, the tempo and time signature, and the practice section when the chart has them.
- **Score box** (under it): the running score in large type, then the multiplier and combo, like `x4 · combo 212`. It appears once the song is analyzed. It reads `Score unavailable` when the path can't be replayed to its stored score.
- **Next activation** (bottom left): the next activation's number, where it is, and its chord.
- **SP meter** (right edge): a gauge of banked Star Power, one line per bar, with the bars shown under it. It rises one bar at each phrase you collect and drains through each activation, reaching empty exactly where the path's SP ends.
- **Drain box** (top right, beside the meter): how long one bar of SP lasts here, like `1 bar / 2.5 s`. While SP is running on the path it reads `SP drain` and `empties in` the time left. Otherwise it reads `SP drain (if activated)` and how long a full meter would last from here.

## Dynamics tab

This tab counts the chart's ghost and accent notes. Ghosts and accents are the soft and hard hits that score double in Clone Hero.

The **Pads** table shows, for each pad (and each cymbal separately under Pro Drums), how many notes are ghosts, accents and normal hits. **Kicks** does the same for kicks, with 2x kicks on their own row and a line saying how many kick notes are 2x. When 2x Bass is off, the 2x kick row stays visible but greyed out and is left out of the totals. **Totals** adds them up.

The **Chart** box says whether the chart has dynamics turned on. A MIDI chart has to opt in. Without that flag Clone Hero ignores the velocity markings, so Hydra shows the counts but notes that the game won't apply them.

Counts are worked out the first time you open the tab and saved, so it opens instantly after that. Analyzing a song with 2x Bass on also saves them.

## Stars tab

This tab shows the score you need for each star.

**Base score** is every note hit once at 1x, with no Star Power. **Solo bonus** is the chart's full solo bonus. The solo bonus does not count toward stars: Clone Hero compares your score without it.

The table lists stars 1 to 7. **Multiplier** is what the base score is multiplied by (0.1, 0.5, 1.0, 2.0, 2.8, 3.6 and 4.4). **Cutoff** is the score that earns the star, rounded up. **With full solo bonus** is the cutoff plus the whole solo bonus: what the results screen shows at that cutoff if you also collect every solo bonus.

A star counts once your score, without the solo bonus, reaches its cutoff.

## Analyzing the whole library

`Analyze library...` analyzes every song in the library. While you are searching, `Analyze search (N)...` analyzes only the N songs the search shows.

First a window titled **Analyze library** asks you to confirm. It says how many songs it will analyze and lists the settings it will use. To change those, cancel and edit the Analysis settings. Songs that already have a result are skipped, unless you tick `Also re-analyze charts that already have a result`. Click `Start analyzing` to begin, or `Cancel`.

The batch runs in the background, using all but one of your CPU cores. You can keep browsing and open songs while it runs. A strip under the toolbar shows its progress: how many songs are done, the song being analyzed, the time so far and, after a few songs, an estimate of the time left.

- **`Pause`** holds the batch. **`Resume`** carries on.
- **`Stop`** ends it. Every result finished so far is kept.

The Analysis settings stay locked until the batch ends.

When it finishes, the strip changes to a summary: how many songs were analyzed, failed and skipped, and how long it took. Songs that failed are listed with the reason. A stopped batch keeps its results but builds no report.

## Reports

### The path report

A finished batch builds the **path report**, `hydra_paths.html`. It is a sortable, searchable web page of every analyzed song's paths, squeeze timings and scores. It follows the current analysis settings.

Reports are saved in your **Documents\Hydra** folder. Hydra makes the folder if it's missing. If Windows can't find your Documents folder, the report goes next to Hydra's database instead. A report an older Hydra saved next to `hydra.db` stays there; the next batch writes a new one in Documents\Hydra.

The finished strip offers:

- **`Open report`** opens it in your browser.
- **`Show in folder`** opens the folder that holds it.
- **`Open automatically`** opens the report by itself whenever a batch finishes.
- Its `X` dismisses the strip.

`Open path report` on the toolbar opens the last report at any time.

### Compare with dmleaderboards

`Compare with dmleaderboards...` compares a [dmleaderboards.com](https://dmleaderboards.com) player's posted scores with your stored optimals. Pick a player from the searchable list; Hydra remembers the last pick. Hydra fetches their scores, matches them to your analyzed songs by chart hash, and saves a sortable page, `hydra_dmcompare.html`, in the same Documents\Hydra folder. The page lists each score, Hydra's optimal, the points left, and a status per row.

When it's done, the window counts the scores that matched, the ones above optimal, the ones for songs you haven't analyzed, and the ones for songs not in your library.

Rows above optimal are expected, not errors. Hydra's optimal leaves out several score backends on purpose. Many leaderboard scores were also set on older Clone Hero versions, whose fill rules allowed totals that are impossible now.

The comparison needs Expert and an SP cap of 4, because the leaderboard only holds Expert scores played under Clone Hero's rule. Only songs analyzed under the current settings can match, so analyze your library first for a full comparison.

The first request after a while can take tens of seconds, because the leaderboard's server has to wake up.

## Scoring rules (`hydra_rules.ini`)

A few of Hydra's rules are judgment calls, not facts read from Clone Hero. You can change them in `hydra_rules.ini`, a plain text file next to Hydra.exe. The app and every command line tool read the same file.

The file is optional. A missing file, or a missing line, means the default below. Each line is `key = value`. A line starting with `#` is a comment. There are no `[section]` headers.

```ini
# Hydra's defaults
backend_leeway_ms = 3.0
sqout_rule = first_note
max_tied_paths = 4
fill_cooldown_measures = 4
fill_max_distance_beats = 0.5
fill_length_measures = 0.5
fill_land_slop_beats = 0.03125
```

What each line does:

- **`backend_leeway_ms`** (default `3.0`): how far past the Star Power end a backend note can land and still count in the score.
- **`sqout_rule`** (default `first_note`): what a squeeze-out costs. `first_note` removes the Star Power doubling from one note of the chord (the lowest-value one). `whole_chord` removes it from every note in the chord.
- **`max_tied_paths`** (default `4`): how many paths Hydra keeps when several reach the same score. More paths means longer lists and slower analysis.
- **`fill_cooldown_measures`** (default `4`): for charts with no authored fills, how many measures must pass after an activation point before Hydra places the next one.
- **`fill_max_distance_beats`** (default `0.5`): for charts with no authored fills, how far from a measure line a note can sit and still get a fill.
- **`fill_length_measures`** (default `0.5`): for charts with no authored fills, how long each fill Hydra places is, in measures.
- **`fill_land_slop_beats`** (default `0.03125`, a 32nd of a beat): how close a fill's end must be to a note for the fill to count. This one applies to authored fills too.

Older files may still have `auto_cap_ladder` or `auto_budget_s` lines. Hydra reads and ignores them, so those files keep working.

A value Hydra can't read, or a key it doesn't know, is an error that names the key. The command line tools print the error and stop with exit code 2. The app still opens and shows the error, but analysis stays off until you fix the file and restart Hydra. Hydra never analyzes on the defaults behind your back.

Every result remembers the rules it was made with. After you change the file, results made under the old rules show `Stale` until you re-analyze them. Switching the rules back brings those results back.

## Command line tools

Three console programs ship alongside the app and share its settings and library:

- **`hydra_batch`** runs the same batch analysis as `Analyze library...`, printing one line per chart. Flags: `--redo` (re-analyze existing results), `--reindex`, `--db <path>`, `--rules <path>`, `--legacy-fills`. `--legacy-fills` prices charts under Clone Hero 1.0's fill rule instead of 1.1's. It is a command-line-only mode, and it refuses to write the app's own database, so give it its own `--db`.
- **`hydra_report`** rebuilds the HTML path report from stored results. Flags: `--paths N`, `--all-paths`, `--out <path>`, `--no-open`, `--db <path>`, `--rules <path>`.
- **`hydra_fillcompare`** compares a Clone Hero 1.0 database with a 1.1 one and writes a sortable page of every chart where the fill rules disagree. Flags: `--old <path>`, `--new <path>`, `--out <path>`, `--no-open`, `--rules <path>`.

All three use the analysis settings from the app's settings file. All three read the scoring rules from `hydra_rules.ini` next to Hydra.exe, or from the file `--rules` names. If that file has an error, they print it and stop with exit code 2.
