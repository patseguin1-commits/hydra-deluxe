// Chord encode table — the C++ port of hydra/hyencode.py's CHORD_ENCODE.
//
// Maps a chord's hash to its short display string. The table body lives in the
// generated chord_tables.cpp (see tools/gen_chord_tables.py); this header is
// the stable interface the rest of the app calls.

#ifndef HYDRA_CORE_CHORD_TABLES_H
#define HYDRA_CORE_CHORD_TABLES_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hydra {

// The full chord hash -> display string table (built once, then cached).
const std::unordered_map<int64_t, std::string>& chord_encode();

// Look up one chord hash; nullptr if it is not in the table.
const std::string* encode_chord(int64_t hash);

// One note as stored in the reverse table: the integer field values that
// hydra/hyencode.py's CHORD_DECODE carries (color/dyn/cym are enum .value
// ints, is2x is 0/1). Chord::from_code rebuilds ChordNotes from these.
struct ChordNoteFields {
    int color;
    int dyn;
    int cym;
    int is2x;
};

// The full display string -> note-field list table (built once, then cached).
const std::unordered_map<std::string, std::vector<ChordNoteFields>>&
chord_decode();

// Look up one chord code; nullptr if it is not in the table.
const std::vector<ChordNoteFields>* decode_chord(const std::string& code);

}  // namespace hydra

#endif  // HYDRA_CORE_CHORD_TABLES_H
