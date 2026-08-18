#include "parse/srb.h"

#include <stdexcept>

#include "miniz.h"

namespace hydra {

std::vector<uint8_t> srb_inflate_stream(const uint8_t* data, size_t size,
                                        size_t offset, size_t max_out,
                                        size_t* end_offset) {
    if (offset >= size)
        throw std::runtime_error("SRB stream starts past end of file.");

    mz_stream s{};
    // Negative window bits selects a raw deflate stream, zlib-style.
    if (mz_inflateInit2(&s, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
        throw std::runtime_error("SRB inflate init failed.");

    s.next_in = data + offset;
    s.avail_in = static_cast<unsigned int>(size - offset);

    std::vector<uint8_t> out;
    uint8_t chunk[64 * 1024];
    int status = MZ_OK;
    while (status != MZ_STREAM_END) {
        s.next_out = chunk;
        s.avail_out = sizeof(chunk);
        status = mz_inflate(&s, MZ_NO_FLUSH);
        if (status != MZ_OK && status != MZ_STREAM_END) {
            mz_inflateEnd(&s);
            throw std::runtime_error("SRB stream is corrupt.");
        }
        out.insert(out.end(), chunk, chunk + (sizeof(chunk) - s.avail_out));
        if (out.size() > max_out) {
            mz_inflateEnd(&s);
            throw std::runtime_error("SRB stream exceeds size limit.");
        }
        // All input consumed without reaching the stream's end marker.
        if (status == MZ_OK && s.avail_in == 0 && s.avail_out != 0) {
            mz_inflateEnd(&s);
            throw std::runtime_error("SRB stream is truncated.");
        }
    }

    if (end_offset) *end_offset = offset + static_cast<size_t>(s.total_in);
    mz_inflateEnd(&s);
    return out;
}

bool srb_parse_metadata(const std::vector<uint8_t>& meta, SrbMetadata& out) {
    const size_t kPrefixSize = 4;  // "4b4\x01" in every known file; not validated.
    if (meta.size() < kPrefixSize) return false;

    size_t pos = kPrefixSize;
    std::string* fields[] = {&out.notes_filename, &out.name,    &out.artist,
                             &out.album,          &out.genre,   &out.charter,
                             &out.year,           &out.description};
    for (std::string* field : fields) {
        if (pos + 4 > meta.size()) break;
        uint32_t len = 0;
        for (int i = 0; i < 4; ++i)
            len |= static_cast<uint32_t>(meta[pos + i]) << (8 * i);
        pos += 4;
        if (len > meta.size() - pos) break;
        field->assign(reinterpret_cast<const char*>(meta.data() + pos), len);
        pos += len;
    }
    return true;
}

}  // namespace hydra
