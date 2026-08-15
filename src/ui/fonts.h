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

}  // namespace hydra::ui

#endif  // HYDRA_UI_FONTS_H
