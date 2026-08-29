#include "render/track_state.h"

#include <algorithm>
#include <map>

namespace hydra::render {

using app::PreviewActivation;
using app::PreviewBeat;
using app::PreviewLane;
using app::PreviewNote;
using app::PreviewScene;
using app::PreviewSpan;

namespace {

// Hydra marks an SP phrase (and a fill) by its last note's tick; Onyx's span
// reaches past that note. Half a millisecond keeps the note inside without
// ever reaching the next note.
constexpr double kSpanEndEpsilonS = 0.0005;

double s_of(double ms) { return ms / 1000.0; }

std::optional<Pad> pad_of(PreviewLane lane) {
    switch (lane) {
        case PreviewLane::Red:    return Pad::Red;
        case PreviewLane::Yellow: return Pad::Yellow;
        case PreviewLane::Blue:   return Pad::Blue;
        case PreviewLane::Green:  return Pad::Green;
        case PreviewLane::Kick:   break;
    }
    return std::nullopt;
}

TrackGem gem_of(const PreviewNote& n, bool pro) {
    TrackGem g;
    if (n.lane == PreviewLane::Kick) {
        g.kick = true;  // 2x kicks are plain kicks (Onyx has no 2x visual)
    } else {
        g.pad = *pad_of(n.lane);
        g.cymbal = pro && n.cymbal && n.lane != PreviewLane::Red;
    }
    g.velocity = n.ghost ? Velocity::Ghost : n.accent ? Velocity::Accent : Velocity::Normal;
    return g;
}

}  // namespace

Toggle toggle_at(const std::vector<std::pair<double, double>>& intervals, double t) {
    bool starts = false, ends = false, inside = false;
    for (const auto& iv : intervals) {
        if (iv.first == t) starts = true;
        if (iv.second == t) ends = true;
        if (iv.first < t && t < iv.second) inside = true;
    }
    // An edge that falls inside another interval of the same span is not an
    // edge of the merged span: the span simply goes on.
    if (inside) return Toggle::On;
    if (starts && ends) return Toggle::Restart;
    if (starts) return Toggle::Start;
    if (ends) return Toggle::End;
    return Toggle::Empty;
}

TrackState build_track_state(const PreviewScene& scene, const TrackStateOptions& opts) {
    TrackState st;
    auto span_iv = [](const PreviewSpan& s, double end_eps) {
        return TrackState::Interval{s_of(s.start_ms), s_of(s.end_ms) + end_eps};
    };
    for (const PreviewSpan& s : scene.sp_phrases) st.overdrive_.push_back(span_iv(s, kSpanEndEpsilonS));
    for (const PreviewSpan& s : scene.solos) st.solo_.push_back(span_iv(s, kSpanEndEpsilonS));
    // A hidden fill is one the game never showed, so it draws nothing.
    for (const app::PreviewFill& f : scene.fills) {
        if (f.state == app::PreviewFillState::Offered)
            st.fill_.push_back(span_iv(f.span, kSpanEndEpsilonS));
        else if (f.state == app::PreviewFillState::Taken)
            st.fill_taken_.push_back(span_iv(f.span, kSpanEndEpsilonS));
    }
    for (const PreviewActivation& a : scene.activations) {
        if (a.has_sp_end && a.sp_end_ms > a.ms)
            st.sp_active_.push_back({s_of(a.ms), s_of(a.sp_end_ms)});
        if (a.has_lane) {
            std::optional<Pad> pad = pad_of(a.lane);
            if (!pad) continue;
            for (const app::PreviewFill& f : scene.fills) {
                if (f.state != app::PreviewFillState::Taken || f.span.end_tick != a.tick) continue;
                st.fill_lane_.push_back({span_iv(f.span, kSpanEndEpsilonS), *pad});
                break;
            }
        }
    }

    // Every moment anything happens becomes an instant.
    std::map<double, TrackInstant> by_time;
    auto at = [&](double t) -> TrackInstant& {
        TrackInstant& inst = by_time[t];
        inst.t = t;
        return inst;
    };
    for (const PreviewNote& n : scene.notes) at(s_of(n.ms)).notes.push_back(gem_of(n, opts.pro));
    for (const PreviewBeat& b : scene.beats) at(s_of(b.ms)).beat = b.kind;
    auto edges = [&](const std::vector<TrackState::Interval>& ivs) {
        for (const auto& iv : ivs) {
            at(iv.first);
            at(iv.second);
        }
    };
    edges(st.overdrive_);
    edges(st.solo_);
    edges(st.fill_);
    edges(st.fill_taken_);
    edges(st.sp_active_);
    for (const auto& li : st.fill_lane_) {
        at(li.span.first);
        at(li.span.second);
    }

    st.instants_.reserve(by_time.size());
    for (auto& kv : by_time) {
        TrackInstant inst = std::move(kv.second);
        TrackInstant filled = st.synthesize(inst.t);
        inst.overdrive = filled.overdrive;
        inst.solo = filled.solo;
        inst.fill = filled.fill;
        inst.fill_taken = filled.fill_taken;
        inst.sp_active = filled.sp_active;
        inst.fill_lane = filled.fill_lane;
        inst.fill_lane_pad = filled.fill_lane_pad;
        st.instants_.push_back(std::move(inst));
    }
    return st;
}

TrackInstant TrackState::synthesize(double t) const {
    TrackInstant inst;
    inst.t = t;
    inst.overdrive = toggle_at(overdrive_, t);
    inst.solo = toggle_at(solo_, t);
    inst.fill = toggle_at(fill_, t);
    inst.fill_taken = toggle_at(fill_taken_, t);
    inst.sp_active = toggle_at(sp_active_, t);
    std::vector<Interval> lane_ivs;
    for (const LaneInterval& li : fill_lane_) lane_ivs.push_back(li.span);
    inst.fill_lane = toggle_at(lane_ivs, t);
    if (inst.fill_lane != Toggle::Empty) {
        // The lane of the interval starting at, ending at, or containing t
        // (a start wins over an end when two fills touch).
        for (const LaneInterval& li : fill_lane_) {
            if (li.span.first == t || (li.span.first < t && t < li.span.second)) {
                inst.fill_lane_pad = li.pad;
                break;
            }
        }
        if (!inst.fill_lane_pad)
            for (const LaneInterval& li : fill_lane_)
                if (li.span.second == t) {
                    inst.fill_lane_pad = li.pad;
                    break;
                }
    }
    return inst;
}

std::vector<TrackInstant> TrackState::window(double near_s, double far_s) const {
    std::vector<TrackInstant> out;
    auto lo = std::upper_bound(instants_.begin(), instants_.end(), near_s,
                               [](double t, const TrackInstant& i) { return t < i.t; });
    for (auto it = lo; it != instants_.end() && it->t < far_s; ++it) out.push_back(*it);
    // Onyx writes the synthesized key as `t1 + (t2 + t1) / 2`, which lands
    // past t2; it never reads that key, only the state (its neighbours'
    // before/after). We place it at the midpoint so the state is evaluated
    // strictly inside the window, which is the same state Onyx carries.
    if (out.empty()) out.push_back(synthesize((near_s + far_s) / 2.0));
    return out;
}

std::vector<ToggleSpan> TrackState::make_toggle_bounds(const std::vector<TrackInstant>& win,
                                                       double near_s, double far_s,
                                                       Toggle TrackInstant::*field) const {
    std::vector<ToggleSpan> spans;
    if (win.empty()) return spans;
    // State entering the window: on if the first instant ends or continues a
    // span (End / Restart / On), off otherwise.
    Toggle first = win.front().*field;
    bool on = first == Toggle::End || first == Toggle::Restart || first == Toggle::On;
    double t = near_s;
    auto push = [&](double t2, bool state) {
        if (t2 <= t) return;
        if (!spans.empty() && spans.back().on == state)
            spans.back().t2 = t2;
        else
            spans.push_back({t, t2, state});
        t = t2;
    };
    for (const TrackInstant& inst : win) {
        push(inst.t, on);
        Toggle tg = inst.*field;
        on = tg == Toggle::Start || tg == Toggle::Restart || tg == Toggle::On;
    }
    push(far_s, on);
    return spans;
}

}  // namespace hydra::render
