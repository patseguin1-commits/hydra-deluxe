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

void write_activation(Writer& w, const Activation& act) {
    w.opt_i32(act.skips);

    if (!act.timecode) throw SerializeError("activation has no timecode");
    w.i64(act.timecode->ticks());

    w.opt_str(act.chord ? std::optional<std::string>(act.chord->code()) : std::nullopt);
    w.opt_i32(act.sp_meter);
    w.opt_i32(act.frontend_points);

    // Trim to what's worth keeping, exactly as hystore._pack does before
    // storing (backends are ~40% of a stock record and display-only).
    std::vector<BackendSqueeze> backends = act.display_backends();
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

    w.opt_f64(act.e_offset);

    // Format version 3 and later: the frontend transfer scales, so per-hit
    // difficulty is computable straight off the blob (no SongTiming needed).
    w.f64(act.transfer_pre.early);
    w.f64(act.transfer_pre.late);
    w.f64(act.transfer_post.early);
    w.f64(act.transfer_post.late);
}

Activation read_activation(Reader& r, uint32_t version) {
    Activation act;
    act.skips = r.opt_i32();
    act.timecode = Timecode::raw(r.i64());

    if (auto code = r.opt_str()) act.chord = Chord::from_code(*code);
    act.sp_meter = r.opt_i32();
    act.frontend_points = r.opt_i32();

    uint32_t nbackends = r.u32();
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

    uint32_t nsq = r.u32();
    act.sqinouts.reserve(nsq);
    for (uint32_t i = 0; i < nsq; ++i) {
        SqueezeKind kind = r.u8() == 0 ? SqueezeKind::SqIn : SqueezeKind::SqOut;
        double offset = r.f64();
        act.sqinouts.push_back(SPSqueeze{kind, offset});
    }

    act.e_offset = r.opt_f64();

    // Pre-v3 blobs keep the 1.0 defaults: the flat-tempo identity, matching
    // the raw-ms difficulty those records were computed with (halved).
    if (version >= 3) {
        act.transfer_pre.early = r.f64();
        act.transfer_pre.late = r.f64();
        act.transfer_post.early = r.f64();
        act.transfer_post.late = r.f64();
    }
    return act;
}

void write_path(Writer& w, const Path& path) {
    w.u32(static_cast<uint32_t>(path.multsqueezes.size()));
    for (const MultSqueeze& m : path.multsqueezes) {
        w.str(m.chord().code());
        w.i32(m.combo());
    }

    w.u32(static_cast<uint32_t>(path.activations.size()));
    for (const Activation& a : path.activations) write_activation(w, a);

    w.i64(path.score_base);
    w.i64(path.score_combo);
    w.i64(path.score_sp);
    w.i64(path.score_solo);
    w.i64(path.score_accents);
    w.i64(path.score_ghosts);

    w.i32(path.notecount);
    w.i32(path.leftover_sp);
    w.i32(path.skipped_ghosts);
    w.i32(path.skipped_accents);

    w.u32(static_cast<uint32_t>(path.variants.size()));
    for (const Path& v : path.variants) write_path(w, v);

    w.opt_i32(path.var_point);
}

Path read_path(Reader& r, uint32_t version) {
    Path path;

    uint32_t nmsq = r.u32();
    path.multsqueezes.reserve(nmsq);
    for (uint32_t i = 0; i < nmsq; ++i) {
        Chord chord = Chord::from_code(r.str());
        int combo = r.i32();
        path.multsqueezes.push_back(MultSqueeze(std::move(chord), combo));
    }

    uint32_t nact = r.u32();
    path.activations.reserve(nact);
    for (uint32_t i = 0; i < nact; ++i)
        path.activations.push_back(read_activation(r, version));

    path.score_base = r.i64();
    path.score_combo = r.i64();
    path.score_sp = r.i64();
    path.score_solo = r.i64();
    path.score_accents = r.i64();
    path.score_ghosts = r.i64();

    path.notecount = r.i32();
    path.leftover_sp = r.i32();
    path.skipped_ghosts = r.i32();
    path.skipped_accents = r.i32();

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

std::vector<uint8_t> write_record(const HydraRecord& record) {
    BinaryWriter w;
    w.u32(kBlobFormatVersion);
    w.opt_f64(record.ms_limit);
    w.opt_i32(record.sp_cap);
    w.boolean(record.sp_cap_converged);

    w.u32(static_cast<uint32_t>(record.paths.size()));
    for (const Path& p : record.paths) write_path(w, p);

    // Format version 2 and later.
    w.u32(static_cast<uint32_t>(record.allzero_paths.size()));
    for (const Path& p : record.allzero_paths) write_path(w, p);

    return std::move(w.bytes);
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
