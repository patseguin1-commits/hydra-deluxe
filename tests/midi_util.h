// Tiny MIDI builders shared by the parser tests.
//
// A unit case that wants to pin one parsing rule needs a real .mid byte
// stream, not a corpus file. smf() wraps a hand-written track into the
// smallest legal file the readers accept.

#ifndef HYDRA_TESTS_MIDI_UTIL_H
#define HYDRA_TESTS_MIDI_UTIL_H

#include <cstdint>
#include <string>
#include <vector>

namespace testmidi {

// Assemble a minimal single-track SMF (format 0, 480 tpqn) from raw track
// bytes (delta+event stream, without the MTrk header).
inline std::vector<uint8_t> smf(const std::vector<uint8_t>& track) {
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

// A delta-0 meta track-name event, so the song parser can find "PART DRUMS".
inline std::vector<uint8_t> track_name(const std::string& name) {
    std::vector<uint8_t> ev = {0x00, 0xFF, 0x03,
                               static_cast<uint8_t>(name.size())};
    ev.insert(ev.end(), name.begin(), name.end());
    return ev;
}

// A delta-0 meta text event, for [ENABLE_CHART_DYNAMICS] and friends.
inline std::vector<uint8_t> text_event(const std::string& text) {
    std::vector<uint8_t> ev = {0x00, 0xFF, 0x01,
                               static_cast<uint8_t>(text.size())};
    ev.insert(ev.end(), text.begin(), text.end());
    return ev;
}

// A delta-0 set_tempo meta. The song parser needs a tick-0 tempo before it can
// build a bpm map, so every hand-built drum track starts with one.
inline std::vector<uint8_t> set_tempo(uint32_t usec_per_beat = 500000) {
    return {0x00, 0xFF, 0x51, 0x03,
            static_cast<uint8_t>(usec_per_beat >> 16),
            static_cast<uint8_t>(usec_per_beat >> 8),
            static_cast<uint8_t>(usec_per_beat)};
}

// A delta-0 note_on with an explicit status byte on channel 0.
inline std::vector<uint8_t> note_on(uint8_t note, uint8_t velocity) {
    return {0x00, 0x90, note, velocity};
}

// End of track, delta 0.
inline std::vector<uint8_t> end_of_track() {
    return {0x00, 0xFF, 0x2F, 0x00};
}

// Concatenate event chunks into one track byte stream.
inline std::vector<uint8_t> concat(
    const std::vector<std::vector<uint8_t>>& parts) {
    std::vector<uint8_t> out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

}  // namespace testmidi

#endif  // HYDRA_TESTS_MIDI_UTIL_H
