// The main window's pieces, shared between the files that draw it. Only the
// library files include this; everything else uses library_view.h.
//
// library_view.cpp     render_main_window: lays the window out (the library
//                      and the song panel side by side), reaps finished
//                      jobs, and places everything below
// library_toolbar.cpp  the status line and the actions row
// settings_bar.cpp     the "Analysis settings" bar under the toolbar
// library_table.cpp    the search box and the library table
// library_dialogs.cpp  the Song folders, Scanning charts, Analyzing and
//                      Compare dmleaderboards user modals

#ifndef HYDRA_UI_LIBRARY_PARTS_H
#define HYDRA_UI_LIBRARY_PARTS_H

#include "ui/app_state.h"

namespace hydra::ui::detail {

// library_toolbar.cpp
void render_status_line(AppState& app, bool same_line);
void render_actions_row(AppState& app);

// settings_bar.cpp
void render_settings_bar(AppState& app);

// library_table.cpp
void render_search_box(AppState& app);
void render_library_table(AppState& app, int visible_rows);

// library_dialogs.cpp
void render_folder_manager(AppState& app);
void render_scan_modal(AppState& app);
void render_batch_modal(AppState& app);
void render_dm_picker_modal(AppState& app);

}  // namespace hydra::ui::detail

#endif  // HYDRA_UI_LIBRARY_PARTS_H
