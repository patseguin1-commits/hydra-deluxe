// Replay invariants: core/replay.h must price a path exactly as the engine
// priced it.
//
// The engine sums scores along graph edges and never looks at a chord; the
// replay walks chords and never looks at the graph. If the two agree on all
// six score categories, for every path of every corpus chart, then the
// replay's three rules (one SP-free combo counter, an inclusive SP window
// with a 3 ms backend leeway, undoubled solos) are the engine's rules.

#include "doctest.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"

#include "app/analysis.h"
#include "app/config.h"
#include "core/model.h"
#include "core/replay.h"
#include "core/scoring.h"
#include "core/timing.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/engine.h"
#include "core/model.h"  // kSqueezeWindowMs, the horizon the warning uses
#include "search/pather.h"

using namespace hydra;
using json = nlohmann::json;

TEST_CASE("replay reproduces the engine's score for every corpus path") {
    int charts = 0, paths = 0, mismatches = 0;
    std::string first_diff;

    // The GUI's defaults, straight from app::Settings rather than five
    // hand-written literals: cap 4, score range 4, 10 ms.
    const app::AnalysisSettings cfg = app::Settings().to_analysis_settings();

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, cfg.prodrums, cfg.bass2x, cfg.difficulty);
        if (song.is_empty()) continue;
        ++charts;

        HydraRecord rec = analyze_chart(song, cfg);

        for (const Path* p : rec.all_paths()) {
            ++paths;
            const PathReplay pr = replay_stored_path(song, *p);
            REQUIRE(pr.all_windows());

            if (!pr.totals_match()) {
                ++mismatches;
                if (first_diff.empty())
                    first_diff = path + " [" + p->pathstring() + "]: replay " +
                                 std::to_string(pr.result.final.total()) + " vs stored " +
                                 std::to_string(p->totalscore());
            }
        }
    }

    CHECK(charts > 0);
    CHECK(paths > 0);
    INFO("first mismatch: " << first_diff);
    CHECK(mismatches == 0);
}

// The combo the game's counter shows once a chord is hit. The Preview's score
// box reads it straight off the replay rather than adding note counts itself.
TEST_CASE("replay: combo_after is the combo once the chord is hit") {
    Song song(480);
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    const std::vector<std::vector<NoteColor>> chords = {
        {NoteColor::Red},
        {NoteColor::Red, NoteColor::Yellow},
        {NoteColor::Kick, NoteColor::Blue, NoteColor::Green}};
    int64_t tick = 0;
    for (const std::vector<NoteColor>& colors : chords) {
        SongTimestamp ts;
        ts.timecode = song.timecode(tick);
        for (NoteColor c : colors) ts.chord.add_note(c);
        song.sequence.push_back(std::move(ts));
        tick += 480;
    }

    const ReplayResult r = replay_path(song, {});
    REQUIRE(r.chords.size() == 3);
    CHECK(r.chords[0].combo_before == 0);
    CHECK(r.chords[0].combo_after == 1);
    CHECK(r.chords[1].combo_before == 1);
    CHECK(r.chords[1].combo_after == 3);
    CHECK(r.chords[2].combo_before == 3);
    CHECK(r.chords[2].combo_after == 6);
}

// A targeted search is only useful if it gives back the same path the ordinary
// search would have found. So take every path the ordinary search DID find,
// hand its activation ticks back to search_target, and require the engine to
// price it identically -- same total, same deactivation node per activation,
// same squeezes. That is the whole contract: "activate exactly here" must not
// change how the engine scores what happens next.
TEST_CASE("targeted search reproduces every corpus path") {
    int charts = 0, paths = 0, mismatches = 0;
    std::string first_diff;

    const app::AnalysisSettings cfg = app::Settings().to_analysis_settings();

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, cfg.prodrums, cfg.bass2x, cfg.difficulty);
        if (song.is_empty()) continue;
        ++charts;

        HydraRecord rec = analyze_chart(song, cfg);

        for (const Path* p : rec.all_paths()) {
            ++paths;
            const std::vector<Activation> want_acts = p->all_activations();

            std::vector<int64_t> ticks;
            bool have_ticks = true;
            for (const Activation& act : want_acts) {
                if (!act.timecode) { have_ticks = false; break; }
                ticks.push_back(act.timecode->ticks());
            }
            REQUIRE(have_ticks);

            const std::vector<Path> got = search_target(song, cfg, ticks);

            // Somewhere in the returned variants must be this exact path.
            const Path* match = nullptr;
            HydraRecord holder;
            holder.paths = got;
            for (const Path* q : holder.all_paths()) {
                if (q->totalscore() != p->totalscore()) continue;
                const std::vector<Activation> qa = q->all_activations();
                if (qa.size() != want_acts.size()) continue;
                bool same = true;
                for (size_t i = 0; i < qa.size() && same; ++i) {
                    if (qa[i].deact_tick != want_acts[i].deact_tick) same = false;
                    if (qa[i].sqinouts.size() != want_acts[i].sqinouts.size())
                        same = false;
                    for (size_t k = 0; k < qa[i].sqinouts.size() && same; ++k) {
                        if (qa[i].sqinouts[k].kind != want_acts[i].sqinouts[k].kind ||
                            qa[i].sqinouts[k].offset_ms !=
                                want_acts[i].sqinouts[k].offset_ms)
                            same = false;
                    }
                }
                if (same) { match = q; break; }
            }

            if (!match) {
                ++mismatches;
                if (first_diff.empty())
                    first_diff = path + " [" + p->pathstring() + "] score " +
                                 std::to_string(p->totalscore()) + ": " +
                                 std::to_string(holder.all_paths().size()) +
                                 " targeted path(s), none matching";
                continue;
            }

            // The recovered path also has to replay to its own score, which is
            // the invariant the first test pins for search-found paths.
            CHECK(replay_stored_path(song, *match).totals_match());
        }
    }

    CHECK(charts > 0);
    REQUIRE(paths >= 300);
    INFO("first mismatch: " << first_diff);
    CHECK(mismatches == 0);
}

// A tick that is not an activation fill cannot be honoured, and the engine says
// so by giving back nothing rather than quietly pricing a different path.
TEST_CASE("targeted search rejects a tick that is not a fill") {
    const app::AnalysisSettings cfg = app::Settings().to_analysis_settings();

    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, cfg.prodrums, cfg.bass2x, cfg.difficulty);
        if (song.is_empty()) continue;
        HydraRecord rec = analyze_chart(song, cfg);
        if (rec.paths.empty()) continue;
        const std::vector<Activation> acts = rec.best_path().all_activations();
        if (acts.empty() || !acts[0].timecode) continue;

        // One tick past a real activation fill: the fill node lives on the
        // tick itself, so tick + 1 is never one.
        const int64_t bogus = acts[0].timecode->ticks() + 1;
        CHECK(search_target(song, cfg, {bogus}).empty());
        return;
    }
    FAIL("no corpus chart with an activation to build the negative case from");
}

TEST_CASE("replay without Star Power scores no doubling at all") {
    Song song = load_songpath(corpus::first_chart_with_suffix(".mid"), true, true);
    REQUIRE_FALSE(song.is_empty());

    const ReplayResult r = replay_path(song, {});
    CHECK(r.final.sp == 0);
    CHECK(r.final.base > 0);
    CHECK(r.chords.size() == song.sequence.size());

    // The running totals are a prefix sum of the per-chord points, and the
    // on-screen total only ever lags the real one (a solo's bonus is withheld
    // until the run ends, never paid early).
    ReplayScore running;
    for (const ReplayChord& c : r.chords) {
        running.add(c.points);
        CHECK(c.cum.total() == running.total());
        CHECK(c.cum_onscreen_total <= c.cum.total());
    }
    CHECK(r.final.total() == running.total());
    CHECK(r.chords.back().cum_onscreen_total == r.final.total());
}

// Round and Round's shape on a hand-built chart: a window whose squeeze-out
// sits on an R+Y phrase chord ~479 ms past the SP end. That chord is outside
// the window and outside the leeway, so it earns no doubling at all -- the
// squeeze-out changes nothing. The chord on the deactivation node is paid.
TEST_CASE("a squeezed-out chord past the leeway earns nothing") {
    // 4/4, 120 BPM, 192 ticks per beat: 768 ticks and 2000 ms per measure.
    Song song(192);
    song.tpm_changes[0] = 768;
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (int64_t tick : {0, 768, 1536, 2304, 3072, 3256}) {
        SongTimestamp ts;
        ts.timecode = song.timecode(tick);
        ts.chord.add_note(NoteColor::Red);
        ts.chord.add_note(NoteColor::Yellow);
        ts.flag_sp = tick == 3256;
        song.sequence.push_back(ts);
    }

    ReplayWindow w;
    w.act_tick = 0;
    w.deact_tick = 3072;
    w.sqout_offset_ms = song.timecode(3256).ms() - song.timecode(3072).ms();
    w.sqout_tick = 3256;

    const ReplayResult r = replay_path(song, {w});
    REQUIRE(r.chords.size() == 6);
    CHECK(r.chords[4].in_sp);  // on the deactivation node: paid in full
    CHECK(r.chords[4].points.sp > 0);
    CHECK_FALSE(r.chords[5].in_sp);
    CHECK(r.chords[5].points.sp == 0);

    // The disc follows the same decision: doubled on the paid chord only.
    CHECK(r.chords[4].multiplier_shown == r.chords[4].multiplier_after * kStarPowerMultiplier);
    CHECK(r.chords[5].multiplier_shown == r.chords[5].multiplier_after);
}

TEST_CASE("shown_multiplier doubles the combo multiplier only under Star Power") {
    CHECK(shown_multiplier(1, false) == 1);
    CHECK(shown_multiplier(1, true) == 2);
    CHECK(shown_multiplier(4, false) == 4);
    CHECK(shown_multiplier(4, true) == 8);
}

// `score --path` prices a path straight out of the JSON `dump` and `target`
// write. The reason to read the file rather than retype the path as an
// "act:deact,..." string is that the file carries the squeeze-out offset and
// a retyped string usually drops it -- and without the offset the squeezed
// note is doubled as if Star Power were still running, so the score comes out
// high. So what has to be pinned is that the offset survives the trip.
TEST_CASE("a path JSON becomes windows with the squeeze-out offset intact") {
    const json path = json::parse(R"({
      "index": 0,
      "activations": [
        {"act_tick": 480, "deact_tick": 3840, "sqinouts": []},
        {"act_tick": 7680, "deact_tick": 11520, "sqout_tick": 11532,
         "sqinouts": [{"kind": "SqIn",  "offset_ms": 12.5},
                      {"kind": "SqOut", "offset_ms": 31.25}]}
      ]})");

    const std::vector<ReplayWindow> w = windows_from_json(path);
    REQUIRE(w.size() == 2);
    CHECK(w[0].act_tick == 480);
    CHECK(w[0].deact_tick == 3840);
    CHECK_FALSE(w[0].sqout_offset_ms.has_value());

    CHECK(w[1].act_tick == 7680);
    CHECK(w[1].deact_tick == 11520);
    REQUIRE(w[1].sqout_offset_ms.has_value());
    // The SqIn sits in the same list and must not be mistaken for the SqOut.
    CHECK(*w[1].sqout_offset_ms == doctest::Approx(31.25));
    CHECK_FALSE(w[0].sqout_tick.has_value());
    REQUIRE(w[1].sqout_tick.has_value());
    CHECK(*w[1].sqout_tick == 11532);

    // JSON that is not a path at all says so rather than scoring something.
    CHECK_THROWS(windows_from_json(json::object()));
    // -1 is how a dump writes "this record has no deactivation node".
    CHECK_THROWS(windows_from_json(json::parse(
        R"({"activations": [{"act_tick": 480, "deact_tick": -1}]})")));
}

// A path read from a file has to give the same windows as the same path read
// from the record it was dumped from, or `--path` would price something the
// database does not agree with.
TEST_CASE("windows read from a path JSON match the ones read from the record") {
    const app::AnalysisSettings cfg = app::Settings().to_analysis_settings();

    int checked = 0;
    for (const std::string& chart : corpus::chart_paths()) {
        Song song = load_songpath(chart, cfg.prodrums, cfg.bass2x, cfg.difficulty);
        if (song.is_empty()) continue;
        HydraRecord rec = analyze_chart(song, cfg);
        if (rec.paths.empty()) continue;

        const std::vector<const Path*> all = rec.all_paths();
        const json dumped = paths_json(all, song.timing());
        REQUIRE(dumped.size() == all.size());
        for (size_t k = 0; k < all.size(); ++k) {
            const Path* p = all[k];
            const std::vector<ReplayWindow> want = windows_for_path(*p);
            if (want.empty()) continue;

            // The real dump producer, so a format change on either side fails.
            const std::vector<ReplayWindow> got = windows_from_json(dumped[k]);
            REQUIRE(got.size() == want.size());
            for (size_t i = 0; i < got.size(); ++i) {
                CHECK(got[i].act_tick == want[i].act_tick);
                CHECK(got[i].deact_tick == want[i].deact_tick);
                CHECK(got[i].sqout_offset_ms == want[i].sqout_offset_ms);
                CHECK(got[i].sqout_tick == want[i].sqout_tick);
            }
            ++checked;
        }
        if (checked > 0) break;  // one chart's paths are the whole contract
    }
    CHECK(checked > 0);
}

// fcvideo and `hydra_replay score --path` read these fields out of a dump. A
// dump-format change that drops one has to fail here, not in the video tools.
TEST_CASE("paths_json writes every field the dump readers use") {
    const app::AnalysisSettings cfg = app::Settings().to_analysis_settings();

    bool checked = false;
    for (const std::string& chart : corpus::chart_paths()) {
        Song song = load_songpath(chart, cfg.prodrums, cfg.bass2x, cfg.difficulty);
        if (song.is_empty()) continue;
        HydraRecord rec = analyze_chart(song, cfg);
        const std::vector<const Path*> all = rec.all_paths();
        if (all.empty() || all[0]->all_activations().empty()) continue;

        const json dumped = paths_json(all, song.timing());
        REQUIRE(dumped.size() == all.size());

        const json& p0 = dumped[0];
        for (const char* k : {"index", "pathstring", "total", "score", "activations"})
            CHECK_MESSAGE(p0.contains(k), k);
        for (const char* k : {"base", "combo", "sp", "solo", "accent", "ghost"})
            CHECK_MESSAGE(p0["score"].contains(k), k);
        CHECK(p0["pathstring"].get<std::string>() == all[0]->pathstring());

        REQUIRE(!p0["activations"].empty());
        const json& a0 = p0["activations"][0];
        for (const char* k : {"act_tick", "deact_tick", "sqout_tick", "nominal_deact_tick",
                              "sp_meter", "skips", "chord_code", "sqinouts"})
            CHECK_MESSAGE(a0.contains(k), k);

        checked = true;
        break;  // one chart's first path is the whole contract
    }
    CHECK(checked);
}

// A window that ends on the note closing a Star Power phrase is exactly where
// a squeeze-out hides. If the player squeezed, that note was hit after Star
// Power ran out and is not doubled; if they did not, it is. The window list
// alone cannot tell the two apart, so the tool says so instead of guessing.
TEST_CASE("a window ending on a phrase note with no offset is flagged") {
    Song song = load_songpath(corpus::first_chart_with_suffix(".mid"), true, true);
    REQUIRE_FALSE(song.is_empty());

    const ReplayResult r = replay_path(song, {});

    // The first phrase note, the chord just after it, the first chord that is
    // further past a phrase note than a squeeze could ever reach, and a chord
    // with no phrase note behind it at all.
    const ReplayChord* phrase_note = nullptr;    // the first phrase note
    const ReplayChord* just_after = nullptr;     // the chord right after it
    const ReplayChord* long_after = nullptr;     // first chord out of reach
    const ReplayChord* no_phrase_yet = nullptr;  // a chord before any of them
    const ReplayChord* last_phrase = nullptr;
    for (const ReplayChord& c : r.chords) {
        if (c.is_sp_phrase_end) {
            last_phrase = &c;
            if (!phrase_note) phrase_note = &c;
            continue;
        }
        if (!last_phrase) {
            if (!no_phrase_yet) no_phrase_yet = &c;
            continue;
        }
        if (last_phrase == phrase_note && !just_after &&
            c.ms - phrase_note->ms < kSqueezeWindowMs)
            just_after = &c;
        if (!long_after && c.ms - last_phrase->ms > kSqueezeWindowMs)
            long_after = &c;
    }
    REQUIRE(phrase_note != nullptr);
    REQUIRE(just_after != nullptr);
    REQUIRE(long_after != nullptr);
    REQUIRE(no_phrase_yet != nullptr);

    ReplayWindow w;
    w.act_tick = r.chords.front().tick;

    // The warning only fires for a chord the window paid, so it reads the
    // replay of that same window, as `hydra_replay score` does.
    auto warnings_for = [&](const ReplayWindow& win) {
        return ambiguous_window_warnings(song, replay_path(song, {win}), {win});
    };

    // Ending on the phrase note itself.
    w.deact_tick = phrase_note->tick;
    const std::vector<std::string> flagged = warnings_for(w);
    REQUIRE(flagged.size() == 1);
    CHECK(flagged[0].find(std::to_string(phrase_note->tick)) !=
          std::string::npos);

    // Ending a hair after it: still the same doubt. This is the case a rule
    // that only looked at the deactivation tick itself would miss, and it is
    // a real one -- five of Hail The Sun - Wake's six squeeze-outs sit on the
    // node and the sixth sits 93.75 ms before it.
    w.deact_tick = just_after->tick;
    CHECK(warnings_for(w).size() == 1);

    // Far enough past it that no squeeze could have reached: no doubt left.
    w.deact_tick = long_after->tick;
    CHECK(warnings_for(w).empty());

    // No phrase note in the window at all: never in doubt.
    w.deact_tick = no_phrase_yet->tick;
    CHECK(warnings_for(w).empty());

    // An offset settles the question, so there is nothing left to warn about.
    ReplayWindow settled;
    settled.act_tick = r.chords.front().tick;
    settled.deact_tick = phrase_note->tick;
    settled.sqout_offset_ms = 8.0;
    CHECK(ambiguous_window_warnings(song, r, {settled}).empty());
}

TEST_CASE("per-note sp points sum to the chord's sp points") {
    Song song = load_songpath(corpus::first_chart_with_suffix(".mid"), true, true);
    REQUIRE_FALSE(song.is_empty());

    // One window over the whole chart, so every chord is under Star Power and
    // its points.sp is the full doubling.
    ReplayWindow w;
    w.act_tick = 0;
    w.deact_tick = song.sequence.back().timecode.ticks();
    const ReplayResult r = replay_path(song, {w});

    for (const ReplayChord& c : r.chords) {
        CHECK(c.in_sp);
        int64_t sum = 0;
        for (const ReplayNote& n : c.notes) sum += n.sp_points;
        CHECK(sum == c.points.sp);
        CHECK(static_cast<int>(c.notes.size()) > 0);
    }
}

namespace {

// 4/4, 120 BPM, 192 ticks per beat: 768 ticks and 2000 ms per measure, so
// 36 ticks are exactly 93.75 ms and 192 ticks exactly 500 ms.
Song song_with(const std::vector<std::pair<int64_t, bool>>& chords) {
    Song song(192);
    song.tpm_changes[0] = 768;
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (const auto& [tick, phrase] : chords) {
        SongTimestamp ts;
        ts.timecode = song.timecode(tick);
        ts.chord.add_note(NoteColor::Red);
        ts.chord.add_note(NoteColor::Yellow);
        ts.flag_sp = phrase;
        song.sequence.push_back(ts);
    }
    return song;
}

// One chord per beat at 120 BPM; chord i holds the colors listed at i.
Song make_chord_song(const std::vector<std::vector<NoteColor>>& chords) {
    Song song(480);
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (size_t i = 0; i < chords.size(); ++i) {
        SongTimestamp ts;
        ts.timecode = song.timecode(480 * static_cast<int64_t>(i));
        for (NoteColor c : chords[i]) ts.chord.add_note(c);
        song.sequence.push_back(ts);
    }
    return song;
}

// Adds a dynamic Yellow cymbal to `chord`.
void add_dynamic_cymbal(Chord& chord, NoteDynamicType dyn) {
    ChordNote& y = chord.add_note(NoteColor::Yellow);
    y.cymbaltype = NoteCymbalType::Cymbal;
    y.dynamictype = dyn;
}

}  // namespace

// A typed offset is only ever an approximation of a chord that sits on a
// tick. The tool resolves it to the phrase chord it means and says which.
TEST_CASE("a typed squeeze-out offset resolves to the phrase chord") {
    const Song song = song_with(
        {{0, false}, {768, false}, {1536, false}, {3036, true}, {3072, false}});

    ReplayWindow typed;
    typed.act_tick = 0;
    typed.deact_tick = 3072;
    typed.sqout_offset_ms = -93.73;  // what a person copies off a screen

    const SqOutNote n = resolve_sqout_note(song, typed);
    CHECK(n.tick == 3036);
    CHECK(n.offset_ms == doctest::Approx(-93.75));

    typed.sqout_tick = n.tick;
    ReplayWindow exact;
    exact.act_tick = 0;
    exact.deact_tick = 3072;
    exact.sqout_tick = 3036;
    CHECK(replay_path(song, {typed}).final == replay_path(song, {exact}).final);

    // No phrase chord within 500 ms of the SP end: nothing to resolve to.
    const Song bare = song_with({{0, false}, {3072, false}});
    CHECK_THROWS(resolve_sqout_note(bare, typed));

    // replay_path refuses an offset it was never told the chord for.
    ReplayWindow unresolved;
    unresolved.act_tick = 0;
    unresolved.deact_tick = 3072;
    unresolved.sqout_offset_ms = -93.75;
    CHECK_THROWS(replay_path(song, {unresolved}));
}

// The engine only ever squeezes out the first phrase chord strictly within
// 500 ms of the SP end. A typed offset that lands on a later one names a
// squeeze-out the search can never produce, so it is refused and nothing is
// priced (user decision 23).
TEST_CASE("a typed squeeze-out on a chord the engine never squeezes out is refused") {
    // Phrase chords 375 ms (tick 2928) and 93.75 ms (tick 3036) before D.
    const Song two = song_with({{0, false}, {768, false}, {2928, true},
                                {3036, true}, {3072, false}});

    ReplayWindow late;
    late.act_tick = 0;
    late.deact_tick = 3072;
    late.sqout_offset_ms = -93.73;
    CHECK_THROWS_WITH_AS(
        resolve_sqout_note(two, late),
        "window 0:3072: the SqOut offset -93.73 ms lands on the phrase chord "
        "at tick 3036 (-93.75 ms from the SP end), which the engine never "
        "squeezes out. The only chord it can squeeze out here is the first "
        "phrase chord within 500 ms of the SP end, at tick 2928 (-375.00 ms). "
        "Not priced.",
        std::runtime_error);

    // The engine's own chord is accepted.
    ReplayWindow first = late;
    first.sqout_offset_ms = -375.0;
    const SqOutNote n = resolve_sqout_note(two, first);
    CHECK(n.tick == 2928);
    CHECK(n.offset_ms == doctest::Approx(-375.0));
}

// The graph lets a deactivation squeeze out exactly one chord: the first
// phrase chord strictly within 500 ms of the SP end (graph.cpp
// add_deact_edge, then store_new_backend for chords after the end). The
// warning names that chord, and only when the window actually paid it.
TEST_CASE("the squeeze-out warning names the chord the graph would squeeze") {
    // Phrase chords 375 ms (tick 2928) and 125 ms (tick 3024) before D.
    const Song two = song_with({{0, false}, {768, false}, {2928, true},
                                {3024, true}, {3072, false}});
    ReplayWindow w;
    w.act_tick = 0;
    w.deact_tick = 3072;
    const ReplayResult r = replay_path(two, {w});
    const std::vector<std::string> warned = ambiguous_window_warnings(two, r, {w});
    REQUIRE(warned.size() == 1);
    CHECK(warned[0].find("tick 2928") != std::string::npos);

    // Exactly 500 ms before D is outside the graph's window: no warning.
    const Song edge = song_with({{0, false}, {768, false}, {2880, true},
                                 {3072, false}});
    const ReplayResult re = replay_path(edge, {w});
    CHECK(ambiguous_window_warnings(edge, re, {w}).empty());

    // A phrase chord one tick (2.6 ms) after D is inside the leeway, so the
    // window paid it and squeezing it out would change the score.
    const Song after = song_with({{0, false}, {768, false}, {3072, false},
                                  {3073, true}});
    const ReplayResult ra = replay_path(after, {w});
    const std::vector<std::string> late = ambiguous_window_warnings(after, ra, {w});
    REQUIRE(late.size() == 1);
    CHECK(late[0].find("tick 3073") != std::string::npos);
}

TEST_CASE("category_scores reports the multiplier each note was paid at") {
    Chord one;
    one.add_note(NoteColor::Red);
    CHECK(category_scores(one, 8).multiplier == 1);  // the 9th note: 1x
    CHECK(category_scores(one, 9).multiplier == 2);  // the 10th note: 2x
    CHECK(category_scores(one, 19).multiplier == 3);
    CHECK(category_scores(one, 29).multiplier == 4);
    CHECK(category_scores(one, 40).multiplier == 4);
    CHECK(category_scores(one, 9).multiplier_after == 2);

    // A two-note chord after 8 notes of combo straddles the step: its first
    // note is the 9th (1x) and its second the 10th (2x).
    Chord two;
    two.add_note(NoteColor::Red);
    two.add_note(NoteColor::Kick);
    std::vector<CategoryScores> per_note;
    const CategoryScores s = category_scores(two, 8, &per_note);
    CHECK(s.multiplier == 1);
    CHECK(s.multiplier_after == 2);
    CHECK(s.combo == 50);  // only the second note earns combo points
    REQUIRE(per_note.size() == 2);
    CHECK(per_note[0].multiplier == 1);
    CHECK(per_note[1].multiplier == 2);
}

TEST_CASE("category_scores prices a dynamic cymbal's bonus at its own note") {
    // A kick and a ghost cymbal after 9 notes of combo: both notes are at 2x.
    // The cymbal's dynamics are the pad's 50 plus the cymbal's 15, times 2.
    // The chord's ghost category holds only the raw 50.
    Chord chord;
    chord.add_note(NoteColor::Kick);
    add_dynamic_cymbal(chord, NoteDynamicType::Ghost);
    std::vector<CategoryScores> per_note;
    const CategoryScores s = category_scores(chord, 9, &per_note);
    CHECK(s.ghost == 50);

    const std::vector<ChordNote> order = chord.notes(true);
    REQUIRE(order.size() == 2);
    REQUIRE(per_note.size() == 2);
    for (size_t k = 0; k < order.size(); ++k) {
        if (order[k].colortype == NoteColor::Yellow) {
            CHECK(per_note[k].dynamics_bonus == 130);  // (50 + 15) x 2
            // Without its dynamics the cymbal pays a plain 2x cymbal: 130.
            CHECK(per_note[k].sp - per_note[k].dynamics_bonus == 130);
        } else {
            CHECK(per_note[k].dynamics_bonus == 0);
        }
    }
}

TEST_CASE("replay reports the multipliers category_scores applied") {
    // Eight Red singles, a Red+Kick chord that straddles 10, then eleven more
    // singles so a later chord crosses 20 on its own. The audit's Evans Blue,
    // Beg case is the single-note crossing: paid at 2x, reported as 1.
    std::vector<std::vector<NoteColor>> chords(8, {NoteColor::Red});
    chords.push_back({NoteColor::Red, NoteColor::Kick});
    for (int i = 0; i < 11; ++i) chords.push_back({NoteColor::Red});
    const ReplayResult r = replay_path(make_chord_song(chords), {});
    REQUIRE(r.chords.size() == 20);

    // The straddling chord: first note 1x, second note 2x.
    const ReplayChord& straddle = r.chords[8];
    CHECK(straddle.combo_before == 8);
    CHECK(straddle.multiplier == 1);
    CHECK(straddle.multiplier_after == 2);
    CHECK(straddle.points.combo == 50);
    REQUIRE(straddle.notes.size() == 2);
    CHECK(straddle.notes[0].multiplier == 1);
    CHECK(straddle.notes[1].multiplier == 2);

    // The chord before it leaves the disc at 1x.
    CHECK(r.chords[7].multiplier_after == 1);

    // A single note that is the 20th: paid at 3x. The old field said 2.
    const ReplayChord& third = r.chords[18];
    CHECK(third.combo_before == 19);
    CHECK(third.multiplier == 3);
    CHECK(third.multiplier_after == 3);
    CHECK(third.notes[0].multiplier == 3);
    CHECK(third.points.combo == 100);  // 50 base x (3 - 1)
    CHECK(r.chords[17].multiplier_after == 2);
}

TEST_CASE("replay copies each note's dynamics bonus") {
    Song song = make_chord_song({{NoteColor::Red}});
    add_dynamic_cymbal(song.sequence[0].chord, NoteDynamicType::Accent);
    const ReplayResult r = replay_path(song, {});
    REQUIRE(r.chords.size() == 1);
    REQUIRE(r.chords[0].notes.size() == 2);
    for (const ReplayNote& n : r.chords[0].notes)
        CHECK(n.dynamics_bonus == (n.color == NoteColor::Yellow ? 65 : 0));
    // The accent category holds only the pad part; the note field holds all.
    CHECK(r.chords[0].points.accent == 50);
}

TEST_CASE("replay names each note's dynamic") {
    // A kick, a ghost Red pad and an accent Yellow cymbal in one chord: each
    // note reports its own kind, so a mixed chord can be worded per note.
    Song song = make_chord_song({{NoteColor::Kick}});
    Chord& chord = song.sequence[0].chord;
    chord.add_note(NoteColor::Red).dynamictype = NoteDynamicType::Ghost;
    add_dynamic_cymbal(chord, NoteDynamicType::Accent);
    const ReplayResult r = replay_path(song, {});
    REQUIRE(r.chords.size() == 1);
    REQUIRE(r.chords[0].notes.size() == 3);
    for (const ReplayNote& n : r.chords[0].notes) {
        if (n.color == NoteColor::Red)
            CHECK(n.dynamic == NoteDynamicType::Ghost);
        else if (n.color == NoteColor::Yellow)
            CHECK(n.dynamic == NoteDynamicType::Accent);
        else
            CHECK(n.dynamic == NoteDynamicType::Normal);
    }
    CHECK(dynamic_str(NoteDynamicType::Ghost) == "ghost");
    CHECK(dynamic_str(NoteDynamicType::Accent) == "accent");
    CHECK(dynamic_str(NoteDynamicType::Normal) == "none");
}

TEST_CASE("replay multipliers agree with the combo on every corpus chord") {
    Song song = load_songpath(corpus::first_chart_with_suffix(".mid"), true, true);
    REQUIRE_FALSE(song.is_empty());
    const ReplayResult r = replay_path(song, {});

    for (size_t i = 0; i < r.chords.size(); ++i) {
        const ReplayChord& c = r.chords[i];
        REQUIRE_FALSE(c.notes.empty());
        // The chord's multiplier is its first note's; multiplier_after is its
        // last note's.
        CHECK(c.multiplier == c.notes.front().multiplier);
        CHECK(c.multiplier_after == c.notes.back().multiplier);
        CHECK(c.multiplier == to_multiplier(c.combo_before + 1));
        // What the disc shows after this chord is what the next chord starts
        // from: the old field's value on the next chord.
        if (i + 1 < r.chords.size())
            CHECK(c.multiplier_after == to_multiplier(r.chords[i + 1].combo_before));
        // A note's dynamics bonus is part of what it pays, never more.
        for (const ReplayNote& n : c.notes) {
            CHECK(n.dynamics_bonus >= 0);
            CHECK(n.dynamics_bonus < n.sp_points);
        }
    }
}

TEST_CASE("replay score fields: one list in schema order") {
    REQUIRE(std::size(kReplayScoreFields) == 6);
    const char* want[] = {"base", "combo", "sp", "solo", "accent", "ghost"};
    for (size_t i = 0; i < 6; ++i) CHECK(std::string(kReplayScoreFields[i].name) == want[i]);

    ReplayScore s;
    s.base = 1; s.combo = 2; s.sp = 3; s.solo = 4; s.accent = 5; s.ghost = 6;
    for (size_t i = 0; i < 6; ++i)
        CHECK(s.*(kReplayScoreFields[i].member) == static_cast<int64_t>(i + 1));
}
