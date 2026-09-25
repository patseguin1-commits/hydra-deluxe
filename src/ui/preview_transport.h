// PreviewTransport — the Preview's Transport (CONTEXT.md): play, pause and
// seek together with the master clock, and the audio playhead that follows it
// (docs/adr/0008).
//
// The clock is the master. It is the song time the highway is drawn at, and
// the audio is seeked to it on play and then just follows. A chart with no
// audio still plays and scrubs — the clock runs on its own.
//
// The playhead is shared with the audio thread, so every access to it goes
// through this object's lock, including the device's pull via read_frames().
// The clock is GUI-thread state and is not locked. The transport also holds
// the stop-at-end rule: tick() pauses and pins the time at the end of the
// song, so call it once per frame where the highway is drawn.

#ifndef HYDRA_UI_PREVIEW_TRANSPORT_H
#define HYDRA_UI_PREVIEW_TRANSPORT_H

#include <cstdint>
#include <memory>
#include <mutex>

#include "app/preview_clock.h"
#include "audio/player.h"

namespace hydra::ui {

class PreviewTransport {
public:
    explicit PreviewTransport(app::PreviewClock::Now now = &app::PreviewClock::steady_now);

    PreviewTransport(const PreviewTransport&) = delete;
    PreviewTransport& operator=(const PreviewTransport&) = delete;

    // Load a chart's audio (may be null/empty for a chart with no audio), the
    // song length, and where chart time 0 sits in the audio (audio_ms =
    // chart_ms + audio_offset_ms, never negative; see PreviewLoadJob). The
    // length is the later of `last_note_ms` and the audio's end in chart time.
    // Resets the playhead to the offset, paused.
    void load(std::unique_ptr<audio::Playhead> playhead, double last_note_ms,
              double audio_offset_ms = 0.0);
    void unload();  // drop the playhead, back to an empty paused transport

    void play();    // seeks the playhead to the clock and starts both
    void pause();
    void toggle();
    void seek_ms(double ms);  // clamped to [0, length_ms()]
    bool playing() const;
    double length_ms() const;
    bool has_audio() const;  // a loaded playhead with > 0 frames

    // The song time now. If playing and at/after the end, pauses and pins the
    // time to length_ms() (the stop-at-end rule) — so call this once per frame
    // where the highway is drawn.
    double tick();
    double now_ms() const;  // read-only, no end handling

    void set_gain(float gain);  // applied to the playhead if any, remembered
                                // for the next load
    float gain() const { return gain_; }

    // Device-facing: the pull source. Locks internally. Returns real frames
    // written.
    int64_t read_frames(float* out, int64_t frame_count);
    int channels() const;  // 0 when no audio
    int sample_rate() const;

private:
    app::PreviewClock clock_;  // GUI thread only
    double length_ms_ = 0.0;
    double audio_offset_ms_ = 0.0;  // audio_ms = chart_ms + this
    float gain_ = 1.0f;
    mutable std::mutex mu_;  // guards playhead_ (device thread pulls, GUI controls)
    std::unique_ptr<audio::Playhead> playhead_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_PREVIEW_TRANSPORT_H
