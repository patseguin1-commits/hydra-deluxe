#include "store/path_codec.h"

#include <unordered_map>
#include <utility>

#include "store/path_binary.h"

namespace hydra::store {

namespace {

// ---- MurmurHash3 x64 128 --------------------------------------------------
//
// Austin Appleby's MurmurHash3, public domain (the author disclaims copyright).
// Dropped in whole rather than pulled from a dependency so the store's node
// naming has no outside owner. The block loads are spelled byte by byte, so
// the hash of a payload is the same value on any machine.

inline uint64_t rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

inline uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

inline uint64_t getblock64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}

void murmur3_x64_128(const uint8_t* data, size_t len, uint32_t seed,
                     uint8_t out[16]) {
    const size_t nblocks = len / 16;

    uint64_t h1 = seed;
    uint64_t h2 = seed;

    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;

    for (size_t i = 0; i < nblocks; ++i) {
        uint64_t k1 = getblock64(data + i * 16);
        uint64_t k2 = getblock64(data + i * 16 + 8);

        k1 *= c1; k1 = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
        h1 = rotl64(h1, 27); h1 += h2; h1 = h1 * 5 + 0x52dce729;

        k2 *= c2; k2 = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
        h2 = rotl64(h2, 31); h2 += h1; h2 = h2 * 5 + 0x38495ab5;
    }

    const uint8_t* tail = data + nblocks * 16;
    uint64_t k1 = 0;
    uint64_t k2 = 0;

    switch (len & 15) {
        case 15: k2 ^= static_cast<uint64_t>(tail[14]) << 48;  [[fallthrough]];
        case 14: k2 ^= static_cast<uint64_t>(tail[13]) << 40;  [[fallthrough]];
        case 13: k2 ^= static_cast<uint64_t>(tail[12]) << 32;  [[fallthrough]];
        case 12: k2 ^= static_cast<uint64_t>(tail[11]) << 24;  [[fallthrough]];
        case 11: k2 ^= static_cast<uint64_t>(tail[10]) << 16;  [[fallthrough]];
        case 10: k2 ^= static_cast<uint64_t>(tail[9]) << 8;    [[fallthrough]];
        case 9:  k2 ^= static_cast<uint64_t>(tail[8]) << 0;
                 k2 *= c2; k2 = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
                 [[fallthrough]];
        case 8:  k1 ^= static_cast<uint64_t>(tail[7]) << 56;   [[fallthrough]];
        case 7:  k1 ^= static_cast<uint64_t>(tail[6]) << 48;   [[fallthrough]];
        case 6:  k1 ^= static_cast<uint64_t>(tail[5]) << 40;   [[fallthrough]];
        case 5:  k1 ^= static_cast<uint64_t>(tail[4]) << 32;   [[fallthrough]];
        case 4:  k1 ^= static_cast<uint64_t>(tail[3]) << 24;   [[fallthrough]];
        case 3:  k1 ^= static_cast<uint64_t>(tail[2]) << 16;   [[fallthrough]];
        case 2:  k1 ^= static_cast<uint64_t>(tail[1]) << 8;    [[fallthrough]];
        case 1:  k1 ^= static_cast<uint64_t>(tail[0]) << 0;
                 k1 *= c1; k1 = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
                 break;
        default: break;
    }

    h1 ^= static_cast<uint64_t>(len);
    h2 ^= static_cast<uint64_t>(len);

    h1 += h2;
    h2 += h1;

    h1 = fmix64(h1);
    h2 = fmix64(h2);

    h1 += h2;
    h2 += h1;

    for (int i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(h1 >> (8 * i));
    for (int i = 0; i < 8; ++i) out[8 + i] = static_cast<uint8_t>(h2 >> (8 * i));
}

// ---- structure blob -------------------------------------------------------

// Emits one node's payload into `flat` (once per distinct hash) and writes its
// tree entry: the raw hash, then each variant's var_point and entry in order.
void write_tree_entry(BinaryWriter& w, const Path& path, FlatRecord& flat,
                      std::unordered_map<std::string, size_t>& seen) {
    std::vector<uint8_t> payload = encode_path_node(path);
    PathHashBytes hash = path_hash_bytes(payload);
    std::string hex = hash_to_hex(hash);
    if (seen.find(hex) == seen.end()) {
        seen.emplace(hex, flat.nodes.size());
        flat.nodes.push_back(StoredPathNode{hex, std::move(payload)});
    }

    w.bytes.insert(w.bytes.end(), hash.begin(), hash.end());

    w.u32(static_cast<uint32_t>(path.variants.size()));
    for (const Path& v : path.variants) {
        w.opt_i32(v.var_point);
        write_tree_entry(w, v, flat, seen);
    }
}

Path read_tree_entry(BinaryReader& r, const PathNodeLookup& lookup) {
    PathHashBytes hash{};
    for (uint8_t& b : hash) b = r.u8();
    const std::string hex = hash_to_hex(hash);

    const std::vector<uint8_t>* payload = lookup(hex);
    if (!payload) throw SerializeError("path node " + hex + " is not stored");
    Path path = decode_path_node(*payload);

    uint32_t nvar = r.u32();
    path.variants.reserve(nvar);
    for (uint32_t i = 0; i < nvar; ++i) {
        std::optional<int> var_point = r.opt_i32();
        Path variant = read_tree_entry(r, lookup);
        variant.var_point = var_point;
        path.variants.push_back(std::move(variant));
    }
    return path;
}

}  // namespace

// ---- node payloads --------------------------------------------------------

std::vector<uint8_t> encode_path_node(const Path& path) {
    BinaryWriter w;
    w.u32(kPathNodeFormatVersion);
    // The activation layout is the record blob's newest one; a node payload
    // never carries an older spelling, so the version passed is fixed.
    detail::write_path_node(w, path, kBlobFormatVersion);
    return std::move(w.bytes);
}

Path decode_path_node(const std::vector<uint8_t>& payload) {
    BinaryReader r(payload);
    uint32_t version = r.u32();
    if (version != kPathNodeFormatVersion)
        throw SerializeError("unsupported path node format version");

    Path path;
    detail::read_path_node(r, path, kBlobFormatVersion);
    return path;
}

// ---- hashing --------------------------------------------------------------

PathHashBytes path_hash_bytes(const std::vector<uint8_t>& payload) {
    PathHashBytes out{};
    murmur3_x64_128(payload.data(), payload.size(), 0, out.data());
    return out;
}

std::string hash_to_hex(const PathHashBytes& hash) {
    static const char* kDigits = "0123456789abcdef";
    std::string hex;
    hex.reserve(hash.size() * 2);
    for (uint8_t b : hash) {
        hex.push_back(kDigits[b >> 4]);
        hex.push_back(kDigits[b & 0x0f]);
    }
    return hex;
}

std::string path_hash(const std::vector<uint8_t>& payload) {
    return hash_to_hex(path_hash_bytes(payload));
}

// ---- record <-> flat form -------------------------------------------------

FlatRecord flatten_record(const HydraRecord& record) {
    FlatRecord flat;
    std::unordered_map<std::string, size_t> seen;

    BinaryWriter w;
    w.u32(kPathStructureFormatVersion);
    w.opt_f64(record.ms_limit);
    w.opt_i32(record.sp_cap);
    w.boolean(record.sp_cap_converged);

    w.u32(static_cast<uint32_t>(record.paths.size()));
    for (const Path& p : record.paths) write_tree_entry(w, p, flat, seen);

    w.u32(static_cast<uint32_t>(record.allzero_paths.size()));
    for (const Path& p : record.allzero_paths) write_tree_entry(w, p, flat, seen);

    flat.structure = std::move(w.bytes);
    return flat;
}

HydraRecord rebuild_record(const std::vector<uint8_t>& structure,
                           const PathNodeLookup& lookup) {
    BinaryReader r(structure);
    uint32_t version = r.u32();
    if (version != kPathStructureFormatVersion)
        throw SerializeError("unsupported path structure format version");

    HydraRecord record;
    record.ms_limit = r.opt_f64();
    record.sp_cap = r.opt_i32();
    record.sp_cap_converged = r.boolean();

    uint32_t nroots = r.u32();
    record.paths.reserve(nroots);
    for (uint32_t i = 0; i < nroots; ++i)
        record.paths.push_back(read_tree_entry(r, lookup));

    uint32_t nzero = r.u32();
    record.allzero_paths.reserve(nzero);
    for (uint32_t i = 0; i < nzero; ++i)
        record.allzero_paths.push_back(read_tree_entry(r, lookup));

    // The same post-passes read_record runs, in the same order. tied_count is
    // a pure function of the variant tree, so it is recounted rather than
    // stored; recount_tied_paths() walks the whole subtree, matching
    // read_path's per-node call. prepare_variants() then pushes each parent's
    // score fields down and rebuilds variant_tail.
    for (Path& p : record.paths) p.recount_tied_paths();
    for (Path& p : record.allzero_paths) p.recount_tied_paths();
    for (Path& p : record.paths) p.prepare_variants();
    for (Path& p : record.allzero_paths) p.prepare_variants();

    return record;
}

HydraRecord rebuild_record(const FlatRecord& flat) {
    std::unordered_map<std::string, const std::vector<uint8_t>*> index;
    index.reserve(flat.nodes.size());
    for (const StoredPathNode& node : flat.nodes)
        index.emplace(node.hash, &node.payload);

    return rebuild_record(flat.structure,
                          [&index](const std::string& hash)
                              -> const std::vector<uint8_t>* {
                              auto it = index.find(hash);
                              return it == index.end() ? nullptr : it->second;
                          });
}

}  // namespace hydra::store
