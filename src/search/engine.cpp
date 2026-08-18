#include "search/engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hydra {

namespace {

// The model's Path, aliased before the engine's own local `Path` (the search
// path state) shadows the name below.
using MPath = ::hydra::Path;

// "No value" for optional doubles a path carries; every comparison with NaN is
// false, so a missing timing fails loudly rather than quietly.
const double NO_DOUBLE = std::numeric_limits<double>::quiet_NaN();
inline bool has_value(double v) { return !std::isnan(v); }

const int64_t NO_TIME = -1;
const int32_t SQ_IN = 0;
const int32_t SQ_OUT = 1;
const int32_t DEPTH_SCORES = 0;
const int32_t DEPTH_POINTS = 1;

const int32_t DEACT_NONE = 0;
const int32_t DEACT_NORMAL = 1;
const int32_t DEACT_SQINOUT = 2;

const int32_t MAX_TIED_PATHS = 4;
const int32_t NODE_BROKEN = -2;

// ---- the graph, enumerated ----------------------------------------------
// The search is index-based (indices pack into memo keys and the output act
// records), so the graph is enumerated into index->object arrays. No per-field
// data is copied: node()/edge() read the objects through these arrays.
struct Enum {
    std::vector<const ScoreGraphNode*> nodes;
    std::vector<const ScoreGraphEdge*> edges;
    std::unordered_map<const ScoreGraphNode*, int32_t> node_idx;
    std::unordered_map<const ScoreGraphEdge*, int32_t> edge_idx;
    int32_t start = -1;

    int32_t node_of(const ScoreGraphNode* n) const {
        if (n == nullptr) return -1;
        auto it = node_idx.find(n);
        return it == node_idx.end() ? -1 : it->second;
    }
    int32_t edge_of(const ScoreGraphEdge* e) const {
        if (e == nullptr) return -1;
        auto it = edge_idx.find(e);
        return it == edge_idx.end() ? -1 : it->second;
    }
};

Enum enumerate(const ScoreGraph& graph) {
    Enum en;
    std::vector<const ScoreGraphNode*> pend_nodes;
    std::vector<const ScoreGraphEdge*> pend_edges;

    std::function<int32_t(const ScoreGraphNode*)> nid =
        [&](const ScoreGraphNode* n) -> int32_t {
        if (n == nullptr) return -1;
        auto it = en.node_idx.find(n);
        if (it != en.node_idx.end()) return it->second;
        int32_t idx = static_cast<int32_t>(en.nodes.size());
        en.node_idx[n] = idx;
        en.nodes.push_back(n);
        pend_nodes.push_back(n);
        return idx;
    };
    std::function<int32_t(const ScoreGraphEdge*)> eid =
        [&](const ScoreGraphEdge* e) -> int32_t {
        if (e == nullptr) return -1;
        auto it = en.edge_idx.find(e);
        if (it != en.edge_idx.end()) return it->second;
        int32_t idx = static_cast<int32_t>(en.edges.size());
        en.edge_idx[e] = idx;
        en.edges.push_back(e);
        pend_edges.push_back(e);
        return idx;
    };

    en.start = nid(graph.start());
    while (!pend_nodes.empty() || !pend_edges.empty()) {
        while (!pend_nodes.empty()) {
            const ScoreGraphNode* n = pend_nodes.back();
            pend_nodes.pop_back();
            eid(n->adv_edge);
            eid(n->branch_edge);
        }
        while (!pend_edges.empty()) {
            const ScoreGraphEdge* e = pend_edges.back();
            pend_edges.pop_back();
            nid(e->dest);
        }
    }
    return en;
}

// Cheap value-views over one node/edge, so the ported engine body keeps reading
// `n.tick` / `e.basescore`. Built per access from the objects.
struct NodeView {
    int64_t tick;
    int64_t base_suffix, max_suffix, total_spscore_suffix;
    double max_spscore_density;
    int32_t adv_edge, branch_edge, is_sp, remaining_sp_phrases;
};
struct EdgeView {
    int32_t dest;
    int32_t notecount, basescore, comboscore, spscore, soloscore, accentscore,
        ghostscore;
    int32_t frontend_points, frontend_is_accent, frontend_is_ghost;
    int32_t skipped_dynamic_points, late_sqin_count;
    double activation_fill_deadline_ms, sqinout_timing;
    int64_t sqinout_time, sqout_time, sqin_time;
};

// ---- engine data structures (verbatim from native/hydra_search.cpp) ------

const int32_t PLAN_KIND_STOP = 0;
const int32_t PLAN_KIND_DONE = 1;
const int32_t PLAN_KIND_ACT = 2;
const int32_t PLAN_STOP = 0;
const int32_t PLAN_DONE = 1;

struct Act {
    int32_t parent;
    int32_t act_node;
    int32_t skips;
    int32_t sp_meter;
    int32_t deact_edge;
    int32_t sq_tail;
    int32_t depth;
    double e_offset;
};
struct SqNode {
    int32_t prev;
    int32_t kind;
    double offset;
};
struct Variant {
    int32_t prev;
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
struct Path {
    int32_t node;
    int32_t sp;
    int32_t currentskips;
    int32_t buffered;
    int32_t act_tail;
    int32_t var_head;
    int32_t tied_count;
    int32_t notecount;
    int32_t skipped_accents;
    int32_t skipped_ghosts;
    int32_t sc[6];
    int64_t score;
    int64_t sp_end_time;
    double sp_ready_ms;
    double skipped_e_offset;
    double diff_prefix;
};

// The decision-log output (was hy_out_*), rebuilt into hydata Paths locally.
struct OutPath {
    int32_t score_base, score_combo, score_sp, score_solo, score_accents,
        score_ghosts;
    int32_t notecount, leftover_sp, skipped_accents, skipped_ghosts;
    int32_t var_point, depth, act_begin, act_end;
};
struct OutAct {
    int32_t act_node, skips, sp_meter, deact_edge, sq_begin, sq_end;
    double e_offset;
};
struct OutSq {
    int32_t kind;
    double offset;
};

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
            std::fill(stamps_.begin(), stamps_.end(), 0u);
            stamp_ = 1;
        }
    }
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

// ---- activation DP structures -------------------------------------------
struct DpEntry {
    int64_t score;
    int32_t plan;
};
struct DpOutcome {
    int32_t completed;
    int32_t idx;
    int64_t delta;
    int32_t landing;
    int32_t residual;
    int32_t buffered;
};
struct DpPlan {
    int32_t kind;
    int32_t cand_index;
    int32_t outcome_idx;
    int32_t child;
};
const int64_t DP_NO_READY = (int64_t)0x7FF8000000000000ll;
inline int64_t dp_ready_bits(double sp_ready_ms) {
    if (std::isnan(sp_ready_ms)) return DP_NO_READY;
    int64_t b;
    std::memcpy(&b, &sp_ready_ms, sizeof(b));
    return b;
}
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
        h ^= mix((uint64_t)k.ready_bits) + 0x9E3779B97F4A7C15ull + (h << 6) +
             (h >> 2);
        return (size_t)h;
    }
};
struct DpWalk {
    int64_t base_score;
    int32_t next_index;
    int32_t sp;
    int32_t buffered;
    double sp_ready_ms;
};
inline uint64_t dp_act_key(int32_t cand_index, int32_t sp) {
    return (uint64_t)(uint32_t)cand_index | ((uint64_t)(uint32_t)sp << 32);
}

// ---- the engine ----------------------------------------------------------

class Engine {
public:
    Engine(const Enum& en, bool has_sp_cap, int32_t sp_cap, int32_t depth_mode,
           int32_t depth_value, bool has_ms_filter, double ms_filter,
           bool flag_skipped_dynamics, bool no_skips, bool hard_ms_filter)
        : en_(en),
          has_sp_cap_(has_sp_cap),
          sp_cap_(sp_cap),
          depth_mode_(depth_mode),
          depth_value_(depth_value),
          has_ms_filter_(has_ms_filter),
          ms_filter_(ms_filter),
          flag_skipped_dynamics_(flag_skipped_dynamics),
          no_skips_(no_skips),
          hard_ms_filter_(hard_ms_filter) {}

    bool run();
    bool dp_run();

    // Optional 0..1 progress sink, called from run()'s BFS sweep as the frontier
    // advances through the chart. Reported values are monotonic non-decreasing.
    void set_progress_cb(std::function<void(float)> cb) { progress_cb_ = std::move(cb); }

    const std::vector<OutPath>& out_paths() const { return out_paths_; }
    const std::vector<OutAct>& out_acts() const { return out_acts_; }
    const std::vector<OutSq>& out_sqs() const { return out_sqs_; }

private:
    NodeView node(int32_t i) const {
        const ScoreGraphNode* o = en_.nodes[(size_t)i];
        NodeView v;
        v.tick = o->timecode.ticks();
        v.base_suffix = o->base_suffix;
        v.max_suffix = o->max_suffix;
        v.total_spscore_suffix = o->total_spscore_suffix;
        v.max_spscore_density = o->max_spscore_density;
        v.adv_edge = en_.edge_of(o->adv_edge);
        v.branch_edge = en_.edge_of(o->branch_edge);
        v.is_sp = o->is_sp ? 1 : 0;
        v.remaining_sp_phrases = (int32_t)o->remaining_sp_phrases;
        return v;
    }
    EdgeView edge(int32_t i) const {
        const ScoreGraphEdge* o = en_.edges[(size_t)i];
        EdgeView v;
        v.dest = en_.node_of(o->dest);
        v.notecount = (int32_t)o->notecount;
        v.basescore = (int32_t)o->basescore;
        v.comboscore = (int32_t)o->comboscore;
        v.spscore = (int32_t)o->spscore;
        v.soloscore = (int32_t)o->soloscore;
        v.accentscore = (int32_t)o->accentscore;
        v.ghostscore = (int32_t)o->ghostscore;
        v.frontend_points = 0;
        v.frontend_is_accent = 0;
        v.frontend_is_ghost = 0;
        if (o->frontend.has_value()) {
            v.frontend_points = o->frontend->points;
            const ChordNote& an = o->frontend->chord.activation_note();
            v.frontend_is_accent = an.is_accent() ? 1 : 0;
            v.frontend_is_ghost = an.is_ghost() ? 1 : 0;
        }
        v.skipped_dynamic_points = o->skipped_dynamic_points;
        v.late_sqin_count = o->late_sqin_count;
        v.activation_fill_deadline_ms =
            o->activation_fill_deadline_ms.value_or(0.0);
        v.sqinout_timing = o->sqinout_timing.value_or(0.0);
        v.sqinout_time = o->sqinout_time ? o->sqinout_time->ticks() : NO_TIME;
        v.sqout_time = o->sqout_time ? o->sqout_time->ticks() : NO_TIME;
        v.sqin_time = o->sqin_time ? o->sqin_time->ticks() : NO_TIME;
        return v;
    }
    const ScoreGraphEdge* eobj(int32_t i) const { return en_.edges[(size_t)i]; }

    int32_t new_act(int32_t parent, int32_t act_node, int32_t skips,
                    int32_t sp_meter, double e_offset) {
        Act a;
        a.parent = parent;
        a.act_node = act_node;
        a.skips = skips;
        a.sp_meter = sp_meter;
        a.deact_edge = -1;
        a.sq_tail = -1;
        a.depth = (parent < 0 ? 0 : acts_[(size_t)parent].depth) + 1;
        a.e_offset = e_offset;
        acts_.push_back(a);
        return (int32_t)acts_.size() - 1;
    }
    int32_t clone_tail(int32_t tail) {
        if (tail < 0) return -1;
        acts_.push_back(acts_[(size_t)tail]);
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
        return p.act_tail < 0 ? 0 : acts_[(size_t)p.act_tail].depth;
    }

    void advance(Path& p);
    bool branch_activate(Path& p, Path* child);
    bool branch_deactivate(Path& p, Path* child, bool* has_child);
    void create_deactivated_path(const Path& p, Path* child, bool is_sq_out);
    int32_t deactivation_type(const EdgeView& e, int64_t sp_end_time) const;

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

    const Enum& en_;
    bool has_sp_cap_;
    int32_t sp_cap_;
    int32_t depth_mode_;
    int32_t depth_value_;
    bool has_ms_filter_;
    double ms_filter_;
    bool flag_skipped_dynamics_;
    // Every activation must record skips == 0: the declining parent is dropped
    // whenever branch_activate produced a real child. BFS only.
    bool no_skips_;
    // Treat ms_filter_ as a requirement rather than a preference: a path over
    // the limit is dropped outright instead of surviving while nothing
    // outscores it. BFS only. See reduce_iteration_paths for why this is exact.
    bool hard_ms_filter_;

    std::vector<Act> acts_;
    std::vector<SqNode> sqs_;
    std::vector<Variant> variants_;

    std::vector<Path> cur_;
    std::vector<Path> next_;

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

    std::vector<OutPath> out_paths_;
    std::vector<OutAct> out_acts_;
    std::vector<OutSq> out_sqs_;
    std::vector<int32_t> chain_scratch_;
    std::vector<int32_t> sq_scratch_;

    std::vector<int32_t> base_nodes_;
    std::vector<int32_t> node_to_base_;
    std::unordered_map<DpNodeKey, std::vector<DpEntry>, DpNodeKeyHash> dp_memo_;
    std::unordered_map<uint64_t, std::vector<DpOutcome>> dp_act_memo_;
    std::vector<DpPlan> plans_;
    bool dp_points_mode_ = false;
    int32_t dp_depth_value_ = 0;
    int32_t dp_k_ = 1;
    double dp_dummy_ready_ms_ = 0.0;
    bool dp_failed_ = false;

    // pruning is off in every golden config, but kept for fidelity.
    bool enable_bound_prune_ = false;

    std::function<void(float)> progress_cb_;
    float progress_reported_ = -1.0f;
};

// --- advance -------------------------------------------------------------
void Engine::advance(Path& p) {
    if (p.node < 0) return;

    const NodeView n = node(p.node);
    if (n.adv_edge < 0) {
        p.node = -1;
        return;
    }

    const EdgeView e = edge(n.adv_edge);
    const ScoreGraphEdge* eo = eobj(n.adv_edge);

    p.sc[0] += e.basescore;
    p.sc[1] += e.comboscore;
    p.sc[2] += e.spscore;
    p.sc[3] += e.soloscore;
    p.sc[4] += e.accentscore;
    p.sc[5] += e.ghostscore;
    p.score += (int64_t)e.basescore + e.comboscore + e.spscore + e.soloscore +
               e.accentscore + e.ghostscore;
    p.notecount += e.notecount;

    const int32_t sp_n = (int32_t)eo->sp_times.size();
    int32_t buffered = p.buffered;

    if (n.is_sp) {
        if (sp_n > 0) {
            int64_t sp_end_time = p.sp_end_time;
            for (int32_t i = 0; i < sp_n; ++i) {
                if (buffered > 0) {
                    --buffered;
                    continue;
                }
                const std::map<int64_t, int64_t>& emap =
                    eo->sp_times[(size_t)i].second;
                auto mit = emap.find(sp_end_time);
                if (mit == emap.end()) {
                    p.node = NODE_BROKEN;
                    return;
                }
                sp_end_time = mit->second;
            }
            p.sp_end_time = sp_end_time;
            p.buffered = buffered;
        }
    } else {
        const int32_t old_sp = p.sp;
        int32_t sp = old_sp + sp_n - buffered;
        if (has_sp_cap_ && sp > sp_cap_) sp = sp_cap_;
        p.sp = sp;

        if (old_sp < 2 && sp >= 2) {
            const int32_t k = 1 - old_sp + buffered;
            if (k < 0 || k >= sp_n) {
                p.node = NODE_BROKEN;
                return;
            }
            p.sp_ready_ms = eo->sp_times[(size_t)k].first.ms();
        }
        p.buffered = 0;
    }

    p.node = e.dest;
}

// --- branch_activate -----------------------------------------------------
bool Engine::branch_activate(Path& p, Path* child) {
    const NodeView n = node(p.node);
    if (n.branch_edge < 0) return false;
    if (p.sp < 2) return false;

    const EdgeView e = edge(n.branch_edge);
    const ScoreGraphEdge* eo = eobj(n.branch_edge);

    const double e_offset = e.activation_fill_deadline_ms - p.sp_ready_ms;
    if (e_offset < -kCalibrationFillWindowMs) return false;

    // activation_initial_end_times, keyed by SP meter. The flat form was a list
    // with NO_TIME gaps in range [0, top]; here the map has meters 2..max.
    int64_t aiet_val = NO_TIME;
    bool in_range = false;
    if (!eo->activation_initial_end_times.empty()) {
        int top = eo->activation_initial_end_times.rbegin()->first;
        if (p.sp >= 0 && p.sp <= top) {
            in_range = true;
            auto ait = eo->activation_initial_end_times.find(p.sp);
            aiet_val = (ait == eo->activation_initial_end_times.end())
                           ? NO_TIME
                           : ait->second.ticks();
        }
    }
    if (!in_range || aiet_val == NO_TIME) {
        p.node = NODE_BROKEN;
        return false;
    }

    Path c = p;
    c.node = e.dest;
    c.currentskips = 0;
    c.sp = 0;

    close_last_activation(c);

    c.act_tail = new_act(p.act_tail, p.node, p.currentskips, p.sp,
                         has_value(p.skipped_e_offset) ? p.skipped_e_offset
                                                       : e_offset);
    c.sc[2] += e.frontend_points;
    c.score += e.frontend_points;
    c.skipped_e_offset = NO_DOUBLE;
    c.sp_ready_ms = NO_DOUBLE;
    c.sp_end_time = aiet_val;

    p.currentskips += 1;

    if (flag_skipped_dynamics_) {
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

    if (!has_value(p.skipped_e_offset)) p.skipped_e_offset = e_offset;

    *child = c;
    return true;
}

int32_t Engine::deactivation_type(const EdgeView& e, int64_t sp_end_time) const {
    if (e.sqinout_time != NO_TIME) {
        return sp_end_time == e.sqout_time ? DEACT_SQINOUT : DEACT_NONE;
    }
    return sp_end_time == node(e.dest).tick ? DEACT_NORMAL : DEACT_NONE;
}

// --- create_deactivated_path ---------------------------------------------
void Engine::create_deactivated_path(const Path& p, Path* child, bool is_sq_out) {
    const int32_t deact_edge = node(p.node).branch_edge;
    const EdgeView e = edge(deact_edge);
    const ScoreGraphEdge* eo = eobj(deact_edge);

    Path c = p;
    c.node = e.dest;
    c.sp = is_sq_out ? 1 : 0;
    c.sp_end_time = NO_TIME;

    c.act_tail = clone_tail(p.act_tail);
    if (c.act_tail >= 0) {
        Act& a = acts_[(size_t)c.act_tail];
        a.deact_edge = deact_edge;
        if (is_sq_out) {
            a.sq_tail = push_sq(a.sq_tail, SQ_OUT, e.sqinout_timing);
        }
    }

    int32_t sp_delta = 0;
    for (const BackendSqueeze& beo : eo->backends) {
        const int64_t be_tick = beo.timecode.ticks();
        const double be_offset = beo.offset_ms.value_or(0.0);
        const int32_t be_points = beo.points;
        const int32_t be_sqout_points = beo.sqout_points;

        const bool is_already_counted = be_offset <= 0;
        const bool is_leeway = be_offset > 0 && be_offset < 3;

        if (is_sq_out) {
            const bool is_before_sqout = be_tick < e.sqinout_time;
            const bool is_exact_sqout = be_tick == e.sqinout_time;
            const bool is_after_sqout = be_tick > e.sqinout_time;

            if (is_already_counted) {
                if (is_exact_sqout) {
                    sp_delta += -be_points + be_sqout_points;
                } else if (is_after_sqout) {
                    sp_delta += -be_points;
                }
            } else if (is_leeway) {
                if (is_before_sqout) {
                    sp_delta += be_points;
                } else if (is_exact_sqout) {
                    sp_delta += be_sqout_points;
                }
            }
        } else {
            if (is_leeway) sp_delta += be_points;
        }
    }

    if (sp_delta) {
        c.sc[2] += sp_delta;
        c.score += sp_delta;
    }

    *child = c;
}

// --- branch_deactivate ---------------------------------------------------
bool Engine::branch_deactivate(Path& p, Path* child, bool* has_child) {
    *has_child = false;

    const NodeView n = node(p.node);
    if (n.branch_edge < 0) return true;

    const EdgeView e = edge(n.branch_edge);
    const int32_t deact_type = deactivation_type(e, p.sp_end_time);

    if (deact_type == DEACT_NONE) return true;

    if (deact_type == DEACT_NORMAL) {
        create_deactivated_path(p, child, false);
        *has_child = true;
        return false;
    }

    create_deactivated_path(p, child, true);
    *has_child = true;

    p.act_tail = clone_tail(p.act_tail);
    if (p.act_tail >= 0) {
        Act& a = acts_[(size_t)p.act_tail];
        a.sq_tail = push_sq(a.sq_tail, SQ_IN, e.sqinout_timing);
    }

    p.sp_end_time = e.sqin_time;
    p.buffered = e.late_sqin_count;
    child->buffered = e.late_sqin_count;
    return true;
}

// --- difficulty ----------------------------------------------------------
double Engine::act_difficulty(int32_t act) const {
    if (act < 0) return NO_DOUBLE;
    const Act& a = acts_[(size_t)act];

    double best = NO_DOUBLE;
    for (int32_t s = a.sq_tail; s >= 0; s = sqs_[(size_t)s].prev) {
        const double d = sqs_[(size_t)s].kind == SQ_IN
                             ? sqs_[(size_t)s].offset
                             : -sqs_[(size_t)s].offset + 0.0;
        if (!has_value(best) || d > best) best = d;
    }
    if (a.e_offset < kCalibrationFillWindowMs && a.skips == 0) {
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
    return d <= ms_filter_;
}

// --- reduce_group --------------------------------------------------------
void Engine::reduce_group(const int32_t* members, int32_t n) {
    if (depth_mode_ == DEPTH_SCORES &&
        (int64_t)n <= (int64_t)depth_value_ + 1) {
        bool any_filtered = false;
        for (int32_t i = 0; i < n; ++i) {
            if (filtered_[(size_t)members[i]]) {
                any_filtered = true;
                break;
            }
        }
        if (!any_filtered) {
            distinct_map_.reset((size_t)n);
            bool all_distinct = true;
            for (int32_t i = 0; i < n; ++i) {
                bool inserted = false;
                distinct_map_.get_or_insert(
                    (uint64_t)cur_[(size_t)members[i]].score, i, &inserted);
                if (!inserted) {
                    all_distinct = false;
                    break;
                }
            }
            if (all_distinct) return;
        }
    }

    survivors_.clear();
    tie_map_.reset((size_t)n);
    for (int32_t i = 0; i < n; ++i) {
        const int32_t idx = members[i];
        const uint64_t key = ((uint64_t)cur_[(size_t)idx].score << 1) |
                             (filtered_[(size_t)idx] ? 1ull : 0ull);

        bool inserted = false;
        const int32_t leader_idx = tie_map_.get_or_insert(key, idx, &inserted);
        if (inserted) {
            survivors_.push_back(idx);
            continue;
        }

        Path& leader = cur_[(size_t)leader_idx];
        const Path& p = cur_[(size_t)idx];
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
        removed_[(size_t)idx] = 1;
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
        dominating_.push_back(cur_[(size_t)survivors_[i]].score);
    }
    std::sort(dominating_.begin(), dominating_.end());
    dominating_.erase(std::unique(dominating_.begin(), dominating_.end()),
                      dominating_.end());
    const int64_t best_all = dominating_.back();
    const int32_t n_dominating = (int32_t)dominating_.size();

    // The scores allowed to eliminate an achievable path. A filtered path
    // can't, unless it's optimal. May be empty mid-search (every live path in
    // this group is filtered): there is then no achievable path to prune, and
    // the band above still reins the filtered ones in.
    beating_.clear();
    for (size_t i = 0; i < survivors_.size(); ++i) {
        const int32_t idx = survivors_[i];
        const int64_t s = cur_[(size_t)idx].score;
        if (!filtered_[(size_t)idx] || (has_optimal_ && s == optimal_score_)) {
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
        const int64_t score = cur_[(size_t)idx].score;
        const int32_t outscored_by =
            n_beating - (int32_t)(std::upper_bound(beating_.begin(),
                                                   beating_.end(), score) -
                                  beating_.begin());

        if (filtered_[(size_t)idx]) {
            if (outscored_by) {
                removed_[(size_t)idx] = 1;
            } else if (depth_mode_ == DEPTH_POINTS) {
                if (score + depth_value_ < best_all) removed_[(size_t)idx] = 1;
            } else if (depth_mode_ == DEPTH_SCORES) {
                const int32_t outscored_by_all =
                    n_dominating - (int32_t)(std::upper_bound(
                                        dominating_.begin(), dominating_.end(),
                                        score) -
                                    dominating_.begin());
                if (outscored_by_all > depth_value_) removed_[(size_t)idx] = 1;
            }
        } else if (depth_mode_ == DEPTH_POINTS) {
            if (score + depth_value_ < best) removed_[(size_t)idx] = 1;
        } else if (depth_mode_ == DEPTH_SCORES) {
            if (outscored_by > depth_value_) removed_[(size_t)idx] = 1;
        }
    }
}

// --- prune_hopeless_paths ------------------------------------------------
void Engine::prune_hopeless_paths() {
    const int32_t n = (int32_t)cur_.size();

    guaranteed_.clear();
    for (int32_t i = 0; i < n; ++i) {
        const Path& p = cur_[(size_t)i];
        if (p.node < 0) {
            guaranteed_.push_back(p.score);
        } else if (!node(p.node).is_sp) {
            guaranteed_.push_back(p.score + node(p.node).base_suffix);
        }
    }
    if (guaranteed_.empty()) return;

    int64_t bar;
    if (depth_mode_ == DEPTH_SCORES) {
        std::sort(guaranteed_.begin(), guaranteed_.end(),
                  [](int64_t a, int64_t b) { return a > b; });
        guaranteed_.erase(std::unique(guaranteed_.begin(), guaranteed_.end()),
                          guaranteed_.end());
        const int64_t band = (int64_t)depth_value_ + 1;
        if ((int64_t)guaranteed_.size() < band) return;
        bar = guaranteed_[(size_t)(band - 1)];
    } else if (depth_mode_ == DEPTH_POINTS) {
        int64_t mx = guaranteed_[0];
        for (size_t i = 1; i < guaranteed_.size(); ++i) {
            if (guaranteed_[i] > mx) mx = guaranteed_[i];
        }
        bar = mx - depth_value_;
    } else {
        return;
    }

    const double bar_d = (double)bar;
    for (int32_t i = 0; i < n; ++i) {
        if (removed_[(size_t)i]) continue;
        const Path& p = cur_[(size_t)i];
        if (p.node < 0) continue;
        const NodeView nd = node(p.node);

        double ceiling;
        if (nd.is_sp) {
            ceiling = (double)p.score + (double)nd.max_suffix;
        } else {
            const int64_t total_sp = nd.total_spscore_suffix;
            const double sp_measures =
                2.0 * (double)(p.sp + nd.remaining_sp_phrases);
            double tight_sp = sp_measures * nd.max_spscore_density;
            if (tight_sp > (double)total_sp) tight_sp = (double)total_sp;
            ceiling = (double)p.score + (double)nd.max_suffix -
                      (double)total_sp + tight_sp;
        }

        if (ceiling < bar_d) removed_[(size_t)i] = 1;
    }
}

// --- reduce_iteration_paths ----------------------------------------------
void Engine::reduce_iteration_paths() {
    const int32_t n = (int32_t)cur_.size();

    filtered_.assign((size_t)n, 0);
    removed_.assign((size_t)n, 0);

    if (has_ms_filter_) {
        for (int32_t i = 0; i < n; ++i) {
            if (!passes_ms_filter(cur_[(size_t)i])) filtered_[(size_t)i] = 1;
        }
    }

    has_optimal_ = false;
    optimal_score_ = 0;

    owner_.assign((size_t)n, -1);
    group_map_.reset((size_t)n);
    int32_t n_groups = 0;

    for (int32_t i = 0; i < n; ++i) {
        const Path& p = cur_[(size_t)i];
        const bool is_complete = p.node < 0;

        // Hard mode kills an over-limit path here instead of handing it to
        // reduce_group, which keeps one while nothing outscores it -- a
        // preference, not a requirement. Dropping it now is exact:
        // search_difficulty is a running max (diff_prefix only ever rises in
        // close_last_activation, and a tail's squeeze list only grows), so a
        // path already over the limit can never come back under it. It also
        // prunes, since the whole subtree below it is over the limit too.
        if (hard_ms_filter_ && filtered_[(size_t)i]) {
            removed_[(size_t)i] = 1;
            continue;
        }

        if (is_complete && (!has_optimal_ || p.score > optimal_score_)) {
            has_optimal_ = true;
            optimal_score_ = p.score;
        }

        if (p.buffered != 0) continue;

        const bool is_sp = !is_complete && node(p.node).is_sp;
        const int64_t sp_value =
            is_complete ? 0 : (is_sp ? p.sp_end_time : (int64_t)p.sp);
        const uint64_t key = ((uint64_t)sp_value << 1) | (is_sp ? 1ull : 0ull);

        bool inserted = false;
        const int32_t g = group_map_.get_or_insert(key, n_groups, &inserted);
        if (inserted) ++n_groups;
        owner_[(size_t)i] = g;
    }

    counts_.assign((size_t)n_groups, 0);
    for (int32_t i = 0; i < n; ++i) {
        if (owner_[(size_t)i] >= 0) ++counts_[(size_t)owner_[(size_t)i]];
    }
    group_begin_.assign((size_t)n_groups, 0);
    group_end_.assign((size_t)n_groups, 0);
    int32_t running = 0;
    for (int32_t g = 0; g < n_groups; ++g) {
        group_begin_[(size_t)g] = running;
        group_end_[(size_t)g] = running;
        running += counts_[(size_t)g];
    }
    group_members_.assign((size_t)running, 0);
    for (int32_t i = 0; i < n; ++i) {
        const int32_t g = owner_[(size_t)i];
        if (g >= 0) group_members_[(size_t)group_end_[(size_t)g]++] = i;
    }

    for (int32_t g = 0; g < n_groups; ++g) {
        const int32_t begin = group_begin_[(size_t)g], end = group_end_[(size_t)g];
        if (end - begin > 1) {
            reduce_group(&group_members_[(size_t)begin], end - begin);
        }
    }

    if (enable_bound_prune_) {
        prune_hopeless_paths();
    }

    int32_t w = 0;
    for (int32_t i = 0; i < n; ++i) {
        if (!removed_[(size_t)i]) {
            if (w != i) cur_[(size_t)w] = cur_[(size_t)i];
            ++w;
        }
    }
    cur_.resize((size_t)w);
}

// --- output --------------------------------------------------------------
void Engine::emit_acts(int32_t act_tail, int32_t* begin, int32_t* end) {
    chain_scratch_.clear();
    for (int32_t a = act_tail; a >= 0; a = acts_[(size_t)a].parent) {
        chain_scratch_.push_back(a);
    }

    *begin = (int32_t)out_acts_.size();
    for (size_t i = chain_scratch_.size(); i-- > 0;) {
        const Act& a = acts_[(size_t)chain_scratch_[i]];

        sq_scratch_.clear();
        for (int32_t s = a.sq_tail; s >= 0; s = sqs_[(size_t)s].prev) {
            sq_scratch_.push_back(s);
        }

        OutAct oa;
        oa.act_node = a.act_node;
        oa.skips = a.skips;
        oa.sp_meter = a.sp_meter;
        oa.deact_edge = a.deact_edge;
        oa.e_offset = a.e_offset;
        oa.sq_begin = (int32_t)out_sqs_.size();
        for (size_t k = sq_scratch_.size(); k-- > 0;) {
            OutSq os;
            os.kind = sqs_[(size_t)sq_scratch_[k]].kind;
            os.offset = sqs_[(size_t)sq_scratch_[k]].offset;
            out_sqs_.push_back(os);
        }
        oa.sq_end = (int32_t)out_sqs_.size();
        out_acts_.push_back(oa);
    }
    *end = (int32_t)out_acts_.size();
}

void Engine::emit_variant(int32_t v, int32_t depth) {
    std::vector<int32_t> order;
    for (int32_t i = v; i >= 0; i = variants_[(size_t)i].prev)
        order.push_back(i);

    for (size_t k = order.size(); k-- > 0;) {
        const Variant& var = variants_[(size_t)order[k]];
        OutPath op;
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
    OutPath op;
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

// --- BFS driver ----------------------------------------------------------
bool Engine::run() {
    Path root;
    std::memset(&root, 0, sizeof(root));
    root.node = en_.start;
    root.act_tail = -1;
    root.var_head = -1;
    root.tied_count = 1;
    root.sp_end_time = NO_TIME;
    root.sp_ready_ms = NO_DOUBLE;
    root.skipped_e_offset = NO_DOUBLE;
    root.diff_prefix = NO_DOUBLE;

    cur_.clear();
    cur_.push_back(root);

    for (;;) {
        bool any_live = false;
        int32_t furthest = 0;
        for (size_t i = 0; i < cur_.size(); ++i) {
            if (cur_[i].node >= 0) {
                any_live = true;
                if (cur_[i].node > furthest) furthest = cur_[i].node;
            }
        }
        if (!any_live) break;

        // Report how far the frontier has advanced through the chart. Node
        // indices increase along the timeline, so the furthest live node over
        // the node count is a fair progress fraction; clamp it monotonic so a
        // finishing lead path can't make the bar step backward.
        if (progress_cb_ && !en_.nodes.empty()) {
            float f = static_cast<float>(furthest) / static_cast<float>(en_.nodes.size());
            if (f >= progress_reported_ + 0.005f) {
                progress_reported_ = f;
                progress_cb_(f);
            }
        }

        next_.clear();
        next_.reserve(cur_.size() * 2);

        for (size_t i = 0; i < cur_.size(); ++i) {
            Path p = cur_[i];
            advance(p);
            if (p.node == NODE_BROKEN) return false;

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
                if (p.node == NODE_BROKEN) return false;
                // The parent is the path that declined this opportunity, and
                // branch_activate has just charged it a skip. Under no_skips_
                // that parent can no longer reach an all-0 path, so drop it and
                // keep only the activating child. branch_activate charges the
                // skip solely on the branch that produced a child -- a refused
                // opportunity (no branch edge, SP under 2 bars, blown fill
                // deadline) leaves currentskips alone, so the surviving path is
                // still free to activate later and still read as 0 skips.
                if (!(no_skips_ && has_child)) next_.push_back(p);
            }
            if (has_child) next_.push_back(child);
        }

        cur_.swap(next_);
        reduce_iteration_paths();

        if (cur_.empty()) return false;
    }

    std::vector<int32_t> order(cur_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int32_t)i;
    std::stable_sort(order.begin(), order.end(), [this](int32_t a, int32_t b) {
        return cur_[(size_t)a].score > cur_[(size_t)b].score;
    });

    for (size_t i = 0; i < order.size(); ++i)
        emit_path(cur_[(size_t)order[i]]);
    return true;
}

// --- DP methods ----------------------------------------------------------
void Engine::dp_build_index() {
    node_to_base_.assign(en_.nodes.size(), -1);
    base_nodes_.clear();
    for (int32_t nd = en_.start; nd >= 0;) {
        node_to_base_[(size_t)nd] = (int32_t)base_nodes_.size();
        base_nodes_.push_back(nd);
        const NodeView n = node(nd);
        nd = n.adv_edge >= 0 ? edge(n.adv_edge).dest : -1;
    }
}

Path Engine::dp_seed(int32_t nd, int32_t sp, int32_t buffered, double ready_ms) {
    Path p;
    std::memset(&p, 0, sizeof(p));
    p.node = nd;
    p.sp = sp;
    p.buffered = buffered;
    p.act_tail = -1;
    p.var_head = -1;
    p.tied_count = 1;
    p.sp_end_time = NO_TIME;
    p.sp_ready_ms = ready_ms;
    p.skipped_e_offset = NO_DOUBLE;
    p.diff_prefix = NO_DOUBLE;
    return p;
}

DpWalk Engine::dp_walk_to_next_candidate(int32_t node_index, int32_t sp,
                                         double sp_ready_ms, int32_t buffered) {
    Path seg = dp_seed(base_nodes_[(size_t)node_index], sp, buffered, sp_ready_ms);
    for (;;) {
        advance(seg);
        if (seg.node == NODE_BROKEN) {
            dp_failed_ = true;
            return DpWalk{0, -1, sp, buffered, sp_ready_ms};
        }
        if (seg.node < 0 || node(seg.node).is_sp) {
            return DpWalk{seg.score, -1, seg.sp, seg.buffered, seg.sp_ready_ms};
        }
        if (node(seg.node).branch_edge >= 0) {
            return DpWalk{seg.score, node_to_base_[(size_t)seg.node], seg.sp,
                          seg.buffered, seg.sp_ready_ms};
        }
    }
}

std::vector<DpEntry> Engine::dp_best_from_node(int32_t node_index, int32_t sp,
                                               double sp_ready_ms,
                                               int32_t buffered) {
    const DpNodeKey key{node_index, sp, buffered, dp_ready_bits(sp_ready_ms)};
    auto it = dp_memo_.find(key);
    if (it != dp_memo_.end()) return it->second;

    std::vector<DpEntry> options;
    const NodeView n = node(base_nodes_[(size_t)node_index]);

    if (n.branch_edge >= 0 && sp >= 2 && has_value(sp_ready_ms)) {
        const EdgeView be = edge(n.branch_edge);
        if (be.activation_fill_deadline_ms - sp_ready_ms >=
            -kCalibrationFillWindowMs) {
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
                        node_to_base_[(size_t)oc.landing], oc.residual,
                        NO_DOUBLE, oc.buffered);
                    if (dp_failed_) return {};
                    for (const DpEntry& fe : futs) {
                        const int32_t plan =
                            dp_make_act(node_index, oc.idx, fe.plan);
                        options.push_back(DpEntry{oc.delta + fe.score, plan});
                    }
                }
            }
        }
    }

    DpWalk w = dp_walk_to_next_candidate(node_index, sp, sp_ready_ms, buffered);
    if (dp_failed_) return {};
    if (w.next_index < 0) {
        options.push_back(DpEntry{w.base_score, PLAN_STOP});
    } else {
        std::vector<DpEntry> futs =
            dp_best_from_node(w.next_index, w.sp, w.sp_ready_ms, w.buffered);
        if (dp_failed_) return {};
        for (const DpEntry& fe : futs) {
            options.push_back(DpEntry{w.base_score + fe.score, fe.plan});
        }
    }

    std::vector<DpEntry> result = dp_merge_topk(options);
    dp_memo_.emplace(key, result);
    return result;
}

const std::vector<DpOutcome>& Engine::dp_activation_outcomes(int32_t cand_index,
                                                             int32_t sp) {
    const uint64_t key = dp_act_key(cand_index, sp);
    auto it = dp_act_memo_.find(key);
    if (it != dp_act_memo_.end()) return it->second;

    Path seed = dp_seed(base_nodes_[(size_t)cand_index], sp, 0, dp_dummy_ready_ms_);
    Path activated;
    std::vector<DpOutcome> outcomes;
    const bool ok = branch_activate(seed, &activated);
    if (seed.node == NODE_BROKEN) {
        dp_failed_ = true;
    } else if (ok) {
        dp_simulate_sp(activated, outcomes);
    }

    auto res = dp_act_memo_.emplace(key, std::move(outcomes));
    return res.first->second;
}

void Engine::dp_simulate_sp(Path p, std::vector<DpOutcome>& outcomes) {
    int32_t idx = 0;
    while (p.node >= 0) {
        advance(p);
        if (p.node == NODE_BROKEN) {
            dp_failed_ = true;
            return;
        }
        if (p.node < 0) {
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

std::vector<DpEntry> Engine::dp_merge_topk(std::vector<DpEntry>& options) {
    if (options.empty()) return {};

    std::unordered_map<int64_t, int32_t> best_plan;
    best_plan.reserve(options.size() * 2);
    for (const DpEntry& e : options) best_plan.emplace(e.score, e.plan);

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
        scores.resize((size_t)dp_k_);
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

Path Engine::dp_replay(int32_t plan) {
    Path p = dp_seed(en_.start, 0, 0, NO_DOUBLE);

    for (;;) {
        const DpPlan pl = plans_[(size_t)plan];
        if (pl.kind == PLAN_KIND_DONE) break;
        if (pl.kind == PLAN_KIND_STOP) {
            while (p.node >= 0) {
                advance(p);
                if (p.node == NODE_BROKEN) {
                    dp_failed_ = true;
                    return p;
                }
                if (p.node >= 0 && !node(p.node).is_sp &&
                    node(p.node).branch_edge >= 0) {
                    Path child;
                    branch_activate(p, &child);
                    if (p.node == NODE_BROKEN) {
                        dp_failed_ = true;
                        return p;
                    }
                }
            }
            break;
        }

        const int32_t cand_index = pl.cand_index;
        const int32_t outcome_idx = pl.outcome_idx;
        const int32_t child_plan = pl.child;

        while (!(p.node >= 0 && node_to_base_[(size_t)p.node] == cand_index)) {
            advance(p);
            if (p.node == NODE_BROKEN) {
                dp_failed_ = true;
                return p;
            }
            if (p.node < 0) break;
            if (!node(p.node).is_sp && node(p.node).branch_edge >= 0 &&
                node_to_base_[(size_t)p.node] != cand_index) {
                Path child;
                branch_activate(p, &child);
                if (p.node == NODE_BROKEN) {
                    dp_failed_ = true;
                    return p;
                }
            }
        }
        if (p.node < 0) {
            dp_failed_ = true;
            return p;
        }

        Path activated;
        const bool ok = branch_activate(p, &activated);
        if (p.node == NODE_BROKEN || !ok) {
            dp_failed_ = true;
            return p;
        }

        Path q = activated;
        int32_t idx = 0;
        int32_t landing_kind = -1;
        Path landing_path{};
        while (q.node >= 0) {
            advance(q);
            if (q.node == NODE_BROKEN) {
                dp_failed_ = true;
                return q;
            }
            if (q.node < 0) {
                landing_kind = 0;
                landing_path = q;
                break;
            }
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

bool Engine::dp_run() {
    dp_build_index();

    dp_points_mode_ = depth_mode_ == DEPTH_POINTS;
    dp_depth_value_ = depth_value_;
    dp_k_ = depth_value_ + 1;
    dp_dummy_ready_ms_ = -1e18;
    dp_failed_ = false;

    plans_.clear();
    plans_.push_back(DpPlan{PLAN_KIND_STOP, -1, -1, -1});
    plans_.push_back(DpPlan{PLAN_KIND_DONE, -1, -1, -1});

    std::vector<DpEntry> entries = dp_best_from_node(0, 0, NO_DOUBLE, 0);
    if (dp_failed_) return false;

    std::vector<Path> finals;
    finals.reserve(entries.size());
    for (const DpEntry& e : entries) {
        Path fp = dp_replay(e.plan);
        if (dp_failed_) return false;
        finals.push_back(fp);
    }

    std::stable_sort(finals.begin(), finals.end(),
                     [](const Path& a, const Path& b) {
                         return a.score > b.score;
                     });
    for (const Path& fp : finals) emit_path(fp);
    return true;
}

// ---- rebuild the decision log into hydata Paths -------------------------
// Mirrors hynative._rebuild + _graph_multsqueezes, reading the graph objects
// straight off the enumeration.

std::vector<MultSqueeze> collect_multsqueezes(const ScoreGraph& graph) {
    std::vector<MultSqueeze> found;
    for (const ScoreGraphNode* n = graph.start(); n != nullptr && n->adv_edge;
         n = n->adv_edge->dest)
        for (const MultSqueeze& m : n->adv_edge->multsqueezes)
            found.push_back(m);
    return found;
}

// One rebuilt path plus its variant children, referenced by index so the pool
// can reallocate freely.
struct BuildNode {
    MPath path;
    std::vector<int> children;
};

std::vector<MPath> rebuild(const Enum& en, const std::vector<OutPath>& out_paths,
                           const std::vector<OutAct>& out_acts,
                           const std::vector<OutSq>& out_sqs,
                           const std::vector<MultSqueeze>& multsqueezes) {
    std::vector<BuildNode> pool;
    pool.reserve(out_paths.size());
    std::vector<int> top_level;
    std::vector<int> by_depth;

    for (const OutPath& op : out_paths) {
        MPath path;
        path.score_base = op.score_base;
        path.score_combo = op.score_combo;
        path.score_sp = op.score_sp;
        path.score_solo = op.score_solo;
        path.score_accents = op.score_accents;
        path.score_ghosts = op.score_ghosts;
        path.notecount = op.notecount;
        path.leftover_sp = op.leftover_sp;
        path.skipped_accents = op.skipped_accents;
        path.skipped_ghosts = op.skipped_ghosts;
        path.multsqueezes = multsqueezes;

        for (int j = op.act_begin; j < op.act_end; ++j) {
            const OutAct& oa = out_acts[(size_t)j];
            const ScoreGraphNode* node = en.nodes[(size_t)oa.act_node];

            Activation act;
            act.skips = oa.skips;
            act.timecode = node->timecode;
            act.chord = node->chord;
            act.sp_meter = oa.sp_meter;
            act.frontend_points = node->branch_edge->frontend->points;
            act.e_offset = oa.e_offset;
            if (oa.deact_edge >= 0)
                act.backends = en.edges[(size_t)oa.deact_edge]->backends;
            for (int k = oa.sq_begin; k < oa.sq_end; ++k) {
                const OutSq& os = out_sqs[(size_t)k];
                SPSqueeze sq;
                sq.kind = (os.kind == SQ_IN) ? SqueezeKind::SqIn
                                             : SqueezeKind::SqOut;
                sq.offset_ms = os.offset;
                act.sqinouts.push_back(sq);
            }
            path.activations.push_back(std::move(act));
        }

        int depth = op.depth;
        int idx = static_cast<int>(pool.size());
        BuildNode bn;
        bn.path = std::move(path);
        pool.push_back(std::move(bn));

        if (depth == 0) {
            top_level.push_back(idx);
        } else {
            pool[(size_t)idx].path.var_point = op.var_point;
            pool[(size_t)by_depth[(size_t)(depth - 1)]].children.push_back(idx);
        }

        if (static_cast<int>(by_depth.size()) == depth)
            by_depth.push_back(idx);
        else
            by_depth[(size_t)depth] = idx;
    }

    std::function<MPath(int)> assemble = [&](int i) -> MPath {
        MPath p = std::move(pool[(size_t)i].path);
        for (int c : pool[(size_t)i].children)
            p.variants.push_back(assemble(c));
        return p;
    };

    std::vector<MPath> result;
    result.reserve(top_level.size());
    for (int t : top_level) {
        MPath p = assemble(t);
        p.recount_tied_paths();
        p.prepare_variants();
        result.push_back(std::move(p));
    }
    return result;
}

}  // namespace

std::vector<MPath> run_search(const ScoreGraph& graph, int depth_mode,
                              int depth_value, std::optional<double> ms_filter,
                              bool use_dp, bool no_skips, bool hard_ms_filter,
                              const std::function<void(float)>& on_progress) {
    Enum en = enumerate(graph);

    const bool has_cap = graph.sp_meter_cap().has_value();
    const int32_t cap = static_cast<int32_t>(graph.sp_meter_cap().value_or(0));

    // The DP enumerates activation sets directly and never walks the BFS
    // decline branch or reduce_iteration_paths, so it has nowhere to apply
    // either constraint.
    if ((no_skips || hard_ms_filter) && use_dp)
        throw std::runtime_error("no_skips/hard_ms_filter are BFS-only constraints");

    Engine engine(en, has_cap, cap, depth_mode, depth_value,
                  ms_filter.has_value(), ms_filter.value_or(0.0),
                  /*flag_skipped_dynamics=*/false, no_skips, hard_ms_filter);
    if (on_progress) engine.set_progress_cb(on_progress);

    bool ok;
    if (use_dp) {
#ifdef _WIN32
        // The DP recurses candidate-deep; run it on a large reserved stack.
        struct Ctx {
            Engine* engine;
            bool ok;
        } ctx{&engine, false};
        HANDLE th = CreateThread(
            nullptr, (SIZE_T)512 * 1024 * 1024,
            [](LPVOID param) -> DWORD {
                Ctx* c = static_cast<Ctx*>(param);
                c->ok = c->engine->dp_run();
                return 0;
            },
            &ctx, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (th == nullptr) {
            ok = engine.dp_run();
        } else {
            WaitForSingleObject(th, INFINITE);
            CloseHandle(th);
            ok = ctx.ok;
        }
#else
        ok = engine.dp_run();
#endif
    } else {
        ok = engine.run();
    }

    if (!ok)
        throw std::runtime_error("native search reached a broken state");

    return rebuild(en, engine.out_paths(), engine.out_acts(), engine.out_sqs(),
                   collect_multsqueezes(graph));
}

}  // namespace hydra
