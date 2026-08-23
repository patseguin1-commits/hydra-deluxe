# Preview audio uses miniaudio plus libopus, not Windows Media Foundation

_Superseded in part by ADR 0008: the display clock is now the master and the
audio follows it; the decoder and mixer decisions below still stand._

The Preview plays a chart's audio in sync with the highway, and the Clone Hero
corpus is dominated by `.ogg` (Vorbis) and `.opus`, usually split into several
stems. Windows Media Foundation — the obvious built-in choice — cannot decode
Vorbis or Opus, so it does not fit the actual file mix. We instead vendor
**miniaudio** (public domain) for the output device, mixing, and resampling,
with **stb_vorbis** for OGG and **libopus + libogg** for Opus; miniaudio's
built-in decoders cover WAV/MP3/FLAC. A stem mixer decodes every stem in a chart
and sums them to one output, because many charts ship stems with no combined
file and would otherwise be silent. Audio playback position is the master clock
that the visuals follow. This adds an audio subsystem to an app that had none;
the trade-off is carrying libopus/libogg (BSD) through the MSVC build in
exchange for full coverage of the real corpus. Do not swap in Media Foundation
"to drop a dependency" — it silently loses OGG and Opus, which is most of the
library.
