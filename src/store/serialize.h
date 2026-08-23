// Binary record serialization — replaces hystore.py's json+zlib blob with a
// versioned, hand-rolled binary format (see docs/CPP_PORT_PLAN.md Phase 4).
//
// A record's blob is written by write_record() and read back by
// read_record(); both walk the same Path tree shape as hydata.json_save /
// json_load (multsqueezes, activations with trimmed display backends,
// sqinouts, recursive variants), but as flat binary rather than JSON. The
// format starts with a version tag so a future layout change can be detected
// instead of misread.
//
// A deserialized record's Activation/BackendSqueeze timecodes carry only raw
// ticks (Timecode::raw) — the blob has no tempo map of its own. Call
// restore_timecodes() with the song's SongTiming (from songmeta) to resolve
// them into full Timecodes, mirroring hystore._restore_timecodes.

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

// Bumped whenever write_record's layout changes. A blob written with a newer
// version is not read; the caller treats it like a version mismatch, the same
// way a row stamped by another Hydra version reads back as
// RecordStatus::Stale (see RecordStore::get_record).
//
// Version 2 appended HydraRecord::allzero_paths. Version 3 appended the four
// frontend transfer scales (pre/post x early/late) to each activation; older
// blobs read back with the 1.0 flat-tempo defaults. Version 1 blobs are still
// read -- they simply have no all-0 path until the chart is re-analyzed -- so
// an existing library does not go stale at the blob layer (the record version
// stamp is what forces re-analysis).
constexpr uint32_t kBlobFormatVersion = 3;

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
    std::optional<double> opt_f64();
    std::optional<std::string> opt_str();

private:
    void need(size_t n) const;

    const std::vector<uint8_t>& bytes_;
    size_t pos_ = 0;
};

// Writes a blob in the given format version (1..kBlobFormatVersion; throws
// SerializeError otherwise). Production always writes the newest layout (the
// default); the version parameter exists so the migration tests can produce a
// genuine old blob through the same writer the readers are gated against —
// the write and read gates live side by side in serialize.cpp and cannot
// drift apart.
std::vector<uint8_t> write_record(const HydraRecord& record,
                                  uint32_t version = kBlobFormatVersion);

// The SP cap a blob was analyzed at, read from its fixed header alone (the
// first 18 bytes are enough for every format version). nullopt if the header
// is malformed or the cap was never recorded. Lets the store key rows by cap
// without inflating the paths.
std::optional<int> peek_sp_cap(const std::vector<uint8_t>& head);

// Throws SerializeError if the blob's format version doesn't match, or the
// bytes are truncated/malformed. The single-argument form returns a record
// whose Timecodes carry raw ticks only (see the header comment) — use the
// timing-taking overload wherever a fully restored record is wanted.
HydraRecord read_record(const std::vector<uint8_t>& blob);

// read_record + restore_timecodes in one call: the record comes back with
// full Timecodes, ready for the display layer. This closes the two-call load
// seam; the raw form above stays for callers that deliberately skip the
// restore (RecordStore::for_each_blob).
HydraRecord read_record(const std::vector<uint8_t>& blob,
                        const SongTiming& timing);

// Rebuilds every Timecode in the record (activations and their backends) from
// raw ticks into full Timecodes derived from `timing`. Call once after
// read_record, using the timing built from the record's song (songmeta).
void restore_timecodes(HydraRecord& record, const SongTiming& timing);

}  // namespace hydra::store

#endif  // HYDRA_STORE_SERIALIZE_H
