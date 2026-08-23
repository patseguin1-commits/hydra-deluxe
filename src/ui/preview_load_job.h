// PreviewLoadJob: the one background job behind the details modal's 3D
// Preview tab. Read by ui/preview_controller.{h,cpp}, which owns the job,
// pulls its finished scene + mixed audio, and hands the buffer to the audio
// device — nothing else in the UI touches it.

#ifndef HYDRA_UI_PREVIEW_LOAD_JOB_H
#define HYDRA_UI_PREVIEW_LOAD_JOB_H

#include <optional>

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

private:
    void run();

    store::ChartLibraryEntry entry_;
    bool pro_;
    bool bass2x_;
    std::optional<Path> path_;
    std::optional<Result> result_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_PREVIEW_LOAD_JOB_H
