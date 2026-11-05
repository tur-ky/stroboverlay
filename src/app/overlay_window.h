#pragma once
#include <windows.h>

#include <functional>

// The single topmost window that hosts both overlay layers. Click-through until
// the menu makes it interactive.
class OverlayWindow {
public:
    // Returns true if the message was handled, with the result in `result`.
    using Handler = std::function<bool(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result)>;

    bool Create(HINSTANCE instance, const RECT& rect, Handler handler);
    void Destroy();
    void Place(const RECT& rect);
    void SetMenuInteractive(bool interactive);
    void SetVisible(bool visible);
    bool Visible() const { return visible_; }
    HWND Handle() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    Handler handler_;
    bool visible_ = false;
};
