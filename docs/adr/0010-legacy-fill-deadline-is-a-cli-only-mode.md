# The legacy fill deadline is a CLI-only mode, split by database file

**Superseded 2026-09-29 by the user's decision: the rule is now part of a
result's key, and the GUI has a "1.0 fills" setting.** See the note at the
end. The rest of this page is the original decision, kept for its reasoning.

A drum fill only appears in-game if your Star Power meter filled up in time.
Clone Hero 1.1 sets that deadline a flat 4 beats before the fill starts. Clone
Hero 1.0 set it about one fill-length earlier, clamped to between 250 ms and 10
seconds.

Hydra scores by the 1.1 rule everywhere. The 1.0 rule exists too, but only as a
command-line mode: `hydra_batch --legacy-fills`. The GUI has no switch for it,
and the setting is not saved anywhere.

The rule is deliberately **not** part of a record's identity. Records are keyed
by chart, chart mode, SP cap, and lens (ADR-0003, ADR-0009). The fill rule is
not in that key. So a legacy run must be given its own database file with
`--db`, and `hydra_batch` refuses to run `--legacy-fills` against the database
the app itself reads.

Each database carries a stamp of the rule that filled it. `hydra_batch`
refuses (exit code 2) a run whose rule disagrees with that stamp, so one file
never mixes the two, and `--reindex` never changes it. A file with results but
no stamp was written before stamping existed, by the 1.1 rule, and counts as
1.1.

`hydra_fillcompare --old <ch10.db> --new <ch11.db>` joins two such files by
chart hash and reports where the two rules disagree.

## Why not put the rule in the key

That is the obvious alternative, and it is what ADR-0003 did for the SP cap. We
turned it down for three reasons.

The key change is not free. It means a schema migration, a new column on every
result row, and every lookup in the app having to say which rule it wants. The
SP cap earned that because it is a real user setting people switch between. The
1.0 rule is a historical curiosity — you run it once to answer a question, look
at the report, and move on.

Nobody wants to see 1.0 numbers in the app. If legacy rows lived in the main
database they would need to be filtered out of the library table, the report,
the leaderboard comparison, and the path view. Every one of those is a place to
get it wrong. A separate file gets it right by construction: the app opens one
file and that file has no legacy rows in it.

The 1.0 rule is off the bit-for-bit scoring surface. It calls
`MsIndex::ms_at_tick_f`, which `core/timing.h` marks as display-layer only
because it interpolates between ticks in floating point. Stored 1.1 records are
promised to be reproducible byte for byte; 1.0 records make no such promise. We
did not want two grades of trustworthiness sharing one table.

## The guard

`hydra_batch --legacy-fills` with no `--db` — or a `--db` that resolves to the
same file as the default — prints an error and exits 2. Paths are compared after
resolving `.`, `..` and relative prefixes, and case-insensitively, because
Windows paths are.

This matters because a poisoned database is silent. A legacy row and a normal
row look identical once stored. If one slipped into the app's database, the
library would show a wrong score with nothing marking it as wrong, and the only
fix would be re-analyzing the chart.

## The stamp

Every `hydra_batch` run writes `engine_mode` into the database's `meta` table:
`"ch10"` or `"ch11"`. It is a label on the file, not on any row, and nothing
reads it to make a decision.

`hydra_fillcompare` checks it and prints a warning if a file's stamp disagrees
with the side it was passed on. It only warns. A file with no stamp is fine and
says nothing — that is just a database written before this existed, and the
normal rule is the right assumption.

## What this costs

The two runs cannot share work. Analyzing a library twice takes twice as long,
and the two databases hold two full copies of the paths.

A user can still put legacy results in the wrong file by naming one explicitly
(`--db` at some path, then later reusing that path for a normal run). The stamp
turns that into a warning rather than a silent wrong answer, which is the most a
file-level split can do.

## Note, 2026-09-29: the rule joins the key

The user asked for a GUI switch. Of the two ways to give it one, they chose
the key over a second database file: the song list lives in the database too,
so a second file would have started empty and needed its own scan.

What changed:

- `store::Lens` gained `legacy_fills`, and the `results` table a
  `legacy_fills` column inside its UNIQUE constraint (schema 3). Every lookup
  already passes a Lens, so the library, the Paths tab, the path report and
  the leaderboard comparison each find only the rule the settings name. The
  worry above, that legacy rows would need filtering out everywhere, is
  answered by construction, the same way the SP cap's was (ADR-0003).
- The schema 3 rebuild files a file's older rows under 1.1, unless hydra_batch
  stamped the file `ch10`; then under 1.0. So a `--legacy-fills` database
  from before still reads as 1.0.
- `prepare_row` refuses a key that names the other rule. `HydraRecord` carries
  the rule in memory (`legacy_fills`), not in its stored bytes; `get_record`
  sets it from the key.
- The app's setting is `legacy_fills` in hydra_settings.ini, a checkbox
  beside the SP cap. "Compare with dmleaderboards" is disabled while it is on.
- `hydra_fillcompare` reads 1.0 results from `--old` and 1.1 results from
  `--new`, so both may name one file. Swapped files now find nothing to
  compare instead of a page with the sides mislabelled.
- `hydra_report` follows the app's setting, except on a file stamped `ch10`,
  which it reports under 1.0 as before.

What stayed: `hydra_batch` goes by `--legacy-fills` alone, never by the app's
setting, and still refuses to write `hydra.db` or a file stamped with the
other rule. Those guards are no longer needed for correctness. They were kept
because the request was for the GUI only.

The byte-for-byte point above still holds: 1.0 results interpolate between
ticks in floating point. They now share a table with 1.1 results, but never a
key, so no lookup can mix the two.
