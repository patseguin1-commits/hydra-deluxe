#include "ui/preview_load_job.h"

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
        app::PreviewSource source =
            app::resolve_preview_source(entry_.notespath, pro_, bass2x_);
        audio::DecodedAudio mixed =
            audio::decode_and_mix(source.stems, /*out_rate=*/48000, /*out_channels=*/2);
        const Path* path = path_ ? &*path_ : nullptr;
        app::PreviewScene scene = app::build_preview_scene(source.song, path);
        result_ = Result{std::move(scene), std::move(mixed)};
        return true;
    });
}

PreviewLoadJob::Result PreviewLoadJob::take_result() { return std::move(*result_); }

}  // namespace hydra::ui
