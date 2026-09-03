// The per-node wire layout shared by the two path writers.
//
// serialize.cpp lays a whole record out as one nested blob; path_codec.cpp
// lays each node out on its own, flat and context-free. Both must spell a
// node's fields the same way, byte for byte, or a record written by one and
// read by the other would silently disagree. So the field-level code lives
// here once and both include it — there is no second copy to drift.
//
// "Node body" means everything write_path emits for a single Path except the
// tree links (the variant list and var_point). Those are the one part the two
// formats deliberately spell differently: serialize nests them inline,
// path_codec lifts them into a separate structure blob.
//
// Internal to src/store/; not part of the public store API.

#ifndef HYDRA_STORE_PATH_BINARY_H
#define HYDRA_STORE_PATH_BINARY_H

#include <cstdint>

#include "core/model.h"
#include "store/serialize.h"

namespace hydra::store::detail {

// One activation. `version` is the record blob format version: 3 and later
// append the four frontend transfer scales, 4 and later the deact_tick (see
// kBlobFormatVersion).
void write_activation(BinaryWriter& w, const Activation& act, uint32_t version);
Activation read_activation(BinaryReader& r, uint32_t version);

// A path's own fields: multsqueezes, activations, the six score totals,
// notecount, leftover_sp, skipped_ghosts, skipped_accents. Nothing about the
// variant tree.
void write_path_node(BinaryWriter& w, const Path& path, uint32_t version);
void read_path_node(BinaryReader& r, Path& path, uint32_t version);

}  // namespace hydra::store::detail

#endif  // HYDRA_STORE_PATH_BINARY_H
