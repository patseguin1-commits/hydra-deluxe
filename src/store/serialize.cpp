#include "store/serialize.h"

#include <cstring>

#include "store/path_binary.h"

namespace hydra::store {

// ---- BinaryWriter / BinaryReader ------------------------------------------

void BinaryWriter::u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void BinaryWriter::u64(uint64_t v) {
    for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void BinaryWriter::f64(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    u64(bits);
}
void BinaryWriter::str(const std::string& s) {
    u32(static_cast<uint32_t>(s.size()));
    bytes.insert(bytes.end(), s.begin(), s.end());
}
void BinaryWriter::opt_i32(const std::optional<int>& v) {
    boolean(v.has_value());
    if (v) i32(*v);
}
void BinaryWriter::opt_i64(const std::optional<int64_t>& v) {
    boolean(v.has_value());
    if (v) i64(*v);
}
void BinaryWriter::opt_f64(const std::optional<double>& v) {
    boolean(v.has_value());
    if (v) f64(*v);
}
void BinaryWriter::opt_str(const std::optional<std::string>& v) {
    boolean(v.has_value());
    if (v) str(*v);
}

void BinaryReader::need(size_t n) const {
    if (pos_ + n > bytes_.size()) throw SerializeError("truncated blob");
}
uint8_t BinaryReader::u8() {
    need(1);
    return bytes_[pos_++];
}
uint32_t BinaryReader::u32() {
    need(4);
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(bytes_[pos_++]) << (8 * i);
    return v;
}
uint64_t BinaryReader::u64() {
    need(8);
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(bytes_[pos_++]) << (8 * i);
    return v;
}
double BinaryReader::f64() {
    uint64_t bits = u64();
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}
std::string BinaryReader::str() {
    uint32_t n = u32();
    need(n);
    std::string s(reinterpret_cast<const char*>(&bytes_[pos_]), n);
    pos_ += n;
    return s;
}
std::optional<int> BinaryReader::opt_i32() {
    if (!boolean()) return std::nullopt;
    return i32();
}
std::optional<int64_t> BinaryReader::opt_i64() {
    if (!boolean()) return std::nullopt;
    return i64();
}
std::optional<double> BinaryReader::opt_f64() {
    if (!boolean()) return std::nullopt;
    return f64();
}
std::optional<std::string> BinaryReader::opt_str() {
    if (!boolean()) return std::nullopt;
    return str();
}

namespace {

using Writer = BinaryWriter;
using Reader = BinaryReader;

// ---- Path tree --------------------------------------------------------
//
// A node's own fields (multsqueezes, activations, scores, counts) are written
// and read by store/path_binary.h — the one spelling of that layout, shared
// with the flat path codec. What is left here is the tree shape: the nested
// variant list and var_point, which is exactly what the two formats differ on.

void write_path(Writer& w, const Path& path, uint32_t version) {
    detail::write_path_node(w, path, version);

    w.u32(static_cast<uint32_t>(path.variants.size()));
    for (const Path& v : path.variants) write_path(w, v, version);

    w.opt_i32(path.var_point);
}

Path read_path(Reader& r, uint32_t version) {
    Path path;
    detail::read_path_node(r, path, version);

    uint32_t nvar = r.u32();
    path.variants.reserve(nvar);
    for (uint32_t i = 0; i < nvar; ++i) path.variants.push_back(read_path(r, version));

    path.var_point = r.opt_i32();

    // tied_count isn't serialized (it's a pure function of the variant tree,
    // like Python's _tied_count); rebuild it now, mirroring json_load's
    // per-path o.recount_tied_paths() call.
    path.recount_tied_paths();
    return path;
}

}  // namespace

std::vector<uint8_t> write_record(const HydraRecord& record, uint32_t version) {
    if (version < 1 || version > kBlobFormatVersion)
        throw SerializeError("unsupported record blob format version");

    BinaryWriter w;
    w.u32(version);
    w.opt_f64(record.ms_limit);
    w.opt_i32(record.sp_cap);
    w.boolean(record.sp_cap_converged);

    w.u32(static_cast<uint32_t>(record.paths.size()));
    for (const Path& p : record.paths) write_path(w, p, version);

    // Format version 2 and later.
    if (version >= 2) {
        w.u32(static_cast<uint32_t>(record.allzero_paths.size()));
        for (const Path& p : record.allzero_paths) write_path(w, p, version);
    }

    return std::move(w.bytes);
}

std::optional<int> peek_sp_cap(const std::vector<uint8_t>& head) {
    try {
        BinaryReader r(head);
        uint32_t version = r.u32();
        if (version < 1 || version > kBlobFormatVersion) return std::nullopt;
        r.opt_f64();  // ms_limit
        return r.opt_i32();
    } catch (const SerializeError&) {
        return std::nullopt;
    }
}

HydraRecord read_record(const std::vector<uint8_t>& blob) {
    BinaryReader r(blob);
    uint32_t version = r.u32();
    if (version < 1 || version > kBlobFormatVersion)
        throw SerializeError("unsupported record blob format version");

    HydraRecord record;
    record.ms_limit = r.opt_f64();
    record.sp_cap = r.opt_i32();
    record.sp_cap_converged = r.boolean();

    uint32_t npaths = r.u32();
    record.paths.reserve(npaths);
    for (uint32_t i = 0; i < npaths; ++i) record.paths.push_back(read_path(r, version));

    // A version 1 blob ends here. It keeps an empty allzero_paths, so the
    // record loads fine and simply shows no all-0 path until re-analyzed.
    if (version >= 2) {
        uint32_t nzero = r.u32();
        record.allzero_paths.reserve(nzero);
        for (uint32_t i = 0; i < nzero; ++i)
            record.allzero_paths.push_back(read_path(r, version));
    }

    for (Path& p : record.paths) p.prepare_variants();
    for (Path& p : record.allzero_paths) p.prepare_variants();

    return record;
}

namespace {

void restore_activation(Activation& act, const SongTiming& timing) {
    if (act.timecode) act.timecode = timing.timecode(act.timecode->ticks());
    for (BackendSqueeze& b : act.backends) b.timecode = timing.timecode(b.timecode.ticks());
}

void restore_path(Path& path, const SongTiming& timing) {
    for (Activation& a : path.activations) restore_activation(a, timing);
    for (Path& v : path.variants) restore_path(v, timing);
}

}  // namespace

HydraRecord read_record(const std::vector<uint8_t>& blob,
                        const SongTiming& timing) {
    HydraRecord record = read_record(blob);
    restore_timecodes(record, timing);
    return record;
}

void restore_timecodes(HydraRecord& record, const SongTiming& timing) {
    for (Path& p : record.paths) restore_path(p, timing);
    for (Path& p : record.allzero_paths) restore_path(p, timing);
    // variant_tail activations alias the same Activation values copied from
    // the base path's all_activations() by prepare_variants(); rebuild it
    // fresh from the now-restored base path so it doesn't keep stale
    // Timecode::raw ticks-only values.
    for (Path& p : record.paths) p.prepare_variants();
    for (Path& p : record.allzero_paths) p.prepare_variants();
}

}  // namespace hydra::store
