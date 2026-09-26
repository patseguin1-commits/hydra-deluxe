// The output-device half of the Preview player.
//
// The audio playhead (audio/player.h) is pure and device-free; this wraps a
// miniaudio playback device around a pull callback that supplies frames on
// miniaudio's audio thread. The device knows nothing about what it pulls from
// and holds no lock of its own: the lock lives in the Transport that supplies
// the source (src/ui/preview_transport.h), which takes it inside the callback.
// The device is torn down (stopped, so the callback has returned) before the
// source it calls may be destroyed, so destroy the device first.
//
// miniaudio's device API lives only here, kept private to hydra_audio like the
// decoders; the header stays free of miniaudio via a pImpl.

#ifndef HYDRA_AUDIO_DEVICE_H
#define HYDRA_AUDIO_DEVICE_H

#include <cstdint>
#include <functional>

namespace hydra::audio {

// Process-wide switch for harnesses with no sound card (the GUI test runner):
// when set, PreviewAudioDevice opens nothing and start() only tracks state,
// so Play/Pause stays testable without touching a real device.
void set_headless(bool headless);

class PreviewAudioDevice {
public:
    // Fills `out` with `frame_count` interleaved f32 frames and returns the
    // count of real audio frames written (the rest being silence).
    using Source = std::function<int64_t(float* out, int64_t frame_count)>;

    // Opens a playback device with `channels` channels at `sample_rate` (f32
    // samples) that pulls through `source`. Throws std::runtime_error if the
    // device won't open.
    PreviewAudioDevice(int channels, int sample_rate, Source source);
    ~PreviewAudioDevice();

    PreviewAudioDevice(const PreviewAudioDevice&) = delete;
    PreviewAudioDevice& operator=(const PreviewAudioDevice&) = delete;

    void start();

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace hydra::audio

#endif  // HYDRA_AUDIO_DEVICE_H
