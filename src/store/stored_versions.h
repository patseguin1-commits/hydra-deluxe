// Every version stamp on stored, computed data, in one file (ADR 0018).
//
// A saved row was computed by some build of Hydra. Before this build shows
// it, it asks one question: would this build have computed the same thing?
// Each kind of stored data answers with a stamp saved on the row. A StampRule
// holds the stamp this build writes and every stamp it reads as current, and
// StampRule::is_current is the only place a stored stamp is compared. The
// store's SQL twin of the results check (record_store.cpp) is built from the
// same lists.
//
// None of these is the app version. A release that changes nothing a stamp
// covers keeps every saved row.

#ifndef HYDRA_STORE_STORED_VERSIONS_H
#define HYDRA_STORE_STORED_VERSIONS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hydra::store {

template <typename T, size_t N>
struct StampRule {
    T written;                  // stamped on every new row
    std::array<T, N> accepted;  // every stamp read as current; holds `written`

    constexpr bool is_current(const T& stored) const {
        for (const T& a : accepted)
            if (a == stored) return true;
        return false;
    }
};

// ---- Results (the results and paths tables) --------------------------------

// The analysis version. BUMP IT, to the version of the release that ships the
// change, whenever analysis output changes in a way the path format and the
// hydra_rules.ini fingerprint don't already catch: the engine (src/search),
// the scoring (src/core), or what a record holds. Then shrink `accepted` to
// the new stamp alone. A stale result reads Stale and asks for re-analysis.
//
// "1.8.3" is accepted because 1.8.3 stamped its app version on results that
// are identical to 1.8.2's.
inline constexpr StampRule<std::string_view, 2> kResultsStamp{"1.8.2", {"1.8.2", "1.8.3"}};

// The stored path layout: the structure blob and the node payloads it points
// at share this one number, because a node is only ever read through its
// structure. BUMP IT when either layout changes. Its history:
// 3 added clamp_tick to nodes. 4 put the u64 rules fingerprint right after
// the version and added sqout_tick and collected_phrase_ticks (ADR 0014).
// 5 spelled every chord as its lane code (ADR 0015). 6 moved the multiplier
// squeezes and root totals into the structure and left a node holding its
// activations alone (ADR 0017).
inline constexpr StampRule<uint32_t, 1> kPathFormatStamp{6, {6}};

// ---- Dynamics counts (the dynamics table) ----------------------------------

// How notes are counted. BUMP IT (add 1) whenever count_dynamics, or the
// parser that feeds it, changes what any chart counts: src/parse/song.cpp
// (the .mid, .chart and .sng loaders), src/parse/midi.cpp and
// src/parse/srb.cpp. The hydra_rules.ini fingerprint doesn't apply: the rules
// move activation marks, never which notes a chart has. A stale count reads as
// missing, and the Dynamics tab recounts it in the background.
// 0 = rows saved before the stamp existed. 1 = the first stamp.
inline constexpr StampRule<int, 1> kDynamicsCountStamp{1, {1}};

// The dynamics blob's byte layout. BUMP IT when encode_dynamics changes.
inline constexpr StampRule<uint8_t, 1> kDynamicsBlobStamp{1, {1}};

// A build always reads back what it writes.
static_assert(kResultsStamp.is_current(kResultsStamp.written));
static_assert(kPathFormatStamp.is_current(kPathFormatStamp.written));
static_assert(kDynamicsCountStamp.is_current(kDynamicsCountStamp.written));
static_assert(kDynamicsBlobStamp.is_current(kDynamicsBlobStamp.written));

}  // namespace hydra::store

#endif  // HYDRA_STORE_STORED_VERSIONS_H
