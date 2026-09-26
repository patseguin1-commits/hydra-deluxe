# Release note: the 2026-09-26 audit fixes

This release fixes everything in the 2026-09-25 codebase audit. Most of it is invisible: bugs that only showed under rare conditions, dead code, speed, and cleanup. The plan is docs/superpowers/plans/2026-09-26-codebase-audit-fixes.md.

## Re-analyze once

Every stored result reads Stale after upgrading. Two changes cause this, and both are in this release, so the library needs one re-analysis, not two. The record format changed (ADR 0017): the multiplier squeezes are stored once per chart, and fields nothing read are gone. And the rules fingerprint changed its text, because the Auto time budget no longer counts (see below). The numbers shown after re-analysis are identical to before; the batch comparison over the test charts is unchanged.

## What you will see

The path report has a Mode column, and its subtitle counts the rows on the page, not the whole database.

Report song names follow song.ini. Before, a song's name was frozen at its first analysis.

The Preview applies the chart's audio offset to .sng charts (their stored delay, else the chart's Offset) and .srb charts (the chart's Offset), the same rule folder charts already used.

On a PC with no audio device, the Preview draws the highway and shows one line: "Audio unavailable: <reason>. The preview is muted." Before, it showed "Preview failed" and drew nothing.

A database from Hydra 1.5 or 1.6 shows "Not analyzed" instead of "Stale". Those rows could not be used since 1.8.1 anyway.

Editing the Auto time budget in hydra_rules.ini no longer marks anything Stale. Editing the Auto ladder marks only Auto results Stale.

## Files next to the database

The database now uses SQLite's WAL mode, which makes saving much cheaper. While Hydra runs, two extra files sit next to it: hydra.db-wal and hydra.db-shm. They are normal and belong to the database. A power cut can lose the last few seconds of saved results, which you would simply analyze again.

## Faster

On the test charts, a full hydra_batch run went from 0.71 s to 0.11 s, a reindex from 0.028 s to 0.011 s, and the unit test suite from 17.7 s to 7.9 s. The engine's benchmark for one chart went from 0.37 s to 0.07 s at the 4-bar cap and from 0.85 s to 0.19 s on Auto.

## Command line

hydra_batch refuses a run (exit code 2) whose --legacy-fills flag disagrees with the database's fill-rule stamp, and --reindex never changes the stamp. hydra_batch now reuses the GUI's scan cache instead of re-hashing every chart.

## The shipped exe

Hydra.exe from the installer no longer contains the GUI test engine's tests or any path of the builder's folders. The Opus library's safety checks name their source files relative to the repo (`/d1trimfile`), and the installer now refuses to package an exe that holds the repo path in any form.
