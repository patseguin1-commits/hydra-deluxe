// The two dmleaderboards jobs: fetching the ladder for the user picker
// (DmFetchUsersJob) and building one user's comparison report
// (DmReportJob). AppState owns both; library_dialogs.cpp's picker and
// finished modal are the only readers. Both wrap net/dmbot_client.h calls, which the
// free-tier backend can leave hanging for tens of seconds.

#ifndef HYDRA_UI_DM_JOBS_H
#define HYDRA_UI_DM_JOBS_H

#include <filesystem>
#include <string>
#include <vector>

#include "app/dm_report.h"
#include "net/dmbot_client.h"
#include "store/record_store.h"
#include "ui/job_base.h"
#include "ui/report_outcome.h"

namespace hydra::ui {

// ---- DmFetchUsersJob --------------------------------------------------

// Fetches the dmleaderboards ladder (GET /api/all-users) for the searchable
// picker. Off the render thread because the render.com backend cold-starts —
// the first request after an idle spell can take tens of seconds.
class DmFetchUsersJob : public ResultJobBase {
public:
    DmFetchUsersJob() = default;
    ~DmFetchUsersJob() { shutdown(); }

    void start();

    // Valid once finished() && ok(); the picker takes ownership (call once).
    std::vector<net::DmUser>& users() { return users_; }

private:
    void run();
    std::vector<net::DmUser> users_;
};

// ---- DmReportJob ------------------------------------------------------

// Fetches one user's scores (GET /api/user/{id}/scores), joins them against the
// store by chart hash, writes the HTML comparison report and opens it. Same
// off-thread + cold-start handling as DmFetchUsersJob. Where the page lands and
// how it reaches the browser live in app/report_files.h.
class DmReportJob : public ResultJobBase {
public:
    // open_when_done: the user's "Open report automatically" setting, same as
    // ReportJob (the finished modal offers an "Open report again" button).
    DmReportJob(store::RecordStore& store, std::string discord_id, std::string username,
                std::string chartmode, store::Lens lens, bool open_when_done);
    ~DmReportJob() { shutdown(); }

    void start();

    // Headline join counts for the finished modal; valid once ok().
    int total() const { return total_; }
    int matched() const { return matched_; }
    int above() const { return above_; }
    int unmatched() const { return unmatched_; }
    // Every count the comparison produced, whatever app/dm_report.h names
    // them (T3 splits "not in your library" in two). Valid once ok().
    const app::dm_report::DmReportStats& stats() const { return stats_; }

    // Valid once finished() && ok(): where the page was written, whether the
    // browser opened it, and why not when it was asked to and didn't.
    const std::filesystem::path& saved_path() const { return outcome_.saved_path; }
    bool opened() const { return outcome_.opened; }
    const std::string& open_problem() const { return outcome_.open_problem; }

private:
    void run();
    store::RecordStore& store_;
    std::string discord_id_;
    std::string username_;
    std::string chartmode_;
    store::Lens lens_;
    bool open_when_done_;
    int total_ = 0, matched_ = 0, above_ = 0, unmatched_ = 0;
    app::dm_report::DmReportStats stats_;
    ReportOutcome outcome_;
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_DM_JOBS_H
