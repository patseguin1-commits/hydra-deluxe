// DynamicsLoadJob: re-parses a chart off the render thread to produce a
// DynamicsBreakdown (per-pad ghost/accent/normal counts).  The details
// modal's Dynamics tab owns this: start it the first time the tab is
// shown, cache the result until the chart/pro/difficulty changes, and
// drop it when they do.  Modelled on PreviewLoadJob (ResultJobBase,
// run_guarded, generation counter for lifecycle).

#ifndef HYDRA_UI_DYNAMICS_LOAD_JOB_H
#define HYDRA_UI_DYNAMICS_LOAD_JOB_H

#include <optional>
#include <string>

#include "app/dynamics_breakdown.h"
#include "store/record_store.h"  // ChartLibraryEntry
#include "ui/job_base.h"

namespace hydra::ui {

class DynamicsLoadJob : public ResultJobBase {
public:
    DynamicsLoadJob(store::ChartLibraryEntry entry, bool pro, Difficulty difficulty);
    ~DynamicsLoadJob() { shutdown(); }

    void start();

    // Valid once finished() && ok(); moves the result out (call once).
    app::DynamicsBreakdown take_result();

    // The identity key: chart path + pro + difficulty.  Used to decide
    // whether the cached result is still valid.
    const std::string& key() const { return key_; }

private:
    void run();

    store::ChartLibraryEntry entry_;
    bool pro_;
    Difficulty difficulty_;
    std::string key_;
    std::optional<app::DynamicsBreakdown> result_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_DYNAMICS_LOAD_JOB_H
