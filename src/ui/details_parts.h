// The song details window's pieces, shared between the files that draw it.
// Only the details files include this; everything else uses details_view.h.
//
// details_panel.cpp  the window itself, the song info, the stored-result
//                    panel, the controls, the analyze job's lifecycle and
//                    progress, and the record states the tabs share
// paths_tab.cpp      the Paths tab: the path list and a path's details
// preview_tab.cpp    the Preview tab: transport row, highway and overlays
// dynamics_tab.cpp   the Dynamics tab
// stars_tab.cpp      the Stars tab

#ifndef HYDRA_UI_DETAILS_PARTS_H
#define HYDRA_UI_DETAILS_PARTS_H

#include "ui/app_state.h"

namespace hydra::ui::detail {

// details_panel.cpp. The states a record-backed tab shows before its own
// content (analyze progress, not analyzed, stale, no paths). True only when
// the record is ready to draw.
bool render_record_state(AppState& app, const char* not_analyzed_text);

// paths_tab.cpp. The path list on the left and the selected path's details
// on the right. A click in the list changes `selected_path`.
void render_path_panel(AppState& app, const Path*& selected_path);

// preview_tab.cpp. The transport row and the 3D highway for `selected_path`.
void render_preview_panel(AppState& app, const Path* selected_path);

// dynamics_tab.cpp.
void render_dynamics_panel(AppState& app);

// stars_tab.cpp.
void render_stars_panel(AppState& app);

}  // namespace hydra::ui::detail

#endif  // HYDRA_UI_DETAILS_PARTS_H
