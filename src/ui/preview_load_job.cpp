#include "ui/preview_load_job.h"

#include <algorithm>
#include <string>

#include "app/preview_source.h"
#include "app/preview_view.h"
#include "audio/mixer.h"
#include "core/model.h"

namespace hydra::ui {

PreviewLoadJob::PreviewLoadJob(store::ChartLibraryEntry entry, bool pro, bool bass2x,
                               std::optional<Path> path)
    : entry_(std::move(entry)),
      pro_(pro),
      bass2x_(bass2x),
      path_(std::move(path)) {}

void PreviewLoadJob::start() { spawn([this] { run(); }); }

void PreviewLoadJob::run() {
    run_guarded([this] {
        // Re-parse the chart and locate its audio (the note stream and stems
        // are never stored), then decode + mix to one 48 kHz stereo buffer.
        step_.store(Step::Reading);
        app::PreviewSource source =
            app::resolve_preview_source(entry_.notespath, pro_, bass2x_);
        step_.store(Step::Decoding);
        audio::DecodedAudio mixed = audio::decode_and_mix(
            source.stems, /*out_rate=*/48000, /*out_channels=*/2, [this](int done, int total) {
                stems_total_.store(total);
                stems_done_.store(done);
                if (done == total) step_.store(Step::Mixing);
            });
        step_.store(Step::Building);
        const Path* path = path_ ? &*path_ : nullptr;
        app::PreviewScene scene = app::build_preview_scene(source.song, path);
        result_ = Result{std::move(scene), std::move(mixed)};
        return true;
    });
}

PreviewLoadJob::Result PreviewLoadJob::take_result() { return std::move(*result_); }

PreviewLoadJob::Progress PreviewLoadJob::progress() const {
    Progress p;
    p.step = step_.load();
    p.stems_done = stems_done_.load();
    p.stems_total = stems_total_.load();
    return p;
}

float PreviewLoadJob::Progress::fraction() const {
    // Reading 0-10%, decoding 10-85%, mixing 85-95%, building 95-100%.
    switch (step) {
        case Step::Reading: return 0.0f;
        case Step::Decoding: {
            float per_stem = stems_total > 0
                                 ? static_cast<float>(stems_done) / static_cast<float>(stems_total)
                                 : 0.0f;
            return 0.10f + 0.75f * per_stem;
        }
        case Step::Mixing: return 0.85f;
        case Step::Building: return 0.95f;
    }
    return 0.0f;
}

std::string PreviewLoadJob::Progress::label() const {
    switch (step) {
        case Step::Reading: return "Reading chart";
        case Step::Decoding:
            return "Decoding audio " + std::to_string(std::min(stems_done + 1, stems_total)) +
                   "/" + std::to_string(stems_total);
        case Step::Mixing: return "Mixing audio";
        case Step::Building: return "Building scene";
    }
    return "";
}

}  // namespace hydra::ui
