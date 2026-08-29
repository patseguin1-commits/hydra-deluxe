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
        return true;
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
        // Join, tally, and framing all live behind generate_dm_report; the
        // job only fetches, forwards the counts, and writes the file.
        app::dm_report::GeneratedDmReport report =
            app::dm_report::generate_dm_report(store_, scores, chartmode_, lens_,
                                               username_);
        if (report.stats.total == 0)
            throw std::runtime_error("this user has no scores to compare");

        total_ = report.stats.total;
        matched_ = report.stats.matched;
        above_ = report.stats.above;
        unmatched_ = report.stats.unmatched;

        std::filesystem::path outpath = app::dm_report_html_path();
        app::write_report_file(outpath, report.html);
        if (open_when_done_ && !app::open_in_browser(outpath.wstring()))
            throw std::runtime_error("could not open " + outpath.u8string());
        return true;
    });
}

}  // namespace hydra::ui
