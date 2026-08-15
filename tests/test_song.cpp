// Parity test for parse/song (the hysong.py port).
//
// Parses every chart in the corpus with the C++ parsers and diffs the resulting
// timestamp sequence (tick, chord code, solo/SP/fill flags) plus the auto-fill
// features against the golden `song` block. Covers .mid, .chart, and .sng.

#include "doctest.h"

#include <string>

#include "core/model.h"
#include "golden_util.h"
#include "parse/song.h"

#ifndef HYDRA_INPUT_DIR
#error "HYDRA_INPUT_DIR must be defined (see CMakeLists.txt)"
#endif

using namespace hydra;

namespace {

golden::json song_view(const Song& song) {
    golden::json out;
    out["features"] = golden::json::array();
    for (const std::string& f : song.features) out["features"].push_back(f);

    out["events"] = golden::json::array();
    for (const SongTimestamp& ts : song.sequence) {
        golden::json e;
        e["tick"] = ts.timecode.ticks();
        e["code"] = ts.chord.code();
        e["flag_solo"] = ts.flag_solo;
        e["flag_sp"] = ts.flag_sp;
        if (ts.activation_length.has_value())
            e["activation_length"] = *ts.activation_length;
        else
            e["activation_length"] = nullptr;
        out["events"].push_back(e);
    }
    return out;
}

// Report the first place two song views differ, for a legible failure message.
std::string first_diff(const golden::json& got, const golden::json& want) {
    if (got["features"] != want["features"])
        return "features differ";
    const auto& ge = got["events"];
    const auto& we = want["events"];
    if (ge.size() != we.size())
        return "event count " + std::to_string(ge.size()) + " vs " +
               std::to_string(we.size());
    for (size_t i = 0; i < ge.size(); ++i) {
        if (ge[i] != we[i])
            return "event " + std::to_string(i) + ": got " + ge[i].dump() +
                   " want " + we[i].dump();
    }
    return "identical";
}

}  // namespace

TEST_CASE("song parse matches golden over the corpus") {
    const golden::json idx = golden::index();

    int charts = 0, mismatches = 0;
    for (const auto& entry : idx) {
        const std::string relpath = entry["relpath"].get<std::string>();
        const std::string slug = entry["slug"].get<std::string>();

        golden::json doc = golden::chart(slug);
        if (!doc.contains("song")) continue;

        const std::string path = std::string(HYDRA_INPUT_DIR) + "/" + relpath;
        Song song = load_songpath(path, "Expert", true, true);
        golden::json got = song_view(song);

        ++charts;
        if (got != doc["song"]) {
            if (++mismatches <= 3)
                CHECK_MESSAGE(false, relpath << " -> "
                                             << first_diff(got, doc["song"]));
        }
    }

    CHECK(mismatches == 0);
    MESSAGE("checked " << charts << " charts");
}
