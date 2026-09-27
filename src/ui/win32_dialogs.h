// Small Win32 dialog helpers Dear ImGui has no native equivalent for.

#ifndef HYDRA_UI_WIN32_DIALOGS_H
#define HYDRA_UI_WIN32_DIALOGS_H

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

struct HWND__;
typedef HWND__* HWND;

namespace hydra::ui {

// The modern folder-picker (IFileOpenDialog + FOS_PICKFOLDERS), mirroring
// DearPyGui's directory_selector file dialog. Returns nullopt if the user
// cancels — and also on COM/dialog failure, which additionally sets *failed
// so the caller can tell the user (a cancel must stay silent, but a dialog
// that never appeared shouldn't). UTF-8 in, UTF-8 out.
std::optional<std::string> browse_for_folder(HWND owner, bool* failed = nullptr);

// Opens Explorer on the file's folder with the file selected
// (explorer.exe /select,"<path>"). Returns false when the shell refuses.
bool show_in_folder(const std::filesystem::path& file);

// The seam behind show_in_folder, like app::set_open_in_browser: a GUI test
// installs one that records the path instead of opening Explorer. An empty
// function restores the default.
using ShowInFolderFn = std::function<bool(const std::wstring& path)>;
void set_show_in_folder(ShowInFolderFn fn);

}  // namespace hydra::ui

#endif  // HYDRA_UI_WIN32_DIALOGS_H
