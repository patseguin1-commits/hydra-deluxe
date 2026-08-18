// Generation counters: a producer-side counter bumped on each event, and a
// consumer-side watcher that compares-then-assigns once per frame. Views key
// per-event state on these rather than on object addresses — the heap can hand
// a new job the previous job's freed block, and a pointer compare then carries
// stale per-job state over (see AppState::analyze_generation).

#ifndef HYDRA_UI_GENERATION_H
#define HYDRA_UI_GENERATION_H

namespace hydra::ui {

struct Generation {
    int n = 0;
    void bump() { ++n; }
};

// `seen` defaults to -1 so the first changed() call observes the counter's
// initial 0; a consumer that must stay quiet until the first bump initializes
// seen to 0 instead.
struct GenerationWatcher {
    int seen = -1;
    bool changed(const Generation& g) {
        if (seen == g.n) return false;
        seen = g.n;
        return true;
    }
};

}  // namespace hydra::ui

#endif  // HYDRA_UI_GENERATION_H
