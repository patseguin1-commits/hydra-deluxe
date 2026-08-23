// See preview_source.h. The container walks mirror the note loaders in
// parse/song.cpp (the .sng file table) and parse/srb.h (the DEFLATE stream
// chain), reading the audio entries those loaders skip.

#include "app/preview_source.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>

#include "core/winstr.h"
#include "parse/srb.h"

namespace hydra::app {

namespace {

std::string to_lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool ends_with_ci(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return to_lower(s.substr(s.size() - suffix.size())) == to_lower(suffix);
}

// Filename after the last '/' or '\\'.
std::string base_name(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Directory portion (no trailing separator); "." when the path has none.
std::string dir_name(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// Base filename without its extension.
std::string stem_of(const std::string& filename) {
    std::string b = base_name(filename);
    size_t dot = b.find_last_of('.');
    return dot == std::string::npos ? b : b.substr(0, dot);
}

uint64_t read_u64(const std::vector<uint8_t>& buf, size_t pos) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(buf[pos + i]) << (8 * i);
    return v;
}

}  // namespace

bool is_audio_filename(const std::string& filename) {
    return ends_with_ci(filename, ".ogg") || ends_with_ci(filename, ".opus") ||
           ends_with_ci(filename, ".mp3") || ends_with_ci(filename, ".wav") ||
           ends_with_ci(filename, ".flac");
}

bool looks_like_audio(const std::vector<uint8_t>& b) {
    auto starts = [&](const char* magic, size_t n) {
        return b.size() >= n && std::memcmp(b.data(), magic, n) == 0;
    };
    if (starts("OggS", 4)) return true;  // Ogg (Vorbis / Opus)
    if (starts("RIFF", 4)) return true;  // WAV
    if (starts("fLaC", 4)) return true;  // FLAC
    if (starts("ID3", 3)) return true;   // MP3 with an ID3 tag
    // A bare MP3 frame sync: 11 set bits at the start of a frame header.
    if (b.size() >= 2 && b[0] == 0xFF && (b[1] & 0xE0) == 0xE0) return true;
    return false;
}

std::vector<PreviewAudioStem> find_loose_audio(const std::string& folder) {
    std::vector<PreviewAudioStem> stems;
    std::wstring pattern = utf8_to_wide(folder + "\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return stems;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = wide_to_utf8(fd.cFileName);
        if (!is_audio_filename(name)) continue;
        std::string stem = stem_of(name);
        // "preview.*" is a short clip, not part of the song mix.
        if (to_lower(stem) == "preview") continue;
        PreviewAudioStem s;
        s.label = stem;
        s.path = folder + "\\" + name;
        stems.push_back(std::move(s));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(stems.begin(), stems.end(),
              [](const PreviewAudioStem& a, const PreviewAudioStem& b) {
                  return a.label < b.label;
              });
    return stems;
}

std::vector<PreviewAudioStem> extract_sng_audio(const std::string& path) {
    std::vector<PreviewAudioStem> stems;
    std::vector<uint8_t> buf = read_file_bytes(path);

    const size_t XORMASK_OFFSET = 10;
    if (buf.size() < XORMASK_OFFSET + 16 + 8) return stems;
    uint8_t xormask[16];
    std::memcpy(xormask, buf.data() + XORMASK_OFFSET, 16);

    size_t pos = XORMASK_OFFSET + 16;
    if (pos + 8 > buf.size()) return stems;
    uint64_t metadata_len = read_u64(buf, pos);
    pos += 8 + static_cast<size_t>(metadata_len);
    pos += 8;  // section length
    if (pos + 8 > buf.size()) return stems;
    uint64_t file_count = read_u64(buf, pos);
    pos += 8;

    for (uint64_t i = 0; i < file_count; ++i) {
        if (pos + 1 > buf.size()) break;
        uint8_t filename_len = buf[pos];
        pos += 1;
        if (pos + filename_len > buf.size()) break;
        std::string filename(reinterpret_cast<const char*>(buf.data() + pos),
                             filename_len);
        pos += filename_len;
        if (pos + 16 > buf.size()) break;
        uint64_t contents_len = read_u64(buf, pos);
        pos += 8;
        uint64_t contents_index = read_u64(buf, pos);
        pos += 8;

        if (!is_audio_filename(filename)) continue;
        if (contents_index + contents_len > buf.size()) continue;  // corrupt entry

        PreviewAudioStem s;
        s.label = stem_of(filename);
        s.bytes.resize(static_cast<size_t>(contents_len));
        for (uint64_t j = 0; j < contents_len; ++j) {
            uint8_t xorkey = xormask[j % 16] ^ static_cast<uint8_t>(j & 0xff);
            s.bytes[static_cast<size_t>(j)] =
                buf[static_cast<size_t>(contents_index + j)] ^ xorkey;
        }
        stems.push_back(std::move(s));
    }
    return stems;
}

std::vector<PreviewAudioStem> extract_srb_audio(const std::string& path) {
    std::vector<PreviewAudioStem> stems;
    std::vector<uint8_t> buf = read_file_bytes(path);
    if (buf.size() <= kSrbHeaderSize) return stems;

    // Walk the DEFLATE stream chain past metadata (1) and notes (2); the rest
    // are audio/art. A trailing stream that fails to inflate ends the walk --
    // the chain is undocumented and the tail is the fragile part.
    try {
        size_t offset = 0;
        srb_inflate_stream(buf.data(), buf.size(), kSrbHeaderSize,
                           kSrbMaxMetadata, &offset);  // stream 1: metadata
        std::vector<uint8_t> notes = srb_inflate_stream(
            buf.data(), buf.size(), offset, size_t{1} << 30, &offset);  // stream 2
        (void)notes;

        int index = 3;
        while (offset < buf.size()) {
            size_t next = 0;
            std::vector<uint8_t> stream = srb_inflate_stream(
                buf.data(), buf.size(), offset, size_t{1} << 30, &next);
            if (next <= offset) break;  // no forward progress: stop
            offset = next;
            if (looks_like_audio(stream)) {
                PreviewAudioStem s;
                s.label = "stream" + std::to_string(index);
                s.bytes = std::move(stream);
                stems.push_back(std::move(s));
            }
            ++index;
        }
    } catch (const std::exception&) {
        // A malformed trailing stream just ends extraction; the notes and
        // visuals are unaffected.
    }
    return stems;
}

PreviewSource resolve_preview_source(const std::string& notespath, bool pro,
                                     bool bass2x) {
    PreviewSource src{load_songpath(notespath, pro, bass2x), {}};
    if (ends_with_ci(notespath, ".sng"))
        src.stems = extract_sng_audio(notespath);
    else if (ends_with_ci(notespath, ".srb"))
        src.stems = extract_srb_audio(notespath);
    else
        src.stems = find_loose_audio(dir_name(notespath));
    return src;
}

}  // namespace hydra::app
