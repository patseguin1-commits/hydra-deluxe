// See preview_source.h. The container walks mirror the note loaders in
// parse/song.cpp (the .sng file table) and parse/srb.h (the DEFLATE stream
// chain), reading the audio entries those loaders skip.

#include "app/preview_source.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>

#include "core/winstr.h"
#include "parse/srb.h"
#include "parse/sng.h"

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

// AES-128-CFB decryption for SRB audio blobs.  CFB decryption is: for each
// block, ECB-encrypt the previous ciphertext block (starting from the IV) to
// get the keystream, then XOR.  We batch all ECB encryptions into one call so
// the cost is a single BCrypt round-trip per blob rather than one per 16 bytes.
bool srb_decrypt_blob(const uint8_t* enc, size_t len, const uint8_t* header16,
                      std::vector<uint8_t>& out) {
    if (len == 0) return true;

    static const uint8_t kSrbAesKey[16] = {
        0xbf, 0xfe, 0x5f, 0xcb, 0xf7, 0x9e, 0x74, 0x60,
        0x57, 0xab, 0xab, 0xf6, 0xce, 0x2f, 0xac, 0x14};

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &alg, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return false;

    // Default chaining mode is CBC; we need ECB (independent blocks).
    if (!BCRYPT_SUCCESS(BCryptSetProperty(
            alg, BCRYPT_CHAINING_MODE,
            (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
            static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_ECB)), 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    BCRYPT_KEY_HANDLE key = nullptr;
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
            alg, &key, nullptr, 0,
            const_cast<PUCHAR>(kSrbAesKey), sizeof(kSrbAesKey), 0))) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    // Build the ECB input: [IV, enc[0..16], enc[16..32], ...].
    // Block i's keystream = AES_ECB_encrypt(ecb_input block i).
    size_t n_blocks = (len + 15) / 16;
    size_t ecb_len = n_blocks * 16;
    std::vector<uint8_t> ecb_buf(ecb_len);

    // First block's input is the IV (header halves swapped).
    std::memcpy(ecb_buf.data(), header16 + 8, 8);
    std::memcpy(ecb_buf.data() + 8, header16, 8);
    // Remaining blocks' inputs are the ciphertext shifted back by one block.
    size_t copy_len = (n_blocks - 1) * 16;
    if (copy_len > 0)
        std::memcpy(ecb_buf.data() + 16, enc, copy_len);

    // One ECB encrypt to produce all keystream blocks at once.
    ULONG written = 0;
    bool ok = BCRYPT_SUCCESS(BCryptEncrypt(
        key, ecb_buf.data(), static_cast<ULONG>(ecb_len), nullptr,
        nullptr, 0, ecb_buf.data(), static_cast<ULONG>(ecb_len), &written, 0));

    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return false;

    // XOR the keystream with the ciphertext.
    out.resize(len);
    for (size_t i = 0; i < len; ++i)
        out[i] = enc[i] ^ ecb_buf[i];
    return true;
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
    for (const SngFileEntry& e : sng_read_file_table(buf)) {
        if (!is_audio_filename(e.name)) continue;
        std::optional<std::vector<uint8_t>> bytes = sng_decode_file(buf, e);
        if (!bytes) continue;  // corrupt entry
        PreviewAudioStem s;
        s.label = stem_of(e.name);
        s.bytes = std::move(*bytes);
        stems.push_back(std::move(s));
    }
    return stems;
}

std::vector<PreviewAudioStem> extract_srb_audio(const std::string& path) {
    std::vector<PreviewAudioStem> stems;
    std::vector<uint8_t> buf = read_file_bytes(path);
    if (buf.size() <= kSrbHeaderSize) return stems;

    // Walk the DEFLATE stream chain past metadata (1) and notes (2).  Any
    // trailing stream whose payload looks like audio is kept (this handles
    // synthetic / future SRBs that embed audio in the chain itself).
    size_t offset = 0;
    try {
        srb_inflate_stream(buf.data(), buf.size(), kSrbHeaderSize,
                           kSrbMaxMetadata, &offset);  // stream 1: metadata
        srb_inflate_stream(buf.data(), buf.size(), offset,
                           kSrbMaxStream, &offset);  // stream 2: notes

        int index = 3;
        while (offset < buf.size()) {
            size_t next = 0;
            std::vector<uint8_t> stream = srb_inflate_stream(
                buf.data(), buf.size(), offset, kSrbMaxStream, &next);
            if (next <= offset) break;
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
        // A malformed trailing stream ends the DEFLATE walk.
    }

    // If the DEFLATE chain already yielded audio we're done.
    if (!stems.empty()) return stems;

    // Real Clone Hero .srb files store audio in an AES-128-CFB-encrypted
    // section after the DEFLATE chain.  Layout:
    //   u64 (purpose unclear — not the blob count; skip it)
    //   blob 0:  16-byte header + u64 size + data[size]
    //   blob 1+: u64 type_id + 16-byte header + u64 size + data[size]
    if (offset + 8 > buf.size()) return stems;
    size_t cursor = offset + 8;  // skip the leading u64

    // Blob 0 has no type prefix; all subsequent blobs do.
    bool first = true;
    int stem_index = 0;
    while (cursor < buf.size()) {
        if (!first) {
            if (cursor + 8 > buf.size()) break;
            cursor += 8;  // skip the type_id prefix
        }
        if (cursor + 24 > buf.size()) break;

        const uint8_t* header = buf.data() + cursor;
        uint64_t blob_size = read_u64(buf, cursor + 16);
        cursor += 24;

        if (blob_size > buf.size() - cursor) break;

        std::vector<uint8_t> plain;
        if (srb_decrypt_blob(buf.data() + cursor, static_cast<size_t>(blob_size),
                             header, plain) &&
            looks_like_audio(plain)) {
            PreviewAudioStem s;
            s.label = first ? "song" : "stem" + std::to_string(stem_index);
            s.bytes = std::move(plain);
            stems.push_back(std::move(s));
        }

        cursor += static_cast<size_t>(blob_size);
        first = false;
        ++stem_index;
    }

    return stems;
}

PreviewSource resolve_preview_source(const std::string& notespath, bool pro,
                                     bool bass2x, Difficulty difficulty,
                                     const core::Rules& rules) {
    PreviewSource src{load_songpath(notespath, pro, bass2x, difficulty, rules), {}};
    if (ends_with_ci(notespath, ".sng"))
        src.stems = extract_sng_audio(notespath);
    else if (ends_with_ci(notespath, ".srb")) {
        src.stems = extract_srb_audio(notespath);
        // If decryption fails (wrong key, corrupt file, etc.) fall back to
        // loose audio files beside the .srb, same as a folder chart.
        if (src.stems.empty()) src.stems = find_loose_audio(dir_name(notespath));
    } else
        src.stems = find_loose_audio(dir_name(notespath));
    return src;
}

}  // namespace hydra::app
