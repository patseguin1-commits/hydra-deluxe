// Tests for core::Rules and hydra_rules.ini (app/rules_file.h).
//
// Every rule the user chose by hand lives in one Rules value. Defaults must
// equal the values Hydra always used, and each non-default value must
// actually change the behavior it names.

#include "doctest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "app/config.h"
#include "app/rules_file.h"
#include "core/model.h"
#include "core/rules.h"
#include "core/scoring.h"
#include "corpus_util.h"
#include "parse/song.h"
#include "search/engine.h"
#include "search/graph.h"
#include "search/pather.h"

using namespace hydra;

namespace {

std::filesystem::path write_rules(const char* tag, const std::string& text) {
    std::filesystem::path p =
        std::filesystem::temp_directory_path() / (std::string("hydra_rules_") + tag + ".ini");
    std::ofstream f(p, std::ios::trunc);
    f << text;
    return p;
}

// One measure per note on a 4/4 120 BPM song with no authored fills, so
// check_activations has to generate them.
Song fill_song() {
    Song song(192);
    song.tpm_changes[0] = 768;
    song.bpm_changes[0] = 120.0;
    song.build_timing();
    for (int64_t m = 0; m < 16; ++m) {
        SongTimestamp ts;
        ts.timecode = song.timecode(m * 768);
        ts.chord.add_note(NoteColor::Red);
        song.sequence.push_back(ts);
    }
    return song;
}

int fill_count(const Song& song) {
    int n = 0;
    for (const SongTimestamp& ts : song.sequence)
        if (ts.has_activation()) ++n;
    return n;
}

}  // namespace

TEST_CASE("rules: defaults are the values Hydra always used") {
    const core::Rules& r = core::default_rules();
    CHECK(r.backend_leeway_ms == 3.0);
    CHECK(r.sqout_rule == core::SqOutRule::FirstNote);
    CHECK(r.max_tied_paths == 4);
    CHECK(r.auto_cap_ladder == std::vector<int>{16, 32, 64, 128, 256, 512});
    CHECK(r.auto_budget_s == 120.0);
    CHECK(r.fill_cooldown_measures == 4);
    CHECK(r.fill_max_distance_beats == 0.5);
    CHECK(r.fill_length_measures == 0.5);
    CHECK(r.fill_land_slop_beats == 1.0 / 32);
}

TEST_CASE("rules: a missing file and an empty file both load the defaults") {
    const uint64_t fp = core::default_rules().fingerprint();
    CHECK(app::load_rules_file(std::filesystem::temp_directory_path() /
                               "hydra_rules_does_not_exist.ini")
              .fingerprint() == fp);
    CHECK(app::load_rules_file(write_rules("empty", "# nothing here\n\n")).fingerprint() == fp);
}

TEST_CASE("rules: every key in the file is read") {
    core::Rules r = app::load_rules_file(write_rules("full",
        "backend_leeway_ms = 5\n"
        "sqout_rule = whole_chord\n"
        "max_tied_paths = 2\n"
        "auto_cap_ladder = 8, 24\n"
        "auto_budget_s = 30\n"
        "fill_cooldown_measures = 2\n"
        "fill_max_distance_beats = 0.25\n"
        "fill_length_measures = 0.25\n"
        "fill_land_slop_beats = 0.125\n"));
    CHECK(r.backend_leeway_ms == 5.0);
    CHECK(r.sqout_rule == core::SqOutRule::WholeChord);
    CHECK(r.max_tied_paths == 2);
    CHECK(r.auto_cap_ladder == std::vector<int>{8, 24});
    CHECK(r.auto_budget_s == 30.0);
    CHECK(r.fill_cooldown_measures == 2);
    CHECK(r.fill_max_distance_beats == 0.25);
    CHECK(r.fill_length_measures == 0.25);
    CHECK(r.fill_land_slop_beats == 0.125);
    CHECK(r.fingerprint() != core::default_rules().fingerprint());
}

TEST_CASE("rules: a # starts a comment anywhere on a line") {
    core::Rules r = app::load_rules_file(write_rules("comments",
        "# my rules\n"
        "max_tied_paths = 2   # fewer ties\n"
        "sqout_rule = whole_chord#no space before the comment\n"));
    CHECK(r.max_tied_paths == 2);
    CHECK(r.sqout_rule == core::SqOutRule::WholeChord);
}

TEST_CASE("rules: a bad value or an unknown key is an error that names the key") {
    auto message_for = [](const char* tag, const std::string& text) -> std::string {
        try {
            app::load_rules_file(write_rules(tag, text));
        } catch (const app::RulesFileError& e) {
            return e.what();
        }
        return "";
    };
    CHECK(message_for("bad1", "max_tied_paths = 0\n").find("max_tied_paths") != std::string::npos);
    CHECK(message_for("bad2", "sqout_rule = every_note\n").find("sqout_rule") != std::string::npos);
    CHECK(message_for("bad3", "backend_leeway_ms = fast\n").find("backend_leeway_ms") !=
          std::string::npos);
    CHECK(message_for("bad4", "auto_cap_ladder = 32, 16\n").find("auto_cap_ladder") !=
          std::string::npos);
    // A typo is an unknown key, never a silent default.
    CHECK(message_for("bad5", "max_tied_path = 4\n").find("max_tied_path") != std::string::npos);
}

TEST_CASE("rules: no rules value has the no-rules fingerprint") {
    // Task 2 opens the store with kNoRulesFingerprint when the file is bad,
    // so no stored row can read Ready. That only works if no real rules
    // value ever hashes to it.
    CHECK(core::default_rules().fingerprint() != core::kNoRulesFingerprint);
    core::Rules other = core::default_rules();
    other.max_tied_paths = 2;
    CHECK(other.fingerprint() != core::kNoRulesFingerprint);
}

TEST_CASE("rules: whole_chord takes every note's SP doubling on a squeeze-out") {
    Chord chord;
    chord.add_note(NoteColor::Red);
    chord.add_note(NoteColor::Blue);
    // Combo 0, so both notes sit at a 1x multiplier and are worth 50 each.
    CHECK(category_scores(chord, 0, nullptr, core::SqOutRule::FirstNote).sqout_reduction == 50);
    CHECK(category_scores(chord, 0, nullptr, core::SqOutRule::WholeChord).sqout_reduction == 100);

    std::vector<CategoryScores> per_note;
    category_scores(chord, 0, &per_note, core::SqOutRule::WholeChord);
    REQUIRE(per_note.size() == 2);
    CHECK(per_note[0].sqout_reduction == 50);
    CHECK(per_note[1].sqout_reduction == 50);
}

TEST_CASE("rules: the leeway moves the Standard edge of a backend rating") {
    BackendSqueeze b;
    b.is_sp = false;
    b.offset_ms = 4.0;
    CHECK(b.summarystr(kDefaultHitWindowMs) == "Hard (uncounted)");
    CHECK(b.summarystr(kDefaultHitWindowMs, 5.0) == "Standard");
}

TEST_CASE("rules: the leeway changes what the engine counts") {
    // A wider leeway can only add backend points and a zero leeway can only
    // remove them. Somewhere in the corpus at least one chart must move.
    core::Rules none = core::default_rules();
    none.backend_leeway_ms = 0.0;
    core::Rules wide = core::default_rules();
    wide.backend_leeway_ms = 50.0;

    bool any_moved = false;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        auto best = [&](const core::Rules& r) {
            ScoreGraph graph(song, 4, FillDeadlineRule::Ch11, r);
            return run_search(graph, EngineOptions{}).front().totalscore();
        };
        const int64_t s_none = best(none);
        const int64_t s_default = best(core::default_rules());
        const int64_t s_wide = best(wide);
        CHECK(s_none <= s_default);
        CHECK(s_default <= s_wide);
        if (s_none != s_default || s_default != s_wide) {
            any_moved = true;
            break;
        }
    }
    CHECK(any_moved);
}

TEST_CASE("rules: max_tied_paths caps the tied paths the engine keeps") {
    core::Rules one = core::default_rules();
    one.max_tied_paths = 1;
    int charts = 0;
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        ScoreGraph graph(song, 4, FillDeadlineRule::Ch11, one);
        std::vector<Path> paths = run_search(graph, EngineOptions{});
        REQUIRE(!paths.empty());
        CHECK(paths.front().tied_pathcount() == 1);
        if (++charts == 5) break;
    }
    CHECK(charts > 0);
}

TEST_CASE("rules: the Auto ladder and budget come from the rules") {
    SearchSettings settings;
    settings.sp_cap = std::nullopt;
    settings.rules.auto_cap_ladder = {8};
    for (const std::string& path : corpus::chart_paths()) {
        Song song = load_songpath(path, true, true);
        if (song.is_empty()) continue;
        HydraRecord record = analyze_chart(song, settings);
        REQUIRE(record.sp_cap.has_value());
        CHECK(*record.sp_cap == 8);
        break;
    }

    app::Settings s;
    s.sp_cap = std::nullopt;
    s.rules.auto_budget_s = 30.0;
    CHECK(s.to_analysis_settings().time_budget_s == std::optional<double>(30.0));
    s.sp_cap = 4;
    CHECK_FALSE(s.to_analysis_settings().time_budget_s.has_value());
}

TEST_CASE("rules: the generated-fill values come from the rules") {
    Song by_default = fill_song();
    by_default.check_activations();
    const int default_fills = fill_count(by_default);
    REQUIRE(default_fills > 0);
    for (const SongTimestamp& ts : by_default.sequence)
        if (ts.has_activation()) CHECK(*ts.activation_length == 384);

    core::Rules tight = core::default_rules();
    tight.fill_cooldown_measures = 2;
    tight.fill_length_measures = 0.25;
    Song with_tight = fill_song();
    with_tight.check_activations(tight);
    CHECK(fill_count(with_tight) > default_fills);
    for (const SongTimestamp& ts : with_tight.sequence)
        if (ts.has_activation()) CHECK(*ts.activation_length == 192);
}
