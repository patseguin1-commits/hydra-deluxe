// Tests for the two fill-spawn deadline rules (search/graph.h
// activation_fill_deadline_ms).
//
// A fill only spawns if the SP meter was full by some deadline. Clone Hero 1.1
// puts that deadline a flat 4 beats before the fill starts. Clone Hero 1.0 put
// it roughly one fill-length earlier, clamped to 250..10000 ms.
//
// No chart fixtures here: the deadline is pure timing math, so the timing is
// built by hand and the expected values are worked out from the formula.

#include "doctest.h"

#include <map>

#include "search/graph.h"

using hydra::FillDeadlineRule;
using hydra::SongTiming;

namespace {

// A flat song: one meter section, one tempo section. 4/4 at `res` ticks per
// quarter note is 4*res ticks per measure.
SongTiming flat_timing(int64_t res, double bpm) {
    std::map<int64_t, int64_t> tpm{{0, 4 * res}};
    std::map<int64_t, double> bpm_map{{0, bpm}};
    return SongTiming(res, tpm, bpm_map);
}

double ch10(const SongTiming& t, int64_t fill_end, int64_t fill_length) {
    return hydra::activation_fill_deadline_ms(t, fill_end, fill_length,
                                              FillDeadlineRule::Ch10);
}

double ch11(const SongTiming& t, int64_t fill_end, int64_t fill_length) {
    return hydra::activation_fill_deadline_ms(t, fill_end, fill_length,
                                              FillDeadlineRule::Ch11);
}

}  // namespace

TEST_CASE("fill deadline: CH 1.0 anchor value") {
    // res 192 at 100 BPM is 320 ticks/second, so 3.125 ms per tick.
    // fill_end 3840 = 12000 ms, fill start 3456 = 10800 ms (1200 ms long).
    // The pad reaches back 384 + 192/16 = 396 ticks from the end, to tick
    // 3444 = 10762.5 ms, so the preroll is 1237.5 ms (inside the clamp).
    // Deadline = 12000 - 1200 - 1237.5.
    SongTiming t = flat_timing(192, 100.0);
    CHECK(ch10(t, 3840, 384) == doctest::Approx(9562.5));
}

TEST_CASE("fill deadline: CH 1.0 lead over two fill lengths is 3750/bpm") {
    // Algebraically the whole formula collapses: the deadline sits exactly
    // (resolution/16) ticks earlier than the tick two fill-lengths back, and
    // (res/16) ticks is 60000/(16*bpm) = 3750/bpm milliseconds -- independent
    // of both the resolution and the fill length. These are Python Hydra
    // v1.2's test_e values (37.5 ms at 100 BPM).
    for (double bpm : {70.0, 100.0, 140.0, 240.0, 400.0}) {
        SongTiming t = flat_timing(192, bpm);
        const int64_t fill_end = 3840, fill_length = 384;
        const double two_back = t.ms_index().at(fill_end - 2 * fill_length);
        CHECK(two_back - ch10(t, fill_end, fill_length) ==
              doctest::Approx(3750.0 / bpm));
    }
}

TEST_CASE("fill deadline: CH 1.0 pad stays fractional at odd resolutions") {
    // res 200 makes the pad 12.5 ticks. Integer division would make it 12 and
    // land the deadline half a tick late, so this case fails loudly if anyone
    // writes `res / 16` on integers.
    //
    // res 200 at 100 BPM is 333.33 ticks/second, so 3 ms per tick.
    // fill_end 4000 = 12000 ms, fill start 3600 = 10800 ms (1200 ms long).
    // Pad reaches to tick 4000 - 412.5 = 3587.5 = 10762.5 ms, preroll 1237.5.
    SongTiming t = flat_timing(200, 100.0);
    CHECK(ch10(t, 4000, 400) == doctest::Approx(9562.5));
    // What integer division would have produced, pinned so the two can't be
    // confused: pad to tick 3588 = 10764 ms, preroll 1236, deadline 9564.
    CHECK(ch10(t, 4000, 400) != doctest::Approx(9564.0));
}

TEST_CASE("fill deadline: CH 1.0 preroll clamps at 250 ms") {
    // 400 BPM at res 192 is 0.78125 ms per tick. A 96-tick fill's raw preroll
    // is (96 + 12) * 0.78125 = 84.375 ms, well under the 250 ms floor.
    SongTiming t = flat_timing(192, 400.0);
    const int64_t fill_end = 3840, fill_length = 96;
    const double fend = t.ms_index().at(fill_end);
    const double fill_len_ms = fend - t.ms_index().at(fill_end - fill_length);
    CHECK(fill_len_ms == doctest::Approx(75.0));
    CHECK(ch10(t, fill_end, fill_length) ==
          doctest::Approx(fend - fill_len_ms - 250.0));
    CHECK(ch10(t, fill_end, fill_length) == doctest::Approx(2675.0));
}

TEST_CASE("fill deadline: CH 1.0 preroll clamps at 10000 ms") {
    // 20 BPM at res 192 is 15.625 ms per tick. A 1000-tick fill's raw preroll
    // is (1000 + 12) * 15.625 = 15812.5 ms, over the 10 s ceiling.
    SongTiming t = flat_timing(192, 20.0);
    const int64_t fill_end = 3840, fill_length = 1000;
    const double fend = t.ms_index().at(fill_end);
    const double fill_len_ms = fend - t.ms_index().at(fill_end - fill_length);
    CHECK(fill_len_ms == doctest::Approx(15625.0));
    CHECK(ch10(t, fill_end, fill_length) ==
          doctest::Approx(fend - fill_len_ms - 10000.0));
    CHECK(ch10(t, fill_end, fill_length) == doctest::Approx(34375.0));
}

TEST_CASE("fill deadline: CH 1.1 is unchanged 4-beats-before math") {
    // The default rule must still be exactly what add_act_edge computed inline
    // before the rule became a parameter: the ms of the tick 4 beats before
    // the fill starts.
    for (int64_t res : {192, 480}) {
        for (double bpm : {90.0, 120.0, 200.0}) {
            SongTiming t = flat_timing(res, bpm);
            for (int64_t fill_length : {96, 384, 960}) {
                const int64_t fill_end = 7680;
                const int64_t tick_e = fill_end - fill_length - 4 * res;
                CHECK(ch11(t, fill_end, fill_length) ==
                      doctest::Approx(t.timecode(tick_e).ms()));
            }
        }
    }
}

TEST_CASE("fill deadline: the two rules genuinely differ") {
    // A short fill is stricter under 1.1 (4 beats is the longer wait), a long
    // one is looser -- the whole reason the comparison report exists.
    SongTiming t = flat_timing(192, 120.0);
    const int64_t fill_end = 7680;

    // Short fill (96 ticks = half a beat): 1.1's deadline comes earlier.
    CHECK(ch11(t, fill_end, 96) < ch10(t, fill_end, 96));

    // Long fill (1920 ticks = 10 beats): 1.0's deadline comes earlier.
    CHECK(ch10(t, fill_end, 1920) < ch11(t, fill_end, 1920));
}
