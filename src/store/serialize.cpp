#include "store/serialize.h"

#include <cstring>

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

void restore_activation(Activation& act, const SongTiming& timing) {
    act.timecode = timing.timecode(act.timecode.ticks());
    for (BackendSqueeze& b : act.backends) b.timecode = timing.timecode(b.timecode.ticks());
}

void restore_path(Path& path, const SongTiming& timing) {
    for (Activation& a : path.activations) restore_activation(a, timing);
    for (Path& v : path.variants) restore_path(v, timing);
}

}  // namespace

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
