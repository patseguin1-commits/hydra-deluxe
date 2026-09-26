// Tests for app/path_view: the Song Details screen's derived strings and
// flags. These forms were previously composed inside ui/details_view.cpp's
// draw functions, where no test could reach them; the view-model is the
// interface, so the cases here pin the exact display strings.

#include "doctest.h"

#include <map>
#include <string>
#include <vector>

#include "app/analysis.h"
#include "app/display_format.h"
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
    CHECK(view.lines[2] == "Path limit:  10 ms");
    CHECK(view.lines[3] == "SP cap:  4 bars");

    HydraRecord nolimit = rec;
    nolimit.ms_limit.reset();
    CHECK(build_record_status(ready(nolimit)).lines[2] == "Path limit:  off");

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

TEST_CASE("build_score_breakdown: exact lines, rounded like the report") {
    Path p;
    p.score_base = 3;
    p.score_combo = 2;  // 5/3 = 1.6666...

    std::vector<std::string> lines = build_score_breakdown(p);
    REQUIRE(lines.size() == 8);
    // Rounded to three places, the same as the report's mult column.
    CHECK(lines[0] == "Avg. Multiplier:      1.667x");
    CHECK(lines[1] == "\nNotes:                     3");
    CHECK(lines[2] == "Combo Bonus:               2");
    CHECK(lines[7] == "\nTotal Score:               5");
}

TEST_CASE("display format: the average multiplier rounds the exact double") {
    // Python's round(x, 3) on the binary value, not on the decimal literal.
    CHECK(format_avg_mult(2.3456) == "2.346");  // a slice would give 2.345
    CHECK(format_avg_mult(2.0005) == "2.001");  // stored as 2.000500000000000167
    CHECK(format_avg_mult(1.0005) == "1.000");  // stored as 1.000499999999999945
    CHECK(py_round3(2.3456) == 2.346);
}

TEST_CASE("display format: ms text is one decimal and a unit") {
    CHECK(format_ms(12.3) == "12.3ms");
    CHECK(format_ms(-20.0) == "-20.0ms");
    CHECK(format_ms(0.0) == "0.0ms");
}

TEST_CASE("build_activations: the calibration fill reads positive = early on both lines") {
    HydraRecord rec;  // only feeds the footer
    auto view_of = [&rec](const Activation& act) {
        Path p;
        p.activations.push_back(act);
        ActivationsView v = build_activations(p, rec, nullptr, 85.0);
        REQUIRE(v.acts.size() == 1);
        return v.acts[0];
    };

    // E0: 12.3 ms early. The header already showed +12.3; the details line
    // used to print the raw offset, -12.3.
    Activation e0;
    e0.skips = 0;
    e0.sp_meter = 2;
    e0.e_offset = -12.3;
    ActivationDetailsView av = view_of(e0);
    // The fixture sets no timecode, so it sits at tick 0 (m1.1.0); an
    // activation always has one now (record format v7, docs/adr/0017).
    CHECK(av.header == "E0    (2 SP)\t   m1.1.0\t   12.3ms");
    CHECK(av.calibration == "Calibration fill: 12.3ms (required)");

    // E-critical but not E0: no ms in the header, and the details line uses
    // the same sign rule, so 20 ms late reads negative.
    Activation e1;
    e1.skips = 1;
    e1.sp_meter = 2;
    e1.e_offset = 20.0;
    av = view_of(e1);
    CHECK(av.header == "E1    (2 SP)\t   m1.1.0");
    CHECK(av.calibration == "Calibration fill: -20.0ms (optional)");
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
    CHECK(list.more_label == "More Paths (Path limit: 10 ms)");

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

TEST_CASE("build_activations: the scale warning prints the end that warned") {
    HydraRecord rec;  // only feeds the footer; irrelevant here
    auto warning_of = [&rec](const Activation& act) {
        Path p;
        p.activations.push_back(act);
        ActivationsView v = build_activations(p, rec, nullptr, 85.0);
        REQUIRE(v.acts.size() == 1);
        return v.acts[0].scale_warning;
    };

    Activation base;
    base.skips = 0;
    base.e_offset = 300.0;  // not e-critical

    // A SqIn is judged at the pre (pre-extension) end. With post at identity,
    // the line must print the pre scale and name the SqIn's end -- the old
    // code printed post's meaningless x1.00.
    Activation sqin = base;
    sqin.transfer_pre = TransferScale{1.0, 0.5};
    sqin.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 50.0});
    std::string warn = warning_of(sqin);
    CHECK(warn ==
          "Frontend timing scales x0.50 at the SqIn's SP end: "
          "+10ms (late) at the frontend moves that end only +5.0ms.");

    // A gap past the combined budget trips materiality even at identity
    // scales. With nothing but x1.00 to report, no line at all.
    Activation overbudget = base;
    overbudget.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 250.0});
    CHECK(warning_of(overbudget).empty());

    // Backend rows still read the post end, worded exactly as before.
    Activation backend = base;
    backend.transfer_post = TransferScale{1.0, 0.5};
    BackendSqueeze row;
    row.offset_ms = 50.0;
    backend.backends.push_back(row);
    warn = warning_of(backend);
    CHECK(warn ==
          "Frontend timing scales x0.50 to the SP end: "
          "+10ms (late) at the frontend moves the SP end only +5.0ms.");

    // Both ends warning in the same direction with different scales are
    // listed separately, labeled by what each end judges.
    Activation both = base;
    both.transfer_pre = TransferScale{1.0, 0.5};
    both.transfer_post = TransferScale{1.0, 0.8};
    both.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 50.0});
    both.backends.push_back(row);
    warn = warning_of(both);
    CHECK(warn ==
          "Frontend timing scales x0.80 (late, backends) / x0.50 (late, SqIn).");
}

TEST_CASE("build_activations: overfill warning text") {
    // 120 BPM, 4/4, 192 ticks per beat -- a measure is 4 beats, so 768 ticks
    // per measure. Tick 960 is one full measure plus one beat in, which
    // prints as m2.2.0 (the display is 1-based: measure 2, beat 2, tick 0).
    std::map<int64_t, int64_t> tpm{{0, 768}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    SongTiming timing(192, tpm, bpm);

    // clamp_tick says the cap pinned this window's end to the note at tick
    // 960; the SqOut is the frontend-decided squeeze the warning needs to
    // have something to attach to (see the cap_clamped tests above).
    Activation act;
    act.timecode = timing.timecode(0);
    act.sp_meter = 2;
    act.clamp_tick = 960;
    act.deact_tick = 6144;
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -50.0});

    Path path;
    path.activations.push_back(act);

    HydraRecord record;
    record.sp_cap = 4;

    // With a SongTiming at hand, the warning names the exact measure the
    // clamped note falls on.
    ActivationsView with_timing = build_activations(path, record, &timing, 85.0);
    REQUIRE(with_timing.acts.size() == 1);
    CHECK(with_timing.acts[0].overfill_warning == "SP overfilled at m2.2.0");

    // Without a SongTiming (no songmeta row), there is no way to turn the
    // clamped tick into a measure string, so the line drops the position.
    ActivationsView without_timing = build_activations(path, record, nullptr, 85.0);
    REQUIRE(without_timing.acts.size() == 1);
    CHECK(without_timing.acts[0].overfill_warning == "SP overfilled");

    // No clamp_tick at all -- the window was never cap-clamped, so there is
    // nothing to warn about even with the same SqOut present.
    Activation unclamped = act;
    unclamped.clamp_tick.reset();
    Path plain_path;
    plain_path.activations.push_back(unclamped);
    ActivationsView plain = build_activations(plain_path, record, &timing, 85.0);
    REQUIRE(plain.acts.size() == 1);
    CHECK(plain.acts[0].overfill_warning.empty());
}

TEST_CASE("build_activations: the backend limit hides far rows but never "
          "squeezed-out ones") {
    HydraRecord rec;  // only feeds the footer; irrelevant here

    Activation act;
    act.skips = 0;
    act.e_offset = 300.0;  // not e-critical
    const std::pair<double, int64_t> rows_at[] = {{-30.0, 200}, {-100.0, 100},
                                                   {60.0, 300}};
    for (const auto& [ms, tick] : rows_at) {
        BackendSqueeze row;
        row.timecode = Timecode::raw(tick);
        row.offset_ms = ms;
        act.backends.push_back(row);
    }
    // sqout_tick names the +60 row (tick 300), so that row is the
    // squeezed-out one. It has to be the last row in chart order: nothing can
    // be a backend past the note squeezed out of SP, and display_backends
    // drops any row that claims to be.
    act.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 60.0});
    act.sqout_tick = 300;

    Path p;
    p.activations.push_back(act);

    auto rows = [&](std::optional<double> limit) {
        ActivationsView v = limit ? build_activations(p, rec, nullptr, 85.0, limit)
                                  : build_activations(p, rec, nullptr, 85.0);
        REQUIRE(v.acts.size() == 1);
        return v.acts[0].backends;
    };

    // No limit: every stored row shows.
    CHECK(rows(std::nullopt).size() == 3);

    // At 50 ms the -100 row goes; the +60 row stays because it is squeezed out.
    std::vector<BackendRowView> limited = rows(50.0);
    REQUIRE(limited.size() == 2);
    CHECK(limited[0].timing == "-30.0");
    CHECK_FALSE(limited[0].warn);
    CHECK(limited[1].timing == "60.0");
    // +60 ms is past the leeway: the engine never counted this chord, so the
    // row is tagged uncounted and not highlighted (user decision 19).
    CHECK(limited[1].rating.find("squeezed out (uncounted)") != std::string::npos);
    CHECK_FALSE(limited[1].warn);
}

TEST_CASE("build_activations: a squeezed-out row past the leeway is worth 0") {
    HydraRecord rec;  // only feeds the footer

    // Round and Round (Ratt), second activation: an R+Y chord squeezed out
    // 479.999 ms past the SP end. The engine never counts it, so the table
    // must not claim it banks 260 and loses 200, and must not highlight it.
    Activation far;
    far.skips = 0;
    far.e_offset = 300.0;  // not e-critical
    BackendSqueeze far_row;
    far_row.timecode = Timecode::raw(3256);
    far_row.points = 460;
    far_row.sqout_points = 260;
    far_row.is_sp = true;
    far_row.offset_ms = 479.999;
    far.backends.push_back(far_row);
    far.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 479.999});
    far.sqout_tick = 3256;

    Path p;
    p.activations.push_back(far);
    ActivationsView v = build_activations(p, rec, nullptr, 85.0);
    REQUIRE(v.acts.size() == 1);
    REQUIRE(v.acts[0].backends.size() == 1);
    CHECK(v.acts[0].backends[0].points == "0");
    CHECK(v.acts[0].backends[0].rating ==
          "Free SqOut <-- squeezed out (uncounted)");
    CHECK_FALSE(v.acts[0].backends[0].warn);

    // The same chord squeezed out 5 ms inside SP really costs 200, and
    // that row keeps its warning colour.
    Activation near = far;
    near.backends[0].offset_ms = -5.0;
    near.sqinouts[0].offset_ms = -5.0;
    Path q;
    q.activations.push_back(near);
    v = build_activations(q, rec, nullptr, 85.0);
    REQUIRE(v.acts[0].backends.size() == 1);
    CHECK(v.acts[0].backends[0].points == "260");
    CHECK(v.acts[0].backends[0].rating ==
          "Standard SqOut <-- squeezed out (-200)");
    CHECK(v.acts[0].backends[0].warn);
}

// User decision 19: every row the engine does not count reads 0 and is not
// highlighted, not only squeezed-out ones. Labels stay as they are.
TEST_CASE("build_activations: plain rows past the leeway show 0") {
    HydraRecord rec;  // only feeds the footer

    Activation act;
    act.skips = 0;
    act.e_offset = 300.0;  // not e-critical
    // Inside SP, inside the 3 ms leeway, just past it, far past it.
    const std::pair<int64_t, double> rows_at[] = {
        {100, -40.0}, {200, 2.5}, {300, 20.0}, {400, 120.0}};
    for (const auto& [tick, ms] : rows_at) {
        BackendSqueeze row;
        row.timecode = Timecode::raw(tick);
        row.points = 460;
        row.sqout_points = 260;
        row.offset_ms = ms;
        act.backends.push_back(row);
    }
    // A phrase chord past the leeway that this path did not squeeze out.
    BackendSqueeze phrase;
    phrase.timecode = Timecode::raw(500);
    phrase.points = 460;
    phrase.sqout_points = 260;
    phrase.is_sp = true;
    phrase.offset_ms = 150.0;
    act.backends.push_back(phrase);

    Path p;
    p.activations.push_back(act);
    ActivationsView v = build_activations(p, rec, nullptr, 85.0);
    REQUIRE(v.acts.size() == 1);
    const std::vector<BackendRowView>& b = v.acts[0].backends;
    REQUIRE(b.size() == 5);
    CHECK(b[0].points == "460");
    CHECK(b[0].rating == "Easy");
    CHECK(b[1].points == "460");
    CHECK(b[1].rating == "Standard");
    CHECK(b[2].points == "0");
    CHECK(b[2].rating == "Hard (uncounted)");
    CHECK(b[3].points == "0");
    CHECK(b[3].rating == "Insane (uncounted)");
    CHECK(b[4].points == "0");
    CHECK(b[4].rating == "Free SqOut");
    for (const BackendRowView& row : b) CHECK_FALSE(row.warn);
}

// Not an invariant: it names a chart the GUI test can open to see an
// uncounted squeezed-out row for real. Prints nothing when none exists.
TEST_CASE("find a chart with an uncounted squeezed-out row" * doctest::skip()) {
    AnalysisSettings settings;
    settings.depth_mode = DepthMode::Scores;
    settings.depth_value = 2;
    for (const std::string& path : corpus::chart_paths()) {
        // AnalysisResult has no default constructor (Song needs a
        // resolution), so it lives in an optional.
        std::optional<AnalysisResult> analyzed_chart;
        try {
            analyzed_chart.emplace(analyze_chart_file(path, settings));
        } catch (const std::exception&) {
            continue;
        }
        const AnalysisResult& r = *analyzed_chart;
        if (r.record.paths.empty()) continue;
        ActivationsView v = build_activations(r.record.best_path(), r.record,
                                              &r.song.timing(), 85.0);
        for (const ActivationDetailsView& av : v.acts)
            for (const BackendRowView& row : av.backends)
                if (row.rating.find("squeezed out (uncounted)") !=
                    std::string::npos) {
                    MESSAGE(path << " | " << av.header);
                    return;
                }
    }
    MESSAGE("no corpus chart has one at depth 2");
}

TEST_CASE("build_multsqueezes: one labeled entry per squeeze") {
    const HydraRecord& rec = analyzed().record;
    std::vector<MultSqueezeView> v = build_multsqueezes(rec);
    CHECK(v.size() == rec.multsqueezes.size());
    for (size_t i = 0; i < v.size(); ++i) {
        CHECK(v[i].label.find(" pts):   ") != std::string::npos);
        CHECK(v[i].howto == rec.multsqueezes[i].howto());
    }
}
