#include "core/image_compose.h"

#include <objbase.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

const UINT kMaxSourceDimension = 16384;

// COM may already be up on this thread, in either apartment. Both are fine for WIC.
class ComScope {
public:
    ComScope() : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr_)) CoUninitialize();
    }

private:
    HRESULT hr_;
};

uint8_t Mul255(int a, int b) {
    return static_cast<uint8_t>((a * b + 127) / 255);
}

bool MatchesTarget(const Bitmap* bmp, int w, int h) {
    return bmp && bmp->w == w && bmp->h == h;
}

}  // namespace

bool LoadImageStretched(const std::wstring& path, int w, int h, Bitmap& out) {
    if (w <= 0 || h <= 0) return false;
    ComScope com;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder))) {
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    UINT srcW = 0, srcH = 0;
    if (FAILED(frame->GetSize(&srcW, &srcH))) return false;
    if (srcW == 0 || srcH == 0 || srcW > kMaxSourceDimension || srcH > kMaxSourceDimension) {
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))) return false;
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return false;
    }
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(&scaler))) return false;
    if (FAILED(scaler->Initialize(converter.Get(), static_cast<UINT>(w), static_cast<UINT>(h),
                                  WICBitmapInterpolationModeFant))) {
        return false;
    }

    Bitmap result;
    result.w = w;
    result.h = h;
    result.bgra.resize(static_cast<size_t>(w) * h * 4);
    if (FAILED(scaler->CopyPixels(nullptr, static_cast<UINT>(w) * 4,
                                  static_cast<UINT>(result.bgra.size()), result.bgra.data()))) {
        return false;
    }
    out = std::move(result);
    return true;
}

Bitmap ComposeStrobe(uint32_t colorRgb, const Bitmap* fillImage, const Bitmap* mask, bool invert,
                     int w, int h) {
    const bool useFill = MatchesTarget(fillImage, w, h);
    const bool useMask = MatchesTarget(mask, w, h);
    const uint8_t colorR = static_cast<uint8_t>(colorRgb >> 16);
    const uint8_t colorG = static_cast<uint8_t>(colorRgb >> 8);
    const uint8_t colorB = static_cast<uint8_t>(colorRgb);

    Bitmap out;
    out.w = w;
    out.h = h;
    const size_t pixels = static_cast<size_t>(w) * h;
    out.bgra.resize(pixels * 4);

    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t* f = useFill ? &fillImage->bgra[i * 4] : nullptr;
        int b = f ? f[0] : colorB;
        int g = f ? f[1] : colorG;
        int r = f ? f[2] : colorR;
        int a = f ? f[3] : 255;

        if (useMask) {
            const uint8_t* m = &mask->bgra[i * 4];
            int lum = (2126 * m[2] + 7152 * m[1] + 722 * m[0] + 5000) / 10000;
            if (invert) lum = 255 - lum;
            a = Mul255(Mul255(a, lum), m[3]);
        }

        uint8_t* p = &out.bgra[i * 4];
        p[0] = Mul255(b, a);
        p[1] = Mul255(g, a);
        p[2] = Mul255(r, a);
        p[3] = static_cast<uint8_t>(a);
    }
    return out;
}

bool ImportAsset(const std::wstring& src, const std::wstring& destDir, std::wstring& outName) {
    DWORD attrs = GetFileAttributesW(src.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;
    CreateDirectoryW(destDir.c_str(), nullptr);

    size_t slash = src.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? src : src.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    std::wstring stem = (dot == std::wstring::npos) ? name : name.substr(0, dot);
    std::wstring ext = (dot == std::wstring::npos) ? L"" : name.substr(dot);

    std::wstring candidate = name;
    for (int n = 1; n < 1000; ++n) {
        if (CopyFileW(src.c_str(), (destDir + candidate).c_str(), TRUE)) {
            outName = candidate;
            return true;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) return false;
        candidate = stem + L"_" + std::to_wstring(n) + ext;
    }
    return false;
}
