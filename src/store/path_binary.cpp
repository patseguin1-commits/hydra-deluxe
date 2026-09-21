#include "store/path_binary.h"

namespace hydra::store::detail {

void write_activation(BinaryWriter& w, const Activation& act, uint32_t version) {
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
    // The gate mirrors read_activation's, in the same file, on purpose.
    if (version >= 3) {
        w.f64(act.transfer_pre.early);
        w.f64(act.transfer_pre.late);
        w.f64(act.transfer_post.early);
        w.f64(act.transfer_post.late);
    }

    // Format version 4 and later: the deactivation node the search stamped
    // on this activation. Nothing recomputes it, so an older blob reads back
    // unset and its consumers say so rather than guessing.
    if (version >= 4) w.opt_i64(act.deact_tick);

    // Format version 5 and later: the collecting note the SP cap pinned
    // this window to. Older blobs read back unset.
    if (version >= 5) w.opt_i64(act.clamp_tick);
}

Activation read_activation(BinaryReader& r, uint32_t version) {
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

    // Pre-v4 blobs leave deact_tick unset. Nothing fills it in.
    if (version >= 4) act.deact_tick = r.opt_i64();

    // Pre-v5 blobs leave clamp_tick unset. Nothing fills it in.
    if (version >= 5) act.clamp_tick = r.opt_i64();
    return act;
}

void write_path_node(BinaryWriter& w, const Path& path, uint32_t version) {
    w.u32(static_cast<uint32_t>(path.multsqueezes.size()));
    for (const MultSqueeze& m : path.multsqueezes) {
        w.str(m.chord().code());
        w.i32(m.combo());
    }

    w.u32(static_cast<uint32_t>(path.activations.size()));
    for (const Activation& a : path.activations) write_activation(w, a, version);

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
}

void read_path_node(BinaryReader& r, Path& path, uint32_t version) {
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
}

}  // namespace hydra::store::detail
