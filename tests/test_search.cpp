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

                // Copy-out stamps deact_tick on every activation it produces
                // (blob v4), so a record fresh off the engine should never be
                // missing it.
                if (!act.deact_tick.has_value()) {
                    d = "activation missing deact_tick";
                    break;
                }

                // A backend row's offset_ms is the gap from the deactivation
                // node to that row, in ms. Walking a row's offset back to a
                // tick has to land on the same node copy-out stamped -- if it
                // didn't, the backend rows and deact_tick would be describing
                // two different SP ends.
                for (const BackendSqueeze& b : act.backends) {
                    if (!b.offset_ms.has_value()) continue;
                    const int64_t implied =
                        *b.offset_ms == 0.0
                            ? b.timecode.ticks()
                            : std::llround(song.timing().ms_index().tick_at_ms(
                                  b.timecode.ms() - *b.offset_ms));
                    if (implied != *act.deact_tick) {
                        d = "backend-implied deact tick disagrees with the stored one";
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
                bool has_sqout = false;
                for (const SPSqueeze& sq : act.sqinouts)
                    if (sq.kind == SqueezeKind::SqOut) has_sqout = true;
                if (!has_sqout) continue;
                ++sqout_acts;

                // The engine stamps the squeezed-out chord's tick (record
                // v6); everything is compared by tick, never by ms.
                if (!act.sqout_tick) {
                    d = "squeeze-out activation missing sqout_tick";
                    break;
                }
                int on_sqout = 0;
                for (const BackendSqueeze& b : act.backends) {
                    if (b.timecode.ticks() > *act.sqout_tick) {
                        d = "backend past the sqout note";
                        break;
                    }
                    if (b.timecode.ticks() == *act.sqout_tick) ++on_sqout;
                }
                if (d.empty() && on_sqout != 1)
                    d = "not exactly one backend row on the sqout tick";
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
            run_search(graph, EngineOptions{})
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
        run_search(graph, EngineOptions{});

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
    auto deact = activation_deact_tick(act);
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
        run_search(graph, EngineOptions{});

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

    auto deact = activation_deact_tick(act);
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
        run_search(graph, EngineOptions{});
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

    CHECK(activation_deact_tick(ract) == activation_deact_tick(act));

    // deact_tick itself is stored data (blob v4), not something the reader
    // rederives -- so the round trip has to hand back the exact tick the
    // engine stamped, not just an equivalent one.
    REQUIRE(act.deact_tick.has_value());
    CHECK(ract.deact_tick == act.deact_tick);
}

// run_search takes its knobs in one EngineOptions value, so no two flags can
// be swapped at a call site. Each knob must still do its own job.
TEST_CASE("run_search: EngineOptions carries each knob to the engine") {
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

    // The defaults are a plain best-path search: score depth 0, no limit.
    const std::vector<Path> best = run_search(graph, EngineOptions{});
    REQUIRE_FALSE(best.empty());
    REQUIRE_FALSE(best.front().activations.empty());

    // no_skips plus a hard 0 ms limit is exactly the all-0 search.
    EngineOptions allzero;
    allzero.ms_filter = 0.0;
    allzero.no_skips = true;
    allzero.hard_ms_filter = true;
    const std::vector<Path> z = run_search(graph, allzero);
    const std::vector<Path> want_z = search_allzero(graph);
    REQUIRE_FALSE(want_z.empty());
    REQUIRE(z.size() == want_z.size());
    for (size_t i = 0; i < z.size(); ++i) {
        CHECK(z[i].pathstring() == want_z[i].pathstring());
        CHECK(z[i].totalscore() == want_z[i].totalscore());
    }

    // Pinning the best path's activation ticks hands that path back.
    EngineOptions pinned;
    pinned.depth_mode = DepthMode::Points;
    pinned.depth_value = 1'000'000'000;
    std::vector<int64_t> ticks;
    for (const Activation& a : best.front().activations) ticks.push_back(a.timecode->ticks());
    pinned.target_act_ticks = ticks;
    const std::vector<Path> again = run_search(graph, pinned);
    REQUIRE_FALSE(again.empty());
    CHECK(again.front().pathstring() == best.front().pathstring());
    CHECK(again.front().totalscore() == best.front().totalscore());
}

// analyze_chart used to run Clone Hero's 4 bars down its own branch, with the
// graph built a flat 4 bars tall. Every other fixed cap builds the graph only
// as tall as the song has phrases (graph_build_cap). Both give the same
// answer: a song with p phrases never holds more than p bars, so a p-bar
// ceiling clamps nothing a 4-bar ceiling would not. This pins it byte for
// byte through the store's own writer before the branches fold into one.
TEST_CASE("a 4-bar graph built at the song's phrase count stores the same paths") {
    std::vector<Song> songs;
    // Hand-built, three phrases, one of them collected mid-SP.
    songs.push_back(build_tail_song({{0, true, false},
                                     {768, true, false},
                                     {1536},
                                     {2304, false, true},
                                     {3072},
                                     {3840, true, false},
                                     {4608},
                                     {5376},
                                     {6144},
                                     {6720},
                                     {6816}}));
    for (const std::string& path : corpus::chart_paths()) {
        Song s = load_songpath(path, true, true);
        if (!s.is_empty() && s.sp_phrase_count() < kCloneHeroSpCap)
            songs.push_back(std::move(s));
    }

    int compared = 0;
    for (const Song& song : songs) {
        const int build_cap = graph_build_cap(kCloneHeroSpCap, song.sp_phrase_count());
        REQUIRE(build_cap < kCloneHeroSpCap);

        ScoreGraph g_tall(song, kCloneHeroSpCap);
        ScoreGraph g_built(song, build_cap);
        EngineOptions options;
        options.depth_value = 4;
        HydraRecord tall, built;
        tall.sp_cap = kCloneHeroSpCap;
        built.sp_cap = kCloneHeroSpCap;
        tall.paths = run_search(g_tall, options);
        built.paths = run_search(g_built, options);
        tall.allzero_paths = search_allzero(g_tall);
        built.allzero_paths = search_allzero(g_built);

        const store::FlatRecord a = store::flatten_record(tall);
        const store::FlatRecord b = store::flatten_record(built);
        bool same = a.structure == b.structure && a.nodes.size() == b.nodes.size();
        for (size_t i = 0; same && i < a.nodes.size(); ++i)
            same = a.nodes[i].payload == b.nodes[i].payload;
        CHECK_MESSAGE(same, "a song with " << song.sp_phrase_count() << " phrases");
        ++compared;
    }
    MESSAGE("compared " << compared << " songs with fewer than 4 phrases");
}

// ---- SP cap overfill: a phrase collected mid-SP can clamp the end ------
//
// The meter holds a fixed number of bars. Once it's full, a phrase you
// collect while SP is active can't push the end out by a plain 2 measures
// any more -- it gets capped at 2*cap measures past that phrase's own note.
// When the cap wins, the note that pinned the end (not the activation) is
// what the timing warning has to point at, so the search stamps that note's
// tick onto the activation as clamp_tick.

TEST_CASE("SP cap overfill: a mid-SP phrase that clamps records the "
          "collecting note") {
    // Cap 2 bars. Two SP phrases fill the meter before the activation at
    // tick 2304 (meter 2), whose plain end is 4 measures later at 5376.
    // The phrase collected at 3072, mid-SP, wants to push the end out by 2
    // more measures to 4608 + 1536 = ... no -- the pending end simply moves
    // to min(prev_end + 2 measures, 3072 + 2*cap measures). prev_end + 2
    // measures is 5376 + 1536 = 6912; the cap ceiling is 3072 + 4*768 =
    // 6144. The ceiling is smaller, so it wins: the end is pinned to 6144,
    // and clamp_tick records the note that pinned it, 3072.
    Song song = build_tail_song({{0, true, false},
                                 {768, true, false},
                                 {2304, false, true},
                                 {3072, true, false},
                                 {3840},
                                 {4608},
                                 {5376},
                                 {6000}});

    ScoreGraph graph(song, 2);
    std::vector<Path> paths =
        run_search(graph, EngineOptions{});

    const Activation& act = last_act(paths);
    REQUIRE(act.sp_meter.has_value());
    CHECK(*act.sp_meter == 2);
    REQUIRE(act.timecode.has_value());
    CHECK(act.timecode->ticks() == 2304);

    auto deact = activation_deact_tick(act);
    REQUIRE(deact.has_value());
    CHECK(*deact == 6144);

    REQUIRE(act.clamp_tick.has_value());
    CHECK(*act.clamp_tick == 3072);
}

TEST_CASE("SP cap overfill: a mid-SP phrase that only ties the cap does "
          "not clamp") {
    // Same cap and activation as above, but the mid-SP phrase lands at 3840
    // instead of 3072. Now both options land on the same tick: prev_end + 2
    // measures is 5376 + 1536 = 6912, and the cap ceiling is 3840 + 4*768 =
    // 6912 too. A tie means the plain extension wins, not the cap -- so this
    // is not a clamp, and clamp_tick stays unset.
    Song song = build_tail_song({{0, true, false},
                                 {768, true, false},
                                 {2304, false, true},
                                 {3840, true, false},
                                 {4608},
                                 {5376},
                                 {6000},
                                 {6500}});

    ScoreGraph graph(song, 2);
    std::vector<Path> paths =
        run_search(graph, EngineOptions{});

    const Activation& act = last_act(paths);
    REQUIRE(act.sp_meter.has_value());
    CHECK(*act.sp_meter == 2);

    auto deact = activation_deact_tick(act);
    REQUIRE(deact.has_value());
    CHECK(*deact == 6912);

    CHECK_FALSE(act.clamp_tick.has_value());
}

TEST_CASE("SP cap overfill: a later unclamped extension keeps the earlier "
          "clamp_tick") {
    // Same cap and activation, but SP is extended twice. The first mid-SP
    // phrase, at 3072, clamps exactly as in the first case above: the end is
    // pinned to 6144, with clamp_tick 3072. The second phrase, at 5760,
    // wants to move the end again -- the cap ceiling from THAT note would be
    // 5760 + 4*768 = 8832, but the plain +2-measure step from the current
    // end (6144 + 1536 = 7680) is smaller and wins instead. Because this
    // second extension is not itself a clamp, the note that pinned the
    // window stays the first one: clamp_tick is still 3072, even though the
    // end has moved again.
    Song song = build_tail_song({{0, true, false},
                                 {768, true, false},
                                 {2304, false, true},
                                 {3072, true, false},
                                 {3840},
                                 {4608},
                                 {5376},
                                 {5760, true, false},
                                 {6000},
                                 {6768},
                                 {7500}});

    ScoreGraph graph(song, 2);
    std::vector<Path> paths =
        run_search(graph, EngineOptions{});

    const Activation& act = last_act(paths);
    REQUIRE(act.sp_meter.has_value());
    CHECK(*act.sp_meter == 2);

    // The chart ends at 7500, before the SP end at 7680, so this is the same
    // "SP outlasts the chart" case as the tests above: the tail rows are
    // synthesized against the tracked end rather than a real deactivation.
    auto deact = activation_deact_tick(act);
    REQUIRE(deact.has_value());
    CHECK(*deact == 7680);

    REQUIRE(act.clamp_tick.has_value());
    CHECK(*act.clamp_tick == 3072);
}

TEST_CASE("SP cap overfill: a second clamp in the same window replaces "
          "clamp_tick") {
    // Same cap and activation. The phrase at 3072 clamps as in the first
    // case: the end is pinned to 6144. The phrase at 3840 then clamps again:
    // the plain step from the current end is 6144 + 1536 = 7680, but the
    // cap ceiling from 3840 is 3840 + 4*768 = 6912, which is smaller. The
    // end is now pinned by the later note, so clamp_tick moves to 3840.
    Song song = build_tail_song({{0, true, false},
                                 {768, true, false},
                                 {2304, false, true},
                                 {3072, true, false},
                                 {3840, true, false},
                                 {4608},
                                 {5376},
                                 {6000},
                                 {6768},
                                 {7500}});

    ScoreGraph graph(song, 2);
    std::vector<Path> paths =
        run_search(graph, EngineOptions{});

    const Activation& act = last_act(paths);
    REQUIRE(act.timecode.has_value());
    CHECK(act.timecode->ticks() == 2304);

    auto deact = activation_deact_tick(act);
    REQUIRE(deact.has_value());
    CHECK(*deact == 6912);

    REQUIRE(act.clamp_tick.has_value());
    CHECK(*act.clamp_tick == 3840);
}

TEST_CASE("collected phrases: none when no phrase lands during the activation") {
    Song song = build_tail_song({{0, true, false}, {768, true, false}, {1536},
                                 {2304, false, true}, {3072}, {3840}, {4608},
                                 {5136}, {5280}});
    ScoreGraph graph(song, 4);
    // The paths are held in a named vector: last_act returns a reference into
    // it, so it has to outlive the checks below.
    const std::vector<Path> paths = run_search(graph, EngineOptions{});
    const Activation& act = last_act(paths);
    CHECK(act.collected_phrase_ticks.empty());
    CHECK_FALSE(act.sqout_tick.has_value());
}

TEST_CASE("collected phrases: one phrase mid-activation is recorded") {
    // The fixture of "SP past the last note: a mid-activation phrase extends
    // the end": the phrase ending at 3840 is collected while SP is active.
    Song song = build_tail_song({{0, true, false}, {768, true, false}, {1536},
                                 {2304, false, true}, {3072}, {3840, true, false},
                                 {4608}, {5376}, {6144}, {6720}, {6816}});
    ScoreGraph graph(song, 4);
    const std::vector<Path> paths = run_search(graph, EngineOptions{});
    const Activation& act = last_act(paths);
    CHECK(act.collected_phrase_ticks == std::vector<int64_t>{3840});
}

TEST_CASE("collected phrases: two phrases under a full meter are both recorded, in order") {
    // The cap-2 clamp fixture: both phrases are collected while active, and
    // the second is the note the cap pinned the end to. A clamped phrase
    // counts as collected.
    Song song = build_tail_song({{0, true, false}, {768, true, false},
                                 {2304, false, true}, {3072, true, false},
                                 {3840, true, false}, {4608}, {5376}, {6000},
                                 {6768}, {7500}});
    ScoreGraph graph(song, 2);
    std::vector<Path> paths = run_search(graph, EngineOptions{});
    REQUIRE(!paths.empty());
    const Activation& act = paths.front().activations.front();
    CHECK(act.collected_phrase_ticks == std::vector<int64_t>{3072, 3840});
    REQUIRE(act.clamp_tick.has_value());
    CHECK(act.collected_phrase_ticks.back() == *act.clamp_tick);
}

TEST_CASE("collected phrases: the corpus agrees with the squeezes and the SP end") {
    int sqouts_seen = 0;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        ScoreGraph graph(song, 4);
        for (const Path& p : run_search(graph, EngineOptions{DepthMode::Scores, 1})) {
            for (const Activation& act : p.all_activations()) {
                REQUIRE(act.timecode.has_value());
                bool took_sqout = false;
                for (const SPSqueeze& sq : act.sqinouts)
                    if (sq.kind == SqueezeKind::SqOut) took_sqout = true;
                // sqout_tick is set exactly when the activation squeezed out.
                CHECK(act.sqout_tick.has_value() == took_sqout);
                if (act.sqout_tick) ++sqouts_seen;

                int64_t prev = -1;
                for (int64_t t : act.collected_phrase_ticks) {
                    CHECK(t > prev);  // strictly ascending
                    CHECK(t >= act.timecode->ticks());
                    // A squeezed-out phrase, and anything after it, was
                    // never collected.
                    if (act.sqout_tick) CHECK(t < *act.sqout_tick);
                    prev = t;
                }
            }
        }
    }
    CHECK(sqouts_seen > 0);
}

TEST_CASE("path codec: encode/decode a path node keeps clamp_tick") {
    // clamp_tick is the newest field on Activation (blob v5 / node v3): a
    // plain node round trip has to carry it, the same way the deact_tick
    // round trip above pins the field before it.
    Activation act;
    act.timecode = Timecode::raw(2304);
    act.clamp_tick = 3072;

    Path path;
    path.activations.push_back(act);

    Path decoded = store::decode_path_node(store::encode_path_node(path));
    REQUIRE(decoded.activations.size() == 1);
    REQUIRE(decoded.activations.front().clamp_tick.has_value());
    CHECK(*decoded.activations.front().clamp_tick == 3072);
}

TEST_CASE("graph_build_cap: never taller than the song's phrases, never below one") {
    CHECK(graph_build_cap(4, 10) == 4);   // the cap binds
    CHECK(graph_build_cap(32, 3) == 3);   // the song's phrases bind
    CHECK(graph_build_cap(8, 0) == 1);    // a phraseless song still builds one level
}
