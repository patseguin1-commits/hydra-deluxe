// The song panel docked beside the library: the song's title and byline,
// previous / next / close, the optimal score and path, the analyze button,
// and the Paths / Preview / Dynamics / Stars tabs. Drawn inside the main
// window's ##songpanel child by render_main_window.

#ifndef HYDRA_UI_DETAILS_VIEW_H
#define HYDRA_UI_DETAILS_VIEW_H

#include "ui/app_state.h"

namespace hydra::ui {

// Widths at UI scale 1 (pass them through px()). The Paths tab's right
// column needs kMinPathDetailsW for its widest line, the footer's fold and
// Copy path buttons; the panel's minimum fits that beside the 240 px path
// list, the gap between them, the panel's padding and a scroll bar.
inline constexpr float kMinPathListW = 240.0f;
// The most of the Paths tab the path list may take; a longer path wraps.
inline constexpr float kMaxPathListShare = 0.4f;
inline constexpr float kMinPathDetailsW = 500.0f;
inline constexpr float kMinSongPanelW = 820.0f;

void render_song_panel(AppState& app);

}  // namespace hydra::ui

#endif  // HYDRA_UI_DETAILS_VIEW_H
