#include "app/asset_store.h"

#include <shobjidl.h>

#include <algorithm>
#include <cwchar>

using Microsoft::WRL::ComPtr;

static const int kThumbnailSize = 72;

static bool IsImageName(const wchar_t* name) {
    const wchar_t* dot = wcsrchr(name, L'.');
    if (!dot) return false;
    for (const wchar_t* ext : {L".png", L".jpg", L".jpeg", L".bmp"}) {
        if (_wcsicmp(dot, ext) == 0) return true;
    }
    return false;
}

void AssetStore::Init(std::wstring exeDir, ID3D11Device* device) {
    exeDir_ = std::move(exeDir);
    device_ = device;
    thumbnails_.clear();
}

std::vector<std::wstring> AssetStore::List(const wchar_t* subdir) const {
    std::vector<std::wstring> names;
    const std::wstring relDir = std::wstring(subdir) + L"\\";
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((exeDir_ + relDir + L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return names;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsImageName(data.cFileName)) {
            names.push_back(relDir + data.cFileName);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(names.begin(), names.end());
    return names;
}

bool AssetStore::Import(HWND owner, const wchar_t* subdir, std::wstring& outRelative) {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return false;
    }
    const COMDLG_FILTERSPEC filter[] = {{L"Images", L"*.png;*.jpg;*.jpeg;*.bmp"}};
    dialog->SetFileTypes(1, filter);
    if (FAILED(dialog->Show(owner))) return false;

    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return false;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return false;
    std::wstring source = path;
    CoTaskMemFree(path);

    const std::wstring relDir = std::wstring(subdir) + L"\\";
    std::wstring name;
    if (!ImportAsset(source, exeDir_ + relDir, name)) return false;
    outRelative = relDir + name;
    return true;
}

bool AssetStore::Load(Cached& cache, const std::wstring& relative, int w, int h) {
    if (cache.ok && cache.path == relative && cache.w == w && cache.h == h) return true;
    cache.path = relative;
    cache.w = w;
    cache.h = h;
    cache.ok = LoadImageStretched(exeDir_ + relative, w, h, cache.bitmap);
    return cache.ok;
}

Bitmap AssetStore::BuildStrobe(const StrobeSettings& settings, int w, int h, std::wstring& warning) {
    StrobeSettings s = settings;
    warning = ProbeMissingAssets(exeDir_, s);

    const auto note = [&warning](const std::wstring& text) {
        if (!warning.empty()) warning += L". ";
        warning += text;
    };
    if (s.fill == FillKind::Image && !Load(fill_, s.imageFile, w, h)) {
        note(L"Can't read image: " + s.imageFile);
        s.fill = FillKind::Color;
    }
    if (s.maskEnabled && !Load(mask_, s.maskFile, w, h)) {
        note(L"Can't read mask: " + s.maskFile);
        s.maskEnabled = false;
    }
    return ComposeStrobe(s.colorRgb, s.fill == FillKind::Image ? &fill_.bitmap : nullptr,
                         s.maskEnabled ? &mask_.bitmap : nullptr, s.maskInvert, w, h);
}

ID3D11ShaderResourceView* AssetStore::Thumbnail(const std::wstring& relative) {
    auto found = thumbnails_.find(relative);
    if (found != thumbnails_.end()) return found->second.Get();

    Bitmap bitmap;
    if (!LoadImageStretched(exeDir_ + relative, kThumbnailSize, kThumbnailSize, bitmap)) {
        // Unreadable files show as a grey square.
        bitmap.w = bitmap.h = kThumbnailSize;
        bitmap.bgra.assign(static_cast<size_t>(kThumbnailSize) * kThumbnailSize * 4, 0x40);
        for (size_t i = 3; i < bitmap.bgra.size(); i += 4) bitmap.bgra[i] = 255;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kThumbnailSize;
    desc.Height = kThumbnailSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{bitmap.bgra.data(), kThumbnailSize * 4, 0};

    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device_->CreateTexture2D(&desc, &data, &texture)) ||
        FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
        return nullptr;
    }
    thumbnails_[relative] = view;
    return view.Get();
}
