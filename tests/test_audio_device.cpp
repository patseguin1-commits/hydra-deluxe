// Tests for audio/device: the one place Hydra opens a real output device.
// miniaudio is built with only its WASAPI backend (CMakeLists.txt), so this
// proves that backend still opens and starts the default device. It needs a
// machine with an audio output, which every Hydra dev machine has.

#include "doctest.h"

#include <algorithm>
#include <cstdint>
#include <memory>

#include "audio/device.h"

using namespace hydra::audio;

TEST_CASE("PreviewAudioDevice opens and starts the default output device") {
    set_headless(false);  // the real device, whatever an earlier case set
    std::unique_ptr<PreviewAudioDevice> device;
    REQUIRE_NOTHROW(device = std::make_unique<PreviewAudioDevice>(
                        2, 48000, [](float* out, int64_t frames) {
                            std::fill(out, out + frames * 2, 0.0f);
                            return int64_t{0};
                        }));
    device->start();
    device.reset();  // stops the device and waits for the callback first
}
