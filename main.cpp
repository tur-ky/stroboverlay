// Win32 application implementing a stroboscopic overlay.
// Keeps GUI + layered overlay, adds high-res waitable timers,
// click-through, INI config, watchdog, CSV drift logging, and BUILD_ID.

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <strsafe.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdint.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>
#pragma comment(lib, "Dwmapi.lib")

#include "version.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

// ----------------- GUI control IDs -----------------
enum {
    IDC_MODE_MS      = 1001,
    IDC_MODE_FRAMES  = 1002,
    IDC_EDIT_MS_ON   = 1101,
    IDC_EDIT_MS_OFF  = 1102,
    IDC_EDIT_FR_ON   = 1103,
    IDC_EDIT_FR_OFF  = 1104,
    IDC_EDIT_FPS     = 1105,
    IDC_BUTTON_START = 1201
};

// ----------------- Hotkeys -----------------
static const UINT HOTKEY_ID_TOGGLE  = 1;
static const UINT HOTKEY_ID_QUIT    = 2;
static const UINT HOTKEY_ID_SHOWGUI = 3;

// ----------------- Config -----------------
enum class TimingMode { Ms, Frames };
struct StrobeConfig {
    TimingMode mode = TimingMode::Ms;
    double msOn  = 100.0;
    double msOff = 100.0;
    int framesOn  = 50;
    int framesOff = 50;
    double refreshHz = 400.0;
    bool running = false;
};

// ----------------- Globals -----------------
static HWND g_hOverlay = nullptr;
static HWND g_hGui     = nullptr;
static HANDLE g_hWorkerThread = nullptr;
static HANDLE g_hEventStop    = nullptr;
static HANDLE g_hEventUpdate  = nullptr;
static CRITICAL_SECTION g_configLock;
static StrobeConfig g_config;

static FILE* g_logFile = nullptr;
static LARGE_INTEGER g_qpf = {0};

// ----------------- Fwds -----------------
LRESULT CALLBACK OverlayWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK GuiWndProc(HWND, UINT, WPARAM, LPARAM);
DWORD   WINAPI   WorkerThreadProc(LPVOID);
void    ToggleOverlay(bool enable);
static  void     UpdateOverlayDimensions();
static  void     OpenCsvLog();
static  void     CloseCsvLog();
static  void     CsvWriteRow(long long seq, const wchar_t* phase,
                             double expected_ms, double actual_ms,
                             double drift_ms, double overshoot_ms,
                             long long qpc_now, const wchar_t* notes);

// ----------------- Utilities -----------------
static void GetModuleDir(wchar_t* dir, size_t cch) {
    if (!dir || cch == 0) return;
    dir[0] = L'\0';
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    StringCchCopyW(dir, cch, path);
}

static int ReadIniInt(const wchar_t* file, const wchar_t* section,
                      const wchar_t* key, int defVal) {
    return GetPrivateProfileIntW(section, key, defVal, file);
}

static void LoadConfigFromIni() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    wcscat_s(path, MAX_PATH, L"config.ini");

    StrobeConfig cfg = g_config;
    int onFrames  = ReadIniInt(path, L"overlay", L"on_frames",  cfg.framesOn);
    int offFrames = ReadIniInt(path, L"overlay", L"off_frames", cfg.framesOff);
    int hz        = ReadIniInt(path, L"overlay", L"refresh_hz", (int)cfg.refreshHz);
    if (onFrames  >= 0 && onFrames  < 10000) cfg.framesOn  = onFrames;
    if (offFrames >= 0 && offFrames < 10000) cfg.framesOff = offFrames;
    if (hz > 0 && hz < 10000) cfg.refreshHz = (double)hz;

    wchar_t units[64] = {0};
    GetPrivateProfileStringW(L"overlay", L"units", L"frames", units, 63, path);
    cfg.mode = (_wcsicmp(units, L"time") == 0) ? TimingMode::Ms : TimingMode::Frames;

    int msOn  = ReadIniInt(path, L"overlay", L"on_ms",  (int)cfg.msOn);
    int msOff = ReadIniInt(path, L"overlay", L"off_ms", (int)cfg.msOff);
    if (msOn  >= 0 && msOn  < 100000) cfg.msOn  = (double)msOn;
    if (msOff >= 0 && msOff < 100000) cfg.msOff = (double)msOff;

    EnterCriticalSection(&g_configLock);
    g_config = cfg;
    LeaveCriticalSection(&g_configLock);
}

// ----------------- Overlay sizing -----------------
static void UpdateOverlayDimensions() {
    POINT origin = {0, 0};
    HMONITOR hMon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return;

    const int x = mi.rcMonitor.left;
    const int y = mi.rcMonitor.top;
    const int w = mi.rcMonitor.right  - mi.rcMonitor.left;
    const int h = mi.rcMonitor.bottom - mi.rcMonitor.top;

    SetWindowPos(
        g_hOverlay, HWND_TOPMOST,
        x, y, w, h,
        SWP_SHOWWINDOW | SWP_NOACTIVATE
    );
    InvalidateRect(g_hOverlay, nullptr, FALSE);
}

// ----------------- Logging -----------------
static void OpenCsvLog() {
    if (g_logFile) return;
    wchar_t base[MAX_PATH]; GetModuleDir(base, MAX_PATH);

    wchar_t logsDir[MAX_PATH];
    StringCchCopyW(logsDir, MAX_PATH, base);
    StringCchCatW(logsDir, MAX_PATH, L"logs");
    CreateDirectoryW(logsDir, nullptr);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t fileName[128];
    StringCchPrintfW(fileName, 128, L"drift_%04u%02u%02u_%02u%02u%02u.csv",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    wchar_t path[MAX_PATH];
    StringCchCopyW(path, MAX_PATH, logsDir);
    StringCchCatW(path, MAX_PATH, L"\\");
    StringCchCatW(path, MAX_PATH, fileName);

    _wfopen_s(&g_logFile, path, L"w, ccs=UTF-8");
    if (g_logFile) {
        fwprintf(g_logFile, L"build_id,%s\n", BUILD_ID_W);
        fwprintf(g_logFile, L"qpc_frequency,%lld\n", (long long)g_qpf.QuadPart);
        fwprintf(g_logFile, L"columns,seq,phase,expected_ms,actual_ms,drift_ms,overshoot_ms,qpc,notes\n");
        fflush(g_logFile);
    }
}
static void CloseCsvLog() { if (g_logFile) { fflush(g_logFile); fclose(g_logFile); g_logFile = nullptr; } }
static void CsvWriteRow(long long seq, const wchar_t* phase, double expected_ms, double actual_ms,
                        double drift_ms, double overshoot_ms, long long qpc_now, const wchar_t* notes) {
    if (!g_logFile) return;
    fwprintf(g_logFile, L"%lld,%s,%.3f,%.3f,%.3f,%.3f,%lld,%s\n",
             seq, phase ? phase : L"", expected_ms, actual_ms, drift_ms, overshoot_ms,
             qpc_now, notes ? notes : L"");
    fflush(g_logFile);
}

// ----------------- GUI helpers -----------------
static double ReadDouble(HWND hwnd, int id) { wchar_t b[64]={0}; GetWindowTextW(GetDlgItem(hwnd,id),b,63); return _wtof(b); }
static int    ReadInt   (HWND hwnd, int id) { wchar_t b[64]={0}; GetWindowTextW(GetDlgItem(hwnd,id),b,63); return _wtoi(b); }

static void UpdateConfigFromGui(HWND hwnd) {
    StrobeConfig cfg; { EnterCriticalSection(&g_configLock); cfg = g_config; LeaveCriticalSection(&g_configLock); }
    BOOL msMode = (IsDlgButtonChecked(hwnd, IDC_MODE_MS) == BST_CHECKED);
    cfg.mode = msMode ? TimingMode::Ms : TimingMode::Frames;
    cfg.msOn  = ReadDouble(hwnd, IDC_EDIT_MS_ON);
    cfg.msOff = ReadDouble(hwnd, IDC_EDIT_MS_OFF);
    cfg.framesOn  = ReadInt(hwnd, IDC_EDIT_FR_ON);
    cfg.framesOff = ReadInt(hwnd, IDC_EDIT_FR_OFF);
    cfg.refreshHz = ReadDouble(hwnd, IDC_EDIT_FPS);
    EnterCriticalSection(&g_configLock); g_config = cfg; LeaveCriticalSection(&g_configLock);
    if (g_hEventUpdate) SetEvent(g_hEventUpdate);
}

void ToggleOverlay(bool enable) {
    EnterCriticalSection(&g_configLock); g_config.running = enable; LeaveCriticalSection(&g_configLock);
    if (g_hEventUpdate) SetEvent(g_hEventUpdate);
}

// ----------------- Windows -----------------
static HWND CreateOverlayWindow(HINSTANCE hInst) {
    WNDCLASSW wc{}; wc.lpfnWndProc = OverlayWndProc; wc.hInstance = hInst;
    wc.lpszClassName = L"StrobeOverlayClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); // we still paint explicitly
    RegisterClassW(&wc);

    // Primary monitor rect
    POINT origin{0,0};
    HMONITOR hMon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(hMon, &mi);
    RECT rc = mi.rcMonitor;

    DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    DWORD st = WS_POPUP;

    HWND hwnd = CreateWindowExW(
        ex, wc.lpszClassName, L"Stroboverlay", st,
        rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top,
        nullptr, nullptr, hInst, nullptr
    );
    if (hwnd) {
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA); // start transparent
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd);
        g_hOverlay = hwnd;
        UpdateOverlayDimensions(); // exact sizing, DPI-safe
    }
    return hwnd;
}

static HWND CreateGuiWindow(HINSTANCE hInst) {
    const wchar_t* cls = L"StroboGuiClass";
    WNDCLASSW wc{}; wc.lpfnWndProc = GuiWndProc; wc.hInstance = hInst; wc.lpszClassName = cls;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, cls, L"Stroboverlay Configuration",
                                WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 320, 230,
                                nullptr, nullptr, hInst, nullptr);

    CreateWindowW(L"STATIC", L"Timing mode:", WS_CHILD|WS_VISIBLE, 10,10,100,20, hwnd, nullptr, hInst, nullptr);
    CreateWindowW(L"BUTTON", L"Milliseconds", WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|WS_GROUP, 10,30,130,20, hwnd, (HMENU)IDC_MODE_MS, hInst, nullptr);
    CreateWindowW(L"BUTTON", L"Frames",       WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON,                      160,30,80,20, hwnd, (HMENU)IDC_MODE_FRAMES, hInst, nullptr);

    CreateWindowW(L"STATIC", L"On duration (ms):",  WS_CHILD|WS_VISIBLE, 10,60,130,20, hwnd, nullptr, hInst, nullptr);
    CreateWindowW(L"EDIT",   L"100", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER, 160,60,100,20, hwnd, (HMENU)IDC_EDIT_MS_ON,  hInst, nullptr);
    CreateWindowW(L"STATIC", L"Off duration (ms):", WS_CHILD|WS_VISIBLE, 10,85,130,20, hwnd, nullptr, hInst, nullptr);
    CreateWindowW(L"EDIT",   L"100", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER, 160,85,100,20, hwnd, (HMENU)IDC_EDIT_MS_OFF, hInst, nullptr);

    CreateWindowW(L"STATIC", L"On frames:",  WS_CHILD|WS_VISIBLE, 10,60,130,20, hwnd, nullptr, hInst, nullptr);
    CreateWindowW(L"EDIT",   L"50", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER, 160,60,100,20, hwnd, (HMENU)IDC_EDIT_FR_ON,  hInst, nullptr);
    CreateWindowW(L"STATIC", L"Off frames:", WS_CHILD|WS_VISIBLE, 10,85,130,20, hwnd, nullptr, hInst, nullptr);
    CreateWindowW(L"EDIT",   L"50", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER, 160,85,100,20, hwnd, (HMENU)IDC_EDIT_FR_OFF, hInst, nullptr);

    CreateWindowW(L"STATIC", L"Refresh rate (Hz):", WS_CHILD|WS_VISIBLE, 10,110,130,20, hwnd, nullptr, hInst, nullptr);
    CreateWindowW(L"EDIT",   L"400", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER, 160,110,100,20, hwnd, (HMENU)IDC_EDIT_FPS, hInst, nullptr);

    CreateWindowW(L"BUTTON", L"Start", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 100,150,120,30, hwnd, (HMENU)IDC_BUTTON_START, hInst, nullptr);

    EnterCriticalSection(&g_configLock);
    bool msMode = (g_config.mode == TimingMode::Ms);
    LeaveCriticalSection(&g_configLock);
    CheckRadioButton(hwnd, IDC_MODE_MS, IDC_MODE_FRAMES, msMode ? IDC_MODE_MS : IDC_MODE_FRAMES);
    EnableWindow(GetDlgItem(hwnd, IDC_EDIT_MS_ON),  msMode);
    EnableWindow(GetDlgItem(hwnd, IDC_EDIT_MS_OFF), msMode);
    EnableWindow(GetDlgItem(hwnd, IDC_EDIT_FR_ON),  !msMode);
    EnableWindow(GetDlgItem(hwnd, IDC_EDIT_FR_OFF), !msMode);

    return hwnd;
}

// ----------------- Worker thread -----------------
DWORD WINAPI WorkerThreadProc(LPVOID) {
    QueryPerformanceFrequency(&g_qpf);
    OpenCsvLog();

    HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr,
                                           CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                           TIMER_ALL_ACCESS);
    if (!hTimer) hTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    if (!hTimer) return 0;

    LARGE_INTEGER qpcStart{}; QueryPerformanceCounter(&qpcStart);
    const double WATCHDOG_MS = 250.0;
    long long seq = 0;
    bool overlayShown = false;

    while (true) {
        // snapshot config
        StrobeConfig cfg;
        EnterCriticalSection(&g_configLock); cfg = g_config; LeaveCriticalSection(&g_configLock);

        if (!cfg.running) {
            if (overlayShown) {
                SetLayeredWindowAttributes(g_hOverlay, 0, 0, LWA_ALPHA);
                RedrawWindow(g_hOverlay, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
                overlayShown = false;
            }
            CancelWaitableTimer(hTimer);
            HANDLE hIdle[2] = { g_hEventStop, g_hEventUpdate };
            DWORD r = WaitForMultipleObjects(2, hIdle, FALSE, INFINITE);
            if (r == WAIT_OBJECT_0) { CloseHandle(hTimer); CloseCsvLog(); return 0; }
            continue;
        }

        // resolve durations
        double onMs, offMs;
        if (cfg.mode == TimingMode::Ms) { onMs = cfg.msOn; offMs = cfg.msOff; }
        else {
            double frame = (cfg.refreshHz > 0.0) ? (1000.0 / cfg.refreshHz) : 2.5;
            onMs  = cfg.framesOn  * frame;
            offMs = cfg.framesOff * frame;
        }
        if (onMs < 0) onMs = 0;
        if (offMs < 0) offMs = 0;

        // ON: show black
        SetWindowPos(g_hOverlay, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetLayeredWindowAttributes(g_hOverlay, 0, 255, LWA_ALPHA);
        ShowWindow(g_hOverlay, SW_SHOWNOACTIVATE);
        RedrawWindow(g_hOverlay, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        DwmFlush();
        overlayShown = true;

        LARGE_INTEGER qpcOnStart{}; QueryPerformanceCounter(&qpcOnStart);
        double onStartMs = (double)(qpcOnStart.QuadPart - qpcStart.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
        double onExpectedEnd = onStartMs + onMs;

        LARGE_INTEGER due{}; due.QuadPart = -(LONGLONG)(onMs * 10000.0);
        SetWaitableTimerEx(hTimer, &due, 0, nullptr, nullptr, nullptr, 0);

        HANDLE hOn[3] = { g_hEventStop, g_hEventUpdate, hTimer };
        DWORD wr = WaitForMultipleObjects(3, hOn, FALSE, INFINITE);
        if (wr == WAIT_OBJECT_0) { CloseHandle(hTimer); CloseCsvLog(); return 0; }
        if (wr == WAIT_OBJECT_0 + 1) continue;

        LARGE_INTEGER qpcOnEnd{}; QueryPerformanceCounter(&qpcOnEnd);
        double onEndMs = (double)(qpcOnEnd.QuadPart - qpcStart.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
        double onDrift = onEndMs - onExpectedEnd;
        double onOver  = (onDrift > 0.0) ? onDrift : 0.0;
        if (onOver > WATCHDOG_MS) {
            SetLayeredWindowAttributes(g_hOverlay, 0, 0, LWA_ALPHA);
            RedrawWindow(g_hOverlay, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            overlayShown = false;
        }
        CsvWriteRow(++seq, L"on", onExpectedEnd, onEndMs, onDrift, onOver, (long long)qpcOnEnd.QuadPart, (onOver>WATCHDOG_MS)?L"STALL":L"");

        // OFF: hide (alpha 0, keep window alive and click-through)
        SetLayeredWindowAttributes(g_hOverlay, 0, 0, LWA_ALPHA);
        RedrawWindow(g_hOverlay, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        overlayShown = false;

        LARGE_INTEGER qpcOffStart{}; QueryPerformanceCounter(&qpcOffStart);
        double offStartMs = (double)(qpcOffStart.QuadPart - qpcStart.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
        double offExpectedEnd = offStartMs + offMs;

        due.QuadPart = -(LONGLONG)(offMs * 10000.0);
        SetWaitableTimerEx(hTimer, &due, 0, nullptr, nullptr, nullptr, 0);

        HANDLE hOff[3] = { g_hEventStop, g_hEventUpdate, hTimer };
        wr = WaitForMultipleObjects(3, hOff, FALSE, INFINITE);
        if (wr == WAIT_OBJECT_0) { CloseHandle(hTimer); CloseCsvLog(); return 0; }
        if (wr == WAIT_OBJECT_0 + 1) continue;

        LARGE_INTEGER qpcOffEnd{}; QueryPerformanceCounter(&qpcOffEnd);
        double offEndMs = (double)(qpcOffEnd.QuadPart - qpcStart.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
        double offDrift = offEndMs - offExpectedEnd;
        double offOver  = (offDrift > 0.0) ? offDrift : 0.0;
        if (offOver > WATCHDOG_MS) {
            SetLayeredWindowAttributes(g_hOverlay, 0, 0, LWA_ALPHA);
            RedrawWindow(g_hOverlay, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        }
        CsvWriteRow(++seq, L"off", offExpectedEnd, offEndMs, offDrift, offOver, (long long)qpcOffEnd.QuadPart, (offOver>WATCHDOG_MS)?L"STALL":L"");
    }
    // unreachable
    // CloseHandle(hTimer); CloseCsvLog(); return 0;
}

// ----------------- WndProcs -----------------
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: return 1; // we fully paint
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH hbr = (HBRUSH)GetStockObject(BLACK_BRUSH);
        FillRect(hdc, &rc, hbr);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        UpdateOverlayDimensions();
        return 0;
    case WM_DESTROY:
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK GuiWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int notify = HIWORD(wParam);
        switch (id) {
        case IDC_MODE_MS:
        case IDC_MODE_FRAMES:
            if (notify == BN_CLICKED) {
                bool msSelected = (id == IDC_MODE_MS);
                EnableWindow(GetDlgItem(hwnd, IDC_EDIT_MS_ON),  msSelected);
                EnableWindow(GetDlgItem(hwnd, IDC_EDIT_MS_OFF), msSelected);
                EnableWindow(GetDlgItem(hwnd, IDC_EDIT_FR_ON),  !msSelected);
                EnableWindow(GetDlgItem(hwnd, IDC_EDIT_FR_OFF), !msSelected);
                UpdateConfigFromGui(hwnd);
            }
            break;
        case IDC_BUTTON_START:
            if (notify == BN_CLICKED) {
                UpdateConfigFromGui(hwnd);
                EnterCriticalSection(&g_configLock);
                bool running = g_config.running;
                LeaveCriticalSection(&g_configLock);
                ToggleOverlay(!running);
                SetWindowTextW(GetDlgItem(hwnd, IDC_BUTTON_START), running ? L"Start" : L"Stop");
            }
            break;
        default:
            if (notify == EN_KILLFOCUS) UpdateConfigFromGui(hwnd);
            break;
        }
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ----------------- Entry -----------------
int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // DPI before any window
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    InitCommonControls();
    InitializeCriticalSection(&g_configLock);
    LoadConfigFromIni();

    // Emit build info
    {
        wchar_t dbg[256];
        StringCchPrintfW(dbg, 256, L"[stroboverlay] %s", BUILD_ID_W);
        OutputDebugStringW(dbg);
    }

    // Create windows
    if (!CreateOverlayWindow(hInst)) {
        MessageBoxW(nullptr, L"Failed to create overlay window", L"Error", MB_OK | MB_ICONERROR);
        DeleteCriticalSection(&g_configLock);
        return 1;
    }
    g_hGui = CreateGuiWindow(hInst);
    {
        wchar_t title[256];
        StringCchPrintfW(title, 256, L"Stroboverlay Configuration (%s)", BUILD_ID_W);
        SetWindowTextW(g_hGui, title);
    }
    ShowWindow(g_hGui, SW_SHOW);

    // Hotkeys (INI-parsed minimal)
    wchar_t pathIni[MAX_PATH]; GetModuleFileNameW(nullptr, pathIni, MAX_PATH);
    if (wchar_t* p = wcsrchr(pathIni, L'\\')) *(p+1) = L'\0';
    wcscat_s(pathIni, MAX_PATH, L"config.ini");

    auto parseKey = [](const wchar_t* sIn, UINT& modsOut, UINT& vkOut) {
        std::wstring ks = sIn ? sIn : L"";
        std::transform(ks.begin(), ks.end(), ks.begin(), ::towupper);
        modsOut = 0; vkOut = 0;
        if (ks.find(L"CTRL+")  != std::wstring::npos) modsOut |= MOD_CONTROL;
        if (ks.find(L"ALT+")   != std::wstring::npos) modsOut |= MOD_ALT;
        if (ks.find(L"SHIFT+") != std::wstring::npos) modsOut |= MOD_SHIFT;
        size_t plus = ks.rfind(L'+');
        std::wstring key = (plus == std::wstring::npos) ? ks : ks.substr(plus + 1);
        if (key == L"UP") vkOut = VK_UP;
        else if (key == L"DOWN") vkOut = VK_DOWN;
        else if (key == L"LEFT") vkOut = VK_LEFT;
        else if (key == L"RIGHT") vkOut = VK_RIGHT;
        else if (key.rfind(L"F", 0) == 0) {
            int n = _wtoi(key.c_str()+1);
            if (n >= 1 && n <= 24) vkOut = VK_F1 + (n-1);
        } else if (key.size() == 1) {
            wchar_t c = towupper(key[0]);
            if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) vkOut = c;
        }
    };

    wchar_t buf[64];
    UINT mods, vk;

    GetPrivateProfileStringW(L"hotkeys", L"toggle", L"CTRL+SHIFT+3", buf, 63, pathIni);
    parseKey(buf, mods, vk); if (!vk) vk = '3';
    RegisterHotKey(nullptr, HOTKEY_ID_TOGGLE, mods | MOD_NOREPEAT, vk);

    GetPrivateProfileStringW(L"hotkeys", L"panic", L"CTRL+SHIFT+0", buf, 63, pathIni);
    parseKey(buf, mods, vk); if (!vk) vk = '0';
    RegisterHotKey(nullptr, HOTKEY_ID_QUIT, mods | MOD_NOREPEAT, vk);

    GetPrivateProfileStringW(L"hotkeys", L"showgui", L"CTRL+SHIFT+9", buf, 63, pathIni);
    parseKey(buf, mods, vk); if (!vk) vk = '9';
    RegisterHotKey(nullptr, HOTKEY_ID_SHOWGUI, mods | MOD_NOREPEAT, vk);

    // Events + worker
    g_hEventStop   = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
    g_hEventUpdate = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_hWorkerThread = CreateThread(nullptr, 0, WorkerThreadProc, nullptr, 0, nullptr);

    // Loop
    MSG msg; BOOL ret;
    while ((ret = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (ret == -1) break;
        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == HOTKEY_ID_TOGGLE) {
                EnterCriticalSection(&g_configLock); bool cur = g_config.running; LeaveCriticalSection(&g_configLock);
                ToggleOverlay(!cur);
                SetWindowTextW(GetDlgItem(g_hGui, IDC_BUTTON_START), cur ? L"Start" : L"Stop");
            } else if (msg.wParam == HOTKEY_ID_QUIT) {
                PostQuitMessage(0);
            } else if (msg.wParam == HOTKEY_ID_SHOWGUI) {
                ShowWindow(g_hGui, SW_SHOWNORMAL);
                SetForegroundWindow(g_hGui);
            }
            continue;
        }
        if (!IsDialogMessageW(g_hGui, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }

    // Teardown
    if (g_hEventStop) SetEvent(g_hEventStop);
    if (g_hWorkerThread) WaitForSingleObject(g_hWorkerThread, INFINITE);
    UnregisterHotKey(nullptr, HOTKEY_ID_TOGGLE);
    UnregisterHotKey(nullptr, HOTKEY_ID_QUIT);
    UnregisterHotKey(nullptr, HOTKEY_ID_SHOWGUI);
    if (g_hOverlay) DestroyWindow(g_hOverlay);
    if (g_hGui) DestroyWindow(g_hGui);
    if (g_hEventStop) CloseHandle(g_hEventStop);
    if (g_hEventUpdate) CloseHandle(g_hEventUpdate);
    if (g_hWorkerThread) CloseHandle(g_hWorkerThread);
    CloseCsvLog();
    DeleteCriticalSection(&g_configLock);
    return 0;
}
