// The output-device half of the Preview player.
//
// PreviewTransport (audio/player.h) is pure and device-free; this wraps a
// miniaudio playback device around one, pulling frames from read_frames() on
// miniaudio's audio thread. The transport is not internally synchronized, so
// the device takes the caller-owned `mutex` around every pull; the UI thread
// locks the same mutex when it calls transport controls. The device is torn
// down (stopped, so the callback has returned) before the transport it points
// at may be destroyed, so destroy the device first.
//
// miniaudio's device API lives only here, kept private to hydra_audio like the
// decoders; the header stays free of miniaudio via a pImpl.

#ifndef HYDRA_AUDIO_DEVICE_H
#define HYDRA_AUDIO_DEVICE_H

#include <mutex>

namespace hydra::audio {

class PreviewTransport;

// Process-wide switch for harnesses with no sound card (the GUI test runner):
// when set, PreviewAudioDevice opens nothing and start()/stop() only track
// state, so Play/Pause stays testable without touching a real device.
void set_headless(bool headless);
bool headless();

class PreviewAudioDevice {
public:
    // Opens a playback device matching `transport`'s channel count and sample
    // rate (f32 samples). Throws std::runtime_error if the device won't open.
    PreviewAudioDevice(PreviewTransport& transport, std::mutex& mutex);
    ~PreviewAudioDevice();

    PreviewAudioDevice(const PreviewAudioDevice&) = delete;
    PreviewAudioDevice& operator=(const PreviewAudioDevice&) = delete;

    void start();
    void stop();
    bool running() const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace hydra::audio

#endif  // HYDRA_AUDIO_DEVICE_H
