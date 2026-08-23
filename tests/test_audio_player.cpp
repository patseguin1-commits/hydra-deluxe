// Tests for audio/player: the Playhead that serves frames to the device
// callback. No device is opened — read_frames() is exactly what the callback
// calls, so the playhead is exercised directly with known PCM.

#include "doctest.h"

#include <vector>

#include "audio/decode.h"
#include "audio/player.h"

using namespace hydra::audio;

namespace {

// `frames` stereo frames at 48 kHz where frame i holds {L=i, R=i+0.5}, so a
// copied block is trivially recognizable.
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

}  // namespace

TEST_CASE("Playhead starts paused at the start with the mix's format") {
    Playhead t(make_ramp(4800));
    CHECK_FALSE(t.playing());
    CHECK(t.position_frames() == 0);
    CHECK(t.length_frames() == 4800);
    CHECK(t.channels() == 2);
    CHECK(t.sample_rate() == 48000);
    CHECK(t.length_ms() == doctest::Approx(100.0));  // 4800 / 48000 s
    CHECK(t.position_ms() == doctest::Approx(0.0));
}

TEST_CASE("play, pause, and toggle drive the playhead state") {
    Playhead t(make_ramp(10));
    t.play();
    CHECK(t.playing());
    t.pause();
    CHECK_FALSE(t.playing());
    t.toggle();
    CHECK(t.playing());
    t.toggle();
    CHECK_FALSE(t.playing());
}

TEST_CASE("read_frames while paused writes silence and does not advance") {
    Playhead t(make_ramp(10));
    std::vector<float> out(8, -1.0f);  // 4 stereo frames, sentinel-filled
    int64_t got = t.read_frames(out.data(), 4);
    CHECK(got == 0);
    CHECK(t.position_frames() == 0);
    for (float v : out) CHECK(v == 0.0f);
}

TEST_CASE("read_frames while playing copies frames and advances the clock") {
    Playhead t(make_ramp(5));
    t.play();

    std::vector<float> out(6, -1.0f);  // room for 3 frames
    int64_t got = t.read_frames(out.data(), 3);
    CHECK(got == 3);
    CHECK(t.position_frames() == 3);
    const float expect[] = {0, 0.5f, 1, 1.5f, 2, 2.5f};
    for (int i = 0; i < 6; ++i) CHECK(out[i] == doctest::Approx(expect[i]));
    CHECK(t.position_ms() == doctest::Approx(3.0 * 1000.0 / 48000.0));
}

TEST_CASE("read_frames past the end zero-fills, auto-pauses, clamps position") {
    Playhead t(make_ramp(5));
    t.play();
    t.seek_frames(3);  // two frames left

    std::vector<float> out(8, -1.0f);  // ask for 4 frames
    int64_t got = t.read_frames(out.data(), 4);
    CHECK(got == 2);  // only frames 3 and 4 are real audio
    // frames 3,4 then silence
    const float expect[] = {3, 3.5f, 4, 4.5f, 0, 0, 0, 0};
    for (int i = 0; i < 8; ++i) CHECK(out[i] == doctest::Approx(expect[i]));
    CHECK(t.position_frames() == 5);  // clamped to length
    CHECK_FALSE(t.playing());         // auto-paused at the end
}

TEST_CASE("seek clamps to the valid range in both frames and ms") {
    Playhead t(make_ramp(100));
    t.seek_frames(40);
    CHECK(t.position_frames() == 40);
    t.seek_frames(-10);
    CHECK(t.position_frames() == 0);
    t.seek_frames(999);
    CHECK(t.position_frames() == 100);  // clamped to length
    t.seek_ms(0.5);                     // 0.5 ms at 48 kHz = 24 frames
    CHECK(t.position_frames() == 24);
}

TEST_CASE("Playhead applies the output gain to served frames") {
    Playhead t(make_ramp(100));
    CHECK(t.gain() == doctest::Approx(1.0f));
    t.set_gain(0.25f);
    CHECK(t.gain() == doctest::Approx(0.25f));
    t.play();
    std::vector<float> out(8 * 2);
    CHECK(t.read_frames(out.data(), 8) == 8);
    CHECK(out[2] == doctest::Approx(1.0f * 0.25f));   // frame 1, L
    CHECK(out[3] == doctest::Approx(1.5f * 0.25f));   // frame 1, R
    CHECK(out[14] == doctest::Approx(7.0f * 0.25f));  // frame 7, L
    t.set_gain(-1.0f);  // clamps to silence, never inverts
    CHECK(t.gain() == doctest::Approx(0.0f));
    CHECK(t.read_frames(out.data(), 4) == 4);
    CHECK(out[0] == doctest::Approx(0.0f));
    CHECK(out[3] == doctest::Approx(0.0f));
}
