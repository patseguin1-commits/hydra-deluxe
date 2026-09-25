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
#include <optional>
#include <string>

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

class PreviewController {
public:
    PreviewController(ID3D11Device* device, ID3D11DeviceContext* context);
    ~PreviewController();

    PreviewController(const PreviewController&) = delete;
    PreviewController& operator=(const PreviewController&) = delete;

    // (Re)start the preview for `entry`. Called every frame the Preview tab is
    // shown. `path` (may be null) supplies the path overlay; it is copied, so
    // the caller's Path need not outlive the call. Already open for the same
    // chart and the same path and SP cap: a no-op. Same chart, different path
    // or a changed `sp_cap`: the overlay is swapped in place off the retained
    // song — no re-parse, no audio re-decode, playback position untouched.
    void open(const store::ChartLibraryEntry& entry, bool pro, bool bass2x,
              Difficulty difficulty, const Path* path, int sp_cap,
              const core::Rules& rules = core::default_rules());
    // Stop audio, drop the scene/transport, and join the load thread. Keeps the
    // renderer for reuse. Safe to call when nothing is open.
    void close();

    bool active() const { return active_; }
    const std::string& open_key() const { return open_key_; }
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
    bool has_audio() const;

    // The scrubber is being held (mouse down on it). As in Onyx, playback
    // pauses for the hold and resumes on release if it was playing; the
    // highway still follows the seeks. Call every frame with the slider's
    // active state. Without this the clock kept running under the held
    // slider and the audio was re-seeked to the held time every frame —
    // heard as a buzz.
    void set_scrubbing(bool held);
    bool scrubbing() const { return scrubbing_; }

    // Playback volume in percent (0..100); applied to the audio as it is
    // served, and remembered for the next chart opened.
    void set_volume(int percent);
    int volume() const { return volume_pct_; }

    // The time box the panel draws over the highway (Onyx's top-left text).
    hydra::app::PreviewTimeBox time_box() const;

    // The Preview's look, as read from 3d-config.json by the renderer. Before
    // the first render (no renderer yet) this is the struct's defaults, which
    // are Onyx's values.
    const render::PreviewConfig& preview_config() const;

private:
    ID3D11Device* device_;
    ID3D11DeviceContext* context_;

    std::unique_ptr<render::PreviewRenderer> renderer_;  // created on first render
    render::RenderParams params_;
    int rt_w_ = 0;
    int rt_h_ = 0;
    bool have_frame_ = false;

    std::unique_ptr<PreviewLoadJob> job_;
    hydra::app::PreviewScene scene_;
    bool scene_dirty_ = true;  // scene_ changed since the renderer last saw it
    bool pro_ = true;          // the pro-drums view setting the chart was opened with
    int sp_cap_ = kCloneHeroSpCap;  // the SP meter's ceiling the scene was built with

    // The path overlay, kept swappable without touching the audio: the parsed
    // song the load produced, the overlay the panel last asked for, and the one
    // scene_ was actually built with. Each key is the path's overlay key plus
    // the SP cap (see overlay_key() in the .cpp), so a cap change rebuilds the
    // scene the same way a path change does. The two keys differ only while a
    // load is in flight and the Paths tab (or the cap) changed the selection
    // under it; poll() closes the gap.
    std::optional<Song> song_;
    std::optional<Path> path_;
    std::string path_key_;        // key of path_ + sp_cap_
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
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_PREVIEW_CONTROLLER_H
