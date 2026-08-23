# The Preview is a faithful port of Onyx's 3D drum previewer

The first Preview drew a highway reverse-engineered from screenshots of Onyx
(mtolly/onyx), guessing at shapes, colours and camera numbers. Onyx already has
the previewer we want, and every model, texture, shader and layout number sits
on disk in its install (`onyx-resources\`). Onyx is Haskell, so it cannot be
linked; instead the Preview is now a **port**: Onyx's `.obj` models and
textures are copied byte-for-byte into `assets/preview/` (`README.md` there is
the manifest), its `3d-config.yml` is converted once to `3d-config.json` and
read at startup, its `object.vert`/`object.frag` shader is translated line for
line into `shaders/object.hlsl` (with the horizon fade from `quad.frag` in
`shaders/fade.hlsl`), and its drawing order and per-frame maths are ported
into the device-free `render/track_state` and `render/highway_draw`. The
renderer (`render/preview_renderer`) only executes that draw list. The display
clock is Onyx's too: song time = time at play plus elapsed wall-clock time;
the audio is seeked to it on play and follows (superseding that part of ADR
0004). Hydra's own additions — the active SP window as a floor tint, every
fill as Onyx's BRE lane strips, the activation lane lit on top — use Onyx's
materials and draw in Onyx's order, so they look native.

"Faithful" is checked, not asserted: `tests/test_preview_golden.cpp` renders
the chart in `testdata/preview/` at the moment captured in `golden_onyx.png`
(a paused Onyx window, cropped by `golden.json`) and requires the mean
per-channel difference to stay under the fixture's tolerance; at the time of
writing it is 5.7/255, almost all of it the capture's gamma. The trade-off is
that the repo now carries Onyx's GPL-3 art and a line-for-line shader port, so
Hydra is a private, non-distributable build unless relicensed. Keep the assets
verbatim and the numbers in `3d-config.json`; when the look must change, change
Onyx's inputs (config, assets), not the draw code, and re-capture the golden.
