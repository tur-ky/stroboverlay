// Times each way of flipping a fullscreen strobe layer, from the call to the end of
// the next DWM composition. Usage: flip_bench <monitor-device> <slwa|ulw|dcomp> <flips>

#include <d3d11.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dxgi1_3.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static bool FindMonitor(const wchar_t* device, RECT& rc) {
    struct Search {
        const wchar_t* device;
        RECT* rc;
        bool found;
    } search{device, &rc, false};
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM param) -> BOOL {
            auto* s = reinterpret_cast<Search*>(param);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            GetMonitorInfoW(mon, &mi);
            if (_wcsicmp(mi.szDevice, s->device) != 0) return TRUE;
            *s->rc = mi.rcMonitor;
            s->found = true;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}

static HWND MakeWindow(const RECT& rc, DWORD extraEx) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FlipBench";
    RegisterClassW(&wc);
    DWORD ex = WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | extraEx;
    return CreateWindowExW(ex, wc.lpszClassName, L"flip bench", WS_POPUP, rc.left, rc.top,
                           rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance,
                           nullptr);
}

static void Pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
}

using Flip = std::function<void(bool show)>;

static bool SetupSlwa(const RECT& rc, HWND& hwnd, Flip& flip) {
    hwnd = MakeWindow(rc, WS_EX_LAYERED);
    if (!hwnd) return false;
    HWND h = hwnd;
    SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
    ShowWindow(h, SW_SHOWNOACTIVATE);
    HDC dc = GetDC(h);
    RECT client{0, 0, rc.right - rc.left, rc.bottom - rc.top};
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    ReleaseDC(h, dc);
    flip = [h](bool show) { SetLayeredWindowAttributes(h, 0, show ? 255 : 0, LWA_ALPHA); };
    return true;
}

static bool SetupUlw(const RECT& rc, HWND& hwnd, Flip& flip) {
    hwnd = MakeWindow(rc, WS_EX_LAYERED);
    if (!hwnd) return false;
    HWND h = hwnd;
    const int w = rc.right - rc.left;
    const int ht = rc.bottom - rc.top;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -ht;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) return false;
    SelectObject(mem, dib);
    auto* px = static_cast<uint32_t*>(bits);
    std::fill(px, px + static_cast<size_t>(w) * ht, 0xFF000000u);

    POINT dst{rc.left, rc.top};
    POINT src{0, 0};
    SIZE size{w, ht};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 0, AC_SRC_ALPHA};
    UpdateLayeredWindow(h, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    ShowWindow(h, SW_SHOWNOACTIVATE);

    flip = [h](bool show) {
        BLENDFUNCTION b{AC_SRC_OVER, 0, static_cast<BYTE>(show ? 255 : 0), AC_SRC_ALPHA};
        UpdateLayeredWindow(h, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &b, ULW_ALPHA);
    };
    return true;
}

static bool SetupDcomp(const RECT& rc, HWND& hwnd, Flip& flip) {
    hwnd = MakeWindow(rc, WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP);
    if (!hwnd) return false;
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    static ComPtr<ID3D11Device> device;
    static ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                 &device, nullptr, &context))) {
        return false;
    }
    ComPtr<IDXGIDevice> dxgi;
    device.As(&dxgi);
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = rc.right - rc.left;
    desc.Height = rc.bottom - rc.top;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    static ComPtr<IDXGISwapChain1> swap;
    if (FAILED(factory->CreateSwapChainForComposition(device.Get(), &desc, nullptr, &swap))) {
        return false;
    }
    ComPtr<ID3D11Texture2D> back;
    swap->GetBuffer(0, IID_PPV_ARGS(&back));
    ComPtr<ID3D11RenderTargetView> rtv;
    device->CreateRenderTargetView(back.Get(), nullptr, &rtv);
    const float black[4] = {0, 0, 0, 1};
    context->ClearRenderTargetView(rtv.Get(), black);
    swap->Present(0, 0);

    static ComPtr<IDCompositionDesktopDevice> dcomp;
    if (FAILED(DCompositionCreateDevice2(dxgi.Get(), IID_PPV_ARGS(&dcomp)))) return false;
    static ComPtr<IDCompositionTarget> target;
    if (FAILED(dcomp->CreateTargetForHwnd(hwnd, TRUE, &target))) return false;
    ComPtr<IDCompositionVisual2> visual;
    if (FAILED(dcomp->CreateVisual(&visual))) return false;
    static ComPtr<IDCompositionVisual3> visual3;
    if (FAILED(visual.As(&visual3))) return false;
    visual3->SetContent(swap.Get());
    visual3->SetVisible(FALSE);
    target->SetRoot(visual3.Get());
    dcomp->Commit();

    flip = [](bool show) {
        visual3->SetVisible(show ? TRUE : FALSE);
        dcomp->Commit();
    };
    return true;
}

static double Percentile(std::vector<double>& v, double p) {
    size_t i = static_cast<size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + i, v.end());
    return v[i];
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) {
        std::printf("usage: flip_bench <monitor-device> <slwa|ulw|dcomp> <flips>\n");
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    RECT rc;
    if (!FindMonitor(argv[1], rc)) {
        std::printf("monitor not found\n");
        return 2;
    }
    const std::wstring method = argv[2];
    const int flips = _wtoi(argv[3]);

    HWND hwnd = nullptr;
    Flip flip;
    bool ready = (method == L"slwa") ? SetupSlwa(rc, hwnd, flip)
                 : (method == L"ulw") ? SetupUlw(rc, hwnd, flip)
                 : (method == L"dcomp") ? SetupDcomp(rc, hwnd, flip)
                                        : false;
    if (!ready) {
        std::printf("setup failed\n");
        return 1;
    }

    Pump();
    Sleep(200);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    std::vector<double> flushed, called;
    flushed.reserve(flips);
    called.reserve(flips);

    for (int i = 0; i < flips; ++i) {
        LARGE_INTEGER a, b, c;
        QueryPerformanceCounter(&a);
        flip(i % 2 == 0);
        QueryPerformanceCounter(&b);
        DwmFlush();
        QueryPerformanceCounter(&c);
        called.push_back((b.QuadPart - a.QuadPart) * 1000.0 / freq.QuadPart);
        flushed.push_back((c.QuadPart - a.QuadPart) * 1000.0 / freq.QuadPart);
        Pump();
        Sleep(20);
    }
    flip(false);
    DwmFlush();
    DestroyWindow(hwnd);

    std::printf("%ls call_median=%.3f call_p99=%.3f flush_median=%.3f flush_p95=%.3f "
                "flush_p99=%.3f flush_max=%.3f ms\n",
                method.c_str(), Percentile(called, 0.5), Percentile(called, 0.99),
                Percentile(flushed, 0.5), Percentile(flushed, 0.95), Percentile(flushed, 0.99),
                *std::max_element(flushed.begin(), flushed.end()));
    return 0;
}
