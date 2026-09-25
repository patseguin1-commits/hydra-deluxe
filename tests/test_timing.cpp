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
        hydra::Song song = hydra::load_songpath(path, true, true);
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

TEST_CASE("timing: continuous helpers are exact, inverse, and monotone") {
    // The field-verified map (What's My Age Again? Sync Chart shape): 4/4,
    // 155 -> 160 at 57600, 160 -> 157 at 67200, 157 -> 160 at 69120.
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{
        {0, 155.0}, {57600, 160.0}, {67200, 157.0}, {69120, 160.0}};
    hydra::SongTiming st(480, tpm, bpm);

    // ms_at_tick_f agrees with the integer-tick scoring path...
    for (int64_t tick : {int64_t(0), int64_t(57600), int64_t(67200),
                         int64_t(68880), int64_t(69120), int64_t(90000)})
        CHECK(st.ms_index().ms_at_tick_f(static_cast<double>(tick)) ==
              doctest::Approx(st.timecode(tick).ms()).epsilon(1e-12));

    // ...and round-trips through its inverse at fractional ticks too.
    for (double x : {0.0, 1234.5, 57599.9, 57600.0, 68880.25, 100000.75}) {
        double ms = st.ms_index().ms_at_tick_f(x);
        CHECK(st.ms_index().tick_at_ms(ms) == doctest::Approx(x).epsilon(1e-9));
    }

    // sp_end_ms: monotone in the hit time, continuous across the 57600
    // boundary, and its interior slope is the mspm ratio the linearized
    // transfer scale samples.
    const double h0 = st.timecode(50000).ms();
    double prev = -1e18;
    bool monotone = true;
    for (double h = h0; h < h0 + 20000.0; h += 37.0) {
        double e = st.sp_end_ms(h, 6);
        if (e < prev) monotone = false;
        prev = e;
    }
    CHECK(monotone);

    const double hb = st.timecode(57600).ms();
    CHECK(st.sp_end_ms(hb + 1e-6, 6) - st.sp_end_ms(hb - 1e-6, 6) <
          doctest::Approx(1e-4));

    // act tick 57000 sits inside the 155 section; its 6-measure end lands
    // inside the 157 section, so the local slope is tps(155)/tps(157).
    const double hi = st.timecode(57000).ms();
    double slope = (st.sp_end_ms(hi + 5.0, 6) - st.sp_end_ms(hi - 5.0, 6)) / 10.0;
    CHECK(slope == doctest::Approx(1240.0 / 1256.0).epsilon(1e-9));

    // Flat tempo, on-tick activation: the continuous map reproduces the
    // tick-rounded scoring path exactly.
    std::map<int64_t, double> bpm120{{0, 120.0}};
    hydra::SongTiming flat(480, tpm, bpm120);
    hydra::Timecode tc = flat.timecode(1920);
    CHECK(flat.sp_end_ms(tc.ms(), 4) ==
          doctest::Approx(flat.plusmeasure(tc, 4).ms()).epsilon(1e-12));
}

TEST_CASE("timing: one SP bar is two measures") {
    CHECK(hydra::kMeasuresPerSpBar == 2);
    CHECK(hydra::sp_bars_to_measures(1) == 2);
    CHECK(hydra::sp_bars_to_measures(4) == 8);
    CHECK(hydra::sp_bars_to_measures(-1) == -2);  // one phrase back, as squeeze_rating uses it

    // 4/4 at 480 ticks per beat: a measure is 1920 ticks, so two bars of SP
    // run four measures, 7680 ticks.
    std::map<int64_t, int64_t> tpm{{0, 1920}};
    std::map<int64_t, double> bpm{{0, 120.0}};
    hydra::SongTiming st(480, tpm, bpm);
    CHECK(st.plusmeasure(st.timecode(0), hydra::sp_bars_to_measures(2)).ticks() == 7680);
}
