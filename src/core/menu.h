#pragma once
#include <functional>
#include <string>
#include <vector>

#include "core/monitors.h"
#include "core/settings.h"

enum class MenuSection { Timing, Appearance, Mask, Profiles, Display };

struct MenuContext {
    AppSettings* settings = nullptr;
    bool running = false;
    std::vector<MonitorInfo> monitors;
    std::vector<std::wstring> images, masks, profiles;
    std::wstring warning;
    float dpiScale = 1.0f;
    double detectedHz = 60.0;
    // Returns an ImTextureID for an asset, so core stays free of graphics types.
    std::function<void*(const std::wstring& relativePath)> thumbnail;
};

struct MenuActions {
    bool strobeSettingsChanged = false, toggleStrobe = false, closeMenu = false;
    bool importImage = false, importMask = false;
    std::wstring loadProfile, saveProfile, deleteProfile;
    std::wstring selectMonitor;
};

void ApplyMenuTheme(float dpiScale);
void LoadMenuFonts(float dpiScale);
MenuActions DrawMenu(MenuContext& ctx, MenuSection& section);

// Keeps timing inside what LoadAppSettings accepts back from config.ini.
void ClampTiming(TimingSettings& timing);

// When set, section labels are recorded here. Tests use this to read the UI text.
extern std::vector<std::string>* g_menuTextLog;
