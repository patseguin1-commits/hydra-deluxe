# The record format stores each chart fact once

A stored record is a structure blob (the path tree's shape) plus
content-addressed path nodes (ADR 0009). Until 1.8.1 each node carried its
own multiplier squeezes, six score totals, note count, leftover SP and two
skipped-note counts, beside its activations. Most of that was never read.

## The decision

A node holds a path's activations and nothing else.

The multiplier squeezes depend on the combo alone, and a full-combo path
never breaks combo. So they are one fact about the chart, not about a path.
The search graph finds them once, the record holds one list
(`HydraRecord::multsqueezes`), and the structure blob stores it once, right
after the record's header.

A root path's totals (the six score categories, the note count and the
leftover SP) are stored next to that root in the structure blob. A variant's
totals are not stored at all: `Path::prepare_variants` copies them from the
parent on every load, as it always did.

The two skipped-note counts are gone. Nothing ever set them to anything but
zero, so the two warnings that read them could never show.

The six activation fields the search always sets (skips, timecode, chord,
SP meter, frontend points, calibration-fill offset) are plain values, with
no presence byte. The deactivation node, the cap-clamp tick and the
squeeze-out tick can legitimately be missing, so they keep theirs.

The whole-record blob format that `write_record` and `read_record` spoke is
deleted. Only tests used it, and it could not read a real old blob since
ADR 0015 anyway.

## What this costs

The path node format and the structure format both go from 5 to 6. We call
the result record format v7, since it follows blob format 6; there is no
constant named 7. Every result analyzed before this change reads Stale, and
the library needs one re-analysis. Every number, label and path string it
shows afterwards is unchanged.
