// Unit tests for the library and leaderboard jobs: Pause, Stop, the batch
// clock and time-left estimate, the chart being analyzed, plain failure
// lines, report pages the browser refuses, and quiet cancels. The analyzer
// and the network are fakes, so nothing here reads a chart or goes online.

#include "doctest.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/analysis.h"
#include "app/report_files.h"
#include "net/dmbot_client.h"
#include "store/record_store.h"
#include "ui/dm_jobs.h"
#include "ui/library_jobs.h"
#include "ui/report_outcome.h"

using hydra::app::AnalysisResult;
using hydra::app::AnalysisSettings;
using hydra::app::BatchRun;
using hydra::store::ChartLibraryEntry;
using hydra::store::RecordStore;
using hydra::ui::BatchClock;
using hydra::ui::BatchJob;

namespace {

using namespace std::chrono_literals;

template <class Pred>
bool wait_until(Pred pred, std::chrono::milliseconds limit = 10s) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

std::vector<ChartLibraryEntry> fake_charts(int n) {
    std::vector<ChartLibraryEntry> charts;
    for (int i = 0; i < n; ++i) {
        ChartLibraryEntry e;
        e.md5 = "fake" + std::to_string(i);
        e.title = "fake " + std::to_string(i);
        e.artist = "artist " + std::to_string(i);
        e.notespath = "fake_" + std::to_string(i) + ".chart";
        charts.push_back(e);
    }
    return charts;
}

BatchRun test_run() {
    BatchRun run;
    run.chartmode = "jobs-test";
    return run;
}

// A chart that counts itself, waits for `release`, then fails with `error`.
hydra::app::ChartAnalyzer gated_failure(std::atomic<int>& started, std::atomic<bool>& release,
                                        std::string error) {
    return [&started, &release, error](const std::string&, const AnalysisSettings&,
                                       const std::function<void(float)>&) -> AnalysisResult {
        ++started;
        while (!release.load()) std::this_thread::sleep_for(1ms);
        throw std::runtime_error(error);
    };
}

}  // namespace

TEST_CASE("jobs: the batch clock leaves paused time out") {
    BatchClock clock;
    clock.start(10.0);
    clock.pause(15.0);
    CHECK(clock.elapsed_s(20.0) == doctest::Approx(5.0));
    CHECK(clock.paused());
    clock.resume(25.0);
    CHECK(clock.elapsed_s(30.0) == doctest::Approx(10.0));
    clock.finish(40.0);
    CHECK(clock.elapsed_s(100.0) == doctest::Approx(20.0));

    BatchClock stopped_while_paused;
    stopped_while_paused.start(0.0);
    stopped_while_paused.pause(5.0);
    stopped_while_paused.finish(9.0);
    CHECK(stopped_while_paused.elapsed_s(50.0) == doctest::Approx(5.0));
    CHECK_FALSE(stopped_while_paused.paused());
}

TEST_CASE("jobs: time left needs three finished charts") {
    CHECK_FALSE(hydra::ui::batch_eta_s(30.0, 2, 10).has_value());
    REQUIRE(hydra::ui::batch_eta_s(30.0, 3, 10).has_value());
    CHECK(*hydra::ui::batch_eta_s(30.0, 3, 10) == doctest::Approx(70.0));
    CHECK(*hydra::ui::batch_eta_s(30.0, 10, 10) == doctest::Approx(0.0));
    CHECK_FALSE(hydra::ui::batch_eta_s(30.0, 3, 0).has_value());
}

TEST_CASE("jobs: pause lets the chart in flight finish and starts no new one") {
    std::atomic<int> started{0};
    std::atomic<bool> release{false};
    RecordStore store(":memory:");
    BatchJob job(fake_charts(3), test_run(), store, /*redo=*/false);
    job.set_analyzer_for_test(gated_failure(started, release, "MD5 hashing failed"),
                              /*workers=*/1);
    job.start();
    REQUIRE(wait_until([&] { return started.load() == 1; }));

    job.pause();
    release = true;
    REQUIRE(wait_until([&] { return job.snapshot().completed == 1; }));
    std::this_thread::sleep_for(200ms);
    CHECK(started.load() == 1);  // nothing new started while paused
    BatchJob::Snapshot paused = job.snapshot();
    CHECK(paused.paused);
    CHECK_FALSE(paused.eta_s.has_value());

    job.resume();
    REQUIRE(wait_until([&] { return job.snapshot().finished; }));
    BatchJob::Snapshot done = job.snapshot();
    CHECK(started.load() == 3);
    CHECK(done.completed == 3);
    CHECK(done.failed == 3);
    CHECK_FALSE(done.paused);
}

TEST_CASE("jobs: stop while paused ends the run and fails nothing") {
    std::atomic<int> started{0};
    std::atomic<bool> release{true};
    RecordStore store(":memory:");
    BatchJob job(fake_charts(4), test_run(), store, /*redo=*/false);
    job.set_analyzer_for_test(gated_failure(started, release, "MD5 hashing failed"),
                              /*workers=*/2);
    job.pause();
    job.start();
    std::this_thread::sleep_for(200ms);
    CHECK(started.load() == 0);
    CHECK(job.snapshot().paused);

    job.stop();
    REQUIRE(wait_until([&] { return job.snapshot().finished; }, 5s));
    BatchJob::Snapshot s = job.snapshot();
    CHECK(started.load() == 0);
    CHECK(s.completed == 0);
    CHECK(s.failed == 0);
    CHECK(s.failures.empty());
    CHECK(job.is_cancelled());
}

TEST_CASE("jobs: the snapshot names the chart being analyzed and freezes its clock at the end") {
    std::atomic<int> started{0};
    std::atomic<bool> release{false};
    RecordStore store(":memory:");
    BatchJob job(fake_charts(1), test_run(), store, /*redo=*/false);
    job.set_analyzer_for_test(gated_failure(started, release, "MD5 hashing failed"),
                              /*workers=*/1);
    job.start();
    REQUIRE(wait_until([&] { return started.load() == 1; }));

    BatchJob::Snapshot running = job.snapshot();
    CHECK(running.current_title == "fake 0");
    CHECK(running.current_artist == "artist 0");
    CHECK(running.elapsed_s >= 0.0);
    CHECK_FALSE(running.eta_s.has_value());

    release = true;
    REQUIRE(wait_until([&] { return job.snapshot().finished; }));
    BatchJob::Snapshot done = job.snapshot();
    CHECK(done.current_title.empty());
    CHECK(done.current_artist.empty());
    std::this_thread::sleep_for(50ms);
    CHECK(job.snapshot().elapsed_s == done.elapsed_s);
}

TEST_CASE("jobs: a failed chart reads in plain words and keeps the raw text") {
    std::atomic<int> started{0};
    std::atomic<bool> release{true};
    RecordStore store(":memory:");
    BatchJob job(fake_charts(1), test_run(), store, /*redo=*/false);
    job.set_analyzer_for_test(
        gated_failure(started, release, "cannot open file: C:\\Songs\\x\\notes.chart"), 1);
    job.start();
    REQUIRE(wait_until([&] { return job.snapshot().finished; }));
    BatchJob::Snapshot s = job.snapshot();
    REQUIRE(s.failures.size() == 1);
    REQUIRE(s.failure_details.size() == 1);
    CHECK(s.failures[0] ==
          "fake 0: Hydra couldn't open the song file. It may have been moved or deleted; "
          "run Scan library to update the library.");
    CHECK(s.failure_details[0] == "fake 0: cannot open file: C:\\Songs\\x\\notes.chart");
}

TEST_CASE("jobs: a report the browser refuses is saved, not failed") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "hydra_jobs_test_report";
    std::filesystem::create_directories(dir);
    const std::filesystem::path page = dir / "report.html";

    int opens = 0;
    bool browser_ok = false;
    hydra::app::set_open_in_browser([&](const std::wstring&) {
        ++opens;
        return browser_ok;
    });

    hydra::ui::ReportOutcome refused = hydra::ui::publish_report(page, "<p>x</p>", true);
    CHECK(std::filesystem::exists(page));
    CHECK(opens == 1);
    CHECK(refused.saved_path == page);
    CHECK_FALSE(refused.opened);
    CHECK(refused.open_problem == "Windows couldn't open the report in your browser.");

    browser_ok = true;
    hydra::ui::ReportOutcome opened = hydra::ui::publish_report(page, "<p>y</p>", true);
    CHECK(opens == 2);
    CHECK(opened.opened);
    CHECK(opened.open_problem.empty());

    hydra::ui::ReportOutcome quiet = hydra::ui::publish_report(page, "<p>z</p>", false);
    CHECK(opens == 2);  // auto-open off: the browser is never asked
    CHECK_FALSE(quiet.opened);
    CHECK(quiet.open_problem.empty());

    hydra::app::set_open_in_browser({});
    std::filesystem::remove_all(dir);
}

TEST_CASE("jobs: a cancelled leaderboard fetch is not an error") {
    int opens = 0;
    hydra::app::set_open_in_browser([&](const std::wstring&) {
        ++opens;
        return true;
    });
    RecordStore store(":memory:");

    // What the WinHTTP fetch does when cancel lands between read chunks.
    hydra::net::set_fetcher([](const std::string&, const std::atomic<bool>* cancel) -> std::string {
        while (!cancel->load()) std::this_thread::sleep_for(1ms);
        throw std::runtime_error("cancelled");
    });
    hydra::ui::DmReportJob thrown(store, "1", "someone", "jobs-test", hydra::store::Lens{}, true);
    thrown.start();
    thrown.cancel();
    REQUIRE(wait_until([&] { return thrown.finished(); }));
    CHECK_FALSE(thrown.ok());
    CHECK(thrown.message().empty());

    // The server answers after the cancel: nothing is written or opened.
    hydra::net::set_fetcher([](const std::string&, const std::atomic<bool>* cancel) -> std::string {
        while (!cancel->load()) std::this_thread::sleep_for(1ms);
        return "{}";
    });
    hydra::ui::DmReportJob late(store, "1", "someone", "jobs-test", hydra::store::Lens{}, true);
    late.start();
    late.cancel();
    REQUIRE(wait_until([&] { return late.finished(); }));
    CHECK_FALSE(late.ok());
    CHECK(late.message().empty());
    CHECK(opens == 0);

    hydra::net::set_fetcher({});
    hydra::app::set_open_in_browser({});
}

TEST_CASE("jobs: a failed leaderboard fetch says what to do") {
    hydra::net::set_fetcher([](const std::string&, const std::atomic<bool>*) -> std::string {
        throw std::runtime_error("could not send the request (error 12029)");
    });
    hydra::ui::DmFetchUsersJob job;
    job.start();
    REQUIRE(wait_until([&] { return job.finished(); }));
    CHECK_FALSE(job.ok());
    CHECK(job.message() ==
          "Hydra couldn't reach dmleaderboards. Check your internet connection and try again.");
    CHECK(job.error() == "could not send the request (error 12029)");
    hydra::net::set_fetcher({});
}
