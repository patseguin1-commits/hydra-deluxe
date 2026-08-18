// Parity + unit tests for parse/midi (the hymidi port).
//
// Parity: for every .mid in the golden, re-read the file and build the same
// absolute-tick event view Python's test_midi_parity builds, then diff it
// against the golden midi block (ticks_per_beat, track names, event stream).
// Unit: the edge cases the corpus may not contain but a user's library might.

#include "doctest.h"

#include <cstdint>
#include <string>
#include <vector>

#include "parse/midi.h"
#include "golden_util.h"

#ifndef HYDRA_INPUT_DIR
#error "HYDRA_INPUT_DIR must be defined (see CMakeLists.txt)"
#endif

namespace {

using golden::json;

// The same absolute-tick view as tools/gen_golden.py:event_view / the Python
// test_midi_parity, built as JSON so it diffs directly against the golden.
json event_view(const hydra::MidiFile& mid) {
    json view = json::array();
    for (const auto& track : mid.tracks) {
        int64_t tick = 0;
        json events = json::array();
        for (const auto& m : track.messages) {
            tick += m.time;
            if (m.type == "note_on") {
                events.push_back({tick, "note_on", m.note, m.velocity});
            } else if (m.type == "note_off") {
                events.push_back({tick, "note_off", m.note, m.velocity});
            } else if (m.type == "set_tempo") {
                events.push_back({tick, "set_tempo", m.tempo});
            } else if (m.type == "time_signature") {
                events.push_back({tick, "time_signature",
                                  m.numerator, m.denominator});
            } else if (m.str_attr == hydra::Message::StrAttr::Text) {
                events.push_back({tick, m.type, "text", m.str});
            } else if (m.str_attr == hydra::Message::StrAttr::Name) {
                events.push_back({tick, m.type, "name", m.str});
            }
        }
        view.push_back(std::move(events));
    }
    return view;
}

// Assemble a minimal single-track SMF (format 0, 480 tpqn) from raw track
// bytes (delta+event stream, without the MTrk header).
std::vector<uint8_t> smf(const std::vector<uint8_t>& track) {
    std::vector<uint8_t> d = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,  // div=480
        'M', 'T', 'r', 'k',
    };
    uint32_t n = static_cast<uint32_t>(track.size());
    d.push_back(uint8_t(n >> 24)); d.push_back(uint8_t(n >> 16));
    d.push_back(uint8_t(n >> 8));  d.push_back(uint8_t(n));
    d.insert(d.end(), track.begin(), track.end());
    return d;
}

}  // namespace

TEST_CASE("midi: event streams match golden across the corpus") {
    const json idx = golden::index();
    size_t mids = 0;
    for (const auto& entry : idx) {
        if (!entry["has_midi"].get<bool>()) continue;
        const std::string slug = entry["slug"].get<std::string>();
        const std::string relpath = entry["relpath"].get<std::string>();
        const json doc = golden::chart(slug);
        const json& gm = doc["midi"];

        hydra::MidiFile mid =
            hydra::MidiFile::from_file(std::string(HYDRA_INPUT_DIR) + "/" + relpath);
        ++mids;

        CHECK_MESSAGE(mid.ticks_per_beat == gm["ticks_per_beat"].get<int>(),
                      relpath << ": ticks_per_beat");

        json names = json::array();
        for (const auto& t : mid.tracks) names.push_back(t.name);
        CHECK_MESSAGE(names == gm["track_names"], relpath << ": track names");

        CHECK_MESSAGE(event_view(mid) == gm["events"], relpath << ": events");
    }
    REQUIRE(mids > 0);
    MESSAGE("midi parity: " << mids << " files");
}

TEST_CASE("midi: running status and zero-velocity note_on") {
    // note_on vel 0 is how most charts spell note_off; consecutive same-status
    // messages exercise running status (the status byte omitted).
    std::vector<uint8_t> track = {
        0x00, 0x90, 0x60, 0x64,   // note_on note 96 vel 100
        0x78, 0x60, 0x00,         // +120, running status, note 96 vel 0
        0x00, 0x61, 0x5A,         // +0, running status, note 97 vel 90
        0x00, 0xFF, 0x2F, 0x00,   // end of track
    };
    hydra::MidiFile mid(smf(track));
    json expected = json::array({ json::array({
        json::array({0, "note_on", 96, 100}),
        json::array({120, "note_on", 96, 0}),
        json::array({120, "note_on", 97, 90}),
    }) });
    CHECK(event_view(mid) == expected);
}

TEST_CASE("midi: running status survives a meta event") {
    // Rock Band rip MIDIs omit the status byte on the channel event right
    // after a meta event. The SMF spec forbids the pattern, but mido reads it:
    // only channel statuses become the running status; 0xFF never does. If a
    // meta byte clobbered it, every note after the first [mix ...] text event
    // would be consumed as meta garbage and the drum track would parse empty.
    std::vector<uint8_t> track = {
        0x00, 0x90, 0x60, 0x64,                    // note_on note 96 vel 100
        0x30, 0xFF, 0x01, 0x03, 'm', 'i', 'x',     // +48 text "mix"
        0x30, 0x61, 0x5A,                          // +48, running status through
                                                   // the meta: note 97 vel 90
        0x00, 0xFF, 0x2F, 0x00,                    // end of track
    };
    hydra::MidiFile mid(smf(track));
    json expected = json::array({ json::array({
        json::array({0, "note_on", 96, 100}),
        json::array({48, "text", "text", "mix"}),
        json::array({96, "note_on", 97, 90}),
    }) });
    CHECK(event_view(mid) == expected);
}

TEST_CASE("midi: first track_name wins over later ones") {
    // mido's MidiTrack.name is the first track_name meta. Some drum charts
    // carry extra 0x03 metas mid-track ("Drums" after "PART DRUMS"); if the
    // last one won, hysong's exact name match would skip the whole track.
    std::vector<uint8_t> track = {
        0x00, 0xFF, 0x03, 0x0A, 'P', 'A', 'R', 'T', ' ', 'D', 'R', 'U', 'M', 'S',
        0x00, 0x90, 0x60, 0x64,                    // note_on note 96 vel 100
        0x00, 0xFF, 0x03, 0x05, 'D', 'r', 'u', 'm', 's',
        0x00, 0xFF, 0x2F, 0x00,                    // end of track
    };
    hydra::MidiFile mid(smf(track));
    REQUIRE(mid.tracks.size() == 1);
    CHECK(mid.tracks[0].name == "PART DRUMS");
}

TEST_CASE("midi: sysex and skipped metas do not lose time") {
    std::vector<uint8_t> track = {
        0x00, 0x90, 0x60, 0x64,               // note_on note 96 vel 100
        0x30, 0xF0, 0x04, 0x01, 0x02, 0x03, 0xF7,  // +48 sysex (skipped)
        0x30, 0xFF, 0x06, 0x03, 'm', 'i', 'x',     // +48 marker "mix"
        0x30, 0x80, 0x60, 0x00,               // +48 note_off note 96
        0x00, 0xFF, 0x2F, 0x00,               // end of track
    };
    hydra::MidiFile mid(smf(track));
    json expected = json::array({ json::array({
        json::array({0, "note_on", 96, 100}),
        json::array({96, "marker", "text", "mix"}),   // sysex delta rolled in
        json::array({144, "note_off", 96, 0}),
    }) });
    CHECK(event_view(mid) == expected);
}

TEST_CASE("midi: track_name is not exposed as text") {
    // hysong matches MetaMessage(text=...) for disco/dynamics markers; a
    // track_name must carry `name`, not `text`, or every title would be offered
    // to those regexes.
    std::vector<uint8_t> track = {
        0x00, 0xFF, 0x03, 0x0A, 'P', 'A', 'R', 'T', ' ', 'D', 'R', 'U', 'M', 'S',
        0x00, 0x90, 0x60, 0x64,
        0x00, 0xFF, 0x2F, 0x00,
    };
    hydra::MidiFile mid(smf(track));
    REQUIRE(mid.tracks.size() == 1);
    CHECK(mid.tracks[0].name == "PART DRUMS");

    bool found = false;
    for (const auto& m : mid.tracks[0].messages) {
        if (m.type == "track_name") {
            found = true;
            CHECK(m.str_attr == hydra::Message::StrAttr::Name);
            CHECK(m.str_attr != hydra::Message::StrAttr::Text);
        }
    }
    CHECK(found);
}

TEST_CASE("midi: non-MIDI input is rejected") {
    std::vector<uint8_t> junk(40, 'x');
    CHECK_THROWS_AS((hydra::MidiFile(junk)), hydra::MidiError);
}

TEST_CASE("midi: SMPTE division is rejected") {
    // A negative division byte pair signals SMPTE timing.
    std::vector<uint8_t> d = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0xE8, 0x00,  // div < 0
    };
    CHECK_THROWS_AS((hydra::MidiFile(d)), hydra::MidiError);
}
