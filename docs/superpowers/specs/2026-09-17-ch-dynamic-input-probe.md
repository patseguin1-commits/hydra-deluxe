# Handoff: dynamic input probe for the Clone Hero drum hit window

**Date:** 2026-09-17
**Status:** design approved, not yet implemented
**For:** the next session, which builds this
**Lives in:** this repo (`hydra-test`), in its own Python subtree (see "Where the code goes")

---

## What this is, in one paragraph

We reverse-engineered Clone Hero's drum hit window from `GameAssembly.dll`
with Ghidra. Static analysis took us as far as it can. It left one question
open that the decompiler physically cannot answer: the game computes the hit
window with a quadratic formula that has **no clamp inside it**, and the code
that calls that formula (and might clamp its result before use) is reached
through a function pointer the decompiler can't follow. So we don't know
whether the window the game actually enforces tops out at 85 ms or rises to
~89.5 ms at moderate note spacings. The only way to find out is to watch the
running game: attach a debugger, read the numbers the engine computes and
enforces, and feed it test inputs to confirm what it accepts. This document
is the plan for that tool.

## The one idea the whole design rests on

You cannot deliver a keystroke to Windows at a precise millisecond — OS
scheduling jitter is several milliseconds, which is huge next to an 85 ms
window. So **don't trust the timing of the input. Trust what the engine
recorded about it.** The game measures its own hit delta and stores it, and
it records whether the note counted as hit. Every test input therefore
produces a fact: "at a measured delta of X ms, this note was HIT / MISSED."
Fire a few hundred inputs scattered near the edge and the boundary between the
hit cluster and the miss cluster is the true window — no precise input timing
required. This turns an impossible input-timing problem into an easy
measurement problem, and the debugger is the measuring instrument.

## The two experiments

**Passive probe — run this first; it may answer the whole question alone.**
Attach, let a song start, and read two numbers per note: the raw value the
formula returns, and the value the engine actually stores and uses as the
window. Play a chart with a wide range of note spacings and log every
`(spacing, raw_formula_output, stored_window)` row. If the stored window
tracks the formula all the way up the parabola (~89.5 ms per side at ~211 ms
spacing), there is no clamp. If it flat-lines at 85 ms while the raw output
climbs past it, the clamp is real and sits at the constructor's default. No
synthetic input is needed for this — the chart's own notes supply the
spacings.

**Active probe — run this second, to confirm what the game accepts.**
The formula tells you the *intended* window. Only a real input proves the
*enforced* one, because there could be a second clamp at the hit-comparison
site. Play a purpose-built probe chart of isolated note pairs at known
spacings, read the song clock from memory, and fire a `SendInput` keystroke
near each note with an offset that walks across the edge. At the hit-check
breakpoint, read the measured delta and the hit/miss flag. Plot hit/miss
against measured delta for each spacing; the crossover point is the enforced
window edge. If it matches the passive numbers, the model is confirmed. If it
caps at 85 ms while the passive numbers say 89.5, the clamp is at the
comparison, not the formula — and now you know exactly where.

---

## Verified reverse-engineering facts

Everything below is cross-checked against the actual Ghidra dumps in
`C:\Users\Patrick\Downloads\Hydra\hydra-data\scratch_ms\` (`ghidra_decompile_output.txt`, `ghidra_decompile_all.txt`). The
summary artifact's addresses check out against these dumps — its
`DrumsEngine constructor at RVA 0x20DF680` is correct (dump `output.txt` line
52 labels `DrumsEngine_ctor` at exactly `0x20df680`, and the window constants
are loaded inside it). Still verify every address live before use: a Clone Hero
update shifts all RVAs, which is a build-drift risk, not an artifact error.

Build analyzed: Clone Hero v1.1.0.6142, Unity IL2CPP x64, engine "StrikeCore".

### Functions (RVAs are offsets from the `GameAssembly.dll` base)

The **dynamic window formula** is `FUN_1820ddda0` at RVA `0x20ddda0`, with
signature `double FUN_1820ddda0(longlong self, double t)` (full body at dump
`ghidra_decompile_all.txt` lines 757–782). It has **two branches**, selected by
the same PrecisionMode bit the constructor uses (`self+0x198 & 0x1000`):

- **Normal** (bit clear, line 774):
  `return ((t*C1 - pow(t,e)*C2) * C3 - C4) / C5;`
  This `(linear − quadratic)·scale − offset` shape matches the artifact's
  `window = (0.0110924·t − 2.628539e-05·t²)·170 − 20`.
- **Precision** (bit set, line 781):
  `return (C0 - (t*C1' - pow(t,e)*C2') * C3') / C5;`
  A *different* shape (`offset − scaled term`), which the artifact never
  mentions.

Two things to notice before trusting any numbers. First, `t` is pre-scaled
(`t = t * C5`, line 767) and the result is divided by that same `C5`
(`DAT_183064c28`, probably 1000 for a seconds↔ms conversion) — the artifact
folded this into "t × 1000" and dropped the matching divide. Second, the
"quadratic" term is really `pow(t, e)` where the exponent `e`
(`DAT_1830658a8`) is a constant to read live; the artifact assumed `e = 2`.

The critical fact stands: **there is no clamp in this function** — both
branches return the raw quotient. Any bounding happens in the caller.

**The coefficient values aren't in these decompile dumps** — only the symbolic
`DAT_*` references below — but the originating session *did* read the resolved
doubles straight from `.rdata` (e.g. `0x0313ECB0 = 0.0110924370`, the linear
coefficient; `0.0375` = normal front, `0.025` = precision front). Those
resolved reads were never saved to disk, so re-read them live to confirm they
match the running build. What is genuinely inferred rather than decoded is the
interpretation on top: the ~211 ms peak and the 89.5 ms per-side maximum are
computed from the formula, and whether that maximum is actually *enforced* is
the open clamp question. Confirming the curve and the clamp is the point of the
passive probe.

The **drums engine constructor** is `DrumsEngine_ctor` at RVA `0x20df680` (dump
`ghidra_decompile_output.txt` line 52 — this is the artifact's address, and it
is correct). It tests the PrecisionMode bit and writes the three window fields
(same dump, lines 142–156). A separate base routine `BaseEngine_ctor_drums` at
`0x100e960` handles shared setup. `DrumsEngine_ctor` is a good breakpoint for
capturing the live engine object pointer, because on entry the object is in
`rcx` (x64 fastcall / IL2CPP convention).

The **per-note hit loop** is `DrumsEngine_noteProcessing` at RVA `0x20dcc90`
(dump `ghidra_decompile_output.txt` line 168). This is where a hit is matched
to a note; it's the place to correlate a fired input with the note and delta
the engine assigned it.

The **hit-check** routine is labeled `BaseEngine_hitCheck_drums` at RVA
`0x100dab0`. The hit-decision comparison against the window field appears at
`ghidra_decompile_all.txt` lines 808–810 and 1049–1051. Note: that
decompilation indexes the object as a `longlong*` (word-indexed), so
`self[0x20]` there is byte offset `0x100`, not `0x20` — confirm every offset
in this routine live with the debugger rather than reading it off the
decompiler, because the pointer-vs-byte indexing is easy to misread.

The **accuracy display path** (the "Accuracy: {0:0.0} ms" text, via a
timer-delta call) was described in the artifact but not pinned down in the
dumps. Locating it is optional — it's only needed if you want to breakpoint
the display directly. The OCR cross-check reads the same number off-screen
without it.

### Engine object offsets (byte offsets from the object pointer)

These come from the constructor, where the casts are unambiguous
(`*(double*)(self + 0x20)` etc.), so treat them as solid — but still confirm
once live:

- `+0x20` — **total search window**, a `double`. Set to `back × 2` at
  construction, then overwritten per note by the dynamic formula's caller.
  **This is the field the passive probe watches.**
- `+0x30` — back-window constant, `double` (max per-side; ~85 ms normal).
- `+0x38` — front-window constant, `double` (min per-side; ~37.5 ms normal).
- `+0x100` — hit time: the engine's timestamp captured at the moment of a hit.
- `+0x198` — flags dword; **bit `0x1000` is PrecisionMode** (clear = normal).
- `+0x8c` — note count used by the processing loop.

### The `.rdata` constant globals

The window constants are doubles in `.rdata`; read them live once you know the
module base. Their Ghidra virtual addresses:

- Normal mode: back = `DAT_1831406c8`, front = `DAT_1831406e0`.
- Precision mode: back = `DAT_1831406b8`, front = `DAT_1831406d0`.
- Formula constants (normal branch): `_DAT_183140738`, `_DAT_183140658`,
  `_DAT_1831406f8`, `DAT_183140700`, and divisor `DAT_183064c28`.
- Formula constants (precision branch): `_DAT_1831406b0`, `_DAT_183140620`,
  `_DAT_183140708`, `DAT_183063fc0`, same divisor `DAT_183064c28`.
- `DAT_1831406e8` — a threshold used in the hit-check comparison; capture it
  too, it may be part of the enforced-window logic.

Reading these live and printing them in decimal is itself a good first
milestone: if `DAT_1831406c8` reads 85.0 and `DAT_1831406e0` reads 37.5, your
address math is correct and you can trust everything downstream.

---

## How the tool is built

Seven small pieces, each understandable and testable on its own. The rule:
the raw debugger plumbing never mixes with the "what the numbers mean" layer.

The **process/address layer** opens the Clone Hero process, finds where
`GameAssembly.dll` loaded, and turns the fixed Ghidra RVAs into live
addresses. Before doing anything else it reads a few bytes at each target and
checks them against what Ghidra saw; on a mismatch it refuses to run rather
than read garbage from a different game build.

The **debug loop** is the Win32 `WaitForDebugEvent` engine. It sets `int3`
(0xCC) software breakpoints on code — constructor, formula return, hit check —
catches them, reads and writes registers and memory, and restores/single-steps
to continue. It can also set a hardware data breakpoint (via the debug
registers) on `self+0x20` if you'd rather catch the write to the window field
than sample it.

The **engine model** is the meaning layer. It knows the RVAs and offsets,
captures the live object pointer at the constructor breakpoint, and exposes
clean reads: current window, measured delta, hit flag, song clock, and the
decoded constants. Everything above it speaks in those terms, never in raw
addresses.

The **probe-chart generator** writes small `.chart` files containing isolated
note pairs at controlled spacings, so the active probe tests clean geometry
instead of hunting for the right spacing inside real songs.

The **input driver** wraps `SendInput` and schedules a keystroke against the
song clock at a target offset. Remember: its timing only needs to land the
input *near* the edge, because the measured delta — not the intended one — is
what we record.

The **OCR cross-check** reads the on-screen "Accuracy: X ms" from a screen
crop, reusing the WinRT OCR approach already proven in this project. Its job
is trust: when the memory delta and the printed number agree, you know your
offsets are right and can believe a whole run's worth of memory reads.

The **experiment runners and analysis** tie it together — one script per
experiment — and aggregate the rows into the boundary plot, drawn against the
parabola the formula predicts.

## Where the code goes

Put it under a dedicated Python subtree so it stays out of the C++ optimizer's
build — suggested `tools/ch_probe/` with the seven pieces as separate modules
and an `experiments/` folder for the two runners. It's Python driving an
external process; it should not tangle into Hydra's CMake. (If the next
session prefers a sibling repo like `fcvideo` has, that's fine too — the user
asked to keep it in `hydra-test` for now, so default to the subtree.)

## Suggested build order

Work outside-in and prove each layer before building on it.

1. **Attach and read constants.** Open the process, resolve the module base,
   read the four window constants, print them in decimal. Success = 85.0 /
   37.5 come out right. This validates the entire address pipeline in one shot.
2. **Capture the live object.** Breakpoint the constructor, grab `rcx`, and
   read `self+0x20/0x30/0x38` back — they must equal the constants (with
   `+0x20 = 2 × +0x30`).
3. **Passive probe.** Breakpoint the formula return (read its `double` result
   from `xmm0`) and sample `self+0x20` right after. Log
   `(spacing, raw, stored)` across a varied chart. **This answers the clamp
   question.** Write up the answer before touching the active probe.
4. **OCR cross-check.** Confirm the measured delta from memory matches the
   on-screen number on a handful of manual hits.
5. **Probe charts + input driver.** Generate the probe chart; fire timed
   inputs; confirm they register.
6. **Active probe + analysis.** Run the offset sweep, collect
   `(measured_delta, hit/miss)` per spacing, plot the boundary against the
   parabola, and compare to the passive result.

## Probe-chart spacings

Sample densely where the clamp decision actually happens (the 180–220 ms
region near the parabola peak) and sparsely elsewhere for shape:
30, 50, 100, 150, 180, 185, 190, 195, 205, 211, 220, 240, 300 ms. Adjust after the
passive probe tells you where the interesting edge sits.

## Open questions the next session must resolve

- **Drum key bindings.** Clone Hero's drum lane keys are user-configurable and
  weren't captured. Read them from the game's config or set known bindings
  before the active probe; don't guess.
- **The clamp's caller.** The passive probe shows *whether* there's a clamp;
  if there is, find the call site that applies it (it's the vtable-dispatched
  caller of `FUN_1820ddda0`) so the model is complete.
- **Precision vs normal mode.** Confirm which mode the test is running in via
  `self+0x198 & 0x1000`, and decide whether to probe both. The constants
  differ (normal 85/37.5, precision ~40/25).
- **Whether OCR is needed at all** once memory reads are trusted, or only as a
  one-time validation.

## Risks

Attaching a debugger is detectable (`IsDebuggerPresent` and similar). Clone
Hero is community-friendly and almost certainly ships no anti-debug, so the
plan is to just attach and only deal with it if it actually bites — don't
build evasion for a problem that probably doesn't exist. Separately, a Clone
Hero update will shift every RVA; that's exactly why the address layer
byte-checks its targets and refuses to run on a mismatch instead of reading
the wrong memory.

## Source material

- Ghidra dumps (in `C:\Users\Patrick\Downloads\Hydra\hydra-data\scratch_ms\`):
  `ghidra_decompile_output.txt` (constructor and
  note-processing, line refs above) and `ghidra_decompile_all.txt`
  (the formula at lines 757–782, hit-check comparison at 808–810 / 1049–1051).
- Summary artifact (treat as secondary; some addresses disagree with the
  dumps): https://claude.ai/artifact/RvK214fUemHR1sH4wiwNEU
- Hydra's current model uses a flat `kDefaultHitWindowMs = 85.0`; the point of
  this work is to decide whether that should become the per-note quadratic.
