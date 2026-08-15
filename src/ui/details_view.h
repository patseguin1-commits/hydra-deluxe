// The song details modal: song info + analysis controls, the path list, path
// details, and the in-place analyze-progress view that replaces the panel
// while a chart is being analyzed. Mirrors hydra_app.py's "songdetails"
// window (view_showsongdetails / refresh_songdetails / on_path_selected /
// on_run_chart).

#ifndef HYDRA_UI_DETAILS_VIEW_H
#define HYDRA_UI_DETAILS_VIEW_H

#include "ui/app_state.h"

namespace hydra::ui {

void render_details_modal(AppState& app);

}  // namespace hydra::ui

#endif  // HYDRA_UI_DETAILS_VIEW_H
