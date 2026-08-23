#include "ui/preview_controller.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <utility>

#include "app/config.h"
#include "audio/device.h"
#include "audio/player.h"
#include "ui/jobs.h"

namespace hydra::ui {

PreviewController::PreviewController(ID3D11Device* device, ID3D11DeviceContext* context)
    : device_(device), context_(context) {}

PreviewController::~PreviewController() { close(); }

void PreviewController::open(const store::ChartLibraryEntry& entry, bool pro,
                             bool bass2x, const Path* path) {
    if (active_ && open_key_ == entry.md5) return;
    close();

    open_key_ = entry.md5;
    active_ = true;
    error_.clear();
    pro_ = pro;

    std::optional<Path> path_copy;
    if (path) path_copy = *path;
    job_ = std::make_unique<PreviewLoadJob>(entry, pro, bass2x, std::move(path_copy));
    job_->start();
}

void PreviewController::close() {
    // Stop the device before the transport it points at is destroyed.
    audio_device_.reset();
    {
        std::lock_guard<std::mutex> lock(transport_mu_);
        transport_.reset();
    }
    job_.reset();  // ResultJobBase's shutdown() joins the worker
    scene_ = hydra::app::PreviewScene{};
    scene_dirty_ = true;  // the renderer (if kept) must drop the old chart
    clock_ = hydra::app::PreviewClock{};
    length_ms_ = 0.0;
    have_frame_ = false;
    active_ = false;
    open_key_.clear();
    error_.clear();
}

void PreviewController::poll() {
    if (!job_ || !job_->finished()) return;

    if (job_->ok()) {
        PreviewLoadJob::Result result = job_->take_result();
        scene_ = std::move(result.scene);
        scene_dirty_ = true;
        transport_ = std::make_unique<audio::PreviewTransport>(std::move(result.mixed));
        transport_->set_gain(static_cast<float>(volume_pct_) / 100.0f);
        length_ms_ = (std::max)(scene_.song_length_ms, transport_->length_ms());
        clock_.seek_ms(0.0);
        // Open the output device only when there is audio to play; a chart with
        // no locatable stems previews silently (the highway still draws).
        if (transport_->length_frames() > 0) {
            try {
                audio_device_ = std::make_unique<audio::PreviewAudioDevice>(
                    *transport_, transport_mu_);
                audio_device_->start();
            } catch (const std::exception& e) {
                error_ = e.what();  // no device: still previewable, just muted
            }
        }
    } else {
        error_ = job_->error();
    }
    job_.reset();
}

ID3D11ShaderResourceView* PreviewController::render(int width, int height) {
    if (width <= 0 || height <= 0)
        return have_frame_ && renderer_ ? renderer_->texture_srv() : nullptr;
    try {
        if (!renderer_) {
            // Authored highway art lives beside the exe, staged by the build
            // (or wherever app::set_path_overrides pointed a harness).
            const std::string asset_dir = hydra::app::asset_dir();
            renderer_ =
                std::make_unique<render::PreviewRenderer>(device_, context_, asset_dir);
        }
        if (width != rt_w_ || height != rt_h_) {
            renderer_->resize(width, height);
            rt_w_ = width;
            rt_h_ = height;
        }
        if (scene_dirty_) {
            render::TrackStateOptions opts;
            opts.pro = pro_;
            renderer_->set_scene(scene_, opts);
            scene_dirty_ = false;
        }
        // Stop at the end of the song rather than scrolling into the void.
        if (clock_.playing() && length_ms_ > 0.0 && clock_.now_ms() >= length_ms_) {
            pause();
            seek_ms(length_ms_);
        }
        renderer_->render(clock_.now_ms(), params_);
        have_frame_ = true;
        return renderer_->texture_srv();
    } catch (const std::exception& e) {
        if (error_.empty()) error_ = e.what();
        return nullptr;
    }
}

// ---- transport controls: the clock leads, audio follows ----------------
// (transport_mu_ guards only the audio transport, which the device thread
// also reads; the clock is GUI-thread state.)

void PreviewController::play() {
    if (!active_ || job_) return;  // nothing loaded yet
    {
        std::lock_guard<std::mutex> lock(transport_mu_);
        if (transport_) {
            transport_->seek_ms(clock_.now_ms());
            transport_->play();
        }
    }
    clock_.play();
}

void PreviewController::pause() {
    clock_.pause();
    std::lock_guard<std::mutex> lock(transport_mu_);
    if (transport_) transport_->pause();
}

void PreviewController::toggle() {
    if (clock_.playing())
        pause();
    else
        play();
}

bool PreviewController::playing() const { return clock_.playing(); }

double PreviewController::position_ms() const { return clock_.now_ms(); }

double PreviewController::length_ms() const { return length_ms_; }

void PreviewController::seek_ms(double ms) {
    if (ms < 0.0) ms = 0.0;
    if (length_ms_ > 0.0 && ms > length_ms_) ms = length_ms_;
    clock_.seek_ms(ms);
    std::lock_guard<std::mutex> lock(transport_mu_);
    if (transport_) transport_->seek_ms(ms);
}

bool PreviewController::has_audio() const {
    std::lock_guard<std::mutex> lock(transport_mu_);
    return transport_ && transport_->length_frames() > 0;
}

void PreviewController::set_volume(int percent) {
    volume_pct_ = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    std::lock_guard<std::mutex> lock(transport_mu_);
    if (transport_) transport_->set_gain(static_cast<float>(volume_pct_) / 100.0f);
}

hydra::app::PreviewTimeBox PreviewController::time_box() const {
    return hydra::app::build_time_box(scene_, clock_.now_ms());
}

}  // namespace hydra::ui
