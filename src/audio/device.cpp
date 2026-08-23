#include "audio/device.h"

#include <atomic>
#include <cstdint>
#include <stdexcept>

#include "audio/player.h"
#include "miniaudio.h"

namespace hydra::audio {

namespace {

// The device callback's user data. Kept a plain file-local struct (not the
// private Impl) so the C-style callback can reach it.
struct Playback {
    PreviewTransport* transport;
    std::mutex* mutex;
};

// miniaudio's audio thread calls this. It fills `output` with `frame_count`
// interleaved f32 frames; read_frames copies real audio while playing, writes
// silence and auto-pauses at the end, and does nothing while paused. The lock
// keeps the UI thread's transport controls from racing the pull.
void data_callback(ma_device* device, void* output, const void* /*input*/,
                   ma_uint32 frame_count) {
    auto* pb = static_cast<Playback*>(device->pUserData);
    std::lock_guard<std::mutex> lock(*pb->mutex);
    pb->transport->read_frames(static_cast<float*>(output),
                               static_cast<int64_t>(frame_count));
}

}  // namespace

namespace {
std::atomic<bool> g_headless{false};
}

void set_headless(bool headless) { g_headless.store(headless); }
bool headless() { return g_headless.load(); }

struct PreviewAudioDevice::Impl {
    Playback playback;
    ma_device device{};
    bool inited = false;
    bool started = false;
};

PreviewAudioDevice::PreviewAudioDevice(PreviewTransport& transport, std::mutex& mutex)
    : impl_(new Impl{Playback{&transport, &mutex}, {}, false, false}) {
    if (g_headless.load()) return;  // inited stays false: start/stop only flip state
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = static_cast<ma_uint32>(transport.channels());
    config.sampleRate = static_cast<ma_uint32>(transport.sample_rate());
    config.dataCallback = data_callback;
    config.pUserData = &impl_->playback;
    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        delete impl_;
        impl_ = nullptr;
        throw std::runtime_error("PreviewAudioDevice: ma_device_init failed");
    }
    impl_->inited = true;
}

PreviewAudioDevice::~PreviewAudioDevice() {
    if (!impl_) return;
    if (impl_->started) ma_device_stop(&impl_->device);  // waits for the callback
    if (impl_->inited) ma_device_uninit(&impl_->device);
    delete impl_;
}

void PreviewAudioDevice::start() {
    if (!impl_ || impl_->started) return;
    if (!impl_->inited) {  // headless
        impl_->started = true;
        return;
    }
    if (ma_device_start(&impl_->device) == MA_SUCCESS) impl_->started = true;
}

void PreviewAudioDevice::stop() {
    if (impl_ && impl_->started) {
        if (impl_->inited) ma_device_stop(&impl_->device);
        impl_->started = false;
    }
}

bool PreviewAudioDevice::running() const { return impl_ && impl_->started; }

}  // namespace hydra::audio
