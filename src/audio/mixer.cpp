#include "audio/mixer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "app/preview_source.h"

// Match the macros the miniaudio implementation TU was compiled with.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

namespace hydra::audio {

namespace {

// Convert one decoded stem to `out_channels` at `out_rate` with miniaudio's
// resampler and channel mapper, returning interleaved float. A stem already in
// the output format passes through unchanged.
// Takes the stem by value so a caller that owns it can move it in; a stem
// already in the output format then hands over its samples without a copy.
std::vector<float> convert_stem(DecodedAudio s, int out_rate,
                                int out_channels) {
    if (s.channels <= 0 || s.samples.empty()) return {};
    if (s.sample_rate == out_rate && s.channels == out_channels)
        return std::move(s.samples);

    ma_data_converter_config cfg = ma_data_converter_config_init(
        ma_format_f32, ma_format_f32, static_cast<ma_uint32>(s.channels),
        static_cast<ma_uint32>(out_channels),
        static_cast<ma_uint32>(s.sample_rate),
        static_cast<ma_uint32>(out_rate));

    ma_data_converter conv;
    if (ma_data_converter_init(&cfg, nullptr, &conv) != MA_SUCCESS)
        throw std::runtime_error("mix_stems: data converter init failed");

    ma_uint64 in_frames = static_cast<ma_uint64>(s.frames());
    ma_uint64 out_frames = 0;
    ma_data_converter_get_expected_output_frame_count(&conv, in_frames,
                                                      &out_frames);

    std::vector<float> converted(static_cast<std::size_t>(out_frames) *
                                 out_channels);
    ma_uint64 in_consumed = in_frames;
    ma_uint64 out_produced = out_frames;
    ma_result r = ma_data_converter_process_pcm_frames(
        &conv, s.samples.data(), &in_consumed, converted.data(), &out_produced);
    ma_data_converter_uninit(&conv, nullptr);
    if (r != MA_SUCCESS)
        throw std::runtime_error("mix_stems: data converter process failed");

    converted.resize(static_cast<std::size_t>(out_produced) * out_channels);
    return converted;
}

// Add one converted stem into the running mix, growing the mix with silence
// when this stem is longer. Every sample still starts at 0.0f and adds the
// stems in order, the same order as converting them all first, so the sum is
// bit-identical to the old mixer.
void add_into(std::vector<float>& mix, const std::vector<float>& stem) {
    if (stem.size() > mix.size()) mix.resize(stem.size(), 0.0f);
    for (std::size_t i = 0; i < stem.size(); ++i) mix[i] += stem[i];
}

}  // namespace

DecodedAudio mix_stems(const std::vector<DecodedAudio>& stems, int out_rate,
                       int out_channels) {
    DecodedAudio out;
    out.sample_rate = out_rate;
    out.channels = out_channels;
    for (const DecodedAudio& s : stems)
        add_into(out.samples, convert_stem(s, out_rate, out_channels));
    return out;
}

DecodedAudio decode_and_mix(const std::vector<app::PreviewAudioStem>& stems,
                            int out_rate, int out_channels,
                            const DecodeProgress& on_progress) {
    DecodedAudio out;
    out.sample_rate = out_rate;
    out.channels = out_channels;
    const int total = static_cast<int>(stems.size());
    if (on_progress) on_progress(0, total);
    int done = 0;
    for (const app::PreviewAudioStem& s : stems) {
        // Decode, convert and add this stem, then let both copies go before
        // the next one decodes: at most one stem is ever held beside the mix.
        DecodedAudio decoded;
        bool ok = false;
        try {
            decoded = decode_stem(s);
            ok = true;
        } catch (const std::exception&) {
            // Skip a stem we cannot decode; the rest of the chart still plays.
        }
        if (ok) add_into(out.samples, convert_stem(std::move(decoded), out_rate, out_channels));
        if (on_progress) on_progress(++done, total);
    }
    return out;
}

void pad_front_ms(DecodedAudio& audio, double ms) {
    if (ms <= 0.0 || audio.channels <= 0 || audio.sample_rate <= 0) return;
    const auto frames = static_cast<std::size_t>(std::llround(ms * audio.sample_rate / 1000.0));
    audio.samples.insert(audio.samples.begin(), frames * static_cast<std::size_t>(audio.channels), 0.0f);
}

}  // namespace hydra::audio
