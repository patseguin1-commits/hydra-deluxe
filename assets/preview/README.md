# Preview assets — copied verbatim from Onyx

Everything under `models/` and `textures/` is a byte-for-byte copy from the
Onyx Music Game Toolkit (mtolly/onyx), release 20251011, taken from
`C:\Program Files\OnyxToolkit\onyx-resources\`. **Do not edit these files.**
To update, copy the same names again from a newer Onyx and re-check the
hashes. Hydra's Preview is a faithful port of Onyx's 3D drum previewer (see
`docs/adr/0008`), so the art must stay identical for the golden-image test
(`tests/test_preview_golden.cpp`) to mean anything.

## What each file is for (Hydra name — Onyx name)

Models (Wavefront `.obj`, read by `src/render/obj_loader`):

- `drum-tom.obj` — tom gem (Red, and Yellow/Blue/Green toms)
- `drum-cymbal.obj` — cymbal gem (Yellow/Blue/Green cymbals)
- `drum-kick.obj` — kick gem (the full-width bar)

Textures:

- `box-{red,yellow,blue,green}.png` — tom gem faces; `box-energy.png` inside an SP phrase
- `cymbal-{yellow,blue,green}.png` — cymbal faces; `cymbal-energy.png` inside an SP phrase
- `long-kick.jpg` — kick gem; `long-energy.jpg` inside an SP phrase
- `overlay-ghost.png`, `overlay-accent.png` — dynamics overlays drawn over the gem face
- `line-1.png`, `line-2.png`, `line-3.png` — bar, beat, half-beat lines
- `target-{red,yellow,blue,green}.png` — the strike line (Onyx "targets");
  `*-light.png` — the glow right after a hit
- `lane-{red,yellow,blue,green}.png` — lane strips: every fill window (Onyx's
  BRE look) and the activation note's lane

## Files Hydra wrote (not from Onyx)

- `3d-config.json` — Onyx's `3d-config.yml` converted by hand, values
  unchanged, plus a `hydra` section for Hydra-only settings.
- `shaders/object.hlsl` — Onyx's `object.vert` + `object.frag` translated to HLSL.
- `shaders/fade.hlsl` — the horizon-fade part of Onyx's `quad.frag`.

Staged next to the executable by the CMake POST_BUILD copy and by `cmake --install`.
