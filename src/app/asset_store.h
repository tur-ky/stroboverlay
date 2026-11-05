#pragma once
#include <d3d11.h>
#include <windows.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <vector>

#include "core/image_compose.h"
#include "core/settings.h"

// The images/ and masks/ folders beside the exe: listing, importing, decoding and
// thumbnails. Paths going in and out are relative to the exe.
class AssetStore {
public:
    void Init(std::wstring exeDir, ID3D11Device* device);  // Also drops old thumbnails.
    std::vector<std::wstring> List(const wchar_t* subdir) const;
    bool Import(HWND owner, const wchar_t* subdir, std::wstring& outRelative);
    Bitmap BuildStrobe(const StrobeSettings& settings, int w, int h, std::wstring& warning);
    ID3D11ShaderResourceView* Thumbnail(const std::wstring& relative);

private:
    struct Cached {
        std::wstring path;
        int w = 0, h = 0;
        Bitmap bitmap;
        bool ok = false;
    };
    bool Load(Cached& cache, const std::wstring& relative, int w, int h);

    std::wstring exeDir_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Cached fill_, mask_;
    std::map<std::wstring, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> thumbnails_;
};
