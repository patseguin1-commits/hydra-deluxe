// The main window's pieces, shared between the files that draw it. Only the
// library files include this; everything else uses library_view.h.
//
// library_view.cpp     render_main_window: lays the window out (the library
//                      and the song panel side by side), reaps finished
//                      jobs, and places everything below
// library_toolbar.cpp  the status line and the actions row
// settings_bar.cpp     the "Analysis settings" bar under the toolbar
// library_table.cpp    the search box and the library table
// library_dialogs.cpp  the Song folders, Scanning charts, Analyze library and
//                      Compare dmleaderboards user modals, and the batch strips

#ifndef HYDRA_UI_LIBRARY_PARTS_H
#define HYDRA_UI_LIBRARY_PARTS_H

#include <cstdint>
#include <string>

#include "app/config.h"
#include "ui/app_state.h"

namespace hydra::ui::detail {

// library_toolbar.cpp
void render_status_line(AppState& app);
void render_actions_row(AppState& app);

// settings_bar.cpp
void render_settings_bar(AppState& app);

// library_table.cpp: the whole library pane (heading, search, chips, table).
// Called every frame inside the "##library" child, even when the library is
// empty: it also runs AppState::tick_library.
void render_library(AppState& app);

// library_dialogs.cpp
void render_folder_manager(AppState& app);
void render_scan_modal(AppState& app);
void render_dm_picker_modal(AppState& app);

// "1 chart", "12,345 charts": the count grouped in thousands, then the noun.
std::string count_label(int64_t n, const char* one, const char* many);

// A duration as "0:42", "12:03" or "1:02:05". Negative reads as 0:00.
std::string format_duration(double seconds);

// The settings lines the batch confirm lists, in the confirm's order.
struct BatchSettingsSummary {
    std::string difficulty;   // "Expert · Pro Drums · 2x Bass"
    std::string sp_cap;       // "4 bars (Clone Hero's rule)"
    std::string score_range;  // "2 scores" or "2,000 points"
    std::string path_limit;   // "10 ms" or "off"
};
BatchSettingsSummary batch_settings_summary(const app::Settings& s);

// What the library area says when there are no charts: add a folder first,
// or scan the folders you have.
const char* empty_library_message(const app::Settings& s);

// The batch confirm popup, the running strip and the finished strip.
void render_batch_confirm(AppState& app);
void render_batch_strip(AppState& app);
void render_batch_done(AppState& app);

}  // namespace hydra::ui::detail

#endif  // HYDRA_UI_LIBRARY_PARTS_H
