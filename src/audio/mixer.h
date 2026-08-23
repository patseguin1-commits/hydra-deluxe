// Stem mixer for the Preview engine.
//
// A chart often ships several stems (song, drums, guitar, ...) and no combined
// file, so the Preview must sum them into one signal to play. Stems decode at
// their own sample rates and channel counts (see audio/decode.h), so the mixer
// converts each to a common output format, then adds them sample for sample.
// The mixed length is the longest stem's; shorter stems contribute silence past
// their end. Summing can push peaks past [-1, 1]; clamping is the player's job,
// not the mixer's, so the raw sum is preserved here for testability.

#ifndef HYDRA_AUDIO_MIXER_H
#define HYDRA_AUDIO_MIXER_H

#include <functional>
#include <vector>

#include "audio/decode.h"

namespace hydra::audio {

// Mix `stems` into one interleaved buffer at `out_rate` Hz and `out_channels`
// channels. Empty input yields an empty result at the requested format.
DecodedAudio mix_stems(const std::vector<DecodedAudio>& stems, int out_rate,
                       int out_channels);

// Decode every Preview stem and mix the results to one buffer at the given
// format. A stem that fails to decode is skipped, so one unreadable or corrupt
// stem never silences the rest of the chart. This is the bridge from a resolved
// PreviewSource's stems (audio/../app/preview_source.h) to a playable buffer.
//
// `on_progress(done, total)` fires once before the first stem (done=0) and
// once after each stem, skipped or not, so a caller can drive a loading bar
// by stem count. Decoding is the long pole of a Preview load, and one stem is
// the finest unit the decoders report at.
using DecodeProgress = std::function<void(int done, int total)>;
DecodedAudio decode_and_mix(const std::vector<app::PreviewAudioStem>& stems,
                            int out_rate, int out_channels,
                            const DecodeProgress& on_progress = nullptr);

}  // namespace hydra::audio

#endif  // HYDRA_AUDIO_MIXER_H
