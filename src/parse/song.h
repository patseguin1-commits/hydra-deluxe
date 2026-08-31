// Chart parsing — the C++ port of hydra/hysong.py.
//
// Turns a .mid/.chart/.sng/.srb file into a Song: a tick-ordered sequence of
// SongTimestamps (a Timecode + Chord + solo/SP/fill flags), plus the tempo and
// meter maps. The MidiParser and ChartParser live in the .cpp; callers use the
// load_songpath_* functions.
//
// Every load_* function takes the difficulty to read and defaults to Expert.

#ifndef HYDRA_PARSE_SONG_H
#define HYDRA_PARSE_SONG_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/timing.h"

namespace hydra {

// Which charted difficulty to read. One chart carries all four: a .mid keeps
// them in the single "PART DRUMS" track at four pitch bases, a .chart keeps
// them in four named sections. Expert is the default everywhere, so passing
// nothing reads exactly what Hydra always read.
enum class Difficulty { Expert, Hard, Medium, Easy };

// "Expert" / "Hard" / "Medium" / "Easy" — the name used in the .chart section
// name, the chartmode key, and the user-facing error strings.
const char* difficulty_name(Difficulty difficulty);

// A timecode paired with a chord and gameplay modifiers, mirroring
// hysong.SongTimestamp.
struct SongTimestamp {
    Timecode timecode;
    Chord chord;
    bool flag_solo = false;
    bool flag_sp = false;
    std::optional<int64_t> activation_length;
    // On the note that ends an SP phrase (flag_sp == true), the tick the phrase
    // began at. The parser otherwise discards the phrase start once the end
    // flag is set; the Preview needs the whole span to shade the phrase, so it
    // is kept here. nullopt on every other note. Not part of the search or the
    // stored record — a display-only addition.
    std::optional<int64_t> sp_phrase_start;

    bool has_activation() const { return activation_length.has_value(); }
};

// A practice section ("Verse 2B", "Chorus 1"), from the chart's own section
// markers. Display only: nothing in the search or the stored record reads these.
struct SongSection {
    int64_t tick = 0;
    std::string name;
};

// A parsed chart: the timestamp sequence plus the tempo/meter maps it was built
// from. Timing is snapshotted once the maps are complete (build_timing), which
// mirrors Python building timecodes only after the whole tempo track is read.
class Song {
public:
    explicit Song(int64_t resolution) : tick_resolution_(resolution) {
        tpm_changes[0] = resolution * 4;
    }

    int64_t tick_resolution() const { return tick_resolution_; }

    // Tick-keyed meter (ticks per measure) and tempo (BPM) maps, filled during
    // parsing. std::map keeps them sorted, which the timing indexes rely on.
    std::map<int64_t, int64_t> tpm_changes;
    std::map<int64_t, double> bpm_changes;

    std::vector<SongTimestamp> sequence;
    std::vector<std::string> features;

    // Practice sections in tick order. A section marker can sit past the last
    // note, so these ticks are not bounded by the sequence.
    std::vector<SongSection> practice_sections;

    // Snapshot the timing indexes from the current maps. Called once the tempo
    // track has been fully mapped and before any timecode is made.
    void build_timing() {
        timing_.emplace(tick_resolution_, tpm_changes, bpm_changes);
    }
    const SongTiming& timing() const { return *timing_; }
    Timecode timecode(int64_t ticks) const { return timing_->timecode(ticks); }
    Timecode start_time() const { return timing_->timecode(0); }

    bool is_empty() const { return sequence.empty(); }

    // How many SP phrases the chart offers. No run can bank more bars than
    // this, so it's the natural clamp on how tall a search graph is worth
    // building.
    int sp_phrase_count() const;

    // If the chart has no drum fills, synthesize them like Clone Hero would.
    void check_activations();

private:
    int64_t tick_resolution_;
    std::optional<SongTiming> timing_;
};

Song load_songpath_mid(const std::string& path, bool pro, bool bass2x,
                       Difficulty difficulty = Difficulty::Expert);
Song load_songpath_chart(const std::string& path, bool pro, bool bass2x,
                         Difficulty difficulty = Difficulty::Expert);
Song load_songpath_sng(const std::string& path, bool pro, bool bass2x,
                       Difficulty difficulty = Difficulty::Expert);
Song load_songpath_srb(const std::string& path, bool pro, bool bass2x,
                       Difficulty difficulty = Difficulty::Expert);

Song load_songbytes_mid(const std::vector<uint8_t>& data, bool pro, bool bass2x,
                        Difficulty difficulty = Difficulty::Expert);
Song load_songbytes_chart(const std::vector<uint8_t>& data, bool pro, bool bass2x,
                          Difficulty difficulty = Difficulty::Expert);

// Dispatch on the file extension (.mid/.chart/.sng/.srb, case-insensitive).
Song load_songpath(const std::string& path, bool pro, bool bass2x,
                   Difficulty difficulty = Difficulty::Expert);

}  // namespace hydra

#endif  // HYDRA_PARSE_SONG_H
