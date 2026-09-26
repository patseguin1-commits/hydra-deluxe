// The JSON shapes hydra_replay writes and reads: the "paths" array of a `dump`
// or `target` file, one score split, and the windows read back out of a path.
//
// These used to live in core/replay.h, which pulled the whole JSON library
// into every file that includes the replay, the Preview's among them. Only
// hydra_replay and tests/test_replay use them, so they live beside the tool.

#ifndef HYDRA_TOOLS_REPLAY_JSON_H
#define HYDRA_TOOLS_REPLAY_JSON_H

#include <vector>

#include "json.hpp"

#include "core/replay.h"

namespace hydra {

// The same windows as windows_for_path, read out of a `dump` or `target` JSON
// file instead of a live record. `path` is one entry of that file's top-level
// "paths" array; each of its "activations" carries act_tick, deact_tick,
// sqout_tick (-1 or absent when there is none, or in a dump from before v6),
// and a "sqinouts" list whose SqOut entry holds the squeeze-out's offset in
// ms. A window with an offset but no sqout_tick must go through
// resolve_sqout_note before it is replayed.
//
// This exists because the only other way to hand a path to `hydra_replay
// score` was to retype it as an "act:deact,..." string, and that string used
// to drop the squeeze-out offset. Without the offset the squeezed phrase note
// is doubled as if it were still inside Star Power, so the score comes out
// high. Reading the file keeps every field.
//
// Throws std::runtime_error when the JSON is not that shape: no "activations"
// array, an activation missing act_tick or deact_tick, or a deact_tick of -1,
// which is how a dump writes "this record has no deactivation node" and means
// the path cannot be replayed faithfully.
std::vector<ReplayWindow> windows_from_json(const nlohmann::json& path);

// One score split as JSON, one key per kReplayScoreFields entry. The dump's
// per-path "score" object and the `score` command's totals both use it.
nlohmann::json score_json(const ReplayScore& s);

// The "paths" array of a hydra_replay dump: one object per path in `all`, in
// order, with the score split and every activation's ticks, SP meter, skips,
// chord and squeezes. windows_from_json reads one element of it back, and
// fcvideo reads the rest.
nlohmann::json paths_json(const std::vector<const Path*>& all, const SongTiming& timing);

}  // namespace hydra

#endif  // HYDRA_TOOLS_REPLAY_JSON_H
