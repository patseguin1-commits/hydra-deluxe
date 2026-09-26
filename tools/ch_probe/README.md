# ch_probe: measuring Clone Hero's real drum hit window

## What this is

We took Clone Hero's drum hit window apart with Ghidra. Static analysis
answered everything except one question: does the window top out at 85 ms, or
does it rise to about 89.5 ms at moderate note spacings? The code that might
clamp the number is reached through a function pointer the decompiler can't
follow. So the only way to know is to watch the running game.

This tool does that, in Python, off to the side of Hydra's C++ build. The full
design and every reverse-engineering fact is in
[`docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md`](../../docs/superpowers/specs/2026-09-17-ch-dynamic-input-probe.md).

## The one idea it rests on

Windows can't deliver a keystroke at a precise millisecond. So don't trust the
input's timing; trust what the engine recorded about it. Every test input
becomes a fact: "at a measured offset of X ms, this note was hit or missed."

## The route that works at the game today

`experiments/play_chart.py` auto-plays a chart and hits the notes (proven on
"Slipping", 2026-09-25). It needs no debugger. It:

1. opens the game and checks the two window constants (`process.py`);
2. finds the live engine object by memory scan (`engine_finder.py`): the
   engine keeps the window constants at +0x30/+0x38, and the live one is the
   only candidate whose song clock moves;
3. reads the song clock (+0x100) and the score (+0x94) through `EngineModel`
   (`engine.py`);
4. presses each chord with `InputDriver.press_chord` (`input_driver.py`),
   using the one key table, `Lane`.

A hit is a score that rose. `experiments/walk_edges.py` and
`experiments/watch_window.py` (the hit-window plan,
docs/superpowers/plans/2026-09-25-hit-window-testing.md) run on the same
pieces.

## The pieces

- `constants.py`: every address, offset and constant. The one place the
  numbers live.
- `interfaces.py`: the API contract each module meets.
- `process.py`: opens the game, turns Ghidra RVAs into live addresses, reads
  memory, and refuses to run if the constants don't match the build.
- `engine_finder.py`: finds the live engine object without a debugger.
- `engine.py`: the meaning layer. Named reads: window, clock, score, flags,
  constants. It takes an engine pointer from `engine_finder` (`use_object`)
  or catches the constructor with the debugger (`capture_object`).
- `input_driver.py`: the key table (`Lane`, `DEFAULT_BINDINGS`) and SendInput.
  "Lane" always means an input lane: 0 is green, 4 is the kick. A .chart
  numbers notes differently (note 0 is the kick), so chart code says "note".
- `debugger.py`: the Win32 debug loop with int3 breakpoints. Attaching turns
  Windows' kill-on-exit off, and `stop()` removes every breakpoint before
  detaching, so the game survives the tool going away.
- `probe_chart.py`, `probe_songs.py`: write probe charts and playable probe
  song folders.
- `ocr.py`: parses "Accuracy: X ms" text (the screen capture was removed).
- `experiments/`: the runners. `passive_probe.py` and `active_probe.py` use
  the debugger; the others don't.

## What runs here, and what needs the game

The unit tests in `tests/` cover the pure logic and every seam through fakes.
Anything that reads a live process needs Clone Hero running. Run the tests from
the repo root:

```bash
python -m pytest tools/ch_probe/tests -q
```

## Why bother

Hydra's model uses a flat 85 ms hit window. The point of this work is to
decide whether that should become the per-note curve the game really uses.
