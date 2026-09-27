# Interface Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild Hydra's interface to the design approved on 2026-09-27: the song details become a panel docked beside the library, every analysis setting moves to one bar on the main screen, batch analysis runs in the background, the library gets sorting, status filters, a scrolling list and a smarter search, Auto is removed, and every bug from the interface audit is fixed.

**Architecture:** The work is split into fourteen fleet tasks plus two the main session does itself. Tasks that share no files run at the same time, each in its own git worktree (a second checkout of the repo in its own folder, on its own branch). The main session merges each wave into `hydra-test`, builds and tests the merged result, and starts the next wave from it. Four waves cover everything. The first wave splits the two big UI files and the GUI test file into smaller ones, so the later UI tasks each own their own files instead of all editing the same two.

**Tech Stack:** C++20, Dear ImGui 1.93 (docking branch, vendored), SQLite, doctest (`hydra_tests`), Dear ImGui Test Engine (`hydra_uitest`), CMake via `build_cpp.ps1`.

**Spec:** `docs/superpowers/specs/2026-09-27-ui-redesign-design.md` describes the whole design in prose: every screen, where each number comes from, and what changes in stored data. Read it before your task section. The drawings behind it are the approved mockup at https://claude.ai/artifact/TRLProeKnWPrDmmD1dtacM (five screens: Main, Preview, Batch, Confirm, Done). Its source files are copied into this repo at `docs/superpowers/specs/2026-09-27-ui-redesign-mockup/` by Task 0, so executors can read the exact labels and layout. The audit findings behind it are summarized under "What the audit found" below.

## How to read this plan

Each task opens with a plain paragraph: what is wrong or missing today, what changes, and what you will see. Then come its wave, the tasks it waits for, the files it owns, the overlaps the merger should expect, the goal, the acceptance checks, one verify command, and the steps. Steps are test-first where there is logic to test: write the failing test, watch it fail, make the change, watch it pass, commit.

Task 0 and Task 15 are the main session's. Task 0 commits the plan and records the "before" numbers. Task 15 checks the finished whole and hands you the new Hydra.exe to look at.

## What you'll see when it's done

The library fills the left of the window and the song you click opens in a panel on the right. You can drag the edge between them. The panel has previous and next song buttons, and Escape closes it.

One "Analysis settings" bar under the toolbar holds Difficulty, Pro Drums, 2x Bass, SP cap, Score range and Path limit. SP cap is a plain number that starts at 4. Auto is gone, and results saved under Auto are deleted the first time the new version starts.

The panel's top shows the optimal score and path large and in gold, with one line of facts under it. The Paths tab lists the paths on the left and every activation on the right, one line each, with a small timeline above them. Clicking an activation opens its details. Backend timings, the multiplier squeeze and the score breakdown fold away until you open them.

The Preview tab names the path it draws, lets you pick another, marks every activation on the scrubber, and jumps between activations with buttons or the `[` and `]` keys. The SP meter has a label.

The library has sortable columns, filter chips for Not analyzed, Stale and Analyzed, and a normal scroll bar instead of pages. Search matches words in any order across title, artist, charter and folder, ignores case and accents properly, finds charters whose names carry colour tags, understands quotes and a few filters (`artist:`, `charter:`, `folder:`, `stars:7`, `squeeze<=20`), and highlights why each row matched.

"Analyze library..." asks once, listing every setting it will use, then runs in a strip across the top while you keep browsing. The strip shows the count, the current song, elapsed time and time left, with Pause and Stop. The settings bar is locked while anything is analyzing. When the batch ends, the strip says where the report was saved, in Documents\Hydra, with Open report and Show in folder.

The report pages render in standards mode, keep their column headers on screen while you scroll, explain every column, print properly and pass contrast checks. Error messages say what happened and what to do.

## The waves

**Task 0 (main session):** commit the plan, handoff and mockup; record the baselines.

**Wave 1, five tasks at once:** T1 split the UI and GUI-test files, T2 search query module, T3 report pages and report location, T4 batch job and job messages, T5 window placement and DPI.

**Wave 2, three tasks at once:** T6 remove Auto, T7 library summaries in the store, T8 path and Preview view data.

**Wave 3, five tasks at once:** T9 layout, settings bar and song panel, T10 Paths tab, T11 Preview tab, T12 library table and search, T13 background batch and dialogs.

**Wave 4, one task:** T14 user guide and developer docs.

**Task 15 (main session):** the final check and your look at the new Hydra.exe.

A task waits for a later wave only when it builds on an earlier task's code. Wave 2 waits for T1 because T6 edits lines T1 moves. Wave 3 waits for wave 2 because every UI task reads `Settings::sp_cap` as a plain `int` (T6), the library table reads the new summaries (T7), and the two tabs draw T8's view data. T14 waits for the final labels.

## What the audit found

The audit on 2026-09-27 used four agents: three read the library screen, the song details and Preview, and the reports and wording, driving the real UI with `hydra_uitest`; one researched the published standards. The findings that became tasks:

1. Changing SP cap during an analysis hides the result it just produced, because the result is filed under the old cap and re-read under the new one. (T9 locks the settings bar while anything analyzes.)
2. Closing the details window silently cancels a run. (T9: closing the panel no longer cancels; the run finishes and stores.)
3. The details window is a fixed modal: no move, no resize, no Escape, no next/previous. Its settings band eats 40% of every tab, and the path list's fixed 600 px width clips the "squeezed out" warning. (T9, T10.)
4. Engine jargon shows raw ("SqOut: Note timing must be later than 163.0ms."), and the same numbers are written in different formats across tabs (m32.1.0 vs [27:2:450]; 4.807x vs x4). (T8, T10, T11.)
5. The Preview doesn't say which path it draws and can't jump to an activation. The SP gauge has no label. (T8, T11.)
6. The library has no sorting, no status filter, pages instead of scrolling, rows of `-----` in empty slots, and shows raw `<color=#e02222>` tags in the Charter column. (T12.)
7. A batch locks the whole app behind a modal for hours with no elapsed time or ETA. "Cancel" keeps finished results, which Windows calls Stop. (T4, T13.)
8. The leaderboard comparison calls unanalyzed charts "not in your library". "Open report again" shows for a report that never opened. Cancelling the fetch looks like an error. A report saved fine is called "failed" when only the browser failed to open. (T3, T4, T13.)
9. Report pages have no doctype (quirks mode), their sticky header doesn't stick, columns like "Cal fill" and "SqIn" are unexplained, some chips fall under WCAG contrast, and reports are overwritten in the program folder with no path shown. (T3.)
10. Escape closes no dialog. White on the teal buttons is about 3.5:1. Disabled inputs are about 1.1:1. The status line is orange for everything and fades after 6 s. (T9, T13.)
11. The Stale tooltip names only one of its two causes. Raw exception text reaches users ("add_song failed: ..."). (T4, T9, T12.)
12. The window always opens at (100,100) 1280x720, and moving it to a monitor with different scaling keeps the old sizes. (T5.)

## What others do

Nielsen Norman Group recommends a side panel, not a modal, for viewing one record from a table, because a content-heavy modal hides the context the user needs ([NN/g: modal and nonmodal dialogs](https://www.nngroup.com/articles/modal-nonmodal-dialog/), [NN/g: data tables](https://www.nngroup.com/articles/data-tables/)). The same data-table article asks for sortable columns, a frozen header, visible active filters and cheap column hiding, which T12 builds with Dear ImGui's own table flags (`ImGuiTableFlags_Sortable`, `Hideable`, `TableSetupScrollFreeze`; [imgui.h](https://github.com/ocornut/imgui/blob/master/imgui.h)) and `ImGuiListClipper`, which draws only the visible rows of a long list.

For anything over 10 seconds, NN/g asks for a percent-done indicator, a time estimate and a way to stop, and says a tracker the user can glance at beats a blocking dialog when they can keep working ([NN/g: progress indicators](https://www.nngroup.com/articles/progress-indicators/), [NN/g: status trackers](https://www.nngroup.com/articles/status-tracker-progress-update/)). Microsoft's progress-bar guideline says a button that keeps finished work is "Stop", not "Cancel", and a long job the user just wants finished should announce completion rather than hold focus ([Microsoft: progress bars](https://learn.microsoft.com/en-us/windows/win32/uxguide/progress-bars)). T4 and T13 follow both.

Microsoft's dialog and keyboard guidelines say Esc cancels and Enter presses the default button, and the title-bar close must act like Cancel ([dialog boxes](https://learn.microsoft.com/en-us/windows/win32/uxguide/win-dialog-box), [keyboard](https://learn.microsoft.com/en-us/windows/win32/uxguide/inter-keyboard)). Its error-message guideline asks for the problem, the cause and the fix in plain words, with codes behind details ([error messages](https://learn.microsoft.com/en-us/windows/win32/uxguide/mess-error)). T13 and T4 follow them.

WCAG 2.2 asks for 4.5:1 text contrast, 3:1 for control boundaries, and never colour alone ([WCAG 2.2](https://www.w3.org/TR/WCAG22/)). T3 and T9 fix the colours that miss it.

SQLite's `LIKE` ignores case only for ASCII letters ([sqlite.org: LIKE](https://www.sqlite.org/lang_expr.html#like)), which is why today's search can't match "Beyoncé" from "beyonce". T2 and T12 move matching into C++ with its own folding.

CHOpt users asked for the settings to be printed on every output and for an explanation when a result looks wrong ([CHOpt #3](https://github.com/GenericMadScientist/CHOpt/issues/3), [CHOpt #29](https://github.com/GenericMadScientist/CHOpt/issues/29)). The confirm dialog listing every setting (T13) and the plain squeeze sentences (T8) come from the same idea.

## Global Constraints

Code blocks quote the code as it is on disk when the plan was written. When an earlier wave already changed those lines, apply the same change to the new lines and keep what the earlier task added. Line numbers are hints; find code by its function name.

Every task works in its own worktree, made by the workflow before the agent starts: `git worktree add C:\Users\Patrick\Downloads\Hydra\wt-ui-T<n> -b ui/T<n> <base>`. `<base>` is the Task 0 commit for wave 1 and the merged `hydra-test` head for later waves. The agent commits only inside its worktree and never touches `hydra-test`. Only the main session merges.

Each worktree builds into its own `build-cpp` folder, so its first build is a full build. At most four builds run at the same time (see "Running it as a workflow").

A task edits only the files it lists, plus new files it creates. It never deletes, moves or rewrites anything else, and never touches the user's data folders or the installed Hydra under Program Files. The reviewer checks this with `git diff --stat <base>..ui/T<n>`; any file outside the list fails the review. When a task has to delete most of a function, it writes the new file to `<file>.new` with the Write tool and moves it over with `Move-Item -Force`, because a hook denies an Edit that keeps under 40% of its block.

Every commit message ends with four lines: `Task: <task name>`, `Agent: <executor>`, `Session: <session>`, and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never amend, rebase or reset. Source edits use the Edit tool, never patch scripts.

Build from the worktree root. The Bash tool runs it as `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target <target> > build.log 2>&1` (running `build_cpp.ps1` from the PowerShell tool fails on CMake's deprecation warning on stderr). Unit tests: `.\build-cpp\Release\hydra_tests.exe`, optionally `-tc="<name>"`. GUI tests: build `hydra_uitest`, then `.\build-cpp\Release\hydra_uitest.exe --test <name>` or `--all`. Every GUI check goes through `hydra_uitest` (docs/agents/ui-testing.md), never screenshots of Hydra.exe. `hydra_batch.exe` with no arguments analyzes the whole library next to the exe; never run it without `--db` and a folder.

**Score-neutral proof.** T6, T7 and T8 touch the engine, the store or the path views, so each proves no score changed. It compares the sorted per-chart lines of `hydra_batch` on `testdata\input` against the Task 0 baseline:

```powershell
$out = "$env:TEMP\hydra_ui_T<n>"; New-Item -ItemType Directory -Force $out | Out-Null
Remove-Item "$out\after.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_batch.exe --db "$out\after.db" testdata\input |
  Select-String '^\[\d+/\d+\] ' | ForEach-Object { $_.Line -replace '^\[\d+/\d+\] ', '' } |
  Sort-Object | Set-Content "$out\batch_sorted.txt"
Compare-Object (Get-Content "$env:TEMP\hydra_ui_base\batch_sorted.txt") (Get-Content "$out\batch_sorted.txt")
```

It must print nothing. Keep `$env:TEMP\hydra_ui_base\` until Task 15 is done.

**No user-visible change beyond the approved design and the decisions below.** If a task finds that a fix would change a number, label or wording that the mockup, this plan's label contract or the decisions don't cover, it stops and reports instead of choosing.

**Every rule is derived once.** A value the UI shows comes from the engine, the record or one shared formatter, never a second calculation in draw code (see CONTEXT.md and the memory rule "derive display from engine truth"). Star counts come from `core/stars`. Measure positions come from one formatter (T8's `format_measure`). Record status comes from the store.

**State lives on AppState, not in draw code.** Starting, reaping and storing jobs happens in `AppState` methods or a per-frame `AppState::tick()`, never inside a tab's render function. Explicit pixel sizes go through `px()`. Disabled widgets use the `begin_disabled_*` helpers in theme.h.

## Fixed interfaces between tasks

These are written here so tasks in the same wave can code against each other. A task that provides one implements it exactly; a task that uses one assumes exactly this.

**T2 provides `src/app/library_query.h`:**

```cpp
namespace hydra::app {

// Lowercase, accents removed (NFD-style fold for Latin-1 and Latin Extended-A:
// "é" -> "e", "ß" -> "ss"), full-width ASCII folded to ASCII, runs of
// whitespace collapsed to one space. Non-Latin text (Japanese, etc.) passes
// through unchanged apart from full-width folding. Input and output are UTF-8.
std::string fold_for_search(std::string_view text);

// Removes Clone Hero rich-text tags: <color=...>, </color>, <b>, </b>, <i>,
// </i>, <size=...>, </size>, <u>, </u>, <s>, </s>, <sub>, </sub>, <sup>,
// </sup>, case-insensitive. Anything else in angle brackets is kept.
std::string strip_rich_tags(std::string_view text);

enum class QueryField { Any, Title, Artist, Charter, Folder };

struct QueryTerm {
    QueryField field = QueryField::Any;
    std::string folded;   // fold_for_search of the word or quoted phrase
    bool phrase = false;  // true when it came from "quotes"
};

struct LibraryQuery {
    std::vector<QueryTerm> terms;         // every term must match
    std::optional<int> stars;             // stars:N, N in 0..7
    std::optional<double> squeeze_max_ms; // squeeze<=N (also squeeze<N treated as <=)
    std::vector<std::string> errors;      // e.g. "stars: needs a number from 0 to 7"
    bool empty() const;                   // no terms and no filters
};

// Parses what the user typed. Words match in any order. "quoted text" is one
// phrase. artist:x, charter:x, folder:x, title:x limit a word or "phrase" to
// one field. stars:N and squeeze<=N filter on the stored best path. Unknown
// field names are treated as plain words.
LibraryQuery parse_library_query(std::string_view text);

// The fields one library row offers to a query, already folded.
struct SearchableRow {
    std::string title, artist, charter, folder;  // fold_for_search(strip_rich_tags(x))
};
SearchableRow make_searchable(std::string_view title, std::string_view artist,
                              std::string_view charter, std::string_view folder);

// The best path's stored facts a filter can test; nullopt when not analyzed.
struct RowFacts {
    std::optional<int> stars;
    std::optional<double> hardest_ms;  // nullopt = no squeeze on the path
};

// True when every term matches its field (or any field) and every filter holds.
// A stars: or squeeze filter never matches a row with no facts.
bool query_matches(const LibraryQuery& q, const SearchableRow& row, const RowFacts& facts);

// Where the query's terms appear in one displayed (unfolded, tag-stripped)
// string, as byte ranges, for highlighting. Ranges are sorted and don't overlap.
struct MatchSpan { size_t begin = 0, end = 0; };
std::vector<MatchSpan> match_spans(const LibraryQuery& q, QueryField field,
                                   std::string_view display_text);

}  // namespace hydra::app
```

**T3 provides, in `src/app/report_files.h`:** `std::filesystem::path reports_dir();` returns `Documents\Hydra` (from `SHGetKnownFolderPath(FOLDERID_Documents)`), creating it if missing, and falls back to the database's folder when Documents can't be found or created. Every report path helper uses it. **In `src/app/dm_report.h`:** the comparison result counts `matched`, `above_optimal`, `not_analyzed` (in the library, no current result) and `not_in_library`.

**T4 provides, in `src/ui/library_jobs.h`:** `BatchJob::pause()`, `BatchJob::resume()`, `BatchJob::stop()` (finished results are kept; `cancel()` remains as an alias until T13 removes its callers), and a snapshot with `bool paused`, `double elapsed_s`, `std::optional<double> eta_s` (nullopt until 3 charts finish), `std::string current_title`, `std::string current_artist`, plus the existing counts. A finished report job answers three methods: `saved_path()`, `opened()`, and `open_problem()`. `open_problem()` is empty when the browser opened or auto-open is off. Otherwise it is the short sentence "Windows couldn't open the report in your browser.", and the Done strip builds its full line around it. A failed open is never a job failure. Every result job also gains `message()`, a plain sentence that is empty when the job was cancelled. Views show `message()`, with `error()` as a small detail line under it. `BatchJob` gains a second constructor that takes the exact list of charts to analyze. "Analyze search (N)..." passes it the rows the in-memory search shows, because SQL `LIKE` and T2's matching pick different rows. **In `src/app/user_messages.h`:** `std::string plain_error(const std::exception& e);` maps known internal errors to one plain sentence that says what to do. `std::string plain_error_detail(const std::exception& e);` returns the raw text for a small detail line. `std::string plain_error_text(std::string_view raw);` does the same mapping for errors that arrive as text only.

**T6 provides:** `app::Settings::sp_cap` is a plain `int`, default `kCloneHeroSpCap` (4), clamped to at least 1. `store::CapQuery` keeps its name with `int exact` and `static CapQuery at(int)`; `automatic()`, `from_setting()` and `is_auto()` are gone. `DetailsViewState::last_cap` is gone.

**T7 provides, in `src/store/record_store.h`:** `PathSummary` gains `std::optional<int> stars`. This is the best path's star count, with the solo bonus left out the way Clone Hero counts it. `SummaryLookup` gains `PathSummary summary` (filled when Ready). `get_summaries` fills both. **In `src/core/stars.h`:** `int path_stars(const Path& path)` is the one function that turns a path into a star count. The store's column uses it, and T9's "7 stars" headline reads that stored count through `get_summary`, so the count is worked out once, at save time. `RecordLookup` gains `std::optional<double> song_length_ms`, the last note's onset, stored in a new `songmeta.length_ms` column. It is empty for songs saved before this update until they are analyzed again. T10's timeline reads it from `app.viewed.song_length_ms` and hides itself when it is empty.

**T12 provides, on `AppState` (replacing `LibraryPage`, `current_page`, `table_viewpage`, `rows_per_page`, `set_rows_per_page` and `refresh_page`):**

- `LibraryModel library` (new `src/ui/library_model.{h,cpp}`);
- `std::string search` and `set_search(std::string)`;
- `int64_t library_total`;
- `reload_library()`, `refresh_library_summaries()`, `refresh_library_row(const std::string& md5)` and `tick_library(double now)`;
- `const std::vector<size_t>& library_view_order() const`, `size_t library_shown_count() const` and `const LibraryRow& library_row_at(size_t view_index) const`;
- `size_t library_match_count() const`, which is the N of "Analyze search (N)...";
- `std::vector<store::ChartLibraryEntry> library_matches() const`, which is what that button hands to `BatchJob`'s chart-list constructor.

The main window calls `detail::render_library(app)` every frame inside `##library`. It draws the heading, the search box, the chips and the table, and draws nothing when the library is empty.

**T8 provides, in `src/app/path_view.h`:** `std::string format_measure(const SongTiming&, int64_t tick)` returning `m<measure>.<beat>.<tick-in-beat>` (the Paths tab's existing form, e.g. `m32.1.0`), used by both tabs; an `ActivationRowView` per activation (number, notation such as `3-`, measure, SP bars, optional badge text such as `squeeze out 163 ms`, chord text, the plain squeeze sentences, the backend rows, and the song fraction 0..1 for the timeline); and in `src/app/preview_view.h`, the activation marks for the scrubber as fractions of the song's length in seconds, the shown path's label, and the SP meter readout string. **In `src/app/display_format.h`:** `std::string format_ms_spaced(double ms)` returns `163.0 ms` (with the space the mockup uses). T9's headline uses it too. **In `PathsTabCache::ui()`:** `std::optional<size_t> preview_jump`, the activation "Show in Preview" asked for; T11 consumes it.

**T10 provides, in `src/ui/details_parts.h`:** `void copy_selected_path(AppState& app)` copies the selected path and starts the "Copied!" flash. T9's Ctrl+C handler calls it.

**T13 provides, on `AppState`:** `void set_problem(std::string message)`, an orange status line that stays until dismissed with `X##dismissstatus`, next to the existing `set_status` (neutral, fades). **In `library_parts.h`:** `const char* empty_library_message(const app::Settings&)`, the "no songs yet" text the main window shows when the library is empty.

**T9 provides, on `AppState`:** `bool settings_locked() const` (true while an analyze or batch job runs); `void select_relative(int delta)` (next/previous row of the current library view, wrapping never); `bool details_open() const` (the panel is showing; replaces reading `show_details` directly outside the panel). T12 provides the ordered view T9's `select_relative` walks: `const std::vector<size_t>& AppState::library_view_order() const`.

## Label contract

GUI tests find widgets by label, so every task writes its tests against these exact labels and IDs. The `...` is three ASCII dots, as Hydra already writes it.

- Toolbar: `Manage folders... (N)`, `Scan library`, `Analyze library...` or `Analyze search (N)...` while searching, `Compare with dmleaderboards...`, `Open path report`.
- Settings bar child `##settingsbar`: combo `##difficulty`, checkboxes `Pro Drums` and `2x Bass`, inputs `##spcap`, `##depthvalue`, combo `##depthmode`, checkbox `Path limit##mslimit`, input `##mslimitvalue`. While locked it shows `Stop the batch to change these.` (batch) or `Settings are locked while this song analyzes.` (single song).
- Library: child `##library`, search input `##search`, clear button `X##clearsearch`, chips `All (N)##chipall`, `Not analyzed (N)##chipnew`, `Stale (N)##chipstale`, `Analyzed (N)##chipdone`, table `##librarytable` with columns `Title`, `Artist`, `Charter`, `Folder`, `Best path`. A row's Best path cell reads `Not analyzed`, `Stale`, or `<score>  <path>`.
- Song panel child `##songpanel`: `<##prevsong`, `>##nextsong`, `X##closepanel` (Escape does the same), analyze button `Analyze this song` (not analyzed) or `Re-analyze` (analyzed or stale), tab bar `##DetailsTabs` with tabs `Paths`, `Preview`, `Dynamics`, `Stars`.
- Paths tab: path buttons `##path<i>`, `Expand all` / `Collapse all`, activation rows `##act<i>` (1-based), links `Show in Preview >##showact<i>`, fold buttons `Backend timings##act<i>`, `Multiplier squeeze##mult`, `Score breakdown##breakdown`, `Copy path`, checkbox `Hide backend rows beyond##backendlimit`, input `##backendlimitvalue`.
- Status line: dismiss button `X##dismissstatus` on a problem.
- Preview tab: text `Showing` beside the combo `##previewpath`, `< Act##prevact`, `Act >##nextact`, existing transport labels unchanged.
- Batch: confirm popup `Analyze library` with checkbox `Also re-analyze charts that already have a result##redo`, buttons `Start analyzing` and `Cancel`. Strip child `##batchstrip` with `Pause` / `Resume` and `Stop`. Finished strip `##batchdone` with `Open report`, `Show in folder`, `X##dismissdone`.

**User decisions (already made, 2026-09-27):**

1. "i like your idea" — the song details become a panel docked beside the library, drawn first as a mockup ("create a mock design before changing anything").
2. Batch analysis runs in the background with a progress strip ("yes").
3. All analysis parameters move to the main screen ("wouldnt it make sense to move all the parameters to the screen before analysis?", then "this all looks good now").
4. Reports save to Documents\Hydra instead of the program folder ("yes").
5. Screen 1 was "very cramped and dense"; the less dense revision with the activation list and the new search was approved ("this all looks good now"). Screens 2 to 5 were approved as drawn ("looks good").
6. "just drop auto completely, it's unnecessary and confusing. leave the default as 4."
7. Results already saved under Auto are deleted on the first start after the update ("Delete them (Recommended)").
8. Go ahead with the activation list, the search upgrades and the rest of the recommendations ("go ahead with the other recommendations").
9. The plan runs as a workflow: agents in parallel, their changes merged by the main session ("design the plan around a workflow, agents running in parallel with their changes merged by the main session").
10. `stars:N` means exactly N stars ("why would stars 6 not mean 6 stars?").
11. An old path report next to `hydra.db` is left where it is; the next batch writes a new one in Documents\Hydra ("we'll be generating a new path report anyway so it doesnt matter").
12. The calls listed below were accepted ("the rest sounds good").

**Calls I made myself (accepted 2026-09-27):**

- `hydra_rules.ini` files that still contain `auto_cap_ladder` or `auto_budget_s` keep working: the keys are read and ignored, not an error. Otherwise every user with an edited rules file would find analysis switched off.
- An INI with `sp_cap=auto` reads back as 4.
- The side-by-side split uses a child window with a draggable right edge (`ImGuiChildFlags_ResizeX`), not Dear ImGui's docking system. It is simpler, saves its width in `hydra_ui.ini` like the table columns do, and needs no dock layout code.
- Closing the panel no longer cancels a running single-song analysis. It finishes, is stored, and the library row updates. This is a change from today, and it's what the audit's finding 2 asked for.
- Filter-chip counts and the stars/squeeze filters work in memory over the whole library (up to about 20,000 rows), not in SQL, because the Stale/Ready decision lives in C++ (the store's winner rule) and must not be written a second time in SQL.
- The stored star count is added as a summary column. Existing results get it from a new `fill_missing_stars()` pass the first time the new version opens a database, so nobody re-analyzes for it. It writes only `stars`, and only on current (Ready) rows. The store's `reindex()` would have been the obvious tool, but it clears the summaries of every row that isn't current. Under a broken `hydra_rules.ini` every row reads Stale, so one bad start would have wiped every stored score.
- Filters like `stars:` and `squeeze<=` only look at current results. A Stale row's old numbers never count.
- The Paths timeline needs each song's length, which nothing stored today. T7 stores it from now on. Results saved before the update show no timeline until that song is analyzed again; nothing else about them changes.
- The Preview's path list labels the all-0 path `0 0 0 0  (0 ms limit)`, not the mockup's `(no squeezes)`. A 0 ms-limit path can still carry a 0 ms squeeze, so "no squeezes" could be false.
- Lines the mockup didn't reword keep today's wording: the frontend timing scale line, the calibration and overfill lines, the backend timing rows, and the SP drain box's "(if activated)". The backend table gets one plain lead-in sentence above it. The Preview's transport buttons keep today's labels (`-5s`, `< 5 Ticks`, `5 Ticks >`, `+5s`) rather than the mockup's `−5 s` and `‹ 5 ticks`, and the drain box keeps "1 bar / 2.5 s" rather than "1 bar per 2.5 s". Each is a two-string change if you want the mockup's words.
- The library's second line shows the folder as it is stored (`common\Summer Blast _25 Setlist\Tier 4`), not the mockup's `Setlist › Tier 4`, so the search highlight lines up with what you typed. A filter chip whose count is 0 is greyed out.
- A stopped batch still builds no report, as today. Everything it finished is kept, and "Open path report" still opens the last full report.

---

## Who owns which files

Each task below owns the files listed for it. "Expected overlaps" name another task that edits the same file in the same or an earlier wave, and which parts each side keeps, so the merger knows a conflict there is planned.

**T1 — Split the UI and GUI-test files (wave 1).** Pure moves, no behaviour change. `src/ui/details_view.cpp` becomes `details_panel.cpp` (song info, record status, controls, analyze progress and record-state helpers, the window itself), `paths_tab.cpp`, `preview_tab.cpp`, `dynamics_tab.cpp`, `stars_tab.cpp`, with shared declarations in a new internal header `details_parts.h`; `details_view.h` stays as the public header. `src/ui/library_view.cpp` keeps `render_main_window` and becomes four files: `library_view.cpp`, `library_toolbar.cpp` (status line, actions row, view controls), `library_table.cpp` (search box, summary label, table), `library_dialogs.cpp` (folder manager, scan, batch and dm-picker modals), with `library_parts.h`. `tests/ui/uitest_tests.cpp` is split by area into `uitest_library.cpp`, `uitest_details.cpp`, `uitest_preview.cpp`, `uitest_batch_reports.cpp`; each has its own entry table, and `uitest_tests.cpp` keeps only `register_tests`, which walks all four. Also owns `tests/ui/uitest_harness.{h,cpp}`, where the shared helpers (`scan_library`, `open_details`, `open_preview`) and the test-table type move so every area file can call them, and `CMakeLists.txt` (the new source lists). `register_tests` keeps today's run order with a fixed list of names, because the ImGui context carries tab and input state from one test to the next. A later task that adds a test appends its name to that list. Expected overlaps: T2 adds lines to `CMakeLists.txt`.

**T2 — Search query module (wave 1).** New `src/app/library_query.{h,cpp}`, new `tests/test_library_query.cpp`, and their lines in `CMakeLists.txt`. Expected overlaps: T1 in `CMakeLists.txt` (different lists; keep both).

**T3 — Report pages and report location (wave 1).** `src/app/html_page.{h,cpp}`, `src/app/report.{h,cpp}`, `src/app/dm_report.{h,cpp}`, `src/app/report_files.{h,cpp}`, `src/app/fill_report.cpp` (only where the shared template forces it), `tests/test_report*.cpp`, `tests/test_dm_report.cpp`, `tests/test_html_page.cpp` if it exists, and the `shell32`/`ole32` link line for `SHGetKnownFolderPath` in `CMakeLists.txt`. Expected overlaps: T1 and T2 in `CMakeLists.txt`.

**T4 — Batch job and job messages (wave 1).** `src/ui/library_jobs.{h,cpp}`, `src/ui/dm_jobs.{h,cpp}`, `src/ui/job_base.h`, new `src/app/user_messages.{h,cpp}`, new `tests/test_user_messages.cpp`, their `CMakeLists.txt` lines, and any existing unit test of the jobs. It does not touch the views: the old modal keeps compiling through `cancel()`. Expected overlaps: T3 (T4 calls `reports_dir()` through the existing report-path helpers; write against the interface above).

**T5 — Window placement and DPI (wave 1).** `src/ui/main.cpp`, `src/ui/app_shell.{h,cpp}`, `src/ui/fonts.h`, new `tests/test_app_shell.cpp` and its line in `CMakeLists.txt`. Expected overlaps: T1, T2 and T4 in `CMakeLists.txt`.

**T6 — Remove Auto (wave 2).** `src/core/rules.{h,cpp}`, `src/app/rules_file.cpp`, `src/app/config.{h,cpp}`, `src/search/pather.{h,cpp}`, `src/store/record_store.{h,cpp}` (CapQuery, the two fingerprints, a one-time delete of Auto rows at open), `src/store/path_codec.cpp`, `src/core/model.h`, `src/app/path_view.cpp` (its Auto line only), `src/cli/*.cpp` where they mention Auto, `tools/replay.cpp`, `tools/bench.cpp`, `src/app/analysis.cpp` and `tests/test_analysis.cpp` (one Auto lookup each), `src/app/report.cpp` (the report subtitle's cap label only; T3 owns the rest and merges first), `src/ui/app_state.{h,cpp}` (sp_cap handling, `last_cap`), `src/ui/details_panel.cpp` (the SP cap row of the controls), `src/ui/library_toolbar.cpp` (the Compare button's cap check), and the tests that mention Auto (`tests/test_rules.cpp`, `test_config.cpp`, `test_store.cpp`, `test_report.cpp`, `test_path_codec.cpp`, `test_dm_report.cpp`, `tests/corpus_util.h`, `tests/ui/uitest_*.cpp` lines that tick Auto). Expected overlaps: T7 in `record_store.{h,cpp}` (T6 edits CapQuery, the fingerprints and adds its delete to the open-time migration block; T7 edits the summary columns and adds its backfill to the same block; keep both, T6's delete first); T8 in `path_view.cpp` (keep T8's version and re-apply T6's one-line change).

**T7 — Library summaries in the store (wave 2).** `src/store/record_store.{h,cpp}` (summary columns, `get_summaries` in chunks of 10,000, the open-time `fill_missing_stars()`, the `songmeta.length_ms` column), `src/core/stars.{h,cpp}` (`path_stars`), `tests/test_stars.cpp`, `tests/test_store.cpp` (new cases only). Expected overlaps: T6 as above.

**T8 — Path and Preview view data (wave 2).** `src/app/path_view.{h,cpp}`, `src/app/preview_view.{h,cpp}`, `src/app/display_format.{h,cpp}`, `tests/test_path_view.cpp`, `tests/test_preview_view.cpp` (create if missing). No UI files: the old tabs keep compiling against the old view fields until T10 and T11 switch over, so T8 adds the new views beside the old ones and T10/T11 delete the old ones. Expected overlaps: T6 in `path_view.cpp`.

**T9 — Layout, settings bar and song panel (wave 3).** `src/ui/library_view.cpp` (the main window's split and where the strips sit), new `src/ui/settings_bar.cpp` (+ its declaration in `library_parts.h`), `src/ui/library_toolbar.cpp` (removes the old view controls it moves), `src/ui/details_panel.cpp`, `src/ui/details_view.h`, `src/ui/details_parts.h`, `src/ui/app_state.{h,cpp}` (the panel, `settings_locked`, `select_relative`, the analyze job no longer cancelled on close), `src/ui/theme.{h,cpp}`, `src/ui/widgets.h`, `tests/ui/uitest_details.cpp`, `tests/ui/uitest_harness.{h,cpp}` (shared helpers such as opening a song, which every GUI test calls), `CMakeLists.txt` (the new file). Expected overlaps: T12 and T13 in `app_state.{h,cpp}` (different members); T13 in `library_view.cpp` (T9 owns the layout; T13 adds its two strip calls where T9 leaves `// T13: batch strips` markers) and in `library_toolbar.cpp` (T9 removes the view controls, T13 rewrites the buttons).

**T10 — Paths tab (wave 3).** `src/ui/paths_tab.cpp`, `src/ui/details_parts.h` (its declarations only), new `tests/ui/uitest_paths.cpp`, its entry in `uitest_tests.cpp`, its line in `CMakeLists.txt`. Removes the old view fields T8 left for it, so it also edits `src/app/path_view.{h,cpp}` and `tests/test_path_view.cpp` (deletions and moved tests only; T8 merged in wave 2). Expected overlaps: T9 in `details_parts.h` and `uitest_tests.cpp`; T11 in `uitest_tests.cpp`.

**T11 — Preview tab (wave 3).** `src/ui/preview_tab.cpp`, `src/ui/preview_controller.{h,cpp}` (activation jumps, which path it draws), `tests/ui/uitest_preview.cpp`. Removes the old Preview view fields T8 left for it, so it also edits `src/app/preview_view.{h,cpp}`, `tests/test_preview_view.cpp` and one `MESSAGE` line in `tests/test_preview_golden.cpp`. Expected overlaps: T10 as above.

**T12 — Library table and search (wave 3).** New `src/ui/library_model.{h,cpp}` (the in-memory model, no ImGui) with its unit test, `tests/test_app_state.cpp`, `src/ui/library_table.cpp`, `src/ui/app_state.{h,cpp}` (the in-memory library model: every chart, its searchable row, its summary, the current order, chip counts, `library_view_order()`), `src/store/record_store.{h,cpp}` only if listing the whole library needs a call it lacks, `tests/ui/uitest_library.cpp`. Expected overlaps: T9 in `app_state.{h,cpp}`.

**T13 — Background batch and dialogs (wave 3).** `src/ui/library_dialogs.cpp`, `src/ui/library_toolbar.cpp` (the buttons and the status line), `src/ui/library_view.cpp` (only its two strip calls), `src/ui/win32_dialogs.{h,cpp}` (`show_in_folder`), `src/ui/app_state.{h,cpp}` (batch and report state), `tests/ui/uitest_batch_reports.cpp`. Expected overlaps: T9 as above.

**T14 — User guide and developer docs (wave 4).** `docs/UserGuide.md`, `docs/agents/ui-testing.md`, `CONTEXT.md`, `README.md` (only where it names the old UI or Auto), and `docs/adr/0003*` and `docs/adr/0014*` (a "Superseded 2026-09-27: Auto was removed" note at the top of each). Also the stale file-name references T1 leaves in `docs/agents/ui-testing.md`.


## Running it as a workflow

The plan runs as four Workflow runs, one per wave. The main session merges between them. You asked for this shape: agents working in parallel, with their changes merged by the main session.

**Before each wave, the main session makes the worktrees.** For every task in the wave it runs `git worktree add C:\Users\Patrick\Downloads\Hydra\wt-ui-T<n> -b ui/T<n> <base>`, where `<base>` is the Task 0 commit for wave 1 and the merged `hydra-test` head after that. Making them up front keeps the script simple, and the script's gate (a hook) requires parallel tasks on one repo to have their own worktrees.

**Inside a wave, each task is a small pipeline.** An Opus implementer does the task in its worktree, from its section of this plan. A Sonnet reviewer then checks it. The reviewer runs the scope gate: `git diff --stat <base>..ui/T<n>` must show only the task's files. It then re-runs the task's Verify command and acceptance checks, and reads the diff against the task text and the label contract. If the review fails, a follow-up Opus agent gets the reviewer's findings, fixes them in the same worktree, and the reviewer runs again. After two failed rounds the task is marked "needs the main session" and the wave goes on without it.

**The tasks in a wave run through `pipeline()`.** So each review starts as soon as its implementer finishes. Builds are the heavy part, and each worktree's first build is a full one. The script lets at most four implementers build at once; the rest wait for a slot. Wave 3 has five tasks, so one of them starts a little later.

**Every implementer and reviewer prompt carries the same rules, and stays under 4,000 characters.** It points at this plan's task section instead of pasting it. The rules are:

- the task line ("Task N of 15: <name>");
- the worktree path;
- the status line: append one line to `C:\Users\Patrick\.claude\hooks\state\status\<agent id>.md` every 10 tool calls or 5 minutes, using `status_append.ps1`;
- "never hand off to a background job";
- "never touch files outside your list";
- "if your session has no Agent tool, do the work yourself and say so".

**When a wave's run finishes, the main session merges.** First it runs the return-time checks on every branch:

1. The process table: any `cl.exe`, `hydra_*.exe` or `ffmpeg` older than 15 minutes is hung, so kill it.
2. `git diff --stat <base>..ui/T<n>` against the task's file list.
3. `git reflog -8` for commits nobody in the fleet made.

Then it merges the branches into `hydra-test` in task-number order, with `git merge --no-ff ui/T<n> -m "<message with the four trailers>"`. The trailer hook rejects `-F`. It resolves the expected overlaps each task names. `tests/ui/uitest_tests.cpp` only gains one registration line per task, so on a conflict it keeps both sides' lines.

**Two branches can merge cleanly and still not compile together.** So after the last merge of a wave, the main session builds everything and runs the checks:

- `hydra_tests.exe`;
- `hydra_uitest.exe --all`;
- for wave 2, the score-neutral batch diff.

The known join edits for each wave are listed in "The merge checklist" below; the main session makes them first. It fixes any other join problem in a merge-fix commit. Only then does the next wave start from that merged head. It removes the merged worktrees with `git worktree remove` once the wave's checks pass.

**Stopping a wave early.** A reviewer's scope gate can fail on an edit a later-merged task forced. In that case, don't let a follow-up "fix" scope by reverting a needed edit. Stop that task's chain, and verify it by hand at merge time.


## Questions settled during planning

Each task section was drafted by a planning agent, and each agent ended with questions for the main session. Most were about who owns a file or which name to use. Those are answered in the file list and the fixed interfaces above, so the task sections no longer carry them. The ones worth knowing about are below.

The star count is worked out once. T7 stores it when a result is saved, using `path_stars()`. T9's "7 stars" headline reads the stored number instead of counting again. This follows the "every rule is derived once" constraint.

The report job's answers are functions, not fields. T4 adds `saved_path()`, `opened()` and `open_problem()`. T13 builds the full "Report saved, but..." line around `open_problem()`, because T13 knows where the file went.

The Paths timeline gets the song's length from the store. T7 gained Step 8a for it. See "Calls I made myself" for what that means for older results.

Cancelling the leaderboard picker no longer freezes the window. Today the cancel waits for the network, for up to two minutes. T13 keeps the cancelled job alive and drops it on a later frame. Closing Hydra while such a job is still waiting can still pause the exit. That needs a fix in `src/net/`, which no task owns, so it is filed as a separate follow-up task instead of being folded in here.

The Auto delete is precise. T6 deletes rows that carry the Auto fingerprint. Auto rows made under a hand-edited `auto_cap_ladder` have a different fingerprint and are missed. They already read Stale, and the next analysis of that chart replaces them. Catching them too would also delete legitimate fixed-cap rows made under other rules. Your installed database has no Auto rows at all (T6's agent counted: 0 Auto rows, 18,775 rows at 4 bars).

The comparison page's "Percent" column gets a neutral note: "The percent the leaderboard lists for this score." Nothing in the repo defines what the leaderboard means by it, so the note doesn't guess.

The fill-spawn page (`fill_report.cpp`, command-line only) gets the same labels on its search box and dropdown as the other reports. That is a two-line fix done at the wave 1 merge; see the checklist.

T11's `preview-activation-jumps` test expects Burnout to end at `m96.3.240`, a value read with the song's audio loaded. The Preview's length is the later of the last note and the audio's end. If the headless runner loads no audio and the check fails with a different `m96...` value, compare against the time box at `length_ms()` instead of the fixed string. That change is approved in advance; any other failure there is a real one.

The report subtitle uses the singular for one: "1 record across 1 chart", not "1 records".

## The merge checklist

These are small edits the main session makes while merging, because they join two tasks' work. Each one is a merge-fix commit with the four trailers. Do them after the wave's last merge and before its build.

**Wave 1.**

1. In `src/app/fill_report.cpp`, give the search box and the dropdown the same `aria-label` attributes T3 gave the other report pages.
2. Stale comments T1 leaves behind: `src/ui/dm_jobs.h` and `src/ui/library_jobs.h` name the old `library_view.cpp` and `details_view.cpp`. Point them at the new file names.

**Wave 2.**

3. `src/app/path_view.h` and `tests/test_path_view.cpp` name `ui/details_view.cpp` in their opening comment. Point them at `ui/paths_tab.cpp`. `path_view.h` also mentions `LibraryPage::RowSummary`; drop that reference, since T12 removes the type in wave 3.

**Wave 3.** Merge in task order: T9, T10, T11, T12, T13. Most conflicts are in `app_state.{h,cpp}` and the GUI test files, and each side keeps its own members.

4. T12 made small temporary edits in `library_view.cpp`, `library_toolbar.cpp` and `library_dialogs.cpp` so its branch compiled. Drop them; T9's and T13's versions of those files win. T12's reviewer should accept those three files in its scope check for the same reason.
5. T9 reads the library through three accessors, `view_row_count()`, `view_row(i)` and `view_row_status(i)`. Their bodies become `return library_shown_count();`, `return library_row_at(i).entry;` and `return library_row_at(i).status;`.
6. T9's `render_library_pane` body becomes `detail::render_library(app);`, followed by `if (app.library_total == 0) ImGui::TextUnformatted(detail::empty_library_message(app.settings));`.
7. T12's table reads `app.show_details` at a line marked `// T9:`. Change it to `app.details_open()`.
8. In `render_search_box`, Ctrl+F only works while `!app.show_details`. That guard was for the old blocking window. Change it to "no popup is open": `!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)`.
9. T13's `open_batch_confirm` becomes `batch_scope = library_matches(); batch_scope_with_result = library.counts().analyzed;`. The "Analyze search (N)..." count becomes `static_cast<int64_t>(app.library_match_count())`. The GUI helper `batch_search` reads `library_match_count()` the same way. Delete T13's `refresh_page()` call in `update_background_jobs`, because `tick_library` re-reads summaries after a batch.
10. T9's two failure messages in `AppState::update_analyze_job` use `set_status`. Switch both to T13's `set_problem`, so a failure stays on screen until dismissed.
11. T9's headline prints the hardest squeeze with its own `snprintf("%.1f ms")`. Switch it to T8's `format_ms_spaced`.
12. T9's Ctrl+C handler calls `copy_selected_path(app)` instead of its own `SetClipboardText` line, so the shortcut also flashes "Copied!". Then add one check to the `paths-folds-copy` test: `ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C)` and expect `Copied!` again.
13. T9's tab bar passes `ImGuiTabItemFlags_SetSelected` to the `Preview` tab item on the frame `app.details_ui.paths_tab.ui().preview_jump` has a value. That makes "Show in Preview" switch tabs.
14. `app_shell.cpp`'s `run_frame`: T9 adds `app.tick()` and T13 adds `app.update_background_jobs()`. Keep both, T9's first.
15. Delete `test_backend_limit` and `test_squeezed_out_uncounted` from `uitest_details.cpp` and from the run-order list. T10's `paths-backend-timings` and `paths-uncounted` replace them.
16. `test_scan`, `test_view_settings` and `test_difficulty` still use old labels. `Refresh scan` is now `Scan library`. The page arrows are gone, so `test_view_settings` scrolls the table instead. The chart mode moved from the old window title to the panel heading.
17. Remove `DmReportStats::above` and `::unmatched` from `src/app/dm_report.h`, with `DmReportJob::above()` and `unmatched()` in `src/ui/dm_jobs.h`. T13 stopped reading them, and nothing else does.
18. T12's GUI test opens and closes the panel with `select()` and `close_details()`. If T9 renamed `close_details`, follow the new name.

After these, build everything and run both test suites, as "Running it as a workflow" says.

---

### Task 0: Commit the plan and record the baselines (main session)

The plan, its tasks file, the handoff and the mockup sources are not committed yet. The worktrees the fleet works in only see committed files, so they would start without the plan. This task commits them and records the "before" numbers that later tasks compare against. The mockup's five `.dc.html` files are already copied into the repo so executors can read the exact layout and wording without the artifact link.

There is also an untracked `docs/handoffs/2026-09-26-ch-probe-live-runs-handoff.md` from another session. Leave it alone: don't stage it, move it or edit it.

**Wave:** before wave 1. **Depends on:** nothing. **Expected overlaps:** none.

**Goal:** A committed base for the fleet, with baselines in `$env:TEMP\hydra_ui_base\`.

**Files:**
- Commit: `docs/superpowers/plans/2026-09-27-ui-redesign.md`, `docs/superpowers/plans/2026-09-27-ui-redesign.md.tasks.json`, `docs/handoffs/2026-09-27-ui-redesign-handoff.md`, `docs/superpowers/specs/2026-09-27-ui-redesign-design.md`
- Commit: `docs/superpowers/specs/2026-09-27-ui-redesign-mockup/` (already copied) holding `Main.dc.html`, `Preview.dc.html`, `Batch.dc.html`, `Confirm.dc.html`, `Done.dc.html`, and a `README.md` that links the artifact and says which screen is which

**Acceptance Criteria:**
- [ ] `git status --short` lists only `?? docs/handoffs/2026-09-26-ch-probe-live-runs-handoff.md`.
- [ ] `$env:TEMP\hydra_ui_base\` holds `batch_sorted.txt`, `tests_summary.txt`, `uitest.txt` and `base_commit.txt`.
- [ ] `uitest.txt` shows all 26 GUI tests passing, and `tests_summary.txt` ends with `Status: SUCCESS!`.

**Verify:** `git status --short; Get-ChildItem $env:TEMP\hydra_ui_base | Select-Object Name` → the one untracked handoff, then the four baseline files.

**Steps:**

- [ ] **Step 1: Check the mockup copy.** The planning session already copied the five artboards and a `README.md` into `docs/superpowers/specs/2026-09-27-ui-redesign-mockup/`, uncommitted. Check that all six files are there with `Get-ChildItem docs\superpowers\specs\2026-09-27-ui-redesign-mockup`. If one is missing, read it from the artifact with the Artifact tool (`action: "read"`, `url: https://claude.ai/artifact/TRLProeKnWPrDmmD1dtacM`, `paths: ["project/Main.dc.html", "project/Preview.dc.html", "project/Batch.dc.html", "project/Confirm.dc.html", "project/Done.dc.html"]`) and save it there.

- [ ] **Step 2: Commit.**

```powershell
git add docs/superpowers/plans/2026-09-27-ui-redesign.md docs/superpowers/plans/2026-09-27-ui-redesign.md.tasks.json docs/handoffs/2026-09-27-ui-redesign-handoff.md docs/superpowers/specs/2026-09-27-ui-redesign-design.md docs/superpowers/specs/2026-09-27-ui-redesign-mockup
git status --short   # only the ch-probe handoff stays untracked
git commit -m "UI redesign: plan, tasks, handoff and approved mockup

Task: Task 0 - commit plan and baselines
Agent: main session
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
New-Item -ItemType Directory -Force $env:TEMP\hydra_ui_base | Out-Null
git rev-parse HEAD | Set-Content "$env:TEMP\hydra_ui_base\base_commit.txt"
```

- [ ] **Step 3: Record the baselines on a quiet machine.** Nothing else may be building or testing while these run. Run the build from Bash with `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 > build.log 2>&1`, once with no target and once with `-Target hydra_uitest`. Then, from PowerShell:

```powershell
$b = "$env:TEMP\hydra_ui_base"
Remove-Item "$b\base.db" -ErrorAction SilentlyContinue
.\build-cpp\Release\hydra_batch.exe --db "$b\base.db" testdata\input | Set-Content "$b\batch_full.txt"
Get-Content "$b\batch_full.txt" | Select-String '^\[\d+/\d+\] ' |
  ForEach-Object { $_.Line -replace '^\[\d+/\d+\] ', '' } | Sort-Object | Set-Content "$b\batch_sorted.txt"
.\build-cpp\Release\hydra_tests.exe | Set-Content "$b\tests_summary.txt"
.\build-cpp\Release\hydra_uitest.exe --all | Set-Content "$b\uitest.txt"
```

Read `src/cli/batch.cpp` first if `hydra_batch` refuses these arguments. Never run it with no arguments: that analyzes the real library next to the exe.

---

### Task 1: Split the UI and GUI-test files

Today three files hold almost all of Hydra's interface and its GUI tests. `src/ui/details_view.cpp` (1,324 lines) draws the whole song details window: the song info, the controls, all four tabs and the analyze progress. `src/ui/library_view.cpp` (811 lines) draws the main window, its toolbar, the library table and every modal. `tests/ui/uitest_tests.cpp` (1,352 lines) holds all 26 GUI tests. Wave 3 has five tasks that each need to edit these files at the same time, and two agents editing one file in parallel means merge conflicts on every wave. This task cuts each file into smaller ones by area, so every later UI task owns its own file. It moves code only. No function body, label, wording or behaviour changes, and you see nothing different in the app.

**Wave:** 1. **Depends on:** Task 0. **Expected overlaps:** T2, T3 and T4 add lines to `CMakeLists.txt` in wave 1. T1 edits only the `hydra_ui` and `hydra_uitest_harness` source lists; T2 and T4 add to the core, app and `hydra_tests` lists, and T3 adds a link line. The merger keeps every side's lines. T9 edits `tests/ui/uitest_harness.{h,cpp}` in wave 3 and builds on the helpers this task moves there.

**Goal:** The same code, split into the files named in 01-ownership.md, builds and passes the same 471 unit tests and the same 26 GUI tests in the same order.

**Files:**
- Create: `src/ui/details_parts.h`, `src/ui/details_panel.cpp`, `src/ui/paths_tab.cpp`, `src/ui/preview_tab.cpp`, `src/ui/dynamics_tab.cpp`, `src/ui/stars_tab.cpp`
- Create: `src/ui/library_parts.h`, `src/ui/library_toolbar.cpp`, `src/ui/library_table.cpp`, `src/ui/library_dialogs.cpp`
- Create: `tests/ui/uitest_library.cpp`, `tests/ui/uitest_details.cpp`, `tests/ui/uitest_preview.cpp`, `tests/ui/uitest_batch_reports.cpp`
- Delete: `src/ui/details_view.cpp` (all of it moves out; `src/ui/details_view.h` stays unchanged as the public header)
- Modify: `src/ui/library_view.cpp` (keeps only `render_main_window`), `tests/ui/uitest_tests.cpp` (keeps only its header comment and `register_tests`), `tests/ui/uitest_harness.h` and `tests/ui/uitest_harness.cpp` (gain the three shared test helpers and the test table type), `CMakeLists.txt` (the two source lists)
- Test: no new tests. The proof is the existing suites plus the pure-move check in Step 2.

**Acceptance Criteria:**
- [ ] The pure-move check (Step 2's `move_check.ps1`) prints `details: 0 line(s) differ`, `library: 0 line(s) differ` and `tests: 0 line(s) differ`.
- [ ] `.\build-cpp\Release\hydra_tests.exe` ends with `Status: SUCCESS!` and reports the same test-case count as the base build (471 when this plan was written).
- [ ] `.\build-cpp\Release\hydra_uitest.exe --list` prints the same 26 names in the same order as the base build (`Compare-Object` of the two lists prints nothing).
- [ ] `.\build-cpp\Release\hydra_uitest.exe --all` prints 26 `[PASS]` lines, no `[FAIL]`, and exits 0.
- [ ] `Hydra.exe` builds (the full build in Step 5 finishes with no errors).
- [ ] `git diff --stat <base>..ui/T1` lists exactly the 20 files named above and nothing else.

**Verify:** `.\build-cpp\Release\hydra_uitest.exe --all` → 26 lines starting `[PASS]`, exit code 0.

#### Where every function goes

The details window splits five ways. `details_panel.cpp` keeps the window and everything around the tabs: `icon_marker`, `render_song_info`, `render_record_status`, `render_controls`, `update_analyze_job`, `render_analyze_progress`, `render_record_state` and `render_details_modal`. `paths_tab.cpp` takes the Paths tab: `begin_section`, `end_section`, `warnable_text`, `render_multsqueeze_section`, `render_activations_section`, `render_score_breakdown_section`, `render_path_details`, `render_path_row` and `render_path_panel`. `preview_tab.cpp` takes `render_preview_panel`. `dynamics_tab.cpp` takes the block under the `// ---- Dynamics tab ----` comment: `pad_color`, `pad_dot`, `dynamics_table_row` and `render_dynamics_panel`. `stars_tab.cpp` takes the block under `// ---- Stars tab ----`, which is `render_stars_panel`.

The main window splits four ways. `library_view.cpp` keeps `render_main_window`. `library_toolbar.cpp` takes `render_status_line`, `render_actions_row` and `render_view_controls`. `library_table.cpp` takes `render_search_box`, `summary_label` and `render_library_table`. `library_dialogs.cpp` takes `main_hwnd`, `render_folder_manager`, `render_scan_modal`, `render_batch_modal` and `render_dm_picker_modal`.

The GUI tests split by the part of the app they drive. Each file keeps its tests in the order they had in the old file.

- `uitest_library.cpp`: `test_scan`, `test_difficulty`, `test_rules_error`, `test_library_state_per_app`, `test_view_settings`.
- `uitest_details.cpp`: `test_analyze`, `test_cap_switch`, `test_backend_limit`, `test_dynamics`, `test_dynamics_stored`, `open_titled`, `analyze_open_song`, `test_stars`, `test_squeezed_out_uncounted`, `test_details_close_teardown`.
- `uitest_preview.cpp`: `test_preview`, `test_analyze_on_preview`, `test_preview_path_overlay`, `test_preview_controls`, `test_preview_drain_box`, `DisplayWidth`, `test_preview_overlay_fit`, `test_preview_buttons_keys`, `test_scrub_hold`, `test_layout_drift`.
- `uitest_batch_reports.cpp`: `test_batch_modal_drift`, `test_settings_and_reports`, `test_dm_compare_flow`, `test_report_buttons`, plus the `namespace fs = std::filesystem;` line (only `test_settings_and_reports` uses `fs`).
- `uitest_harness.cpp`: `scan_library`, `open_details`, `open_preview`, with their comments. Every area file needs `scan_library`, three need `open_details`, and two need `open_preview`, so they become shared.

That is 5 + 8 + 9 + 4 = 26 tests. `open_titled` and `analyze_open_song` stay file-local in `uitest_details.cpp`, since only `test_stars` uses them.

#### How file-local helpers become shared

Today every helper sits in an anonymous namespace (a namespace with no name, which makes its functions private to one `.cpp` file). A helper that only one new file calls stays in an anonymous namespace in that file. A helper that another file calls moves into `namespace hydra::ui::detail` and is declared in the area's parts header. Nothing becomes `static`.

For the details files, `details_parts.h` declares five functions. `render_record_state` is defined in `details_panel.cpp` and called by the window and by `render_stars_panel`. `render_path_panel`, `render_preview_panel`, `render_dynamics_panel` and `render_stars_panel` are called by the window. For the library files, `library_parts.h` declares nine: `render_status_line` (the toolbar and the folder manager both call it), `render_folder_manager` (the actions row calls it), and the seven functions `render_main_window` calls.

A call from `hydra::ui` into `detail` needs the `detail::` prefix. That touches only calls inside `render_details_modal` and `render_main_window`. A function inside `detail` calls another `detail` function, or an anonymous-namespace helper in its own file, without any prefix, so those lines stay byte-identical. The move check strips `detail::` before comparing, so the prefix is the only allowed difference.

The tests do the same with a plain `uitest` namespace. The three shared helpers leave the anonymous namespace and are declared in `uitest_harness.h`. Each area file's tests stay in an anonymous namespace, and the file exposes one function that returns its table.

`--all` runs tests in the order they were registered, and the ImGui context carries over from one test to the next (the tab bar remembers its tab, input boxes can keep text). So `register_tests` keeps the old order with a fixed name list instead of registering file by file. A test not in that list, such as one a later task adds, runs after the 26, in its file's order. Later tasks never have to edit the list.

**Steps:**

- [ ] **Step 1: Build the untouched worktree and record the "before" numbers.**

The worktree `C:\Users\Patrick\Downloads\Hydra\wt-ui-T1` is on branch `ui/T1` at the Task 0 commit. Its first build is a full build. Build with the Bash tool from the worktree root:

```bash
cd /c/Users/Patrick/Downloads/Hydra/wt-ui-T1 && powershell -ExecutionPolicy Bypass -File build_cpp.ps1 > build.log 2>&1; tail -3 build.log
```

Expected: the last lines show the build finishing with no `error` lines. `build.log` sits at the repo root, where `.gitignore`'s `/*.log` keeps it out of git.

Then record the base commit and the three baselines with the PowerShell tool:

```powershell
$out = "$env:TEMP\hydra_ui_T1"; New-Item -ItemType Directory -Force $out | Out-Null
git rev-parse HEAD | Set-Content "$out\base.txt"
.\build-cpp\Release\hydra_tests.exe | Select-Object -Last 3 | Set-Content "$out\unit_before.txt"
.\build-cpp\Release\hydra_uitest.exe --list | Set-Content "$out\list_before.txt"
.\build-cpp\Release\hydra_uitest.exe --all 2>&1 | Set-Content "$out\all_before.txt"
Get-Content "$out\unit_before.txt"; (Get-Content "$out\list_before.txt").Count
Select-String '^\[(PASS|FAIL)\]' "$out\all_before.txt" | Group-Object { $_.Line.Substring(0,6) } | Select-Object Name, Count
```

Expected: `Status: SUCCESS!` with the test-case count (471 at plan time), `26`, and `[PASS] 26`. If the base already fails a GUI test, stop and report it; this task must not start from a red suite.

- [ ] **Step 2: Save the pure-move check.**

This script lives outside the repo, in `$env:TEMP\hydra_ui_T1\move_check.ps1`. It compares every non-blank line of the old file with every non-blank line of the new files, as sorted lists. It ignores lines that a move has to change: preprocessor lines (`#include`, the `NOMINMAX` guard), namespace openers and closers, brace-only lines (`{`, `}`, `};`), and the `detail::` prefix. For the tests it also sets aside the parts of `register_tests` that are rewritten (Step 5 lists them). Write it with the Write tool:

```powershell
# Pure-move check for Task 1. Run from the worktree root:
#   & "$env:TEMP\hydra_ui_T1\move_check.ps1" -Part details|library|tests
# Prints "<part>: N line(s) differ" and each differing line. N must be 0.
param([Parameter(Mandatory)][ValidateSet('details', 'library', 'tests')][string]$Part)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8   # git output has em dashes
$Base = (Get-Content "$env:TEMP\hydra_ui_T1\base.txt").Trim()

function Norm([string[]]$lines) {
    foreach ($l in $lines) {
        $t = ($l -replace '\bdetail::', '').Trim()
        if ($t -eq '') { continue }
        if ($t -match '^#') { continue }
        if ($t -match '^namespace [\w:]*\s*\{$') { continue }
        if ($t -match '^\}\s*//\s*namespace\b') { continue }
        if ($t -match '^[{}]+;?$') { continue }
        $t
    }
}
function Old([string]$path) { git show "${Base}:$path" }
function Check([string]$name, [string[]]$old, [string[]]$new) {
    $a = @(Norm $old | Sort-Object -CaseSensitive)
    $b = @(Norm $new | Sort-Object -CaseSensitive)
    $d = @(Compare-Object -CaseSensitive $a $b)
    "{0}: {1} line(s) differ" -f $name, $d.Count
    foreach ($x in $d) { "  {0} {1}" -f $x.SideIndicator, $x.InputObject }
}
function Before-Register([string[]]$lines) {
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -like 'void register_tests(Harness& h) {*') { return $lines[0..($i - 1)] }
    }
    return $lines
}

switch ($Part) {
    'details' {
        Check 'details' (Old 'src/ui/details_view.cpp') (Get-Content src/ui/details_panel.cpp,
            src/ui/paths_tab.cpp, src/ui/preview_tab.cpp, src/ui/dynamics_tab.cpp, src/ui/stars_tab.cpp)
    }
    'library' {
        Check 'library' (Old 'src/ui/library_view.cpp') (Get-Content src/ui/library_view.cpp,
            src/ui/library_toolbar.cpp, src/ui/library_table.cpp, src/ui/library_dialogs.cpp)
    }
    'tests' {
        $oldFile = @(Old 'tests/ui/uitest_tests.cpp')
        # The old side: everything above register_tests, plus its 26 table rows.
        $oldSide = @(Before-Register $oldFile) +
                   @($oldFile | Where-Object { $_ -match '^\s*\{"[^"]+", test_\w+\},$' })
        # The new side: the new uitest_tests.cpp above register_tests, the four
        # area files minus their three table-wrapper lines, and the lines added
        # to uitest_harness.cpp.
        $wrapper = '^\s*(const std::vector<TestEntry>& \w+_tests\(\) \{|static const std::vector<TestEntry> entries = \{|return entries;)$'
        $areas = Get-Content tests/ui/uitest_library.cpp, tests/ui/uitest_details.cpp,
                     tests/ui/uitest_preview.cpp, tests/ui/uitest_batch_reports.cpp |
                 Where-Object { $_ -notmatch $wrapper }
        $moved = git diff $Base -- tests/ui/uitest_harness.cpp |
                 Where-Object { $_ -match '^\+' -and $_ -notmatch '^\+\+\+' } |
                 ForEach-Object { $_.Substring(1) }
        $newSide = @(Before-Register (Get-Content tests/ui/uitest_tests.cpp)) + @($areas) + @($moved)
        Check 'tests' $oldSide $newSide
    }
}
```

Run it once now with `-Part details`. Expected: it fails with a "Cannot find path ... details_panel.cpp" error, because the new files don't exist yet. That shows the check reads the new files and not the old ones.

- [ ] **Step 3: Split the details window.**

Create each new file with the Write tool. Each skeleton below is complete except for its `//>> paste` lines. Replace every `//>> paste` line with the named functions, copied byte for byte from `src/ui/details_view.cpp` at the base commit, including the comment block above each function. Don't leave a `//>> paste` line in place: the move check counts it as a new line and fails.

`src/ui/details_parts.h`:

```cpp
// The song details window's pieces, shared between the files that draw it.
// Only the details files include this; everything else uses details_view.h.
//
// details_panel.cpp  the window itself, the song info, the stored-result
//                    panel, the controls, the analyze job's lifecycle and
//                    progress, and the record states the tabs share
// paths_tab.cpp      the Paths tab: the path list and a path's details
// preview_tab.cpp    the Preview tab: transport row, highway and overlays
// dynamics_tab.cpp   the Dynamics tab
// stars_tab.cpp      the Stars tab

#ifndef HYDRA_UI_DETAILS_PARTS_H
#define HYDRA_UI_DETAILS_PARTS_H

#include "ui/app_state.h"

namespace hydra::ui::detail {

// details_panel.cpp. The states a record-backed tab shows before its own
// content (analyze progress, not analyzed, stale, no paths). True only when
// the record is ready to draw.
bool render_record_state(AppState& app, const char* not_analyzed_text);

// paths_tab.cpp. The path list on the left and the selected path's details
// on the right. A click in the list changes `selected_path`.
void render_path_panel(AppState& app, const Path*& selected_path);

// preview_tab.cpp. The transport row and the 3D highway for `selected_path`.
void render_preview_panel(AppState& app, const Path* selected_path);

// dynamics_tab.cpp.
void render_dynamics_panel(AppState& app);

// stars_tab.cpp.
void render_stars_panel(AppState& app);

}  // namespace hydra::ui::detail

#endif  // HYDRA_UI_DETAILS_PARTS_H
```

`src/ui/details_panel.cpp`. The include block keeps the base file's order: the public header first, then the `NOMINMAX` guard before `<windows.h>` (`ui/icons.h` pulls in `<d3d11.h>`, and this file calls `std::max`):

```cpp
#include "ui/details_view.h"
#include "ui/details_parts.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/path_view.h"
#include "core/model.h"   // kCloneHeroSpCap
#include "imgui.h"
#include "ui/fonts.h"
#include "ui/generation.h"
#include "ui/icons.h"
#include "ui/preview_controller.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

namespace hydra::ui {

namespace {

//>> paste: icon_marker, render_song_info, render_record_status, render_controls, update_analyze_job, render_analyze_progress

}  // namespace

namespace detail {

//>> paste: render_record_state

}  // namespace detail

//>> paste: render_details_modal

}  // namespace hydra::ui
```

In the pasted `render_details_modal`, add `detail::` to exactly five calls and change nothing else:

```cpp
            if (render_record_state(
                    app, "After analyzing this song, paths will show up here."))
                render_path_panel(app, selected_path);
```

becomes

```cpp
            if (detail::render_record_state(
                    app, "After analyzing this song, paths will show up here."))
                detail::render_path_panel(app, selected_path);
```

and `render_preview_panel(app, selected_path);`, `render_dynamics_panel(app);` and `render_stars_panel(app);` become `detail::render_preview_panel(app, selected_path);`, `detail::render_dynamics_panel(app);` and `detail::render_stars_panel(app);`.

`src/ui/paths_tab.cpp`:

```cpp
#include "ui/details_parts.h"

#include "app/path_view.h"
#include "core/model.h"
#include "imgui.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <string>
#include <vector>

namespace hydra::ui {

namespace {

//>> paste: begin_section, end_section, warnable_text, render_multsqueeze_section, render_activations_section, render_score_breakdown_section, render_path_details, render_path_row

}  // namespace

namespace detail {

//>> paste: render_path_panel

}  // namespace detail

}  // namespace hydra::ui
```

`src/ui/preview_tab.cpp` keeps the base file's `NOMINMAX` guard too. It calls `std::max`, and the guard stops `<windows.h>` from defining `max` as a macro, whichever header pulls it in:

```cpp
#include "ui/details_parts.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/preview_view.h"  // path_overlay_key
#include "core/model.h"        // kCloneHeroSpCap
#include "imgui.h"
#include "imgui_internal.h"  // SetKeyOwner, owner-aware IsKeyPressed
#include "render/overlay_layout.h"
#include "ui/fonts.h"
#include "ui/preview_controller.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <string>

namespace hydra::ui::detail {

//>> paste: render_preview_panel

}  // namespace hydra::ui::detail
```

`src/ui/dynamics_tab.cpp`. The `// ---- Dynamics tab ---...` rule line and the `// Pad dot colours` comment move with `pad_color`:

```cpp
#include "ui/details_parts.h"

#include "app/dynamics_breakdown.h"
#include "core/model.h"  // group_thousands
#include "imgui.h"
#include "ui/dynamics_load_job.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace hydra::ui {

namespace {

//>> paste: the "// ---- Dynamics tab ----" line, pad_color, pad_dot, dynamics_table_row

}  // namespace

namespace detail {

//>> paste: render_dynamics_panel

}  // namespace detail

}  // namespace hydra::ui
```

`src/ui/stars_tab.cpp`:

```cpp
#include "ui/details_parts.h"

#include "core/model.h"  // group_thousands
#include "core/stars.h"
#include "imgui.h"

#include <cstdint>

namespace hydra::ui::detail {

//>> paste: the "// ---- Stars tab ----" line, render_stars_panel

}  // namespace hydra::ui::detail
```

Remove the old file and put the new ones in the build. In `CMakeLists.txt`, the `hydra_ui` list changes from

```cmake
add_library(hydra_ui STATIC
    src/ui/app_shell.cpp
    src/ui/app_state.cpp
    src/ui/details_view.cpp
    src/ui/dm_jobs.cpp
    src/ui/icons.cpp
```

to

```cmake
add_library(hydra_ui STATIC
    src/ui/app_shell.cpp
    src/ui/app_state.cpp
    src/ui/details_panel.cpp
    src/ui/dm_jobs.cpp
    src/ui/dynamics_tab.cpp
    src/ui/icons.cpp
    src/ui/paths_tab.cpp
    src/ui/preview_tab.cpp
    src/ui/stars_tab.cpp
```

(the rest of the list stays as it is). Then:

```powershell
git rm -q src/ui/details_view.cpp
& "$env:TEMP\hydra_ui_T1\move_check.ps1" -Part details
```

Expected: `details: 0 line(s) differ`. Any line it prints is a copy mistake: a `<=` line was lost or changed, a `=>` line is new. Fix the copy until it prints 0. Then build Hydra with the Bash tool:

```bash
cd /c/Users/Patrick/Downloads/Hydra/wt-ui-T1 && powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target Hydra > build.log 2>&1; grep -c " error " build.log; tail -2 build.log
```

Expected: `0` errors. An "undeclared identifier" error means a new file's include list misses a header the base file had. Add the missing line from the base file's include block (lines 3 to 29 of `details_view.cpp`); include lines are outside the move check. Commit:

```powershell
git add CMakeLists.txt src/ui/details_parts.h src/ui/details_panel.cpp src/ui/paths_tab.cpp src/ui/preview_tab.cpp src/ui/dynamics_tab.cpp src/ui/stars_tab.cpp
git commit -m "Split details_view.cpp into the panel and one file per tab

Code moves only: every function keeps its body. The five functions the
window calls across files move from the anonymous namespace into
hydra::ui::detail and are declared in details_parts.h.

Task: Task 1 - split the UI and GUI-test files
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 4: Split the main window.**

`src/ui/library_parts.h`:

```cpp
// The main window's pieces, shared between the files that draw it. Only the
// library files include this; everything else uses library_view.h.
//
// library_view.cpp     render_main_window: lays the window out, reaps
//                      finished jobs, and places everything below
// library_toolbar.cpp  the status line, the actions row, the view controls
// library_table.cpp    the search box and the library table
// library_dialogs.cpp  the Song folders, Scanning charts, Analyzing and
//                      Compare dmleaderboards user modals

#ifndef HYDRA_UI_LIBRARY_PARTS_H
#define HYDRA_UI_LIBRARY_PARTS_H

#include "ui/app_state.h"

namespace hydra::ui::detail {

// library_toolbar.cpp
void render_status_line(AppState& app, bool same_line);
void render_actions_row(AppState& app);
void render_view_controls(AppState& app);

// library_table.cpp
void render_search_box(AppState& app);
void render_library_table(AppState& app, int visible_rows);

// library_dialogs.cpp
void render_folder_manager(AppState& app);
void render_scan_modal(AppState& app);
void render_batch_modal(AppState& app);
void render_dm_picker_modal(AppState& app);

}  // namespace hydra::ui::detail

#endif  // HYDRA_UI_LIBRARY_PARTS_H
```

`src/ui/library_toolbar.cpp`:

```cpp
#include "ui/library_parts.h"

#include "imgui.h"
#include "ui/fonts.h"
#include "ui/generation.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace hydra::ui::detail {

//>> paste: render_status_line, render_actions_row, render_view_controls

}  // namespace hydra::ui::detail
```

`src/ui/library_table.cpp`:

```cpp
#include "ui/library_parts.h"

#include "imgui.h"
#include "store/record_store.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace hydra::ui {

namespace {

//>> paste: summary_label

}  // namespace

namespace detail {

//>> paste: render_search_box, render_library_table

}  // namespace detail

}  // namespace hydra::ui
```

`src/ui/library_dialogs.cpp`:

```cpp
#include "ui/library_parts.h"

#include "app/report_files.h"
#include "core/strutil.h"
#include "imgui.h"
#include "store/record_store.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/win32_dialogs.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

namespace hydra::ui {

namespace {

//>> paste: main_hwnd

}  // namespace

namespace detail {

//>> paste: render_folder_manager, render_scan_modal, render_batch_modal, render_dm_picker_modal

}  // namespace detail

}  // namespace hydra::ui
```

`library_view.cpp` keeps under a fifth of its lines, and a hook denies an Edit that keeps under 40% of a block. So write the new version to `src/ui/library_view.cpp.new` with the Write tool and move it over:

```cpp
#include "ui/library_view.h"
#include "ui/library_parts.h"

#include "app/report_files.h"
#include "imgui.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cstdio>

namespace hydra::ui {

//>> paste: render_main_window

}  // namespace hydra::ui
```

In the pasted `render_main_window`, seven calls gain `detail::` and nothing else changes: `render_actions_row(app);`, `render_view_controls(app);`, `render_search_box(app);`, `render_library_table(app, visible_rows);`, and the three modal calls at the end, which become

```cpp
    if (app.scan_job) detail::render_scan_modal(app);
    if (app.batch_job || app.batch_confirm_pending) detail::render_batch_modal(app);
    if (app.dm_picker_open) detail::render_dm_picker_modal(app);
```

Then:

```powershell
Move-Item -Force src/ui/library_view.cpp.new src/ui/library_view.cpp
```

In `CMakeLists.txt`'s `hydra_ui` list, the lines around the library file change from

```cmake
    src/ui/library_jobs.cpp
    src/ui/library_view.cpp
```

to

```cmake
    src/ui/library_dialogs.cpp
    src/ui/library_jobs.cpp
    src/ui/library_table.cpp
    src/ui/library_toolbar.cpp
    src/ui/library_view.cpp
```

Run `& "$env:TEMP\hydra_ui_T1\move_check.ps1" -Part library` (expected `library: 0 line(s) differ`), build `Hydra` as in Step 3 (expected 0 errors), and commit the six files with the message `Split library_view.cpp into toolbar, table and dialogs`, a body saying "Code moves only. The nine functions called across files move into hydra::ui::detail and are declared in library_parts.h.", and the same four trailer lines.

- [ ] **Step 5: Split the GUI tests.**

Add the test table type and the three shared helpers to `tests/ui/uitest_harness.h`. The comment above `register_tests` changes from

```cpp
// Register every C++ test (uitest_tests.cpp) and, when h.script_path is set,
// the "script" test (uitest_script.cpp). Each test's UserData is &h.
```

to

```cpp
// Register every C++ test (the uitest_<area>.cpp files, in the order
// uitest_tests.cpp fixes) and, when h.script_path is set, the "script" test
// (uitest_script.cpp). Each test's UserData is &h.
```

and after the `escape_ref` declaration, before `}  // namespace uitest`, add:

```cpp
// ---- the checked-in C++ tests ---------------------------------------------

// One checked-in test: the name --test and --list use, and its body.
struct TestEntry {
    const char* name;
    void (*fn)(ImGuiTestContext*);
};

// Each area file's tests, in the order that file lists them.
const std::vector<TestEntry>& library_tests();       // uitest_library.cpp
const std::vector<TestEntry>& details_tests();       // uitest_details.cpp
const std::vector<TestEntry>& preview_tests();       // uitest_preview.cpp
const std::vector<TestEntry>& batch_report_tests();  // uitest_batch_reports.cpp

// Steps most tests start with (defined in uitest_harness.cpp).
// Scan testdata/input through the UI and land on the populated library.
void scan_library(ImGuiTestContext* ctx);
// Click library row `index`, wait for Song Details, and land on its Paths tab.
void open_details(ImGuiTestContext* ctx, size_t index);
// Fresh app, scan, open chart 0's Preview and wait for the load. False on error.
bool open_preview(ImGuiTestContext* ctx);
```

In `tests/ui/uitest_harness.cpp`, paste `scan_library`, `open_details` and `open_preview` (each with the comment above it, byte for byte) after the end of `escape_ref` and before the file's last line, `}  // namespace uitest`. They go in `namespace uitest` itself, not in an anonymous namespace. The file already includes everything they use (`ui/app_state.h`, `ui/preview_controller.h`, and the Test Engine through `uitest_harness.h`).

Each area file follows one pattern. `tests/ui/uitest_library.cpp`:

```cpp
#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "ui/app_state.h"
#include "ui/preview_controller.h"

namespace uitest {

namespace {

//>> paste: test_scan, test_difficulty, test_rules_error, test_library_state_per_app, test_view_settings

}  // namespace

const std::vector<TestEntry>& library_tests() {
    static const std::vector<TestEntry> entries = {
        {"scan", test_scan},
        {"difficulty", test_difficulty},
        {"rules-error", test_rules_error},
        {"library-state-per-app", test_library_state_per_app},
        {"view-settings", test_view_settings},
    };
    return entries;
}

}  // namespace uitest
```

`tests/ui/uitest_details.cpp` uses the same shape with these includes and table:

```cpp
#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "core/model.h"
#include "core/stars.h"
#include "ui/app_state.h"
#include "ui/dynamics_load_job.h"
#include "ui/preview_controller.h"

namespace uitest {

namespace {

//>> paste: test_analyze, test_cap_switch, test_backend_limit, test_dynamics, test_dynamics_stored, open_titled, analyze_open_song, test_stars, test_squeezed_out_uncounted, test_details_close_teardown

}  // namespace

const std::vector<TestEntry>& details_tests() {
    static const std::vector<TestEntry> entries = {
        {"analyze", test_analyze},
        {"cap-switch", test_cap_switch},
        {"backend-limit", test_backend_limit},
        {"dynamics", test_dynamics},
        {"dynamics-stored", test_dynamics_stored},
        {"stars", test_stars},
        {"squeezed_out_uncounted", test_squeezed_out_uncounted},
        {"details-close-teardown", test_details_close_teardown},
    };
    return entries;
}

}  // namespace uitest
```

`tests/ui/uitest_preview.cpp`:

```cpp
#include <string>
#include <vector>

#include "uitest_harness.h"

#include "app/preview_view.h"
#include "core/model.h"
#include "render/overlay_layout.h"
#include "ui/app_state.h"
#include "ui/preview_controller.h"
#include "ui/preview_load_job.h"

namespace uitest {

namespace {

//>> paste: test_preview, test_analyze_on_preview, test_preview_path_overlay, test_preview_controls, test_preview_drain_box, DisplayWidth, test_preview_overlay_fit, test_preview_buttons_keys, test_scrub_hold, test_layout_drift

}  // namespace

const std::vector<TestEntry>& preview_tests() {
    static const std::vector<TestEntry> entries = {
        {"preview", test_preview},
        {"analyze-on-preview", test_analyze_on_preview},
        {"preview-path-overlay", test_preview_path_overlay},
        {"preview-controls", test_preview_controls},
        {"preview-drain-box", test_preview_drain_box},
        {"preview-overlay-fit", test_preview_overlay_fit},
        {"preview-buttons-keys", test_preview_buttons_keys},
        {"scrub-hold", test_scrub_hold},
        {"layout-drift", test_layout_drift},
    };
    return entries;
}

}  // namespace uitest
```

`tests/ui/uitest_batch_reports.cpp`:

```cpp
#include <cstdio>
#include <filesystem>
#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "app/report_files.h"
#include "ui/app_state.h"

namespace fs = std::filesystem;

namespace uitest {

namespace {

//>> paste: test_batch_modal_drift, test_settings_and_reports, test_dm_compare_flow, test_report_buttons

}  // namespace

const std::vector<TestEntry>& batch_report_tests() {
    static const std::vector<TestEntry> entries = {
        {"batch-modal-drift", test_batch_modal_drift},
        {"settings-and-reports", test_settings_and_reports},
        {"dm-compare-flow", test_dm_compare_flow},
        {"report-buttons", test_report_buttons},
    };
    return entries;
}

}  // namespace uitest
```

`uitest_tests.cpp` keeps under a tenth of its lines, so write it to `tests/ui/uitest_tests.cpp.new` with the Write tool, then `Move-Item -Force tests/ui/uitest_tests.cpp.new tests/ui/uitest_tests.cpp`. Its first four lines are the base file's header comment, unchanged:

```cpp
// The checked-in GUI tests. Each one starts from a fresh scratch app
// (reset_app), drives the real UI by widget label, and checks both the app's
// state and what is on screen. See docs/agents/ui-testing.md for the label
// cheat-sheet and how to add one.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "uitest_harness.h"

namespace uitest {

void register_tests(Harness& h) {
    // --all runs the tests in the order they are registered, and the ImGui
    // context carries over from one test to the next. So the 26 tests that
    // lived in this one file keep their old order. A test not named here (a
    // later task's new test) runs after them, in its file's order.
    static const char* const kRunOrder[] = {
        "scan", "analyze", "cap-switch", "preview", "difficulty",
        "analyze-on-preview", "preview-path-overlay", "preview-controls",
        "preview-drain-box", "preview-overlay-fit", "preview-buttons-keys",
        "scrub-hold", "layout-drift", "batch-modal-drift", "settings-and-reports",
        "backend-limit", "dynamics", "dynamics-stored", "stars", "rules-error",
        "squeezed_out_uncounted", "details-close-teardown", "library-state-per-app",
        "dm-compare-flow", "report-buttons", "view-settings",
    };
    std::vector<TestEntry> pending;
    for (const std::vector<TestEntry>* area :
         {&library_tests(), &details_tests(), &preview_tests(), &batch_report_tests()})
        pending.insert(pending.end(), area->begin(), area->end());

    std::vector<TestEntry> ordered;
    for (const char* name : kRunOrder) {
        auto it = std::find_if(pending.begin(), pending.end(), [&](const TestEntry& e) {
            return std::strcmp(e.name, name) == 0;
        });
        if (it == pending.end()) {
            // A renamed or deleted test must be renamed here too, or --all
            // would silently skip it.
            std::fprintf(stderr, "hydra_uitest: no test file registers \"%s\"\n", name);
            std::abort();
        }
        ordered.push_back(*it);
        pending.erase(it);
    }
    ordered.insert(ordered.end(), pending.begin(), pending.end());

    for (const TestEntry& e : ordered) {
        ImGuiTest* t = IM_REGISTER_TEST(h.engine, "hydra", e.name);
        t->UserData = &h;
        t->TestFunc = e.fn;
    }
}

}  // namespace uitest
```

In `CMakeLists.txt`, the harness list changes from

```cmake
add_library(hydra_uitest_harness STATIC
    tests/ui/uitest_harness.cpp
    tests/ui/uitest_script.cpp
    tests/ui/uitest_tests.cpp
)
```

to

```cmake
add_library(hydra_uitest_harness STATIC
    tests/ui/uitest_harness.cpp
    tests/ui/uitest_script.cpp
    tests/ui/uitest_tests.cpp
    tests/ui/uitest_library.cpp
    tests/ui/uitest_details.cpp
    tests/ui/uitest_preview.cpp
    tests/ui/uitest_batch_reports.cpp
)
```

Run `& "$env:TEMP\hydra_ui_T1\move_check.ps1" -Part tests`. Expected: `tests: 0 line(s) differ`. The check sets aside only the rewritten parts of `register_tests`: the old `struct Entry` and `entries[]` wrapper and loop header on the old side, and the new `register_tests` body plus each area file's three wrapper lines on the new side. Read the new `register_tests` by eye against the block above. Commit the seven test files (`uitest_tests.cpp`, the four area files, `uitest_harness.h`, `uitest_harness.cpp`) and `CMakeLists.txt` with the message `Split the GUI tests into one file per area`, a body saying "Code moves only. scan_library, open_details and open_preview move to uitest_harness.cpp; register_tests keeps the old run order.", and the four trailer lines.

- [ ] **Step 6: Build everything and prove nothing changed.**

Full build with the Bash tool (no `-Target` builds Hydra, hydra_tests and hydra_uitest):

```bash
cd /c/Users/Patrick/Downloads/Hydra/wt-ui-T1 && powershell -ExecutionPolicy Bypass -File build_cpp.ps1 > build.log 2>&1; grep -c " error " build.log; tail -2 build.log
```

Expected: `0` errors. Then, with the PowerShell tool:

```powershell
$out = "$env:TEMP\hydra_ui_T1"
foreach ($p in 'details', 'library', 'tests') { & "$out\move_check.ps1" -Part $p }
.\build-cpp\Release\hydra_tests.exe | Select-Object -Last 3 | Set-Content "$out\unit_after.txt"
Compare-Object (Get-Content "$out\unit_before.txt") (Get-Content "$out\unit_after.txt")
Compare-Object (Get-Content "$out\list_before.txt") (.\build-cpp\Release\hydra_uitest.exe --list) -SyncWindow 0
.\build-cpp\Release\hydra_uitest.exe --all 2>&1 | Set-Content "$out\all_after.txt"; "exit $LASTEXITCODE"
Select-String '^\[(PASS|FAIL)\]' "$out\all_after.txt" | Group-Object { $_.Line.Substring(0,6) } | Select-Object Name, Count
git diff --stat "$(Get-Content "$out\base.txt")..HEAD" | Select-Object -Last 1
```

Expected, in order: the three `0 line(s) differ` lines; nothing from either `Compare-Object` (the unit summary and the 26 names in their order are unchanged; `-SyncWindow 0` makes the list comparison order-sensitive); `exit 0`; `[PASS] 26` and no `[FAIL]` group; and `20 files changed`. Nothing needs committing in this step.

---

### Task 2: The search query module

Today the library search is one SQL `LIKE '%text%'` over title, artist and charter (`RecordStore::chart_library_count` and `chart_library_page`). SQLite's `LIKE` ignores case only for A to Z, so "beyonce" can't find "Beyoncé". The whole box is one substring, so "green burnout" finds nothing, because no single field holds both words. The folder isn't searched at all. A charter stored as `<color=#e02222>Blood</color>line` is only found by typing the tag. This task writes the search rules as a small C++ module with no UI in it: how text is folded for matching, how rich-text tags are removed, how a typed query is read, whether a row matches, and which bytes of a shown string to highlight. Nothing changes on screen yet. T12 wires it into the library table and T9 uses `strip_rich_tags` for the panel header.

**Wave:** 1. **Depends on:** Task 0 only. **Expected overlaps:** T1 in `CMakeLists.txt`. T1 edits the `hydra_ui` and `hydra_uitest_harness` source lists; this task adds one line to `hydra_core` and one to `hydra_tests`. Keep both.

**Goal:** `src/app/library_query.{h,cpp}` implement the fixed T2 interface from the frame exactly, and 17 doctest cases pin its behaviour.

**Files:**
- Create: `src/app/library_query.h`
- Create: `src/app/library_query.cpp`
- Create: `tests/test_library_query.cpp`
- Modify: `CMakeLists.txt` (add `src/app/library_query.cpp` to `hydra_core` after `src/app/display_format.cpp`, and `tests/test_library_query.cpp` to `hydra_tests` after `tests/test_strutil.cpp`)

#### How the rules work

Folding makes two strings comparable. It lowercases A to Z. It turns every letter of Latin-1 Supplement and Latin Extended-A (U+00C0 to U+017F) into plain ASCII: "é" becomes "e", "ß" becomes "ss", "Æ" becomes "ae", "Ł" becomes "l". The two signs in that range, × and ÷, stay as they are. Full-width ASCII (U+FF01 to U+FF5E, the wide letters Japanese text uses) becomes normal ASCII, so "ＢＵＲＮＯＵＴ" becomes "burnout". Every run of whitespace becomes one space. That covers tabs, the no-break space (U+00A0) and the ideographic space (U+3000, the full-width space). Everything else passes through unchanged, so Japanese titles are kept as they are.

Invalid UTF-8 passes through byte for byte. A lead byte with no continuation, a stray `0xFF`, or a sequence cut off at the end is copied as it is and never throws. The fold only rewrites byte sequences it fully recognizes. Nothing is decoded that it can't re-encode, so it never loses a byte.

Tag stripping removes Clone Hero's rich-text tags, in any letter case: `<color=...>`, `<size=...>`, and the open and close forms of `color`, `size`, `b`, `i`, `u`, `s`, `sub` and `sup`. `<color>` and `<size>` without a value are not tags and are kept. So is anything else in angle brackets. That matters because the scanner writes `<unknown artist>` and `<unknown charter>` for charts with no metadata (`src/app/analysis.cpp`), and those must still read and search as they do today. A tag that never closes, such as `<color=#fff`, is kept as text.

A query is read left to right. Words split on spaces, and every word must match somewhere in the row, in any order. A word matches when its folded text appears anywhere inside a folded field, the same substring rule the SQL search uses today. Text in double quotes is one phrase. It must appear whole, spaces included, inside one field. A quote that is never closed runs to the end of the box.

`title:`, `artist:`, `charter:` and `folder:` limit the word or quoted phrase right after the colon to that one field. `artist:"green day"` works. The field names ignore case. A field name with nothing after it (`artist:` while the user is still typing) is dropped, so the list doesn't empty out mid-word. Any other `name:value` is an ordinary word, so `genre:rock` searches for the text "genre:rock".

`stars:N` keeps rows whose best path has exactly N stars, N from 0 to 7 (`kMaxStars` from `core/stars.h`). `squeeze<=N` keeps rows whose best path's hardest squeeze is at most N milliseconds. `squeeze<N` means the same thing. N may be negative or have decimals. The hardest squeeze is the path's "Difficulty" in CONTEXT.md: the largest raw gap in ms any of its activations needs, so a bigger number is harder, just as with the Path limit. A path with no squeeze at all passes any squeeze limit, because it is the easiest a path can be. If a filter appears twice, the last one counts.

A bad filter value adds one plain sentence to `errors` and filters nothing. The sentences are `stars: needs a number from 0 to 7` and `squeeze<= needs a number of milliseconds, like squeeze<=20`. The same sentence is never listed twice. The rest of the query still works, so `burnout stars:9` still finds Burnout and shows the error.

A row counts as analyzed when its `RowFacts::stars` holds a value. T7 stores a star count for every result that has a path, so an analyzed row always has one. A `stars:` or `squeeze` filter never matches a row with no facts. The caller decides which rows get facts. That choice is T12's: facts are passed only for Ready rows.

Highlighting works on the text as shown: unfolded, with tags already stripped. `match_spans` folds that text and remembers, for every folded byte, which bytes of the shown text it came from. So a match on "beyonce" lights up all 8 bytes of "Beyoncé", including both bytes of "é". A term limited to one field only lights up that field. Passing `QueryField::Any` lights up every term. Spans that overlap or touch merge into one.

`query_matches` runs for every row on every keystroke, up to 20,000 rows. It allocates nothing: the row's fields and the query's terms are folded once, beforehand, and the test is `std::string::find`. The filters are checked before the words, since they cost less. A unit test holds 20,000 rows under 20 ms.

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="library query*"` reports 17 test cases, all passed, and `Status: SUCCESS!`.
- [ ] `library query: an accented name is found without its accents` passes: "beyonce" finds artist "Beyoncé".
- [ ] `library query: highlight spans cover the displayed bytes` passes: the span for "beyonce" in `Halo (Beyoncé cover)` is bytes 6 to 14, all of "Beyoncé".
- [ ] `library query: a tagged charter is found by its plain name` passes: `<color=#e02222>Blood</color>line` becomes `Bloodline` and "bloodline" finds it.
- [ ] `library query: a quoted phrase must appear whole in one field` passes: `"tier 4"` matches folder `common\Summer Blast _25 Setlist\Tier 4` and not `common\IB24\T4`.
- [ ] `library query: matching 20000 rows takes well under a frame` passes (under 20 ms).
- [ ] The whole `hydra_tests.exe` suite still reports `Status: SUCCESS!`.
- [ ] `git diff --stat <base>..ui/T2` lists exactly the four files above.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="library query*"` → `[doctest] test cases: 17 | 17 passed | 0 failed | 0 skipped` and `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests.** Create `tests/test_library_query.cpp`. Burnout, Deadbolt and Acid Romance are real charts from `testdata\input`, with their stored title, artist and charter. Their folder is the pack folders above the song's own folder. The other rows are made up and are labelled so.

```cpp
// Unit tests for app/library_query: how the library search folds text,
// strips Clone Hero's rich-text tags, reads a query, matches a row and marks
// what to highlight. Burnout, Deadbolt and Acid Romance use their real
// metadata from testdata\input; every other row is made up.

#include "doctest.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/library_query.h"

using namespace hydra::app;

namespace {

bool matches(std::string_view query, const SearchableRow& row, const RowFacts& facts = {}) {
    return query_matches(parse_library_query(query), row, facts);
}

// The facts of an analyzed row: its best path's stars and hardest squeeze.
RowFacts analyzed(int stars, std::optional<double> hardest_ms) {
    RowFacts facts;
    facts.stars = stars;
    facts.hardest_ms = hardest_ms;
    return facts;
}

SearchableRow burnout() {
    return make_searchable("Burnout", "Green Day", "Hoph2o",
                           "common\\Summer Blast _25 Setlist\\Tier 4");
}

SearchableRow deadbolt() {
    return make_searchable("Deadbolt", "Thrice", "Hoph2o", "common\\IB24\\T3");
}

SearchableRow acid_romance() {
    return make_searchable("Acid Romance", "Alpha Wolf", "<color=#e02222>Blood</color>line",
                           "common\\IB24\\T3");
}

bool spans_equal(const std::vector<MatchSpan>& got, const std::vector<MatchSpan>& want) {
    return std::equal(got.begin(), got.end(), want.begin(), want.end(),
                      [](const MatchSpan& a, const MatchSpan& b) {
                          return a.begin == b.begin && a.end == b.end;
                      });
}

}  // namespace

TEST_CASE("library query: folding lowercases and removes accents") {
    CHECK(fold_for_search("Beyoncé") == "beyonce");
    CHECK(fold_for_search("ÀÉÎÕÜ Ñ Ç Ý Ÿ") == "aeiou n c y y");
    CHECK(fold_for_search("Straße") == "strasse");
    CHECK(fold_for_search("Æther Œuvre Þorn") == "aether oeuvre thorn");
    CHECK(fold_for_search("Łódź Škoda Ğİı") == "lodz skoda gii");
    CHECK(fold_for_search("a × b ÷ c") == "a × b ÷ c");  // signs, not letters
}

TEST_CASE("library query: folding turns full-width ASCII into ASCII") {
    CHECK(fold_for_search("ＢＵＲＮＯＵＴ！") == "burnout!");
    // Full-width "tier", the ideographic space U+3000, full-width "4".
    CHECK(fold_for_search("ｔｉｅｒ" "\xE3\x80\x80" "４") == "tier 4");
}

TEST_CASE("library query: folding collapses whitespace and keeps other scripts") {
    CHECK(fold_for_search("Tier  4\t\tSong") == "tier 4 song");
    CHECK(fold_for_search("a" "\xC2\xA0\xC2\xA0" "b") == "a b");  // two no-break spaces
    CHECK(fold_for_search("東京事変") == "東京事変");
    CHECK(fold_for_search("") == "");
}

TEST_CASE("library query: invalid UTF-8 passes through byte for byte") {
    CHECK(fold_for_search("A" "\xC3" "(B") == "a" "\xC3" "(b");  // lead byte, no continuation
    CHECK(fold_for_search("\xFF\xFE") == "\xFF\xFE");
    CHECK(fold_for_search("x" "\xC3") == "x" "\xC3");  // cut off at the end
    CHECK(fold_for_search("\xEF\xBC") == "\xEF\xBC");  // half a full-width letter
}

TEST_CASE("library query: rich-text tags are stripped and other angle brackets kept") {
    CHECK(strip_rich_tags("<color=#e02222>Blood</color>line") == "Bloodline");
    CHECK(strip_rich_tags("<COLOR=red>Loud</Color>") == "Loud");
    CHECK(strip_rich_tags("<b>Bold</b> <i>it</i> <u>u</u> <s>s</s>") == "Bold it u s");
    CHECK(strip_rich_tags("<size=20>big</size> H<sub>2</sub>O x<sup>2</sup>") == "big H2O x2");
    CHECK(strip_rich_tags("<unknown artist>") == "<unknown artist>");
    CHECK(strip_rich_tags("a < b > c") == "a < b > c");
    CHECK(strip_rich_tags("<color=#fff") == "<color=#fff");  // never closed
    CHECK(strip_rich_tags("") == "");
}

TEST_CASE("library query: a tagged charter is found by its plain name") {
    const SearchableRow row = acid_romance();
    CHECK(row.charter == "bloodline");
    CHECK(matches("bloodline", row));
    CHECK(matches("charter:blood", row));
    CHECK_FALSE(matches("e02222", row));  // the tag's text is gone
    CHECK_FALSE(matches("color", row));
}

TEST_CASE("library query: an accented name is found without its accents") {
    const SearchableRow row = make_searchable("Halo", "Beyoncé", "Someone", "pack");  // made up
    CHECK(matches("beyonce", row));
    CHECK(matches("BEYONCE", row));
    CHECK(matches("beyoncé", row));
}

TEST_CASE("library query: words match in any order and across fields") {
    const SearchableRow row = burnout();
    CHECK(matches("green burnout", row));
    CHECK(matches("burnout green", row));
    CHECK(matches("  Green   BURNOUT  ", row));
    CHECK(matches("hoph2o tier", row));  // charter and folder
    CHECK_FALSE(matches("green deadbolt", row));
    CHECK_FALSE(matches("thrice burnout", row));
}

TEST_CASE("library query: a quoted phrase must appear whole in one field") {
    const LibraryQuery q = parse_library_query("\"tier 4\"");
    REQUIRE(q.terms.size() == 1);
    CHECK(q.terms[0].phrase);
    CHECK(q.terms[0].field == QueryField::Any);
    CHECK(q.terms[0].folded == "tier 4");

    const RowFacts none;
    CHECK(query_matches(q, burnout(), none));
    CHECK_FALSE(query_matches(q, make_searchable("Song", "Band", "Charter", "common\\IB24\\T4"),
                              none));  // made up

    // As two words, "tier" and "4" may sit apart. As a phrase they may not.
    const SearchableRow apart = make_searchable("Song 4", "Band", "Charter", "Pack\\Tier 1");  // made up
    CHECK(matches("tier 4", apart));
    CHECK_FALSE(matches("\"tier 4\"", apart));
    // A phrase can't be split across two fields either.
    CHECK_FALSE(matches("\"burnout green\"", burnout()));
    // A quote that is never closed runs to the end.
    CHECK(matches("\"summer blast", burnout()));
}

TEST_CASE("library query: artist: charter: folder: and title: limit a term to one field") {
    CHECK(matches("artist:thrice", deadbolt()));
    CHECK_FALSE(matches("artist:thrice", burnout()));
    CHECK_FALSE(matches("title:thrice", deadbolt()));
    CHECK(matches("charter:hoph2o", burnout()));
    CHECK(matches("charter:hoph2o", deadbolt()));
    CHECK_FALSE(matches("charter:hoph2o", acid_romance()));
    CHECK(matches("folder:ib24", deadbolt()));
    CHECK_FALSE(matches("folder:ib24", burnout()));
    CHECK(matches("title:burnout", burnout()));
    CHECK(matches("Artist:\"green day\"", burnout()));
    CHECK_FALSE(matches("title:\"green day\"", burnout()));

    const LibraryQuery q = parse_library_query("ARTIST:\"Green Day\" folder:tier");
    REQUIRE(q.terms.size() == 2);
    CHECK(q.terms[0].field == QueryField::Artist);
    CHECK(q.terms[0].folded == "green day");
    CHECK(q.terms[0].phrase);
    CHECK(q.terms[1].field == QueryField::Folder);
    CHECK(q.terms[1].folded == "tier");
    CHECK_FALSE(q.terms[1].phrase);
}

TEST_CASE("library query: unknown field names are words and empty field values are dropped") {
    const LibraryQuery unknown = parse_library_query("genre:rock");
    REQUIRE(unknown.terms.size() == 1);
    CHECK(unknown.terms[0].field == QueryField::Any);
    CHECK(unknown.terms[0].folded == "genre:rock");
    CHECK(unknown.errors.empty());

    // A field name with nothing after it is dropped, so the list doesn't
    // empty out while the user is still typing.
    const LibraryQuery typing = parse_library_query("artist:");
    CHECK(typing.terms.empty());
    CHECK(typing.empty());
}

TEST_CASE("library query: stars:N keeps analyzed rows with exactly N stars") {
    const LibraryQuery q = parse_library_query("stars:7");
    REQUIRE(q.stars.has_value());
    CHECK(*q.stars == 7);
    CHECK(q.errors.empty());
    CHECK(query_matches(q, burnout(), analyzed(7, 163.0)));
    CHECK_FALSE(query_matches(q, burnout(), analyzed(6, 163.0)));
    CHECK_FALSE(query_matches(q, burnout(), RowFacts{}));  // not analyzed
    CHECK(matches("stars:0", burnout(), analyzed(0, std::nullopt)));
    CHECK(matches("green stars:7", burnout(), analyzed(7, 163.0)));
    CHECK_FALSE(matches("thrice stars:7", burnout(), analyzed(7, 163.0)));
}

TEST_CASE("library query: squeeze<=N keeps analyzed rows whose hardest squeeze is at most N ms") {
    const LibraryQuery q = parse_library_query("squeeze<=20");
    REQUIRE(q.squeeze_max_ms.has_value());
    CHECK(*q.squeeze_max_ms == doctest::Approx(20.0));
    CHECK(query_matches(q, burnout(), analyzed(7, 12.5)));
    CHECK(query_matches(q, burnout(), analyzed(7, 20.0)));
    CHECK_FALSE(query_matches(q, burnout(), analyzed(7, 163.0)));   // Burnout's real 163.0 ms
    CHECK(query_matches(q, burnout(), analyzed(7, std::nullopt)));  // no squeeze at all
    CHECK_FALSE(query_matches(q, burnout(), RowFacts{}));           // not analyzed

    CHECK(matches("squeeze<=163", burnout(), analyzed(7, 163.0)));
    CHECK(matches("squeeze<20", burnout(), analyzed(7, 20.0)));  // < reads as <=
    CHECK(matches("SQUEEZE<=20", burnout(), analyzed(7, 20.0)));
    CHECK(matches("squeeze<=-5", burnout(), analyzed(7, -12.0)));
    CHECK(matches("squeeze<=2.5", burnout(), analyzed(7, 2.5)));
}

TEST_CASE("library query: bad filter values give a plain error and filter nothing") {
    const LibraryQuery stars = parse_library_query("burnout stars:9");
    CHECK_FALSE(stars.stars.has_value());
    REQUIRE(stars.errors.size() == 1);
    CHECK(stars.errors[0] == "stars: needs a number from 0 to 7");
    REQUIRE(stars.terms.size() == 1);  // the rest of the query still works
    CHECK(stars.terms[0].folded == "burnout");
    CHECK(query_matches(stars, burnout(), RowFacts{}));

    CHECK(parse_library_query("stars:x").errors ==
          std::vector<std::string>{"stars: needs a number from 0 to 7"});
    CHECK(parse_library_query("stars:-1").errors.size() == 1);
    CHECK(parse_library_query("stars:").errors.size() == 1);

    const LibraryQuery squeeze = parse_library_query("squeeze<=abc");
    CHECK_FALSE(squeeze.squeeze_max_ms.has_value());
    REQUIRE(squeeze.errors.size() == 1);
    CHECK(squeeze.errors[0] == "squeeze<= needs a number of milliseconds, like squeeze<=20");
    CHECK(parse_library_query("squeeze<=").errors.size() == 1);
    CHECK(parse_library_query("squeeze<=20ms").errors.size() == 1);

    // The same mistake twice is reported once.
    CHECK(parse_library_query("stars:9 stars:10").errors.size() == 1);
}

TEST_CASE("library query: an empty query matches every row") {
    for (std::string_view text : {"", "   ", "\"\"", "\" \"", "title:"}) {
        const LibraryQuery q = parse_library_query(text);
        CHECK(q.empty());
        CHECK(q.errors.empty());
        CHECK(query_matches(q, burnout(), RowFacts{}));
        CHECK(query_matches(q, acid_romance(), analyzed(3, 40.0)));
    }
    CHECK_FALSE(parse_library_query("x").empty());
    CHECK_FALSE(parse_library_query("stars:7").empty());
    CHECK_FALSE(parse_library_query("squeeze<=20").empty());
}

TEST_CASE("library query: highlight spans cover the displayed bytes") {
    // "é" is two bytes, so the span runs to byte 14, not 13.
    const std::string title = "Halo (Beyoncé cover)";  // made up
    CHECK(spans_equal(match_spans(parse_library_query("beyonce"), QueryField::Title, title),
                      std::vector<MatchSpan>{MatchSpan{6, 14}}));
    CHECK(title.substr(6, 8) == "Beyoncé");

    // The caller passes the tag-stripped charter, as the table shows it.
    const std::string charter = strip_rich_tags("<color=#e02222>Blood</color>line");
    CHECK(spans_equal(match_spans(parse_library_query("bloodline"), QueryField::Charter, charter),
                      std::vector<MatchSpan>{MatchSpan{0, 9}}));

    // A phrase in the folder, despite the capital T.
    const std::string folder = "common\\Summer Blast _25 Setlist\\Tier 4";
    CHECK(spans_equal(match_spans(parse_library_query("\"tier 4\""), QueryField::Folder, folder),
                      std::vector<MatchSpan>{MatchSpan{32, 38}}));

    // Two spaces fold to one, and the span still covers both.
    CHECK(spans_equal(match_spans(parse_library_query("\"tier 4\""), QueryField::Folder, "Tier  4"),
                      std::vector<MatchSpan>{MatchSpan{0, 7}}));

    // Every occurrence lights up.
    CHECK(spans_equal(match_spans(parse_library_query("tier"), QueryField::Folder, "Tier 1\\Tier 2"),
                      std::vector<MatchSpan>{MatchSpan{0, 4}, MatchSpan{7, 11}}));

    // Touching matches merge. A field-limited term lights up only its field.
    const LibraryQuery words = parse_library_query("burn out artist:green");
    CHECK(spans_equal(match_spans(words, QueryField::Title, "Burnout"),
                      std::vector<MatchSpan>{MatchSpan{0, 7}}));
    CHECK(spans_equal(match_spans(words, QueryField::Artist, "Green Day"),
                      std::vector<MatchSpan>{MatchSpan{0, 5}}));
    CHECK(match_spans(parse_library_query("artist:green"), QueryField::Title, "Green Light").empty());
}

TEST_CASE("library query: matching 20000 rows takes well under a frame") {
    std::vector<SearchableRow> rows;  // made up, library-shaped
    rows.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
        rows.push_back(make_searchable(
            "Song number " + std::to_string(i), "Artist " + std::to_string(i % 500),
            "Charter " + std::to_string(i % 50),
            "common\\Pack " + std::to_string(i % 40) + "\\Tier " + std::to_string(i % 8)));
    }
    const LibraryQuery q = parse_library_query("artist 12 \"tier 3\"");
    const RowFacts none;

    // Best of three, so a busy machine doesn't fail the test.
    double best_ms = 1e9;
    size_t hits = 0;
    for (int run = 0; run < 3; ++run) {
        const auto start = std::chrono::steady_clock::now();
        hits = 0;
        for (const SearchableRow& row : rows)
            if (query_matches(q, row, none)) ++hits;
        const std::chrono::duration<double, std::milli> took =
            std::chrono::steady_clock::now() - start;
        best_ms = std::min(best_ms, took.count());
    }
    CHECK(hits > 0);
    CHECK(best_ms < 20.0);
}
```

Then add the test file to `hydra_tests` in `CMakeLists.txt`. Before:

```cmake
    tests/test_main.cpp
    tests/test_strutil.cpp
    tests/test_corpus_cache.cpp
```

After:

```cmake
    tests/test_main.cpp
    tests/test_strutil.cpp
    tests/test_library_query.cpp
    tests/test_corpus_cache.cpp
```

- [ ] **Step 2: Run them and watch them fail.** From the worktree root, run `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1` with the Bash tool. Expected: the build fails, and `build.log` shows `fatal error C1083: Cannot open include file: 'app/library_query.h'`.

- [ ] **Step 3: Write the header.** Create `src/app/library_query.h`. The declarations are the frame's fixed T2 interface, word for word, with longer comments.

```cpp
// The library search box's rules, in one place: how text is folded for
// matching (case, accents, full-width letters, whitespace), how Clone Hero's
// rich-text tags are removed, how a typed query is read, whether a row
// matches it, and which bytes of a shown string to highlight. Pure functions
// with no ImGui and no store, so the library table and the song panel share
// them and the unit tests pin them.

#ifndef HYDRA_APP_LIBRARY_QUERY_H
#define HYDRA_APP_LIBRARY_QUERY_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hydra::app {

// Lowercase, accents removed (NFD-style fold for Latin-1 and Latin Extended-A:
// "é" -> "e", "ß" -> "ss"), full-width ASCII folded to ASCII, runs of
// whitespace collapsed to one space. Non-Latin text (Japanese, etc.) passes
// through unchanged apart from full-width folding. Input and output are UTF-8.
// Whitespace includes the no-break space (U+00A0) and the ideographic space
// (U+3000). The signs × and ÷ are kept. Invalid UTF-8 passes through byte for
// byte; nothing is dropped and nothing throws.
std::string fold_for_search(std::string_view text);

// Removes Clone Hero rich-text tags: <color=...>, </color>, <b>, </b>, <i>,
// </i>, <size=...>, </size>, <u>, </u>, <s>, </s>, <sub>, </sub>, <sup>,
// </sup>, case-insensitive. Anything else in angle brackets is kept, including
// <color> with no value, a tag that never closes, and "<unknown artist>".
std::string strip_rich_tags(std::string_view text);

enum class QueryField { Any, Title, Artist, Charter, Folder };

struct QueryTerm {
    QueryField field = QueryField::Any;
    std::string folded;   // fold_for_search of the word or quoted phrase
    bool phrase = false;  // true when it came from "quotes"
};

struct LibraryQuery {
    std::vector<QueryTerm> terms;         // every term must match
    std::optional<int> stars;             // stars:N, N in 0..7
    std::optional<double> squeeze_max_ms; // squeeze<=N (also squeeze<N treated as <=)
    std::vector<std::string> errors;      // e.g. "stars: needs a number from 0 to 7"
    bool empty() const;                   // no terms and no filters
};

// Parses what the user typed. Words match in any order. "quoted text" is one
// phrase. artist:x, charter:x, folder:x, title:x limit a word or "phrase" to
// one field. stars:N and squeeze<=N filter on the stored best path. Unknown
// field names are treated as plain words.
// A word or phrase matches when its folded text appears anywhere in a folded
// field. An unclosed quote runs to the end. A field name with nothing after it
// is dropped. A bad filter value adds one sentence to `errors` and filters
// nothing. When a filter appears twice, the last one counts.
LibraryQuery parse_library_query(std::string_view text);

// The fields one library row offers to a query, already folded.
struct SearchableRow {
    std::string title, artist, charter, folder;  // fold_for_search(strip_rich_tags(x))
};
SearchableRow make_searchable(std::string_view title, std::string_view artist,
                              std::string_view charter, std::string_view folder);

// The best path's stored facts a filter can test; nullopt when not analyzed.
// A row counts as analyzed when `stars` holds a value.
struct RowFacts {
    std::optional<int> stars;
    std::optional<double> hardest_ms;  // nullopt = no squeeze on the path
};

// True when every term matches its field (or any field) and every filter holds.
// A stars: or squeeze filter never matches a row with no facts.
// A path with no squeeze passes any squeeze limit. Allocates nothing.
bool query_matches(const LibraryQuery& q, const SearchableRow& row, const RowFacts& facts);

// Where the query's terms appear in one displayed (unfolded, tag-stripped)
// string, as byte ranges, for highlighting. Ranges are sorted and don't overlap.
// Only terms for `field` or for any field count; QueryField::Any counts every
// term. A match covers every byte of each character it touches, so "beyonce"
// in "Beyoncé" covers both bytes of "é". Touching ranges merge.
struct MatchSpan { size_t begin = 0, end = 0; };
std::vector<MatchSpan> match_spans(const LibraryQuery& q, QueryField field,
                                   std::string_view display_text);

}  // namespace hydra::app

#endif  // HYDRA_APP_LIBRARY_QUERY_H
```

- [ ] **Step 4: Write the implementation.** Create `src/app/library_query.cpp`. The two tables list the ASCII for each letter, eight characters per row, with the characters in the comment so a reviewer can check each one.

```cpp
// Library search: folding, rich-tag stripping, the query language, the
// matcher and the highlight spans. library_query.h describes the rules.

#include "app/library_query.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

#include "core/stars.h"  // kMaxStars

namespace hydra::app {

namespace {

constexpr size_t npos = std::string_view::npos;

constexpr const char* kStarsError = "stars: needs a number from 0 to 7";
constexpr const char* kSqueezeError =
    "squeeze<= needs a number of milliseconds, like squeeze<=20";

// ---- bytes -----------------------------------------------------------------

// The byte at s[i], or 0 past the end. 0 is never a continuation byte, so a
// cut-off sequence simply fails to match.
unsigned char byte_at(std::string_view s, size_t i) {
    return i < s.size() ? static_cast<unsigned char>(s[i]) : 0;
}

bool is_ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool is_ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_continuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

char ascii_lower(unsigned char c) {
    return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
}

bool iequals_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (ascii_lower(static_cast<unsigned char>(a[i])) !=
            ascii_lower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals_ascii(s.substr(0, prefix.size()), prefix);
}

// ---- folding ---------------------------------------------------------------

// ASCII for U+00C0..U+00FF. nullptr keeps the character (the two signs).
constexpr const char* kLatin1[64] = {
    "a", "a", "a", "a", "a", "a", "ae", "c",     // C0  À Á Â Ã Ä Å Æ Ç
    "e", "e", "e", "e", "i", "i", "i",  "i",     // C8  È É Ê Ë Ì Í Î Ï
    "d", "n", "o", "o", "o", "o", "o",  nullptr, // D0  Ð Ñ Ò Ó Ô Õ Ö ×
    "o", "u", "u", "u", "u", "y", "th", "ss",    // D8  Ø Ù Ú Û Ü Ý Þ ß
    "a", "a", "a", "a", "a", "a", "ae", "c",     // E0  à á â ã ä å æ ç
    "e", "e", "e", "e", "i", "i", "i",  "i",     // E8  è é ê ë ì í î ï
    "d", "n", "o", "o", "o", "o", "o",  nullptr, // F0  ð ñ ò ó ô õ ö ÷
    "o", "u", "u", "u", "u", "y", "th", "y",     // F8  ø ù ú û ü ý þ ÿ
};

// ASCII for U+0100..U+017F, Latin Extended-A. Every entry is a letter.
constexpr const char* kLatinExtA[128] = {
    "a", "a", "a",  "a",  "a", "a", "c", "c",  // 0100  Ā ā Ă ă Ą ą Ć ć
    "c", "c", "c",  "c",  "c", "c", "d", "d",  // 0108  Ĉ ĉ Ċ ċ Č č Ď ď
    "d", "d", "e",  "e",  "e", "e", "e", "e",  // 0110  Đ đ Ē ē Ĕ ĕ Ė ė
    "e", "e", "e",  "e",  "g", "g", "g", "g",  // 0118  Ę ę Ě ě Ĝ ĝ Ğ ğ
    "g", "g", "g",  "g",  "h", "h", "h", "h",  // 0120  Ġ ġ Ģ ģ Ĥ ĥ Ħ ħ
    "i", "i", "i",  "i",  "i", "i", "i", "i",  // 0128  Ĩ ĩ Ī ī Ĭ ĭ Į į
    "i", "i", "ij", "ij", "j", "j", "k", "k",  // 0130  İ ı Ĳ ĳ Ĵ ĵ Ķ ķ
    "k", "l", "l",  "l",  "l", "l", "l", "l",  // 0138  ĸ Ĺ ĺ Ļ ļ Ľ ľ Ŀ
    "l", "l", "l",  "n",  "n", "n", "n", "n",  // 0140  ŀ Ł ł Ń ń Ņ ņ Ň
    "n", "n", "n",  "n",  "o", "o", "o", "o",  // 0148  ň ŉ Ŋ ŋ Ō ō Ŏ ŏ
    "o", "o", "oe", "oe", "r", "r", "r", "r",  // 0150  Ő ő Œ œ Ŕ ŕ Ŗ ŗ
    "r", "r", "s",  "s",  "s", "s", "s", "s",  // 0158  Ř ř Ś ś Ŝ ŝ Ş ş
    "s", "s", "t",  "t",  "t", "t", "t", "t",  // 0160  Š š Ţ ţ Ť ť Ŧ ŧ
    "u", "u", "u",  "u",  "u", "u", "u", "u",  // 0168  Ũ ũ Ū ū Ŭ ŭ Ů ů
    "u", "u", "u",  "u",  "w", "w", "y", "y",  // 0170  Ű ű Ų ų Ŵ ŵ Ŷ ŷ
    "y", "z", "z",  "z",  "z", "z", "z", "s",  // 0178  Ÿ Ź ź Ż ż Ž ž ſ
};

// The shown-text bytes one folded byte came from: the whole character, or the
// whole whitespace run, that produced it.
struct SourceRange {
    size_t begin = 0;
    size_t end = 0;
};

// Folds `text` into `out`. When `map` is given, it gets one entry per byte of
// `out`, so a match in the folded text can be traced back to the shown text.
void fold_into(std::string_view text, std::string& out, std::vector<SourceRange>* map) {
    out.clear();
    out.reserve(text.size());
    if (map) {
        map->clear();
        map->reserve(text.size());
    }
    auto emit = [&](std::string_view piece, size_t begin, size_t end) {
        out.append(piece);
        if (map)
            for (size_t k = 0; k < piece.size(); ++k) map->push_back(SourceRange{begin, end});
    };

    bool in_space = false;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c0 = byte_at(text, i);
        const unsigned char c1 = byte_at(text, i + 1);
        const unsigned char c2 = byte_at(text, i + 2);

        // Whitespace: ASCII, the no-break space (C2 A0) and the ideographic
        // space (E3 80 80). A run of any mix becomes one space.
        size_t space_len = 0;
        if (is_ascii_space(c0)) space_len = 1;
        else if (c0 == 0xC2 && c1 == 0xA0) space_len = 2;
        else if (c0 == 0xE3 && c1 == 0x80 && c2 == 0x80) space_len = 3;
        if (space_len > 0) {
            if (!in_space) emit(" ", i, i + space_len);
            else if (map) map->back().end = i + space_len;
            in_space = true;
            i += space_len;
            continue;
        }
        in_space = false;

        if (c0 < 0x80) {
            const char lower = ascii_lower(c0);
            emit(std::string_view(&lower, 1), i, i + 1);
            i += 1;
            continue;
        }

        // U+00C0..U+017F are the two-byte sequences C3 80 to C5 BF.
        if (c0 >= 0xC3 && c0 <= 0xC5 && is_continuation(c1)) {
            const unsigned cp = ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
            const char* ascii = cp <= 0xFF ? kLatin1[cp - 0xC0] : kLatinExtA[cp - 0x100];
            if (ascii) emit(ascii, i, i + 2);
            else emit(text.substr(i, 2), i, i + 2);
            i += 2;
            continue;
        }

        // Full-width ASCII, U+FF01..U+FF5E, is EF BC 81 to EF BD 9E. Its ASCII
        // twin is 0xFEE0 lower.
        if (c0 == 0xEF && (c1 == 0xBC || c1 == 0xBD) && is_continuation(c2)) {
            const unsigned cp = 0xF000u | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
            if (cp >= 0xFF01 && cp <= 0xFF5E) {
                const char lower = ascii_lower(static_cast<unsigned char>(cp - 0xFEE0));
                emit(std::string_view(&lower, 1), i, i + 3);
                i += 3;
                continue;
            }
        }

        // Anything else is kept: the whole character when the bytes form a
        // well-shaped UTF-8 sequence, otherwise this one byte as it is.
        size_t len = 1;
        if (c0 >= 0xC2 && c0 <= 0xDF) len = 2;
        else if (c0 >= 0xE0 && c0 <= 0xEF) len = 3;
        else if (c0 >= 0xF0 && c0 <= 0xF4) len = 4;
        for (size_t k = 1; k < len; ++k)
            if (!is_continuation(byte_at(text, i + k))) len = 1;
        emit(text.substr(i, len), i, i + len);
        i += len;
    }
}

// ---- rich-text tags --------------------------------------------------------

struct RichTag {
    std::string_view name;
    bool takes_value;  // the opening tag is <name=value>
};

constexpr RichTag kRichTags[] = {
    {"color", true}, {"size", true}, {"b", false},   {"i", false},
    {"u", false},    {"s", false},   {"sub", false}, {"sup", false},
};

// The byte length of the rich-text tag that starts at text[at] (a '<'), or 0
// when the text there is not one strip_rich_tags removes.
size_t rich_tag_length(std::string_view text, size_t at) {
    size_t i = at + 1;
    const bool closing = i < text.size() && text[i] == '/';
    if (closing) ++i;
    size_t name_end = i;
    while (name_end < text.size() && is_ascii_alpha(text[name_end])) ++name_end;
    if (name_end == i || name_end >= text.size()) return 0;

    const std::string_view name = text.substr(i, name_end - i);
    for (const RichTag& tag : kRichTags) {
        if (!iequals_ascii(name, tag.name)) continue;
        if (text[name_end] == '>')
            return (closing || !tag.takes_value) ? name_end + 1 - at : 0;
        if (!closing && tag.takes_value && text[name_end] == '=') {
            const size_t close = text.find('>', name_end);
            const size_t reopen = text.find('<', name_end);
            if (close == npos || reopen < close) return 0;
            return close + 1 - at;
        }
        return 0;
    }
    return 0;
}

// ---- parsing ---------------------------------------------------------------

std::optional<QueryField> field_named(std::string_view key) {
    if (iequals_ascii(key, "title")) return QueryField::Title;
    if (iequals_ascii(key, "artist")) return QueryField::Artist;
    if (iequals_ascii(key, "charter")) return QueryField::Charter;
    if (iequals_ascii(key, "folder")) return QueryField::Folder;
    return std::nullopt;
}

// Reads the quoted run that opens at text[i] and moves i past its closing
// quote. A quote that is never closed runs to the end of the text.
std::string_view read_quoted(std::string_view text, size_t& i) {
    const size_t start = i + 1;
    const size_t close = text.find('"', start);
    const size_t end = close == npos ? text.size() : close;
    i = close == npos ? text.size() : close + 1;
    return text.substr(start, end - start);
}

void add_term(LibraryQuery& q, QueryField field, std::string_view raw, bool phrase) {
    std::string folded = fold_for_search(raw);
    if (!folded.empty() && folded.front() == ' ') folded.erase(0, 1);
    if (!folded.empty() && folded.back() == ' ') folded.pop_back();
    if (folded.empty()) return;
    q.terms.push_back(QueryTerm{field, std::move(folded), phrase});
}

void add_error(LibraryQuery& q, std::string message) {
    if (std::find(q.errors.begin(), q.errors.end(), message) == q.errors.end())
        q.errors.push_back(std::move(message));
}

void parse_stars(LibraryQuery& q, std::string_view value) {
    int n = -1;
    const char* first = value.data();
    const char* last = value.data() + value.size();
    const auto [end, ec] = std::from_chars(first, last, n);
    if (value.empty() || ec != std::errc{} || end != last || n < 0 || n > kMaxStars) {
        add_error(q, kStarsError);
        return;
    }
    q.stars = n;
}

void parse_squeeze(LibraryQuery& q, std::string_view value) {
    double ms = 0.0;
    const char* first = value.data();
    const char* last = value.data() + value.size();
    const auto [end, ec] = std::from_chars(first, last, ms);
    if (value.empty() || ec != std::errc{} || end != last || !std::isfinite(ms)) {
        add_error(q, kSqueezeError);
        return;
    }
    q.squeeze_max_ms = ms;
}

// ---- matching --------------------------------------------------------------

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool term_matches(const QueryTerm& term, const SearchableRow& row) {
    switch (term.field) {
        case QueryField::Title: return contains(row.title, term.folded);
        case QueryField::Artist: return contains(row.artist, term.folded);
        case QueryField::Charter: return contains(row.charter, term.folded);
        case QueryField::Folder: return contains(row.folder, term.folded);
        case QueryField::Any:
            return contains(row.title, term.folded) || contains(row.artist, term.folded) ||
                   contains(row.charter, term.folded) || contains(row.folder, term.folded);
    }
    return false;
}

}  // namespace

std::string fold_for_search(std::string_view text) {
    std::string out;
    fold_into(text, out, nullptr);
    return out;
}

std::string strip_rich_tags(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '<') {
            if (const size_t len = rich_tag_length(text, i)) {
                i += len;
                continue;
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

bool LibraryQuery::empty() const {
    return terms.empty() && !stars && !squeeze_max_ms;
}

LibraryQuery parse_library_query(std::string_view text) {
    LibraryQuery q;
    size_t i = 0;
    while (i < text.size()) {
        if (is_ascii_space(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        if (text[i] == '"') {
            add_term(q, QueryField::Any, read_quoted(text, i), true);
            continue;
        }

        // A word runs to the next space or quote.
        size_t end = i;
        while (end < text.size() && text[end] != '"' &&
               !is_ascii_space(static_cast<unsigned char>(text[end])))
            ++end;
        const std::string_view word = text.substr(i, end - i);
        i = end;

        if (starts_with_ci(word, "squeeze<=")) {
            parse_squeeze(q, word.substr(9));
            continue;
        }
        if (starts_with_ci(word, "squeeze<")) {
            parse_squeeze(q, word.substr(8));
            continue;
        }

        const size_t colon = word.find(':');
        if (colon != npos) {
            const std::string_view key = word.substr(0, colon);
            const std::string_view value = word.substr(colon + 1);
            // field:"a phrase": the quote ended the word right after the colon.
            const bool quoted = value.empty() && i < text.size() && text[i] == '"';
            if (iequals_ascii(key, "stars")) {
                parse_stars(q, quoted ? read_quoted(text, i) : value);
                continue;
            }
            if (const std::optional<QueryField> field = field_named(key)) {
                add_term(q, *field, quoted ? read_quoted(text, i) : value, quoted);
                continue;
            }
        }
        add_term(q, QueryField::Any, word, false);
    }
    return q;
}

SearchableRow make_searchable(std::string_view title, std::string_view artist,
                              std::string_view charter, std::string_view folder) {
    SearchableRow row;
    row.title = fold_for_search(strip_rich_tags(title));
    row.artist = fold_for_search(strip_rich_tags(artist));
    row.charter = fold_for_search(strip_rich_tags(charter));
    row.folder = fold_for_search(strip_rich_tags(folder));
    return row;
}

bool query_matches(const LibraryQuery& q, const SearchableRow& row, const RowFacts& facts) {
    if (q.stars || q.squeeze_max_ms) {
        if (!facts.stars) return false;  // not analyzed: nothing to test
        if (q.stars && *facts.stars != *q.stars) return false;
        // A path with no squeeze passes any squeeze limit.
        if (q.squeeze_max_ms && facts.hardest_ms && *facts.hardest_ms > *q.squeeze_max_ms)
            return false;
    }
    for (const QueryTerm& term : q.terms)
        if (!term_matches(term, row)) return false;
    return true;
}

std::vector<MatchSpan> match_spans(const LibraryQuery& q, QueryField field,
                                   std::string_view display_text) {
    std::vector<MatchSpan> spans;
    if (q.terms.empty() || display_text.empty()) return spans;

    std::string folded;
    std::vector<SourceRange> source;
    fold_into(display_text, folded, &source);
    for (const QueryTerm& term : q.terms) {
        if (term.folded.empty()) continue;
        if (field != QueryField::Any && term.field != QueryField::Any && term.field != field)
            continue;
        for (size_t at = folded.find(term.folded); at != std::string::npos;
             at = folded.find(term.folded, at + 1))
            spans.push_back(
                MatchSpan{source[at].begin, source[at + term.folded.size() - 1].end});
    }

    std::sort(spans.begin(), spans.end(), [](const MatchSpan& a, const MatchSpan& b) {
        return a.begin != b.begin ? a.begin < b.begin : a.end < b.end;
    });
    std::vector<MatchSpan> merged;
    for (const MatchSpan& span : spans) {
        if (!merged.empty() && span.begin <= merged.back().end)
            merged.back().end = std::max(merged.back().end, span.end);
        else
            merged.push_back(span);
    }
    return merged;
}

}  // namespace hydra::app
```

Then add the source to `hydra_core` in `CMakeLists.txt`. Before:

```cmake
    src/app/display_format.cpp
    src/app/path_view.cpp
```

After:

```cmake
    src/app/display_format.cpp
    src/app/library_query.cpp
    src/app/path_view.cpp
```

- [ ] **Step 5: Run them and watch them pass.** Run `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1` with the Bash tool, then `.\build-cpp\Release\hydra_tests.exe -tc="library query*"`. Expected: `[doctest] test cases: 17 | 17 passed | 0 failed | 0 skipped` and `[doctest] Status: SUCCESS!`. Check `build.log` for new warnings from `library_query.cpp` or `test_library_query.cpp`; the build uses `/W4`, and there should be none. Then run the whole suite, `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, with 17 more test cases than the Task 0 baseline.

- [ ] **Step 6: Check the scope.** Run `git diff --stat <base>..HEAD` after committing, or `git status --short` before. Expected: only `src/app/library_query.h`, `src/app/library_query.cpp`, `tests/test_library_query.cpp` and `CMakeLists.txt`. Delete `build.log` if the build left it in the worktree root, or leave it untracked; never commit it.

- [ ] **Step 7: Commit.**

```bash
git add src/app/library_query.h src/app/library_query.cpp tests/test_library_query.cpp CMakeLists.txt
```

```bash
git commit -m "Library search: query module with accent folding, tag stripping, phrases, field filters and highlight spans" -m "Task: 2" -m "Agent: <agent id>" -m "Session: <session id>" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Report pages and report location

The three report pages (the path report, the dmleaderboards comparison and the fill-spawn comparison) are built from one shared shell in `html_page.cpp`. That shell has no `<!DOCTYPE html>`, no `<html lang>`, and no head or body, so browsers draw it in quirks mode (the old, non-standard layout rules). Its column headers are meant to stick while you scroll, but the table's wrapper only scrolls sideways, so the header scrolls away with the page. Columns such as "Cal fill", "Avg mult", "SqIn" and "Hydra opt" are never explained. Four text colours fall under the 4.5:1 contrast WCAG asks for: the "no squeeze" grey (2.6:1 light, 2.8:1 dark), the gold "Hard" tier (4.1:1), the orange "Extreme" tier (3.9:1 on a hovered row) and the header text (4.48:1). The "Charts" tile counts title plus artist glued together with no separator, while the subtitle counts chart hashes and calls them "songs", so the two can disagree. The tier filter says "No squeezes" but the chip beside the row says "None". Reports are written next to `hydra.db` in the program folder. The comparison page calls every unmatched score "not in your library", even when the chart is in the library and simply hasn't been analyzed. This task fixes all of that. When it's done, the pages open in standards mode, the header stays on screen while you scroll, hovering a header explains it and a legend under the table repeats every explanation, every text colour passes 4.5:1 in both themes, a narrow "#" column numbers the rows, the pages print cleanly, and the comparison splits "Not analyzed" from "Not in your library". Reports are saved in `Documents\Hydra`.

**Wave:** 1. **Depends on:** Task 0. **Expected overlaps:** T1 and T2 add source lines to `CMakeLists.txt`; T3 changes only the `target_link_libraries(hydra_core PRIVATE ...)` line, so keep all three. T4 writes report pages through `app::report_html_path()` and `app::dm_report_html_path()`, which now resolve inside `reports_dir()`; T4 needs no change for that. T4's `src/ui/dm_jobs.cpp` reads `stats.above` and `stats.unmatched`; T3 keeps both fields, filled from the new counts, so it compiles unchanged. T6 (wave 2) later edits `tests/test_report.cpp` and `tests/test_dm_report.cpp` for Auto and must also edit the `cap_label` line in `report.cpp` (see "Questions settled during planning"); keep T3's version and re-apply T6's lines. T12 (wave 3) must keep `RecordStore::list_chart_library`, which the comparison now calls.

**Goal:** the report pages are standards-mode, readable, explained and accessible, they save to `Documents\Hydra`, and the comparison tells "not analyzed" from "not in library".

**Files:**
- Modify: `src/app/report_files.h`, `src/app/report_files.cpp`, `src/app/html_page.h`, `src/app/html_page.cpp`, `src/app/report.h`, `src/app/report.cpp`, `src/app/dm_report.h`, `src/app/dm_report.cpp`, `CMakeLists.txt` (the `hydra_core` link line only)
- Test: `tests/test_report.cpp`, `tests/test_dm_report.cpp`
- Not modified: `src/app/fill_report.cpp`. The shared shell gives the fill page the doctype, the sticky header, the "#" column, the colours and the print rules without touching it. Its search box and dropdown get their labels in a merge-fix at the wave 1 merge.

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="reports_dir*"` passes: Documents\Hydra is made and used, the database's folder is the fallback when Documents is missing or the folder can't be made, and a test harness's reports stay next to its own database.
- [ ] `hydra_tests.exe -tc="report pages are standards-mode documents"` passes: all three pages start with `<!DOCTYPE html>` then `<html lang="en">` and end with `</body>` then `</html>`.
- [ ] `hydra_tests.exe -tc="report colours meet WCAG contrast in both themes"` passes. It computes the ratios from the stylesheet itself. The lowest text ratios after the change are 4.55:1 (light `--t0` on a hovered row), 4.58:1 (light `--t1`), 4.89:1 (light `--tn`) and 5.28:1 (light `--muted`, the header text, on the header band).
- [ ] `hydra_tests.exe -tc="report table header sticks and the page prints"`, `-tc="report pages number their rows*"`, `-tc="path report explains and renames its columns"`, `-tc="path report counts charts by hash*"` and `-tc="comparison page explains its columns*"` pass.
- [ ] `hydra_tests.exe -tc="collect_dm_rows tells not analyzed from not in library"` passes, with counts matched 1, above_optimal 0, not_analyzed 1, not_in_library 1.
- [ ] The whole `hydra_tests.exe` run ends `Status: SUCCESS!`, and `hydra_uitest.exe --all` prints `[PASS]` for all 26 GUI tests (they write their reports next to their scratch database, not into Documents).
- [ ] `git diff --stat <base>..ui/T3` lists only the files above.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="*report*,*dm*"` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests for the report folder.**

The new folder comes from Windows' Documents lookup (`SHGetKnownFolderPath`, the API that finds a user's known folders). The test can't make that call fail on demand, so `report_files` gets a seam: a settable lookup function, the same pattern as the existing `set_open_in_browser`. The test installs a lookup that returns a scratch folder, or nothing, or a folder where `Hydra` is already a plain file.

There is one more rule, and it keeps every existing test out of the user's real Documents folder. The GUI test harness and `tests/test_app_state.cpp` both point the app at a scratch database through `app::set_path_overrides` (the real app never sets it). When that database override is set, reports stay next to that database, exactly where they are today. So the GUI tests, which delete `<temp>\hydra_paths.html` in `reset_app`, keep working untouched.

In `tests/test_report.cpp`, add these includes. Put `#include <algorithm>`, `#include <cmath>` and `#include <map>` after `#include <atomic>`. Put `#include "app/config.h"` after `#include "app/analysis.h"`. Then append at the end of the file:

```cpp
// ---- where reports are saved ------------------------------------------------

namespace {

namespace fs = std::filesystem;

// One test's view of the Documents folder. It clears the database override
// (a harness override keeps reports next to its database, which would hide
// the Documents rule), and afterwards puts the overrides and the lookup back
// and deletes its scratch folder.
struct DocumentsSandbox {
    PathOverrides previous = path_overrides();
    fs::path root;

    explicit DocumentsSandbox(const char* tag) {
        PathOverrides cleared = previous;
        cleared.db_path.clear();
        set_path_overrides(cleared);
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        root = fs::path(tmp) / ("hydra_test_docs_" + std::to_string(GetCurrentProcessId())) /
               fs::u8path(tag);
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }

    ~DocumentsSandbox() {
        set_documents_dir_lookup({});
        set_path_overrides(previous);
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

}  // namespace

TEST_CASE("reports_dir is Documents\\Hydra, made on first use") {
    DocumentsSandbox box("made");
    set_documents_dir_lookup([&box] { return std::optional<fs::path>(box.root); });

    const fs::path dir = reports_dir();
    CHECK(dir == box.root / "Hydra");
    CHECK(fs::is_directory(dir));
    // Every report path helper lives in it.
    CHECK(fs::path(report_html_path()) == dir / "hydra_paths.html");
    CHECK(fs::path(dm_report_html_path()) == dir / "hydra_dmcompare.html");
}

TEST_CASE("reports_dir falls back to the database folder without Documents") {
    DocumentsSandbox box("fallback");
    const fs::path db_folder = fs::u8path(db_path()).parent_path();

    // No Documents folder at all.
    set_documents_dir_lookup([] { return std::optional<fs::path>(); });
    CHECK(reports_dir() == db_folder);

    // A Documents folder where "Hydra" can't be made: a plain file is in the way.
    { std::ofstream(box.root / "Hydra") << "not a folder"; }
    set_documents_dir_lookup([&box] { return std::optional<fs::path>(box.root); });
    CHECK(reports_dir() == db_folder);
}

TEST_CASE("reports_dir keeps a harness's reports next to its database") {
    DocumentsSandbox box("override");
    set_documents_dir_lookup([&box] { return std::optional<fs::path>(box.root); });
    PathOverrides scratch = path_overrides();
    scratch.db_path = (box.root / "scratch" / "hydra.db").u8string();
    set_path_overrides(scratch);

    CHECK(reports_dir() == box.root / "scratch");
    CHECK_FALSE(fs::exists(box.root / "Hydra"));  // Documents was never touched
}
```

- [ ] **Step 2: Run them and watch them fail.** From the worktree root, with the Bash tool:

`powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1`

Expected: the build fails. `build.log` shows `'reports_dir': identifier not found` and `'set_documents_dir_lookup': identifier not found` in `test_report.cpp`.

- [ ] **Step 3: Add `reports_dir()` and the lookup seam.**

In `src/app/report_files.h`, replace the two path comments and add the new declarations. Before:

```cpp
// Where the batch path report lives on disk (next to the db).
std::wstring report_html_path();

// Where the comparison page lives on disk (next to the db).
std::wstring dm_report_html_path();
```

After:

```cpp
// The folder every report page is saved in: Documents\Hydra, made on first
// use. It falls back to the database's folder when Documents can't be found
// or the Hydra folder can't be made there. When a harness has overridden the
// database path (app::set_path_overrides), reports stay next to that
// database instead, so no test ever writes into the real Documents folder.
std::filesystem::path reports_dir();

// The seam behind reports_dir's Documents lookup (SHGetKnownFolderPath by
// default). A test installs one that returns a scratch folder, or nullopt to
// act like a machine with no Documents folder; an empty function restores
// the default.
using DocumentsDirFn = std::function<std::optional<std::filesystem::path>()>;
void set_documents_dir_lookup(DocumentsDirFn fn);

// Where the batch path report lives on disk (in reports_dir()).
std::wstring report_html_path();

// Where the comparison page lives on disk (in reports_dir()).
std::wstring dm_report_html_path();
```

Add `#include <optional>` after `#include <functional>` in the same header.

In `src/app/report_files.cpp`, add `#include <shlobj.h>` right after `#include <windows.h>` (it declares `SHGetKnownFolderPath` and `FOLDERID_Documents`, and brings in `CoTaskMemFree`). Then replace the anonymous namespace. Before:

```cpp
namespace {

// Report pages live next to the db.
std::wstring html_artifact_path(const wchar_t* name) {
    std::filesystem::path dbp = std::filesystem::u8path(app::db_path());
    return (dbp.parent_path() / name).wstring();
}

OpenInBrowserFn g_open_in_browser;

}  // namespace

void set_open_in_browser(OpenInBrowserFn fn) { g_open_in_browser = std::move(fn); }
```

After:

```cpp
namespace {

std::filesystem::path db_folder() {
    return std::filesystem::u8path(app::db_path()).parent_path();
}

// The user's Documents folder, or nullopt when Windows can't name one.
std::optional<std::filesystem::path> known_documents_dir() {
    PWSTR raw = nullptr;
    std::optional<std::filesystem::path> out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &raw)) &&
        raw)
        out = std::filesystem::path(raw);
    CoTaskMemFree(raw);  // safe on nullptr
    return out;
}

// Every report page lives in reports_dir().
std::wstring html_artifact_path(const wchar_t* name) { return (reports_dir() / name).wstring(); }

DocumentsDirFn g_documents_dir;
OpenInBrowserFn g_open_in_browser;

}  // namespace

std::filesystem::path reports_dir() {
    // A harness pointed the app at a scratch database: keep its pages there.
    if (!path_overrides().db_path.empty()) return db_folder();

    std::optional<std::filesystem::path> docs =
        g_documents_dir ? g_documents_dir() : known_documents_dir();
    if (!docs || docs->empty()) return db_folder();

    const std::filesystem::path dir = *docs / L"Hydra";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::is_directory(dir, ec)) return db_folder();
    return dir;
}

void set_documents_dir_lookup(DocumentsDirFn fn) { g_documents_dir = std::move(fn); }

void set_open_in_browser(OpenInBrowserFn fn) { g_open_in_browser = std::move(fn); }
```

`report_html_path()`, `dm_report_html_path()`, `report_file_exists()` and the two `open_*_in_browser()` functions keep their bodies. They all go through `html_artifact_path`, so they all move with it. `path_overrides()` comes from `app/config.h`, which the file already includes.

In `CMakeLists.txt`, the `hydra_core` link line and its comment. Before:

```cmake
# shell32: report_files.cpp hands finished report pages to the browser with
# ShellExecuteW, for both the GUI's report jobs and the hydra_report CLI, and
# winstr.cpp splits every entry point's Unicode command line with
# CommandLineToArgvW.
target_link_libraries(hydra_core PUBLIC sqlite3 miniz)
target_link_libraries(hydra_core PRIVATE bcrypt shell32 hydra_warnings)
```

After:

```cmake
# shell32: report_files.cpp hands finished report pages to the browser with
# ShellExecuteW, for both the GUI's report jobs and the hydra_report CLI, and
# winstr.cpp splits every entry point's Unicode command line with
# CommandLineToArgvW. shell32 + ole32 + uuid: reports_dir() finds Documents
# with SHGetKnownFolderPath(FOLDERID_Documents) and frees the answer with
# CoTaskMemFree.
target_link_libraries(hydra_core PUBLIC sqlite3 miniz)
target_link_libraries(hydra_core PRIVATE bcrypt shell32 ole32 uuid hydra_warnings)
```

- [ ] **Step 4: Run them and watch them pass.** Build as in Step 2, then `.\build-cpp\Release\hydra_tests.exe -tc="reports_dir*"`. Expected: `[doctest] test cases: 3 | 3 passed`. Then run the whole suite, `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, including `the report-file check is cached for two seconds` in `test_app_state.cpp` (its `ScratchPaths` sets a database override, so its report stays in the temp folder).

- [ ] **Step 5: Commit.** With the PowerShell tool, so the four trailer lines stay together at the end of the message:

```powershell
git add src/app/report_files.h src/app/report_files.cpp CMakeLists.txt tests/test_report.cpp
git commit -m @'
Reports save to Documents\Hydra, falling back to the database folder

Task: T3 report pages and report location
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@
```

- [ ] **Step 6: Write the failing tests for the page shell and the path report.**

These tests read the built pages as text, the way the existing report tests do. The contrast test is the one real calculation: it reads the colour tokens out of the stylesheet and computes the WCAG ratio, so a later colour edit that breaks contrast fails the build.

Append to `tests/test_report.cpp`:

```cpp
// ---- the page shell and the path report -------------------------------------

namespace {

// The one COLS line of a page's script that declares column `key`.
std::string col_line(const std::string& page, const std::string& key) {
    const size_t at = page.find("{k:'" + key + "',");
    if (at == std::string::npos) return {};
    return page.substr(at, page.find('\n', at) - at);
}

size_t occurrences(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

// WCAG 2.2 relative luminance of "#rrggbb".
double luminance(const std::string& hex) {
    auto channel = [&hex](size_t at) {
        const double c = std::stoi(hex.substr(at, 2), nullptr, 16) / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(1) + 0.7152 * channel(3) + 0.0722 * channel(5);
}

double contrast(const std::string& a, const std::string& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// The "--name: #rrggbb" tokens of the first ":root {" block at or after `from`.
std::map<std::string, std::string> css_tokens(const std::string& css, size_t from) {
    std::map<std::string, std::string> out;
    const size_t open = css.find(":root {", from);
    REQUIRE(open != std::string::npos);
    const std::string block = css.substr(open, css.find('}', open) - open);
    size_t at = 0;
    while ((at = block.find("--", at)) != std::string::npos) {
        const size_t colon = block.find(':', at);
        const size_t semi = block.find(';', colon);
        const size_t hash = block.find('#', colon);
        if (hash != std::string::npos && hash < semi && semi - hash == 7)
            out[block.substr(at + 2, colon - at - 2)] = block.substr(hash, 7);
        if (semi == std::string::npos) break;
        at = semi;
    }
    return out;
}

}  // namespace

TEST_CASE("report pages are standards-mode documents") {
    const std::string paths = report::build_html({}, "sub", "foot", 85.0);
    const std::string dm = dm_report::build_dm_html({}, "sub", "foot");
    const std::string fill = fill_report::build_fill_html({}, "sub", "foot");
    for (const std::string* page : {&paths, &dm, &fill}) {
        CHECK(page->rfind("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n", 0) == 0);
        CHECK(page->find("</style>\n</head>\n<body>\n") != std::string::npos);
        REQUIRE(page->size() > 16);
        CHECK(page->substr(page->size() - 16) == "</body>\n</html>\n");
        CHECK(occurrences(*page, "<title>") == 1);
    }
}

TEST_CASE("report colours meet WCAG contrast in both themes") {
    const std::string css = html::kReportCss;
    const std::map<std::string, std::string> light = css_tokens(css, 0);
    const std::map<std::string, std::string> dark =
        css_tokens(css, css.find("prefers-color-scheme: dark"));
    for (const auto* theme : {&light, &dark}) {
        // Body text, dim text, header text and every chip colour, on the page,
        // on a cell, and on a hovered row or the header band.
        for (const char* text : {"ink", "muted", "t0", "t1", "t2", "t3", "t4", "t5", "tn"}) {
            for (const char* ground : {"paper", "surface", "raised"}) {
                INFO(text << " on " << ground);
                CHECK(contrast(theme->at(text), theme->at(ground)) >= 4.5);
            }
        }
        // The accent is the large bold heading word and the focus ring: 3:1.
        CHECK(contrast(theme->at("sp"), theme->at("surface")) >= 3.0);
        CHECK(contrast(theme->at("sp"), theme->at("raised")) >= 3.0);
    }
}

TEST_CASE("report table header sticks and the page prints") {
    const std::string css = html::kReportCss;
    const size_t at = css.find(".tablewrap {");
    REQUIRE(at != std::string::npos);
    const std::string rule = css.substr(at, css.find('}', at) - at);
    // Scrolling both ways makes the wrapper the header's sticky container.
    CHECK(rule.find("overflow: auto;") != std::string::npos);
    CHECK(rule.find("max-height:") != std::string::npos);
    CHECK(rule.find("overflow-x") == std::string::npos);

    CHECK(css.find("@media print {") != std::string::npos);
    CHECK(css.find("thead { display: table-header-group; }") != std::string::npos);
    CHECK(css.find(".controls { display: none; }") != std::string::npos);
}

TEST_CASE("report pages number their rows in a # column") {
    // The shared script adds the column, so all three pages get it.
    const std::string js = html::kReportJs;
    CHECK(js.find("th.textContent = '#';") != std::string::npos);
    CHECK(js.find("idx.textContent = (++n).toLocaleString();") != std::string::npos);
    // The sort control and the arrows walk the real columns, not the # one.
    CHECK(js.find("document.querySelectorAll('#head th.sortable')") != std::string::npos);
    CHECK(std::string(html::kReportCss).find("th.idx, td.idx {") != std::string::npos);
}

TEST_CASE("path report explains and renames its columns") {
    const std::string html = report::build_html({}, "sub", "foot", 85.0);
    CHECK(html.find("t:'Cal fill (ms)'") != std::string::npos);
    CHECK(html.find("t:'Avg multiplier'") != std::string::npos);
    CHECK(html.find("t:'Cal fill',") == std::string::npos);
    CHECK(html.find("t:'Avg mult',") == std::string::npos);
    for (const char* key : {"mode", "path", "score", "acts", "skip", "ms", "tier", "efill",
                            "mult", "sqin", "sqout", "notes"}) {
        INFO(key);
        CHECK(col_line(html, key).find("d:'") != std::string::npos);
    }
    // Hover text and the footer legend both read the definitions.
    CHECK(html.find("<dl class=\"legend\" id=\"legend\"></dl>") != std::string::npos);
    CHECK(html.find("const legend = document.getElementById('legend');") != std::string::npos);
    // One name per tier for the dropdown and the chip ("No squeezes" in both).
    CHECK(html.find("function tierLabel(name)") != std::string::npos);
    CHECK(html.find("o.textContent = tierLabel(t.name);") != std::string::npos);
    CHECK(html.find("['chip ' + r.tok, tierLabel(r.tier), 'chip']") != std::string::npos);
    // The search box and the tier dropdown have names a screen reader reads.
    CHECK(html.find("id=\"q\" aria-label=\"Search paths\"") != std::string::npos);
    CHECK(html.find("id=\"tier\" aria-label=\"Timing tier\"") != std::string::npos);
}

TEST_CASE("path report counts charts by hash in the tile and the subtitle") {
    report::ReportRow a;
    a.song = "Same";
    a.artist = "Name";
    a.path = "1";
    a.tier = "None";
    a.tok = "tn";
    a.hyhash = "h1";
    report::ReportRow a2 = a;
    a2.rank = 2;
    report::ReportRow b = a;  // another chart with the same title and artist
    b.hyhash = "h2";
    const std::string html = report::build_html({a, a2, b}, "sub", "foot", 85.0);

    // Each chart gets a small id in order of first appearance.
    CHECK(occurrences(html, "\"c\":0") == 2);
    CHECK(occurrences(html, "\"c\":1") == 1);
    CHECK(html.find("['Charts', new Set(rows.map(r => r.c)).size.toLocaleString()]") !=
          std::string::npos);
    CHECK(html.find("r.song + r.artist))") == std::string::npos);
}
```

Also change three existing expectations in the same file, because the subtitle now says "charts" and uses the singular for one. In `report lists only the wanted cap and names it`, before:

```cpp
    CHECK(four.html.find("1 records across 1 songs") != std::string::npos);
```

After:

```cpp
    CHECK(four.html.find("1 record across 1 chart") != std::string::npos);
```

In `generate_report: one seam frames the page for every entry point`, before:

```cpp
    std::string subtitle = group_thousands(result.records) +
                           " records across " + group_thousands(result.songs) +
                           " songs — top 5 paths per chart";
```

After:

```cpp
    std::string subtitle = report::counted(result.records, "record", "records") +
                           " across " + report::counted(result.songs, "chart", "charts") +
                           " — top 5 paths per chart";
```

And before:

```cpp
    CHECK(report::generate_report(store, options)
              .html.find("songs — every path") != std::string::npos);
```

After:

```cpp
    CHECK(report::generate_report(store, options)
              .html.find(report::counted(result.songs, "chart", "charts") + " — every path") !=
          std::string::npos);
```

- [ ] **Step 7: Run them and watch them fail.** Build as in Step 2. Expected: the build fails with `'counted': is not a member of 'hydra::app::report'` in `test_report.cpp`. That compile error is the failing state; Steps 8 and 9 make it build and pass. (If you want to see the checks fail one by one, add only the `counted` declaration and definition from Step 9 first: then `report pages are standards-mode documents` fails on the doctype check, the contrast test on `tn on paper` at 2.50, the sticky test on `overflow: auto;`, and the column, `#` and chart-count tests on their first `find`.)

- [ ] **Step 8: Change the shared shell in `src/app/html_page.cpp`.**

The head fragment becomes a real document start. Before:

```cpp
const char* const kHead = R"frag(<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
)frag";
```

After:

```cpp
// The doctype puts browsers in standards mode (without it they fall back to
// quirks mode); lang tells screen readers which voice to read in.
const char* const kHead = R"frag(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
)frag";

const char* const kEnd = "</body>\n</html>\n";
```

`page_template` closes the head, opens the body and closes the document. Before:

```cpp
    page += kReportCss;
    page += "</style>\n\n";
    page += body;
    page += kReportJsHead;
    page += page_js;
    page += kReportJs;
    return page;
```

After:

```cpp
    page += kReportCss;
    page += "</style>\n</head>\n<body>\n";
    page += body;
    page += kReportJsHead;
    page += page_js;
    page += kReportJs;
    page += kEnd;
    return page;
```

Now the stylesheet, `kReportCss`, in six edits.

The light colours. The table below shows each changed token, its old and new value, and the new lowest ratio (on the hovered-row colour `--raised`, the darkest ground it sits on). Unchanged tokens keep their values.

| Token | Old | New | Lowest ratio now |
|---|---|---|---|
| `--muted` (dim text, header text) | `#6a6e79` (4.48:1) | `#5f636d` | 5.28:1 |
| `--t1` (Hard) | `#9a7a1e` (3.56:1) | `#85690f` | 4.58:1 |
| `--t2` (Extreme) | `#b85f2c` (3.92:1) | `#a0501f` | 5.04:1 |
| `--tn` (no squeeze) | `#9aa0ab` (2.31:1) | `#646873` | 4.89:1 |
| dark `--tn` | `#5c626e` (2.60:1) | `#949aa6` | 5.63:1 |

The accent `--sp` stays `#b07d0a` (3.19:1 at worst). It is only used for the 20 px bold heading word, the focus ring and the active sort arrow, where WCAG asks for 3:1. Every dark token other than `--tn` already passes (lowest 5.28:1).

Before:

```css
  --muted: #6a6e79;
```

After:

```css
  --muted: #5f636d;
```

Before:

```css
  --t0: #2c7a5e; --t1: #9a7a1e; --t2: #b85f2c; --t3: #b23c3c; --t4: #8e3070; --t5: #5b3fa8;
  --tn: #9aa0ab;
```

After:

```css
  --t0: #2c7a5e; --t1: #85690f; --t2: #a0501f; --t3: #b23c3c; --t4: #8e3070; --t5: #5b3fa8;
  --tn: #646873;
  --idxw: 64px;  /* the "#" column's width; the song column sticks just right of it */
```

Before (dark block):

```css
    --tn: #5c626e;
```

After:

```css
    --tn: #949aa6;
```

The table wrapper. Before:

```css
.tablewrap {
  overflow-x: auto; background: var(--surface);
```

After:

```css
/* The wrapper scrolls both ways and has a height, so it is the header's
   sticky container. With only a sideways scroll it never scrolled down, and
   the header scrolled away with the page. */
.tablewrap {
  overflow: auto; max-height: 80vh; background: var(--surface);
```

And before:

```css
.tablewrap::-webkit-scrollbar { height: 12px; }
```

After:

```css
.tablewrap::-webkit-scrollbar { height: 12px; width: 12px; }
```

The sticky left columns. Before:

```css
/* Keep the song visible while reading the numbers off to the right. */
thead th:first-child { left: 0; z-index: 4; }
tbody td:first-child { position: sticky; left: 0; z-index: 1; background: var(--surface); }
tbody tr:hover td:first-child { background: var(--raised); }
```

After:

```css
/* The "#" column numbers the rows in the current sort. It isn't a sort key. */
th.idx, td.idx { width: var(--idxw); min-width: var(--idxw); max-width: var(--idxw); color: var(--muted); }
thead th.idx { cursor: default; }
thead th.idx:hover { color: var(--muted); background: var(--raised); }

/* Keep the row number and the song visible while reading the numbers off to
   the right. */
thead th:first-child { left: 0; z-index: 4; }
thead th:nth-child(2) { left: var(--idxw); z-index: 4; }
tbody td:first-child, tbody td:nth-child(2) { position: sticky; z-index: 1; background: var(--surface); }
tbody td:first-child { left: 0; }
tbody td:nth-child(2) { left: var(--idxw); }
tbody tr:hover td:first-child, tbody tr:hover td:nth-child(2) { background: var(--raised); }
```

The comparison chips gain a class for "not analyzed". Before:

```css
.s-matched{color:var(--t0)} .s-above{color:var(--t1)} .s-unmatched{color:var(--tn); border-color:transparent}
```

After:

```css
.s-matched{color:var(--t0)} .s-above{color:var(--t1)} .s-notanalyzed{color:var(--muted)} .s-unmatched{color:var(--tn); border-color:transparent}
```

The footer legend and the print rules. Before (the end of `kReportCss`):

```css
.empty { padding: 40px; text-align: center; color: var(--muted); }
footer { color: var(--muted); font-size: 12px; }
)css";
```

After:

```css
.empty { padding: 40px; text-align: center; color: var(--muted); }
footer { color: var(--muted); font-size: 12px; }
footer p { margin: 0 0 8px; }
.legend { display: grid; grid-template-columns: max-content 1fr; gap: 2px 12px; margin: 0; }
.legend dt { font-weight: 600; color: var(--ink); }
.legend dd { margin: 0; }

/* Paper: light colours whatever the screen theme, no controls, the whole
   table (no inner scroller), the header repeated on every page, and rows
   kept whole. */
@media print {
  :root {
    color-scheme: light;
    --paper: #ffffff; --surface: #ffffff; --raised: #f2f0ec;
    --ink: #000000; --muted: #4a4d55; --rule: #c9c6bf;
    --sp: #8f6508;
    --t0: #2c7a5e; --t1: #85690f; --t2: #a0501f; --t3: #b23c3c; --t4: #8e3070; --t5: #5b3fa8;
    --tn: #646873;
    --shadow: none;
  }
  .controls { display: none; }
  .wrap { max-width: none; padding: 0; }
  .tablewrap { overflow: visible; max-height: none; border: 0; }
  thead { display: table-header-group; }
  thead th, tbody td:first-child, tbody td:nth-child(2) { position: static; }
  tbody tr { break-inside: avoid; }
  td.trunc, .song { max-width: none; white-space: normal; }
}
)css";
```

Now the shared script, `kReportJs`, in four edits.

The sort arrows walk only the sortable headers. Before:

```js
  document.querySelectorAll('#head th').forEach((th, i) => {
    const c = COLS[i];
```

After:

```js
  document.querySelectorAll('#head th.sortable').forEach((th, i) => {
    const c = COLS[i];
```

Each row starts with its number. Before:

```js
  for (const r of rows) {
    const tr = document.createElement('tr');
    const rowCls = PAGE.rowClass ? PAGE.rowClass(r) : '';
    if (rowCls) tr.className = rowCls;
```

After:

```js
  let n = 0;
  for (const r of rows) {
    const tr = document.createElement('tr');
    const rowCls = PAGE.rowClass ? PAGE.rowClass(r) : '';
    if (rowCls) tr.className = rowCls;

    // The "#" column: the row's place in the current sort and filter.
    const idx = document.createElement('td');
    idx.className = 'idx num';
    idx.textContent = (++n).toLocaleString();
    tr.appendChild(idx);
```

The header row gets the `#` cell first, and each column's hover text carries its definition. A column's optional `d` field is its definition. Before:

```js
const head = document.getElementById('head');
COLS.forEach(c => {
  const th = document.createElement('th');
  th.textContent = c.t;
  th.tabIndex = 0;
  th.title = 'Sort by ' + c.t;
  if (c.num) th.className = 'num';
```

After:

```js
const head = document.getElementById('head');
{
  // Row numbers: not a sort key, so no arrow and no tab stop.
  const th = document.createElement('th');
  th.className = 'idx num';
  th.scope = 'col';
  th.textContent = '#';
  th.title = 'Row number in the current sort';
  head.appendChild(th);
}
COLS.forEach(c => {
  const th = document.createElement('th');
  th.textContent = c.t;
  th.tabIndex = 0;
  th.scope = 'col';
  // A column with a definition shows it on hover; every column says it sorts.
  th.title = (c.d ? c.t + ': ' + c.d + '\n' : '') + 'Click to sort by ' + c.t + '.';
  th.className = c.num ? 'sortable num' : 'sortable';
```

The footer legend lists every defined column. A page without a `#legend` element (the fill page) skips it. Before:

```js
for (const [id, ev] of PAGE.controls)
  document.getElementById(id).addEventListener(ev, render);
```

After:

```js
// The footer legend: every column that carries a definition, in table order.
const legend = document.getElementById('legend');
if (legend) {
  for (const c of COLS) {
    if (!c.d) continue;
    const dt = document.createElement('dt');
    dt.textContent = c.t;
    const dd = document.createElement('dd');
    dd.textContent = c.d;
    legend.append(dt, dd);
  }
}

for (const [id, ev] of PAGE.controls)
  document.getElementById(id).addEventListener(ev, render);
```

In `src/app/html_page.h`, the `page_template` comment. Before:

```cpp
// One page's template: the shared head, stylesheet and script around the
// page's <title> text, its body markup and its `const PAGE = {...};` script.
```

After:

```cpp
// One page's template: a whole standards-mode document (doctype, <html
// lang="en">, head, body) with the shared stylesheet and script around the
// page's <title> text, its body markup and its `const PAGE = {...};` script.
// A column in PAGE.cols may carry `d:'...'`, its definition: the header's
// hover text and the footer legend (#legend, when the body has one) show it.
```

- [ ] **Step 9: Change the path report in `src/app/report.{h,cpp}`.**

In `report.h`, the `ReportRow` comment's first lines. Before:

```cpp
// One table row. Field order is the JSON key order the page's script reads,
// except `hyhash`, which the page never sees.
```

After:

```cpp
// One table row. Field order is the JSON key order the page's script reads.
// `hyhash` goes out as "c", a small per-chart number in order of first
// appearance, which the Charts tile counts.
```

And before:

```cpp
    // The chart this row belongs to. Not written to the page: generate_report
    // counts the distinct charts on the page with it.
    std::string hyhash;
```

After:

```cpp
    // The chart this row belongs to. generate_report counts the distinct
    // charts with it, and build_html turns it into the page's "c" number.
    std::string hyhash;
```

Add the plural helper after `plain`'s declaration:

```cpp
// "1 record" / "12,345 records": the count with thousands grouped, then the
// singular or plural noun. The report subtitles use it.
std::string counted(int64_t n, const char* one, const char* many);
```

And the `GeneratedReport::songs` comment. Before:

```cpp
    int64_t songs = 0;    // distinct charts with rows on the page
```

After:

```cpp
    int64_t songs = 0;    // distinct charts (by chart hash) with rows on the page
```

In `report.cpp`, add `#include <unordered_map>` after `#include <unordered_set>`. Replace the page body (`kBody`) so the search box and the dropdown carry labels and the footer holds the legend. Before:

```cpp
    <input type="search" id="q" placeholder="Search song, artist, charter, or path notation">
    <select id="tier">
```

After:

```cpp
    <input type="search" id="q" aria-label="Search paths" placeholder="Search song, artist, charter, or path notation">
    <select id="tier" aria-label="Timing tier">
```

Before:

```cpp
  <footer>__FOOTER__</footer>
</div>
```

After:

```cpp
  <footer>
    <p>__FOOTER__</p>
    <dl class="legend" id="legend"></dl>
  </footer>
</div>
```

Replace the top of `kPageJs` (the tier dropdown) with the shared tier label. Before:

```js
// The tier dropdown mirrors the bands the rows were labeled with.
{
  const sel = document.getElementById('tier');
  for (const t of DATA.tiers) {
    const o = document.createElement('option');
    o.value = t.name;
    o.textContent = t.name === 'Beyond' ? 'Beyond ' + BEYOND + ' ms'
                  : t.name === 'None' ? 'No squeezes'
                  : t.name;
    sel.appendChild(o);
  }
}
```

After:

```js
// One name per tier, for both the dropdown and the chips, so a row's chip
// reads the same words as the filter that finds it.
function tierLabel(name) {
  return name === 'Beyond' ? 'Beyond ' + BEYOND + ' ms'
       : name === 'None' ? 'No squeezes'
       : name;
}

// The tier dropdown mirrors the bands the rows were labeled with.
{
  const sel = document.getElementById('tier');
  for (const t of DATA.tiers) {
    const o = document.createElement('option');
    o.value = t.name;
    o.textContent = tierLabel(t.name);
    sel.appendChild(o);
  }
}
```

The columns, with the two renames and a definition on each column that needs one. Every definition comes from CONTEXT.md or the code named in brackets here: Acts is `summarize_path`'s activation count; Max skip is CONTEXT.md's "Skip"; Hardest ms is `Activation::difficulty` (the larger of the squeezes and the E0 fill) and CONTEXT.md's "Difficulty" (raw ms); Timing is `tier_for` in `report.h` (bands of the two-hit budget, twice the hit window); Cal fill is the `efill` comment in `report.h`; Avg multiplier is `Path::avg_mult` (score minus solo bonus over the chart's base score) and CONTEXT.md's "Star cutoff" (base score = every note at 1x); SqIn and SqOut are CONTEXT.md's "SqIn / SqOut". Strings stay ASCII with no apostrophes, because `hydra_core` isn't built with `/utf-8` and the strings sit in single-quoted JavaScript. Before:

```js
  cols: [
    {k:'song',    t:'Song',     num:false},
    {k:'artist',  t:'Artist',   num:false},
    {k:'charter', t:'Charter',  num:false},
    {k:'mode',    t:'Mode',     num:false},
    {k:'path',    t:'Path',     num:false},
    {k:'score',   t:'Score',    num:true},
    {k:'acts',    t:'Acts',     num:true},
    {k:'skip',    t:'Max skip', num:true},
    {k:'ms',      t:'Hardest ms', num:true},
    {k:'tier',    t:'Timing',   num:false},
    {k:'efill',   t:'Cal fill', num:true},
    {k:'mult',    t:'Avg mult', num:true},
    {k:'sqin',    t:'SqIn',     num:true},
    {k:'sqout',   t:'SqOut',    num:true},
    {k:'notes',   t:'Notes',    num:true},
  ],
```

After:

```js
  cols: [
    {k:'song',    t:'Song',     num:false},
    {k:'artist',  t:'Artist',   num:false},
    {k:'charter', t:'Charter',  num:false},
    {k:'mode',    t:'Mode',     num:false, d:'The difficulty and drum options the path was found for.'},
    {k:'path',    t:'Path',     num:false, d:'The path in path notation: one entry per activation, with its skip count and squeeze symbols.'},
    {k:'score',   t:'Score',    num:true,  d:'The total score the path reaches.'},
    {k:'acts',    t:'Acts',     num:true,  d:'Activations: how many times the path uses Star Power.'},
    {k:'skip',    t:'Max skip', num:true,  d:'The most fills any one activation passes over before activating.'},
    {k:'ms',      t:'Hardest ms', num:true, d:'The hardest squeeze or calibration fill the path needs, in raw ms. A dash means it needs none.'},
    {k:'tier',    t:'Timing',   num:false, d:'How hard Hardest ms is, in bands of your hit window. Beyond means at least twice the hit window.'},
    {k:'efill',   t:'Cal fill (ms)', num:true, d:'The hardest calibration fill (E0) on the path: how many ms early you must hit to summon the fill. Negative means slack. A dash means the path has none.'},
    {k:'mult',    t:'Avg multiplier', num:true, d:'Points per note on average: the score without solo bonuses divided by the base score (every note at 1x).'},
    {k:'sqin',    t:'SqIn',     num:true,  d:'SP phrase notes squeezed into an active Star Power window (+ in the path).'},
    {k:'sqout',   t:'SqOut',    num:true,  d:'SP phrase notes squeezed out of an active Star Power window (- in the path).'},
    {k:'notes',   t:'Notes',    num:true,  d:'Notes in the chart.'},
  ],
```

The chip uses the tier label. Before:

```js
    ['chip ' + r.tok, r.tier, 'chip'],
```

After:

```js
    ['chip ' + r.tok, tierLabel(r.tier), 'chip'],
```

The Charts tile counts charts by their id. Before:

```js
  stats(rows) {
    const best = rows.filter(r => r.rank === 1);
    const withMs = rows.filter(r => r.ms !== null && r.ms !== undefined);
```

After:

```js
  stats(rows) {
    const withMs = rows.filter(r => r.ms !== null && r.ms !== undefined);
```

And before:

```js
      ['Charts', new Set(best.map(r => r.song + r.artist)).size.toLocaleString()],
```

After:

```js
      ['Charts', new Set(rows.map(r => r.c)).size.toLocaleString()],
```

This also removes the old key's collision: "AB" by "C" and "A" by "BC" glued into the same string. With the default "Best path only" and no filter, the tile now equals the subtitle's chart count.

In `build_html`, write each row's chart id. Before:

```cpp
    data += "],\"rows\":[";
    bool first_row = true;
    for (const ReportRow& r : rows) {
        if (!first_row) data.push_back(',');
        first_row = false;

        data += "{\"song\":";
```

After:

```cpp
    data += "],\"rows\":[";
    // A small number per chart, in order of first appearance: the Charts
    // tile counts distinct charts by it, the way the subtitle counts chart
    // hashes, without the 32-character hash on every row.
    std::unordered_map<std::string, int> chart_ids;
    bool first_row = true;
    for (const ReportRow& r : rows) {
        if (!first_row) data.push_back(',');
        first_row = false;
        const int chart_id =
            chart_ids.emplace(r.hyhash, static_cast<int>(chart_ids.size())).first->second;

        data += "{\"c\":" + std::to_string(chart_id);
        data += ",\"song\":";
```

Add `counted` after `plain`'s definition:

```cpp
std::string counted(int64_t n, const char* one, const char* many) {
    return group_thousands(n) + " " + (n == 1 ? one : many);
}
```

In `generate_report`, the subtitle's noun. Before:

```cpp
    std::string subtitle = group_thousands(out.records) + " records across " +
                           group_thousands(out.songs) + " songs — " + shown + " — " + cap_label;
```

After:

```cpp
    std::string subtitle = counted(out.records, "record", "records") + " across " +
                           counted(out.songs, "chart", "charts") + " — " + shown + " — " +
                           cap_label;
```

- [ ] **Step 10: Run them and watch them pass.** Build as in Step 2, then `.\build-cpp\Release\hydra_tests.exe -tc="*report*"`. Expected: `Status: SUCCESS!`, with the six new cases and every existing report case passing.

- [ ] **Step 11: Commit.**

```powershell
git add src/app/html_page.h src/app/html_page.cpp src/app/report.h src/app/report.cpp tests/test_report.cpp
git commit -m @'
Report pages: standards mode, sticky header, column definitions, # column, contrast, print

Task: T3 report pages and report location
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@
```

- [ ] **Step 12: Write the failing tests for the comparison split.**

Today every score with no Ready result at SP cap 4 is "unmatched" and counted as "not in your library". The store already knows which charts the last scan found (`RecordStore::list_chart_library`), and `collect_dm_rows` already has the store. So the split needs no new input: a score whose chart the scan found is "not analyzed", and any other is "not in library". A chart with only a stale result counts as not analyzed, because it has no current result.

In `tests/test_dm_report.cpp`, change the file comment. Before:

```cpp
// Tests for app/dm_report: the score-vs-optimal join (collect_dm_rows) and
// the comparison page (build_dm_html). Pins the status strings the UI
// compares as raw literals (see ui/dm_jobs.cpp's DmReportJob tally).
```

After:

```cpp
// Tests for app/dm_report: the score-vs-optimal join (collect_dm_rows) and
// the comparison page (build_dm_html). Pins the status strings the page's
// filter and chip classes key on, and the four counts the app shows.
```

In `collect_dm_rows joins scores to records and labels them`, the third score now reads "not in library" (the in-memory store has no scanned charts). Before:

```cpp
    // These exact strings are load-bearing: ui/dm_jobs.cpp tallies the finished
    // modal's counts by comparing them as literals.
    CHECK(rows[0].status == "matched");
    CHECK(rows[1].status == "above optimal");
    CHECK(rows[2].status == "unmatched");
```

After:

```cpp
    // These exact strings are load-bearing: the page's status filter and chip
    // classes key on them.
    CHECK(rows[0].status == "matched");
    CHECK(rows[1].status == "above optimal");
    CHECK(rows[2].status == "not in library");
```

In `generate_dm_report: tally and framing behind one seam`, before:

```cpp
    CHECK(result.stats.above == 1);
    CHECK(result.stats.unmatched == 1);

    // The subtitle the finished modal's counts must agree with.
    CHECK(result.html.find("TestUser — 3 scores: 1 matched, 1 above optimal, "
                           "1 not in your library") != std::string::npos);
```

After:

```cpp
    CHECK(result.stats.above_optimal == 1);
    CHECK(result.stats.not_analyzed == 0);
    CHECK(result.stats.not_in_library == 1);

    // The subtitle the finished modal's counts must agree with.
    CHECK(result.html.find("TestUser — 3 scores: 1 matched, 1 above optimal, "
                           "0 not analyzed, 1 not in your library") != std::string::npos);
```

Append a new case:

```cpp
TEST_CASE("collect_dm_rows tells not analyzed from not in library") {
    store::RecordStore store(":memory:");
    const int64_t optimal = fill_store(store);
    REQUIRE(optimal > 0);

    // The last scan found kHash and one more chart nobody has analyzed. The
    // scanned hash is upper case on purpose: the join ignores case.
    constexpr const char* kScannedUpper = "ABCDEF00112233445566778899AABBCC";
    constexpr const char* kScanned = "abcdef00112233445566778899aabbcc";
    store::ChartLibraryEntry analyzed;
    analyzed.md5 = kHash;
    analyzed.title = "Stored Title";
    store::ChartLibraryEntry scanned;
    scanned.md5 = kScannedUpper;
    scanned.title = "Scanned Only";
    store.rebuild_chart_library({analyzed, scanned});

    std::vector<DmReportRow> rows = app::dm_report::collect_dm_rows(
        store,
        {make_score(kHash, optimal - 1000),                        // matched
         make_score(kScanned, 5000),                               // in the library, no result
         make_score("00ff00ff00ff00ff00ff00ff00ff00ff", 123456)},  // never scanned
        kMode, store::Lens{});
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].status == "matched");
    CHECK(rows[1].status == "not analyzed");
    CHECK(rows[2].status == "not in library");
    CHECK_FALSE(rows[1].optimal.has_value());

    const app::dm_report::DmReportStats stats = app::dm_report::tally_dm_rows(rows);
    CHECK(stats.total == 3);
    CHECK(stats.matched == 1);
    CHECK(stats.above_optimal == 0);
    CHECK(stats.not_analyzed == 1);
    CHECK(stats.not_in_library == 1);
    // The old names, kept filled until ui/dm_jobs.cpp reads the new ones.
    CHECK(stats.above == 0);
    CHECK(stats.unmatched == 2);
}
```

In `tests/test_report.cpp`, append the page check:

```cpp
TEST_CASE("comparison page explains its columns and splits the missing scores") {
    const std::string html = dm_report::build_dm_html({}, "sub", "foot");
    for (const char* key : {"actual", "optimal", "delta", "pct", "fc", "speed", "rank",
                            "posted", "status"}) {
        INFO(key);
        CHECK(col_line(html, key).find("d:'") != std::string::npos);
    }
    CHECK(html.find("<option value=\"not analyzed\">") != std::string::npos);
    CHECK(html.find("<option value=\"not in library\">") != std::string::npos);
    CHECK(html.find("value=\"unmatched\"") == std::string::npos);
    CHECK(html.find("'not analyzed':'s-notanalyzed'") != std::string::npos);
    CHECK(html.find("id=\"q\" aria-label=\"Search scores\"") != std::string::npos);
    CHECK(html.find("id=\"status\" aria-label=\"Status\"") != std::string::npos);
    CHECK(html.find("<dl class=\"legend\" id=\"legend\"></dl>") != std::string::npos);
}
```

And in the skipped sample writer at the end of `tests/test_report.cpp`, before:

```cpp
    add_dm("Song C", 90000, std::nullopt, "unmatched", false, std::nullopt);
```

After:

```cpp
    add_dm("Song C", 90000, std::nullopt, "not in library", false, std::nullopt);
    add_dm("Song D", 80000, std::nullopt, "not analyzed", false, std::nullopt);
```

- [ ] **Step 13: Run them and watch them fail.** Build as in Step 2. Expected: a compile error, `'above_optimal': is not a member of 'hydra::app::dm_report::DmReportStats'`.

- [ ] **Step 14: Split the statuses in `src/app/dm_report.{h,cpp}`.**

In `dm_report.h`, the row's status comment. Before:

```cpp
    std::optional<int64_t> optimal;     // Hydra best-path score; unset if unmatched
```

After:

```cpp
    std::optional<int64_t> optimal;     // Hydra best-path score; unset with no current result
```

Before:

```cpp
    std::string status;                 // "matched" | "above optimal" | "unmatched"
```

After:

```cpp
    // "matched" | "above optimal" | "not analyzed" (the last scan found the
    // chart, but it has no current result at SP cap 4 for this mode) |
    // "not in library" (the last scan never found it).
    std::string status;
```

The counts. Before:

```cpp
struct DmReportStats {
    int total = 0;
    int matched = 0;
    int above = 0;      // "above optimal"
    int unmatched = 0;  // not in the library
};
```

After:

```cpp
struct DmReportStats {
    int total = 0;
    int matched = 0;
    int above_optimal = 0;
    int not_analyzed = 0;    // in the library, no current result
    int not_in_library = 0;
    // The old names, still filled so ui/dm_jobs.cpp compiles unchanged:
    // above == above_optimal, unmatched == not_analyzed + not_in_library.
    // Remove them once the job reads the four counts above.
    int above = 0;
    int unmatched = 0;
};
```

In `dm_report.cpp`, add `#include <unordered_set>` after `#include <unordered_map>`, and `#include "core/strutil.h"  // to_lower_ascii` after `#include "app/report.h"`.

The page body's search box and filter. Before:

```cpp
    <input type="search" id="q" placeholder="Search song, artist, or charter">
    <select id="status">
      <option value="">All charts</option>
      <option value="matched">Matched</option>
      <option value="above optimal">Above optimal</option>
      <option value="unmatched">Unmatched (not in library)</option>
    </select>
```

After:

```cpp
    <input type="search" id="q" aria-label="Search scores" placeholder="Search song, artist, or charter">
    <select id="status" aria-label="Status">
      <option value="">All charts</option>
      <option value="matched">Matched</option>
      <option value="above optimal">Above optimal</option>
      <option value="not analyzed">Not analyzed (in your library)</option>
      <option value="not in library">Not in your library</option>
    </select>
```

Before:

```cpp
  <footer>__FOOTER__</footer>
</div>
```

After:

```cpp
  <footer>
    <p>__FOOTER__</p>
    <dl class="legend" id="legend"></dl>
  </footer>
</div>
```

The chip classes. Before:

```js
const STATUS_CLASS = {'matched':'s-matched', 'above optimal':'s-above', 'unmatched':'s-unmatched'};
```

After:

```js
const STATUS_CLASS = {'matched':'s-matched', 'above optimal':'s-above',
                      'not analyzed':'s-notanalyzed', 'not in library':'s-unmatched'};
```

The columns. The definitions come from `dm_report.h` (`delta`, `pct`) and `net/dmbot_client.h` (`speed`, `rank`, `posted`). Percent gets the neutral note `The percent the leaderboard lists for this score.`, because nothing in the repo defines it. Before:

```js
  cols: [
    {k:'song',    t:'Song',      num:false},
    {k:'artist',  t:'Artist',    num:false},
    {k:'charter', t:'Charter',   num:false},
    {k:'actual',  t:'Actual',    num:true},
    {k:'optimal', t:'Hydra opt', num:true},
    {k:'delta',   t:'Points left', num:true},
    {k:'pct',     t:'% of opt',  num:true},
    {k:'fc',      t:'FC',        num:true},
    {k:'percent', t:'Percent',   num:true},
    {k:'speed',   t:'Speed',     num:true},
    {k:'rank',    t:'Rank',      num:true},
    {k:'posted',  t:'Posted',    num:false},
    {k:'status',  t:'Status',    num:false},
  ],
```

After:

```js
  cols: [
    {k:'song',    t:'Song',      num:false},
    {k:'artist',  t:'Artist',    num:false},
    {k:'charter', t:'Charter',   num:false},
    {k:'actual',  t:'Actual',    num:true,  d:'The score the player posted.'},
    {k:'optimal', t:'Hydra opt', num:true,  d:'The optimal score Hydra found for the chart at SP cap 4, the Clone Hero rule.'},
    {k:'delta',   t:'Points left', num:true, d:'Hydra opt minus Actual. Marked over when the posted score is higher.'},
    {k:'pct',     t:'% of opt',  num:true,  d:'Actual as a percent of Hydra opt. Only for scores played at 100% speed.'},
    {k:'fc',      t:'FC',        num:true,  d:'Full combo: every note hit.'},
    {k:'percent', t:'Percent',   num:true,  d:'The percent the leaderboard lists for this score.'},
    {k:'speed',   t:'Speed',     num:true,  d:'The playback speed the score was set at. 100% is normal speed.'},
    {k:'rank',    t:'Rank',      num:true,  d:'The score rank on this chart leaderboard.'},
    {k:'posted',  t:'Posted',    num:false, d:'The date the score was posted.'},
    {k:'status',  t:'Status',    num:false, d:'Matched or Above optimal when Hydra has a result. Not analyzed: the chart is in your library but has no current result for this mode at SP cap 4. Not in your library: the last scan did not find it.'},
  ],
```

The tiles. Before:

```js
    const unmatched = rows.filter(r => r.status === 'unmatched');
```

After:

```js
    const notAnalyzed = rows.filter(r => r.status === 'not analyzed');
    const notInLibrary = rows.filter(r => r.status === 'not in library');
```

And before:

```js
      ['Unmatched', unmatched.length.toLocaleString()],
```

After:

```js
      ['Not analyzed', notAnalyzed.length.toLocaleString()],
      ['Not in library', notInLibrary.length.toLocaleString()],
```

In `collect_dm_rows`, read the scanned charts once, then split the unmatched branch. Before:

```cpp
    std::vector<DmReportRow> rows;
    rows.reserve(scores.size());
```

After:

```cpp
    // Every chart the last scan found, lower-cased like the leaderboard's
    // identifiers, so a score with no current result can say whether
    // analyzing would fix it.
    std::unordered_set<std::string> in_library;
    for (const store::ChartLibraryEntry& e : store.list_chart_library(std::nullopt, 0, -1))
        in_library.insert(to_lower_ascii(e.md5));

    std::vector<DmReportRow> rows;
    rows.reserve(scores.size());
```

Before:

```cpp
        } else {
            row.status = "unmatched";
        }
```

After:

```cpp
        } else {
            row.status = in_library.count(s.identifier) ? "not analyzed" : "not in library";
        }
```

`tally_dm_rows`. Before:

```cpp
    for (const DmReportRow& r : rows) {
        if (r.status == "matched") ++stats.matched;
        else if (r.status == "above optimal") ++stats.above;
        else ++stats.unmatched;
    }
    return stats;
```

After:

```cpp
    for (const DmReportRow& r : rows) {
        if (r.status == "matched") ++stats.matched;
        else if (r.status == "above optimal") ++stats.above_optimal;
        else if (r.status == "not analyzed") ++stats.not_analyzed;
        else ++stats.not_in_library;
    }
    stats.above = stats.above_optimal;
    stats.unmatched = stats.not_analyzed + stats.not_in_library;
    return stats;
```

`generate_dm_report`'s subtitle and footer. Before:

```cpp
    std::string subtitle =
        username + " — " + group_thousands(out.stats.total) + " scores: " +
        group_thousands(out.stats.matched) + " matched, " +
        group_thousands(out.stats.above) + " above optimal, " +
        group_thousands(out.stats.unmatched) + " not in your library";
    std::string footer =
        "Actual scores from dmleaderboards.com against Hydra's optimal for " + chartmode +
        ". Above-optimal scores are expected — Hydra's optimal excludes several score "
        "backends, and older Clone Hero versions allowed fills that are impossible now.";
```

After:

```cpp
    std::string subtitle =
        username + " — " + report::counted(out.stats.total, "score", "scores") + ": " +
        group_thousands(out.stats.matched) + " matched, " +
        group_thousands(out.stats.above_optimal) + " above optimal, " +
        group_thousands(out.stats.not_analyzed) + " not analyzed, " +
        group_thousands(out.stats.not_in_library) + " not in your library";
    std::string footer =
        "Actual scores from dmleaderboards.com against Hydra's optimal for " + chartmode +
        ". Above-optimal scores are expected — Hydra's optimal excludes several score "
        "backends, and older Clone Hero versions allowed fills that are impossible now. "
        "Not analyzed charts are in your library without a current result for this mode "
        "at SP cap 4: analyze them, then compare again.";
```

- [ ] **Step 15: Run them and watch them pass.** Build as in Step 2, then `.\build-cpp\Release\hydra_tests.exe -tc="*report*,*dm*"`. Expected: `Status: SUCCESS!`.

- [ ] **Step 16: Commit.**

```powershell
git add src/app/dm_report.h src/app/dm_report.cpp tests/test_dm_report.cpp tests/test_report.cpp
git commit -m @'
Leaderboard comparison tells not analyzed from not in library

Task: T3 report pages and report location
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
'@
```

- [ ] **Step 17: Check the whole build, the GUI tests and the file list.** Build `hydra_tests`, run `.\build-cpp\Release\hydra_tests.exe` (expected `Status: SUCCESS!`, with the unit-test count from Task 0 plus the 11 new cases). Build `hydra_uitest` with `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_uitest > build.log 2>&1`, then run `.\build-cpp\Release\hydra_uitest.exe --all` (expected `[PASS]` on all 26). The two report GUI tests compare the opened path against `report_html_path()` and `dm_report_html_path()`, which follow the scratch database, so they pass unchanged. Confirm nothing was written to your real Documents: `Test-Path "$([Environment]::GetFolderPath('MyDocuments'))\Hydra\hydra_paths.html"` must not have changed its answer or timestamp since before the run. Then run `git diff --stat <base>..ui/T3`; it must list only the files in **Files**.

- [ ] **Step 18: Write sample pages for the main session's look.** Run `$env:HYDRA_PAGE_SAMPLES = "$env:TEMP\hydra_ui_T3\pages"; .\build-cpp\Release\hydra_tests.exe --no-skip -tc="report pages: write samples*"`. Expected: `paths.html`, `dm.html` and `fill.html` in that folder. Report the folder in the task summary so the main session can open them in a browser at Task 15 (header sticks, "#" column, legend, print preview). Nothing to commit.

---

### Task 4: The batch job can pause, stop and tell the time, and job errors read in plain words

Today a library batch can only be cancelled. Its progress has a count and a title, but no elapsed time and no estimate of time left. The title it shows is the chart that just finished, not the one being worked on. A report that saved fine is called "failed" when only the browser refused to open it. Cancelling a leaderboard fetch shows up as an error that says "cancelled". Raw exception text such as `add_song failed: disk I/O error` reaches the user as it is.

This task changes the jobs only, not the screens. `BatchJob` gains Pause, Resume and Stop, and its snapshot gains elapsed time, time left and the chart now being analyzed. The two report jobs record where the page was saved and whether the browser opened it, and a browser that refuses is no longer a failure. A cancelled job carries no error message. A new module, `app/user_messages`, turns every known internal error into a short message that says what happened and what to do. The old batch modal keeps compiling and working through `cancel()`, so nothing looks different until T13 draws the new strip.

**Wave:** 1. **Depends on:** Task 0. **Expected overlaps:** `CMakeLists.txt` with T1, T2, T3 and T5 (T4 adds one line to `hydra_core` and two to `hydra_tests`; keep every task's lines). T3 in `src/app/dm_report.h`: T3 renames the comparison counts, and `dm_jobs.cpp` reads them (see the merge note in Step 9). T3 in `src/app/report_files.h`: T4 keeps calling `report_html_path()` and `dm_report_html_path()`, which T3 points at `reports_dir()`; nothing to merge.

**Goal:** Give T13 everything the background batch strip, the finished strip and the plain error lines need, with no change on screen yet.

**Files:**
- Create: `src/app/user_messages.h`, `src/app/user_messages.cpp`
- Create: `src/ui/report_outcome.h`
- Modify: `src/ui/job_base.h` (the plain message on every result job; a cancelled job has none)
- Modify: `src/ui/library_jobs.h`, `src/ui/library_jobs.cpp` (BatchJob pause/resume/stop, clock, snapshot fields, a second constructor that takes the chart list; ReportJob outcome)
- Modify: `src/ui/dm_jobs.h`, `src/ui/dm_jobs.cpp` (report outcome, the whole comparison counts, cancel is quiet)
- Modify: `CMakeLists.txt` (`src/app/user_messages.cpp` after `src/app/report_files.cpp` in `hydra_core`; `tests/test_user_messages.cpp` and `tests/test_library_jobs.cpp` after `tests/test_dm_report.cpp` in `hydra_tests`)
- Test: `tests/test_user_messages.cpp`, `tests/test_library_jobs.cpp` (both new)

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="user_messages*"` passes: `add_song failed: disk I/O error` reads "Hydra couldn't save to its database (hydra.db). Check that the disk isn't full and that no other copy of Hydra is running, then try again.", and every other mapping in Step 1 holds, including the fallback.
- [ ] `hydra_tests.exe -tc="jobs:*"` passes: pausing lets the chart in flight finish and starts no new one; Stop while paused ends the run with 0 failed; the snapshot names the chart being analyzed and its artist; elapsed time freezes when the run ends; a failed chart's line is plain with the raw text kept in `failure_details`.
- [ ] `BatchClock` leaves paused time out (start 10, pause 15, resume 25, finish 40 gives 20 s), and `batch_eta_s(30, 3, 10)` is 70 s while `batch_eta_s(30, 2, 10)` is empty.
- [ ] A report the browser refuses is still written, `opened()` is false and `open_problem()` is "Windows couldn't open the report in your browser."; with auto-open off, nothing opens and `open_problem()` is empty.
- [ ] A leaderboard fetch cancelled mid-request finishes with `ok() == false` and an empty `message()`, and opens nothing, whether the server answers late or the fetch throws "cancelled".
- [ ] The whole unit suite reports `Status: SUCCESS!`, and `hydra_uitest.exe --all` passes the same 26 GUI tests as the Task 0 baseline.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="user_messages*,jobs:*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests for the plain messages.** Create `tests/test_user_messages.cpp`. The expected texts below are the wording; the implementation in Step 3 must produce them exactly.

```cpp
// Unit tests for app/user_messages: every known internal error becomes a
// short message that says what happened and what to do, and the raw text
// stays available for a details line.

#include "doctest.h"

#include <new>
#include <stdexcept>
#include <string>

#include "app/rules_file.h"
#include "app/user_messages.h"
#include "core/model.h"
#include "parse/midi.h"
#include "store/serialize.h"

using hydra::app::plain_error;
using hydra::app::plain_error_detail;
using hydra::app::plain_error_text;

namespace {

const std::string kDatabaseWrite =
    "Hydra couldn't save to its database (hydra.db). Check that the disk isn't full and "
    "that no other copy of Hydra is running, then try again.";
const std::string kChartUnreadable =
    "Hydra couldn't read this chart file. It may be damaged or in a format Hydra doesn't "
    "support; try downloading the song again.";
const std::string kSongFileMissing =
    "Hydra couldn't open the song file. It may have been moved or deleted; run Scan "
    "library to update the library.";
const std::string kNetUnreachable =
    "Hydra couldn't reach dmleaderboards. Check your internet connection and try again.";

}  // namespace

TEST_CASE("user_messages: database errors say to check the disk") {
    for (const char* raw : {"add_song failed: disk I/O error",
                            "add_row path failed: database is locked",
                            "put_dynamics failed: disk I/O error",
                            "meta_set failed: disk I/O error",
                            "reindex failed: disk I/O error",
                            "rebuild_chart_library failed: disk I/O error",
                            "sqlite exec failed: disk I/O error",
                            "prepare failed: no such table: records"}) {
        CAPTURE(raw);
        CHECK(plain_error(std::runtime_error(raw)) == kDatabaseWrite);
    }
    CHECK(plain_error(std::runtime_error("failed to open database 'C:\\x\\hydra.db': unable to open")) ==
          "Hydra couldn't open its database (hydra.db). Check that no other copy of Hydra is "
          "running and that the Hydra folder isn't read-only.");
}

TEST_CASE("user_messages: a missing or unreadable song file") {
    CHECK(plain_error(std::runtime_error("cannot open file: C:\\Songs\\x\\notes.chart")) ==
          kSongFileMissing);
    CHECK(plain_error(hydra::MidiError("cannot open MIDI file: C:\\Songs\\x\\notes.mid")) ==
          kSongFileMissing);
    CHECK(plain_error(std::runtime_error("MD5 hashing failed")) ==
          "Windows couldn't read a song file to identify it. Restart Hydra and run Scan "
          "library again.");
    CHECK(plain_error(hydra::ChartFileError("Duplicate note.")) == kChartUnreadable);
    CHECK(plain_error(hydra::MidiError("not a MIDI file: missing MThd header")) ==
          kChartUnreadable);
    CHECK(plain_error(std::runtime_error("Truncated SNG file.")) == kChartUnreadable);
    CHECK(plain_error(std::runtime_error("unexpected chart type: C:\\x\\song.txt")) ==
          kChartUnreadable);
    // A chart-file error Hydra doesn't know yet still reads as a chart problem.
    CHECK(plain_error(hydra::ChartFileError("a brand new parse failure")) == kChartUnreadable);
}

TEST_CASE("user_messages: the no-notes message is already plain and passes through") {
    const std::string msg = "No Expert Pro Drums notes in this chart.";
    CHECK(plain_error(hydra::ChartFileError(msg)) == msg);
    CHECK(plain_error_text(msg) == msg);
}

TEST_CASE("user_messages: a broken search is reported as Hydra's bug") {
    CHECK(plain_error(std::runtime_error("search reached a broken state")) ==
          "The analysis failed on this chart because of a bug in Hydra. Please report it "
          "with the song's name.");
}

TEST_CASE("user_messages: leaderboard errors") {
    CHECK(plain_error(std::runtime_error("could not send the request (error 12029)")) ==
          kNetUnreachable);
    CHECK(plain_error(std::runtime_error("could not connect to the leaderboard (error 12007)")) ==
          kNetUnreachable);
    CHECK(plain_error(std::runtime_error("no response from the leaderboard (error 12002)")) ==
          "dmleaderboards didn't answer in time. Its server may be waking up; try again in "
          "a minute.");
    CHECK(plain_error(std::runtime_error("leaderboard returned HTTP 503")) ==
          "dmleaderboards returned an error (HTTP 503). Try again later.");
    CHECK(plain_error(std::runtime_error("the leaderboard sent a response Hydra couldn't read")) ==
          "dmleaderboards sent a reply Hydra couldn't read. Try again later.");
    CHECK(plain_error(std::runtime_error("this user has no scores to compare")) ==
          "This player has no drum scores on dmleaderboards to compare.");
}

TEST_CASE("user_messages: report, rules, stored results, memory") {
    CHECK(plain_error(std::runtime_error("no records stored yet")) ==
          "There are no analyzed songs to put in a report yet. Analyze some songs first.");
    CHECK(plain_error(std::runtime_error("cannot write C:\\Users\\x\\Documents\\Hydra\\hydra_paths.html")) ==
          "Hydra couldn't save the report file. Check that the disk isn't full and the "
          "report folder isn't read-only.");
    CHECK(plain_error(hydra::app::RulesFileError("hydra_rules.ini:3: unknown key \"x\"")) ==
          "hydra_rules.ini has a line Hydra can't read. Fix or delete that line, then "
          "restart Hydra.");
    CHECK(plain_error(hydra::store::SerializeError("truncated blob")) ==
          "A saved result couldn't be read. Re-analyze this song to replace it.");
    CHECK(plain_error(std::bad_alloc()) ==
          "Hydra ran out of memory on this chart. Close other programs and try again.");
    CHECK(plain_error(std::runtime_error("decode_audio: opus_decode failed")) ==
          "Hydra couldn't decode this song's audio files. They may be damaged; try "
          "downloading the song again.");
    CHECK(plain_error(std::runtime_error("PreviewRenderer: missing texture x.png")) ==
          "Some of Hydra's Preview files are missing. Reinstall Hydra to restore them.");
}

TEST_CASE("user_messages: anything else falls back, and the detail keeps the raw text") {
    const std::runtime_error odd("prepare_row: key asks for sp_cap 5");
    CHECK(plain_error(odd) == hydra::app::kSomethingWentWrong);
    CHECK(std::string(hydra::app::kSomethingWentWrong) ==
          "Something went wrong. Try again, and if it keeps happening, report it with the "
          "details below.");
    CHECK(plain_error_detail(odd) == "prepare_row: key asks for sp_cap 5");
    CHECK(plain_error_text("cancelled") == "Stopped before it finished.");
}
```

- [ ] **Step 2: Add the test file to the build and watch it fail.** In `CMakeLists.txt`, in the `hydra_tests` list, add two lines after `tests/test_dm_report.cpp`:

```cmake
    tests/test_dm_report.cpp
    tests/test_user_messages.cpp
    tests/test_library_jobs.cpp
```

Create `tests/test_library_jobs.cpp` now with only `#include "doctest.h"` so the build finds it; Step 5 fills it. Build with the Bash tool from the worktree root: `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1`. Expected: the build fails, because `app/user_messages.h` does not exist yet.

- [ ] **Step 3: Write `app/user_messages`.** Create `src/app/user_messages.h`:

```cpp
// Plain-English versions of the errors Hydra's jobs can hit. The raw text of
// an exception ("add_song failed: disk I/O error") is for a small details
// line. What the user reads first is a short message that says what
// happened and what to do. Every job and view that shows an error takes the
// wording from here, so it lives in one place.

#ifndef HYDRA_APP_USER_MESSAGES_H
#define HYDRA_APP_USER_MESSAGES_H

#include <exception>
#include <string>
#include <string_view>

namespace hydra::app {

// What every error Hydra doesn't recognize reads as.
inline constexpr const char* kSomethingWentWrong =
    "Something went wrong. Try again, and if it keeps happening, report it with the "
    "details below.";

// What happened and what to do, in at most two short sentences. Uses the
// exception's type where it says more than its text (a chart-file error
// Hydra has no specific wording for still reads as a chart problem).
std::string plain_error(const std::exception& e);

// The same mapping for an error that arrives as text only. run_batch hands
// its failures to the job as strings, so the batch uses this one.
std::string plain_error_text(std::string_view what);

// The raw text, for a small details line under the plain message.
std::string plain_error_detail(const std::exception& e);

}  // namespace hydra::app

#endif  // HYDRA_APP_USER_MESSAGES_H
```

Create `src/app/user_messages.cpp`. Every raw string it matches was found with `rg "runtime_error\(|throw \w+Error\("` over `src/` on 2026-09-27; the comment beside each group names where it is thrown.

```cpp
#include "app/user_messages.h"

#include <initializer_list>
#include <new>

#include "app/rules_file.h"
#include "core/model.h"
#include "parse/midi.h"
#include "store/serialize.h"

namespace hydra::app {

namespace {

constexpr const char* kDatabaseWrite =
    "Hydra couldn't save to its database (hydra.db). Check that the disk isn't full and "
    "that no other copy of Hydra is running, then try again.";
constexpr const char* kDatabaseOpen =
    "Hydra couldn't open its database (hydra.db). Check that no other copy of Hydra is "
    "running and that the Hydra folder isn't read-only.";
constexpr const char* kChartUnreadable =
    "Hydra couldn't read this chart file. It may be damaged or in a format Hydra doesn't "
    "support; try downloading the song again.";
constexpr const char* kSongFileMissing =
    "Hydra couldn't open the song file. It may have been moved or deleted; run Scan "
    "library to update the library.";
constexpr const char* kHashFailed =
    "Windows couldn't read a song file to identify it. Restart Hydra and run Scan "
    "library again.";
constexpr const char* kSearchBroken =
    "The analysis failed on this chart because of a bug in Hydra. Please report it with "
    "the song's name.";
constexpr const char* kNetUnreachable =
    "Hydra couldn't reach dmleaderboards. Check your internet connection and try again.";
constexpr const char* kNetTimeout =
    "dmleaderboards didn't answer in time. Its server may be waking up; try again in a "
    "minute.";
constexpr const char* kNetBadReply =
    "dmleaderboards sent a reply Hydra couldn't read. Try again later.";
constexpr const char* kNoScores =
    "This player has no drum scores on dmleaderboards to compare.";
constexpr const char* kNoRecords =
    "There are no analyzed songs to put in a report yet. Analyze some songs first.";
constexpr const char* kReportWrite =
    "Hydra couldn't save the report file. Check that the disk isn't full and the report "
    "folder isn't read-only.";
constexpr const char* kRulesFile =
    "hydra_rules.ini has a line Hydra can't read. Fix or delete that line, then restart "
    "Hydra.";
constexpr const char* kStoredResult =
    "A saved result couldn't be read. Re-analyze this song to replace it.";
constexpr const char* kOutOfMemory =
    "Hydra ran out of memory on this chart. Close other programs and try again.";
constexpr const char* kAudioDecode =
    "Hydra couldn't decode this song's audio files. They may be damaged; try downloading "
    "the song again.";
constexpr const char* kPreviewAssets =
    "Some of Hydra's Preview files are missing. Reinstall Hydra to restore them.";
constexpr const char* kStopped = "Stopped before it finished.";

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}
bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}
bool starts_with_any(std::string_view s, std::initializer_list<std::string_view> prefixes) {
    for (std::string_view p : prefixes)
        if (starts_with(s, p)) return true;
    return false;
}

// parse/song.cpp no_notes_message: "No <difficulty> [Pro ]Drums notes in this
// chart." It is already written for the user.
bool is_no_notes_message(std::string_view s) {
    return starts_with(s, "No ") && ends_with(s, " notes in this chart.");
}

}  // namespace

std::string plain_error_text(std::string_view what) {
    // A cancel: net/dmbot_client.cpp and ui/job_base.h's JobCancelled.
    if (what == "cancelled") return kStopped;

    // net/dmbot_client.cpp: fail() appends " (error <GetLastError>)" to these.
    // 12002 is ERROR_WINHTTP_TIMEOUT.
    if (what.find("(error 12002)") != std::string_view::npos) return kNetTimeout;
    if (starts_with_any(what, {"malformed leaderboard URL", "could not start the network session",
                               "could not connect to the leaderboard", "could not build the request",
                               "could not send the request", "no response from the leaderboard",
                               "could not read the response"}))
        return kNetUnreachable;
    if (starts_with(what, "leaderboard returned HTTP ")) {
        std::string_view code = what.substr(std::string_view("leaderboard returned HTTP ").size());
        return "dmleaderboards returned an error (HTTP " + std::string(code) + "). Try again later.";
    }
    if (what == "the leaderboard sent a response Hydra couldn't read" ||
        what == "unexpected user-list format")
        return kNetBadReply;
    // ui/dm_jobs.cpp and ui/library_jobs.cpp.
    if (what == "this user has no scores to compare") return kNoScores;
    if (what == "no records stored yet") return kNoRecords;
    // app/report_files.cpp write_report_file.
    if (starts_with(what, "cannot write ")) return kReportWrite;

    // app/analysis.cpp stream_md5, core/winstr.cpp, parse/midi.cpp.
    if (starts_with_any(what, {"cannot open file: ", "cannot open MIDI file: "}))
        return kSongFileMissing;
    if (what == "MD5 hashing failed" || starts_with(what, "BCryptOpenAlgorithmProvider(MD5)"))
        return kHashFailed;

    // store/record_store.cpp.
    if (starts_with(what, "failed to open database ")) return kDatabaseOpen;
    if (starts_with_any(what, {"add_song failed", "add_row ", "put_dynamics failed",
                               "meta_set failed", "reindex failed",
                               "rebuild_chart_library failed", "sqlite exec failed",
                               "prepare failed"}))
        return kDatabaseWrite;

    // search/engine.cpp.
    if (what == "search reached a broken state") return kSearchBroken;

    // The chart readers: core/model.cpp, parse/song.cpp, parse/srb.cpp,
    // parse/midi.cpp. The no-notes message is already plain.
    if (is_no_notes_message(what)) return std::string(what);
    if (what == "Duplicate note." || what == "expected a [section] header" ||
        what == "No chart files found in SNG file." || what == "Truncated SNG file." ||
        what == "Truncated SRB file." || what == "SMPTE time division is not supported" ||
        starts_with_any(what, {"unexpected chart type: ", "SRB stream", "SRB inflate",
                               "not a MIDI file", "Message length "}))
        return kChartUnreadable;

    // store/serialize.cpp and store/path_codec.cpp.
    if (what == "truncated blob" ||
        starts_with_any(what, {"path node ", "unsupported path node format",
                               "unsupported path structure format"}))
        return kStoredResult;

    // audio/decode.cpp, audio/mixer.cpp, render/preview_renderer.cpp,
    // render/preview_config.cpp.
    if (starts_with_any(what, {"decode_audio:", "mix_stems:"})) return kAudioDecode;
    if (starts_with_any(what, {"PreviewRenderer: missing", "3d-config.json:"}))
        return kPreviewAssets;

    return kSomethingWentWrong;
}

std::string plain_error(const std::exception& e) {
    if (dynamic_cast<const std::bad_alloc*>(&e)) return kOutOfMemory;
    if (dynamic_cast<const RulesFileError*>(&e)) return kRulesFile;
    std::string text = plain_error_text(e.what());
    if (text != kSomethingWentWrong) return text;
    // Types whose every message means the same thing to the user.
    if (dynamic_cast<const ChartFileError*>(&e) || dynamic_cast<const MidiError*>(&e))
        return kChartUnreadable;
    if (dynamic_cast<const store::SerializeError*>(&e)) return kStoredResult;
    return text;
}

std::string plain_error_detail(const std::exception& e) { return e.what(); }

}  // namespace hydra::app
```

In `CMakeLists.txt`, add the source to `hydra_core` after `src/app/report_files.cpp`:

```cmake
    src/app/report_files.cpp
    src/app/user_messages.cpp
)
```

- [ ] **Step 4: Run the message tests.** Build `hydra_tests` as in Step 2, then `.\build-cpp\Release\hydra_tests.exe -tc="user_messages*"`. Expected: `Status: SUCCESS!`. If one mapping fails, fix the table in `user_messages.cpp`, not the test's expected text; the test is the wording.

- [ ] **Step 5: Write the failing job tests.** Replace the stub `tests/test_library_jobs.cpp` with:

```cpp
// Unit tests for the library and leaderboard jobs: Pause, Stop, the batch
// clock and time-left estimate, the chart being analyzed, plain failure
// lines, report pages the browser refuses, and quiet cancels. The analyzer
// and the network are fakes, so nothing here reads a chart or goes online.

#include "doctest.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/analysis.h"
#include "app/report_files.h"
#include "net/dmbot_client.h"
#include "store/record_store.h"
#include "ui/dm_jobs.h"
#include "ui/library_jobs.h"
#include "ui/report_outcome.h"

using hydra::app::AnalysisResult;
using hydra::app::AnalysisSettings;
using hydra::app::BatchRun;
using hydra::store::ChartLibraryEntry;
using hydra::store::RecordStore;
using hydra::ui::BatchClock;
using hydra::ui::BatchJob;

namespace {

using namespace std::chrono_literals;

template <class Pred>
bool wait_until(Pred pred, std::chrono::milliseconds limit = 10s) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

std::vector<ChartLibraryEntry> fake_charts(int n) {
    std::vector<ChartLibraryEntry> charts;
    for (int i = 0; i < n; ++i) {
        ChartLibraryEntry e;
        e.md5 = "fake" + std::to_string(i);
        e.title = "fake " + std::to_string(i);
        e.artist = "artist " + std::to_string(i);
        e.notespath = "fake_" + std::to_string(i) + ".chart";
        charts.push_back(e);
    }
    return charts;
}

BatchRun test_run() {
    BatchRun run;
    run.chartmode = "jobs-test";
    return run;
}

// A chart that counts itself, waits for `release`, then fails with `error`.
hydra::app::ChartAnalyzer gated_failure(std::atomic<int>& started, std::atomic<bool>& release,
                                        std::string error) {
    return [&started, &release, error](const std::string&, const AnalysisSettings&,
                                       const std::function<void(float)>&) -> AnalysisResult {
        ++started;
        while (!release.load()) std::this_thread::sleep_for(1ms);
        throw std::runtime_error(error);
    };
}

}  // namespace

TEST_CASE("jobs: the batch clock leaves paused time out") {
    BatchClock clock;
    clock.start(10.0);
    clock.pause(15.0);
    CHECK(clock.elapsed_s(20.0) == doctest::Approx(5.0));
    CHECK(clock.paused());
    clock.resume(25.0);
    CHECK(clock.elapsed_s(30.0) == doctest::Approx(10.0));
    clock.finish(40.0);
    CHECK(clock.elapsed_s(100.0) == doctest::Approx(20.0));

    BatchClock stopped_while_paused;
    stopped_while_paused.start(0.0);
    stopped_while_paused.pause(5.0);
    stopped_while_paused.finish(9.0);
    CHECK(stopped_while_paused.elapsed_s(50.0) == doctest::Approx(5.0));
    CHECK_FALSE(stopped_while_paused.paused());
}

TEST_CASE("jobs: time left needs three finished charts") {
    CHECK_FALSE(hydra::ui::batch_eta_s(30.0, 2, 10).has_value());
    REQUIRE(hydra::ui::batch_eta_s(30.0, 3, 10).has_value());
    CHECK(*hydra::ui::batch_eta_s(30.0, 3, 10) == doctest::Approx(70.0));
    CHECK(*hydra::ui::batch_eta_s(30.0, 10, 10) == doctest::Approx(0.0));
    CHECK_FALSE(hydra::ui::batch_eta_s(30.0, 3, 0).has_value());
}

TEST_CASE("jobs: pause lets the chart in flight finish and starts no new one") {
    std::atomic<int> started{0};
    std::atomic<bool> release{false};
    RecordStore store(":memory:");
    BatchJob job(fake_charts(3), test_run(), store, /*redo=*/false);
    job.set_analyzer_for_test(gated_failure(started, release, "MD5 hashing failed"),
                              /*workers=*/1);
    job.start();
    REQUIRE(wait_until([&] { return started.load() == 1; }));

    job.pause();
    release = true;
    REQUIRE(wait_until([&] { return job.snapshot().completed == 1; }));
    std::this_thread::sleep_for(200ms);
    CHECK(started.load() == 1);  // nothing new started while paused
    BatchJob::Snapshot paused = job.snapshot();
    CHECK(paused.paused);
    CHECK_FALSE(paused.eta_s.has_value());

    job.resume();
    REQUIRE(wait_until([&] { return job.snapshot().finished; }));
    BatchJob::Snapshot done = job.snapshot();
    CHECK(started.load() == 3);
    CHECK(done.completed == 3);
    CHECK(done.failed == 3);
    CHECK_FALSE(done.paused);
}

TEST_CASE("jobs: stop while paused ends the run and fails nothing") {
    std::atomic<int> started{0};
    std::atomic<bool> release{true};
    RecordStore store(":memory:");
    BatchJob job(fake_charts(4), test_run(), store, /*redo=*/false);
    job.set_analyzer_for_test(gated_failure(started, release, "MD5 hashing failed"),
                              /*workers=*/2);
    job.pause();
    job.start();
    std::this_thread::sleep_for(200ms);
    CHECK(started.load() == 0);
    CHECK(job.snapshot().paused);

    job.stop();
    REQUIRE(wait_until([&] { return job.snapshot().finished; }, 5s));
    BatchJob::Snapshot s = job.snapshot();
    CHECK(started.load() == 0);
    CHECK(s.completed == 0);
    CHECK(s.failed == 0);
    CHECK(s.failures.empty());
    CHECK(job.is_cancelled());
}

TEST_CASE("jobs: the snapshot names the chart being analyzed and freezes its clock at the end") {
    std::atomic<int> started{0};
    std::atomic<bool> release{false};
    RecordStore store(":memory:");
    BatchJob job(fake_charts(1), test_run(), store, /*redo=*/false);
    job.set_analyzer_for_test(gated_failure(started, release, "MD5 hashing failed"),
                              /*workers=*/1);
    job.start();
    REQUIRE(wait_until([&] { return started.load() == 1; }));

    BatchJob::Snapshot running = job.snapshot();
    CHECK(running.current_title == "fake 0");
    CHECK(running.current_artist == "artist 0");
    CHECK(running.elapsed_s >= 0.0);
    CHECK_FALSE(running.eta_s.has_value());

    release = true;
    REQUIRE(wait_until([&] { return job.snapshot().finished; }));
    BatchJob::Snapshot done = job.snapshot();
    CHECK(done.current_title.empty());
    CHECK(done.current_artist.empty());
    std::this_thread::sleep_for(50ms);
    CHECK(job.snapshot().elapsed_s == done.elapsed_s);
}

TEST_CASE("jobs: a failed chart reads in plain words and keeps the raw text") {
    std::atomic<int> started{0};
    std::atomic<bool> release{true};
    RecordStore store(":memory:");
    BatchJob job(fake_charts(1), test_run(), store, /*redo=*/false);
    job.set_analyzer_for_test(
        gated_failure(started, release, "cannot open file: C:\\Songs\\x\\notes.chart"), 1);
    job.start();
    REQUIRE(wait_until([&] { return job.snapshot().finished; }));
    BatchJob::Snapshot s = job.snapshot();
    REQUIRE(s.failures.size() == 1);
    REQUIRE(s.failure_details.size() == 1);
    CHECK(s.failures[0] ==
          "fake 0: Hydra couldn't open the song file. It may have been moved or deleted; "
          "run Scan library to update the library.");
    CHECK(s.failure_details[0] == "fake 0: cannot open file: C:\\Songs\\x\\notes.chart");
}

TEST_CASE("jobs: a report the browser refuses is saved, not failed") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "hydra_jobs_test_report";
    std::filesystem::create_directories(dir);
    const std::filesystem::path page = dir / "report.html";

    int opens = 0;
    bool browser_ok = false;
    hydra::app::set_open_in_browser([&](const std::wstring&) {
        ++opens;
        return browser_ok;
    });

    hydra::ui::ReportOutcome refused = hydra::ui::publish_report(page, "<p>x</p>", true);
    CHECK(std::filesystem::exists(page));
    CHECK(opens == 1);
    CHECK(refused.saved_path == page);
    CHECK_FALSE(refused.opened);
    CHECK(refused.open_problem == "Windows couldn't open the report in your browser.");

    browser_ok = true;
    hydra::ui::ReportOutcome opened = hydra::ui::publish_report(page, "<p>y</p>", true);
    CHECK(opens == 2);
    CHECK(opened.opened);
    CHECK(opened.open_problem.empty());

    hydra::ui::ReportOutcome quiet = hydra::ui::publish_report(page, "<p>z</p>", false);
    CHECK(opens == 2);  // auto-open off: the browser is never asked
    CHECK_FALSE(quiet.opened);
    CHECK(quiet.open_problem.empty());

    hydra::app::set_open_in_browser({});
    std::filesystem::remove_all(dir);
}

TEST_CASE("jobs: a cancelled leaderboard fetch is not an error") {
    int opens = 0;
    hydra::app::set_open_in_browser([&](const std::wstring&) {
        ++opens;
        return true;
    });
    RecordStore store(":memory:");

    // What the WinHTTP fetch does when cancel lands between read chunks.
    hydra::net::set_fetcher([](const std::string&, const std::atomic<bool>* cancel) -> std::string {
        while (!cancel->load()) std::this_thread::sleep_for(1ms);
        throw std::runtime_error("cancelled");
    });
    hydra::ui::DmReportJob thrown(store, "1", "someone", "jobs-test", hydra::store::Lens{}, true);
    thrown.start();
    thrown.cancel();
    REQUIRE(wait_until([&] { return thrown.finished(); }));
    CHECK_FALSE(thrown.ok());
    CHECK(thrown.message().empty());

    // The server answers after the cancel: nothing is written or opened.
    hydra::net::set_fetcher([](const std::string&, const std::atomic<bool>* cancel) -> std::string {
        while (!cancel->load()) std::this_thread::sleep_for(1ms);
        return "{}";
    });
    hydra::ui::DmReportJob late(store, "1", "someone", "jobs-test", hydra::store::Lens{}, true);
    late.start();
    late.cancel();
    REQUIRE(wait_until([&] { return late.finished(); }));
    CHECK_FALSE(late.ok());
    CHECK(late.message().empty());
    CHECK(opens == 0);

    hydra::net::set_fetcher({});
    hydra::app::set_open_in_browser({});
}

TEST_CASE("jobs: a failed leaderboard fetch says what to do") {
    hydra::net::set_fetcher([](const std::string&, const std::atomic<bool>*) -> std::string {
        throw std::runtime_error("could not send the request (error 12029)");
    });
    hydra::ui::DmFetchUsersJob job;
    job.start();
    REQUIRE(wait_until([&] { return job.finished(); }));
    CHECK_FALSE(job.ok());
    CHECK(job.message() ==
          "Hydra couldn't reach dmleaderboards. Check your internet connection and try again.");
    CHECK(job.error() == "could not send the request (error 12029)");
    hydra::net::set_fetcher({});
}
```

Build `hydra_tests`. Expected: the build fails on `BatchClock`, `set_analyzer_for_test`, `ui/report_outcome.h` and `message()`.

- [ ] **Step 6: Give every result job a plain message, and keep a cancel quiet.** In `src/ui/job_base.h`, add the include after `#include <thread>`:

```cpp
#include <thread>

#include "app/user_messages.h"
```

Replace the `ResultJobBase` class:

```cpp
// Adds the ok/error result surface and the guarded-run tail shared by the
// jobs that produce one result instead of a mutex-guarded snapshot.
class ResultJobBase : public JobBase {
public:
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

protected:
    // Runs the job body; f returns whether the job succeeded. Any escaping
    // exception becomes the job's error text. Always publishes finished.
    template <class F>
    void run_guarded(F&& f) {
        try {
            ok_ = f();
        } catch (const std::exception& e) {
            error_ = e.what();
            ok_ = false;
        }
        finished_.store(true);
    }

    bool ok_ = false;
    std::string error_;
};
```

with:

```cpp
// Adds the ok/error result surface and the guarded-run tail shared by the
// jobs that produce one result instead of a mutex-guarded snapshot.
class ResultJobBase : public JobBase {
public:
    bool ok() const { return ok_; }
    // The raw exception text, for a small details line. Unchanged from
    // before: a cancelled job still reads "cancelled" here.
    const std::string& error() const { return error_; }
    // What the user reads: app::plain_error of the exception. Empty when the
    // job succeeded or was cancelled, because a cancel is the user's own
    // click and needs no message.
    const std::string& message() const { return message_; }

protected:
    // Runs the job body; f returns whether the job succeeded. Any escaping
    // exception becomes the job's error text and plain message. Always
    // publishes finished.
    template <class F>
    void run_guarded(F&& f) {
        try {
            ok_ = f();
        } catch (const std::exception& e) {
            error_ = e.what();
            if (!is_cancelled()) message_ = app::plain_error(e);
            ok_ = false;
        }
        finished_.store(true);
    }

    bool ok_ = false;
    std::string error_;
    std::string message_;
};
```

In `src/ui/library_jobs.cpp`, `AnalyzeJob::start`'s catch sets the message too:

```cpp
    } catch (const std::exception& e) {
        error_ = e.what();
        ok_ = false;
        finished_.store(true);
    }
```

becomes:

```cpp
    } catch (const std::exception& e) {
        error_ = e.what();
        message_ = app::plain_error(e);
        ok_ = false;
        finished_.store(true);
    }
```

`error()` keeps its old text on purpose. `tests/test_preview_controller.cpp` checks `job.error() == "cancelled"`, and the details, Preview and Dynamics tabs print `error()` today; T9, T11 and T13 switch them to `message()` with `error()` as the detail line.

- [ ] **Step 7: Add the report outcome.** Create `src/ui/report_outcome.h`:

```cpp
// Where a report job put its page and whether the browser took it. Shared by
// ReportJob (ui/library_jobs.h) and DmReportJob (ui/dm_jobs.h). Saving the
// page is the job; opening it is a courtesy, so a browser that refuses is
// recorded here and never fails the job.

#ifndef HYDRA_UI_REPORT_OUTCOME_H
#define HYDRA_UI_REPORT_OUTCOME_H

#include <filesystem>
#include <string>

#include "app/report_files.h"

namespace hydra::ui {

// The one sentence a report job gives when the browser won't open its page.
// The finished strip (T13) puts the saved path and "Open report" around it.
inline constexpr const char* kReportOpenProblem =
    "Windows couldn't open the report in your browser.";

struct ReportOutcome {
    std::filesystem::path saved_path;
    bool opened = false;       // the browser took the page
    std::string open_problem;  // empty when it opened or wasn't asked to open
};

// Writes a finished page to `path` (a failed write throws, and the job fails)
// and, when `open`, hands it to the browser.
inline ReportOutcome publish_report(const std::filesystem::path& path, const std::string& html,
                                    bool open) {
    app::write_report_file(path, html);
    ReportOutcome out;
    out.saved_path = path;
    if (open) {
        out.opened = app::open_in_browser(path.wstring());
        if (!out.opened) out.open_problem = kReportOpenProblem;
    }
    return out;
}

}  // namespace hydra::ui

#endif  // HYDRA_UI_REPORT_OUTCOME_H
```

In `src/ui/library_jobs.h`, add `#include "ui/report_outcome.h"` after `#include "ui/job_base.h"`, and give `ReportJob` its three accessors and the outcome member. Replace:

```cpp
    ~ReportJob() { shutdown(); }

    void start();

private:
    void run();

    store::RecordStore& store_;
    store::CapQuery cap_;
    store::Lens lens_;
    bool open_when_done_;
    int hit_window_ms_;
};
```

with:

```cpp
    ~ReportJob() { shutdown(); }

    void start();

    // Valid once finished() && ok(): where the page was written, whether the
    // browser opened it, and why not when it was asked to and didn't.
    const std::filesystem::path& saved_path() const { return outcome_.saved_path; }
    bool opened() const { return outcome_.opened; }
    const std::string& open_problem() const { return outcome_.open_problem; }

private:
    void run();

    store::RecordStore& store_;
    store::CapQuery cap_;
    store::Lens lens_;
    bool open_when_done_;
    int hit_window_ms_;
    ReportOutcome outcome_;
};
```

In `src/ui/library_jobs.cpp`, `ReportJob::run` stops throwing when the browser refuses. Replace:

```cpp
        std::filesystem::path outpath = app::report_html_path();
        app::write_report_file(outpath, report.html);
        if (open_when_done_ && !app::open_in_browser(outpath.wstring()))
            throw std::runtime_error("could not open " + outpath.u8string());
        return true;
```

with:

```cpp
        // A browser that won't open the page is not a failed report: the
        // page is saved, and the finished strip says so (audit B1).
        outcome_ = publish_report(app::report_html_path(), report.html, open_when_done_);
        return true;
```

- [ ] **Step 8: Give BatchJob its clock, Pause, Resume and Stop.** Pause works by gating the analyzer, so `app/analysis.cpp` does not change. `run_batch` already accepts a replacement analyzer (`BatchCallbacks::analyze`). The job wraps the real one: each worker, before it starts a chart, waits while the job is paused. A worker already inside a chart is past that gate, so it finishes and its result is stored. Stop is today's cancel: it sets the flag `run_batch` already watches, and it also wakes any worker waiting at the gate, which then gives up the chart as "stopped", neither stored nor failed.

Elapsed time is wall-clock time since `start()`, with paused stretches left out, and it freezes when the run ends. Time left is the average wall time per finished chart so far, times the charts still to go. Parallel workers are already inside that average: with 7 workers, 7 charts finish in roughly the time of one, so the average is wall time per chart, not CPU time per chart. The early estimate runs high, because the first few charts finish before the other workers' first charts do, and it settles once every worker has finished one. It is empty until 3 charts have finished (the fixed interface), while paused (the clock is stopped, so a countdown would be wrong), and once the run has ended.

In `src/ui/library_jobs.h`, add these includes after `#include <atomic>`:

```cpp
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
```

(replacing the existing `<atomic>`, `<mutex>`, `<optional>`, `<string>`, `<vector>` lines). Then replace the whole `// ---- BatchJob` section, from its comment line down to the line before `// ---- AnalyzeJob`, with:

```cpp
// ---- BatchJob ---------------------------------------------------------

// How long a batch has been working, with paused stretches left out. Times
// are seconds on any steady clock: BatchJob feeds it steady_clock readings,
// tests feed it plain numbers.
class BatchClock {
public:
    void start(double now);
    void pause(double now);   // does nothing unless running
    void resume(double now);  // does nothing unless paused
    void finish(double now);  // freezes the total; a paused clock stops at its pause
    double elapsed_s(double now) const;
    bool paused() const { return paused_at_.has_value(); }

private:
    std::optional<double> started_, paused_at_, finished_at_;
    double paused_total_ = 0.0;
};

// How many charts must finish before a time-left estimate shows.
inline constexpr int kEtaMinFinished = 3;

// Time left: the average wall time per finished chart so far, times the
// charts still to go. Empty until kEtaMinFinished charts have finished.
std::optional<double> batch_eta_s(double elapsed_s, int completed, int total);

class BatchJob : public JobBase {
public:
    // The job queries the library itself (filtered by `search`, like the
    // table) on its own thread — loading thousands of rows synchronously
    // before the progress modal appeared froze the UI for the whole query.
    BatchJob(std::optional<std::string> search, app::BatchRun run,
             store::RecordStore& store, bool redo);
    // Analyzes exactly these charts, in this order. The library screen's own
    // search (T12) decides which rows match, so "Analyze search (N)..." hands
    // over the N rows it shows instead of a search string SQL would read
    // differently.
    BatchJob(std::vector<store::ChartLibraryEntry> charts, app::BatchRun run,
             store::RecordStore& store, bool redo);
    ~BatchJob() {
        stop();
        shutdown();
    }

    // Test seam: what analyzes one chart, and how many workers run. Call
    // before start().
    void set_analyzer_for_test(app::ChartAnalyzer analyze, int workers);

    void start();

    // Pause stops handing out new charts. Charts already being analyzed
    // finish and are stored. Resume carries on. Both may be called before
    // start(); neither does anything once the run has finished.
    void pause();
    void resume();
    // Ends the run. Results already stored stay stored. A chart in the middle
    // of its analysis stops at its next progress tick and is neither stored
    // nor counted as failed. Works while paused.
    void stop();
    // The old name for stop(), kept until T13 moves the batch modal's
    // callers to stop().
    void cancel() { stop(); }

    struct Snapshot {
        bool preparing = true;  // still loading the item list from the store
        int total = 0;      // items actually dispatched (excludes pre-skipped)
        int completed = 0;  // finished charts, stored or failed
        int skipped = 0;    // already stored, known up front (not part of total)
        int failed = 0;
        bool paused = false;
        // Wall time since start(), paused time left out; frozen once finished.
        double elapsed_s = 0.0;
        // Seconds left. Empty until kEtaMinFinished charts finish, while
        // paused, and once finished.
        std::optional<double> eta_s;
        // The chart a worker started most recently. Empty once finished.
        std::string current_title;
        std::string current_artist;
        std::vector<std::string> failures;         // "Title: plain message"
        std::vector<std::string> failure_details;  // "Title: raw error", same order
        bool finished = false;
    };
    Snapshot snapshot() const;

private:
    void run();
    // On a worker, before each chart: waits while paused. Throws
    // app::AnalysisCancelled once stopped, so the chart counts as stopped.
    void wait_while_paused();
    // On a worker: records the chart it is starting as the current one.
    void note_started(const std::string& notespath);

    std::optional<std::string> search_;
    std::optional<std::vector<store::ChartLibraryEntry>> given_;
    std::vector<app::ScanItem> items_;
    // notespath -> index into items_. Built before run_batch starts and only
    // read after that, so workers read it without a lock.
    std::unordered_map<std::string, size_t> by_path_;
    app::BatchRun run_;
    store::RecordStore& store_;
    bool redo_;
    int workers_;
    app::ChartAnalyzer analyze_;  // empty = app::analyze_chart_file

    std::mutex pause_mu_;
    std::condition_variable pause_cv_;
    bool paused_ = false;  // guarded by pause_mu_

    mutable std::mutex mu_;
    Snapshot snap_;     // guarded by mu_
    BatchClock clock_;  // guarded by mu_
};
```

In `src/ui/library_jobs.cpp`, replace the includes at the top:

```cpp
#include "ui/library_jobs.h"

#include <filesystem>
#include <stdexcept>

#include "app/config.h"
#include "app/report.h"
#include "app/report_files.h"
```

with:

```cpp
#include "ui/library_jobs.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>

#include "app/config.h"
#include "app/report.h"
#include "app/report_files.h"
#include "app/user_messages.h"
```

Then replace the whole `// ---- BatchJob` section of `library_jobs.cpp`, from its comment line down to the line before `// ---- AnalyzeJob`, with the code below. If the Edit hook refuses the replacement because too little of the old block survives, write the full new `library_jobs.cpp` to `src/ui/library_jobs.cpp.new` with the Write tool and move it over with `Move-Item -Force`.

```cpp
// ---- BatchJob ---------------------------------------------------------

namespace {

double steady_seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

void BatchClock::start(double now) {
    started_ = now;
    paused_at_.reset();
    finished_at_.reset();
    paused_total_ = 0.0;
}

void BatchClock::pause(double now) {
    if (started_ && !paused_at_ && !finished_at_) paused_at_ = now;
}

void BatchClock::resume(double now) {
    if (!paused_at_ || finished_at_) return;
    paused_total_ += now - *paused_at_;
    paused_at_.reset();
}

void BatchClock::finish(double now) {
    if (!started_ || finished_at_) return;
    finished_at_ = paused_at_ ? *paused_at_ : now;
    paused_at_.reset();
}

double BatchClock::elapsed_s(double now) const {
    if (!started_) return 0.0;
    const double end = finished_at_ ? *finished_at_ : paused_at_ ? *paused_at_ : now;
    return std::max(0.0, end - *started_ - paused_total_);
}

std::optional<double> batch_eta_s(double elapsed_s, int completed, int total) {
    if (total <= 0 || completed < kEtaMinFinished || completed > total) return std::nullopt;
    return elapsed_s / completed * (total - completed);
}

BatchJob::BatchJob(std::optional<std::string> search, app::BatchRun run,
                   store::RecordStore& store, bool redo)
    : search_(std::move(search)),
      run_(std::move(run)),
      store_(store),
      redo_(redo),
      workers_(app::batch_worker_count()) {}

BatchJob::BatchJob(std::vector<store::ChartLibraryEntry> charts, app::BatchRun run,
                   store::RecordStore& store, bool redo)
    : given_(std::move(charts)),
      run_(std::move(run)),
      store_(store),
      redo_(redo),
      workers_(app::batch_worker_count()) {}

void BatchJob::set_analyzer_for_test(app::ChartAnalyzer analyze, int workers) {
    analyze_ = std::move(analyze);
    workers_ = std::max(1, workers);
}

void BatchJob::start() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        const double now = steady_seconds();
        clock_.start(now);
        if (snap_.paused) clock_.pause(now);  // paused before it started
    }
    spawn([this] { run(); });
}

void BatchJob::pause() {
    {
        std::lock_guard<std::mutex> lock(pause_mu_);
        if (paused_) return;
        paused_ = true;
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (snap_.finished) return;
    snap_.paused = true;
    clock_.pause(steady_seconds());
}

void BatchJob::resume() {
    {
        std::lock_guard<std::mutex> lock(pause_mu_);
        if (!paused_) return;
        paused_ = false;
    }
    pause_cv_.notify_all();
    std::lock_guard<std::mutex> lock(mu_);
    snap_.paused = false;
    clock_.resume(steady_seconds());
}

void BatchJob::stop() {
    cancel_.store(true);
    {
        // Taking the lock puts this stop between a waiting worker's checks,
        // so no worker can sleep through it.
        std::lock_guard<std::mutex> lock(pause_mu_);
    }
    pause_cv_.notify_all();
}

void BatchJob::wait_while_paused() {
    std::unique_lock<std::mutex> lock(pause_mu_);
    pause_cv_.wait(lock, [this] { return !paused_ || cancel_.load(); });
    if (cancel_.load()) throw app::AnalysisCancelled{};
}

void BatchJob::note_started(const std::string& notespath) {
    auto it = by_path_.find(notespath);
    if (it == by_path_.end()) return;
    const app::ScanItem& item = items_[it->second];
    std::lock_guard<std::mutex> lock(mu_);
    snap_.current_title = item.title;
    snap_.current_artist = item.artist;
}

BatchJob::Snapshot BatchJob::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    Snapshot s = snap_;
    s.elapsed_s = clock_.elapsed_s(steady_seconds());
    if (!s.finished && !s.paused) s.eta_s = batch_eta_s(s.elapsed_s, s.completed, s.total);
    return s;
}

void BatchJob::run() {
    // Load the item list here rather than on the UI thread: an unbounded
    // SELECT over a big library takes long enough to freeze a frame.
    try {
        std::vector<store::ChartLibraryEntry> entries =
            given_ ? std::move(*given_)
                   : store_.list_chart_library(search_, 0, -1);  // LIMIT -1 = no limit
        items_.reserve(entries.size());
        for (const store::ChartLibraryEntry& e : entries)
            items_.push_back({e.md5, e.title, e.artist, e.charter, e.notespath, e.rootfolder});
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.preparing = false;
        snap_.failures.push_back(app::plain_error(e));
        snap_.failure_details.push_back(std::string("Could not load the library: ") + e.what());
        ++snap_.failed;
        clock_.finish(steady_seconds());
        snap_.finished = true;
        return;
    }
    for (size_t i = 0; i < items_.size(); ++i) by_path_.emplace(items_[i].notespath, i);

    {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.preparing = false;
    }
    if (cancel_.load()) {
        std::lock_guard<std::mutex> lock(mu_);
        clock_.finish(steady_seconds());
        snap_.finished = true;
        return;
    }

    bool total_known = false;

    app::BatchCallbacks callbacks;
    callbacks.on_progress = [this, &total_known](const app::BatchProgress& p) {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.total = p.total;
        snap_.completed = p.completed;
        if (!total_known) {
            snap_.skipped = static_cast<int>(items_.size()) - p.total;
            total_known = true;
        }
    };
    callbacks.on_error = [this](const std::string& title, const std::string& error) {
        std::lock_guard<std::mutex> lock(mu_);
        ++snap_.failed;
        snap_.failures.push_back(title + ": " + app::plain_error_text(error));
        snap_.failure_details.push_back(title + ": " + error);
    };
    callbacks.cancel = &cancel_;
    // The pause gate and the "now analyzing" line sit in front of the real
    // analyzer, so run_batch and its pool stay as they are.
    const app::ChartAnalyzer inner =
        analyze_ ? analyze_ : app::ChartAnalyzer(app::analyze_chart_file);
    callbacks.analyze = [this, inner](const std::string& path,
                                      const app::AnalysisSettings& settings,
                                      const std::function<void(float)>& on_progress) {
        wait_while_paused();
        note_started(path);
        return inner(path, settings, on_progress);
    };
    app::run_batch(items_, run_, store_, redo_, workers_, callbacks);

    std::lock_guard<std::mutex> lock(mu_);
    snap_.current_title.clear();
    snap_.current_artist.clear();
    snap_.paused = false;
    clock_.finish(steady_seconds());
    snap_.finished = true;
}
```

Two behaviour notes for the reviewer. The old `on_progress` set `current_title` to the chart that had just finished; now `note_started` sets it to the chart just started, which is what "Now:" in the Batch mockup shows. The old modal prints `current_title` and still works; it now names the chart in progress.

- [ ] **Step 9: Make the leaderboard jobs quiet on cancel and give DmReportJob its outcome.** In `src/ui/dm_jobs.h`, add the includes after `#include <vector>`:

```cpp
#include <filesystem>
#include <string>
#include <vector>

#include "app/dm_report.h"
#include "net/dmbot_client.h"
#include "store/record_store.h"
#include "ui/job_base.h"
#include "ui/report_outcome.h"
```

(replacing the current include block). In the `DmReportJob` class, replace:

```cpp
    // Headline join counts for the finished modal; valid once ok().
    int total() const { return total_; }
    int matched() const { return matched_; }
    int above() const { return above_; }
    int unmatched() const { return unmatched_; }

private:
    void run();
    store::RecordStore& store_;
    std::string discord_id_;
    std::string username_;
    std::string chartmode_;
    store::Lens lens_;
    bool open_when_done_;
    int total_ = 0, matched_ = 0, above_ = 0, unmatched_ = 0;
};
```

with:

```cpp
    // Headline join counts for the finished modal; valid once ok().
    int total() const { return total_; }
    int matched() const { return matched_; }
    int above() const { return above_; }
    int unmatched() const { return unmatched_; }
    // Every count the comparison produced, whatever app/dm_report.h names
    // them (T3 splits "not in your library" in two). Valid once ok().
    const app::dm_report::DmReportStats& stats() const { return stats_; }

    // Valid once finished() && ok(): where the page was written, whether the
    // browser opened it, and why not when it was asked to and didn't.
    const std::filesystem::path& saved_path() const { return outcome_.saved_path; }
    bool opened() const { return outcome_.opened; }
    const std::string& open_problem() const { return outcome_.open_problem; }

private:
    void run();
    store::RecordStore& store_;
    std::string discord_id_;
    std::string username_;
    std::string chartmode_;
    store::Lens lens_;
    bool open_when_done_;
    int total_ = 0, matched_ = 0, above_ = 0, unmatched_ = 0;
    app::dm_report::DmReportStats stats_;
    ReportOutcome outcome_;
};
```

In `src/ui/dm_jobs.cpp`, replace `DmFetchUsersJob::run`:

```cpp
void DmFetchUsersJob::run() {
    run_guarded([this] {
        users_ = net::fetch_users(net::kDefaultApiBase, &cancel_);
        return true;
    });
}
```

with:

```cpp
void DmFetchUsersJob::run() {
    run_guarded([this] {
        users_ = net::fetch_users(net::kDefaultApiBase, &cancel_);
        // A cancel can land while the server is still answering; the fetch
        // then returns normally. The picker asked to stop, so stop.
        return !is_cancelled();
    });
}
```

and replace the body of `DmReportJob::run`:

```cpp
    run_guarded([this] {
        std::vector<net::DmScore> scores =
            net::fetch_scores(discord_id_, net::kDefaultApiBase, &cancel_);
        // Join, tally, and framing all live behind generate_dm_report; the
        // job only fetches, forwards the counts, and writes the file.
        app::dm_report::GeneratedDmReport report =
            app::dm_report::generate_dm_report(store_, scores, chartmode_, lens_,
                                               username_);
        if (report.stats.total == 0)
            throw std::runtime_error("this user has no scores to compare");

        total_ = report.stats.total;
        matched_ = report.stats.matched;
        above_ = report.stats.above;
        unmatched_ = report.stats.unmatched;

        std::filesystem::path outpath = app::dm_report_html_path();
        app::write_report_file(outpath, report.html);
        if (open_when_done_ && !app::open_in_browser(outpath.wstring()))
            throw std::runtime_error("could not open " + outpath.u8string());
        return true;
    });
```

with:

```cpp
    run_guarded([this] {
        std::vector<net::DmScore> scores =
            net::fetch_scores(discord_id_, net::kDefaultApiBase, &cancel_);
        // The fetch only checks cancel between read chunks, so a cancel
        // pressed while the server was waking up arrives here. Stop before
        // anything is written or opened (audit B3).
        if (is_cancelled()) return false;
        // Join, tally, and framing all live behind generate_dm_report; the
        // job only fetches, forwards the counts, and writes the file.
        app::dm_report::GeneratedDmReport report =
            app::dm_report::generate_dm_report(store_, scores, chartmode_, lens_,
                                               username_);
        if (report.stats.total == 0)
            throw std::runtime_error("this user has no scores to compare");

        total_ = report.stats.total;
        matched_ = report.stats.matched;
        above_ = report.stats.above;
        unmatched_ = report.stats.unmatched;
        stats_ = report.stats;

        // A browser that won't open the page is not a failed report.
        outcome_ = publish_report(app::dm_report_html_path(), report.html, open_when_done_);
        return true;
    });
```

**Merge note for the main session (T3 and T4 meet here).** T3 renames the counts in `DmReportStats` to `matched`, `above_optimal`, `not_analyzed` and `not_in_library`. T4 cannot code against those names in its own worktree, because they don't exist there yet. When wave 1 is merged, the four lines `total_ = ...` to `unmatched_ = ...` in `DmReportJob::run` stop compiling. Replace them with `total_ = report.stats.total; matched_ = report.stats.matched; above_ = report.stats.above_optimal; unmatched_ = report.stats.not_analyzed + report.stats.not_in_library;` (if T3 drops `total`, use the sum of the four counts). `stats_ = report.stats;` needs no change. The old picker line keeps compiling through `above()` and `unmatched()` until T13 reads `stats()` instead.

- [ ] **Step 10: Run the job tests, then the whole suite and the GUI tests.** Build `hydra_tests`, then:

```powershell
.\build-cpp\Release\hydra_tests.exe -tc="user_messages*,jobs:*"
.\build-cpp\Release\hydra_tests.exe
```

Expected: `Status: SUCCESS!` both times. If "pause lets the chart in flight finish" hangs at the `completed == 1` wait, the gate is in the wrong place: `wait_while_paused()` must run before `note_started` and `inner`, never after. Then build the GUI and its tests with the Bash tool (`powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target Hydra > build.log 2>&1`, then the same for `-Target hydra_uitest`) and run `.\build-cpp\Release\hydra_uitest.exe --all`. Expected: the same 26 tests pass as in the Task 0 baseline. Nothing on screen changed, so no GUI test should need editing.

- [ ] **Step 11: Check the diff stays inside the task.** Run `git diff --stat <base>` (the Task 0 commit). Expected files only: `CMakeLists.txt`, `src/app/user_messages.h`, `src/app/user_messages.cpp`, `src/ui/job_base.h`, `src/ui/library_jobs.h`, `src/ui/library_jobs.cpp`, `src/ui/dm_jobs.h`, `src/ui/dm_jobs.cpp`, `src/ui/report_outcome.h`, `tests/test_user_messages.cpp`, `tests/test_library_jobs.cpp`.

- [ ] **Step 12: Commit.**

```powershell
git add CMakeLists.txt src/app/user_messages.h src/app/user_messages.cpp src/ui/job_base.h src/ui/library_jobs.h src/ui/library_jobs.cpp src/ui/dm_jobs.h src/ui/dm_jobs.cpp src/ui/report_outcome.h tests/test_user_messages.cpp tests/test_library_jobs.cpp
git commit -m "Batch job pause/stop with elapsed and time left; plain error messages; a refused browser is not a failed report

Task: Task 4 - batch job and job messages
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Hydra reopens where you left it and rescales when it changes monitor

Today the window always opens at (100, 100), 1280 by 720, however the user left it. Moving it to a monitor with a different Windows scale keeps the old sizes: `main.cpp` reads the primary monitor's scale once at start-up and never again, and it doesn't handle `WM_DPICHANGED` (the message Windows sends when a window lands on a monitor with another scale).

After this task, Hydra remembers the window's position, size and whether it was maximized, in `hydra_ui.ini` next to the table column widths it already keeps there. On the next start it reopens in the same place, unless that place is on no monitor any more (a monitor was unplugged or rearranged), in which case it uses today's default. When the window moves to a monitor with another scale, it takes the size Windows suggests and rescales the whole UI: paddings, fonts and every `px()` size. Nothing else changes, and no label is added.

**Wave:** 1. **Depends on:** Task 0. **Expected overlaps:** `CMakeLists.txt` with T1, T2, T3 and T4 (T5 adds one line, `tests/test_app_shell.cpp`, to `hydra_tests`; keep every task's lines). T9 in `app_shell.cpp` only if T9 changes `run_frame` for the panel; T5 doesn't touch `run_frame`, so the two edits sit in different functions. T9 also rewrites colours in `theme.cpp`; T5 captures the theme after `apply_theme()` runs, so it picks up T9's colours with no merge work.

**Goal:** The window keeps its place between runs and its sizes follow the monitor it is on.

**Files:**
- Modify: `src/ui/app_shell.h` (placement types and functions, the scale functions)
- Modify: `src/ui/app_shell.cpp` (the `hydra_ui.ini` handler, reading the ini early, `set_ui_scale`)
- Modify: `src/ui/main.cpp` (open at the saved place, record moves, handle `WM_DPICHANGED`)
- Modify: `src/ui/fonts.h` (the `g_ui_scale` comment: it is no longer set once)
- Modify: `CMakeLists.txt` (`tests/test_app_shell.cpp` after `tests/test_app_state.cpp` in `hydra_tests`)
- Test: `tests/test_app_shell.cpp` (new)

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="app_shell*"` passes, covering: a saved rectangle whose title bar is on a monitor is used; one that is off every monitor, shows under 64 px, or has its title bar above the screen is ignored; a window on a second monitor at negative coordinates is used; the `[Hydra][Window]` text round-trips, including negative positions; `ui_scale_for_dpi(144)` is 1.5; scaling from the base style twice gives the same sizes as once.
- [ ] A real ImGui context started on a temporary `hydra_ui.ini` holding `[Hydra][Window]` / `Pos=-1700,40` / `Size=1400,900` / `Maximized=1` reports that placement, and after a new placement is remembered, closing the context writes `Pos=200,150`, `Size=1280,720`, `Maximized=0` back to the file.
- [ ] `set_ui_scale(2.0f)` doubles `FramePadding`, sets `FontScaleDpi` and `g_ui_scale` to 2, makes `px(10)` read 20, and `set_ui_scale(1.0f)` puts all of them back exactly.
- [ ] The whole unit suite reports `Status: SUCCESS!`, and `hydra_uitest.exe --all` passes the same 26 GUI tests as the Task 0 baseline.
- [ ] Main-session check in Task 15 (a GUI test can't move a real window): move and resize Hydra, close it, reopen it, and it comes back in the same place; maximize, close, reopen, and it comes back maximized; with two monitors at different scales, drag it across and the text and paddings change size together.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="app_shell*"` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests.** Create `tests/test_app_shell.cpp`:

```cpp
// Unit tests for the window-placement and DPI half of ui/app_shell: which
// saved rectangles are safe to reopen at, the hydra_ui.ini text, and the UI
// scale. A test can't move a real window between monitors, so the Win32
// side in main.cpp stays thin and everything it decides is tested here.

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"

#include "ui/app_shell.h"
#include "ui/fonts.h"

using hydra::ui::ScreenRect;
using hydra::ui::WindowPlacement;
using hydra::ui::placement_on_screen;

namespace {

const std::vector<ScreenRect> kOneMonitor = {{0, 0, 1920, 1040}};
const std::vector<ScreenRect> kTwoMonitors = {{0, 0, 1920, 1040}, {-1920, 0, 0, 1080}};

hydra::ui::ImGuiSetupOptions test_options(const std::string& ini_file) {
    hydra::ui::ImGuiSetupOptions opts;
    opts.dpi_scale = 1.0f;
    opts.ini_file = ini_file;
    opts.resource_dir = std::string(HYDRA_TESTDATA_DIR) + "/../resource";
    return opts;
}

}  // namespace

TEST_CASE("app_shell: a saved window is reopened only where its title bar can be reached") {
    CHECK(placement_on_screen({100, 100, 1380, 820}, kOneMonitor));
    CHECK_FALSE(placement_on_screen({5000, 100, 6280, 820}, kOneMonitor));  // monitor gone
    CHECK_FALSE(placement_on_screen({-1250, 100, 30, 820}, kOneMonitor));   // 30 px showing
    CHECK(placement_on_screen({-1180, 100, 100, 820}, kOneMonitor));        // 100 px showing
    CHECK_FALSE(placement_on_screen({100, -700, 1380, 20}, kOneMonitor));   // title bar above
    CHECK_FALSE(placement_on_screen({100, 100, 100, 100}, kOneMonitor));    // no size
    CHECK_FALSE(placement_on_screen({-1800, 40, -400, 940}, kOneMonitor));
    CHECK(placement_on_screen({-1800, 40, -400, 940}, kTwoMonitors));      // left monitor
    CHECK_FALSE(placement_on_screen({100, 100, 1380, 820}, {}));           // no monitors known
}

TEST_CASE("app_shell: the [Hydra][Window] text round-trips") {
    WindowPlacement p;
    p.valid = true;
    p.normal = {-1700, 40, -300, 940};
    p.maximized = true;
    const std::string text = hydra::ui::format_window_placement(p);
    CHECK(text == "Pos=-1700,40\nSize=1400,900\nMaximized=1\n");

    WindowPlacement back;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);)
        hydra::ui::parse_window_placement_line(line, back);
    CHECK(back == p);

    // Size before Pos reads the same.
    WindowPlacement reordered;
    hydra::ui::parse_window_placement_line("Size=1400,900", reordered);
    hydra::ui::parse_window_placement_line("Pos=-1700,40", reordered);
    CHECK(reordered.normal == p.normal);
    CHECK(reordered.valid);

    // Lines Hydra doesn't know, or a zero size, leave nothing usable.
    WindowPlacement junk;
    hydra::ui::parse_window_placement_line("Pos=abc,1", junk);
    hydra::ui::parse_window_placement_line("Size=0,0", junk);
    hydra::ui::parse_window_placement_line("Colour=blue", junk);
    CHECK_FALSE(junk.valid);
}

TEST_CASE("app_shell: the UI scale for a monitor DPI") {
    CHECK(hydra::ui::ui_scale_for_dpi(96) == doctest::Approx(1.0f));
    CHECK(hydra::ui::ui_scale_for_dpi(120) == doctest::Approx(1.25f));
    CHECK(hydra::ui::ui_scale_for_dpi(144) == doctest::Approx(1.5f));
    CHECK(hydra::ui::ui_scale_for_dpi(192) == doctest::Approx(2.0f));
    CHECK(hydra::ui::ui_scale_for_dpi(0) == doctest::Approx(1.0f));  // no reading: unscaled
}

TEST_CASE("app_shell: scaling always starts from the unscaled style") {
    const ImGuiStyle base;
    const ImGuiStyle once = hydra::ui::scaled_style(base, 1.5f);
    const ImGuiStyle again = hydra::ui::scaled_style(base, 1.5f);
    CHECK(once.FramePadding.x == again.FramePadding.x);
    CHECK(once.ItemSpacing.y == again.ItemSpacing.y);
    CHECK(once.FontScaleDpi == doctest::Approx(1.5f));
    const ImGuiStyle doubled = hydra::ui::scaled_style(base, 2.0f);
    CHECK(doubled.FramePadding.x == base.FramePadding.x * 2.0f);
    const ImGuiStyle unscaled = hydra::ui::scaled_style(base, 1.0f);
    CHECK(unscaled.FramePadding.x == base.FramePadding.x);
    CHECK(unscaled.ScrollbarSize == base.ScrollbarSize);
}

TEST_CASE("app_shell: hydra_ui.ini remembers the window placement") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "hydra_app_shell_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path ini = dir / "hydra_ui.ini";
    {
        std::ofstream f(ini);
        f << "[Hydra][Window]\nPos=-1700,40\nSize=1400,900\nMaximized=1\n\n";
    }

    hydra::ui::setup_imgui(test_options(ini.string()));
    const WindowPlacement read = hydra::ui::window_placement();
    CHECK(read.valid);
    CHECK(read.normal == ScreenRect{-1700, 40, -300, 940});
    CHECK(read.maximized);

    WindowPlacement moved;
    moved.valid = true;
    moved.normal = {200, 150, 1480, 870};
    moved.maximized = false;
    hydra::ui::remember_window_placement(moved);
    hydra::ui::shutdown_imgui();  // DestroyContext writes the ini

    std::ifstream f(ini);
    std::stringstream text;
    text << f.rdbuf();
    CHECK(text.str().find("[Hydra][Window]\nPos=200,150\nSize=1280,720\nMaximized=0\n") !=
          std::string::npos);
    f.close();
    std::filesystem::remove_all(dir);
}

TEST_CASE("app_shell: set_ui_scale rescales sizes, fonts and px() together") {
    hydra::ui::setup_imgui(test_options("-"));
    const float before = ImGui::GetStyle().FramePadding.x;

    hydra::ui::set_ui_scale(2.0f);
    CHECK(ImGui::GetStyle().FramePadding.x ==
          static_cast<float>(static_cast<int>(before * 2.0f)));
    CHECK(ImGui::GetStyle().FontScaleDpi == doctest::Approx(2.0f));
    CHECK(hydra::ui::g_ui_scale == doctest::Approx(2.0f));
    CHECK(hydra::ui::px(10.0f) == doctest::Approx(20.0f));

    hydra::ui::set_ui_scale(1.0f);
    CHECK(ImGui::GetStyle().FramePadding.x == before);
    CHECK(ImGui::GetStyle().FontScaleDpi == doctest::Approx(1.0f));
    CHECK(hydra::ui::px(10.0f) == doctest::Approx(10.0f));
    hydra::ui::shutdown_imgui();
}
```

In `CMakeLists.txt`, add the file to `hydra_tests` after `tests/test_app_state.cpp`:

```cmake
    tests/test_app_state.cpp
    tests/test_app_shell.cpp
```

Build with the Bash tool from the worktree root: `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1`. Expected: the build fails on `ScreenRect`, `WindowPlacement` and the other new names.

- [ ] **Step 2: Declare the placement and scale functions.** In `src/ui/app_shell.h`, replace the includes:

```cpp
#include <string>

namespace hydra::ui {
```

with:

```cpp
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"

namespace hydra::ui {
```

and add this block after `void shutdown_imgui();  // DestroyContext`:

```cpp
// ---- The main window's placement, kept in hydra_ui.ini ----------------

// A rectangle in virtual-screen pixels, the units of GetWindowRect and of a
// monitor's work area. right/bottom are one past the last pixel.
struct ScreenRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool operator==(const ScreenRect&) const = default;
};

// Where the main window was when Hydra last closed.
struct WindowPlacement {
    bool valid = false;  // false: nothing saved yet, so use the default
    ScreenRect normal;   // the un-maximized rectangle
    bool maximized = false;
    bool operator==(const WindowPlacement&) const = default;
};

// How much of a saved window's title-bar band must sit on one monitor for
// the user to grab it: this many pixels wide, half as many tall.
inline constexpr int kMinVisiblePx = 64;

// True when the top kMinVisiblePx rows of `r` (where the title bar is)
// overlap one of `work_areas` by at least kMinVisiblePx wide and
// kMinVisiblePx / 2 tall. False for a rectangle smaller than that.
bool placement_on_screen(const ScreenRect& r, const std::vector<ScreenRect>& work_areas);

// The body of the [Hydra][Window] section: "Pos=x,y", "Size=w,h" and
// "Maximized=0|1", one per line. Parsing takes one line at a time, in any
// order, and ignores lines it doesn't know.
std::string format_window_placement(const WindowPlacement& p);
void parse_window_placement_line(std::string_view line, WindowPlacement& p);

// The placement setup_imgui read from hydra_ui.ini, updated by every
// remember_window_placement since. valid is false when there was none.
WindowPlacement window_placement();
// Records where the window is now. It reaches hydra_ui.ini with ImGui's own
// settings: a few seconds later, and again when ImGui shuts down.
void remember_window_placement(const WindowPlacement& p);

// ---- UI scale ----------------------------------------------------------

// The UI scale for a monitor's DPI: 96 DPI is 1.0. A DPI of 0 reads as 1.0.
float ui_scale_for_dpi(unsigned dpi);

// The theme at `scale`: base with ScaleAllSizes(scale) and FontScaleDpi =
// scale. Always from the unscaled base, because ImGui rounds every size on
// each ScaleAllSizes call, so scaling an already-scaled style drifts.
ImGuiStyle scaled_style(const ImGuiStyle& base, float scale);

// Rescales the running UI to `scale`: ImGui's paddings and sizes, the font
// size, and px(). setup_imgui calls it once; main.cpp calls it again when the
// window lands on a monitor with another scale. Call between frames only.
void set_ui_scale(float scale);
```

Also update the file's opening comment. Replace:

```cpp
// The window-independent half of the GUI shell: ImGui context setup and the
// per-frame draw, shared by Hydra.exe (src/ui/main.cpp) and the headless GUI
// test runner (tests/ui, docs/agents/ui-testing.md). main.cpp keeps only the
// Win32 window, swapchain, and message pump; the runner swaps those for an
// offscreen render target and the Test Engine's injected input.
```

with:

```cpp
// The window-independent half of the GUI shell: ImGui context setup, the UI
// scale, the remembered window placement, and the per-frame draw, shared by
// Hydra.exe (src/ui/main.cpp) and the headless GUI test runner (tests/ui,
// docs/agents/ui-testing.md). main.cpp keeps only the Win32 window,
// swapchain, and message pump; the runner swaps those for an offscreen render
// target and the Test Engine's injected input.
```

- [ ] **Step 3: Implement them in `app_shell.cpp`.** Replace the includes block:

```cpp
#include "imgui.h"
#include "imgui_internal.h"  // g.LogBuffer for FrameText
```

with:

```cpp
#include <algorithm>
#include <charconv>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"  // g.LogBuffer for FrameText; ImGuiSettingsHandler
```

Right after `namespace hydra::ui {`, add the placement code and the ini handler:

```cpp
namespace {

// What hydra_ui.ini said, then what main.cpp reported since.
WindowPlacement g_placement;
// The theme at scale 1, captured by setup_imgui after apply_theme().
ImGuiStyle g_base_style;

// Reads "<a>,<b>" into two ints; false unless the text is exactly that.
bool read_int_pair(std::string_view text, int& a, int& b) {
    const size_t comma = text.find(',');
    if (comma == std::string_view::npos) return false;
    const char* first = text.data();
    const char* mid = first + comma;
    const char* last = first + text.size();
    const auto r1 = std::from_chars(first, mid, a);
    if (r1.ec != std::errc() || r1.ptr != mid) return false;
    const auto r2 = std::from_chars(mid + 1, last, b);
    return r2.ec == std::errc() && r2.ptr == last;
}

// The [Hydra][Window] section's handler. ImGui calls ReadOpen for each
// "[Hydra][<name>]" header and ReadLine for each line under it, and WriteAll
// whenever it saves the file.
void* placement_read_open(ImGuiContext*, ImGuiSettingsHandler*, const char* name) {
    return std::strcmp(name, "Window") == 0 ? &g_placement : nullptr;
}

void placement_read_line(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
    parse_window_placement_line(line, *static_cast<WindowPlacement*>(entry));
}

void placement_write_all(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out) {
    if (!g_placement.valid) return;
    out->appendf("[%s][Window]\n", handler->TypeName);
    out->append(format_window_placement(g_placement).c_str());
    out->append("\n");
}

}  // namespace

bool placement_on_screen(const ScreenRect& r, const std::vector<ScreenRect>& work_areas) {
    if (r.width() < kMinVisiblePx || r.height() < kMinVisiblePx) return false;
    const ScreenRect title_band{r.left, r.top, r.right, r.top + kMinVisiblePx};
    for (const ScreenRect& area : work_areas) {
        const int w = std::min(title_band.right, area.right) - std::max(title_band.left, area.left);
        const int h = std::min(title_band.bottom, area.bottom) - std::max(title_band.top, area.top);
        if (w >= kMinVisiblePx && h >= kMinVisiblePx / 2) return true;
    }
    return false;
}

std::string format_window_placement(const WindowPlacement& p) {
    return "Pos=" + std::to_string(p.normal.left) + "," + std::to_string(p.normal.top) + "\n" +
           "Size=" + std::to_string(p.normal.width()) + "," + std::to_string(p.normal.height()) +
           "\n" + "Maximized=" + (p.maximized ? "1" : "0") + "\n";
}

void parse_window_placement_line(std::string_view line, WindowPlacement& p) {
    int a = 0, b = 0;
    if (line.substr(0, 4) == "Pos=" && read_int_pair(line.substr(4), a, b)) {
        const int w = p.normal.width(), h = p.normal.height();
        p.normal = {a, b, a + w, b + h};
    } else if (line.substr(0, 5) == "Size=" && read_int_pair(line.substr(5), a, b)) {
        p.normal.right = p.normal.left + a;
        p.normal.bottom = p.normal.top + b;
    } else if (line == "Maximized=1") {
        p.maximized = true;
    } else if (line == "Maximized=0") {
        p.maximized = false;
    }
    p.valid = p.normal.width() > 0 && p.normal.height() > 0;
}

WindowPlacement window_placement() { return g_placement; }

void remember_window_placement(const WindowPlacement& p) {
    if (p == g_placement) return;
    g_placement = p;
    // WndProc can run before the context exists (CreateWindow sends its
    // first WM_SIZE early); the placement is kept either way.
    if (ImGui::GetCurrentContext()) ImGui::MarkIniSettingsDirty();
}

float ui_scale_for_dpi(unsigned dpi) {
    return dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;  // 96 = USER_DEFAULT_SCREEN_DPI
}

ImGuiStyle scaled_style(const ImGuiStyle& base, float scale) {
    ImGuiStyle style = base;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    return style;
}

void set_ui_scale(float scale) {
    if (scale <= 0.0f) return;
    ImGuiStyle& style = ImGui::GetStyle();
    // FontSizeBase is filled in by ImGui on the first frame; keep it.
    const float font_size_base = style.FontSizeBase;
    style = scaled_style(g_base_style, scale);
    style.FontSizeBase = font_size_base;
    g_ui_scale = scale;  // for the views' explicit pixel sizes
}
```

In `setup_imgui`, register the handler and read the ini right after the ini path is set. Replace:

```cpp
        io.IniFilename = ini_file.c_str();
    }

    ImGui::StyleColorsDark();
    apply_theme();

    const float main_scale = options.dpi_scale;
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    io.ConfigDpiScaleFonts = true;
    g_ui_scale = main_scale;  // for the views' explicit pixel sizes
```

with:

```cpp
        io.IniFilename = ini_file.c_str();
    }

    // The main window's placement lives in the same file, as a
    // [Hydra][Window] section. Read the file now instead of on the first
    // frame (ImGui skips its own first-frame read once this has run), so
    // main.cpp can put the window back before it is shown.
    g_placement = WindowPlacement{};
    ImGuiSettingsHandler placement_handler;
    placement_handler.TypeName = "Hydra";
    placement_handler.TypeHash = ImHashStr("Hydra");
    placement_handler.ReadOpenFn = placement_read_open;
    placement_handler.ReadLineFn = placement_read_line;
    placement_handler.WriteAllFn = placement_write_all;
    ImGui::AddSettingsHandler(&placement_handler);  // ImGui keeps a copy
    if (io.IniFilename) ImGui::LoadIniSettingsFromDisk(io.IniFilename);

    ImGui::StyleColorsDark();
    apply_theme();

    // Every scale change starts again from this unscaled theme.
    g_base_style = ImGui::GetStyle();
    // Hydra sets the font scale itself, in set_ui_scale, together with the
    // sizes. With ConfigDpiScaleFonts on, ImGui would also overwrite it every
    // frame from the main viewport's DPI, a second writer that Hydra does not
    // keep in step with WM_DPICHANGED.
    io.ConfigDpiScaleFonts = false;
    set_ui_scale(options.dpi_scale);
```

The fonts need no rebuild. This ImGui (1.93 WIP) has the dynamic font atlas: a font is baked at whatever size `FontSizeBase * FontScaleMain * FontScaleDpi` asks for, the first time that size is drawn, and the DX11 backend supports it (`ImGuiBackendFlags_RendererHasTextures` in `imgui_impl_dx11.cpp`). Setting `FontScaleDpi` is the whole font change.

- [ ] **Step 4: Update the `g_ui_scale` comment in `fonts.h`.** Replace:

```cpp
// The monitor's DPI scale, set once by main.cpp. style.ScaleAllSizes() covers
// ImGui's own paddings and ConfigDpiScaleFonts covers text, but neither
// touches explicit pixel values (SetNextItemWidth, ImVec2 sizes, SameLine
// offsets) — wrap those in px() so widths/heights scale with the display.
```

with:

```cpp
// The UI scale: the DPI scale of the monitor the window is on. set_ui_scale
// (ui/app_shell.h) sets it at start-up and again when the window moves to a
// monitor with another scale. style.ScaleAllSizes() covers ImGui's own
// paddings and style.FontScaleDpi covers text, but neither touches explicit
// pixel values (SetNextItemWidth, ImVec2 sizes, SameLine offsets) — wrap
// those in px() so widths/heights scale with the display. Call px() while
// drawing; a value kept from an earlier frame misses a scale change.
```

- [ ] **Step 5: Run the unit tests.** Build `hydra_tests` as in Step 1, then `.\build-cpp\Release\hydra_tests.exe -tc="app_shell*"`. Expected: `Status: SUCCESS!`. Then the whole suite, `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`.

- [ ] **Step 6: Open the window at the saved place.** In `src/ui/main.cpp`, the ImGui context now has to exist before the window, so `setup_imgui` moves up. Add a pending-scale variable after the other Direct3D statics. Replace:

```cpp
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
```

with:

```cpp
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
// Set by WM_DPICHANGED, applied by the frame loop before the next frame.
static float                    g_PendingUiScale = 0.0f;
```

After the forward declaration `LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);`, add the three helpers:

```cpp
// Adds one monitor's work area (the screen minus the taskbar) to the list.
static BOOL CALLBACK add_work_area(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetMonitorInfoW(monitor, &info))
        reinterpret_cast<std::vector<hydra::ui::ScreenRect>*>(data)->push_back(
            { info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom });
    return TRUE;
}

// Every monitor's work area, in virtual-screen pixels.
static std::vector<hydra::ui::ScreenRect> monitor_work_areas()
{
    std::vector<hydra::ui::ScreenRect> areas;
    ::EnumDisplayMonitors(nullptr, nullptr, add_work_area, reinterpret_cast<LPARAM>(&areas));
    return areas;
}

// Keeps the remembered placement current as the user moves, resizes,
// maximizes and restores the window. The un-maximized rectangle is read only
// while the window is neither maximized nor minimized, so a Hydra closed
// while maximized still restores to the size it had before.
static void note_window_placement(HWND hWnd)
{
    if (::IsIconic(hWnd))
        return;
    hydra::ui::WindowPlacement p = hydra::ui::window_placement();
    p.maximized = ::IsZoomed(hWnd) != FALSE;
    RECT r;
    if (!p.maximized && ::GetWindowRect(hWnd, &r))
    {
        p.normal = { r.left, r.top, r.right, r.bottom };
        p.valid = true;
    }
    hydra::ui::remember_window_placement(p);
}
```

In `main()`, replace the window creation:

```cpp
    ::RegisterClassExW(&wc);
    // hymisc.HYDRA_VERSION as of this port; matches the "{EDITION_NAME} v..."
    // title dpg.create_viewport builds. Bump alongside HYDRA_VERSION.
    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName, hydra::kWindowTitleW, WS_OVERLAPPEDWINDOW, 100, 100,
        (int)(1280 * main_scale), (int)(720 * main_scale),
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
```

with:

```cpp
    ::RegisterClassExW(&wc);

    // Set up Dear ImGui before the window exists: setup_imgui reads
    // hydra_ui.ini, which remembers where the window was when Hydra last
    // closed. (Context, theme, DPI and fonts live in app_shell.cpp, shared
    // with the headless GUI test runner.)
    hydra::ui::ImGuiSetupOptions imgui_options;
    imgui_options.dpi_scale = main_scale;
    hydra::ui::setup_imgui(imgui_options);

    // Reopen where the user left it, unless that spot is on no monitor now
    // (a monitor was unplugged or rearranged). Otherwise the old default.
    const hydra::ui::WindowPlacement saved = hydra::ui::window_placement();
    const bool use_saved =
        saved.valid && hydra::ui::placement_on_screen(saved.normal, monitor_work_areas());
    const hydra::ui::ScreenRect rect =
        use_saved ? saved.normal
                  : hydra::ui::ScreenRect{ 100, 100, 100 + (int)(1280 * main_scale),
                                           100 + (int)(720 * main_scale) };

    // hymisc.HYDRA_VERSION as of this port; matches the "{EDITION_NAME} v..."
    // title dpg.create_viewport builds. Bump alongside HYDRA_VERSION.
    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName, hydra::kWindowTitleW, WS_OVERLAPPEDWINDOW, rect.left, rect.top,
        rect.width(), rect.height(), nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        hydra::ui::shutdown_imgui();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
```

Then replace the show-and-setup block:

```cpp
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Set up the Dear ImGui context (context, theme, DPI, fonts live in
    // app_shell.cpp, shared with the headless GUI test runner).
    hydra::ui::ImGuiSetupOptions imgui_options;
    imgui_options.dpi_scale = main_scale;
    hydra::ui::setup_imgui(imgui_options);

    ImGui_ImplWin32_Init(hwnd);
```

with:

```cpp
    ::ShowWindow(hwnd, use_saved && saved.maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // setup_imgui assumed the primary monitor's scale. A window reopened on
    // another monitor may need a different one.
    const float window_scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    if (window_scale > 0.0f && window_scale != main_scale)
        hydra::ui::set_ui_scale(window_scale);

    ImGui_ImplWin32_Init(hwnd);
```

- [ ] **Step 7: Apply a new scale between frames.** In the frame loop, add the pending-scale block after the resize block. Replace:

```cpp
            CreateRenderTarget();
        }

        // Begin the frame.
```

with:

```cpp
            CreateRenderTarget();
        }

        // The window moved to a monitor with another scale (WM_DPICHANGED).
        // Restyle here, between frames: ImGui's style must not change inside
        // one.
        if (g_PendingUiScale > 0.0f)
        {
            hydra::ui::set_ui_scale(g_PendingUiScale);
            g_PendingUiScale = 0.0f;
        }

        // Begin the frame.
```

- [ ] **Step 8: Record moves and handle `WM_DPICHANGED` in `WndProc`.** The ImGui Win32 backend only moves the window on `WM_DPICHANGED` when multi-viewport scaling is on (`io.ConfigDpiScaleViewports`), which Hydra leaves off, so the message reaches Hydra's own switch. Replace:

```cpp
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
```

with:

```cpp
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        note_window_placement(hWnd);
        return 0;
    case WM_MOVE:
        note_window_placement(hWnd);
        return 0;
    case WM_DPICHANGED:
    {
        // Windows moved us to a monitor with another scale. Take the
        // rectangle it suggests (the same physical size there), and rescale
        // the UI before the next frame. HIWORD and LOWORD of wParam carry the
        // same DPI.
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        ::SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                       suggested->right - suggested->left, suggested->bottom - suggested->top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        g_PendingUiScale = hydra::ui::ui_scale_for_dpi(HIWORD(wParam));
        return 0;
    }
```

Nothing is needed at exit. `hydra::ui::shutdown_imgui()` destroys the ImGui context, and ImGui writes `hydra_ui.ini` then, including the `[Hydra][Window]` section.

- [ ] **Step 9: Build everything and run the GUI tests.** With the Bash tool: `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target Hydra > build.log 2>&1`, then the same with `-Target hydra_uitest`. Run `.\build-cpp\Release\hydra_uitest.exe --all`. Expected: the same 26 tests pass as in the Task 0 baseline. The runner passes `ini_file = "-"`, so it reads and writes no ini, and it passes a scale of 1, so its sizes are unchanged.

- [ ] **Step 10: Leave the by-eye check to Task 15.** Executors never start `Hydra.exe` during a task, because a GUI app launched by an agent can wait forever for input. Task 15 checks that the window reopens where it was left and that moving it to a monitor with a different scale keeps the text sharp and the layout in proportion.

- [ ] **Step 11: Check the diff stays inside the task.** Run `git diff --stat <base>` (the Task 0 commit). Expected files only: `CMakeLists.txt`, `src/ui/app_shell.h`, `src/ui/app_shell.cpp`, `src/ui/main.cpp`, `src/ui/fonts.h`, `tests/test_app_shell.cpp`.

- [ ] **Step 12: Commit.**

```powershell
git add CMakeLists.txt src/ui/app_shell.h src/ui/app_shell.cpp src/ui/main.cpp src/ui/fonts.h tests/test_app_shell.cpp
git commit -m "Remember the window's place in hydra_ui.ini and rescale the UI when it changes monitor

Task: Task 5 - window placement and DPI
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Remove Auto

Today the SP cap has an "Auto" checkbox beside its number box. Auto climbs a ladder of caps (16, 32, 64 up to 512) until the score stops rising, under a two-minute time budget. It is stored differently from every other run: it carries its own rules fingerprint (a hash of the rules a result was made under), lookups under Auto ask the store for "the newest row above 4 bars" instead of one exact cap, and two `hydra_rules.ini` keys (`auto_cap_ladder`, `auto_budget_s`) exist only for it. You asked to drop it completely and keep 4 as the default. After this task the SP cap is a plain number everywhere: in the settings, in the search, in the store's lookups, and in the tools. The first time the new build opens a database, it deletes the results Auto saved and the stored paths only they used, once. An INI that says `sp_cap=auto` opens at 4. A rules file that still sets the two Auto keys keeps loading; the keys are read and do nothing. The only thing you see change in the app is that the Auto checkbox and the sentence about Auto in the SP cap hint are gone. No score changes.

**How an Auto result is stored today, and how this task finds it.** An Auto row looks like any other row in the `results` table: its `sp_cap` column is the cap the ladder settled on (16 or more), and there is no Auto flag. The one thing that marks it is the rules fingerprint written into bytes 5 to 12 of its `structure` blob. A fixed-cap run writes `Rules::fingerprint()`. An Auto run writes `Rules::auto_fingerprint()`, which hashes the same text plus one more line, `auto_cap_ladder=16,32,64,128,256,512,`. So this task keeps one function that rebuilds that second hash, `Rules::retired_auto_fingerprint()`, and the store deletes exactly the rows whose blob carries it. I checked the text is reproducible. A Python copy of `fixed_cap_text` and FNV-1a gives `0x70d2e96669604cf2` for the default rules, and that is the fingerprint on all 18,775 4-bar rows in the installed `C:\Program Files\Hydra\hydra.db`. The same code gives `0x5b610b430a43a4be` for the Auto fingerprint. That database holds no Auto rows, so for you the delete finds nothing. A fixed-cap what-if at 16 or 32 bars carries the plain fingerprint and is kept.

**What the record format keeps.** `HydraRecord::sp_cap` stays `std::optional<int>` and `HydraRecord::sp_cap_converged` stays, because the path structure blob stores both (`path_codec.cpp` `flatten_record` / `rebuild_record`). Removing either would change the record format and make every stored result Stale. After this task every run writes `sp_cap_converged = true`, which is what fixed-cap runs always wrote. The "search ran out of time" footer line in `path_view.cpp` can no longer happen, so that branch goes.

**Wave:** 2. **Depends on:** T1 (the SP cap row now lives in `src/ui/details_panel.cpp` and the Compare button in `src/ui/library_toolbar.cpp`; find code by function name), and all of wave 1 merged (T3 changed `report.cpp`, `test_report.cpp` and `test_dm_report.cpp`). **Expected overlaps:** T7 in `src/store/record_store.{h,cpp}` and `tests/test_store.cpp` (T6 edits CapQuery, the fingerprint SQL and adds `delete_auto_results()` to the constructor; T7 edits the summary columns and adds its backfill after it; keep both, T6's delete first). T8 in `src/app/path_view.cpp` (keep T8's version and re-apply T6's footer change). Found while drafting, not in 01-ownership: `src/app/analysis.cpp` and `tests/test_analysis.cpp` (each calls `CapQuery::from_setting` once; no other task owns them), `src/app/report.cpp` (T3's file from wave 1: its `cap_label` reads the optional cap), `tests/test_report.cpp` and `tests/test_dm_report.cpp` (T3's in wave 1; T6 edits only their Auto lines), and `tests/ui/uitest_details.cpp` (T9 rewrites it in wave 3; T6 only removes the Auto clicks in `test_analyze`). `src/ui/library_toolbar.cpp` needs no edit: its check `app.settings.sp_cap != kCloneHeroSpCap` compiles unchanged once `sp_cap` is an `int`.

**Goal:** The SP cap is a plain `int` (default 4, at least 1) from the INI to the store, Auto's code, keys and stored results are gone, and every score stays the same.

**Files:**
- Modify: `src/core/rules.h`, `src/core/rules.cpp` (drop the ladder, the budget and `auto_fingerprint()`; add `retired_auto_fingerprint()`; `RulesStamp::autocap` becomes `retired_auto`)
- Modify: `src/app/rules_file.cpp` (the two Auto keys are read and ignored)
- Modify: `src/app/config.h`, `src/app/config.cpp` (`Settings::sp_cap` is an `int`; `auto` reads as 4)
- Modify: `src/search/pather.h`, `src/search/pather.cpp` (`SearchSettings::sp_cap` is an `int`; `analyze_auto_cap` deleted)
- Modify: `src/store/record_store.h`, `src/store/record_store.cpp` (CapQuery, one accepted fingerprint, `collect_orphan_paths`, `delete_auto_results`)
- Modify: `src/store/path_codec.cpp` (one comment), `src/core/model.h` (two comments)
- Modify: `src/app/path_view.cpp` (the SP meter footer line only)
- Modify: `src/app/analysis.cpp` (the batch skip list's cap), `src/app/report.cpp` (the subtitle's cap label)
- Modify: `src/cli/batch.cpp` (the "SP cap" line it prints)
- Modify: `tools/replay.cpp`, `tools/bench.cpp`
- Modify: `src/ui/app_state.h` (`DetailsViewState::last_cap` goes), `src/ui/app_state.cpp` (one comment), `src/ui/details_panel.cpp` (the SP cap row in `render_controls`)
- Test: `tests/test_rules.cpp`, `tests/test_config.cpp`, `tests/test_store.cpp`, `tests/test_report.cpp`, `tests/test_dm_report.cpp`, `tests/test_analysis.cpp`, `tests/corpus_util.h`, `tests/ui/uitest_details.cpp` (`test_analyze`)
- No change needed: `tests/test_path_codec.cpp` (its `sp_cap_converged` round-trip checks still hold), `src/ui/library_toolbar.cpp`, `CMakeLists.txt`

**Acceptance Criteria:**
- [ ] `rg -n "is_auto\(|automatic\(\)|from_setting|analyze_auto_cap|auto_fingerprint|autocap|spcapauto|last_cap|CapBudgetExceeded" src tests tools` prints nothing.
- [ ] `rg -n "auto_cap_ladder|auto_budget_s" src tests tools` prints only the ignore line in `src/app/rules_file.cpp` and the test `rules: the retired Auto keys are read and ignored` in `tests/test_rules.cpp`.
- [ ] `hydra_tests -tc="the first open deletes the results Auto saved*"` passes: a database with a 4-bar row, an 8-bar what-if, and two Auto rows keeps the first two byte for byte and loses the Auto rows and the paths only they used; a second open deletes nothing.
- [ ] `hydra_tests -tc="a start with a bad rules file leaves the Auto results*"` passes.
- [ ] `hydra_tests -tc="rules: the retired Auto fingerprint is what Hydra 1.8.4 stamped"` passes (pins `0x70d2e96669604cf2` and `0x5b610b430a43a4be`).
- [ ] `hydra_tests -tc="rules: the retired Auto keys are read and ignored"` passes.
- [ ] `hydra_tests -tc="sp_cap round-trips as a number*"` passes: an INI line `sp_cap=auto` loads as 4.
- [ ] `.\build-cpp\Release\hydra_tests.exe` ends `Status: SUCCESS!`.
- [ ] `.\build-cpp\Release\hydra_uitest.exe --test analyze` prints `[PASS] analyze` (the SP cap row has no `Auto##spcapauto` checkbox), and `--all` passes every test.
- [ ] The score-neutral proof from the Global Constraints (with `$out = "$env:TEMP\hydra_ui_T6"`) prints nothing.

**Verify:** `.\build-cpp\Release\hydra_tests.exe; .\build-cpp\Release\hydra_uitest.exe --all` → `Status: SUCCESS!` and every GUI test `[PASS]`; then the score-neutral proof prints nothing.

**Steps:**

A note on deletions. A hook refuses an Edit that keeps under 40% of its old block. Where a step deletes a whole test case or function, make the Edit's `old_string` include the unchanged code right before it and repeat that code in `new_string`. For `src/search/pather.cpp`, where most of a region goes, Step 5 uses the `.new` file route from the Global Constraints.

- [ ] **Step 1: Write the failing tests.**

In `tests/test_rules.cpp`, the test `rules: defaults are the values Hydra always used` loses these two lines:

```cpp
    CHECK(r.auto_cap_ladder == std::vector<int>{16, 32, 64, 128, 256, 512});
    CHECK(r.auto_budget_s == 120.0);
```

In `rules: every key in the file is read`, delete the two file lines `"auto_cap_ladder = 8, 24\n"` and `"auto_budget_s = 30\n"`, and the two checks `CHECK(r.auto_cap_ladder == std::vector<int>{8, 24});` and `CHECK(r.auto_budget_s == 30.0);`.

In `rules: a bad value or an unknown key is an error that names the key`, delete:

```cpp
    CHECK(message_for("bad4", "auto_cap_ladder = 32, 16\n").find("auto_cap_ladder") !=
          std::string::npos);
```

In `rules: no rules value has the no-rules fingerprint`, replace the two `auto_fingerprint()` lines:

```cpp
    CHECK(core::default_rules().auto_fingerprint() != core::kNoRulesFingerprint);
```
becomes
```cpp
    CHECK(core::default_rules().retired_auto_fingerprint() != core::kNoRulesFingerprint);
```
and
```cpp
    CHECK(other.auto_fingerprint() != core::kNoRulesFingerprint);
```
becomes
```cpp
    CHECK(other.retired_auto_fingerprint() != core::kNoRulesFingerprint);
```

Delete the whole test case `rules: the Auto ladder and budget come from the rules`. Replace everything from the line `// ---- the fingerprint's scope (docs/adr/0014, amended 2026-09-26) ----------` to the end of the file with:

```cpp
// ---- the fingerprint's scope (docs/adr/0014, amended 2026-09-26) ----------

TEST_CASE("rules: the retired Auto keys are read and ignored") {
    // Auto is gone (2026-09-27), but a hydra_rules.ini that still sets its
    // two keys must keep loading: an error here would switch analysis off.
    // Any value is accepted, since the keys no longer do anything.
    core::Rules r = app::load_rules_file(write_rules("retired",
        "auto_cap_ladder = 32, 16\n"
        "auto_budget_s = banana\n"
        "max_tied_paths = 2\n"));
    CHECK(r.max_tied_paths == 2);
    core::Rules expected = core::default_rules();
    expected.max_tied_paths = 2;
    CHECK(r.fingerprint() == expected.fingerprint());
}

TEST_CASE("rules: the retired Auto fingerprint is what Hydra 1.8.4 stamped") {
    // Pinned. The store finds the results Auto saved by this value, so it must
    // equal 1.8.4's auto_fingerprint() under its default ladder. The fixed-cap
    // value beside it is the one on every 4-bar row of a real 1.8.4 database,
    // which proves the text both hash is unchanged.
    CHECK(core::default_rules().fingerprint() == 0x70d2e96669604cf2ull);
    CHECK(core::default_rules().retired_auto_fingerprint() == 0x5b610b430a43a4beull);
    // Every rule is in it, as it was in auto_fingerprint().
    core::Rules ties = core::default_rules();
    ties.max_tied_paths = 2;
    CHECK(ties.retired_auto_fingerprint() != core::default_rules().retired_auto_fingerprint());
    CHECK(ties.retired_auto_fingerprint() != ties.fingerprint());
}

TEST_CASE("rules: the default stamp is built once and matches a fresh record") {
    // HydraRecord's default fingerprint used to re-hash the default rules for
    // every record built, which includes every record decoded.
    CHECK(&core::default_stamp() == &core::default_stamp());
    CHECK(core::default_stamp().fixed == core::default_rules().fingerprint());
    CHECK(core::default_stamp().retired_auto ==
          core::default_rules().retired_auto_fingerprint());
    CHECK(HydraRecord{}.rules_fingerprint == core::default_stamp().fixed);
    CHECK(core::RulesStamp::none().fixed == core::kNoRulesFingerprint);
    CHECK(core::RulesStamp::none().retired_auto == core::kNoRulesFingerprint);
}

TEST_CASE("rules: a run is stamped with the rules fingerprint at every cap") {
    SearchSettings settings;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        settings.sp_cap = 8;
        CHECK(analyze_chart(song, settings).rules_fingerprint == settings.rules.fingerprint());
        settings.sp_cap = 4;
        CHECK(analyze_chart(song, settings).rules_fingerprint == settings.rules.fingerprint());
        break;
    }
}
```

In `tests/test_config.cpp`, replace the test case `sp_cap round-trips as a number or auto; pre-1.6 keys are ignored` with:

```cpp
TEST_CASE("sp_cap round-trips as a number; auto, zero, junk and pre-1.6 keys read as 4") {
    const std::string path = temp_ini("spcap");

    // A number reads back as that number, and cap_query asks for it exactly.
    Settings s;
    s.sp_cap = 64;
    REQUIRE(s.save_file(path));
    CHECK(Settings::load_file(path).sp_cap == 64);
    CHECK(Settings::load_file(path).cap_query().exact == 64);

    // Hydra 1.8.4 wrote "auto" for Auto, which is gone. It reads as Clone
    // Hero's 4, and saving writes the number back.
    {
        std::ofstream f(path, std::ios::trunc);
        f << "sp_cap=auto\n";
    }
    Settings from_auto = Settings::load_file(path);
    CHECK(from_auto.sp_cap == 4);
    CHECK(from_auto.cap_query().exact == 4);
    REQUIRE(from_auto.save_file(path));
    CHECK(Settings::load_file(path).sp_cap == 4);

    // The old Uncapped-edition keys, which the main app also used to write as
    // "off, 8", must not turn an existing INI into 8 bars. Garbage and zero
    // keep the default too.
    {
        std::ofstream f(path, std::ios::trunc);
        f << "sp_cap_enabled=0\n"
          << "sp_cap_value=8\n";
    }
    CHECK(Settings::load_file(path).sp_cap == 4);
    {
        std::ofstream f(path, std::ios::trunc);
        f << "sp_cap=0\n";
    }
    CHECK(Settings::load_file(path).sp_cap == 4);
    {
        std::ofstream f(path, std::ios::trunc);
        f << "sp_cap=banana\n";
    }
    CHECK(Settings::load_file(path).sp_cap == 4);
    std::remove(path.c_str());
}
```

In `tests/test_store.cpp`, replace the whole last test case, `editing the Auto ladder marks only Auto runs Stale` (from its `TEST_CASE` line to the end of the file), and the section comment above it, with:

```cpp
// ---- Auto removed (2026-09-27, interface redesign Task 6) ------------------

namespace {

// A result the way Hydra 1.8.4's Auto stamped it: the fixture's paths at
// `cap`, under the default rules' Auto fingerprint.
HydraRecord auto_run_at(int cap) {
    HydraRecord r = at_cap(cap);
    r.rules_fingerprint = core::default_rules().retired_auto_fingerprint();
    return r;
}

}  // namespace

TEST_CASE("the first open deletes the results Auto saved, and their paths, once") {
    const std::string path = temp_db("auto_delete");
    std::remove(path.c_str());
    const RecordKey kept{"h", "mode", CapQuery::at(4)};
    const RecordKey whatif{"h", "mode", CapQuery::at(8)};
    const RecordKey auto_same_chart{"h", "mode", CapQuery::at(16)};
    const RecordKey auto_only{"a", "mode", CapQuery::at(32)};

    std::vector<uint8_t> kept_bytes;
    {
        RecordStore store(path);
        store.add_song("h", "Song", "Artist", "Charter", fixture().song);
        store.add_song("a", "Other", "Artist", "Charter", fixture().song);
        store.add_record(kept, at_cap(4));
        store.add_record(whatif, at_cap(8));
        store.add_record(auto_same_chart, auto_run_at(16));
        store.add_record(auto_only, auto_run_at(32));
        kept_bytes = record_bytes(*store.get_record(kept).record);
        // This build accepts only the fixed-cap fingerprint, so an Auto row
        // reads Stale even before anything deletes it.
        CHECK(store.get_record(auto_only).status == RecordStatus::Stale);
        CHECK(store.counts().second == 4);
    }
    REQUIRE(scalar(path, "SELECT COUNT(*) FROM paths WHERE hyhash='a'") > 0);
    // A database Hydra 1.8.4 wrote has no mark yet.
    exec_on_file(path, "DELETE FROM meta WHERE key='auto_results_deleted'");

    {
        RecordStore store(path);
        CHECK(store.counts().second == 2);
        CHECK(store.get_record(auto_only).status == RecordStatus::NotAnalyzed);
        CHECK(store.get_record(auto_same_chart).status == RecordStatus::NotAnalyzed);
        // A fixed-cap what-if above 4 bars is not an Auto result and stays.
        CHECK(store.get_record(whatif).status == RecordStatus::Ready);
        const RecordLookup left = store.get_record(kept);
        REQUIRE(left.status == RecordStatus::Ready);
        CHECK(record_bytes(*left.record) == kept_bytes);
    }
    // The chart that held only an Auto row has no paths left; the other
    // chart's paths are still used by its kept rows.
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths WHERE hyhash='a'") == 0);
    CHECK(scalar(path, "SELECT COUNT(*) FROM path_refs WHERE hyhash='a'") == 0);
    CHECK(scalar(path, "SELECT COUNT(*) FROM paths WHERE hyhash='h'") > 0);
    CHECK(scalar(path, "SELECT COUNT(*) FROM meta WHERE key='auto_results_deleted'") == 1);

    // Marked done: a later open never deletes again.
    {
        RecordStore store(path);
        store.add_record(auto_only, auto_run_at(32));
    }
    {
        RecordStore store(path);
        CHECK(store.counts().second == 3);
        CHECK(store.get_record(auto_only).status == RecordStatus::Stale);
    }
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(path), ec);
}

TEST_CASE("a start with a bad rules file leaves the Auto results for the next good start") {
    const std::string path = temp_db("auto_delete_none");
    std::remove(path.c_str());
    const RecordKey auto_only{"a", "mode", CapQuery::at(32)};
    {
        RecordStore store(path);
        store.add_song("a", "Other", "Artist", "Charter", fixture().song);
        store.add_record(auto_only, auto_run_at(32));
    }
    exec_on_file(path, "DELETE FROM meta WHERE key='auto_results_deleted'");
    {
        // A bad hydra_rules.ini: there is no fingerprint to look for, so
        // nothing is deleted and nothing is marked done.
        RecordStore store(path, core::RulesStamp::none());
        CHECK(store.counts().second == 1);
    }
    CHECK(scalar(path, "SELECT COUNT(*) FROM meta WHERE key='auto_results_deleted'") == 0);
    {
        RecordStore store(path);
        CHECK(store.counts().second == 0);
    }
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(path), ec);
}
```

- [ ] **Step 2: Run them and watch them fail.** Run `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1` from the Bash tool. Expected: the build fails, and `build.log` names `retired_auto_fingerprint` and `retired_auto` as unknown members. That is the red state; the code in Steps 3 to 8 makes it compile.

- [ ] **Step 3: Rules.** In `src/core/rules.h`, replace the top comment's last two sentences:

```cpp
// used, so an absent file changes nothing. A stored record carries a
// fingerprint of the rules it ran under (docs/adr/0014): fingerprint() for a
// fixed-cap run, auto_fingerprint() for an Auto run.
```
with
```cpp
// used, so an absent file changes nothing. A stored record carries a
// fingerprint of the rules it ran under (docs/adr/0014): fingerprint().
```

Delete the ladder and the budget:

```cpp
    // Auto cap: the SP ceilings tried in order, and the seconds before a slow
    // rung is abandoned. The search reads the budget here and nowhere else;
    // nullopt runs every rung to the end (what tests use, so their results
    // stay deterministic). A fixed-cap run reads neither.
    std::vector<int> auto_cap_ladder{16, 32, 64, 128, 256, 512};
    std::optional<double> auto_budget_s = 120.0;
```

Replace the two fingerprint declarations:

```cpp
    // A 64-bit hash of every field that can change a fixed-cap run's answer:
    // every field above except auto_cap_ladder and auto_budget_s. Equal rules
    // give equal fingerprints in every build; any changed field gives a
    // different one. Never kNoRulesFingerprint.
    uint64_t fingerprint() const;
    // fingerprint()'s fields plus auto_cap_ladder: what an Auto run is
    // stamped with, since only an Auto run climbs the ladder. The budget is
    // in neither: a wall-clock limit can't make a result repeatable anyway.
    // Never kNoRulesFingerprint.
    uint64_t auto_fingerprint() const;
```
with
```cpp
    // A 64-bit hash of every field above: everything that can change a run's
    // answer. Equal rules give equal fingerprints in every build; any changed
    // field gives a different one. Never kNoRulesFingerprint.
    uint64_t fingerprint() const;
    // The fingerprint Hydra 1.8.4 and earlier stamped on an Auto run under
    // these rules and Auto's default ladder (16, 32, ..., 512). Auto is gone
    // (2026-09-27); the store reads this only to delete the results Auto
    // saved, once. Never kNoRulesFingerprint.
    uint64_t retired_auto_fingerprint() const;
```

Replace `RulesStamp`:

```cpp
// The two fingerprints a store accepts as "these rules": a fixed-cap run's
// and an Auto run's (docs/adr/0014, amended 2026-09-26). none() accepts
// nothing, so a store gated on it (a bad hydra_rules.ini) reads no row as
// Ready.
struct RulesStamp {
    uint64_t fixed = kNoRulesFingerprint;
    uint64_t autocap = kNoRulesFingerprint;
    static RulesStamp of(const Rules& rules) {
        return RulesStamp{rules.fingerprint(), rules.auto_fingerprint()};
    }
    static RulesStamp none() { return RulesStamp{}; }
};
```
with
```cpp
// What a store knows about the rules this process runs under. `fixed` is the
// one fingerprint it accepts as "these rules" (docs/adr/0014). `retired_auto`
// is what an Auto run under the same rules carried; the store reads it only
// to delete those results, and no row carrying it reads Ready. none() holds
// neither, so a store gated on it (a bad hydra_rules.ini) reads no row as
// Ready and deletes nothing.
struct RulesStamp {
    uint64_t fixed = kNoRulesFingerprint;
    uint64_t retired_auto = kNoRulesFingerprint;
    static RulesStamp of(const Rules& rules) {
        return RulesStamp{rules.fingerprint(), rules.retired_auto_fingerprint()};
    }
    static RulesStamp none() { return RulesStamp{}; }
};
```

In `src/core/rules.cpp`, the comment above `fixed_cap_text`:

```cpp
// Every field that can change a fixed-cap run's answer, one line each. The
// Auto ladder and the Auto budget are not here: a fixed-cap run never climbs
// the ladder, and the budget is a wall-clock limit no fingerprint can make
// repeatable (docs/adr/0014, amended 2026-09-26).
```
becomes
```cpp
// Every field that can change a run's answer, one line each. The text is
// frozen: changing it makes every stored result Stale (docs/adr/0014).
```

and `Rules::auto_fingerprint`:

```cpp
uint64_t Rules::auto_fingerprint() const {
    std::string text = fixed_cap_text(*this);
    text += "auto_cap_ladder=";
    for (int cap : auto_cap_ladder) text += std::to_string(cap) + ",";
    text += "\n";
    return hash_rules_text(text);
}
```
becomes
```cpp
uint64_t Rules::retired_auto_fingerprint() const {
    // Byte for byte what 1.8.4's auto_fingerprint() hashed under the default
    // ladder. Pinned by the test "rules: the retired Auto fingerprint is what
    // Hydra 1.8.4 stamped".
    return hash_rules_text(fixed_cap_text(*this) +
                           "auto_cap_ladder=16,32,64,128,256,512,\n");
}
```

In `src/app/rules_file.cpp`, replace the two Auto branches of `load_rules_file`:

```cpp
        else if (key == "auto_cap_ladder") {
            r.auto_cap_ladder.clear();
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                int cap = to_int(where, key, trim(item), 1);
                if (!r.auto_cap_ladder.empty() && cap <= r.auto_cap_ladder.back())
                    bad(where, key, v, "a rising list of caps");
                r.auto_cap_ladder.push_back(cap);
            }
            if (r.auto_cap_ladder.empty()) bad(where, key, v, "a rising list of caps");
        }
        else if (key == "auto_budget_s") {
            const double budget = to_double(where, key, v, 0.0);
            if (budget == 0.0) bad(where, key, v, "above zero");
            r.auto_budget_s = budget;
        }
```
with
```cpp
        // Auto's two keys (Auto was removed 2026-09-27). A file that still
        // sets them keeps loading; whatever they say is ignored.
        else if (key == "auto_cap_ladder" || key == "auto_budget_s") {}
```
and delete `#include <sstream>`, which only the ladder used.

- [ ] **Step 4: Settings.** In `src/app/config.h`, replace:

```cpp
    // The Star Power meter ceiling in bars. 4 is Clone Hero's rule (the
    // default). nullopt is "Auto": raise the ceiling until the score settles
    // (search/pather.h analyze_auto_cap). INI line: sp_cap=4 / sp_cap=auto.
    // The pre-1.6 keys sp_cap_enabled/sp_cap_value are ignored on load.
    std::optional<int> sp_cap = kCloneHeroSpCap;
```
with
```cpp
    // The Star Power meter ceiling in bars, at least 1. 4 is Clone Hero's
    // rule (the default); other values are what-ifs. INI line: sp_cap=4.
    // Hydra 1.8.4's sp_cap=auto, zero and junk load as 4. The pre-1.6 keys
    // sp_cap_enabled/sp_cap_value are ignored on load.
    int sp_cap = kCloneHeroSpCap;
```

In `src/app/config.cpp` `Settings::load_file`:

```cpp
        else if (key == "sp_cap") {
            if (value == "auto") s.sp_cap = std::nullopt;
            else if (int v = std::atoi(value.c_str()); v >= 1) s.sp_cap = v;
        }
```
becomes
```cpp
        else if (key == "sp_cap") {
            // "auto" (Auto, removed 2026-09-27) is 0 to atoi, so it keeps
            // the default 4, like zero and junk.
            if (int v = std::atoi(value.c_str()); v >= 1) s.sp_cap = v;
        }
```

In `Settings::save_file`:

```cpp
    if (sp_cap) f << "sp_cap=" << *sp_cap << "\n";
    else f << "sp_cap=auto\n";
```
becomes
```cpp
    f << "sp_cap=" << sp_cap << "\n";
```

And `Settings::cap_query`:

```cpp
    return store::CapQuery::from_setting(sp_cap);
```
becomes
```cpp
    return store::CapQuery::at(sp_cap);
```

- [ ] **Step 5: The search.** In `src/search/pather.h`, the top comment:

```cpp
// Analysis orchestration: one run at a fixed cap, the Auto SP-cap ladder,
// and the dispatch between them. Discovery and the batch thread pool live
// in app/analysis; this is only what produces a record for one chart.
```
becomes
```cpp
// Analysis orchestration: one run at the chosen SP cap. Discovery and the
// batch thread pool live in app/analysis; this is only what produces a
// record for one chart.
```

In `SearchSettings`:

```cpp
    // The SP meter ceiling in bars (4 = Clone Hero's rule). nullopt is Auto:
    // the ladder that raises the ceiling until the score settles.
    std::optional<int> sp_cap = kCloneHeroSpCap;
```
becomes
```cpp
    // The SP meter ceiling in bars (4 = Clone Hero's rule).
    int sp_cap = kCloneHeroSpCap;
```

In the `search_target` comment, `// that is not a fill node). settings.sp_cap must be a fixed cap (Auto is` and the line after it, `// rejected with std::invalid_argument). settings.depth_* and ms_filter are`, become one line: `// that is not a fill node). settings.depth_* and ms_filter are`. The `analyze_chart` comment:

```cpp
// Full analysis for one chart. settings.sp_cap is the SP meter ceiling in
// bars: 4 is Clone Hero's rule and runs exactly the classic single pass; any
// other number runs a single pass at that ceiling; nullopt is Auto, the
// self-settling ladder (settings.rules.auto_budget_s applies only there). Throws
// hydra::ChartFileError when the song has no notes.
```
becomes
```cpp
// Full analysis for one chart: one pass at settings.sp_cap bars (4 is Clone
// Hero's rule; any other number is a what-if). Throws hydra::ChartFileError
// when the song has no notes.
```

`src/search/pather.cpp` loses most of a region, so use the `.new` route. Read the file, then Write `src/search/pather.cpp.new` holding the whole current file with these four changes, and move it over with `Move-Item -Force src\search\pather.cpp.new src\search\pather.cpp`:

1. Delete the line `#include <chrono>`.
2. In the first anonymous namespace, delete `using bench_clock = std::chrono::steady_clock;` and the three lines `// Thrown out of the progress callback to abandon a ladder rung that has blown`, `// the time budget (auto_budget_s in hydra_rules.ini).`, `struct CapBudgetExceeded {};` (and the blank lines between them).
3. In `search_target`, delete the Auto guard:
   ```cpp
       if (!settings.sp_cap)
           throw std::invalid_argument(
               "search_target needs a fixed SP cap; Auto has no single graph to "
               "price the path against");

   ```
   and change `ScoreGraph graph(song, std::optional<int>(*settings.sp_cap),` to `ScoreGraph graph(song, std::optional<int>(settings.sp_cap),`.
4. Delete everything from the comment line `// Auto cap: raise the ceiling up the SP-cap ladder until the score settles,` through the closing `}` of `analyze_auto_cap` (the anonymous namespace's `}  // namespace` after it stays), and replace the whole of `analyze_chart` with:

```cpp
HydraRecord analyze_chart(const Song& song, const SearchSettings& settings,
                          const std::function<void(float)>& on_progress) {
    if (song.is_empty())
        throw ChartFileError("No drum notes in this chart.");

    // One pass at the chosen ceiling, Clone Hero's 4 bars included. The graph
    // is only built as tall as the song has phrases to bank -- no run can
    // exceed that -- so a huge cap on a short song stays cheap, and a 4-bar
    // graph built lower stores the same bytes (test "a 4-bar graph built at
    // the song's phrase count stores the same paths").
    const int build_cap = graph_build_cap(settings.sp_cap, song.sp_phrase_count());
    HydraRecord record =
        analyze_at_cap(song, settings.sp_cap, settings.depth_mode, settings.depth_value,
                       settings.ms_filter, build_cap, settings.legacy_fill_deadline,
                       settings.rules, /*want_allzero=*/true, on_progress);
    record.rules_fingerprint = settings.rules.fingerprint();
    return record;
}
```

This is the old fixed-cap branch unchanged, so no score can move.

In `src/app/analysis.cpp` (in `run_batch`), replace:

```cpp
    // "Already has a result" means a current-version record at the cap this
    // run would produce (Auto: any record above 4 bars) AND under this run's
    // ms limit and score range, so stale rows, other caps' rows and other
    // settings' rows are re-run rather than skipped.
    const store::CapQuery cap = store::CapQuery::from_setting(settings.sp_cap);
```
with
```cpp
    // "Already has a result" means a current-version record at exactly this
    // run's cap AND under this run's ms limit and score range, so stale rows,
    // other caps' rows and other settings' rows are re-run rather than skipped.
    const store::CapQuery cap = store::CapQuery::at(settings.sp_cap);
```

In `src/core/model.h` (`HydraRecord`), replace:

```cpp
    std::optional<int> sp_cap;
    bool sp_cap_converged = true;
    // The fingerprint of the rules the search ran under (blob v6, path
    // structure v4): Rules::fingerprint() for a fixed-cap run,
    // Rules::auto_fingerprint() for an Auto run. A record built in memory
```
with
```cpp
    std::optional<int> sp_cap;
    // Always true since Auto went (2026-09-27): Auto was the only search that
    // could stop before its score settled. Kept because the stored path
    // structure carries it (store/path_codec.cpp); dropping it would change
    // the record format.
    bool sp_cap_converged = true;
    // The fingerprint of the rules the search ran under (blob v6, path
    // structure v4): Rules::fingerprint(). Results Hydra 1.8.4's Auto saved
    // carry Rules::retired_auto_fingerprint() and are deleted when the store
    // opens (RecordStore::delete_auto_results). A record built in memory
```

In `src/store/path_codec.cpp` `flatten_record`, `    w.boolean(record.sp_cap_converged);` becomes `    w.boolean(record.sp_cap_converged);  // always true now; see core/model.h`.

In `src/app/path_view.cpp`, at the end of the function that builds the activation footer (find `"SP meter: "`), replace:

```cpp
    // Which SP ceiling this result was found under
    // (warning-colored when an Auto run ran out of
    // time before the score settled).
    if (record.sp_cap) {
        std::string bars = std::to_string(*record.sp_cap);
        if (record.sp_cap_converged) {
            view.footer.push_back({"SP meter: " + bars + " bars.", false});
        } else {
            view.footer.push_back(
                {"SP meter: " + bars +
                     " bars. The search ran out of time before the score "
                     "settled, so a higher meter may still score more.",
                 true});
        }
    }
```
with
```cpp
    // Which SP ceiling this result was found under.
    if (record.sp_cap)
        view.footer.push_back(
            {"SP meter: " + std::to_string(*record.sp_cap) + " bars.", false});
```

In `src/app/report.cpp` `generate_report` (find `cap_label`; T3 may have moved it), replace:

```cpp
    std::string cap_label = options.cap.exact
                                ? "SP cap " + std::to_string(*options.cap.exact) + " bars"
                                : "SP cap Auto";
```
with
```cpp
    std::string cap_label = "SP cap " + std::to_string(options.cap.exact) + " bars";
```

- [ ] **Step 6: The store.** In `src/store/record_store.h`, replace `CapQuery`:

```cpp
// Which cap's record a lookup wants. at(N): the record analyzed at exactly N
// bars. automatic(): the chart's highest cap above Clone Hero's 4 -- what an
// Auto run would reuse -- preferring current-version rows over stale ones.
struct CapQuery {
    std::optional<int> exact;
    static CapQuery at(int cap) { return CapQuery{cap}; }
    static CapQuery automatic() { return CapQuery{std::nullopt}; }
    // The user's SP cap setting as a query: a set cap asks for exactly that
    // one, "Auto" (unset) asks for whatever an Auto run would reuse.
    static CapQuery from_setting(std::optional<int> sp_cap) {
        return sp_cap ? at(*sp_cap) : automatic();
    }
    bool is_auto() const { return !exact.has_value(); }
```
with
```cpp
// Which cap's record a lookup wants: at(N), the record analyzed at exactly N
// bars. (Auto, which asked for "the newest row above 4 bars", was removed
// on 2026-09-27.)
struct CapQuery {
    int exact = kCloneHeroSpCap;
    static CapQuery at(int cap) { return CapQuery{cap}; }
```
(the two operators below it stay). The `RecordKey` comment:

```cpp
// One result's identity: the chart, the chart mode, the SP cap (ADR-0003) and
// the lens. `cap` is a query because a caller may ask for "whatever Auto would
// reuse"; a row itself always has an exact cap.
```
becomes
```cpp
// One result's identity: the chart, the chart mode, the SP cap (ADR-0003) and
// the lens.
```

The `prepare_row` comment:

```cpp
// Throws std::invalid_argument if the record carries no sp_cap (every
// analyzer result does), if the key names an exact cap that isn't the cap the
// record was analyzed at, or if the key's lens has the ms limit on at a value
// the record wasn't analyzed under -- each mismatch would file the result
// under settings it doesn't belong to. An automatic key takes whatever cap the
// record carries.
```
becomes
```cpp
// Throws std::invalid_argument if the record carries no sp_cap (every
// analyzer result does), if the key's cap isn't the cap the record was
// analyzed at, or if the key's lens has the ms limit on at a value the record
// wasn't analyzed under -- each mismatch would file the result under settings
// it doesn't belong to.
```

In the constructor's comment, `// under. A row stamped with neither of its two fingerprints reads Stale.` becomes these two lines:

```cpp
    // under. A row stamped with any other fingerprint reads Stale. The first
    // open by this build also deletes the results Auto saved (delete_auto_results).
```

In the private section, replace:

```cpp
    // The two fingerprints of the rules this process runs under (fixed-cap
    // and Auto); a row stamped with neither reads Stale. Computed once, when
    // the store opens.
    core::RulesStamp rules_fingerprint_;
```
with
```cpp
    // The fingerprint of the rules this process runs under, and the one an
    // Auto run under them carried (read only by delete_auto_results). A row
    // stamped with anything but `fixed` reads Stale. Computed once, when the
    // store opens.
    core::RulesStamp rules_fingerprint_;
```
and add after `void write_row(const PreparedRow& row);`:

```cpp
    // Deletes this chart's path nodes that no result refers to any more:
    // write_row's last step, and the Auto cleanup's. The caller holds the lock
    // (or is the constructor) and an open transaction. `caller` names the
    // operation in the error message.
    void collect_orphan_paths(const std::string& hyhash, const std::string& chartmode,
                              const char* caller);
    // Runs once per database file, when it opens: deletes every result
    // Hydra 1.8.4's Auto saved (user decision 7, 2026-09-27), then the path
    // nodes only they used, and marks it done in `meta`.
    void delete_auto_results();
```

In `src/store/record_store.cpp`, the results-table comment `// means "written later", which is how Auto picks the newest run.` becomes `// means "written later".`

`structure_is_current`:

```cpp
// Is this row's stored path tree in a path format this build reads, analyzed
// under the rules this process runs, as a fixed-cap run or as an Auto run?
// Takes the whole blob or just the substr(structure,1,12) a query selected.
bool structure_is_current(const std::vector<uint8_t>& structure_head,
                          const core::RulesStamp& rules) {
    if (structure_head.size() < kStructureHeadBytes) return false;
    const uint64_t fingerprint = read_le(structure_head, 4, 8);
    return layout_is_current(structure_head) &&
           (fingerprint == rules.fixed || fingerprint == rules.autocap);
}
```
becomes
```cpp
// Is this row's stored path tree in a path format this build reads, analyzed
// under the rules this process runs? Takes the whole blob or just the
// substr(structure,1,12) a query selected.
bool structure_is_current(const std::vector<uint8_t>& structure_head,
                          const core::RulesStamp& rules) {
    if (structure_head.size() < kStructureHeadBytes) return false;
    const uint64_t fingerprint = read_le(structure_head, 4, 8);
    return layout_is_current(structure_head) && fingerprint == rules.fixed;
}
```

In the comment above `placeholders`, `// (the first four structure bytes), and the two rules fingerprints (the next` becomes `// (the first four structure bytes), and the rules fingerprint (the next`. In `row_ready_sql`, `") AND substr(structure,5,8) IN (?, ?))";` becomes `") AND substr(structure,5,8) = ?)";`. In `bind_ready_params`, delete `    bind_blob(s, idx++, write_le(rules.autocap, 8));`.

The `outranks` comment:

```cpp
// Does `a` beat `b`? This version before another, then this path format
// before an older one, then the newest write. Newest, not tallest: an Auto
// run that settles below an older, taller row (a what-if the user typed) is
// the result the user just asked for, so every lookup must show it. The
// tallest rule showed the old row forever and "Analyze paths!" could never
// replace it. Write order is result_id: add_row deletes and re-inserts, so a
// rewritten row is newest.
```
becomes
```cpp
// Does `a` beat `b`? This version before another, then this path format
// before an older one, then the newest write. Write order is result_id:
// add_row deletes and re-inserts, so a rewritten row is newest. Since Auto
// went (2026-09-27) every lookup names one exact cap and lens, and the
// results table holds one row per cap and lens, so a chart offers one
// candidate; the order still decides if that ever changes.
```

`append_candidate_filter`, `bind_candidate_filter`, `analyzed_filter` and `bind_analyzed_filter` always name the cap now:

```cpp
    if (cap.exact) sql += " AND " + p + "sp_cap=?";
    else sql += " AND " + p + "sp_cap>" + std::to_string(kCloneHeroSpCap);
```
becomes `    sql += " AND " + p + "sp_cap=?";`;

```cpp
// Binds the lens's four parameters, plus one more for an exact cap.
int bind_candidate_filter(sqlite3_stmt* s, int idx, const CapQuery& cap, const Lens& lens) {
    idx = bind_lens(s, idx, lens);
    if (cap.exact) sqlite3_bind_int(s, idx++, *cap.exact);
    return idx;
}
```
becomes
```cpp
// Binds the lens's four parameters, then the cap.
int bind_candidate_filter(sqlite3_stmt* s, int idx, const CapQuery& cap, const Lens& lens) {
    idx = bind_lens(s, idx, lens);
    sqlite3_bind_int(s, idx++, cap.exact);
    return idx;
}
```
In `analyzed_filter`:

```cpp
    if (cap.exact) sql += " AND sp_cap=?";
    else sql += " AND sp_cap>" + std::to_string(kCloneHeroSpCap);
    return sql;
```
becomes
```cpp
    sql += " AND sp_cap=?";
    return sql;
```
its comment line `// exact cap when there is one.` becomes `// cap.`, and in `bind_analyzed_filter` `    if (cap.exact) sqlite3_bind_int(s, idx++, *cap.exact);` becomes `    sqlite3_bind_int(s, idx++, cap.exact);`. The `analyzed_filter` signature keeps its `cap` parameter, now unused in the text; mark it `[[maybe_unused]] const CapQuery& cap` so the build stays warning-free.

In `prepare_row`:

```cpp
    if (key.cap.exact && *key.cap.exact != *record.sp_cap)
        throw std::invalid_argument("prepare_row: key asks for sp_cap " +
                                    std::to_string(*key.cap.exact) +
```
becomes
```cpp
    if (key.cap.exact != *record.sp_cap)
        throw std::invalid_argument("prepare_row: key asks for sp_cap " +
                                    std::to_string(key.cap.exact) +
```

In the constructor, right after `    create_result_tables();`, add:

```cpp
    // Auto was removed (2026-09-27). Its results go the first time this
    // build opens the file. T7's summary backfill runs after this.
    delete_auto_results();
```

In `write_row`, step (2)'s comment line `//     re-insert must take a fresh result_id so Auto sees it as newest.` becomes `//     re-insert must take a fresh result_id so the newest write ranks first.`, and step (4):

```cpp
    // (4) Whatever the replaced row was the last owner of.
    {
        Stmt s = prepare(db_,
            "DELETE FROM paths WHERE hyhash=? AND chartmode=? AND phash NOT IN"
            " (SELECT phash FROM path_refs WHERE hyhash=? AND chartmode=?)");
        bind_text(s, 1, row.hyhash);
        bind_text(s, 2, row.chartmode);
        bind_text(s, 3, row.hyhash);
        bind_text(s, 4, row.chartmode);
        run(s, "path gc");
    }
}
```
becomes
```cpp
    // (4) Whatever the replaced row was the last owner of.
    collect_orphan_paths(row.hyhash, row.chartmode, "add_row");
}

void RecordStore::collect_orphan_paths(const std::string& hyhash,
                                       const std::string& chartmode, const char* caller) {
    Stmt s = prepare(db_,
        "DELETE FROM paths WHERE hyhash=? AND chartmode=? AND phash NOT IN"
        " (SELECT phash FROM path_refs WHERE hyhash=? AND chartmode=?)");
    bind_text(s, 1, hyhash);
    bind_text(s, 2, chartmode);
    bind_text(s, 3, hyhash);
    bind_text(s, 4, chartmode);
    // Same message as before for write_row: "add_row path gc failed: ...".
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string(caller) + " path gc failed: " +
                                 sqlite3_errmsg(db_));
}

namespace {
// The meta key that marks the Auto results deleted for this file.
constexpr const char* kAutoResultsDeletedKey = "auto_results_deleted";
}  // namespace

void RecordStore::delete_auto_results() {
    // Under RulesStamp::none() (a bad hydra_rules.ini) there is no
    // fingerprint to look for. Leave the key unset, so the next start with
    // good rules does it.
    if (rules_fingerprint_.retired_auto == core::kNoRulesFingerprint) return;
    if (meta_get(kAutoResultsDeletedKey)) return;

    // An Auto row is known only by the rules fingerprint in bytes 5..12 of
    // its structure blob (flatten_record writes it right after the format).
    // Auto rows made under other rules are already Stale; the next write of
    // their chart purges them (write_row step 1).
    const std::vector<uint8_t> auto_fp = write_le(rules_fingerprint_.retired_auto, 8);
    exec("BEGIN");
    try {
        // The charts that hold one, so their orphaned paths can be collected.
        std::vector<std::pair<std::string, std::string>> charts;
        {
            Stmt s = prepare(db_,
                "SELECT DISTINCT hyhash, chartmode FROM results"
                " WHERE substr(structure,5,8) = ?");
            bind_blob(s, 1, auto_fp);
            while (sqlite3_step(s) == SQLITE_ROW)
                charts.emplace_back(column_text(s, 0), column_text(s, 1));
        }
        // Refs first, always: they are what keep a result's paths alive.
        for (const char* sql :
             {"DELETE FROM path_refs WHERE result_id IN"
              " (SELECT result_id FROM results WHERE substr(structure,5,8) = ?)",
              "DELETE FROM results WHERE substr(structure,5,8) = ?"}) {
            Stmt s = prepare(db_, sql);
            bind_blob(s, 1, auto_fp);
            if (sqlite3_step(s) != SQLITE_DONE)
                throw std::runtime_error(std::string("deleting Auto results failed: ") +
                                         sqlite3_errmsg(db_));
        }
        for (const auto& [hyhash, chartmode] : charts)
            collect_orphan_paths(hyhash, chartmode, "Auto cleanup");
        meta_set(kAutoResultsDeletedKey, "1");
        exec("COMMIT");
    } catch (...) {
        rollback_if_open(db_);
        throw;
    }
}
```

The constructor runs before anyone else holds the store, so this needs no lock, like the other open-time `exec` calls.

- [ ] **Step 7: The command-line tools.** In `src/cli/batch.cpp`:

```cpp
    if (settings.sp_cap) std::printf("SP cap     : %d bars\n", *settings.sp_cap);
    else std::printf("SP cap     : Auto\n");
```
becomes
```cpp
    std::printf("SP cap     : %d bars\n", settings.sp_cap);
```

In `tools/replay.cpp`, the usage line `"  hydra_replay dump  --chart <file> --db <path> [--cap N|auto]\n"` becomes `"  hydra_replay dump  --chart <file> --db <path> [--cap N]\n"`. In `settings_from`:

```cpp
    if (a.cap == "auto") s.sp_cap = std::nullopt;
    else s.sp_cap = std::atoi(a.cap.c_str());
```
becomes
```cpp
    const int cap = std::atoi(a.cap.c_str());
    if (cap < 1)
        throw std::runtime_error("--cap takes a whole number of bars, 1 or more (4 is "
                                 "Clone Hero's rule), not \"" + a.cap + "\"");
    s.sp_cap = cap;
```
In `cmd_target`, delete the Auto guard:

```cpp
    if (a.cap == "auto") {
        std::fprintf(stderr,
                     "target needs a fixed --cap: Auto has no single graph to "
                     "price the path against.\n");
        return 2;
    }
```
(`--cap auto` now fails in `settings_from` with the message above, caught by `main`), and change `              {"sp_cap", cfg.sp_cap ? *cfg.sp_cap : -1},` to `              {"sp_cap", cfg.sp_cap},`. The dump's `{"sp_cap", rec.sp_cap ? *rec.sp_cap : -1},` reads the record and stays.

In `tools/bench.cpp` `folder_breakdown`, `                gui.sp_cap.value_or(-1), gui.depth_value, gui.mslimit_value);` becomes `                gui.sp_cap, gui.depth_value, gui.mslimit_value);`, and

```cpp
        std::printf("  best score %lld | %d paths | sp_cap %d (%s)\n\n", best,
                    static_cast<int>(rec.all_paths().size()), rec.sp_cap.value_or(-1),
                    rec.sp_cap_converged ? "settled" : "unsettled");
```
becomes
```cpp
        std::printf("  best score %lld | %d paths | sp_cap %d\n\n", best,
                    static_cast<int>(rec.all_paths().size()), rec.sp_cap.value_or(-1));
```
In `corpus_bench`, `    auto bench = [&](const char* name, std::optional<int> cap, int dvalue) {` becomes `    auto bench = [&](const char* name, int cap, int dvalue) {`, the three lines

```cpp
        // No budget, as before: every Auto rung runs to the end, so the
        // timing is of the search and not of a wall-clock cut-off.
        settings.rules.auto_budget_s = std::nullopt;
```
go, and so does `    bench("auto d4", std::nullopt, 4);`.

- [ ] **Step 8: The UI.** In `src/ui/app_state.h` `DetailsViewState`, delete:

```cpp
    // The SP cap number box keeps its last value while Auto is ticked, so
    // unticking returns to it.
    int last_cap = kCloneHeroSpCap;
```

In `src/ui/app_state.cpp` `AppState::close_details`, `    // and the search would keep burning CPU (up to the Auto budget) unseen.` becomes `    // and the search would keep burning CPU unseen.` (T9 rewrites this function later; this only removes the Auto mention.)

In `src/ui/details_panel.cpp` `render_controls` (T1 moved it there from `details_view.cpp`), replace the SP cap block:

```cpp
    // The SP meter ceiling in bars: 4 is Clone Hero's rule; other values are
    // what-ifs. "Auto" raises the ceiling until the score settles. Records are
    // kept per cap, so changing it re-reads which record this song shows.
    {
        ImGui::TextUnformatted("SP cap:");
        ImGui::SameLine(px(100));
        // The number box keeps its last value while Auto is ticked, so
        // unticking returns to it.
        int& last_cap = app.details_ui.last_cap;
        if (app.settings.sp_cap) last_cap = *app.settings.sp_cap;
        bool spcap_auto = !app.settings.sp_cap.has_value();
        begin_disabled_input(spcap_auto);
        ImGui::SetNextItemWidth(px(100));
        if (ImGui::InputInt("##spcapvalue", &last_cap)) {
            if (last_cap < 1) last_cap = 1;
            app.settings.sp_cap = last_cap;
            app.edit_settings();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("bars");
        end_disabled_input(spcap_auto);
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto##spcapauto", &spcap_auto)) {
            app.settings.sp_cap = spcap_auto ? std::nullopt : std::optional<int>(last_cap);
            app.commit_settings();
        }
        hint((std::to_string(kCloneHeroSpCap) +
              " bars is Clone Hero's rule. Higher caps are what-ifs; Auto raises the "
              "cap until the score stops improving.").c_str());
    }
```
with
```cpp
    // The SP meter ceiling in bars: 4 is Clone Hero's rule; other values are
    // what-ifs. Records are kept per cap, so changing it re-reads which record
    // this song shows.
    {
        ImGui::TextUnformatted("SP cap:");
        ImGui::SameLine(px(100));
        ImGui::SetNextItemWidth(px(100));
        if (ImGui::InputInt("##spcapvalue", &app.settings.sp_cap)) {
            if (app.settings.sp_cap < 1) app.settings.sp_cap = 1;
            app.edit_settings();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("bars");
        hint((std::to_string(kCloneHeroSpCap) +
              " bars is Clone Hero's rule. Higher caps are what-ifs.").c_str());
    }
```

The label `##spcapvalue` stays; T9 renames it to `##spcap` when it moves the row into the settings bar.

- [ ] **Step 9: The rest of the tests.** These only drop Auto; none changes what a kept test proves.

`tests/corpus_util.h` `add_settings`: the comment

```cpp
// Every SearchSettings field that can change a record. fingerprint() leaves
// out the Auto ladder and the Auto budget. Only an Auto run reads those two,
// so an Auto key adds auto_fingerprint() (the ladder) and the budget, and a
// fixed-cap key leaves them out: a loop that clears the budget and one that
// keeps the default then share one fixed-cap answer.
```
becomes `// Every SearchSettings field that can change a record.`, `    opt(s.sp_cap);` becomes `    k << s.sp_cap << '|';`, and this block goes:

```cpp
    if (!s.sp_cap) {
        k << s.rules.auto_fingerprint() << '|';
        opt(s.rules.auto_budget_s);
    }
```

`tests/test_config.cpp`: in `record_key carries the chartmode, the SP cap and the lens`, the opening

```cpp
    Settings s;
    s.sp_cap = std::nullopt;
    s.mslimit_enabled = true;
    s.mslimit_value = 10;
    s.depth_mode = 0;
    s.depth_value = 4;
    store::RecordKey auto_key = s.record_key("abc");
    CHECK(auto_key.hyhash == "abc");
    CHECK(auto_key.chartmode == s.chartmode_key());
    CHECK(auto_key.cap == store::CapQuery::automatic());
    CHECK(auto_key.lens == store::Lens::from(10, 0, 4));
```
becomes
```cpp
    Settings s;
    s.mslimit_enabled = true;
    s.mslimit_value = 10;
    s.depth_mode = 0;
    s.depth_value = 4;
    store::RecordKey default_key = s.record_key("abc");
    CHECK(default_key.hyhash == "abc");
    CHECK(default_key.chartmode == s.chartmode_key());
    CHECK(default_key.cap == store::CapQuery::at(4));
    CHECK(default_key.lens == store::Lens::from(10, 0, 4));
```
In `batch_run bundles one Settings' chartmode, lens and search settings`, `    s.sp_cap = std::nullopt;` becomes `    s.sp_cap = 16;` and `    CHECK(!run.settings.sp_cap.has_value());` becomes `    CHECK(run.settings.sp_cap == 16);`. Rename `to_analysis_settings maps the cap and its Auto budget` to `to_analysis_settings maps the cap`; its comment `    // A fixed cap: a single run. The rules still carry the Auto budget; only` / `    // an Auto run reads it (analyze_chart).` becomes `    // The cap rides along as it is.`, and this block goes:

```cpp
    // Auto: the ladder and its budget come along in the rules.
    s.sp_cap = std::nullopt;
    a = s.to_analysis_settings();
    CHECK_FALSE(a.sp_cap.has_value());
    CHECK(a.rules.auto_budget_s == s.rules.auto_budget_s);

```

`tests/test_store.cpp`:
- `struct Config`: `    std::optional<int> cap;  // nullopt = Auto` becomes `    int cap;`. The matrix comment `// filter, a fixed what-if cap, and Auto.` becomes `// filter, and a fixed what-if cap.`, and the row `    {"auto.scores.200", std::nullopt, DepthMode::Scores, 200, std::nullopt},` goes.
- In `records round-trip through RecordStore across the corpus and config matrix`, delete `// No budget: every Auto rung runs to the end, so the result`, `// never depends on how busy the machine is.` and `settings.rules.auto_budget_s = std::nullopt;`.
- In `stored transfer scales equal a live recompute after a store round trip`, delete the config row `{"auto", std::nullopt, DepthMode::Scores, 4, std::nullopt},`, and the two lines `// No budget, as before this field moved into the rules.` and `settings.rules.auto_budget_s = std::nullopt;`.
- In `RecordStore maintenance: has_record, list_records, reindex`, delete `    CHECK_FALSE(store.has_record(RecordKey{"h1", "Expert Pro Drums, 2x Bass", CapQuery::automatic()}));`.
- Rename `records at different caps coexist; Auto picks the newest current one` to `records at different caps coexist; each lookup sees only its own cap`. In it, delete the block

  ```cpp
      // Auto takes the newest row above 4 and counts it as already analyzed.
      CHECK(store.get_record(RecordKey{"h", "mode", CapQuery::automatic()}).record->sp_cap == 32);
      CHECK(store.has_record(RecordKey{"h", "mode", CapQuery::automatic()}));
      CHECK(store.get_summary(RecordKey{"h", "mode", CapQuery::automatic()}).status ==
            RecordStatus::Ready);

  ```
  replace `    // A stale 64-bar row does not outrank a current 32-bar one -- for single` with `    // A stale 64-bar row leaves the 32-bar answer alone -- for single`, and in the lines after `store.add_row(stale);` change the four `CapQuery::automatic()` to `CapQuery::at(32)` (the `get_record`, the `list_records` and the `for_each_blob` calls). Delete the block from `    // With only a 4-bar row, Auto has nothing to reuse.` through `    CHECK_FALSE(only4.has_record(RecordKey{"h", "mode", CapQuery::automatic()}));`. The `reindex` check at the end stays (3 rows).
- Delete the whole test case `an Auto run that settles below an existing row becomes the Auto answer`.
- Replace the test case `the listing and a lookup agree on which row is a chart's answer` with:

  ```cpp
  TEST_CASE("the listing and a lookup agree on which row is a chart's answer") {
      // The lock between the two paths. Each chart below holds rows at several
      // caps. At every cap, whatever get_record picks is what the listing must
      // show, and when that pick is not readable the chart must not be listed.
      RecordStore store(":memory:");
      const std::vector<const char*> charts = {"two_caps", "over_stale", "over_sentinel",
                                               "all_stale"};
      for (const char* hash : charts)
          store.add_song(hash, hash, "Artist", "Charter", fixture().song);

      // Two current rows at two caps.
      store.add_record(RecordKey{"two_caps", "mode", CapQuery::at(32)}, at_cap(32));
      store.add_record(RecordKey{"two_caps", "mode", CapQuery::at(16)}, at_cap(16));

      // A current row and a taller stale one. The stale row goes in second
      // because a current-version write purges the chart's other-version rows.
      store.add_record(RecordKey{"over_stale", "mode", CapQuery::at(8)}, at_cap(8));
      PreparedRow stale =
          prepare_row(RecordKey{"over_stale", "mode", CapQuery::at(64)}, at_cap(64));
      stale.hyversion = "0.0.0";
      store.add_row(stale);

      // A current row and a taller row an old migration left: the migrated row
      // is no candidate at all.
      PreparedRow sentinel =
          prepare_row(RecordKey{"over_sentinel", "mode", CapQuery::at(64)}, at_cap(64));
      sentinel.lens.ms_enabled = -1;
      store.add_row(sentinel);
      store.add_record(RecordKey{"over_sentinel", "mode", CapQuery::at(8)}, at_cap(8));

      // Nothing readable at all.
      PreparedRow only_stale =
          prepare_row(RecordKey{"all_stale", "mode", CapQuery::at(16)}, at_cap(16));
      only_stale.hyversion = "0.0.0";
      store.add_row(only_stale);

      auto listed_at = [&](int cap) {
          std::unordered_map<std::string, int> listed;
          for (const RecordListing& r : store.list_records(std::nullopt, CapQuery::at(cap),
                                                           Lens{}, SortColumn::Score, true))
              listed[r.hyhash] = r.sp_cap;
          return listed;
      };

      for (int cap : {8, 16, 32, 64}) {
          std::unordered_map<std::string, int> listed = listed_at(cap);
          for (const char* hash : charts) {
              INFO(hash << " at " << cap);
              const RecordKey key{hash, "mode", CapQuery::at(cap)};
              const RecordLookup rec = store.get_record(key);
              CHECK(store.get_summary(key).status == rec.status);
              if (rec.status == RecordStatus::Ready) {
                  REQUIRE(listed.count(hash) == 1);
                  CHECK(listed[hash] == rec.record->sp_cap);
              } else {
                  CHECK(listed.count(hash) == 0);
              }
          }
      }

      // Spelled out, so a change that moves both paths together still has to
      // answer for itself.
      CHECK(listed_at(16).at("two_caps") == 16);
      CHECK(listed_at(32).at("two_caps") == 32);
      CHECK(listed_at(8).at("over_stale") == 8);
      CHECK(listed_at(64).count("over_stale") == 0);
      CHECK(listed_at(8).at("over_sentinel") == 8);
      CHECK(listed_at(64).count("over_sentinel") == 0);
      CHECK(store.get_record(RecordKey{"all_stale", "mode", CapQuery::at(16)}).status ==
            RecordStatus::Stale);
  }
  ```
- In `prepare_row refuses a key whose exact cap isn't the record's`, delete

  ```cpp
      // An automatic key takes whatever cap the record carries.
      PreparedRow row = prepare_row(RecordKey{"h", "mode", CapQuery::automatic()}, rec);
      CHECK(row.sp_cap == 4);

  ```
  and change `    // So does the matching exact key.` to `    // The matching key is fine.`.
- In `RecordKey compares on every part of the identity`, delete `    CHECK_FALSE(key == RecordKey{"h", "mode", CapQuery::automatic(), kLensA});` and replace

  ```cpp
      CHECK(CapQuery::at(4) != CapQuery::automatic());
      CHECK(CapQuery::automatic() == CapQuery::from_setting(std::nullopt));
      CHECK(CapQuery::at(8) == CapQuery::from_setting(8));
  ```
  with
  ```cpp
      CHECK(CapQuery::at(4) != CapQuery::at(8));
      CHECK(CapQuery{} == CapQuery::at(kCloneHeroSpCap));
  ```
- Delete the whole test case `Auto answers inside the lens it was asked about` (the lens tests above it already cover exact caps per lens).
- In `analyzed_hashes names exactly the charts has_record would skip`, `    for (const CapQuery& cap : {CapQuery::at(4), CapQuery::at(8), CapQuery::automatic()}) {` becomes `    for (const CapQuery& cap : {CapQuery::at(4), CapQuery::at(8)}) {`.
- In `get_summaries answers a page the same as get_summary row by row`, `    for (const CapQuery& cap : {CapQuery::at(4), CapQuery::automatic()}) {` becomes `    for (const CapQuery& cap : {CapQuery::at(4), CapQuery::at(16), CapQuery::at(32)}) {`.

`tests/test_report.cpp`: `int fill_store(store::RecordStore& store, std::optional<int> cap, int want) {` becomes `int fill_store(store::RecordStore& store, int cap, int want) {`; in it delete `    settings.rules.auto_budget_s = std::nullopt;` and change `store::CapQuery::from_setting(cap)` to `store::CapQuery::at(cap)`. `void check_cap(std::optional<int> cap) {` becomes `void check_cap(int cap) {`; `    const store::CapQuery query = cap ? store::CapQuery::at(*cap) : store::CapQuery::automatic();` becomes `    const store::CapQuery query = store::CapQuery::at(cap);`; `    MESSAGE((cap ? std::to_string(*cap) + " bars" : "auto") << ": " << rows.size()` becomes `    MESSAGE(std::to_string(cap) << " bars: " << rows.size()`. Delete `TEST_CASE("report page embeds every stored record (Auto)") { check_cap(std::nullopt); }`. In `report lists only the wanted cap and names it`, delete the block from `    options.cap = store::CapQuery::automatic();` through the `CHECK(rank1 == 1);` after it. In `collect_rows: a blank or old-placeholder song name reads (unknown)`, delete `    settings.rules.auto_budget_s = std::nullopt;` and change `store::CapQuery::from_setting(settings.sp_cap)` to `store::CapQuery::at(settings.sp_cap)`. In `records_by_hash keys every listed record by its lower-case hash`, delete `    settings.rules.auto_budget_s = std::nullopt;`.

`tests/test_dm_report.cpp` `fill_store`: `                store::RecordKey{kHash, kMode, store::CapQuery::automatic()},` becomes `                store::RecordKey{kHash, kMode, store::CapQuery::at(kCloneHeroSpCap)},`.

`tests/test_analysis.cpp`: `        hydra::store::CapQuery::from_setting(run.settings.sp_cap);` becomes `        hydra::store::CapQuery::at(run.settings.sp_cap);`.

`tests/ui/uitest_details.cpp` `test_analyze` (T1 moved it there from `uitest_tests.cpp`; if T1's map put it in another `uitest_*.cpp`, find it by the function name and edit it there): replace

```cpp
    // Auto has nothing above 4 bars to reuse, so it reads as new too.
    ctx->ItemClick("**/Auto##spcapauto");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->settings.sp_cap.has_value(); }, 5));
    IM_CHECK(h.app->current_page.summaries[0].state == hydra::store::RecordStatus::NotAnalyzed);
    IM_CHECK(!hydra::app::Settings::load_file(h.ini_path).sp_cap.has_value());
    ctx->ItemClick("**/Auto##spcapauto");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.sp_cap == 4; }, 5));
    IM_CHECK(h.app->current_page.summaries[0].state == hydra::store::RecordStatus::Ready);
```
with
```cpp
    // Auto is gone (2026-09-27): the SP cap row is a number and nothing else.
    IM_CHECK(ctx->ItemInfo("**/Auto##spcapauto", ImGuiTestOpFlags_NoError).ID == 0);
```

- [ ] **Step 10: Build and run everything.** From the Bash tool: `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1`, then the same for `-Target hydra_uitest`, `-Target Hydra`, `-Target hydra_batch`, `-Target hydra_report`, `-Target hydra_fillcompare`, and the two tools the default build skips (EXCLUDE_FROM_ALL), `-Target hydra_replay` and `-Target hydra_bench`. Every one must build with no warnings. Then run `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`. The new and rewritten test cases pass. Three fewer cases run than before this task: seven are deleted (three in `test_store.cpp`, three in `test_rules.cpp`, one in `test_report.cpp`) and four are added (two in each of `test_store.cpp` and `test_rules.cpp`). Then `.\build-cpp\Release\hydra_uitest.exe --test analyze` → `[PASS] analyze`, and `.\build-cpp\Release\hydra_uitest.exe --all` → every test `[PASS]`.

- [ ] **Step 11: Prove no score changed.** Run the score-neutral proof from the Global Constraints with `$out = "$env:TEMP\hydra_ui_T6"`. Expected: `Compare-Object` prints nothing. The fixed-cap search path and the fingerprint text are unchanged, so the baseline's rows read Ready and every chart scores the same.

- [ ] **Step 12: Check nothing of Auto is left.** Run `rg -n "is_auto\(|automatic\(\)|from_setting|analyze_auto_cap|auto_fingerprint|autocap|spcapauto|last_cap|CapBudgetExceeded" src tests tools`. Expected: no output. Run `rg -n "auto_cap_ladder|auto_budget_s" src tests tools`. Expected: only `src/app/rules_file.cpp` (the ignore line) and `tests/test_rules.cpp` (`rules: the retired Auto keys are read and ignored`). Run `git diff --stat <base>..HEAD` and check every file is in this task's Files list.

- [ ] **Step 13: Commit.**

```bash
git add src/core/rules.h src/core/rules.cpp src/app/rules_file.cpp src/app/config.h src/app/config.cpp src/search/pather.h src/search/pather.cpp src/store/record_store.h src/store/record_store.cpp src/store/path_codec.cpp src/core/model.h src/app/path_view.cpp src/app/analysis.cpp src/app/report.cpp src/cli/batch.cpp tools/replay.cpp tools/bench.cpp src/ui/app_state.h src/ui/app_state.cpp src/ui/details_panel.cpp tests/test_rules.cpp tests/test_config.cpp tests/test_store.cpp tests/test_report.cpp tests/test_dm_report.cpp tests/test_analysis.cpp tests/corpus_util.h tests/ui/uitest_details.cpp
git commit -m "Remove Auto: the SP cap is a plain number, Auto's stored results are deleted once" -m "Task: 6" -m "Agent: <agent id>" -m "Session: <session id>" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Library summaries in the store

Today the library can only ask the store two things about a chart: its status and its best-path string. The new library needs more. The Best path column sorts by score, and the search filters `stars:7` and `squeeze<=20` test the star count and the hardest squeeze of the best path. The score and hardest squeeze are already stored as summary columns on every result, but `get_summaries` doesn't hand them out, and the star count isn't stored at all. This task adds a `stars` summary column, computed at save time from the best path by `core/stars`, and makes `get_summaries` return the whole summary. Older databases get the column when they open, and their readable rows are filled in place, so nobody re-analyzes anything. `get_summaries` also learns to split a big request into chunks, because SQLite refuses a statement with more than 32,766 bound values and the library now asks about every chart at once. You won't see anything change yet; Task 12 draws it.

**Wave:** 2. **Depends on:** T1 (wave 1 merged). **Expected overlaps:** T6 in `src/store/record_store.{h,cpp}` and `tests/test_store.cpp`. T6 edits `CapQuery`, the two rules fingerprints and adds a one-time delete of Auto rows to the store's constructor; this task edits the summary columns, `get_summaries`, `list_records`, `reindex`, and adds its fill call to the same constructor. Keep both, with T6's delete before this task's fill. T6 also removes the Auto entry from `kMatrix` in `test_store.cpp`; this task adds one line to that file's `diff_summary` helper and appends new test cases at the end. New overlap found while drafting: `src/core/stars.{h,cpp}` and `tests/test_stars.cpp` gain one function each. Both are on T7's file list.

**Goal:** Every stored result carries its best path's star count, every stored song carries its length, and `get_summaries` returns each chart's full summary for any number of charts.

**Files:**
- Modify: `src/core/stars.h`, `src/core/stars.cpp` (new `stars_for_score` and `path_stars`)
- Modify: `src/store/record_store.h`, `src/store/record_store.cpp`
- Test: `tests/test_stars.cpp` (two new cases), `tests/test_store.cpp` (four new cases, one line in `diff_summary`)

**Acceptance Criteria:**
- [ ] `stars: path_stars counts cutoffs reached without the solo bonus` passes.
- [ ] `stars: Burnout's optimal path earns 7 stars` passes (378,315 points against a 7-star cutoff of 335,500).
- [ ] `a saved result stores its best path's star count` passes, including after `reindex()`.
- [ ] `get_summaries answers a whole library in chunks` passes with 20,000 hashes.
- [ ] `a database from before the stars column gets its stars filled on open` passes.
- [ ] `under rules that make every row Stale, the stars fill changes nothing` passes.
- [ ] `a stored song keeps its length, and an old songmeta row reads none` passes (Step 8a).
- [ ] Every existing `hydra_tests` case still passes, including `records round-trip through RecordStore across the corpus and config matrix` (which now compares `stars` too).
- [ ] The score-neutral proof from the Global Constraints prints nothing.
- [ ] Opening a copy of the installed database the first time (the one-time fill) takes at most 10 seconds longer than opening it again; the executor reports both times.

**Verify:** `.\build-cpp\Release\hydra_tests.exe -tc="*stars*,*summar*,*round-trip*,*keeps its length*"` → the last line reads `[doctest] Status: SUCCESS!`.

**Steps:**

- [ ] **Step 1: Write the failing star-count tests.**

`core/stars` computes the cutoffs today but nothing counts the stars a score earns. The Stars tab never needed it; the store and the panel headline (T9) do. The count belongs next to the cutoffs, so there is one rule for it. Append to `tests/test_stars.cpp`:

```cpp
TEST_CASE("stars: path_stars counts cutoffs reached without the solo bonus") {
    // Base 1300, so the cutoffs are 130, 650, 1300, 2600, 3640, 4680, 5720
    // (the star_cutoffs case above).
    Path path;
    path.score_base = 1000;
    path.score_ghosts = 100;
    path.score_accents = 200;
    const StarCutoffs sc = star_cutoffs(path);

    CHECK(stars_for_score(sc, 0) == 0);
    CHECK(stars_for_score(sc, 129) == 0);
    CHECK(stars_for_score(sc, 130) == 1);    // a cutoff counts when reached
    CHECK(stars_for_score(sc, 4679) == 5);
    CHECK(stars_for_score(sc, 4680) == 6);
    CHECK(stars_for_score(sc, 5720) == 7);
    CHECK(stars_for_score(sc, 1000000) == 7);  // the game stops at 7

    // 1300 base + 3380 combo = 4680 without the solo bonus: 6 stars. A solo
    // bonus big enough to pass the 7-star cutoff doesn't count.
    path.score_combo = 3380;
    path.score_solo = 2000;
    CHECK(path.totalscore() == 6680);
    CHECK(path_stars(path) == 6);
    path.score_combo = 3379;
    CHECK(path_stars(path) == 5);
}

TEST_CASE("stars: Burnout's optimal path earns 7 stars") {
    // Green Day - Burnout at Expert, Pro Drums, 2x Bass, cap 4: optimal
    // 378,315 against a 7-star cutoff of 335,500 (the plan's reference data).
    const std::string chart = corpus::root() +
        "/common/Summer Blast _25 Setlist/Tier 4/Green Day - Burnout/notes.mid";
    app::AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 2;
    settings.ms_filter = 10.0;
    const HydraRecord& record = corpus::analyzed(chart, settings);
    REQUIRE(!record.paths.empty());
    const Path& best = record.best_path();
    CHECK(best.totalscore() == 378315);
    CHECK(star_cutoffs(best).cutoffs[kMaxStars - 1] == 335500);
    CHECK(path_stars(best) == 7);
}
```

- [ ] **Step 2: Build and watch them fail.**

Run `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1` from the Bash tool. Expected: the build fails with `'stars_for_score': identifier not found` and `'path_stars': identifier not found` in `test_stars.cpp`.

- [ ] **Step 3: Add the two functions to `core/stars`.**

In `src/core/stars.h`, after the `star_cutoffs` declaration:

```cpp
StarCutoffs star_cutoffs(const Path& path);

// How many stars (0..kMaxStars) a score earns against these cutoffs. The score
// must already leave out the solo bonus, as the game counts it: the game adds
// the solo bonus only after counting stars.
int stars_for_score(const StarCutoffs& cutoffs, int64_t score_without_solo);

// The stars a path earns: its total score minus its solo bonus, against its
// own cutoffs. The one place a star count is worked out.
int path_stars(const Path& path);
```

In `src/core/stars.cpp`, after `star_cutoffs`:

```cpp
int stars_for_score(const StarCutoffs& cutoffs, int64_t score_without_solo) {
    // The game's loop: add a star while the score reaches the next cutoff,
    // and stop at 7.
    int stars = 0;
    while (stars < kMaxStars && score_without_solo >= cutoffs.cutoffs[stars]) ++stars;
    return stars;
}

int path_stars(const Path& path) {
    return stars_for_score(star_cutoffs(path), path.totalscore() - path.score_solo);
}
```

- [ ] **Step 4: Build and run the star tests.**

Build `hydra_tests` as in Step 2, then run `.\build-cpp\Release\hydra_tests.exe -tc="stars:*"`. Expected: every `stars:` case passes, the two new ones included.

- [ ] **Step 5: Write the failing store tests.**

In `tests/test_store.cpp`, add `#include "core/stars.h"` after `#include "core/squeeze_rating.h"`, and add one line to `diff_summary` so the round-trip test compares the new column too:

Before:
```cpp
    if (a.pathcount != b.pathcount) return "pathcount";
    return "";
```
After:
```cpp
    if (a.pathcount != b.pathcount) return "pathcount";
    if (a.stars != b.stars) return "stars";
    return "";
```

Append these cases at the end of the file. They use the file's existing helpers `fixture()`, `at_cap()`, `temp_db()`, `scalar()` and `exec_on_file()`.

```cpp
// ---- library summaries (interface redesign, Task 7) ------------------------

TEST_CASE("a saved result stores its best path's star count") {
    RecordStore store(":memory:");
    for (const char* h : {"ready", "stale"})
        store.add_song(h, h, "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"ready", "mode", CapQuery::at(4)}, at_cap(4));
    PreparedRow stale = prepare_row(RecordKey{"stale", "mode", CapQuery::at(4)}, at_cap(4));
    stale.hyversion = "0.0.0";
    store.add_row(stale);

    const Path& best = fixture().record.best_path();
    const PathSummary expected = summarize_record(fixture().record);
    REQUIRE(expected.stars.has_value());
    CHECK(*expected.stars == path_stars(best));

    auto check = [&] {
        const std::vector<SummaryLookup> got =
            store.get_summaries({"ready", "stale", "none"}, "mode", CapQuery::at(4), Lens{});
        REQUIRE(got.size() == 3);
        CHECK(got[0].status == RecordStatus::Ready);
        CHECK(got[0].summary.score == best.totalscore());
        CHECK(got[0].summary.hardest_ms == expected.hardest_ms);
        CHECK(got[0].summary.stars == expected.stars);
        CHECK(diff_summary(got[0].summary, expected) == "");
        // Only a Ready answer carries a summary.
        CHECK(got[1].status == RecordStatus::Stale);
        CHECK_FALSE(got[1].summary.score.has_value());
        CHECK(got[2].status == RecordStatus::NotAnalyzed);
        CHECK_FALSE(got[2].summary.stars.has_value());

        const std::vector<RecordListing> listing =
            store.list_records(std::nullopt, CapQuery::at(4), Lens{}, SortColumn::Score, true);
        REQUIRE(listing.size() == 1);
        CHECK(listing[0].summary.stars == expected.stars);
        CHECK(listing[0].sp_cap == 4);
    };
    check();
    // reindex rewrites every summary column, stars included.
    store.reindex();
    check();
}

TEST_CASE("get_summaries answers a whole library in chunks") {
    // SQLite refuses more than 32,766 bound values in one statement. The
    // library asks about every chart at once, so the store must split it.
    RecordStore store(":memory:");
    store.add_song("ready", "ready", "Artist", "Charter", fixture().song);
    store.add_record(RecordKey{"ready", "mode", CapQuery::at(4)}, at_cap(4));

    std::vector<std::string> hashes;
    for (int i = 0; i < 20000; ++i) {
        char h[16];
        std::snprintf(h, sizeof(h), "fake%05d", i);
        hashes.emplace_back(h);
    }
    // The real chart at the start, the middle and the end, so it lands in
    // more than one chunk's position.
    for (size_t at : {size_t{0}, size_t{10000}, size_t{19999}}) hashes[at] = "ready";

    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<SummaryLookup> got =
        store.get_summaries(hashes, "mode", CapQuery::at(4), Lens{});
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("get_summaries on 20,000 hashes: " << ms << " ms");

    REQUIRE(got.size() == hashes.size());
    int ready = 0;
    for (size_t i = 0; i < got.size(); ++i) {
        if (hashes[i] == "ready") {
            CHECK(got[i].status == RecordStatus::Ready);
            CHECK(got[i].summary.stars.has_value());
            ++ready;
        } else if (got[i].status != RecordStatus::NotAnalyzed) {
            FAIL("fake hash " << hashes[i] << " read as analyzed");
        }
    }
    CHECK(ready == 3);
}

TEST_CASE("a database from before the stars column gets its stars filled on open") {
    const std::string path = temp_db("stars_fill");
    std::remove(path.c_str());
    {
        RecordStore seed(path);
        for (const char* h : {"ready", "stale"})
            seed.add_song(h, h, "Artist", "Charter", fixture().song);
        seed.add_record(RecordKey{"ready", "mode", CapQuery::at(4)}, at_cap(4));
        PreparedRow old = prepare_row(RecordKey{"stale", "mode", CapQuery::at(4)}, at_cap(4));
        old.hyversion = "0.0.0";
        seed.add_row(old);
    }
    // What the previous Hydra wrote: the same table with no stars column.
    exec_on_file(path, "ALTER TABLE results DROP COLUMN stars");

    const int expected = path_stars(fixture().record.best_path());
    {
        RecordStore store(path);
        const std::vector<SummaryLookup> got =
            store.get_summaries({"ready", "stale"}, "mode", CapQuery::at(4), Lens{});
        CHECK(got[0].summary.stars == expected);
        CHECK(got[1].status == RecordStatus::Stale);
    }
    CHECK(scalar(path, "SELECT stars FROM results WHERE hyhash='ready'") == expected);
    // The Stale row is left exactly as it was: no stars, and its other
    // summaries kept rather than blanked.
    CHECK(scalar(path, "SELECT COUNT(*) FROM results WHERE hyhash='stale'"
                       " AND stars IS NULL AND score IS NOT NULL") == 1);
    // A second open finds nothing left to fill and changes nothing.
    { RecordStore again(path); }
    CHECK(scalar(path, "SELECT stars FROM results WHERE hyhash='ready'") == expected);
    std::remove(path.c_str());
}

TEST_CASE("under rules that make every row Stale, the stars fill changes nothing") {
    // A bad hydra_rules.ini opens the store under RulesStamp::none(), where
    // every row reads Stale. The fill must not blank or skip-mark anything:
    // once the rules are fixed, the next open fills the row.
    const std::string path = temp_db("stars_badrules");
    std::remove(path.c_str());
    {
        RecordStore seed(path);
        seed.add_song("h", "h", "Artist", "Charter", fixture().song);
        seed.add_record(RecordKey{"h", "mode", CapQuery::at(4)}, at_cap(4));
    }
    exec_on_file(path, "ALTER TABLE results DROP COLUMN stars");
    const int64_t score = fixture().record.best_path().totalscore();

    { RecordStore bad(path, core::RulesStamp::none()); }
    CHECK(scalar(path, "SELECT COUNT(*) FROM results WHERE stars IS NULL") == 1);
    CHECK(scalar(path, "SELECT score FROM results WHERE hyhash='h'") == score);

    { RecordStore good(path); }
    CHECK(scalar(path, "SELECT stars FROM results WHERE hyhash='h'") ==
          path_stars(fixture().record.best_path()));
    CHECK(scalar(path, "SELECT score FROM results WHERE hyhash='h'") == score);
    std::remove(path.c_str());
}
```

- [ ] **Step 6: Build and watch them fail.**

Build `hydra_tests` as in Step 2. Expected: the build fails with `'stars': is not a member of 'hydra::store::PathSummary'` and `'summary': is not a member of 'hydra::store::SummaryLookup'`.

- [ ] **Step 7: Add the fields to `record_store.h`.**

In `PathSummary`:

Before:
```cpp
    std::optional<int> sqout_count;
    std::optional<int> pathcount;
};
```
After:
```cpp
    std::optional<int> sqout_count;
    std::optional<int> pathcount;
    // The best path's star count by core/stars' path_stars (solo bonus left
    // out, as Clone Hero counts it). Unset on a row written before the column
    // existed until the store fills it (fill_missing_stars).
    std::optional<int> stars;
};
```

In `SummaryLookup`:

Before:
```cpp
struct SummaryLookup {
    RecordStatus status = RecordStatus::NotAnalyzed;
    std::string bestpath;  // meaningful only when status == Ready
};
```
After:
```cpp
struct SummaryLookup {
    RecordStatus status = RecordStatus::NotAnalyzed;
    std::string bestpath;  // meaningful only when status == Ready
    // The row's summary columns; filled only when status == Ready. A Stale
    // row's numbers came from bytes this build doesn't trust, so they're not
    // handed out.
    PathSummary summary;
};
```

Replace the comment on `get_summaries`:

Before:
```cpp
    // get_summary for many charts at once, in one query: one answer per
    // entry of `hyhashes`, in the same order (a repeated hash gets the same
    // answer twice). What a library page asks for.
```
After:
```cpp
    // get_summary for many charts at once: one answer per entry of
    // `hyhashes`, in the same order (a repeated hash gets the same answer
    // twice). The library asks it about every chart, so the hashes are sent
    // in chunks under SQLite's bound-value limit.
```

In the private section, after `void create_result_tables();`:

```cpp
    void create_result_tables();
    // Fills the stars column of every Ready row that lacks it (rows written
    // before the column existed). Runs on every open; with nothing to fill
    // it reads only small columns. Returns rows filled.
    int fill_missing_stars();
```

- [ ] **Step 8: Store, read and fill the column in `record_store.cpp`.**

Add the include after `#include "store/serialize.h"`:

```cpp
#include "core/stars.h"
#include "store/serialize.h"
```

`bind_summary` gets a tenth column. After its `pathcount` lines:

Before:
```cpp
    if (sum.pathcount) sqlite3_bind_int(s, first_idx + 8, *sum.pathcount);
    else sqlite3_bind_null(s, first_idx + 8);
}
```
After:
```cpp
    if (sum.pathcount) sqlite3_bind_int(s, first_idx + 8, *sum.pathcount);
    else sqlite3_bind_null(s, first_idx + 8);
    if (sum.stars) sqlite3_bind_int(s, first_idx + 9, *sum.stars);
    else sqlite3_bind_null(s, first_idx + 9);
}
```

`read_summary`:

Before:
```cpp
    if (auto v = column_opt_i64(s, first_idx + 8)) sum.pathcount = static_cast<int>(*v);
    return sum;
```
After:
```cpp
    if (auto v = column_opt_i64(s, first_idx + 8)) sum.pathcount = static_cast<int>(*v);
    if (auto v = column_opt_i64(s, first_idx + 9)) sum.stars = static_cast<int>(*v);
    return sum;
```

The table definition and the column list:

Before:
```cpp
    "  pathcount   INTEGER,"
    "  UNIQUE (hyhash, chartmode, sp_cap, ms_enabled, ms_value, depth_mode, depth_value)";

// The summary columns, in the order bind_summary/read_summary use.
constexpr const char* kSummaryColumnList =
    "score, actcount, maxskip, hardest_ms, avgmult, notecount, sqin_count, "
    "sqout_count, pathcount";
```
After:
```cpp
    "  pathcount   INTEGER,"
    "  stars       INTEGER,"
    "  UNIQUE (hyhash, chartmode, sp_cap, ms_enabled, ms_value, depth_mode, depth_value)";

// The summary columns, in the order bind_summary/read_summary use.
constexpr const char* kSummaryColumnList =
    "score, actcount, maxskip, hardest_ms, avgmult, notecount, sqin_count, "
    "sqout_count, pathcount, stars";

// SQLite refuses a statement with more than 32,766 bound values (the vendored
// 3.46's SQLITE_MAX_VARIABLE_NUMBER). get_summaries sends a whole library's
// hashes, so it sends them this many at a time.
constexpr size_t kHashesPerQuery = 10000;
```

`summarize_path` sets the count, from the one rule in `core/stars`:

Before:
```cpp
    s.sqin_count = sqin;
    s.sqout_count = sqout;
    return s;
}
```
After:
```cpp
    s.sqin_count = sqin;
    s.sqout_count = sqout;
    s.stars = path_stars(path);
    return s;
}
```

In the constructor, after `create_result_tables();`. T6 adds its one-time Auto delete here too; the fill goes after it:

Before:
```cpp
    create_result_tables();
    exec("PRAGMA user_version = 2");
}
```
After:
```cpp
    create_result_tables();
    // A results table from before the stars summary has no column for it.
    // Its Ready rows are filled now, from their stored paths; nothing is
    // analyzed again. A row that isn't Ready (another build, other rules) is
    // left alone and filled on a later open once it reads Ready.
    if (!has_column("results", "stars")) exec("ALTER TABLE results ADD COLUMN stars INTEGER");
    fill_missing_stars();
    exec("PRAGMA user_version = 2");
}
```

`write_row`'s insert gains one placeholder, because `kSummaryColumnList` now names ten columns:

Before:
```cpp
             kSummaryColumnList + ") VALUES (?,?,?,?,?,?,?,?,?,?, ?,?,?,?,?,?,?,?,?)")
```
After:
```cpp
             kSummaryColumnList + ") VALUES (?,?,?,?,?,?,?,?,?,?, ?,?,?,?,?,?,?,?,?,?)")
```

Replace the whole body of `get_summaries`. It keeps the one winner rule (`WinnerPicker`), asks about each distinct hash once, and sends the hashes in chunks. The lock is released before the answers are assembled, since that part touches no SQLite state.

```cpp
std::vector<SummaryLookup> RecordStore::get_summaries(const std::vector<std::string>& hyhashes,
                                                      const std::string& chartmode,
                                                      const CapQuery& cap, const Lens& lens) {
    std::vector<SummaryLookup> out(hyhashes.size());
    if (hyhashes.empty()) return out;

    // Each chart is asked about once, however many folders list it. A chart's
    // rows all come back in its own chunk, so one picker sees every
    // candidate a chart has.
    std::vector<std::string> distinct(hyhashes);
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());

    struct Offered {
        std::string hyhash;
        std::string bestpath;
        PathSummary summary;
    };
    WinnerPicker picker;
    std::vector<Offered> offered;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (size_t first = 0; first < distinct.size(); first += kHashesPerQuery) {
            const size_t n = std::min(kHashesPerQuery, distinct.size() - first);
            std::string sql =
                std::string("SELECT hyhash, hyversion, bestpath, result_id, substr(structure,1,12), ") +
                kSummaryColumnList + " FROM results WHERE chartmode=? AND hyhash IN (" +
                placeholders(n) + ")";
            append_candidate_filter(sql, "", cap);
            Stmt s = prepare(db_, sql.c_str());
            int idx = 1;
            bind_text(s, idx++, chartmode);
            for (size_t i = 0; i < n; ++i) bind_text(s, idx++, distinct[first + i]);
            bind_candidate_filter(s, idx, cap, lens);
            while (sqlite3_step(s) == SQLITE_ROW) {
                std::string hyhash = column_text(s, 0);
                picker.offer(hyhash, chartmode,
                             rank_row(column_text(s, 1), column_blob(s, 4),
                                      sqlite3_column_int64(s, 3), rules_fingerprint_));
                offered.push_back({std::move(hyhash), column_text(s, 2), read_summary(s, 5)});
            }
        }
    }

    // One answer per chart, then handed to every position that asked for it.
    // A stale winner is reported as Stale, not hidden: the library has to
    // tell "analyzed by another build" apart from "never analyzed".
    const std::vector<bool> won = picker.winners();
    std::unordered_map<std::string, SummaryLookup> by_hash;
    for (size_t i = 0; i < offered.size(); ++i) {
        if (!won[i]) continue;
        SummaryLookup& answer = by_hash[offered[i].hyhash];
        if (picker.rank(i).ready()) {
            answer.status = RecordStatus::Ready;
            answer.bestpath = std::move(offered[i].bestpath);
            answer.summary = offered[i].summary;
        } else {
            answer.status = RecordStatus::Stale;
        }
    }
    for (size_t i = 0; i < hyhashes.size(); ++i) {
        auto it = by_hash.find(hyhashes[i]);
        if (it != by_hash.end()) out[i] = it->second;
    }
    return out;
}
```

Add `fill_missing_stars` right after `reindex`:

```cpp
int RecordStore::fill_missing_stars() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Rows with a score but no stars: written before the column existed. Only
    // Ready rows are filled. The rest are skipped, never blanked -- reindex
    // blanks a Stale row's summaries, and under a bad hydra_rules.ini every
    // row reads Stale, so reusing it here would wipe the whole library's
    // scores on one bad start.
    std::vector<int64_t> ids;
    {
        Stmt s = prepare(db_, "SELECT result_id, hyversion, substr(structure,1,12) FROM results"
                              " WHERE stars IS NULL AND score IS NOT NULL ORDER BY result_id");
        while (sqlite3_step(s) == SQLITE_ROW) {
            const int64_t id = sqlite3_column_int64(s, 0);
            if (rank_row(column_text(s, 1), column_blob(s, 2), id, rules_fingerprint_).ready())
                ids.push_back(id);
        }
    }
    if (ids.empty()) return 0;

    // One transaction, each statement compiled once, as in reindex. Only the
    // stars column is written: every other summary stays byte for byte.
    exec("BEGIN");
    try {
        Stmt structure_stmt = prepare(db_, "SELECT structure FROM results WHERE result_id=?");
        Stmt nodes_stmt = prepare(db_, kLoadNodesSql);
        Stmt update = prepare(db_, "UPDATE results SET stars=? WHERE result_id=?");
        int filled = 0;
        for (int64_t id : ids) {
            std::vector<uint8_t> structure;
            {
                ResetOnExit reset{structure_stmt};
                sqlite3_bind_int64(structure_stmt, 1, id);
                if (sqlite3_step(structure_stmt) != SQLITE_ROW) continue;
                structure = column_blob(structure_stmt, 0);
            }
            const std::unordered_map<std::string, std::vector<uint8_t>> nodes =
                load_nodes(nodes_stmt, id);
            const PathSummary summary = summarize_record(rebuild_record(
                structure, [&nodes](const std::string& hash) -> const std::vector<uint8_t>* {
                    auto it = nodes.find(hash);
                    return it == nodes.end() ? nullptr : &it->second;
                }));
            ResetOnExit reset{update};
            if (summary.stars) sqlite3_bind_int(update, 1, *summary.stars);
            else sqlite3_bind_null(update, 1);
            sqlite3_bind_int64(update, 2, id);
            if (sqlite3_step(update) != SQLITE_DONE)
                throw std::runtime_error(std::string("fill_missing_stars failed: ") +
                                         sqlite3_errmsg(db_));
            ++filled;
        }
        exec("COMMIT");
        return filled;
    } catch (...) {
        rollback_if_open(db_);
        throw;
    }
}
```

`reindex`'s update writes the new column too, and the result id moves to the eleventh parameter:

Before:
```cpp
        Stmt update = prepare(db_,
            "UPDATE results SET score=?,actcount=?,maxskip=?,hardest_ms=?,avgmult=?,"
            "notecount=?,sqin_count=?,sqout_count=?,pathcount=? WHERE result_id=?");
```
After:
```cpp
        Stmt update = prepare(db_,
            "UPDATE results SET score=?,actcount=?,maxskip=?,hardest_ms=?,avgmult=?,"
            "notecount=?,sqin_count=?,sqout_count=?,pathcount=?,stars=? WHERE result_id=?");
```

Before:
```cpp
            bind_summary(update, 1, summary);
            sqlite3_bind_int64(update, 10, row.result_id);
```
After:
```cpp
            bind_summary(update, 1, summary);
            sqlite3_bind_int64(update, 11, row.result_id);
```

`list_records` selects the summary columns by hand, so `r.stars` joins them and every later column moves up by one:

Before:
```cpp
        "r.score, r.actcount, r.maxskip, r.hardest_ms, r.avgmult, r.notecount, "
        "r.sqin_count, r.sqout_count, r.pathcount, r.sp_cap, "
        "r.hyversion, r.result_id, substr(r.structure,1,12) "
```
After:
```cpp
        "r.score, r.actcount, r.maxskip, r.hardest_ms, r.avgmult, r.notecount, "
        "r.sqin_count, r.sqout_count, r.pathcount, r.stars, r.sp_cap, "
        "r.hyversion, r.result_id, substr(r.structure,1,12) "
```

Before:
```cpp
        listing.summary = read_summary(s, 6);
        listing.sp_cap = sqlite3_column_int(s, 15);
        picker.offer(listing.hyhash, listing.chartmode,
                     rank_row(column_text(s, 16), column_blob(s, 18),
                              sqlite3_column_int64(s, 17), rules_fingerprint_));
```
After:
```cpp
        listing.summary = read_summary(s, 6);
        listing.sp_cap = sqlite3_column_int(s, 16);
        picker.offer(listing.hyhash, listing.chartmode,
                     rank_row(column_text(s, 17), column_blob(s, 19),
                              sqlite3_column_int64(s, 18), rules_fingerprint_));
```

No results-version or path-format stamp changes: the blob format is untouched, and an old row is only missing a column the store fills itself.

- [ ] **Step 8a: Keep each song's length (added by the main session).**

The Paths tab's timeline (Task 10) draws every activation along the song, so it needs the song's length. Nothing stored holds it today: `songmeta` keeps only the tempo map, and the Paths tab never parses the chart. This step stores the length where the tempo map lives, as the last note's onset in milliseconds. That is the same value the Preview uses for its scrubber (`preview_view.cpp` sets `song_length_ms` from the last note). Songs saved before this update read "no length" until they are analyzed again, and Task 10 hides the timeline for them.

First the test. Append it to `tests/test_store.cpp`:

```cpp
TEST_CASE("a stored song keeps its length, and an old songmeta row reads none") {
    const Song& song = fixture().song;
    REQUIRE_FALSE(song.sequence.empty());
    const double expected = song.sequence.back().timecode.ms();

    RecordStore store(":memory:");
    store.add_song("h", "Song", "Artist", "Charter", song);
    store.add_record(RecordKey{"h", "mode", CapQuery::at(4)}, at_cap(4));
    const RecordLookup got = store.get_record(RecordKey{"h", "mode", CapQuery::at(4)});
    REQUIRE(got.status == RecordStatus::Ready);
    REQUIRE(got.song_length_ms.has_value());
    CHECK(*got.song_length_ms == doctest::Approx(expected));

    // A row written before the column existed reads no length.
    store.exec_for_tests("UPDATE songmeta SET length_ms = NULL WHERE hyhash = 'h'");
    CHECK_FALSE(
        store.get_record(RecordKey{"h", "mode", CapQuery::at(4)}).song_length_ms.has_value());
}
```

If `RecordStore` has no test hook for raw SQL, don't add `exec_for_tests`. Instead, open a file database, write the row, null the column with `sqlite3_exec` on a second connection, and reopen. That is the pattern the existing "before the stars column" case in Step 5 uses; copy it.

In `record_store.h`, add the field to `RecordLookup`, under `timing`:

```cpp
    std::optional<SongTiming> timing;   // set when Ready and the song is registered
    // The last note's onset, in ms. Empty when the song was saved before
    // Hydra stored lengths; it fills on the chart's next analysis.
    std::optional<double> song_length_ms;
```

In `record_store.cpp`, four edits. First, the column, next to the other one-time `ALTER TABLE` lines in the constructor:

```cpp
    // A songmeta table from before stored lengths. Old rows read NULL until
    // their chart is analyzed again.
    if (!has_column("songmeta", "length_ms")) exec("ALTER TABLE songmeta ADD COLUMN length_ms REAL");
```

Second, `upsert_song` takes the length and writes it on insert and on update, because a new analysis may be of another difficulty with a different last note:

```cpp
void RecordStore::upsert_song(const std::string& hyhash, const std::string& ref_name,
                              const std::string& ref_artist, const std::string& ref_charter,
                              const std::vector<uint8_t>& tempomap,
                              std::optional<double> length_ms) {
    Stmt s = prepare(db_,
        "INSERT INTO songmeta (hyhash, ref_name, ref_artist, ref_charter, tempomap, length_ms) "
        "VALUES (?,?,?,?,?,?) "
        "ON CONFLICT(hyhash) DO UPDATE SET ref_name = excluded.ref_name, "
        "ref_artist = excluded.ref_artist, ref_charter = excluded.ref_charter, "
        "length_ms = COALESCE(excluded.length_ms, songmeta.length_ms)");
    bind_text(s, 1, hyhash);
    bind_text(s, 2, ref_name);
    bind_text(s, 3, ref_artist);
    bind_text(s, 4, ref_charter);
    bind_blob(s, 5, tempomap);
    if (length_ms) sqlite3_bind_double(s, 6, *length_ms);
    else sqlite3_bind_null(s, 6);
    if (sqlite3_step(s) != SQLITE_DONE)
        throw std::runtime_error(std::string("add_song failed: ") + sqlite3_errmsg(db_));
}
```

Keep the existing comment above the statement. Update the declaration in `record_store.h` to match. Third, both callers (`add_song` and `save_analysis`) work the length out before the lock, next to `encode_tempomap`, and pass it:

```cpp
    const std::optional<double> length_ms =
        song.sequence.empty() ? std::nullopt
                              : std::optional<double>(song.sequence.back().timecode.ms());
```

Fourth, `get_record` reads it with the tempo map. Change `read_tempomap`'s query to `SELECT tempomap, length_ms FROM songmeta WHERE hyhash=?`, and have it return both. A small struct next to it is enough:

```cpp
struct SongMetaRead {
    std::vector<uint8_t> tempomap;
    std::optional<double> length_ms;
};
```

`get_timing` keeps its signature and uses only `.tempomap`. In `get_record`, after the timing is decoded, set `out.song_length_ms` from the read. Build and run the new case: `.\build-cpp\Release\hydra_tests.exe -tc="*keeps its length*"` → `Status: SUCCESS!`.

- [ ] **Step 9: Build and run the whole unit suite.**

Build `hydra_tests` as in Step 2, then run `.\build-cpp\Release\hydra_tests.exe`. Expected: `[doctest] Status: SUCCESS!`, with seven more test cases than the Task 0 baseline. The `get_summaries answers a whole library in chunks` case prints its time; note it for the main session.

- [ ] **Step 10: Run the score-neutral proof.**

Build `hydra_batch` (`-Target hydra_batch`), then run the proof from the Global Constraints with `<n>` = 7. Expected: `Compare-Object` prints nothing.

- [ ] **Step 11: Time the one-time fill on a real library.**

The fill decodes every Ready result once, on the first start after the update, on the thread that opens the store. Measure it on a copy of the installed database; the original is never opened. `--reindex` analyzes nothing: it opens the store (which runs the fill) and then rewrites the summary columns.

```powershell
$copy = "$env:TEMP\hydra_ui_T7\installed_copy.db"
New-Item -ItemType Directory -Force (Split-Path $copy) | Out-Null
Copy-Item "C:\Program Files\Hydra\hydra.db" $copy -Force
Copy-Item "C:\Program Files\Hydra\hydra.db-wal" "$copy-wal" -Force -ErrorAction SilentlyContinue
$first  = Measure-Command { .\build-cpp\Release\hydra_batch.exe --reindex --db $copy }
$second = Measure-Command { .\build-cpp\Release\hydra_batch.exe --reindex --db $copy }
"first open (fill + reindex): {0:N1} s; second (reindex only): {1:N1} s" -f $first.TotalSeconds, $second.TotalSeconds
Remove-Item "$env:TEMP\hydra_ui_T7\installed_copy.db*" -Force
```

Expected: the first run is at most 10 seconds longer than the second. If it is longer, stop here and report both numbers to the main session instead of committing; a start-up that long needs a progress message or a background fill, which is a design call.

- [ ] **Step 12: Commit.**

```
git add src/core/stars.h src/core/stars.cpp src/store/record_store.h src/store/record_store.cpp tests/test_stars.cpp tests/test_store.cpp
git commit -m "Store: each result keeps its best path's star count; summaries for the whole library

Task: T7 library summaries in the store
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Path and Preview view data

Today the Paths tab and the Preview each build their own display text, and they disagree. The Paths tab writes a measure as `m32.1.0`, the Preview's time box writes `[27:2:450]`. Squeezes show as raw engine lines such as "SqOut: Note timing must be later than 163.0ms." The new tabs (Task 10 and Task 11) need more than today's views carry: one line per activation with a badge and a place on a timeline, plain squeeze sentences, a flat path list with the mockup's group names, the Preview's path choices, its scrubber marks, the next activation, and the SP meter's number. This task adds all of that as view data in the app layer, beside the old fields, with unit tests. Nothing on screen changes yet. The old tabs keep compiling against the old fields until Task 10 and Task 11 switch over and delete them.

**Wave:** 2. **Depends on:** Task 1 (the wave-2 base). **Expected overlaps:** Task 6 edits the footer's Auto line in `src/app/path_view.cpp` ("SP meter: N bars. The search ran out of time..."). Keep this task's version of the file and re-apply Task 6's one-line change to the footer.

**Goal:** Every string and number the new Paths and Preview tabs draw comes from a tested function in `src/app`, and one function, `format_measure`, writes every measure position.

**Files:**
- Modify: `src/app/display_format.h`, `src/app/display_format.cpp` (`format_ms_spaced`)
- Modify: `src/app/path_view.h`, `src/app/path_view.cpp` (`format_measure`, the activation rows, the badge, the squeeze sentences, the path buttons, the fold state, the cache's new inputs)
- Modify: `src/app/preview_view.h`, `src/app/preview_view.cpp` (the activation's measure and chord, the new time-box lines, scrubber marks, activation jumps, the next-activation box, the SP readout, the path label)
- Test: `tests/test_path_view.cpp`, `tests/test_preview_view.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests -tc="format_measure*"` passes: tick 960 on a 192-tick, 4/4 timing prints `m2.2.0`, tick 1000 prints `m2.2.40`.
- [ ] `hydra_tests -tc="activation rows: Burnout*"` passes: Burnout's rows read `3-` / `m32.1.0` / `3 bars` / badge `squeeze out 163 ms`, then `1` / `m58.1.0`, then `2` / `m88.1.0`; the first row's sentence is exactly "Hit the [  Y  ] note more than 163.0 ms late so it lands after Star Power ends. It scores 260 fewer points, and its SP phrase banks for later."; the summary is `3 · 3 bars each · no SP left over`.
- [ ] `hydra_tests -tc="path buttons*"` passes: Burnout lists `378,315 · 3- 1 2` (Optimal, `hardest squeeze 163.0 ms`), `378,175 · 0 4 1`, `378,075 · 2 1 2` (both under `Within 2 scores`), then `375,955 · 0 0 0 0` (`2,360 below optimal`).
- [ ] `hydra_tests -tc="squeeze sentences*,activation badge*,multiplier squeeze*,PathsTabUi*,PathsTabCache*,activation timeline*"` passes; Burnout's multiplier squeeze summary reads `+15`.
- [ ] `hydra_tests -tc="build_time_box*,scrub marks*,activation jumps*,next activation box*,sp meter readout*,preview path label*,build_preview_scene*"` passes, including the new `position`, `length`, `tempo` and `section_line` checks.
- [ ] The whole `hydra_tests` suite ends `Status: SUCCESS!`, and every old test still passes unchanged.
- [ ] The score-neutral proof (Global Constraints) prints nothing.

**Verify:** `.\build-cpp\Release\hydra_tests.exe` → `Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing path-view tests.** In `tests/test_path_view.cpp`, add Burnout to the file's anonymous namespace, right after the closing `}();` / `return result;` / `}` of `analyzed()`:

```cpp
// Burnout (Green Day, charter Hoph2o), analyzed the way the GUI tests
// analyze it: Expert, Pro Drums, 2x Bass, SP cap 4, 2 scores, 10 ms limit.
const AnalysisResult& burnout() {
    static const AnalysisResult result = [] {
        AnalysisSettings settings;
        settings.depth_mode = DepthMode::Scores;
        settings.depth_value = 2;
        settings.ms_filter = 10.0;
        for (const std::string& path : corpus::chart_paths())
            if (path.find("Green Day - Burnout") != std::string::npos)
                return analyze_chart_file(path, settings);
        throw std::runtime_error("Burnout is not in testdata/input");
    }();
    return result;
}

// The separator the new labels use: a middle dot, U+00B7, in UTF-8.
const std::string kDot = " \xC2\xB7 ";
```

Then append these cases at the end of the file:

```cpp
TEST_CASE("format_measure: one form for both tabs") {
    // 192 ticks a beat, 768 a measure: tick 960 is measure 2, beat 2.
    std::map<int64_t, int64_t> tpm{{0, 768}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    SongTiming timing(192, tpm, bpm);
    CHECK(format_measure(timing, 0) == "m1.1.0");
    CHECK(format_measure(timing, 960) == "m2.2.0");
    CHECK(format_measure(timing, 1000) == "m2.2.40");
    CHECK(format_measure(timing.timecode(960)) == "m2.2.0");
    CHECK(format_ms_spaced(163.0) == "163.0 ms");
}

TEST_CASE("activation rows: Burnout's three activations") {
    const AnalysisResult& ar = burnout();
    const HydraRecord& rec = ar.record;
    const Path& best = rec.best_path();
    REQUIRE(best.pathstring() == "3- 1 2");
    REQUIRE(best.totalscore() == 378315);
    const SongTiming& timing = ar.song.timing();

    ActivationsView view = build_activations(best, rec, &timing, kDefaultHitWindowMs);
    REQUIRE(view.acts.size() == 3);
    CHECK(view.summary == "3" + kDot + "3 bars each" + kDot + "no SP left over");

    const ActivationRowView& a1 = view.acts[0];
    CHECK(a1.number == 1);
    CHECK(a1.notation == "3-");
    CHECK(a1.measure == "m32.1.0");
    CHECK(a1.sp_bars == 3);
    CHECK(a1.bars == "3 bars");
    CHECK(a1.badge == "squeeze out 163 ms");
    CHECK(a1.difficult);
    CHECK(a1.chord == "[Kick - GreenCym]");
    REQUIRE(a1.squeeze_sentences.size() == 1);
    CHECK(a1.squeeze_sentences[0].text ==
          "Hit the [  Y  ] note more than 163.0 ms late so it lands after Star Power "
          "ends. It scores 260 fewer points, and its SP phrase banks for later.");
    CHECK(a1.squeeze_sentences[0].warn);
    CHECK(a1.backends_label == "3 notes near the SP end");

    const ActivationRowView& a2 = view.acts[1];
    CHECK(a2.number == 2);
    CHECK(a2.notation == "1");
    CHECK(a2.measure == "m58.1.0");
    CHECK(a2.badge.empty());
    CHECK(a2.squeeze_sentences.empty());
    CHECK(a2.backends_label == "6 notes near the SP end");

    const ActivationRowView& a3 = view.acts[2];
    CHECK(a3.notation == "2");
    CHECK(a3.measure == "m88.1.0");
    CHECK(a3.backends_label == "7 notes near the SP end");

    // No song length given: no timeline.
    CHECK_FALSE(a1.song_fraction.has_value());
    CHECK(view.timeline_end.empty());
}

TEST_CASE("activation timeline: onset over the song's length, and the end measure") {
    // 120 BPM, 4/4, 192 ticks a beat: a measure is 2000 ms.
    std::map<int64_t, int64_t> tpm{{0, 768}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    SongTiming timing(192, tpm, bpm);
    Activation act;
    act.timecode = timing.timecode(768);  // measure 2, 2000 ms
    act.sp_meter = 2;
    act.skips = 0;
    act.e_offset = 300.0;  // not e-critical
    Path p;
    p.activations.push_back(act);
    HydraRecord rec;

    ActivationsView view = build_activations(p, rec, &timing, 85.0, std::nullopt,
                                             core::default_rules(), 10000.0);
    REQUIRE(view.acts.size() == 1);
    REQUIRE(view.acts[0].song_fraction.has_value());
    CHECK(*view.acts[0].song_fraction == doctest::Approx(0.2));
    CHECK(view.timeline_end == "m6");  // 10 s is tick 3840, the start of measure 6
    CHECK(view.summary == "1" + kDot + "2 bars" + kDot + "no SP left over");

    // No timing: no fraction, whatever the length.
    ActivationsView blind = build_activations(p, rec, nullptr, 85.0, std::nullopt,
                                              core::default_rules(), 10000.0);
    CHECK_FALSE(blind.acts[0].song_fraction.has_value());
    CHECK(blind.timeline_end.empty());
}

TEST_CASE("activation badge: shown only when the activation needs a squeeze") {
    Activation none;
    none.skips = 0;
    none.e_offset = 300.0;  // not e-critical
    CHECK(activation_badge(none).empty());

    Activation sqin = none;
    sqin.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 12.4});
    CHECK(activation_badge(sqin) == "squeeze in 12 ms");

    Activation sqout = none;
    sqout.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -163.0});
    CHECK(activation_badge(sqout) == "squeeze out 163 ms");

    // A required (E0) calibration fill is a squeeze too; the hardest one names the badge.
    Activation e0 = none;
    e0.e_offset = -30.0;
    CHECK(activation_badge(e0) == "calibration fill 30 ms");
    e0.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -5.0});
    CHECK(activation_badge(e0) == "calibration fill 30 ms");

    // An optional (E1) fill is not required, so no badge.
    Activation e1 = none;
    e1.skips = 1;
    e1.e_offset = -30.0;
    CHECK(activation_badge(e1).empty());
}

TEST_CASE("squeeze sentences: SqIn, SqOut, and what a squeeze-out costs") {
    HydraRecord rec;
    auto sentence_of = [&rec](const Activation& act) {
        Path p;
        p.activations.push_back(act);
        ActivationsView v = build_activations(p, rec, nullptr, 85.0);
        REQUIRE(v.acts.size() == 1);
        REQUIRE(v.acts[0].squeeze_sentences.size() == 1);
        return v.acts[0].squeeze_sentences[0];
    };
    Activation base;
    base.skips = 0;
    base.e_offset = 300.0;  // not e-critical

    Activation sqin = base;
    sqin.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 50.0});
    TextLine s = sentence_of(sqin);
    CHECK(s.text ==
          "Hit the SP phrase's last note more than 50.0 ms early so it lands before Star "
          "Power ends. The phrase then counts while Star Power runs, which makes Star "
          "Power last longer.");
    CHECK(s.warn);

    Activation easy_in = base;
    easy_in.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, -20.0});
    s = sentence_of(easy_in);
    CHECK(s.text.rfind("Hit the SP phrase's last note no more than 20.0 ms late so it "
                       "lands before Star Power ends.", 0) == 0);
    CHECK_FALSE(s.warn);

    // A SqOut with no stored squeezed-out row names no chord and no cost.
    Activation bare_out = base;
    bare_out.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 60.0});
    CHECK(sentence_of(bare_out).text ==
          "Hit the SP phrase's last note no more than 60.0 ms early so it lands after "
          "Star Power ends. Its SP phrase banks for later.");

    // The Round and Round fixture: an R+Y phrase chord squeezed out 480 ms past
    // the SP end. The engine never counted it, so it costs nothing.
    Activation far = base;
    BackendSqueeze row;
    row.timecode = Timecode::raw(3256);
    row.chord.add_note(NoteColor::Red);
    row.chord.add_note(NoteColor::Yellow);
    row.points = 460;
    row.sqout_points = 260;
    row.is_sp = true;
    row.offset_ms = 479.999;
    far.backends.push_back(row);
    far.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 479.999});
    far.sqout_tick = 3256;
    s = sentence_of(far);
    CHECK(s.text ==
          "Hit the [ RY  ] note no more than 480.0 ms early so it lands after Star Power "
          "ends. It costs no points, because Hydra's score never counted that note under "
          "Star Power, and its SP phrase banks for later.");
    CHECK_FALSE(s.warn);

    // The same chord 5 ms inside SP really costs 460 - 260 = 200.
    Activation near = far;
    near.backends[0].offset_ms = -5.0;
    near.sqinouts[0].offset_ms = -5.0;
    s = sentence_of(near);
    CHECK(s.text ==
          "Hit the [ RY  ] note more than 5.0 ms late so it lands after Star Power ends. "
          "It scores 200 fewer points, and its SP phrase banks for later.");
    CHECK(s.warn);
}

TEST_CASE("path buttons: Burnout's list, in the mockup's groups") {
    const HydraRecord& rec = burnout().record;
    PathButtonsView v = build_path_buttons(rec, /*depth_mode=*/0, /*depth_value=*/2);
    CHECK(v.within_label == "Within 2 scores");
    REQUIRE(v.buttons.size() == 4);

    CHECK(v.buttons[0].path == &rec.best_path());
    CHECK(v.buttons[0].group == PathButtonView::Group::Optimal);
    CHECK(v.buttons[0].notation == "3- 1 2");
    CHECK(v.buttons[0].title == "378,315" + kDot + "3- 1 2");
    CHECK(v.buttons[0].detail == "hardest squeeze 163.0 ms");
    CHECK(v.buttons[0].detail_warn);

    CHECK(v.buttons[1].group == PathButtonView::Group::Within);
    CHECK(v.buttons[1].title == "378,175" + kDot + "0 4 1");
    CHECK(v.buttons[1].detail.empty());
    CHECK(v.buttons[2].group == PathButtonView::Group::Within);
    CHECK(v.buttons[2].title == "378,075" + kDot + "2 1 2");

    CHECK(v.buttons[3].group == PathButtonView::Group::AllZero);
    CHECK(v.buttons[3].title == "375,955" + kDot + "0 0 0 0");
    CHECK(v.buttons[3].detail == "2,360 below optimal");
    CHECK_FALSE(v.buttons[3].detail_warn);

    CHECK(within_label(0, 1) == "Within 1 score");
    CHECK(within_label(1, 5000) == "Within 5,000 points");
    CHECK(within_label(1, 1) == "Within 1 point");
}

TEST_CASE("multiplier squeeze: Burnout's one squeeze and the fold's summary") {
    std::vector<MultSqueezeView> v = build_multsqueezes(burnout().record);
    REQUIRE(v.size() == 1);
    CHECK(v[0].label == "2x   (+15 pts):   [Red - YellowCym]");
    CHECK(v[0].howto == "Hit [Red] first.");
    CHECK(v[0].points == 15);
    CHECK(multsqueeze_summary(v) == "+15");
    CHECK(multsqueeze_summary({}) == "none");
    std::vector<MultSqueezeView> three(3, v[0]);
    CHECK(multsqueeze_summary(three) == "3" + kDot + "+45");
}

TEST_CASE("PathsTabUi: one row open at a time, expand and collapse all") {
    PathsTabUi ui;
    ui.reset(3);
    CHECK(ui.act_open == std::vector<char>{1, 0, 0});
    CHECK(ui.backends_open == std::vector<char>{0, 0, 0});
    CHECK_FALSE(ui.all_open());

    ui.click_row(2);  // opens row 3 alone
    CHECK(ui.act_open == std::vector<char>{0, 0, 1});
    ui.click_row(2);  // closes it again
    CHECK(ui.act_open == std::vector<char>{0, 0, 0});

    ui.set_all(true);
    CHECK(ui.all_open());
    ui.click_row(1);  // an open row closes and leaves the others open
    CHECK(ui.act_open == std::vector<char>{1, 0, 1});
    ui.set_all(false);
    CHECK(ui.act_open == std::vector<char>{0, 0, 0});
    ui.click_row(7);  // past the end: nothing
    CHECK(ui.act_open == std::vector<char>{0, 0, 0});

    PathsTabUi empty;
    empty.reset(0);
    CHECK_FALSE(empty.all_open());
}

TEST_CASE("PathsTabCache: folds reset for a new path, not for a display setting") {
    const AnalysisResult& ar = burnout();
    const HydraRecord& rec = ar.record;
    const SongTiming& timing = ar.song.timing();
    const Path& best = rec.best_path();
    PathsTabCache cache;

    cache.details(best, rec, 1, &timing, 70.0, std::nullopt, core::default_rules());
    REQUIRE(cache.ui().act_open.size() == 3);
    CHECK(cache.ui().act_open[0] == 1);
    cache.ui().set_all(true);

    // A display setting rebuilds the rows but keeps what is unfolded.
    cache.details(best, rec, 1, &timing, 70.0, 30.0, core::default_rules());
    CHECK(cache.ui().all_open());
    cache.details(best, rec, 1, &timing, 70.0, 30.0, core::default_rules(), 126000.0);
    CHECK(cache.ui().all_open());
    CHECK(cache.details_builds() == 3);

    // Another path starts fresh: first row open, the rest folded.
    const Path& other = *rec.all_paths()[1];
    cache.details(other, rec, 1, &timing, 70.0, 30.0, core::default_rules(), 126000.0);
    CHECK(cache.ui().act_open.size() == other.walk_activations().size());
    CHECK(cache.ui().act_open[0] == 1);
    CHECK_FALSE(cache.ui().all_open());

    // The path buttons: once per record generation and score range.
    for (int frame = 0; frame < 5; ++frame) cache.buttons(rec, 1, 0, 2);
    CHECK(cache.buttons_builds() == 1);
    cache.buttons(rec, 1, 0, 3);
    CHECK(cache.buttons_builds() == 2);
    cache.buttons(rec, 2, 0, 3);
    CHECK(cache.buttons_builds() == 3);
}
```

- [ ] **Step 2: Run them and watch them fail.** Build with `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1` from the Bash tool. Expected: the build fails in `tests/test_path_view.cpp` with errors naming `format_measure`, `format_ms_spaced`, `ActivationRowView`, `activation_badge`, `build_path_buttons`, `PathsTabUi` and `within_label` as undeclared.

- [ ] **Step 3: Add `format_ms_spaced`.** In `src/app/display_format.h`, after the `format_ms` declaration:

```cpp
// A timing in ms for a sentence or a label: one decimal, a space, the unit
// ("163.0 ms"). The new Paths and Preview text uses this; format_ms keeps the
// older "163.0ms" form the report and the backend table print.
std::string format_ms_spaced(double ms);
```

In `src/app/display_format.cpp`, after `format_ms`:

```cpp
std::string format_ms_spaced(double ms) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f ms", ms);
    return buf;
}
```

- [ ] **Step 4: Extend `src/app/path_view.h`.** Add `#include <cstdint>` to the includes. Then make these edits.

After the `TextLine` struct, add the measure formatter:

```cpp
// ---- one measure format ------------------------------------------------------

// A chart position as both tabs print it: "m<measure>.<beat>.<tick-in-beat>",
// measure and beat 1-based, the tick counted from the beat line ("m32.1.0").
// The only measure formatter the Paths and Preview tabs use.
std::string format_measure(const SongTiming& timing, int64_t tick);
// The same for a Timecode that is already resolved, such as an activation's.
std::string format_measure(const Timecode& tc);
```

Replace the `ActivationDetailsView` and `ActivationsView` structs:

```cpp
struct ActivationDetailsView {
    std::string header;  // "%-6s(%d SP)\t%9s" (+ "\t" and format_ms right-aligned in 9 when difficulty-rated)
    bool difficult = false;
    std::string calibration;    // "Calibration fill: " + format_ms(positive = early); empty when not E-critical
    std::string frontend;       // "Frontend: ..."
    std::string scale_warning;  // the transfer-scale prose; empty when immaterial
    std::string overfill_warning;  // cap-clamped anchor prose; empty when not clamped
    std::vector<TextLine> sqinouts;
    std::vector<BackendRowView> backends;
};

struct ActivationsView {
    std::vector<ActivationDetailsView> acts;
    std::vector<TextLine> footer;  // leftover SP, SP meter, skipped notes
};
```

with:

```cpp
// One activation as the Paths tab lists it: a one-line row, and what the row
// shows when it is opened.
struct ActivationRowView {
    // The row.
    int number = 0;              // 1-based; the row's ##act<number> id
    std::string notation;        // Activation::notationstr(), e.g. "3-"
    std::string measure;         // format_measure of the activation, e.g. "m32.1.0"
    int sp_bars = 0;             // bars of SP the activation spends
    std::string bars;            // "3 bars" / "1 bar"
    std::string badge;           // activation_badge(); empty = no badge
    bool difficult = false;      // Activation::is_difficult(): badge and sentences warn-coloured
    // Where the activation falls in the song, 0..1: its onset over the song's
    // length. Unset when the caller passed no timing or no length.
    std::optional<double> song_fraction;

    // The opened row.
    std::string chord;           // Chord::rowstr(), e.g. "[Kick - GreenCym]"
    std::string calibration;    // "Calibration fill: " + format_ms(positive = early); empty when not E-critical
    std::vector<TextLine> squeeze_sentences;  // one per SqIn/SqOut, see squeeze_sentences()
    std::string scale_warning;  // the transfer-scale prose; empty when immaterial
    std::string overfill_warning;  // cap-clamped anchor prose; empty when not clamped
    std::string backends_label;  // "3 notes near the SP end": the rows below, after the backend limit
    std::vector<BackendRowView> backends;

    // The old details modal's fields. Task 10 deletes these three.
    std::string header;  // "%-6s(%d SP)\t%9s" (+ "\t" and format_ms right-aligned in 9 when difficulty-rated)
    std::string frontend;       // "Frontend: ..."
    std::vector<TextLine> sqinouts;
};
// The old name, kept so the old Paths tab compiles until Task 10 replaces it.
using ActivationDetailsView = ActivationRowView;

struct ActivationsView {
    std::vector<ActivationRowView> acts;
    // Beside the "Activations" heading: "3 · 3 bars each · no SP left over".
    // Empty when the path has no activations.
    std::string summary;
    // The timeline's right-hand label, "m96": the measure the song's length
    // falls in. Empty when there is no timeline (no timing or no length).
    std::string timeline_end;
    std::vector<TextLine> footer;  // leftover SP, SP meter, skipped notes. Task 10 deletes it.
};
```

Replace the `build_activations` declaration:

```cpp
ActivationsView build_activations(const Path& path, const HydraRecord& record,
                                  const SongTiming* timing,
                                  double hit_window_ms,
                                  std::optional<double> backend_limit_ms = std::nullopt,
                                  const core::Rules& rules = core::default_rules());
```

with:

```cpp
// `song_length_ms` is the chart's length for the timeline; with it and a
// `timing`, every row gets its song_fraction and the view its timeline_end.
ActivationsView build_activations(const Path& path, const HydraRecord& record,
                                  const SongTiming* timing,
                                  double hit_window_ms,
                                  std::optional<double> backend_limit_ms = std::nullopt,
                                  const core::Rules& rules = core::default_rules(),
                                  std::optional<double> song_length_ms = std::nullopt);

// The badge on an activation row. It shows exactly when the activation needs
// a squeeze, which is when Activation::difficulty() has a value: a SqIn, a
// SqOut, or a required (E0) calibration fill. It names the hardest of them,
// the one difficulty() reports, in whole ms: "squeeze out 163 ms",
// "squeeze in 12 ms", "calibration fill 30 ms". Empty otherwise.
std::string activation_badge(const Activation& act);

// One plain sentence per SqIn/SqOut of `act`, in its order, warn-coloured when
// that squeeze is difficult. `squeezed_out` is the backend row the rating
// flagged as the squeezed-out note (nullptr when the record names none); it
// supplies the SqOut's chord and what the squeeze-out costs.
// `leeway_ms` is Rules::backend_leeway_ms.
std::vector<TextLine> squeeze_sentences(const Activation& act,
                                        const BackendRating* squeezed_out,
                                        double leeway_ms);

// The line above a backend table, explaining its columns in plain words.
extern const char* const kBackendTimingsLead;
```

In `struct MultSqueezeView`, after `std::string howto;` add `int points = 0;  // MultSqueeze::points()`. After `build_multsqueezes`'s declaration add:

```cpp
// Beside the "Multiplier squeeze" fold: "none", "+15" for one squeeze, or
// "3 · +45" (how many, then their total) for several.
std::string multsqueeze_summary(const std::vector<MultSqueezeView>& squeezes);
```

After `build_path_list`'s declaration, add the flat list:

```cpp
// ---- the path list as buttons (the Paths tab and the Preview's list) --------

// One path as a button in the list.
struct PathButtonView {
    enum class Group { Optimal, Within, AllZero };
    const Path* path = nullptr;
    Group group = Group::Optimal;
    std::string notation;   // Path::pathstring(), "3- 1 2"
    std::string title;      // "378,315 · 3- 1 2"
    // The line under the title: "hardest squeeze 163.0 ms" when the path has
    // a difficulty, "2,360 below optimal" on the all-0 path, else empty.
    std::string detail;
    bool detail_warn = false;  // Path::is_difficult()
};

struct PathButtonsView {
    // In drawn order; a button's index is its ##path<i> id. Optimal first
    // (every path tied at the best score), then the rest of the generated
    // list, then the all-0 path when build_path_list shows it.
    std::vector<PathButtonView> buttons;
    std::string within_label;  // the heading over the Within group
};

// The heading over the non-optimal paths: "Within 2 scores", "Within 1 score",
// "Within 5,000 points". depth_mode 0 is scores, 1 is points (Settings).
std::string within_label(int depth_mode, int depth_value);

// Pointers into `record`, like build_path_list's. The viewed record is always
// the one stored under the current Score range (records are keyed by it), so
// the caller passes the current Settings::depth_mode and depth_value.
PathButtonsView build_path_buttons(const HydraRecord& record, int depth_mode, int depth_value);

// What the Paths tab has unfolded, and a pending "Show in Preview". Kept on
// AppState through PathsTabCache::ui(), so it dies with the app state.
struct PathsTabUi {
    std::vector<char> act_open;       // one per activation row; char, not vector<bool>
    std::vector<char> backends_open;  // one per activation row
    bool mult_open = false;
    bool breakdown_open = false;
    // The 0-based activation "Show in Preview" asked for, until the Preview
    // tab has moved its playhead there.
    std::optional<size_t> preview_jump;

    // A fresh path: `rows` rows, the first one open, every backend table folded.
    void reset(size_t rows);
    // Every row open; false when there are no rows.
    bool all_open() const;
    // Expand all / Collapse all.
    void set_all(bool open);
    // A click on row i: an open row closes; a closed row opens and every
    // other row closes. Past the end: nothing.
    void click_row(size_t i);
};
```

In `class PathsTabCache`, replace the `details` declaration:

```cpp
    // The selected path's squeezes, activations and score breakdown.
    const Details& details(const Path& path, const HydraRecord& record, int record_generation,
                           const SongTiming* timing, double hit_window_ms,
                           std::optional<double> backend_limit_ms, const core::Rules& rules);
```

with:

```cpp
    // The selected path's squeezes, activations and score breakdown. A new
    // path or record also resets ui() for it; a display setting does not.
    const Details& details(const Path& path, const HydraRecord& record, int record_generation,
                           const SongTiming* timing, double hit_window_ms,
                           std::optional<double> backend_limit_ms, const core::Rules& rules,
                           std::optional<double> song_length_ms = std::nullopt);
    // The path list as buttons, rebuilt when the record or the score range moves.
    const PathButtonsView& buttons(const HydraRecord& record, int record_generation,
                                   int depth_mode, int depth_value);
    // What the Paths tab has unfolded (see PathsTabUi).
    PathsTabUi& ui() { return ui_; }
    const PathsTabUi& ui() const { return ui_; }
```

After `int details_builds() const { return details_builds_; }` add `int buttons_builds() const { return buttons_builds_; }`. In the private members, after `std::optional<double> details_backend_limit_ms_;` add `std::optional<double> details_song_length_ms_;`, and after `int details_builds_ = 0;` add:

```cpp

    int buttons_generation_ = -1;
    int buttons_depth_mode_ = -1;
    int buttons_depth_value_ = -1;
    PathButtonsView buttons_;
    int buttons_builds_ = 0;

    PathsTabUi ui_;
```

- [ ] **Step 5: Implement them in `src/app/path_view.cpp`.** Add `#include <algorithm>` and `#include <optional>` to the includes. Replace the anonymous namespace at the top:

```cpp
namespace {

std::string measurestr(const Timecode& tc) {
    const int64_t* mbt = tc.measure_beats_ticks();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "m%lld.%lld.%lld", (long long)mbt[0] + 1,
                  (long long)mbt[1] + 1, (long long)mbt[2]);
    return buf;
}

}  // namespace
```

with:

```cpp
namespace {

std::string bars_text(int bars) {
    return std::to_string(bars) + (bars == 1 ? " bar" : " bars");
}

// The separator the new labels use: " · " (U+00B7 in UTF-8).
const char* const kDot = " \xC2\xB7 ";

}  // namespace

std::string format_measure(const Timecode& tc) {
    const int64_t* mbt = tc.measure_beats_ticks();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "m%lld.%lld.%lld", (long long)mbt[0] + 1,
                  (long long)mbt[1] + 1, (long long)mbt[2]);
    return buf;
}

std::string format_measure(const SongTiming& timing, int64_t tick) {
    return format_measure(timing.timecode(tick));
}

std::string activation_badge(const Activation& act) {
    const std::optional<double> hardest = act.difficulty();
    if (!hardest) return {};
    // difficulty() is the max over the SqIns/SqOuts and a required fill, so
    // the squeeze that produced it compares equal; a tie names the squeeze.
    const char* what = "calibration fill";
    for (const SPSqueeze& sq : act.sqinouts)
        if (sq.difficulty() == *hardest) {
            what = sq.kind == SqueezeKind::SqIn ? "squeeze in" : "squeeze out";
            break;
        }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %.0f ms", what, *hardest);
    return buf;
}

std::vector<TextLine> squeeze_sentences(const Activation& act,
                                        const BackendRating* squeezed_out,
                                        double leeway_ms) {
    std::vector<TextLine> out;
    for (const SPSqueeze& sq : act.sqinouts) {
        // timing() is the edge SPSqueeze::description() prints: a SqOut must
        // be hit later than it, a SqIn earlier than it.
        const double t = sq.timing();
        std::string text;
        if (sq.kind == SqueezeKind::SqOut) {
            const std::string note =
                squeezed_out ? "the " + squeezed_out->row.chord.notationstr() + " note"
                             : std::string("the SP phrase's last note");
            const std::string when = t >= 0.0
                                         ? "more than " + format_ms_spaced(std::fabs(t)) + " late"
                                         : "no more than " + format_ms_spaced(std::fabs(t)) + " early";
            text = "Hit " + note + " " + when + " so it lands after Star Power ends.";
            if (squeezed_out) {
                // What the squeeze-out costs, from the same function the
                // search prices the row with (the table's "(-N)").
                const BackendSqueeze& row = squeezed_out->row;
                const double off = row.offset_ms.value_or(0.0);
                if (core::counted_without_squeeze(off, leeway_ms)) {
                    const int value = core::backend_row_value(
                        off, row.points, row.sqout_points, core::SqOutPosition::Exact, leeway_ms);
                    const int lost = row.points - value;
                    text += lost > 0 ? " It scores " + std::to_string(lost) +
                                           " fewer points, and its SP phrase banks for later."
                                     : std::string(" It costs no points, and its SP phrase "
                                                   "banks for later.");
                } else {
                    text += " It costs no points, because Hydra's score never counted that "
                            "note under Star Power, and its SP phrase banks for later.";
                }
            } else {
                text += " Its SP phrase banks for later.";
            }
        } else {
            const std::string when = t <= 0.0
                                         ? "more than " + format_ms_spaced(std::fabs(t)) + " early"
                                         : "no more than " + format_ms_spaced(std::fabs(t)) + " late";
            text = "Hit the SP phrase's last note " + when +
                   " so it lands before Star Power ends. The phrase then counts while Star "
                   "Power runs, which makes Star Power last longer.";
        }
        out.push_back({std::move(text), sq.is_difficult()});
    }
    return out;
}

const char* const kBackendTimingsLead =
    "Timing is how far each note sits from the Star Power end, in ms; negative is "
    "before it. Points is what this path scores for the note under Star Power. A note "
    "marked (uncounted) lands outside Star Power unless it is squeezed in, so this "
    "path's score leaves it out.";
```

In `build_multsqueezes`, after `v.howto = msq.howto();` add `v.points = msq.points();`. After `build_multsqueezes`, add:

```cpp
std::string multsqueeze_summary(const std::vector<MultSqueezeView>& squeezes) {
    if (squeezes.empty()) return "none";
    int total = 0;
    for (const MultSqueezeView& s : squeezes) total += s.points;
    const std::string sum = "+" + group_thousands(total);
    return squeezes.size() == 1 ? sum : std::to_string(squeezes.size()) + kDot + sum;
}
```

In `build_activations`, update the signature to match the header (add `std::optional<double> song_length_ms` after `const core::Rules& rules`). Inside the loop, replace:

```cpp
        std::string ntn = act.notationstr();
        std::string meas = measurestr(act.timecode);
```

with:

```cpp
        std::string ntn = act.notationstr();
        std::string meas = format_measure(act.timecode);
```

Replace:

```cpp
        av.difficult = act.is_difficult();
```

with:

```cpp
        av.difficult = act.is_difficult();
        av.number = static_cast<int>(view.acts.size()) + 1;
        av.notation = ntn;
        av.measure = meas;
        av.sp_bars = act.sp_meter;
        av.bars = bars_text(act.sp_meter);
        av.badge = activation_badge(act);
        av.chord = act.chord.rowstr();
        if (timing && song_length_ms && *song_length_ms > 0.0) {
            const double at = timing->timecode(act.timecode.ticks()).ms();
            av.song_fraction = std::clamp(at / *song_length_ms, 0.0, 1.0);
        }
```

Replace:

```cpp
                av.overfill_warning =
                    "SP overfilled at " +
                    measurestr(timing->timecode(*act.clamp_tick));
```

with:

```cpp
                av.overfill_warning =
                    "SP overfilled at " + format_measure(*timing, *act.clamp_tick);
```

Replace:

```cpp
        for (const SPSqueeze& sq : act.sqinouts)
            av.sqinouts.push_back({sq.description(), sq.is_difficult()});
```

with:

```cpp
        for (const SPSqueeze& sq : act.sqinouts)
            av.sqinouts.push_back({sq.description(), sq.is_difficult()});
        const BackendRating* squeezed_out = nullptr;
        for (const BackendRating& br : rate.backends)
            if (br.squeezed_out) squeezed_out = &br;
        av.squeeze_sentences = squeeze_sentences(act, squeezed_out, rules.backend_leeway_ms);
```

Replace the end of the loop:

```cpp
            av.backends.push_back(std::move(row));
        }

        view.acts.push_back(std::move(av));
    }
```

with:

```cpp
            av.backends.push_back(std::move(row));
        }
        const size_t shown = av.backends.size();
        av.backends_label = std::to_string(shown) + (shown == 1 ? " note" : " notes") +
                            " near the SP end";

        view.acts.push_back(std::move(av));
    }

    // The line beside the heading: how many, how many bars each, what is left.
    if (!view.acts.empty()) {
        int lo = view.acts.front().sp_bars, hi = lo;
        for (const ActivationRowView& a : view.acts) {
            lo = std::min(lo, a.sp_bars);
            hi = std::max(hi, a.sp_bars);
        }
        const std::string bars =
            lo == hi ? bars_text(lo) + (view.acts.size() > 1 ? " each" : "")
                     : std::to_string(lo) + " to " + std::to_string(hi) + " bars";
        const std::string left = path.leftover_sp == 0
                                     ? std::string("no SP left over")
                                     : bars_text(path.leftover_sp) + " of SP left over";
        view.summary = std::to_string(view.acts.size()) + kDot + bars + kDot + left;
    }
    if (timing && song_length_ms && *song_length_ms > 0.0) {
        const int64_t end_tick = std::llround(timing->ms_index().tick_at_ms(*song_length_ms));
        view.timeline_end =
            "m" + std::to_string((long long)timing->timecode(end_tick).measure_beats_ticks()[0] + 1);
    }
```

After `build_path_list`, add:

```cpp
std::string within_label(int depth_mode, int depth_value) {
    const bool points = depth_mode == 1;
    const std::string n = points ? group_thousands(depth_value) : std::to_string(depth_value);
    const char* unit = points ? (depth_value == 1 ? " point" : " points")
                              : (depth_value == 1 ? " score" : " scores");
    return "Within " + n + unit;
}

PathButtonsView build_path_buttons(const HydraRecord& record, int depth_mode, int depth_value) {
    PathButtonsView view;
    view.within_label = within_label(depth_mode, depth_value);
    const PathListView list = build_path_list(record);
    const int64_t best = record.paths.empty() ? 0 : record.best_path().totalscore();

    auto add = [&](const Path* p, PathButtonView::Group group) {
        PathButtonView b;
        b.path = p;
        b.group = group;
        b.notation = p->pathstring();
        b.title = group_thousands(p->totalscore()) + kDot + b.notation;
        if (group == PathButtonView::Group::AllZero) {
            // What the all-0 path costs against the optimal one.
            const int64_t delta = p->totalscore() - best;
            if (delta < 0) b.detail = group_thousands(-delta) + " below optimal";
            if (delta > 0) b.detail = group_thousands(delta) + " above optimal";
        } else if (std::optional<double> hardest = p->difficulty()) {
            b.detail = "hardest squeeze " + format_ms_spaced(*hardest);
            b.detail_warn = p->is_difficult();
        }
        view.buttons.push_back(std::move(b));
    };
    for (size_t g = 0; g < list.groups.size(); ++g)
        for (const Path* p : list.groups[g].paths)
            add(p, g == 0 ? PathButtonView::Group::Optimal : PathButtonView::Group::Within);
    if (list.show_allzero)
        for (const Path* p : list.allzero) add(p, PathButtonView::Group::AllZero);
    return view;
}

void PathsTabUi::reset(size_t rows) {
    act_open.assign(rows, 0);
    backends_open.assign(rows, 0);
    if (rows > 0) act_open[0] = 1;
}

bool PathsTabUi::all_open() const {
    if (act_open.empty()) return false;
    for (char open : act_open)
        if (!open) return false;
    return true;
}

void PathsTabUi::set_all(bool open) {
    std::fill(act_open.begin(), act_open.end(), static_cast<char>(open ? 1 : 0));
}

void PathsTabUi::click_row(size_t i) {
    if (i >= act_open.size()) return;
    if (act_open[i]) {
        act_open[i] = 0;
        return;
    }
    std::fill(act_open.begin(), act_open.end(), static_cast<char>(0));
    act_open[i] = 1;
}
```

Replace `PathsTabCache::details`:

```cpp
const PathsTabCache::Details& PathsTabCache::details(
    const Path& path, const HydraRecord& record, int record_generation,
    const SongTiming* timing, double hit_window_ms, std::optional<double> backend_limit_ms,
    const core::Rules& rules) {
    if (record_generation != details_generation_ || &path != details_path_ ||
        hit_window_ms != details_hit_window_ms_ ||
        backend_limit_ms != details_backend_limit_ms_) {
        details_.squeezes = build_multsqueezes(record);
        details_.activations =
            build_activations(path, record, timing, hit_window_ms, backend_limit_ms, rules);
        details_.breakdown = build_score_breakdown(path);
        details_generation_ = record_generation;
        details_path_ = &path;
        details_hit_window_ms_ = hit_window_ms;
        details_backend_limit_ms_ = backend_limit_ms;
        ++details_builds_;
    }
    return details_;
}
```

with:

```cpp
const PathsTabCache::Details& PathsTabCache::details(
    const Path& path, const HydraRecord& record, int record_generation,
    const SongTiming* timing, double hit_window_ms, std::optional<double> backend_limit_ms,
    const core::Rules& rules, std::optional<double> song_length_ms) {
    const bool new_path = record_generation != details_generation_ || &path != details_path_;
    if (new_path || hit_window_ms != details_hit_window_ms_ ||
        backend_limit_ms != details_backend_limit_ms_ ||
        song_length_ms != details_song_length_ms_) {
        details_.squeezes = build_multsqueezes(record);
        details_.activations = build_activations(path, record, timing, hit_window_ms,
                                                 backend_limit_ms, rules, song_length_ms);
        details_.breakdown = build_score_breakdown(path);
        details_generation_ = record_generation;
        details_path_ = &path;
        details_hit_window_ms_ = hit_window_ms;
        details_backend_limit_ms_ = backend_limit_ms;
        details_song_length_ms_ = song_length_ms;
        // What is unfolded belongs to the path; a display setting keeps it.
        if (new_path) ui_.reset(details_.activations.acts.size());
        ++details_builds_;
    }
    return details_;
}

const PathButtonsView& PathsTabCache::buttons(const HydraRecord& record, int record_generation,
                                              int depth_mode, int depth_value) {
    if (record_generation != buttons_generation_ || depth_mode != buttons_depth_mode_ ||
        depth_value != buttons_depth_value_) {
        buttons_ = build_path_buttons(record, depth_mode, depth_value);
        buttons_generation_ = record_generation;
        buttons_depth_mode_ = depth_mode;
        buttons_depth_value_ = depth_value;
        ++buttons_builds_;
    }
    return buttons_;
}
```

- [ ] **Step 6: Run the path-view tests and watch them pass.** Rebuild `hydra_tests` the same way, then run `.\build-cpp\Release\hydra_tests.exe -tc="format_measure*,activation*,squeeze sentences*,multiplier squeeze*,path buttons*,PathsTabUi*,PathsTabCache*,build_activations*,build_path*,build_multsqueezes*"`. Expected: `Status: SUCCESS!`. If "path buttons" finds more than four buttons, a tie was added to Burnout's list: print the titles with `MESSAGE` and stop; the brief's data (four paths) is what the mockup shows, so a difference is a question for the main session, not a test to loosen.

- [ ] **Step 7: Commit.**

```powershell
git add src/app/display_format.h src/app/display_format.cpp src/app/path_view.h src/app/path_view.cpp tests/test_path_view.cpp
git commit -m "Path view: activation rows, badge, plain squeeze sentences, path buttons, fold state

Task: Task 8 - path and Preview view data
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 8: Write the failing Preview tests.** In `tests/test_preview_view.cpp`, add `#include "app/path_view.h"` after `#include "app/preview_view.h"`. Add the new time-box lines beside the old checks (Task 11 deletes the old ones). In `TEST_CASE("build_time_box: timestamp, measure:beat:tick, BPM")`, after `CHECK(box.section.empty());` add:

```cpp
    CHECK(box.position == "m1.3.288");
    CHECK(box.length == "m3.3.0");
    CHECK(box.tempo == "BPM 120.000 " + kDot + " 4/4");
    CHECK(box.section_line.empty());
```

After `CHECK(build_time_box(scene, 0.0, 5000.0).measure_beat == "[1:1:000] / [3:3:000]");` add `CHECK(build_time_box(scene, 0.0, 5000.0).position == "m1.1.0");`.

In `TEST_CASE("build_time_box: the end bracket runs past the last beat line")`, after `CHECK(box.measure_beat == "[16:1:000] / [16:1:000]");` add `CHECK(box.position == "m16.1.0");` and `CHECK(box.length == "m16.1.0");`.

In `TEST_CASE("build_time_box: the practice section in force")`, after the last `CHECK`, add:

```cpp
    CHECK(build_time_box(scene, 400.0, len).section_line.empty());
    CHECK(build_time_box(scene, 500.0, len).section_line == "Section Verse 1");
    CHECK(build_time_box(scene, 2500.0, len).section_line == "Section Chorus");
```

In `TEST_CASE("build_time_box: a mid-measure meter change follows the engine")`, after `CHECK(box.measure_beat == "[3:1:240] / [3:1:240]");` add `CHECK(box.position == "m3.1.240");`.

In `TEST_CASE("build_time_box: the time signature in force, as the chart wrote it")`, after the last `CHECK`, add:

```cpp
    CHECK(build_time_box(scene, 2000.0, 5000.0).tempo == "BPM 120.000 " + kDot + " 6/8");
    CHECK(build_time_box(scene, 3500.0, 5000.0).tempo == "BPM 120.000 " + kDot + " 3/4");
    CHECK(build_time_box(PreviewScene{}, 0.0, 0.0).tempo == "BPM 0.000 " + kDot + " 4/4");
    CHECK(build_time_box(PreviewScene{}, 0.0, 0.0).position == "m1.1.0");
```

In `TEST_CASE("step_tick_ms: one tick from the tick the time box shows")`, after the two `measure_beat` checks on `fwd` and `back` add `CHECK(build_time_box(scene, fwd, 5000.0).position == "m1.3.289");` and `CHECK(build_time_box(scene, back, 5000.0).position == "m1.3.287");`.

In `TEST_CASE("build_preview_scene: an analyzed chart's overlay matches its path")`, inside the `for` loop after the lane block, add:

```cpp
        CHECK(pa.measure == format_measure(r.song.timing(), pa.tick));
        CHECK(pa.chord == (a.chord.count() > 0 ? a.chord.rowstr() : std::string()));
```

Then append these cases at the end of the file:

```cpp
namespace {

// Two one-bar activations on make_sp_song's timing (120 BPM, 4/4, 480 ticks a
// beat): tick 1920 is 2000 ms, measure 2; tick 7680 is 8000 ms, measure 5.
// One phrase ends at tick 960 (1000 ms) to fill the first bar. The notes run
// to tick 13440 so the second activation's deact node (tick 11520) is inside.
struct TwoActs {
    Song song = make_sp_song({960}, /*last_tick=*/13440);
    Path path;
    PreviewScene scene;
    TwoActs() {
        Activation a = sp_act_at(song, 1920, /*sp_meter=*/1);
        a.chord.add_note(NoteColor::Red);
        Activation b = sp_act_at(song, 7680, /*sp_meter=*/1);
        b.chord.add_note(NoteColor::Red);
        path.activations = {a, b};
        scene = build_preview_scene(song, &path);
    }
};

}  // namespace

TEST_CASE("scrub marks: each activation's onset over the scrubber's length") {
    TwoActs t;
    std::vector<double> marks = build_scrub_marks(t.scene, 10000.0);
    REQUIRE(marks.size() == 2);
    CHECK(marks[0] == doctest::Approx(0.2));
    CHECK(marks[1] == doctest::Approx(0.8));
    // Clamped into the bar when the length is shorter than the path.
    CHECK(build_scrub_marks(t.scene, 4000.0)[1] == doctest::Approx(1.0));
    CHECK(build_scrub_marks(t.scene, 0.0).empty());
    CHECK(build_scrub_marks(build_preview_scene(t.song, nullptr), 10000.0).empty());
}

TEST_CASE("activation jumps: nearest activation before or after the playhead") {
    TwoActs t;
    CHECK(activation_jump_ms(t.scene, 0.0, +1) == doctest::Approx(2000.0));
    // Parked on an activation, "next" moves on and "previous" goes back past it.
    CHECK(activation_jump_ms(t.scene, 2000.0, +1) == doctest::Approx(8000.0));
    CHECK(activation_jump_ms(t.scene, 2000.3, +1) == doctest::Approx(8000.0));
    CHECK_FALSE(activation_jump_ms(t.scene, 2000.0, -1).has_value());
    CHECK(activation_jump_ms(t.scene, 5000.0, -1) == doctest::Approx(2000.0));
    CHECK(activation_jump_ms(t.scene, 8000.0, -1) == doctest::Approx(2000.0));
    CHECK_FALSE(activation_jump_ms(t.scene, 8000.0, +1).has_value());
    CHECK_FALSE(activation_jump_ms(build_preview_scene(t.song, nullptr), 0.0, +1).has_value());
}

TEST_CASE("next activation box: the activation at or after the playhead") {
    TwoActs t;
    PreviewNextActBox box = build_next_act_box(t.scene, 0.0);
    CHECK(box.shown);
    CHECK(box.header == "Next: activation 1 of 2");
    CHECK(box.detail == "at m2.1.0 " + kDot + " [Red]");
    CHECK(build_next_act_box(t.scene, 2000.0).header == "Next: activation 1 of 2");
    CHECK(build_next_act_box(t.scene, 2001.0).header == "Next: activation 2 of 2");
    CHECK(build_next_act_box(t.scene, 2001.0).detail == "at m5.1.0 " + kDot + " [Red]");
    CHECK_FALSE(build_next_act_box(t.scene, 9000.0).shown);
    CHECK_FALSE(build_next_act_box(build_preview_scene(t.song, nullptr), 0.0).shown);
}

TEST_CASE("sp meter readout: bars banked over the cap") {
    TwoActs t;
    // The phrase at 1000 ms banks a bar; the activation at 2000 ms drains it
    // to empty at its deact node, two measures later (6000 ms).
    CHECK(sp_meter_readout(t.scene.sp_meter, 1500.0) == "1.0/4");
    CHECK(sp_meter_readout(t.scene.sp_meter, 4000.0) == "0.5/4");
    CHECK(sp_meter_readout(SpMeterCurve{}, 0.0).empty());
}

TEST_CASE("preview path label: the notation and which list it came from") {
    PathButtonView b;
    b.notation = "3- 1 2";
    b.group = PathButtonView::Group::Optimal;
    CHECK(preview_path_label(b) == "3- 1 2  (optimal)");
    b.notation = "0 4 1";
    b.group = PathButtonView::Group::Within;
    CHECK(preview_path_label(b) == "0 4 1");
    b.notation = "0 0 0 0";
    b.group = PathButtonView::Group::AllZero;
    CHECK(preview_path_label(b) == "0 0 0 0  (0 ms limit)");
}
```

`kDot` in this file is the bare middle dot (`"\xC2\xB7"`, already defined in the file's namespace), so the strings above put a space on each side of it themselves.

- [ ] **Step 9: Run them and watch them fail.** Rebuild `hydra_tests`. Expected: the build fails in `tests/test_preview_view.cpp` naming `position`, `length`, `tempo`, `section_line`, `measure`, `chord`, `build_scrub_marks`, `activation_jump_ms`, `PreviewNextActBox`, `build_next_act_box`, `sp_meter_readout` and `preview_path_label`.

- [ ] **Step 10: Extend `src/app/preview_view.h`.** Add `#include "app/path_view.h"` after `#include "core/timing.h"`. In `struct PreviewActivation`, after `bool has_lane = false;` add:

```cpp
    // The activation's position as the Paths tab prints it (format_measure)
    // and its chord (Chord::rowstr); chord is empty when the record has none.
    std::string measure;
    std::string chord;
```

In `struct PreviewTimeBox`, after `std::string section;` add:

```cpp
    // The redesigned box's lines. Task 11 draws these, deletes measure_beat,
    // bpm, time_sig and section above, and moves `timestamp` beside the scrubber.
    std::string position;      // format_measure at the playhead, "m27.2.450"
    std::string length;        // format_measure at the song's end, "m96.3.240"
    std::string tempo;         // "BPM 191.001 · 4/4"
    std::string section_line;  // "Section chorus_1"; empty when no section is in force
```

Before `double sp_meter_bars_at(...)`, add:

```cpp
// Where each activation of the shown path sits on the scrubber: its onset over
// `length_ms` (the transport's length, the scrubber's right edge), clamped to
// 0..1, in activation order. Empty with no path or no length.
std::vector<double> build_scrub_marks(const PreviewScene& scene, double length_ms);

// Where "< Act" (direction -1) or "Act >" (+1) moves the playhead from
// `now_ms`: the onset of the nearest activation strictly before or after it.
// Half a millisecond of slack each way, so a playhead parked on an activation
// moves past it. nullopt when there is none that way.
std::optional<double> activation_jump_ms(const PreviewScene& scene, double now_ms,
                                         int direction);

// The box at the highway's bottom-left: the first activation at or after the
// playhead (the same half-millisecond slack), "Next: activation 1 of 3" and
// "at m32.1.0 · [Kick - GreenCym]". Hidden past the last activation and when
// the scene has no path.
struct PreviewNextActBox {
    bool shown = false;
    std::string header;
    std::string detail;
};
PreviewNextActBox build_next_act_box(const PreviewScene& scene, double now_ms);

// The number under the SP gauge: bars banked at `now_ms` over the cap, one
// decimal ("2.5/4"). Empty when the curve has no segments.
std::string sp_meter_readout(const SpMeterCurve& curve, double now_ms);

// One entry of the Preview's "Showing" list: the path's notation, with
// "  (optimal)" on an Optimal path and "  (0 ms limit)" on the all-0 path.
std::string preview_path_label(const PathButtonView& button);
```

- [ ] **Step 11: Implement them in `src/app/preview_view.cpp`.** In `build_preview_scene`'s overlay loop, replace:

```cpp
            if (a.chord.count() > 0) {
                pa.has_lane = true;
                pa.lane = lane_of(a.chord.activation_note().colortype);
            }
```

with:

```cpp
            if (a.chord.count() > 0) {
                pa.has_lane = true;
                pa.lane = lane_of(a.chord.activation_note().colortype);
                pa.chord = a.chord.rowstr();
            }
            pa.measure = format_measure(timing, pa.tick);
```

In `build_time_box`, replace:

```cpp
    box.measure_beat = mbt_str(now_tick) + " / " + mbt_str(end_tick);
```

with:

```cpp
    box.measure_beat = mbt_str(now_tick) + " / " + mbt_str(end_tick);
    box.position = scene.timing ? format_measure(*scene.timing, now_tick) : "m1.1.0";
    box.length = scene.timing ? format_measure(*scene.timing, end_tick) : "m1.1.0";
```

Replace:

```cpp
    std::snprintf(buf, sizeof buf, "Time signature: %d/%d", ts_num, ts_den);
    box.time_sig = buf;

    for (const PreviewSection& s : scene.sections) {
        if (s.tick > now_tick) break;
        box.section = s.name;
    }
    return box;
```

with:

```cpp
    std::snprintf(buf, sizeof buf, "Time signature: %d/%d", ts_num, ts_den);
    box.time_sig = buf;
    std::snprintf(buf, sizeof buf, "BPM %.3f \xC2\xB7 %d/%d", bpm, ts_num, ts_den);
    box.tempo = buf;

    for (const PreviewSection& s : scene.sections) {
        if (s.tick > now_tick) break;
        box.section = s.name;
    }
    if (!box.section.empty()) box.section_line = "Section " + box.section;
    return box;
```

Before `std::string path_overlay_key(...)`, add:

```cpp
std::vector<double> build_scrub_marks(const PreviewScene& scene, double length_ms) {
    std::vector<double> marks;
    if (length_ms <= 0.0) return marks;
    marks.reserve(scene.activations.size());
    for (const PreviewActivation& a : scene.activations)
        marks.push_back(std::clamp(a.ms / length_ms, 0.0, 1.0));
    return marks;
}

namespace {
// How far from an activation the playhead may sit and still count as on it.
constexpr double kOnActivationMs = 0.5;
}  // namespace

std::optional<double> activation_jump_ms(const PreviewScene& scene, double now_ms,
                                         int direction) {
    if (direction > 0) {
        for (const PreviewActivation& a : scene.activations)
            if (a.ms > now_ms + kOnActivationMs) return a.ms;
        return std::nullopt;
    }
    for (auto it = scene.activations.rbegin(); it != scene.activations.rend(); ++it)
        if (it->ms < now_ms - kOnActivationMs) return it->ms;
    return std::nullopt;
}

PreviewNextActBox build_next_act_box(const PreviewScene& scene, double now_ms) {
    PreviewNextActBox box;
    const size_t count = scene.activations.size();
    for (size_t i = 0; i < count; ++i) {
        const PreviewActivation& a = scene.activations[i];
        if (a.ms < now_ms - kOnActivationMs) continue;
        box.shown = true;
        box.header = "Next: activation " + std::to_string(i + 1) + " of " + std::to_string(count);
        box.detail = "at " + a.measure;
        if (!a.chord.empty()) box.detail += " \xC2\xB7 " + a.chord;
        break;
    }
    return box;
}

std::string sp_meter_readout(const SpMeterCurve& curve, double now_ms) {
    if (curve.segments.empty()) return {};
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f/%d", sp_meter_bars_at(curve, now_ms), curve.cap);
    return buf;
}

std::string preview_path_label(const PathButtonView& button) {
    switch (button.group) {
        case PathButtonView::Group::Optimal: return button.notation + "  (optimal)";
        case PathButtonView::Group::AllZero: return button.notation + "  (0 ms limit)";
        case PathButtonView::Group::Within:  break;
    }
    return button.notation;
}
```

- [ ] **Step 12: Run everything and watch it pass.** Rebuild `hydra_tests` and run `.\build-cpp\Release\hydra_tests.exe`. Expected: `Status: SUCCESS!`, with the old time-box and path-view cases passing unchanged beside the new ones. Then build the whole tree once (`powershell -ExecutionPolicy Bypass -File build_cpp.ps1 > build.log 2>&1`) so `Hydra.exe`, `hydra_uitest` and the tools still compile against the old fields, and run `.\build-cpp\Release\hydra_uitest.exe --all`. Expected: the same 26 tests pass, since nothing on screen changed.

- [ ] **Step 13: Prove no score changed.** Run the score-neutral proof from Global Constraints with `<n>` = 8. Expected: `Compare-Object` prints nothing.

- [ ] **Step 14: Commit.**

```powershell
git add src/app/preview_view.h src/app/preview_view.cpp tests/test_preview_view.cpp
git commit -m "Preview view: one measure format, scrubber marks, activation jumps, next-activation box, SP readout

Task: Task 8 - path and Preview view data
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: The song panel beside the library, and one settings bar

Today a click on a library row opens "Song Details" as a fixed modal. It covers the library, can't be moved, resized or closed with Escape, and has no next or previous song. A third of it is a settings band, and closing it silently cancels a running analysis. Changing SP cap while something analyzes files the result under the old cap and then hides it (audit findings 1, 2, 3, 10 and 11). This task rebuilds the main screen to the approved Main mockup. The library fills the left, and the song you click opens in a panel on the right with a draggable edge between them. When no song is open, the library takes the full width. One "Analysis settings" bar under the toolbar holds Difficulty, Pro Drums, 2x Bass, SP cap, Score range and Path limit, and it locks while anything analyzes. The panel's top shows the title, "artist · charted by charter", previous/next/close buttons, the optimal score and path in gold with one line of facts under it, and the Analyze this song / Re-analyze button. Closing the panel no longer cancels an analysis: it finishes, is stored, and the library row updates. The teal buttons and the dimmed and disabled text get colours that pass WCAG's 4.5:1.

**Wave:** 3. **Depends on:** T1 (the split files), T2 (`strip_rich_tags`), T4 (`plain_error`, `BatchJob::stop()`), T6 (`Settings::sp_cap` is an `int`, `last_cap` gone), T7 (`SummaryLookup::summary` with `stars`). **Expected overlaps:** T12 in `app_state.{h,cpp}` (T9 adds the panel, lock and navigation members; T12 adds the library model and re-implements the three `view_row*` accessors T9 adds, see the merge checklist) and in `library_view.cpp` (T9's `render_library_pane` wrapper holds today's library block; T12's pane replaces its body). T13 in `library_view.cpp` (T13 adds its two strip calls at the `// T13: batch strips` marker), in `library_toolbar.cpp` (T9 deletes `render_view_controls`, renames the scan button and moves the "Open path report" button into `render_actions_row`; T13 then rewrites `render_actions_row` whole, keeping those two), in `app_state.{h,cpp}` (different members) and in `app_shell.cpp` (T9 adds the `app.tick(...)` line, T13 adds its `app.update_batch(...)` line under it; keep both). T10 in `details_parts.h` (T9 changes only the analyze-job and panel declarations). T12 in `uitest_harness.cpp` only if T12 also edits the harness; T9 owns it.

**Goal:** The details modal becomes a resizable side panel, every analysis setting sits in one lockable bar on the main screen, and closing the panel never cancels work.

**Files:**
- Create: `src/ui/settings_bar.cpp`
- Create: `tests/test_theme.cpp`
- Create: `tests/test_song_panel_state.cpp`
- Modify: `src/ui/library_view.cpp` (`render_main_window`: the split, the settings bar, the strip marker)
- Modify: `src/ui/library_parts.h` (declare `render_settings_bar`; drop `render_view_controls`)
- Modify: `src/ui/library_toolbar.cpp` (delete `render_view_controls`; scan label; "Open path report" moves in)
- Modify: `src/ui/details_panel.cpp` (header, headline, notices, `render_song_panel`; `update_analyze_job` leaves)
- Modify: `src/ui/details_view.h` (`render_details_modal` becomes `render_song_panel`)
- Modify: `src/ui/details_parts.h` (drop the `update_analyze_job` declaration if T1 put one there)
- Modify: `src/ui/app_state.h`, `src/ui/app_state.cpp`
- Modify: `src/ui/app_shell.cpp` (`run_frame` calls `app.tick()` and no longer calls `render_details_modal`; T5 owned it in wave 1, nobody owns it in wave 3)
- Modify: `src/ui/theme.h`, `src/ui/theme.cpp`
- Modify: `src/ui/widgets.h` (`help_marker`, a width cap on `text_ellipsized`)
- Modify: `CMakeLists.txt` (`src/ui/settings_bar.cpp` in `hydra_ui`; the two new test files in `hydra_tests`)
- Test: `tests/ui/uitest_details.cpp`, `tests/ui/uitest_harness.h`, `tests/ui/uitest_harness.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="theme*"` passes: text on the button colour and its hovered and active shades is at least 4.5:1, and so are dimmed text on the window, settings bar and panel, disabled input text on its face, and disabled button text.
- [ ] `hydra_tests.exe -tc="song panel*"` passes: next and previous walk the library view and never wrap, the panel's teardown runs on its closing edge, and nothing is locked while idle.
- [ ] `hydra_uitest --test panel-open-close` passes: a row click shows `##songpanel` and narrows `##library`; `X##closepanel` and Escape each close it and the library goes back to full width.
- [ ] `hydra_uitest --test panel-prev-next` passes: `<##prevsong` is disabled on the first row, and `>##nextsong` / `<##prevsong` step one row each way.
- [ ] `hydra_uitest --test panel-headline` passes on Burnout: before analysis the panel shows `Not analyzed yet.` and `Analyze this song`; after it, `378,315`, `3- 1 2`, `Optimal path · 7 stars · hardest squeeze`, `163.0 ms`, `Green Day · charted by Hoph2o`, and the button reads `Re-analyze`.
- [ ] `hydra_uitest --test panel-keeps-analysis` passes: an analysis started and then closed with `X##closepanel` is not cancelled, and the library row reads Ready afterwards.
- [ ] `hydra_uitest --test settings-lock` passes: while a batch runs, `##spcap` is disabled and `Stop the batch to change these.` is on screen; once it stops, `##spcap` is live again.
- [ ] Every test in `tests/ui/uitest_details.cpp` passes.

**Verify:** `.\build-cpp\Release\hydra_uitest.exe --test panel-open-close --test panel-prev-next --test panel-headline --test panel-keeps-analysis --test settings-lock` → five `[PASS]` lines and exit code 0.

**Steps:**

- [ ] **Step 1: Write the failing theme test.** Create `tests/test_theme.cpp`. It computes the WCAG 2.2 contrast ratio from the theme's own constants, so a later colour change can't slip under 4.5:1 unnoticed.

```cpp
// The theme's text colours against the surfaces they sit on, measured the way
// WCAG 2.2 defines contrast: (L1 + 0.05) / (L2 + 0.05) on relative luminance.
// 4.5:1 is the minimum for normal-size text.

#include "doctest.h"

#include <cmath>
#include <utility>

#include "imgui.h"
#include "ui/theme.h"

using namespace hydra::ui;

namespace {

double channel(float c) {
    return c <= 0.04045f ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(const ImVec4& c) {
    return 0.2126 * channel(c.x) + 0.7152 * channel(c.y) + 0.0722 * channel(c.z);
}

double contrast(const ImVec4& a, const ImVec4& b) {
    double la = luminance(a), lb = luminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

}  // namespace

TEST_CASE("theme: button text reads at 4.5:1 in every button state") {
    CHECK(contrast(kDefaultTextColor, kButtonColor) >= 4.5);
    CHECK(contrast(kDefaultTextColor, kButtonHoveredColor) >= 4.5);
    CHECK(contrast(kDefaultTextColor, kButtonActiveColor) >= 4.5);
}

TEST_CASE("theme: dimmed and disabled text stays readable") {
    CHECK(contrast(kDimTextColor, kWindowBgColor) >= 4.5);
    CHECK(contrast(kDimTextColor, kSettingsBarBg) >= 4.5);
    CHECK(contrast(kDimTextColor, kPanelBg) >= 4.5);
    CHECK(contrast(kDisabledInputTextColor, kDisabledInputBgColor) >= 4.5);
    CHECK(contrast(kDisabledButtonTextColor, kDisabledButtonColor) >= 4.5);
}

TEST_CASE("theme: apply_theme uses the readable shades") {
    ImGui::CreateContext();
    apply_theme();
    const ImVec4* c = ImGui::GetStyle().Colors;
    CHECK(c[ImGuiCol_Button].y == kButtonColor.y);
    CHECK(c[ImGuiCol_ButtonHovered].y == kButtonHoveredColor.y);
    CHECK(c[ImGuiCol_ButtonActive].y == kButtonActiveColor.y);
    CHECK(c[ImGuiCol_TextDisabled].x == kDimTextColor.x);
    ImGui::DestroyContext();
}
```

Add `tests/test_theme.cpp` to the `hydra_tests` source list in `CMakeLists.txt`, after `tests/test_stars.cpp`. Build `hydra_tests` from Bash (`powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1`). It fails to compile: `kButtonHoveredColor`, `kButtonActiveColor`, `kDimTextColor`, `kWindowBgColor`, `kSettingsBarBg` and `kPanelBg` don't exist yet.

- [ ] **Step 2: Fix the theme.** In `src/ui/theme.h`, replace the button line:

```cpp
inline const ImVec4 kButtonColor{0 / 255.0f, 150 / 255.0f, 150 / 255.0f, 1.0f};
```

with the readable button shades and the new surfaces. The ratios are for the app's text colour (250,250,250): 4.95:1 on (0,122,122), 6.33:1 on (0,104,104), 7.93:1 on (0,88,88). The old (0,150,150) was 3.47:1, and its hover (0,180,180) was 2.4:1. Hover and press go darker, because a lighter teal can't keep white text above 4.5:1.

```cpp
// Button faces. White text on today's (0,150,150) was 3.5:1 and on its
// (0,180,180) hover 2.4:1, under WCAG's 4.5:1. These keep (250,250,250) text
// at 4.95:1, 6.33:1 and 7.93:1; hover and press go darker, since a lighter
// teal can't hold white text above 4.5:1.
inline const ImVec4 kButtonColor{0 / 255.0f, 122 / 255.0f, 122 / 255.0f, 1.0f};
inline const ImVec4 kButtonHoveredColor{0 / 255.0f, 104 / 255.0f, 104 / 255.0f, 1.0f};
inline const ImVec4 kButtonActiveColor{0 / 255.0f, 88 / 255.0f, 88 / 255.0f, 1.0f};

// Surfaces. The window is DPG's baseline (it used to be a local in
// apply_theme); the settings bar and the song panel sit a shade lighter so
// they read as their own areas, as in the approved mockup.
inline const ImVec4 kWindowBgColor{37 / 255.0f, 37 / 255.0f, 38 / 255.0f, 1.0f};
inline const ImVec4 kSettingsBarBg{43 / 255.0f, 43 / 255.0f, 46 / 255.0f, 1.0f};
inline const ImVec4 kPanelBg{40 / 255.0f, 40 / 255.0f, 42 / 255.0f, 1.0f};

// Dimmed text: hints, "(?)" markers, secondary lines, ImGui's TextDisabled,
// and disabled labels. (160,160,160) is 5.86:1 on the window, 5.40:1 on the
// settings bar and 4.81:1 on an input face. kNewSongColor (145) would be
// 4.48:1 on the settings bar, just under the line.
inline const ImVec4 kDimTextColor{160 / 255.0f, 160 / 255.0f, 160 / 255.0f, 1.0f};
// The byline under the song title: brighter than dimmed, quieter than text.
inline const ImVec4 kSubtleTextColor{200 / 255.0f, 200 / 255.0f, 200 / 255.0f, 1.0f};
```

In the same file, replace the disabled input text line:

```cpp
inline const ImVec4 kDisabledInputTextColor{50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f};
```

with:

```cpp
// Was (50,50,50): 1.15:1 on its (40,40,40) face, unreadable. (160,160,160)
// is 5.64:1 there; the flat dark face still says "off".
inline const ImVec4 kDisabledInputTextColor{160 / 255.0f, 160 / 255.0f, 160 / 255.0f, 1.0f};
```

Also update the comment above `begin_disabled_checkbox` so it says the label uses `kDimTextColor`. In `src/ui/theme.cpp`, `apply_theme()`: replace `ImVec4 dpg_window(37 / 255.0f, 37 / 255.0f, 38 / 255.0f, 1.0f);` with `const ImVec4 dpg_window = kWindowBgColor;`, and replace the two button lines

```cpp
    colors[ImGuiCol_ButtonHovered] = kAccentColor;
    colors[ImGuiCol_ButtonActive] = ImVec4(0, 200 / 255.0f, 200 / 255.0f, 1.0f);
```

with

```cpp
    colors[ImGuiCol_ButtonHovered] = kButtonHoveredColor;
    colors[ImGuiCol_ButtonActive] = kButtonActiveColor;
    colors[ImGuiCol_TextDisabled] = kDimTextColor;
```

In `begin_disabled_checkbox`, change the two `kNewSongColor` pushes (`ImGuiCol_Text` and `ImGuiCol_CheckMark`) to `kDimTextColor`. Build and run `.\build-cpp\Release\hydra_tests.exe -tc="theme*"`: `Status: SUCCESS!`.

Commit:

```powershell
git add src/ui/theme.h src/ui/theme.cpp tests/test_theme.cpp CMakeLists.txt
git commit -m "Theme: button, dimmed and disabled text at 4.5:1 or better

Task: Task 9 - song panel and settings bar
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 3: Write the failing panel-state tests.** Create `tests/test_song_panel_state.cpp`. It reuses the scratch-path pattern of `tests/test_app_state.cpp` (a scratch INI and DB for one test, restored afterwards), on a library of 20 charts with no records.

```cpp
// The song panel's state rules on AppState: next/previous walk the rows the
// library shows and never wrap, the panel's teardown runs on its closing
// edge (from tick(), not from draw code), and nothing is locked while idle.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "app/config.h"
#include "core/winstr.h"
#include "store/record_store.h"
#include "ui/app_state.h"

using hydra::app::Settings;
using hydra::store::ChartLibraryEntry;
using hydra::store::RecordStore;
using hydra::ui::AppState;

namespace {

std::string temp_path(const char* tag, const char* ext) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return hydra::wide_to_utf8(tmp) + "hydra_test_" + tag + "_" +
           std::to_string(GetCurrentProcessId()) + ext;
}

// Scratch INI and DB for one test; the process's paths come back afterwards.
struct ScratchPaths {
    hydra::app::PathOverrides previous;
    std::string ini, db;
    explicit ScratchPaths(const char* tag)
        : previous(hydra::app::path_overrides()),
          ini(temp_path(tag, ".ini")),
          db(temp_path(tag, ".db")) {
        std::remove(ini.c_str());
        std::remove(db.c_str());
        hydra::app::PathOverrides o = previous;
        o.ini_path = ini;
        o.db_path = db;
        hydra::app::set_path_overrides(o);
    }
    ~ScratchPaths() {
        hydra::app::set_path_overrides(previous);
        std::remove(ini.c_str());
        std::remove(db.c_str());
    }
};

ChartLibraryEntry entry(int i) {
    char hash[32];
    std::snprintf(hash, sizeof(hash), "hash%03d", i);
    ChartLibraryEntry e;
    e.md5 = hash;
    e.title = std::string("Song ") + hash;
    e.artist = "Artist";
    e.charter = "Charter";
    e.notespath = std::string("C:\\charts\\") + hash + "\\notes.chart";
    e.rootfolder = "C:\\charts";
    e.sig = "sig";
    return e;
}

std::unique_ptr<AppState> app_with_library(const ScratchPaths& paths, int charts) {
    auto store = std::make_unique<RecordStore>(paths.db);
    std::vector<ChartLibraryEntry> all;
    for (int i = 0; i < charts; ++i) all.push_back(entry(i));
    store->rebuild_chart_library(all);
    auto app = std::make_unique<AppState>(Settings{}, std::move(store));
    REQUIRE(app->view_row_count() >= 3);
    return app;
}

}  // namespace

TEST_CASE("song panel: next and previous walk the view and never wrap") {
    ScratchPaths paths("panel_nav");
    auto app = app_with_library(paths, 20);
    app->select(app->view_row(0));
    CHECK(app->details_open());
    CHECK_FALSE(app->can_select_relative(-1));  // first row: nothing before it
    CHECK(app->can_select_relative(1));

    app->select_relative(1);
    CHECK(app->selected->notespath == app->view_row(1).notespath);
    app->select_relative(-1);
    CHECK(app->selected->notespath == app->view_row(0).notespath);
    app->select_relative(-1);  // no wrap to the last row
    CHECK(app->selected->notespath == app->view_row(0).notespath);

    const size_t last = app->view_row_count() - 1;
    app->select(app->view_row(last));
    CHECK_FALSE(app->can_select_relative(1));
    app->select_relative(1);
    CHECK(app->selected->notespath == app->view_row(last).notespath);
}

TEST_CASE("song panel: a song outside the view has no neighbours") {
    ScratchPaths paths("panel_outside");
    auto app = app_with_library(paths, 20);
    app->select(entry(999));  // not in the library at all
    CHECK_FALSE(app->can_select_relative(1));
    CHECK_FALSE(app->can_select_relative(-1));
}

TEST_CASE("song panel: tick runs the teardown once, on the closing edge") {
    ScratchPaths paths("panel_edge");
    auto app = app_with_library(paths, 5);
    app->select(app->view_row(0));
    app->tick(0.0);
    app->details_ui.file_checked_at = 1.0;  // as if the file was looked at
    app->tick(0.1);                          // still open: no teardown
    CHECK(app->details_ui.file_checked_at == 1.0);

    app->show_details = false;               // what the X and Escape do
    app->tick(0.2);
    CHECK_FALSE(app->details_open());
    CHECK(app->details_ui.file_checked_at == -1.0);  // close_details ran
}

TEST_CASE("song panel: nothing is locked while idle") {
    ScratchPaths paths("panel_lock");
    auto app = app_with_library(paths, 5);
    CHECK_FALSE(app->settings_locked());
    CHECK_FALSE(app->analyze_running());
    CHECK_FALSE(app->batch_running());
}
```

Add `tests/test_song_panel_state.cpp` after `tests/test_app_state.cpp` in the `hydra_tests` list. Build: it fails to compile, because `view_row_count`, `view_row`, `details_open`, `can_select_relative`, `select_relative`, `tick`, `settings_locked`, `analyze_running` and `batch_running` don't exist.

- [ ] **Step 4: Add the panel state to AppState.** In `src/ui/app_state.h`:

In `DetailsViewState`, replace

```cpp
    // Tracks show_details' false->true edge, which is what opens the popup.
    bool prev_open = false;
```

with

```cpp
    // show_details as of the last tick(); its true->false edge runs
    // close_details() once, whatever closed the panel.
    bool prev_open = false;
```

In `LibraryViewState`, add at the end:

```cpp
    // The library's width beside the panel, in pixels, as the user last left
    // it this session; -1 = not measured yet. The child's own .ini entry
    // holds it across runs, but a full-width frame while the panel is closed
    // overwrites the live size, so the split puts this back on reopen.
    float library_w = -1.0f;
    bool panel_was_open = false;
```

Add `#include <cstddef>` to the includes. In `class AppState`, after `void select(const store::ChartLibraryEntry& entry);`, add:

```cpp
    // Whether the song panel is showing. Code outside the panel reads this,
    // not show_details (which the X, Escape and the tests write).
    bool details_open() const { return show_details; }

    // The rows the library currently shows, in order. Next / previous and
    // the GUI tests walk these. (T12 re-implements the three over its
    // in-memory model and library_view_order(); callers don't change.)
    size_t view_row_count() const;
    const store::ChartLibraryEntry& view_row(size_t i) const;
    store::RecordStatus view_row_status(size_t i) const;

    // Opens the next (delta 1) or previous (delta -1) row of the current
    // view. Never wraps, and does nothing when the open song isn't in the view.
    void select_relative(int delta);
    bool can_select_relative(int delta) const;

    // The best path's stored facts for the open song (stars, hardest squeeze),
    // read with the record. Empty unless `viewed` is Ready.
    store::PathSummary viewed_summary;
```

Replace the `close_details` comment block with:

```cpp
    // Everything that must stop when the song panel closes. It runs once, on
    // the panel's open-to-closed edge (tick() watches it), whatever closed
    // it: the X, Escape, the Rescan library button, or a new selection. The
    // Preview stops and lets go of its audio device, a finished Dynamics count
    // is kept and an unfinished one is cancelled. A running analysis is NOT
    // cancelled: it finishes and is stored (tick()). Safe to call when closed.
    void close_details();
```

After `std::unique_ptr<ReportJob> report_job;`, add:

```cpp
    // True while a single-song analysis or a batch is running. While either
    // runs, the settings bar is locked: a result is filed under the settings
    // it ran with, so changing them mid-run used to hide the result it made.
    bool analyze_running() const;
    bool batch_running() const;
    bool settings_locked() const { return analyze_running() || batch_running(); }
    // True when the analyze job belongs to the song the open panel shows, so
    // the panel is where its progress and errors appear.
    bool analyze_job_shown() const;

    // Once per frame, before any view draws (run_frame). Owns the panel's
    // closing edge, storing and reaping the analyze job, and storing a
    // finished Dynamics count. `now` is ImGui::GetTime() in the app.
    void tick(double now);
```

In the `private:` section, add:

```cpp
    // The analyze job's lifecycle: store a finished result, reap the job.
    // Moved out of the details view's draw code; tick() calls it every frame.
    void update_analyze_job(double now);
    // The row index select_relative would open, if there is one.
    std::optional<size_t> relative_row(int delta) const;
    // Re-reads viewed_summary for the open song under the current settings.
    void refresh_viewed_summary();
```

In `src/ui/app_state.cpp`, add `#include "app/user_messages.h"` to the includes. Add the accessors and navigation after `AppState::set_rows_per_page`:

```cpp
// Over the page the table shows today. T12 re-implements these three over its
// whole-library model (library_view_order()); every caller stays the same.
size_t AppState::view_row_count() const { return current_page.rows.size(); }

const store::ChartLibraryEntry& AppState::view_row(size_t i) const {
    return current_page.rows[i];
}

store::RecordStatus AppState::view_row_status(size_t i) const {
    return i < current_page.summaries.size() ? current_page.summaries[i].state
                                             : store::RecordStatus::NotAnalyzed;
}

std::optional<size_t> AppState::relative_row(int delta) const {
    if (!selected || delta == 0) return std::nullopt;
    const size_t n = view_row_count();
    for (size_t i = 0; i < n; ++i) {
        // notespath, not md5: the same chart can sit in two folders.
        if (view_row(i).notespath != selected->notespath) continue;
        const long long j = static_cast<long long>(i) + delta;
        if (j < 0 || j >= static_cast<long long>(n)) return std::nullopt;
        return static_cast<size_t>(j);
    }
    return std::nullopt;
}

bool AppState::can_select_relative(int delta) const { return relative_row(delta).has_value(); }

void AppState::select_relative(int delta) {
    if (std::optional<size_t> i = relative_row(delta)) select(view_row(*i));
}
```

Replace `AppState::close_details()` whole. Write the new body over the old one with the Edit tool; it keeps most lines:

```cpp
void AppState::close_details() {
    show_details = false;
    // A running analysis keeps going: tick() stores it when it finishes,
    // whichever song is showing by then. (Closing used to cancel it.)
    // The audio device must stop, and the GPU and decode work must not keep
    // running behind a hidden panel.
    if (preview) preview->close();
    // Keep a count that already finished; cancel one still parsing.
    reap_dynamics();
    if (dynamics_job) {
        dynamics_job->cancel();
        dynamics_job.reset();
    }
    dynamics_result.reset();
    dynamics_key.clear();
    dynamics_store_error.clear();
    // The next open looks at the chart file at once.
    details_ui.file_checked_at = -1.0;
}
```

At the end of `AppState::refresh_viewed_record()`, after `record_generation.bump();`, add `refresh_viewed_summary();`. At the start of its `if (!selected)` branch, add `viewed_summary = store::PathSummary{};` before the `return;`. In `AppState::show_record_for_settings()`, after the `record_generation.bump();  // selected_path must re-sync, as after a read` line, add `refresh_viewed_summary();`. Then add:

```cpp
void AppState::refresh_viewed_summary() {
    viewed_summary = store::PathSummary{};
    if (!selected || viewed.status != store::RecordStatus::Ready) return;
    // One chart, one query, only when the record changes -- never per frame.
    std::vector<store::SummaryLookup> found = store->get_summaries(
        {selected->md5}, settings.chartmode_key(), settings.cap_query(), settings.lens());
    if (!found.empty()) viewed_summary = std::move(found.front().summary);
}

bool AppState::analyze_running() const { return analyze_job && !analyze_job->finished(); }

bool AppState::batch_running() const { return batch_job && !batch_job->snapshot().finished; }

bool AppState::analyze_job_shown() const {
    return analyze_job && show_details && selected &&
           analyze_job->song().notespath == selected->notespath;
}

void AppState::tick(double now) {
    if (!show_details && details_ui.prev_open) close_details();
    details_ui.prev_open = show_details;
    update_analyze_job(now);
    reap_dynamics();
}

void AppState::update_analyze_job(double now) {
    // Keyed on the job generation, not the job's address: a freed AnalyzeJob's
    // block can be handed straight back to the next make_unique, and a pointer
    // compare then carries `stored`/`done_at` over from the previous job.
    DetailsViewState& d = details_ui;
    if (d.analyze_watcher.changed(analyze_generation)) {
        d.done_at = -1.0;
        d.stored = false;
        d.store_error.clear();
    }
    AnalyzeJob* job = analyze_job.get();
    if (!job || !job->finished()) return;
    if (job->is_cancelled()) {  // the panel's Cancel: nothing to store
        analyze_job.reset();
        return;
    }
    // A result or error for a song the panel isn't showing goes to the status
    // line; one for the shown song stays in the panel until Continue.
    const bool shown = analyze_job_shown();
    const std::string title = job->song().title;
    if (!job->ok()) {
        if (!shown) {
            // message() is T4's plain sentence; the raw error() stays in the
            // panel's detail line for a song that is showing.
            set_status("Could not analyze " + title + ". " + job->message());
            analyze_job.reset();
        }
        return;
    }
    if (!d.stored) {
        d.stored = true;
        d.store_error = store_finished_analysis();
        if (d.store_error.empty()) d.done_at = now;
    }
    if (!d.store_error.empty()) {
        if (!shown) {
            set_status(d.store_error);
            analyze_job.reset();
        }
        return;
    }
    // The panel flashes "Done!" for half a second; nobody sees it otherwise.
    if (!shown || now - d.done_at > 0.5) analyze_job.reset();
}
```

In `AppState::store_finished_analysis()`, replace the catch line

```cpp
        return std::string("Analyzed, but saving failed: ") + e.what();
```

with

```cpp
        return "Analyzed, but saving failed. " + app::plain_error(e);
```

Build `hydra_tests` and run `.\build-cpp\Release\hydra_tests.exe -tc="song panel*"`: `Status: SUCCESS!`. Run the whole suite too: `Status: SUCCESS!`. (The "close_details keeps a Dynamics count" case in `test_app_state.cpp` still passes; nothing there relied on the cancel.)

Commit with message "AppState: panel navigation, settings lock, analyze job reaped in tick()" and the four trailers.

- [ ] **Step 5: Update the GUI test helpers.** T1 moved the shared helpers `scan_library`, `open_details`, `open_titled` and `analyze_open_song` into `tests/ui/uitest_harness.{h,cpp}`; find them by name. If T1 left them anywhere else, stop and tell the main session. Replace the four with these, and add the two new helpers `set_panel_ref` and `analyze_button_ref` (declare all six in `uitest_harness.h` under the "helpers shared by the tests" heading). They use only the label contract and the `view_row*` accessors, so T12's model change doesn't touch them.

```cpp
// Scan testdata/input through the UI and land on the populated library.
void scan_library(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Scan library");
    IM_CHECK(wait_until(ctx, [&] { return h.app->scan_job && h.app->scan_job->snapshot().finished; }, 60));
    ctx->SetRef("//Scanning charts");
    // State, not the modal's wording: T13 rewrites "chart(s) found" with
    // real plurals in this same wave.
    IM_CHECK(h.app->scan_job->snapshot().charts_found > 0);
    ctx->ItemClick("Continue");
    ctx->Yield(2);
    ctx->SetRef("//Hydra");
    IM_CHECK(h.app->scan_job == nullptr);
    IM_CHECK(h.app->library_total > 0);
    IM_CHECK(h.app->view_row_count() > 0);
}

// Point the ref at the song panel. It is a child window of the main window,
// and child window names are mangled, so go through WindowInfo.
void set_panel_ref(ImGuiTestContext* ctx) {
    ImGuiWindow* panel = ctx->WindowInfo("//Hydra/##songpanel").Window;
    IM_CHECK(panel != nullptr);
    ctx->SetRef(panel);
}

// Click row `index` of the library view and wait for the song panel.
void open_details(ImGuiTestContext* ctx, size_t index) {
    Harness& h = harness(ctx);
    IM_CHECK(index < h.app->view_row_count());
    const std::string title = h.app->view_row(index).title;
    ctx->SetRef("//Hydra");
    ctx->ItemClick(("**/" + escape_ref(title)).c_str());
    ctx->Yield(3);
    IM_CHECK(h.app->details_open());
    IM_CHECK(h.app->selected && h.app->selected->title == title);
    set_panel_ref(ctx);
    if (ctx->IsError()) return;
    // The ImGui context outlives reset_app, so the tab bar remembers the tab a
    // previous test left selected. Land on Paths deterministically.
    ctx->ItemClick("##DetailsTabs/Paths");
}

// Narrow the library with `search` typed into the search box, then open the
// row titled `title`.
void open_titled(ImGuiTestContext* ctx, const std::string& search, const std::string& title) {
    Harness& h = harness(ctx);
    ctx->SetRef("//Hydra");
    ctx->ItemInputValue("**/##search", search.c_str());
    size_t idx = 0;
    auto find_row = [&] {
        for (size_t i = 0; i < h.app->view_row_count(); ++i)
            if (h.app->view_row(i).title == title) { idx = i; return true; }
        return false;
    };
    IM_CHECK(wait_until(ctx, find_row, 5));
    open_details(ctx, idx);
}

// The analyze button's ref for the open song: its label depends on whether
// the song has a result.
std::string analyze_button_ref(Harness& h) {
    return h.app->viewed.status == hydra::store::RecordStatus::NotAnalyzed ? "**/Analyze this song"
                                                                          : "**/Re-analyze";
}

// Analyze the open song and wait for a Ready record.
void analyze_open_song(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    set_panel_ref(ctx);
    if (ctx->IsError()) return;
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->ItemClick(analyze_button_ref(h).c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.status == hydra::store::RecordStatus::Ready);
}
```

In `uitest_harness.cpp`, two more spots read the old page directly. In `reset_app`'s canned fetcher, replace

```cpp
        std::string md5 = (h.app && !h.app->current_page.rows.empty())
                              ? h.app->current_page.rows[0].md5
                              : "00000000000000000000000000000000";
```

with

```cpp
        std::string md5 = (h.app && h.app->view_row_count() > 0)
                              ? h.app->view_row(0).md5
                              : "00000000000000000000000000000000";
```

In `dump_state`, replace the library lines (from `std::printf("  library_total=%lld page_rows=...` through the end of the row loop) with:

```cpp
    std::printf("  library_total=%lld view_rows=%zu search=\"%s\"\n",
                (long long)a.library_total, a.view_row_count(), a.search.c_str());
    for (size_t i = 0; i < a.view_row_count(); ++i) {
        const auto& r = a.view_row(i);
        const auto s = a.view_row_status(i);
        const char* st = s == hydra::store::RecordStatus::Ready   ? "current"
                         : s == hydra::store::RecordStatus::Stale ? "stale"
                                                                  : "new";
        std::printf("    row[%zu] \"%s\" - %s (%s) md5=%s bestpath=%s\n", i, r.title.c_str(),
                    r.artist.c_str(), r.charter.c_str(), r.md5.c_str(), st);
    }
```

and in its selection line change `yes_no(a.show_details)` to `yes_no(a.details_open())` and the label `show_details=` to `panel_open=`.

- [ ] **Step 6: Write the failing GUI tests.** Add these five to `tests/ui/uitest_details.cpp` and to its entry table as `{"panel-open-close", test_panel_open_close}`, `{"panel-prev-next", test_panel_prev_next}`, `{"panel-headline", test_panel_headline}`, `{"panel-keeps-analysis", test_panel_keeps_analysis}`, `{"settings-lock", test_settings_lock}`:

```cpp
// The panel docks beside the library: opening it narrows the library, and
// the X and Escape both close it and give the library its width back.
void test_panel_open_close(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    const float main_w = ctx->GetWindowByRef("//Hydra")->Size.x;
    auto library_w = [&] { return ctx->WindowInfo("//Hydra/##library").Window->Size.x; };
    IM_CHECK(library_w() > main_w * 0.9f);

    open_details(ctx, 0);
    if (ctx->IsError()) return;
    IM_CHECK(library_w() < main_w * 0.7f);
    IM_CHECK(ctx->WindowInfo("//Hydra/##songpanel").Window != nullptr);
    IM_CHECK(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));  // no modal any more

    ctx->ItemClick("X##closepanel");
    ctx->Yield(3);
    IM_CHECK(!h.app->details_open());
    IM_CHECK(library_w() > main_w * 0.9f);

    open_details(ctx, 0);
    if (ctx->IsError()) return;
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(3);
    IM_CHECK(!h.app->details_open());
}

// Previous / next step through the library's rows and stop at the ends.
void test_panel_prev_next(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    IM_CHECK((ctx->ItemInfo("<##prevsong").ItemFlags & ImGuiItemFlags_Disabled) != 0);
    ctx->ItemClick(">##nextsong");
    ctx->Yield(2);
    IM_CHECK(h.app->selected->notespath == h.app->view_row(1).notespath);
    IM_CHECK(h.app->details_open());
    ctx->ItemClick("<##prevsong");
    ctx->Yield(2);
    IM_CHECK(h.app->selected->notespath == h.app->view_row(0).notespath);
}

// The panel's top on Burnout (Green Day, charted by Hoph2o), before and after
// analysis, at the scratch settings (Expert, Pro Drums, 2x Bass, cap 4,
// 2 scores, 10 ms).
void test_panel_headline(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_titled(ctx, "Burnout", "Burnout");
    if (ctx->IsError()) return;
    std::string text = visible_text(h);
    IM_CHECK(text.find("Green Day \xC2\xB7 charted by Hoph2o") != std::string::npos);
    IM_CHECK(text.find("Not analyzed yet.") != std::string::npos);
    IM_CHECK(ctx->ItemExists("**/Analyze this song"));

    analyze_open_song(ctx);
    if (ctx->IsError()) return;
    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("378,315") != std::string::npos;
    }, 5));
    text = visible_text(h);
    IM_CHECK(text.find("3- 1 2") != std::string::npos);
    IM_CHECK(text.find("Optimal path \xC2\xB7 7 stars \xC2\xB7 hardest squeeze") !=
             std::string::npos);
    IM_CHECK(text.find("163.0 ms") != std::string::npos);
    IM_CHECK(ctx->ItemExists("**/Re-analyze"));
}

// Closing the panel mid-analysis no longer cancels it: the result is stored
// and the row reads Ready.
void test_panel_keeps_analysis(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_details(ctx, 0);
    if (ctx->IsError()) return;
    ctx->ItemClick("**/Analyze this song");
    ctx->ItemClick("X##closepanel");
    ctx->Yield(2);
    IM_CHECK(!h.app->details_open());
    IM_CHECK(!h.app->analyze_job || !h.app->analyze_job->is_cancelled());
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->view_row_status(0) == hydra::store::RecordStatus::Ready;
    }, 5));
}

// The settings bar locks while a batch runs and unlocks when it stops.
void test_settings_lock(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    IM_CHECK((ctx->ItemInfo("**/##spcap").ItemFlags & ImGuiItemFlags_Disabled) == 0);

    h.app->start_batch(false);  // the whole library: long enough to look at
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_running(); }, 10));
    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("Stop the batch to change these.") != std::string::npos;
    }, 5));
    IM_CHECK((ctx->ItemInfo("**/##spcap").ItemFlags & ImGuiItemFlags_Disabled) != 0);
    IM_CHECK((ctx->ItemInfo("**/Pro Drums").ItemFlags & ImGuiItemFlags_Disabled) != 0);

    h.app->batch_job->stop();
    IM_CHECK(wait_until(ctx, [&] { return !h.app->batch_running(); }, 300));
    IM_CHECK(wait_until(ctx, [&] { return !jobs_busy(h); }, 120));
    IM_CHECK((ctx->ItemInfo("**/##spcap").ItemFlags & ImGuiItemFlags_Disabled) == 0);
}
```

Then move every existing test in `uitest_details.cpp` to the new UI by these rules. Each is a label or accessor swap, not a change of what the test checks.

- `ctx->SetRef("//$FOCUSED")` becomes `set_panel_ref(ctx)` followed by `if (ctx->IsError()) return;`.
- `"**/Analyze paths!"` becomes `analyze_button_ref(h).c_str()`.
- `"**/##spcapvalue"` becomes `"**/##spcap"`.
- `h.app->current_page.summaries[i].state` becomes `h.app->view_row_status(i)`, and `h.app->current_page.rows[i]` becomes `h.app->view_row(i)`.
- A check that the modal title says `Song Details` or names the chart mode is deleted; the settings bar shows the difficulty now.
- In `test_analyze`, delete the Auto block (from the comment `// Auto has nothing above 4 bars to reuse` to the end of the test; T6 already removed the checkbox), and replace the `"SP cap:  4 bars"` wait with a wait for the best path string (`best`), which the headline shows.
- If `test_backend_limit` is in this file, delete it: the Backend limit control moves to the Paths tab, and T10 re-creates the test in `uitest_paths.cpp` (see the merge checklist).

Build `hydra_uitest` and run `.\build-cpp\Release\hydra_uitest.exe --test panel-open-close`. It fails at `set_panel_ref`: there is no `##songpanel` window yet.

- [ ] **Step 7: Add the widget helpers.** In `src/ui/widgets.h`, add `#include <algorithm>` and give `text_ellipsized` a width cap. Replace

```cpp
inline void text_ellipsized(const char* text) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    float avail = ImGui::GetContentRegionAvail().x;
```

with

```cpp
// `max_width` caps the space the text may take, for text that shares its
// line with right-aligned buttons (the song panel's title).
inline void text_ellipsized(const char* text, float max_width = FLT_MAX) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    float avail = std::min(ImGui::GetContentRegionAvail().x, max_width);
```

After `hint()`, add:

```cpp
// The "(?)" marker after a setting's label: dimmed, with the explanation on
// hover. Hovers even inside a disabled (locked) group.
inline void help_marker(const char* text) {
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}
```

- [ ] **Step 8: Build the settings bar.** Create `src/ui/settings_bar.cpp`. It draws every setting that lived in `render_view_controls` (library_toolbar.cpp) and in the details modal's `render_controls`, except Backend limit, which T10 moves to the Paths tab. The hint texts are today's; Score range and Path limit get theirs from the user guide's own sections.

```cpp
// The "Analysis settings" bar under the toolbar: every setting an analysis
// runs with, for every song. Locked while anything analyzes, because a result
// is filed under the settings it ran with -- changing SP cap mid-run used to
// hide the result it had just made.

#include "imgui.h"
#include "imgui_internal.h"  // SeparatorEx (vertical)
#include "ui/app_state.h"
#include "ui/library_parts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <algorithm>
#include <iterator>
#include <string>

namespace hydra::ui::detail {

namespace {

void bar_separator() {
    ImGui::SameLine(0.0f, px(14.0f));
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine(0.0f, px(14.0f));
}

void render_difficulty(AppState& app, bool locked) {
    ImGui::TextUnformatted("Difficulty");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(90));
    const char* names[std::size(kAllDifficulties)];
    for (size_t i = 0; i < std::size(kAllDifficulties); ++i)
        names[i] = difficulty_name(kAllDifficulties[i]);
    int idx = static_cast<int>(app.settings.difficulty());
    begin_disabled_input(locked);
    if (ImGui::Combo("##difficulty", &idx, names, IM_ARRAYSIZE(names))) {
        app.settings.view_difficulty = names[idx];
        // A different difficulty is a different chartmode: commit_settings
        // re-reads the library and rewrites the INI.
        app.commit_settings();
    }
    end_disabled_input(locked);
    help_marker("Which charted difficulty to analyze, path and preview");

    ImGui::SameLine();
    begin_disabled_checkbox(locked);
    if (ImGui::Checkbox("Pro Drums", &app.settings.view_prodrums)) app.commit_settings();
    end_disabled_checkbox(locked);

    // A second kick pedal only exists in Expert charting, so off Expert the
    // box reads unchecked and is disabled; the stored view_bass2x is left
    // alone, so returning to Expert brings the user's own setting back.
    ImGui::SameLine();
    const bool expert = app.settings.difficulty() == Difficulty::Expert;
    bool bass2x_shown = app.settings.effective_bass2x();
    begin_disabled_checkbox(!expert || locked);
    if (ImGui::Checkbox("2x Bass", &bass2x_shown)) {
        app.settings.view_bass2x = bass2x_shown;
        app.commit_settings();
    }
    end_disabled_checkbox(!expert || locked);
    if (!expert && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                        ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("2x Bass is an Expert-only charting concept.");
}

void render_sp_cap(AppState& app, bool locked) {
    ImGui::TextUnformatted("SP cap");
    help_marker((std::to_string(kCloneHeroSpCap) +
                 " bars is Clone Hero's rule. Higher caps are what-ifs; their scores "
                 "are not achievable in game.").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(90));
    begin_disabled_input(locked);
    // Number boxes apply every step live but save the INI once the edit ends
    // (AppState::edit_settings, flushed by run_frame).
    int cap = app.settings.sp_cap;
    if (ImGui::InputInt("##spcap", &cap)) {
        app.settings.sp_cap = std::max(1, cap);
        app.edit_settings();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("bars");
    end_disabled_input(locked);
}

void render_score_range(AppState& app, bool locked) {
    ImGui::TextUnformatted("Score range");
    help_marker("How many extra paths below optimal to keep: a number of scores, or of "
                "points. More paths take longer to analyze.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(90));
    begin_disabled_input(locked);
    if (ImGui::InputInt("##depthvalue", &app.settings.depth_value)) {
        if (app.settings.depth_value < 0) app.settings.depth_value = 0;
        app.edit_settings();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(80));
    int mode_idx = app.settings.depth_mode;
    const char* modes[] = {"scores", "points"};
    if (ImGui::Combo("##depthmode", &mode_idx, modes, 2)) {
        app.settings.depth_mode = mode_idx;
        app.commit_settings();
    }
    end_disabled_input(locked);
}

void render_path_limit(AppState& app, bool locked) {
    begin_disabled_checkbox(locked);
    if (ImGui::Checkbox("Path limit##mslimit", &app.settings.mslimit_enabled))
        app.commit_settings();
    end_disabled_checkbox(locked);
    help_marker("Keep extra paths only when their hardest squeeze is within this many "
                "ms. Lower or negative values demand more slack.");
    ImGui::SameLine();
    const bool off = locked || !app.settings.mslimit_enabled;
    begin_disabled_input(off);
    ImGui::SetNextItemWidth(px(100));
    if (ImGui::InputInt("##mslimitvalue", &app.settings.mslimit_value)) {
        app.settings.mslimit_value = std::clamp(app.settings.mslimit_value, -500, 500);
        app.edit_settings();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("ms");
    end_disabled_input(off);
}

}  // namespace

void render_settings_bar(AppState& app) {
    const bool locked = app.settings_locked();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kSettingsBarBg);
    ImGui::BeginChild("##settingsbar", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();

    // Two stacked caption lines; the controls sit centred on them.
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Analysis settings");
    ImGui::TextDisabled(locked ? "locked" : "for every song");
    ImGui::EndGroup();
    const float caption_h = ImGui::GetItemRectSize().y;
    bar_separator();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (caption_h - ImGui::GetFrameHeight()) * 0.5f);
    ImGui::AlignTextToFramePadding();

    render_difficulty(app, locked);
    bar_separator();
    render_sp_cap(app, locked);
    bar_separator();
    render_score_range(app, locked);
    bar_separator();
    render_path_limit(app, locked);

    if (locked) {
        const char* why = app.batch_running() ? "Stop the batch to change these."
                                              : "Settings are locked while this song analyzes.";
        const float w = ImGui::CalcTextSize(why).x;
        ImGui::SameLine();
        const float right = ImGui::GetContentRegionMax().x - w;
        if (right > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right);
        ImGui::PushStyleColor(ImGuiCol_Text, kSubtleTextColor);
        ImGui::TextUnformatted(why);
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

}  // namespace hydra::ui::detail
```

In `src/ui/library_parts.h`, add `void render_settings_bar(AppState& app);` beside the other declarations in `namespace hydra::ui::detail`, and delete the `render_view_controls` declaration. Add `src/ui/settings_bar.cpp` to `hydra_ui`'s source list in `CMakeLists.txt`, after the `library_toolbar.cpp` line T1 added.

In `src/ui/library_toolbar.cpp`, delete the whole `render_view_controls` function (from `void render_view_controls(AppState& app) {` to its closing brace); its controls live in the bar now. In `render_actions_row`, the scan button gets its one label. Replace

```cpp
    const float scan_w = std::max(button_slot_width("Refresh scan"), button_slot_width("Scan charts"));
    if (button_in_slot(app.settings.is_rescan ? "Refresh scan" : "Scan charts", scan_w)) {
```

with

```cpp
    if (ImGui::Button("Scan library")) {
```

Then move the "Open path report" block out of `render_main_window` (Step 10 deletes it there) into `render_actions_row`, just before `render_status_line(app, /*same_line=*/true);`. It keeps its logic exactly:

```cpp
    // A way back into the last batch's HTML report. While a report job is
    // still building, the slot shows a greyed "Building path report..." button.
    if (app.report_job && !app.report_job->finished()) {
        ImGui::SameLine();
        begin_disabled_button(true);
        ImGui::Button("Building path report...");
        end_disabled_button(true);
    } else if (app.report_file_shown(ImGui::GetTime())) {
        ImGui::SameLine();
        if (ImGui::Button("Open path report") && !app::open_report_in_browser())
            app.set_status("The path report could not be opened.");
    }
```

Add `#include "app/report_files.h"` to `library_toolbar.cpp` if T1 didn't carry it there.

- [ ] **Step 9: Build the panel.** In `src/ui/details_panel.cpp`, add `#include "app/library_query.h"` (for `strip_rich_tags`). Delete `render_song_info`, `render_record_status` and `render_controls` whole, and `icon_marker` if nothing else calls it (grep first). Delete `update_analyze_job`; it moved to `AppState` in Step 4. Put these three in its place, in the file's anonymous namespace:

```cpp
// Title, "artist · charted by charter", and previous / next / close at the
// right. Clone Hero rich-text tags (<color=...>) are stripped for display.
void render_panel_header(AppState& app) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float button = ImGui::GetFrameHeight();
    const float buttons_w = button * 3.0f + style.ItemSpacing.x * 2.0f;
    const float left_x = ImGui::GetCursorPosX();
    const float top_y = ImGui::GetCursorPosY();
    const float text_w = ImGui::GetContentRegionAvail().x - buttons_w - style.ItemSpacing.x;

    // The buttons first, at the right edge, so the title can take the rest.
    ImGui::SetCursorPosX(left_x + text_w + style.ItemSpacing.x);
    const bool can_prev = app.can_select_relative(-1);
    begin_disabled_button(!can_prev);
    if (ImGui::Button("<##prevsong", ImVec2(button, button))) app.select_relative(-1);
    end_disabled_button(!can_prev);
    hint("Previous song in the list");
    ImGui::SameLine();
    const bool can_next = app.can_select_relative(1);
    begin_disabled_button(!can_next);
    if (ImGui::Button(">##nextsong", ImVec2(button, button))) app.select_relative(1);
    end_disabled_button(!can_next);
    hint("Next song in the list");
    ImGui::SameLine();
    if (ImGui::Button("X##closepanel", ImVec2(button, button))) app.show_details = false;
    hint("Close (Esc)");

    ImGui::SetCursorPos(ImVec2(left_x, top_y));
    const store::ChartLibraryEntry& song = *app.selected;
    ImGui::PushFont(nullptr, 26.0f);
    text_ellipsized(app::strip_rich_tags(song.title).c_str(), text_w);
    ImGui::PopFont();
    std::string byline = app::strip_rich_tags(song.artist);
    const std::string charter = app::strip_rich_tags(song.charter);
    if (!charter.empty()) byline += " \xC2\xB7 charted by " + charter;
    ImGui::PushStyleColor(ImGuiCol_Text, kSubtleTextColor);
    text_ellipsized(byline.c_str(), text_w);
    ImGui::PopStyleColor();
}

// The optimal score and path in gold with one line of facts under it, and the
// analyze button at the right. Before a Ready record, the same place says why
// there is nothing to show. Stars and the hardest squeeze are the stored
// summary's (T7), never worked out again here.
void render_headline(AppState& app) {
    const store::RecordStatus status = app.viewed.status;
    const char* label = status == store::RecordStatus::NotAnalyzed ? "Analyze this song"
                                                                   : "Re-analyze";
    const float button_w = button_slot_width("Analyze this song");  // the wider label
    const float left_x = ImGui::GetCursorPosX();
    const float top_y = ImGui::GetCursorPosY();
    const float text_w =
        ImGui::GetContentRegionAvail().x - button_w - ImGui::GetStyle().ItemSpacing.x;

    ImGui::SetCursorPosX(left_x + text_w + ImGui::GetStyle().ItemSpacing.x);
    const bool file_ok = app.selected_file_ok(ImGui::GetTime());
    const bool busy = app.analyze_running();
    const bool off = app.analysis_blocked() || !file_ok || busy;
    begin_disabled_button(off);
    if (button_in_slot(label, button_w)) app.start_analyze();
    end_disabled_button(off);
    if (busy && !app.analyze_job_shown() &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Wait for %s to finish analyzing.",
                          app.analyze_job->song().title.c_str());

    ImGui::SetCursorPos(ImVec2(left_x, top_y));
    ImGui::BeginGroup();
    if (status == store::RecordStatus::NotAnalyzed) {
        ImGui::TextDisabled("Not analyzed yet.");
    } else if (status == store::RecordStatus::Stale) {
        ImGui::PushTextWrapPos(left_x + text_w);
        ImGui::TextColored(kWarningColor,
                           "Out of date: this result came from another Hydra version or "
                           "from different rules in hydra_rules.ini. Re-analyze to refresh it.");
        ImGui::PopTextWrapPos();
    } else if (app.viewed.record->paths.empty()) {
        ImGui::TextUnformatted("No paths found.");
    } else {
        const Path& best = app.viewed.record->best_path();
        ImGui::PushStyleColor(ImGuiCol_Text, kBestPathColor);
        ImGui::PushFont(nullptr, 40.0f);
        ImGui::TextUnformatted(group_thousands(best.totalscore()).c_str());
        ImGui::PopFont();
        ImGui::SameLine(0.0f, px(18.0f));
        ImGui::PushFont(g_mono_font, 24.0f);
        text_ellipsized(best.pathstring().c_str(), left_x + text_w - ImGui::GetCursorPosX());
        ImGui::PopFont();
        ImGui::PopStyleColor();

        const store::PathSummary& summary = app.viewed_summary;
        std::string facts = "Optimal path";
        if (summary.stars)
            facts += " \xC2\xB7 " + std::to_string(*summary.stars) +
                     (*summary.stars == 1 ? " star" : " stars");
        ImGui::PushStyleColor(ImGuiCol_Text, kSubtleTextColor);
        if (summary.hardest_ms) {
            facts += " \xC2\xB7 hardest squeeze";
            ImGui::TextUnformatted(facts.c_str());
            ImGui::SameLine();
            char ms[32];
            std::snprintf(ms, sizeof(ms), "%.1f ms", *summary.hardest_ms);
            ImGui::TextColored(kWarningColor, "%s", ms);
        } else {
            facts += " \xC2\xB7 no squeezes";
            ImGui::TextUnformatted(facts.c_str());
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
}

// The lines that say why analysis can't run, each with its remedy.
void render_panel_notices(AppState& app) {
    // The error is a warning line with a remedy, not the button's label.
    if (!app.selected_file_ok(ImGui::GetTime())) {
        ImGui::TextColored(kWarningColor, "Song file not found.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Rescan library")) {
            app.request_scan = true;   // the main window starts the scan
            app.show_details = false;  // tick() runs close_details() on the edge
        }
    }
    if (app.analysis_blocked())
        ImGui::TextColored(kWarningColor,
                           "Analysis is off until hydra_rules.ini is fixed and Hydra is "
                           "restarted.");
}
```

In `render_record_state`, the progress box now shows only for this song's job, and the stale line names both causes. Replace

```cpp
    if (app.analyze_job) {
        render_analyze_progress(app);
        return false;
    }
```

with

```cpp
    if (app.analyze_job_shown()) {
        render_analyze_progress(app);
        return false;
    }
```

and replace the stale message

```cpp
        ImGui::TextColored(
            kWarningColor,
            "This record is out of date. To make sure you have the latest "
            "results, please re-analyze.");
```

with

```cpp
        ImGui::TextColored(kWarningColor,
                           "Out of date: this result came from another Hydra version or "
                           "from different rules in hydra_rules.ini. Re-analyze to refresh it.");
```

In `render_analyze_progress`, change the comment `// An Auto-cap run can take minutes; the user needs an out that` / `// isn't killing the app.` to `// A long chart can take a while; the user needs an out that isn't` / `// killing the app.`. Its failure branch shows T4's plain `message()` with the raw `error()` as a small detail line. Replace

```cpp
    } else if (!job->ok()) {
        ImGui::TextColored(kWarningColor, "An error occurred:");
        ImGui::TextWrapped("%s", job->error().c_str());
        if (ImGui::Button("Continue")) app.analyze_job.reset();
```

with

```cpp
    } else if (!job->ok()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
        ImGui::TextWrapped("%s", job->message().c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", job->error().c_str());
        if (ImGui::Button("Continue")) app.analyze_job.reset();
```

and in the store-error branch below it, drop the `"An error occurred:"` line and keep the `TextWrapped` of `store_error` in `kWarningColor` (it is already plain: Step 4 builds it with `plain_error`).

Replace `void render_details_modal(AppState& app)` whole with `render_song_panel`. Write it with the Write tool into `details_panel.cpp.new`-style only if the Edit hook refuses; the tab bar in the middle is today's code unchanged:

```cpp
void render_song_panel(AppState& app) {
    if (!app.selected) return;

    // All of this panel's own state lives on AppState (DetailsViewState).
    const Path*& selected_path = app.details_ui.selected_path;
    GenerationWatcher& record_watcher = app.details_ui.record_watcher;

    // selected_path points into app.viewed's record, so it is only valid for
    // the record generation it was chosen under. Re-sync wherever the record
    // may have changed: at the top of the frame, and after the header, whose
    // previous / next buttons swap the song in the middle of this frame.
    auto sync_selected_path = [&] {
        if (record_watcher.changed(app.record_generation)) {
            selected_path = app.viewed.status == store::RecordStatus::Ready &&
                                    !app.viewed.record->paths.empty()
                                ? &app.viewed.record->best_path()
                                : nullptr;
        }
    };
    sync_selected_path();

    // Escape closes the panel like its X -- but not while a popup (a
    // confirm, an open combo) or a text box has the key.
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        app.show_details = false;

    // Ctrl+C copies the selected path -- unless a text input has focus.
    if (selected_path && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
        ImGui::SetClipboardText(
            selected_path->pathstring_verbose(app.viewed.record->multsqueezes).c_str());

    render_panel_header(app);
    if (!app.selected) return;
    sync_selected_path();
    ImGui::Spacing();
    render_headline(app);
    render_panel_notices(app);
    ImGui::Spacing();

    // The tabs, as before. The Preview only renders while its tab is the
    // active one; leaving it pauses playback.
    if (ImGui::BeginTabBar("##DetailsTabs")) {
        if (ImGui::BeginTabItem("Paths")) {
            if (render_record_state(
                    app, "After analyzing this song, paths will show up here."))
                render_path_panel(app, selected_path);
            ImGui::EndTabItem();
        }

        bool preview_shown = false;
        if (ImGui::BeginTabItem("Preview")) {
            preview_shown = true;
            render_preview_panel(app, selected_path);
            ImGui::EndTabItem();
        }
        if (!preview_shown && app.preview) app.preview->pause();

        if (ImGui::BeginTabItem("Dynamics")) {
            render_dynamics_panel(app);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stars")) {
            render_stars_panel(app);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
```

The tab bodies are whatever T1 named them (`render_path_panel`, `render_preview_panel`, `render_dynamics_panel`, `render_stars_panel`); call them by the names in `details_parts.h`. `app.reap_dynamics()` is no longer called here; `tick()` does it.

In `src/ui/details_view.h`, replace the top comment and the declaration with:

```cpp
// The song panel docked beside the library: the song's title and byline,
// previous / next / close, the optimal score and path, the analyze button,
// and the Paths / Preview / Dynamics / Stars tabs. Drawn inside the main
// window's ##songpanel child by render_main_window.
```

and `void render_song_panel(AppState& app);`. In `src/ui/details_parts.h`, delete a declaration of `update_analyze_job` if T1 put one there.

- [ ] **Step 10: Lay out the main window.** In `src/ui/app_shell.cpp`, `run_frame`, replace

```cpp
    render_main_window(app);
    render_details_modal(app);
```

with

```cpp
    // State first, once per frame: the panel's closing edge, storing and
    // reaping the analyze job, a finished Dynamics count.
    app.tick(ImGui::GetTime());
    render_main_window(app);
```

In `src/ui/library_view.cpp`, add `#include "ui/details_view.h"`, `#include <cfloat>`, and these two functions in the file's anonymous namespace, above `render_main_window`:

```cpp
// Today's library block: the title with its counts, the search box, and the
// table or the empty-library message. T12 replaces this body with its pane.
void render_library_pane(AppState& app) {
    char libtitle[96];
    if (!app.search.empty())
        std::snprintf(libtitle, sizeof(libtitle), "Library (%lld of %lld chart%s)",
                      (long long)app.current_page.total_count, (long long)app.library_total,
                      app.library_total == 1 ? "" : "s");
    else
        std::snprintf(libtitle, sizeof(libtitle), "Library (%lld chart%s)",
                      (long long)app.library_total, app.library_total == 1 ? "" : "s");
    ImGui::SeparatorText(libtitle);
    render_search_box(app);
    ImGui::Spacing();

    float footer_h = ImGui::GetFrameHeightWithSpacing();
    float header_h = ImGui::GetFrameHeightWithSpacing();
    float row_h = ImGui::GetTextLineHeightWithSpacing();
    float avail = ImGui::GetContentRegionAvail().y - footer_h - header_h;
    int visible_rows = std::clamp((int)(avail / row_h), 5, 200);
    app.set_rows_per_page(visible_rows);

    if (app.current_page.total_count > 0) {
        render_library_table(app, visible_rows);
    } else if (app.library_total > 0) {
        ImGui::TextUnformatted("No charts match your search.");
    } else {
        ImGui::TextUnformatted(
            "No songs scanned. Click \"Manage folders...\" to add your song folder, "
            "then \"Scan library\" to get started!");
    }
}

// The library and, when a song is open, the song panel beside it. The
// library's right edge drags (ImGuiChildFlags_ResizeX, width saved in
// hydra_ui.ini); with no song open the library takes the full width.
void render_library_and_panel(AppState& app) {
    LibraryViewState& ui = app.library_ui;
    const bool panel = app.details_open();
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float min_library = px(320.0f);
    const float min_panel = px(480.0f);
    if (panel) {
        const float max_library = std::max(min_library, avail_w - min_panel);
        ImGui::SetNextWindowSizeConstraints(ImVec2(min_library, 0.0f),
                                            ImVec2(max_library, FLT_MAX));
        // Reopening: the full-width frames while closed replaced the live
        // width, so put back the one the user left.
        if (!ui.panel_was_open && ui.library_w > 0.0f)
            ImGui::SetNextWindowSize(
                ImVec2(std::clamp(ui.library_w, min_library, max_library), 0.0f),
                ImGuiCond_Always);
        ImGui::BeginChild("##library", ImVec2(avail_w * 0.4f, 0.0f), ImGuiChildFlags_ResizeX);
        ui.library_w = ImGui::GetWindowWidth();
    } else {
        // No resize flag: ImGui marks this frame's size NoSavedSettings, so
        // the saved split survives a session that ends with the panel shut.
        ImGui::BeginChild("##library", ImVec2(0.0f, 0.0f));
    }
    ui.panel_was_open = panel;
    render_library_pane(app);
    ImGui::EndChild();

    if (panel) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelBg);
        ImGui::BeginChild("##songpanel", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleColor();
        render_song_panel(app);
        ImGui::EndChild();
    }
}
```

In `render_main_window`, delete the block that reaps a cancelled analysis (it starts `// A cancelled single-chart analysis has nothing to report; reap it here`); `tick()` does that now. Replace everything from `render_actions_row(app);` down to, but not including, `if (app.scan_job) render_scan_modal(app);` with:

```cpp
    render_actions_row(app);
    // T13: batch strips (the Batch mockup puts them between the toolbar and
    // the settings bar)
    render_settings_bar(app);
    render_library_and_panel(app);
```

That removes the old library title, the `render_view_controls` call, the search box, the "Open path report" block (now in the toolbar) and the table code from `render_main_window`; they live in `render_library_pane` and `render_actions_row` now. The request-scan block's comment says "The details modal's"; change it to "The song panel's".

- [ ] **Step 11: Build and run.** From Bash, build `Hydra`, `hydra_tests` and `hydra_uitest`. Run `.\build-cpp\Release\hydra_tests.exe` (`Status: SUCCESS!`), the Verify command (five `[PASS]`), and every test registered in `uitest_details.cpp` by name. Then run `--all` once and write down which failures are in other files. Tests in `uitest_library.cpp`, `uitest_preview.cpp` and `uitest_batch_reports.cpp` that click `Analyze paths!`, `Scan charts`, `Refresh scan` or `##spcapvalue`, use `//$FOCUSED`, or check the "Song Details" title are moved to the new labels by T11, T12 and T13 in this wave; list the failing names in your report so the main session can check them after the merge.

- [ ] **Step 12: Commit.**

```powershell
git add src/ui/settings_bar.cpp src/ui/library_view.cpp src/ui/library_parts.h src/ui/library_toolbar.cpp src/ui/details_panel.cpp src/ui/details_view.h src/ui/details_parts.h src/ui/app_state.h src/ui/app_state.cpp src/ui/app_shell.cpp src/ui/widgets.h CMakeLists.txt tests/test_song_panel_state.cpp tests/ui/uitest_details.cpp tests/ui/uitest_harness.h tests/ui/uitest_harness.cpp
git commit -m "Song panel beside the library, one Analysis settings bar that locks while analyzing

Task: Task 9 - song panel and settings bar
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: The Paths tab

Today the Paths tab is a 600 px path list beside a stack of tree nodes. Every score gets its own tree node, every activation is a closed tree node headed by a tab-aligned line such as `3-    (3 SP)       m32.1.0     163.0ms`, and the squeeze reads "SqOut: Note timing must be later than 163.0ms." The fixed list width clips the "squeezed out" warning on a narrow window. This task redraws the tab from the Main mockup. The path list sits on the left as buttons under three headings: Optimal, "Within 2 scores" and "Best at 0 ms limit". The chosen path's activations sit on the right: a small timeline, then one line per activation with its number, notation, measure, bars and a badge when it needs a squeeze. Opening a row shows its chord, the plain squeeze sentence, and a folded "Backend timings" table. Multiplier squeeze and Score breakdown fold away under the list, next to Copy path. The Backend limit setting moves here from the old controls panel. Everything drawn comes from Task 8's view data; this file only lays it out. The old view fields Task 8 kept for the old tab are deleted.

**Wave:** 3. **Depends on:** Task 1 (`paths_tab.cpp` exists), Task 8 (the view data), Task 6 (the wave-3 base). **Expected overlaps:** Task 9 in `src/ui/details_parts.h` (Task 10 changes only the Paths-tab declarations) and in `tests/ui/uitest_tests.cpp` (one registration line each; keep both). Task 11 in `tests/ui/uitest_tests.cpp` (the same). `src/app/path_view.{h,cpp}` and `tests/test_path_view.cpp` are Task 8's files from wave 2; nobody else edits them in wave 3.

**Goal:** The Paths tab matches the Main mockup and is driven by `hydra_uitest` through the label contract.

**Files:**
- Modify: `src/ui/paths_tab.cpp` (rewritten whole; written to `paths_tab.cpp.new` and moved over, because most of the file goes)
- Modify: `src/ui/details_parts.h` (the Paths-tab declarations only)
- Modify: `src/app/path_view.h`, `src/app/path_view.cpp` (delete the old fields Task 8 left)
- Modify: `tests/test_path_view.cpp` (move the old assertions onto the new fields)
- Create: `tests/ui/uitest_paths.cpp`
- Modify: `tests/ui/uitest_tests.cpp` (one registration line and its declaration)
- Modify: `CMakeLists.txt` (`tests/ui/uitest_paths.cpp` in `hydra_uitest_harness`)

**Acceptance Criteria:**
- [ ] `hydra_uitest --test paths-list` passes: on Burnout the screen shows `Optimal`, `Within 2 scores`, `Best at 0 ms limit`, `378,315 · 3- 1 2`, `hardest squeeze 163.0 ms` and `2,360 below optimal`, and clicking `##path1` selects `0 4 1`.
- [ ] `hydra_uitest --test paths-rows` passes: the summary `3 · 3 bars each · no SP left over`, rows `m32.1.0`, `m58.1.0`, `m88.1.0`, the badge `squeeze out 163 ms`, the sentence "Hit the [  Y  ] note more than 163.0 ms late…", `Show in Preview >##showact1` setting `preview_jump` to 0, one row open at a time, `Expand all` / `Collapse all`, and no "SqOut: Note timing" text anywhere.
- [ ] `hydra_uitest --test paths-backend-timings` passes: `Backend timings##act1` shows `Insane SqOut <-- squeezed out (-260)`; `Hide backend rows beyond##backendlimit` at 30 ms leaves `1 note near the SP end` and persists to the INI; values clamp at 500.
- [ ] `hydra_uitest --test paths-folds-copy` passes: `Multiplier squeeze##mult` shows `Hit [Red] first.`, `Score breakdown##breakdown` shows `Total Score:`, and `Copy path` puts the verbose path on the clipboard and shows `Copied!`.
- [ ] `hydra_uitest --test paths-uncounted` passes: the Tapestry chart shows `squeezed out (uncounted)` and "It costs no points, because Hydra's score never counted that note under Star Power".
- [ ] `hydra_tests.exe` ends `Status: SUCCESS!` after the old fields are deleted.
- [ ] `hydra_uitest --all` passes everything except, at most, three tests other wave-3 tasks own and rewrite: `backend-limit` and `squeezed_out_uncounted` (in `uitest_details.cpp`; Task 9 deletes them and this task's tests replace them) and `preview-path-overlay` (in `uitest_preview.cpp`; Task 11 rewrites it to pick paths by `##path<i>`). The reviewer checks that any of those three that fail do so only on an old label: `**/Activations` (no longer a tree node), a path row labelled by its pathstring, or `##backendlimitvalue` drawn twice while the old controls panel still exists in this worktree.

**Verify:** `.\build-cpp\Release\hydra_uitest.exe --test paths-list --test paths-rows --test paths-backend-timings --test paths-folds-copy --test paths-uncounted` → five `[PASS]` lines.

**Steps:**

- [ ] **Step 1: Write the failing GUI tests.** Create `tests/ui/uitest_paths.cpp`. It uses the shared helpers Task 1 moved out of `uitest_tests.cpp` and Task 9 updated for the panel (`scan_library`, `open_titled`, `analyze_open_song`); they keep those names. If Task 1 declared them in a header other than `uitest_harness.h`, include that header too.

```cpp
// The Paths tab's GUI tests, driven on Burnout (Green Day) from the scratch
// library: the path buttons, the activation rows and their folds, the backend
// table and its limit, the two folds under the list, and Copy path. Labels are
// the plan's label contract (docs/superpowers/plans/2026-09-27-ui-redesign.md).

#include <optional>
#include <string>

#include "uitest_harness.h"

#include "app/config.h"
#include "app/path_view.h"
#include "core/model.h"
#include "ui/app_state.h"

namespace uitest {

namespace {

// Keeps a test off the real Windows clipboard: while alive, ImGui reads and
// writes text() instead, and the previous handlers come back afterwards.
struct FakeClipboard {
    static std::string& text() {
        static std::string t;
        return t;
    }
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    const char* (*old_get)(ImGuiContext*) = pio.Platform_GetClipboardTextFn;
    void (*old_set)(ImGuiContext*, const char*) = pio.Platform_SetClipboardTextFn;
    FakeClipboard() {
        text().clear();
        pio.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
            return text().c_str();
        };
        pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* s) {
            text() = s ? s : "";
        };
    }
    ~FakeClipboard() {
        pio.Platform_GetClipboardTextFn = old_get;
        pio.Platform_SetClipboardTextFn = old_set;
    }
};

bool on_screen(Harness& h, const std::string& s) {
    return visible_text(h).find(s) != std::string::npos;
}

// A fresh app with Burnout analyzed and its Paths tab showing.
bool open_burnout(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return false;
    open_titled(ctx, "burnout", "Burnout");
    if (ctx->IsError()) return false;
    analyze_open_song(ctx);  // clicks the Paths tab first, then analyzes
    if (ctx->IsError()) return false;
    IM_CHECK_RETV(h.app->viewed.record->best_path().pathstring() == "3- 1 2", false);
    ctx->Yield(2);
    return true;
}

// The path list: three headings, the buttons' titles and detail lines, and a
// click that changes the shared selection.
void test_paths_list(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    const std::string dot = " \xC2\xB7 ";
    IM_CHECK(on_screen(h, "Optimal"));
    IM_CHECK(on_screen(h, "Within 2 scores"));
    IM_CHECK(on_screen(h, "Best at 0 ms limit"));
    IM_CHECK(on_screen(h, "378,315" + dot + "3- 1 2"));
    IM_CHECK(on_screen(h, "hardest squeeze 163.0 ms"));
    IM_CHECK(on_screen(h, "378,175" + dot + "0 4 1"));
    IM_CHECK(on_screen(h, "375,955" + dot + "0 0 0 0"));
    IM_CHECK(on_screen(h, "2,360 below optimal"));
    // The old list's headings are gone.
    IM_CHECK(!on_screen(h, "Optimal Path"));
    IM_CHECK(!on_screen(h, "More Paths"));

    IM_CHECK(h.app->details_ui.selected_path == &h.app->viewed.record->best_path());
    ctx->ItemClick("**/##path1");
    ctx->Yield(2);
    IM_CHECK(h.app->details_ui.selected_path != nullptr);
    IM_CHECK(h.app->details_ui.selected_path->pathstring() == "0 4 1");
    // A new path opens on its first row.
    IM_CHECK(h.app->details_ui.paths_tab.ui().act_open.size() == 3);
    IM_CHECK(h.app->details_ui.paths_tab.ui().act_open[0] == 1);
    ctx->ItemClick("**/##path3");
    ctx->Yield(2);
    IM_CHECK(h.app->details_ui.selected_path->pathstring() == "0 0 0 0");
}

// The activation rows: one line each, the first open, one open at a time,
// Expand all and Collapse all, and the plain sentence instead of the old line.
void test_paths_rows(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    const std::string dot = " \xC2\xB7 ";
    IM_CHECK(on_screen(h, "Activations"));
    IM_CHECK(on_screen(h, "3" + dot + "3 bars each" + dot + "no SP left over"));
    IM_CHECK(on_screen(h, "m32.1.0"));
    IM_CHECK(on_screen(h, "m58.1.0"));
    IM_CHECK(on_screen(h, "m88.1.0"));
    IM_CHECK(on_screen(h, "squeeze out 163 ms"));
    // Row 1 starts open: its chord and sentence show, rows 2 and 3 are shut.
    IM_CHECK(on_screen(h, "[Kick - GreenCym]"));
    IM_CHECK(on_screen(h, "Hit the [  Y  ] note more than 163.0 ms late so it lands after "
                          "Star Power ends."));
    IM_CHECK(on_screen(h, "3 notes near the SP end"));
    IM_CHECK(!on_screen(h, "6 notes near the SP end"));
    IM_CHECK(!on_screen(h, "SqOut: Note timing"));
    IM_CHECK(!on_screen(h, "Frontend:"));

    // "Show in Preview" asks the Preview for activation 1 (Task 11 consumes it).
    ctx->ItemClick("**/Show in Preview >##showact1");
    IM_CHECK(h.app->details_ui.paths_tab.ui().preview_jump == std::optional<size_t>(0));
    h.app->details_ui.paths_tab.ui().preview_jump.reset();

    ctx->ItemClick("**/##act2");  // opens row 2, closes row 1
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "6 notes near the SP end"));
    IM_CHECK(!on_screen(h, "3 notes near the SP end"));
    ctx->ItemClick("**/##act2");  // closes it again
    ctx->Yield(2);
    IM_CHECK(!on_screen(h, "6 notes near the SP end"));

    ctx->ItemClick("**/Expand all");
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "3 notes near the SP end"));
    IM_CHECK(on_screen(h, "6 notes near the SP end"));
    IM_CHECK(on_screen(h, "7 notes near the SP end"));
    IM_CHECK(h.app->details_ui.paths_tab.ui().all_open());
    ctx->ItemClick("**/Collapse all");
    ctx->Yield(2);
    IM_CHECK(!on_screen(h, "3 notes near the SP end"));
    IM_CHECK(!on_screen(h, "7 notes near the SP end"));
}

// The backend table folds per activation, and the Backend limit moved here.
void test_paths_backend_timings(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    IM_CHECK(!on_screen(h, "Insane SqOut"));
    ctx->ItemClick("**/Backend timings##act1");
    ctx->Yield(2);
    IM_CHECK(h.app->details_ui.paths_tab.ui().backends_open[0] == 1);
    IM_CHECK(on_screen(h, "Insane SqOut <-- squeezed out (-260)"));
    IM_CHECK(on_screen(h, "Timing is how far each note sits from the Star Power end"));

    // Off by default, and the number box is inert until it is ticked.
    IM_CHECK(!h.app->settings.backendlimit_enabled);
    IM_CHECK_EQ(h.app->settings.backendlimit_value, 50);
    IM_CHECK((ctx->ItemInfo("**/##backendlimitvalue").ItemFlags & ImGuiItemFlags_Disabled) != 0);
    ctx->ItemClick("**/Hide backend rows beyond##backendlimit");
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_enabled; }, 5));
    IM_CHECK(hydra::app::Settings::load_file(h.ini_path).backendlimit_enabled);

    // At 30 ms the -489.1 and -326.1 rows go; the squeezed-out -163.0 row stays.
    ctx->ItemInputValue("**/##backendlimitvalue", 30);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_value == 30; }, 5));
    IM_CHECK_EQ(hydra::app::Settings::load_file(h.ini_path).backendlimit_value, 30);
    IM_CHECK(wait_until(ctx, [&] { return on_screen(h, "1 note near the SP end"); }, 5));
    IM_CHECK(on_screen(h, "squeezed out (-260)"));

    // The full engine window (500 ms) is reachable; beyond it clamps back.
    ctx->ItemInputValue("**/##backendlimitvalue", 500);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_value == 500; }, 5));
    ctx->ItemInputValue("**/##backendlimitvalue", 600);
    IM_CHECK(wait_until(ctx, [&] { return h.app->settings.backendlimit_value == 500; }, 5));

    // Display only: the record is still the analyzed one. Unticking restores
    // every row and persists too.
    IM_CHECK(h.app->viewed.status == hydra::store::RecordStatus::Ready);
    ctx->ItemClick("**/Hide backend rows beyond##backendlimit");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->settings.backendlimit_enabled; }, 5));
    IM_CHECK(!hydra::app::Settings::load_file(h.ini_path).backendlimit_enabled);
    IM_CHECK(wait_until(ctx, [&] { return on_screen(h, "3 notes near the SP end"); }, 5));
}

// The two folds under the list, and Copy path with its "Copied!" flash.
void test_paths_folds_copy(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout(ctx)) return;
    FakeClipboard clipboard;

    IM_CHECK(on_screen(h, "+15"));  // beside the Multiplier squeeze fold
    IM_CHECK(!on_screen(h, "Hit [Red] first."));
    ctx->ItemClick("**/Multiplier squeeze##mult");
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "Hit [Red] first."));
    IM_CHECK(on_screen(h, "2x   (+15 pts):   [Red - YellowCym]"));

    IM_CHECK(!on_screen(h, "Total Score:"));
    ctx->ItemClick("**/Score breakdown##breakdown");
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "Total Score:"));
    IM_CHECK(on_screen(h, "Avg. Multiplier:"));
    ctx->ItemClick("**/Score breakdown##breakdown");
    ctx->Yield(2);
    IM_CHECK(!on_screen(h, "Total Score:"));

    IM_CHECK(!on_screen(h, "Copied!"));
    ctx->ItemClick("**/Copy path");
    ctx->Yield(2);
    const hydra::HydraRecord& rec = *h.app->viewed.record;
    IM_CHECK_STR_EQ(FakeClipboard::text().c_str(),
                    rec.best_path().pathstring_verbose(rec.multsqueezes).c_str());
    IM_CHECK(on_screen(h, "Copied!"));
}

// A squeezed-out row the engine never counted: the table says "(uncounted)"
// and the sentence says it costs nothing. Found by the skipped doctest "find a
// chart with an uncounted squeezed-out row" (tests/test_path_view.cpp).
void test_paths_uncounted(ImGuiTestContext* ctx) {
    static const char* kTitle = "Tapestry of the Starless Abstract (Shortened)";
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    open_titled(ctx, "tapestry", kTitle);
    if (ctx->IsError()) return;
    analyze_open_song(ctx);
    if (ctx->IsError()) return;
    ctx->ItemClick("**/Expand all");
    ctx->Yield(2);
    const size_t rows = h.app->details_ui.paths_tab.ui().act_open.size();
    IM_CHECK(rows > 0);
    for (size_t i = 1; i <= rows; ++i) {
        ctx->ItemClick(("**/Backend timings##act" + std::to_string(i)).c_str());
        ctx->Yield(1);
    }
    ctx->Yield(2);
    IM_CHECK(on_screen(h, "squeezed out (uncounted)"));
    IM_CHECK(on_screen(h, "It costs no points, because Hydra's score never counted that "
                          "note under Star Power"));
}

}  // namespace

// Registers this file's tests; register_tests() (uitest_tests.cpp) calls it.
void register_paths_tests(Harness& h) {
    struct Entry {
        const char* name;
        void (*fn)(ImGuiTestContext*);
    };
    const Entry entries[] = {
        {"paths-list", test_paths_list},
        {"paths-rows", test_paths_rows},
        {"paths-backend-timings", test_paths_backend_timings},
        {"paths-folds-copy", test_paths_folds_copy},
        {"paths-uncounted", test_paths_uncounted},
    };
    for (const Entry& e : entries) {
        ImGuiTest* t = IM_REGISTER_TEST(h.engine, "hydra", e.name);
        t->UserData = &h;
        t->TestFunc = e.fn;
    }
}

}  // namespace uitest
```

The "sentence" check in `paths-rows` breaks the approved sentence across two string literals only for line length; the joined text is the exact sentence up to "Star Power ends.".

Register it. In `tests/ui/uitest_tests.cpp`, just above `void register_tests(Harness& h) {`, add `void register_paths_tests(Harness& h);  // uitest_paths.cpp`. As the last statement inside `register_tests`, add `register_paths_tests(h);`. If Task 1's `register_tests` walks per-file tables of a different shape, keep that shape for the other files and add this one call next to them; the registration is the same either way.

In `CMakeLists.txt`, in `add_library(hydra_uitest_harness STATIC ...)`, add `tests/ui/uitest_paths.cpp` after the last `tests/ui/uitest_*.cpp` line Task 1 put there.

- [ ] **Step 2: Run them and watch them fail.** Build with `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_uitest > build.log 2>&1` from the Bash tool, then `.\build-cpp\Release\hydra_uitest.exe --test paths-list --test paths-rows`. Expected: both `[FAIL]`, the log naming a missing text such as `Within 2 scores` or the missing item `**/##path1`.

- [ ] **Step 3: Rewrite `src/ui/paths_tab.cpp`.** Write the whole file below to `src/ui/paths_tab.cpp.new` with the Write tool, then `Move-Item -Force src\ui\paths_tab.cpp.new src\ui\paths_tab.cpp`. Keep Task 1's namespace for the shared functions: `render_path_panel` and `copy_selected_path` go in the namespace `details_parts.h` declares them in (`hydra::ui::detail`, per Task 1's brief); if Task 1 used a different name, use Task 1's. The file's own helpers stay in an anonymous namespace.

```cpp
// The Paths tab: the path list on the left, the chosen path's activations on
// the right, the multiplier squeeze and score breakdown folds, Copy path and
// the backend-row limit. Every string comes from app::PathsTabCache
// (app/path_view.h); this file only lays it out. What is unfolded lives in
// PathsTabCache::ui(), on AppState, never in a static here.
//
// Text is drawn with ImGui's Text calls, not the draw list, so hydra_uitest's
// visible_text sees it. Rows are a full-width Selectable with the text laid
// over it; only decoration (arrows, circles, the timeline) uses the draw list.

#include "ui/details_parts.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

#include "app/path_view.h"
#include "core/model.h"
#include "imgui.h"
#include "imgui_internal.h"  // RenderArrow
#include "ui/app_state.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace hydra::ui::detail {

namespace {

ImVec4 text_color() { return ImGui::GetStyleColorVec4(ImGuiCol_Text); }
ImVec4 dim_color() { return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled); }

// Draw `text` at `pos` over an item already submitted. `font` null keeps the
// current font.
void text_at(const ImVec2& pos, const ImVec4& color, const char* text,
             ImFont* font = nullptr) {
    ImGui::SetCursorScreenPos(pos);
    if (font) ImGui::PushFont(font, 0.0f);
    ImGui::TextColored(color, "%s", text);
    if (font) ImGui::PopFont();
}

// After text laid over a block that starts at `top` and is `h` tall: put the
// cursor under the block and submit an empty item there, so the layout goes
// on from a real item (a bare SetCursorScreenPos asserts at the window's end).
void end_overlay(const ImVec2& top, float h) {
    ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + h));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// Put the next item flush right on the current line, `w` wide.
void align_right(float w) {
    ImGui::SameLine();
    const float room = ImGui::GetContentRegionAvail().x - w;
    if (room > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room);
}

float button_width(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

// ---- the path list ----------------------------------------------------------

// One path: a full-width Selectable with the id ##path<i>, the title over it
// (gold for an optimal path) and the detail line under the title.
bool path_button(size_t i, const app::PathButtonView& b, bool selected) {
    const float pad = px(6.0f);
    const float gap = px(2.0f);
    const float line = ImGui::GetTextLineHeight();
    const float h = pad * 2.0f + line + (b.detail.empty() ? 0.0f : gap + line);
    char id[32];
    std::snprintf(id, sizeof(id), "##path%zu", i);
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable(id, selected, ImGuiSelectableFlags_None, ImVec2(0.0f, h));
    // The list is narrow; the full title is a hover away.
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal);
    const bool optimal = b.group == app::PathButtonView::Group::Optimal;
    text_at(ImVec2(top.x + pad, top.y + pad), optimal ? kBestPathColor : text_color(),
            b.title.c_str(), g_mono_font);
    if (!b.detail.empty())
        text_at(ImVec2(top.x + pad, top.y + pad + line + gap),
                b.detail_warn ? kWarningColor : dim_color(), b.detail.c_str());
    end_overlay(top, h);
    if (hovered) ImGui::SetTooltip("%s", b.title.c_str());
    return clicked;
}

// The buttons under their headings: Optimal, the "Within N" group, and
// "Best at 0 ms limit". A heading is drawn where the group changes.
void render_path_list(const app::PathButtonsView& list, const Path*& selected_path) {
    using Group = app::PathButtonView::Group;
    for (size_t i = 0; i < list.buttons.size(); ++i) {
        const app::PathButtonView& b = list.buttons[i];
        if (i == 0 || b.group != list.buttons[i - 1].group) {
            if (i > 0) ImGui::Spacing();
            const char* heading = b.group == Group::Optimal  ? "Optimal"
                                  : b.group == Group::Within ? list.within_label.c_str()
                                                             : "Best at 0 ms limit";
            ImGui::TextDisabled("%s", heading);
        }
        if (path_button(i, b, b.path == selected_path)) selected_path = b.path;
    }
}

// ---- the activations --------------------------------------------------------

// The strip above the rows: a thin bar the column's width, a gold mark per
// activation at its song_fraction with its number above, and the first and
// last measure under the ends. Not drawn unless every row has a fraction.
void render_timeline(const app::ActivationsView& view) {
    for (const app::ActivationRowView& a : view.acts)
        if (!a.song_fraction) return;
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = px(40.0f);
    const ImVec2 top = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float bar_y = top.y + px(26.0f);
    dl->AddRectFilled(ImVec2(top.x, bar_y), ImVec2(top.x + w, bar_y + px(4.0f)),
                      IM_COL32(58, 58, 62, 255), px(2.0f));
    const float num_size = ImGui::GetFontSize() * 0.7f;
    for (const app::ActivationRowView& a : view.acts) {
        const float x = top.x + static_cast<float>(*a.song_fraction) * w;
        const ImVec2 m_min(x - px(2.0f), top.y + px(20.0f));
        const ImVec2 m_max(x + px(2.0f), top.y + px(36.0f));
        dl->AddRectFilled(m_min, m_max, ImGui::GetColorU32(kBestPathColor), px(1.0f));
        // A mark whose activation needs a squeeze gets the badge's outline.
        if (!a.badge.empty())
            dl->AddRect(ImVec2(m_min.x - px(2.0f), m_min.y - px(2.0f)),
                        ImVec2(m_max.x + px(2.0f), m_max.y + px(2.0f)),
                        ImGui::GetColorU32(kWarningColor), px(1.0f), 0, px(1.5f));
        const std::string num = std::to_string(a.number);
        const float nw = ImGui::GetFont()->CalcTextSizeA(num_size, FLT_MAX, 0.0f, num.c_str()).x;
        dl->AddText(ImGui::GetFont(), num_size, ImVec2(x - nw * 0.5f, top.y + px(2.0f)),
                    ImGui::GetColorU32(a.badge.empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text),
                    num.c_str());
    }
    ImGui::PushFont(g_mono_font, 0.0f);
    ImGui::TextDisabled("m1");
    align_right(ImGui::CalcTextSize(view.timeline_end.c_str()).x);
    ImGui::TextDisabled("%s", view.timeline_end.c_str());
    ImGui::PopFont();
}

// One activation's line: a full-width Selectable (##act<number>) with the fold
// arrow, the number in a circle, the notation, the measure, the bars and the
// badge laid over it. A click opens it alone, or closes it.
void render_activation_row(size_t i, const app::ActivationRowView& a, app::PathsTabUi& ui) {
    const bool open = i < ui.act_open.size() && ui.act_open[i];
    const float h = px(34.0f);
    char id[32];
    std::snprintf(id, sizeof(id), "##act%d", a.number);
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    if (ImGui::Selectable(id, open, ImGuiSelectableFlags_None, ImVec2(0.0f, h))) ui.click_row(i);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line = ImGui::GetTextLineHeight();
    const float text_y = top.y + (h - line) * 0.5f;
    ImGui::RenderArrow(dl, ImVec2(top.x + px(4.0f), text_y + line * 0.15f),
                       ImGui::GetColorU32(ImGuiCol_TextDisabled),
                       open ? ImGuiDir_Down : ImGuiDir_Right, 0.7f);
    const ImVec2 dot(top.x + px(32.0f), top.y + h * 0.5f);
    dl->AddCircleFilled(dot, px(10.0f), IM_COL32(58, 58, 62, 255));
    const std::string num = std::to_string(a.number);
    text_at(ImVec2(dot.x - ImGui::CalcTextSize(num.c_str()).x * 0.5f, text_y), text_color(),
            num.c_str());
    text_at(ImVec2(top.x + px(52.0f), text_y), kBestPathColor, a.notation.c_str(), g_mono_font);
    text_at(ImVec2(top.x + px(104.0f), text_y), text_color(), a.measure.c_str(), g_mono_font);
    text_at(ImVec2(top.x + px(200.0f), text_y), dim_color(), a.bars.c_str());
    if (!a.badge.empty()) {
        const ImVec2 sz = ImGui::CalcTextSize(a.badge.c_str());
        const float bx = top.x + width - sz.x - px(14.0f);
        const ImVec2 b_min(bx - px(8.0f), text_y - px(2.0f));
        const ImVec2 b_max(bx + sz.x + px(8.0f), text_y + sz.y + px(2.0f));
        dl->AddRectFilled(b_min, b_max, IM_COL32(51, 38, 26, 255), px(9.0f));
        dl->AddRect(b_min, b_max, IM_COL32(106, 69, 32, 255), px(9.0f));
        text_at(ImVec2(bx, text_y), a.difficult ? kWarningColor : dim_color(), a.badge.c_str());
    }
    end_overlay(top, h);
}

// One squeeze sentence in its warm box; the border turns warning-coloured
// when the squeeze is difficult.
void squeeze_box(int act_number, size_t k, const app::TextLine& s) {
    char id[32];
    std::snprintf(id, sizeof(id), "##sq%d_%zu", act_number, k);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(51 / 255.0f, 38 / 255.0f, 26 / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          s.warn ? kWarningColor
                                 : ImVec4(106 / 255.0f, 69 / 255.0f, 32 / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(232 / 255.0f, 214 / 255.0f, 196 / 255.0f, 1.0f));
    ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(s.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor(3);
}

// The backend rows of one activation, in the columns the old table had.
void render_backend_table(const app::ActivationRowView& a) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", app::kBackendTimingsLead);
    ImGui::PopTextWrapPos();
    if (a.backends.empty()) {
        ImGui::TextDisabled("None.");
        return;
    }
    char id[32];
    std::snprintf(id, sizeof(id), "##backends%d", a.number);
    ImGui::PushFont(g_mono_font, 0.0f);
    if (ImGui::BeginTable(id, 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Timing", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("Chord", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("Rating", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const app::BackendRowView& row : a.backends) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.timing.c_str());
            if (!row.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", row.tooltip.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row.chord.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(row.points.c_str());
            ImGui::TableSetColumnIndex(3);
            if (row.warn) ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
            ImGui::TextUnformatted(row.rating.c_str());
            if (row.warn) ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    ImGui::PopFont();
}

// What an opened row shows: the chord and "Show in Preview", the calibration
// line, the squeeze sentences, the scale and overfill notes with their hover
// hints, and the folded backend table.
void render_activation_body(size_t i, const app::ActivationRowView& a, app::PathsTabUi& ui) {
    ImGui::Indent(px(48.0f));
    ImGui::TextDisabled("Chord");
    ImGui::SameLine();
    ImGui::PushFont(g_mono_font, 0.0f);
    ImGui::TextUnformatted(a.chord.c_str());
    ImGui::PopFont();
    char show[48];
    std::snprintf(show, sizeof(show), "Show in Preview >##showact%d", a.number);
    align_right(button_width("Show in Preview >"));
    if (ImGui::SmallButton(show)) ui.preview_jump = i;

    if (!a.calibration.empty()) ImGui::TextUnformatted(a.calibration.c_str());
    for (size_t k = 0; k < a.squeeze_sentences.size(); ++k)
        squeeze_box(a.number, k, a.squeeze_sentences[k]);
    if (!a.scale_warning.empty()) {
        {
            WarnColor warn;
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(a.scale_warning.c_str());
            ImGui::PopTextWrapPos();
        }
        hint(app::kTransferScaleHint);
    }
    if (!a.overfill_warning.empty()) {
        {
            WarnColor warn;
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(a.overfill_warning.c_str());
            ImGui::PopTextWrapPos();
        }
        hint(app::kOverfillHint);
    }

    char label[48];
    std::snprintf(label, sizeof(label), "Backend timings##act%d", a.number);
    const bool tracked = i < ui.backends_open.size();
    if (tracked) ImGui::SetNextItemOpen(ui.backends_open[i] != 0, ImGuiCond_Always);
    const bool backends = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_NoTreePushOnOpen);
    if (tracked) ui.backends_open[i] = backends ? 1 : 0;
    ImGui::SameLine();
    ImGui::TextDisabled("\xC2\xB7 %s", a.backends_label.c_str());
    if (backends) render_backend_table(a);
    ImGui::Unindent(px(48.0f));
    ImGui::Spacing();
}

// The right-hand column's top half: the heading and its summary, Expand all,
// the timeline, and every row with its body when open.
void render_activations(const app::ActivationsView& view, app::PathsTabUi& ui) {
    ImGui::TextUnformatted("Activations");
    if (!view.summary.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", view.summary.c_str());
    }
    if (view.acts.empty()) {
        ImGui::TextDisabled("None.");
        return;
    }
    const bool all = ui.all_open();
    const char* toggle = all ? "Collapse all" : "Expand all";
    align_right(button_width(toggle));
    if (ImGui::SmallButton(toggle)) ui.set_all(!all);

    render_timeline(view);
    ImGui::Spacing();
    for (size_t i = 0; i < view.acts.size(); ++i) {
        const app::ActivationRowView& a = view.acts[i];
        render_activation_row(i, a, ui);
        if (i < ui.act_open.size() && ui.act_open[i]) render_activation_body(i, a, ui);
    }
}

// ---- the folds, Copy path and the backend limit -----------------------------

// A fold toggle drawn as a button that looks pressed while open.
void fold_button(const char* label, bool& open) {
    const bool was_open = open;
    if (was_open) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(label)) open = !open;
    if (was_open) ImGui::PopStyleColor();
}

void render_path_footer(AppState& app, const app::PathsTabCache::Details& d,
                        app::PathsTabUi& ui) {
    ImGui::Spacing();
    fold_button("Multiplier squeeze##mult", ui.mult_open);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", app::multsqueeze_summary(d.squeezes).c_str());
    ImGui::SameLine(0.0f, px(16.0f));
    fold_button("Score breakdown##breakdown", ui.breakdown_open);
    ImGui::SameLine(0.0f, px(24.0f));
    if (ImGui::Button("Copy path")) copy_selected_path(app);
    hint("Ctrl+C also copies the selected path");
    const double copied_at = app.details_ui.copied_at;
    if (copied_at >= 0.0 && ImGui::GetTime() - copied_at < 2.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("Copied!");
    }

    if (ui.mult_open) {
        ImGui::PushFont(g_mono_font, 0.0f);
        if (d.squeezes.empty()) ImGui::TextDisabled("None.");
        for (const app::MultSqueezeView& msq : d.squeezes) {
            ImGui::TextUnformatted(msq.label.c_str());
            ImGui::Indent(px(24.0f));
            ImGui::TextUnformatted(msq.howto.c_str());
            ImGui::Unindent(px(24.0f));
        }
        ImGui::PopFont();
    }
    if (ui.breakdown_open) {
        ImGui::PushFont(g_mono_font, 0.0f);
        for (const std::string& line : d.breakdown) ImGui::TextUnformatted(line.c_str());
        ImGui::PopFont();
    }

    // The backend tables' display window. Purely a filter on what the tables
    // draw: it never reaches the search, so it never re-keys a stored record.
    ImGui::Spacing();
    if (ImGui::Checkbox("Hide backend rows beyond##backendlimit", &app.settings.backendlimit_enabled))
        app.commit_settings();
    ImGui::SameLine();
    const bool limit_off = !app.settings.backendlimit_enabled;
    begin_disabled_input(limit_off);
    ImGui::SetNextItemWidth(px(100.0f));
    if (ImGui::InputInt("##backendlimitvalue", &app.settings.backendlimit_value)) {
        app.settings.backendlimit_value = std::clamp(app.settings.backendlimit_value, 0, 500);
        app.commit_settings();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("ms");
    end_disabled_input(limit_off);
    hint("Hides backend rows beyond +/- this many ms; squeezed-out notes always "
         "show. Display only: changing it never re-analyzes.");
}

}  // namespace

void copy_selected_path(AppState& app) {
    const Path* path = app.details_ui.selected_path;
    if (path == nullptr || !app.viewed.record) return;
    ImGui::SetClipboardText(path->pathstring_verbose(app.viewed.record->multsqueezes).c_str());
    app.details_ui.copied_at = ImGui::GetTime();
}

void render_path_panel(AppState& app, const Path*& selected_path) {
    app::PathsTabCache& cache = app.details_ui.paths_tab;
    const HydraRecord& record = *app.viewed.record;
    const int generation = app.record_generation.n;
    const app::PathButtonsView& list =
        cache.buttons(record, generation, app.settings.depth_mode, app.settings.depth_value);

    ImGui::BeginChild("##pathlist", ImVec2(px(240.0f), 0.0f));
    render_path_list(list, selected_path);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, px(24.0f));
    ImGui::BeginChild("##pathdetails", ImVec2(0.0f, 0.0f));
    if (selected_path) {
        // details() first: a new path resets ui() before anything reads it.
        const app::PathsTabCache::Details& d = cache.details(
            *selected_path, record, generation,
            app.viewed.timing ? &*app.viewed.timing : nullptr,
            static_cast<double>(app.settings.hit_window_ms), app.settings.backend_limit(),
            app.settings.rules, app.viewed.song_length_ms);
        app::PathsTabUi& ui = cache.ui();
        render_activations(d.activations, ui);
        render_path_footer(app, d, ui);
    }
    ImGui::EndChild();
}

}  // namespace hydra::ui::detail
```

`app.viewed.song_length_ms` is the field Task 7 adds in Step 8a. If the main session decided against storing the length, replace `app.viewed.song_length_ms` with `std::nullopt` and the timeline is simply not drawn.

- [ ] **Step 4: Update `src/ui/details_parts.h`.** Keep the `render_path_panel` declaration Task 1 wrote. After it add:

```cpp
// Copy the selected path's verbose string (Path::pathstring_verbose) and
// start the "Copied!" flash. The Copy path button and Ctrl+C both call it.
void copy_selected_path(AppState& app);
```

Delete the declarations of any Paths-only helpers Task 1 made shared, if it did: `begin_section`, `end_section`, `warnable_text`, `render_multsqueeze_section`, `render_activations_section`, `render_score_breakdown_section`, `render_path_details`, `render_path_row`. Before deleting one, grep `src/ui` for its name; if any file other than `paths_tab.cpp` still calls it, keep the declaration and report it instead.

- [ ] **Step 5: Run the GUI tests and watch them pass.** Rebuild `hydra_uitest` the same way and run the Verify command. Expected: five `[PASS]` lines.

- [ ] **Step 6: Delete the old view fields.** Nothing draws them any more.

In `src/app/path_view.h`, in `ActivationRowView`, delete these lines:

```cpp
    // The old details modal's fields. Task 10 deletes these three.
    std::string header;  // "%-6s(%d SP)\t%9s" (+ "\t" and format_ms right-aligned in 9 when difficulty-rated)
    std::string frontend;       // "Frontend: ..."
    std::vector<TextLine> sqinouts;
```

and after the struct delete:

```cpp
// The old name, kept so the old Paths tab compiles until Task 10 replaces it.
using ActivationDetailsView = ActivationRowView;
```

In `ActivationsView` delete `std::vector<TextLine> footer;  // leftover SP, SP meter, skipped notes. Task 10 deletes it.` Delete the `PathRowView` struct and `PathRowView build_path_row(const Path& path);` with their comment. In `PathListView` delete `std::string more_label;  // "More Paths" (+ " (Path limit: N ms)")` and change its first comment line to `// Groups in traversal order.` In `class PathsTabCache` delete the `struct Row { ... };`, the `list()` and `row()` declarations with their comments, `int list_builds() const { return list_builds_; }`, and the private members `list_generation_`, `list_`, `rows_` and `list_builds_`. Drop `#include <unordered_map>`, which only `rows_` used.

In `src/app/path_view.cpp`, in `build_activations` delete the header block from `// The row layout, including the literal tabs:` through `av.header += buf;` and its closing `}` (keep `std::string ntn = ...` and `std::string meas = ...`, which the new fields read), delete `av.frontend = "Frontend: " + act.chord.rowstr();`, delete the two lines that fill `av.sqinouts`, and delete the footer block from `view.footer.push_back(` through the end of the `if (record.sp_cap) { ... }` block (Task 6's version of it). Delete `build_path_row`. In `build_path_list` delete the three `view.more_label` lines. Delete `PathsTabCache::list` and `PathsTabCache::row`.

The "SP meter: 4 bars." footer line goes with it. The record shown is always the one stored at the settings bar's SP cap, so the bar already says which cap it is.

In `tests/test_path_view.cpp`, move the old assertions onto the new fields:

In `TEST_CASE("build_activations: the calibration fill reads positive = early on both lines")`, replace `CHECK(av.header == "E0    (2 SP)\t   m1.1.0\t   12.3ms");` with:

```cpp
    CHECK(av.notation == "E0");
    CHECK(av.measure == "m1.1.0");
    CHECK(av.badge == "calibration fill 12 ms");
```

and `CHECK(av.header == "E1    (2 SP)\t   m1.1.0");` with:

```cpp
    CHECK(av.notation == "E1");
    CHECK(av.badge.empty());
```

Replace the whole `TEST_CASE("build_path_row: right-aligned ms, warn past the difficult floor")` with:

```cpp
TEST_CASE("path buttons: the hardest squeeze line, warn past the difficult floor") {
    auto detail_of = [](const Path& p) {
        HydraRecord rec;
        rec.paths.push_back(p);
        PathButtonsView v = build_path_buttons(rec, 0, 2);
        REQUIRE(v.buttons.size() == 1);
        return v.buttons[0];
    };
    CHECK(detail_of(Path{}).detail.empty());  // no activations, no difficulty

    Path hard;
    Activation act;
    act.skips = 0;
    act.e_offset = 300.0;  // not e-critical
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -12.5});
    hard.activations.push_back(act);
    PathButtonView b = detail_of(hard);
    CHECK(b.detail == "hardest squeeze 12.5 ms");
    CHECK(b.detail_warn);

    Path easy = hard;
    easy.activations[0].sqinouts[0].offset_ms = -1.5;
    b = detail_of(easy);
    CHECK(b.detail == "hardest squeeze 1.5 ms");
    CHECK_FALSE(b.detail_warn);
}
```

In `TEST_CASE("build_path_list: score groups and the all-0 dedupe rule")`, delete `CHECK(list.more_label == "More Paths (Path limit: 10 ms)");`.

In `TEST_CASE("build_activations: headers, footer, and backend rows line up")`, rename it `"build_activations: rows and backend rows line up"`, replace `CHECK(av.header.rfind(acts[i].notationstr(), 0) == 0);` with `CHECK(av.notation == acts[i].notationstr());`, replace `CHECK(av.sqinouts.size() == acts[i].sqinouts.size());` with `CHECK(av.squeeze_sentences.size() == acts[i].sqinouts.size());`, and replace the footer block

```cpp
    REQUIRE(!view.footer.empty());
    CHECK(view.footer[0].text ==
          "Leftover SP: " + std::to_string(best.leftover_sp) + ".");
    CHECK_FALSE(view.footer[0].warn);
```

with `CHECK(!view.summary.empty());`.

In the skipped `TEST_CASE("find a chart with an uncounted squeezed-out row" ...)`, replace `for (const ActivationDetailsView& av : v.acts)` with `for (const ActivationRowView& av : v.acts)` and `MESSAGE(path << " | " << av.header);` with `MESSAGE(path << " | activation " << av.number << " " << av.notation);`.

In `TEST_CASE("PathsTabCache: views are built once and rebuilt only when their inputs move")`, replace:

```cpp
    // The list: once per record generation, however many frames ask.
    for (int frame = 0; frame < 5; ++frame) cache.list(rec, 7);
    CHECK(cache.list_builds() == 1);
    const PathListView& list = cache.list(rec, 8);  // the record was re-read
    CHECK(cache.list_builds() == 2);

    // Every listed row carries the path's own label and ms cell.
    for (const PathGroupView& g : list.groups)
        for (const Path* p : g.paths) {
            CHECK(cache.row(p).label == p->pathstring());
            CHECK(cache.row(p).cell.ms == build_path_row(*p).ms);
        }
```

with:

```cpp
    // The buttons: once per record generation, however many frames ask.
    for (int frame = 0; frame < 5; ++frame) cache.buttons(rec, 7, 0, 10);
    CHECK(cache.buttons_builds() == 1);
    const PathButtonsView& list = cache.buttons(rec, 8, 0, 10);  // the record was re-read
    CHECK(cache.buttons_builds() == 2);

    // Every button carries its path's own notation.
    for (const PathButtonView& b : list.buttons) CHECK(b.notation == b.path->pathstring());
```

In `TEST_CASE("PathsTabCache: 600 cached frames cost far less than 600 rebuilds")`, replace:

```cpp
        PathListView list = build_path_list(rec);
        for (const Path* p : rec.all_paths()) {
            std::string label = p->pathstring();
            PathRowView row = build_path_row(*p);
        }
```

with `PathButtonsView list = build_path_buttons(rec, 0, 10);`, and `cache.list(rec, 1);` with `cache.buttons(rec, 1, 0, 10);`.

- [ ] **Step 7: Build everything and run both suites.** Build the whole tree (`powershell -ExecutionPolicy Bypass -File build_cpp.ps1 > build.log 2>&1`), then run `.\build-cpp\Release\hydra_tests.exe` (expected `Status: SUCCESS!`) and `.\build-cpp\Release\hydra_uitest.exe --all`. Expected: every test passes except `backend-limit`, `squeezed_out_uncounted` and `preview-path-overlay`, for the reasons in the acceptance criteria. Grep `src` and `tests` for `ActivationDetailsView`, `build_path_row`, `more_label`, `.footer` and `.header` on an activation view; nothing may remain.

- [ ] **Step 8: Commit.**

```powershell
git add src/ui/paths_tab.cpp src/ui/details_parts.h src/app/path_view.h src/app/path_view.cpp tests/test_path_view.cpp tests/ui/uitest_paths.cpp tests/ui/uitest_tests.cpp CMakeLists.txt
git commit -m "Paths tab: path buttons, one-line activations with timeline and folds, plain squeeze sentences

Task: Task 10 - Paths tab
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: The Preview tab

Today the Preview draws a path, but it never says which one, and the only way to change it is to go back to the Paths tab. It can't jump to an activation, the scrubber shows nothing of the path, the SP gauge has no label or number, and the time box writes measures as `[27:2:450]` while the Paths tab writes `m32.1.0`. This task adds what the Preview mockup shows. A "Showing" list (`##previewpath`) names the drawn path and picks another; it shares one selection with the Paths tab, so a pick in either place moves both. `< Act` and `Act >` buttons, and the `[` and `]` keys, jump between activations. The scrubber gets a gold mark per activation, and the clock moves beside it. The time box shows `m27.2.450  of m96.3.240`, the tempo with its signature, and the section. A box at the highway's bottom-left names the next activation and its chord. The gauge gets an "SP" label and a number such as `2.5/4`, and a key-hint line sits under the highway. "Show in Preview" on a Paths-tab activation lands the playhead on that activation. Every string comes from Task 8's view data. The old time-box fields Task 8 kept are deleted.

**Wave:** 3. **Depends on:** Task 1 (`preview_tab.cpp` and `uitest_preview.cpp` exist), Task 8 (the view data), Task 6 (the wave-3 base). **Expected overlaps:** Task 10 in `tests/ui/uitest_tests.cpp` only if Task 1's registration needs a line there (this task registers inside `uitest_preview.cpp`'s own table). `src/app/preview_view.{h,cpp}`, `tests/test_preview_view.cpp` and `tests/test_preview_golden.cpp` are Task 8's area from wave 2; nobody else edits them in wave 3. `test_preview_golden.cpp` is not in the ownership list; this task touches one `MESSAGE` line in it because it reads a deleted field.

**Goal:** The Preview tab matches the Preview mockup, shares its path selection with the Paths tab, and writes measures only through `format_measure`.

**Files:**
- Modify: `src/ui/preview_tab.cpp` (`render_preview_panel` and two new helpers)
- Modify: `src/ui/preview_controller.h`, `src/ui/preview_controller.cpp` (activation jumps, scrubber marks, next-activation box, SP readout)
- Modify: `src/app/preview_view.h`, `src/app/preview_view.cpp` (delete `measure_beat`, `bpm`, `time_sig`, `section` from `PreviewTimeBox`)
- Modify: `tests/test_preview_view.cpp`, `tests/test_preview_golden.cpp` (the old time-box assertions)
- Modify: `tests/ui/uitest_preview.cpp` (two new tests, the path-overlay test's clicks, the `measure_beat` reads)

**Acceptance Criteria:**
- [ ] `hydra_uitest --test preview-path-picker` passes: `Showing` and `3- 1 2  (optimal)` are on screen; picking `0 4 1` in `##previewpath` changes `details_ui.selected_path` and the overlay key, and the Paths tab keeps it; the list offers `0 0 0 0  (0 ms limit)`; a pending `preview_jump` of 0 (what "Show in Preview" on activation 1 sets) is consumed and lands the playhead where `next_act_box()` reads `Next: activation 1 of 3` / `at m32.1.0`.
- [ ] `hydra_uitest --test preview-activation-jumps` passes on Burnout: three scrubber marks in order inside 0..1; `Act >##nextact` lands on `m32.1.0`, `]` on `m58.1.0`, `[` back to `m32.1.0`, `< Act##prevact` stays put at the first; a jump while playing keeps playing; the song ends at `m96.3.240`; the readout ends `/4`; the key hint `[ ] previous/next activation` is on screen.
- [ ] `hydra_uitest --test preview-path-overlay --test preview-controls --test preview-buttons-keys --test preview-drain-box --test scrub-hold --test preview-overlay-fit` all pass.
- [ ] `hydra_tests.exe` ends `Status: SUCCESS!` with `measure_beat`, `bpm`, `time_sig` and `section` gone from `PreviewTimeBox`; `rg "measure_beat|bracket_str" src tests` finds nothing.
- [ ] `hydra_uitest --all` passes except, at most, the old `backend-limit` and `squeezed_out_uncounted` tests Task 10 replaces (see Task 10).

**Verify:** `.\build-cpp\Release\hydra_uitest.exe --test preview-path-picker --test preview-activation-jumps --test preview-path-overlay --test preview-buttons-keys` → four `[PASS]` lines.

**Steps:**

- [ ] **Step 1: Write the failing GUI tests.** In `tests/ui/uitest_preview.cpp`, add `#include "app/preview_view.h"` if Task 1 did not keep it. Add the code below inside the file's anonymous namespace. Put `pick_preview_path` above `test_preview_path_overlay`, which uses it too (Step 7); put the rest after the last existing test. It uses the shared helpers Task 1 moved out and Task 9 updated (`scan_library`, `open_titled`, `analyze_open_song`).

```cpp
// Burnout analyzed, its Preview open with the optimal path's overlay loaded.
// Returns false (the check already failed) when a step did not work.
bool open_burnout_preview(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return false;
    open_titled(ctx, "burnout", "Burnout");
    if (ctx->IsError()) return false;
    analyze_open_song(ctx);
    if (ctx->IsError()) return false;
    IM_CHECK_RETV(h.app->viewed.record->best_path().pathstring() == "3- 1 2", false);
    ctx->ItemClick("##DetailsTabs/Preview");
    IM_CHECK_RETV(wait_until(ctx, [&] { return h.app->preview && h.app->preview->active(); }, 10),
                  false);
    IM_CHECK_RETV(wait_until(ctx, [&] {
        return !h.app->preview->loading() && h.app->preview->scrub_marks().size() == 3;
    }, 120), false);
    IM_CHECK_RETV(h.app->preview->error().empty(), false);
    return true;
}

// Pick path `index` (its place in the Paths tab's list, ##path<index>) in the
// Preview's "Showing" list. The Preview tab must be showing.
void pick_preview_path(ImGuiTestContext* ctx, size_t index) {
    Harness& h = harness(ctx);
    const hydra::app::PathButtonsView list = hydra::app::build_path_buttons(
        *h.app->viewed.record, h.app->settings.depth_mode, h.app->settings.depth_value);
    IM_CHECK(index < list.buttons.size());
    if (index >= list.buttons.size()) return;
    const std::string item =
        hydra::app::preview_path_label(list.buttons[index]) + "##" + std::to_string(index);
    ctx->ItemClick("**/##previewpath");
    ctx->Yield(1);
    ctx->ItemClick(("//$FOCUSED/" + item).c_str());
    ctx->Yield(2);
}

// The "Showing" list: it names the drawn path, lists the all-0 path under its
// own name, and a pick changes the one selection the Paths tab reads too. A
// pending "Show in Preview" lands the playhead on its activation. (Clicking
// the link itself is Task 10's, checked in paths-rows.)
void test_preview_path_picker(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout_preview(ctx)) return;
    auto& pc = *h.app->preview;
    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("Showing") != std::string::npos);
    IM_CHECK(visible_text(h).find("3- 1 2  (optimal)") != std::string::npos);

    ctx->ItemClick("**/##previewpath");
    ctx->Yield(1);
    IM_CHECK(visible_text(h).find("0 0 0 0  (0 ms limit)") != std::string::npos);
    ctx->PopupCloseAll();
    ctx->Yield(1);

    pick_preview_path(ctx, 1);
    IM_CHECK(h.app->details_ui.selected_path != nullptr);
    IM_CHECK(h.app->details_ui.selected_path->pathstring() == "0 4 1");
    const std::string key = hydra::app::path_overlay_key(h.app->details_ui.selected_path);
    IM_CHECK(wait_until(ctx, [&] { return pc.overlay_path_key().rfind(key, 0) == 0; }, 30));
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);
    IM_CHECK(h.app->details_ui.selected_path->pathstring() == "0 4 1");
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);

    // Back to the optimal path, then a pending jump to its first activation.
    pick_preview_path(ctx, 0);
    IM_CHECK(h.app->details_ui.selected_path == &h.app->viewed.record->best_path());
    pc.seek_ms(0.0);
    h.app->details_ui.paths_tab.ui().preview_jump = 0;
    IM_CHECK(wait_until(ctx, [&] {
        return !h.app->details_ui.paths_tab.ui().preview_jump.has_value();
    }, 30));
    IM_CHECK(pc.position_ms() > 0.0);
    IM_CHECK_STR_EQ(pc.next_act_box().header.c_str(), "Next: activation 1 of 3");
    IM_CHECK(pc.next_act_box().detail.rfind("at m32.1.0", 0) == 0);
}

// Activation jumps by button and key, the scrubber marks, the next-activation
// box, the SP readout, the one measure format, and the key hint.
void test_preview_activation_jumps(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    if (!open_burnout_preview(ctx)) return;
    auto& pc = *h.app->preview;

    const std::vector<double> marks = pc.scrub_marks();
    IM_CHECK_EQ(marks.size(), (size_t)3);
    IM_CHECK(marks[0] > 0.0);
    IM_CHECK(marks[0] < marks[1]);
    IM_CHECK(marks[1] < marks[2]);
    IM_CHECK(marks[2] < 1.0);

    pc.seek_ms(0.0);
    IM_CHECK_STR_EQ(pc.next_act_box().header.c_str(), "Next: activation 1 of 3");
    IM_CHECK_STR_EQ(pc.next_act_box().detail.c_str(), "at m32.1.0 \xC2\xB7 [Kick - GreenCym]");

    ctx->ItemClick("**/Act >##nextact");
    const double act1 = pc.position_ms();
    IM_CHECK(act1 > 0.0);
    IM_CHECK_STR_EQ(pc.time_box().position.c_str(), "m32.1.0");
    ctx->KeyPress(ImGuiKey_RightBracket);
    IM_CHECK_STR_EQ(pc.time_box().position.c_str(), "m58.1.0");
    IM_CHECK_STR_EQ(pc.next_act_box().header.c_str(), "Next: activation 2 of 3");
    ctx->KeyPress(ImGuiKey_LeftBracket);
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), act1, 0.5);
    // Nothing before the first activation: the playhead stays.
    ctx->ItemClick("**/< Act##prevact");
    IM_CHECK_FLOAT_NEAR_EQ(pc.position_ms(), act1, 0.5);

    // A jump keeps playing when playing.
    ctx->ItemClick("**/Play");
    IM_CHECK(pc.playing());
    ctx->KeyPress(ImGuiKey_RightBracket);
    IM_CHECK(pc.playing());
    ctx->ItemClick("**/Pause");
    IM_CHECK(!pc.playing());

    const std::string readout = pc.sp_meter_readout();
    IM_CHECK(readout.size() >= 5);
    IM_CHECK(readout.substr(readout.size() - 2) == "/4");
    pc.seek_ms(pc.length_ms());
    IM_CHECK_STR_EQ(pc.time_box().length.c_str(), "m96.3.240");
    IM_CHECK_STR_EQ(pc.time_box().position.c_str(), pc.time_box().length.c_str());
    IM_CHECK(pc.time_box().tempo.rfind("BPM ", 0) == 0);

    ctx->Yield(2);
    IM_CHECK(visible_text(h).find("[ ] previous/next activation") != std::string::npos);
}
```

Add both to the file's test table, next to the other Preview entries, in the table's own form (Task 1's). With the old `Entry` form they read:

```cpp
        {"preview-path-picker", test_preview_path_picker},
        {"preview-activation-jumps", test_preview_activation_jumps},
```

- [ ] **Step 2: Run them and watch them fail.** Build with `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_uitest > build.log 2>&1` from the Bash tool. Expected: the build fails naming `scrub_marks`, `next_act_box` and `sp_meter_readout` as members `PreviewController` lacks.

- [ ] **Step 3: Add the controller's pieces.** In `src/ui/preview_controller.h`, after `hydra::app::PreviewDrainBox drain_box() const;` add:

```cpp

    // The drawn path's activations on the scrubber, as fractions of
    // length_ms() (app::build_scrub_marks). Empty until a path's scene is in.
    std::vector<double> scrub_marks() const;

    // The box at the highway's bottom-left (app::build_next_act_box).
    hydra::app::PreviewNextActBox next_act_box() const;

    // The number under the SP gauge, "2.5/4" (app::sp_meter_readout).
    std::string sp_meter_readout() const;

    // Move the playhead to the previous (-1) or next (+1) activation of the
    // drawn path (app::activation_jump_ms). Playing stays playing, as with
    // jump_ms. False when there is none that way or nothing is loaded.
    bool jump_activation(int direction);

    // Move the playhead to activation `index` (0-based) of the drawn path,
    // for "Show in Preview". False when nothing is loaded or the drawn path
    // has no such activation.
    bool seek_activation(size_t index);
```

In `src/ui/preview_controller.cpp`, after `PreviewController::drain_box`:

```cpp
std::vector<double> PreviewController::scrub_marks() const {
    return hydra::app::build_scrub_marks(scene_, transport_.length_ms());
}

hydra::app::PreviewNextActBox PreviewController::next_act_box() const {
    return hydra::app::build_next_act_box(scene_, transport_.now_ms());
}

std::string PreviewController::sp_meter_readout() const {
    return hydra::app::sp_meter_readout(scene_.sp_meter, transport_.now_ms());
}

bool PreviewController::jump_activation(int direction) {
    if (!active_ || job_) return false;  // nothing loaded yet
    const std::optional<double> to =
        hydra::app::activation_jump_ms(scene_, transport_.now_ms(), direction);
    if (!to) return false;
    transport_.seek_ms(*to);
    return true;
}

bool PreviewController::seek_activation(size_t index) {
    if (!active_ || job_ || index >= scene_.activations.size()) return false;
    transport_.seek_ms(scene_.activations[index].ms);
    return true;
}
```

- [ ] **Step 4: Add the two panel helpers to `src/ui/preview_tab.cpp`.** Make sure the file includes `<algorithm>`, `<optional>`, `<string>`, `<vector>`, `"ui/fonts.h"`, `"ui/theme.h"` and `"ui/widgets.h"` (Task 1 moved most of them over with the function). Put these in the file's anonymous namespace, above `render_preview_panel` (if the file has none, open `namespace {` there and close it before `render_preview_panel`):

```cpp
// The line under the highway naming the Preview's keys.
const char* const kPreviewKeysHint =
    "Space play/pause \xC2\xB7 \xE2\x86\x90 \xE2\x86\x92 5 s \xC2\xB7 , . 5 ticks "
    "\xC2\xB7 [ ] previous/next activation";

// "Showing" and the ##previewpath list: the same paths, in the same order and
// from the same cache, as the Paths tab's buttons. A pick sets the one
// selection both tabs read (DetailsViewState::selected_path). Drawn only when
// the song has a Ready record with paths.
void render_path_picker(AppState& app) {
    if (app.viewed.status != store::RecordStatus::Ready || !app.viewed.record ||
        app.viewed.record->paths.empty())
        return;
    const app::PathButtonsView& list = app.details_ui.paths_tab.buttons(
        *app.viewed.record, app.record_generation.n, app.settings.depth_mode,
        app.settings.depth_value);
    const Path*& selected = app.details_ui.selected_path;
    std::string current;
    for (const app::PathButtonView& b : list.buttons)
        if (b.path == selected) current = app::preview_path_label(b);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Showing");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(200.0f));
    ImGui::PushFont(g_mono_font, 0.0f);
    if (ImGui::BeginCombo("##previewpath", current.c_str())) {
        for (size_t i = 0; i < list.buttons.size(); ++i) {
            const app::PathButtonView& b = list.buttons[i];
            // "##<i>" keeps two paths with the same notation apart.
            const std::string item = app::preview_path_label(b) + "##" + std::to_string(i);
            const bool is_selected = b.path == selected;
            if (ImGui::Selectable(item.c_str(), is_selected)) selected = b.path;
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopFont();
    ImGui::SameLine(0.0f, px(16.0f));
}

// Gold ticks over the scrubber just drawn, one per activation, where the
// grab's centre sits for that time. ImGui keeps 2 px of padding and half a
// grab at each end of a float slider, so the ticks do too.
void draw_scrub_marks(const std::vector<double>& marks) {
    if (marks.empty()) return;
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const float grab = ImGui::GetStyle().GrabMinSize;
    const float pad = 2.0f;  // ImGui's slider grab_padding, not scaled
    const float x0 = mn.x + pad + grab * 0.5f;
    const float span = (mx.x - mn.x) - 2.0f * pad - grab;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (double f : marks) {
        const float x = x0 + static_cast<float>(f) * span;
        dl->AddRectFilled(ImVec2(x - px(1.5f), mn.y + px(2.0f)), ImVec2(x + px(1.5f), mx.y - px(2.0f)),
                          ImGui::GetColorU32(kBestPathColor));
    }
}
```

- [ ] **Step 5: Change `render_preview_panel`.** Six edits in `src/ui/preview_tab.cpp`.

(a) The top of the transport. Replace:

```cpp
    // Transport row: back 5 s, back 5 ticks, play/pause, forward 5 ticks,
    // forward 5 s, a scrubber, and the time readout. Every piece whose text
    // changes while playing sits in a fixed slot (see widgets.h): otherwise
    // the Vol slider walked under a held mouse as the readout's digits
    // changed width. The four step buttons have fixed labels, so they need
    // no slot.
    // The tick buttons and comma/period step this many chart ticks.
    constexpr int kTickStep = 5;
```

with:

```cpp
    // "Show in Preview" on the Paths tab: once the overlay for the selected
    // path is in, move the playhead to that activation.
    std::optional<size_t>& jump = app.details_ui.paths_tab.ui().preview_jump;
    if (jump && pc->overlay_path_key().rfind(ui.overlay_key, 0) == 0) {
        pc->seek_activation(*jump);
        jump.reset();
    }

    // Row 1: which path the overlay draws, the activation jumps, the
    // transport buttons and the volume. Row 2: the scrubber, a gold mark per
    // activation, and the clock. The clock sits in a fixed slot (see
    // widgets.h), so nothing walks under a held mouse as its digits change.
    // The tick buttons and comma/period step this many chart ticks.
    constexpr int kTickStep = 5;
    render_path_picker(app);
    const bool no_acts = pc->scrub_marks().empty();
    begin_disabled_button(no_acts);
    if (ImGui::Button("< Act##prevact")) pc->jump_activation(-1);
    ImGui::SameLine();
    if (ImGui::Button("Act >##nextact")) pc->jump_activation(+1);
    end_disabled_button(no_acts);
    hint("Previous or next activation ([ and ])");
    ImGui::SameLine(0.0f, px(16.0f));
```

(b) Volume moves up to row 1 and the scrubber gets its own row. Replace everything from:

```cpp
    if (ImGui::Button("+5s")) pc->jump_ms(5000.0);
    hint("Forward 5 seconds (Right arrow)");
    ImGui::SameLine();

    // The clock drives the scrubber, so a chart with no audio still scrubs.
```

down to and including:

```cpp
    if (ImGui::IsItemDeactivatedAfterEdit()) app.commit_settings();
```

with:

```cpp
    if (ImGui::Button("+5s")) pc->jump_ms(5000.0);
    hint("Forward 5 seconds (Right arrow)");
    ImGui::SameLine(0.0f, px(16.0f));

    // Volume: applied live and remembered in the settings file.
    ImGui::TextUnformatted("Vol");
    ImGui::SameLine();
    int volume = app.settings.preview_volume;
    ImGui::SetNextItemWidth(px(110.0f));
    if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d%%")) {
        app.settings.preview_volume = volume;
        pc->set_volume(volume);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) app.commit_settings();

    // The clock drives the scrubber, so a chart with no audio still scrubs.
    const hydra::app::PreviewTimeBox box = pc->time_box();
    float pos_s = static_cast<float>(pc->position_ms() / 1000.0);
    const float len_s = static_cast<float>(pc->length_ms() / 1000.0);
    // The clock's slot fits its widest form: every digit drawn as the widest one.
    std::string readout_sample = box.timestamp;
    const char widest = widest_digits(1)[0];
    for (char& c : readout_sample)
        if (c >= '0' && c <= '9') c = widest;
    ImGui::PushFont(g_mono_font, 0.0f);
    const float readout_w = text_slot_width(readout_sample.c_str());
    ImGui::PopFont();
    ImGui::SetNextItemWidth(std::max(px(120.0f), ImGui::GetContentRegionAvail().x - readout_w -
                                                     ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::SliderFloat("##scrub", &pos_s, 0.0f, len_s > 0.0f ? len_s : 1.0f, ""))
        pc->seek_ms(static_cast<double>(pos_s) * 1000.0);
    // Holding the scrubber pauses playback (Onyx's rule); release resumes.
    pc->set_scrubbing(ImGui::IsItemActive());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Gold marks are this path's activations.");
    draw_scrub_marks(pc->scrub_marks());
    ImGui::SameLine();
    ImGui::PushFont(g_mono_font, 0.0f);
    text_in_slot(box.timestamp.c_str(), readout_w);
    ImGui::PopFont();
```

(c) The keys. Replace:

```cpp
        if (ImGui::IsKeyPressed(ImGuiKey_Comma, true)) pc->step_ticks(-kTickStep);
        if (ImGui::IsKeyPressed(ImGuiKey_Period, true)) pc->step_ticks(kTickStep);
```

with:

```cpp
        if (ImGui::IsKeyPressed(ImGuiKey_Comma, true)) pc->step_ticks(-kTickStep);
        if (ImGui::IsKeyPressed(ImGuiKey_Period, true)) pc->step_ticks(kTickStep);
        // [ and ] jump between the drawn path's activations; no repeat.
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) pc->jump_activation(-1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) pc->jump_activation(+1);
```

and in the comment above that block change `comma/period step\n    // 5 ticks;` to `comma/period step\n    // 5 ticks, [ and ] jump between activations;`.

(d) The highway leaves a line for the key hint. Replace:

```cpp
    // Highway viewport: size the offscreen target to the remaining region.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = static_cast<int>(avail.x);
    int h = static_cast<int>(avail.y);
```

with:

```cpp
    // Highway viewport: the remaining region, less one line for the key hint.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = static_cast<int>(avail.x);
    int h = static_cast<int>(avail.y - ImGui::GetTextLineHeightWithSpacing());
```

(e) The time box's lines and the next-activation box. Replace:

```cpp
        // The time box's lines, the way Onyx draws its own (top-left,
        // monospace, on a translucent dark panel): time / length,
        // [measure:beat:tick] for both, BPM, the time signature, and the
        // practice section (absent on charts that have none).
        hydra::app::PreviewTimeBox box = pc->time_box();
        const char* lines[5];
        int line_count = 0;
        lines[line_count++] = box.timestamp.c_str();
        lines[line_count++] = box.measure_beat.c_str();
        lines[line_count++] = box.bpm.c_str();
        lines[line_count++] = box.time_sig.c_str();
        if (!box.section.empty()) lines[line_count++] = box.section.c_str();
        hydra::app::PreviewScoreBox score = pc->score_box();
```

with:

```cpp
        // The time box's lines, the way Onyx draws its own (top-left,
        // monospace, on a translucent dark panel): the playhead's measure and
        // the song's last, the tempo and signature in force, and the practice
        // section (absent on charts that have none). The clock is beside the
        // scrubber now.
        const std::string where = box.position + "  of " + box.length;
        const char* lines[3];
        int line_count = 0;
        lines[line_count++] = where.c_str();
        lines[line_count++] = box.tempo.c_str();
        if (!box.section_line.empty()) lines[line_count++] = box.section_line.c_str();
        hydra::app::PreviewNextActBox next = pc->next_act_box();
        hydra::app::PreviewScoreBox score = pc->score_box();
```

Replace:

```cpp
        fit.gap = gap1;
        const float scale = render::overlay_scale(pcfg, w, h, fit);
```

with:

```cpp
        if (next.shown) {
            const float next_w1 = std::max(text_width(size1, next.header.c_str()),
                                           text_width(size1, next.detail.c_str()));
            fit.left_w = std::max(fit.left_w, margin1 + next_w1 + pad1 * 2.0f);
        }
        fit.gap = gap1;
        const float scale = render::overlay_scale(pcfg, w, h, fit);
```

Replace the comment line that opens the gauge section, `// The Star Power meter: a gauge down the image's right edge, filling`, with this block followed by that same comment line:

```cpp
        // The next activation, bottom-left in the same panel style: its number
        // in gold, then where it is and its chord. Hidden past the last one.
        if (next.shown) {
            const float nw = std::max(text_width(size, next.header.c_str()),
                                      text_width(size, next.detail.c_str()));
            const ImVec2 n_min(origin.x, img_max.y - (pad * 2.0f + line_h * 2.0f));
            const ImVec2 n_max(origin.x + margin + nw + pad * 2.0f, img_max.y);
            dl->AddRectFilled(n_min, n_max, IM_COL32(0, 0, 0, 128), corner,
                              ImDrawFlags_RoundCornersTopRight);
            dl->AddText(font, size, ImVec2(origin.x + margin, n_min.y + pad),
                        IM_COL32(255, 204, 51, 255), next.header.c_str());
            dl->AddText(font, size, ImVec2(origin.x + margin, n_min.y + pad + line_h),
                        IM_COL32(255, 255, 255, 255), next.detail.c_str());
        }

```

(f) The gauge's label and number. Replace:

```cpp
        if (has_gauge) {
            ImVec2 gauge_min(gauge_left, origin.y + v_margin);
            ImVec2 gauge_max(img_max.x - inset, img_max.y - v_margin);
```

with:

```cpp
        if (has_gauge) {
            // "SP" above the gauge in gold, the banked bars under it.
            const float label_size = px(14.0f);
            const float label_h = label_size * 1.25f;
            const float centre_x = gauge_left + bar_w * 0.5f;
            dl->AddText(font, label_size,
                        ImVec2(centre_x - text_width(label_size, "SP") * 0.5f, origin.y + v_margin),
                        IM_COL32(255, 204, 51, 255), "SP");
            const std::string readout = pc->sp_meter_readout();
            const float rw = text_width(label_size, readout.c_str());
            dl->AddText(font, label_size,
                        ImVec2(std::min(centre_x - rw * 0.5f, img_max.x - px(2.0f) - rw),
                               img_max.y - v_margin - label_size),
                        IM_COL32(255, 255, 255, 255), readout.c_str());
            ImVec2 gauge_min(gauge_left, origin.y + v_margin + label_h);
            ImVec2 gauge_max(img_max.x - inset, img_max.y - v_margin - label_h);
```

Finally, the key hint. The function ends with the drain box's drawing loop and three closing braces. Replace:

```cpp
                            colors[i], d_lines[i]);
            }
        }
    }
}
```

with:

```cpp
                            colors[i], d_lines[i]);
            }
        }
    }

    // The keys, under the highway.
    ImGui::TextDisabled("%s", kPreviewKeysHint);
}
```

- [ ] **Step 6: Run the new tests and watch them pass.** Rebuild `hydra_uitest` and run `.\build-cpp\Release\hydra_uitest.exe --test preview-path-picker --test preview-activation-jumps`. Expected: two `[PASS]` lines.

- [ ] **Step 7: Delete the old time-box fields.** In `src/app/preview_view.h`, replace the `PreviewTimeBox` comment and struct:

```cpp
// The Preview's time box at `now_ms`, in Moonscraper's layout: the playhead
// time and the song length as "m:ss.mmm / m:ss.mmm", the same two points as
// 1-based "[measure:beat:tick]", the BPM in force, the time signature in force
// ("Time signature: 6/4"), and the practice section in force. `section` is empty when the chart has none at or before the playhead;
// the box is four lines tall then.
struct PreviewTimeBox {
    std::string timestamp;
    std::string measure_beat;
    std::string bpm;
    std::string time_sig;
    std::string section;
    // The redesigned box's lines. Task 11 draws these, deletes measure_beat,
    // bpm, time_sig and section above, and moves `timestamp` beside the scrubber.
    std::string position;      // format_measure at the playhead, "m27.2.450"
    std::string length;        // format_measure at the song's end, "m96.3.240"
    std::string tempo;         // "BPM 191.001 · 4/4"
    std::string section_line;  // "Section chorus_1"; empty when no section is in force
};
```

with:

```cpp
// The Preview's time readouts at `now_ms`. `timestamp` is the clock beside the
// scrubber, playhead and song length as "m:ss.mmm / m:ss.mmm". The time box
// over the highway shows the two same points through format_measure, the BPM
// and time signature in force, and the practice section in force.
struct PreviewTimeBox {
    std::string timestamp;     // "0:35.000 / 2:06.253"
    std::string position;      // format_measure at the playhead, "m27.2.450"
    std::string length;        // format_measure at the song's end, "m96.3.240"
    std::string tempo;         // "BPM 191.001 · 4/4"
    std::string section_line;  // "Section chorus_1"; empty when no section is in force
};
```

In `src/app/preview_view.cpp`, delete the `bracket_str` helper and its comment ("Moonscraper's "[measure:beat:tick]"..."), and replace the whole `build_time_box` function with:

```cpp
PreviewTimeBox build_time_box(const PreviewScene& scene, double now_ms,
                              double length_ms) {
    PreviewTimeBox box;

    const double len = shown_length(length_ms);
    const double now = shown_ms(now_ms, length_ms);

    box.timestamp = clock_str(now) + " / " + clock_str(len);

    // A default-built scene carries no song and so no timing: it reads as tick
    // 0 at measure 1, beat 1, exactly as it always has.
    const int64_t now_tick = scene.timing ? tick_at(*scene.timing, now) : 0;
    const int64_t end_tick = scene.timing ? tick_at(*scene.timing, len) : 0;
    box.position = scene.timing ? format_measure(*scene.timing, now_tick) : "m1.1.0";
    box.length = scene.timing ? format_measure(*scene.timing, end_tick) : "m1.1.0";

    // The tempo in force: the last change at or before now (the opening tempo
    // before any change).
    double bpm = scene.tempos.empty() ? 0.0 : scene.tempos.front().bpm;
    for (const PreviewTempo& t : scene.tempos) {
        if (t.ms > now) break;
        bpm = t.bpm;
    }
    // The time signature in force at the playhead's tick, as the chart wrote
    // it; 4/4 before any, the chart default.
    int ts_num = 4, ts_den = 4;
    for (const PreviewTimeSig& t : scene.time_sigs) {
        if (t.tick > now_tick) break;
        ts_num = t.numerator;
        ts_den = t.denominator;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "BPM %.3f \xC2\xB7 %d/%d", bpm, ts_num, ts_den);
    box.tempo = buf;

    std::string section;
    for (const PreviewSection& s : scene.sections) {
        if (s.tick > now_tick) break;
        section = s.name;
    }
    if (!section.empty()) box.section_line = "Section " + section;
    return box;
}
```

In `tests/test_preview_view.cpp`, rename `TEST_CASE("build_time_box: timestamp, measure:beat:tick, BPM")` to `"build_time_box: timestamp, measure, tempo"` and `TEST_CASE("build_time_box: the end bracket runs past the last beat line")` to `"build_time_box: the end measure runs past the last beat line"`. Then change the old assertions:

- Delete `CHECK(box.measure_beat == "[1:3:288] / [3:3:000]");`, `CHECK(box.bpm == "BPM: 120.000");`, `CHECK(box.section.empty());`, `CHECK(build_time_box(scene, 0.0, 5000.0).measure_beat == "[1:1:000] / [3:3:000]");`, `CHECK(box.measure_beat == "[16:1:000] / [16:1:000]");`, `CHECK(box.measure_beat == "[3:1:240] / [3:1:240]");` and the two `step_tick_ms` checks `CHECK(build_time_box(scene, fwd, 5000.0).measure_beat == "[1:3:289] / [3:3:000]");` and `CHECK(build_time_box(scene, back, 5000.0).measure_beat == "[1:3:287] / [3:3:000]");`. Task 8 put the matching `position`, `length`, `tempo` and `section_line` checks beside each of them.
- Replace `CHECK(build_time_box(scene, 6000.0, 5000.0).measure_beat == "[3:3:000] / [3:3:000]");` with `CHECK(build_time_box(scene, 6000.0, 5000.0).position == "m3.3.0");`.
- In `TEST_CASE("build_time_box: the practice section in force")`, replace the six `.section` checks with:

```cpp
    CHECK(build_time_box(scene, 0.0, len).section_line.empty());
    CHECK(build_time_box(scene, 400.0, len).section_line.empty());
    CHECK(build_time_box(scene, 500.0, len).section_line == "Section Verse 1");
    CHECK(build_time_box(scene, 2000.0, len).section_line == "Section Verse 1");
    CHECK(build_time_box(scene, 2500.0, len).section_line == "Section Chorus");
    CHECK(build_time_box(scene, 9000.0, len).section_line == "Section Chorus");
```

and delete the three `section_line` checks Task 8 appended after them (now duplicates).
- In `TEST_CASE("build_time_box: the time signature in force, as the chart wrote it")`, replace the five `.time_sig` checks with:

```cpp
    CHECK(build_time_box(scene, 0.0, 5000.0).tempo == "BPM 120.000 " + kDot + " 4/4");
    CHECK(build_time_box(scene, 1999.0, 5000.0).tempo == "BPM 120.000 " + kDot + " 4/4");
    CHECK(build_time_box(scene, 2000.0, 5000.0).tempo == "BPM 120.000 " + kDot + " 6/8");
    CHECK(build_time_box(scene, 3500.0, 5000.0).tempo == "BPM 120.000 " + kDot + " 3/4");
    // A scene built from nothing reads the chart default.
    CHECK(build_time_box(PreviewScene{}, 0.0, 0.0).tempo == "BPM 0.000 " + kDot + " 4/4");
```

and delete the three `tempo` checks Task 8 appended after them (now duplicates), keeping Task 8's `position == "m1.1.0"` check on the empty scene.

In `tests/test_preview_golden.cpp`, replace:

```cpp
    MESSAGE("time box: " << box.timestamp << " | " << box.measure_beat << " | " << box.bpm
                         << " | " << box.section);
```

with:

```cpp
    MESSAGE("time box: " << box.timestamp << " | " << box.position << " of " << box.length
                         << " | " << box.tempo << " | " << box.section_line);
```

In `tests/ui/uitest_preview.cpp`, `test_preview_controls` and `test_preview_buttons_keys` read `pc.time_box().measure_beat` three times each; change every one to `pc.time_box().position`. In `test_preview_path_overlay`, the path rows it clicked by pathstring are gone (Task 10 draws them as `##path<i>` buttons, in another worktree). Pick the paths in the Preview's own list instead, which needs nothing from Task 10 and still sends the Preview through a Paths-and-back visit. Replace:

```cpp
    const hydra::Path* other = nullptr;
    for (size_t i = 1; i < paths.size() && other == nullptr; ++i) {
        std::string label = paths[i]->pathstring();
        if (label == paths[0]->pathstring()) continue;
        size_t seen = 0;
        for (const hydra::Path* p : rows)
            if (p->pathstring() == label) ++seen;
        if (seen == 1) other = paths[i];
    }
```

with:

```cpp
    const hydra::Path* other = nullptr;
    size_t other_index = 0;  // its place in the list: the list follows all_paths()
    for (size_t i = 1; i < paths.size() && other == nullptr; ++i) {
        std::string label = paths[i]->pathstring();
        if (label == paths[0]->pathstring()) continue;
        size_t seen = 0;
        for (const hydra::Path* p : rows)
            if (p->pathstring() == label) ++seen;
        if (seen == 1) {
            other = paths[i];
            other_index = i;
        }
    }
```

Replace:

```cpp
    // Pick the other path and come back to the Preview.
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->ItemClick(("**/" + escape_ref(other_label)).c_str());
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
```

with:

```cpp
    // Pick the other path in the Preview's list, then visit Paths and come
    // back, so the Preview is re-opened on the same chart.
    pick_preview_path(ctx, other_index);
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
```

and:

```cpp
    // The same chart still previews the first path when it is selected again.
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->ItemClick(("**/" + escape_ref(first_label)).c_str());
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
```

with:

```cpp
    // The same chart still previews the first path when it is picked again.
    pick_preview_path(ctx, 0);
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->Yield(2);
    ctx->ItemClick("##DetailsTabs/Preview");
    ctx->Yield(2);
```

If `other_label` or `first_label` is now unused, keep them: the checks above them still read `first_label`, and an unused `other_label` only needs deleting if the build warns.

- [ ] **Step 8: Build everything and run both suites.** Build the whole tree (`powershell -ExecutionPolicy Bypass -File build_cpp.ps1 > build.log 2>&1`). Run `.\build-cpp\Release\hydra_tests.exe` (expected `Status: SUCCESS!`), `rg "measure_beat|bracket_str" src tests` (expected: no output), and `.\build-cpp\Release\hydra_uitest.exe --all` (expected: all pass except, at most, `backend-limit` and `squeezed_out_uncounted`, which Task 10 replaces and Task 9 deletes).

- [ ] **Step 9: Commit.**

```powershell
git add src/ui/preview_tab.cpp src/ui/preview_controller.h src/ui/preview_controller.cpp src/app/preview_view.h src/app/preview_view.cpp tests/test_preview_view.cpp tests/test_preview_golden.cpp tests/ui/uitest_preview.cpp
git commit -m "Preview tab: path picker shared with Paths, activation jumps, scrubber marks, SP label, one measure format

Task: Task 11 - Preview tab
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Library table and search

Today the library is a paged table. It shows one window-height of rows at a time, fills empty slots with `-----`, can't be sorted, has no way to show only unanalyzed charts, and draws raw `<color=#e02222>` tags in the Charter column. Its search is a SQL `LIKE` over title, artist and charter only, so it can't find "Beyoncé" from "beyonce", can't match words out of order, and never looks at the folder. This task replaces it. Every chart is held in memory in a small model (`ui/library_model.{h,cpp}`) that knows each row's summary from the store (T7), filters it with T2's query module, counts the status chips and sorts it. The table draws that model with a normal scroll bar, a frozen header row, sortable columns, and only the rows on screen. You will see a "Library" heading with "5 of 97 charts", a search box with a one-line hint and any query error under it, four chips (All, Not analyzed, Stale, Analyzed) with counts, and a table whose Best path cell reads `Not analyzed`, `Stale` or `378,315  3- 1 2`. While the song panel is open, Charter and Folder make way; when a search matched in a hidden column, each row gets a second line showing where, with the matched text highlighted, and a line under the table says "Matched on folder." with a `Clear search` button. Ctrl+F jumps to the search box and Escape inside it clears it.

**Wave:** 3. **Depends on:** T1 (the split: `library_table.cpp`, `library_parts.h`, `uitest_library.cpp`), T2 (`app/library_query.h`), T6 (`Settings::sp_cap` is an `int`), T7 (`SummaryLookup::summary`, `PathSummary::stars`). **Expected overlaps:** T9 in `src/ui/app_state.{h,cpp}` (T9 adds the panel, `settings_locked`, `select_relative`, `details_open`, and changes `store_finished_analysis`; this task adds the library members, replaces the paging members, and changes one line of `store_finished_analysis`, the constructor's first call, `apply_settings`' two branches and `start_scan`). T13 in `app_state.{h,cpp}` (T13 changes `start_batch`; this task adds one line to it). T9 and T13 in `CMakeLists.txt` (this task adds two lines). T9 in `src/ui/library_parts.h` (this task replaces only the declarations of the functions in `library_table.cpp`). New overlap: `tests/test_app_state.cpp`, which no task owns; this task edits it because it reads the paging members being removed. The callers of the removed members in other tasks' files are listed in Step 12, and the main session's merge checklist covers them.

**Goal:** The library shows every chart in one scrolling, sortable table, filtered by the new search and the status chips, with counts, highlights and no paging.

**Files:**
- Create: `src/ui/library_model.h`, `src/ui/library_model.cpp`, `tests/test_library_model.cpp`
- Modify: `src/ui/library_table.cpp` (rewritten), `src/ui/library_parts.h` (its own declarations only), `src/ui/app_state.h`, `src/ui/app_state.cpp`, `CMakeLists.txt` (two lines), `tests/test_app_state.cpp`
- Modify, interim only (so this worktree compiles; the main session keeps T9's and T13's versions at merge): the few lines of `src/ui/library_view.cpp`, `src/ui/library_toolbar.cpp` and `src/ui/library_dialogs.cpp` that Step 9 names
- Test: `tests/test_library_model.cpp` (new), `tests/ui/uitest_library.cpp` (two new GUI tests; existing ones updated), `tests/test_app_state.cpp`

**Acceptance Criteria:**
- [ ] Every `library model:` unit test passes, including `library model: filtering 20,000 charts takes under 20 ms` (each query under 20 ms in Release).
- [ ] `tests/test_app_state.cpp` passes with no reference to `table_viewpage`, `current_page` or `rows_per_page`.
- [ ] `hydra_uitest --test library-search` passes: 97 charts, `All (97)##chipall`, `"tier 4"` shows exactly 5 rows (Burnout, Chair, Limb From Limb, Unbound (The Wild Ride), YYZ) and `5 of 97 charts`, `green burnout` shows only Burnout, `bloodline` shows Acid Romance with `Bloodline` on screen and no `<color=`, `stars:9` shows its error, `zzqx` shows `No charts match your search.` and `Clear search` brings back all 97, Escape clears the box, Ctrl+F focuses it, and with the panel open `Matched on folder.` appears.
- [ ] `hydra_uitest --test library-sort-scroll` passes: Title ascending by default, the last title is not drawn until the table scrolls to the bottom, clicking `Title` reverses the order, `Not analyzed` shows, `-----` never does.
- [ ] `rg "table_viewpage|rows_per_page|set_rows_per_page|current_page|LibraryPage|refresh_page|##pageleft|##pageright" src tests` finds nothing in this task's files.
- [ ] `hydra_uitest --all` passes after the main session merges wave 3 (the other owners' callers use the replacements in Step 12).

**Verify:** `.\build-cpp\Release\hydra_uitest.exe --test library-search` → prints `[PASS] library-search` and exits 0.

**Steps:**

- [ ] **Step 1: Write the failing model tests.**

The model is plain C++ with no ImGui, so its rules and its speed can be tested directly. Create `tests/test_library_model.cpp`:

```cpp
// Unit tests for ui/library_model: the in-memory library behind the main
// window's table. It filters with app/library_query, counts the status chips,
// sorts, and turns each stored summary into the Best path cell's text.

#include "doctest.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "store/record_store.h"
#include "ui/library_model.h"

using hydra::store::ChartLibraryEntry;
using hydra::store::RecordStatus;
using hydra::store::SummaryLookup;
using hydra::ui::LibraryModel;
using hydra::ui::LibrarySort;
using hydra::ui::StatusChip;

namespace {

ChartLibraryEntry chart(const char* md5, const char* title, const char* artist,
                        const char* charter, const char* folder) {
    ChartLibraryEntry e;
    e.md5 = md5;
    e.title = title;
    e.artist = artist;
    e.charter = charter;
    e.rootfolder = folder;
    e.notespath = std::string("C:\\songs\\") + folder + "\\" + title + "\\notes.mid";
    return e;
}

SummaryLookup ready(int64_t score, const char* bestpath, int stars, double hardest_ms) {
    SummaryLookup s;
    s.status = RecordStatus::Ready;
    s.bestpath = bestpath;
    s.summary.score = score;
    s.summary.stars = stars;
    s.summary.hardest_ms = hardest_ms;
    return s;
}

SummaryLookup stale() {
    SummaryLookup s;
    s.status = RecordStatus::Stale;
    return s;
}

// Six charts from the scratch library's shapes: Burnout analyzed, Chair
// stale, the rest not analyzed. Row order here is scan order, not title order.
LibraryModel sample() {
    LibraryModel m;
    m.set_charts({
        chart("burnout", "Burnout", "Green Day", "Hoph2o", "common\\Summer Blast _25 Setlist\\Tier 4"),
        chart("yyz", "YYZ", "Rush", "Harmonix, Onyxite", "common\\Summer Blast _25 Setlist\\Tier 4"),
        chart("chair", "Chair", "Sufferer", "Satan", "common\\Summer Blast _25 Setlist\\Tier 4"),
        chart("acid", "Acid Romance", "Some Band", "<color=#e02222>Blood</color>line", "common\\Other"),
        chart("halo", "Halo", "Beyoncé", "Someone", "common\\Other"),
        chart("other", "Other", "Thrice", "Someone", "IB24\\T4"),
    });
    SummaryLookup none;
    m.set_summaries({ready(378315, "3- 1 2", 7, 163.0), none, stale(), none, none, none});
    return m;
}

std::vector<std::string> titles(const LibraryModel& m) {
    std::vector<std::string> out;
    for (size_t i : m.order()) out.push_back(m.rows()[i].title);
    return out;
}

}  // namespace

TEST_CASE("library model: every chart shows, sorted by title, with its Best path text") {
    const LibraryModel m = sample();
    CHECK(titles(m) == std::vector<std::string>{"Acid Romance", "Burnout", "Chair", "Halo",
                                                "Other", "YYZ"});
    CHECK(m.rows()[0].best_label == "378,315  3- 1 2");
    CHECK(m.rows()[2].best_label == "Stale");
    CHECK(m.rows()[1].best_label == "Not analyzed");
    // Colour tags never reach the screen.
    CHECK(m.rows()[3].charter == "Bloodline");
    CHECK(m.counts().all == 6);
    CHECK(m.counts().not_analyzed == 4);
    CHECK(m.counts().stale == 1);
    CHECK(m.counts().analyzed == 1);
}

TEST_CASE("library model: the search narrows the rows and the chip counts follow it") {
    LibraryModel m = sample();
    m.set_query("\"tier 4\"");
    CHECK(titles(m) == std::vector<std::string>{"Burnout", "Chair", "YYZ"});
    CHECK(m.counts().all == 3);
    CHECK(m.counts().not_analyzed == 1);
    CHECK(m.counts().stale == 1);
    CHECK(m.counts().analyzed == 1);

    m.set_query("green burnout");
    CHECK(titles(m) == std::vector<std::string>{"Burnout"});
    m.set_query("bloodline");
    CHECK(titles(m) == std::vector<std::string>{"Acid Romance"});
    m.set_query("beyonce");
    CHECK(titles(m) == std::vector<std::string>{"Halo"});
    m.set_query("stars:7");
    CHECK(titles(m) == std::vector<std::string>{"Burnout"});
    m.set_query("squeeze<=200");
    CHECK(titles(m) == std::vector<std::string>{"Burnout"});
    m.set_query("squeeze<=20");
    CHECK(titles(m).empty());
    CHECK(m.counts().all == 0);
    m.set_query("stars:9");
    CHECK_FALSE(m.query().errors.empty());
    m.set_query("");
    CHECK(m.order().size() == 6);
}

TEST_CASE("library model: a status chip narrows the rows, and an emptied chip falls back to All") {
    LibraryModel m = sample();
    m.set_chip(StatusChip::NotAnalyzed);
    CHECK(titles(m) == std::vector<std::string>{"Acid Romance", "Halo", "Other", "YYZ"});
    CHECK(m.counts().all == 6);  // counts ignore the chip
    m.set_chip(StatusChip::Stale);
    CHECK(titles(m) == std::vector<std::string>{"Chair"});
    // "What would Analyze search analyze": the query's matches, whatever the chip.
    CHECK(m.matches().size() == 6);

    // Chair is re-analyzed: the Stale group is empty, so the table goes back
    // to All instead of sitting empty.
    CHECK(m.set_summary_for("chair", ready(300000, "1 1", 6, 40.0)) == 1);
    CHECK(m.chip() == StatusChip::All);
    CHECK(m.order().size() == 6);
    CHECK(m.counts().analyzed == 2);
}

TEST_CASE("library model: Best path sorts by score, with unscored rows last both ways") {
    LibraryModel m = sample();
    m.set_summary_for("yyz", ready(500000, "2 2", 7, 12.0));
    m.set_sort(LibrarySort::BestPath, false);
    CHECK(titles(m) == std::vector<std::string>{"YYZ", "Burnout", "Chair", "Acid Romance",
                                                "Halo", "Other"});
    m.set_sort(LibrarySort::BestPath, true);
    CHECK(titles(m) == std::vector<std::string>{"Burnout", "YYZ", "Chair", "Acid Romance",
                                                "Halo", "Other"});
    m.set_sort(LibrarySort::Title, false);
    CHECK(titles(m).front() == "YYZ");
    m.set_sort(LibrarySort::Artist, true);
    CHECK(titles(m).front() == "Halo");  // "beyonce" folds first
}

TEST_CASE("library model: one chart in two folders gets both rows updated") {
    LibraryModel m;
    m.set_charts({chart("same", "Song", "A", "C", "Pack 1"), chart("same", "Song", "A", "C", "Pack 2")});
    CHECK(m.counts().not_analyzed == 2);
    CHECK(m.set_summary_for("same", ready(1000, "1", 3, 0.0)) == 2);
    CHECK(m.counts().analyzed == 2);
    // Asking again with the same answer changes nothing.
    CHECK(m.set_summary_for("same", ready(1000, "1", 3, 0.0)) == 0);
}

TEST_CASE("library model: filtering 20,000 charts takes under 20 ms") {
    // query_matches runs for every row on every applied keystroke, so the
    // whole pass has to fit well inside a frame.
    std::vector<ChartLibraryEntry> charts;
    std::vector<SummaryLookup> summaries;
    charts.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
        char md5[16], title[32], artist[32], charter[64], folder[48];
        std::snprintf(md5, sizeof(md5), "h%05d", i);
        std::snprintf(title, sizeof(title), "Song %05d", i);
        std::snprintf(artist, sizeof(artist), "Artist %02d", i % 50);
        std::snprintf(charter, sizeof(charter), "<color=#e02222>Char</color>ter %d", i % 30);
        std::snprintf(folder, sizeof(folder), "Pack %03d\\Tier %d", i % 200, i % 7);
        charts.push_back(chart(md5, title, artist, charter, folder));
        summaries.push_back(i % 3 == 0 ? ready(300000 + i, "1 2 3", 5 + i % 3, i % 90)
                                       : SummaryLookup{});
    }
    LibraryModel m;
    auto t0 = std::chrono::steady_clock::now();
    m.set_charts(std::move(charts));
    m.set_summaries(summaries);
    MESSAGE("load + sort 20,000: "
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()
            << " ms");

    for (const char* q : {"s", "song 1", "\"tier 4\"", "artist 07 song", "charter", "stars:7",
                          "squeeze<=20 pack", "zzqx", ""}) {
        t0 = std::chrono::steady_clock::now();
        m.set_query(q);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        MESSAGE("query \"" << q << "\": " << ms << " ms, " << m.order().size() << " rows");
        CHECK(ms < 20.0);
    }
    t0 = std::chrono::steady_clock::now();
    m.set_sort(LibrarySort::BestPath, false);
    MESSAGE("sort by Best path: "
            << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()
            << " ms");
}
```

Add the file to `hydra_tests` in `CMakeLists.txt`, after `tests/test_app_state.cpp`:

```cmake
    tests/test_app_state.cpp
    tests/test_library_model.cpp
```

Run `powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1` from the Bash tool. Expected: the build fails with `cannot open include file 'ui/library_model.h'`.

- [ ] **Step 2: Write the model header.**

Create `src/ui/library_model.h`:

```cpp
// The library as the main window browses it: every scanned chart, its stored
// summary, the user's search, the status chip and the sort. Plain data with no
// ImGui, so it can be unit-tested and timed. AppState owns one.
//
// Why in memory and not in SQL: whether a row is Stale or Ready is the store's
// C++ winner rule, and search folding (accents, colour tags) lives in
// app/library_query. Writing either again in SQL would be a second copy of the
// rule. A library of about 20,000 charts filters here in a few milliseconds.

#ifndef HYDRA_UI_LIBRARY_MODEL_H
#define HYDRA_UI_LIBRARY_MODEL_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "app/library_query.h"
#include "store/record_store.h"

namespace hydra::ui {

// The four filter chips above the table.
enum class StatusChip { All, NotAnalyzed, Stale, Analyzed };

// The table's sortable columns. The values are the columns' user IDs in the
// table, so a sort spec maps straight back to one of these.
enum class LibrarySort { Title = 0, Artist = 1, Charter = 2, Folder = 3, BestPath = 4 };

// One scanned chart as the table shows it.
struct LibraryRow {
    store::ChartLibraryEntry entry;       // as scanned; entry.md5 is the chart's hash
    std::string title, artist, charter;   // colour tags removed: what the table draws
    app::SearchableRow searchable;        // folded copies, for matching and sorting
    store::RecordStatus status = store::RecordStatus::NotAnalyzed;
    std::string bestpath;                 // set when Ready
    store::PathSummary summary;           // set when Ready (T7)
    std::string best_label;               // the Best path cell (best_path_label)
};

// The Best path cell: "Not analyzed", "Stale", or "<score>  <path>" such as
// "378,315  3- 1 2". The score is the stored summary's, never recomputed.
std::string best_path_label(store::RecordStatus status, const std::string& bestpath,
                            const store::PathSummary& summary);

// Rows the current query matches, by status. The chips show these; the
// selected chip never changes them.
struct ChipCounts {
    size_t all = 0;
    size_t not_analyzed = 0;
    size_t stale = 0;
    size_t analyzed = 0;
    size_t of(StatusChip chip) const;
};

class LibraryModel {
public:
    // Replaces every row (startup, after a scan). Rows start Not analyzed
    // until set_summaries.
    void set_charts(std::vector<store::ChartLibraryEntry> charts);
    // Each row's chart hash, parallel to rows(): what get_summaries asks about.
    std::vector<std::string> hashes() const;
    // `lookups` is parallel to rows(). Returns how many rows changed; the
    // order and counts are rebuilt only when some did.
    size_t set_summaries(const std::vector<store::SummaryLookup>& lookups);
    // One chart's new answer, applied to every row that lists it (a chart can
    // sit in two folders). Returns how many rows changed.
    size_t set_summary_for(const std::string& md5, const store::SummaryLookup& lookup);

    void set_query(std::string_view text);
    void set_chip(StatusChip chip);
    void set_sort(LibrarySort column, bool ascending);

    const std::vector<LibraryRow>& rows() const { return rows_; }
    // The rows shown, as indices into rows(), in sort order: the query and the
    // chip both applied.
    const std::vector<size_t>& order() const { return order_; }
    // The rows the query matches, whatever the chip, in sort order: what
    // "Analyze search (N)..." analyzes.
    std::vector<size_t> matches() const;
    const app::LibraryQuery& query() const { return query_; }
    const ChipCounts& counts() const { return counts_; }
    StatusChip chip() const { return chip_; }
    LibrarySort sort_column() const { return sort_column_; }
    bool ascending() const { return ascending_; }

private:
    void resort();     // rebuilds sorted_ from rows_ and the sort
    void refilter();   // rebuilds order_ and counts_ from sorted_, the query and the chip
    void summaries_changed();

    std::vector<LibraryRow> rows_;
    std::vector<size_t> sorted_;  // every row, in sort order
    std::vector<size_t> order_;
    std::string query_text_;
    app::LibraryQuery query_;
    ChipCounts counts_;
    StatusChip chip_ = StatusChip::All;
    LibrarySort sort_column_ = LibrarySort::Title;
    bool ascending_ = true;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_LIBRARY_MODEL_H
```

- [ ] **Step 3: Write the model.**

Create `src/ui/library_model.cpp`:

```cpp
#include "ui/library_model.h"

#include <algorithm>
#include <numeric>
#include <utility>

#include "core/model.h"  // group_thousands

namespace hydra::ui {

namespace {

StatusChip chip_of(store::RecordStatus status) {
    switch (status) {
        case store::RecordStatus::Ready: return StatusChip::Analyzed;
        case store::RecordStatus::Stale: return StatusChip::Stale;
        case store::RecordStatus::NotAnalyzed: break;
    }
    return StatusChip::NotAnalyzed;
}

// Where a row with no score sorts under Best path, the same in both
// directions: a Ready result with no paths, then Stale, then Not analyzed.
int unscored_rank(store::RecordStatus status) {
    switch (status) {
        case store::RecordStatus::Ready: return 0;
        case store::RecordStatus::Stale: return 1;
        case store::RecordStatus::NotAnalyzed: break;
    }
    return 2;
}

// What a stars: or squeeze filter may test. Only a Ready result's numbers
// count; anything else has no facts, so those filters never match it.
app::RowFacts facts_of(const LibraryRow& row) {
    if (row.status != store::RecordStatus::Ready) return app::RowFacts{};
    return app::RowFacts{row.summary.stars, row.summary.hardest_ms};
}

bool matches_query(const app::LibraryQuery& q, const LibraryRow& row) {
    return q.empty() || app::query_matches(q, row.searchable, facts_of(row));
}

// Applies one lookup to one row. False when nothing the library shows or
// filters on changed, so a refresh that finds the same answers costs no
// re-sort.
bool apply_summary(LibraryRow& row, const store::SummaryLookup& lookup) {
    if (row.status == lookup.status && row.bestpath == lookup.bestpath &&
        row.summary.score == lookup.summary.score && row.summary.stars == lookup.summary.stars &&
        row.summary.hardest_ms == lookup.summary.hardest_ms)
        return false;
    row.status = lookup.status;
    row.bestpath = lookup.bestpath;
    row.summary = lookup.summary;
    row.best_label = best_path_label(row.status, row.bestpath, row.summary);
    return true;
}

}  // namespace

std::string best_path_label(store::RecordStatus status, const std::string& bestpath,
                            const store::PathSummary& summary) {
    switch (status) {
        case store::RecordStatus::Stale: return "Stale";
        case store::RecordStatus::NotAnalyzed: return "Not analyzed";
        case store::RecordStatus::Ready: break;
    }
    // A Ready result with no paths has no score; its cell shows its path
    // string (empty), as the table always has.
    if (!summary.score) return bestpath;
    return group_thousands(*summary.score) + "  " + bestpath;
}

size_t ChipCounts::of(StatusChip chip) const {
    switch (chip) {
        case StatusChip::NotAnalyzed: return not_analyzed;
        case StatusChip::Stale: return stale;
        case StatusChip::Analyzed: return analyzed;
        case StatusChip::All: break;
    }
    return all;
}

void LibraryModel::set_charts(std::vector<store::ChartLibraryEntry> charts) {
    rows_.clear();
    rows_.reserve(charts.size());
    for (store::ChartLibraryEntry& entry : charts) {
        LibraryRow row;
        row.title = app::strip_rich_tags(entry.title);
        row.artist = app::strip_rich_tags(entry.artist);
        row.charter = app::strip_rich_tags(entry.charter);
        row.searchable =
            app::make_searchable(entry.title, entry.artist, entry.charter, entry.rootfolder);
        row.entry = std::move(entry);
        row.best_label = best_path_label(row.status, row.bestpath, row.summary);
        rows_.push_back(std::move(row));
    }
    resort();
    refilter();
}

std::vector<std::string> LibraryModel::hashes() const {
    std::vector<std::string> out;
    out.reserve(rows_.size());
    for (const LibraryRow& row : rows_) out.push_back(row.entry.md5);
    return out;
}

size_t LibraryModel::set_summaries(const std::vector<store::SummaryLookup>& lookups) {
    size_t changed = 0;
    const size_t n = std::min(rows_.size(), lookups.size());
    for (size_t i = 0; i < n; ++i)
        if (apply_summary(rows_[i], lookups[i])) ++changed;
    if (changed > 0) summaries_changed();
    return changed;
}

size_t LibraryModel::set_summary_for(const std::string& md5, const store::SummaryLookup& lookup) {
    size_t changed = 0;
    for (LibraryRow& row : rows_)
        if (row.entry.md5 == md5 && apply_summary(row, lookup)) ++changed;
    if (changed > 0) summaries_changed();
    return changed;
}

void LibraryModel::summaries_changed() {
    // A new score moves a row only under the Best path sort; a new status
    // can move it between chips and in or out of a stars:/squeeze filter.
    if (sort_column_ == LibrarySort::BestPath) resort();
    refilter();
}

void LibraryModel::set_query(std::string_view text) {
    if (text == query_text_) return;
    query_text_.assign(text.data(), text.size());
    query_ = app::parse_library_query(text);
    refilter();
}

void LibraryModel::set_chip(StatusChip chip) {
    if (chip == chip_) return;
    chip_ = chip;
    refilter();
}

void LibraryModel::set_sort(LibrarySort column, bool ascending) {
    if (column == sort_column_ && ascending == ascending_) return;
    sort_column_ = column;
    ascending_ = ascending;
    resort();
    refilter();
}

std::vector<size_t> LibraryModel::matches() const {
    if (query_.empty()) return sorted_;
    std::vector<size_t> out;
    for (size_t i : sorted_)
        if (matches_query(query_, rows_[i])) out.push_back(i);
    return out;
}

void LibraryModel::resort() {
    sorted_.resize(rows_.size());
    std::iota(sorted_.begin(), sorted_.end(), size_t{0});

    // Equal keys fall back to title, folder, then file path, always
    // ascending: one fixed order, so a row never jumps on a refresh. The
    // folded strings sort without case or accents getting in the way.
    auto tie_break = [this](size_t a, size_t b) {
        const LibraryRow& x = rows_[a];
        const LibraryRow& y = rows_[b];
        if (int c = x.searchable.title.compare(y.searchable.title)) return c < 0;
        if (int c = x.searchable.folder.compare(y.searchable.folder)) return c < 0;
        return x.entry.notespath < y.entry.notespath;
    };

    if (sort_column_ == LibrarySort::BestPath) {
        std::sort(sorted_.begin(), sorted_.end(), [&](size_t a, size_t b) {
            const LibraryRow& x = rows_[a];
            const LibraryRow& y = rows_[b];
            const bool xs = x.summary.score.has_value();
            const bool ys = y.summary.score.has_value();
            // Scored rows first in both directions: "not analyzed" is not a
            // low score.
            if (xs != ys) return xs;
            if (xs) {
                if (*x.summary.score != *y.summary.score)
                    return ascending_ ? *x.summary.score < *y.summary.score
                                      : *x.summary.score > *y.summary.score;
            } else {
                const int rx = unscored_rank(x.status), ry = unscored_rank(y.status);
                if (rx != ry) return rx < ry;
            }
            return tie_break(a, b);
        });
        return;
    }

    auto key = [this](const LibraryRow& r) -> const std::string& {
        switch (sort_column_) {
            case LibrarySort::Artist: return r.searchable.artist;
            case LibrarySort::Charter: return r.searchable.charter;
            case LibrarySort::Folder: return r.searchable.folder;
            case LibrarySort::Title:
            case LibrarySort::BestPath: break;
        }
        return r.searchable.title;
    };
    std::sort(sorted_.begin(), sorted_.end(), [&](size_t a, size_t b) {
        if (int c = key(rows_[a]).compare(key(rows_[b]))) return ascending_ ? c < 0 : c > 0;
        return tie_break(a, b);
    });
}

void LibraryModel::refilter() {
    counts_ = ChipCounts{};
    order_.clear();
    for (size_t i : sorted_) {
        const LibraryRow& row = rows_[i];
        if (!matches_query(query_, row)) continue;
        const StatusChip group = chip_of(row.status);
        ++counts_.all;
        if (group == StatusChip::NotAnalyzed) ++counts_.not_analyzed;
        else if (group == StatusChip::Stale) ++counts_.stale;
        else ++counts_.analyzed;
        if (chip_ == StatusChip::All || chip_ == group) order_.push_back(i);
    }
    // A chip whose last chart left it (the last Stale chart was re-analyzed,
    // or the search matches none of that group) falls back to All, so the
    // table never sits empty behind a filter with nothing in it.
    if (chip_ != StatusChip::All && counts_.of(chip_) == 0) {
        chip_ = StatusChip::All;
        refilter();
    }
}

}  // namespace hydra::ui
```

Add it to `hydra_ui` in `CMakeLists.txt`, after `src/ui/library_jobs.cpp` (T1 and T9 add other lines to the same list; keep theirs):

```cmake
    src/ui/library_jobs.cpp
    src/ui/library_model.cpp
```

- [ ] **Step 4: Run the model tests.**

Build `hydra_tests` as in Step 1, then run `.\build-cpp\Release\hydra_tests.exe -tc="library model:*"`. Expected: all six pass. The timing case prints one line per query; every query is under 20 ms. If one isn't, stop and report the printed times rather than loosening the bound.

- [ ] **Step 5: Replace the paging members on `AppState` (header).**

In `src/ui/app_state.h`, add the include after `#include "ui/library_jobs.h"`:

```cpp
#include "ui/library_jobs.h"
#include "ui/library_model.h"
```

Delete the whole `LibraryPage` struct (from `// One page of the library table, plus enough to know if there's more.` through its closing `};`).

In `LibraryViewState`, replace the search fields:

Before:
```cpp
    // The search box's text, whether it was filled from `search` yet, and
    // when it was last typed in (the library re-queries 0.25 s after).
    char search_buf[256] = "";
    bool search_synced = false;
    double search_edited_at = -1.0;
```
After:
```cpp
    // The search box's text and whether it was filled from `search` yet.
    // Typing is applied at most every 150 ms: `search_pending` holds an edit
    // not applied yet, `search_applied_at` when the last one was.
    char search_buf[256] = "";
    bool search_synced = false;
    bool search_pending = false;
    double search_applied_at = -1.0;
    // Whether the table's sort was read from its header yet. The ImGui
    // context outlives an AppState (the GUI tests build one per test), so a
    // fresh model reads the header's current sort on its first frame.
    bool sort_synced = false;
    // The panel state the table last fitted its Charter and Folder columns
    // to; unset = fit them on the next frame.
    std::optional<bool> columns_for_panel;
    // The selection (notespath) the table last scrolled to, so it scrolls
    // only when the selection changes (the panel's Previous/Next song).
    std::string scrolled_to;
```

Replace the library browsing block of `AppState`:

Before:
```cpp
    // Library browsing.
    std::string search;              // empty = no filter
    int table_viewpage = 0;

    // How many library rows fit the current window height. Recomputed by
    // library_view each frame from the available content region (rather than
    // a fixed constant) so a taller window shows more rows instead of
    // leaving blank space below a fixed-size table, and a shorter one still
    // fits without clipping. set_rows_per_page() re-queries the current page
    // only when the count actually changes.
    int rows_per_page = 15;
    void set_rows_per_page(int rows);

    LibraryPage current_page;
    int64_t library_total = 0;  // unfiltered count, for the "Library (N charts)" title
    void refresh_page();  // re-queries current_page + library_total from `store`
```
After:
```cpp
    // Library browsing: every scanned chart in memory, with its stored
    // summary, filtered by the search and the status chip and sorted by the
    // table (ui/library_model.h).
    LibraryModel library;
    std::string search;          // the applied search text; empty = no filter
    int64_t library_total = 0;   // every chart, for "5 of 97 charts"
    // Applies a search at once (the box throttles its own calls).
    void set_search(std::string text);
    // Re-reads every chart and summary: at startup and after a scan.
    void reload_library();
    // Re-reads every row's summary: after a settings change or a batch step.
    void refresh_library_summaries();
    // Re-reads one chart's summary: after one song's analysis is stored.
    void refresh_library_row(const std::string& md5);
    // Once per frame, from the library pane: reloads after a scan finishes,
    // and re-reads summaries while a batch runs -- at most once a second, and
    // only when the batch stored something since the last look.
    void tick_library(double now);

    // The rows on screen, in order, as indices into library.rows().
    const std::vector<size_t>& library_view_order() const { return library.order(); }
    size_t library_shown_count() const { return library.order().size(); }
    // The row at position `view_index` of library_view_order().
    const LibraryRow& library_row_at(size_t view_index) const {
        return library.rows()[library.order()[view_index]];
    }
    // How many charts the search matches (the "All" chip's count): the N of
    // "Analyze search (N)...".
    size_t library_match_count() const { return library.counts().all; }
    // Those charts, in table order: what "Analyze search (N)..." analyzes.
    std::vector<store::ChartLibraryEntry> library_matches() const;
```

In the private section, replace the page refresh:

Before:
```cpp
    // Re-reads each row's Best Path summary for the current page only.
    void refresh_summaries();
```
After:
```cpp
    // tick_library's memory: whether the current scan's result was read
    // yet, and the batch's stored count and time at the last summary read.
    bool scan_reloaded_ = true;
    int batch_seen_completed_ = 0;
    double batch_refreshed_at_ = -1.0;
```

- [ ] **Step 6: Replace the paging members on `AppState` (source).**

In `src/ui/app_state.cpp`, the constructor's last call:

Before:
```cpp
      committed_lens_(settings.lens()) {
    refresh_page();
}
```
After:
```cpp
      committed_lens_(settings.lens()) {
    reload_library();
}
```

Replace `refresh_page`, `refresh_summaries` and `set_rows_per_page` (the three functions from `void AppState::refresh_page() {` through the end of `set_rows_per_page`) with:

```cpp
void AppState::reload_library() {
    // The whole scan in one read. Every chart is needed anyway: the chips
    // count them and the search filters them in memory.
    library.set_charts(store->list_chart_library(std::nullopt, 0, -1));  // -1 = no limit
    library_total = static_cast<int64_t>(library.rows().size());
    library.set_query(search);
    refresh_library_summaries();
}

void AppState::refresh_library_summaries() {
    // One store call for the whole library (T7 splits it into chunks), on
    // this thread, never per row per frame: the batch workers share the
    // store's lock.
    library.set_summaries(store->get_summaries(library.hashes(), settings.chartmode_key(),
                                               settings.cap_query(), settings.lens()));
}

void AppState::refresh_library_row(const std::string& md5) {
    library.set_summary_for(md5, store->get_summary(settings.record_key(md5)));
}

void AppState::set_search(std::string text) {
    search = std::move(text);
    library.set_query(search);
}

std::vector<store::ChartLibraryEntry> AppState::library_matches() const {
    std::vector<store::ChartLibraryEntry> out;
    const std::vector<size_t> matched = library.matches();
    out.reserve(matched.size());
    for (size_t i : matched) out.push_back(library.rows()[i].entry);
    return out;
}

void AppState::tick_library(double now) {
    // A finished scan replaced the chart table: read it once.
    if (scan_job && !scan_reloaded_ && scan_job->snapshot().finished) {
        scan_reloaded_ = true;
        reload_library();
    }
    // A batch stores results on its own threads. Re-read the summaries at
    // most once a second, only when it stored something since the last read,
    // and once more when it ends so the last results show.
    if (batch_job) {
        const BatchJob::Snapshot snap = batch_job->snapshot();
        if (snap.completed != batch_seen_completed_ &&
            (snap.finished || now - batch_refreshed_at_ >= 1.0)) {
            batch_seen_completed_ = snap.completed;
            batch_refreshed_at_ = now;
            refresh_library_summaries();
        }
    }
}
```

In `start_scan`, mark the next scan's result unread:

Before:
```cpp
    scan_job = std::make_unique<ScanJob>(settings.chartfolders, *store);
    scan_job->start();
```
After:
```cpp
    scan_job = std::make_unique<ScanJob>(settings.chartfolders, *store);
    scan_reloaded_ = false;
    scan_job->start();
```

In `start_batch`, start the batch's summary watch from zero (T13 rewrites the rest of this function; keep this line next to wherever it creates the job):

Before:
```cpp
    report_started = false;
    report_outcome_shown = false;
    batch_job->start();
```
After:
```cpp
    report_started = false;
    report_outcome_shown = false;
    batch_seen_completed_ = 0;
    batch_refreshed_at_ = -1.0;
    batch_job->start();
```

In `store_finished_analysis`, one chart changed, so only its rows are re-read (T9 reworks the rest of this function; keep this call where the old one was):

Before:
```cpp
        refresh_viewed_record();
        refresh_page();  // the library row's Best Path cell is cached per page
        return "";
```
After:
```cpp
        refresh_viewed_record();
        refresh_library_row(song.md5);  // its row's Best path cell and chip
        return "";
```

In `apply_settings`, neither branch resets a page any more; both re-read the summaries, since the rows (the scan) never depend on the settings:

Before:
```cpp
    if (chartmode != committed_chartmode_) {
        // A different chart mode is a different library listing, so the user
        // starts over at page one.
        table_viewpage = 0;
        refresh_page();
        refresh_viewed_record();
    } else if (cap != committed_cap_ || lens != committed_lens_) {
        // The cap box and the search controls live in the details modal.
        // Resetting the page here would yank the library out from under a
        // user who never touched it. The page's rows and counts do not depend
        // on the cap or lens, so only the Best Path summaries are asked again.
        refresh_summaries();
        show_record_for_settings();
    }
```
After:
```cpp
    if (chartmode != committed_chartmode_) {
        // A different chart mode asks every chart a different question. The
        // rows themselves come from the scan and stay; their summaries and
        // the viewed record are read again.
        refresh_library_summaries();
        refresh_viewed_record();
    } else if (cap != committed_cap_ || lens != committed_lens_) {
        refresh_library_summaries();
        show_record_for_settings();
    }
```

- [ ] **Step 7: Update `tests/test_app_state.cpp` and add two cases.**

These tests read the paging members. The first chart in title order is the seeded one ("Song hash000"), so its row stands in for "the library's view of the seeded record".

Replace the comment and constant:

Before:
```cpp
// Enough charts that page 3 exists at the default 15 rows per page — the cap
// case has to show that the page is NOT reset, which needs a page to stay on.
const int kChartCount = 60;
```
After:
```cpp
// A library big enough to scroll; entry 0 ("Song hash000") sorts first by
// title, so its row is library_row_at(0).
const int kChartCount = 60;
```

In `app_on`:

Before:
```cpp
    REQUIRE(app->current_page.rows.size() == 15);
```
After:
```cpp
    REQUIRE(app->library_shown_count() == static_cast<size_t>(kChartCount));
    REQUIRE(app->library_row_at(0).entry.md5 == library_entry(0).md5);
    REQUIRE(app->library_row_at(0).status == RecordStatus::Ready);
```

In `commit_settings refreshes the viewed record when the chart mode changes`, delete `app->table_viewpage = 3;` and replace the page check:

Before:
```cpp
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);
    CHECK(app->table_viewpage == 0);  // a new listing starts at page one
    CHECK(records.changed(app->record_generation));
```
After:
```cpp
    CHECK(app->viewed.status == RecordStatus::NotAnalyzed);
    CHECK(app->library_row_at(0).status == RecordStatus::NotAnalyzed);  // the row follows
    CHECK(app->library_shown_count() == static_cast<size_t>(kChartCount));
    CHECK(records.changed(app->record_generation));
```

and after its final `CHECK(app->viewed.status == RecordStatus::Ready);` add `CHECK(app->library_row_at(0).status == RecordStatus::Ready);`.

Rename `commit_settings refreshes on an SP cap change without resetting the page` to `commit_settings refreshes the record and the library row on an SP cap change`. In it, delete `app->table_viewpage = 3;`, replace `CHECK(app->table_viewpage == 3);` after the change to 8 with `CHECK(app->library_row_at(0).status == RecordStatus::NotAnalyzed);`, and the one after the change back with `CHECK(app->library_row_at(0).status == RecordStatus::Ready);`. Its comment `// The cap box lives in the details modal; changing it must re-read the record but leave the library where the user left it.` becomes `// Changing the cap re-reads the record and the row's summary.`

In `commit_settings refreshes when the ms limit or the score range changes`, delete `app->table_viewpage = 3;` and replace `CHECK(app->table_viewpage == 3);  // the library stays where the user left it` with `CHECK(app->library_row_at(0).status == RecordStatus::NotAnalyzed);`.

Append two cases at the end of the file:

```cpp
// The search runs in memory over every chart; a word matches inside a title.
TEST_CASE("set_search narrows the library and the match count") {
    ScratchPaths paths("appstate_search");
    std::unique_ptr<AppState> app = app_on(paths);
    app->set_search("hash017");
    CHECK(app->library_shown_count() == 1);
    CHECK(app->library_match_count() == 1);
    CHECK(app->library_row_at(0).entry.md5 == "hash017");
    CHECK(app->library_matches().size() == 1);
    app->set_search("");
    CHECK(app->library_shown_count() == static_cast<size_t>(kChartCount));
}

// A result stored behind the view's back shows once its row is re-read, and
// only that row is asked about.
TEST_CASE("refresh_library_row picks up one chart's new result") {
    ScratchPaths paths("appstate_row");
    std::unique_ptr<AppState> app = app_on(paths);
    const ChartLibraryEntry fifth = library_entry(5);
    size_t at = app->library_shown_count();
    for (size_t i = 0; i < app->library_shown_count(); ++i)
        if (app->library_row_at(i).entry.md5 == fifth.md5) at = i;
    REQUIRE(at < app->library_shown_count());
    CHECK(app->library_row_at(at).status == RecordStatus::NotAnalyzed);

    HydraRecord record;
    record.sp_cap = kSeededCap;
    record.ms_limit = Settings{}.mslimit_value;
    app->store->add_record(
        RecordKey{fifth.md5, kChartMode, CapQuery::at(kSeededCap), Settings{}.lens()}, record);
    app->refresh_library_row(fifth.md5);
    CHECK(app->library_row_at(at).status == RecordStatus::Ready);
    CHECK(app->library.counts().analyzed == 2);
}
```

Build `hydra_tests` and run `.\build-cpp\Release\hydra_tests.exe -tc="*commit_settings*,*set_search*,*refresh_library_row*"`. Expected: the build fails until Step 8 (the old `library_table.cpp` still calls the removed members); after Step 8 they all pass.

- [ ] **Step 8: Rewrite `library_table.cpp`.**

T1 moved `render_search_box`, `summary_label` and `render_library_table` into `src/ui/library_table.cpp`, in `namespace hydra::ui::detail`, with their declarations in `library_parts.h`. Nearly all of that code goes, so write the new file to `src/ui/library_table.cpp.new` with the Write tool and move it over with `Move-Item -Force src/ui/library_table.cpp.new src/ui/library_table.cpp`.

```cpp
// The library pane: the heading, the search box with its hint and errors,
// the four status chips, and the sortable, scrolling table of every chart.
// What it shows comes from AppState::library (ui/library_model.h); this file
// draws it and hands clicks back. It replaced the paged table in the 2026-09
// interface redesign (Task 12).

#include "ui/library_parts.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "app/library_query.h"
#include "core/model.h"  // group_thousands
#include "imgui.h"
#include "ui/app_state.h"
#include "ui/fonts.h"
#include "ui/library_model.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace hydra::ui::detail {

namespace {

// Typing is applied at most this often, so a burst of keys on a big library
// filters a few times rather than once per key.
constexpr double kSearchThrottleSeconds = 0.15;

// The table's columns, by index. Each column's user ID is its LibrarySort.
constexpr int kColumnTitle = 0;
constexpr int kColumnArtist = 1;
constexpr int kColumnCharter = 2;
constexpr int kColumnFolder = 3;
constexpr int kColumnBestPath = 4;

// The chips' look, from the approved mockup: the selected chip is filled
// teal with an accent border, the others are outlined only. White on the
// selected fill is about 7:1.
const ImVec4 kChipOnColor{0 / 255.0f, 102 / 255.0f, 102 / 255.0f, 1.0f};
const ImVec4 kChipOnHoveredColor{0 / 255.0f, 122 / 255.0f, 122 / 255.0f, 1.0f};
const ImVec4 kChipOffColor{0.0f, 0.0f, 0.0f, 0.0f};
const ImVec4 kChipOffHoveredColor{60 / 255.0f, 60 / 255.0f, 64 / 255.0f, 1.0f};
const ImVec4 kChipOffBorderColor{74 / 255.0f, 74 / 255.0f, 80 / 255.0f, 1.0f};

// Matched text: a dark gold box behind it and light gold letters (the
// mockup's highlight).
const ImU32 kMatchBgColor = IM_COL32(0x4d, 0x42, 0x00, 0xff);
const ImU32 kMatchTextColor = IM_COL32(0xff, 0xe6, 0x80, 0xff);

// "1 chart", "97 charts", "12,345 charts".
std::string charts_text(size_t n) {
    return group_thousands(static_cast<int64_t>(n)) + (n == 1 ? " chart" : " charts");
}

void clear_search(AppState& app) {
    app.library_ui.search_buf[0] = '\0';
    app.library_ui.search_pending = false;
    app.library_ui.search_applied_at = ImGui::GetTime();
    app.set_search("");
}

// "Library   5 of 97 charts".
void render_heading(AppState& app) {
    ImGui::TextUnformatted("Library");
    ImGui::SameLine();
    const size_t shown = app.library_shown_count();
    const size_t total = app.library.rows().size();
    const std::string count =
        shown == total ? charts_text(total)
                       : group_thousands(static_cast<int64_t>(shown)) + " of " + charts_text(total);
    ImGui::TextColored(kNewSongColor, "%s", count.c_str());
}

void render_search_box(AppState& app) {
    LibraryViewState& ui = app.library_ui;
    char (&buf)[256] = ui.search_buf;
    if (!ui.search_synced) {
        std::snprintf(buf, sizeof(buf), "%s", app.search.c_str());
        ui.search_synced = true;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float clear_w = ImGui::CalcTextSize("X").x + style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(clear_w + style.ItemSpacing.x));

    // Ctrl+F jumps here from anywhere except behind a dialog: focusing a
    // control behind an open popup would fight its focus.
    const bool any_popup =
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!any_popup && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
        ImGui::SetKeyboardFocusHere();

    // EscapeClearsAll: the first Escape empties the box, the next leaves it.
    ImGui::PushFont(g_mono_font, 0.0f);
    const bool edited =
        ImGui::InputTextWithHint("##search", "Search title, artist, charter or folder", buf,
                                 sizeof(buf), ImGuiInputTextFlags_EscapeClearsAll);
    ImGui::PopFont();
    hint("Ctrl+F jumps here. Escape clears it.");
    if (edited) ui.search_pending = true;

    // An emptied box applies at once; typing applies at most every 150 ms.
    const double now = ImGui::GetTime();
    if (ui.search_pending &&
        (buf[0] == '\0' || now - ui.search_applied_at >= kSearchThrottleSeconds)) {
        ui.search_pending = false;
        ui.search_applied_at = now;
        app.set_search(buf);
    }

    ImGui::SameLine();
    if (ImGui::Button("X##clearsearch")) clear_search(app);
    hint("Clear search (Esc)");

    // What the box understands, and what it couldn't.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kNewSongColor,
                       "Quotes match an exact phrase. Narrow with artist: charter: folder: "
                       "stars:7 squeeze<=20");
    for (const std::string& error : app.library.query().errors)
        ImGui::TextColored(kWarningColor, "%s", error.c_str());
    ImGui::PopTextWrapPos();
}

bool chip_button(const char* label, bool on, bool disabled) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, on ? kChipOnColor : kChipOffColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? kChipOnHoveredColor : kChipOffHoveredColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? kChipOnHoveredColor : kChipOffHoveredColor);
    ImGui::PushStyleColor(ImGuiCol_Border, on ? kAccentColor : kChipOffBorderColor);
    begin_disabled_button(disabled);
    const bool clicked = ImGui::Button(label);
    end_disabled_button(disabled);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    return clicked;
}

// All (N), Not analyzed (N), Stale (N), Analyzed (N): counts over what the
// search matches. A group with nothing in it can't be picked.
void render_chips(AppState& app) {
    struct Chip {
        StatusChip chip;
        const char* name;
        const char* id;
    };
    static constexpr Chip kChips[] = {
        {StatusChip::All, "All", "chipall"},
        {StatusChip::NotAnalyzed, "Not analyzed", "chipnew"},
        {StatusChip::Stale, "Stale", "chipstale"},
        {StatusChip::Analyzed, "Analyzed", "chipdone"},
    };
    const ChipCounts& counts = app.library.counts();
    for (size_t i = 0; i < std::size(kChips); ++i) {
        const Chip& c = kChips[i];
        if (i > 0) ImGui::SameLine();
        const size_t n = counts.of(c.chip);
        const std::string label = std::string(c.name) + " (" +
                                  group_thousands(static_cast<int64_t>(n)) + ")##" + c.id;
        const bool on = app.library.chip() == c.chip;
        if (chip_button(label.c_str(), on, !on && n == 0)) app.library.set_chip(c.chip);
        if (c.chip == StatusChip::Stale)
            hint("Analyzed by another Hydra version, or under different rules in "
                 "hydra_rules.ini. Re-analyze to refresh.");
    }
}

// Draws the matched parts of text already drawn at `pos` again, in the
// highlight colours, clipped at max_x.
void overlay_matches(ImVec2 pos, const std::string& text, const std::vector<app::MatchSpan>& spans,
                     float max_x) {
    if (spans.empty()) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float h = ImGui::GetTextLineHeight();
    draw->PushClipRect(pos, ImVec2(max_x, pos.y + h), true);
    for (const app::MatchSpan& s : spans) {
        const float x0 = pos.x + ImGui::CalcTextSize(text.data(), text.data() + s.begin).x;
        const float x1 = pos.x + ImGui::CalcTextSize(text.data(), text.data() + s.end).x;
        if (x0 >= max_x) break;
        draw->AddRectFilled(ImVec2(x0, pos.y), ImVec2(x1, pos.y + h), kMatchBgColor, 2.0f);
        draw->AddText(ImVec2(x0, pos.y), kMatchTextColor, text.data() + s.begin,
                      text.data() + s.end);
    }
    draw->PopClipRect();
}

// A cell's text, ellipsized, with the query's matches highlighted.
void cell_text(const std::string& text, const std::vector<app::MatchSpan>& spans) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float max_x = pos.x + ImGui::GetContentRegionAvail().x;
    text_ellipsized(text.c_str());
    overlay_matches(pos, text, spans, max_x);
}

// Which hidden columns this frame's second lines named, for the footer.
struct SecondLineUse {
    bool folder = false;
    bool charter = false;
};

// The second line under a title while a search is on: where the row matched
// when that place isn't a visible column. The folder by default.
struct SecondLine {
    std::string text;
    std::vector<app::MatchSpan> spans;
    bool is_charter = false;
};

SecondLine second_line(const app::LibraryQuery& q, const LibraryRow& row, bool folder_shown,
                       bool charter_shown) {
    SecondLine line;
    line.text = row.entry.rootfolder;
    if (!folder_shown) line.spans = app::match_spans(q, app::QueryField::Folder, line.text);
    if (line.spans.empty() && !charter_shown) {
        std::vector<app::MatchSpan> spans = app::match_spans(q, app::QueryField::Charter, row.charter);
        if (!spans.empty()) {
            const std::string prefix = "charted by ";
            line.text = prefix + row.charter;
            for (app::MatchSpan& s : spans) {
                s.begin += prefix.size();
                s.end += prefix.size();
            }
            line.spans = std::move(spans);
            line.is_charter = true;
        }
    }
    return line;
}

SecondLineUse render_table(AppState& app, ImVec2 size) {
    SecondLineUse used;
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuterH |
        ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##librarytable", 5, flags, size)) return used;

    ImGui::TableSetupScrollFreeze(0, 1);  // the header row stays on screen
    ImGui::TableSetupColumn("Title",
                            ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort |
                                ImGuiTableColumnFlags_NoHide,
                            1.0f, static_cast<ImGuiID>(LibrarySort::Title));
    ImGui::TableSetupColumn("Artist", ImGuiTableColumnFlags_WidthStretch, 0.75f,
                            static_cast<ImGuiID>(LibrarySort::Artist));
    ImGui::TableSetupColumn("Charter", ImGuiTableColumnFlags_WidthStretch, 0.5f,
                            static_cast<ImGuiID>(LibrarySort::Charter));
    ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_WidthStretch, 0.75f,
                            static_cast<ImGuiID>(LibrarySort::Folder));
    // Highest score first on the first click.
    ImGui::TableSetupColumn("Best path",
                            ImGuiTableColumnFlags_WidthStretch |
                                ImGuiTableColumnFlags_PreferSortDescending,
                            1.0f, static_cast<ImGuiID>(LibrarySort::BestPath));

    // Charter and Folder make way for the song panel: hidden when it opens,
    // shown when it closes. In between, the header's right-click menu shows
    // or hides any column but Title.
    // T9: read app.details_open() here once it exists.
    const bool panel_open = app.show_details;
    if (app.library_ui.columns_for_panel != panel_open) {
        ImGui::TableSetColumnEnabled(kColumnCharter, !panel_open);
        ImGui::TableSetColumnEnabled(kColumnFolder, !panel_open);
        app.library_ui.columns_for_panel = panel_open;
    }
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if ((specs->SpecsDirty || !app.library_ui.sort_synced) && specs->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
            app.library.set_sort(static_cast<LibrarySort>(s.ColumnUserID),
                                 s.SortDirection != ImGuiSortDirection_Descending);
        }
        specs->SpecsDirty = false;
        app.library_ui.sort_synced = true;
    }

    const app::LibraryQuery& q = app.library.query();
    const bool folder_shown =
        (ImGui::TableGetColumnFlags(kColumnFolder) & ImGuiTableColumnFlags_IsEnabled) != 0;
    const bool charter_shown =
        (ImGui::TableGetColumnFlags(kColumnCharter) & ImGuiTableColumnFlags_IsEnabled) != 0;
    const bool searching_words = !q.terms.empty();
    // Every row gets the same height (the clipper needs that): two lines
    // while a word search is on and a column is hidden, one otherwise.
    const bool two_lines = searching_words && (!folder_shown || !charter_shown);
    const float line_h = ImGui::GetTextLineHeight();
    const float line_gap = ImGui::GetStyle().ItemSpacing.y;
    const float row_h = two_lines ? line_h * 2.0f + line_gap : line_h;

    const std::vector<size_t>& order = app.library_view_order();
    const std::vector<LibraryRow>& rows = app.library.rows();
    const std::string selected_path = app.selected ? app.selected->notespath : std::string();

    // A new selection (a click, or the panel's Previous/Next) is scrolled
    // into view once.
    std::optional<size_t> scroll_to;
    if (selected_path != app.library_ui.scrolled_to) {
        app.library_ui.scrolled_to = selected_path;
        for (size_t k = 0; k < order.size(); ++k)
            if (rows[order[k]].entry.notespath == selected_path) {
                scroll_to = k;
                break;
            }
    }

    // Only the rows on screen are drawn.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(order.size()));
    if (scroll_to) clipper.IncludeItemByIndex(static_cast<int>(*scroll_to));
    while (clipper.Step()) {
        for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k) {
            const size_t index = order[static_cast<size_t>(k)];
            const LibraryRow& row = rows[index];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, row_h);
            ImGui::PushID(static_cast<int>(index));

            // The row's Selectable goes first, spanning every column, so the
            // whole row is one click target. Its label is the title, which is
            // also how GUI tests click a row ("**/<title>").
            ImGui::TableSetColumnIndex(kColumnTitle);
            const ImVec2 title_pos = ImGui::GetCursorScreenPos();
            const float title_w = ImGui::GetContentRegionAvail().x;
            const bool selected = !selected_path.empty() && row.entry.notespath == selected_path;
            if (ImGui::Selectable(row.title.c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, row_h)))
                app.select(row.entry);
            if (ImGui::TableGetHoveredColumn() == kColumnTitle &&
                ImGui::CalcTextSize(row.title.c_str()).x > title_w)
                overflow_tooltip(row.title.c_str());
            if (scroll_to && *scroll_to == static_cast<size_t>(k)) ImGui::SetScrollHereY(0.5f);
            if (searching_words)
                overlay_matches(title_pos, row.title,
                                app::match_spans(q, app::QueryField::Title, row.title),
                                title_pos.x + title_w);
            if (two_lines) {
                const SecondLine line = second_line(q, row, folder_shown, charter_shown);
                ImGui::SetCursorScreenPos(ImVec2(title_pos.x, title_pos.y + line_h + line_gap));
                ImGui::PushStyleColor(ImGuiCol_Text, kNewSongColor);
                const ImVec2 line_pos = ImGui::GetCursorScreenPos();
                text_ellipsized(line.text.c_str());
                ImGui::PopStyleColor();
                overlay_matches(line_pos, line.text, line.spans, title_pos.x + title_w);
                if (!line.spans.empty()) (line.is_charter ? used.charter : used.folder) = true;
            }

            // Every column but Title can be hidden from the header menu, and a
            // hidden column's TableSetColumnIndex returns false.
            if (ImGui::TableSetColumnIndex(kColumnArtist))
                cell_text(row.artist,
                          searching_words ? app::match_spans(q, app::QueryField::Artist, row.artist)
                                          : std::vector<app::MatchSpan>{});
            if (ImGui::TableSetColumnIndex(kColumnCharter))
                cell_text(row.charter,
                          searching_words ? app::match_spans(q, app::QueryField::Charter, row.charter)
                                          : std::vector<app::MatchSpan>{});
            if (ImGui::TableSetColumnIndex(kColumnFolder))
                cell_text(row.entry.rootfolder,
                          searching_words
                              ? app::match_spans(q, app::QueryField::Folder, row.entry.rootfolder)
                              : std::vector<app::MatchSpan>{});

            if (!ImGui::TableSetColumnIndex(kColumnBestPath)) {
                ImGui::PopID();
                continue;
            }
            const ImVec4 color = row.status == store::RecordStatus::Ready   ? kBestPathColor
                                 : row.status == store::RecordStatus::Stale ? kWarningColor
                                                                            : kNewSongColor;
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushFont(g_mono_font, 0.0f);
            text_ellipsized(row.best_label.c_str());
            ImGui::PopFont();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                if (row.status == store::RecordStatus::NotAnalyzed)
                    ImGui::SetTooltip("Not analyzed yet. Open the song and press \"Analyze this "
                                      "song\", or use \"Analyze library...\".");
                else if (row.status == store::RecordStatus::Stale)
                    ImGui::SetTooltip("Analyzed by another Hydra version, or under different "
                                      "rules in hydra_rules.ini. Re-analyze to refresh it.");
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    return used;
}

// Under the table while a search is on: why the rows on screen matched when
// the reason is in a hidden column, and a way out.
void render_footer(AppState& app, const SecondLineUse& used) {
    const char* why = used.folder && used.charter ? "Matched on folder and charter."
                      : used.folder               ? "Matched on folder."
                      : used.charter              ? "Matched on charter."
                                                  : nullptr;
    if (why) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kNewSongColor, "%s", why);
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Clear search")) clear_search(app);
}

}  // namespace

void render_library(AppState& app) {
    // Job-driven refreshes first, every frame, whatever is drawn below.
    app.tick_library(ImGui::GetTime());
    // An empty library shows the main window's "no songs" message instead.
    if (app.library_total == 0) return;

    render_heading(app);
    render_search_box(app);
    render_chips(app);
    ImGui::Spacing();

    if (app.library_shown_count() == 0) {
        // The chips fall back to All when their group empties, so nothing
        // shown means the search matched nothing.
        ImGui::TextUnformatted("No charts match your search.");
        if (ImGui::Button("Clear search")) clear_search(app);
        return;
    }

    const bool searching = !app.library.query().empty();
    const float footer_h = searching ? ImGui::GetFrameHeightWithSpacing() : 0.0f;
    const float table_h = std::max(ImGui::GetContentRegionAvail().y - footer_h,
                                   ImGui::GetTextLineHeightWithSpacing() * 4.0f);
    const SecondLineUse used = render_table(app, ImVec2(0.0f, table_h));
    if (searching) render_footer(app, used);
}

}  // namespace hydra::ui::detail
```

In `src/ui/library_parts.h`, replace the declarations T1 wrote for the three old functions of `library_table.cpp` (`render_search_box`, `summary_label`, `render_library_table`) with the one entry point:

```cpp
// library_table.cpp: the whole library pane (heading, search, chips, table).
// Called every frame inside the "##library" child, even when the library is
// empty: it also runs AppState::tick_library.
void render_library(AppState& app);
```

If `library_parts.h` included `ui/app_state.h` only for `LibraryPage` (the old `summary_label` parameter), keep the include anyway: `render_library` takes an `AppState&`.

- [ ] **Step 9: Build the app and the unit tests.**

Build `hydra_tests` and `Hydra` (`-Target Hydra`). Expected: both link. Until T9's merged `render_main_window` calls `detail::render_library(app)`, this worktree's `library_view.cpp` still calls the removed functions, so fix only that call site here, minimally, so the worktree builds and the GUI tests can run: in `render_main_window`, replace everything from `render_view_controls(app);` through the end of the `if (app.current_page.total_count > 0) { ... } else { ... }` block with:

```cpp
    render_view_controls(app);
    detail::render_library(app);
    if (app.library_total == 0)
        ImGui::TextUnformatted(
            "No songs scanned. Click \"Manage folders...\" to add your song folder, "
            "then \"Scan charts\" to get started!");
```

and in `render_actions_row` (T1 moved it to `library_toolbar.cpp`), replace `app.current_page.total_count` with `static_cast<int64_t>(app.library_match_count())` in both places. In the batch modal (`library_dialogs.cpp`), replace `app.current_page.total_count` the same way, delete `app.table_viewpage = 0;` and turn both `app.refresh_page();` calls into nothing (the tick reloads after a scan and after a batch). The main session drops these interim edits in favour of T9's and T13's versions at merge; they exist only so this worktree compiles.

Then run `.\build-cpp\Release\hydra_tests.exe`. Expected: `[doctest] Status: SUCCESS!`.

- [ ] **Step 10: Write the GUI tests.**

In `tests/ui/uitest_library.cpp`, add `#include <cstring>` and `#include "ui/library_model.h"` to the includes, then these two tests inside the file's anonymous namespace, and add `{"library-search", test_library_search}` and `{"library-sort-scroll", test_library_sort_scroll}` to its entry table.

```cpp
// The search box, the chips, the second line and the empty state, on the
// scratch library (97 charts in testdata\input).
void test_library_search(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    const auto shown = [&] { return h.app->library_shown_count(); };

    IM_CHECK_EQ(shown(), (size_t)97);
    IM_CHECK(visible_text(h).find("97 charts") != std::string::npos);
    IM_CHECK(ctx->ItemExists("**/All (97)##chipall"));
    IM_CHECK(ctx->ItemExists("**/Not analyzed (97)##chipnew"));
    IM_CHECK((ctx->ItemInfo("**/Stale (0)##chipstale").ItemFlags & ImGuiItemFlags_Disabled) != 0);

    // A quoted phrase: the five charts under "...\Tier 4".
    ctx->ItemInputValue("**/##search", "\"tier 4\"");
    IM_CHECK(wait_until(ctx, [&] { return shown() == 5; }, 5));
    IM_CHECK(ctx->ItemExists("**/All (5)##chipall"));
    std::string text = visible_text(h);
    IM_CHECK(text.find("5 of 97 charts") != std::string::npos);
    for (const char* title : {"Burnout", "Chair", "Limb From Limb", "Unbound (The Wild Ride)", "YYZ"})
        IM_CHECK(text.find(title) != std::string::npos);

    // With a song open, Folder makes way, so each row says where it matched.
    h.app->select(h.app->library_row_at(0).entry);
    IM_CHECK(wait_until(ctx, [&] { return !ctx->ItemExists("**/Folder"); }, 5));
    IM_CHECK(wait_until(ctx, [&] {
        return visible_text(h).find("Matched on folder.") != std::string::npos;
    }, 5));
    h.app->close_details();
    IM_CHECK(wait_until(ctx, [&] { return ctx->ItemExists("**/Folder"); }, 5));

    // Words in any order, across title and artist.
    ctx->ItemInputValue("**/##search", "green burnout");
    IM_CHECK(wait_until(ctx, [&] { return shown() == 1; }, 5));
    IM_CHECK(h.app->library_row_at(0).title == "Burnout");

    // A charter stored with colour tags is found and drawn without them.
    ctx->ItemInputValue("**/##search", "bloodline");
    IM_CHECK(wait_until(ctx, [&] { return shown() == 1; }, 5));
    IM_CHECK(h.app->library_row_at(0).title == "Acid Romance");
    text = visible_text(h);
    IM_CHECK(text.find("Bloodline") != std::string::npos);
    IM_CHECK(text.find("<color=") == std::string::npos);

    // A filter it can't read says so under the box.
    ctx->ItemInputValue("**/##search", "stars:9");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->library.query().errors.empty(); }, 5));
    IM_CHECK(visible_text(h).find(h.app->library.query().errors[0]) != std::string::npos);

    // Nothing matches: the empty state, and Clear search brings it all back.
    ctx->ItemInputValue("**/##search", "zzqx");
    IM_CHECK(wait_until(ctx, [&] { return shown() == 0; }, 5));
    IM_CHECK(visible_text(h).find("No charts match your search.") != std::string::npos);
    ctx->ItemClick("**/Clear search");
    IM_CHECK(wait_until(ctx, [&] { return shown() == 97 && h.app->search.empty(); }, 5));

    // Escape in the box clears it; a second Escape leaves the box.
    ctx->ItemInputValue("**/##search", "chair");
    IM_CHECK(wait_until(ctx, [&] { return h.app->search == "chair"; }, 5));
    ctx->ItemClick("**/##search");
    ctx->KeyPress(ImGuiKey_Escape);
    IM_CHECK(wait_until(ctx, [&] { return h.app->search.empty(); }, 5));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);

    // Ctrl+F puts the cursor in the box.
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_F);
    ctx->Yield(2);
    IM_CHECK_EQ(ctx->UiContext->ActiveId, ctx->ItemInfo("**/##search").ID);
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
}

// Sorting and scrolling: every chart is one scroll away, and only the rows on
// screen are drawn.
void test_library_sort_scroll(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");

    IM_CHECK(h.app->library.sort_column() == hydra::ui::LibrarySort::Title);
    IM_CHECK(h.app->library.ascending());
    const size_t count = h.app->library_shown_count();
    const std::string first = h.app->library_row_at(0).title;
    const std::string last = h.app->library_row_at(count - 1).title;
    IM_CHECK(visible_text(h).find(first) != std::string::npos);
    IM_CHECK(visible_text(h).find(last) == std::string::npos);  // off screen, not drawn
    IM_CHECK(visible_text(h).find("Not analyzed") != std::string::npos);
    IM_CHECK(visible_text(h).find("-----") == std::string::npos);  // no filler rows

    // The table scrolls like any list: its end brings the last title.
    ImGuiWindow* table = nullptr;
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if ((w->Flags & ImGuiWindowFlags_ChildWindow) && std::strstr(w->Name, "##librarytable"))
            table = w;
    IM_CHECK(table != nullptr);
    if (table == nullptr) return;
    ctx->ScrollToBottom(ImGuiTestRef(table->ID));
    IM_CHECK(wait_until(ctx, [&] { return visible_text(h).find(last) != std::string::npos; }, 5));

    // The Title header reverses the order; a second click restores it (the
    // ImGui context, and so the table's sort, outlives this test's app).
    ctx->ItemClick("**/Title");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->library.ascending(); }, 5));
    IM_CHECK(h.app->library_row_at(0).title == last);
    ctx->ItemClick("**/Title");
    IM_CHECK(wait_until(ctx, [&] { return h.app->library.ascending(); }, 5));
    IM_CHECK(h.app->library_row_at(0).title == first);
}
```

Then update the existing tests T1 placed in `uitest_library.cpp`, by function name:

- `test_scan`: `h.app->current_page.rows[0].title` becomes `h.app->library_row_at(0).title`.
- `test_view_settings`: delete the "page arrows" block (`// The page arrows move one page each way.` through the second `IM_CHECK_EQ(h.app->table_viewpage, 0);`), the `ctx->ItemClick("##pageright");` and `IM_CHECK_EQ(h.app->table_viewpage, 1);` before the Pro Drums click, and the `IM_CHECK_EQ(h.app->table_viewpage, 0);` after it; change the comment `// Pro Drums off is a different chart mode: persisted at once, and the library starts over at page one.` to `// Pro Drums off is a different chart mode: persisted at once.` and the function's header comment `the page arrows,` goes.
- `test_library_state_per_app`: `ctx->ItemInputValue("##search", "zzqx");` becomes `ctx->ItemInputValue("**/##search", "zzqx");` (the box now sits in the `##library` child).
- `test_difficulty`, if T1 put it here: `"##search"` becomes `"**/##search"`, `!h.app->current_page.rows.empty()` becomes `h.app->library_shown_count() > 0`, and `h.app->current_page.summaries[0].state` becomes `h.app->library_row_at(0).status`.

- [ ] **Step 11: Build and run the GUI tests.**

Build `hydra_uitest` (`-Target hydra_uitest`), then run `.\build-cpp\Release\hydra_uitest.exe --test library-search` and `--test library-sort-scroll`. Expected: `[PASS]` for each. Then run `.\build-cpp\Release\hydra_uitest.exe --all`; every test in `uitest_library.cpp` passes. Tests in other files may fail here until their owners' wave-3 changes (Step 12) are merged; list any failures in the report with the file each is in.

- [ ] **Step 12: Record what other files must change (for the merge).**

Removing the paging members breaks every other reader of them. Those files belong to T9, T11 and T13 in this wave, so this task does not edit them beyond Step 9's interim lines. Write this list into the task report for the main session. Each change is mechanical:

- `h.app->current_page.rows[i]` → `h.app->library_row_at(i).entry` (or `.title` for the drawn title, which is what `**/<title>` clicks need).
- `h.app->current_page.rows.size()` / `.empty()` → `h.app->library_shown_count()` / `== 0`.
- `h.app->current_page.summaries[i].state` → `h.app->library_row_at(i).status`.
- `h.app->current_page.total_count` → `h.app->library_match_count()`.
- `h.app->search = X; h.app->refresh_page();` → `h.app->set_search(X);`.
- `ctx->ItemInputValue("##search", ...)` from `//Hydra` → `ctx->ItemInputValue("**/##search", ...)`.
- A test that searches for a whole title should put it in quotes, `"\"" + title + "\""`, so it stays one phrase under the new parser.

Where they are today (function names; T1 decides the file): the harness's canned leaderboard reply in `reset_app` (`current_page.rows[0].md5`) and `dump_state` (prints the page; print `library_total`, `library_shown_count()`, `search` and the first 20 shown rows instead) in `uitest_harness.cpp`; `scan_library` and `open_details`; `test_analyze` (five `summaries[0]` checks); `test_settings_and_reports`, `test_report_buttons` (both read `total_count` for the "Analyze search (N)" label and type a title into `##search`); `test_dynamics_stored` and `open_titled` (both set `search` and call `refresh_page`); `test_squeezed_out_uncounted` (types into `##search`, waits on `current_page.rows`). In the app: `render_actions_row` (`current_page.total_count` for the Analyze label and count), `render_batch_modal` (`current_page.total_count`, `refresh_page` after a batch), `render_scan_modal` (`table_viewpage = 0; refresh_page();` after a scan, now done by `tick_library`), and `render_main_window` (the old table call, `set_rows_per_page`, the "Library (N charts)" separator, which `render_library`'s heading replaces).

- [ ] **Step 13: Commit.**

```
git add src/ui/library_model.h src/ui/library_model.cpp src/ui/library_table.cpp src/ui/library_parts.h src/ui/app_state.h src/ui/app_state.cpp src/ui/library_view.cpp src/ui/library_toolbar.cpp src/ui/library_dialogs.cpp CMakeLists.txt tests/test_library_model.cpp tests/test_app_state.cpp tests/ui/uitest_library.cpp
git commit -m "Library: one scrolling, sortable table with chips and the new search

Task: T12 library table and search
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Batch analysis in the background, and dialogs that answer the keyboard

Today "Analyze library" opens a modal that locks the whole app for as long as the batch runs, which can be hours. It shows no elapsed time or time left, and its "Cancel" keeps finished results, which Windows calls Stop (audit finding 7). The report it builds is called "failed" when only the browser failed to open, and "Open report again" shows for a report that never opened (finding 8). Escape closes no dialog, the status line is orange for everything and fades after 6 seconds even for a problem, and some counts read "chart(s)" (finding 10). This task follows the approved Confirm, Batch and Done mockups. "Analyze library..." asks once, listing every setting it will use and how many charts already have a result. The batch then runs in a teal strip under the toolbar while you keep browsing, with the count, the chart being analyzed, elapsed time, time left, Pause and Stop. When it ends, a strip says where the report was saved, with Open report and Show in folder, and a clear warning when only the browser failed. The dialogs answer Escape and Enter, the status line uses neutral text for news and orange only for problems (which stay until dismissed), and counts read "1 chart" and "12,345 charts".

**Wave:** 3. **Depends on:** T1 (`library_dialogs.cpp`, `library_toolbar.cpp`, `library_parts.h`), T3 (`reports_dir()`, `DmReportStats` with `matched`, `above_optimal`, `not_analyzed`, `not_in_library`), T4 (`BatchJob` pause/resume/stop, its snapshot, the chart-list constructor, `ReportJob`/`DmReportJob` `saved_path()`/`opened()`/`open_problem()`, `message()`, `DmReportJob::stats()`), T6 (`Settings::sp_cap` is an `int`). **Expected overlaps:** T9 in `library_view.cpp` (T9 owns the layout and leaves a `// T13: batch strips` marker between the toolbar and the settings bar; this task's two strip calls go there at merge), in `library_toolbar.cpp` (T9 deletes `render_view_controls`, renames the scan button to `Scan library` and moves the "Open path report" button into `render_actions_row`; this task rewrites `render_actions_row` whole and keeps both), in `library_parts.h` (both add declarations; keep both), in `app_state.{h,cpp}` (different members) and in `app_shell.cpp` (T9 adds `app.tick(...)`, this task adds `app.update_background_jobs();` right after it; keep both). T12 in `app_state.{h,cpp}` and `library_view.cpp`: two bodies read the old paged library in this worktree and switch to T12's model at merge (`open_batch_confirm` and the "Analyze search (N)..." count; see the merge checklist). T12's interim edits to `render_actions_row` and the batch modal in `library_dialogs.cpp` are dropped in favour of this task's versions.

**Goal:** A library batch runs in a strip you can glance at while you browse, with Pause, Stop and an honest ending, and every dialog answers Escape and Enter.

**Files:**
- Create: `tests/test_batch_text.cpp`
- Modify: `src/ui/library_dialogs.cpp` (the confirm, the two strips, the scan and folder modals' keys, the dm picker)
- Modify: `src/ui/library_toolbar.cpp` (`render_actions_row`, `render_status_line`)
- Modify: `src/ui/library_parts.h` (the new declarations; `render_batch_modal` goes)
- Modify: `src/ui/library_view.cpp` (the two strip calls, the confirm call, the report-reap block leaves, the empty-library line)
- Modify: `src/ui/win32_dialogs.h`, `src/ui/win32_dialogs.cpp` (`show_in_folder` and its test seam)
- Modify: `src/ui/app_state.h`, `src/ui/app_state.cpp` (status kinds, the batch scope, `update_background_jobs`, parked leaderboard jobs)
- Modify: `src/ui/app_shell.cpp` (one call in `run_frame`; nobody owns it in wave 3)
- Modify: `CMakeLists.txt` (`tests/test_batch_text.cpp` in `hydra_tests`)
- Test: `tests/ui/uitest_batch_reports.cpp`

**Acceptance Criteria:**
- [ ] `hydra_tests.exe -tc="batch text*"` passes: `count_label(1, "chart", "charts") == "1 chart"`, `count_label(12345, ...) == "12,345 charts"`, `format_duration(3725) == "1:02:05"`, and the settings summary reads `Expert · Pro Drums · 2x Bass`, `4 bars (Clone Hero's rule)`, `4 scores`, `10 ms` for default settings.
- [ ] `hydra_uitest --test batch-confirm` passes: the `Analyze library` popup says `Analyze 97 charts that have no result yet?` and lists the four settings; Escape closes it with no batch; Enter starts one, which shows `##batchstrip`.
- [ ] `hydra_uitest --test batch-pause-stop` passes: `Pause` sets the snapshot's `paused` and the button reads `Resume`; `Stop` ends the run, the finished strip `##batchdone` says `Stopped:`, and no path report is built.
- [ ] `hydra_uitest --test batch-done-strip` passes: after a one-chart batch, `##batchdone` says `Path report saved to`, `Open report` opens the saved path once, `Show in folder` hands the saved path to the seam, and `X##dismissdone` clears the strip.
- [ ] `hydra_uitest --test batch-open-failure` passes: with auto-open on and a browser that refuses, the strip says `Report saved, but Windows couldn't open it in your browser.` and the report job is `ok()`.
- [ ] `hydra_uitest --test status-line` passes: a status message is gone from the screen after 400 frames, a problem is still there, and `X##dismissstatus` clears it.
- [ ] `hydra_uitest --test compare-disabled` passes: `Compare with dmleaderboards...` is disabled at SP cap 6 and at Hard, and enabled at cap 4 and Expert.
- [ ] `hydra_uitest --test dialog-keys` passes: Escape closes `Song folders`, and Enter continues a finished `Scanning charts`.
- [ ] Every test in `tests/ui/uitest_batch_reports.cpp` passes.

**Verify:** `.\build-cpp\Release\hydra_uitest.exe --test batch-confirm --test batch-pause-stop --test batch-done-strip --test batch-open-failure --test status-line --test compare-disabled --test dialog-keys` → seven `[PASS]` lines and exit code 0.

**Steps:**

- [ ] **Step 1: Write the failing text-helper test.** Create `tests/test_batch_text.cpp`. The helpers it tests are the ones every new line of this task goes through, so "1 chart" and "12,345 charts" are decided once.

```cpp
// The batch strip's and confirm's text helpers: counts with real plurals and
// thousands grouping, durations, and the settings lines the confirm lists.

#include "doctest.h"

#include "app/config.h"
#include "ui/library_parts.h"

using hydra::app::Settings;
using namespace hydra::ui::detail;

TEST_CASE("batch text: counts group thousands and pick the right noun") {
    CHECK(count_label(0, "chart", "charts") == "0 charts");
    CHECK(count_label(1, "chart", "charts") == "1 chart");
    CHECK(count_label(2, "chart", "charts") == "2 charts");
    CHECK(count_label(12345, "chart", "charts") == "12,345 charts");
}

TEST_CASE("batch text: durations read m:ss under an hour and h:mm:ss over it") {
    CHECK(format_duration(0.0) == "0:00");
    CHECK(format_duration(42.4) == "0:42");
    CHECK(format_duration(723.0) == "12:03");
    CHECK(format_duration(3725.0) == "1:02:05");
    CHECK(format_duration(-3.0) == "0:00");
}

TEST_CASE("batch text: the confirm lists the settings a batch runs with") {
    Settings s;  // Expert, Pro Drums, 2x Bass, cap 4, 4 scores, 10 ms
    BatchSettingsSummary d = batch_settings_summary(s);
    CHECK(d.difficulty == "Expert \xC2\xB7 Pro Drums \xC2\xB7 2x Bass");
    CHECK(d.sp_cap == "4 bars (Clone Hero's rule)");
    CHECK(d.score_range == "4 scores");
    CHECK(d.path_limit == "10 ms");

    s.view_difficulty = "Hard";  // 2x Bass is Expert-only, so it drops out
    s.view_prodrums = false;
    s.sp_cap = 1;
    s.depth_mode = 1;
    s.depth_value = 2000;
    s.mslimit_enabled = false;
    d = batch_settings_summary(s);
    CHECK(d.difficulty == "Hard");
    CHECK(d.sp_cap == "1 bar (a what-if)");
    CHECK(d.score_range == "2,000 points");
    CHECK(d.path_limit == "off");
}

TEST_CASE("batch text: the empty library says what to do next") {
    Settings s;
    s.chartfolders.clear();
    CHECK(std::string(empty_library_message(s)).find("Manage folders...") != std::string::npos);
    s.chartfolders.push_back("C:\\songs");
    CHECK(std::string(empty_library_message(s)).find("Scan library") != std::string::npos);
    CHECK(std::string(empty_library_message(s)).find("Manage folders") == std::string::npos);
}
```

Add it after `tests/test_stars.cpp` in the `hydra_tests` list of `CMakeLists.txt`. Build `hydra_tests` from Bash (`powershell -ExecutionPolicy Bypass -File build_cpp.ps1 -Target hydra_tests > build.log 2>&1`). It fails to compile: `count_label`, `format_duration`, `BatchSettingsSummary`, `batch_settings_summary` and `empty_library_message` don't exist.

- [ ] **Step 2: Add the text helpers.** In `src/ui/library_parts.h`, inside `namespace hydra::ui::detail`, add (with `#include <cstdint>` and `#include <string>` if T1 didn't include them, and `#include "app/config.h"`):

```cpp
// "1 chart", "12,345 charts": the count grouped in thousands, then the noun.
std::string count_label(int64_t n, const char* one, const char* many);

// A duration as "0:42", "12:03" or "1:02:05". Negative reads as 0:00.
std::string format_duration(double seconds);

// The settings lines the batch confirm lists, in the confirm's order.
struct BatchSettingsSummary {
    std::string difficulty;   // "Expert · Pro Drums · 2x Bass"
    std::string sp_cap;       // "4 bars (Clone Hero's rule)"
    std::string score_range;  // "2 scores" or "2,000 points"
    std::string path_limit;   // "10 ms" or "off"
};
BatchSettingsSummary batch_settings_summary(const app::Settings& s);

// What the library area says when there are no charts: add a folder first,
// or scan the folders you have.
const char* empty_library_message(const app::Settings& s);

// The batch confirm popup, the running strip and the finished strip.
void render_batch_confirm(AppState& app);
void render_batch_strip(AppState& app);
void render_batch_done(AppState& app);
```

Delete the `render_batch_modal` declaration. Change the `render_status_line` declaration to `void render_status_line(AppState& app);` (Step 6 drops its `same_line` flag). In `src/ui/library_dialogs.cpp`, add `#include "core/model.h"` (for `group_thousands` and `kCloneHeroSpCap`) and define the helpers in `namespace hydra::ui::detail`, outside any anonymous namespace:

```cpp
std::string count_label(int64_t n, const char* one, const char* many) {
    return group_thousands(n) + " " + (n == 1 ? one : many);
}

std::string format_duration(double seconds) {
    const long long total = seconds <= 0.0 ? 0 : static_cast<long long>(seconds + 0.5);
    const long long h = total / 3600, m = (total / 60) % 60, s = total % 60;
    char buf[32];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", h, m, s);
    else
        std::snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    return buf;
}

BatchSettingsSummary batch_settings_summary(const app::Settings& s) {
    BatchSettingsSummary out;
    out.difficulty = difficulty_name(s.difficulty());
    if (s.view_prodrums) out.difficulty += " \xC2\xB7 Pro Drums";
    if (s.effective_bass2x()) out.difficulty += " \xC2\xB7 2x Bass";
    out.sp_cap = count_label(s.sp_cap, "bar", "bars") +
                 (s.sp_cap == kCloneHeroSpCap ? " (Clone Hero's rule)" : " (a what-if)");
    out.score_range = s.depth_mode == 0 ? count_label(s.depth_value, "score", "scores")
                                        : count_label(s.depth_value, "point", "points");
    out.path_limit = s.mslimit_enabled ? std::to_string(s.mslimit_value) + " ms" : "off";
    return out;
}

const char* empty_library_message(const app::Settings& s) {
    return s.chartfolders.empty()
               ? "No song folders yet. Click \"Manage folders...\" to add the folder your "
                 "charts are in, then \"Scan library\"."
               : "No charts found yet. Click \"Scan library\" to read your song folders.";
}
```

Build and run `.\build-cpp\Release\hydra_tests.exe -tc="batch text*"`. The file still fails to link while Steps 3 to 7 are missing (`render_batch_confirm` and the strips are declared but not defined); do Steps 3 to 7, then run it: `Status: SUCCESS!`.

- [ ] **Step 3: The state behind it, on AppState.** In `src/ui/app_state.h`, add `#include <unordered_set>`. In `LibraryViewState`, add at the end:

```cpp
    // Song folders: a folder was added or removed since the dialog opened,
    // so it offers "Scan now".
    bool folders_changed = false;
    // The leaderboard report was opened by the "Open report" button (the
    // job itself knows whether it auto-opened).
    bool dm_opened_by_click = false;
```

In `class AppState`, replace

```cpp
    bool batch_redo = false;  // "redo existing" checkbox state

    // "Analyze library" opens its modal in a confirm stage before any work
    // starts; true while that stage is showing (batch_job not yet created).
    bool batch_confirm_pending = false;
```

with

```cpp
    // The confirm's "Also re-analyze charts that already have a result" box.
    bool batch_redo = false;

    // True while the "Analyze library" confirm shows. open_batch_confirm()
    // loads what it lists: the charts the batch would analyze (the library,
    // or the search's matches) and how many already have a result under the
    // current settings. start_batch() analyzes exactly those charts.
    bool batch_confirm_pending = false;
    std::vector<store::ChartLibraryEntry> batch_scope;
    int64_t batch_scope_with_result = 0;
    void open_batch_confirm();

    // Once per frame (run_frame), after tick(): when a batch ends, start its
    // path report (never for a stopped batch); when the finished strip was
    // dismissed before the report landed, post the outcome to the status
    // line; let go of cancelled leaderboard jobs once they finish.
    void update_background_jobs();

    // Leaderboard jobs cancelled while a request was in flight. WinHTTP only
    // checks the cancel flag between reads, so joining one on the spot could
    // freeze the window for up to two minutes. They wait here instead, and
    // update_background_jobs() drops each once it has finished.
    std::vector<std::unique_ptr<DmFetchUsersJob>> parked_dm_fetches;
    std::vector<std::unique_ptr<DmReportJob>> parked_dm_reports;
    void cancel_dm_fetch();
    void cancel_dm_report();
```

Replace the status block

```cpp
    // Transient feedback line ("folder already added", save failures, ...).
    // The view times the fade-out off status_generation changing.
    std::string status_message;
    Generation status_generation;
    void set_status(std::string message);
```

with

```cpp
    // The status line under the toolbar. set_status is news ("Path report
    // saved"): neutral text that fades after a few seconds. set_problem is
    // something the user should act on: orange, and it stays until dismissed.
    // The view times the fade off status_generation changing.
    std::string status_message;
    bool status_is_problem = false;
    Generation status_generation;
    void set_status(std::string message);
    void set_problem(std::string message);
    void dismiss_status();
```

In the `private:` section add `bool batch_finish_seen_ = false;  // update_background_jobs saw this run end`.

In `src/ui/app_state.cpp`, add `#include <unordered_set>`. Replace `AppState::start_batch` whole:

```cpp
void AppState::open_batch_confirm() {
    // T12 merge-fix: these two lines become `batch_scope = library_matches();`
    // and `batch_scope_with_result = library.counts().analyzed;`, the rows the
    // library shows. In this worktree the library is still the SQL-searched page.
    std::optional<std::string> search_opt = search.empty() ? std::nullopt : std::optional(search);
    batch_scope = store->list_chart_library(search_opt, 0, -1);  // -1 = no limit
    const std::unordered_set<std::string> done =
        store->analyzed_hashes(settings.chartmode_key(), settings.cap_query(), settings.lens());
    batch_scope_with_result = 0;
    for (const store::ChartLibraryEntry& e : batch_scope)
        if (done.count(e.md5)) ++batch_scope_with_result;
    batch_confirm_pending = true;
}

void AppState::start_batch(bool redo) {
    if (analysis_blocked()) return;
    if (batch_job && !batch_job->snapshot().finished) return;
    // A direct call (a test) has no confirm open: load the list here.
    if (!batch_confirm_pending) open_batch_confirm();
    batch_confirm_pending = false;
    // Exactly the charts the confirm counted -- not a search string that SQL
    // would match differently from the library's own search.
    batch_job = std::make_unique<BatchJob>(std::move(batch_scope), settings.batch_run(), *store,
                                           redo);
    batch_scope.clear();
    batch_scope_with_result = 0;
    report_started = false;
    report_outcome_shown = false;
    batch_finish_seen_ = false;
    batch_job->start();
}

void AppState::update_background_jobs() {
    if (batch_job && !batch_finish_seen_ && batch_job->snapshot().finished) {
        batch_finish_seen_ = true;
        refresh_page();  // T12 merge-fix: delete; tick_library re-reads after a batch
        // One path report per finished run. A stopped run keeps its results
        // but builds no report: a report of part of the library would read
        // as the whole of it.
        if (!batch_job->is_cancelled() && !report_started) {
            report_started = true;
            report_job = std::make_unique<ReportJob>(*store, settings.cap_query(), settings.lens(),
                                                     settings.auto_open_report,
                                                     settings.hit_window_ms);
            report_job->start();
        }
    }

    // The finished strip shows the report's outcome. Dismissed before the
    // report landed, the outcome goes to the status line instead.
    if (!batch_job && report_job && report_job->finished()) {
        library_ui.report_checked_at = -1.0;  // a new report: look at once
        if (!report_outcome_shown && !report_job->is_cancelled()) {
            const std::string where = report_job->saved_path().u8string();
            if (!report_job->ok())
                set_problem("The path report could not be built. " + report_job->message());
            else if (!report_job->open_problem().empty())
                set_problem("Path report saved to " + where + ". " + report_job->open_problem());
            else
                set_status("Path report saved to " + where + ".");
        }
        report_job.reset();
    }

    std::erase_if(parked_dm_fetches, [](const auto& job) { return job->finished(); });
    std::erase_if(parked_dm_reports, [](const auto& job) { return job->finished(); });
}

void AppState::cancel_dm_fetch() {
    if (!dm_fetch_job) return;
    dm_fetch_job->cancel();
    if (!dm_fetch_job->finished()) parked_dm_fetches.push_back(std::move(dm_fetch_job));
    dm_fetch_job.reset();
}

void AppState::cancel_dm_report() {
    if (!dm_report_job) return;
    dm_report_job->cancel();
    if (!dm_report_job->finished()) parked_dm_reports.push_back(std::move(dm_report_job));
    dm_report_job.reset();
}
```

In `AppState::start_dm_fetch`, the reassignment `dm_fetch_job = std::make_unique<DmFetchUsersJob>();` happens only after the early return for a running job, so it never joins a live one; leave it. In `AppState::start_dm_report`, add `library_ui.dm_opened_by_click = false;` before `dm_report_job->start();`. Replace `AppState::set_status` with:

```cpp
void AppState::set_status(std::string message) {
    status_message = std::move(message);
    status_is_problem = false;
    status_generation.bump();
}

void AppState::set_problem(std::string message) {
    status_message = std::move(message);
    status_is_problem = true;
    status_generation.bump();
}

void AppState::dismiss_status() {
    status_message.clear();
    status_is_problem = false;
    status_generation.bump();
}
```

In `AppState::save_settings`, change `set_status("Settings could not be saved — "` to `set_problem("Settings could not be saved — "`. In `src/ui/app_shell.cpp`, `run_frame`, add `app.update_background_jobs();` as the first line after the capture block, before `render_main_window(app);` (T9 adds `app.tick(ImGui::GetTime());` there too; at merge the order is `tick`, then `update_background_jobs`).

- [ ] **Step 4: Show in folder.** In `src/ui/win32_dialogs.h`, add `#include <filesystem>` and `#include <functional>` and, after `browse_for_folder`:

```cpp
// Opens Explorer on the file's folder with the file selected
// (explorer.exe /select,"<path>"). Returns false when the shell refuses.
bool show_in_folder(const std::filesystem::path& file);

// The seam behind show_in_folder, like app::set_open_in_browser: a GUI test
// installs one that records the path instead of opening Explorer. An empty
// function restores the default.
using ShowInFolderFn = std::function<bool(const std::wstring& path)>;
void set_show_in_folder(ShowInFolderFn fn);
```

In `src/ui/win32_dialogs.cpp`, add `#include <shellapi.h>` after `<shobjidl.h>`, and at the end of the namespace:

```cpp
namespace {
ShowInFolderFn& show_in_folder_seam() {
    static ShowInFolderFn fn;
    return fn;
}
}  // namespace

void set_show_in_folder(ShowInFolderFn fn) { show_in_folder_seam() = std::move(fn); }

bool show_in_folder(const std::filesystem::path& file) {
    const std::wstring path = file.wstring();
    if (show_in_folder_seam()) return show_in_folder_seam()(path);
    const std::wstring args = L"/select,\"" + path + L"\"";
    HINSTANCE r = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr,
                                SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(r) > 32;  // ShellExecute's documented success test
}
```

- [ ] **Step 5: Write the failing GUI tests.** In `tests/ui/uitest_batch_reports.cpp`, add `#include "ui/win32_dialogs.h"` and these helpers and tests in its anonymous namespace, and add `{"batch-confirm", test_batch_confirm}`, `{"batch-pause-stop", test_batch_pause_stop}`, `{"batch-done-strip", test_batch_done_strip}`, `{"batch-open-failure", test_batch_open_failure}`, `{"status-line", test_status_line}`, `{"compare-disabled", test_compare_disabled}`, `{"dialog-keys", test_dialog_keys}` to its entry table.

```cpp
// A child window of the main window, or null. Child window names are
// mangled, so go through WindowInfo; NoError because "not there" is a result.
ImGuiWindow* child_window(ImGuiTestContext* ctx, const char* path) {
    return ctx->WindowInfo(path, ImGuiTestOpFlags_NoError).Window;
}

// Narrow the library to charts matching `search` and batch them through the
// confirm; waits for the batch and its report to finish.
bool batch_search(ImGuiTestContext* ctx, const std::string& search) {
    Harness& h = harness(ctx);
    ctx->SetRef("//Hydra");
    ctx->ItemInputValue("**/##search", search.c_str());
    if (!wait_until(ctx, [&] { return h.app->search == search; }, 5)) return false;
    // T12 merge-fix: the count becomes h.app->library_match_count().
    const std::string label =
        "Analyze search (" + hydra::group_thousands(h.app->current_page.total_count) + ")...";
    ctx->ItemClick(label.c_str());
    ctx->SetRef("//Analyze library");
    ctx->ItemClick("Start analyzing");
    return wait_until(ctx, [&] {
               return h.app->batch_job && h.app->batch_job->snapshot().finished;
           }, 300) &&
           wait_until(ctx, [&] { return h.app->report_job && h.app->report_job->finished(); }, 60);
}

// The confirm lists every setting and the real count; Escape backs out,
// Enter starts, and the batch runs in the strip, not a modal.
void test_batch_confirm(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Analyze library...");
    ctx->Yield(2);
    IM_CHECK(h.app->batch_confirm_pending);
    std::string text = visible_text(h);
    IM_CHECK(text.find("Analyze 97 charts that have no result yet?") != std::string::npos);
    IM_CHECK(text.find("Expert \xC2\xB7 Pro Drums \xC2\xB7 2x Bass") != std::string::npos);
    IM_CHECK(text.find("4 bars (Clone Hero's rule)") != std::string::npos);
    IM_CHECK(text.find("2 scores") != std::string::npos);
    IM_CHECK(text.find("10 ms") != std::string::npos);
    ctx->SetRef("//Analyze library");
    // Nothing has a result yet, so there is nothing to re-analyze.
    IM_CHECK((ctx->ItemInfo("Also re-analyze charts that already have a result##redo").ItemFlags &
              ImGuiItemFlags_Disabled) != 0);

    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    IM_CHECK(!h.app->batch_confirm_pending);
    IM_CHECK(h.app->batch_job == nullptr);

    ctx->SetRef("//Hydra");
    ctx->ItemClick("Analyze library...");
    ctx->Yield(2);
    ctx->KeyPress(ImGuiKey_Enter);
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job != nullptr; }, 5));
    IM_CHECK(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));  // no modal while it runs
    IM_CHECK(wait_until(ctx, [&] { return child_window(ctx, "//Hydra/##batchstrip") != nullptr; }, 10));
    h.app->batch_job->stop();
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job->snapshot().finished; }, 300));
}

// Pause holds the run, Resume carries on, Stop ends it and keeps what's done.
void test_batch_pause_stop(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Analyze library...");
    ctx->SetRef("//Analyze library");
    ctx->ItemClick("Start analyzing");
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->batch_job && !h.app->batch_job->snapshot().preparing;
    }, 30));
    IM_CHECK(wait_until(ctx, [&] { return child_window(ctx, "//Hydra/##batchstrip") != nullptr; }, 5));
    ctx->SetRef(child_window(ctx, "//Hydra/##batchstrip"));
    ctx->ItemClick("Pause");
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job->snapshot().paused; }, 5));
    IM_CHECK(ctx->ItemExists("Resume"));
    ctx->ItemClick("Resume");
    IM_CHECK(wait_until(ctx, [&] { return !h.app->batch_job->snapshot().paused; }, 5));
    ctx->ItemClick("Stop");
    IM_CHECK(wait_until(ctx, [&] { return h.app->batch_job->snapshot().finished; }, 300));
    IM_CHECK(wait_until(ctx, [&] { return child_window(ctx, "//Hydra/##batchdone") != nullptr; }, 5));
    IM_CHECK(visible_text(h).find("Stopped:") != std::string::npos);
    ctx->Yield(5);
    IM_CHECK(h.app->report_job == nullptr);  // a stopped run builds no report
    ctx->SetRef(child_window(ctx, "//Hydra/##batchdone"));
    ctx->ItemClick("X##dismissdone");
    ctx->Yield(2);
    IM_CHECK(h.app->batch_job == nullptr);
}

// The finished strip: where the report went, Open report, Show in folder.
void test_batch_done_strip(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    std::vector<std::wstring> shown;
    hydra::ui::set_show_in_folder([&](const std::wstring& p) {
        shown.push_back(p);
        return true;
    });
    scan_library(ctx);
    if (ctx->IsError()) return;
    IM_CHECK(batch_search(ctx, "Burnout"));
    IM_CHECK(h.app->report_job->ok());
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)0);  // auto-open is off
    ImGuiWindow* done = child_window(ctx, "//Hydra/##batchdone");
    IM_CHECK(done != nullptr);
    IM_CHECK(visible_text(h).find("Path report saved to") != std::string::npos);
    ctx->SetRef(done);
    ctx->ItemClick("Open report");
    IM_CHECK_EQ(h.opened_urls.size(), (size_t)1);
    IM_CHECK(h.opened_urls[0] == h.app->report_job->saved_path().wstring());
    ctx->ItemClick("Show in folder");
    IM_CHECK_EQ(shown.size(), (size_t)1);
    IM_CHECK(shown[0] == h.app->report_job->saved_path().wstring());
    ctx->ItemClick("X##dismissdone");
    ctx->Yield(3);
    IM_CHECK(h.app->batch_job == nullptr);
    ctx->SetRef("//Hydra");
    IM_CHECK(wait_until(ctx, [&] { return ctx->ItemExists("**/Open path report"); }, 5));
    hydra::ui::set_show_in_folder({});
}

// A browser that refuses is not a failed report: the page is saved, and the
// strip says only the opening failed.
void test_batch_open_failure(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    h.app->settings.auto_open_report = true;
    h.app->commit_settings();
    hydra::app::set_open_in_browser([](const std::wstring&) { return false; });
    scan_library(ctx);
    if (ctx->IsError()) return;
    IM_CHECK(batch_search(ctx, "Burnout"));
    IM_CHECK(h.app->report_job->ok());
    IM_CHECK(!h.app->report_job->opened());
    IM_CHECK(visible_text(h).find("Report saved, but Windows couldn't open it in your browser.") !=
             std::string::npos);
    // reset_app reinstalls the recording seam for the next test.
}

// News fades; a problem stays until dismissed.
void test_status_line(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    ctx->SetRef("//Hydra");
    h.app->set_status("Saved the thing.");
    ctx->Yield(2);
    IM_CHECK(h.frame_text.text.find("Saved the thing.") != std::string::npos);
    ctx->Yield(400);  // over 6 s of 1/60 s frames
    IM_CHECK(h.frame_text.text.find("Saved the thing.") == std::string::npos);

    h.app->set_problem("The thing broke.");
    ctx->Yield(400);
    IM_CHECK(h.frame_text.text.find("The thing broke.") != std::string::npos);
    ctx->ItemClick("**/X##dismissstatus");
    ctx->Yield(2);
    IM_CHECK(h.app->status_message.empty());
}

// The comparison only means something at Clone Hero's cap and Expert; off
// either, the button is disabled (with the reason on hover) instead of
// posting a refusal after the click.
void test_compare_disabled(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return;
    ctx->SetRef("//Hydra");
    auto disabled = [&] {
        return (ctx->ItemInfo("Compare with dmleaderboards...").ItemFlags &
                ImGuiItemFlags_Disabled) != 0;
    };
    IM_CHECK(!disabled());
    h.app->settings.sp_cap = 6;
    h.app->commit_settings();
    ctx->Yield(2);
    IM_CHECK(disabled());
    h.app->settings.sp_cap = 4;
    h.app->settings.view_difficulty = "Hard";
    h.app->commit_settings();
    ctx->Yield(2);
    IM_CHECK(disabled());
    h.app->settings.view_difficulty = "Expert";
    h.app->commit_settings();
    ctx->Yield(2);
    IM_CHECK(!disabled());
}

// Escape closes Song folders; Enter continues a finished scan.
void test_dialog_keys(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Manage folders... (1)");
    ctx->Yield(2);
    IM_CHECK(ImGui::IsPopupOpen("Song folders"));
    ctx->KeyPress(ImGuiKey_Escape);
    ctx->Yield(2);
    IM_CHECK(!ImGui::IsPopupOpen("Song folders"));

    ctx->ItemClick("Scan library");
    IM_CHECK(wait_until(ctx, [&] {
        return h.app->scan_job && h.app->scan_job->snapshot().finished;
    }, 60));
    ctx->KeyPress(ImGuiKey_Enter);
    ctx->Yield(2);
    IM_CHECK(h.app->scan_job == nullptr);
    IM_CHECK(h.app->library_total > 0);
}
```

`ImGui::IsPopupOpen("Song folders")` asks at the current ID stack level; if the check reads false while the popup shows, use `ImGui::IsPopupOpen("Song folders", ImGuiPopupFlags_AnyPopupLevel)`.

Then move the existing tests in `uitest_batch_reports.cpp` to the new UI. Each keeps what it checks:

- `test_batch_modal_drift` becomes `test_batch_strip_drift` (rename its entry to `batch-strip-drift`). Start through `Analyze library...` and `Start analyzing`, then in the loop read `ctx->ItemInfo("Stop").RectFull` with the ref set to `child_window(ctx, "//Hydra/##batchstrip")`, and check the Stop button's x and y and the strip's width (`child_window(...)->Size.x`) never move while `current_title` changes. End with `h.app->batch_job->stop()`, wait for `finished`, and click `X##dismissdone` in `##batchdone`.
- `test_settings_and_reports`: the batch part becomes `IM_CHECK(batch_search(ctx, title));` with the title in quotes (`"\"" + title + "\""`), then `Open report` and `X##dismissdone` in `##batchdone` replace `**/Open path report` and `**/Continue`. The leaderboard part clicks `Compare with dmleaderboards...`, and the count check reads `h.app->dm_report_job->stats().matched == 1`.
- `test_report_buttons`: `run_batch` becomes `batch_search`. "Open automatically" is ticked in `##batchdone`. The redo step opens the confirm with the `Analyze search (1)...` label and ticks `Also re-analyze charts that already have a result##redo` in `//Analyze library` instead of the old `redo existing`; the text check becomes `Also re-analyze`, and `Cancel` backs out. `Continue` becomes `X##dismissdone`.
- `test_dm_compare_flow`: the two refusal blocks become the disabled checks of `test_compare_disabled` (delete the `needs SP cap 4` and `needs Expert difficulty` text checks). With auto-open off, the first open button reads `Open report`; after one click it reads `Open report again`. Add `IM_CHECK(visible_text(h).find("not analyzed") != std::string::npos);` after the report finishes.
- Anywhere: `Compare dmleaderboards user...` becomes `Compare with dmleaderboards...`, `Analyze library` becomes `Analyze library...`, `Analyze search (N)` becomes `Analyze search (N)...` with `N` grouped by `group_thousands`, `//Analyzing` becomes `//Analyze library` for the confirm and the strip refs above for the rest, `Start` becomes `Start analyzing`, and `batch_job->cancel()` becomes `batch_job->stop()`.

Build `hydra_uitest` and run the Verify command. The new tests fail: there is no `Analyze library...` button yet.

- [ ] **Step 6: The toolbar and the status line.** In `src/ui/library_toolbar.cpp`, replace `render_status_line` whole:

```cpp
// The status line, on its own wrapping line under the toolbar. News is
// neutral and fades after a few seconds; a problem is orange and stays until
// its X is clicked.
void render_status_line(AppState& app) {
    // The watcher starts at "seen" for this app's counter, so startup does
    // not start a fade.
    GenerationWatcher& generation = app.library_ui.status_watcher;
    double& shown_at = app.library_ui.status_shown_at;
    if (generation.changed(app.status_generation)) shown_at = ImGui::GetTime();
    if (shown_at < 0.0 || app.status_message.empty()) return;
    if (!app.status_is_problem && ImGui::GetTime() - shown_at > 6.0) return;
    if (app.status_is_problem) {
        if (ImGui::SmallButton("X##dismissstatus")) {
            app.dismiss_status();
            return;
        }
        hint("Dismiss");
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, app.status_is_problem ? kWarningColor : kDefaultTextColor);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(app.status_message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}
```

Replace `render_actions_row` whole. It keeps T9's two changes (the one `Scan library` label and the "Open path report" button, now right-aligned as in the mockup):

```cpp
// The toolbar: library-wide actions on the left, the last report on the right.
void render_actions_row(AppState& app) {
    const bool batch_busy = app.batch_job && !app.batch_job->snapshot().finished;
    auto busy_tooltip = [&] {
        if (batch_busy && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal |
                                               ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Busy: a batch is running.");
    };

    // Buttons with live counts take the width of their widest label, so the
    // row doesn't reflow under the mouse when a count changes.
    int folder_count = (int)app.settings.chartfolders.size();
    char manage_label[64];
    std::snprintf(manage_label, sizeof(manage_label), "Manage folders... (%d)", folder_count);
    std::string manage_widest =
        "Manage folders... (" + widest_digits(digit_count(folder_count)) + ")";
    if (button_in_slot(manage_label, button_slot_width(manage_widest.c_str())))
        ImGui::OpenPopup("Song folders");
    hint("Add or remove the folders Hydra scans for charts");
    ImGui::SameLine();

    const bool can_scan = !app.settings.chartfolders.empty() && !batch_busy && !app.scan_job;
    begin_disabled_button(!can_scan);
    if (ImGui::Button("Scan library")) {
        app.start_scan();
        ImGui::OpenPopup("Scanning charts");
    }
    end_disabled_button(!can_scan);
    busy_tooltip();
    ImGui::SameLine();

    // T12 merge-fix: the search count becomes library_match_count().
    const bool searching = !app.search.empty();
    const int64_t analyzable = searching ? app.current_page.total_count : app.library_total;
    const std::string label = searching
                                  ? "Analyze search (" + group_thousands(analyzable) + ")..."
                                  : std::string("Analyze library...");
    // The slot fits the widest count this library can show, commas included.
    std::string sample = group_thousands(app.library_total);
    const char widest = widest_digits(1)[0];
    for (char& c : sample)
        if (c >= '0' && c <= '9') c = widest;
    const float analyze_w = std::max(button_slot_width("Analyze library..."),
                                     button_slot_width(("Analyze search (" + sample + ")...").c_str()));
    // Off with nothing to analyze, under a bad hydra_rules.ini, or mid-batch.
    const bool analyze_off = analyzable == 0 || app.analysis_blocked() || batch_busy;
    begin_disabled_button(analyze_off);
    if (button_in_slot(label.c_str(), analyze_w)) app.open_batch_confirm();
    end_disabled_button(analyze_off);
    busy_tooltip();
    ImGui::SameLine();

    // The leaderboard plays by Clone Hero's rules at Expert, so the comparison
    // only means anything there. Disabled elsewhere, with the reason on hover.
    const bool expert = app.settings.difficulty() == Difficulty::Expert;
    const bool ch_cap = app.settings.sp_cap == kCloneHeroSpCap;
    const bool compare_off = !expert || !ch_cap;
    begin_disabled_button(compare_off);
    if (ImGui::Button("Compare with dmleaderboards...")) {
        // Fresh picker: drop a finished report (a running one is parked, not
        // joined), and refetch the ladder only when this session has none.
        app.cancel_dm_report();
        app.dm_picker_open = true;
        if (app.dm_users.empty()) app.start_dm_fetch();
        ImGui::OpenPopup("Compare dmleaderboards user");
    }
    end_disabled_button(compare_off);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (!expert)
            ImGui::SetTooltip("Needs Expert: the leaderboard only has Expert scores.");
        else if (!ch_cap)
            ImGui::SetTooltip("Needs SP cap %d, Clone Hero's rule: the leaderboard's scores "
                              "were played under it.",
                              kCloneHeroSpCap);
        else
            ImGui::SetTooltip("Compare a dmleaderboards.com player's scores against your library");
    }

    // The last report, at the right. While one builds, a greyed button says so.
    const bool building = app.report_job && !app.report_job->finished();
    if (building || app.report_file_shown(ImGui::GetTime())) {
        const float report_w = std::max(button_slot_width("Open path report"),
                                        button_slot_width("Building path report..."));
        ImGui::SameLine();
        const float right = ImGui::GetContentRegionMax().x - report_w;
        if (right > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right);
        if (building) {
            begin_disabled_button(true);
            ImGui::Button("Building path report...", ImVec2(report_w, 0.0f));
            end_disabled_button(true);
        } else if (ImGui::Button("Open path report", ImVec2(report_w, 0.0f)) &&
                   !app::open_report_in_browser()) {
            app.set_problem("Windows couldn't open the path report in your browser.");
        }
    }

    render_status_line(app);
    // A bad rules file is not a passing message: it stays under the toolbar
    // for as long as analysis is off.
    if (app.analysis_blocked()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarningColor,
                           "hydra_rules.ini has an error, so analysis is off until the "
                           "file is fixed and Hydra is restarted.");
        ImGui::TextColored(kWarningColor, "%s", app.rules_error.c_str());
        ImGui::PopTextWrapPos();
    }
    render_folder_manager(app);
}
```

Add `#include "core/model.h"` and `#include "app/report_files.h"` to `library_toolbar.cpp` if T1 didn't carry them.

- [ ] **Step 7: The confirm, the strips and the dialogs.** In `src/ui/library_dialogs.cpp`, add these colours to the file's anonymous namespace. Each text colour was measured on its strip: (250,250,250) is 11.97:1 on the running strip, 12.94:1 on the done strip and 13.73:1 on the problem strip; the secondary lines are 9.47:1, 9.27:1 and 9.38:1; orange on the problem strip is 5.65:1; the teal bar on its track is 6.4:1.

```cpp
const ImVec4 kStripBg{22 / 255.0f, 57 / 255.0f, 58 / 255.0f, 1.0f};
const ImVec4 kStripText2{200 / 255.0f, 230 / 255.0f, 230 / 255.0f, 1.0f};
const ImVec4 kStripTrack{11 / 255.0f, 35 / 255.0f, 36 / 255.0f, 1.0f};
const ImVec4 kDoneBg{31 / 255.0f, 51 / 255.0f, 34 / 255.0f, 1.0f};
const ImVec4 kDoneText2{196 / 255.0f, 220 / 255.0f, 199 / 255.0f, 1.0f};
const ImVec4 kProblemBg{58 / 255.0f, 38 / 255.0f, 18 / 255.0f, 1.0f};
const ImVec4 kProblemText2{230 / 255.0f, 205 / 255.0f, 180 / 255.0f, 1.0f};

// Enter or keypad Enter this frame.
bool enter_pressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
           ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
}

// "21 analyzed · 0 failed · 1 skipped (already had a result)".
std::string batch_counts(const BatchJob::Snapshot& s) {
    return count_label(s.completed - s.failed, "analyzed", "analyzed") + " \xC2\xB7 " +
           count_label(s.failed, "failed", "failed") + " \xC2\xB7 " +
           count_label(s.skipped, "skipped", "skipped") + " (already had a result)";
}
```

Delete `render_batch_modal` whole and add, in `namespace hydra::ui::detail` outside the anonymous namespace:

```cpp
// The one question before a batch: how many charts, every setting it will
// run with, and whether to redo charts that already have a result. Esc and
// the title-bar X cancel; Enter starts (unless Cancel has keyboard focus).
void render_batch_confirm(AppState& app) {
    if (!app.batch_confirm_pending) return;
    if (!ImGui::IsPopupOpen("Analyze library")) ImGui::OpenPopup("Analyze library");
    pin_next_modal_width(px(520.0f));
    bool open = true;
    if (!ImGui::BeginPopupModal("Analyze library", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        app.batch_confirm_pending = false;  // closed without our buttons
        app.batch_scope.clear();
        return;
    }

    const int64_t total = static_cast<int64_t>(app.batch_scope.size());
    const int64_t with = app.batch_scope_with_result;
    const int64_t without = total - with;
    const int64_t to_run = app.batch_redo ? total : without;
    std::string question;
    if (to_run == 0)
        question = "Every chart here already has a result.";
    else if (app.batch_redo && with > 0)
        question = "Analyze " + count_label(total, "chart", "charts") + ", re-analyzing " +
                   group_thousands(with) + (with == 1 ? " that already has" : " that already have") +
                   " a result?";
    else
        question = "Analyze " + count_label(without, "chart", "charts") +
                   (without == 1 ? " that has" : " that have") + " no result yet?";
    ImGui::PushFont(nullptr, 20.0f);
    ImGui::TextWrapped("%s", question.c_str());
    ImGui::PopFont();

    const BatchSettingsSummary d = batch_settings_summary(app.settings);
    if (ImGui::BeginTable("##batchsettings", 2,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuter)) {
        auto row = [](const char* key, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", key);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
        };
        row("Difficulty", d.difficulty);
        row("SP cap", d.sp_cap);
        row("Score range", d.score_range);
        row("Path limit", d.path_limit);
        ImGui::EndTable();
    }
    ImGui::TextDisabled("To change these, cancel and edit Analysis settings on the main screen.");

    begin_disabled_checkbox(with == 0);
    ImGui::Checkbox("Also re-analyze charts that already have a result##redo", &app.batch_redo);
    end_disabled_checkbox(with == 0);
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", count_label(with, "chart", "charts").c_str());

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("It runs in the background, so you can keep browsing. Analysis "
                           "settings stay locked until it finishes or you stop it. Stop keeps "
                           "every result finished so far.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const bool can_start = to_run > 0 && !app.analysis_blocked();
    const float start_w = button_slot_width("Start analyzing");
    const float cancel_w = button_slot_width("Cancel");
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - start_w - cancel_w -
                         ImGui::GetStyle().ItemSpacing.x);
    bool cancel = !open || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (ImGui::Button("Cancel")) cancel = true;
    const bool cancel_focused = ImGui::IsItemFocused();
    ImGui::SameLine();
    begin_disabled_button(!can_start);
    bool start = ImGui::Button("Start analyzing");
    end_disabled_button(!can_start);
    ImGui::SetItemDefaultFocus();
    if (can_start && !cancel_focused && enter_pressed()) start = true;

    if (start && can_start) {
        app.start_batch(app.batch_redo);  // clears batch_confirm_pending
        ImGui::CloseCurrentPopup();
    } else if (cancel) {
        app.batch_confirm_pending = false;
        app.batch_scope.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// The running batch, in a strip under the toolbar: what it's doing, how far,
// how long, and Pause / Stop. The buttons sit at fixed x so live numbers
// never walk them around.
void render_batch_strip(AppState& app) {
    if (!app.batch_job) return;
    const BatchJob::Snapshot s = app.batch_job->snapshot();
    if (s.finished) return;
    const bool stopping = app.batch_job->is_cancelled();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kStripBg);
    ImGui::BeginChild("##batchstrip", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float pause_w = std::max(button_slot_width("Pause"), button_slot_width("Resume"));
    const float stop_w = button_slot_width("Stop");
    const float buttons_x = ImGui::GetContentRegionMax().x - pause_w - stop_w - style.ItemSpacing.x;
    const float text_w = buttons_x - ImGui::GetCursorPosX() - style.ItemSpacing.x;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##striptext", ImVec2(text_w, 0.0f), ImGuiChildFlags_AutoResizeY);
    ImGui::PopStyleColor();
    if (s.preparing) {
        ImGui::TextUnformatted("Preparing the chart list...");
    } else {
        std::string line = stopping ? "Stopping..." : s.paused ? "Paused" : "Analyzing...";
        line += "  " + group_thousands(s.completed) + " of " + group_thousands(s.total);
        if (!s.current_title.empty() && !s.paused) {
            line += "  \xC2\xB7  Now: " + s.current_title;
            if (!s.current_artist.empty()) line += " \xC2\xB7 " + s.current_artist;
        }
        text_ellipsized(line.c_str());

        const float frac = s.total > 0 ? (float)s.completed / (float)s.total : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kStripTrack);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccentColor);
        ImGui::ProgressBar(frac, ImVec2(-1.0f, px(6.0f)), "");
        ImGui::PopStyleColor(2);

        std::string time = format_duration(s.elapsed_s) + " elapsed";
        if (s.eta_s) time += " \xC2\xB7 about " + format_duration(*s.eta_s) + " left";
        ImGui::PushStyleColor(ImGuiCol_Text, kStripText2);
        ImGui::TextUnformatted(batch_counts(s).c_str());
        const float time_w = ImGui::CalcTextSize(time.c_str()).x;
        ImGui::SameLine();
        const float right = ImGui::GetContentRegionMax().x - time_w;
        if (right > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right);
        ImGui::TextUnformatted(time.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::SetCursorPosX(buttons_x);
    const bool pause_off = stopping || s.preparing;
    begin_disabled_button(pause_off);
    if (button_in_slot(s.paused ? "Resume" : "Pause", pause_w)) {
        if (s.paused) app.batch_job->resume();
        else app.batch_job->pause();
    }
    end_disabled_button(pause_off);
    ImGui::SameLine();
    begin_disabled_button(stopping);
    if (button_in_slot("Stop", stop_w)) app.batch_job->stop();
    end_disabled_button(stopping);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        const int kept = s.completed - s.failed;
        ImGui::SetTooltip("Keeps the %s already finished",
                          count_label(kept, "result", "results").c_str());
    }
    ImGui::EndChild();
}

// The finished batch: the counts, how long it took, where the report went,
// and what to do with it. It stays until its X is clicked.
void render_batch_done(AppState& app) {
    if (!app.batch_job) return;
    const BatchJob::Snapshot s = app.batch_job->snapshot();
    if (!s.finished) return;
    const bool stopped = app.batch_job->is_cancelled();
    ReportJob* report = app.report_job.get();
    const bool building = report && !report->finished();
    const bool report_ok = report && report->finished() && report->ok();
    const bool report_failed = report && report->finished() && !report->ok() &&
                               !report->is_cancelled();
    const bool open_failed = report_ok && !report->open_problem().empty();
    if (report && report->finished()) app.report_outcome_shown = true;

    const bool problem = report_failed || open_failed;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, problem ? kProblemBg : kDoneBg);
    ImGui::BeginChild("##batchdone", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    const ImVec4 text2 = problem ? kProblemText2 : kDoneText2;

    // Buttons at the right, placed first so the text can take the rest.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float close_w = ImGui::GetFrameHeight();
    const float open_w = button_slot_width("Open report");
    const float folder_w = button_slot_width("Show in folder");
    const float buttons_w = (report_ok ? open_w + folder_w + style.ItemSpacing.x * 2 : 0.0f) + close_w;
    const float left_x = ImGui::GetCursorPosX();
    const float top_y = ImGui::GetCursorPosY();
    const float text_w = ImGui::GetContentRegionAvail().x - buttons_w - style.ItemSpacing.x;
    ImGui::SetCursorPosX(left_x + text_w + style.ItemSpacing.x);
    if (report_ok) {
        const std::string path = report->saved_path().u8string();
        if (button_in_slot("Open report", open_w) &&
            !app::open_in_browser(report->saved_path().wstring()))
            app.set_problem("Windows couldn't open the report in your browser. Open " + path +
                            " directly.");
        ImGui::SameLine();
        if (button_in_slot("Show in folder", folder_w) && !show_in_folder(report->saved_path()))
            app.set_problem("Windows couldn't open the folder that holds " + path + ".");
        ImGui::SameLine();
    }
    if (ImGui::Button("X##dismissdone", ImVec2(close_w, close_w))) {
        app.batch_job.reset();  // update_background_jobs reaps the report after this
        ImGui::EndChild();
        return;
    }
    hint("Dismiss");

    ImGui::SetCursorPos(ImVec2(left_x, top_y));
    ImGui::PushTextWrapPos(left_x + text_w);
    ImGui::TextWrapped("%s: %s \xC2\xB7 took %s", stopped ? "Stopped" : "Finished",
                       batch_counts(s).c_str(), format_duration(s.elapsed_s).c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, text2);
    if (stopped) {
        ImGui::TextWrapped("Every result finished before Stop is kept.");
    } else if (building) {
        ImGui::TextWrapped("Building the path report...");
    } else if (report_failed) {
        ImGui::TextColored(kWarningColor, "The path report could not be built.");
        ImGui::TextWrapped("%s", report->message().c_str());
        ImGui::TextDisabled("%s", report->error().c_str());
    } else if (open_failed) {
        ImGui::PopStyleColor();
        ImGui::TextColored(kWarningColor,
                           "Report saved, but Windows couldn't open it in your browser.");
        ImGui::PushStyleColor(ImGuiCol_Text, text2);
        ImGui::TextWrapped("Open %s directly, or try Open report again.",
                           report->saved_path().u8string().c_str());
    } else if (report_ok) {
        ImGui::TextWrapped("Path report saved to %s", report->saved_path().u8string().c_str());
    }
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();

    if (report_ok) {
        if (ImGui::Checkbox("Open automatically", &app.settings.auto_open_report))
            app.commit_settings();
        hint("Open the report in the browser whenever a batch finishes");
    }
    if (!s.failures.empty()) {
        const std::string head = count_label((int64_t)s.failures.size(), "chart", "charts") +
                                 " failed##batchfailures";
        if (ImGui::TreeNode(head.c_str())) {
            for (size_t i = 0; i < s.failures.size(); ++i) {
                ImGui::TextUnformatted(s.failures[i].c_str());
                if (i < s.failure_details.size())
                    ImGui::TextDisabled("%s", s.failure_details[i].c_str());
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
}
```

Add `#include "ui/win32_dialogs.h"` and `#include "app/report_files.h"` if T1 didn't carry them. `text_ellipsized`, `button_slot_width`, `button_in_slot`, `pin_next_modal_width` and `hint` come from `ui/widgets.h`.

In `render_scan_modal`, Enter and Escape act on the visible button. Change the running branch's `if (ImGui::Button("Cancel")) app.scan_job->cancel();` to `if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) app.scan_job->cancel();`. In both finished branches (cancelled and done), change `if (ImGui::Button("Continue")) {` to `if (ImGui::Button("Continue") || enter_pressed() || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {`. Replace the done line `ImGui::Text("Done! %d chart(s) found.", p.charts_found);` with `ImGui::Text("Done: %s found.", count_label(p.charts_found, "chart", "charts").c_str());`, and the problems line `ImGui::TextColored(kWarningColor, "%d problem(s) during the scan:", (int)p.errors.size());` with `ImGui::TextColored(kWarningColor, "%s during the scan:", count_label((int64_t)p.errors.size(), "problem", "problems").c_str());`. In the done branch's Continue block, delete `app.table_viewpage = 0;` only if T12 has already removed it in the merged base; in this worktree leave it.

In `render_folder_manager`: set `app.library_ui.folders_changed = true;` after each `app.commit_settings();` that adds or removes a folder (two places). Replace the Close button line `if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();` with:

```cpp
    const bool nested_open = ImGui::IsPopupOpen("Remove folder?");
    bool close = ImGui::Button("Close") ||
                 (!nested_open && ImGui::IsKeyPressed(ImGuiKey_Escape, false));
    // A changed list is only real after a scan: offer it right here.
    if (app.library_ui.folders_changed && !app.settings.chartfolders.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Scan now")) {
            app.request_scan = true;  // the main window starts it next frame
            close = true;
        }
    }
    if (close) {
        app.library_ui.folders_changed = false;
        ImGui::CloseCurrentPopup();
    }
```

and change its `render_status_line(app, /*same_line=*/false);` to `render_status_line(app);`. In the nested "Remove folder?" popup, change `if (ImGui::Button("Remove")) {` to `if (ImGui::Button("Remove") || enter_pressed()) {` and `if (ImGui::Button("Cancel")) {` to `if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {`. The folder picker's failure line becomes `app.set_problem("The folder picker could not be opened.");`.

In `render_dm_picker_modal`, replace the stage-2 block (from `if (app.dm_report_job) {` to its `return;` and closing brace) with:

```cpp
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    // A cancelled report goes back to the list, quietly: the user asked.
    if (app.dm_report_job && app.dm_report_job->finished() && app.dm_report_job->is_cancelled())
        app.dm_report_job.reset();

    if (app.dm_report_job) {
        DmReportJob& job = *app.dm_report_job;
        if (!job.finished()) {
            ImGui::TextUnformatted("Fetching scores and building the report...");
            ImGui::TextDisabled("The leaderboard server can take a moment to wake up.");
            if (ImGui::Button("Cancel") || escape) app.cancel_dm_report();  // never joins
        } else if (!job.ok()) {
            ImGui::TextColored(kWarningColor, "Could not build the report.");
            ImGui::TextWrapped("%s", job.message().c_str());
            ImGui::TextDisabled("%s", job.error().c_str());
            ImGui::Spacing();
            if (ImGui::Button("Back to list") || escape) app.dm_report_job.reset();
        } else {
            const auto& st = job.stats();
            ImGui::TextWrapped("Done: %s matched, %s above optimal, %s not analyzed, %s not in "
                               "your library.",
                               group_thousands(st.matched).c_str(),
                               group_thousands(st.above_optimal).c_str(),
                               group_thousands(st.not_analyzed).c_str(),
                               group_thousands(st.not_in_library).c_str());
            if (job.opened())
                ImGui::TextDisabled("The report opened in your browser.");
            else if (!job.open_problem().empty())
                ImGui::TextColored(kWarningColor,
                                   "Report saved, but Windows couldn't open it in your browser.");
            else
                ImGui::TextDisabled("The report is ready.");
            ImGui::Spacing();
            // "again" only once it really opened.
            const bool opened = job.opened() || app.library_ui.dm_opened_by_click;
            if (ImGui::Button(opened ? "Open report again" : "Open report")) {
                if (app::open_in_browser(job.saved_path().wstring()))
                    app.library_ui.dm_opened_by_click = true;
                else
                    app.set_problem("Windows couldn't open the report in your browser. Open " +
                                    job.saved_path().u8string() + " directly.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Compare another")) app.dm_report_job.reset();
            ImGui::SameLine();
            if (ImGui::Button("Close") || escape) {
                app.dm_report_job.reset();
                app.dm_picker_open = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
        return;
    }
```

In the stage-0 block, replace `app.dm_fetch_job.reset();  // destructor cancels + joins` with `app.cancel_dm_fetch();  // parked, never joined here` and let `escape` trigger the same branch (`if (ImGui::Button("Cancel") || escape) {`). The fetch-failure branch shows `app.dm_fetch_job->message()` in `TextWrapped` with `error()` as a `TextDisabled` line under it, and its Close answers `escape` too. In stage 1, change `if (ImGui::Button("Close")) {` to `if (ImGui::Button("Close") || (escape && !ImGui::GetIO().WantTextInput)) {` so Escape in the filter box doesn't close the picker.

- [ ] **Step 8: Wire the strips into the main window.** In `src/ui/library_view.cpp`, `render_main_window`:

Delete the report-reap block (it starts `// A finished report job has nothing left to show once the batch modal is` and ends with `app.report_job.reset();` and its closing brace); `AppState::update_background_jobs` does it now.

Right after `render_actions_row(app);`, add:

```cpp
    render_batch_strip(app);
    render_batch_done(app);
```

(At merge these go at T9's `// T13: batch strips` marker, which is the same place: between the toolbar and the settings bar.)

Replace `if (app.batch_job || app.batch_confirm_pending) render_batch_modal(app);` with `render_batch_confirm(app);`.

In the empty-library branch, replace

```cpp
        ImGui::TextUnformatted(
            "No songs scanned. Click \"Manage folders...\" to add your song folder, "
            "then \"Scan charts\" to get started!");
```

with `ImGui::TextUnformatted(empty_library_message(app.settings));`. (At merge this line lands in T9's `render_library_pane`, which calls the same helper.)

- [ ] **Step 9: Build and run.** From Bash, build `Hydra`, `hydra_tests` and `hydra_uitest`. Run `.\build-cpp\Release\hydra_tests.exe` (`Status: SUCCESS!`, including `batch text*`), the Verify command (seven `[PASS]`), and every test registered in `uitest_batch_reports.cpp` by name. Then run `--all` once. Failures in other files that click `Scan charts`, `Analyze library` without the dots, `redo existing` or `Compare dmleaderboards user...` are other wave-3 tasks' to move; list their names in your report for the main session.

- [ ] **Step 10: Commit.**

```powershell
git add src/ui/library_dialogs.cpp src/ui/library_toolbar.cpp src/ui/library_parts.h src/ui/library_view.cpp src/ui/win32_dialogs.h src/ui/win32_dialogs.cpp src/ui/app_state.h src/ui/app_state.cpp src/ui/app_shell.cpp CMakeLists.txt tests/test_batch_text.cpp tests/ui/uitest_batch_reports.cpp
git commit -m "Batch runs in a strip under the toolbar; dialogs answer Esc and Enter; problems stay on the status line

Task: Task 13 - background batch and dialogs
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: The user guide and developer docs describe the new interface

The user guide still describes the details modal, the paging arrows and the Auto checkbox. It has no section at all for the Preview tab or the Stars tab. The developer cheat-sheet for GUI tests lists the old labels, and CONTEXT.md still defines Auto. This task rewrites those parts to match what waves 1 to 3 built. It changes no code.

What the user sees: `docs/UserGuide.md` walks through the new screen in the order you meet it: the toolbar, the settings bar, the library and search, the song panel and its four tabs, background batch analysis, and reports.

**Wave:** 4. **Depends on:** T1 to T13 merged. **Expected overlaps:** none.

**Goal:** Every doc that names a label, a setting or a screen matches the merged app.

**Files:**
- Modify: `docs/UserGuide.md`, `docs/agents/ui-testing.md`, `CONTEXT.md`
- Modify: `README.md` (only lines that mention Auto, the details window or report locations)

**Acceptance Criteria:**
- [ ] `Select-String -Path docs\UserGuide.md,docs\agents\ui-testing.md,CONTEXT.md,README.md -Pattern '\bAuto\b|Analyze paths!|Song Details|redo existing|Refresh scan|##pageleft'` prints nothing.
- [ ] `docs/UserGuide.md` has a section for each of: Analysis settings, Searching the library, The song panel, Paths tab, Preview tab, Dynamics tab, Stars tab, Analyzing the whole library, Reports.
- [ ] Every label in the plan's label contract appears in the cheat-sheet in `docs/agents/ui-testing.md`, and each one appears in `hydra_uitest.exe --script` output or the source (the reviewer spot-checks five).
- [ ] The search syntax in the guide matches `parse_library_query`: quotes, `artist:`, `charter:`, `folder:`, `title:`, `stars:N`, `squeeze<=N`, each with one example drawn from the scratch library.

**Verify:** `Select-String -Path docs\UserGuide.md,docs\agents\ui-testing.md,CONTEXT.md,README.md -Pattern '\bAuto\b|Analyze paths!|Song Details|redo existing|Refresh scan|##pageleft'` → no output.

**Steps:**

- [ ] **Step 1: Collect the facts from the merged code, not from this plan.** Run `.\build-cpp\Release\hydra_uitest.exe --script` with a short script that opens Burnout (`click Scan library`, `wait-idle`, then open the song and dump `##songpanel`, `##settingsbar` and each tab). The script verbs are in `docs/agents/ui-testing.md`. Save the dumps in `$env:TEMP\hydra_ui_T14\`. Every label the docs quote must appear in those dumps.

- [ ] **Step 2: Rewrite the user guide's interface sections.** Keep the guide's existing voice: plain sentences, one idea each, short paragraphs. Use the section order in the acceptance criteria. For each setting in the settings bar, say what it changes and that it applies to every song. For SP cap, say that 4 is Clone Hero's rule and that other values are what-ifs. Don't mention Auto. Describe Stale with both of its causes: another Hydra version, or different rules in `hydra_rules.ini`. The Preview section covers the path picker, the activation jumps and their keys (`[` and `]`), the transport keys (Space, arrows, comma and period), the score box, the drain box and the SP meter. The Stars section covers the cutoffs, the solo-bonus column and the rule that the solo bonus doesn't count toward stars. The batch section covers the confirm dialog, the strip, Pause and Stop, and where reports are saved (Documents\Hydra).

- [ ] **Step 3: Update the GUI-test cheat-sheet.** In `docs/agents/ui-testing.md`, replace the label table with the plan's label contract, as merged. Add the four per-area test files and say that a new test is registered in its area file's entry table.

- [ ] **Step 4: Update CONTEXT.md.** Remove the Auto glossary text. Add two entries in the file's existing style. "Library search": what the query language matches, and that folding ignores case and accents. "Analysis settings": the six settings that make up a result's identity, and that the Backend limit is display-only and is not one of them.

- [ ] **Step 5: Check README.md.** Fix only the lines the acceptance grep finds.

- [ ] **Step 6: Commit.**

```powershell
git add docs/UserGuide.md docs/agents/ui-testing.md CONTEXT.md README.md
git commit -m "Docs: user guide, GUI-test cheat-sheet and glossary for the redesigned interface

Task: Task 14 - user guide and developer docs
Agent: <executor>
Session: <session>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 15: The final check and your look at the new Hydra.exe (main session)

After four waves of merges, this task checks the whole thing on a quiet machine, then builds `Hydra.exe` for you to try. It fixes nothing by itself. A failure goes back to the task that owns the file, as a follow-up.

**Wave:** after wave 4. **Depends on:** everything. **Expected overlaps:** none.

**Goal:** Every check passes on the merged `hydra-test`, and you have looked at the new interface yourself.

> **USER-ORDERED GATE — NON-SKIPPABLE.** This task was requested by the user in the current conversation. It MUST NOT be closed by walking around it, by declaring it "verified inline", or by substituting a cheaper check. Close only after every item in `acceptanceCriteria` has been re-validated independently, with output captured.

**Files:** none changed. Output goes to `$env:TEMP\hydra_ui_final\`.

**Acceptance Criteria:**
- [ ] A full build (`build_cpp.ps1` with no target, then `-Target hydra_uitest`) finishes with no errors.
- [ ] `hydra_tests.exe` ends with `Status: SUCCESS!`, with more test cases than `tests_summary.txt` from Task 0.
- [ ] `hydra_uitest.exe --all` passes every test, and the count is at least the 26 from Task 0 plus the new ones each task added.
- [ ] The score-neutral diff against `$env:TEMP\hydra_ui_base\batch_sorted.txt` prints nothing.
- [ ] `git worktree list` shows no `wt-ui-T*` worktrees, and `git status --short` shows only the untracked ch-probe handoff.
- [ ] You have run the new `Hydra.exe` and said in chat whether it matches the mockup.

**Verify:** `.\build-cpp\Release\hydra_uitest.exe --all | Select-String 'PASS|FAIL' | Measure-Object` → every line is PASS.

**Steps:**

- [ ] **Step 1: Build and run every check on a quiet machine.** Wait until no `cl.exe`, `link.exe`, `MSBuild.exe` or `hydra_*.exe` has run for 60 seconds, checked with `tasklist`. Then build and run the four checks from the acceptance list, saving each output in `$env:TEMP\hydra_ui_final\`. Use the score-neutral recipe from the Global Constraints with `$out = "$env:TEMP\hydra_ui_final"`.

- [ ] **Step 2: Check the Auto cleanup on a copy of a real database.** Copy the dev build's `build-cpp\Release\hydra.db` to `$env:TEMP\hydra_ui_final\copy.db`. Count its Auto rows with the query T6's section names, open it once with the new store (T6 names a `hydra_tests` case or CLI call that does this), and count again. The second count must be 0, and the fixed-cap row count must be unchanged. Never run this against the installed app's database under Program Files.

- [ ] **Step 3: Build Hydra.exe and hand it over.** Build with `-Target Hydra` (the test targets don't rebuild it). Check that its timestamp is later than the last merge commit. Tell the user three things in chat. First, where the exe is. Second, that the dev build reads `hydra.db` from its own folder. Third, that the first start deletes that database's Auto results (decision 7), and it only does this once. Ask them to try the flows in "What you'll see when it's done" and say whether it matches the mockup. Also ask them to check two things no automated test can reach. First, closing and reopening Hydra.exe puts the window back where it was, at the same size. Second, dragging the window onto a monitor with a different Windows scale keeps the text sharp and the layout in proportion (T5).

- [ ] **Step 4: Record the outcome.** Put the user's verdict and any follow-ups in a short release note, `docs/handoffs/2026-09-27-ui-redesign-release-note.md`, in the style of `docs/handoffs/2026-09-26-audit-fixes-release-note.md`. Commit it with the four trailer lines. Tagging, the installer and a GitHub release are a separate step, done only when the user asks.
