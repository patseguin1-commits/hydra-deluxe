// The main window: settings panel (chart folders, view/search controls,
// scan/analyze buttons) and the paginated library table. Also owns the two
// progress modals those buttons kick off (scan, batch analyze). Mirrors
// hydra_app.py's view_main + build_main_ui's "mainwindowcontent" group plus
// the "scanprogress"/"batchprogress" modal windows.

#ifndef HYDRA_UI_LIBRARY_VIEW_H
#define HYDRA_UI_LIBRARY_VIEW_H

#include "ui/app_state.h"

namespace hydra::ui {

void render_main_window(AppState& app);

}  // namespace hydra::ui

#endif  // HYDRA_UI_LIBRARY_VIEW_H
