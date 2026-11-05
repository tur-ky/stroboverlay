#pragma once
#include <d3d11.h>
#include <windows.h>
#include <wrl/client.h>

#include "app/dcomp_renderer.h"
#include "core/menu.h"

// Runs Dear ImGui on the menu layer. Draws only while the menu is open.
class MenuHost {
public:
    bool Init(HWND hwnd, OverlayRenderer* renderer, float dpiScale);
    void Shutdown();
    void SetDpiScale(float dpiScale);
    void ReleaseTargets();  // Before the swapchain resizes.
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);
    MenuActions Frame(MenuContext& ctx);
    void Clear();

private:
    bool EnsureTarget();

    OverlayRenderer* renderer_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    MenuSection section_ = MenuSection::Timing;
    bool ready_ = false;
};
