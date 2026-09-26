// Tests for audio/decode: classifying and decoding a chart stem's bytes to
// float PCM. The format sniff is exercised with both hand-built magic bytes and
// the real public-domain sine fixtures under testdata/audio (a 220 Hz tone
// encoded as OGG Vorbis, Ogg-Opus, and MP3), because the one hard case — telling
// Vorbis and Opus apart when both start with "OggS" — only shows up on real
// container headers.

#include "doctest.h"

#include <cstdint>
#include <string>
#include <vector>

#include "app/preview_source.h"
#include "audio/decode.h"
#include "core/winstr.h"

using namespace hydra;
using namespace hydra::audio;

namespace {

#ifndef HYDRA_TESTDATA_DIR
#error "HYDRA_TESTDATA_DIR must be defined (see CMakeLists.txt)"
#endif

std::vector<uint8_t> read_fixture(const std::string& name) {
    return hydra::read_file_bytes(std::string(HYDRA_TESTDATA_DIR) + "/audio/" +
                                  name);
}

std::vector<uint8_t> bytes(std::initializer_list<int> vals) {
    std::vector<uint8_t> out;
    out.reserve(vals.size());
    for (int v : vals) out.push_back(static_cast<uint8_t>(v));
    return out;
}

void put_u16(std::vector<uint8_t>& o, uint16_t n) {
    o.push_back(static_cast<uint8_t>(n));
    o.push_back(static_cast<uint8_t>(n >> 8));
}
void put_u32(std::vector<uint8_t>& o, uint32_t n) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(n >> (8 * i)));
}

// A canonical 44-byte-header PCM16 WAV around the given mono samples.
std::vector<uint8_t> make_wav_mono16(const std::vector<int16_t>& pcm,
                                     uint32_t sample_rate) {
    const uint16_t channels = 1, bits = 16;
    const uint32_t data_len = static_cast<uint32_t>(pcm.size()) * 2;
    const uint32_t byte_rate = sample_rate * channels * (bits / 8);
    std::vector<uint8_t> o;
    o.insert(o.end(), {'R', 'I', 'F', 'F'});
    put_u32(o, 36 + data_len);
    o.insert(o.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put_u32(o, 16);
    put_u16(o, 1);  // PCM
    put_u16(o, channels);
    put_u32(o, sample_rate);
    put_u32(o, byte_rate);
    put_u16(o, static_cast<uint16_t>(channels * (bits / 8)));  // block align
    put_u16(o, bits);
    o.insert(o.end(), {'d', 'a', 't', 'a'});
    put_u32(o, data_len);
    for (int16_t s : pcm) put_u16(o, static_cast<uint16_t>(s));
    return o;
}

// Dominant frequency of channel 0, from zero crossings over the middle half of
// the signal (edges carry encoder padding/fades). Good enough to confirm a
// decoded stem really is the 220 Hz sine, not silence or garbage.
double estimate_freq_hz(const DecodedAudio& a) {
    if (a.channels <= 0 || a.frames() < 4) return 0.0;
    int64_t n = a.frames();
    int64_t lo = n / 4, hi = n - n / 4;  // middle 50%
    int crossings = 0;
    float prev = a.samples[static_cast<size_t>(lo) * a.channels];
    for (int64_t i = lo + 1; i < hi; ++i) {
        float s = a.samples[static_cast<size_t>(i) * a.channels];
        if ((prev < 0.0f && s >= 0.0f) || (prev >= 0.0f && s < 0.0f)) ++crossings;
        prev = s;
    }
    double dur = static_cast<double>(hi - lo) / a.sample_rate;
    return (crossings / 2.0) / dur;
}

float peak_abs(const DecodedAudio& a) {
    float m = 0.0f;
    for (float s : a.samples) {
        float v = s < 0 ? -s : s;
        if (v > m) m = v;
    }
    return m;
}

}  // namespace

TEST_CASE("sniff_format classifies audio containers by their magic bytes") {
    // Hand-built magics for the simple containers.
    CHECK(sniff_format(bytes({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V',
                              'E'})) == AudioFormat::Wav);
    CHECK(sniff_format(bytes({'f', 'L', 'a', 'C'})) == AudioFormat::Flac);
    CHECK(sniff_format(bytes({'I', 'D', '3', 3, 0, 0})) == AudioFormat::Mp3);
    CHECK(sniff_format(bytes({0xFF, 0xFB, 0x90, 0x00})) ==
          AudioFormat::Mp3);  // raw MP3 frame sync

    // The real discrimination: both Ogg streams start "OggS"; only the codec tag
    // in the first page separates Vorbis from Opus.
    CHECK(sniff_format(read_fixture("sine220.ogg")) == AudioFormat::OggVorbis);
    CHECK(sniff_format(read_fixture("sine220.opus")) == AudioFormat::OggOpus);
    CHECK(sniff_format(read_fixture("sine220.mp3")) == AudioFormat::Mp3);

    // Too short or unrecognized -> Unknown, never a wrong guess.
    CHECK(sniff_format(std::vector<uint8_t>{}) == AudioFormat::Unknown);
    CHECK(sniff_format(bytes({0x89, 'P', 'N', 'G'})) == AudioFormat::Unknown);
}

TEST_CASE("decode_audio: PCM16 WAV decodes to matching float samples") {
    const uint32_t rate = 8000;
    const std::vector<int16_t> pcm = {0, 16384, -16384, 32767, -32768};
    DecodedAudio out = decode_audio(make_wav_mono16(pcm, rate));

    CHECK(out.channels == 1);
    CHECK(out.sample_rate == static_cast<int>(rate));
    REQUIRE(out.frames() == static_cast<int64_t>(pcm.size()));
    // int16 -> float within a sample's quantization; 32767 lands just under 1.0.
    const float expect[] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f};
    for (size_t i = 0; i < pcm.size(); ++i)
        CHECK(out.samples[i] == doctest::Approx(expect[i]).epsilon(0.001));
}

TEST_CASE("decode_audio: MP3 fixture decodes to the 220 Hz sine") {
    DecodedAudio out = decode_audio(read_fixture("sine220.mp3"));

    CHECK(out.channels >= 1);
    CHECK(out.sample_rate >= 8000);
    REQUIRE(out.frames() > out.sample_rate / 10);  // at least ~0.1 s
    CHECK(peak_abs(out) <= 1.0001f);
    CHECK(estimate_freq_hz(out) == doctest::Approx(220.0).epsilon(0.07));
}

TEST_CASE("decode_audio: OGG Vorbis fixture decodes to the 220 Hz sine") {
    DecodedAudio out = decode_audio(read_fixture("sine220.ogg"));

    CHECK(out.channels >= 1);
    CHECK(out.sample_rate >= 8000);
    REQUIRE(out.frames() > out.sample_rate / 10);
    CHECK(peak_abs(out) <= 1.0001f);
    CHECK(estimate_freq_hz(out) == doctest::Approx(220.0).epsilon(0.07));
}

TEST_CASE("decode_stem: a file-path stem and a bytes stem decode identically") {
    hydra::app::PreviewAudioStem file_stem;
    file_stem.label = "song";
    file_stem.path = std::string(HYDRA_TESTDATA_DIR) + "/audio/sine220.ogg";

    hydra::app::PreviewAudioStem mem_stem;
    mem_stem.label = "song";
    mem_stem.bytes = read_fixture("sine220.ogg");

    REQUIRE(file_stem.from_file());
    REQUIRE_FALSE(mem_stem.from_file());

    DecodedAudio a = decode_stem(file_stem);
    DecodedAudio b = decode_stem(mem_stem);

    CHECK(a.channels == b.channels);
    CHECK(a.sample_rate == b.sample_rate);
    REQUIRE(a.samples.size() == b.samples.size());
    CHECK(a.samples == b.samples);  // byte-for-byte the same PCM
    CHECK(estimate_freq_hz(a) == doctest::Approx(220.0).epsilon(0.07));
}

TEST_CASE("decode_audio: Ogg-Opus fixture decodes to the 220 Hz sine at 48 kHz") {
    DecodedAudio out = decode_audio(read_fixture("sine220.opus"));

    CHECK(out.channels >= 1);
    CHECK(out.sample_rate == 48000);  // Opus always decodes at 48 kHz
    REQUIRE(out.frames() > out.sample_rate / 10);
    CHECK(peak_abs(out) <= 1.0001f);
    CHECK(estimate_freq_hz(out) == doctest::Approx(220.0).epsilon(0.07));
}

// FNV-1a over the decoded float bytes. It pins the Opus decoder's exact output,
// so reusing one decode buffer cannot change a single sample. If libopus is
// ever upgraded, re-capture both numbers from a build of the old code first.
TEST_CASE("decode_audio: Ogg-Opus output is pinned bit for bit") {
    constexpr int64_t kPinnedFrames = 240648;                 // from the per-packet-buffer decoder
    constexpr uint64_t kPinnedHash = 10485106968528604345ull; // from the per-packet-buffer decoder

    const DecodedAudio a = decode_audio(read_fixture("sine220.opus"));
    uint64_t h = 1469598103934665603ull;
    const auto* p = reinterpret_cast<const uint8_t*>(a.samples.data());
    for (size_t i = 0; i < a.samples.size() * sizeof(float); ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    MESSAGE("opus fingerprint: frames=" << a.frames() << " hash=" << h);
    CHECK(a.frames() == kPinnedFrames);
    CHECK(h == kPinnedHash);
}