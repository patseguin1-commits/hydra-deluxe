// PreviewLoadJob: the one background job behind the details modal's 3D
// Preview tab. Read by ui/preview_controller.{h,cpp}, which owns the job,
// pulls its finished scene + mixed audio, and hands the buffer to the audio
// device — nothing else in the UI touches it.

#ifndef HYDRA_UI_PREVIEW_LOAD_JOB_H
#define HYDRA_UI_PREVIEW_LOAD_JOB_H

#include <atomic>
#include <optional>
#include <string>

#include "app/preview_view.h"
#include "audio/decode.h"
#include "core/model.h"
#include "store/record_store.h"
#include "ui/job_base.h"

namespace hydra::ui {

// Prepares a chart for the 3D Preview off the render thread, mirroring
// AnalyzeJob: re-parse the notes and gather audio (resolve_preview_source),
// decode and mix every stem to one buffer, and build the PreviewScene. The
// controller pulls the finished scene + mixed audio and hands the buffer to an
// audio::Playhead. Parse + decode are heavy, so none of it runs on-frame.
class PreviewLoadJob : public ResultJobBase {
public:
    PreviewLoadJob(store::ChartLibraryEntry entry, bool pro, bool bass2x,
                   std::optional<Path> path);
    ~PreviewLoadJob() { shutdown(); }

    void start();

    struct Result {
        app::PreviewScene scene;
        audio::DecodedAudio mixed;
    };

    // Valid once finished() && ok(); moves the result out (call once).
    Result take_result();

    // Where the load is, for the Preview tab's loading bar. The steps run in
    // this order; Decoding is the long one on big charts, so it is the only
    // step that reports a count (stems done / stems total).
    enum class Step { Reading, Decoding, Mixing, Building };
    struct Progress {
        Step step = Step::Reading;
        int stems_done = 0;
        int stems_total = 0;
        // Overall 0..1 estimate: the three fixed steps get a slice each and
        // decoding spreads its slice across the stems.
        float fraction() const;
        // Short label for the current step ("Decoding audio 2/5").
        std::string label() const;
    };
    Progress progress() const;

private:
    void run();

    store::ChartLibraryEntry entry_;
    bool pro_;
    bool bass2x_;
    std::optional<Path> path_;
    std::optional<Result> result_;

    // Written by the worker, read by the render thread; each field is its own
    // atomic so a torn read can only be one step stale, never garbage.
    std::atomic<Step> step_{Step::Reading};
    std::atomic<int> stems_done_{0};
    std::atomic<int> stems_total_{0};
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_PREVIEW_LOAD_JOB_H
