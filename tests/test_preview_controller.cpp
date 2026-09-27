// Tests for the details window's two background loads and the Preview
// controller's no-audio fallback. The loads are real threads on real corpus
// charts; nothing here opens a window or a real audio device.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "app/preview_view.h"
#include "audio/device.h"
#include "core/winstr.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "store/record_store.h"
#include "ui/dynamics_load_job.h"
#include "ui/preview_controller.h"
#include "ui/preview_load_job.h"

#ifndef HYDRA_TESTDATA_DIR
#error "HYDRA_TESTDATA_DIR must be defined (see CMakeLists.txt)"
#endif

using hydra::Difficulty;
using hydra::store::ChartLibraryEntry;
using hydra::ui::DynamicsLoadJob;
using hydra::ui::PreviewController;
using hydra::ui::PreviewLoadJob;

namespace {

// Wait up to 60 s for a job's worker to finish.
template <class Job>
void wait_finished(const Job& job) {
    for (int i = 0; i < 1200 && !job.finished(); ++i) Sleep(50);
    REQUIRE(job.finished());
}

ChartLibraryEntry entry_for(const std::string& notespath) {
    ChartLibraryEntry e;
    e.md5 = "prevctl";
    e.title = "Preview controller test";
    e.notespath = notespath;
    return e;
}

void copy_file_utf8(const std::string& from, const std::string& to) {
    std::vector<uint8_t> bytes = hydra::read_file_bytes(from);
    std::FILE* f = hydra::fopen_utf8(to, L"wb");
    REQUIRE(f != nullptr);
    if (!bytes.empty()) std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

// A chart folder that has audio: a corpus .chart plus the test sine as
// song.ogg. The GUI test library has no audio at all, so the no-device path
// can only be reached here.
std::string chart_with_audio() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"hydra_prevctl_" +
                       std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::string d = hydra::wide_to_utf8(dir);
    copy_file_utf8(corpus::first_chart_with_suffix(".chart"), d + "\\notes.chart");
    copy_file_utf8(std::string(HYDRA_TESTDATA_DIR) + "/audio/sine220.ogg", d + "\\song.ogg");
    return d + "\\notes.chart";
}

}  // namespace

// Closing the details window joins the load's thread on the UI thread. A
// job that ignored its cancel flag ran its whole parse, decode and mix first.
TEST_CASE("a cancelled Preview load stops before decoding") {
    PreviewLoadJob job(entry_for(corpus::first_chart_with_suffix(".chart")), true, true,
                       Difficulty::Expert, std::nullopt, 4);
    job.cancel();  // the window closed before the worker got going
    job.start();
    wait_finished(job);
    CHECK_FALSE(job.ok());
    CHECK(job.error() == "cancelled");
    CHECK(job.progress().step == PreviewLoadJob::Step::Reading);
}

TEST_CASE("a cancelled Dynamics load stops before counting") {
    DynamicsLoadJob job(entry_for(corpus::first_chart_with_suffix(".chart")), true,
                        Difficulty::Expert);
    job.cancel();
    job.start();
    wait_finished(job);
    CHECK_FALSE(job.ok());
    CHECK(job.error() == "cancelled");
}

// No output device is a warning, not a failure: the chart loads, the clock
// plays, and only the sound is missing.
TEST_CASE("with no audio device the Preview still loads, muted, with a warning") {
    PreviewController pc(nullptr, nullptr);
    pc.set_audio_device_factory(
        [](int, int, PreviewController::AudioSource)
            -> std::unique_ptr<hydra::audio::PreviewAudioDevice> {
            throw std::runtime_error("PreviewAudioDevice: ma_device_init failed");
        });
    pc.open(entry_for(chart_with_audio()), true, true, Difficulty::Expert, nullptr, "", 4);
    for (int i = 0; i < 1200 && pc.loading(); ++i) {
        pc.poll();
        Sleep(50);
    }
    REQUIRE_FALSE(pc.loading());

    CHECK(pc.has_audio());        // the stem decoded; only the device failed
    CHECK_FALSE(pc.has_error());  // so no "Preview failed"
    CHECK(pc.audio_warning() == "PreviewAudioDevice: ma_device_init failed");
    CHECK(pc.length_ms() > 0.0);  // the scene is there to draw
    pc.play();
    CHECK(pc.playing());          // the clock runs without a device

    pc.close();
    CHECK(pc.audio_warning().empty());
}

// Picking another path with the Preview open used to rebuild the scene inside
// open(), on the UI thread. It now builds on a job and lands on a later poll().
TEST_CASE("switching paths builds the new overlay off the UI thread") {
    using namespace hydra;
    using namespace hydra::app;
    AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 2;
    settings.ms_filter = 10.0;
    std::string chart;
    std::optional<AnalysisResult> analyzed;
    for (const std::string& p : corpus::chart_paths()) {
        try {
            AnalysisResult r = analyze_chart_file(p, settings);
            if (!r.record.paths.empty()) {
                chart = p;
                analyzed.emplace(std::move(r));
                break;
            }
        } catch (const std::exception&) {
        }
    }
    REQUIRE(analyzed.has_value());
    const Path& best = analyzed->record.best_path();
    const std::string best_key = path_overlay_key(&best);

    PreviewController pc(nullptr, nullptr);
    pc.open(entry_for(chart), true, true, Difficulty::Expert, nullptr, "", 4);
    for (int i = 0; i < 1200 && pc.loading(); ++i) {
        pc.poll();
        Sleep(50);
    }
    REQUIRE_FALSE(pc.loading());
    const std::string before = pc.overlay_path_key();

    // open() returns at once: the old overlay is still up, and nothing reloads.
    pc.open(entry_for(chart), true, true, Difficulty::Expert, &best, best_key, 4);
    CHECK(pc.overlay_path_key() == before);
    CHECK_FALSE(pc.loading());

    // The new overlay lands on a later poll. A failed build sets the error,
    // and then nothing more will land, so stop waiting.
    for (int i = 0; i < 1200 && pc.overlay_path_key().rfind(best_key, 0) != 0 && !pc.has_error();
         ++i) {
        pc.poll();
        Sleep(10);
    }
    CHECK_FALSE(pc.has_error());
    CHECK(pc.overlay_path_key().rfind(best_key, 0) == 0);
}
