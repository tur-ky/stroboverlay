#pragma once
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <windows.h>
#include <wrl/client.h>

#include "core/image_compose.h"

// Two DirectComposition visuals in one window: the strobe below, the menu above.
// A flash only toggles a visual, so its cost doesn't depend on what the strobe shows.
class OverlayRenderer {
public:
    bool Init(HWND hwnd, int w, int h);
    void SetStrobeBitmap(const Bitmap& bitmap);
    void ShowStrobe(bool show);  // Safe from the worker thread.
    void ShowMenuLayer(bool show);
    bool Resize(int w, int h);  // Callers must release views of the menu swapchain first.
    bool DeviceLost() const;
    void Reset();  // Drops everything so Init can run again.

    ID3D11Device* Device() const { return device_.Get(); }
    ID3D11DeviceContext* Context() const { return context_.Get(); }
    IDXGISwapChain1* MenuSwapChain() const { return menuSwap_.Get(); }

private:
    bool Build(HWND hwnd, int w, int h);
    bool CreateSwapChain(int w, int h, Microsoft::WRL::ComPtr<IDXGISwapChain1>& out);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> strobeSwap_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> menuSwap_;
    Microsoft::WRL::ComPtr<IDCompositionDesktopDevice> dcomp_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcompState_;  // May be null.
    Microsoft::WRL::ComPtr<IDCompositionTarget> target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual2> root_;
    Microsoft::WRL::ComPtr<IDCompositionVisual3> strobeVisual_;
    Microsoft::WRL::ComPtr<IDCompositionVisual3> menuVisual_;
    int w_ = 0, h_ = 0;
    SRWLOCK visualLock_ = SRWLOCK_INIT;  // Keeps a flash from racing a device rebuild.
};
