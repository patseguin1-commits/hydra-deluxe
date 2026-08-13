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
#include <limits>
#include <new>
#include <vector>

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

class Engine {
public:
    explicit Engine(const hy_search_in& in) : in_(in) {}

    int32_t run(hy_search_out* out);

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

    void emit_path(const Path& p);
    void emit_variant(int32_t v, int32_t depth);
    void emit_acts(int32_t act_tail, int32_t* begin, int32_t* end);

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

    // The scores that are allowed to eliminate. A filtered path cannot, unless
    // it is optimal.
    beating_.clear();
    for (size_t i = 0; i < survivors_.size(); ++i) {
        const int32_t idx = survivors_[i];
        const int64_t s = cur_[idx].score;
        if (!filtered_[idx] || (has_optimal_ && s == optimal_score_)) {
            beating_.push_back(s);
        }
    }
    if (beating_.empty()) return;
    std::sort(beating_.begin(), beating_.end());
    beating_.erase(std::unique(beating_.begin(), beating_.end()),
                   beating_.end());

    const int64_t best = beating_.back();
    const int32_t n_beating = (int32_t)beating_.size();

    for (size_t i = 0; i < survivors_.size(); ++i) {
        const int32_t idx = survivors_[i];
        const int64_t score = cur_[idx].score;
        // How many distinct scores beat this path.
        const int32_t outscored_by =
            n_beating - (int32_t)(std::upper_bound(beating_.begin(),
                                                   beating_.end(), score)
                                  - beating_.begin());

        if (filtered_[idx]) {
            // Filtered paths are removed as soon as they are worse than
            // anything.
            if (outscored_by) removed_[idx] = 1;
        } else if (in_.depth_mode == HY_DEPTH_POINTS) {
            if (score + in_.depth_value < best) removed_[idx] = 1;
        } else if (in_.depth_mode == HY_DEPTH_SCORES) {
            if (outscored_by > in_.depth_value) removed_[idx] = 1;
        }
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

void hy_search_free(hy_search_out* out) {
    if (!out || !out->handle) return;
    delete static_cast<Holder*>(out->handle);
    std::memset(out, 0, sizeof(*out));
}

}  // extern "C"
