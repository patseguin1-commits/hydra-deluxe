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
#include <vector>

namespace hydra {

class Song;  // forward — defined in parse/song.h

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

}  // namespace app
}  // namespace hydra

#endif  // HYDRA_APP_DYNAMICS_BREAKDOWN_H
