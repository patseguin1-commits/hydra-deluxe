// Tests for app/preview_clock: Onyx's display-clock arithmetic on a fake
// monotonic source.

#include "doctest.h"

#include "app/preview_clock.h"

using hydra::app::PreviewClock;

TEST_CASE("PreviewClock: frozen while paused, wall-clock while playing") {
    double t = 100.0;  // fake monotonic seconds
    PreviewClock clock([&] { return t; });

    CHECK(clock.now_ms() == doctest::Approx(0.0));
    CHECK_FALSE(clock.playing());
    t += 5.0;
    CHECK(clock.now_ms() == doctest::Approx(0.0));  // paused: no advance

    clock.play();
    CHECK(clock.playing());
    t += 1.5;
    CHECK(clock.now_ms() == doctest::Approx(1500.0));
    clock.play();  // idempotent
    t += 0.5;
    CHECK(clock.now_ms() == doctest::Approx(2000.0));

    clock.pause();
    CHECK_FALSE(clock.playing());
    t += 10.0;
    CHECK(clock.now_ms() == doctest::Approx(2000.0));
    clock.pause();  // idempotent
    CHECK(clock.now_ms() == doctest::Approx(2000.0));
}

TEST_CASE("PreviewClock: seek sets the song time, playing or not") {
    double t = 0.0;
    PreviewClock clock([&] { return t; });

    clock.seek_ms(30000.0);
    CHECK(clock.now_ms() == doctest::Approx(30000.0));

    clock.play();
    t += 2.0;
    CHECK(clock.now_ms() == doctest::Approx(32000.0));
    clock.seek_ms(10000.0);  // while playing: continues from the new time
    CHECK(clock.now_ms() == doctest::Approx(10000.0));
    t += 1.0;
    CHECK(clock.now_ms() == doctest::Approx(11000.0));

    clock.toggle();
    CHECK_FALSE(clock.playing());
    t += 1.0;
    CHECK(clock.now_ms() == doctest::Approx(11000.0));
    clock.toggle();
    CHECK(clock.playing());
}
