// Tests for parse/sng: the one reader of the .sng container layout, used by
// the note loader, the Preview's audio extractor and the library scan.

#include "doctest.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/winstr.h"
#include "midi_util.h"
#include "parse/sng.h"
#include "parse/song.h"

using namespace hydra;

namespace {

void push_u32(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void push_u64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// A .sng: 10 prefix bytes, the 16-byte XOR mask, the metadata block (pair
// count, then u32-length key and value strings), the file section (its
// length, the file count, then name/length/absolute-offset entries), then
// each file's bytes XOR-encoded from its own index 0.
std::vector<uint8_t> make_sng(
    const std::vector<std::pair<std::string, std::string>>& meta,
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::vector<uint8_t> out(10, 0x53);
    uint8_t mask[16];
    for (int i = 0; i < 16; ++i) mask[i] = static_cast<uint8_t>(0x30 + i * 7);
    out.insert(out.end(), mask, mask + 16);

    std::vector<uint8_t> md;
    push_u64(md, meta.size());
    for (const auto& [k, v] : meta) {
        push_u32(md, k.size());
        md.insert(md.end(), k.begin(), k.end());
        push_u32(md, v.size());
        md.insert(md.end(), v.begin(), v.end());
    }
    push_u64(out, md.size());
    out.insert(out.end(), md.begin(), md.end());

    size_t entries = 0;
    for (const auto& f : files) entries += 1 + f.first.size() + 16;
    uint64_t offset = out.size() + 16 + entries;
    push_u64(out, 8 + entries);
    push_u64(out, files.size());
    for (const auto& f : files) {
        out.push_back(static_cast<uint8_t>(f.first.size()));
        out.insert(out.end(), f.first.begin(), f.first.end());
        push_u64(out, f.second.size());
        push_u64(out, offset);
        offset += f.second.size();
    }
    for (const auto& f : files)
        for (size_t i = 0; i < f.second.size(); ++i)
            out.push_back(static_cast<uint8_t>(f.second[i] ^ mask[i % 16] ^ (i & 0xff)));
    return out;
}

std::vector<uint8_t> tiny_mid() {
    return testmidi::smf(testmidi::concat({testmidi::track_name("PART DRUMS"),
                                           testmidi::set_tempo(),
                                           testmidi::note_on(96, 100),
                                           {0x83, 0x60, 0x90, 97, 100},  // tick 480: red
                                           testmidi::end_of_track()}));
}

std::string sng_fixture_path(const char* name) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    return wide_to_utf8(tmp) + "hydra_sng_" + std::to_string(GetCurrentProcessId()) + "_" + name;
}

void write_fixture(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* f = fopen_utf8(path, L"wb");
    REQUIRE_MESSAGE(f != nullptr, "cannot write " << path);
    if (!bytes.empty()) std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

}  // namespace

TEST_CASE("sng: metadata pairs read back in order") {
    std::vector<uint8_t> buf = make_sng({{"name", "Song"}, {"Artist", "Band"}}, {});
    auto pairs = sng_read_metadata(buf);
    REQUIRE(pairs.size() == 2);
    CHECK(pairs[0] == std::make_pair(std::string("name"), std::string("Song")));
    CHECK(pairs[1] == std::make_pair(std::string("Artist"), std::string("Band")));
}

TEST_CASE("sng: the file table and each file decode") {
    const std::vector<uint8_t> mid = tiny_mid();
    const std::vector<uint8_t> ogg = {'O', 'g', 'g', 'S', 1, 2, 3};
    std::vector<uint8_t> buf = make_sng({{"name", "Song"}}, {{"notes.mid", mid}, {"song.ogg", ogg}});
    auto table = sng_read_file_table(buf);
    REQUIRE(table.size() == 2);
    CHECK(table[0].name == "notes.mid");
    CHECK(table[1].name == "song.ogg");
    auto got_mid = sng_decode_file(buf, table[0]);
    auto got_ogg = sng_decode_file(buf, table[1]);
    REQUIRE(got_mid.has_value());
    REQUIRE(got_ogg.has_value());
    CHECK(*got_mid == mid);
    CHECK(*got_ogg == ogg);
}

TEST_CASE("sng: truncated input stops early instead of reading past the end") {
    std::vector<uint8_t> whole = make_sng({{"name", "Song"}}, {{"notes.mid", tiny_mid()}});
    CHECK(sng_read_metadata({1, 2, 3}).empty());
    CHECK(sng_read_file_table({1, 2, 3}).empty());

    // Cut inside the file table: the entry is dropped, nothing is read past the end.
    std::vector<uint8_t> cut(whole.begin(), whole.begin() + (whole.size() - tiny_mid().size() - 4));
    CHECK(sng_read_file_table(cut).empty());

    // An entry whose offset + length wraps around is refused.
    SngFileEntry bad;
    bad.name = "notes.mid";
    bad.offset = 40;
    bad.length = UINT64_MAX - 10;
    CHECK_FALSE(sng_decode_file(whole, bad).has_value());
}

TEST_CASE("sng: the note loader reads the chart through the shared reader") {
    const std::string path = sng_fixture_path("loader.sng");
    write_fixture(path, make_sng({{"name", "Song"}}, {{"song.ogg", {1, 2}}, {"NOTES.MID", tiny_mid()}}));
    Song direct = load_songbytes_mid(tiny_mid(), true, true);
    Song via_sng = load_songpath_sng(path, true, true);
    REQUIRE(via_sng.sequence.size() == direct.sequence.size());
    for (size_t i = 0; i < direct.sequence.size(); ++i)
        CHECK(via_sng.sequence[i].timecode.ticks() == direct.sequence[i].timecode.ticks());

    // A file too short to hold a table throws a clear error.
    const std::string tiny = sng_fixture_path("tiny.sng");
    write_fixture(tiny, {1, 2, 3});
    CHECK_THROWS_AS(load_songpath_sng(tiny, true, true), std::runtime_error);
}
