# Upgrade note: derivation fixes

Every saved result reads Stale after this upgrade. The record format changed, so old results cannot be shown until re-analyzed. Run Analyze on your library once. Nothing is lost; the old rows stay in the database.

What you will see change:

- A squeezed-out backend row the game does not count now shows 0 points with "(uncounted)", and so does any plain "Hard (uncounted)" or "Insane (uncounted)" row. None of them is highlighted.
- The calibration fill reads the same sign everywhere: positive means you hit early.
- The average multiplier is rounded everywhere, so Song Details can read 1.667x where it read 1.666x.
- A song with no name shows "(unknown)".
- The Preview's SP gauge fills from the phrases the path really collected, and the Preview honors the song's audio delay.
- The scan finds notes.mid, notes.chart and song.ini in any letter case.

New: hydra_rules.ini. Put it next to Hydra.exe to change the rules that used to be fixed: the 3 ms backend leeway, the squeeze-out rule, the fill constants, the number of tied paths, and the Auto cap ladder and budget. Any change makes every result read Stale until re-analyzed. Switching back brings the old results back. A mistake in the file stops Analyze and shows which key is wrong.
