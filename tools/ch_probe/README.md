# ch_probe — measuring Clone Hero's real drum hit window

## What this is

We took Clone Hero's drum hit window apart with Ghidra, and static analysis
answered everything except one question: does the window top out at 85 ms, or
does it rise to about 89.5 ms at moderate note spacings? The decompiler can't
tell us, because the code that might clamp the number is reached through a
function pointer it can't follow. The only way to know is to watch the running
game — attach a debugger, read the numbers the engine computes, and feed it
test inputs to see what it accepts.

This tool does that. It's Python driving the live game through a debugger. It
lives here, off to the side of Hydra's C++ build, so it never tangles into the
optimizer's CMake.

The full design and every reverse-engineering fact is in
[`docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md`](../../docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md).

## The one idea it rests on

You can't deliver a keystroke to Windows at a precise millisecond — the OS adds
several milliseconds of jitter, which is huge next to an 85 ms window. So don't
trust the timing of the input. Trust what the engine recorded about it. The
game measures its own hit delta and stores whether the note counted. So every
test input is a fact: "at a measured delta of X ms, this note was hit or
missed." Fire a few hundred inputs near the edge and the line between the hit
cluster and the miss cluster is the true window. No precise input timing
needed — the debugger is the measuring instrument.

## The pieces

Two shared files hold everything the pieces must agree on:

- `constants.py` — every address, offset, and constant from the reverse
  engineering. The one place the numbers live. Nothing else hard-codes an
  address.
- `interfaces.py` — the API contract. Each module implements the Protocol
  named for it, so the pieces snap together without having seen each other.

The seven working pieces, each understandable and testable on its own:

- `process.py` — opens the game, finds where `GameAssembly.dll` loaded, turns
  the fixed Ghidra addresses into live ones, and reads memory. Before anything
  else it checks its targets against what Ghidra saw and refuses to run on a
  mismatch, so a game update can't make it read garbage.
- `debugger.py` — the Win32 debug loop. Sets software breakpoints, catches
  them, reads and writes registers and memory, and continues.
- `engine.py` — the meaning layer. Grabs the live engine object at the
  constructor breakpoint and exposes clean reads: window, delta, hit flag, song
  clock, constants. Everything above it speaks in those terms, never raw
  addresses.
- `probe_chart.py` — writes small `.chart` files of isolated note pairs at
  controlled spacings, so the active probe tests clean geometry.
- `input_driver.py` — wraps `SendInput` and fires a keystroke near a target
  moment on the song clock.
- `ocr.py` — reads the on-screen "Accuracy: X ms" as a trust check: when the
  memory delta and the printed number agree, the memory reads are believable.
- `experiments/` — the two runners (passive and active probe) plus the
  analysis that turns rows into the boundary answer.

## What runs here, and what needs the game

Anything that reads a live process or attaches a debugger needs Clone Hero
actually running — it can't be tested on a build machine. So the unit tests in
`tests/` cover only the pure logic: the address math, the byte-check refusal,
the `.chart` file format, and the boundary detection on made-up data. The parts
that only a live game exercises are isolated behind clean seams and marked in
the code.

Run the pure tests from the repo root:

```bash
python -m pytest tools/ch_probe/tests -q
```

## Build order (for the session sitting at the game)

Work outside-in and prove each layer before building on it.

1. Attach and read the two window constants. If 85.0 and 37.5 come out, the
   whole address pipeline is correct.
2. Breakpoint the constructor, grab the object pointer, read the window fields
   back — they must match the constants.
3. Passive probe: breakpoint the formula's return, sample the stored window,
   log `(spacing, raw, stored)` across a varied chart. This answers the clamp
   question — write up the answer before touching the active probe.
4. OCR cross-check on a handful of manual hits.
5. Probe chart plus input driver: generate the chart, fire timed inputs,
   confirm they register.
6. Active probe plus analysis: sweep the offset, collect
   `(measured_delta, hit/miss)` per spacing, plot the boundary against the
   parabola the formula predicts, compare to the passive result.

## Why bother

Hydra's model currently uses a flat 85 ms hit window. The point of this work is
to decide whether that should become the per-note quadratic the game really
uses.
