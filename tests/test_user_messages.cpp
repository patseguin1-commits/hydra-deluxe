// Unit tests for app/user_messages: every known internal error becomes a
// short message that says what happened and what to do, and the raw text
// stays available for a details line.

#include "doctest.h"

#include <new>
#include <stdexcept>
#include <string>

#include "app/rules_file.h"
#include "app/user_messages.h"
#include "core/model.h"
#include "parse/midi.h"
#include "store/serialize.h"

using hydra::app::plain_error;
using hydra::app::plain_error_detail;
using hydra::app::plain_error_text;

namespace {

const std::string kDatabaseWrite =
    "Hydra couldn't save to its database (hydra.db). Check that the disk isn't full and "
    "that no other copy of Hydra is running, then try again.";
const std::string kChartUnreadable =
    "Hydra couldn't read this chart file. It may be damaged or in a format Hydra doesn't "
    "support; try downloading the song again.";
const std::string kSongFileMissing =
    "Hydra couldn't open the song file. It may have been moved or deleted; run Scan "
    "library to update the library.";
const std::string kNetUnreachable =
    "Hydra couldn't reach dmleaderboards. Check your internet connection and try again.";

}  // namespace

TEST_CASE("user_messages: database errors say to check the disk") {
    for (const char* raw : {"add_song failed: disk I/O error",
                            "add_row path failed: database is locked",
                            "put_dynamics failed: disk I/O error",
                            "meta_set failed: disk I/O error",
                            "reindex failed: disk I/O error",
                            "rebuild_chart_library failed: disk I/O error",
                            "sqlite exec failed: disk I/O error",
                            "prepare failed: no such table: records"}) {
        CAPTURE(raw);
        CHECK(plain_error(std::runtime_error(raw)) == kDatabaseWrite);
    }
    CHECK(plain_error(std::runtime_error("failed to open database 'C:\\x\\hydra.db': unable to open")) ==
          "Hydra couldn't open its database (hydra.db). Check that no other copy of Hydra is "
          "running and that the Hydra folder isn't read-only.");
}

TEST_CASE("user_messages: a missing or unreadable song file") {
    CHECK(plain_error(std::runtime_error("cannot open file: C:\\Songs\\x\\notes.chart")) ==
          kSongFileMissing);
    CHECK(plain_error(hydra::MidiError("cannot open MIDI file: C:\\Songs\\x\\notes.mid")) ==
          kSongFileMissing);
    CHECK(plain_error(std::runtime_error("MD5 hashing failed")) ==
          "Windows couldn't read a song file to identify it. Restart Hydra and run Scan "
          "library again.");
    CHECK(plain_error(hydra::ChartFileError("Duplicate note.")) == kChartUnreadable);
    CHECK(plain_error(hydra::MidiError("not a MIDI file: missing MThd header")) ==
          kChartUnreadable);
    CHECK(plain_error(std::runtime_error("Truncated SNG file.")) == kChartUnreadable);
    CHECK(plain_error(std::runtime_error("unexpected chart type: C:\\x\\song.txt")) ==
          kChartUnreadable);
    // A chart-file error Hydra doesn't know yet still reads as a chart problem.
    CHECK(plain_error(hydra::ChartFileError("a brand new parse failure")) == kChartUnreadable);
}

TEST_CASE("user_messages: the no-notes message is already plain and passes through") {
    const std::string msg = "No Expert Pro Drums notes in this chart.";
    CHECK(plain_error(hydra::ChartFileError(msg)) == msg);
    CHECK(plain_error_text(msg) == msg);
}

TEST_CASE("user_messages: a broken search is reported as Hydra's bug") {
    CHECK(plain_error(std::runtime_error("search reached a broken state")) ==
          "The analysis failed on this chart because of a bug in Hydra. Please report it "
          "with the song's name.");
}

TEST_CASE("user_messages: leaderboard errors") {
    CHECK(plain_error(std::runtime_error("could not send the request (error 12029)")) ==
          kNetUnreachable);
    CHECK(plain_error(std::runtime_error("could not connect to the leaderboard (error 12007)")) ==
          kNetUnreachable);
    CHECK(plain_error(std::runtime_error("no response from the leaderboard (error 12002)")) ==
          "dmleaderboards didn't answer in time. Its server may be waking up; try again in "
          "a minute.");
    CHECK(plain_error(std::runtime_error("leaderboard returned HTTP 503")) ==
          "dmleaderboards returned an error (HTTP 503). Try again later.");
    CHECK(plain_error(std::runtime_error("the leaderboard sent a response Hydra couldn't read")) ==
          "dmleaderboards sent a reply Hydra couldn't read. Try again later.");
    CHECK(plain_error(std::runtime_error("this user has no scores to compare")) ==
          "This player has no drum scores on dmleaderboards to compare.");
}

TEST_CASE("user_messages: report, rules, stored results, memory") {
    CHECK(plain_error(std::runtime_error("no records stored yet")) ==
          "There are no analyzed songs to put in a report yet. Analyze some songs first.");
    CHECK(plain_error(std::runtime_error("cannot write C:\\Users\\x\\Documents\\Hydra\\hydra_paths.html")) ==
          "Hydra couldn't save the report file. Check that the disk isn't full and the "
          "report folder isn't read-only.");
    CHECK(plain_error(hydra::app::RulesFileError("hydra_rules.ini:3: unknown key \"x\"")) ==
          "hydra_rules.ini has a line Hydra can't read. Fix or delete that line, then "
          "restart Hydra.");
    CHECK(plain_error(hydra::store::SerializeError("truncated blob")) ==
          "A saved result couldn't be read. Re-analyze this song to replace it.");
    CHECK(plain_error(std::bad_alloc()) ==
          "Hydra ran out of memory on this chart. Close other programs and try again.");
    CHECK(plain_error(std::runtime_error("decode_audio: opus_decode failed")) ==
          "Hydra couldn't decode this song's audio files. They may be damaged; try "
          "downloading the song again.");
    CHECK(plain_error(std::runtime_error("PreviewRenderer: missing texture x.png")) ==
          "Some of Hydra's Preview files are missing. Reinstall Hydra to restore them.");
}

TEST_CASE("user_messages: anything else falls back, and the detail keeps the raw text") {
    const std::runtime_error odd("prepare_row: key asks for sp_cap 5");
    CHECK(plain_error(odd) == hydra::app::kSomethingWentWrong);
    CHECK(std::string(hydra::app::kSomethingWentWrong) ==
          "Something went wrong. Try again, and if it keeps happening, report it with the "
          "details below.");
    CHECK(plain_error_detail(odd) == "prepare_row: key asks for sp_cap 5");
    CHECK(plain_error_text("cancelled") == "Stopped before it finished.");
}
