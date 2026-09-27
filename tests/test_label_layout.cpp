// The timeline's label rule (ui/label_layout.h): no two drawn numbers
// overlap, the first and last always show, and every label stays on the strip.

#include "doctest.h"

#include <optional>
#include <vector>

#include "ui/label_layout.h"

using hydra::ui::spaced_labels;

namespace {

// No two drawn labels come within `gap` of each other, and all sit inside.
void check_spaced(const std::vector<std::optional<float>>& out, const std::vector<float>& widths,
                  float left, float right, float gap) {
    std::optional<float> end;
    for (size_t i = 0; i < out.size(); ++i) {
        if (!out[i]) continue;
        CHECK(*out[i] >= left);
        CHECK(*out[i] + widths[i] <= right + 0.001f);
        if (end) CHECK(*out[i] >= *end + gap - 0.001f);
        end = *out[i] + widths[i];
    }
}

}  // namespace

TEST_CASE("label layout: labels with room all show, centred on their marks") {
    const std::vector<float> centres{50, 150, 250};
    const std::vector<float> widths{10, 10, 10};
    const auto out = spaced_labels(centres, widths, 0, 300, 4);
    REQUIRE(out.size() == 3);
    CHECK(*out[0] == doctest::Approx(45));
    CHECK(*out[1] == doctest::Approx(145));
    CHECK(*out[2] == doctest::Approx(245));
}

TEST_CASE("label layout: 39 crowded numbers never overlap, first and last show") {
    // Activation numbers on a 480 px strip, bunched like a long chart's: runs
    // of marks a few pixels apart.
    std::vector<float> centres, widths;
    float x = 2.0f;
    for (int i = 1; i <= 39; ++i) {
        x += (i % 5 == 0) ? 40.0f : 4.0f;
        centres.push_back(x);
        widths.push_back(i < 10 ? 6.0f : 12.0f);
    }
    const auto out = spaced_labels(centres, widths, 0, 480, 3);
    check_spaced(out, widths, 0, 480, 3);
    CHECK(out.front().has_value());
    CHECK(out.back().has_value());
    int shown = 0;
    for (const auto& o : out) shown += o ? 1 : 0;
    CHECK(shown > 2);
    CHECK(shown < 39);
}

TEST_CASE("label layout: labels at the ends stay on the strip") {
    const std::vector<float> centres{0, 300};
    const std::vector<float> widths{12, 12};
    const auto out = spaced_labels(centres, widths, 0, 300, 3);
    CHECK(*out[0] == doctest::Approx(0));
    CHECK(*out[1] == doctest::Approx(288));
}

TEST_CASE("label layout: the last label drops the ones it would cover, never the first") {
    const std::vector<float> centres{10, 50, 94, 100};
    const std::vector<float> widths{10, 10, 10, 10};
    const auto out = spaced_labels(centres, widths, 0, 200, 2);
    check_spaced(out, widths, 0, 200, 2);
    CHECK(out[0].has_value());
    CHECK(out[1].has_value());
    CHECK_FALSE(out[2].has_value());
    CHECK(out[3].has_value());

    // Two labels on top of each other: only the first.
    const auto two = spaced_labels({10, 11}, {10, 10}, 0, 200, 2);
    CHECK(two[0].has_value());
    CHECK_FALSE(two[1].has_value());
}
