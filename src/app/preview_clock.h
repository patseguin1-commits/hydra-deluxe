// PreviewClock — the Preview's display clock, the way Onyx keeps time.
//
// While playing, the song time is "the time at which play started, plus how
// long ago that was" on a monotonic clock. Pause freezes the song time; seek
// sets it. The audio transport is told to start from this clock's time on
// play and then just follows — the audio position is not the master (this
// supersedes that part of docs/adr/0004; see docs/adr/0008).
//
// The monotonic source is injectable so the arithmetic is unit-tested with a
// fake clock. Not thread-safe: the GUI thread owns it.

#ifndef HYDRA_APP_PREVIEW_CLOCK_H
#define HYDRA_APP_PREVIEW_CLOCK_H

#include <chrono>
#include <functional>

namespace hydra::app {

class PreviewClock {
public:
    using Now = std::function<double()>;  // monotonic seconds

    static double steady_now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    explicit PreviewClock(Now now = &PreviewClock::steady_now) : now_(std::move(now)) {}

    void play() {
        if (playing_) return;
        start_s_ = now_();
        playing_ = true;
    }
    void pause() {
        if (!playing_) return;
        song_ms_ = now_ms();
        playing_ = false;
    }
    void toggle() { playing_ ? pause() : play(); }

    // Jump to `ms`; playback (if any) continues from there.
    void seek_ms(double ms) {
        song_ms_ = ms;
        start_s_ = now_();
    }

    double now_ms() const {
        return playing_ ? song_ms_ + (now_() - start_s_) * 1000.0 : song_ms_;
    }
    bool playing() const { return playing_; }

private:
    Now now_;
    double song_ms_ = 0.0;
    double start_s_ = 0.0;
    bool playing_ = false;
};

}  // namespace hydra::app

#endif  // HYDRA_APP_PREVIEW_CLOCK_H
