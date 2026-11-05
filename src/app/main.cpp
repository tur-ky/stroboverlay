// Stroboverlay: a stroboscopic training overlay.

#include <dwmapi.h>
#include <objbase.h>
#include <windows.h>

#include <string>
#include <vector>

#include "app/dcomp_renderer.h"
#include "app/menu_host.h"
#include "app/overlay_window.h"
#include "app/strobe_worker.h"
#include "core/image_compose.h"
#include "core/monitors.h"
#include "core/settings.h"
#include "version.h"

namespace {

enum HotkeyId { kToggle = 1, kPanic = 2, kShowGui = 3 };
const DWORD kMenuFrameMs = 16;

std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = L'\0';
    return path;
}

class App {
public:
    bool Init(HINSTANCE instance);
    int Run();
    void Shutdown();

private:
    void ToggleStrobe();
    void UpdateVisibility();
    void OpenMenu();
    void CloseMenu();
    void DrawMenuFrame();
    void HandleMessage(const MSG& msg);
    void Save();

    std::wstring exeDir_;
    AppSettings settings_;
    std::vector<MonitorInfo> monitors_;
    MonitorInfo monitor_{};
    OverlayWindow window_;
    OverlayRenderer renderer_;
    StrobeWorker worker_;
    MenuHost menu_;
    bool menuOpen_ = false;
    bool quit_ = false;
    HWND previousForeground_ = nullptr;
};

bool App::Init(HINSTANCE instance) {
    exeDir_ = ExeDir();
    settings_ = LoadAppSettings(exeDir_ + L"config.ini");
    monitors_ = EnumerateMonitors();
    if (monitors_.empty()) return false;
    monitor_ = PickMonitor(monitors_, settings_.monitor);

    const RECT& rc = monitor_.rect;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    auto handler = [this](UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
        return menu_.HandleMessage(window_.Handle(), msg, wp, lp, result);
    };
    if (!window_.Create(instance, rc, handler)) return false;
    if (!renderer_.Init(window_.Handle(), w, h)) return false;
    renderer_.SetStrobeBitmap(ComposeStrobe(settings_.strobe.colorRgb, nullptr, nullptr, false, w, h));
    if (!menu_.Init(window_.Handle(), &renderer_, GetDpiForWindow(window_.Handle()) / 96.0f)) {
        return false;
    }

    if (!worker_.Start([this](bool show) { renderer_.ShowStrobe(show); }, exeDir_ + L"logs\\")) {
        return false;
    }
    worker_.SetDurations(StrobeDurations(settings_.strobe, monitor_.refreshHz));

    RegisterHotKey(nullptr, kToggle, settings_.toggle.mods | MOD_NOREPEAT, settings_.toggle.vk);
    RegisterHotKey(nullptr, kPanic, settings_.panic.mods | MOD_NOREPEAT, settings_.panic.vk);
    RegisterHotKey(nullptr, kShowGui, settings_.showGui.mods | MOD_NOREPEAT, settings_.showGui.vk);
    OpenMenu();
    return true;
}

void App::UpdateVisibility() {
    window_.SetVisible(worker_.Running() || menuOpen_);
}

void App::ToggleStrobe() {
    bool next = !worker_.Running();
    worker_.SetRunning(next);
    if (!next) DwmFlush();
    UpdateVisibility();
}

void App::OpenMenu() {
    if (menuOpen_) return;
    previousForeground_ = GetForegroundWindow();
    menuOpen_ = true;
    DrawMenuFrame();
    window_.SetMenuInteractive(true);
    UpdateVisibility();
    renderer_.ShowMenuLayer(true);
    SetForegroundWindow(window_.Handle());
}

void App::CloseMenu() {
    if (!menuOpen_) return;
    menuOpen_ = false;
    renderer_.ShowMenuLayer(false);
    window_.SetMenuInteractive(false);
    UpdateVisibility();
    if (previousForeground_ && IsWindow(previousForeground_)) {
        SetForegroundWindow(previousForeground_);
    }
    Save();
}

void App::DrawMenuFrame() {
    MenuContext ctx;
    ctx.settings = &settings_;
    ctx.running = worker_.Running();
    ctx.monitors = monitors_;
    ctx.dpiScale = GetDpiForWindow(window_.Handle()) / 96.0f;
    ctx.detectedHz = monitor_.refreshHz;

    MenuActions actions = menu_.Frame(ctx);
    if (actions.strobeSettingsChanged) {
        worker_.SetDurations(StrobeDurations(settings_.strobe, monitor_.refreshHz));
    }
    if (actions.toggleStrobe) ToggleStrobe();
    if (actions.closeMenu) CloseMenu();
}

void App::HandleMessage(const MSG& msg) {
    if (msg.message == WM_QUIT) {
        quit_ = true;
    } else if (msg.message == WM_HOTKEY) {
        if (msg.wParam == kToggle) ToggleStrobe();
        if (msg.wParam == kPanic) quit_ = true;
        if (msg.wParam == kShowGui) {
            if (menuOpen_) {
                CloseMenu();
            } else {
                OpenMenu();
            }
        }
    } else {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

int App::Run() {
    MSG msg;
    while (!quit_) {
        if (!menuOpen_) {
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) break;
            HandleMessage(msg);
            continue;
        }
        while (!quit_ && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) HandleMessage(msg);
        if (quit_ || !menuOpen_) continue;
        DrawMenuFrame();
        // About 60 fps is plenty for the menu, and input wakes it early.
        MsgWaitForMultipleObjects(0, nullptr, FALSE, kMenuFrameMs, QS_ALLINPUT);
    }
    return 0;
}

void App::Save() {
    SaveAppSettings(exeDir_ + L"config.ini", settings_);
}

void App::Shutdown() {
    worker_.Stop();
    Save();
    UnregisterHotKey(nullptr, kToggle);
    UnregisterHotKey(nullptr, kPanic);
    UnregisterHotKey(nullptr, kShowGui);
    menu_.Shutdown();
    window_.Destroy();
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    App app;
    if (!app.Init(instance)) {
        MessageBoxW(nullptr, L"Couldn't start the overlay.", L"Stroboverlay", MB_OK | MB_ICONERROR);
        return 1;
    }
    int code = app.Run();
    app.Shutdown();
    CoUninitialize();
    return code;
}
