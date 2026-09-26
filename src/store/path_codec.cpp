#include "store/path_codec.h"

#include <unordered_map>
#include <utility>

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

// ---- node and structure pieces (record format v7, docs/adr/0017) ------------

// One activation. The six fields the search always sets carry no presence
// byte; the three ticks that can be missing keep theirs.
void write_activation(BinaryWriter& w, const Activation& act) {
    w.i32(act.skips);
    w.i64(act.timecode.ticks());
    w.str(act.chord.code());
    w.i32(act.sp_meter);
    w.i32(act.frontend_points);

    // Only the rows the details view shows (display_backends). Backends are
    // a large, display-only part of a record, so the rest are dropped.
    const std::vector<BackendSqueeze> backends = act.display_backends();
    w.u32(static_cast<uint32_t>(backends.size()));
    for (const BackendSqueeze& b : backends) {
        w.i64(b.timecode.ticks());
        w.str(b.chord.code());
        w.i32(b.points);
        w.i32(b.sqout_points);
        w.boolean(b.is_sp);
        w.opt_f64(b.offset_ms);
    }

    w.u32(static_cast<uint32_t>(act.sqinouts.size()));
    for (const SPSqueeze& sq : act.sqinouts) {
        w.u8(sq.kind == SqueezeKind::SqIn ? 0 : 1);
        w.f64(sq.offset_ms);
    }

    w.f64(act.e_offset);
    w.f64(act.transfer_pre.early);
    w.f64(act.transfer_pre.late);
    w.f64(act.transfer_post.early);
    w.f64(act.transfer_post.late);
    w.opt_i64(act.deact_tick);
    w.opt_i64(act.clamp_tick);
    w.opt_i64(act.sqout_tick);
    w.u32(static_cast<uint32_t>(act.collected_phrase_ticks.size()));
    for (int64_t t : act.collected_phrase_ticks) w.i64(t);
}

Activation read_activation(BinaryReader& r) {
    Activation act;
    act.skips = r.i32();
    act.timecode = Timecode::raw(r.i64());
    act.chord = Chord::from_code(r.str());
    act.sp_meter = r.i32();
    act.frontend_points = r.i32();

    const uint32_t nbackends = r.u32();
    act.backends.reserve(nbackends);
    for (uint32_t i = 0; i < nbackends; ++i) {
        BackendSqueeze b;
        b.timecode = Timecode::raw(r.i64());
        b.chord = Chord::from_code(r.str());
        b.points = r.i32();
        b.sqout_points = r.i32();
        b.is_sp = r.boolean();
        b.offset_ms = r.opt_f64();
        act.backends.push_back(std::move(b));
    }

    const uint32_t nsq = r.u32();
    act.sqinouts.reserve(nsq);
    for (uint32_t i = 0; i < nsq; ++i) {
        const SqueezeKind kind = r.u8() == 0 ? SqueezeKind::SqIn : SqueezeKind::SqOut;
        const double offset = r.f64();
        act.sqinouts.push_back(SPSqueeze{kind, offset});
    }

    act.e_offset = r.f64();
    act.transfer_pre.early = r.f64();
    act.transfer_pre.late = r.f64();
    act.transfer_post.early = r.f64();
    act.transfer_post.late = r.f64();
    act.deact_tick = r.opt_i64();
    act.clamp_tick = r.opt_i64();
    act.sqout_tick = r.opt_i64();
    const uint32_t n = r.u32();
    act.collected_phrase_ticks.reserve(n);
    for (uint32_t i = 0; i < n; ++i) act.collected_phrase_ticks.push_back(r.i64());
    return act;
}

// A root path's own totals: the six score categories, the chart's note count
// and the SP left at the end. A variant's copies are overwritten from its
// parent by prepare_variants on every load, so only roots store them.
void write_root_totals(BinaryWriter& w, const Path& p) {
    w.i64(p.score_base);
    w.i64(p.score_combo);
    w.i64(p.score_sp);
    w.i64(p.score_solo);
    w.i64(p.score_accents);
    w.i64(p.score_ghosts);
    w.i32(p.notecount);
    w.i32(p.leftover_sp);
}

void read_root_totals(BinaryReader& r, Path& p) {
    p.score_base = r.i64();
    p.score_combo = r.i64();
    p.score_sp = r.i64();
    p.score_solo = r.i64();
    p.score_accents = r.i64();
    p.score_ghosts = r.i64();
    p.notecount = r.i32();
    p.leftover_sp = r.i32();
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
    w.u32(static_cast<uint32_t>(path.activations.size()));
    for (const Activation& a : path.activations) write_activation(w, a);
    return std::move(w.bytes);
}

Path decode_path_node(const std::vector<uint8_t>& payload) {
    BinaryReader r(payload);
    // Only the current node version is readable. An older node is reachable
    // only through an older structure, and the store never decodes one of
    // those (structure_is_current in record_store.cpp).
    if (r.u32() != kPathNodeFormatVersion)
        throw SerializeError("unsupported path node format version");

    Path path;
    const uint32_t nact = r.u32();
    path.activations.reserve(nact);
    for (uint32_t i = 0; i < nact; ++i) path.activations.push_back(read_activation(r));
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
    // Right after the version, so the store can compare version and rules
    // as one fixed 12-byte head in SQL (record_store.cpp kRowReadySql).
    w.u64(record.rules_fingerprint);
    w.opt_f64(record.ms_limit);
    w.opt_i32(record.sp_cap);
    w.boolean(record.sp_cap_converged);

    w.u32(static_cast<uint32_t>(record.multsqueezes.size()));
    for (const MultSqueeze& m : record.multsqueezes) {
        w.str(m.chord().code());
        w.i32(m.combo());
    }

    w.u32(static_cast<uint32_t>(record.paths.size()));
    for (const Path& p : record.paths) {
        write_tree_entry(w, p, flat, seen);
        write_root_totals(w, p);
    }

    w.u32(static_cast<uint32_t>(record.allzero_paths.size()));
    for (const Path& p : record.allzero_paths) {
        write_tree_entry(w, p, flat, seen);
        write_root_totals(w, p);
    }

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
    record.rules_fingerprint = r.u64();
    record.ms_limit = r.opt_f64();
    record.sp_cap = r.opt_i32();
    record.sp_cap_converged = r.boolean();

    const uint32_t nmsq = r.u32();
    record.multsqueezes.reserve(nmsq);
    for (uint32_t i = 0; i < nmsq; ++i) {
        Chord chord = Chord::from_code(r.str());
        const int combo = r.i32();
        record.multsqueezes.push_back(MultSqueeze(std::move(chord), combo));
    }

    const uint32_t nroots = r.u32();
    record.paths.reserve(nroots);
    for (uint32_t i = 0; i < nroots; ++i) {
        Path p = read_tree_entry(r, lookup);
        read_root_totals(r, p);
        record.paths.push_back(std::move(p));
    }

    const uint32_t nzero = r.u32();
    record.allzero_paths.reserve(nzero);
    for (uint32_t i = 0; i < nzero; ++i) {
        Path p = read_tree_entry(r, lookup);
        read_root_totals(r, p);
        record.allzero_paths.push_back(std::move(p));
    }

    // tied_count is a pure function of the variant tree, so it is recounted
    // rather than stored. prepare_variants() then pushes each root's totals
    // down to its variants and rebuilds variant_tail.
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
