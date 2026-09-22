#include "app/dynamics_breakdown.h"

#include <cstring>

#include "core/model.h"
#include "parse/song.h"

namespace hydra {
namespace app {

// ---- DynamicsBreakdown --------------------------------------------------

const DynamicsCounts& DynamicsBreakdown::row(DynamicsRow r) const {
    return rows[static_cast<size_t>(r)];
}

DynamicsCounts DynamicsBreakdown::pads_total() const {
    DynamicsCounts t;
    for (size_t i = 0; i <= static_cast<size_t>(DynamicsRow::GreenTom); ++i) {
        t.ghost += rows[i].ghost;
        t.accent += rows[i].accent;
        t.normal += rows[i].normal;
    }
    return t;
}

DynamicsCounts DynamicsBreakdown::kicks_total() const {
    DynamicsCounts t;
    const auto& k = row(DynamicsRow::Kick);
    const auto& k2 = row(DynamicsRow::Kick2x);
    t.ghost = k.ghost + k2.ghost;
    t.accent = k.accent + k2.accent;
    t.normal = k.normal + k2.normal;
    return t;
}

DynamicsCounts DynamicsBreakdown::played_total(bool bass2x) const {
    DynamicsCounts t = pads_total();
    const auto& k = row(DynamicsRow::Kick);
    t.ghost += k.ghost;
    t.accent += k.accent;
    t.normal += k.normal;
    if (bass2x) {
        const auto& k2 = row(DynamicsRow::Kick2x);
        t.ghost += k2.ghost;
        t.accent += k2.accent;
        t.normal += k2.normal;
    }
    return t;
}

// ---- labels -------------------------------------------------------------

const char* dynamics_row_label(DynamicsRow r, bool pro) {
    switch (r) {
        case DynamicsRow::RedSnare:
            return pro ? "Red snare" : "Red";
        case DynamicsRow::YellowCymbal:
            return "Yellow cymbal";
        case DynamicsRow::YellowTom:
            return pro ? "Yellow tom" : "Yellow";
        case DynamicsRow::BlueCymbal:
            return "Blue cymbal";
        case DynamicsRow::BlueTom:
            return pro ? "Blue tom" : "Blue";
        case DynamicsRow::GreenCymbal:
            return "Green cymbal";
        case DynamicsRow::GreenTom:
            return pro ? "Green tom" : "Green";
        case DynamicsRow::Kick:
            return "Kick";
        case DynamicsRow::Kick2x:
            return "2x kick";
        default:
            return "";
    }
}

// ---- counting -----------------------------------------------------------

namespace {

DynamicsRow row_for(const ChordNote& note) {
    switch (note.colortype) {
        case NoteColor::Kick:
            return note.is2x ? DynamicsRow::Kick2x : DynamicsRow::Kick;
        case NoteColor::Red:
            return DynamicsRow::RedSnare;
        case NoteColor::Yellow:
            return note.is_cymbal() ? DynamicsRow::YellowCymbal
                                    : DynamicsRow::YellowTom;
        case NoteColor::Blue:
            return note.is_cymbal() ? DynamicsRow::BlueCymbal
                                    : DynamicsRow::BlueTom;
        case NoteColor::Green:
            return note.is_cymbal() ? DynamicsRow::GreenCymbal
                                    : DynamicsRow::GreenTom;
    }
    return DynamicsRow::Kick;  // unreachable
}

}  // namespace

DynamicsBreakdown count_dynamics(const Song& song) {
    DynamicsBreakdown bd;
    bd.dynamics_enabled = song.dynamics_enabled;

    for (const SongTimestamp& ts : song.sequence) {
        for (const ChordNote& note : ts.chord.notes()) {
            DynamicsCounts& c = bd.rows[static_cast<size_t>(row_for(note))];
            switch (note.dynamictype) {
                case NoteDynamicType::Ghost:  ++c.ghost;  break;
                case NoteDynamicType::Accent: ++c.accent; break;
                case NoteDynamicType::Normal: ++c.normal; break;
            }
        }
    }
    return bd;
}

// ---- encode / decode --------------------------------------------------------

namespace {

constexpr uint8_t kDynamicsBlobVersion = 1;
constexpr size_t kRowCount = static_cast<size_t>(DynamicsRow::Count);  // 9
// version(1) + dynamics_enabled(1) + 9 rows * 3 fields * 4 bytes = 110
constexpr size_t kDynamicsBlobSize = 1 + 1 + kRowCount * 3 * 4;

void write_u32_le(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

std::vector<uint8_t> encode_dynamics(const DynamicsBreakdown& b) {
    std::vector<uint8_t> out;
    out.reserve(kDynamicsBlobSize);
    out.push_back(kDynamicsBlobVersion);
    out.push_back(b.dynamics_enabled ? 1 : 0);
    for (size_t i = 0; i < kRowCount; ++i) {
        write_u32_le(out, static_cast<uint32_t>(b.rows[i].ghost));
        write_u32_le(out, static_cast<uint32_t>(b.rows[i].accent));
        write_u32_le(out, static_cast<uint32_t>(b.rows[i].normal));
    }
    return out;
}

std::optional<DynamicsBreakdown> decode_dynamics(const std::vector<uint8_t>& blob) {
    if (blob.size() < kDynamicsBlobSize) return std::nullopt;
    if (blob[0] != kDynamicsBlobVersion) return std::nullopt;

    DynamicsBreakdown b;
    b.dynamics_enabled = blob[1] != 0;
    const uint8_t* p = blob.data() + 2;
    for (size_t i = 0; i < kRowCount; ++i) {
        b.rows[i].ghost  = static_cast<int>(read_u32_le(p));      p += 4;
        b.rows[i].accent = static_cast<int>(read_u32_le(p));      p += 4;
        b.rows[i].normal = static_cast<int>(read_u32_le(p));      p += 4;
    }
    return b;
}

}  // namespace app
}  // namespace hydra
