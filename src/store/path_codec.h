// Content-addressed path storage — the flat half of the storage rework.
//
// serialize.h writes a whole record as one nested blob: every path, every
// variant, all inlined in tree order. This codec splits the same information
// in two.
//
//   * Each Path *node* becomes a flat, context-free payload: its own fields
//     and nothing about where it sits in the tree. Two nodes with the same
//     fields produce the same bytes, so they share one stored copy.
//   * A record's *structure* blob records the tree shape only — which node
//     hash is each root, which hashes hang off it as variants, and at which
//     var_point.
//
// A node payload is named by a 128-bit content hash of its bytes, so the same
// path stored twice (in two records, or twice in one) is written once. Nothing
// in a payload depends on the record it came from.
//
// Flat storage is safe because tree-contextual data is rebuilt on load, not
// read from the blob: Path::prepare_variants() overwrites each variant's six
// score fields, notecount and leftover_sp from its parent, and rebuilds
// variant_tail from var_point. It leaves skipped_ghosts and skipped_accents
// alone, which is why those two really are per-node data and are stored.
//
// A deserialized record carries raw-tick Timecodes only, exactly like
// read_record's — call restore_timecodes() with the song's SongTiming.
//
// CONSTRAINT: var_point is stored per *variant*, on the edge from a parent to
// a child, never on a root. Engine::emit_path only assigns var_point when
// depth != 0, so an engine-produced root always carries nullopt and nothing is
// lost. A hand-built record whose root path has a var_point would lose it here.

#ifndef HYDRA_STORE_PATH_CODEC_H
#define HYDRA_STORE_PATH_CODEC_H

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/model.h"
#include "store/serialize.h"

namespace hydra::store {

// Bumped when a node payload's layout changes. A payload is the write_path
// field order minus the variant list and var_point. Version 1 held
// activations in the kBlobFormatVersion == 3 layout; version 2 holds them in
// the version 4 layout (adds deact_tick); version 3 holds them in the
// version 5 layout (adds clamp_tick); version 4 holds them in the blob
// version 6 layout (adds sqout_tick and collected_phrase_ticks, ADR 0014).
// Version 5 is the version 4 byte layout, but every chord string is the
// lane-spelled Chord::code instead of the old lookup-table code (ADR 0015).
// Only version 5 is written, and only version 5 is decoded. An older node can
// be reached only through an older structure, and the store never decodes
// one of those (see structure_is_current in record_store.cpp), so the old read paths
// are dead.
constexpr uint32_t kPathNodeFormatVersion = 5;

// Bumped when the structure blob's layout changes, and also when the node
// layout it points at changes. Version 3 is the same byte layout as version
// 2, but a version-3 structure references node payloads in node format 3
// (activations carry clamp_tick). Version 4 puts the u64 rules fingerprint
// right after the version, and references node format 4. Version 5 is the
// version 4 layout referencing node format 5. This version (and,
// from version 4, the fingerprint after it) is what the store's Ready rule
// reads off a stored row, so bumping the node layout means bumping this too.
constexpr uint32_t kPathStructureFormatVersion = 5;

// The 128-bit content hash of a node payload, raw. The structure blob stores
// these 16 bytes; path_hash() renders the same value as lowercase hex.
using PathHashBytes = std::array<uint8_t, 16>;

// One node's flat payload: no variants, no var_point, no record context.
std::vector<uint8_t> encode_path_node(const Path& path);

// The inverse. The returned Path has no variants and no var_point — the
// structure blob supplies those. Throws SerializeError on a bad version or
// truncated bytes.
Path decode_path_node(const std::vector<uint8_t>& payload);

// MurmurHash3 x64 128 of the payload, as 32 lowercase hex characters.
std::string path_hash(const std::vector<uint8_t>& payload);
PathHashBytes path_hash_bytes(const std::vector<uint8_t>& payload);
std::string hash_to_hex(const PathHashBytes& hash);

// A node as stored: its content hash (hex) and the bytes that hash names.
struct StoredPathNode {
    std::string hash;
    std::vector<uint8_t> payload;
};

// A record taken apart: the tree shape plus every distinct node it references.
struct FlatRecord {
    std::vector<uint8_t> structure;
    // Deduplicated, in first-seen order (roots depth-first, then allzero
    // paths). Every hash the structure blob names appears exactly once.
    std::vector<StoredPathNode> nodes;
};

// Takes a record apart. Covers roots, their variants recursively, and the
// allzero paths with theirs.
FlatRecord flatten_record(const HydraRecord& record);

// Hands back the payload for a hash (hex), or nullptr when it is not stored.
using PathNodeLookup =
    std::function<const std::vector<uint8_t>*(const std::string& hash)>;

// Puts a record back together: resolves every node through `lookup`, rewires
// the variant tree and var_points, then runs the same post-passes read_record
// runs (recount_tied_paths, then prepare_variants), in the same order. Throws
// SerializeError on an unknown hash, a bad version, or malformed bytes.
HydraRecord rebuild_record(const std::vector<uint8_t>& structure,
                           const PathNodeLookup& lookup);

// The round-trip convenience: rebuild straight from what flatten produced.
HydraRecord rebuild_record(const FlatRecord& flat);

}  // namespace hydra::store

#endif  // HYDRA_STORE_PATH_CODEC_H
