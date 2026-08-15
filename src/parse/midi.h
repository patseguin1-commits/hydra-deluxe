// MIDI reader — the C++ port of hydra/hymidi.py.
//
// A purpose-built reader for exactly what the chart parser consumes: it pulls
// the file into one byte buffer and walks it with an index, emitting only the
// events hysong can act on. Events it cannot match are dropped and their delta
// rolled into the next emitted event, so absolute tick totals are preserved.
//
// Shape mirrors hymidi so the parser port (Phase 2) maps across directly:
//     MidiFile     ticks_per_beat, tracks
//     MidiTrack    name, messages
//     Message      channel note_on/note_off, and the metas hysong reads
//
// The load-bearing meta-string split is preserved: text-metas carry a string
// under StrAttr::Text, name-metas under StrAttr::Name, and 0x08 carries neither
// — hysong matches MetaMessage(text=...), which must not fire on a track name.

#ifndef HYDRA_PARSE_MIDI_H
#define HYDRA_PARSE_MIDI_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace hydra {

// Thrown for input that is not a usable MIDI file (mirrors hymidi's ValueError).
class MidiError : public std::runtime_error {
public:
    explicit MidiError(const std::string& what) : std::runtime_error(what) {}
};

// One event. A single struct covers channel and meta messages, matching the
// dynamic shape hymidi produces; only the fields relevant to `type` are set.
struct Message {
    // Which string attribute a meta carries, if any. Text-metas and name-metas
    // are kept distinct because hysong pattern-matches on `text` specifically.
    enum class StrAttr { None, Text, Name };

    std::string type;          // "note_on", "note_off", "set_tempo",
                               // "time_signature", or a meta type name.
    int64_t time = 0;          // delta ticks since the previous emitted event.

    int note = 0;              // note_on / note_off
    int velocity = 0;          // note_on / note_off

    uint32_t tempo = 0;        // set_tempo (microseconds per quarter note)

    int numerator = 0;         // time_signature
    int denominator = 0;       // time_signature (already 2**b, as in mido)

    std::string str;           // text/name payload, UTF-8 (latin-1 decoded)
    StrAttr str_attr = StrAttr::None;
};

class MidiTrack {
public:
    std::string name;
    std::vector<Message> messages;
};

class MidiFile {
public:
    // Parse from raw file bytes. Throws MidiError on invalid input.
    explicit MidiFile(const std::vector<uint8_t>& data);
    MidiFile(const uint8_t* data, size_t size);

    // Read and parse a file from disk. Throws MidiError if it cannot be read
    // or is not a MIDI file.
    static MidiFile from_file(const std::string& path);

    int ticks_per_beat = 0;
    int format = 0;
    std::vector<MidiTrack> tracks;

private:
    void parse(const uint8_t* data, size_t size);
    MidiTrack parse_track(const uint8_t* data, size_t pos, size_t end);
};

}  // namespace hydra

#endif  // HYDRA_PARSE_MIDI_H
