// Stroboverlay: a stroboscopic training overlay.

#include <dwmapi.h>
#include <objbase.h>
#include <windows.h>

#include <string>
#include <vector>

#include "app/dcomp_renderer.h"
#include "app/overlay_window.h"
#include "app/strobe_worker.h"
#include "core/image_compose.h"
#include "core/monitors.h"
#include "core/settings.h"
#include "version.h"

namespace {

enum HotkeyId { kToggle = 1, kPanic = 2, kShowGui = 3 };

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

    std::wstring exeDir_;
    AppSettings settings_;
    std::vector<MonitorInfo> monitors_;
    MonitorInfo monitor_{};
    OverlayWindow window_;
    OverlayRenderer renderer_;
    StrobeWorker worker_;
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
    auto handler = [](UINT, WPARAM, LPARAM, LRESULT&) { return false; };
    if (!window_.Create(instance, rc, handler)) return false;
    if (!renderer_.Init(window_.Handle(), w, h)) return false;
    renderer_.SetStrobeBitmap(ComposeStrobe(settings_.strobe.colorRgb, nullptr, nullptr, false, w, h));

    if (!worker_.Start([this](bool show) { renderer_.ShowStrobe(show); }, exeDir_ + L"logs\\")) {
        return false;
    }
    worker_.SetDurations(StrobeDurations(settings_.strobe, monitor_.refreshHz));

    RegisterHotKey(nullptr, kToggle, settings_.toggle.mods | MOD_NOREPEAT, settings_.toggle.vk);
    RegisterHotKey(nullptr, kPanic, settings_.panic.mods | MOD_NOREPEAT, settings_.panic.vk);
    RegisterHotKey(nullptr, kShowGui, settings_.showGui.mods | MOD_NOREPEAT, settings_.showGui.vk);
    return true;
}

void App::UpdateVisibility() {
    window_.SetVisible(worker_.Running());
}

void App::ToggleStrobe() {
    bool next = !worker_.Running();
    if (next) {
        worker_.SetRunning(true);
        UpdateVisibility();
    } else {
        worker_.SetRunning(false);
        DwmFlush();
        UpdateVisibility();
    }
}

int App::Run() {
    MSG msg;
    BOOL ret;
    while ((ret = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (ret == -1) break;
        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == kToggle) ToggleStrobe();
            if (msg.wParam == kPanic) PostQuitMessage(0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

void App::Shutdown() {
    worker_.Stop();
    UnregisterHotKey(nullptr, kToggle);
    UnregisterHotKey(nullptr, kPanic);
    UnregisterHotKey(nullptr, kShowGui);
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
