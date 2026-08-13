// C ABI for Hydra's native path search. Kept in sync with hydra/hynative.py,
// which declares the same structs and signatures to ctypes.
//
// This is the coarse-grained half of the native core: where hy_category_scores
// crosses the boundary once per chord and loses to marshalling, hy_search
// crosses it once per chart. The whole graph goes over as arrays (built by
// hydra/hyflat.py) and the whole result comes back as a decision log.
//
// Nothing here builds hydata objects. The engine reports, per surviving path,
// the score components plus which node it activated at, which edge ended each
// activation and which squeeze markers landed on it; Python rebuilds the
// objects from that. Keeping the data model on one side of the boundary is
// what makes the port reviewable.
//
// Bump HY_ABI_VERSION in hydra_score.h on any change to a struct layout or
// signature here.

#ifndef HYDRA_SEARCH_H
#define HYDRA_SEARCH_H

#include <cstdint>

#include "hydra_score.h"

// Absent timecode. Matches hyflat.NO_TIME, and sorts below every real tick.
#define HY_NO_TIME ((int64_t)-1)

// depth_mode, pre-resolved so the hot loop compares ints not strings.
#define HY_DEPTH_SCORES 0
#define HY_DEPTH_POINTS 1
#define HY_DEPTH_OTHER  2

// hy_out_sq.kind
#define HY_SQ_IN  0
#define HY_SQ_OUT 1

#define HY_ERR_NULL_IN    (-4)
#define HY_ERR_BAD_GRAPH  (-5)
#define HY_ERR_NO_MEMORY  (-6)
#define HY_ERR_BAD_STATE  (-7)
#define HY_ERR_CANCELLED  (-8)

extern "C" {

typedef struct hy_node {
    int64_t tick;
    int32_t adv_edge;       // index into hy_search_in.edges, or -1
    int32_t branch_edge;    // index into hy_search_in.edges, or -1
    int32_t is_sp;
    int32_t _pad;
} hy_node;

// One (sp_timecode, extension_map) pair off ScoreGraphEdge.sp_times. The map
// is a slice of the parallel ext_key/ext_val arrays, sorted by key.
typedef struct hy_sptime {
    int64_t tick;
    double  ms;             // this phrase's time in ms; becomes sp_ready_time
    int32_t map_begin;
    int32_t map_end;
} hy_sptime;

typedef struct hy_backend {
    int64_t tick;
    double  offset_ms;
    int32_t points;
    int32_t sqout_points;
} hy_backend;

typedef struct hy_edge {
    int32_t dest;
    int32_t notecount;
    int32_t basescore;
    int32_t comboscore;
    int32_t spscore;
    int32_t soloscore;
    int32_t accentscore;
    int32_t ghostscore;
    int32_t frontend_points;
    int32_t frontend_is_accent;
    int32_t frontend_is_ghost;
    int32_t skipped_dynamic_points;
    int32_t late_sqin_count;
    int32_t sp_begin;       // slice of hy_search_in.sptimes
    int32_t sp_end;
    int32_t aiet_begin;     // slice of hy_search_in.aiet, indexed by SP meter
    int32_t aiet_end;
    int32_t be_begin;       // slice of hy_search_in.backends
    int32_t be_end;
    double  activation_fill_deadline_ms;
    double  sqinout_timing;
    int64_t sqinout_time;   // HY_NO_TIME when absent
    int64_t sqout_time;
    int64_t sqin_time;
} hy_edge;

// Called once per iteration, so the caller's progress bar behaves as it does
// under the pure-Python search. node_index is the first live path's node, or
// -1 when every path has finished. Around 70 calls per chart, so the cost of
// re-entering Python here is not worth avoiding.
//
// Returns 0 to continue, non-zero to abandon the search -- hy_search then
// returns HY_ERR_CANCELLED having allocated nothing. This exists because the
// pure-Python search can be interrupted by raising out of this callback (see
// hyutil._deactline_callback, which is how a too-slow SP meter rung is
// abandoned), and an exception cannot propagate back through a C callback.
// Without a cancel path the adaptive ladder would silently ignore its time
// budget whenever the native engine was in use.
typedef int32_t (*hy_progress_fn)(int32_t node_index, double fraction);

typedef struct hy_search_in {
    const hy_node*    nodes;
    const hy_edge*    edges;
    const hy_sptime*  sptimes;
    const int64_t*    ext_key;
    const int64_t*    ext_val;
    const hy_backend* backends;
    const int64_t*    aiet;

    int32_t n_nodes;
    int32_t n_edges;
    int32_t n_sptimes;
    int32_t n_ext;
    int32_t n_backends;
    int32_t n_aiet;

    int32_t start_node;
    int32_t has_sp_cap;     // 0 means an uncapped meter
    int32_t sp_cap;
    int32_t depth_mode;     // one of HY_DEPTH_*
    int32_t depth_value;
    int32_t has_ms_filter;
    double  ms_filter;
    int32_t flag_skipped_dynamics;
    int32_t graph_length;   // ScoreGraph.length, the progress denominator
    hy_progress_fn progress;    // may be null
} hy_search_in;

// One SqIn/SqOut marker on an activation.
typedef struct hy_out_sq {
    int32_t kind;           // HY_SQ_IN or HY_SQ_OUT
    int32_t _pad;
    double  offset;         // the edge's sqinout_timing, as handed to hydata
} hy_out_sq;

typedef struct hy_out_act {
    int32_t act_node;       // node the path activated at; gives timecode + chord
    int32_t skips;
    int32_t sp_meter;
    int32_t deact_edge;     // edge whose backends this activation carries, or -1
    int32_t sq_begin;       // slice of hy_search_out.sqs
    int32_t sq_end;
    double  e_offset;
} hy_out_act;

// Paths come back in preorder: each top-level path (depth 0) in final sorted
// order, immediately followed by its variant subtree. depth is what lets the
// caller rebuild the tree without a parent pointer.
typedef struct hy_out_path {
    int32_t score_base;
    int32_t score_combo;
    int32_t score_sp;
    int32_t score_solo;
    int32_t score_accents;
    int32_t score_ghosts;
    int32_t notecount;
    int32_t leftover_sp;
    int32_t skipped_accents;
    int32_t skipped_ghosts;
    int32_t var_point;      // -1 for a top-level path
    int32_t depth;          // 0 for a top-level path
    int32_t act_begin;      // slice of hy_search_out.acts
    int32_t act_end;
} hy_out_path;

typedef struct hy_search_out {
    const hy_out_path* paths;
    const hy_out_act*  acts;
    const hy_out_sq*   sqs;
    int32_t n_paths;
    int32_t n_acts;
    int32_t n_sqs;
    int32_t iterations;     // how many times the graph was stepped
    void*   handle;         // owns the three arrays; release with hy_search_free
} hy_search_out;

// Runs the whole search. On HY_OK the caller owns *out and must release it
// with hy_search_free exactly once. On any other return code nothing was
// allocated and *out is zeroed.
HY_EXPORT int32_t hy_search(const hy_search_in* in, hy_search_out* out);

HY_EXPORT void hy_search_free(hy_search_out* out);

}  // extern "C"

#endif  // HYDRA_SEARCH_H
