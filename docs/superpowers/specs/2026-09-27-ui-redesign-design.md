# Interface redesign

Approved 2026-09-27. The drawings are in `2026-09-27-ui-redesign-mockup/` next to this file, and live at https://claude.ai/artifact/TRLProeKnWPrDmmD1dtacM. The plan that builds it is `docs/superpowers/plans/2026-09-27-ui-redesign.md`.

## What it is

Today, clicking a song opens a fixed window that covers the library. You can't move it, resize it or close it with Escape. Its settings band takes 40% of every tab, and closing it quietly cancels a running analysis. A batch locks the whole app behind another window for hours. The library pages instead of scrolling, can't sort, and its search misses accented names.

The redesign keeps everything Hydra shows, but rearranges it around one idea. The library stays on screen, and the song you're looking at sits beside it. The analysis settings move out of the song window and onto the main screen, because they apply to every song. Long work runs in the background, and details fold away until you ask for them.

It does not change any score, path or rule. A chart analyzed before the update gives the same result after it.

## The main screen

From top to bottom, the window has a toolbar, the analysis settings bar, and then the library with the song panel beside it.

**The toolbar** has the same five buttons as today, with clearer names: "Manage folders... (1)", "Scan library", "Analyze library...", "Compare with dmleaderboards..." and "Open path report". While a search is active, "Analyze library..." becomes "Analyze search (5)...", so you can analyze just what you found.

**The analysis settings bar** is titled "Analysis settings, for every song". It holds the six settings that decide a result: Difficulty, Pro Drums, 2x Bass, SP cap (in bars), Score range (in scores or points) and Path limit (in ms). The three with numbers have a "?" that explains them. Changing a setting changes which stored result every row shows, exactly as the same controls do inside the song window today.

While anything is analyzing, the bar is locked. It says "Stop the batch to change these." during a batch, or "Settings are locked while this song analyzes." for one song. Today, changing SP cap mid-analysis files the new result under the old cap and then hides it. The lock prevents that.

SP cap is a plain number, and it starts at 4, which is Clone Hero's rule. Auto is gone (see "Auto is removed").

**The library** fills the left side. Its heading says how many charts it holds, or "5 of 97 charts" while searching. Under the search box are four filter chips: "All", "Not analyzed", "Stale" and "Analyzed", each with its count. A chip with a count of 0 is greyed out.

The table has a frozen header and scrolls normally; there are no pages. Every column sorts when you click its header. With the song panel open, the table shows Title, Artist and Best path, and each title gets a second line with its folder. With the panel closed, it shows Title, Artist, Charter, Folder and Best path. The Best path cell reads "Not analyzed", "Stale", or the score and path, such as "378,315  3- 1 2".

You can drag the edge between the library and the panel. The width is remembered with the other window settings.

## The song panel

The panel opens when you click a song and closes with its "×" or Escape. The "‹" and "›" buttons step to the previous and next song in the library's current order, so you can walk through a search result without going back to the list.

**The headline** shows the song's title, then "Green Day · charted by Hoph2o". Under that, the optimal score and path are shown large and in gold: "378,315" and "3- 1 2". One line of facts follows: "Optimal path · 7 stars · hardest squeeze 163.0 ms". The button beside it reads "Analyze this song" when there's no result, and "Re-analyze" when there is one, current or stale.

Closing the panel no longer cancels an analysis. The run finishes, its result is stored, and its library row updates.

The panel has the same four tabs as today: Paths, Preview, Dynamics and Stars. Dynamics and Stars are unchanged apart from moving into the panel.

## The Paths tab

The left column lists the paths in three groups. "Optimal" has the best score and its path, and its hardest squeeze. "Within 2 scores" has the other paths inside the score range. "Best at 0 ms limit" has the best path that needs no squeeze, with how far below optimal it is ("2,360 below optimal"). Clicking one shows it on the right, and the Preview draws the same path.

The right column starts with a summary of the activations: "3 · 3 bars each · no SP left over". A small timeline under it runs from the first measure to the last ("m1" to "m96") and marks where each activation falls.

Then every activation gets one line: its number, its notation ("3-"), where it starts ("m32.1.0"), how many bars it uses, and a badge when it needs a squeeze ("squeeze out 163 ms"). One line is open at a time, and "Expand all" opens every one.

An open line shows the chord to activate on ("[Kick - GreenCym]"), a "Show in Preview ›" link, and the squeeze in a plain sentence: "Hit the [Y] note more than 163.0 ms late so it lands after Star Power ends. It scores 260 fewer points, and its SP phrase banks for later." The backend timings fold under "Backend timings", with a plain sentence above the table. The "Hide backend rows beyond N ms" control moves here from the settings band, because it changes only what this tab shows.

Below the activations, "Multiplier squeeze" and "Score breakdown" fold away until opened. "Copy path" copies the path and flashes "Copied!". Ctrl+C does the same.

The timeline needs the song's length, which Hydra has never stored. It is stored from now on, so a song analyzed before the update shows no timeline until it's analyzed again. Nothing else about that song's result changes.

## The Preview tab

"Showing" names the path the Preview draws, with a list to pick another. The list is shared with the Paths tab, so picking a path in either one picks it in both. The all-0 path is labelled "0 0 0 0 (0 ms limit)".

"‹ Act" and "Act ›" jump the playhead to the previous or next activation, and so do the "[" and "]" keys. Gold marks on the scrubber show every activation of the shown path, and the clock sits beside the scrubber. "Show in Preview ›" on the Paths tab switches to this tab and lands on that activation.

A new box names the next activation: "Next: activation 1 of 3", then "at m32.1.0 · [Kick - GreenCym]". The SP meter gets an "SP" label and a "2/4" readout. A hint line along the bottom lists the keys.

The transport buttons keep today's labels ("-5s", "< 5 Ticks", "5 Ticks >", "+5s"), and the drain box keeps today's wording. The GUI tests find buttons by their labels, and the mockup's small rewordings weren't worth breaking that.

## Search

Search looks at title, artist, charter and folder together, and the words can be in any order. It ignores case and accents properly, so "beyonce" finds "Beyoncé". It sees through the colour tags some charters put in their names, so "<color=#e02222>" never shows and never stops a match.

Quotes match an exact phrase: `"tier 4"`. Five filters narrow the list: `artist:`, `charter:`, `folder:`, `stars:7` (exactly 7 stars) and `squeeze<=20` (the best path's hardest squeeze is at most 20 ms). The star and squeeze filters look only at current results. A stale row's old numbers never count.

Every matching row highlights what matched. When the match was in a column that isn't showing, the row says so ("Matched on folder."). A search with no matches offers "Clear search".

The hint under the box reads "Quotes match an exact phrase. Narrow with artist: charter: folder: stars:7 squeeze<=20".

## Batch analysis

"Analyze library..." first asks once. The dialog, titled "Analyze library", says what it will do: "Analyze 96 charts that have no result yet?" It then lists every setting it will use, for example "Expert · Pro Drums · 2x Bass", "SP cap 4 bars (Clone Hero's rule)", "Score range 2 scores" and "Path limit 10 ms". It says those settings are changed on the main screen, not here.

A checkbox offers "Also re-analyze the 1 chart that already has a result". A closing line explains that it runs in the background and that Stop keeps every finished result. The buttons are "Cancel" and "Start analyzing". Escape cancels and Enter starts.

While it runs, a strip across the top of the window shows the progress and the count ("22 of 96"). It also shows the current song ("Now: Chair · Sufferer"), the time elapsed and about how long is left. Under those are the tallies: "21 analyzed · 0 failed · 1 skipped (already had a result)". The estimate appears once three charts have finished. "Pause" becomes "Resume" while paused. "Stop" ends the run and keeps everything it finished; it's called Stop, not Cancel, because nothing is thrown away. The library stays usable underneath, and the settings bar is locked.

When it ends, the strip reports the result: "Finished: 96 analyzed · 0 failed · 1 skipped (already had a result) · took [time]". It then says "Path report saved to Documents\Hydra\hydra_paths.html", with "Open report", "Show in folder" and "×". If Windows couldn't open the browser, the strip says the report was saved anyway and shows where. A stopped batch builds no report, as today, and "Open path report" still opens the last full one.

## Reports

Reports save to Documents\Hydra instead of the program folder, and every place that mentions a report shows its path. If Documents can't be found, they fall back to the folder next to `hydra.db`. An old report left next to `hydra.db` stays where it is; the next batch writes a new one.

The report pages are fixed:
- They render in standards mode.
- Their column headers stay on screen while you scroll.
- Every column explains itself on hover and in a legend at the foot of the page.
- They print cleanly.
- They pass the WCAG contrast checks.

The subtitle uses the singular for one ("1 record across 1 chart").

The leaderboard comparison stops calling unanalyzed charts "not in your library". It shows four counts: matched, above Hydra's optimal, not analyzed yet, and not in your library. Cancelling the fetch is not shown as an error. A report that saved fine is never called "failed" just because the browser didn't open. Nothing in the repo defines the comparison's "Percent" column, so its note says only "The percent the leaderboard lists for this score."

## Messages and the keyboard

The status line has two kinds of message. News, such as a finished scan, is neutral and fades after a few seconds. A problem is orange and stays until you dismiss it with its "×".

Error messages say what happened and what to do, in plain words. The raw text ("add_song failed: ...") moves to a small detail line underneath.

Escape closes every dialog and the song panel. Enter presses a dialog's default button. Ctrl+F focuses the search whenever no dialog is open.

White text on the teal buttons and the disabled inputs are raised to readable contrast. The Stale tooltip names both of its causes: a result from another Hydra version or path layout, or a changed rules file.

## The window

Hydra reopens where you left it, at the same size. If that spot is no longer on any screen, for example because a monitor was unplugged, it opens at the default place instead. Moving the window to a monitor with a different Windows scale rescales the text and layout, so nothing turns blurry or tiny.

## Auto is removed

SP cap no longer has an Auto choice. The default is 4 bars.

Results saved under Auto are deleted the first time the new version opens a database, and only that once. Paths left with no result are cleaned up with them. The delete recognises Auto results by the rules fingerprint Auto used. An Auto result made under a hand-edited ladder has a different fingerprint and is missed. It already reads Stale, and the chart's next analysis replaces it. The installed database had no Auto results when this was planned.

A settings file that says `sp_cap=auto` reads as 4. A `hydra_rules.ini` that still has `auto_cap_ladder` or `auto_budget_s` keeps working; those two keys are ignored.

## Where every number comes from

Nothing on these screens is worked out twice.
- The score, path and activations come from the stored record.
- The star count is worked out once, when a result is saved, by `core/stars`. The panel's "7 stars" and the `stars:` filter both read that stored number.
- Measure positions ("m32.1.0") come from one shared formatter, used by both tabs.
- Whether a row is Not analyzed, Stale or Ready comes from the store's winner rule, never from SQL or draw code.
- The squeeze sentences are built from the same stored values the old "SqOut" lines used.

## What changes in stored data

Three things, none of which needs a re-analysis:

1. Each result gains a star count. Existing current results get theirs filled in the first time the new version opens the database. Only that column is written, and only on current results, so a broken rules file can't wipe any stored score.
2. Each song gains its length, stored from the next analysis on.
3. Auto results are deleted once, as described above.

The stored path format and the results stamp don't change.

## Which task builds what

The plan builds this in four waves:
- Wave 1 is groundwork with no visible change: T1 splits the two big UI files and the GUI test file, T2 is the search matching, T3 the reports, T4 the batch job and plain error text, and T5 the window placement.
- Wave 2 is the data: T6 removes Auto, T7 stores the star counts and song lengths, and T8 builds the Paths and Preview view data.
- Wave 3 is the screens: T9 the layout, settings bar and song panel, T10 the Paths tab, T11 the Preview tab, T12 the library table and search, and T13 the batch strip, dialogs and status line.
- Wave 4 is T14, the user guide and developer docs.

Task 15 is the final check, which ends with you trying the new Hydra.exe.

Owned-file check: every part of this design is built by a task whose file list in the plan's "Who owns which files" contains the file that part lives in. The planning agents confirmed each owned file exists at 031d218, or is created by its task. The few cross-task edits are named there as expected overlaps, or in the plan's merge checklist.

## How it is tested

Logic is tested in `hydra_tests` first: search matching, the star count, the stored summaries and length, the Auto delete, the view data, the error text and the window placement math. Every screen is checked headlessly with `hydra_uitest`. Tests find widgets by the exact labels in the plan's label contract. A before-and-after `hydra_batch` run on `testdata\input` must show no score change. At the end you run the new Hydra.exe and say whether it matches the mockup. Two checks are done only by you, because the harness can't reach them: the window reopening where you left it, and the rescale when you drag it to another monitor.

## Preflight

Command: `.\build-cpp\Release\hydra_tests.exe --count`, run in `C:\Users\Patrick\Downloads\Hydra\hydra-test` at 031d218 on 2026-09-27.
Output: `[doctest] unskipped test cases passing the current filters: 471`. The unit suite builds and runs at the base this design starts from, and the song details are still the fixed modal it replaces (`details_view.cpp:1225`, `BeginPopupModal(title, &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)`).
