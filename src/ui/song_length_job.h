// SongLengthJob: reads one chart off the render thread for its length (the
// last note's onset, store::song_length_ms). A result saved before Hydra
// stored lengths has none, so the Paths tab's timeline would stay empty until
// a re-analysis; AppState runs this when such a song is opened and fills the
// length in (RecordStore::set_song_length). Reading the chart takes a moment,
// an analysis far longer, and nothing about the result changes.

#ifndef HYDRA_UI_SONG_LENGTH_JOB_H
#define HYDRA_UI_SONG_LENGTH_JOB_H

#include <optional>
#include <string>

#include "app/analysis.h"        // AnalysisSettings
#include "store/record_store.h"  // ChartLibraryEntry
#include "ui/job_base.h"

namespace hydra::ui {

class SongLengthJob : public ResultJobBase {
public:
    // Reads the chart the way an analysis under `settings` would, so the
    // length matches what that analysis would store.
    SongLengthJob(store::ChartLibraryEntry entry, app::AnalysisSettings settings);
    ~SongLengthJob() { shutdown(); }

    void start();

    const store::ChartLibraryEntry& entry() const { return entry_; }
    // Valid once finished() && ok(); empty for a chart with no notes.
    std::optional<double> length_ms() const { return length_ms_; }

private:
    void run();

    store::ChartLibraryEntry entry_;
    app::AnalysisSettings settings_;
    std::optional<double> length_ms_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_SONG_LENGTH_JOB_H
