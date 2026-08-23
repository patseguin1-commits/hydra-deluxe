#include "audio/player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hydra::audio {

PreviewTransport::PreviewTransport(DecodedAudio mixed)
    : audio_(std::move(mixed)),
      channels_(audio_.channels),
      sample_rate_(audio_.sample_rate),
      length_(audio_.frames()) {}

void PreviewTransport::play() { playing_ = true; }
void PreviewTransport::pause() { playing_ = false; }
void PreviewTransport::toggle() { playing_ = !playing_; }

void PreviewTransport::seek_frames(int64_t frame) {
    position_ = std::clamp<int64_t>(frame, 0, length_);
}

void PreviewTransport::seek_ms(double ms) {
    if (sample_rate_ <= 0) return;
    seek_frames(static_cast<int64_t>(std::llround(ms * sample_rate_ / 1000.0)));
}

double PreviewTransport::position_ms() const {
    return sample_rate_ > 0 ? position_ * 1000.0 / sample_rate_ : 0.0;
}

double PreviewTransport::length_ms() const {
    return sample_rate_ > 0 ? length_ * 1000.0 / sample_rate_ : 0.0;
}

int64_t PreviewTransport::read_frames(float* out, int64_t frame_count) {
    if (frame_count <= 0) return 0;
    std::size_t total = static_cast<std::size_t>(frame_count) * channels_;

    if (!playing_) {
        std::memset(out, 0, total * sizeof(float));
        return 0;
    }

    int64_t avail = length_ - position_;
    int64_t n = std::min<int64_t>(frame_count, std::max<int64_t>(avail, 0));

    if (n > 0) {
        const float* src =
            audio_.samples.data() + static_cast<std::size_t>(position_) * channels_;
        const std::size_t count = static_cast<std::size_t>(n) * channels_;
        if (gain_ == 1.0f) {
            std::memcpy(out, src, count * sizeof(float));
        } else {
            for (std::size_t i = 0; i < count; ++i) out[i] = src[i] * gain_;
        }
    }
    // Silence any frames past the end of the mix.
    std::size_t written = static_cast<std::size_t>(n) * channels_;
    if (written < total)
        std::memset(out + written, 0, (total - written) * sizeof(float));

    position_ += n;
    if (position_ >= length_) {
        position_ = length_;  // clamp; the master clock rests at the end
        playing_ = false;     // auto-pause at end of stream
    }
    return n;
}

}  // namespace hydra::audio
