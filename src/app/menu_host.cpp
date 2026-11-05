#include "app/menu_host.h"

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "imgui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

bool MenuHost::Init(HWND hwnd, OverlayRenderer* renderer, float dpiScale) {
    renderer_ = renderer;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    LoadMenuFonts(dpiScale);
    ApplyMenuTheme(dpiScale);
    if (!ImGui_ImplWin32_Init(hwnd)) return false;
    if (!ImGui_ImplDX11_Init(renderer->Device(), renderer->Context())) return false;
    ready_ = true;
    return true;
}

void MenuHost::Shutdown() {
    if (!ready_) return;
    ready_ = false;
    target_.Reset();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void MenuHost::SetDpiScale(float dpiScale) {
    ApplyMenuTheme(dpiScale);
}

void MenuHost::ReleaseTargets() {
    target_.Reset();
    renderer_->Context()->OMSetRenderTargets(0, nullptr, nullptr);
    renderer_->Context()->Flush();
}

bool MenuHost::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    if (!ready_) return false;
    result = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
    return result != 0;
}

bool MenuHost::EnsureTarget() {
    if (target_) return true;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back;
    if (FAILED(renderer_->MenuSwapChain()->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    return SUCCEEDED(renderer_->Device()->CreateRenderTargetView(back.Get(), nullptr, &target_));
}

MenuActions MenuHost::Frame(MenuContext& ctx) {
    MenuActions actions;
    if (!EnsureTarget()) return actions;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    actions = DrawMenu(ctx, section_);
    ImGui::Render();

    const float clear[4] = {0, 0, 0, 0};
    ID3D11DeviceContext* context = renderer_->Context();
    context->OMSetRenderTargets(1, target_.GetAddressOf(), nullptr);
    context->ClearRenderTargetView(target_.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    renderer_->MenuSwapChain()->Present(1, 0);
    return actions;
}

void MenuHost::Clear() {
    if (!EnsureTarget()) return;
    const float clear[4] = {0, 0, 0, 0};
    renderer_->Context()->ClearRenderTargetView(target_.Get(), clear);
    renderer_->MenuSwapChain()->Present(0, 0);
}
