// Font handles shared across views. hydra_app.py binds "MonoFont"
// (CourierPrime) to nearly every tabular/numeric detail — path lists,
// activation headers, backends tables, score breakdowns — so columns line up
// even without a real table underneath. main.cpp loads the font; views pull
// it from here rather than threading an ImFont* through every call.

#ifndef HYDRA_UI_FONTS_H
#define HYDRA_UI_FONTS_H

#include "imgui.h"

namespace hydra::ui {

inline ImFont* g_mono_font = nullptr;

// The monitor's DPI scale, set once by main.cpp. style.ScaleAllSizes() covers
// ImGui's own paddings and ConfigDpiScaleFonts covers text, but neither
// touches explicit pixel values (SetNextItemWidth, ImVec2 sizes, SameLine
// offsets) — wrap those in px() so widths/heights scale with the display.
inline float g_ui_scale = 1.0f;
inline float px(float v) { return v * g_ui_scale; }

}  // namespace hydra::ui

#endif  // HYDRA_UI_FONTS_H
