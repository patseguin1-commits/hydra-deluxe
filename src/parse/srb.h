// Clone Hero bundled-song (.srb) container reading.
//
// The 31 songs that ship with Clone Hero (Clone Hero_Data/StreamingAssets/
// songs/*.srb) use an undocumented container. Reverse-engineered and verified
// against all of them, the layout is a 16-byte header followed by back-to-back
// raw DEFLATE streams (no zlib/gzip framing):
//
//   offset 0..11   12 bytes that vary per file (never needed for reading)
//   offset 12..15  u32 LE, purpose unknown (never needed for reading)
//   offset 16      stream 1: metadata block
//   right after    stream 2: the notes file bytes (a standard notes.mid or
//                  notes.chart); later streams hold audio/art and are ignored
//
// Decompressed, stream 1 is a 4-byte prefix ("4b4\x01" in every known file)
// followed by eight length-prefixed strings (u32 LE length + UTF-8 bytes) in
// a fixed order: notes filename, name, artist, album, genre, charter, year,
// description. Trailing binary fields (difficulties, preview time, contained
// file sizes) follow and are not read here.
//
// DEFLATE streams do not encode their own compressed length, so finding
// stream 2 requires inflating stream 1 while tracking consumed input — which
// is why srb_inflate_stream reports an end offset.

#ifndef HYDRA_PARSE_SRB_H
#define HYDRA_PARSE_SRB_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hydra {

constexpr size_t kSrbHeaderSize = 16;

// A metadata block is a few hundred bytes in practice; a megabyte is far
// beyond any legitimate block and bounds hostile input.
constexpr size_t kSrbMaxMetadata = 1 << 20;

// The cap on any stream after the metadata (the notes file, and the audio or
// art streams that follow it). A notes file inflates to well under a hundred
// MB even for mega-charts; 1 GB exists only to bound hostile input.
constexpr size_t kSrbMaxStream = size_t{1} << 30;

// Inflate the raw-deflate stream starting at data[offset]. Returns the
// decompressed bytes; if end_offset is non-null it receives the offset of the
// first byte past the stream's compressed data (i.e. where the next stream
// starts). Throws std::runtime_error on corrupt data, a truncated stream, or
// output larger than max_out.
std::vector<uint8_t> srb_inflate_stream(const uint8_t* data, size_t size,
                                        size_t offset, size_t max_out,
                                        size_t* end_offset);

// The string fields of a metadata block, in file order.
struct SrbMetadata {
    std::string notes_filename;
    std::string name;
    std::string artist;
    std::string album;
    std::string genre;
    std::string charter;
    std::string year;
    std::string description;
};

// Parse the string table of a decompressed metadata block. Returns false if
// the block is too short to even hold the prefix; a block that truncates
// mid-table keeps the fields read so far and leaves the rest empty.
bool srb_parse_metadata(const std::vector<uint8_t>& meta, SrbMetadata& out);

}  // namespace hydra

#endif  // HYDRA_PARSE_SRB_H
