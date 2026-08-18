// Invariant + unit tests for core/timing.
//
// The corpus half converts every chart timestamp through the chart's real
// tempo/meter maps and asserts the conversions are monotone and consistent.
// The unit half pins the edge cases the corpus may not contain.

#include "doctest.h"

#include <cstdint>
#include <map>
#include <string>

#include "core/timing.h"
#include "corpus_util.h"
#include "parse/song.h"

TEST_CASE("timing: corpus conversions are monotone and consistent") {
    size_t charts = 0, samples = 0;
    for (const std::string& path : corpus::chart_paths()) {
        hydra::Song song = hydra::load_songpath(path, "Expert", true, true);
        if (song.is_empty()) continue;
        ++charts;

        bool ms_ok = true, mbt_ok = true;
        double prev_ms = -1e18;
        for (const hydra::SongTimestamp& ts : song.sequence) {
            const hydra::Timecode& tc = ts.timecode;
            ++samples;

            // Later ticks never map to earlier times. (measures_decimal is
            // NOT monotone: a mid-measure meter change legitimately resets
            // the fractional part below the previous value.)
            if (tc.ms() < prev_ms) ms_ok = false;
            prev_ms = tc.ms();

            // measure-beats-ticks agrees with the decimal measure count
            // (measures_decimal = whole measures + fraction, all 0-based).
            const int64_t* mbt = tc.measure_beats_ticks();
            const double md = tc.measures_decimal();
            if (mbt[0] < 0 || mbt[1] < 0 || mbt[2] < 0 ||
                static_cast<double>(mbt[0]) > md ||
                md > static_cast<double>(mbt[0] + 1))
                mbt_ok = false;
        }
        CHECK_MESSAGE(ms_ok, path << ": ms not monotone");
        CHECK_MESSAGE(mbt_ok, path << ": measure_beats_ticks inconsistent");
    }

    REQUIRE(charts > 0);
    MESSAGE("timing invariants: " << charts << " charts, " << samples
                                  << " samples");
}

TEST_CASE("timing: to_multiplier thresholds") {
    using hydra::to_multiplier;
    CHECK(to_multiplier(0) == 1);
    CHECK(to_multiplier(9) == 1);
    CHECK(to_multiplier(10) == 2);
    CHECK(to_multiplier(19) == 2);
    CHECK(to_multiplier(20) == 3);
    CHECK(to_multiplier(29) == 3);
    CHECK(to_multiplier(30) == 4);
    CHECK(to_multiplier(1000) == 4);
}

TEST_CASE("timing: a map without a tick-0 entry throws") {
    std::map<int64_t, double> bpm{{480, 120.0}};
    CHECK_THROWS_AS(hydra::MsIndex(bpm, 480), std::out_of_range);

    std::map<int64_t, int64_t> tpm{{480, 1920}};
    CHECK_THROWS_AS(hydra::MeasureIndex(tpm, 480), std::out_of_range);
}

TEST_CASE("timing: a tick before the first tempo mark reads at the opening tempo") {
    // 120 BPM, resolution 480 -> 2 ticks/ms, so one beat (480 ticks) = 500 ms.
    std::map<int64_t, double> bpm{{0, 120.0}};
    hydra::MsIndex ms(bpm, 480);
    CHECK(ms.at(0) == doctest::Approx(0.0));
    CHECK(ms.at(480) == doctest::Approx(500.0));
    CHECK(ms.at(-480) == doctest::Approx(-500.0));  // extrapolated backwards
}

TEST_CASE("timing: a meter change off a barline carries a partial measure") {
    // 4/4 (1920 ticks/measure) until tick 2880, which is 1.5 measures in, then
    // switch. section_at reports the section a tick is measured in; the second
    // section begins mid-measure, so its measure count is the whole measures
    // counted so far (1), not 1.5.
    std::map<int64_t, int64_t> tpm{{0, 1920}, {2880, 960}};
    hydra::MeasureIndex mi(tpm, 480);
    CHECK(mi.section_at(0) == 0);
    CHECK(mi.section_at(1919) == 0);
    CHECK(mi.section_at(2880) == 0);   // exactly on the boundary -> prior section
    CHECK(mi.section_at(2881) == 1);
    CHECK(mi.measures_at(1) == 1);     // one whole measure counted by tick 2880
    CHECK(mi.starts_at(1) == 1920);    // last barline at or before tick 2880
}

TEST_CASE("timing: ms_per_measure_at reads local measure durations") {
    // The Tom Sawyer (Onyxite) shape: ten 7/8 measures at 87.35 BPM, then
    // 7/16 at 85.1. 7/8 = 1680 ticks at res 480; 7/16 = 840.
    std::map<int64_t, int64_t> tpm{{0, 1680}, {16800, 840}};
    std::map<int64_t, double> bpm{{0, 87.35}, {16800, 85.1}};
    hydra::SongTiming st(480, tpm, bpm);

    // 3.5 quarters * 60000/87.35 and 1.75 quarters * 60000/85.1.
    CHECK(st.ms_per_measure_at(0) == doctest::Approx(2404.1214).epsilon(1e-6));
    CHECK(st.ms_per_measure_at(16800) == doctest::Approx(1233.8425).epsilon(1e-6));

    // A tick exactly on the change reads the new section; tick-1 the old one.
    CHECK(st.ms_per_measure_at(16799) == doctest::Approx(2404.1214).epsilon(1e-6));
    CHECK(st.ms_per_measure_at(16801) == doctest::Approx(1233.8425).epsilon(1e-6));

    // Uniform map: the same value everywhere.
    std::map<int64_t, int64_t> tpm44{{0, 1920}};
    std::map<int64_t, double> bpm120{{0, 120.0}};
    hydra::SongTiming flat(480, tpm44, bpm120);
    CHECK(flat.ms_per_measure_at(0) == doctest::Approx(2000.0));
    CHECK(flat.ms_per_measure_at(12345) == doctest::Approx(2000.0));
}
