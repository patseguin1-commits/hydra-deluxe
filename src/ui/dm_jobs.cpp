#include "ui/dm_jobs.h"

#include <filesystem>
#include <stdexcept>

#include "app/dm_report.h"
#include "app/report_files.h"
#include "net/dmbot_client.h"

namespace hydra::ui {

// ---- DmFetchUsersJob --------------------------------------------------

void DmFetchUsersJob::start() { spawn([this] { run(); }); }

void DmFetchUsersJob::run() {
    run_guarded([this] {
        users_ = net::fetch_users(net::kDefaultApiBase, &cancel_);
        // A cancel can land while the server is still answering; the fetch
        // then returns normally. The picker asked to stop, so stop.
        return !is_cancelled();
    });
}

// ---- DmReportJob ------------------------------------------------------

DmReportJob::DmReportJob(store::RecordStore& store, std::string discord_id, std::string username,
                         std::string chartmode, store::Lens lens, bool open_when_done)
    : store_(store),
      discord_id_(std::move(discord_id)),
      username_(std::move(username)),
      chartmode_(std::move(chartmode)),
      lens_(lens),
      open_when_done_(open_when_done) {}

void DmReportJob::start() { spawn([this] { run(); }); }

void DmReportJob::run() {
    run_guarded([this] {
        std::vector<net::DmScore> scores =
            net::fetch_scores(discord_id_, net::kDefaultApiBase, &cancel_);
        // The fetch only checks cancel between read chunks, so a cancel
        // pressed while the server was waking up arrives here. Stop before
        // anything is written or opened (audit B3).
        if (is_cancelled()) return false;
        // Join, tally, and framing all live behind generate_dm_report; the
        // job only fetches, forwards the counts, and writes the file.
        app::dm_report::GeneratedDmReport report =
            app::dm_report::generate_dm_report(store_, scores, chartmode_, lens_,
                                               username_);
        if (report.stats.total == 0)
            throw std::runtime_error("this user has no scores to compare");

        stats_ = report.stats;

        // A browser that won't open the page is not a failed report.
        outcome_ = publish_report(app::dm_report_html_path(), report.html, open_when_done_);
        return true;
    });
}

}  // namespace hydra::ui
