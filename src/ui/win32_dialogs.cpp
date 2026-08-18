#include "ui/win32_dialogs.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace hydra::ui {

namespace {

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                  nullptr, nullptr);
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], len, nullptr,
                        nullptr);
    return s;
}

}  // namespace

std::optional<std::string> browse_for_folder(HWND owner, bool* failed) {
    if (failed) *failed = false;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool we_initialized_com = SUCCEEDED(hr);
    // RPC_E_CHANGED_MODE means COM is already initialized on this thread in a
    // different mode (the Win32/DX11 message loop may have done so) -- fine,
    // just don't uninitialize what we didn't initialize.
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        if (failed) *failed = true;
        return std::nullopt;
    }

    std::optional<std::string> result;
    ComPtr<IFileOpenDialog> dialog;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD flags = 0;
        dialog->GetOptions(&flags);
        dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

        if (SUCCEEDED(dialog->Show(owner))) {
            ComPtr<IShellItem> item;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = wide_to_utf8(path);
                    CoTaskMemFree(path);
                }
            }
        }
        // Show() failing is (almost always) the user cancelling: not a failure.
    } else {
        if (failed) *failed = true;
    }

    if (we_initialized_com) CoUninitialize();
    return result;
}

}  // namespace hydra::ui
