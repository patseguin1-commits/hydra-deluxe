#include "search/graph.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace hydra {

// How far apart (ms) a note and a deactivation can be and still be a SqIn/SqOut,
// and how far back edge.backends must stay complete. Mirrors
// hypath.SQUEEZE_WINDOW_MS.
namespace {
constexpr double SQUEEZE_WINDOW_MS = kSqueezeWindowMs;  // exported in graph.h

// Min-heap comparator on ticks (std::*_heap build a max-heap by default, so
// `greater` yields a min-heap whose front() is the earliest tick).
struct TickGreater {
    bool operator()(const Timecode& a, const Timecode& b) const {
        return a.ticks() > b.ticks();
    }
};
}  // namespace

hy_scores category_scores(const Chord& chord, int combo) {
    std::vector<ChordNote> ordering = chord.notes(true);
    int n = static_cast<int>(ordering.size());
    std::vector<uint8_t> flags(n > 0 ? static_cast<size_t>(n) : 1u, 0);
    for (int i = 0; i < n; ++i) {
        uint8_t f = 0;
        if (ordering[static_cast<size_t>(i)].is_cymbal()) f |= HY_NOTE_CYMBAL;
        if (ordering[static_cast<size_t>(i)].is_accent()) f |= HY_NOTE_ACCENT;
        if (ordering[static_cast<size_t>(i)].is_ghost()) f |= HY_NOTE_GHOST;
        // hymisc.FLAG_SKIPPED_DYNAMICS is off for every golden config, so the
        // activation flag is never set.
        flags[static_cast<size_t>(i)] = f;
    }
    hy_scores out{};
    hy_category_scores(flags.data(), n, combo, 0, &out);
    return out;
}

ScoreGraphNode* ScoreGraph::new_node(const Timecode& tc, bool is_sp) {
    node_pool_.emplace_back();
    ScoreGraphNode* n = &node_pool_.back();
    n->timecode = tc;
    n->is_sp = is_sp;
    return n;
}

ScoreGraphEdge* ScoreGraph::new_edge() {
    edge_pool_.emplace_back();
    return &edge_pool_.back();
}

Timecode ScoreGraph::plusmeasure(const Timecode& tc, int64_t add_measures) {
    auto key = std::make_pair(tc.ticks(), add_measures);
    auto it = plusmeasure_cache_.find(key);
    if (it != plusmeasure_cache_.end()) return it->second;
    Timecode r = song_.timing().plusmeasure(tc, add_measures);
    plusmeasure_cache_.emplace(key, r);
    return r;
}

ScoreGraph::ScoreGraph(const Song& song, std::optional<int> sp_meter_cap)
    : song_(song), sp_meter_cap_(sp_meter_cap) {
    start_ = new_node(song_.start_time(), false);
    base_track_head_ = start_;
    sp_track_head_ = new_node(song_.start_time(), true);
    sp_start_ = sp_track_head_;
    proto_base_edge_ = new_edge();
    proto_sp_edge_ = new_edge();

    build();
    compute_bounds();
}

void ScoreGraph::build() {
    TickGreater cmp;

    for (const SongTimestamp& timestamp : song_.sequence) {
        // SP can fall off between timestamps: handle deacts due before this one.
        while (!deact_heap_.empty() &&
               deact_heap_.front().ticks() < timestamp.timecode.ticks()) {
            std::pop_heap(deact_heap_.begin(), deact_heap_.end(), cmp);
            Timecode pending_deact = deact_heap_.back();
            deact_heap_.pop_back();
            if (pending_deacts_.find(pending_deact.ticks()) ==
                pending_deacts_.end())
                continue;  // stale entry, already handled or extended past
            set_head_time(pending_deact);
            handle_deact(pending_deact, std::nullopt);
        }

        set_head_time(timestamp.timecode);

        store_notecount(timestamp.chord.count());
        if (timestamp.flag_solo)
            store_soloscore(100 * timestamp.chord.count());

        hy_scores sg = category_scores(timestamp.chord, combo_);

        try {
            MultSqueeze msq(timestamp.chord, combo_);
            store_multsqueeze(msq);
        } catch (const std::invalid_argument&) {
        }

        store_basescore(sg.base);
        store_comboscore(sg.combo);
        store_spscore(sg.sp);
        store_accentscore(sg.accent);
        store_ghostscore(sg.ghost);

        combo_ += timestamp.chord.count();

        store_new_backend(timestamp, sg.sp, sg.sp - sg.sqout_reduction);

        if (timestamp.flag_sp) {
            sp_phrase_count_ += 1;

            // Deacts within the squeeze window keep a non-extended copy (SqOut).
            std::vector<Timecode> sqout_deacts;
            for (const auto& kv : pending_deacts_)
                if (kv.second.ms() - timestamp.timecode.ms() < SQUEEZE_WINDOW_MS)
                    sqout_deacts.push_back(kv.second);

            // Timecodes this SP phrase can extend: pending deacts plus very
            // recently handled deacts (SqIn). Deduped by ticks via std::map.
            std::map<int64_t, Timecode> extendable;
            for (const auto& kv : pending_deacts_)
                extendable.emplace(kv.first, kv.second);
            for (ScoreGraphEdge* e : recent_deact_edges_)
                extendable.emplace(e->dest->timecode.ticks(), e->dest->timecode);

            std::vector<Timecode> extendable_list;
            extendable_list.reserve(extendable.size());
            for (const auto& kv : extendable) extendable_list.push_back(kv.second);

            std::vector<std::pair<Timecode, Timecode>> ext =
                extend_deacts(extendable_list, timestamp.timecode);

            std::map<int64_t, int64_t> ext_map;
            std::unordered_map<int64_t, Timecode> new_pending;
            for (const auto& pr : ext) {
                ext_map[pr.first.ticks()] = pr.second.ticks();
                new_pending[pr.second.ticks()] = pr.second;
            }
            for (const Timecode& t : sqout_deacts)
                new_pending[t.ticks()] = t;

            pending_deacts_ = std::move(new_pending);
            deact_heap_.clear();
            for (const auto& kv : pending_deacts_)
                deact_heap_.push_back(kv.second);
            std::make_heap(deact_heap_.begin(), deact_heap_.end(), cmp);

            proto_base_edge_->sp_times.push_back({timestamp.timecode, ext_map});
            proto_sp_edge_->sp_times.push_back({timestamp.timecode, ext_map});
        }

        if (timestamp.has_activation()) {
            advance_tracks(timestamp.timecode, timestamp.chord);
            ScoreGraphEdge* act_edge = add_act_edge(
                timestamp.chord, sg.sp, sg.skipped_dynamic_reduction,
                *timestamp.activation_length);

            for (const auto& kv : act_edge->activation_initial_end_times) {
                const Timecode& end_time = kv.second;
                if (pending_deacts_.find(end_time.ticks()) ==
                    pending_deacts_.end()) {
                    pending_deacts_[end_time.ticks()] = end_time;
                    deact_heap_.push_back(end_time);
                    std::push_heap(deact_heap_.begin(), deact_heap_.end(), cmp);
                }
            }
        }

        if (pending_deacts_.find(timestamp.timecode.ticks()) !=
            pending_deacts_.end())
            handle_deact(timestamp.timecode, timestamp.chord);
    }

    const SongTimestamp& last = song_.sequence.back();
    advance_tracks(last.timecode, last.chord);
}

void ScoreGraph::store_notecount(int64_t count) {
    proto_base_edge_->notecount += count;
    proto_sp_edge_->notecount += count;
}
void ScoreGraph::store_soloscore(int64_t points) {
    proto_base_edge_->soloscore += points;
    proto_sp_edge_->soloscore += points;
}
void ScoreGraph::store_basescore(int64_t points) {
    proto_base_edge_->basescore += points;
    proto_sp_edge_->basescore += points;
}
void ScoreGraph::store_comboscore(int64_t points) {
    proto_base_edge_->comboscore += points;
    proto_sp_edge_->comboscore += points;
}
void ScoreGraph::store_spscore(int64_t points) {
    proto_sp_edge_->spscore += points;
}
void ScoreGraph::store_accentscore(int64_t points) {
    proto_base_edge_->accentscore += points;
    proto_sp_edge_->accentscore += points;
}
void ScoreGraph::store_ghostscore(int64_t points) {
    proto_base_edge_->ghostscore += points;
    proto_sp_edge_->ghostscore += points;
}
void ScoreGraph::store_multsqueeze(const MultSqueeze& msq) {
    proto_base_edge_->multsqueezes.push_back(msq);
    proto_sp_edge_->multsqueezes.push_back(msq);
}

void ScoreGraph::store_new_backend(const SongTimestamp& ts, int sp_points,
                                   int sqout_points) {
    BackendSqueeze backend;
    backend.timecode = ts.timecode;
    backend.chord = ts.chord;
    backend.points = sp_points;
    backend.sqout_points = sqout_points;
    backend.is_sp = ts.flag_sp;

    recent_backends_.push_back(backend);

    for (ScoreGraphEdge* recent_edge : recent_deact_edges_) {
        double offset_ms =
            ts.timecode.ms() - recent_edge->dest->timecode.ms();
        BackendSqueeze copy = backend;
        copy.offset_ms = offset_ms;
        recent_edge->backends.push_back(copy);

        if (recent_edge->backends.back().is_sp &&
            !recent_edge->sqinout_time.has_value()) {
            recent_edge->sqinout_time = ts.timecode;
            recent_edge->sqinout_timing = offset_ms;
            recent_edge->late_sqin_count += 1;
            recent_edge->sqin_time = plusmeasure(*recent_edge->sqin_time, 2);
        }
    }
}

int ScoreGraph::max_sp_bars() const {
    if (!sp_meter_cap_.has_value()) return sp_phrase_count_;
    return std::min(*sp_meter_cap_, sp_phrase_count_);
}

std::vector<std::pair<Timecode, Timecode>> ScoreGraph::extend_deacts(
    const std::vector<Timecode>& deact_tcs, const Timecode& sp_timecode) {
    std::vector<std::pair<Timecode, Timecode>> out;
    out.reserve(deact_tcs.size());

    if (!sp_meter_cap_.has_value()) {
        for (const Timecode& tc : deact_tcs)
            out.push_back({tc, plusmeasure(tc, 2)});
        return out;
    }

    Timecode ceiling = plusmeasure(sp_timecode, 2 * (*sp_meter_cap_));
    for (const Timecode& tc : deact_tcs) {
        Timecode ext = plusmeasure(tc, 2);
        // min(ext, ceiling): ceiling only when it is strictly earlier.
        const Timecode& chosen =
            (ceiling.ticks() < ext.ticks()) ? ceiling : ext;
        out.push_back({tc, chosen});
    }
    return out;
}

bool ScoreGraph::is_recent_to_head(const Timecode& tc) const {
    return head_time_offset(tc) < SQUEEZE_WINDOW_MS;
}

void ScoreGraph::set_head_time(const Timecode& tc) {
    head_time_ = tc;
    head_time_set_ = true;

    std::vector<ScoreGraphEdge*> keep_edges;
    for (ScoreGraphEdge* edge : recent_deact_edges_)
        if (is_recent_to_head(edge->dest->timecode))
            keep_edges.push_back(edge);
    recent_deact_edges_ = std::move(keep_edges);

    std::vector<BackendSqueeze> keep_be;
    for (const BackendSqueeze& be : recent_backends_)
        if (is_recent_to_head(be.timecode)) keep_be.push_back(be);
    recent_backends_ = std::move(keep_be);
}

void ScoreGraph::handle_deact(const Timecode& deact_tc,
                              const std::optional<Chord>& chord) {
    if (pending_deacts_.find(deact_tc.ticks()) == pending_deacts_.end())
        return;
    advance_tracks(deact_tc, chord);
    add_deact_edge();
    pending_deacts_.erase(deact_tc.ticks());
}

void ScoreGraph::advance_tracks(const Timecode& tc,
                                const std::optional<Chord>& chord) {
    if (base_track_head_->timecode.ticks() >= tc.ticks()) return;

    length_ += 1;

    proto_base_edge_->dest = new_node(tc, false);
    proto_sp_edge_->dest = new_node(tc, true);
    proto_base_edge_->dest->chord = chord;
    proto_sp_edge_->dest->chord = chord;

    base_track_head_->adv_edge = proto_base_edge_;
    sp_track_head_->adv_edge = proto_sp_edge_;

    proto_base_edge_ = new_edge();
    proto_sp_edge_ = new_edge();

    base_track_head_ = base_track_head_->adv_edge->dest;
    sp_track_head_ = sp_track_head_->adv_edge->dest;
}

ScoreGraphEdge* ScoreGraph::add_act_edge(const Chord& frontend_chord,
                                         int frontend_points,
                                         int skipped_dynamic_reduction,
                                         int64_t fill_length_ticks) {
    ScoreGraphEdge* act_edge = new_edge();
    act_edge->dest = sp_track_head_;

    act_edge->frontend = FrontendSqueeze{frontend_chord, frontend_points};

    // E threshold is 4 beats before the fill marker.
    int64_t tick_E = act_edge->dest->timecode.ticks() - fill_length_ticks -
                     4 * song_.tick_resolution();
    Timecode tc_E = song_.timing().timecode(tick_E);
    act_edge->activation_fill_deadline_ms = tc_E.ms();

    for (int sp = 2; sp <= max_sp_bars(); ++sp)
        act_edge->activation_initial_end_times[sp] =
            plusmeasure(act_edge->dest->timecode, 2 * sp);

    act_edge->skipped_dynamic_points = skipped_dynamic_reduction;

    base_track_head_->branch_edge = act_edge;
    return act_edge;
}

void ScoreGraph::add_deact_edge() {
    ScoreGraphEdge* deact_edge = new_edge();
    deact_edge->dest = base_track_head_;

    deact_edge->sqout_time = deact_edge->dest->timecode;
    deact_edge->sqin_time = deact_edge->dest->timecode;

    for (const BackendSqueeze& recent_backend : recent_backends_) {
        BackendSqueeze copy = recent_backend;
        double offset_ms =
            recent_backend.timecode.ms() - deact_edge->dest->timecode.ms();
        copy.offset_ms = offset_ms;
        deact_edge->backends.push_back(copy);

        if (recent_backend.is_sp && !deact_edge->sqinout_time.has_value()) {
            deact_edge->sqinout_time = recent_backend.timecode;
            deact_edge->sqinout_timing = offset_ms;
            deact_edge->sqout_time = plusmeasure(*deact_edge->sqout_time, 2);
            deact_edge->sqin_time = plusmeasure(*deact_edge->sqin_time, 2);
        }
    }

    recent_deact_edges_.push_back(deact_edge);
    sp_track_head_->branch_edge = deact_edge;
}

void ScoreGraph::compute_bounds() {
    std::vector<ScoreGraphNode*> base_nodes, sp_nodes;
    for (ScoreGraphNode* n = start_; n != nullptr;
         n = (n->adv_edge ? n->adv_edge->dest : nullptr))
        base_nodes.push_back(n);
    for (ScoreGraphNode* n = sp_start_; n != nullptr;
         n = (n->adv_edge ? n->adv_edge->dest : nullptr))
        sp_nodes.push_back(n);

    int64_t base_running = 0, max_running = 0, sp_running = 0,
            phrase_running = 0;
    double density_running = 0.0;

    for (int i = static_cast<int>(base_nodes.size()) - 1; i >= 0; --i) {
        ScoreGraphNode* b_node = base_nodes[static_cast<size_t>(i)];
        ScoreGraphNode* s_node =
            (i < static_cast<int>(sp_nodes.size()))
                ? sp_nodes[static_cast<size_t>(i)]
                : nullptr;

        b_node->base_suffix = base_running;
        b_node->max_suffix = max_running;
        b_node->total_spscore_suffix = sp_running;
        b_node->remaining_sp_phrases = phrase_running;
        b_node->max_spscore_density = density_running;
        if (s_node) {
            s_node->base_suffix = base_running;
            s_node->max_suffix = max_running;
            s_node->total_spscore_suffix = sp_running;
            s_node->remaining_sp_phrases = phrase_running;
            s_node->max_spscore_density = density_running;
        }

        ScoreGraphEdge* b_edge =
            (i > 0) ? base_nodes[static_cast<size_t>(i - 1)]->adv_edge : nullptr;
        ScoreGraphEdge* s_edge =
            (i > 0 && (i - 1) < static_cast<int>(sp_nodes.size()))
                ? sp_nodes[static_cast<size_t>(i - 1)]->adv_edge
                : nullptr;

        if (b_edge) {
            base_running += b_edge->basescore + b_edge->comboscore +
                            b_edge->spscore + b_edge->soloscore +
                            b_edge->accentscore + b_edge->ghostscore;
        }
        if (s_edge) {
            max_running += s_edge->basescore + s_edge->comboscore +
                           s_edge->spscore + s_edge->soloscore +
                           s_edge->accentscore + s_edge->ghostscore;
            sp_running += s_edge->spscore;
            phrase_running += static_cast<int64_t>(s_edge->sp_times.size());

            ScoreGraphNode* src = sp_nodes[static_cast<size_t>(i - 1)];
            double span = s_edge->dest->timecode.measures_decimal() -
                          src->timecode.measures_decimal();
            if (span > 0) {
                double density = static_cast<double>(s_edge->spscore) / span;
                if (density > density_running) density_running = density;
            }
        }

        if (i > 0) {
            ScoreGraphEdge* act_edge =
                base_nodes[static_cast<size_t>(i - 1)]->branch_edge;
            if (act_edge && act_edge->frontend.has_value())
                max_running += act_edge->frontend->points;

            ScoreGraphEdge* deact_edge =
                ((i - 1) < static_cast<int>(sp_nodes.size()))
                    ? sp_nodes[static_cast<size_t>(i - 1)]->branch_edge
                    : nullptr;
            if (deact_edge) {
                for (const BackendSqueeze& be : deact_edge->backends) {
                    int64_t gain = be.points > 0 ? be.points : 0;
                    if (be.sqout_points > gain) gain = be.sqout_points;
                    max_running += gain;
                }
            }
        }
    }
}

}  // namespace hydra
