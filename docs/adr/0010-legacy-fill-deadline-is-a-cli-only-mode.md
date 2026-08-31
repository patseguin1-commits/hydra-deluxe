# The legacy fill deadline is a CLI-only mode, split by database file

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
