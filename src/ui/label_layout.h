// Which labels in a row fit without overlapping: the Paths tab's timeline
// puts a number over each activation's mark, and with many activations close
// together the numbers would print on top of each other. Pure arithmetic, so
// the rule is unit-tested (tests/test_label_layout.cpp).

#ifndef HYDRA_UI_LABEL_LAYOUT_H
#define HYDRA_UI_LABEL_LAYOUT_H

#include <algorithm>
#include <optional>
#include <vector>

namespace hydra::ui {

// Labels centred on `centres` (ascending), each `widths[i]` wide, kept inside
// [left, right]. Returns each label's left edge when it is drawn, or nothing
// when it would come within `gap` of the one before. Greedy from the left;
// the first and the last label always show (the last one drops any earlier
// ones it would overlap, but never the first).
inline std::vector<std::optional<float>> spaced_labels(const std::vector<float>& centres,
                                                       const std::vector<float>& widths,
                                                       float left, float right, float gap) {
    const size_t n = std::min(centres.size(), widths.size());
    std::vector<std::optional<float>> out(n);
    if (n == 0) return out;
    auto place = [&](size_t i) {
        return std::max(left, std::min(centres[i] - widths[i] * 0.5f, right - widths[i]));
    };
    float end = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float l = place(i);
        if (i == 0 || l >= end + gap) {
            out[i] = l;
            end = l + widths[i];
        }
    }
    if (n > 1 && !out[n - 1]) {
        const float l = place(n - 1);
        for (size_t j = n - 1; j-- > 1;) {
            if (!out[j]) continue;
            if (*out[j] + widths[j] + gap <= l) break;
            out[j].reset();
        }
        if (*out[0] + widths[0] + gap <= l) out[n - 1] = l;
    }
    return out;
}

}  // namespace hydra::ui

#endif  // HYDRA_UI_LABEL_LAYOUT_H
