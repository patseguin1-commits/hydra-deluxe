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
    int32_t adv_edge, branch_edge, is_sp;
};
struct EdgeView {
    int32_t dest;
    int32_t notecount, basescore, comboscore, spscore, soloscore, accentscore,
        ghostscore;
    int32_t frontend_points;
    int32_t late_sqin_count;
    double activation_fill_deadline_ms, sqinout_timing;
    int64_t sqinout_time, sqout_time, sqin_time;
};

// ---- engine data structures (verbatim from native/hydra_search.cpp) ------

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

// ---- the engine ----------------------------------------------------------

class Engine {
public:
    Engine(const Enum& en, bool has_sp_cap, int32_t sp_cap, int32_t depth_mode,
           int32_t depth_value, bool has_ms_filter, double ms_filter,
           bool no_skips, bool hard_ms_filter)
        : en_(en),
          has_sp_cap_(has_sp_cap),
          sp_cap_(sp_cap),
          depth_mode_(depth_mode),
          depth_value_(depth_value),
          has_ms_filter_(has_ms_filter),
          ms_filter_(ms_filter),
          no_skips_(no_skips),
          hard_ms_filter_(hard_ms_filter) {}

    bool run();

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
        v.adv_edge = en_.edge_of(o->adv_edge);
        v.branch_edge = en_.edge_of(o->branch_edge);
        v.is_sp = o->is_sp ? 1 : 0;
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
        v.frontend_points = o->frontend.has_value() ? o->frontend->points : 0;
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

    void emit_path(const Path& p);
    void emit_variant(int32_t v, int32_t depth);
    void emit_acts(int32_t act_tail, int32_t* begin, int32_t* end);

    const Enum& en_;
    bool has_sp_cap_;
    int32_t sp_cap_;
    int32_t depth_mode_;
    int32_t depth_value_;
    bool has_ms_filter_;
    double ms_filter_;
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
                              bool no_skips, bool hard_ms_filter,
                              const std::function<void(float)>& on_progress) {
    Enum en = enumerate(graph);

    const bool has_cap = graph.sp_meter_cap().has_value();
    const int32_t cap = static_cast<int32_t>(graph.sp_meter_cap().value_or(0));

    Engine engine(en, has_cap, cap, depth_mode, depth_value,
                  ms_filter.has_value(), ms_filter.value_or(0.0),
                  no_skips, hard_ms_filter);
    if (on_progress) engine.set_progress_cb(on_progress);

    if (!engine.run())
        throw std::runtime_error("search reached a broken state");

    return rebuild(en, engine.out_paths(), engine.out_acts(), engine.out_sqs(),
                   collect_multsqueezes(graph));
}

}  // namespace hydra
