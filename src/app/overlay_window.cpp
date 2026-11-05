#include "app/overlay_window.h"

static const UINT_PTR kTopmostTimer = 1;
static const UINT kTopmostIntervalMs = 1000;

static const DWORD kClickThrough = WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;

bool OverlayWindow::Create(HINSTANCE instance, const RECT& rect, Handler handler) {
    handler_ = std::move(handler);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"StrobeOverlayClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    const DWORD ex = WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                     kClickThrough;
    hwnd_ = CreateWindowExW(ex, wc.lpszClassName, L"Stroboverlay", WS_POPUP, rect.left, rect.top,
                            rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                            instance, this);
    if (!hwnd_) return false;
    // A layered window draws nothing until its attributes are set once.
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    return true;
}

void OverlayWindow::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

void OverlayWindow::Place(const RECT& rect) {
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left,
                 rect.bottom - rect.top, SWP_NOACTIVATE);
}

void OverlayWindow::SetMenuInteractive(bool interactive) {
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    ex = interactive ? (ex & ~static_cast<LONG_PTR>(kClickThrough)) : (ex | kClickThrough);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void OverlayWindow::SetVisible(bool visible) {
    if (visible == visible_) return;
    visible_ = visible;
    if (visible) {
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetTimer(hwnd_, kTopmostTimer, kTopmostIntervalMs, nullptr);
    } else {
        KillTimer(hwnd_, kTopmostTimer);
        ShowWindow(hwnd_, SW_HIDE);
    }
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_TIMER && wp == kTopmostTimer) {
        // Other topmost windows can land above us; flashes no longer reassert this.
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    }
    if (self && self->handler_) {
        LRESULT result = 0;
        if (self->handler_(msg, wp, lp, result)) return result;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
