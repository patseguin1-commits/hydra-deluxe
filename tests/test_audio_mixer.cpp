// Tests for audio/mixer: converting stems to a common format and summing them.
// Summation is checked exactly with hand-built PCM; the resample and channel
// conversion is checked on a synthesized tone whose frequency must survive.

#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "app/preview_source.h"
#include "audio/decode.h"
#include "audio/mixer.h"
#include "core/winstr.h"

using namespace hydra::audio;

namespace {

#ifndef HYDRA_TESTDATA_DIR
#error "HYDRA_TESTDATA_DIR must be defined (see CMakeLists.txt)"
#endif

std::vector<uint8_t> read_fixture(const std::string& name) {
    return hydra::read_file_bytes(std::string(HYDRA_TESTDATA_DIR) + "/audio/" +
                                  name);
}

DecodedAudio make_pcm(std::vector<float> samples, int channels, int rate) {
    DecodedAudio a;
    a.samples = std::move(samples);
    a.channels = channels;
    a.sample_rate = rate;
    return a;
}

// A mono sine of `freq` Hz, `seconds` long, sampled at `rate`.
DecodedAudio synth_tone(double freq, int rate, double seconds) {
    const double pi = 3.14159265358979323846;
    int n = static_cast<int>(rate * seconds);
    DecodedAudio a;
    a.channels = 1;
    a.sample_rate = rate;
    a.samples.resize(n);
    for (int i = 0; i < n; ++i)
        a.samples[i] = static_cast<float>(0.5 * std::sin(2 * pi * freq * i / rate));
    return a;
}

// Dominant frequency of one channel via zero crossings over the middle half.
double estimate_freq_hz(const DecodedAudio& a, int channel) {
    if (a.channels <= 0 || a.frames() < 4) return 0.0;
    int64_t n = a.frames(), lo = n / 4, hi = n - n / 4;
    int crossings = 0;
    float prev = a.samples[static_cast<size_t>(lo) * a.channels + channel];
    for (int64_t i = lo + 1; i < hi; ++i) {
        float s = a.samples[static_cast<size_t>(i) * a.channels + channel];
        if ((prev < 0.0f && s >= 0.0f) || (prev >= 0.0f && s < 0.0f)) ++crossings;
        prev = s;
    }
    double dur = static_cast<double>(hi - lo) / a.sample_rate;
    return (crossings / 2.0) / dur;
}

// Today's mixer, rebuilt from the public API: convert every stem on its own
// (a one-stem mix is 0.0f plus the converted stem), keep every converted copy,
// then sum them in stem order into a zeroed buffer as long as the longest.
// The one-at-a-time mixer must match it bit for bit.
DecodedAudio reference_mix(const std::vector<DecodedAudio>& stems, int rate,
                           int channels) {
    std::vector<std::vector<float>> converted;
    std::size_t longest = 0;
    for (const DecodedAudio& s : stems) {
        converted.push_back(mix_stems({s}, rate, channels).samples);
        longest = std::max(longest, converted.back().size());
    }
    DecodedAudio out;
    out.sample_rate = rate;
    out.channels = channels;
    out.samples.assign(longest, 0.0f);
    for (const std::vector<float>& c : converted)
        for (std::size_t i = 0; i < c.size(); ++i) out.samples[i] += c[i];
    return out;
}

// Same format and the same float bits, sample for sample.
bool same_bits(const DecodedAudio& a, const DecodedAudio& b) {
    if (a.sample_rate != b.sample_rate || a.channels != b.channels) return false;
    if (a.samples.size() != b.samples.size()) return false;
    return a.samples.empty() ||
           std::memcmp(a.samples.data(), b.samples.data(),
                       a.samples.size() * sizeof(float)) == 0;
}

}  // namespace

TEST_CASE("mix_stems sums same-format stems and zero-extends the shorter") {
    DecodedAudio s1 = make_pcm({0.10f, 0.20f, 0.30f}, 1, 48000);
    DecodedAudio s2 = make_pcm({0.01f, 0.02f}, 1, 48000);

    DecodedAudio out = mix_stems({s1, s2}, 48000, 1);

    CHECK(out.channels == 1);
    CHECK(out.sample_rate == 48000);
    REQUIRE(out.frames() == 3);  // longest stem
    CHECK(out.samples[0] == doctest::Approx(0.11f));
    CHECK(out.samples[1] == doctest::Approx(0.22f));
    CHECK(out.samples[2] == doctest::Approx(0.30f));  // s2 silent past its end
}

TEST_CASE("mix_stems of no stems is empty at the requested format") {
    DecodedAudio out = mix_stems({}, 44100, 2);
    CHECK(out.channels == 2);
    CHECK(out.sample_rate == 44100);
    CHECK(out.samples.empty());
}

TEST_CASE("mix_stems resamples to the output rate and unifies channels") {
    DecodedAudio mono24k = synth_tone(300.0, 24000, 1.0);  // 24000 frames, mono

    DecodedAudio out = mix_stems({mono24k}, 48000, 2);

    CHECK(out.channels == 2);
    CHECK(out.sample_rate == 48000);
    // Resampled 24k -> 48k over ~1 s lands near 48000 frames (resampler latency
    // trims a few).
    CHECK(out.frames() == doctest::Approx(48000).epsilon(0.02));
    CHECK(estimate_freq_hz(out, 0) == doctest::Approx(300.0).epsilon(0.05));
    // A mono stem upmixed to stereo puts the same signal in both channels.
    for (int64_t i = 100; i < out.frames() - 100; i += 977) {
        CHECK(out.samples[static_cast<size_t>(i) * 2] ==
              doctest::Approx(out.samples[static_cast<size_t>(i) * 2 + 1]));
    }
}

TEST_CASE("decode_and_mix decodes each stem, skips undecodable ones") {
    hydra::app::PreviewAudioStem ogg;  // a file-path stem
    ogg.label = "song";
    ogg.path = std::string(HYDRA_TESTDATA_DIR) + "/audio/sine220.ogg";

    hydra::app::PreviewAudioStem mp3;  // a container-bytes stem
    mp3.label = "drums";
    mp3.bytes = read_fixture("sine220.mp3");

    hydra::app::PreviewAudioStem junk;  // undecodable — must be skipped
    junk.label = "broken";
    junk.bytes = {'n', 'o', 't', ' ', 'a', 'u', 'd', 'i', 'o'};

    // The progress callback fires once per stem, even for one that is skipped,
    // so a loading bar can count stems without caring which ones decoded.
    std::vector<std::pair<int, int>> ticks;
    DecodedAudio out = decode_and_mix({ogg, mp3, junk}, 48000, 2,
                                      [&](int done, int total) { ticks.emplace_back(done, total); });
    CHECK(ticks == std::vector<std::pair<int, int>>{{0, 3}, {1, 3}, {2, 3}, {3, 3}});
    CHECK(out.channels == 2);
    CHECK(out.sample_rate == 48000);
    REQUIRE(out.frames() > 4800);  // both real 220 Hz stems mixed in
    CHECK(estimate_freq_hz(out, 0) == doctest::Approx(220.0).epsilon(0.07));

    // Every stem undecodable -> an empty mix, never a throw.
    DecodedAudio none = decode_and_mix({junk}, 48000, 2);
    CHECK(none.samples.empty());
    CHECK(none.channels == 2);
}

TEST_CASE("mix_stems matches the convert-all-then-sum mix bit for bit") {
    // Mixed rates, channel counts and lengths, so resampling, upmixing and
    // the grow-with-silence path all run. The middle stem is the longest.
    DecodedAudio a = synth_tone(300.0, 24000, 0.5);   // mono 24 kHz, short
    DecodedAudio b = synth_tone(440.0, 44100, 1.2);   // mono 44.1 kHz, longest
    DecodedAudio c = make_pcm(std::vector<float>(48000 * 2, 0.25f), 2, 48000);
    const std::vector<DecodedAudio> stems = {a, b, c};

    CHECK(same_bits(mix_stems(stems, 48000, 2), reference_mix(stems, 48000, 2)));
    CHECK(same_bits(mix_stems(stems, 44100, 1), reference_mix(stems, 44100, 1)));
}

TEST_CASE("decode_and_mix matches decoding every stem then mixing") {
    hydra::app::PreviewAudioStem ogg;
    ogg.label = "song";
    ogg.path = std::string(HYDRA_TESTDATA_DIR) + "/audio/sine220.ogg";
    hydra::app::PreviewAudioStem mp3;
    mp3.label = "drums";
    mp3.bytes = read_fixture("sine220.mp3");
    hydra::app::PreviewAudioStem opus;
    opus.label = "guitar";
    opus.bytes = read_fixture("sine220.opus");
    hydra::app::PreviewAudioStem junk;
    junk.label = "broken";
    junk.bytes = {'n', 'o', 't', ' ', 'a', 'u', 'd', 'i', 'o'};
    const std::vector<hydra::app::PreviewAudioStem> stems = {ogg, junk, mp3, opus};

    std::vector<DecodedAudio> decoded;
    for (const hydra::app::PreviewAudioStem& s : stems) {
        try {
            decoded.push_back(decode_stem(s));
        } catch (const std::exception&) {
        }
    }
    REQUIRE(decoded.size() == 3);

    CHECK(same_bits(decode_and_mix(stems, 48000, 2), mix_stems(decoded, 48000, 2)));
}