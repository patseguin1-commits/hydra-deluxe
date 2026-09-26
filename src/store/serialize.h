// Small binary primitives for the store's own formats -- the path codec
// (store/path_codec.h) and the songmeta tempo-map blob (record_store.cpp) --
// plus restore_timecodes. A decoded record's Activation/BackendSqueeze
// timecodes carry only raw ticks (Timecode::raw), because a stored path has
// no tempo map of its own. restore_timecodes() with the song's SongTiming
// (from songmeta) resolves them into full Timecodes.

#ifndef HYDRA_STORE_SERIALIZE_H
#define HYDRA_STORE_SERIALIZE_H

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/timing.h"

namespace hydra::store {

class SerializeError : public std::runtime_error {
public:
    explicit SerializeError(const std::string& what) : std::runtime_error(what) {}
};

// Small little-endian binary primitives, shared by the record blob and the
// songmeta tempomap blob (record_store.cpp). Not a general-purpose format —
// just enough structure for this store's own writers/readers to agree.
class BinaryWriter {
public:
    std::vector<uint8_t> bytes;

    void u8(uint8_t v) { bytes.push_back(v); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void u32(uint32_t v);
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void u64(uint64_t v);
    void i64(int64_t v) { u64(static_cast<uint64_t>(v)); }
    void f64(double v);
    void str(const std::string& s);

    void opt_i32(const std::optional<int>& v);
    void opt_i64(const std::optional<int64_t>& v);
    void opt_f64(const std::optional<double>& v);
    void opt_str(const std::optional<std::string>& v);
};

class BinaryReader {
public:
    explicit BinaryReader(const std::vector<uint8_t>& b) : bytes_(b) {}

    uint8_t u8();
    bool boolean() { return u8() != 0; }
    uint32_t u32();
    int32_t i32() { return static_cast<int32_t>(u32()); }
    uint64_t u64();
    int64_t i64() { return static_cast<int64_t>(u64()); }
    double f64();
    std::string str();

    std::optional<int> opt_i32();
    std::optional<int64_t> opt_i64();
    std::optional<double> opt_f64();
    std::optional<std::string> opt_str();

private:
    void need(size_t n) const;

    const std::vector<uint8_t>& bytes_;
    size_t pos_ = 0;
};

// Rebuilds every Timecode in the record (activations and their backends) from
// raw ticks into full Timecodes derived from `timing`. Call once after
// decoding, using the timing built from the record's song (songmeta).
void restore_timecodes(HydraRecord& record, const SongTiming& timing);

}  // namespace hydra::store

#endif  // HYDRA_STORE_SERIALIZE_H
