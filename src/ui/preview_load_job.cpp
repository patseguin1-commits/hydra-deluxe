#include "ui/preview_load_job.h"

#include <algorithm>
#include <string>

#include "app/preview_source.h"
#include "app/preview_view.h"
#include "audio/mixer.h"
#include "core/model.h"

namespace hydra::ui {

PreviewLoadJob::PreviewLoadJob(store::ChartLibraryEntry entry, bool pro, bool bass2x,
                               Difficulty difficulty, std::optional<Path> path, int sp_cap,
                               core::Rules rules)
    : entry_(std::move(entry)),
      pro_(pro),
      bass2x_(bass2x),
      difficulty_(difficulty),
      path_(std::move(path)),
      sp_cap_(sp_cap),
      rules_(std::move(rules)) {}

void PreviewLoadJob::start() { spawn([this] { run(); }); }

void PreviewLoadJob::run() {
    run_guarded([this] {
        // Re-parse the chart and locate its audio (the note stream and stems
        // are never stored), then decode + mix to one 48 kHz stereo buffer.
        // Closing the details window joins this thread on the UI thread, so
        // the job looks at its cancel flag between steps and between stems.
        step_.store(Step::Reading);
        throw_if_cancelled();
        app::PreviewSource source =
            app::resolve_preview_source(entry_.notespath, pro_, bass2x_, difficulty_, rules_);
        // A chart with no charting at this difficulty would otherwise build an
        // empty scene and the tab would show a blank highway with no reason
        // given. Throwing here surfaces it as "Preview failed: ...", the same
        // wording analysis uses.
        if (source.song.is_empty())
            throw ChartFileError(no_notes_message(difficulty_, pro_));
        throw_if_cancelled();
        step_.store(Step::Decoding);
        audio::DecodedAudio mixed = audio::decode_and_mix(
            source.stems, /*out_rate=*/48000, /*out_channels=*/2, [this](int done, int total) {
                stems_total_.store(total);
                stems_done_.store(done);
                if (done == total) step_.store(Step::Mixing);
                // Called before the first stem and after each one, outside the
                // decoder's per-stem catch, so a cancel stops the load here.
                throw_if_cancelled();
            });
        throw_if_cancelled();
        double offset_ms = source.audio_offset_ms;
        if (offset_ms < 0.0) {
            audio::pad_front_ms(mixed, -offset_ms);
            offset_ms = 0.0;
        }
        step_.store(Step::Building);
        const Path* path = path_ ? &*path_ : nullptr;
        app::PreviewScene scene = app::build_preview_scene(source.song, path, sp_cap_, rules_);
        result_ = Result{std::move(scene), std::move(mixed), offset_ms, std::move(source.song)};
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
