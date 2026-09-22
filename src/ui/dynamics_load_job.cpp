#include "ui/dynamics_load_job.h"

#include "app/dynamics_breakdown.h"
#include "parse/song.h"

namespace hydra::ui {

DynamicsLoadJob::DynamicsLoadJob(store::ChartLibraryEntry entry, bool pro,
                                 Difficulty difficulty)
    : entry_(std::move(entry)), pro_(pro), difficulty_(difficulty) {
    // Always parse with bass2x=true so the 2x row is known even when
    // the "2x Bass" box is off.
    key_ = entry_.notespath + "|" + (pro_ ? "pro" : "std") + "|" +
           difficulty_name(difficulty_);
}

void DynamicsLoadJob::start() { spawn([this] { run(); }); }

void DynamicsLoadJob::run() {
    run_guarded([this] {
        Song song = load_songpath(entry_.notespath, pro_,
                                  /*bass2x=*/true, difficulty_);
        result_ = app::count_dynamics(song);
        return true;
    });
}

app::DynamicsBreakdown DynamicsLoadJob::take_result() {
    return std::move(*result_);
}

}  // namespace hydra::ui
