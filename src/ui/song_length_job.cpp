#include "ui/song_length_job.h"

#include "parse/song.h"

namespace hydra::ui {

SongLengthJob::SongLengthJob(store::ChartLibraryEntry entry, app::AnalysisSettings settings)
    : entry_(std::move(entry)), settings_(std::move(settings)) {}

void SongLengthJob::start() { spawn([this] { run(); }); }

void SongLengthJob::run() {
    run_guarded([this] {
        // The parse is one call and cannot stop midway, so the job looks at
        // its cancel flag before and after it.
        throw_if_cancelled();
        Song song = load_songpath(entry_.notespath, settings_.prodrums, settings_.bass2x,
                                  settings_.difficulty, settings_.rules);
        throw_if_cancelled();
        length_ms_ = store::song_length_ms(song);
        return true;
    });
}

}  // namespace hydra::ui
