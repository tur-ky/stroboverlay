#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/strobe_clock.h"

enum class FillKind { Color, Image };

// Everything a profile stores. Paths are relative to the exe folder.
struct StrobeSettings {
    TimingSettings timing;
    bool refreshAuto = false;
    FillKind fill = FillKind::Color;
    uint32_t colorRgb = 0x000000;
    std::wstring imageFile;
    bool maskEnabled = false;
    std::wstring maskFile;
    bool maskInvert = false;
};

struct Hotkey {
    UINT mods = 0;
    UINT vk = 0;
};

struct AppSettings {
    StrobeSettings strobe;
    std::wstring monitor;  // Device name such as \\.\DISPLAY1. Empty means primary.
    Hotkey toggle{MOD_CONTROL | MOD_SHIFT, '3'};
    Hotkey panic{MOD_CONTROL | MOD_SHIFT, '5'};
    Hotkey showGui{MOD_CONTROL | MOD_SHIFT, '1'};
};

AppSettings LoadAppSettings(const std::wstring& iniPath);
// Hotkeys are read-only here: they are edited by hand in config.ini.
void SaveAppSettings(const std::wstring& iniPath, const AppSettings& settings);

StrobeSettings LoadProfile(const std::wstring& path, const StrobeSettings& base);
void SaveProfile(const std::wstring& path, const StrobeSettings& settings);
std::vector<std::wstring> ListProfiles(const std::wstring& dir);
bool IsValidProfileName(const std::wstring& name);

bool ParseHotkey(const std::wstring& text, Hotkey& out);

// Turns off any fill or mask whose file is missing, keeping the saved path so
// restoring the file works. Returns a warning naming the file, or empty.
std::wstring ProbeMissingAssets(const std::wstring& exeDir, StrobeSettings& settings);
