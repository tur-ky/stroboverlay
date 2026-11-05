#include "core/image_compose.h"
#include "temp.h"
#include "test.h"

#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdlib>

using Microsoft::WRL::ComPtr;

static bool SaveBitmapPng(const std::wstring& path, Bitmap bmp) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(bmp.w, bmp.h))) {
        return false;
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    return SUCCEEDED(frame->SetPixelFormat(&format)) &&
           SUCCEEDED(frame->WritePixels(bmp.h, bmp.w * 4, static_cast<UINT>(bmp.bgra.size()),
                                        bmp.bgra.data())) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

static Bitmap Solid(int w, int h, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    Bitmap bmp;
    bmp.w = w;
    bmp.h = h;
    bmp.bgra.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < bmp.bgra.size(); i += 4) {
        bmp.bgra[i] = b;
        bmp.bgra[i + 1] = g;
        bmp.bgra[i + 2] = r;
        bmp.bgra[i + 3] = a;
    }
    return bmp;
}

static void SetPx(Bitmap& bmp, int x, int y, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    uint8_t* p = &bmp.bgra[(static_cast<size_t>(y) * bmp.w + x) * 4];
    p[0] = b;
    p[1] = g;
    p[2] = r;
    p[3] = a;
}

static const uint8_t* Px(const Bitmap& bmp, int x, int y) {
    return &bmp.bgra[(static_cast<size_t>(y) * bmp.w + x) * 4];
}

static bool Near(int a, int b, int eps) { return std::abs(a - b) <= eps; }

TEST(no_mask_is_opaque_color) {
    Bitmap b = ComposeStrobe(0xFF0000, nullptr, nullptr, false, 2, 2);
    const uint8_t* p = Px(b, 1, 1);
    CHECK(p[0] == 0);
    CHECK(p[1] == 0);
    CHECK(p[2] == 255);
    CHECK(p[3] == 255);
}

TEST(white_mask_draws_black_mask_clears) {
    Bitmap mask = Solid(2, 1, 255, 255, 255, 255);
    SetPx(mask, 1, 0, 0, 0, 0, 255);
    Bitmap b = ComposeStrobe(0x000000, nullptr, &mask, false, 2, 1);
    CHECK(Px(b, 0, 0)[3] == 255);
    CHECK(Px(b, 1, 0)[3] == 0);
}

TEST(grey_is_partial_and_premultiplied) {
    Bitmap mask = Solid(1, 1, 128, 128, 128, 255);
    Bitmap b = ComposeStrobe(0xFF0000, nullptr, &mask, false, 1, 1);
    CHECK(Near(Px(b, 0, 0)[3], 128, 1));
    CHECK(Near(Px(b, 0, 0)[2], 128, 1));
}

TEST(invert_flips_mask) {
    Bitmap mask = Solid(2, 1, 255, 255, 255, 255);
    SetPx(mask, 1, 0, 0, 0, 0, 255);
    Bitmap b = ComposeStrobe(0x000000, nullptr, &mask, true, 2, 1);
    CHECK(Px(b, 0, 0)[3] == 0);
    CHECK(Px(b, 1, 0)[3] == 255);
}

TEST(mask_alpha_multiplies_in) {
    Bitmap mask = Solid(1, 1, 255, 255, 255, 0);
    Bitmap b = ComposeStrobe(0x000000, nullptr, &mask, false, 1, 1);
    CHECK(Px(b, 0, 0)[3] == 0);
}

TEST(transparent_mask_stays_clear_when_inverted) {
    Bitmap mask = Solid(1, 1, 255, 255, 255, 0);
    Bitmap b = ComposeStrobe(0x000000, nullptr, &mask, true, 1, 1);
    CHECK(Px(b, 0, 0)[3] == 0);
}

TEST(rec709_luminance) {
    Bitmap mask = Solid(1, 1, 0, 255, 0, 255);
    Bitmap b = ComposeStrobe(0x000000, nullptr, &mask, false, 1, 1);
    CHECK(Near(Px(b, 0, 0)[3], 182, 1));
}

TEST(image_fill_keeps_its_transparency) {
    Bitmap fill = Solid(1, 1, 10, 20, 200, 128);
    Bitmap b = ComposeStrobe(0xFFFFFF, &fill, nullptr, false, 1, 1);
    const uint8_t* p = Px(b, 0, 0);
    CHECK(p[3] == 128);
    CHECK(Near(p[2], 100, 1));
    CHECK(Near(p[1], 10, 1));
}

TEST(portrait_target_keeps_dimensions) {
    Bitmap b = ComposeStrobe(0, nullptr, nullptr, false, 1080, 1920);
    CHECK(b.w == 1080);
    CHECK(b.h == 1920);
    CHECK(b.bgra.size() == 1080u * 1920u * 4u);
}

TEST(mismatched_mask_size_is_ignored) {
    Bitmap mask = Solid(3, 3, 0, 0, 0, 255);
    Bitmap b = ComposeStrobe(0x000000, nullptr, &mask, false, 2, 2);
    CHECK(Px(b, 0, 0)[3] == 255);
}

TEST(load_stretches_to_requested_size) {
    std::wstring p = MakeTempDir() + L"a.png";
    Bitmap src = Solid(4, 4, 0, 0, 255, 255);
    CHECK(SaveBitmapPng(p, src));
    Bitmap out;
    CHECK(LoadImageStretched(p, 8, 2, out));
    CHECK(out.w == 8);
    CHECK(out.h == 2);
    CHECK(Near(Px(out, 7, 1)[2], 255, 1));
    CHECK(Near(Px(out, 7, 1)[3], 255, 1));
}

TEST(load_keeps_png_alpha) {
    std::wstring p = MakeTempDir() + L"a.png";
    Bitmap src = Solid(2, 2, 255, 255, 255, 0);
    CHECK(SaveBitmapPng(p, src));
    Bitmap out;
    CHECK(LoadImageStretched(p, 2, 2, out));
    CHECK(Px(out, 0, 0)[3] == 0);
}

TEST(corrupt_file_returns_false) {
    std::wstring p = MakeTempDir() + L"bad.png";
    WriteTextFile(p, "not an image");
    Bitmap out;
    CHECK(!LoadImageStretched(p, 4, 4, out));
    CHECK(!LoadImageStretched(MakeTempDir() + L"missing.png", 4, 4, out));
}

TEST(import_suffixes_on_clash) {
    std::wstring from = MakeTempDir();
    std::wstring to = MakeTempDir() + L"masks\\";
    WriteTextFile(from + L"a.png", "one");
    std::wstring name;
    CHECK(ImportAsset(from + L"a.png", to, name));
    CHECK(name == L"a.png");
    CHECK(ImportAsset(from + L"a.png", to, name));
    CHECK(name == L"a_1.png");
    CHECK(GetFileAttributesW((to + L"a_1.png").c_str()) != INVALID_FILE_ATTRIBUTES);
}

TEST(import_of_missing_file_fails) {
    std::wstring name;
    CHECK(!ImportAsset(MakeTempDir() + L"nope.png", MakeTempDir(), name));
}
