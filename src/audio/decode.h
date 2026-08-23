// Audio decode layer for the Preview engine.
//
// One entry point, decode_audio, turns a chart stem's compressed bytes into
// interleaved float PCM. It dispatches by the container's own magic, not by a
// file extension, because container stems (.sng/.srb) carry bytes with no name:
//   * OggS + a Vorbis identification header -> stb_vorbis
//   * OggS + an OpusHead header             -> libopus over a libogg demux
//   * RIFF/WAVE, fLaC, ID3 or an MP3 sync   -> miniaudio's own decoders
// Two OggS codecs share the container magic, so sniff_format looks past "OggS"
// for the codec tag; that discrimination is the whole reason this is a distinct,
// tested step. This layer never opens an output device — it is pure and fully
// testable off a real audio device (see the player for the device side).

#ifndef HYDRA_AUDIO_DECODE_H
#define HYDRA_AUDIO_DECODE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hydra::app {
struct PreviewAudioStem;
}

namespace hydra::audio {

// PCM decoded from one stem: interleaved float samples in [-1, 1], with the
// channel count and sample rate they were decoded at. frames() is the count of
// per-channel sample frames.
struct DecodedAudio {
    std::vector<float> samples;  // interleaved by channel
    int channels = 0;
    int sample_rate = 0;

    int64_t frames() const {
        return channels > 0 ? static_cast<int64_t>(samples.size()) / channels : 0;
    }
    bool empty() const { return samples.empty(); }
};

// The container Hydra recognizes for a stem, decided from its leading bytes.
enum class AudioFormat { Unknown, Wav, Mp3, Flac, OggVorbis, OggOpus };

// Classify audio bytes by content. Distinguishes the two OggS codecs by the
// codec tag in the first page. Returns Unknown for anything unrecognized or too
// short to tell.
AudioFormat sniff_format(const uint8_t* data, std::size_t size);
inline AudioFormat sniff_format(const std::vector<uint8_t>& bytes) {
    return sniff_format(bytes.data(), bytes.size());
}

// Decode compressed audio bytes to float PCM. Throws std::runtime_error on an
// unrecognized container or a decode failure.
DecodedAudio decode_audio(const uint8_t* data, std::size_t size);
inline DecodedAudio decode_audio(const std::vector<uint8_t>& bytes) {
    return decode_audio(bytes.data(), bytes.size());
}

// Decode a Preview stem from whichever source it holds: its file on disk
// (from_file()) or its already-extracted container bytes.
DecodedAudio decode_stem(const app::PreviewAudioStem& stem);

}  // namespace hydra::audio

#endif  // HYDRA_AUDIO_DECODE_H
