#include "audio/mixer.h"

#include <algorithm>
#include <stdexcept>

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
std::vector<float> convert_stem(const DecodedAudio& s, int out_rate,
                                int out_channels) {
    if (s.channels <= 0 || s.samples.empty()) return {};
    if (s.sample_rate == out_rate && s.channels == out_channels)
        return s.samples;

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

}  // namespace

DecodedAudio mix_stems(const std::vector<DecodedAudio>& stems, int out_rate,
                       int out_channels) {
    DecodedAudio out;
    out.sample_rate = out_rate;
    out.channels = out_channels;

    std::vector<std::vector<float>> converted;
    converted.reserve(stems.size());
    std::size_t longest = 0;
    for (const DecodedAudio& s : stems) {
        converted.push_back(convert_stem(s, out_rate, out_channels));
        longest = std::max(longest, converted.back().size());
    }

    out.samples.assign(longest, 0.0f);
    for (const std::vector<float>& c : converted)
        for (std::size_t i = 0; i < c.size(); ++i) out.samples[i] += c[i];

    return out;
}

DecodedAudio decode_and_mix(const std::vector<app::PreviewAudioStem>& stems,
                            int out_rate, int out_channels,
                            const DecodeProgress& on_progress) {
    const int total = static_cast<int>(stems.size());
    if (on_progress) on_progress(0, total);
    std::vector<DecodedAudio> decoded;
    decoded.reserve(stems.size());
    int done = 0;
    for (const app::PreviewAudioStem& s : stems) {
        try {
            decoded.push_back(decode_stem(s));
        } catch (const std::exception&) {
            // Skip a stem we cannot decode; the rest of the chart still plays.
        }
        if (on_progress) on_progress(++done, total);
    }
    return mix_stems(decoded, out_rate, out_channels);
}

}  // namespace hydra::audio
