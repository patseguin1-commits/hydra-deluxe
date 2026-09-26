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
    pc.open(entry_for(chart_with_audio()), true, true, Difficulty::Expert, nullptr, 4);
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
