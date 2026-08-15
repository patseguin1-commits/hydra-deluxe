// Parity + unit tests for core/timing (the hymisc port).
//
// The parity half rebuilds each chart's tempo/meter maps from the golden and
// asserts every sampled tick converts to the exact same ms / measure-beat-tick
// / decimal-measure values Python produced. The unit half pins the edge cases
// the corpus may not contain.

#include "doctest.h"

#include <cstdint>
#include <map>
#include <string>

#include "core/timing.h"
#include "golden_util.h"

namespace {

hydra::SongTiming build_timing(const golden::json& timing) {
    int64_t res = timing["res"].get<int64_t>();

    std::map<int64_t, int64_t> tpm;
    for (auto it = timing["tpm"].begin(); it != timing["tpm"].end(); ++it)
        tpm[std::stoll(it.key())] = it.value().get<int64_t>();

    std::map<int64_t, double> bpm;
    for (auto it = timing["bpm"].begin(); it != timing["bpm"].end(); ++it)
        bpm[std::stoll(it.key())] = golden::as_double(it.value());

    return hydra::SongTiming(res, tpm, bpm);
}

}  // namespace

TEST_CASE("timing: ms and measures match golden across the corpus") {
    const golden::json idx = golden::index();
    REQUIRE(idx.size() > 0);

    size_t charts = 0, samples = 0;
    for (const auto& entry : idx) {
        const std::string slug = entry["slug"].get<std::string>();
        const golden::json doc = golden::chart(slug);
        const golden::json& timing = doc["timing"];

        hydra::SongTiming st = build_timing(timing);
        ++charts;

        std::string first_ms_diff, first_mbt_diff, first_md_diff;
        int ms_bad = 0, mbt_bad = 0, md_bad = 0;

        for (const auto& s : timing["samples"]) {
            int64_t tick = s["tick"].get<int64_t>();
            hydra::Timecode tc = st.timecode(tick);
            ++samples;

            double want_ms = golden::as_double(s["ms"]);
            if (tc.ms() != want_ms) {
                if (ms_bad++ == 0) {
                    first_ms_diff = "tick " + std::to_string(tick) +
                        " ms " + std::to_string(tc.ms()) + " != " +
                        s["ms"].get<std::string>();
                }
            }

            if (s.contains("mbt")) {
                const auto& m = s["mbt"];
                const int64_t* got = tc.measure_beats_ticks();
                if (got[0] != m[0].get<int64_t>() ||
                    got[1] != m[1].get<int64_t>() ||
                    got[2] != m[2].get<int64_t>()) {
                    if (mbt_bad++ == 0)
                        first_mbt_diff = "tick " + std::to_string(tick);
                }
                double want_md = golden::as_double(s["measures_decimal"]);
                if (tc.measures_decimal() != want_md) {
                    if (md_bad++ == 0) {
                        first_md_diff = "tick " + std::to_string(tick) +
                            " md " + std::to_string(tc.measures_decimal()) +
                            " != " + s["measures_decimal"].get<std::string>();
                    }
                }
            }
        }

        CHECK_MESSAGE(ms_bad == 0, slug << ": " << ms_bad
                      << " ms diffs, first: " << first_ms_diff);
        CHECK_MESSAGE(mbt_bad == 0, slug << ": " << mbt_bad
                      << " mbt diffs, first: " << first_mbt_diff);
        CHECK_MESSAGE(md_bad == 0, slug << ": " << md_bad
                      << " measures_decimal diffs, first: " << first_md_diff);
    }

    MESSAGE("timing parity: " << charts << " charts, " << samples << " samples");
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
