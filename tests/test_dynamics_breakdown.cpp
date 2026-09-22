// Tests for app/dynamics_breakdown: per-pad ghost/accent/normal counting.

#include "doctest.h"

#include "app/dynamics_breakdown.h"
#include "core/model.h"
#include "parse/song.h"

#ifndef HYDRA_TESTDATA_DIR
#error "HYDRA_TESTDATA_DIR must be defined (see CMakeLists.txt)"
#endif

using namespace hydra;
using namespace hydra::app;

// ---------------------------------------------------------------------------
// Helper: build a minimal Song with hand-picked notes.
// ---------------------------------------------------------------------------

namespace {

// Append a single-note chord to the song at the given tick.
void add_note(Song& song, int64_t tick, NoteColor color,
              NoteDynamicType dyn = NoteDynamicType::Normal,
              NoteCymbalType cym = NoteCymbalType::Normal,
              bool is2x = false) {
    SongTimestamp ts;
    ts.timecode = song.timecode(tick);
    ChordNote& n = ts.chord.add_note(color);
    n.dynamictype = dyn;
    n.cymbaltype = cym;
    n.is2x = is2x;
    song.sequence.push_back(ts);
}

Song make_test_song() {
    Song song(480);
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    song.dynamics_enabled = true;

    // Red snare: 2 ghost, 1 accent, 3 normal
    add_note(song, 0,    NoteColor::Red, NoteDynamicType::Ghost);
    add_note(song, 480,  NoteColor::Red, NoteDynamicType::Ghost);
    add_note(song, 960,  NoteColor::Red, NoteDynamicType::Accent);
    add_note(song, 1440, NoteColor::Red);
    add_note(song, 1920, NoteColor::Red);
    add_note(song, 2400, NoteColor::Red);

    // Yellow cymbal: 1 accent, 2 normal
    add_note(song, 2880, NoteColor::Yellow, NoteDynamicType::Accent,
             NoteCymbalType::Cymbal);
    add_note(song, 3360, NoteColor::Yellow, NoteDynamicType::Normal,
             NoteCymbalType::Cymbal);
    add_note(song, 3840, NoteColor::Yellow, NoteDynamicType::Normal,
             NoteCymbalType::Cymbal);

    // Yellow tom: 1 normal
    add_note(song, 4320, NoteColor::Yellow, NoteDynamicType::Normal,
             NoteCymbalType::Normal);

    // Blue tom: 1 ghost
    add_note(song, 4800, NoteColor::Blue, NoteDynamicType::Ghost,
             NoteCymbalType::Normal);

    // Kick: 2 normal, 1 ghost
    add_note(song, 5280, NoteColor::Kick);
    add_note(song, 5760, NoteColor::Kick);
    add_note(song, 6240, NoteColor::Kick, NoteDynamicType::Ghost);

    // Kick 2x: 1 normal
    add_note(song, 6720, NoteColor::Kick, NoteDynamicType::Normal,
             NoteCymbalType::Normal, /*is2x=*/true);

    return song;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: hand-built Song gives expected per-row counts.
// ---------------------------------------------------------------------------

TEST_CASE("dynamics_breakdown: hand-built song row counts") {
    const Song song = make_test_song();
    const DynamicsBreakdown bd = count_dynamics(song);

    CHECK(bd.dynamics_enabled);

    // Red snare
    CHECK(bd.row(DynamicsRow::RedSnare).ghost  == 2);
    CHECK(bd.row(DynamicsRow::RedSnare).accent == 1);
    CHECK(bd.row(DynamicsRow::RedSnare).normal == 3);
    CHECK(bd.row(DynamicsRow::RedSnare).all()  == 6);

    // Yellow cymbal
    CHECK(bd.row(DynamicsRow::YellowCymbal).ghost  == 0);
    CHECK(bd.row(DynamicsRow::YellowCymbal).accent == 1);
    CHECK(bd.row(DynamicsRow::YellowCymbal).normal == 2);

    // Yellow tom
    CHECK(bd.row(DynamicsRow::YellowTom).all() == 1);

    // Blue tom (ghost)
    CHECK(bd.row(DynamicsRow::BlueTom).ghost == 1);
    CHECK(bd.row(DynamicsRow::BlueTom).all() == 1);

    // Blue cymbal, Green cymbal, Green tom — untouched
    CHECK(bd.row(DynamicsRow::BlueCymbal).all()  == 0);
    CHECK(bd.row(DynamicsRow::GreenCymbal).all() == 0);
    CHECK(bd.row(DynamicsRow::GreenTom).all()    == 0);

    // Kick: 1 ghost + 2 normal
    CHECK(bd.row(DynamicsRow::Kick).ghost  == 1);
    CHECK(bd.row(DynamicsRow::Kick).normal == 2);
    CHECK(bd.row(DynamicsRow::Kick).all()  == 3);

    // Kick 2x: 1 normal
    CHECK(bd.row(DynamicsRow::Kick2x).normal == 1);
    CHECK(bd.row(DynamicsRow::Kick2x).all()  == 1);

    CHECK(bd.row(DynamicsRow::RedSnare).has_dynamics());
    CHECK_FALSE(bd.row(DynamicsRow::GreenTom).has_dynamics());
}

// ---------------------------------------------------------------------------
// Test 2: played_total with and without bass2x.
// ---------------------------------------------------------------------------

TEST_CASE("dynamics_breakdown: played_total includes/excludes 2x kick") {
    const Song song = make_test_song();
    const DynamicsBreakdown bd = count_dynamics(song);

    const DynamicsCounts pads = bd.pads_total();
    // pads = RedSnare(6) + YCym(3) + YTom(1) + BCym(0) + BTom(1) + GCym(0) + GTom(0) = 11
    CHECK(pads.all() == 11);

    // played_total(false) = pads(11) + Kick(3) = 14, excludes Kick2x
    CHECK(bd.played_total(false).all() == 14);

    // played_total(true) = pads(11) + Kick(3) + Kick2x(1) = 15
    CHECK(bd.played_total(true).all() == 15);

    // Ghost totals: pads have 2(red)+1(blue tom)=3, kick has 1, kick2x has 0.
    CHECK(bd.played_total(false).ghost == 4);
    CHECK(bd.played_total(true).ghost  == 4);

    // kicks_total = Kick(3) + Kick2x(1)
    CHECK(bd.kicks_total().all() == 4);
}

// ---------------------------------------------------------------------------
// Test 3: real chart — Alpha Wolf "Acid Romance" (MIDI, pro, bass2x).
//
// Cross-checked with the standalone dyn.py MIDI counter.
// ---------------------------------------------------------------------------

TEST_CASE("dynamics_breakdown: Alpha Wolf - Acid Romance (real chart)") {
    const std::string path =
        std::string(HYDRA_TESTDATA_DIR) +
        "/input/common/IB24/T3/Alpha Wolf - Acid Romance/notes.mid";
    const Song song = load_songpath_mid(path, /*pro=*/true, /*bass2x=*/true);
    const DynamicsBreakdown bd = count_dynamics(song);

    CHECK(bd.dynamics_enabled);

    // Red snare: 5 ghost, 5 accent, 137 normal
    CHECK(bd.row(DynamicsRow::RedSnare).ghost  == 5);
    CHECK(bd.row(DynamicsRow::RedSnare).accent == 5);
    CHECK(bd.row(DynamicsRow::RedSnare).normal == 137);

    // Yellow cymbal: 0 ghost, 3 accent, 88 normal
    CHECK(bd.row(DynamicsRow::YellowCymbal).accent == 3);
    CHECK(bd.row(DynamicsRow::YellowCymbal).normal == 88);

    // Yellow tom: 0 ghost, 1 accent, 25 normal
    CHECK(bd.row(DynamicsRow::YellowTom).accent == 1);
    CHECK(bd.row(DynamicsRow::YellowTom).normal == 25);

    // Blue cymbal: 0 ghost, 2 accent, 133 normal
    CHECK(bd.row(DynamicsRow::BlueCymbal).accent == 2);
    CHECK(bd.row(DynamicsRow::BlueCymbal).normal == 133);

    // Blue tom: 32 normal, no dynamics
    CHECK(bd.row(DynamicsRow::BlueTom).normal == 32);
    CHECK_FALSE(bd.row(DynamicsRow::BlueTom).has_dynamics());

    // Green cymbal: 0 ghost, 1 accent, 60 normal
    CHECK(bd.row(DynamicsRow::GreenCymbal).accent == 1);
    CHECK(bd.row(DynamicsRow::GreenCymbal).normal == 60);

    // Green tom: 15 normal
    CHECK(bd.row(DynamicsRow::GreenTom).normal == 15);

    // Kick: 418 normal
    CHECK(bd.row(DynamicsRow::Kick).normal == 418);
    CHECK_FALSE(bd.row(DynamicsRow::Kick).has_dynamics());

    // Kick 2x: 37 normal
    CHECK(bd.row(DynamicsRow::Kick2x).normal == 37);

    // Total note count: 5+5+137 + 3+88 + 1+25 + 2+133 + 32 + 1+60 + 15 + 418 + 37 = 962
    CHECK(bd.played_total(true).all() == 962);
}

// ---------------------------------------------------------------------------
// Test 4: MIDI chart without ENABLE_CHART_DYNAMICS => dynamics_enabled false.
// ---------------------------------------------------------------------------

TEST_CASE("dynamics_breakdown: no ENABLE_CHART_DYNAMICS tag") {
    const std::string path =
        std::string(HYDRA_TESTDATA_DIR) +
        "/input/test_flammarker/flammarker_authored.mid";
    const Song song = load_songpath_mid(path, /*pro=*/true, /*bass2x=*/true);

    CHECK_FALSE(song.dynamics_enabled);

    const DynamicsBreakdown bd = count_dynamics(song);
    CHECK_FALSE(bd.dynamics_enabled);
}

// ---------------------------------------------------------------------------
// Labels.
// ---------------------------------------------------------------------------

TEST_CASE("dynamics_breakdown: row labels pro vs non-pro") {
    // Pro labels include instrument type.
    CHECK(std::string(dynamics_row_label(DynamicsRow::RedSnare, true)) == "Red snare");
    CHECK(std::string(dynamics_row_label(DynamicsRow::YellowCymbal, true)) == "Yellow cymbal");
    CHECK(std::string(dynamics_row_label(DynamicsRow::YellowTom, true)) == "Yellow tom");
    CHECK(std::string(dynamics_row_label(DynamicsRow::Kick2x, true)) == "2x kick");

    // Non-pro: no cymbal distinction; Y/B/G just get the color name.
    CHECK(std::string(dynamics_row_label(DynamicsRow::RedSnare, false)) == "Red");
    CHECK(std::string(dynamics_row_label(DynamicsRow::YellowTom, false)) == "Yellow");
    CHECK(std::string(dynamics_row_label(DynamicsRow::BlueTom, false)) == "Blue");
    CHECK(std::string(dynamics_row_label(DynamicsRow::GreenTom, false)) == "Green");
}
