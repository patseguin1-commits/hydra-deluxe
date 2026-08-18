// Hydra (C++ port) — application entry point.
//
// Stands up the Win32 window, DirectX 11 device, and Dear ImGui context
// (single primary window; docking/multi-viewport deliberately off) and runs
// the frame loop over the library view + details modal. The Win32/DX11
// plumbing is the upstream example_win32_directx11 boilerplate, unchanged
// except for the window identity and the frame contents.

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <shobjidl.h>
#include <tchar.h>

#include <string>

#include "app/config.h"
#include "app/edition.h"
#include "ui/resource.h"
#include "ui/app_state.h"
#include "ui/details_view.h"
#include "ui/fonts.h"
#include "ui/icons.h"
#include "ui/library_view.h"
#include "ui/theme.h"

// Direct3D state.
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int main(int, char**)
{
    // Make the process DPI aware and read the primary monitor's scale.
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Give each edition its own taskbar identity so Hydra and HydraUncapped
    // windows/pins never group together.
    ::SetCurrentProcessExplicitAppUserModelID(hydra::kAppUserModelIDW);

    // App icon, embedded in the exe by src/app/hydra.rc. The pinned-taskbar /
    // Explorer icon comes straight from that PE resource; these runtime loads
    // cover the window class and the live window (WM_SETICON below), and
    // unlike the old cwd-relative resource/icon_app.ico file load they work
    // from any working directory.
    HINSTANCE hInstance = ::GetModuleHandleW(nullptr);
    HICON hIconLarge = (HICON)::LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON),
                                           IMAGE_ICON, 32, 32, 0);
    HICON hIconSmall = (HICON)::LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON),
                                           IMAGE_ICON, 16, 16, 0);

    // Create the application window.
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                       hInstance, hIconLarge, nullptr, nullptr,
                       nullptr, L"Hydra", hIconSmall };
    ::RegisterClassExW(&wc);
    // hymisc.HYDRA_VERSION as of this port; matches the "{EDITION_NAME} v..."
    // title dpg.create_viewport builds. Bump alongside HYDRA_VERSION.
    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName, hydra::kWindowTitleW, WS_OVERLAPPEDWINDOW, 100, 100,
        (int)(1280 * main_scale), (int)(720 * main_scale),
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Song-info icons (record/star/pencil/hash), matching hydra_app.py's
    // dpg.add_static_texture loads. Best-effort: see icons.h.
    hydra::ui::load_icons(g_pd3dDevice);

    // Window icon, matching dpg.create_viewport's small_icon/large_icon
    // (loaded from the embedded resource above).
    if (hIconSmall) ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    if (hIconLarge) ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconLarge);

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Set up the Dear ImGui context.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // No docking/viewports: Hydra is one primary window filling the OS
    // window, like hydra_app.py's dpg.set_primary_window -- not a docking
    // workspace with panels that can be torn into their own OS windows.

    // Persist ImGui state (library table column widths) next to the exe,
    // edition-suffixed so both editions can run at once without clobbering
    // each other. The default cwd-relative "imgui.ini" landed wherever the
    // app happened to be launched from.
    static std::string ini_file =
        hydra::app::exe_dir() + "\\hydra" + (hydra::kUncapped ? "_uncapped" : "") + "_ui.ini";
    io.IniFilename = ini_file.c_str();

    ImGui::StyleColorsDark();
    hydra::ui::apply_theme();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;
    hydra::ui::g_ui_scale = main_scale;  // for the views' explicit pixel sizes

    // Fonts (resource/ is copied beside the exe by the build; see
    // CMakeLists.txt). exe-relative, not cwd-relative: the app may be
    // launched with any working directory (e.g. a shortcut's Start-in).
    // Falls back to ImGui's built-in font if the files aren't found,
    // rather than asserting.
    const std::string resource_dir = hydra::app::exe_dir() + "\\resource\\";
    ImFont* main_font = io.Fonts->AddFontFromFileTTF(
        (resource_dir + "ShipporiAntiqueB1-Regular.ttf").c_str(), 18.0f);
    hydra::ui::g_mono_font = io.Fonts->AddFontFromFileTTF(
        (resource_dir + "CourierPrime-Regular.ttf").c_str(), 18.0f);
    if (main_font) io.FontDefault = main_font;

    // CJK fallback: Clone Hero libraries are full of Japanese (and other
    // non-Latin) titles, which rendered as ?/boxes with the Latin-only fonts.
    // Merge the first available system font into the main font; ImGui's
    // dynamic font loader rasterizes glyphs on demand, so this costs nothing
    // until a non-Latin title is actually drawn.
    if (main_font) {
        const char* cjk_candidates[] = {
            "C:\\Windows\\Fonts\\YuGothM.ttc",   // Yu Gothic Medium (Win 8.1+)
            "C:\\Windows\\Fonts\\meiryo.ttc",    // Meiryo
            "C:\\Windows\\Fonts\\msgothic.ttc",  // MS Gothic (bitmap-ish, last resort)
        };
        for (const char* path : cjk_candidates) {
            if (::GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
            ImFontConfig merge;
            merge.MergeMode = true;
            if (io.Fonts->AddFontFromFileTTF(path, 18.0f, &merge)) break;
        }
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    const ImVec4 clear_color = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);

    hydra::ui::AppState app;

    bool done = false;
    while (!done)
    {
        // Drain the Win32 message queue.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Skip rendering while minimized / occluded.
        if (g_SwapChainOccluded &&
            g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Apply any queued resize.
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight,
                                        DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Begin the frame.
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        hydra::ui::render_main_window(app);
        hydra::ui::render_details_modal(app);

        // Render.
        ImGui::Render();
        const float clear_with_alpha[4] = {
            clear_color.x * clear_color.w, clear_color.y * clear_color.w,
            clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView,
                                                   clear_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Draw and present the additional platform windows (viewports).
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        HRESULT hr = g_pSwapChain->Present(1, 0);   // vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)  // Fall back to the WARP software driver.
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    // Disable DXGI's Alt+Enter, which does not play well with viewports.
    IDXGIFactory* pSwapChainFactory = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetParent(IID_PPV_ARGS(&pSwapChainFactory))))
    {
        pSwapChainFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
        pSwapChainFactory->Release();
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward-declared in imgui_impl_win32.cpp.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)  // Disable the ALT app menu.
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
