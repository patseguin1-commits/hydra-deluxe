// The song panel docked beside the library: the song's title and byline,
// previous / next / close, the optimal score and path, the analyze button,
// and the Paths / Preview / Dynamics / Stars tabs. Drawn inside the main
// window's ##songpanel child by render_main_window.

#ifndef HYDRA_UI_DETAILS_VIEW_H
#define HYDRA_UI_DETAILS_VIEW_H

#include "ui/app_state.h"

namespace hydra::ui {

void render_song_panel(AppState& app);

}  // namespace hydra::ui

#endif  // HYDRA_UI_DETAILS_VIEW_H
