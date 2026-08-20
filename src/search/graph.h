// ScoreGraph — a song modelled as a two-track graph (base / SP) of timecode
// nodes joined by advance edges (deeper into the song, accruing points) and
// branch edges (toggling SP without advancing time). The engine in
// search/engine.cpp searches over it.

#ifndef HYDRA_SEARCH_GRAPH_H
#define HYDRA_SEARCH_GRAPH_H

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/model.h"
#include "core/timing.h"
#include "parse/song.h"

namespace hydra {

// Reachable-squeeze horizon in ms. Exposed so the batch CLI can print it in
// its settings header.
constexpr double kSqueezeWindowMs = 500.0;

struct ScoreGraphEdge;

struct ScoreGraphNode {
    Timecode timecode;
    ScoreGraphEdge* adv_edge = nullptr;
    ScoreGraphEdge* branch_edge = nullptr;
    bool is_sp = false;
    std::optional<Chord> chord;
};

struct ScoreGraphEdge {
    ScoreGraphNode* dest = nullptr;

    int64_t notecount = 0;
    int64_t basescore = 0;
    int64_t comboscore = 0;
    int64_t spscore = 0;
    int64_t soloscore = 0;
    int64_t accentscore = 0;
    int64_t ghostscore = 0;

    // (sp_timecode, extension_map) per SP phrase collected on this advance edge.
    // The extension map is from_tick -> to_tick; the engine scans it by key.
    std::vector<std::pair<Timecode, std::map<int64_t, int64_t>>> sp_times;

    std::optional<FrontendSqueeze> frontend;
    std::vector<BackendSqueeze> backends;
    std::vector<MultSqueeze> multsqueezes;

    // Set only on activation edges; absent (nullopt / empty) otherwise.
    std::optional<double> activation_fill_deadline_ms;
    std::map<int, Timecode> activation_initial_end_times;  // SP meter -> Timecode

    std::optional<Timecode> sqinout_time;
    std::optional<double> sqinout_timing;
    int late_sqin_count = 0;
    std::optional<Timecode> sqout_time;
    std::optional<Timecode> sqin_time;
};

class ScoreGraph {
public:
    // sp_meter_cap: bars the meter holds, or nullopt for no ceiling.
    ScoreGraph(const Song& song, std::optional<int> sp_meter_cap);

    ScoreGraphNode* start() const { return start_; }
    ScoreGraphNode* sp_start() const { return sp_start_; }
    int length() const { return length_; }
    std::optional<int> sp_meter_cap() const { return sp_meter_cap_; }
    const SongTiming& timing() const { return song_.timing(); }

private:
    // Graph construction.
    void build();

    void store_notecount(int64_t count);
    void store_soloscore(int64_t points);
    void store_basescore(int64_t points);
    void store_comboscore(int64_t points);
    void store_spscore(int64_t points);
    void store_accentscore(int64_t points);
    void store_ghostscore(int64_t points);
    void store_multsqueeze(const MultSqueeze& msq);
    void store_new_backend(const SongTimestamp& ts, int sp_points,
                           int sqout_points);

    int max_sp_bars() const;
    // Returns (from_tc, to_tc) pairs for the given deact timecodes.
    std::vector<std::pair<Timecode, Timecode>> extend_deacts(
        const std::vector<Timecode>& deact_tcs, const Timecode& sp_timecode);

    double head_time_offset(const Timecode& tc) const {
        return head_time_.ms() - tc.ms();
    }
    bool is_recent_to_head(const Timecode& tc) const;
    void set_head_time(const Timecode& tc);
    void handle_deact(const Timecode& deact_tc,
                      const std::optional<Chord>& chord);
    void advance_tracks(const Timecode& tc, const std::optional<Chord>& chord);
    ScoreGraphEdge* add_act_edge(const Chord& frontend_chord,
                                 int frontend_points,
                                 int64_t fill_length_ticks);
    void add_deact_edge();

    Timecode plusmeasure(const Timecode& tc, int64_t add_measures);

    ScoreGraphNode* new_node(const Timecode& tc, bool is_sp);
    ScoreGraphEdge* new_edge();

    const Song& song_;
    std::optional<int> sp_meter_cap_;

    // Stable-address storage for the graph. deque never invalidates element
    // references on push_back.
    std::deque<ScoreGraphNode> node_pool_;
    std::deque<ScoreGraphEdge> edge_pool_;

    ScoreGraphNode* start_ = nullptr;
    ScoreGraphNode* sp_start_ = nullptr;
    int length_ = 0;

    // Processing state.
    Timecode head_time_;
    bool head_time_set_ = false;
    ScoreGraphNode* base_track_head_ = nullptr;
    ScoreGraphNode* sp_track_head_ = nullptr;
    int combo_ = 0;
    int sp_phrase_count_ = 0;
    std::unordered_map<int64_t, Timecode> pending_deacts_;  // ticks -> Timecode
    std::vector<Timecode> deact_heap_;                      // min-heap on ticks
    std::vector<ScoreGraphEdge*> recent_deact_edges_;
    std::vector<BackendSqueeze> recent_backends_;
    ScoreGraphEdge* proto_base_edge_ = nullptr;
    ScoreGraphEdge* proto_sp_edge_ = nullptr;

    std::map<std::pair<int64_t, int64_t>, Timecode> plusmeasure_cache_;
};

}  // namespace hydra

#endif  // HYDRA_SEARCH_GRAPH_H
