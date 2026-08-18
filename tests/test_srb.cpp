// Tests for parse/srb — the Clone Hero bundled-song (.srb) container.
//
// No .srb ships in the corpus (the real ones are Clone Hero's copyrighted
// bundles), so these tests fabricate containers with miniz's compressor:
// 16 header bytes + a deflated metadata block + a deflated notes payload,
// wrapping real corpus charts. A wrapped chart must parse identically to the
// loose file, and discovery must surface the embedded metadata.

#define _CRT_SECURE_NO_WARNINGS  // _wfopen; matches parse/song.cpp's file open.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "corpus_util.h"
#include "miniz.h"
#include "parse/song.h"
#include "parse/srb.h"

using namespace hydra;

namespace {

std::wstring utf8_to_wide(const std::string& s) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], wlen);
    w.resize(w.size() - 1);  // drop the terminator
    return w;
}

std::vector<uint8_t> read_bytes(const std::string& path) {
    FILE* f = _wfopen(utf8_to_wide(path).c_str(), L"rb");
    REQUIRE_MESSAGE(f != nullptr, "cannot open " << path);
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size > 0 ? static_cast<size_t>(size) : 0);
    if (size > 0) buf.resize(std::fread(buf.data(), 1, buf.size(), f));
    std::fclose(f);
    return buf;
}

void write_bytes(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = _wfopen(utf8_to_wide(path).c_str(), L"wb");
    REQUIRE_MESSAGE(f != nullptr, "cannot write " << path);
    if (!data.empty()) std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

// A fresh directory under %TEMP% for this process's fixtures.
std::string fixture_dir() {
    static std::string dir = [] {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring d = std::wstring(tmp) + L"hydra_srb_test_" +
                         std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(d.c_str(), nullptr);
        int len = WideCharToMultiByte(CP_UTF8, 0, d.c_str(), -1, nullptr, 0,
                                      nullptr, nullptr);
        std::string out(static_cast<size_t>(len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, d.c_str(), -1, &out[0], len, nullptr,
                            nullptr);
        out.resize(out.size() - 1);
        return out;
    }();
    return dir;
}

std::vector<uint8_t> deflate_raw(const std::vector<uint8_t>& src) {
    size_t out_len = 0;
    // Flags 0 = raw deflate, no zlib header — what .srb streams use.
    void* p = tdefl_compress_mem_to_heap(src.data(), src.size(), &out_len,
                                         TDEFL_DEFAULT_MAX_PROBES);
    REQUIRE(p != nullptr);
    std::vector<uint8_t> out(static_cast<uint8_t*>(p),
                             static_cast<uint8_t*>(p) + out_len);
    mz_free(p);
    return out;
}

void push_str(std::vector<uint8_t>& out, const std::string& s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(n >> (8 * i)));
    out.insert(out.end(), s.begin(), s.end());
}

std::vector<uint8_t> make_metadata(const std::string& notes_filename,
                                   const std::string& name,
                                   const std::string& artist,
                                   const std::string& charter) {
    std::vector<uint8_t> meta = {'4', 'b', '4', 1};
    push_str(meta, notes_filename);
    push_str(meta, name);
    push_str(meta, artist);
    push_str(meta, "Test Album");
    push_str(meta, "Test Genre");
    push_str(meta, charter);
    push_str(meta, "2026");
    push_str(meta, "A synthetic bundle for the test suite.");
    // Real files carry trailing binary fields (difficulties, sizes, ...);
    // a few junk bytes stand in for them.
    for (int i = 0; i < 24; ++i) meta.push_back(static_cast<uint8_t>(i * 7));
    return meta;
}

std::vector<uint8_t> make_srb(const std::vector<uint8_t>& metadata,
                              const std::vector<uint8_t>& notes,
                              bool trailing_stream = true) {
    std::vector<uint8_t> out;
    // 12 arbitrary bytes + an arbitrary u32, like the real header.
    for (int i = 0; i < 12; ++i) out.push_back(static_cast<uint8_t>(0xA0 + i));
    for (int i = 0; i < 4; ++i) out.push_back(i == 0 ? 17 : 0);
    std::vector<uint8_t> s1 = deflate_raw(metadata);
    std::vector<uint8_t> s2 = deflate_raw(notes);
    out.insert(out.end(), s1.begin(), s1.end());
    out.insert(out.end(), s2.begin(), s2.end());
    if (trailing_stream) {
        // Real bundles continue with audio streams the reader must ignore.
        std::vector<uint8_t> audio(4096, 0x55);
        std::vector<uint8_t> s3 = deflate_raw(audio);
        out.insert(out.end(), s3.begin(), s3.end());
    }
    return out;
}

bool songs_equal(const Song& a, const Song& b) {
    if (a.tick_resolution() != b.tick_resolution()) return false;
    if (a.tpm_changes != b.tpm_changes) return false;
    if (a.bpm_changes != b.bpm_changes) return false;
    if (a.features != b.features) return false;
    if (a.sequence.size() != b.sequence.size()) return false;
    for (size_t i = 0; i < a.sequence.size(); ++i) {
        const SongTimestamp& x = a.sequence[i];
        const SongTimestamp& y = b.sequence[i];
        if (x.timecode.ticks() != y.timecode.ticks()) return false;
        if (x.chord.code() != y.chord.code()) return false;
        if (x.flag_solo != y.flag_solo || x.flag_sp != y.flag_sp) return false;
        if (x.activation_length != y.activation_length) return false;
    }
    return true;
}

// First corpus chart with the given extension.
std::string corpus_chart_path(const std::string& ext) {
    return corpus::first_chart_with_suffix(ext);
}

}  // namespace

TEST_CASE("srb: a wrapped chart parses identically to the loose file") {
    struct Case {
        const char* ext;
        const char* notes_filename;
    };
    for (Case c : {Case{".mid", "notes.mid"}, Case{".chart", "notes.chart"}}) {
        CAPTURE(c.ext);
        std::string src = corpus_chart_path(c.ext);
        std::vector<uint8_t> notes = read_bytes(src);

        std::vector<uint8_t> srb = make_srb(
            make_metadata(c.notes_filename, "Name", "Artist", "Charter"), notes);
        std::string path = fixture_dir() + "\\wrapped" + c.ext + ".srb";
        write_bytes(path, srb);

        Song direct = load_songpath(src, "Expert", true, true);
        Song via_srb = load_songpath(path, "Expert", true, true);
        CHECK_MESSAGE(songs_equal(direct, via_srb), src);
    }
}

TEST_CASE("srb: an unexpected notes filename falls back to payload sniffing") {
    std::string src = corpus_chart_path(".mid");
    std::vector<uint8_t> notes = read_bytes(src);
    std::vector<uint8_t> srb =
        make_srb(make_metadata("weird.bin", "N", "A", "C"), notes);
    std::string path = fixture_dir() + "\\sniffed.srb";
    write_bytes(path, srb);

    Song direct = load_songpath(src, "Expert", true, true);
    Song via_srb = load_songpath_srb(path, "Expert", true, true);
    CHECK(songs_equal(direct, via_srb));
}

TEST_CASE("srb: malformed containers throw instead of crashing") {
    std::string tiny = fixture_dir() + "\\tiny.srb";
    write_bytes(tiny, {1, 2, 3});
    CHECK_THROWS_AS(load_songpath_srb(tiny, "Expert", true, true),
                    std::runtime_error);

    std::string garbage = fixture_dir() + "\\garbage.srb";
    std::vector<uint8_t> junk(64);
    for (size_t i = 0; i < junk.size(); ++i)
        junk[i] = static_cast<uint8_t>(i * 37 + 11);
    write_bytes(garbage, junk);
    CHECK_THROWS_AS(load_songpath_srb(garbage, "Expert", true, true),
                    std::runtime_error);

    // Metadata stream present but the notes stream is cut off mid-way.
    std::vector<uint8_t> notes = read_bytes(corpus_chart_path(".mid"));
    std::vector<uint8_t> whole =
        make_srb(make_metadata("notes.mid", "N", "A", "C"), notes, false);
    whole.resize(whole.size() / 2);
    std::string truncated = fixture_dir() + "\\truncated.srb";
    write_bytes(truncated, whole);
    CHECK_THROWS_AS(load_songpath_srb(truncated, "Expert", true, true),
                    std::runtime_error);
}

TEST_CASE("srb: metadata parser reads the string table") {
    std::vector<uint8_t> meta =
        make_metadata("notes.chart", "Song Name", "The Artist", "The Charter");
    SrbMetadata md;
    REQUIRE(srb_parse_metadata(meta, md));
    CHECK(md.notes_filename == "notes.chart");
    CHECK(md.name == "Song Name");
    CHECK(md.artist == "The Artist");
    CHECK(md.album == "Test Album");
    CHECK(md.genre == "Test Genre");
    CHECK(md.charter == "The Charter");
    CHECK(md.year == "2026");

    // Truncated mid-table: earlier fields survive, later ones stay empty.
    std::vector<uint8_t> cut(meta.begin(), meta.begin() + 4 + 4 + 11 + 4 + 9 / 2);
    SrbMetadata partial;
    REQUIRE(srb_parse_metadata(cut, partial));
    CHECK(partial.notes_filename == "notes.chart");
    CHECK(partial.name.empty());

    std::vector<uint8_t> too_short = {'4', 'b'};
    SrbMetadata none;
    CHECK_FALSE(srb_parse_metadata(too_short, none));
}

TEST_CASE("srb: discovery surfaces the embedded metadata") {
    // A dedicated folder so the corpus snapshot tests are unaffected.
    std::string dir = fixture_dir() + "\\scan";
    CreateDirectoryW(utf8_to_wide(dir).c_str(), nullptr);

    std::vector<uint8_t> notes = read_bytes(corpus_chart_path(".chart"));
    std::vector<uint8_t> srb = make_srb(
        make_metadata("notes.chart", "Scanned Song", "Scanned Artist",
                      "Scanned Charter"),
        notes);
    write_bytes(dir + "\\bundle.srb", srb);

    auto [items, errors] = hydra::app::discover_charts({dir});
    REQUIRE(errors.empty());
    REQUIRE(items.size() == 1);
    CHECK(items[0].title == "Scanned Song");
    CHECK(items[0].artist == "Scanned Artist");
    CHECK(items[0].charter == "Scanned Charter");
    CHECK(items[0].md5.size() == 32);
    CHECK(!items[0].sig.empty());
}
