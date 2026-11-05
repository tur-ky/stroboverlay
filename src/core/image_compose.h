#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Bitmap {
    int w = 0, h = 0;
    std::vector<uint8_t> bgra;
};

// Decodes any WIC-supported image and scales it to w by h, with straight alpha.
bool LoadImageStretched(const std::wstring& path, int w, int h, Bitmap& out);

// Builds the premultiplied strobe bitmap. White mask pixels draw the fill, black
// ones leave the game visible, and mask alpha always multiplies in. A fill image
// or mask that isn't exactly w by h is ignored.
Bitmap ComposeStrobe(uint32_t colorRgb, const Bitmap* fillImage, const Bitmap* mask, bool invert,
                     int w, int h);

// Copies src into destDir, creating it if needed. A name clash gets a numeric
// suffix. outName receives the file name used.
bool ImportAsset(const std::wstring& src, const std::wstring& destDir, std::wstring& outName);
