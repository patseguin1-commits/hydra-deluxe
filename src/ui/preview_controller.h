// PreviewController — owns the live 3D Preview for one open chart.
//
// The details modal's Preview tab drives this. It ties together an async
// PreviewLoadJob (re-parse + decode + build scene), the offscreen
// PreviewRenderer (the Onyx port, docs/adr/0008), a PreviewTransport over the
// mixed audio, and a PreviewAudioDevice that plays it. The display clock
// (app/preview_clock.h) is the master, as in Onyx: the highway is drawn at
// clock.now_ms() each frame, and on play the audio is seeked to that time and
// then follows. A chart with no audio still plays and scrubs.
//
// One controller lives on AppState, created on first use with the GUI's shared
// D3D11 device. It is opened for the selected chart, torn down when the modal
// or selection changes, and only renders + decodes while the Preview tab shows
// — so a library browse never spins up a GPU scene or an audio device.

#ifndef HYDRA_UI_PREVIEW_CONTROLLER_H
#define HYDRA_UI_PREVIEW_CONTROLLER_H

#include <memory>
#include <mutex>
#include <string>

#include "app/preview_clock.h"
#include "app/preview_view.h"
#include "core/model.h"  // Path
#include "render/preview_renderer.h"
#include "store/record_store.h"  // ChartLibraryEntry

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

namespace hydra::audio {
class PreviewTransport;
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

    // (Re)start the preview for `entry`. A no-op if already open for the same
    // chart. `path` (may be null) supplies the activation overlay; it is copied,
    // so the caller's Path need not outlive the call.
    void open(const store::ChartLibraryEntry& entry, bool pro, bool bass2x,
              const Path* path);
    // Stop audio, drop the scene/transport, and join the load thread. Keeps the
    // renderer for reuse. Safe to call when nothing is open.
    void close();

    bool active() const { return active_; }
    const std::string& open_key() const { return open_key_; }

    // Advance the async load; once finished, build the transport + audio device.
    // Call once per frame while the Preview tab is shown.
    void poll();

    bool loading() const { return job_ != nullptr; }
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

    // Playback volume in percent (0..100); applied to the audio as it is
    // served, and remembered for the next chart opened.
    void set_volume(int percent);
    int volume() const { return volume_pct_; }

    // The time box the panel draws over the highway (Onyx's top-left text).
    hydra::app::PreviewTimeBox time_box() const;

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

    // The master clock (GUI thread only) and the song length it runs to: the
    // later of the last note and the audio's end.
    hydra::app::PreviewClock clock_;
    double length_ms_ = 0.0;
    int volume_pct_ = 40;

    // The transport is shared with the audio thread; transport_mu_ guards every
    // access (the device's pull and the UI's controls). Declared before
    // audio_device_ so the device (which references the transport) is destroyed
    // first.
    mutable std::mutex transport_mu_;
    std::unique_ptr<hydra::audio::PreviewTransport> transport_;
    std::unique_ptr<hydra::audio::PreviewAudioDevice> audio_device_;

    bool active_ = false;
    std::string open_key_;
    std::string error_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_PREVIEW_CONTROLLER_H
