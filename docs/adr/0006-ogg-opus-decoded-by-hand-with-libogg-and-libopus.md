# Ogg-Opus is decoded by hand with libogg and libopus, not opusfile

The Preview must decode `.opus` stems, and the usual library for this is
opusfile, which reads an Ogg-Opus file and hands back PCM. Hydra does not vendor
opusfile: its MSVC and CMake build is awkward, and it would add a third media
library on top of the libogg and libopus that ADR 0004 already vendors. So the
audio engine demuxes Ogg-Opus by hand (`src/audio/decode.cpp`,
`decode_ogg_opus`). It walks the Ogg pages with libogg, assembles the packets,
and decodes them with libopus directly: it reads the channel count and the
16-bit pre-skip from the OpusHead identification packet, skips the OpusTags
packet, decodes each audio packet to 48 kHz float, and drops the pre-skip
samples from the start. Opus always decodes at 48 kHz, so the sample rate is
fixed, not read from the source.

This keeps the dependency set to the two BSD libraries already present and lets
the same code decode a stem from either a file or in-memory container bytes. The
cost is that Hydra owns the container handling — page and packet assembly, the
OpusHead fields, and the pre-skip — that opusfile would hide, so this path is
covered by a real Ogg-Opus fixture in the tests (`testdata/audio/sine220.opus`).
Only mapping family 0 (mono and stereo) is handled, which covers the Clone Hero
corpus. Do not add opusfile "to simplify this": it re-introduces the build
problem the hand demux was written to avoid.
