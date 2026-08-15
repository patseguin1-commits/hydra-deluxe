// Native path search. The C++ half of hypath.GraphPather.read.
//
// This is a transcription, not a reinterpretation. Every decision the Python
// makes is made here in the same order on the same values, because the paths
// this produces are the product -- a faster search that changes one pathstring
// is worthless. Where the Python does something surprising (a buffer that is
// only cleared on one of two branches, an SP meter with no lower clamp, a tie
// leader chosen purely by position) the comment says so, and the code does the
// surprising thing.
//
// Two representation choices carry the speed-up, and neither changes results:
//
//   * Activations are a persistent chain, not a list. hydata.Path.copy copies
//     the activation list and deep-copies its last element; branching is the
//     single most-executed operation in the search, and that copy is most of
//     it. Here a path holds only the index of its last activation, each
//     activation points at the one before, and branching allocates exactly one
//     record -- a copy of the mutable tail, which is the only element the
//     Python copies anyway.
//
//   * The per-iteration grouping tables are open-addressed and cleared by
//     bumping a stamp rather than by reallocating. The reduction runs once per
//     iteration over every live path, so a std::unordered_map here would cost
//     more than the whole rest of the loop.
//
// Everything float-valued (E offsets, backend offsets, squeeze timings) is
// computed on the Python side during graph build and carried in as a double.
// Nothing here derives a millisecond from a tick, so there is no opportunity
// for the two implementations to round differently.

#include "hydra_search.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
// The activation DP recurses about once per candidate (thousands deep on a
// discography-sized chart), which overruns the default 1 MB thread stack. The
// DP runs on a dedicated large-stack thread; see hy_dp_search. NOMINMAX keeps
// windows.h from clobbering std::min/std::max.
#define NOMINMAX
#include <windows.h>
#endif

namespace {

// "No value", for the optional doubles a path carries. NaN is used rather than
// a sentinel magnitude because these are compared against real timings, and
// every comparison against NaN is false -- wrong loudly rather than quietly.
const double NO_DOUBLE = std::numeric_limits<double>::quiet_NaN();

inline bool has_value(double v) { return !std::isnan(v); }

// Mirrors hypath.DEACT_*.
const int32_t DEACT_NONE = 0;
const int32_t DEACT_NORMAL = 1;
const int32_t DEACT_SQINOUT = 2;

// hypath.MAX_TIED_PATHS.
const int32_t MAX_TIED_PATHS = 4;

// Marks a path whose state went somewhere the Python would have raised from.
// Checked by the driver, which fails the whole run rather than continuing.
const int32_t NODE_BROKEN = -2;

// One activation. Immutable once another activation is appended after it,
// which is exactly when hydata.Path.copy stops copying it.
struct Act {
    int32_t parent;      // previous Act in this path's chain, or -1
    int32_t act_node;
    int32_t skips;
    int32_t sp_meter;
    int32_t deact_edge;  // edge supplying backends, or -1
    int32_t sq_tail;     // last SqNode, or -1
    int32_t depth;       // 1-based chain length, i.e. len(Path._activations)
    double  e_offset;
};

// A SqIn/SqOut marker. Appending builds a new node, so a path that copied an
// activation and then appended does not disturb the original's markers -- the
// guarantee Activation.copy gets from copying the sqinouts list.
struct SqNode {
    int32_t prev;
    int32_t kind;
    double  offset;
};

// A path that tied with another and was folded into it. Frozen: hydata relies
// on a path being finished the moment it becomes a variant.
struct Variant {
    int32_t prev;        // cons list, reversed on output to restore add order
    int32_t var_point;
    int32_t sc[6];
    int32_t notecount;
    int32_t leftover_sp;
    int32_t skipped_accents;
    int32_t skipped_ghosts;
    int32_t act_tail;
    int32_t var_head;
    int32_t tied_count;
};

// hypath.GraphPath.
struct Path {
    int32_t node;            // current node index, -1 when complete
    int32_t sp;
    int32_t currentskips;
    int32_t buffered;        // buffered_sqinout_sp
    int32_t act_tail;
    int32_t var_head;
    int32_t tied_count;
    int32_t notecount;
    int32_t skipped_accents;
    int32_t skipped_ghosts;
    int32_t sc[6];           // base, combo, sp, solo, accents, ghosts
    int64_t score;
    int64_t sp_end_time;
    double  sp_ready_ms;     // NaN when sp_ready_time is None
    double  skipped_e_offset;
    double  diff_prefix;     // Path._difficulty_prefix
};

// Owns the output arrays after hy_search returns. Declared at namespace scope
// so that hy_search_free deletes the same type that was allocated.
struct Holder {
    std::vector<hy_out_path> paths;
    std::vector<hy_out_act> acts;
    std::vector<hy_out_sq> sqs;
};

// Open addressing with a generation stamp, so clearing between iterations is a
// single increment. An empty slot is one whose stamp is stale, which means no
// key value is reserved as "absent".
class StampMap {
public:
    void reset(size_t want) {
        size_t cap = 16;
        while (cap < want * 2) cap <<= 1;
        if (cap > mask_ + 1) {
            mask_ = cap - 1;
            keys_.assign(cap, 0);
            vals_.assign(cap, 0);
            stamps_.assign(cap, 0);
            stamp_ = 0;
        }
        if (++stamp_ == 0) {
            // Wrapped: every stale stamp would read as current.
            std::fill(stamps_.begin(), stamps_.end(), 0u);
            stamp_ = 1;
        }
    }

    // Returns the stored value, inserting want_value if the key is new.
    // *inserted reports which happened.
    int32_t get_or_insert(uint64_t key, int32_t want_value, bool* inserted) {
        size_t i = hash(key) & mask_;
        for (;;) {
            if (stamps_[i] != stamp_) {
                stamps_[i] = stamp_;
                keys_[i] = key;
                vals_[i] = want_value;
                *inserted = true;
                return want_value;
            }
            if (keys_[i] == key) {
                *inserted = false;
                return vals_[i];
            }
            i = (i + 1) & mask_;
        }
    }

private:
    static uint64_t hash(uint64_t x) {
        // splitmix64 finalizer: the keys here are small integers and packed
        // tick values, which a plain mask would pile into a few buckets.
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    std::vector<uint64_t> keys_;
    std::vector<int32_t> vals_;
    std::vector<uint32_t> stamps_;
    size_t mask_ = 0;
    uint32_t stamp_ = 0;
};

// --- Activation DP (hy_dp_search) ----------------------------------------
// A backward activation dynamic program over the same flat graph the BFS
// consumes. Where the BFS enumerates every live path, the DP observes that
// between two activations a path follows the base track deterministically, so
// the only decision is *where* to activate (an activation always spends the
// whole meter). It drives the very same advance / branch_activate /
// branch_deactivate primitives the BFS uses, so every segment score is
// identical by construction. Mirrors hypath.DPPather. Best-score parity with
// the BFS, not variant parity.
//
// The recurrence is candidate-local: from "arriving at a base node" the only
// moves are activate-here and skip-to-the-next-candidate. Skipping recurses a
// single step to the next candidate rather than walking to the end and fanning
// out over every downstream candidate, and the memo is keyed at every candidate
// and landing -- so histories from different resets that converge on the same
// (node, meter) state share one sub-result. That turns the old
// O(candidates^2) reset fan-out into a per-candidate DP.

// A backtrack plan. STOP and DONE are sentinels held at fixed arena indices;
// ACT records one activation choice and points at the plan to follow after it.
const int32_t PLAN_KIND_STOP = 0;
const int32_t PLAN_KIND_DONE = 1;
const int32_t PLAN_KIND_ACT  = 2;
const int32_t PLAN_STOP = 0;   // plans_[0], allocated first in dp_run
const int32_t PLAN_DONE = 1;   // plans_[1]

struct DpEntry {
    int64_t score;
    int32_t plan;        // index into Engine::plans_
};

struct DpOutcome {
    int32_t completed;   // 1 if the song ended while still in SP
    int32_t idx;         // ordinal among this activation's deactivation options
    int64_t delta;       // score from the activation onward (excludes base-before)
    int32_t landing;     // base node the deactivation returned to (-1 if completed)
    int32_t residual;    // SP bars on landing: 0 normal, 1 squeeze-out
    int32_t buffered;    // buffered_sqinout_sp on landing
};

struct DpPlan {
    int32_t kind;        // PLAN_KIND_*
    int32_t cand_index;  // base-track index of the activation candidate
    int32_t outcome_idx; // which DpOutcome of that candidate was taken
    int32_t child;       // plan index to follow after this activation
};

// The bit pattern used in a memo key to mean "sp_ready_time is None". No finite
// double reaches it, and it is only ever compared for equality.
const int64_t DP_NO_READY = (int64_t)0x7FF8000000000000ll;

inline int64_t dp_ready_bits(double sp_ready_ms) {
    if (std::isnan(sp_ready_ms)) return DP_NO_READY;
    int64_t b;
    std::memcpy(&b, &sp_ready_ms, sizeof(b));
    return b;
}

// Memo key for the candidate-local DP: base node, meter, sp_ready (its bits, so
// two histories with different ready times are distinct -- it gates activation
// legality and is not implied by (node, sp)), and buffered for the rare
// squeeze-out landing that lands on a candidate.
struct DpNodeKey {
    int32_t node;
    int32_t sp;
    int32_t buffered;
    int64_t ready_bits;
    bool operator==(const DpNodeKey& o) const {
        return node == o.node && sp == o.sp && buffered == o.buffered &&
               ready_bits == o.ready_bits;
    }
};

struct DpNodeKeyHash {
    size_t operator()(const DpNodeKey& k) const {
        auto mix = [](uint64_t x) {
            x += 0x9E3779B97F4A7C15ull;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            return x ^ (x >> 31);
        };
        uint64_t h = mix((uint64_t)(uint32_t)k.node);
        h ^= mix(((uint64_t)(uint32_t)k.sp << 32) ^ (uint32_t)k.buffered) +
             0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h ^= mix((uint64_t)k.ready_bits) + 0x9E3779B97F4A7C15ull +
             (h << 6) + (h >> 2);
        return (size_t)h;
    }
};

// The outcome of driving the base track from a node to the next candidate.
struct DpWalk {
    int64_t base_score;
    int32_t next_index;   // base index of the next candidate, or -1 for the end
    int32_t sp;
    int32_t buffered;
    double  sp_ready_ms;
};

// (candidate_index, sp) packed into a memo key for the activation-outcome cache.
inline uint64_t dp_act_key(int32_t cand_index, int32_t sp) {
    return (uint64_t)(uint32_t)cand_index | ((uint64_t)(uint32_t)sp << 32);
}

class Engine {
public:
    explicit Engine(const hy_search_in& in) : in_(in) {}

    int32_t run(hy_search_out* out);
    int32_t dp_run(hy_search_out* out);

private:
    const hy_node& node(int32_t i) const { return in_.nodes[i]; }
    const hy_edge& edge(int32_t i) const { return in_.edges[i]; }

    int32_t new_act(int32_t parent, int32_t act_node, int32_t skips,
                    int32_t sp_meter, double e_offset) {
        Act a;
        a.parent = parent;
        a.act_node = act_node;
        a.skips = skips;
        a.sp_meter = sp_meter;
        a.deact_edge = -1;
        a.sq_tail = -1;
        a.depth = (parent < 0 ? 0 : acts_[parent].depth) + 1;
        a.e_offset = e_offset;
        acts_.push_back(a);
        return (int32_t)acts_.size() - 1;
    }

    // hydata.Path.copy's "except for the latest activation" clause: the copy
    // owns its tail and may write to it, the original keeps its own.
    int32_t clone_tail(int32_t tail) {
        if (tail < 0) return -1;
        acts_.push_back(acts_[tail]);
        return (int32_t)acts_.size() - 1;
    }

    int32_t push_sq(int32_t prev, int32_t kind, double offset) {
        SqNode s;
        s.prev = prev;
        s.kind = kind;
        s.offset = offset;
        sqs_.push_back(s);
        return (int32_t)sqs_.size() - 1;
    }

    int32_t act_count(const Path& p) const {
        return p.act_tail < 0 ? 0 : acts_[p.act_tail].depth;
    }

    void advance(Path& p);
    bool branch_activate(Path& p, Path* child);
    bool branch_deactivate(Path& p, Path* child, bool* has_child);
    void create_deactivated_path(const Path& p, Path* child, bool is_sq_out);
    int32_t deactivation_type(const hy_edge& e, int64_t sp_end_time) const;

    double act_difficulty(int32_t act) const;
    double search_difficulty(const Path& p) const;
    bool passes_ms_filter(const Path& p) const;
    void close_last_activation(Path& p) const;

    void reduce_iteration_paths();
    void reduce_group(const int32_t* members, int32_t n);
    void prune_hopeless_paths();

    void emit_path(const Path& p);
    void emit_variant(int32_t v, int32_t depth);
    void emit_acts(int32_t act_tail, int32_t* begin, int32_t* end);

    // Activation DP. dp_run builds the base index and drives the recursion;
    // dp_best_from_node is the memoized candidate-local DP; the rest mirror
    // hypath.DPPather.
    void dp_build_index();
    Path dp_seed(int32_t node, int32_t sp, int32_t buffered, double ready_ms);
    std::vector<DpEntry> dp_best_from_node(int32_t node_index, int32_t sp,
                                           double sp_ready_ms, int32_t buffered);
    DpWalk dp_walk_to_next_candidate(int32_t node_index, int32_t sp,
                                     double sp_ready_ms, int32_t buffered);
    const std::vector<DpOutcome>& dp_activation_outcomes(int32_t cand_index,
                                                         int32_t sp);
    void dp_simulate_sp(Path p, std::vector<DpOutcome>& outcomes);
    std::vector<DpEntry> dp_merge_topk(std::vector<DpEntry>& options);
    int32_t dp_make_act(int32_t cand_index, int32_t outcome_idx, int32_t child);
    Path dp_replay(int32_t plan);

    const hy_search_in& in_;

    std::vector<Act> acts_;
    std::vector<SqNode> sqs_;
    std::vector<Variant> variants_;

    std::vector<Path> cur_;
    std::vector<Path> next_;

    // Reduction scratch, reused across iterations so the hot loop allocates
    // nothing.
    std::vector<uint8_t> filtered_;
    std::vector<uint8_t> removed_;
    std::vector<int32_t> owner_;
    std::vector<int32_t> counts_;
    std::vector<int32_t> group_members_;
    std::vector<int32_t> group_begin_;
    std::vector<int32_t> group_end_;
    std::vector<int32_t> survivors_;
    std::vector<int64_t> beating_;
    std::vector<int64_t> dominating_;
    std::vector<int64_t> guaranteed_;
    StampMap group_map_;
    StampMap tie_map_;
    StampMap distinct_map_;

    bool has_optimal_ = false;
    int64_t optimal_score_ = 0;

    std::vector<hy_out_path> out_paths_;
    std::vector<hy_out_act> out_acts_;
    std::vector<hy_out_sq> out_sqs_;
    std::vector<int32_t> chain_scratch_;
    std::vector<int32_t> sq_scratch_;

    int32_t iterations_ = 0;

    // Activation DP state.
    std::vector<int32_t> base_nodes_;     // base index -> node id
    std::vector<int32_t> node_to_base_;   // node id -> base index, -1 if not base
    std::unordered_map<DpNodeKey, std::vector<DpEntry>, DpNodeKeyHash> dp_memo_;
    std::unordered_map<uint64_t, std::vector<DpOutcome>> dp_act_memo_;
    std::vector<DpPlan> plans_;
    bool dp_points_mode_ = false;
    int32_t dp_depth_value_ = 0;
    int32_t dp_k_ = 1;
    double dp_dummy_ready_ms_ = 0.0;
    bool dp_failed_ = false;
};

// --- hypath.GraphPath.advance -------------------------------------------
void Engine::advance(Path& p) {
    if (p.node < 0) return;

    const hy_node& n = node(p.node);
    if (n.adv_edge < 0) {
        p.node = -1;         // reached the end of the song
        return;
    }

    const hy_edge& e = edge(n.adv_edge);

    p.sc[0] += e.basescore;
    p.sc[1] += e.comboscore;
    p.sc[2] += e.spscore;
    p.sc[3] += e.soloscore;
    p.sc[4] += e.accentscore;
    p.sc[5] += e.ghostscore;
    p.score += (int64_t)e.basescore + e.comboscore + e.spscore + e.soloscore
             + e.accentscore + e.ghostscore;
    p.notecount += e.notecount;

    const int32_t sp_n = e.sp_end - e.sp_begin;
    int32_t buffered = p.buffered;

    if (n.is_sp) {
        // Nothing at all happens when the edge carries no phrase -- including
        // the write-back of buffered, which is why a SqIn path keeps its
        // buffer across edges that have no SP on them.
        if (sp_n > 0) {
            int64_t sp_end_time = p.sp_end_time;
            for (int32_t i = 0; i < sp_n; ++i) {
                const hy_sptime& st = in_.sptimes[e.sp_begin + i];
                if (buffered > 0) {
                    --buffered;
                    continue;
                }
                // extension_map[sp_end_time]. The maps hold a handful of
                // entries, so a scan beats a search.
                int32_t k = st.map_begin;
                for (; k < st.map_end; ++k) {
                    if (in_.ext_key[k] == sp_end_time) break;
                }
                if (k == st.map_end) {
                    // The Python raises KeyError here. Leaving the time
                    // unchanged would silently mis-path, so the run is failed.
                    p.node = NODE_BROKEN;
                    return;
                }
                sp_end_time = in_.ext_val[k];
            }
            p.sp_end_time = sp_end_time;
            p.buffered = buffered;
        }
    } else {
        const int32_t old_sp = p.sp;
        int32_t sp = old_sp + sp_n - buffered;
        // Clamped from above only. The Python has no lower clamp, and a path
        // can arrive here with a buffer larger than its meter; clamping to
        // zero would merge paths the reduction is supposed to keep apart.
        if (in_.has_sp_cap && sp > in_.sp_cap) sp = in_.sp_cap;
        p.sp = sp;

        if (old_sp < 2 && sp >= 2) {
            // The index uses the pre-advance meter and the pre-advance buffer.
            const int32_t k = e.sp_begin + (1 - old_sp + buffered);
            if (k < e.sp_begin || k >= e.sp_end) {
                p.node = NODE_BROKEN;
                return;
            }
            p.sp_ready_ms = in_.sptimes[k].ms;
        }

        p.buffered = 0;
    }

    // multsqueezes are accumulated on the Python side: the base and SP edges
    // at a position carry the same objects, so every path through the graph
    // collects the same set and they are a property of the graph, not a path.

    p.node = e.dest;
}

// --- hypath.GraphPath.branch_activate ------------------------------------
bool Engine::branch_activate(Path& p, Path* child) {
    const hy_node& n = node(p.node);
    if (n.branch_edge < 0) return false;

    // Must have enough SP.
    if (p.sp < 2) return false;

    const hy_edge& e = edge(n.branch_edge);

    // SP ready time must be before this activation fill's deadline. Guarded by
    // sp >= 2 above: a path holding two bars banked them, so it has one.
    const double e_offset = e.activation_fill_deadline_ms - p.sp_ready_ms;

    // Thanks to the timing window the cutoff is -70ms, not 0ms.
    if (e_offset < -70) return false;

    const int32_t aiet_i = e.aiet_begin + p.sp;
    if (aiet_i < e.aiet_begin || aiet_i >= e.aiet_end ||
        in_.aiet[aiet_i] == HY_NO_TIME) {
        p.node = NODE_BROKEN;     // the Python would raise KeyError
        return false;
    }

    Path c = p;
    c.node = e.dest;
    c.currentskips = 0;
    // Activated paths immediately spend the SP and just know when it ends.
    c.sp = 0;

    // The activation this one follows can no longer change, so fold it into
    // the running difficulty maximum before it stops being the last.
    close_last_activation(c);

    c.act_tail = new_act(p.act_tail, p.node, p.currentskips, p.sp,
                         has_value(p.skipped_e_offset) ? p.skipped_e_offset
                                                       : e_offset);
    c.sc[2] += e.frontend_points;
    c.score += e.frontend_points;
    c.skipped_e_offset = NO_DOUBLE;
    c.sp_ready_ms = NO_DOUBLE;
    c.sp_end_time = in_.aiet[aiet_i];

    // From here on the mutations land on the path that declined the fill.
    p.currentskips += 1;

    if (in_.flag_skipped_dynamics) {
        // Independent ifs, not else-if, exactly as the Python has them.
        if (e.frontend_is_accent) {
            p.sc[4] -= e.skipped_dynamic_points;
            p.skipped_accents += 1;
            p.score -= e.skipped_dynamic_points;
        }
        if (e.frontend_is_ghost) {
            p.sc[5] -= e.skipped_dynamic_points;
            p.skipped_ghosts += 1;
            p.score -= e.skipped_dynamic_points;
        }
    }

    // Even if the E fill is skipped, the eventual activation should know the
    // offset of the first one this path passed up.
    if (!has_value(p.skipped_e_offset)) p.skipped_e_offset = e_offset;

    *child = c;
    return true;
}

// --- hypath.ScoreGraphEdge.deactivation_type -----------------------------
int32_t Engine::deactivation_type(const hy_edge& e, int64_t sp_end_time) const {
    if (e.sqinout_time != HY_NO_TIME) {
        // This deact has SP on it, so if valid the deact path is a SqOut and
        // the continuing path a SqIn.
        return sp_end_time == e.sqout_time ? DEACT_SQINOUT : DEACT_NONE;
    }
    // Normal backend, no sqin/sqouts.
    return sp_end_time == node(e.dest).tick ? DEACT_NORMAL : DEACT_NONE;
}

// --- hypath.GraphPath.create_deactivated_path ----------------------------
void Engine::create_deactivated_path(const Path& p, Path* child,
                                     bool is_sq_out) {
    const int32_t deact_edge = node(p.node).branch_edge;
    const hy_edge& e = edge(deact_edge);

    Path c = p;
    c.node = e.dest;
    c.sp = is_sq_out ? 1 : 0;
    c.sp_end_time = HY_NO_TIME;

    // The deactivating path owns its last activation and writes the backends
    // and the SqOut marker onto it; the parent keeps its own copy untouched.
    c.act_tail = clone_tail(p.act_tail);
    if (c.act_tail >= 0) {
        Act& a = acts_[c.act_tail];
        a.deact_edge = deact_edge;
        if (is_sq_out) {
            a.sq_tail = push_sq(a.sq_tail, HY_SQ_OUT, e.sqinout_timing);
        }
    }

    // Backend scoring adjustments. Accumulated once and applied to the
    // breakdown and the running total together, so the two cannot drift.
    int32_t sp_delta = 0;
    for (int32_t i = e.be_begin; i < e.be_end; ++i) {
        const hy_backend& be = in_.backends[i];
        const bool is_already_counted = be.offset_ms <= 0;
        const bool is_leeway = be.offset_ms > 0 && be.offset_ms < 3;

        if (is_sq_out) {
            const bool is_before_sqout = be.tick < e.sqinout_time;
            const bool is_exact_sqout = be.tick == e.sqinout_time;
            const bool is_after_sqout = be.tick > e.sqinout_time;

            if (is_already_counted) {
                if (is_exact_sqout) {
                    // Replace already-counted SP points with reduced ones.
                    sp_delta += -be.points + be.sqout_points;
                } else if (is_after_sqout) {
                    // Remove already-counted SP points: in this path the
                    // backend was forced out of SP even though it is early.
                    sp_delta += -be.points;
                }
            } else if (is_leeway) {
                if (is_before_sqout) {
                    // Leeway squeeze, counted even though it is late.
                    sp_delta += be.points;
                } else if (is_exact_sqout) {
                    // Leeway squeeze, but with the reduced sqout points.
                    sp_delta += be.sqout_points;
                }
            }
        } else {
            if (is_leeway) sp_delta += be.points;
        }
    }

    if (sp_delta) {
        c.sc[2] += sp_delta;
        c.score += sp_delta;
    }

    *child = c;
}

// --- hypath.GraphPath.branch_deactivate ----------------------------------
bool Engine::branch_deactivate(Path& p, Path* child, bool* has_child) {
    *has_child = false;

    const hy_node& n = node(p.node);
    if (n.branch_edge < 0) return true;

    const hy_edge& e = edge(n.branch_edge);
    const int32_t deact_type = deactivation_type(e, p.sp_end_time);

    if (deact_type == DEACT_NONE) return true;

    if (deact_type == DEACT_NORMAL) {
        create_deactivated_path(p, child, false);
        *has_child = true;
        return false;   // SP ran out here; this path cannot continue
    }

    // DEACT_SQINOUT. The SqOut child is built first, so it snapshots this path
    // before the SqIn marker lands and before sp_end_time is pushed out.
    create_deactivated_path(p, child, true);
    *has_child = true;

    p.act_tail = clone_tail(p.act_tail);
    if (p.act_tail >= 0) {
        Act& a = acts_[p.act_tail];
        a.sq_tail = push_sq(a.sq_tail, HY_SQ_IN, e.sqinout_timing);
    }

    p.sp_end_time = e.sqin_time;
    // Both sides remember the phrase, so advance does not count it twice.
    p.buffered = e.late_sqin_count;
    child->buffered = e.late_sqin_count;
    return true;
}

// --- difficulty (only consulted when an ms filter is set) ----------------
double Engine::act_difficulty(int32_t act) const {
    if (act < 0) return NO_DOUBLE;
    const Act& a = acts_[act];

    double best = NO_DOUBLE;
    for (int32_t s = a.sq_tail; s >= 0; s = sqs_[s].prev) {
        // SqIn.difficulty is the offset; SqOut.difficulty negates it.
        const double d =
            sqs_[s].kind == HY_SQ_IN ? sqs_[s].offset : -sqs_[s].offset + 0.0;
        if (!has_value(best) || d > best) best = d;
    }

    // e_difficulty contributes only on an E0: critical and unskipped.
    if (a.e_offset < 70 && a.skips == 0) {
        const double d = -a.e_offset + 0.0;
        if (!has_value(best) || d > best) best = d;
    }
    return best;
}

void Engine::close_last_activation(Path& p) const {
    const double d = act_difficulty(p.act_tail);
    if (has_value(d) && (!has_value(p.diff_prefix) || d > p.diff_prefix)) {
        p.diff_prefix = d;
    }
}

double Engine::search_difficulty(const Path& p) const {
    double d = p.diff_prefix;
    const double last = act_difficulty(p.act_tail);
    if (has_value(last) && (!has_value(d) || last > d)) d = last;
    return d;
}

bool Engine::passes_ms_filter(const Path& p) const {
    const double d = search_difficulty(p);
    if (!has_value(d)) return true;
    return d <= in_.ms_filter;
}

// --- hypath.GraphPather._reduce_group ------------------------------------
void Engine::reduce_group(const int32_t* members, int32_t n) {
    // In 'scores' mode a path is dropped only when more than depth_value
    // distinct scores beat it, so a group holding at most depth_value + 1 of
    // them cannot drop anything. With no ties there is nothing to merge, and
    // with nothing filtered nothing to remove on that account, which leaves
    // the general case below with no work to do.
    if (in_.depth_mode == HY_DEPTH_SCORES &&
        (int64_t)n <= (int64_t)in_.depth_value + 1) {
        bool any_filtered = false;
        for (int32_t i = 0; i < n; ++i) {
            if (filtered_[members[i]]) { any_filtered = true; break; }
        }
        if (!any_filtered) {
            distinct_map_.reset((size_t)n);
            bool all_distinct = true;
            for (int32_t i = 0; i < n; ++i) {
                bool inserted = false;
                distinct_map_.get_or_insert((uint64_t)cur_[members[i]].score, i,
                                            &inserted);
                if (!inserted) { all_distinct = false; break; }
            }
            if (all_distinct) return;
        }
    }

    // Ties first. Paths that score the same under the same filter status are
    // one result reached different ways, so the first of them carries the rest
    // as variants and continues on behalf of all of them. "First" is position
    // in this group, which is expansion order -- that ordering is load-bearing.
    survivors_.clear();
    tie_map_.reset((size_t)n);
    for (int32_t i = 0; i < n; ++i) {
        const int32_t idx = members[i];
        const uint64_t key = ((uint64_t)cur_[idx].score << 1)
                           | (filtered_[idx] ? 1ull : 0ull);

        bool inserted = false;
        const int32_t leader_idx = tie_map_.get_or_insert(key, idx, &inserted);
        if (inserted) {
            survivors_.push_back(idx);
            continue;
        }

        Path& leader = cur_[leader_idx];
        const Path& p = cur_[idx];
        // Up to MAX_TIED_PATHS ways of scoring this much. Past that the path
        // is simply dropped: it is neither kept nor followed any further.
        if (leader.tied_count + p.tied_count <= MAX_TIED_PATHS) {
            Variant v;
            v.prev = leader.var_head;
            v.var_point = act_count(leader);
            for (int32_t k = 0; k < 6; ++k) v.sc[k] = p.sc[k];
            v.notecount = p.notecount;
            v.leftover_sp = p.sp;
            v.skipped_accents = p.skipped_accents;
            v.skipped_ghosts = p.skipped_ghosts;
            v.act_tail = p.act_tail;
            v.var_head = p.var_head;
            v.tied_count = p.tied_count;
            variants_.push_back(v);
            leader.var_head = (int32_t)variants_.size() - 1;
            leader.tied_count += p.tied_count;
        }
        removed_[idx] = 1;
    }

    if (survivors_.size() < 2) return;

    // Distinct scores over the whole group, achievable or not. A filtered path
    // stays only while it is within the depth band here -- while it could still
    // be the single best path, which is shown even when unachievable. Without
    // this band a filtered path is dropped only when an achievable path beats
    // it, and on an uncapped chart the achievable frontier scores far below the
    // hard paths, so they all survive and the frontier explodes. The band
    // collapses each group back to the depth setting, as an unfiltered search
    // does, and cannot drop the eventual best path (score dominance keeps the
    // top band at every step). Mirror of hypath._reduce_group.
    dominating_.clear();
    for (size_t i = 0; i < survivors_.size(); ++i) {
        dominating_.push_back(cur_[survivors_[i]].score);
    }
    std::sort(dominating_.begin(), dominating_.end());
    dominating_.erase(std::unique(dominating_.begin(), dominating_.end()),
                      dominating_.end());
    const int64_t best_all = dominating_.back();
    const int32_t n_dominating = (int32_t)dominating_.size();

    // The scores that are allowed to eliminate an achievable path. A filtered
    // path cannot, unless it is optimal. May be empty mid-search (every live
    // path in this group is filtered): there is then no achievable path to
    // prune, and the band above still reins the filtered ones in.
    beating_.clear();
    for (size_t i = 0; i < survivors_.size(); ++i) {
        const int32_t idx = survivors_[i];
        const int64_t s = cur_[idx].score;
        if (!filtered_[idx] || (has_optimal_ && s == optimal_score_)) {
            beating_.push_back(s);
        }
    }
    std::sort(beating_.begin(), beating_.end());
    beating_.erase(std::unique(beating_.begin(), beating_.end()),
                   beating_.end());
    const int64_t best = beating_.empty() ? 0 : beating_.back();
    const int32_t n_beating = (int32_t)beating_.size();

    for (size_t i = 0; i < survivors_.size(); ++i) {
        const int32_t idx = survivors_[i];
        const int64_t score = cur_[idx].score;
        // How many distinct achievable scores beat this path.
        const int32_t outscored_by =
            n_beating - (int32_t)(std::upper_bound(beating_.begin(),
                                                   beating_.end(), score)
                                  - beating_.begin());

        if (filtered_[idx]) {
            // Removed once an achievable path beats it (as before), and also
            // once it falls outside the overall depth band -- past that it can
            // neither be shown nor become the best path.
            if (outscored_by) {
                removed_[idx] = 1;
            } else if (in_.depth_mode == HY_DEPTH_POINTS) {
                if (score + in_.depth_value < best_all) removed_[idx] = 1;
            } else if (in_.depth_mode == HY_DEPTH_SCORES) {
                const int32_t outscored_by_all =
                    n_dominating - (int32_t)(std::upper_bound(
                                        dominating_.begin(), dominating_.end(),
                                        score)
                                    - dominating_.begin());
                if (outscored_by_all > in_.depth_value) removed_[idx] = 1;
            }
        } else if (in_.depth_mode == HY_DEPTH_POINTS) {
            if (score + in_.depth_value < best) removed_[idx] = 1;
        } else if (in_.depth_mode == HY_DEPTH_SCORES) {
            if (outscored_by > in_.depth_value) removed_[idx] = 1;
        }
    }
}

// --- hypath.GraphPather._prune_hopeless_paths ----------------------------
void Engine::prune_hopeless_paths() {
    const int32_t n = (int32_t)cur_.size();

    // The guaranteed scores: a finished path's score, and an unfinished
    // base-track path's score plus the rest of the song played without
    // activating again. Both are achievable. Built from every path this
    // iteration, before compaction, matching the Python which assembles this
    // list before its reduction removes anything -- so a path a tie merge just
    // dropped still contributes the score it reached. The ladder incumbent is
    // not seeded here (the native search is not handed it); it would only raise
    // the bar and prune more, never change which paths are kept.
    guaranteed_.clear();
    for (int32_t i = 0; i < n; ++i) {
        const Path& p = cur_[i];
        if (p.node < 0) {
            guaranteed_.push_back(p.score);
        } else if (!node(p.node).is_sp) {
            guaranteed_.push_back(p.score + node(p.node).base_suffix);
        }
    }
    if (guaranteed_.empty()) return;

    int64_t bar;
    if (in_.depth_mode == HY_DEPTH_SCORES) {
        // The (depth_value + 1)-th largest distinct guaranteed score: clearing
        // it is necessary to be listed.
        std::sort(guaranteed_.begin(), guaranteed_.end(),
                  [](int64_t a, int64_t b) { return a > b; });
        guaranteed_.erase(std::unique(guaranteed_.begin(), guaranteed_.end()),
                          guaranteed_.end());
        const int64_t band = (int64_t)in_.depth_value + 1;
        if ((int64_t)guaranteed_.size() < band) return;
        bar = guaranteed_[(size_t)(band - 1)];
    } else if (in_.depth_mode == HY_DEPTH_POINTS) {
        int64_t mx = guaranteed_[0];
        for (size_t i = 1; i < guaranteed_.size(); ++i) {
            if (guaranteed_[i] > mx) mx = guaranteed_[i];
        }
        bar = mx - in_.depth_value;
    } else {
        return;
    }

    // Every integer here is well under 2^53, so double arithmetic is exact and
    // matches the Python's int/float mix bit for bit -- the density is the only
    // genuinely fractional term, and it is the same double on both sides (it is
    // computed once in Python and carried in through the flat graph).
    const double bar_d = (double)bar;
    for (int32_t i = 0; i < n; ++i) {
        if (removed_[i]) continue;
        const Path& p = cur_[i];
        if (p.node < 0) continue;
        const hy_node& nd = node(p.node);

        double ceiling;
        if (nd.is_sp) {
            // An SP-active path has spent its meter (sp reads 0), so 2*(0+r)
            // would understate the SP time left in its current activation.
            // Keep the loose but always-safe max_suffix ceiling.
            ceiling = (double)p.score + (double)nd.max_suffix;
        } else {
            const int64_t total_sp = nd.total_spscore_suffix;
            const double sp_measures =
                2.0 * (double)(p.sp + nd.remaining_sp_phrases);
            double tight_sp = sp_measures * nd.max_spscore_density;
            if (tight_sp > (double)total_sp) tight_sp = (double)total_sp;
            ceiling = (double)p.score + (double)nd.max_suffix
                    - (double)total_sp + tight_sp;
        }

        if (ceiling < bar_d) removed_[i] = 1;
    }
}

// --- hypath.GraphPather._reduce_iteration_paths --------------------------
void Engine::reduce_iteration_paths() {
    const int32_t n = (int32_t)cur_.size();

    filtered_.assign((size_t)n, 0);
    removed_.assign((size_t)n, 0);

    // Before path comparisons, check each path against the ms filter. Filtered
    // paths cannot be used to eliminate paths, and are eliminated immediately
    // if worse than a single path.
    if (in_.has_ms_filter) {
        for (int32_t i = 0; i < n; ++i) {
            if (!passes_ms_filter(cur_[i])) filtered_[i] = 1;
        }
    }

    has_optimal_ = false;
    optimal_score_ = 0;

    owner_.assign((size_t)n, -1);
    group_map_.reset((size_t)n);
    int32_t n_groups = 0;

    for (int32_t i = 0; i < n; ++i) {
        const Path& p = cur_[i];
        const bool is_complete = p.node < 0;

        // Computed over every path, before the grouping gate below.
        if (is_complete && (!has_optimal_ || p.score > optimal_score_)) {
            has_optimal_ = true;
            optimal_score_ = p.score;
        }

        // Paths that recently SqIn/SqOuted have interacted with an SP phrase
        // earlier than the others, so they are in no group at all: never
        // merged, never dropped by a comparison.
        if (p.buffered != 0) continue;

        const bool is_sp = !is_complete && node(p.node).is_sp;
        const int64_t sp_value =
            is_complete ? 0 : (is_sp ? p.sp_end_time : (int64_t)p.sp);
        // A complete path and an unfinished base-track path holding no SP
        // share the key (false, 0). That collision is deliberate: it is what
        // reduces every finished path together on the last iteration.
        const uint64_t key = ((uint64_t)sp_value << 1) | (is_sp ? 1ull : 0ull);

        bool inserted = false;
        const int32_t g = group_map_.get_or_insert(key, n_groups, &inserted);
        if (inserted) ++n_groups;
        owner_[i] = g;
    }

    // Lay the groups out contiguously by counting sort, preserving the order
    // paths appear in -- which is what decides tie leaders.
    counts_.assign((size_t)n_groups, 0);
    for (int32_t i = 0; i < n; ++i) {
        if (owner_[i] >= 0) ++counts_[owner_[i]];
    }
    group_begin_.assign((size_t)n_groups, 0);
    group_end_.assign((size_t)n_groups, 0);
    int32_t running = 0;
    for (int32_t g = 0; g < n_groups; ++g) {
        group_begin_[g] = running;
        group_end_[g] = running;
        running += counts_[g];
    }
    group_members_.assign((size_t)running, 0);
    for (int32_t i = 0; i < n; ++i) {
        const int32_t g = owner_[i];
        if (g >= 0) group_members_[group_end_[g]++] = i;
    }

    for (int32_t g = 0; g < n_groups; ++g) {
        const int32_t begin = group_begin_[g], end = group_end_[g];
        if (end - begin > 1) {
            reduce_group(&group_members_[begin], end - begin);
        }
    }

    // Cross-group pruning: drop paths that provably cannot reach the results,
    // whatever they do next. The reduction above only compares paths holding
    // the same SP, so this is the only thing that cuts across SP situations.
    if (in_.enable_bound_prune) {
        prune_hopeless_paths();
    }

    // Order-preserving compaction, matching the Python's list comprehension.
    int32_t w = 0;
    for (int32_t i = 0; i < n; ++i) {
        if (!removed_[i]) {
            if (w != i) cur_[(size_t)w] = cur_[(size_t)i];
            ++w;
        }
    }
    cur_.resize((size_t)w);
}

// --- output --------------------------------------------------------------
void Engine::emit_acts(int32_t act_tail, int32_t* begin, int32_t* end) {
    // The chain runs backwards; collect and reverse so activations come out in
    // the order they were made.
    chain_scratch_.clear();
    for (int32_t a = act_tail; a >= 0; a = acts_[a].parent) {
        chain_scratch_.push_back(a);
    }

    *begin = (int32_t)out_acts_.size();
    for (size_t i = chain_scratch_.size(); i-- > 0;) {
        const Act& a = acts_[chain_scratch_[i]];

        sq_scratch_.clear();
        for (int32_t s = a.sq_tail; s >= 0; s = sqs_[s].prev) {
            sq_scratch_.push_back(s);
        }

        hy_out_act oa;
        oa.act_node = a.act_node;
        oa.skips = a.skips;
        oa.sp_meter = a.sp_meter;
        oa.deact_edge = a.deact_edge;
        oa.e_offset = a.e_offset;
        oa.sq_begin = (int32_t)out_sqs_.size();
        for (size_t k = sq_scratch_.size(); k-- > 0;) {
            hy_out_sq os;
            os.kind = sqs_[sq_scratch_[k]].kind;
            os._pad = 0;
            os.offset = sqs_[sq_scratch_[k]].offset;
            out_sqs_.push_back(os);
        }
        oa.sq_end = (int32_t)out_sqs_.size();
        out_acts_.push_back(oa);
    }
    *end = (int32_t)out_acts_.size();

    // emit_acts is re-entered by the caller for the next path, so the scratch
    // buffers carry nothing between calls.
}

void Engine::emit_variant(int32_t v, int32_t depth) {
    // Restore add order: the cons list was built by prepending.
    std::vector<int32_t> order;
    for (int32_t i = v; i >= 0; i = variants_[i].prev) order.push_back(i);

    for (size_t k = order.size(); k-- > 0;) {
        const Variant& var = variants_[order[k]];
        hy_out_path op;
        op.score_base = var.sc[0];
        op.score_combo = var.sc[1];
        op.score_sp = var.sc[2];
        op.score_solo = var.sc[3];
        op.score_accents = var.sc[4];
        op.score_ghosts = var.sc[5];
        op.notecount = var.notecount;
        op.leftover_sp = var.leftover_sp;
        op.skipped_accents = var.skipped_accents;
        op.skipped_ghosts = var.skipped_ghosts;
        op.var_point = var.var_point;
        op.depth = depth;
        emit_acts(var.act_tail, &op.act_begin, &op.act_end);
        out_paths_.push_back(op);

        emit_variant(var.var_head, depth + 1);
    }
}

void Engine::emit_path(const Path& p) {
    hy_out_path op;
    op.score_base = p.sc[0];
    op.score_combo = p.sc[1];
    op.score_sp = p.sc[2];
    op.score_solo = p.sc[3];
    op.score_accents = p.sc[4];
    op.score_ghosts = p.sc[5];
    op.notecount = p.notecount;
    op.leftover_sp = p.sp;
    op.skipped_accents = p.skipped_accents;
    op.skipped_ghosts = p.skipped_ghosts;
    op.var_point = -1;
    op.depth = 0;
    emit_acts(p.act_tail, &op.act_begin, &op.act_end);
    out_paths_.push_back(op);

    emit_variant(p.var_head, 1);
}

int32_t Engine::run(hy_search_out* out) {
    Path root;
    std::memset(&root, 0, sizeof(root));
    root.node = in_.start_node;
    root.act_tail = -1;
    root.var_head = -1;
    root.tied_count = 1;
    root.sp_end_time = HY_NO_TIME;
    root.sp_ready_ms = NO_DOUBLE;
    root.skipped_e_offset = NO_DOUBLE;
    root.diff_prefix = NO_DOUBLE;

    cur_.clear();
    cur_.push_back(root);

    for (;;) {
        bool any_live = false;
        for (size_t i = 0; i < cur_.size(); ++i) {
            if (cur_[i].node >= 0) { any_live = true; break; }
        }
        if (!any_live) break;

        next_.clear();
        next_.reserve(cur_.size() * 2);

        for (size_t i = 0; i < cur_.size(); ++i) {
            Path p = cur_[i];
            advance(p);
            if (p.node == NODE_BROKEN) return HY_ERR_BAD_STATE;

            if (p.node < 0) {
                next_.push_back(p);
                continue;
            }

            Path child;
            bool has_child = false;
            if (node(p.node).is_sp) {
                const bool can_extend = branch_deactivate(p, &child, &has_child);
                if (can_extend) next_.push_back(p);
            } else {
                has_child = branch_activate(p, &child);
                if (p.node == NODE_BROKEN) return HY_ERR_BAD_STATE;
                next_.push_back(p);
            }
            if (has_child) next_.push_back(child);
        }

        cur_.swap(next_);
        reduce_iteration_paths();
        ++iterations_;

        if (in_.progress) {
            // Matches the Python: the first surviving path's node, and the
            // iteration count over the graph length.
            const int32_t head = cur_.empty() ? -1 : cur_[0].node;
            const double frac = in_.graph_length > 0
                                    ? (double)iterations_ / in_.graph_length
                                    : 0.0;
            if (in_.progress(head, frac) != 0) return HY_ERR_CANCELLED;
        }

        // The reduction never drops the best path of a group, so an empty list
        // means the invariant broke rather than that the song ended.
        if (cur_.empty()) return HY_ERR_BAD_STATE;
    }

    // Order the completed paths by score. Stable, so tied paths keep the order
    // the reduction left them in.
    std::vector<int32_t> order(cur_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int32_t)i;
    std::stable_sort(order.begin(), order.end(),
                     [this](int32_t a, int32_t b) {
                         return cur_[(size_t)a].score > cur_[(size_t)b].score;
                     });

    for (size_t i = 0; i < order.size(); ++i) emit_path(cur_[(size_t)order[i]]);

    // Hand the three arrays over. They move into a heap-allocated holder so
    // the pointers stay valid until hy_search_free runs.
    Holder* h = new (std::nothrow) Holder();
    if (!h) return HY_ERR_NO_MEMORY;
    h->paths.swap(out_paths_);
    h->acts.swap(out_acts_);
    h->sqs.swap(out_sqs_);

    out->paths = h->paths.empty() ? nullptr : h->paths.data();
    out->acts = h->acts.empty() ? nullptr : h->acts.data();
    out->sqs = h->sqs.empty() ? nullptr : h->sqs.data();
    out->n_paths = (int32_t)h->paths.size();
    out->n_acts = (int32_t)h->acts.size();
    out->n_sqs = (int32_t)h->sqs.size();
    out->iterations = iterations_;
    out->handle = h;
    return HY_OK;
}

// --- Activation DP methods ----------------------------------------------

// The base track is a single linear chain reached by following adv_edge from
// the start; every deactivation lands on one of its nodes, so node->index is a
// complete addressing scheme for reset points and candidates.
void Engine::dp_build_index() {
    node_to_base_.assign(in_.n_nodes, -1);
    base_nodes_.clear();
    for (int32_t nd = in_.start_node; nd >= 0;) {
        node_to_base_[nd] = (int32_t)base_nodes_.size();
        base_nodes_.push_back(nd);
        const hy_node& n = node(nd);
        nd = n.adv_edge >= 0 ? edge(n.adv_edge).dest : -1;
    }
}

// A throwaway path seeded to a reset (or candidate) state, matching the fresh
// GraphPath hypath.DPPather builds.
Path Engine::dp_seed(int32_t nd, int32_t sp, int32_t buffered, double ready_ms) {
    Path p;
    std::memset(&p, 0, sizeof(p));
    p.node = nd;
    p.sp = sp;
    p.buffered = buffered;
    p.act_tail = -1;
    p.var_head = -1;
    p.tied_count = 1;
    p.sp_end_time = HY_NO_TIME;
    p.sp_ready_ms = ready_ms;
    p.skipped_e_offset = NO_DOUBLE;
    p.diff_prefix = NO_DOUBLE;
    return p;
}

// Drive the base track forward from a node to the next candidate (a base node
// carrying an activation branch edge), or the end. Returns the base score
// collected and the meter state on arrival. Reuses advance so the base score
// and meter/ready-time evolution are exactly the BFS's.
DpWalk Engine::dp_walk_to_next_candidate(int32_t node_index, int32_t sp,
                                         double sp_ready_ms, int32_t buffered) {
    Path seg = dp_seed(base_nodes_[node_index], sp, buffered, sp_ready_ms);
    for (;;) {
        advance(seg);
        if (seg.node == NODE_BROKEN) {
            dp_failed_ = true;
            return DpWalk{0, -1, sp, buffered, sp_ready_ms};
        }
        if (seg.node < 0 || node(seg.node).is_sp) {
            // End of song (or the guard the base track never trips).
            return DpWalk{seg.score, -1, seg.sp, seg.buffered, seg.sp_ready_ms};
        }
        if (node(seg.node).branch_edge >= 0) {
            return DpWalk{seg.score, node_to_base_[seg.node], seg.sp,
                          seg.buffered, seg.sp_ready_ms};
        }
    }
}

// Top-k (score, plan) achievable from *arriving at* a base node. The candidate-
// local recurrence (see the header comment above): activate here, or skip to
// the next candidate. Mirrors hypath.DPPather._best_from_node. The score
// returned excludes the base track consumed to reach this node; the caller adds
// it. The result is computed fully before it is stored, so the recursion never
// holds a reference into dp_memo_ across a mutation.
std::vector<DpEntry> Engine::dp_best_from_node(int32_t node_index, int32_t sp,
                                               double sp_ready_ms,
                                               int32_t buffered) {
    const DpNodeKey key{node_index, sp, buffered, dp_ready_bits(sp_ready_ms)};
    auto it = dp_memo_.find(key);
    if (it != dp_memo_.end()) return it->second;

    std::vector<DpEntry> options;
    const hy_node& n = node(base_nodes_[node_index]);

    // Option A: activate here, if this node is a legal candidate.
    if (n.branch_edge >= 0 && sp >= 2 && has_value(sp_ready_ms)) {
        const hy_edge& be = edge(n.branch_edge);
        if (be.activation_fill_deadline_ms - sp_ready_ms >= -70) {
            const std::vector<DpOutcome>& outs =
                dp_activation_outcomes(node_index, sp);
            if (dp_failed_) return {};
            for (const DpOutcome& oc : outs) {
                if (oc.completed) {
                    const int32_t plan =
                        dp_make_act(node_index, oc.idx, PLAN_DONE);
                    options.push_back(DpEntry{oc.delta, plan});
                } else {
                    std::vector<DpEntry> futs = dp_best_from_node(
                        node_to_base_[oc.landing], oc.residual, NO_DOUBLE,
                        oc.buffered);
                    if (dp_failed_) return {};
                    for (const DpEntry& fe : futs) {
                        const int32_t plan =
                            dp_make_act(node_index, oc.idx, fe.plan);
                        options.push_back(
                            DpEntry{oc.delta + fe.score, plan});
                    }
                }
            }
        }
    }

    // Option B: skip -- walk to the next candidate, then continue from there.
    DpWalk w = dp_walk_to_next_candidate(node_index, sp, sp_ready_ms, buffered);
    if (dp_failed_) return {};
    if (w.next_index < 0) {
        options.push_back(DpEntry{w.base_score, PLAN_STOP});
    } else {
        std::vector<DpEntry> futs = dp_best_from_node(w.next_index, w.sp,
                                                      w.sp_ready_ms, w.buffered);
        if (dp_failed_) return {};
        for (const DpEntry& fe : futs) {
            options.push_back(DpEntry{w.base_score + fe.score, fe.plan});
        }
    }

    std::vector<DpEntry> result = dp_merge_topk(options);
    dp_memo_.emplace(key, result);
    return result;
}

// Every way activating at a candidate holding `sp` bars can resolve. Pure score
// deltas from the activation onward; depends only on (candidate, sp), so it is
// memoized. Mirrors hypath.DPPather._activation_outcomes. References into
// dp_act_memo_ stay valid across later insertions (unordered_map only
// invalidates references on erase), so callers may hold the returned reference
// across recursive calls.
const std::vector<DpOutcome>& Engine::dp_activation_outcomes(int32_t cand_index,
                                                             int32_t sp) {
    const uint64_t key = dp_act_key(cand_index, sp);
    auto it = dp_act_memo_.find(key);
    if (it != dp_act_memo_.end()) return it->second;

    // The dummy ready time only has to keep branch_activate's own -70 check from
    // firing; the genuine deadline gate is applied by dp_best_from before this
    // is ever called. It never affects score, only the (unused) Act e_offset.
    Path seed = dp_seed(base_nodes_[cand_index], sp, 0, dp_dummy_ready_ms_);
    Path activated;
    std::vector<DpOutcome> outcomes;
    const bool ok = branch_activate(seed, &activated);
    if (seed.node == NODE_BROKEN) { dp_failed_ = true; }
    else if (ok) { dp_simulate_sp(activated, outcomes); }

    auto res = dp_act_memo_.emplace(key, std::move(outcomes));
    return res.first->second;
}

// Follow an activated path through the SP track, collecting outcomes. Reuses
// advance / branch_deactivate, so the SP scoring, the sqin/sqout branching and
// the backend adjustments are exactly the BFS's. Mirrors
// hypath.DPPather._simulate_sp.
void Engine::dp_simulate_sp(Path p, std::vector<DpOutcome>& outcomes) {
    int32_t idx = 0;
    while (p.node >= 0) {
        advance(p);
        if (p.node == NODE_BROKEN) { dp_failed_ = true; return; }
        if (p.node < 0) {
            // The song ended while still in SP: a completed path.
            DpOutcome o;
            o.completed = 1;
            o.idx = idx;
            o.delta = p.score;
            o.landing = -1;
            o.residual = 0;
            o.buffered = 0;
            outcomes.push_back(o);
            break;
        }
        if (node(p.node).is_sp) {
            Path child;
            bool has_child = false;
            const bool can_extend = branch_deactivate(p, &child, &has_child);
            if (has_child) {
                DpOutcome o;
                o.completed = 0;
                o.idx = idx;
                o.delta = child.score;
                o.landing = child.node;
                o.residual = child.sp;
                o.buffered = child.buffered;
                outcomes.push_back(o);
                ++idx;
            }
            if (!can_extend) break;
        } else {
            break;
        }
    }
}

// Collapse (score, plan) options to the best plan per distinct score (first
// seen wins, matching the Python dict), then keep the depth slice: the top k
// distinct scores in 'scores' mode, or everything within depth_value of the
// best in 'points' mode. Mirrors hypath.DPPather._merge_topk.
std::vector<DpEntry> Engine::dp_merge_topk(std::vector<DpEntry>& options) {
    if (options.empty()) return {};

    std::unordered_map<int64_t, int32_t> best_plan;
    best_plan.reserve(options.size() * 2);
    for (const DpEntry& e : options) {
        best_plan.emplace(e.score, e.plan);   // no-op if the score is present
    }

    std::vector<int64_t> scores;
    scores.reserve(best_plan.size());
    for (const auto& kv : best_plan) scores.push_back(kv.first);
    std::sort(scores.begin(), scores.end(), std::greater<int64_t>());

    if (dp_points_mode_) {
        const int64_t best = scores[0];
        std::vector<int64_t> kept;
        for (int64_t s : scores) {
            if (s + dp_depth_value_ >= best) kept.push_back(s);
        }
        scores.swap(kept);
    } else if ((int32_t)scores.size() > dp_k_) {
        scores.resize(dp_k_);
    }

    std::vector<DpEntry> result;
    result.reserve(scores.size());
    for (int64_t s : scores) result.push_back(DpEntry{s, best_plan[s]});
    return result;
}

int32_t Engine::dp_make_act(int32_t cand_index, int32_t outcome_idx,
                            int32_t child) {
    DpPlan pl;
    pl.kind = PLAN_KIND_ACT;
    pl.cand_index = cand_index;
    pl.outcome_idx = outcome_idx;
    pl.child = child;
    plans_.push_back(pl);
    return (int32_t)plans_.size() - 1;
}

// Rebuild a real Path by replaying a backtrack plan through the same primitives,
// so the result carries genuine activations, backends and squeezes -- not just
// the right total. Mirrors hypath.DPPather._replay. The finished Path is fed to
// emit_path unchanged.
Path Engine::dp_replay(int32_t plan) {
    Path p = dp_seed(in_.start_node, 0, 0, NO_DOUBLE);

    for (;;) {
        const DpPlan pl = plans_[plan];
        if (pl.kind == PLAN_KIND_DONE) break;
        if (pl.kind == PLAN_KIND_STOP) {
            while (p.node >= 0) {
                advance(p);
                if (p.node == NODE_BROKEN) { dp_failed_ = true; return p; }
                if (p.node >= 0 && !node(p.node).is_sp &&
                    node(p.node).branch_edge >= 0) {
                    Path child;
                    branch_activate(p, &child);
                    if (p.node == NODE_BROKEN) { dp_failed_ = true; return p; }
                }
            }
            break;
        }

        const int32_t cand_index = pl.cand_index;
        const int32_t outcome_idx = pl.outcome_idx;
        const int32_t child_plan = pl.child;

        // Advance to the chosen candidate, activating at intervening candidates
        // so skip counts and e-offsets stay faithful.
        while (!(p.node >= 0 && node_to_base_[p.node] == cand_index)) {
            advance(p);
            if (p.node == NODE_BROKEN) { dp_failed_ = true; return p; }
            if (p.node < 0) break;
            if (!node(p.node).is_sp && node(p.node).branch_edge >= 0 &&
                node_to_base_[p.node] != cand_index) {
                Path child;
                branch_activate(p, &child);
                if (p.node == NODE_BROKEN) { dp_failed_ = true; return p; }
            }
        }
        if (p.node < 0) { dp_failed_ = true; return p; }

        Path activated;
        const bool ok = branch_activate(p, &activated);
        if (p.node == NODE_BROKEN || !ok) { dp_failed_ = true; return p; }

        // Follow the SP track to the recorded outcome.
        Path q = activated;
        int32_t idx = 0;
        int32_t landing_kind = -1;   // 0 completed, 1 deact
        Path landing_path{};
        while (q.node >= 0) {
            advance(q);
            if (q.node == NODE_BROKEN) { dp_failed_ = true; return q; }
            if (q.node < 0) { landing_kind = 0; landing_path = q; break; }
            if (node(q.node).is_sp) {
                Path child;
                bool has_child = false;
                const bool can_extend = branch_deactivate(q, &child, &has_child);
                if (has_child) {
                    if (idx == outcome_idx) {
                        landing_kind = 1;
                        landing_path = child;
                        break;
                    }
                    ++idx;
                }
                if (!can_extend) {
                    landing_kind = 1;
                    landing_path = child;
                    break;
                }
            } else {
                break;
            }
        }

        if (landing_kind != 1) {
            p = (landing_kind == 0) ? landing_path : q;
            plan = PLAN_DONE;
            continue;
        }
        p = landing_path;
        plan = child_plan;
    }

    return p;
}

int32_t Engine::dp_run(hy_search_out* out) {
    dp_build_index();

    dp_points_mode_ = in_.depth_mode == HY_DEPTH_POINTS;
    dp_depth_value_ = in_.depth_value;
    dp_k_ = in_.depth_value + 1;

    // Any ready time early enough that no fill deadline can be more than 70ms
    // before it; the real deadline gate lives in dp_best_from. It never affects
    // score, so a floor well below every real ms is exact here.
    dp_dummy_ready_ms_ = -1e18;
    dp_failed_ = false;

    // Sentinel plans first, so PLAN_STOP / PLAN_DONE name plans_[0] / plans_[1].
    plans_.clear();
    plans_.push_back(DpPlan{PLAN_KIND_STOP, -1, -1, -1});
    plans_.push_back(DpPlan{PLAN_KIND_DONE, -1, -1, -1});

    std::vector<DpEntry> entries = dp_best_from_node(0, 0, NO_DOUBLE, 0);
    if (dp_failed_) return HY_ERR_BAD_STATE;

    std::vector<Path> finals;
    finals.reserve(entries.size());
    for (const DpEntry& e : entries) {
        Path fp = dp_replay(e.plan);
        if (dp_failed_) return HY_ERR_BAD_STATE;
        finals.push_back(fp);
    }

    // Order by score, descending, stable to match the Python's sort.
    std::stable_sort(finals.begin(), finals.end(),
                     [](const Path& a, const Path& b) {
                         return a.score > b.score;
                     });
    for (const Path& fp : finals) emit_path(fp);

    Holder* h = new (std::nothrow) Holder();
    if (!h) return HY_ERR_NO_MEMORY;
    h->paths.swap(out_paths_);
    h->acts.swap(out_acts_);
    h->sqs.swap(out_sqs_);

    out->paths = h->paths.empty() ? nullptr : h->paths.data();
    out->acts = h->acts.empty() ? nullptr : h->acts.data();
    out->sqs = h->sqs.empty() ? nullptr : h->sqs.data();
    out->n_paths = (int32_t)h->paths.size();
    out->n_acts = (int32_t)h->acts.size();
    out->n_sqs = (int32_t)h->sqs.size();
    out->iterations = 0;
    out->handle = h;
    return HY_OK;
}

#ifdef _WIN32
// Thread entry so dp_run can execute on a large reserved stack.
struct DpThreadCtx {
    Engine* engine;
    hy_search_out* out;
    int32_t rc;
};

DWORD WINAPI dp_thread_entry(LPVOID param) {
    DpThreadCtx* c = static_cast<DpThreadCtx*>(param);
    c->rc = c->engine->dp_run(c->out);
    return 0;
}
#endif

}  // namespace

extern "C" {

int32_t hy_search(const hy_search_in* in, hy_search_out* out) {
    if (!out) return HY_ERR_NULL_OUT;
    std::memset(out, 0, sizeof(*out));
    if (!in) return HY_ERR_NULL_IN;
    if (!in->nodes || in->n_nodes <= 0) return HY_ERR_BAD_GRAPH;
    if (in->start_node < 0 || in->start_node >= in->n_nodes) {
        return HY_ERR_BAD_GRAPH;
    }
    if (in->n_edges > 0 && !in->edges) return HY_ERR_BAD_GRAPH;

    Engine engine(*in);
    return engine.run(out);
}

int32_t hy_dp_search(const hy_search_in* in, hy_search_out* out) {
    if (!out) return HY_ERR_NULL_OUT;
    std::memset(out, 0, sizeof(*out));
    if (!in) return HY_ERR_NULL_IN;
    if (!in->nodes || in->n_nodes <= 0) return HY_ERR_BAD_GRAPH;
    if (in->start_node < 0 || in->start_node >= in->n_nodes) {
        return HY_ERR_BAD_GRAPH;
    }
    if (in->n_edges > 0 && !in->edges) return HY_ERR_BAD_GRAPH;

    Engine engine(*in);
#ifdef _WIN32
    // Run on a 512 MB-reserved stack; the DP recursion is candidate-deep.
    DpThreadCtx ctx{&engine, out, HY_ERR_BAD_STATE};
    HANDLE th = CreateThread(nullptr, (SIZE_T)512 * 1024 * 1024,
                             dp_thread_entry, &ctx,
                             STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (th == nullptr) return engine.dp_run(out);   // fall back to inline
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    return ctx.rc;
#else
    return engine.dp_run(out);
#endif
}

void hy_search_free(hy_search_out* out) {
    if (!out || !out->handle) return;
    delete static_cast<Holder*>(out->handle);
    std::memset(out, 0, sizeof(*out));
}

}  // extern "C"
