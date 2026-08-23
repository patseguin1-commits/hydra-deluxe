// Client for the DMBot leaderboard API (the backend behind dmleaderboards.com).
//
// The public site is a static SPA fronted by Cloudflare (which 403s bot fetches
// of the HTML); its data comes from a public, unauthenticated JSON API on
// render.com, reachable with a plain HTTPS GET. This is Hydra's first and only
// network client — two endpoints, both read-only:
//   GET /api/all-users            -> the ladder (for the searchable picker)
//   GET /api/user/{id}/scores     -> one user's posted scores
//
// The render.com backend is free-tier and spins down when idle, so the first
// request after a lull can take tens of seconds to cold-start. Callers run
// these off the render thread (see ui/dm_jobs.h) with a generous timeout and a
// cancel flag.

#ifndef HYDRA_NET_DMBOT_CLIENT_H
#define HYDRA_NET_DMBOT_CLIENT_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hydra::net {

// The production API root. Overridable per call so a mirror/proxy can be swapped
// in without touching callers.
inline constexpr const char* kDefaultApiBase = "https://dmbot-kb5j.onrender.com/api";

// One user on the ladder, from GET /api/all-users.
struct DmUser {
    std::string id;             // Discord ID — the /scores path parameter
    std::string username;
    std::optional<int> elo;     // unset for users with no ranked play
    int total_scores = 0;       // stats.total_scores, for the picker's row hint
    int64_t total_score = 0;    // stats.total_score, so the picker can sort by it
};

// One score a user has posted, from GET /api/user/{id}/scores. The endpoint
// returns two arrays, `scores` (metadata known) and `unknown_scores` (title/
// artist unknown to DMBot); both are flattened here and told apart by `known`.
// `identifier` is the Clone Hero chart-file MD5 — the exact join key against
// Hydra's hyhash.
struct DmScore {
    std::string identifier;     // 32-char MD5 hex (lowercased by fetch)
    std::string song_name;
    std::string artist;
    std::string charter;        // charter_refs joined with ", "
    int64_t score = 0;          // the score the player actually achieved
    bool is_fc = false;
    int percent = 0;
    int speed = 100;            // playback speed %, 100 = base
    std::optional<int> rank;    // leaderboard rank for this chart, if any
    std::string posted;         // ISO-8601 timestamp
    bool known = true;          // false for entries from unknown_scores
};

// Both throw std::runtime_error on any transport/HTTP/parse failure; the
// message is user-facing (the picker and report modals surface it verbatim).
// `cancel`, when non-null and set mid-transfer, aborts with a "cancelled"
// error (checked between read chunks; a cold-start connect can't be
// interrupted until the timeout).
std::vector<DmUser> fetch_users(const std::string& api_base = kDefaultApiBase,
                                const std::atomic<bool>* cancel = nullptr);

std::vector<DmScore> fetch_scores(const std::string& discord_id,
                                  const std::string& api_base = kDefaultApiBase,
                                  const std::atomic<bool>* cancel = nullptr);

// The transport seam: GET `url`, return the body, throw std::runtime_error on
// failure. The default is WinHTTP (set_fetcher with an empty function restores
// it). A harness with no network (the GUI test runner) installs one that
// returns canned JSON; the parse_* functions below then run unchanged.
using Fetcher = std::function<std::string(const std::string& url,
                                          const std::atomic<bool>* cancel)>;
void set_fetcher(Fetcher fetcher);

// The JSON halves of the two fetches, separated from the WinHTTP transport so
// tests can exercise them with canned payloads. Throw std::runtime_error with
// a user-facing message on unparseable or unexpectedly-shaped bodies.
std::vector<DmUser> parse_users_json(const std::string& body);
std::vector<DmScore> parse_scores_json(const std::string& body);

}  // namespace hydra::net

#endif  // HYDRA_NET_DMBOT_CLIENT_H
