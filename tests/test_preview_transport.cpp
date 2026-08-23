// Tests for ui/preview_transport: the Preview's Transport — play, pause and
// seek together with the master clock, and the audio playhead that follows it.
// The monotonic source is a fake, so the timing is exact; no device is opened,
// and read_frames() is exactly what a device callback would pull.

#include "doctest.h"

#include <memory>
#include <vector>

#include "audio/decode.h"
#include "audio/player.h"
#include "ui/preview_transport.h"

using hydra::audio::DecodedAudio;
using hydra::audio::Playhead;
using hydra::ui::PreviewTransport;

namespace {

// `frames` stereo frames at 48 kHz where frame i holds {L=i, R=i+0.5}, so a
// copied block is trivially recognizable (as in test_audio_player).
DecodedAudio make_ramp(int frames) {
    DecodedAudio a;
    a.channels = 2;
    a.sample_rate = 48000;
    a.samples.resize(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        a.samples[i * 2] = static_cast<float>(i);
        a.samples[i * 2 + 1] = static_cast<float>(i) + 0.5f;
    }
    return a;
}

// A playhead `ms` milliseconds long at 48 kHz stereo.
std::unique_ptr<Playhead> make_playhead(double ms) {
    return std::make_unique<Playhead>(make_ramp(static_cast<int>(ms * 48.0)));
}

}  // namespace

TEST_CASE("an unloaded transport plays and scrubs with no audio") {
    double t = 10.0;  // fake monotonic seconds
    PreviewTransport transport([&] { return t; });

    CHECK_FALSE(transport.has_audio());
    CHECK(transport.length_ms() == doctest::Approx(0.0));
    CHECK(transport.channels() == 0);
    CHECK(transport.sample_rate() == 0);

    transport.play();
    CHECK(transport.playing());
    t += 0.5;
    CHECK(transport.now_ms() == doctest::Approx(500.0));
    // With no length there is no end to stop at: tick() never pauses.
    CHECK(transport.tick() == doctest::Approx(500.0));
    CHECK(transport.playing());

    transport.seek_ms(20000.0);  // scrubbing past a zero length is allowed
    CHECK(transport.now_ms() == doctest::Approx(20000.0));
}

TEST_CASE("load takes the later of last note and audio end as the length") {
    PreviewTransport transport([] { return 0.0; });

    transport.load(make_playhead(1000.0), 1500.0);
    CHECK(transport.length_ms() == doctest::Approx(1500.0));  // notes run longer
    CHECK(transport.has_audio());
    CHECK(transport.channels() == 2);
    CHECK(transport.sample_rate() == 48000);

    transport.load(make_playhead(1000.0), 200.0);
    CHECK(transport.length_ms() == doctest::Approx(1000.0));  // audio runs longer

    transport.unload();
    CHECK(transport.length_ms() == doctest::Approx(0.0));
    CHECK_FALSE(transport.has_audio());
    CHECK(transport.now_ms() == doctest::Approx(0.0));
}

TEST_CASE("play seeks the playhead to the clock and both run") {
    double t = 0.0;
    PreviewTransport transport([&] { return t; });

    auto* playhead = new Playhead(make_ramp(48000));  // 1000 ms
    transport.load(std::unique_ptr<Playhead>(playhead), 0.0);
    CHECK_FALSE(playhead->playing());

    transport.seek_ms(250.0);
    CHECK(playhead->position_ms() == doctest::Approx(250.0));

    transport.play();
    CHECK(transport.playing());
    CHECK(playhead->playing());
    CHECK(playhead->position_ms() == doctest::Approx(250.0));

    transport.pause();
    CHECK_FALSE(transport.playing());
    CHECK_FALSE(playhead->playing());

    transport.toggle();
    CHECK(transport.playing());
    CHECK(playhead->playing());
    transport.toggle();
    CHECK_FALSE(transport.playing());
    CHECK_FALSE(playhead->playing());
}

TEST_CASE("tick pauses at the end and pins the time") {
    double t = 0.0;
    PreviewTransport transport([&] { return t; });
    transport.load(make_playhead(1000.0), 0.0);
    CHECK(transport.length_ms() == doctest::Approx(1000.0));

    transport.play();
    t += 0.4;
    CHECK(transport.tick() == doctest::Approx(400.0));  // still short of the end
    CHECK(transport.playing());

    t += 1.1;  // 1500 ms in, past the end
    CHECK(transport.tick() == doctest::Approx(1000.0));
    CHECK_FALSE(transport.playing());
    CHECK(transport.now_ms() == doctest::Approx(1000.0));

    t += 5.0;  // paused: the time stays pinned
    CHECK(transport.tick() == doctest::Approx(1000.0));
}

TEST_CASE("seek clamps to [0, length]") {
    PreviewTransport transport([] { return 0.0; });
    transport.load(make_playhead(1000.0), 0.0);

    transport.seek_ms(-5.0);
    CHECK(transport.now_ms() == doctest::Approx(0.0));
    transport.seek_ms(400.0);
    CHECK(transport.now_ms() == doctest::Approx(400.0));
    transport.seek_ms(9999.0);
    CHECK(transport.now_ms() == doctest::Approx(transport.length_ms()));
}

TEST_CASE("read_frames serves audio while playing and silence while paused") {
    PreviewTransport transport([] { return 0.0; });
    transport.load(std::make_unique<Playhead>(make_ramp(5)), 0.0);

    std::vector<float> out(8, -1.0f);  // 4 stereo frames, sentinel-filled
    CHECK(transport.read_frames(out.data(), 4) == 0);  // paused: silence
    for (float v : out) CHECK(v == 0.0f);

    transport.play();
    CHECK(transport.read_frames(out.data(), 3) == 3);
    const float expect[] = {0, 0.5f, 1, 1.5f, 2, 2.5f};
    for (int i = 0; i < 6; ++i) CHECK(out[i] == doctest::Approx(expect[i]));

    // Two frames left: the tail is zero-filled and only the real frames count.
    CHECK(transport.read_frames(out.data(), 4) == 2);
    CHECK(out[0] == doctest::Approx(3.0f));
    CHECK(out[4] == doctest::Approx(0.0f));

    // With no playhead at all the pull is a no-op, not a crash.
    transport.unload();
    CHECK(transport.read_frames(out.data(), 4) == 0);
}

TEST_CASE("gain set before load applies to the next playhead") {
    PreviewTransport transport([] { return 0.0; });
    CHECK(transport.gain() == doctest::Approx(1.0f));

    transport.set_gain(0.5f);
    CHECK(transport.gain() == doctest::Approx(0.5f));

    auto* playhead = new Playhead(make_ramp(100));
    transport.load(std::unique_ptr<Playhead>(playhead), 0.0);
    CHECK(playhead->gain() == doctest::Approx(0.5f));

    transport.play();
    std::vector<float> out(8, -1.0f);
    CHECK(transport.read_frames(out.data(), 4) == 4);
    CHECK(out[2] == doctest::Approx(1.0f * 0.5f));  // frame 1, L
    CHECK(out[3] == doctest::Approx(1.5f * 0.5f));  // frame 1, R

    // A later change reaches the loaded playhead too.
    transport.set_gain(0.25f);
    CHECK(playhead->gain() == doctest::Approx(0.25f));
}
