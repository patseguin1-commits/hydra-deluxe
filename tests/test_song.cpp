// Smoke test for parse/song: every chart in the corpus parses without
// throwing, and the resulting timestamp sequence holds the structural
// invariants the search depends on (monotone ticks, decodable chords,
// consistent activation-fill placement). Covers .mid, .chart, and .sng.

#include "doctest.h"

#include <cstdint>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/strutil.h"
#include "corpus_util.h"
#include "midi_util.h"
#include "multidiff_chart.h"
#include "parse/chart_files.h"
#include "parse/song.h"

#ifndef HYDRA_TESTDATA_DIR
#error "HYDRA_TESTDATA_DIR must be defined (see CMakeLists.txt)"
#endif

using namespace hydra;

namespace {

// The structural invariants the search depends on, checked on one parsed song.
// Returns false (with a message naming `what`) on the first violation.
void check_invariants(const Song& song, const std::string& what) {
    bool ticks_ok = true, codes_ok = true, fills_ok = true;
    int64_t prev = -1;
    for (const SongTimestamp& ts : song.sequence) {
        // Strictly increasing: two chords never share a tick.
        if (ts.timecode.ticks() <= prev) ticks_ok = false;
        prev = ts.timecode.ticks();

        // Every chord code decodes back to the same chord.
        const std::string code = ts.chord.code();
        if (code.empty() || Chord::from_code(code).code() != code) codes_ok = false;

        // An activation fill length is always positive.
        if (ts.activation_length.has_value() && *ts.activation_length <= 0)
            fills_ok = false;
    }
    CHECK_MESSAGE(ticks_ok, what << ": ticks not strictly increasing");
    CHECK_MESSAGE(codes_ok, what << ": chord code round-trip");
    CHECK_MESSAGE(fills_ok, what << ": non-positive activation fill");
}

}  // namespace

TEST_CASE("song parse holds its invariants over the corpus") {
    int charts = 0, nonempty = 0;
    for (const std::string& path : corpus::chart_paths()) {
        const Song& song = corpus::song(path, true, true);
        ++charts;
        if (song.is_empty()) continue;
        ++nonempty;
        check_invariants(song, path);
    }

    CHECK(nonempty > 0);
    MESSAGE("checked " << charts << " charts (" << nonempty << " non-empty)");
}

TEST_CASE("song parse holds its invariants at Hard too") {
    // Same sweep at Hard. Not every chart has Hard charting (an empty song is
    // the honest answer there), but plenty do, and whatever parses must hold
    // exactly the same structure Expert does.
    int charts = 0, nonempty = 0, mids = 0, mids_nonempty = 0;
    for (const std::string& path : corpus::chart_paths()) {
        const Song& song = corpus::song(path, true, true, Difficulty::Hard);
        ++charts;
        const bool is_mid = ends_with(path, ".mid");
        if (is_mid) ++mids;
        if (song.is_empty()) continue;
        ++nonempty;
        if (is_mid) ++mids_nonempty;
        check_invariants(song, path + " [Hard]");
    }

    // The corpus has 18 .chart files with a [HardDrums] section, so this is a
    // real sweep and not a silently-empty one.
    CHECK(nonempty > 0);
    // .mid charts keep every difficulty in the one "PART DRUMS" track, so a
    // corpus .mid with Hard notes proves the pitch base works end to end.
    CHECK(mids_nonempty > 0);
    MESSAGE("checked " << charts << " charts at Hard (" << nonempty << " non-empty, "
                       << mids_nonempty << "/" << mids << " .mid)");
}

TEST_CASE(".chart: each difficulty reads its own section") {
    const std::vector<uint8_t> data = multidiff::chart_bytes();

    Song expert = load_songbytes_chart(data, true, true);
    Song hard = load_songbytes_chart(data, true, true, Difficulty::Hard);
    Song medium = load_songbytes_chart(data, true, true, Difficulty::Medium);
    Song easy = load_songbytes_chart(data, true, true, Difficulty::Easy);

    CHECK(expert.sequence.size() == multidiff::kExpertChords);
    CHECK(hard.sequence.size() == multidiff::kHardChords);
    CHECK(easy.sequence.size() == multidiff::kEasyChords);
    // No [MediumDrums] section at all: a missing difficulty parses empty
    // rather than throwing or falling back to another difficulty.
    CHECK(medium.is_empty());
    CHECK(medium.sequence.size() == multidiff::kMediumChords);

    // The sections really are different charting, not the same notes read
    // four times.
    REQUIRE(!expert.sequence.empty());
    REQUIRE(!hard.sequence.empty());
    REQUIRE(!easy.sequence.empty());
    CHECK(expert.sequence[0].chord.code() != hard.sequence[0].chord.code());
    CHECK(easy.sequence[0].chord.code() != hard.sequence[0].chord.code());

    // Everything inside the section follows it: the ghost, the pro cymbal, the
    // SP phrase and the activation fill are all in [HardDrums] alone.
    CHECK(hard.sequence[0].flag_sp);
    bool any_activation = false;
    for (const SongTimestamp& ts : hard.sequence)
        if (ts.has_activation()) any_activation = true;
    CHECK(any_activation);
    // The fill was charted, so no fills had to be synthesized.
    CHECK(hard.features.empty());
    // Pro cymbals are still a pro-drums-only reading at Hard.
    Song hard_nonpro = load_songbytes_chart(data, false, true, Difficulty::Hard);
    CHECK(hard_nonpro.sequence[1].chord.code() != hard.sequence[1].chord.code());

    check_invariants(hard, "multidiff [Hard]");
    check_invariants(easy, "multidiff [Easy]");
}

TEST_CASE(".mid: each difficulty reads its own pitch base") {
    // Corpus .mid files that carry Hard notes parse to a different sequence
    // than their Expert one — the pitch base is what selects the difficulty.
    int compared = 0, differed = 0;
    for (const std::string& path : corpus::chart_paths()) {
        if (!ends_with(path, ".mid")) continue;
        Song expert = load_songpath_mid(path, true, true);
        Song hard = load_songpath_mid(path, true, true, Difficulty::Hard);
        if (hard.is_empty()) continue;
        ++compared;
        if (hard.sequence.size() != expert.sequence.size()) ++differed;
        check_invariants(hard, path + " [Hard]");
        // Medium and Easy sit at their own bases and must not throw either.
        check_invariants(load_songpath_mid(path, true, true, Difficulty::Medium),
                         path + " [Medium]");
        check_invariants(load_songpath_mid(path, true, true, Difficulty::Easy),
                         path + " [Easy]");
        if (compared >= 5) break;  // a handful is enough; the sweep covers the rest
    }
    CHECK(compared > 0);
    // Hard is a reduction of Expert, so the two streams are not the same one
    // read twice. (Per-chart the counts could coincide; across the sample they
    // cannot all coincide.)
    CHECK(differed > 0);
}

TEST_CASE(".chart: [Events] section markers become practice sections") {
    const std::string text =
        "[Song]\n"
        "{\n"
        "  Resolution = 192\n"
        "}\n"
        "[SyncTrack]\n"
        "{\n"
        "  0 = TS 4\n"
        "  0 = B 120000\n"
        "}\n"
        "[Events]\n"
        "{\n"
        "  1536 = E \"prc_chorus_1a\"\n"
        "  0 = E \"section Intro\"\n"
        "  768 = E \"section Verse 1\"\n"
        "  1920 = E \"lighting (blackout)\"\n"
        "}\n"
        "[ExpertDrums]\n"
        "{\n"
        "  0 = N 0 0\n"
        "  192 = N 1 0\n"
        "}\n";
    const std::vector<uint8_t> data(text.begin(), text.end());
    Song song = load_songbytes_chart(data, true, true);

    // Both spellings are read, non-section text events are not, and the file's
    // own order does not have to be sorted.
    REQUIRE(song.practice_sections.size() == 3);
    CHECK(song.practice_sections[0].tick == 0);
    CHECK(song.practice_sections[0].name == "Intro");
    CHECK(song.practice_sections[1].tick == 768);
    CHECK(song.practice_sections[1].name == "Verse 1");
    CHECK(song.practice_sections[2].tick == 1536);
    CHECK(song.practice_sections[2].name == "chorus_1a");

    // A chart with no [Events] section has no sections and still parses.
    Song plain = load_songbytes_chart(multidiff::chart_bytes(), true, true);
    CHECK(plain.practice_sections.empty());
}

// In a .chart the `E soloend` event sits on the solo's last note, so that note
// is in the solo. The parser runs solo end after the notes at its tick.
TEST_CASE(".chart: the note on the solo end tick is in the solo") {
    const std::string text =
        "[Song]\n{\n  Resolution = 192\n}\n"
        "[SyncTrack]\n{\n  0 = TS 4\n  0 = B 120000\n}\n"
        "[ExpertDrums]\n{\n"
        "  0 = E solo\n  0 = N 1 0\n"
        "  192 = N 2 0\n"
        "  384 = N 3 0\n  384 = E soloend\n"
        "  576 = N 4 0\n"
        "}\n";
    const std::vector<uint8_t> data(text.begin(), text.end());
    Song song = load_songbytes_chart(data, true, true);
    REQUIRE(song.sequence.size() == 4);
    CHECK(song.sequence[2].flag_solo);
    CHECK_FALSE(song.sequence[3].flag_solo);
}

// In a .mid the solo is a held marker note (103); its note-off tick is where
// the marker stops covering, so a note on that tick is outside the solo.
TEST_CASE(".mid: the note on the solo marker's note-off tick is outside the solo") {
    using namespace testmidi;
    // Delta 480 (one beat at 480 tpqn) as a two-byte variable-length number.
    const std::vector<uint8_t> beat = {0x83, 0x60};
    auto at_beat = [&](std::vector<uint8_t> ev) {  // replace the leading 0 delta
        ev.erase(ev.begin());
        std::vector<uint8_t> out = beat;
        out.insert(out.end(), ev.begin(), ev.end());
        return out;
    };
    const std::vector<uint8_t> track = concat({
        track_name("PART DRUMS"), set_tempo(),
        note_on(103, 100), note_on(97, 100),   // tick 0: solo on, Red
        at_beat(note_on(98, 100)),             // tick 480: Yellow
        at_beat(note_on(103, 0)),              // tick 960: solo marker off
        note_on(99, 100),                      // tick 960: Blue
        end_of_track(),
    });
    Song song = load_songbytes_mid(smf(track), true, true);
    REQUIRE(song.sequence.size() == 3);
    CHECK(song.sequence[0].flag_solo);
    CHECK(song.sequence[1].flag_solo);
    CHECK_FALSE(song.sequence[2].flag_solo);
}

namespace {

void put_varlen(std::vector<uint8_t>& out, uint32_t v) {
    uint8_t stack[5];
    int n = 0;
    do {
        stack[n++] = static_cast<uint8_t>(v & 0x7F);
        v >>= 7;
    } while (v != 0);
    while (n > 0) {
        --n;
        out.push_back(static_cast<uint8_t>(stack[n] | (n > 0 ? 0x80 : 0x00)));
    }
}

void put_meta(std::vector<uint8_t>& out, uint32_t delta, uint8_t type,
              const std::string& payload) {
    put_varlen(out, delta);
    out.push_back(0xFF);
    out.push_back(type);
    put_varlen(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

void put_bytes(std::vector<uint8_t>& out, std::initializer_list<int> bytes) {
    for (int b : bytes) out.push_back(static_cast<uint8_t>(b));
}

void put_track(std::vector<uint8_t>& file, const std::vector<uint8_t>& events) {
    const char* tag = "MTrk";
    file.insert(file.end(), tag, tag + 4);
    uint32_t len = static_cast<uint32_t>(events.size());
    for (int shift = 24; shift >= 0; shift -= 8)
        file.push_back(static_cast<uint8_t>((len >> shift) & 0xFF));
    file.insert(file.end(), events.begin(), events.end());
}

}  // namespace

TEST_CASE(".mid: EVENTS text metas become practice sections") {
    std::vector<uint8_t> tempo_track;
    put_meta(tempo_track, 0, 0x03, "tempo");
    put_varlen(tempo_track, 0);  // set_tempo 500000 us/qn = 120 BPM
    put_bytes(tempo_track, {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});
    put_meta(tempo_track, 0, 0x2F, "");

    std::vector<uint8_t> events_track;
    put_meta(events_track, 0, 0x03, "EVENTS");
    put_meta(events_track, 0, 0x01, "[section Intro]");
    put_meta(events_track, 384, 0x01, "[prc_verse_1]");
    put_meta(events_track, 384, 0x01, "[crowd_realtime]");
    put_meta(events_track, 0, 0x2F, "");

    std::vector<uint8_t> drums_track;
    put_meta(drums_track, 0, 0x03, "PART DRUMS");
    put_bytes(drums_track, {0x00, 0x90, 0x60, 0x64});  // note_on 96, Expert kick
    put_bytes(drums_track, {0x00, 0x80, 0x60, 0x00});
    put_meta(drums_track, 0, 0x2F, "");

    std::vector<uint8_t> file;
    put_bytes(file, {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 3, 0, 192});
    put_track(file, tempo_track);
    put_track(file, events_track);
    put_track(file, drums_track);

    Song song = load_songbytes_mid(file, true, true);
    REQUIRE(song.sequence.size() == 1);
    REQUIRE(song.practice_sections.size() == 2);
    CHECK(song.practice_sections[0].tick == 0);
    CHECK(song.practice_sections[0].name == "Intro");
    CHECK(song.practice_sections[1].tick == 384);
    CHECK(song.practice_sections[1].name == "verse_1");
}

TEST_CASE("mid: kick velocity is read as ghost/accent, like a pad's") {
    // Clone Hero prices a velocity-1 kick as a ghost and a velocity-127 kick
    // as an accent, both worth double. Hydra used to hand every kick Normal.
    auto drums_track = [](bool dynamics) {
        std::vector<std::vector<uint8_t>> ev;
        ev.push_back(testmidi::track_name("PART DRUMS"));
        ev.push_back(testmidi::set_tempo());
        if (dynamics)
            ev.push_back(testmidi::text_event("[ENABLE_CHART_DYNAMICS]"));
        ev.push_back(testmidi::note_on(96, 1));     // tick 0:   kick, vel 1
        ev.push_back({0x40, 0x90, 95, 127});        // tick 64:  2x kick, vel 127
        ev.push_back({0x40, 0x90, 97, 1});          // tick 128: Red pad, vel 1
        ev.push_back(testmidi::end_of_track());
        return testmidi::smf(testmidi::concat(ev));
    };

    SUBCASE("with [ENABLE_CHART_DYNAMICS]") {
        Song song = load_songbytes_mid(drums_track(true), true, true);
        REQUIRE(song.sequence.size() == 3);

        const auto& kick = song.sequence[0].chord.at(NoteColor::Kick);
        REQUIRE(kick.has_value());
        CHECK(kick->dynamictype == NoteDynamicType::Ghost);
        CHECK(kick->is2x == false);
        CHECK(kick->str() == "Kick (Ghost)");

        const auto& kick2x = song.sequence[1].chord.at(NoteColor::Kick);
        REQUIRE(kick2x.has_value());
        CHECK(kick2x->dynamictype == NoteDynamicType::Accent);
        CHECK(kick2x->is2x == true);
        CHECK(kick2x->str() == "Kick (Accent, 2x)");

        const auto& red = song.sequence[2].chord.at(NoteColor::Red);
        REQUIRE(red.has_value());
        CHECK(red->dynamictype == NoteDynamicType::Ghost);

        check_invariants(song, "kick dynamics fixture");
    }

    SUBCASE("without the text event, every note is Normal") {
        Song song = load_songbytes_mid(drums_track(false), true, true);
        REQUIRE(song.sequence.size() == 3);
        CHECK(song.sequence[0].chord.at(NoteColor::Kick)->dynamictype ==
              NoteDynamicType::Normal);
        CHECK(song.sequence[1].chord.at(NoteColor::Kick)->dynamictype ==
              NoteDynamicType::Normal);
        CHECK(song.sequence[1].chord.at(NoteColor::Kick)->is2x == true);
        CHECK(song.sequence[2].chord.at(NoteColor::Red)->dynamictype ==
              NoteDynamicType::Normal);
        check_invariants(song, "kick dynamics fixture (no marker)");
    }
}

TEST_CASE("mid: Won't Get Fooled Again (O) has 36 ghost kicks") {
    // The chart that proved the bug: Hydra called it 1,134,335 while a real FC
    // on Hydra's own path scored 1,142,235. 36 velocity-1 kicks, no accents.
    const std::string path =
        std::string(HYDRA_TESTDATA_DIR) + "/midi/wgfa_onyxite/notes.mid";
    Song song = load_songpath(path, true, true, Difficulty::Expert);
    REQUIRE(!song.is_empty());

    int ghost = 0, accent = 0, kicks = 0;
    for (const SongTimestamp& ts : song.sequence) {
        const auto& kick = ts.chord.at(NoteColor::Kick);
        if (!kick.has_value()) continue;
        ++kicks;
        if (kick->dynamictype == NoteDynamicType::Ghost) ++ghost;
        if (kick->dynamictype == NoteDynamicType::Accent) ++accent;
    }
    CHECK(kicks > 36);
    CHECK(ghost == 36);
    CHECK(accent == 0);

    // Every chord code still round-trips, which is what the new ghost-kick
    // codes have to prove. (The full check_invariants sweep is not used here:
    // this chart has a fill that starts on its own last note, so its
    // activation length is 0 — true before this change and unrelated to it.)
    bool codes_ok = true, ticks_ok = true;
    int64_t prev = -1;
    for (const SongTimestamp& ts : song.sequence) {
        if (ts.timecode.ticks() <= prev) ticks_ok = false;
        prev = ts.timecode.ticks();
        const std::string code = ts.chord.code();
        if (code.empty() || Chord::from_code(code).code() != code) codes_ok = false;
    }
    CHECK(codes_ok);
    CHECK(ticks_ok);
}

TEST_CASE("chart_files: loose-folder notes names match in any case") {
    CHECK(notes_file_format("notes.mid") == ChartFormat::Mid);
    CHECK(notes_file_format("NOTES.MID") == ChartFormat::Mid);
    CHECK(notes_file_format("Notes.Chart") == ChartFormat::Chart);
    CHECK(notes_file_format("notes.sng") == ChartFormat::None);
    CHECK(notes_file_format("mynotes.mid") == ChartFormat::None);
    CHECK(is_song_ini("song.ini"));
    CHECK(is_song_ini("Song.INI"));
    CHECK_FALSE(is_song_ini("song.ini.bak"));
}

TEST_CASE("chart_files: a path's format comes from its extension in any case") {
    CHECK(chart_format_of("C:\\songs\\a\\notes.mid") == ChartFormat::Mid);
    CHECK(chart_format_of("x.CHART") == ChartFormat::Chart);
    CHECK(chart_format_of("C:\\songs\\bundle.SNG") == ChartFormat::Sng);
    CHECK(chart_format_of("pack.Srb") == ChartFormat::Srb);
    CHECK(chart_format_of("notes.txt") == ChartFormat::None);
    CHECK(chart_format_of("mid") == ChartFormat::None);
}

TEST_CASE("mid: a stray SP note-off flags nothing") {
    std::vector<std::vector<uint8_t>> ev;
    ev.push_back(testmidi::track_name("PART DRUMS"));
    ev.push_back(testmidi::set_tempo());
    ev.push_back(testmidi::note_on(116, 100));       // tick 0:   SP phrase starts
    ev.push_back(testmidi::note_on(96, 100));        // tick 0:   kick, inside the phrase
    ev.push_back({0x81, 0x70, 0x80, 116, 0});        // tick 240: SP phrase ends
    ev.push_back({0x81, 0x70, 0x90, 96, 100});       // tick 480: kick, outside any phrase
    ev.push_back({0x81, 0x70, 0x80, 116, 0});        // tick 720: stray SP note-off
    ev.push_back(testmidi::end_of_track());
    Song song = load_songbytes_mid(testmidi::smf(testmidi::concat(ev)), true, true);

    REQUIRE(song.sequence.size() == 2);
    CHECK(song.sequence[0].flag_sp);
    CHECK_FALSE(song.sequence[1].flag_sp);
}

TEST_CASE(".chart: [Song] Offset is read in seconds") {
    std::string text = multidiff::chart_text();
    const std::string at = "  Resolution = 192\n";
    text.insert(text.find(at) + at.size(), "  Offset = 0.25\n");
    const std::vector<uint8_t> data(text.begin(), text.end());

    Song song = load_songbytes_chart(data, true, true);
    REQUIRE(song.chart_offset_s.has_value());
    CHECK(*song.chart_offset_s == doctest::Approx(0.25));

    Song plain = load_songbytes_chart(multidiff::chart_bytes(), true, true);
    CHECK_FALSE(plain.chart_offset_s.has_value());
}
