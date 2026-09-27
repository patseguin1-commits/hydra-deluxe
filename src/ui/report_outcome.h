// Where a report job put its page and whether the browser took it. Shared by
// ReportJob (ui/library_jobs.h) and DmReportJob (ui/dm_jobs.h). Saving the
// page is the job; opening it is a courtesy, so a browser that refuses is
// recorded here and never fails the job.

#ifndef HYDRA_UI_REPORT_OUTCOME_H
#define HYDRA_UI_REPORT_OUTCOME_H

#include <filesystem>
#include <string>

#include "app/report_files.h"

namespace hydra::ui {

// The one sentence a report job gives when the browser won't open its page.
// The finished strip (T13) puts the saved path and "Open report" around it.
inline constexpr const char* kReportOpenProblem =
    "Windows couldn't open the report in your browser.";

struct ReportOutcome {
    std::filesystem::path saved_path;
    bool opened = false;       // the browser took the page
    std::string open_problem;  // empty when it opened or wasn't asked to open
};

// Writes a finished page to `path` (a failed write throws, and the job fails)
// and, when `open`, hands it to the browser.
inline ReportOutcome publish_report(const std::filesystem::path& path, const std::string& html,
                                    bool open) {
    app::write_report_file(path, html);
    ReportOutcome out;
    out.saved_path = path;
    if (open) {
        out.opened = app::open_in_browser(path.wstring());
        if (!out.opened) out.open_problem = kReportOpenProblem;
    }
    return out;
}

}  // namespace hydra::ui

#endif  // HYDRA_UI_REPORT_OUTCOME_H
