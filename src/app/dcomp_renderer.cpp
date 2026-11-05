#include "app/dcomp_renderer.h"

using Microsoft::WRL::ComPtr;

bool OverlayRenderer::CreateSwapChain(int w, int h, ComPtr<IDXGISwapChain1>& out) {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = w;
    desc.Height = h;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    return SUCCEEDED(factory_->CreateSwapChainForComposition(device_.Get(), &desc, nullptr, &out));
}

bool OverlayRenderer::Init(HWND hwnd, int w, int h) {
    AcquireSRWLockExclusive(&visualLock_);
    const bool ok = Build(hwnd, w, h);
    if (!ok) strobeVisual_.Reset();
    ReleaseSRWLockExclusive(&visualLock_);
    return ok;
}

bool OverlayRenderer::Build(HWND hwnd, int w, int h) {
    w_ = w;
    h_ = h;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                 &device_, nullptr, &context_))) {
        return false;
    }
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(device_.As(&dxgi))) return false;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)))) return false;
    if (!CreateSwapChain(w, h, strobeSwap_) || !CreateSwapChain(w, h, menuSwap_)) return false;

    if (FAILED(DCompositionCreateDevice2(dxgi.Get(), IID_PPV_ARGS(&dcomp_)))) return false;
    if (FAILED(dcomp_->CreateTargetForHwnd(hwnd, TRUE, &target_))) return false;

    ComPtr<IDCompositionVisual2> strobe, menu;
    if (FAILED(dcomp_->CreateVisual(&root_)) || FAILED(dcomp_->CreateVisual(&strobe)) ||
        FAILED(dcomp_->CreateVisual(&menu)) || FAILED(strobe.As(&strobeVisual_)) ||
        FAILED(menu.As(&menuVisual_))) {
        return false;
    }
    strobeVisual_->SetContent(strobeSwap_.Get());
    strobeVisual_->SetVisible(FALSE);
    menuVisual_->SetContent(menuSwap_.Get());
    menuVisual_->SetVisible(FALSE);

    // The menu goes on top, so the strobe can never cover it.
    root_->AddVisual(strobeVisual_.Get(), FALSE, nullptr);
    root_->AddVisual(menuVisual_.Get(), TRUE, strobeVisual_.Get());
    target_->SetRoot(root_.Get());
    return SUCCEEDED(dcomp_->Commit());
}

void OverlayRenderer::SetStrobeBitmap(const Bitmap& bitmap) {
    if (bitmap.w != w_ || bitmap.h != h_) return;
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(strobeSwap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
    context_->UpdateSubresource(back.Get(), 0, nullptr, bitmap.bgra.data(), bitmap.w * 4, 0);
    strobeSwap_->Present(0, 0);
}

void OverlayRenderer::ShowStrobe(bool show) {
    AcquireSRWLockShared(&visualLock_);
    if (strobeVisual_) {
        strobeVisual_->SetVisible(show ? TRUE : FALSE);
        dcomp_->Commit();
    }
    ReleaseSRWLockShared(&visualLock_);
}

void OverlayRenderer::ShowMenuLayer(bool show) {
    menuVisual_->SetVisible(show ? TRUE : FALSE);
    dcomp_->Commit();
}

bool OverlayRenderer::Resize(int w, int h) {
    if (FAILED(strobeSwap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0)) ||
        FAILED(menuSwap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) {
        return false;
    }
    w_ = w;
    h_ = h;
    return true;
}

bool OverlayRenderer::DeviceLost() const {
    return FAILED(device_->GetDeviceRemovedReason());
}

void OverlayRenderer::Reset() {
    AcquireSRWLockExclusive(&visualLock_);
    strobeVisual_.Reset();
    menuVisual_.Reset();
    root_.Reset();
    target_.Reset();
    dcomp_.Reset();
    strobeSwap_.Reset();
    menuSwap_.Reset();
    factory_.Reset();
    context_.Reset();
    device_.Reset();
    ReleaseSRWLockExclusive(&visualLock_);
}
