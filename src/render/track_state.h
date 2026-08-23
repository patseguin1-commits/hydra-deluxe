// Track state — the chart as a timeline of instants, the way Onyx's previewer
// holds it (its `Map Double (CommonState DrumState)`): one entry per moment
// anything happens, carrying the gems struck then and, for every span-like
// thing (SP phrase, solo, fill, Hydra's active SP window and activation
// lane), whether that span starts, ends, restarts, is ongoing, or is absent at
// that moment. The draw code walks the slice of instants inside the visible
// time window and never touches the PreviewScene directly.
//
// Times are seconds (Onyx's unit). Device-free and unit-tested.

#ifndef HYDRA_RENDER_TRACK_STATE_H
#define HYDRA_RENDER_TRACK_STATE_H

#include <optional>
#include <utility>
#include <vector>

#include "app/preview_view.h"

namespace hydra::render {

// Onyx's SustainState for a span with no payload: what a span does at an
// instant. Between instants a span is On (ongoing) or Empty (absent).
enum class Toggle { Empty, Start, End, Restart, On };

// The four pads, left to right. Kick is separate (TrackGem::kick).
enum class Pad { Red = 0, Yellow = 1, Blue = 2, Green = 3 };

enum class Velocity { Ghost, Normal, Accent };

struct TrackGem {
    bool kick = false;
    Pad pad = Pad::Red;    // meaningless when kick
    bool cymbal = false;   // pro-drums cymbal (never for Red or kick)
    Velocity velocity = Velocity::Normal;
};

struct TrackInstant {
    double t = 0.0;  // seconds
    std::vector<TrackGem> notes;
    Toggle overdrive = Toggle::Empty;  // SP phrase (Onyx: overdrive)
    Toggle solo = Toggle::Empty;
    Toggle fill = Toggle::Empty;       // any fill window (drawn like Onyx's BRE)
    Toggle sp_active = Toggle::Empty;  // Hydra: the path's active SP window
    Toggle fill_lane = Toggle::Empty;  // Hydra: the activated fill's lit lane
    std::optional<Pad> fill_lane_pad;  // which lane, while fill_lane != Empty
    std::optional<app::PreviewBeatKind> beat;
};

struct TrackStateOptions {
    bool pro = true;  // false: every pad draws as a tom (Onyx's 4-lane mode)
};

// A [t1, t2) stretch of the visible window with one span state, from
// make_toggle_bounds.
struct ToggleSpan {
    double t1 = 0.0, t2 = 0.0;
    bool on = false;
};

class TrackState {
public:
    TrackState() = default;

    const std::vector<TrackInstant>& instants() const { return instants_; }

    // Onyx's zoomMap: the instants with near < t < far. When none fall inside,
    // one synthesized instant at the window's midpoint carries the
    // ongoing/absent state of every span so the floor and lanes still draw.
    std::vector<TrackInstant> window(double near_s, double far_s) const;

    // Onyx's makeToggleBounds over one span field: consecutive spans covering
    // [near, far] with that field on or off, adjacent equal states merged.
    std::vector<ToggleSpan> make_toggle_bounds(const std::vector<TrackInstant>& win,
                                               double near_s, double far_s,
                                               Toggle TrackInstant::*field) const;

private:
    friend TrackState build_track_state(const app::PreviewScene&, const TrackStateOptions&);

    using Interval = std::pair<double, double>;  // [start, end] seconds
    struct LaneInterval {
        Interval span;
        Pad pad;
    };

    std::vector<TrackInstant> instants_;
    std::vector<Interval> overdrive_, solo_, fill_, sp_active_;
    std::vector<LaneInterval> fill_lane_;

    TrackInstant synthesize(double t) const;
};

// Build the timeline from a scene. SP phrases and fills are extended by half a
// millisecond past their last note so that note reads as inside (Hydra marks
// a phrase by its last note; Onyx's phrase extends past it). Active SP
// windows end exactly at the deact node. The activated fill's lane comes from
// the activation at the fill's end note.
TrackState build_track_state(const app::PreviewScene& scene, const TrackStateOptions& opts);

// Onyx's makeToggle: the state of a span at `t` given its intervals.
Toggle toggle_at(const std::vector<std::pair<double, double>>& intervals, double t);

}  // namespace hydra::render

#endif  // HYDRA_RENDER_TRACK_STATE_H
