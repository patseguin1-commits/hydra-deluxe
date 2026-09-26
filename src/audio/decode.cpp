#include "audio/decode.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include <ogg/ogg.h>
#include <opus.h>

#include "app/preview_source.h"
#include "core/winstr.h"

// miniaudio's configuration macros come from the miniaudio target
// (CMakeLists.txt), the same set its implementation TU is compiled with.
#include "miniaudio.h"

// stb_vorbis is compiled as its own TU (third_party/stb/stb_vorbis.c); take only
// its prototypes here and link the implementation.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace hydra::audio {

namespace {

// data[off..] begins with the literal `lit` (a plain ASCII tag, no NUL).
bool has_tag(const uint8_t* data, std::size_t size, std::size_t off,
             const char* lit) {
    std::size_t n = std::strlen(lit);
    if (off + n > size) return false;
    return std::memcmp(data + off, lit, n) == 0;
}

// The literal `lit` appears anywhere in the first `limit` bytes. Used to read
// the codec tag out of an Ogg stream's first page without parsing the page.
bool contains_tag(const uint8_t* data, std::size_t size, std::size_t limit,
                  const char* lit) {
    std::size_t n = std::strlen(lit);
    std::size_t end = size < limit ? size : limit;
    if (n == 0 || end < n) return false;
    for (std::size_t i = 0; i + n <= end; ++i)
        if (std::memcmp(data + i, lit, n) == 0) return true;
    return false;
}

}  // namespace

AudioFormat sniff_format(const uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 2) return AudioFormat::Unknown;

    if (has_tag(data, size, 0, "RIFF") && has_tag(data, size, 8, "WAVE"))
        return AudioFormat::Wav;
    if (has_tag(data, size, 0, "fLaC")) return AudioFormat::Flac;

    if (has_tag(data, size, 0, "OggS")) {
        // Two codecs share the OggS container; the first page names which.
        if (contains_tag(data, size, 64, "OpusHead")) return AudioFormat::OggOpus;
        if (contains_tag(data, size, 64, "vorbis")) return AudioFormat::OggVorbis;
        return AudioFormat::Unknown;
    }

    // MP3: an ID3v2 tag, or a raw frame sync (11 set bits: FF Ex/Fx).
    if (has_tag(data, size, 0, "ID3")) return AudioFormat::Mp3;
    if (data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) return AudioFormat::Mp3;

    return AudioFormat::Unknown;
}

namespace {

// Decode WAV/MP3/FLAC from memory with miniaudio's own decoders, to native
// channel count and sample rate as interleaved float.
DecodedAudio decode_with_miniaudio(const uint8_t* data, std::size_t size) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder dec;
    if (ma_decoder_init_memory(data, size, &cfg, &dec) != MA_SUCCESS)
        throw std::runtime_error("decode_audio: miniaudio could not open the stream");

    DecodedAudio out;
    out.channels = static_cast<int>(dec.outputChannels);
    out.sample_rate = static_cast<int>(dec.outputSampleRate);

    const ma_uint64 kChunkFrames = 4096;
    for (;;) {
        std::size_t base = out.samples.size();
        out.samples.resize(base + static_cast<std::size_t>(kChunkFrames) *
                                      out.channels);
        ma_uint64 read = 0;
        ma_result r = ma_decoder_read_pcm_frames(&dec, out.samples.data() + base,
                                                 kChunkFrames, &read);
        out.samples.resize(base +
                           static_cast<std::size_t>(read) * out.channels);
        if (read == 0) break;
        if (r != MA_SUCCESS && r != MA_AT_END) {
            ma_decoder_uninit(&dec);
            throw std::runtime_error("decode_audio: miniaudio read failed");
        }
        if (r == MA_AT_END) break;
    }
    ma_decoder_uninit(&dec);
    return out;
}

// Decode an Ogg-Vorbis stream from memory with stb_vorbis to interleaved float.
DecodedAudio decode_ogg_vorbis(const uint8_t* data, std::size_t size) {
    int channels = 0, rate = 0;
    short* pcm = nullptr;
    int frames = stb_vorbis_decode_memory(data, static_cast<int>(size),
                                          &channels, &rate, &pcm);
    if (frames < 0 || pcm == nullptr)
        throw std::runtime_error("decode_audio: stb_vorbis could not decode the stream");

    DecodedAudio out;
    out.channels = channels;
    out.sample_rate = rate;
    out.samples.resize(static_cast<std::size_t>(frames) * channels);
    for (std::size_t i = 0; i < out.samples.size(); ++i)
        out.samples[i] = pcm[i] / 32768.0f;
    std::free(pcm);
    return out;
}

// Decode an Ogg-Opus stream from memory. opusfile is not vendored, so this walks
// the Ogg pages with libogg and decodes the packets with libopus by hand:
//   * the first packet is the OpusHead identification header — read the channel
//     count and the 16-bit little-endian pre-skip (encoder delay at 48 kHz);
//   * the second packet is OpusTags — skipped;
//   * the rest are audio packets, decoded to 48 kHz float and concatenated, with
//     the pre-skip samples dropped from the very start.
// Opus always decodes at 48 kHz regardless of the source rate. Only mapping
// family 0 (mono/stereo) is handled; that covers the Clone Hero corpus.
DecodedAudio decode_ogg_opus(const uint8_t* data, std::size_t size) {
    ogg_sync_state oy;
    ogg_sync_init(&oy);

    ogg_stream_state os;
    bool stream_ready = false;
    OpusDecoder* dec = nullptr;
    DecodedAudio out;
    out.sample_rate = 48000;

    auto cleanup = [&] {
        if (dec) opus_decoder_destroy(dec);
        if (stream_ready) ogg_stream_clear(&os);
        ogg_sync_clear(&oy);
    };

    try {
        char* buf = ogg_sync_buffer(&oy, static_cast<long>(size));
        std::memcpy(buf, data, size);
        ogg_sync_wrote(&oy, static_cast<long>(size));

        int channels = 0;
        long skip_remaining = 0;
        long packet_index = 0;
        const int kMaxFrame = 5760;  // 120 ms at 48 kHz, the largest Opus packet

        ogg_page og;
        while (ogg_sync_pageout(&oy, &og) == 1) {
            if (!stream_ready) {
                ogg_stream_init(&os, ogg_page_serialno(&og));
                stream_ready = true;
            }
            ogg_stream_pagein(&os, &og);

            ogg_packet op;
            while (ogg_stream_packetout(&os, &op) == 1) {
                if (packet_index == 0) {
                    if (op.bytes < 19 ||
                        std::memcmp(op.packet, "OpusHead", 8) != 0)
                        throw std::runtime_error(
                            "decode_audio: Opus stream has no OpusHead");
                    channels = op.packet[9];
                    skip_remaining =
                        op.packet[10] | (static_cast<int>(op.packet[11]) << 8);
                    if (channels < 1 || channels > 2)
                        throw std::runtime_error(
                            "decode_audio: only mono/stereo Opus is supported");
                    int err = 0;
                    dec = opus_decoder_create(48000, channels, &err);
                    if (err != OPUS_OK || dec == nullptr)
                        throw std::runtime_error(
                            "decode_audio: opus_decoder_create failed");
                    out.channels = channels;
                } else if (packet_index == 1) {
                    // OpusTags comment header — nothing to decode.
                } else {
                    std::vector<float> tmp(static_cast<std::size_t>(kMaxFrame) *
                                           channels);
                    int n = opus_decode_float(dec, op.packet,
                                              static_cast<opus_int32>(op.bytes),
                                              tmp.data(), kMaxFrame, 0);
                    if (n < 0)
                        throw std::runtime_error("decode_audio: opus_decode failed");
                    int start = 0;
                    if (skip_remaining > 0) {
                        int drop = static_cast<int>(
                            std::min<long>(skip_remaining, n));
                        start = drop;
                        skip_remaining -= drop;
                    }
                    out.samples.insert(
                        out.samples.end(),
                        tmp.begin() + static_cast<std::size_t>(start) * channels,
                        tmp.begin() + static_cast<std::size_t>(n) * channels);
                }
                ++packet_index;
            }
        }

        if (dec == nullptr || out.samples.empty())
            throw std::runtime_error("decode_audio: no Opus audio decoded");
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
    return out;
}

}  // namespace

DecodedAudio decode_audio(const uint8_t* data, std::size_t size) {
    switch (sniff_format(data, size)) {
        case AudioFormat::Wav:
        case AudioFormat::Mp3:
        case AudioFormat::Flac:
            return decode_with_miniaudio(data, size);
        case AudioFormat::OggVorbis:
            return decode_ogg_vorbis(data, size);
        case AudioFormat::OggOpus:
            return decode_ogg_opus(data, size);
        default:
            throw std::runtime_error(
                "decode_audio: unrecognized audio container");
    }
}

DecodedAudio decode_stem(const app::PreviewAudioStem& stem) {
    if (stem.from_file()) {
        std::vector<uint8_t> bytes = hydra::read_file_bytes(stem.path);
        return decode_audio(bytes);
    }
    return decode_audio(stem.bytes);
}

}  // namespace hydra::audio
