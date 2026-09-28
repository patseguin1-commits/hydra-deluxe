// Where the pieces of one Paths-tab activation row go: the measure, the bars
// after it and the badge flush right. The bars used to sit at a fixed +200 px,
// so a long measure like "m1024.1.120" ran into "3 bars". Now they start after
// the widest measure in the path's list, never earlier than before. Pure
// arithmetic, so the rule is unit-tested (tests/test_activation_row_layout.cpp).

#ifndef HYDRA_UI_ACTIVATION_ROW_LAYOUT_H
#define HYDRA_UI_ACTIVATION_ROW_LAYOUT_H

#include <algorithm>

namespace hydra::ui {

// Offsets from the row's left edge, unscaled (multiply by px(1)).
inline constexpr float kRowMeasureX = 104.0f;   // where the measure starts
inline constexpr float kRowMinBarsX = 200.0f;   // the bars never start before this
inline constexpr float kRowBarsGap = 16.0f;     // room between the widest measure and the bars
inline constexpr float kRowBadgeRight = 14.0f;  // badge text's right edge, in from the row's
inline constexpr float kRowBadgePad = 8.0f;     // the badge's pill reaches this far past its text

// Pixel positions of one row's pieces, relative to the row's left edge.
struct ActivationRowLayout {
    float measure_x = 0.0f;
    float bars_x = 0.0f;
    float badge_x = 0.0f;         // the badge text's left edge
    float badge_pill_min = 0.0f;  // the pill's left edge (badge_x - pad)
};

// `widest_measure_w` is the widest measure text in this path's list, so every
// row's bars line up; `badge_w` this row's badge text width (0: no badge);
// `row_w` the row's width; `scale` is px(1).
inline ActivationRowLayout activation_row_layout(float widest_measure_w, float badge_w,
                                                 float row_w, float scale) {
    ActivationRowLayout l;
    l.measure_x = kRowMeasureX * scale;
    // (std::max): windows.h's max macro reaches the GUI tests that include this.
    l.bars_x =
        (std::max)(kRowMinBarsX * scale, l.measure_x + widest_measure_w + kRowBarsGap * scale);
    l.badge_x = row_w - badge_w - kRowBadgeRight * scale;
    l.badge_pill_min = l.badge_x - kRowBadgePad * scale;
    return l;
}

}  // namespace hydra::ui

#endif  // HYDRA_UI_ACTIVATION_ROW_LAYOUT_H
