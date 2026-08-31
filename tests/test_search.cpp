// Invariant tests for search/ (ScoreGraph + engine + pather).
//
// The engine's best score must be stable across the config knobs that are not
// supposed to change it (search depth, the ms filter), every reported path
// must be internally consistent, and the all-0 pass must honor its contract.

#include "doctest.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/squeeze_rating.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/engine.h"
#include "search/graph.h"
#include "search/pather.h"
#include "store/path_codec.h"
#include "store/serialize.h"

using namespace hydra;

TEST_CASE("search invariants hold across the corpus and config knobs") {
    int charts = 0, mismatches = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        ++charts;

        std::string d;
        try {
            // Depth asks for more alternate paths and must not move the
            // optimum; the ms filter is a constraint, so its best can only
            // be at or below the unconstrained best.
            SearchSettings cfg;
            cfg.sp_cap = 4;
            cfg.depth_mode = DepthMode::Scores;
            cfg.depth_value = 0;
            cfg.ms_filter = std::nullopt;
            HydraRecord shallow = analyze_chart(song, cfg);
            cfg.depth_value = 200;
            HydraRecord deep = analyze_chart(song, cfg);
            cfg.ms_filter = 20.0;
            HydraRecord filtered = analyze_chart(song, cfg);

            if (shallow.paths.empty()) {
                d = "no paths";
            } else {
                const int64_t best = shallow.paths.front().totalscore();
                if (deep.paths.front().totalscore() != best)
                    d = "depth changed the best score";
                else if (!filtered.paths.empty() &&
                         filtered.paths.front().totalscore() > best)
                    d = "ms filter scored above the unconstrained best";
                else if (deep.paths.size() < shallow.paths.size())
                    d = "deeper search returned fewer paths";

                // Paths come out best-first, and every path in a record
                // reports the same chart notecount.
                int64_t prev = INT64_MAX;
                const int notecount = deep.paths.front().notecount;
                for (const Path& p : deep.paths) {
                    if (p.totalscore() > prev) {
                        d = "paths not sorted by score";
                        break;
                    }
                    prev = p.totalscore();
                    if (p.notecount != notecount) {
                        d = "notecount varies between paths";
                        break;
                    }
                    if (p.tied_pathcount() < 1) {
                        d = "tied_pathcount below 1";
                        break;
                    }
                }
            }
        } catch (const ChartFileError&) {
            continue;  // charts the engine rejects are covered elsewhere
        }

        if (!d.empty() && ++mismatches <= 8)
            CHECK_MESSAGE(false, path << " " << d);
    }

    CHECK(mismatches == 0);
    REQUIRE(charts > 0);
    MESSAGE("checked " << charts << " charts");
}

// The engine stamps each activation with its frontend transfer scales at
// copy-out; the details view recomputes them from the song timing on demand.
// Both go through frontend_transfer_scales, so this pins the stored values
// against a live recompute across the corpus -- and with them the ratios the
// The legacy Clone Hero 1.0 fill rule is a whole different spawn deadline, so
// it reshapes which activations exist at all. That must still produce a normal,
// complete record -- the score itself is not pinned here (it is a different
// game's answer, and tests/test_fill_deadline.cpp pins the math instead).
TEST_CASE("legacy fill deadline analyzes a chart end to end") {
    int analyzed = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;

        std::optional<HydraRecord> legacy;
        try {
            SearchSettings cfg;
            cfg.sp_cap = 4;
            cfg.depth_value = 0;
            cfg.legacy_fill_deadline = true;
            legacy = analyze_chart(song, cfg);
        } catch (const ChartFileError&) {
            continue;
        }

        // A record came back, and its paths are real ones the engine scored.
        REQUIRE(legacy.has_value());
        CHECK(legacy->sp_cap == 4);
        if (!legacy->paths.empty()) {
            CHECK(legacy->best_path().totalscore() > 0);
            ++analyzed;
        }
        if (analyzed >= 3) break;  // three charts is enough to prove the path
    }

    CHECK(analyzed > 0);
}

// squeeze detail lines and eff. figures show.
TEST_CASE("stored transfer scales match the display-layer recomputation") {
    int charts = 0, acts = 0, nonflat = 0, mismatches = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;

        std::optional<HydraRecord> record;
        try {
            SearchSettings cfg;
            cfg.sp_cap = 4;
            cfg.depth_mode = DepthMode::Scores;
            cfg.depth_value = 4;
            cfg.ms_filter = std::nullopt;
            record = analyze_chart(song, cfg);
        } catch (const ChartFileError&) {
            continue;
        }
        ++charts;

        std::string d;
        for (const Path* p : record->all_paths()) {
            for (const Activation& act : p->all_activations()) {
                ++acts;
                if (act.transfer_pre.early != 1.0 || act.transfer_pre.late != 1.0)
                    ++nonflat;

                auto scales = frontend_transfer_scales(act, song.timing());
                if (!scales) {
                    d = "display recomputation returned no scales";
                    break;
                }
                if (std::abs(scales->pre.early - act.transfer_pre.early) > 1e-9 ||
                    std::abs(scales->pre.late - act.transfer_pre.late) > 1e-9 ||
                    std::abs(scales->post.early - act.transfer_post.early) > 1e-9 ||
                    std::abs(scales->post.late - act.transfer_post.late) > 1e-9) {
                    d = "stored scales diverge from recomputation";
                    break;
                }
                if (act.transfer_pre.early <= 0.0 || act.transfer_pre.late <= 0.0 ||
                    act.transfer_post.early <= 0.0 || act.transfer_post.late <= 0.0) {
                    d = "non-positive transfer scale";
                    break;
                }
            }
            if (!d.empty()) break;
        }
        if (!d.empty() && ++mismatches <= 8)
            CHECK_MESSAGE(false, path << " " << d);
    }

    CHECK(mismatches == 0);
    REQUIRE(charts > 0);
    REQUIRE(acts > 0);
    MESSAGE("checked " << acts << " activations on " << charts << " charts ("
                       << nonflat << " with a non-flat scale)");
}

// An activation that squeezes a note out of SP ends its SP on that note, so
// the note is hit after SP is gone -- and every note after it is hit later
// still. None of them can be a backend squeeze. The deactivation edge holds
// backends for every path that deactivates there, squeezed out or not, so the
// record build has to trim per activation; this pins that it does.
TEST_CASE("no activation keeps backends past its squeezed-out note") {
    int charts = 0, sqout_acts = 0, mismatches = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;

        std::optional<HydraRecord> record;
        try {
            SearchSettings cfg;
            cfg.sp_cap = 4;
            cfg.depth_mode = DepthMode::Scores;
            cfg.depth_value = 4;
            cfg.ms_filter = std::nullopt;
            record = analyze_chart(song, cfg);
        } catch (const ChartFileError&) {
            continue;
        }
        ++charts;

        std::string d;
        for (const Path* p : record->all_paths()) {
            for (const Activation& act : p->all_activations()) {
                std::optional<double> sqout;
                for (const SPSqueeze& sq : act.sqinouts)
                    if (sq.kind == SqueezeKind::SqOut &&
                        (!sqout || sq.offset() < *sqout))
                        sqout = sq.offset();
                if (!sqout) continue;
                ++sqout_acts;

                for (const BackendSqueeze& b : act.backends) {
                    if (b.offset_ms.value_or(0.0) > *sqout + 0.01) {
                        d = "backend past the sqout note";
                        break;
                    }
                }
                if (!d.empty()) break;
            }
            if (!d.empty()) break;
        }
        if (!d.empty() && ++mismatches <= 8)
            CHECK_MESSAGE(false, path << " " << d);
    }

    CHECK(mismatches == 0);
    REQUIRE(charts > 0);
    MESSAGE("checked " << sqout_acts << " squeeze-out activations on " << charts
                       << " charts");
}

// The all-0 pass is a second, constrained search. Its whole contract is that
// every activation it reports records skips == 0, that the 0 ms limit is a
// requirement rather than a preference, and that it never scores above the
// unconstrained optimum.
TEST_CASE("search_allzero returns only all-0 paths inside the 0 ms limit") {
    int checks = 0, mismatches = 0, found = 0;

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;

        ScoreGraph graph(song, 4);
        std::vector<Path> allzero = search_allzero(graph);
        ++checks;
        if (allzero.empty()) continue;
        ++found;

        HydraRecord holder;
        holder.allzero_paths = allzero;
        const int64_t optimum =
            run_search(graph, DepthMode::Scores, 0, std::nullopt, false)
                .front()
                .totalscore();

        std::string d;
        for (const Path* p : holder.all_allzero_paths()) {
            if (!p->is_allzero()) {
                d = "not all-0: " + p->pathstring();
                break;
            }
            if (p->difficulty().value_or(0.0) > 0.0) {
                d = "over the 0 ms limit: " + p->pathstring() + " needs " +
                    std::to_string(*p->difficulty()) + " ms";
                break;
            }
            if (p->totalscore() > optimum) {
                d = "scores above the optimum: " + p->pathstring();
                break;
            }
        }
        if (!d.empty() && ++mismatches <= 8)
            CHECK_MESSAGE(false, path << " " << d);
    }

    CHECK(mismatches == 0);
    CHECK(found > 0);
    MESSAGE("checked " << checks << " charts, " << found << " with an all-0 path");
}

// ---- SP that outlasts the chart ----------------------------------------
//
// When the last activation's Star Power ends after the chart's final note,
// the graph never builds a deactivation edge, so no edge holds the trailing
// notes. The rebuild step synthesizes those rows against the SP end the
// engine tracked (Path::sp_end_time), which includes every mid-SP phrase
// extension the plain measure-count reconstruction cannot see.

namespace {

// A hand-built 4/4 120 BPM song. 192 ticks per beat, so a measure is 768
// ticks and 2000 ms; one tick is 2000/768 ms. Built directly rather than
// parsed so the note ticks in the assertions below are exactly these.
struct TailNote {
    int64_t tick;
    bool sp_phrase = false;
    bool activation = false;
};

Song build_tail_song(const std::vector<TailNote>& notes) {
    Song song(192);
    song.tpm_changes[0] = 768;
    song.bpm_changes[0] = 120.0;
    song.build_timing();

    for (const TailNote& n : notes) {
        SongTimestamp ts;
        ts.timecode = song.timecode(n.tick);
        ts.chord.add_note(NoteColor::Red);
        ts.flag_sp = n.sp_phrase;
        if (n.activation) ts.activation_length = 384;
        song.sequence.push_back(ts);
    }
    return song;
}

double tick_ms(const Song& song, int64_t tick) {
    return song.timing().ms_index().at(tick);
}

// The best path's final activation, which every case here expects to be one
// that never deactivated.
const Activation& last_act(const std::vector<Path>& paths) {
    REQUIRE(!paths.empty());
    REQUIRE(!paths.front().activations.empty());
    return paths.front().activations.back();
}

}  // namespace

TEST_CASE("SP past the last note: backends measured from the tracked SP end") {
    // Two SP phrases, then an activation at tick 2304 with a 2-bar meter, so
    // SP ends 4 measures later at tick 5376. The chart stops at 5280.
    Song song = build_tail_song({{0, true, false},
                                 {768, true, false},
                                 {1536},
                                 {2304, false, true},
                                 {3072},
                                 {3840},
                                 {4608},
                                 {5136},
                                 {5280}});

    ScoreGraph graph(song, 4);
    std::vector<Path> paths =
        run_search(graph, DepthMode::Scores, 0, std::nullopt);

    const Activation& act = last_act(paths);
    REQUIRE(act.sp_meter.has_value());
    CHECK(*act.sp_meter == 2);
    REQUIRE(act.timecode.has_value());
    CHECK(act.timecode->ticks() == 2304);

    const int64_t end_tick = 5376;
    const double end_ms = tick_ms(song, end_tick);

    // The rows exist at all -- the bug this pins showed "Backends: None."
    REQUIRE(!act.backends.empty());

    // Every trailing note is before the SP end, so every offset is negative.
    for (const BackendSqueeze& b : act.backends) {
        REQUIRE(b.offset_ms.has_value());
        CHECK(*b.offset_ms < 0.0);
    }

    // The chart's last note is one of the rows, at its true distance.
    const BackendSqueeze* last_note = nullptr;
    for (const BackendSqueeze& b : act.backends)
        if (b.timecode.ticks() == 5280) last_note = &b;
    REQUIRE(last_note != nullptr);
    CHECK(*last_note->offset_ms ==
          doctest::Approx(tick_ms(song, 5280) - end_ms).epsilon(1e-9));

    // And the rows put the SP end back exactly where the engine had it.
    auto deact = activation_deact_tick(act, song.timing());
    REQUIRE(deact.has_value());
    CHECK(*deact == end_tick);
}

TEST_CASE("SP past the last note: a mid-activation phrase extends the end") {
    // Same shape, but the note at 3840 completes an SP phrase during the
    // activation. That pushes the pending deactivation two measures out, from
    // tick 5376 to 6912 -- an extension the measure-count fallback (2 measures
    // per SP bar, from a meter still recorded as 2) cannot see.
    Song song = build_tail_song({{0, true, false},
                                 {768, true, false},
                                 {1536},
                                 {2304, false, true},
                                 {3072},
                                 {3840, true, false},
                                 {4608},
                                 {5376},
                                 {6144},
                                 {6720},
                                 {6816}});

    ScoreGraph graph(song, 4);
    std::vector<Path> paths =
        run_search(graph, DepthMode::Scores, 0, std::nullopt);

    const Activation& act = last_act(paths);
    REQUIRE(act.sp_meter.has_value());
    CHECK(*act.sp_meter == 2);

    const int64_t extended_tick = 6912;
    const int64_t plain_tick = 5376;

    REQUIRE(!act.backends.empty());
    for (const BackendSqueeze& b : act.backends) {
        REQUIRE(b.offset_ms.has_value());
        CHECK(*b.offset_ms < 0.0);
    }

    auto deact = activation_deact_tick(act, song.timing());
    REQUIRE(deact.has_value());
    CHECK(*deact == extended_tick);
    CHECK(*deact != plain_tick);
    // The activation records no SqIn, so the old fallback would have landed
    // on the plain end. Pin that the rows, not the reconstruction, answered.
    CHECK(act.sqinouts.empty());
    CHECK(song.timing().plusmeasure(*act.timecode, 4).ticks() == plain_tick);
}

TEST_CASE("SP past the last note: synthesized rows survive a store round-trip") {
    Song song = build_tail_song({{0, true, false},
                                 {768, true, false},
                                 {1536},
                                 {2304, false, true},
                                 {3072},
                                 {3840},
                                 {4608},
                                 {5136},
                                 {5280}});

    ScoreGraph graph(song, 4);
    std::vector<Path> paths =
        run_search(graph, DepthMode::Scores, 0, std::nullopt);
    const Activation& act = last_act(paths);
    REQUIRE(!act.backends.empty());

    // Decoded timecodes are ticks-only until restore_timecodes resolves them
    // against the song's tempo map -- the same two steps read_record takes.
    HydraRecord back;
    back.paths.push_back(
        store::decode_path_node(store::encode_path_node(paths.front())));
    store::restore_timecodes(back, song.timing());
    REQUIRE(back.paths.front().activations.size() ==
            paths.front().activations.size());
    const Activation& ract = back.paths.front().activations.back();

    // The writer stores display_backends(), so what survives is the rows
    // inside the +/-500 ms display window -- the same trim a deactivating
    // activation's rows get. Here that is the last note, at -250 ms.
    std::vector<BackendSqueeze> want = act.display_backends();
    REQUIRE(!want.empty());
    REQUIRE(ract.backends.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i) CHECK(ract.backends[i] == want[i]);

    CHECK(activation_deact_tick(ract, song.timing()) ==
          activation_deact_tick(act, song.timing()));
}
