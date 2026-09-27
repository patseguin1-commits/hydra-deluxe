// Tests for app/path_view: the Song Details screen's derived strings and
// flags. These forms were previously composed inside ui/paths_tab.cpp's
// draw functions, where no test could reach them; the view-model is the
// interface, so the cases here pin the exact display strings.

#include "doctest.h"

#include <map>
#include <chrono>
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

// Burnout (Green Day, charter Hoph2o), analyzed the way the GUI tests
// analyze it: Expert, Pro Drums, 2x Bass, SP cap 4, 2 scores, 10 ms limit.
const AnalysisResult& burnout() {
    static const AnalysisResult result = [] {
        AnalysisSettings settings;
        settings.depth_mode = DepthMode::Scores;
        settings.depth_value = 2;
        settings.ms_filter = 10.0;
        for (const std::string& path : corpus::chart_paths())
            if (path.find("Green Day - Burnout") != std::string::npos)
                return analyze_chart_file(path, settings);
        throw std::runtime_error("Burnout is not in testdata/input");
    }();
    return result;
}

// The separator the new labels use: a middle dot, U+00B7, in UTF-8.
const std::string kDot = " \xC2\xB7 ";

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

TEST_CASE("PathsTabCache: views are built once and rebuilt only when their inputs move") {
    const AnalysisResult& ar = analyzed();
    const HydraRecord& rec = ar.record;
    const SongTiming& timing = ar.song.timing();
    PathsTabCache cache;

    // The list: once per record generation, however many frames ask.
    for (int frame = 0; frame < 5; ++frame) cache.list(rec, 7);
    CHECK(cache.list_builds() == 1);
    const PathListView& list = cache.list(rec, 8);  // the record was re-read
    CHECK(cache.list_builds() == 2);

    // Every listed row carries the path's own label and ms cell.
    for (const PathGroupView& g : list.groups)
        for (const Path* p : g.paths) {
            CHECK(cache.row(p).label == p->pathstring());
            CHECK(cache.row(p).cell.ms == build_path_row(*p).ms);
        }

    // The details: once per (path, record, hit window, backend limit).
    const Path& best = rec.best_path();
    for (int frame = 0; frame < 5; ++frame)
        cache.details(best, rec, 8, &timing, 70.0, std::nullopt, core::default_rules());
    CHECK(cache.details_builds() == 1);
    cache.details(best, rec, 8, &timing, 71.0, std::nullopt, core::default_rules());
    CHECK(cache.details_builds() == 2);
    cache.details(best, rec, 8, &timing, 71.0, 30.0, core::default_rules());
    CHECK(cache.details_builds() == 3);
    cache.details(best, rec, 9, &timing, 71.0, 30.0, core::default_rules());
    CHECK(cache.details_builds() == 4);
    std::vector<const Path*> all = rec.all_paths();
    if (all.size() > 1) {
        cache.details(*all[1], rec, 9, &timing, 71.0, 30.0, core::default_rules());
        CHECK(cache.details_builds() == 5);
    }

    // What the cache hands back is what a fresh build gives.
    const PathsTabCache::Details& d =
        cache.details(best, rec, 9, &timing, 71.0, 30.0, core::default_rules());
    CHECK(d.breakdown == build_score_breakdown(best));
    CHECK(d.squeezes.size() == build_multsqueezes(rec).size());
    CHECK(d.activations.acts.size() ==
          build_activations(best, rec, &timing, 71.0, 30.0).acts.size());

    // The stored-result lines: once per record generation.
    store::RecordLookup lookup;
    lookup.status = store::RecordStatus::Ready;
    lookup.record = rec;
    for (int frame = 0; frame < 5; ++frame) cache.status(lookup, 9);
    CHECK(cache.status_builds() == 1);
    CHECK(cache.status(lookup, 9).lines == build_record_status(lookup).lines);
}

TEST_CASE("PathsTabCache: 600 cached frames cost far less than 600 rebuilds") {
    using clock = std::chrono::steady_clock;
    const AnalysisResult& ar = analyzed();
    const HydraRecord& rec = ar.record;
    const SongTiming& timing = ar.song.timing();
    const Path& best = rec.best_path();

    // What a Paths frame did before: every view, every row label.
    const clock::time_point t0 = clock::now();
    for (int frame = 0; frame < 600; ++frame) {
        PathListView list = build_path_list(rec);
        for (const Path* p : rec.all_paths()) {
            std::string label = p->pathstring();
            PathRowView row = build_path_row(*p);
        }
        std::vector<MultSqueezeView> sq = build_multsqueezes(rec);
        ActivationsView acts = build_activations(best, rec, &timing, 70.0);
        std::vector<std::string> bd = build_score_breakdown(best);
    }
    const clock::time_point t1 = clock::now();
    PathsTabCache cache;
    for (int frame = 0; frame < 600; ++frame) {
        cache.list(rec, 1);
        cache.details(best, rec, 1, &timing, 70.0, std::nullopt, core::default_rules());
    }
    const clock::time_point t2 = clock::now();

    const double rebuilt_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double cached_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    MESSAGE("600 rebuilt frames: " << rebuilt_ms << " ms; 600 cached frames: " << cached_ms
                                   << " ms");
    CHECK(cached_ms * 10.0 < rebuilt_ms);
}

TEST_CASE("format_measure: one form for both tabs") {
    // 192 ticks a beat, 768 a measure: tick 960 is measure 2, beat 2.
    std::map<int64_t, int64_t> tpm{{0, 768}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    SongTiming timing(192, tpm, bpm);
    CHECK(format_measure(timing, 0) == "m1.1.0");
    CHECK(format_measure(timing, 960) == "m2.2.0");
    CHECK(format_measure(timing, 1000) == "m2.2.40");
    CHECK(format_measure(timing.timecode(960)) == "m2.2.0");
    CHECK(format_ms_spaced(163.0) == "163.0 ms");
}

TEST_CASE("activation rows: Burnout's three activations") {
    const AnalysisResult& ar = burnout();
    const HydraRecord& rec = ar.record;
    const Path& best = rec.best_path();
    REQUIRE(best.pathstring() == "3- 1 2");
    REQUIRE(best.totalscore() == 378315);
    const SongTiming& timing = ar.song.timing();

    ActivationsView view = build_activations(best, rec, &timing, kDefaultHitWindowMs);
    REQUIRE(view.acts.size() == 3);
    CHECK(view.summary == "3" + kDot + "3 bars each" + kDot + "no SP left over");

    const ActivationRowView& a1 = view.acts[0];
    CHECK(a1.number == 1);
    CHECK(a1.notation == "3-");
    CHECK(a1.measure == "m32.1.0");
    CHECK(a1.sp_bars == 3);
    CHECK(a1.bars == "3 bars");
    CHECK(a1.badge == "squeeze out 163 ms");
    CHECK(a1.difficult);
    CHECK(a1.chord == "[Kick - GreenCym]");
    REQUIRE(a1.squeeze_sentences.size() == 1);
    CHECK(a1.squeeze_sentences[0].text ==
          "Hit the [  Y  ] note more than 163.0 ms late so it lands after Star Power "
          "ends. It scores 260 fewer points, and its SP phrase banks for later.");
    CHECK(a1.squeeze_sentences[0].warn);
    CHECK(a1.backends_label == "3 notes near the SP end");

    const ActivationRowView& a2 = view.acts[1];
    CHECK(a2.number == 2);
    CHECK(a2.notation == "1");
    CHECK(a2.measure == "m58.1.0");
    CHECK(a2.badge.empty());
    CHECK(a2.squeeze_sentences.empty());
    CHECK(a2.backends_label == "6 notes near the SP end");

    const ActivationRowView& a3 = view.acts[2];
    CHECK(a3.notation == "2");
    CHECK(a3.measure == "m88.1.0");
    CHECK(a3.backends_label == "7 notes near the SP end");

    // No song length given: no timeline.
    CHECK_FALSE(a1.song_fraction.has_value());
    CHECK(view.timeline_end.empty());
}

TEST_CASE("activation timeline: onset over the song's length, and the end measure") {
    // 120 BPM, 4/4, 192 ticks a beat: a measure is 2000 ms.
    std::map<int64_t, int64_t> tpm{{0, 768}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    SongTiming timing(192, tpm, bpm);
    Activation act;
    act.timecode = timing.timecode(768);  // measure 2, 2000 ms
    act.sp_meter = 2;
    act.skips = 0;
    act.e_offset = 300.0;  // not e-critical
    Path p;
    p.activations.push_back(act);
    HydraRecord rec;

    ActivationsView view = build_activations(p, rec, &timing, 85.0, std::nullopt,
                                             core::default_rules(), 10000.0);
    REQUIRE(view.acts.size() == 1);
    REQUIRE(view.acts[0].song_fraction.has_value());
    CHECK(*view.acts[0].song_fraction == doctest::Approx(0.2));
    CHECK(view.timeline_end == "m6");  // 10 s is tick 3840, the start of measure 6
    CHECK(view.summary == "1" + kDot + "2 bars" + kDot + "no SP left over");

    // No timing: no fraction, whatever the length.
    ActivationsView blind = build_activations(p, rec, nullptr, 85.0, std::nullopt,
                                              core::default_rules(), 10000.0);
    CHECK_FALSE(blind.acts[0].song_fraction.has_value());
    CHECK(blind.timeline_end.empty());
}

TEST_CASE("activation badge: shown only when the activation needs a squeeze") {
    Activation none;
    none.skips = 0;
    none.e_offset = 300.0;  // not e-critical
    CHECK(activation_badge(none).empty());

    Activation sqin = none;
    sqin.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 12.4});
    CHECK(activation_badge(sqin) == "squeeze in 12 ms");

    Activation sqout = none;
    sqout.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -163.0});
    CHECK(activation_badge(sqout) == "squeeze out 163 ms");

    // A required (E0) calibration fill is a squeeze too; the hardest one names the badge.
    Activation e0 = none;
    e0.e_offset = -30.0;
    CHECK(activation_badge(e0) == "calibration fill 30 ms");
    e0.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, -5.0});
    CHECK(activation_badge(e0) == "calibration fill 30 ms");

    // An optional (E1) fill is not required, so no badge.
    Activation e1 = none;
    e1.skips = 1;
    e1.e_offset = -30.0;
    CHECK(activation_badge(e1).empty());
}

TEST_CASE("squeeze sentences: SqIn, SqOut, and what a squeeze-out costs") {
    HydraRecord rec;
    auto sentence_of = [&rec](const Activation& act) {
        Path p;
        p.activations.push_back(act);
        ActivationsView v = build_activations(p, rec, nullptr, 85.0);
        REQUIRE(v.acts.size() == 1);
        REQUIRE(v.acts[0].squeeze_sentences.size() == 1);
        return v.acts[0].squeeze_sentences[0];
    };
    Activation base;
    base.skips = 0;
    base.e_offset = 300.0;  // not e-critical

    Activation sqin = base;
    sqin.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, 50.0});
    TextLine s = sentence_of(sqin);
    CHECK(s.text ==
          "Hit the SP phrase's last note more than 50.0 ms early so it lands before Star "
          "Power ends. The phrase then counts while Star Power runs, which makes Star "
          "Power last longer.");
    CHECK(s.warn);

    Activation easy_in = base;
    easy_in.sqinouts.push_back(SPSqueeze{SqueezeKind::SqIn, -20.0});
    s = sentence_of(easy_in);
    CHECK(s.text.rfind("Hit the SP phrase's last note no more than 20.0 ms late so it "
                       "lands before Star Power ends.", 0) == 0);
    CHECK_FALSE(s.warn);

    // A SqOut with no stored squeezed-out row names no chord and no cost.
    Activation bare_out = base;
    bare_out.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 60.0});
    CHECK(sentence_of(bare_out).text ==
          "Hit the SP phrase's last note no more than 60.0 ms early so it lands after "
          "Star Power ends. Its SP phrase banks for later.");

    // The Round and Round fixture: an R+Y phrase chord squeezed out 480 ms past
    // the SP end. The engine never counted it, so it costs nothing.
    Activation far = base;
    BackendSqueeze row;
    row.timecode = Timecode::raw(3256);
    row.chord.add_note(NoteColor::Red);
    row.chord.add_note(NoteColor::Yellow);
    row.points = 460;
    row.sqout_points = 260;
    row.is_sp = true;
    row.offset_ms = 479.999;
    far.backends.push_back(row);
    far.sqinouts.push_back(SPSqueeze{SqueezeKind::SqOut, 479.999});
    far.sqout_tick = 3256;
    s = sentence_of(far);
    CHECK(s.text ==
          "Hit the [ RY  ] note no more than 480.0 ms early so it lands after Star Power "
          "ends. It costs no points, because Hydra's score never counted that note under "
          "Star Power, and its SP phrase banks for later.");
    CHECK_FALSE(s.warn);

    // The same chord 5 ms inside SP really costs 460 - 260 = 200.
    Activation near = far;
    near.backends[0].offset_ms = -5.0;
    near.sqinouts[0].offset_ms = -5.0;
    s = sentence_of(near);
    CHECK(s.text ==
          "Hit the [ RY  ] note more than 5.0 ms late so it lands after Star Power ends. "
          "It scores 200 fewer points, and its SP phrase banks for later.");
    CHECK(s.warn);
}

TEST_CASE("path buttons: Burnout's list, in the mockup's groups") {
    const HydraRecord& rec = burnout().record;
    PathButtonsView v = build_path_buttons(rec, /*depth_mode=*/0, /*depth_value=*/2);
    CHECK(v.within_label == "Within 2 scores");
    REQUIRE(v.buttons.size() == 4);

    CHECK(v.buttons[0].path == &rec.best_path());
    CHECK(v.buttons[0].group == PathButtonView::Group::Optimal);
    CHECK(v.buttons[0].notation == "3- 1 2");
    CHECK(v.buttons[0].title == "378,315" + kDot + "3- 1 2");
    CHECK(v.buttons[0].detail == "hardest squeeze 163.0 ms");
    CHECK(v.buttons[0].detail_warn);

    CHECK(v.buttons[1].group == PathButtonView::Group::Within);
    CHECK(v.buttons[1].title == "378,175" + kDot + "0 4 1");
    CHECK(v.buttons[1].detail.empty());
    CHECK(v.buttons[2].group == PathButtonView::Group::Within);
    CHECK(v.buttons[2].title == "378,075" + kDot + "2 1 2");

    CHECK(v.buttons[3].group == PathButtonView::Group::AllZero);
    CHECK(v.buttons[3].title == "375,955" + kDot + "0 0 0 0");
    CHECK(v.buttons[3].detail == "2,360 below optimal");
    CHECK_FALSE(v.buttons[3].detail_warn);

    CHECK(within_label(0, 1) == "Within 1 score");
    CHECK(within_label(1, 5000) == "Within 5,000 points");
    CHECK(within_label(1, 1) == "Within 1 point");
}

TEST_CASE("multiplier squeeze: Burnout's one squeeze and the fold's summary") {
    std::vector<MultSqueezeView> v = build_multsqueezes(burnout().record);
    REQUIRE(v.size() == 1);
    CHECK(v[0].label == "2x   (+15 pts):   [Red - YellowCym]");
    CHECK(v[0].howto == "Hit [Red] first.");
    CHECK(v[0].points == 15);
    CHECK(multsqueeze_summary(v) == "+15");
    CHECK(multsqueeze_summary({}) == "none");
    std::vector<MultSqueezeView> three(3, v[0]);
    CHECK(multsqueeze_summary(three) == "3" + kDot + "+45");
}

TEST_CASE("PathsTabUi: one row open at a time, expand and collapse all") {
    PathsTabUi ui;
    ui.reset(3);
    CHECK(ui.act_open == std::vector<char>{1, 0, 0});
    CHECK(ui.backends_open == std::vector<char>{0, 0, 0});
    CHECK_FALSE(ui.all_open());

    ui.click_row(2);  // opens row 3 alone
    CHECK(ui.act_open == std::vector<char>{0, 0, 1});
    ui.click_row(2);  // closes it again
    CHECK(ui.act_open == std::vector<char>{0, 0, 0});

    ui.set_all(true);
    CHECK(ui.all_open());
    ui.click_row(1);  // an open row closes and leaves the others open
    CHECK(ui.act_open == std::vector<char>{1, 0, 1});
    ui.set_all(false);
    CHECK(ui.act_open == std::vector<char>{0, 0, 0});
    ui.click_row(7);  // past the end: nothing
    CHECK(ui.act_open == std::vector<char>{0, 0, 0});

    PathsTabUi empty;
    empty.reset(0);
    CHECK_FALSE(empty.all_open());
}

TEST_CASE("PathsTabCache: folds reset for a new path, not for a display setting") {
    const AnalysisResult& ar = burnout();
    const HydraRecord& rec = ar.record;
    const SongTiming& timing = ar.song.timing();
    const Path& best = rec.best_path();
    PathsTabCache cache;

    cache.details(best, rec, 1, &timing, 70.0, std::nullopt, core::default_rules());
    REQUIRE(cache.ui().act_open.size() == 3);
    CHECK(cache.ui().act_open[0] == 1);
    cache.ui().set_all(true);

    // A display setting rebuilds the rows but keeps what is unfolded.
    cache.details(best, rec, 1, &timing, 70.0, 30.0, core::default_rules());
    CHECK(cache.ui().all_open());
    cache.details(best, rec, 1, &timing, 70.0, 30.0, core::default_rules(), 126000.0);
    CHECK(cache.ui().all_open());
    CHECK(cache.details_builds() == 3);

    // Another path starts fresh: first row open, the rest folded.
    const Path& other = *rec.all_paths()[1];
    cache.details(other, rec, 1, &timing, 70.0, 30.0, core::default_rules(), 126000.0);
    CHECK(cache.ui().act_open.size() == other.walk_activations().size());
    CHECK(cache.ui().act_open[0] == 1);
    CHECK_FALSE(cache.ui().all_open());

    // The path buttons: once per record generation and score range.
    for (int frame = 0; frame < 5; ++frame) cache.buttons(rec, 1, 0, 2);
    CHECK(cache.buttons_builds() == 1);
    cache.buttons(rec, 1, 0, 3);
    CHECK(cache.buttons_builds() == 2);
    cache.buttons(rec, 2, 0, 3);
    CHECK(cache.buttons_builds() == 3);
}
