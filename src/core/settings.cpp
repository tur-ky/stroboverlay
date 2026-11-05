#include "core/settings.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>

static std::wstring ReadString(const std::wstring& file, const wchar_t* section,
                               const wchar_t* key) {
    wchar_t buf[512] = {0};
    GetPrivateProfileStringW(section, key, L"", buf, 512, file.c_str());
    return buf;
}

static bool ReadFlag(const std::wstring& file, const wchar_t* section, const wchar_t* key,
                     bool fallback) {
    return GetPrivateProfileIntW(section, key, fallback ? 1 : 0, file.c_str()) != 0;
}

static int ReadInt(const std::wstring& file, const wchar_t* section, const wchar_t* key, int lo,
                   int hi, int fallback) {
    std::wstring text = ReadString(file, section, key);
    if (text.empty()) return fallback;
    int value = _wtoi(text.c_str());
    return (value >= lo && value < hi) ? value : fallback;
}

static double ReadDouble(const std::wstring& file, const wchar_t* section, const wchar_t* key,
                         double loExclusive, double hi, double fallback) {
    std::wstring text = ReadString(file, section, key);
    if (text.empty()) return fallback;
    double value = _wtof(text.c_str());
    return (value > loExclusive && value < hi) ? value : fallback;
}

static uint32_t ParseColor(const std::wstring& text, uint32_t fallback) {
    if (text.size() != 6) return fallback;
    for (wchar_t c : text) {
        if (!iswxdigit(c)) return fallback;
    }
    return static_cast<uint32_t>(wcstoul(text.c_str(), nullptr, 16));
}

static StrobeSettings ReadStrobe(const std::wstring& file, StrobeSettings s) {
    TimingSettings& t = s.timing;
    std::wstring units = ReadString(file, L"overlay", L"units");
    if (!units.empty()) {
        t.mode = (_wcsicmp(units.c_str(), L"time") == 0) ? TimingMode::Ms : TimingMode::Frames;
    }
    t.onFrames = ReadInt(file, L"overlay", L"on_frames", 0, 10000, t.onFrames);
    t.offFrames = ReadInt(file, L"overlay", L"off_frames", 0, 10000, t.offFrames);
    t.onMs = ReadDouble(file, L"overlay", L"on_ms", -1e-9, 100000.0, t.onMs);
    t.offMs = ReadDouble(file, L"overlay", L"off_ms", -1e-9, 100000.0, t.offMs);

    std::wstring refresh = ReadString(file, L"overlay", L"refresh_hz");
    if (_wcsicmp(refresh.c_str(), L"auto") == 0) {
        s.refreshAuto = true;
    } else if (!refresh.empty()) {
        s.refreshAuto = false;
        t.refreshHz = ReadDouble(file, L"overlay", L"refresh_hz", 0.0, 10000.0, t.refreshHz);
    }

    std::wstring fill = ReadString(file, L"appearance", L"fill");
    if (!fill.empty()) {
        s.fill = (_wcsicmp(fill.c_str(), L"image") == 0) ? FillKind::Image : FillKind::Color;
    }
    s.colorRgb = ParseColor(ReadString(file, L"appearance", L"color"), s.colorRgb);
    std::wstring image = ReadString(file, L"appearance", L"image");
    if (!image.empty()) s.imageFile = image;

    s.maskEnabled = ReadFlag(file, L"mask", L"enabled", s.maskEnabled);
    std::wstring mask = ReadString(file, L"mask", L"file");
    if (!mask.empty()) s.maskFile = mask;
    s.maskInvert = ReadFlag(file, L"mask", L"invert", s.maskInvert);
    return s;
}

static void WriteValue(const std::wstring& file, const wchar_t* section, const wchar_t* key,
                       const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), file.c_str());
}

static std::wstring FormatDouble(double value) {
    wchar_t buf[64];
    swprintf_s(buf, L"%.10g", value);
    return buf;
}

static void WriteStrobe(const std::wstring& file, const StrobeSettings& s) {
    const TimingSettings& t = s.timing;
    WriteValue(file, L"overlay", L"units", t.mode == TimingMode::Ms ? L"time" : L"frames");
    WriteValue(file, L"overlay", L"on_frames", std::to_wstring(t.onFrames));
    WriteValue(file, L"overlay", L"off_frames", std::to_wstring(t.offFrames));
    WriteValue(file, L"overlay", L"on_ms", FormatDouble(t.onMs));
    WriteValue(file, L"overlay", L"off_ms", FormatDouble(t.offMs));
    WriteValue(file, L"overlay", L"refresh_hz", s.refreshAuto ? L"auto" : FormatDouble(t.refreshHz));

    wchar_t color[16];
    swprintf_s(color, L"%06X", s.colorRgb & 0xFFFFFF);
    WriteValue(file, L"appearance", L"fill", s.fill == FillKind::Image ? L"image" : L"color");
    WriteValue(file, L"appearance", L"color", color);
    WriteValue(file, L"appearance", L"image", s.imageFile);

    WriteValue(file, L"mask", L"enabled", s.maskEnabled ? L"1" : L"0");
    WriteValue(file, L"mask", L"file", s.maskFile);
    WriteValue(file, L"mask", L"invert", s.maskInvert ? L"1" : L"0");
}

static void ReadHotkey(const std::wstring& file, const wchar_t* key, Hotkey& hotkey) {
    Hotkey parsed;
    if (ParseHotkey(ReadString(file, L"hotkeys", key), parsed)) hotkey = parsed;
}

AppSettings LoadAppSettings(const std::wstring& iniPath) {
    AppSettings s;
    StrobeSettings defaults;
    defaults.refreshAuto = true;
    s.strobe = ReadStrobe(iniPath, defaults);
    s.monitor = ReadString(iniPath, L"display", L"monitor");
    ReadHotkey(iniPath, L"toggle", s.toggle);
    ReadHotkey(iniPath, L"panic", s.panic);
    ReadHotkey(iniPath, L"showgui", s.showGui);
    return s;
}

void SaveAppSettings(const std::wstring& iniPath, const AppSettings& settings) {
    WriteStrobe(iniPath, settings.strobe);
    WriteValue(iniPath, L"display", L"monitor", settings.monitor);
}

StrobeSettings LoadProfile(const std::wstring& path, const StrobeSettings& base) {
    return ReadStrobe(path, base);
}

void SaveProfile(const std::wstring& path, const StrobeSettings& settings) {
    WriteStrobe(path, settings);
}

std::vector<std::wstring> ListProfiles(const std::wstring& dir) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir + L"*.ini").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return names;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = data.cFileName;
        names.push_back(name.substr(0, name.size() - 4));
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(names.begin(), names.end());
    return names;
}

bool IsValidProfileName(const std::wstring& name) {
    if (name.empty() || name.size() > 64) return false;
    if (name.back() == L'.' || name.back() == L' ') return false;
    for (wchar_t c : name) {
        if (c < 32 || wcschr(L"\\/:*?\"<>|", c)) return false;
    }
    static const wchar_t* const reserved[] = {L"CON", L"PRN", L"AUX", L"NUL"};
    for (const wchar_t* r : reserved) {
        if (_wcsicmp(name.c_str(), r) == 0) return false;
    }
    if (name.size() == 4 && iswdigit(name[3]) && name[3] != L'0') {
        std::wstring stem = name.substr(0, 3);
        if (_wcsicmp(stem.c_str(), L"COM") == 0 || _wcsicmp(stem.c_str(), L"LPT") == 0) {
            return false;
        }
    }
    return true;
}

bool ParseHotkey(const std::wstring& text, Hotkey& out) {
    std::wstring ks = text;
    ks.erase(std::remove_if(ks.begin(), ks.end(), ::iswspace), ks.end());
    std::transform(ks.begin(), ks.end(), ks.begin(), ::towupper);

    Hotkey parsed;
    if (ks.find(L"CTRL+") != std::wstring::npos) parsed.mods |= MOD_CONTROL;
    if (ks.find(L"ALT+") != std::wstring::npos) parsed.mods |= MOD_ALT;
    if (ks.find(L"SHIFT+") != std::wstring::npos) parsed.mods |= MOD_SHIFT;

    size_t plus = ks.rfind(L'+');
    std::wstring key = (plus == std::wstring::npos) ? ks : ks.substr(plus + 1);
    if (key == L"UP") {
        parsed.vk = VK_UP;
    } else if (key == L"DOWN") {
        parsed.vk = VK_DOWN;
    } else if (key == L"LEFT") {
        parsed.vk = VK_LEFT;
    } else if (key == L"RIGHT") {
        parsed.vk = VK_RIGHT;
    } else if (key.size() > 1 && key[0] == L'F' && iswdigit(key[1])) {
        int n = _wtoi(key.c_str() + 1);
        if (n >= 1 && n <= 24) parsed.vk = VK_F1 + (n - 1);
    } else if (key.size() == 1 && iswalnum(key[0])) {
        parsed.vk = key[0];
    }
    if (parsed.vk == 0) return false;
    out = parsed;
    return true;
}

static bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ProbeMissingAssets(const std::wstring& exeDir, StrobeSettings& settings) {
    std::wstring warning;
    if (settings.fill == FillKind::Image && !FileExists(exeDir + settings.imageFile)) {
        warning = L"Missing image: " + settings.imageFile;
        settings.fill = FillKind::Color;
    }
    if (settings.maskEnabled && !FileExists(exeDir + settings.maskFile)) {
        if (!warning.empty()) warning += L". ";
        warning += L"Missing mask: " + settings.maskFile;
        settings.maskEnabled = false;
    }
    return warning;
}

Durations StrobeDurations(const StrobeSettings& settings, double monitorHz) {
    TimingSettings timing = settings.timing;
    if (settings.refreshAuto) timing.refreshHz = monitorHz;
    return ResolveDurations(timing);
}
