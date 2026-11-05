// Stroboverlay: a stroboscopic training overlay.

#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <windows.h>

#include <string>
#include <vector>

#include "app/asset_store.h"
#include "app/dcomp_renderer.h"
#include "app/menu_host.h"
#include "app/notify.h"
#include "app/overlay_window.h"
#include "app/strobe_worker.h"
#include "core/image_compose.h"
#include "core/monitors.h"
#include "core/process_probe.h"
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
    void HandleHotkey(WPARAM id);
    void Quit();
    void Save();
    void Recompose();
    void RefreshAssets();
    void HandleProfiles(const MenuActions& actions);
    std::wstring ProfilePath(const std::wstring& name) const;
    void ImportInto(const wchar_t* subdir, std::wstring& target);
    void ApplyMonitor();
    void RefreshMonitors();
    void RegisterHotkeys();
    bool RecoverDevice();
    bool HandleWindowMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);

    std::wstring exeDir_;
    AppSettings settings_;
    std::vector<MonitorInfo> monitors_;
    MonitorInfo monitor_{};
    OverlayWindow window_;
    OverlayRenderer renderer_;
    StrobeWorker worker_;
    MenuHost menu_;
    AssetStore assets_;
    std::vector<std::wstring> images_, masks_, profiles_;
    std::wstring warning_, displayWarning_, hotkeyWarning_;
    bool menuOpen_ = false;
    bool quit_ = false;
    bool importing_ = false;
    bool canReopenMenu_ = true;
    HWND previousForeground_ = nullptr;
};

bool App::Init(HINSTANCE instance) {
    exeDir_ = ExeDir();
    settings_ = LoadAppSettings(exeDir_ + L"config.ini");
    monitors_ = EnumerateMonitors();
    if (monitors_.empty()) return false;
    monitor_ = PickMonitor(monitors_, settings_.monitor);
    if (!settings_.monitor.empty() && monitor_.device != settings_.monitor) {
        displayWarning_ = L"Saved monitor isn't connected, using the primary.";
    }

    const RECT& rc = monitor_.rect;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    auto handler = [this](UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
        return HandleWindowMessage(msg, wp, lp, result);
    };
    if (!window_.Create(instance, rc, handler)) return false;
    if (!renderer_.Init(window_.Handle(), w, h)) return false;
    assets_.Init(exeDir_, renderer_.Device());
    RefreshAssets();
    Recompose();
    if (!menu_.Init(window_.Handle(), &renderer_, MenuScaleForDpi(GetDpiForWindow(window_.Handle())))) {
        return false;
    }

    if (!worker_.Start([this](bool show) { renderer_.ShowStrobe(show); }, exeDir_ + L"logs\\")) {
        return false;
    }
    worker_.SetDurations(StrobeDurations(settings_.strobe, monitor_.refreshHz));

    RegisterHotkeys();
    OpenMenu();
    return true;
}

void App::UpdateVisibility() {
    window_.SetVisible(worker_.Running() || menuOpen_);
}

void App::RegisterHotkeys() {
    // Tied to the window, not the thread, so they still arrive while a modal
    // dialog runs its own message loop.
    const auto reg = [this](int id, const Hotkey& key) {
        return RegisterHotKey(window_.Handle(), id, key.mods | MOD_NOREPEAT, key.vk) != 0;
    };
    const bool toggleOk = reg(kToggle, settings_.toggle);
    const bool panicOk = reg(kPanic, settings_.panic);
    const bool showOk = reg(kShowGui, settings_.showGui);
    canReopenMenu_ = panicOk || showOk;
    if (toggleOk && panicOk && showOk) return;
    hotkeyWarning_ = L"A hotkey is in use by another app. Change it in config.ini.";
    ShowDesktopNotice(window_.Handle(), L"Stroboverlay", hotkeyWarning_.c_str());
}

void App::ToggleStrobe() {
    bool next = !worker_.Running();
    if (next && IsProcessRunning({L"faceitclient.exe", L"faceitservice.exe"})) {
        ShowDesktopNotice(window_.Handle(), L"Stroboverlay",
                          L"FACEIT is running, so the overlay may not show.");
    }
    worker_.SetRunning(next);
    if (!next) DwmFlush();
    UpdateVisibility();
}

void App::OpenMenu() {
    if (menuOpen_) return;
    previousForeground_ = GetForegroundWindow();
    menuOpen_ = true;
    RefreshAssets();
    DrawMenuFrame();
    window_.SetMenuInteractive(true);
    UpdateVisibility();
    renderer_.ShowMenuLayer(true);
    SetForegroundWindow(window_.Handle());
}

void App::CloseMenu() {
    // With no hotkey left to get back, the menu is the only way to stop the strobe.
    if (!menuOpen_ || !canReopenMenu_) return;
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
    if (renderer_.DeviceLost() && !RecoverDevice()) return;
    MenuContext ctx;
    ctx.settings = &settings_;
    ctx.running = worker_.Running();
    ctx.monitors = monitors_;
    ctx.dpiScale = MenuScaleForDpi(GetDpiForWindow(window_.Handle()));
    ctx.detectedHz = monitor_.refreshHz;
    ctx.activeMonitor = monitor_.device;
    ctx.images = images_;
    ctx.masks = masks_;
    ctx.profiles = profiles_;
    for (const std::wstring* text : {&warning_, &displayWarning_, &hotkeyWarning_}) {
        if (text->empty()) continue;
        if (!ctx.warning.empty()) ctx.warning += L" ";
        ctx.warning += *text;
    }
    ctx.thumbnail = [this](const std::wstring& file) -> void* { return assets_.Thumbnail(file); };

    const StrobeSettings before = settings_.strobe;
    MenuActions actions = menu_.Frame(ctx);
    if (actions.importImage) {
        ImportInto(L"images", settings_.strobe.imageFile);
        actions.strobeSettingsChanged = true;
    }
    if (actions.importMask) {
        ImportInto(L"masks", settings_.strobe.maskFile);
        actions.strobeSettingsChanged = true;
    }
    if (actions.strobeSettingsChanged) {
        worker_.SetDurations(StrobeDurations(settings_.strobe, monitor_.refreshHz));
        if (NeedsRecompose(before, settings_.strobe)) Recompose();
    }
    HandleProfiles(actions);
    if (!actions.selectMonitor.empty()) {
        settings_.monitor = actions.selectMonitor;
        RefreshMonitors();
    }
    if (actions.toggleStrobe) ToggleStrobe();
    if (actions.closeMenu) CloseMenu();
    if (actions.quit) Quit();
}

void App::ApplyMonitor() {
    const RECT& rc = monitor_.rect;
    menu_.ReleaseTargets();
    window_.Place(rc);
    renderer_.Resize(rc.right - rc.left, rc.bottom - rc.top);
    Recompose();
    worker_.SetDurations(StrobeDurations(settings_.strobe, monitor_.refreshHz));
    menu_.SetDpiScale(MenuScaleForDpi(GetDpiForWindow(window_.Handle())));
}

void App::RefreshMonitors() {
    std::vector<MonitorInfo> monitors = EnumerateMonitors();
    if (monitors.empty()) return;
    monitors_ = std::move(monitors);

    MonitorInfo next = PickMonitor(monitors_, settings_.monitor);
    const bool missing = !settings_.monitor.empty() && next.device != settings_.monitor;
    displayWarning_ = missing ? L"Saved monitor isn't connected, using the primary." : L"";

    const RECT& a = monitor_.rect;
    const RECT& b = next.rect;
    const bool moved = next.device != monitor_.device || a.left != b.left || a.top != b.top ||
                       a.right != b.right || a.bottom != b.bottom;
    monitor_ = next;
    if (moved) {
        ApplyMonitor();
    } else {
        worker_.SetDurations(StrobeDurations(settings_.strobe, monitor_.refreshHz));
    }
}

bool App::RecoverDevice() {
    const bool wasRunning = worker_.Running();
    worker_.SetRunning(false);
    DwmFlush();
    menu_.ReleaseTargets();
    menu_.Shutdown();
    renderer_.Reset();

    const RECT& rc = monitor_.rect;
    const bool ok = renderer_.Init(window_.Handle(), rc.right - rc.left, rc.bottom - rc.top) &&
                    menu_.Init(window_.Handle(), &renderer_,
                               MenuScaleForDpi(GetDpiForWindow(window_.Handle())));
    if (!ok) {
        ShowDesktopNotice(window_.Handle(), L"Stroboverlay",
                          L"The graphics device failed, so the strobe has stopped.");
        Quit();
        return false;
    }
    assets_.Init(exeDir_, renderer_.Device());
    Recompose();
    if (menuOpen_) renderer_.ShowMenuLayer(true);
    worker_.SetRunning(wasRunning);
    return true;
}

void App::Quit() {
    // Stop now rather than on the way out: a modal dialog may be holding the loop.
    worker_.SetRunning(false);
    window_.SetVisible(false);
    quit_ = true;
    PostQuitMessage(0);
}

void App::HandleHotkey(WPARAM id) {
    if (id == kToggle) ToggleStrobe();
    if (id == kPanic) Quit();
    if (id == kShowGui && !importing_) {
        if (menuOpen_) {
            CloseMenu();
        } else {
            OpenMenu();
        }
    }
}

bool App::HandleWindowMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    switch (msg) {
        case WM_HOTKEY:
            HandleHotkey(wp);
            return true;
        case WM_CLOSE:
            Quit();
            return true;
        case WM_TIMER:
            if (!quit_ && renderer_.DeviceLost()) RecoverDevice();
            return true;
        case kOpenMenuMessage:
            OpenMenu();
            return true;
        case WM_DISPLAYCHANGE:
            RefreshMonitors();
            return true;
        case WM_DPICHANGED:
            menu_.SetDpiScale(MenuScaleForDpi(HIWORD(wp)));
            return true;
        case kNoticeMessage:
            if (LOWORD(lp) == NIN_BALLOONHIDE || LOWORD(lp) == NIN_BALLOONTIMEOUT ||
                LOWORD(lp) == NIN_BALLOONUSERCLICK) {
                RemoveDesktopNotice(window_.Handle());
            }
            return true;
        default:
            return menu_.HandleMessage(window_.Handle(), msg, wp, lp, result);
    }
}

void App::Recompose() {
    const RECT& rc = monitor_.rect;
    renderer_.SetStrobeBitmap(
        assets_.BuildStrobe(settings_.strobe, rc.right - rc.left, rc.bottom - rc.top, warning_));
}

void App::RefreshAssets() {
    images_ = assets_.List(L"images");
    masks_ = assets_.List(L"masks");
    profiles_ = ListProfiles(exeDir_ + L"profiles\\");
}

std::wstring App::ProfilePath(const std::wstring& name) const {
    return exeDir_ + L"profiles\\" + name + L".ini";
}

void App::HandleProfiles(const MenuActions& actions) {
    if (!actions.saveProfile.empty()) {
        CreateDirectoryW((exeDir_ + L"profiles").c_str(), nullptr);
        SaveProfile(ProfilePath(actions.saveProfile), settings_.strobe);
        RefreshAssets();
    }
    if (!actions.deleteProfile.empty()) {
        DeleteFileW(ProfilePath(actions.deleteProfile).c_str());
        RefreshAssets();
    }
    if (!actions.loadProfile.empty()) {
        settings_.strobe = LoadProfile(ProfilePath(actions.loadProfile), StrobeSettings{});
        ClampTiming(settings_.strobe.timing);
        worker_.SetDurations(StrobeDurations(settings_.strobe, monitor_.refreshHz));
        Recompose();
    }
}

void App::ImportInto(const wchar_t* subdir, std::wstring& target) {
    std::wstring relative;
    importing_ = true;
    const bool imported = assets_.Import(window_.Handle(), subdir, relative);
    importing_ = false;
    if (imported) {
        target = relative;
        RefreshAssets();
    }
}

void App::HandleMessage(const MSG& msg) {
    if (msg.message == WM_QUIT) {
        quit_ = true;
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
    UnregisterHotKey(window_.Handle(), kToggle);
    UnregisterHotKey(window_.Handle(), kPanic);
    UnregisterHotKey(window_.Handle(), kShowGui);
    menu_.Shutdown();
    RemoveDesktopNotice(window_.Handle());
    window_.Destroy();
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // A second overlay would strobe with no hotkeys able to reach it.
    HANDLE instanceLock = CreateMutexW(nullptr, TRUE, L"Local\\stroboverlay_single_instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND running = FindWindowW(L"StrobeOverlayClass", nullptr);
        if (running) {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(running, kOpenMenuMessage, 0, 0);
        }
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    App app;
    if (!app.Init(instance)) {
        MessageBoxW(nullptr, L"Couldn't start the overlay.", L"Stroboverlay", MB_OK | MB_ICONERROR);
        return 1;
    }
    int code = app.Run();
    app.Shutdown();
    CoUninitialize();
    if (instanceLock) CloseHandle(instanceLock);
    return code;
}
