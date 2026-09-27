// The theme's text colours against the surfaces they sit on, measured the way
// WCAG 2.2 defines contrast: (L1 + 0.05) / (L2 + 0.05) on relative luminance.
// 4.5:1 is the minimum for normal-size text.

#include "doctest.h"

#include <cmath>
#include <utility>

#include "imgui.h"
#include "ui/theme.h"

using namespace hydra::ui;

namespace {

double channel(float c) {
    return c <= 0.04045f ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(const ImVec4& c) {
    return 0.2126 * channel(c.x) + 0.7152 * channel(c.y) + 0.0722 * channel(c.z);
}

double contrast(const ImVec4& a, const ImVec4& b) {
    double la = luminance(a), lb = luminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

}  // namespace

TEST_CASE("theme: button text reads at 4.5:1 in every button state") {
    CHECK(contrast(kDefaultTextColor, kButtonColor) >= 4.5);
    CHECK(contrast(kDefaultTextColor, kButtonHoveredColor) >= 4.5);
    CHECK(contrast(kDefaultTextColor, kButtonActiveColor) >= 4.5);
}

TEST_CASE("theme: dimmed and disabled text stays readable") {
    CHECK(contrast(kDimTextColor, kWindowBgColor) >= 4.5);
    CHECK(contrast(kDimTextColor, kSettingsBarBg) >= 4.5);
    CHECK(contrast(kDimTextColor, kPanelBg) >= 4.5);
    CHECK(contrast(kDisabledInputTextColor, kDisabledInputBgColor) >= 4.5);
    CHECK(contrast(kDisabledButtonTextColor, kDisabledButtonColor) >= 4.5);
}

TEST_CASE("theme: apply_theme uses the readable shades") {
    ImGui::CreateContext();
    apply_theme();
    const ImVec4* c = ImGui::GetStyle().Colors;
    CHECK(c[ImGuiCol_Button].y == kButtonColor.y);
    CHECK(c[ImGuiCol_ButtonHovered].y == kButtonHoveredColor.y);
    CHECK(c[ImGuiCol_ButtonActive].y == kButtonActiveColor.y);
    CHECK(c[ImGuiCol_TextDisabled].x == kDimTextColor.x);
    ImGui::DestroyContext();
}
