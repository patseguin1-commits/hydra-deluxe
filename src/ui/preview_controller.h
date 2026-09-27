// PreviewController — owns the live 3D Preview for one open chart.
//
// The details modal's Preview tab drives this. It ties together an async
// PreviewLoadJob (re-parse + decode + build scene), the offscreen
// PreviewRenderer (the Onyx port, docs/adr/0008), a PreviewTransport over the
// mixed audio, and a PreviewAudioDevice that plays it. The Transport owns the
// play/pause/seek rules and the master display clock, as in Onyx: the highway
// is drawn at transport_.tick() each frame, and on play the audio is seeked to
// that time and then follows. A chart with no audio still plays and scrubs.
//
// One controller lives on AppState, created on first use with the GUI's shared
// D3D11 device. It is opened for the selected chart, torn down when the modal
// or selection changes, and only renders + decodes while the Preview tab shows
// — so a library browse never spins up a GPU scene or an audio device.

#ifndef HYDRA_UI_PREVIEW_CONTROLLER_H
#define HYDRA_UI_PREVIEW_CONTROLLER_H

#include <memory>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app/preview_view.h"
#include "core/model.h"  // Path
#include "parse/song.h"
#include "render/preview_renderer.h"
#include "store/record_store.h"  // ChartLibraryEntry
#include "ui/preview_transport.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace hydra::audio {
class PreviewAudioDevice;
}  // namespace hydra::audio

namespace hydra::ui {

class PreviewLoadJob;
class PreviewSceneJob;

class PreviewController {
public:
    PreviewController(ID3D11Device* device, ID3D11DeviceContext* context);
    ~PreviewController();

    PreviewController(const PreviewController&) = delete;
    PreviewController& operator=(const PreviewController&) = delete;

    // (Re)start the preview for `entry`. Called every frame the Preview tab is
    // shown. `path` (may be null) supplies the path overlay; it is copied, so
    // the caller's Path need not outlive the call. `path_key` is
    // app::path_overlay_key(path), which the caller builds once per selection
    // (it is too heavy to build per frame). Already open for the same chart,
    // path key and SP cap: a no-op. Same chart, different path or cap: the new
    // overlay is built on a background job off the retained song and swapped
    // in by a later poll() — no re-parse, no audio re-decode, playback
    // position untouched; the old overlay stays up until then.
    void open(const store::ChartLibraryEntry& entry, bool pro, bool bass2x,
              Difficulty difficulty, const Path* path, const std::string& path_key,
              int sp_cap, const core::Rules& rules = core::default_rules());
    // Stop audio, drop the scene/transport, and join the load thread. Keeps the
    // renderer for reuse. Safe to call when nothing is open.
    void close();

    bool active() const { return active_; }
    // The drawn overlay's identity: the path it was built from
    // (app::path_overlay_key) plus the SP cap its meter was scaled to, in the
    // one string open() compares. Empty until something has loaded. Treat the
    // exact spelling as opaque — compare two of these, don't parse one.
    const std::string& overlay_path_key() const { return scene_path_key_; }

    // Advance the async load; once finished, build the transport + audio device.
    // Call once per frame while the Preview tab is shown.
    void poll();

    // Bars of SP banked at the transport's current time, and the meter's
    // ceiling — what the panel's gauge draws.
    double sp_meter_bars() const;
    int sp_meter_cap() const;
    bool sp_meter_has_curve() const;

    bool loading() const { return job_ != nullptr; }
    // Only meaningful while loading(); the load's current step and stem count.
    struct LoadProgress {
        float fraction = 0.0f;  // 0..1 estimate
        std::string label;      // "Decoding audio 2/5"
    };
    LoadProgress load_progress() const;
    bool has_error() const { return !error_.empty(); }
    const std::string& error() const { return error_; }

    // Set when the audio output device would not open. Not an error: the
    // chart still loads, draws and plays on the clock, just muted. The panel
    // shows one warning line and keeps drawing.
    bool has_audio_warning() const { return !audio_warning_.empty(); }
    const std::string& audio_warning() const { return audio_warning_; }

    // What opens the output device. Empty (the default) opens the real one; a
    // test installs a factory that throws, standing in for a PC with no audio
    // device.
    using AudioSource = std::function<int64_t(float* out, int64_t frames)>;
    using AudioDeviceFactory = std::function<std::unique_ptr<hydra::audio::PreviewAudioDevice>(
        int channels, int sample_rate, AudioSource source)>;
    void set_audio_device_factory(AudioDeviceFactory factory) {
        device_factory_ = std::move(factory);
    }

    // Draw the highway at the current playhead into a width x height offscreen
    // target; returns its shader-resource view for ImGui::Image (null until the
    // first successful render). Resizes the target when the region changes.
    ID3D11ShaderResourceView* render(int width, int height);

    // Transport controls, driven by the display clock; audio (when any)
    // follows. Safe before the chart has loaded (nothing to move yet).
    void play();
    void pause();
    void toggle();
    bool playing() const;
    double position_ms() const;
    double length_ms() const;
    void seek_ms(double ms);
    // Move the playhead by `delta_ms` (the -5s/+5s buttons, Left/Right).
    // Playing stays playing; the transport stops it at the song's ends.
    void jump_ms(double delta_ms);
    // Pause, then move the playhead `delta_ticks` chart ticks from the tick
    // the time box shows (the < 5 Ticks / 5 Ticks > buttons, comma and period).
    // A step of 0 snaps onto the displayed tick.
    void step_ticks(int delta_ticks);
    bool has_audio() const;

    // The scrubber is being held (mouse down on it). As in Onyx, playback
    // pauses for the hold and resumes on release if it was playing; the
    // highway still follows the seeks. Call every frame with the slider's
    // active state. Without this the clock kept running under the held
    // slider and the audio was re-seeked to the held time every frame —
    // heard as a buzz.
    void set_scrubbing(bool held);

    // Playback volume in percent (0..100); applied to the audio as it is
    // served, and remembered for the next chart opened.
    void set_volume(int percent);

    // The time box the panel draws over the highway (Onyx's top-left text).
    hydra::app::PreviewTimeBox time_box() const;

    // The score box the panel draws under the time box.
    hydra::app::PreviewScoreBox score_box() const;

    // The SP drain box the panel draws beside the gauge.
    hydra::app::PreviewDrainBox drain_box() const;

    // The drawn path's activations on the scrubber, as fractions of
    // length_ms() (app::build_scrub_marks). Empty until a path's scene is in.
    std::vector<double> scrub_marks() const;

    // The box at the highway's bottom-left (app::build_next_act_box).
    hydra::app::PreviewNextActBox next_act_box() const;

    // The number under the SP gauge, "2.5/4" (app::sp_meter_readout).
    std::string sp_meter_readout() const;

    // Move the playhead to the previous (-1) or next (+1) activation of the
    // drawn path (app::activation_jump_ms). Playing stays playing, as with
    // jump_ms. False when there is none that way or nothing is loaded.
    bool jump_activation(int direction);

    // Move the playhead to activation `index` (0-based) of the drawn path,
    // for "Show in Preview". False when nothing is loaded or the drawn path
    // has no such activation.
    bool seek_activation(size_t index);

    // The text overlays' scale the panel last drew at (1 = full size). The
    // panel sets it each frame; only the GUI test reads it back.
    void set_overlay_scale(float scale) { overlay_scale_ = scale; }
    float overlay_scale() const { return overlay_scale_; }

    // The Preview's look, as read from 3d-config.json by the renderer. Before
    // the first render (no renderer yet) this is the struct's defaults, which
    // are Onyx's values.
    const render::PreviewConfig& preview_config() const;

private:
    float overlay_scale_ = 1.0f;
    ID3D11Device* device_;
    ID3D11DeviceContext* context_;

    std::unique_ptr<render::PreviewRenderer> renderer_;  // created on first render
    int rt_w_ = 0;
    int rt_h_ = 0;
    bool have_frame_ = false;

    std::unique_ptr<PreviewLoadJob> job_;
    hydra::app::PreviewScene scene_;
    bool scene_dirty_ = true;  // scene_ changed since the renderer last saw it
    bool pro_ = true;          // the pro-drums view setting the chart was opened with
    int sp_cap_ = kCloneHeroSpCap;  // the SP meter's ceiling the scene was built with
    // The rules the running score is priced under: the user's
    // hydra_rules.ini, as the panel passes it to every open().
    core::Rules rules_ = core::default_rules();

    // The path overlay, kept swappable without touching the audio: the parsed
    // song the load produced, the overlay the panel last asked for, and the one
    // scene_ was actually built with. Each key is the path's overlay key plus
    // the SP cap (see overlay_key() in the .cpp), so a cap change rebuilds the
    // scene the same way a path change does. The two keys differ only while a
    // load is in flight and the Paths tab (or the cap) changed the selection
    // under it; poll() closes the gap.
    std::shared_ptr<const Song> song_;  // shared read-only with scene jobs
    std::optional<Path> path_;
    std::string requested_path_key_;  // the path half of path_key_, as open() got it
    std::string path_key_;        // key of path_ + sp_cap_
    // The overlay being built for a new selection, and replaced ones still
    // finishing (dropped by poll() once done, so replacing one never joins
    // its thread on the UI thread).
    std::unique_ptr<PreviewSceneJob> scene_job_;
    std::vector<std::unique_ptr<PreviewSceneJob>> retired_scene_jobs_;
    void start_scene_job();
    std::string job_path_key_;    // key the in-flight job was started with
    std::string scene_path_key_;  // key scene_'s overlay was built from

    int volume_pct_ = 40;
    bool scrubbing_ = false;
    bool resume_after_scrub_ = false;

    // Play, pause, seek, the master clock and the audio that follows it.
    // Declared before audio_device_ so the device (whose callback pulls from
    // the transport) is destroyed first.
    PreviewTransport transport_;
    std::unique_ptr<hydra::audio::PreviewAudioDevice> audio_device_;

    bool active_ = false;
    std::string open_key_;
    std::string error_;
    std::string audio_warning_;
    AudioDeviceFactory device_factory_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_PREVIEW_CONTROLLER_H
