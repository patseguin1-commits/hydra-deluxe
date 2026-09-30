// End-to-end tests for the three console tools (src/cli/). Each test copies
// the built exes into a fresh temp folder and runs them there, because the
// tools read hydra_settings.ini and hydra_rules.ini from their own folder: a
// folder with neither gives the app's defaults, whatever the developer's build
// folder holds. The exe paths come from CMakeLists.txt, which also builds the
// tools before the tests.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "app/config.h"
#include "core/winstr.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/graph.h"
#include "store/record_store.h"

#if !defined(HYDRA_BATCH_EXE) || !defined(HYDRA_REPORT_EXE) || \
    !defined(HYDRA_FILLCOMPARE_EXE)
#error "the CLI exe paths must be defined (see CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace {

const std::string kCh10 = hydra::engine_mode_stamp(hydra::FillDeadlineRule::Ch10);
const std::string kCh11 = hydra::engine_mode_stamp(hydra::FillDeadlineRule::Ch11);

struct RunResult {
    int exit_code = -1;
    std::string output;  // stdout and stderr, interleaved
};

// Runs `exe` with `args`, waits for it, and captures everything it prints.
RunResult run_exe(const fs::path& exe, const std::vector<std::string>& args) {
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    for (const std::string& a : args) cmd += L" \"" + hydra::utf8_to_wide(a) + L"\"";

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    REQUIRE(CreatePipe(&read_end, &write_end, &sa, 0));
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write_end);  // the child holds its own copy
    REQUIRE_MESSAGE(started, "could not start " << exe.u8string());

    RunResult r;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(read_end, buf, sizeof(buf), &got, nullptr) && got > 0)
        r.output.append(buf, got);
    CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    r.exit_code = static_cast<int>(code);
    return r;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The smallest corpus .chart that has drum notes and at least two Star Power
// phrases: every tool gets a real path to work with, and each run is quick.
std::string small_chart() {
    static const std::string chosen = [] {
        std::string best;
        uintmax_t best_size = UINTMAX_MAX;
        for (const std::string& p : corpus::chart_paths()) {
            if (p.size() < 6 || p.compare(p.size() - 6, 6, ".chart") != 0) continue;
            const uintmax_t size = fs::file_size(fs::u8path(p));
            if (size >= best_size) continue;
            const hydra::Song song = hydra::load_songpath(p, true, true);
            if (song.is_empty() || song.sp_phrase_count() < 2) continue;
            best = p;
            best_size = size;
        }
        return best;
    }();
    return chosen;
}

// A scratch folder holding a copy of each tool and one chart folder with a
// known song name. Removed again when the test ends.
struct CliSandbox {
    fs::path dir;
    fs::path batch, report, fillcompare;
    fs::path songs;  // holds one chart folder, "fixture"

    explicit CliSandbox(const char* name) {
        dir = fs::temp_directory_path() /
              ("hydra_cli_" + std::to_string(GetCurrentProcessId()) + "_" + name);
        fs::remove_all(dir);
        fs::create_directories(dir);
        batch = copy_tool(HYDRA_BATCH_EXE);
        report = copy_tool(HYDRA_REPORT_EXE);
        fillcompare = copy_tool(HYDRA_FILLCOMPARE_EXE);

        const std::string chart = small_chart();
        REQUIRE(!chart.empty());
        songs = dir / "songs";
        fs::create_directories(songs / "fixture");
        fs::copy_file(fs::u8path(chart), songs / "fixture" / "notes.chart");
        std::ofstream ini(songs / "fixture" / "song.ini", std::ios::binary);
        ini << "[song]\nname = CLI Fixture\nartist = Tester\ncharter = Nobody\n";
    }
    ~CliSandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path copy_tool(const char* built) {
        const fs::path from = fs::u8path(built).make_preferred();
        const fs::path to = dir / from.filename();
        fs::copy_file(from, to);
        return to;
    }
    std::string db(const char* name) const { return (dir / name).u8string(); }
    std::string folder() const { return songs.u8string(); }
};

}  // namespace

TEST_CASE("hydra_batch stamps a new database with the rule it ran under") {
    CliSandbox box("stamp");
    const std::string normal = box.db("ch11.db");
    RunResult r = run_exe(box.batch, {"--db", normal, box.folder()});
    INFO(r.output);
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.output, "Tester - CLI Fixture"));
    {
        hydra::store::RecordStore store(normal);
        CHECK(store.engine_mode() == std::optional<std::string>(kCh11));
        CHECK(store.counts().second == 1);
    }

    const std::string legacy = box.db("ch10.db");
    r = run_exe(box.batch, {"--legacy-fills", "--db", legacy, box.folder()});
    INFO(r.output);
    REQUIRE(r.exit_code == 0);
    hydra::store::RecordStore store(legacy);
    CHECK(store.engine_mode() == std::optional<std::string>(kCh10));
}

TEST_CASE("hydra_batch --legacy-fills refuses the tool's own hydra.db") {
    // The sandboxed exe's default database is hydra.db beside it: the file the
    // app itself would read.
    CliSandbox box("owndb");
    RunResult r = run_exe(box.batch, {"--legacy-fills", box.folder()});
    INFO(r.output);
    CHECK(r.exit_code == 2);
    CHECK(contains(r.output, "--db legacy.db"));
}

TEST_CASE("hydra_batch --reindex keeps a legacy database's stamp") {
    CliSandbox box("reindex");
    const std::string legacy = box.db("ch10.db");
    REQUIRE(run_exe(box.batch, {"--legacy-fills", "--db", legacy, box.folder()})
                .exit_code == 0);

    RunResult r = run_exe(box.batch, {"--reindex", "--db", legacy});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "Reindexed 1 records."));
    hydra::store::RecordStore store(legacy);
    CHECK(store.engine_mode() == std::optional<std::string>(kCh10));
}

TEST_CASE("hydra_batch refuses a run whose fill rule disagrees with the database") {
    CliSandbox box("mismatch");

    // A normal run into a 1.0 file.
    const std::string legacy = box.db("ch10.db");
    REQUIRE(run_exe(box.batch, {"--legacy-fills", "--db", legacy, box.folder()})
                .exit_code == 0);
    RunResult r = run_exe(box.batch, {"--redo", "--db", legacy, box.folder()});
    INFO(r.output);
    CHECK(r.exit_code == 2);
    CHECK(contains(r.output, "Add --legacy-fills"));
    {
        hydra::store::RecordStore store(legacy);
        CHECK(store.engine_mode() == std::optional<std::string>(kCh10));
        CHECK(store.counts().second == 1);
    }

    // A legacy run into a 1.1 file.
    const std::string normal = box.db("ch11.db");
    REQUIRE(run_exe(box.batch, {"--db", normal, box.folder()}).exit_code == 0);
    r = run_exe(box.batch, {"--legacy-fills", "--redo", "--db", normal, box.folder()});
    INFO(r.output);
    CHECK(r.exit_code == 2);
    CHECK(contains(r.output, "Drop --legacy-fills"));
    hydra::store::RecordStore store(normal);
    CHECK(store.engine_mode() == std::optional<std::string>(kCh11));
}

TEST_CASE("hydra_batch treats an unstamped database with records as Clone Hero 1.1") {
    // A file written before hydra_batch stamped: one normal result, no stamp.
    CliSandbox box("unstamped");
    const std::string db = box.db("old.db");
    {
        const hydra::app::Settings settings{};  // the defaults the sandboxed exe reads
        const std::string chart = (box.songs / "fixture" / "notes.chart").u8string();
        const std::string md5 = hydra::app::hash_chart_file(chart);
        hydra::app::AnalysisResult ar =
            hydra::app::analyze_chart_file(chart, settings.to_analysis_settings());
        hydra::store::RecordStore store(db);
        store.add_song(md5, "CLI Fixture", "Tester", "Nobody", ar.song);
        store.add_row(hydra::store::prepare_row(settings.record_key(md5), ar.record));
        REQUIRE(!store.engine_mode().has_value());
    }

    RunResult r = run_exe(box.batch, {"--legacy-fills", "--db", db, box.folder()});
    INFO(r.output);
    CHECK(r.exit_code == 2);
    hydra::store::RecordStore store(db);
    CHECK(!store.engine_mode().has_value());
}

TEST_CASE("hydra_batch reuses the GUI's scan cache") {
    CliSandbox box("cache");
    const std::string db = box.db("cached.db");
    {
        auto [items, errors] = hydra::app::discover_charts({box.folder()});
        REQUIRE(items.size() == 1);
        // The row a GUI scan writes, with only its title changed. The file
        // sizes and times still match, so a scan that reads the cache takes
        // this title; one that re-reads song.ini gets "CLI Fixture".
        hydra::store::RecordStore store(db);
        store.rebuild_chart_library({{items[0].md5, "Title From Cache", items[0].artist,
                                      items[0].charter, items[0].notespath,
                                      items[0].rootfolder, items[0].sig}});
    }

    RunResult r = run_exe(box.batch, {"--db", db, box.folder()});
    INFO(r.output);
    REQUIRE(r.exit_code == 0);
    CHECK(contains(r.output, "Tester - Title From Cache"));
    CHECK(!contains(r.output, "CLI Fixture"));
}

TEST_CASE("hydra_report writes a page for a filled database and says so for an empty one") {
    CliSandbox box("report");
    const std::string db = box.db("report.db");
    REQUIRE(run_exe(box.batch, {"--db", db, box.folder()}).exit_code == 0);

    const fs::path page = box.dir / "out" / "paths.html";
    RunResult r =
        run_exe(box.report, {"--db", db, "--out", page.u8string(), "--no-open"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "Wrote "));
    REQUIRE(fs::exists(page));
    std::ifstream f(page, std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    CHECK(contains(html, "CLI Fixture"));

    RunResult empty = run_exe(box.report, {"--db", box.db("empty.db"), "--out",
                                           (box.dir / "empty.html").u8string(),
                                           "--no-open"});
    INFO(empty.output);
    CHECK(empty.exit_code == 1);
    CHECK(contains(empty.output, "No records stored yet"));

    CHECK(run_exe(box.report, {"--bogus"}).exit_code == 2);
}

TEST_CASE("hydra_fillcompare compares a 1.0 and a 1.1 database") {
    CliSandbox box("fillcompare");
    const std::string ch10 = box.db("ch10.db"), ch11 = box.db("ch11.db");
    REQUIRE(run_exe(box.batch, {"--legacy-fills", "--db", ch10, box.folder()})
                .exit_code == 0);
    REQUIRE(run_exe(box.batch, {"--db", ch11, box.folder()}).exit_code == 0);

    const fs::path page = box.dir / "compare.html";
    RunResult r = run_exe(box.fillcompare, {"--old", ch10, "--new", ch11, "--out",
                                            page.u8string(), "--no-open"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "Compared 1 charts"));
    CHECK(!contains(r.output, "Warning"));
    CHECK(fs::exists(page));

    // Swapped files: each stamp disagrees with the side it was passed as, so
    // it warns. Each result carries its rule, so the 1.1 file has no 1.0
    // results to offer and the 1.0 file no 1.1 ones: nothing to compare.
    RunResult swapped = run_exe(box.fillcompare,
                                {"--old", ch11, "--new", ch10, "--out",
                                 (box.dir / "swapped.html").u8string(), "--no-open"});
    INFO(swapped.output);
    CHECK(swapped.exit_code == 1);
    CHECK(contains(swapped.output, "is stamped engine_mode=" + kCh11 + ", not " + kCh10));
    CHECK(contains(swapped.output, "No records to compare"));

    CHECK(run_exe(box.fillcompare, {"--old", ch10}).exit_code == 2);
}

TEST_CASE("hydra_fillcompare compares both rules out of one database") {
    // The app's "1.0 fills" setting keeps both rules' results in one file.
    CliSandbox box("fillcompare_one");
    const std::string db = box.db("both.db");
    {
        const std::string chart = (box.songs / "fixture" / "notes.chart").u8string();
        const std::string md5 = hydra::app::hash_chart_file(chart);
        hydra::store::RecordStore store(db);
        for (bool legacy : {false, true}) {
            hydra::app::Settings settings{};  // the defaults the sandboxed exe reads
            settings.legacy_fills = legacy;
            hydra::app::AnalysisResult ar =
                hydra::app::analyze_chart_file(chart, settings.to_analysis_settings());
            store.add_song(md5, "CLI Fixture", "Tester", "Nobody", ar.song);
            store.add_row(hydra::store::prepare_row(settings.record_key(md5), ar.record));
        }
        REQUIRE(store.counts().second == 2);
    }

    RunResult r = run_exe(box.fillcompare, {"--old", db, "--new", db, "--out",
                                            (box.dir / "one.html").u8string(), "--no-open"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "Compared 1 charts"));
    CHECK(contains(r.output, "0 only in 1.0, 0 only in 1.1"));
}

TEST_CASE("hydra_report reports a --legacy-fills database under the 1.0 rule") {
    // The app's own setting is 1.1 (the sandbox's defaults), but a file that
    // hydra_batch --legacy-fills filled holds only 1.0 results.
    CliSandbox box("report_legacy");
    const std::string db = box.db("ch10.db");
    REQUIRE(run_exe(box.batch, {"--legacy-fills", "--db", db, box.folder()}).exit_code == 0);

    const fs::path page = box.dir / "legacy.html";
    RunResult r = run_exe(box.report, {"--db", db, "--out", page.u8string(), "--no-open"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    REQUIRE(fs::exists(page));
    std::ifstream f(page, std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    CHECK(contains(html, "CLI Fixture"));
    CHECK(contains(html, "Clone Hero 1.0 fills"));
}
