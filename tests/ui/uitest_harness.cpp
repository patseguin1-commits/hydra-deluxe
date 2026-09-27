#include "uitest_harness.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"
#include "imgui_te_internal.h"

#include "app/config.h"
#include "app/report_files.h"
#include "audio/device.h"
#include "net/dmbot_client.h"
#include "ui/app_state.h"
#include "ui/preview_controller.h"

namespace fs = std::filesystem;

namespace uitest {

namespace {

// The engine's screen-capture callback: copy the requested rect of the
// offscreen render target (R8G8B8A8) to CPU memory.
bool capture_pixels(ImGuiID /*viewport_id*/, int x, int y, int w, int h, unsigned int* pixels,
                    void* user) {
    auto& hz = *static_cast<Harness*>(user);
    // Headless: the offscreen target. Attached: the swapchain's back buffer.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source = hz.rt;
    if (hz.attached) {
        if (!hz.swapchain || FAILED(hz.swapchain->GetBuffer(0, IID_PPV_ARGS(&source))))
            return false;
        D3D11_TEXTURE2D_DESC sd;
        source->GetDesc(&sd);
        hz.width = (int)sd.Width;
        hz.height = (int)sd.Height;
    }
    D3D11_TEXTURE2D_DESC d;
    source->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(hz.device->CreateTexture2D(&d, nullptr, &staging))) return false;
    hz.context->CopyResource(staging.Get(), source.Get());
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(hz.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    const auto* src = static_cast<const unsigned char*>(m.pData);
    for (int row = 0; row < h; ++row) {
        int sy = y + row;
        if (sy < 0 || sy >= hz.height) continue;
        for (int col = 0; col < w; ++col) {
            int sx = x + col;
            if (sx < 0 || sx >= hz.width) continue;
            std::memcpy(&pixels[row * w + col], src + sy * m.RowPitch + sx * 4, 4);
        }
    }
    hz.context->Unmap(staging.Get(), 0);
    return true;
}

std::string temp_root() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, buf);
    std::wstring w(buf, n);
    fs::path p = fs::path(w) / L"hydra_uitest" / std::to_wstring(GetCurrentProcessId());
    return p.u8string();
}

// Scratch folder + the app's path overrides (db, ini, preview assets).
void init_scratch(Harness& h) {
    h.temp_dir = temp_root();
    fs::create_directories(fs::u8path(h.temp_dir));
    h.db_path = h.temp_dir + "\\hydra.db";
    h.ini_path = h.temp_dir + "\\hydra_settings.ini";
    h.rules_path = h.temp_dir + "\\hydra_rules.ini";
    if (h.shots_dir.empty()) h.shots_dir = h.temp_dir;

    // The app's own path lookups now resolve to the scratch files; the real
    // app never sets these.
    hydra::app::PathOverrides po;
    po.db_path = h.db_path;
    po.ini_path = h.ini_path;
    po.rules_path = h.rules_path;
    po.asset_dir = HYDRA_ASSET_DIR;
    hydra::app::set_path_overrides(po);
}

void init_engine(Harness& h, ImGuiTestRunSpeed speed) {
    h.engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& eio = ImGuiTestEngine_GetIO(h.engine);
    eio.ConfigRunSpeed = speed;
    eio.ConfigNoThrottle = speed == ImGuiTestRunSpeed_Fast;
    eio.ConfigFixedDeltaTime = speed == ImGuiTestRunSpeed_Fast ? 1.0f / 60.0f : 0.0f;
    eio.ConfigSavedSettings = false;
    eio.ConfigMouseDrawCursor = speed != ImGuiTestRunSpeed_Fast;
    eio.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    eio.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    eio.ConfigLogToTTY = false;  // the runner prints each failed test's log itself
    eio.ConfigWatchdogWarning = 120.0f;
    eio.ConfigWatchdogKillTest = 600.0f;  // a real analysis can take a while
    eio.ScreenCaptureFunc = capture_pixels;
    eio.ScreenCaptureUserData = &h;
    h.frame_text.enabled = true;
}

}  // namespace

bool Harness::init_attached(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc) {
    attached = true;
    device = dev;
    context = ctx;
    swapchain = sc;
    init_scratch(*this);
    init_engine(*this, ImGuiTestRunSpeed_Normal);
    return true;
}

bool Harness::queue(const std::string& what) {
    if (fs::exists(fs::u8path(what)) && !fs::is_directory(fs::u8path(what))) {
        script_path = what;
        register_script_test(*this);
        ImGuiTestEngine_QueueTest(engine, ImGuiTestEngine_FindTestByName(engine, "hydra", "script"),
                                  ImGuiTestRunFlags_RunFromCommandLine);
        return true;
    }
    ImVector<ImGuiTest*> tests;
    ImGuiTestEngine_GetTestList(engine, &tests);
    int queued = 0;
    for (ImGuiTest* t : tests) {
        if (std::strcmp(t->Name, "script") == 0) continue;
        if (what == "all" || what == t->Name) {
            ImGuiTestEngine_QueueTest(engine, t, ImGuiTestRunFlags_RunFromCommandLine);
            ++queued;
        }
    }
    return queued > 0;
}

int Harness::print_results(FILE* out) {
    ImVector<ImGuiTest*> tests;
    ImGuiTestEngine_GetTestList(engine, &tests);
    int failed = 0;
    for (ImGuiTest* t : tests) {
        if (t->Output.Status == ImGuiTestStatus_Unknown) continue;  // not run
        bool ok = t->Output.Status == ImGuiTestStatus_Success;
        std::fprintf(out, "[%s] %s/%s\n", ok ? "PASS" : "FAIL", t->Category, t->Name);
        if (!ok) {
            ++failed;
            std::fprintf(out, "---- log ----\n%s---- end ----\n", t->Output.Log.Buffer.c_str());
        }
    }
    std::fflush(out);
    return failed;
}

bool Harness::init() {
    D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0, got;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &want, 1,
                                 D3D11_SDK_VERSION, &device, &got, &context))) {
        std::fprintf(stderr, "hydra_uitest: could not create a WARP D3D11 device\n");
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &rt)) ||
        FAILED(device->CreateRenderTargetView(rt.Get(), nullptr, &rtv))) {
        std::fprintf(stderr, "hydra_uitest: could not create the render target\n");
        return false;
    }

    init_scratch(*this);

    hydra::ui::ImGuiSetupOptions opts;
    opts.dpi_scale = 1.0f;
    opts.ini_file = "-";  // no persisted ImGui layout between runs
    opts.resource_dir = HYDRA_RESOURCE_DIR;
    hydra::ui::setup_imgui(opts);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    ImGui_ImplDX11_Init(device.Get(), context.Get());

    init_engine(*this, ImGuiTestRunSpeed_Fast);
    return true;
}

void Harness::frame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    if (app) hydra::ui::run_frame(*app, &frame_text);
    ImGui::Render();
    const float clear[4] = {0.10f, 0.11f, 0.13f, 1.0f};
    ID3D11RenderTargetView* views[] = {rtv.Get()};
    context->OMSetRenderTargets(1, views, nullptr);
    context->ClearRenderTargetView(rtv.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ImGuiTestEngine_PostSwap(engine);
}

void Harness::stop() {
    if (engine && !stopped) ImGuiTestEngine_Stop(engine);
    stopped = true;
    if (app && app->preview) app->preview->close();
    app.reset();
}

void Harness::shutdown() {
    stop();
    if (!attached) {
        ImGui_ImplDX11_Shutdown();
        hydra::ui::shutdown_imgui();
    }
    if (engine) ImGuiTestEngine_DestroyContext(engine);
    engine = nullptr;
    if (!keep_temp && !temp_dir.empty()) {
        std::error_code ec;
        fs::remove_all(fs::u8path(temp_dir), ec);
    }
}

void reset_app(Harness& h, const std::string& rules_text) {
    if (h.app && h.app->preview) h.app->preview->close();
    h.app.reset();

    hydra::audio::set_headless(true);
    hydra::app::set_open_in_browser([&h](const std::wstring& path) {
        h.opened_urls.push_back(path);
        return true;
    });
    // The dmleaderboards API, canned: one user whose only score is the first
    // chart of the scanned library (so the join has something to match).
    hydra::net::set_fetcher([&h](const std::string& url, const std::atomic<bool>*) {
        if (url.find("/all-users") != std::string::npos)
            return std::string(R"([{"id":"111","username":"alice","elo":1500,
                                     "stats":{"total_scores":1,"total_score":100000}}])");
        std::string md5 = (h.app && h.app->library_shown_count() > 0)
                              ? h.app->library_row_at(0).entry.md5
                              : "00000000000000000000000000000000";
        return std::string(R"({"scores":[{"identifier":")") + md5 +
               R"(","song_name":"x","artist":"y","charter_refs":["z"],"score":100000,)"
               R"("is_fc":0,"percent":95,"speed":100,"rank":1,"posted":"2026-01-01"}],)"
               R"("unknown_scores":[]})";
    });

    std::error_code ec;
    fs::remove(fs::u8path(h.db_path), ec);
    fs::remove(fs::u8path(h.temp_dir + "\\hydra_paths.html"), ec);
    fs::remove(fs::u8path(h.temp_dir + "\\hydra_dmcompare.html"), ec);
    {
        std::ofstream f(fs::u8path(h.ini_path), std::ios::trunc);
        f << "chartfolder=" << HYDRA_INPUT_DIR << "\n";
        f << "auto_open_report=0\n";
        f << "depth_value=2\n";  // keep analyses short
    }
    if (rules_text.empty()) {
        fs::remove(fs::u8path(h.rules_path), ec);
    } else {
        std::ofstream f(fs::u8path(h.rules_path), std::ios::trunc);
        f << rules_text;
    }
    h.opened_urls.clear();
    h.frame_text.text.clear();

    h.app = std::make_unique<hydra::ui::AppState>();
    h.app->set_render_device(h.device.Get(), h.context.Get());
}

bool wait_until(ImGuiTestContext* ctx, const std::function<bool()>& pred, double seconds) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!pred()) {
        // The test has already failed (a click found no such item, a check
        // failed): nothing it waits on will happen, so don't sit out the
        // timeout.
        if (ctx->IsError()) return false;
        if (std::chrono::steady_clock::now() > deadline) return false;
        ctx->Yield();
        // Fast mode spins frames flat out; a short nap keeps the worker
        // threads from being starved while we poll.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // One more frame so the screen reflects the state the caller waited for.
    ctx->Yield();
    return true;
}

bool jobs_busy(Harness& h) {
    auto& a = *h.app;
    if (a.scan_job && !a.scan_job->snapshot().finished) return true;
    if (a.batch_job && !a.batch_job->snapshot().finished) return true;
    if (a.analyze_job && !a.analyze_job->finished()) return true;
    if (a.report_job && !a.report_job->finished()) return true;
    if (a.dm_fetch_job && !a.dm_fetch_job->finished()) return true;
    if (a.dm_report_job && !a.dm_report_job->finished()) return true;
    if (a.preview && a.preview->loading()) return true;
    return false;
}

std::string visible_text(Harness& h) {
    std::string s = h.frame_text.text;
    if (h.app) {
        s += "\n";
        s += h.app->status_message;
    }
    return s;
}

namespace {

const char* yes_no(bool b) { return b ? "yes" : "no"; }

void dump_window(ImGuiTestContext* ctx, ImGuiWindow* w) {
    std::printf("window \"%s\" id=0x%08X pos=(%.0f,%.0f) size=(%.0f,%.0f)%s%s\n", w->Name, w->ID,
                w->Pos.x, w->Pos.y, w->Size.x, w->Size.y,
                (w->Flags & ImGuiWindowFlags_Popup) ? " popup" : "",
                (w->Flags & ImGuiWindowFlags_ChildWindow) ? " child" : "");
    ImGuiTestItemList items;
    ctx->GatherItems(&items, ImGuiTestRef(w->ID), 1);
    for (const ImGuiTestItemInfo& it : items) {
        std::string flags;
        if (it.ItemFlags & ImGuiItemFlags_Disabled) flags += " disabled";
        if (it.StatusFlags & ImGuiItemStatusFlags_Checkable)
            flags += (it.StatusFlags & ImGuiItemStatusFlags_Checked) ? " checked" : " unchecked";
        if (it.StatusFlags & ImGuiItemStatusFlags_Openable)
            flags += (it.StatusFlags & ImGuiItemStatusFlags_Opened) ? " opened" : " closed";
        if (it.StatusFlags & ImGuiItemStatusFlags_Inputable) flags += " inputable";
        std::printf("  %*s\"%s\" id=0x%08X rect=(%.0f,%.0f %.0fx%.0f)%s\n", it.Depth * 2, "",
                    it.DebugLabel, it.ID, it.RectFull.Min.x, it.RectFull.Min.y,
                    it.RectFull.GetWidth(), it.RectFull.GetHeight(), flags.c_str());
    }
}

}  // namespace

void dump_widgets(ImGuiTestContext* ctx, const std::string& window_name) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    bool any = false;
    for (ImGuiWindow* w : g.Windows) {
        if (!w->WasActive || w->Hidden) continue;
        if (std::strcmp(w->Name, "Debug##Default") == 0) continue;
        if (!window_name.empty() && std::strstr(w->Name, window_name.c_str()) == nullptr) continue;
        dump_window(ctx, w);
        any = true;
    }
    if (!any) std::printf("(no window matching \"%s\")\n", window_name.c_str());
}

void dump_state(Harness& h) {
    auto& a = *h.app;
    std::printf("state:\n");
    std::printf("  library_total=%lld shown=%zu search=\"%s\"\n", (long long)a.library_total,
                a.library_shown_count(), a.search.c_str());
    for (size_t i = 0; i < a.library_shown_count() && i < 20; ++i) {
        const auto& r = a.library_row_at(i);
        auto s = r.status;
        const char* st = s == hydra::store::RecordStatus::Ready   ? "current"
                         : s == hydra::store::RecordStatus::Stale ? "stale"
                                                                  : "new";
        std::printf("    row[%zu] \"%s\" - %s (%s) md5=%s status=%s\n", i, r.title.c_str(),
                    r.artist.c_str(), r.charter.c_str(), r.entry.md5.c_str(), st);
    }
    std::printf("  selected=%s panel_open=%s viewed_record=%s paths=%zu\n",
                a.selected ? a.selected->title.c_str() : "(none)", yes_no(a.details_open()),
                yes_no(a.viewed.record.has_value()),
                a.viewed.record ? a.viewed.record->paths.size() : 0);
    if (a.viewed.record && !a.viewed.record->paths.empty())
        std::printf("  best_path=%s\n", a.viewed.record->best_path().pathstring().c_str());
    std::printf("  chartmode=\"%s\" prodrums=%s bass2x=%s depth=%d auto_open_report=%s\n",
                a.settings.chartmode_key().c_str(), yes_no(a.settings.view_prodrums),
                yes_no(a.settings.view_bass2x), a.settings.depth_value,
                yes_no(a.settings.auto_open_report));
    std::printf("  jobs: scan=%s batch=%s analyze=%s report=%s dm_fetch=%s dm_report=%s\n",
                a.scan_job ? (a.scan_job->snapshot().finished ? "finished" : "running") : "-",
                a.batch_job ? (a.batch_job->snapshot().finished ? "finished" : "running") : "-",
                a.analyze_job ? (a.analyze_job->finished() ? "finished" : "running") : "-",
                a.report_job ? (a.report_job->finished() ? "finished" : "running") : "-",
                a.dm_fetch_job ? (a.dm_fetch_job->finished() ? "finished" : "running") : "-",
                a.dm_report_job ? (a.dm_report_job->finished() ? "finished" : "running") : "-");
    if (a.preview)
        std::printf("  preview: active=%s loading=%s playing=%s error=\"%s\"\n",
                    yes_no(a.preview->active()), yes_no(a.preview->loading()),
                    yes_no(a.preview->playing()), a.preview->error().c_str());
    std::printf("  status=\"%s\" dm_users=%zu opened_urls=%zu\n", a.status_message.c_str(),
                a.dm_users.size(), h.opened_urls.size());
}

bool screenshot(ImGuiTestContext* ctx, const std::string& file) {
    Harness& h = harness(ctx);
    std::string path = file;
    if (fs::u8path(path).is_relative()) path = h.shots_dir + "\\" + path;
    ImGuiCaptureArgs* args = ctx->CaptureArgs;
    ImStrncpy(args->InOutputFile, path.c_str(), IM_ARRAYSIZE(args->InOutputFile));
    args->InCaptureWindows.clear();
    args->InCaptureRect = ImRect(0, 0, (float)h.width, (float)h.height);
    args->InPadding = 0.0f;
    bool ok = ctx->CaptureScreenshot(ImGuiCaptureFlags_IncludeOtherWindows |
                                     ImGuiCaptureFlags_HideMouseCursor);
    if (ok) std::printf("screenshot: %s\n", path.c_str());
    return ok;
}

std::string escape_ref(const std::string& label) {
    std::string out;
    for (char c : label) {
        if (c == '/' || c == '#' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

// Scan testdata/input through the UI and land on the populated library.
void scan_library(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    ctx->SetRef("//Hydra");
    ctx->ItemClick("Scan library");
    IM_CHECK(wait_until(ctx, [&] { return h.app->scan_job && h.app->scan_job->snapshot().finished; }, 60));
    ctx->SetRef("//Scanning charts");
    // State, not the modal's wording: T13 rewrites "chart(s) found" with
    // real plurals in this same wave.
    IM_CHECK(h.app->scan_job->snapshot().charts_found > 0);
    ctx->ItemClick("Continue");
    ctx->Yield(2);
    ctx->SetRef("//Hydra");
    IM_CHECK(h.app->scan_job == nullptr);
    IM_CHECK(h.app->library_total > 0);
    IM_CHECK(h.app->library_shown_count() > 0);
}

// Point the ref at the song panel. It is a child window of the main window,
// and child window names are mangled, so go through WindowInfo.
void set_panel_ref(ImGuiTestContext* ctx) {
    ImGuiWindow* panel = ctx->WindowInfo("//Hydra/##songpanel").Window;
    IM_CHECK(panel != nullptr);
    ctx->SetRef(panel);
}

// Click row `index` of the library view and wait for the song panel.
void open_details(ImGuiTestContext* ctx, size_t index) {
    Harness& h = harness(ctx);
    IM_CHECK(index < h.app->library_shown_count());
    const std::string title = h.app->library_row_at(index).title;
    ctx->SetRef("//Hydra");
    ctx->ItemClick(("**/" + escape_ref(title)).c_str());
    ctx->Yield(3);
    IM_CHECK(h.app->details_open());
    IM_CHECK(h.app->selected && h.app->selected->title == title);
    set_panel_ref(ctx);
    if (ctx->IsError()) return;
    // The ImGui context outlives reset_app, so the tab bar remembers the tab a
    // previous test left selected. Land on Paths deterministically.
    ctx->ItemClick("##DetailsTabs/Paths");
}

// Narrow the library with `search` typed into the search box, then open the
// row titled `title`.
void open_titled(ImGuiTestContext* ctx, const std::string& search, const std::string& title) {
    Harness& h = harness(ctx);
    ctx->SetRef("//Hydra");
    ctx->ItemInputValue("**/##search", search.c_str());
    size_t idx = 0;
    auto find_row = [&] {
        for (size_t i = 0; i < h.app->library_shown_count(); ++i)
            if (h.app->library_row_at(i).title == title) { idx = i; return true; }
        return false;
    };
    IM_CHECK(wait_until(ctx, find_row, 5));
    open_details(ctx, idx);
}

// The analyze button's ref for the open song: its label depends on whether
// the song has a result.
std::string analyze_button_ref(Harness& h) {
    return h.app->viewed.status == hydra::store::RecordStatus::NotAnalyzed ? "**/Analyze this song"
                                                                          : "**/Re-analyze";
}

// Analyze the open song and wait for a Ready record.
void analyze_open_song(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    set_panel_ref(ctx);
    if (ctx->IsError()) return;
    ctx->ItemClick("##DetailsTabs/Paths");
    ctx->ItemClick(analyze_button_ref(h).c_str());
    IM_CHECK(wait_until(ctx, [&] { return h.app->analyze_job == nullptr; }, 300));
    IM_CHECK(h.app->viewed.status == hydra::store::RecordStatus::Ready);
}

// Shared: open chart 0's Preview and wait for the load. Returns false on error.
bool open_preview(ImGuiTestContext* ctx) {
    Harness& h = harness(ctx);
    reset_app(h);
    scan_library(ctx);
    if (ctx->IsError()) return false;
    open_details(ctx, 0);
    if (ctx->IsError()) return false;
    ctx->ItemClick("**/Preview");
    IM_CHECK_RETV(wait_until(ctx, [&] { return h.app->preview && h.app->preview->active(); }, 10), false);
    IM_CHECK_RETV(wait_until(ctx, [&] { return !h.app->preview->loading(); }, 120), false);
    IM_CHECK_RETV(h.app->preview->error().empty(), false);
    return true;
}

}  // namespace uitest
