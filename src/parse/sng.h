// .sng container reading: the one owner of the layout. The note loader
// (parse/song.cpp), the Preview's audio extractor (app/preview_source.cpp)
// and the library scan's metadata reader (app/analysis.cpp) all read through
// here.
//
//   offset 0..9    prefix (never needed for reading)
//   offset 10..25  a 16-byte XOR mask
//   offset 26      u64 LE: the metadata block's length
//   offset 34      the metadata block: u64 pair count, then pairs of
//                  (u32 LE length + bytes) key and value strings
//   after it       u64 LE: the file section's length (not needed: entries
//                  carry absolute offsets), u64 LE file count, then entries
//                  of u8 name length + name, u64 LE length, u64 LE offset
//
// A file's byte i is stored XORed with mask[i % 16] ^ (i & 0xff), counting i
// from the file's own start. Every read is bounds-checked: truncated input
// stops early and never reads past the buffer.

#ifndef HYDRA_PARSE_SNG_H
#define HYDRA_PARSE_SNG_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hydra {

constexpr size_t kSngXorMaskOffset = 10;
constexpr size_t kSngXorMaskSize = 16;
constexpr size_t kSngMetadataLenOffset = kSngXorMaskOffset + kSngXorMaskSize;  // 26
constexpr size_t kSngMetadataOffset = kSngMetadataLenOffset + 8;               // 34

struct SngFileEntry {
    std::string name;
    uint64_t length = 0;
    uint64_t offset = 0;  // absolute, from the start of the file
};

// The metadata pairs in file order, keys as stored (callers fold case).
// Stops at the first pair that does not fit.
std::vector<std::pair<std::string, std::string>> sng_read_metadata(const std::vector<uint8_t>& buf);

// The file table. Stops at the first entry that does not fit and returns the
// entries before it.
std::vector<SngFileEntry> sng_read_file_table(const std::vector<uint8_t>& buf);

// One file's decoded bytes, or nullopt when its range is outside the buffer.
std::optional<std::vector<uint8_t>> sng_decode_file(const std::vector<uint8_t>& buf,
                                                    const SngFileEntry& entry);

}  // namespace hydra

#endif  // HYDRA_PARSE_SNG_H
