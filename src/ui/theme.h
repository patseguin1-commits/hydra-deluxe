// Color constants, ported from hydra_app.py's build_main_ui theme blocks
// (default_theme, bestpath_theme, warning_theme, newsong_theme,
// songfolder_theme, delete_theme, disabled_text), plus DearPyGui's own
// baseline "Default Theme" colors -- verified against the real values via
// DPG's built-in style editor (dpg.show_style_editor()), not guessed --
// for every slot hydra_app.py leaves at DPG's default rather than
// overriding (WindowBg, FrameBg, Header, Border/Separator, Scrollbar*).
// Dear ImGui's own StyleColorsDark() defaults are meaningfully different
// (bluer, darker) from DPG's, so relying on them for un-overridden slots
// was the source of several color mismatches.

#ifndef HYDRA_UI_THEME_H
#define HYDRA_UI_THEME_H

#include "imgui.h"

namespace hydra::ui {

inline const ImVec4 kBestPathColor{250 / 255.0f, 210 / 255.0f, 0 / 255.0f, 1.0f};
inline const ImVec4 kWarningColor{255 / 255.0f, 127 / 255.0f, 0 / 255.0f, 1.0f};
inline const ImVec4 kNewSongColor{100 / 255.0f, 100 / 255.0f, 100 / 255.0f, 1.0f};
inline const ImVec4 kDefaultTextColor{250 / 255.0f, 250 / 255.0f, 250 / 255.0f, 1.0f};
inline const ImVec4 kDeleteButtonColor{180 / 255.0f, 5 / 255.0f, 5 / 255.0f, 1.0f};
inline const ImVec4 kDeleteButtonHoveredColor{250 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f};
inline const ImVec4 kDeleteButtonActiveColor{250 / 255.0f, 100 / 255.0f, 100 / 255.0f, 1.0f};
inline const ImVec4 kAccentColor{0 / 255.0f, 180 / 255.0f, 180 / 255.0f, 1.0f};
inline const ImVec4 kButtonColor{0 / 255.0f, 150 / 255.0f, 150 / 255.0f, 1.0f};
inline const ImVec4 kFolderListBg{50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f};

// disabled_text theme + mvButton/mvInputInt(enabled_state=False) components.
// DPG's disabled state is a flat, fully-opaque gray -- not ImGui's default
// DisabledAlpha fade -- so apply_theme() sets style.DisabledAlpha = 1 and
// begin_disabled_button()/begin_disabled_input() push these explicitly.
inline const ImVec4 kDisabledTextColor{50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f};
inline const ImVec4 kDisabledButtonTextColor{200 / 255.0f, 200 / 255.0f, 200 / 255.0f, 1.0f};
inline const ImVec4 kDisabledButtonColor{100 / 255.0f, 100 / 255.0f, 100 / 255.0f, 1.0f};
inline const ImVec4 kDisabledInputTextColor{50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f};
inline const ImVec4 kDisabledInputBgColor{40 / 255.0f, 40 / 255.0f, 40 / 255.0f, 1.0f};

// Applies the app-wide accent (teal buttons/headers, matching
// build_main_ui's "default_theme") plus DPG's own baseline colors for
// slots hydra_app.py never overrides. Call once after
// ImGui::StyleColorsDark().
void apply_theme();

// Wrap a Button (or ArrowButton/etc) that becomes non-interactive under
// `disabled`, matching mvButton(enabled_state=False)'s flat gray -- use
// instead of a bare ImGui::BeginDisabled()/EndDisabled() pair whenever the
// disabled visual should match hydra_app.py exactly.
void begin_disabled_button(bool disabled);
void end_disabled_button(bool disabled);

// Same, for an InputInt matching mvInputInt(enabled_state=False).
void begin_disabled_input(bool disabled);
void end_disabled_input(bool disabled);

}  // namespace hydra::ui

#endif  // HYDRA_UI_THEME_H
