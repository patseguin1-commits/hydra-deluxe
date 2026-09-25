// Per-pad ghost/accent/normal note counts for a parsed chart.
//
// count_dynamics walks the Song's sequence once and bins every ChordNote by
// pad, cymbal flag, and dynamic type. The GUI (task 2) draws the result;
// this module is pure data, no UI dependency.

#ifndef HYDRA_APP_DYNAMICS_BREAKDOWN_H
#define HYDRA_APP_DYNAMICS_BREAKDOWN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "parse/song.h"
#include "store/record_store.h"

namespace hydra {
namespace app {

struct DynamicsCounts {
    int ghost = 0, accent = 0, normal = 0;
    int all() const { return ghost + accent + normal; }
    bool has_dynamics() const { return ghost + accent > 0; }
};

enum class DynamicsRow {
    RedSnare,
    YellowCymbal,
    YellowTom,
    BlueCymbal,
    BlueTom,
    GreenCymbal,
    GreenTom,
    Kick,
    Kick2x,
    Count
};

struct DynamicsBreakdown {
    std::array<DynamicsCounts, static_cast<size_t>(DynamicsRow::Count)> rows{};
    bool dynamics_enabled = false;

    const DynamicsCounts& row(DynamicsRow r) const;
    DynamicsCounts pads_total() const;
    DynamicsCounts kicks_total() const;
    DynamicsCounts played_total(bool bass2x) const;
};

const char* dynamics_row_label(DynamicsRow r, bool pro);

DynamicsBreakdown count_dynamics(const Song& song);

// Versioned binary encoding for storage in the dynamics table (record_store.h).
// Version byte 1, then dynamics_enabled (1 byte), then the nine rows in
// DynamicsRow order, each as ghost/accent/normal (little-endian uint32).
std::vector<uint8_t> encode_dynamics(const DynamicsBreakdown& b);

// Returns nullopt on an unknown version or data too short.
std::optional<DynamicsBreakdown> decode_dynamics(const std::vector<uint8_t>& blob);

// ---- the cache rules, in one place ----------------------------------------

// The Dynamics tab's background count always parses with 2x kicks kept, so
// the "2x kick" row is known even while the "2x Bass" box is off.
constexpr bool kDynamicsParseBass2x = true;

// The in-memory key of one count: the chart file, the pro-drums view and the
// difficulty, as "path|pro|Expert" or "path|std|Hard".
std::string dynamics_cache_key(const std::string& notespath, bool pro, Difficulty difficulty);

// The stored-row key for one count.
store::DynamicsKey dynamics_store_key(const std::string& md5, Difficulty difficulty, bool pro);

// After an analysis, store its dynamics counts as a free by-product (the
// chart is already parsed). Stores only when the analysis parsed with bass2x
// on: with it off the parse dropped the 2x kicks and the counts would be
// incomplete. Best effort: a failed save is swallowed so it can never block
// the analysis record.
void store_dynamics_from_analysis(store::RecordStore& store, const std::string& md5,
                                  const Song& song, bool bass2x, Difficulty difficulty, bool pro);

}  // namespace app
}  // namespace hydra

#endif  // HYDRA_APP_DYNAMICS_BREAKDOWN_H
