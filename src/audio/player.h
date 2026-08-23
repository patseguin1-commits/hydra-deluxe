// The transport core of the Preview player.
//
// It owns one mixed PCM buffer (see audio/mixer.h) and the playback position,
// and serves frames to an output callback through read_frames(). The Preview's
// master clock is the display clock (app/preview_clock.h, as in Onyx — see
// docs/adr/0008); the controller seeks this transport to that clock on play,
// and every frame served advances the audio position from there. This type
// deliberately holds no output device — it is pure and fully unit-tested. The
// GUI wraps a miniaudio ma_device around a PreviewTransport and calls
// read_frames() from the device callback; opening the device needs real
// hardware and stays out of the tests.

#ifndef HYDRA_AUDIO_PLAYER_H
#define HYDRA_AUDIO_PLAYER_H

#include <cstdint>

#include "audio/decode.h"

namespace hydra::audio {

class PreviewTransport {
public:
    explicit PreviewTransport(DecodedAudio mixed);

    // Transport state. A new transport is paused at the start.
    void play();
    void pause();
    void toggle();
    bool playing() const { return playing_; }

    // Move the playhead. Positions are clamped to [0, length].
    void seek_frames(int64_t frame);
    void seek_ms(double ms);

    int64_t position_frames() const { return position_; }
    double position_ms() const;
    int64_t length_frames() const { return length_; }
    double length_ms() const;
    int channels() const { return channels_; }
    int sample_rate() const { return sample_rate_; }

    // Output gain applied to every frame served (1.0 = the mix as decoded).
    // A summed multi-stem mix is loud, so the GUI defaults well below 1.
    void set_gain(float gain) { gain_ = gain < 0.0f ? 0.0f : gain; }
    float gain() const { return gain_; }

    // Fill `out` with `frame_count` interleaved frames for the device. While
    // playing, copies from the current position and advances the clock, then
    // zero-fills any frames past the end and auto-pauses there. While paused,
    // writes silence and does not advance. Returns the count of real audio
    // frames written (never counting the silence tail).
    int64_t read_frames(float* out, int64_t frame_count);

private:
    DecodedAudio audio_;
    int channels_;
    int sample_rate_;
    int64_t length_;
    int64_t position_ = 0;
    bool playing_ = false;
    float gain_ = 1.0f;
};

}  // namespace hydra::audio

#endif  // HYDRA_AUDIO_PLAYER_H
