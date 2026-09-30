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
#include "core/rules.h"
#include "core/timing.h"
#include "parse/song.h"

namespace hydra {

// Which game version's rule decides whether a drum fill spawns at all.
//
// A fill only appears in-game if the player's SP meter was already full by
// some deadline before the fill. The two versions disagree on that deadline:
//
//   * Ch11 — Clone Hero 1.1 and later: a flat 4 beats before the fill starts.
//     This is Hydra's normal rule and the default everywhere.
//   * Ch10 — Clone Hero 1.0, as Python Hydra 1.2 modelled it: roughly one
//     fill-length of lead time before the fill, clamped to 250..10000 ms.
//
// Ch10 answers "what could a 1.0 player have reached?": the app's "1.0 fills"
// setting and hydra_batch --legacy-fills. A stored result carries the rule it
// ran under in its key (store::Lens::legacy_fills, docs/adr/0010).
enum class FillDeadlineRule { Ch11, Ch10 };

// The engine_mode string hydra_batch stamps into a database's meta table so
// hydra_fillcompare can tell which rule produced it later (docs/adr/0010).
inline const char* engine_mode_stamp(FillDeadlineRule rule) {
    return rule == FillDeadlineRule::Ch10 ? "ch10" : "ch11";
}

// The latest a player's SP may become ready and still have this fill spawn.
// `fill_end_tick` is the fill marker's end; `fill_length_ticks` its length.
double activation_fill_deadline_ms(const SongTiming& timing,
                                   int64_t fill_end_tick,
                                   int64_t fill_length_ticks,
                                   FillDeadlineRule rule);

struct ScoreGraphEdge;

// Where one pending SP end moves when a phrase is collected: +2 measures,
// unless the SP cap's ceiling (2 * cap measures past the collecting note) is
// earlier. `clamped` says the ceiling won -- the end is now pinned to the
// collecting note, not to whatever anchored it before.
struct SpExtension {
    int64_t to_tick = 0;
    bool clamped = false;
};

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
    // The extension map is from_tick -> where that end moves (and whether the
    // cap pinned it there); the engine scans it by key. Only the SP track moves
    // an SP end, so base-track edges leave the map empty and read the times.
    std::vector<std::pair<Timecode, std::map<int64_t, SpExtension>>> sp_times;

    // The frontend chord's SP points: set only on activation edges, 0 elsewhere.
    int frontend_points = 0;
    std::vector<BackendSqueeze> backends;

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
    // rule: which game version decides when a fill spawns (default Ch11).
    // rules: the user's rule choices; the graph prices squeeze-outs by
    // rules.sqout_rule and hands the rest to the engine through rules().
    ScoreGraph(const Song& song, std::optional<int> sp_meter_cap,
               FillDeadlineRule rule = FillDeadlineRule::Ch11,
               const core::Rules& rules = core::default_rules());

    ScoreGraphNode* start() const { return start_; }
    std::optional<int> sp_meter_cap() const { return sp_meter_cap_; }
    const core::Rules& rules() const { return rules_; }
    const SongTiming& timing() const { return song_.timing(); }
    // The chart's multiplier squeezes, in chart order. One list: the combo
    // that decides them never depends on the path.
    const std::vector<MultSqueeze>& multsqueezes() const { return multsqueezes_; }

    // The notes left in the squeeze window (kSqueezeWindowMs) before the song's
    // last timestamp, i.e. the trailing notes no deactivation edge ever got to
    // claim. offset_ms is unset on these: offsets are only stamped on the copies
    // an edge keeps, measured against that edge's own destination.
    const std::vector<BackendSqueeze>& tail_backends() const {
        return recent_backends_;
    }

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
    // One moved end per given deact timecode, in order: where it goes and
    // whether the SP cap's ceiling (measured from sp_timecode) is what put
    // it there.
    struct DeactExtension {
        Timecode from;
        Timecode to;
        bool clamped = false;
    };
    std::vector<DeactExtension> extend_deacts(
        const std::vector<Timecode>& deact_tcs, const Timecode& sp_timecode);

    double head_time_offset(const Timecode& tc) const {
        return head_time_.ms() - tc.ms();
    }
    bool is_recent_to_head(const Timecode& tc) const;
    void set_head_time(const Timecode& tc);
    void handle_deact(const Timecode& deact_tc,
                      const std::optional<Chord>& chord);
    void advance_tracks(const Timecode& tc, const std::optional<Chord>& chord);
    ScoreGraphEdge* add_act_edge(int frontend_points, int64_t fill_length_ticks);
    void add_deact_edge();

    Timecode plusmeasure(const Timecode& tc, int64_t add_measures);

    ScoreGraphNode* new_node(const Timecode& tc, bool is_sp);
    ScoreGraphEdge* new_edge();

    const Song& song_;
    std::optional<int> sp_meter_cap_;
    FillDeadlineRule rule_ = FillDeadlineRule::Ch11;
    core::Rules rules_;

    // Stable-address storage for the graph. deque never invalidates element
    // references on push_back.
    std::deque<ScoreGraphNode> node_pool_;
    std::deque<ScoreGraphEdge> edge_pool_;

    ScoreGraphNode* start_ = nullptr;

    // Processing state.
    Timecode head_time_;
    ScoreGraphNode* base_track_head_ = nullptr;
    ScoreGraphNode* sp_track_head_ = nullptr;
    int combo_ = 0;
    std::vector<MultSqueeze> multsqueezes_;
    // The Song's sp_phrase_count(), kept here for max_sp_bars() -- not tallied
    // by this graph itself.
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
