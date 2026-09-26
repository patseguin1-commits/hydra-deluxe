// Tests for app/preview_source: resolving a chart's notes and locating its
// audio across the three source kinds. No .sng/.srb ship in the corpus, so the
// container cases fabricate one byte-for-byte the way the loaders read it (a
// real corpus chart stands in for the notes), then confirm the audio entries
// the note loaders skip come back intact.

#define _CRT_SECURE_NO_WARNINGS

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/preview_source.h"
#include "core/winstr.h"
#include "corpus_util.h"
#include "miniz.h"
#include "multidiff_chart.h"
#include "parse/song.h"

using namespace hydra;
using namespace hydra::app;

namespace {

void write_bytes(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = hydra::fopen_utf8(path, L"wb");
    REQUIRE_MESSAGE(f != nullptr, "cannot write " << path);
    if (!data.empty()) std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

// A fresh directory under %TEMP% for this process's fixtures.
std::string fixture_dir() {
    static std::string dir = [] {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring d = std::wstring(tmp) + L"hydra_prevsrc_" +
                         std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(d.c_str(), nullptr);
        return wide_to_utf8(d);
    }();
    return dir;
}

std::string make_subdir(const std::string& name) {
    std::string dir = fixture_dir() + "\\" + name;
    CreateDirectoryW(utf8_to_wide(dir).c_str(), nullptr);
    return dir;
}

void push_u32(std::vector<uint8_t>& out, uint32_t n) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(n >> (8 * i)));
}
void push_u64(std::vector<uint8_t>& out, uint64_t n) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(n >> (8 * i)));
}

std::vector<uint8_t> deflate_raw(const std::vector<uint8_t>& src) {
    size_t out_len = 0;
    void* p = tdefl_compress_mem_to_heap(src.data(), src.size(), &out_len,
                                         TDEFL_DEFAULT_MAX_PROBES);
    REQUIRE(p != nullptr);
    std::vector<uint8_t> out(static_cast<uint8_t*>(p),
                             static_cast<uint8_t*>(p) + out_len);
    mz_free(p);
    return out;
}

std::vector<uint8_t> bytes_of(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// ---- .sng fixture --------------------------------------------------------

struct SngFile {
    std::string name;
    std::vector<uint8_t> bytes;
};

// Build a .sng exactly as load_songpath_sng reads it: 10 prefix bytes, a
// 16-byte XOR mask, a metadata block, then a file table whose contents live at
// absolute offsets, every file's bytes XOR-masked by mask[j%16]^(j&0xff).
std::vector<uint8_t> make_sng(
    const std::vector<SngFile>& files,
    const std::vector<std::pair<std::string, std::string>>& metadata = {}) {
    uint8_t mask[16];
    for (int i = 0; i < 16; ++i) mask[i] = static_cast<uint8_t>(i * 13 + 7);
    // No pairs: the old 20 junk bytes, which read as no metadata at all, so
    // every existing fixture stays byte-identical. With pairs: a real block,
    // u64 count then (u32 length + bytes) key and value strings.
    std::vector<uint8_t> meta(20, 0xAB);
    if (!metadata.empty()) {
        meta.clear();
        push_u64(meta, metadata.size());
        for (const auto& [key, value] : metadata) {
            push_u32(meta, static_cast<uint32_t>(key.size()));
            meta.insert(meta.end(), key.begin(), key.end());
            push_u32(meta, static_cast<uint32_t>(value.size()));
            meta.insert(meta.end(), value.begin(), value.end());
        }
    }

    size_t table_bytes = 0;
    for (const SngFile& f : files) table_bytes += 1 + f.name.size() + 8 + 8;
    size_t header_bytes = 10 + 16 + (8 + meta.size()) + 8 + 8;
    size_t contents_start = header_bytes + table_bytes;

    std::vector<uint64_t> offsets;
    uint64_t cur = contents_start;
    for (const SngFile& f : files) {
        offsets.push_back(cur);
        cur += f.bytes.size();
    }

    std::vector<uint8_t> out;
    for (int i = 0; i < 10; ++i) out.push_back(static_cast<uint8_t>('S' + i));
    out.insert(out.end(), mask, mask + 16);
    push_u64(out, meta.size());
    out.insert(out.end(), meta.begin(), meta.end());
    push_u64(out, 0);  // section length (skipped by the loader)
    push_u64(out, files.size());
    for (size_t k = 0; k < files.size(); ++k) {
        out.push_back(static_cast<uint8_t>(files[k].name.size()));
        out.insert(out.end(), files[k].name.begin(), files[k].name.end());
        push_u64(out, files[k].bytes.size());
        push_u64(out, offsets[k]);
    }
    REQUIRE(out.size() == contents_start);
    for (const SngFile& f : files)
        for (uint64_t j = 0; j < f.bytes.size(); ++j) {
            uint8_t xorkey = mask[j % 16] ^ static_cast<uint8_t>(j & 0xff);
            out.push_back(f.bytes[static_cast<size_t>(j)] ^ xorkey);
        }
    return out;
}

// ---- .srb fixture --------------------------------------------------------

void push_str(std::vector<uint8_t>& out, const std::string& s) {
    push_u32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

std::vector<uint8_t> make_metadata(const std::string& notes_filename) {
    std::vector<uint8_t> meta = {'4', 'b', '4', 1};
    push_str(meta, notes_filename);
    for (const char* s : {"Name", "Artist", "Album", "Genre", "Charter", "2026",
                          "desc"})
        push_str(meta, s);
    for (int i = 0; i < 16; ++i) meta.push_back(static_cast<uint8_t>(i));
    return meta;
}

// 16-byte header + deflated metadata + deflated notes + one deflated stream per
// `extra`, like a real bundle's trailing audio/art streams.
std::vector<uint8_t> make_srb(const std::vector<uint8_t>& notes,
                              const std::vector<std::vector<uint8_t>>& extra,
                              const std::string& notes_filename = "notes.mid") {
    std::vector<uint8_t> out;
    for (int i = 0; i < 16; ++i) out.push_back(static_cast<uint8_t>(0xA0 + i));
    std::vector<uint8_t> s1 = deflate_raw(make_metadata(notes_filename));
    std::vector<uint8_t> s2 = deflate_raw(notes);
    out.insert(out.end(), s1.begin(), s1.end());
    out.insert(out.end(), s2.begin(), s2.end());
    for (const std::vector<uint8_t>& e : extra) {
        std::vector<uint8_t> s = deflate_raw(e);
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}

// multidiff's chart with an Offset line in its [Song] section.
std::vector<uint8_t> chart_with_offset(const std::string& seconds) {
    std::string s = multidiff::chart_text();
    const std::string anchor = "  Resolution = 192\n";
    const size_t at = s.find(anchor);
    REQUIRE(at != std::string::npos);
    s.insert(at + anchor.size(), "  Offset = " + seconds + "\n");
    return bytes_of(s);
}

}  // namespace

TEST_CASE("is_audio_filename / looks_like_audio recognize the formats") {
    CHECK(is_audio_filename("song.ogg"));
    CHECK(is_audio_filename("DRUMS.OPUS"));
    CHECK(is_audio_filename("x.mp3"));
    CHECK(is_audio_filename("y.wav"));
    CHECK(is_audio_filename("z.flac"));
    CHECK_FALSE(is_audio_filename("album.png"));
    CHECK_FALSE(is_audio_filename("notes.chart"));

    CHECK(looks_like_audio(bytes_of("OggS\x00\x02")));
    CHECK(looks_like_audio(bytes_of("RIFF....WAVE")));
    CHECK(looks_like_audio(bytes_of("fLaC")));
    CHECK(looks_like_audio(bytes_of("ID3\x03")));
    CHECK(looks_like_audio({0xFF, 0xFB, 0x90, 0x00}));  // MP3 frame sync
    CHECK_FALSE(looks_like_audio(bytes_of("\x89PNG\r\n")));
    CHECK_FALSE(looks_like_audio({}));
}

TEST_CASE("find_loose_audio: stems beside the notes, preview and art excluded") {
    std::string dir = make_subdir("loose");
    write_bytes(dir + "\\notes.chart", bytes_of("[Song]"));
    write_bytes(dir + "\\song.ogg", bytes_of("OggS song"));
    write_bytes(dir + "\\drums.opus", bytes_of("OggS drums"));
    write_bytes(dir + "\\preview.ogg", bytes_of("OggS preview"));
    write_bytes(dir + "\\album.png", bytes_of("\x89PNG"));

    std::vector<PreviewAudioStem> stems = find_loose_audio(dir);
    REQUIRE(stems.size() == 2);  // preview and png excluded
    CHECK(stems[0].label == "drums");  // sorted by label
    CHECK(stems[1].label == "song");
    for (const PreviewAudioStem& s : stems) {
        CHECK(s.from_file());
        CHECK(s.bytes.empty());
    }
}

TEST_CASE("extract_sng_audio: audio entries come back XOR-demasked") {
    std::vector<uint8_t> song = bytes_of("OggS the song bytes");
    std::vector<uint8_t> drums = bytes_of("OggS the drums stem bytes!!");
    std::vector<uint8_t> notes = hydra::read_file_bytes(
        corpus::first_chart_with_suffix(".chart"));

    std::vector<uint8_t> sng = make_sng({{"notes.chart", notes},
                                         {"song.ogg", song},
                                         {"drums.opus", drums},
                                         {"album.jpg", bytes_of("\xFF\xD8" "art")}});
    std::string path = fixture_dir() + "\\bundle.sng";
    write_bytes(path, sng);

    std::vector<PreviewAudioStem> stems = extract_sng_audio(path);
    REQUIRE(stems.size() == 2);  // notes and the .jpg are not audio
    CHECK(stems[0].label == "song");
    CHECK(stems[0].bytes == song);
    CHECK(stems[1].label == "drums");
    CHECK(stems[1].bytes == drums);

    // resolve_preview_source parses the embedded chart and returns the audio.
    PreviewSource src = resolve_preview_source(path, true, true);
    CHECK_FALSE(src.song.is_empty());
    CHECK(src.stems.size() == 2);
}

TEST_CASE("extract_srb_audio: trailing audio streams inflate; art is skipped") {
    std::vector<uint8_t> notes = hydra::read_file_bytes(
        corpus::first_chart_with_suffix(".mid"));
    std::vector<uint8_t> ogg = bytes_of("OggS opus payload for the preview");
    std::vector<uint8_t> art(512, 0);
    std::memcpy(art.data(), "\x89PNG\r\n\x1a\n", 8);

    std::string path = fixture_dir() + "\\bundle.srb";
    write_bytes(path, make_srb(notes, {ogg, art}));

    std::vector<PreviewAudioStem> stems = extract_srb_audio(path);
    REQUIRE(stems.size() == 1);  // the PNG stream is not audio
    CHECK(stems[0].bytes == ogg);

    // A bundle with no trailing streams yields no audio, and never throws.
    std::string bare = fixture_dir() + "\\bare.srb";
    write_bytes(bare, make_srb(notes, {}));
    CHECK(extract_srb_audio(bare).empty());

    PreviewSource src = resolve_preview_source(path, true, true);
    CHECK_FALSE(src.song.is_empty());
    CHECK(src.stems.size() == 1);
}

TEST_CASE("resolve_preview_source: a .srb with no extractable audio falls "
          "back to a loose file beside it") {
    // Mirrors a real Clone Hero bundle: extract_srb_audio finds nothing
    // (its audio is encrypted, outside the DEFLATE chain this reads), so
    // resolve_preview_source should still find a stem placed next to it.
    std::string dir = make_subdir("srb_fallback");
    std::vector<uint8_t> notes = hydra::read_file_bytes(
        corpus::first_chart_with_suffix(".mid"));
    write_bytes(dir + "\\bundle.srb", make_srb(notes, {}));
    write_bytes(dir + "\\song.ogg", bytes_of("OggS fallback audio"));

    CHECK(extract_srb_audio(dir + "\\bundle.srb").empty());

    PreviewSource src = resolve_preview_source(dir + "\\bundle.srb", true, true);
    CHECK_FALSE(src.song.is_empty());
    REQUIRE(src.stems.size() == 1);
    CHECK(src.stems[0].label == "song");
    CHECK(src.stems[0].from_file());
}

TEST_CASE("resolve_preview_source: a loose chart parses and finds its audio") {
    std::string dir = make_subdir("resolve_loose");
    std::vector<uint8_t> notes = hydra::read_file_bytes(
        corpus::first_chart_with_suffix(".chart"));
    write_bytes(dir + "\\notes.chart", notes);
    write_bytes(dir + "\\song.ogg", bytes_of("OggS audio"));

    PreviewSource src = resolve_preview_source(dir + "\\notes.chart", true, true);
    CHECK_FALSE(src.song.is_empty());
    REQUIRE(src.stems.size() == 1);
    CHECK(src.stems[0].label == "song");
    CHECK(src.stems[0].from_file());
}

TEST_CASE("containers pass the difficulty through to the chart inside") {
    // .sng and .srb are pure containers: they hand the extracted notes to the
    // .mid/.chart loader, so the difficulty has to survive the trip. The
    // fixture chart has four deliberately different difficulty sections.
    const std::vector<uint8_t> notes = multidiff::chart_bytes();

    std::string sng = fixture_dir() + "\\multidiff.sng";
    write_bytes(sng, make_sng({{"notes.chart", notes},
                               {"song.ogg", bytes_of("OggS audio")}}));

    std::string srb = fixture_dir() + "\\multidiff.srb";
    write_bytes(srb, make_srb(notes, {}, "notes.chart"));

    for (const std::string& path : {sng, srb}) {
        CHECK(resolve_preview_source(path, true, true).song.sequence.size() ==
              multidiff::kExpertChords);
        CHECK(resolve_preview_source(path, true, true, Difficulty::Hard)
                  .song.sequence.size() == multidiff::kHardChords);
        CHECK(resolve_preview_source(path, true, true, Difficulty::Easy)
                  .song.sequence.size() == multidiff::kEasyChords);
        // No [MediumDrums] section: an empty song, not a fallback.
        CHECK(resolve_preview_source(path, true, true, Difficulty::Medium)
                  .song.is_empty());
    }

    // The same, straight through the loaders (no preview resolution involved).
    CHECK(load_songpath_sng(sng, true, true, Difficulty::Hard).sequence.size() ==
          multidiff::kHardChords);
    CHECK(load_songpath_srb(srb, true, true, Difficulty::Hard).sequence.size() ==
          multidiff::kHardChords);
}

TEST_CASE("read_ini_delay_ms reads song.ini delay in milliseconds") {
    const std::string dir = make_subdir("ini_delay");
    const std::string ini = dir + "\\song.ini";

    write_bytes(ini, bytes_of("[song]\nname = X\ndelay = 1016\n"));
    REQUIRE(read_ini_delay_ms(ini).has_value());
    CHECK(*read_ini_delay_ms(ini) == doctest::Approx(1016.0));

    write_bytes(ini, bytes_of("[Song]\r\nDelay = -250\r\n"));  // any case, CRLF
    REQUIRE(read_ini_delay_ms(ini).has_value());
    CHECK(*read_ini_delay_ms(ini) == doctest::Approx(-250.0));

    write_bytes(ini, bytes_of("[song]\nname = X\n"));
    CHECK_FALSE(read_ini_delay_ms(ini).has_value());

    write_bytes(ini, bytes_of("[song]\ndelay = soon\n"));
    CHECK_FALSE(read_ini_delay_ms(ini).has_value());

    CHECK_FALSE(read_ini_delay_ms(dir + "\\missing.ini").has_value());
}

TEST_CASE("preview_audio_offset_ms combines delay and Offset") {
    CHECK(preview_audio_offset_ms(std::nullopt, std::nullopt) == doctest::Approx(0.0));
    CHECK(preview_audio_offset_ms(1016.0, std::nullopt) == doctest::Approx(1016.0));
    CHECK(preview_audio_offset_ms(std::nullopt, 0.25) == doctest::Approx(250.0));
    // Lunaris: delay = 0 in song.ini, Offset = 0.25 in the chart. A delay of 0
    // counts as unset, so the Offset applies (measured +248 ms at the game).
    CHECK(preview_audio_offset_ms(0.0, 0.25) == doctest::Approx(250.0));
    // The delay-500 copy of Lunaris from Task 17 step 1: both set, and the
    // nonzero delay replaces the Offset (measured +496 ms at the game).
    CHECK(preview_audio_offset_ms(500.0, 0.25) == doctest::Approx(500.0));
}

TEST_CASE("resolve_preview_source reads delay from a song.ini in any case") {
    const std::string dir = make_subdir("ini_delay_case");
    write_bytes(dir + "\\notes.chart", multidiff::chart_bytes());
    write_bytes(dir + "\\Song.INI", bytes_of("[song]\ndelay = 500\n"));
    const PreviewSource src =
        resolve_preview_source(dir + "\\notes.chart", true, true, Difficulty::Expert);
    CHECK(src.audio_offset_ms == doctest::Approx(500.0));
}

TEST_CASE("sng_delay_ms reads the delay key in any case") {
    const std::vector<uint8_t> notes = multidiff::chart_bytes();
    const std::vector<uint8_t> upper = make_sng({{"notes.chart", notes}}, {{"DELAY", "-120"}});
    REQUIRE(sng_delay_ms(upper).has_value());
    CHECK(*sng_delay_ms(upper) == doctest::Approx(-120.0));

    CHECK_FALSE(sng_delay_ms(make_sng({{"notes.chart", notes}}, {{"delay", "soon"}})).has_value());
    CHECK_FALSE(sng_delay_ms(make_sng({{"notes.chart", notes}}, {{"name", "X"}})).has_value());
    CHECK_FALSE(sng_delay_ms(make_sng({{"notes.chart", notes}})).has_value());
}

TEST_CASE("resolve_preview_source: a .sng's metadata delay replaces the chart Offset") {
    const std::vector<uint8_t> notes = chart_with_offset("0.25");

    const std::string with_delay = fixture_dir() + "\\delay500.sng";
    write_bytes(with_delay, make_sng({{"notes.chart", notes}}, {{"name", "X"}, {"delay", "500"}}));
    CHECK(resolve_preview_source(with_delay, true, true).audio_offset_ms ==
          doctest::Approx(500.0));

    // A delay of 0 counts as unset (the Lunaris rule), whatever the key's case.
    const std::string zero_delay = fixture_dir() + "\\delay0.sng";
    write_bytes(zero_delay, make_sng({{"notes.chart", notes}}, {{"Delay", "0"}}));
    CHECK(resolve_preview_source(zero_delay, true, true).audio_offset_ms ==
          doctest::Approx(250.0));

    // No delay key at all: the chart's Offset applies.
    const std::string no_delay = fixture_dir() + "\\nodelay.sng";
    write_bytes(no_delay, make_sng({{"notes.chart", notes}}, {{"name", "X"}}));
    CHECK(resolve_preview_source(no_delay, true, true).audio_offset_ms ==
          doctest::Approx(250.0));
}

TEST_CASE("resolve_preview_source: a .srb uses its chart's Offset") {
    const std::string path = fixture_dir() + "\\offset.srb";
    write_bytes(path, make_srb(chart_with_offset("0.25"), {}, "notes.chart"));
    CHECK(resolve_preview_source(path, true, true).audio_offset_ms ==
          doctest::Approx(250.0));
}
