// A record's bytes through the store's own writer: the structure blob, then
// every node payload in first-seen order. Two records that give the same
// bytes store the same thing, so tests use this as their equality proxy now
// that the whole-record blob format is gone (docs/adr/0017).

#ifndef HYDRA_TESTS_RECORD_BYTES_H
#define HYDRA_TESTS_RECORD_BYTES_H

#include <cstdint>
#include <vector>

#include "core/model.h"
#include "store/path_codec.h"

inline std::vector<uint8_t> record_bytes(const hydra::HydraRecord& record) {
    const hydra::store::FlatRecord flat = hydra::store::flatten_record(record);
    std::vector<uint8_t> out = flat.structure;
    for (const hydra::store::StoredPathNode& n : flat.nodes)
        out.insert(out.end(), n.payload.begin(), n.payload.end());
    return out;
}

#endif  // HYDRA_TESTS_RECORD_BYTES_H
