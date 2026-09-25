#include "ui/preview_transport.h"

#include <algorithm>
#include <utility>

namespace hydra::ui {

PreviewTransport::PreviewTransport(app::PreviewClock::Now now)
    : clock_(std::move(now)) {}

void PreviewTransport::load(std::unique_ptr<audio::Playhead> playhead,
                            double last_note_ms, double audio_offset_ms) {
    std::lock_guard<std::mutex> lock(mu_);
    playhead_ = std::move(playhead);
    audio_offset_ms_ = audio_offset_ms;
    double audio_end = playhead_ ? playhead_->length_ms() - audio_offset_ms_ : 0.0;
    length_ms_ = (std::max)(last_note_ms, audio_end);
    if (playhead_) {
        playhead_->pause();
        playhead_->seek_ms(audio_offset_ms_);
        playhead_->set_gain(gain_);
    }
    clock_.pause();
    clock_.seek_ms(0.0);
}

void PreviewTransport::unload() {
    std::lock_guard<std::mutex> lock(mu_);
    playhead_.reset();
    length_ms_ = 0.0;
    audio_offset_ms_ = 0.0;
    clock_.pause();
    clock_.seek_ms(0.0);
}

void PreviewTransport::play() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (playhead_) {
            playhead_->seek_ms(clock_.now_ms() + audio_offset_ms_);
            playhead_->play();
        }
    }
    clock_.play();  // a chart with no audio still plays: the clock is the master
}

void PreviewTransport::pause() {
    clock_.pause();
    std::lock_guard<std::mutex> lock(mu_);
    if (playhead_) playhead_->pause();
}

void PreviewTransport::toggle() {
    if (clock_.playing())
        pause();
    else
        play();
}

void PreviewTransport::seek_ms(double ms) {
    if (ms < 0.0) ms = 0.0;
    if (length_ms_ > 0.0 && ms > length_ms_) ms = length_ms_;
    clock_.seek_ms(ms);
    std::lock_guard<std::mutex> lock(mu_);
    if (playhead_) playhead_->seek_ms(ms + audio_offset_ms_);
}

bool PreviewTransport::playing() const { return clock_.playing(); }

double PreviewTransport::length_ms() const { return length_ms_; }

bool PreviewTransport::has_audio() const {
    std::lock_guard<std::mutex> lock(mu_);
    return playhead_ && playhead_->length_frames() > 0;
}

double PreviewTransport::tick() {
    // Stop at the end of the song rather than scrolling into the void.
    if (clock_.playing() && length_ms_ > 0.0 && clock_.now_ms() >= length_ms_) {
        pause();
        seek_ms(length_ms_);
    }
    return now_ms();
}

double PreviewTransport::now_ms() const { return clock_.now_ms(); }

void PreviewTransport::set_gain(float gain) {
    gain_ = gain < 0.0f ? 0.0f : gain;
    std::lock_guard<std::mutex> lock(mu_);
    if (playhead_) playhead_->set_gain(gain_);
}

int64_t PreviewTransport::read_frames(float* out, int64_t frame_count) {
    std::lock_guard<std::mutex> lock(mu_);
    if (playhead_) return playhead_->read_frames(out, frame_count);
    return 0;
}

int PreviewTransport::channels() const {
    std::lock_guard<std::mutex> lock(mu_);
    return playhead_ ? playhead_->channels() : 0;
}

int PreviewTransport::sample_rate() const {
    std::lock_guard<std::mutex> lock(mu_);
    return playhead_ ? playhead_->sample_rate() : 0;
}

}  // namespace hydra::ui
