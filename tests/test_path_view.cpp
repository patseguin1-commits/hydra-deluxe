// Tests for app/path_view: the Song Details screen's derived strings and
// flags. These forms were previously composed inside ui/details_view.cpp's
// draw functions, where no test could reach them; the view-model is the
// interface, so the cases here pin the exact display strings.

#include "doctest.h"

#include <string>
#include <vector>

#include "app/analysis.h"
#include "app/path_view.h"
#include "corpus_util.h"

using namespace hydra;
using namespace hydra::app;

namespace {

// One analyzed corpus chart (the first that yields paths), shared across
// cases: analysis is the slow part.
const AnalysisResult& analyzed() {
    static const AnalysisResult result = [] {
        AnalysisSettings settings;
        settings.depth_mode = DepthMode::Scores;
        settings.depth_value = 10;
        settings.ms_filter = 10.0;
        for (const std::string& path : corpus::chart_paths()) {
            try {
                AnalysisResult r = analyze_chart_file(path, settings);
                if (!r.song.is_empty() && !r.record.paths.empty()) return r;
            } catch (const std::exception&) {
                continue;
            }
        }
        throw std::runtime_error("no analyzable corpus chart");
    }();
    return result;
}

}  // namespace

TEST_CASE("build_record_status: the three states and their lines") {
    // The store's status is what decides the panel; the view only formats it.
    auto ready = [](const HydraRecord& record) {
        store::RecordLookup lookup;
        lookup.status = store::RecordStatus::Ready;
        lookup.hyversion = store::current_record_version();
        lookup.record = record;
        return lookup;
    };

    CHECK(build_record_status(store::RecordLookup{}).state ==
          store::RecordStatus::NotAnalyzed);

    store::RecordLookup stale;
    stale.status = store::RecordStatus::Stale;
    stale.hyversion = "0.0.0";  // no record: a stale blob is never decoded
    RecordStatusView stale_view = build_record_status(stale);
    CHECK(stale_view.state == store::RecordStatus::Stale);
    CHECK(stale_view.lines.empty());

    const HydraRecord& rec = analyzed().record;
    RecordStatusView view = build_record_status(ready(rec));
    REQUIRE(view.state == store::RecordStatus::Ready);
    REQUIRE(view.lines.size() == 4);
    CHECK(view.lines[0] ==
          "Best score:  " + group_thousands(rec.best_path().totalscore()));
    CHECK(view.lines[1] ==
          "Paths kept:  " + std::to_string((int)rec.all_paths().size()));
    CHECK(view.lines[2] == "Limit timings:  10 ms");
    CHECK(view.lines[3] == "SP cap:  4 bars");

    HydraRecord nolimit = rec;
    nolimit.ms_limit.reset();
    CHECK(build_record_status(ready(nolimit)).lines[2] == "Limit timings:  off");

    // The cap line always names the cap the record ran at.
    HydraRecord whatif = rec;
    whatif.sp_cap = 32;
    CHECK(build_record_status(ready(whatif)).lines[3] == "SP cap:  32 bars");

    // A Ready record that found nothing stays Ready and says so in one line.
    HydraRecord nothing = rec;
    nothing.paths.clear();
    RecordStatusView none = build_record_status(ready(nothing));
    CHECK(none.state == store::RecordStatus::Ready);
    REQUIRE(none.lines.size() == 1);
    CHECK(none.lines[0] == "No paths found.");
}

TEST_CASE("build_score_breakdown: exact lines, truncation not rounding") {
    Path p;
    p.score_base = 3;
    p.score_combo = 2;  // 5/3 = 1.6666... — a slice truncates, %.3f would round

    std::vector<std::string> lines = build_score_breakdown(p);
    REQUIRE(lines.size() == 8);
    // (str(avg_mult()) + "000")[:5]: "1.666", NOT the rounded "1.667".
    CHECK(lines[0] == "Avg. Multiplier:      1.666x");
    CHECK(lines[1] == "\nNotes:                     3");
    CHECK(lines[2] == "Combo Bonus:               2");
    CHECK(lines[7] == "\nTotal Score:               5");
}

TEST_CASE("build_path_row: right-aligned ms, warn past the difficult floor") {
    Path p;
    CHECK(build_path_row(p).ms.empty());  // no activations, no difficulty

    Path hard;
    Activation act;
    act.skips = 0;
    act.e_offset = 300.0;  // not e-critical
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -12.5});
    hard.activations.push_back(act);
    PathRowView row = build_path_row(hard);
    CHECK(row.ms == "     12.5 ms");
    CHECK(row.warn);

    Path easy = hard;
    easy.activations[0].sqinouts[0].offset_ms = -1.5;
    row = build_path_row(easy);
    CHECK(row.ms == "      1.5 ms");
    CHECK_FALSE(row.warn);
}

TEST_CASE("build_path_list: score groups and the all-0 dedupe rule") {
    const HydraRecord& rec = analyzed().record;
    PathListView list = build_path_list(rec);

    // One group per distinct score along the traversal; every path lands in
    // exactly one group, in order.
    REQUIRE(!list.groups.empty());
    std::vector<const Path*> flat = rec.all_paths();
    size_t total = 0;
    for (const PathGroupView& g : list.groups) total += g.paths.size();
    CHECK(total == flat.size());
    CHECK(list.groups.front().score_label ==
          group_thousands(flat.front()->totalscore()));
    CHECK(list.more_label == "More Paths (Limit timings: 10 ms)");

    // An all-0 path that duplicates a listed path (same score AND notation)
    // stays hidden.
    HydraRecord dup = rec;
    dup.allzero_paths.clear();
    dup.allzero_paths.push_back(rec.paths.front());
    CHECK_FALSE(build_path_list(dup).show_allzero);

    // A different score shows the section, with the delta in the label.
    HydraRecord worse = dup;
    worse.allzero_paths.front().score_base -= 100;
    PathListView wl = build_path_list(worse);
    CHECK(wl.show_allzero);
    CHECK(wl.allzero_label ==
          group_thousands(worse.allzero_paths.front().totalscore()) +
              "   (-100)");
}

TEST_CASE("build_activations: headers, footer, and backend rows line up") {
    const AnalysisResult& ar = analyzed();
    const Path& best = ar.record.best_path();
    const SongTiming& timing = ar.song.timing();

    ActivationsView view = build_activations(best, ar.record, &timing,
                                             /*hit_window_ms=*/85.0);
    CHECK(view.acts.size() == best.all_activations().size());

    std::vector<Activation> acts = best.all_activations();
    for (size_t i = 0; i < view.acts.size(); ++i) {
        const ActivationDetailsView& av = view.acts[i];
        // The header leads with the notation string.
        CHECK(av.header.rfind(acts[i].notationstr(), 0) == 0);
        CHECK(av.difficult == acts[i].is_difficult());
        CHECK(av.sqinouts.size() == acts[i].sqinouts.size());
        CHECK(av.backends.size() == acts[i].display_backends().size());
        for (const BackendRowView& row : av.backends) {
            CHECK(!row.timing.empty());
            CHECK(!row.rating.empty());
        }
    }

    REQUIRE(!view.footer.empty());
    CHECK(view.footer[0].text ==
          "Leftover SP: " + std::to_string(best.leftover_sp) + ".");
    CHECK_FALSE(view.footer[0].warn);
}

TEST_CASE("build_multsqueezes: one labeled entry per squeeze") {
    const Path& best = analyzed().record.best_path();
    std::vector<MultSqueezeView> v = build_multsqueezes(best);
    CHECK(v.size() == best.multsqueezes.size());
    for (size_t i = 0; i < v.size(); ++i) {
        CHECK(v[i].label.find(" pts):   ") != std::string::npos);
        CHECK(v[i].howto == best.multsqueezes[i].howto());
    }
}
