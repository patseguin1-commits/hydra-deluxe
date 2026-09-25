#include "ui/dynamics_load_job.h"

#include "app/dynamics_breakdown.h"
#include "parse/song.h"

namespace hydra::ui {

DynamicsLoadJob::DynamicsLoadJob(store::ChartLibraryEntry entry, bool pro,
                                 Difficulty difficulty)
    : entry_(std::move(entry)), pro_(pro), difficulty_(difficulty) {
    key_ = app::dynamics_cache_key(entry_.notespath, pro_, difficulty_);
}

void DynamicsLoadJob::start() { spawn([this] { run(); }); }

void DynamicsLoadJob::run() {
    run_guarded([this] {
        Song song = load_songpath(entry_.notespath, pro_, app::kDynamicsParseBass2x,
                                  difficulty_);
        result_ = app::count_dynamics(song);
        return true;
    });
}

app::DynamicsBreakdown DynamicsLoadJob::take_result() {
    return std::move(*result_);
}

}  // namespace hydra::ui
