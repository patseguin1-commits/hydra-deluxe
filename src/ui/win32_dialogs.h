// Small Win32 dialog helpers Dear ImGui has no native equivalent for.

#ifndef HYDRA_UI_WIN32_DIALOGS_H
#define HYDRA_UI_WIN32_DIALOGS_H

#include <optional>
#include <string>

struct HWND__;
typedef HWND__* HWND;

namespace hydra::ui {

// The modern folder-picker (IFileOpenDialog + FOS_PICKFOLDERS), mirroring
// DearPyGui's directory_selector file dialog. Returns nullopt if the user
// cancels. UTF-8 in, UTF-8 out.
std::optional<std::string> browse_for_folder(HWND owner);

}  // namespace hydra::ui

#endif  // HYDRA_UI_WIN32_DIALOGS_H
