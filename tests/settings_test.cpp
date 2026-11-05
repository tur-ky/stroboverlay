#include "core/settings.h"
#include "temp.h"
#include "test.h"

#include <cstring>
#include <vector>

TEST(old_config_keeps_manual_refresh) {
    std::wstring p = MakeTempDir() + L"config.ini";
    WriteTextFile(p,
                  "[overlay]\r\n; comment\r\non_frames=100\r\noff_frames=100\r\nrefresh_hz=400\r\n"
                  "[hotkeys]\r\ntoggle=CTRL+SHIFT+3\r\npanic=CTRL+SHIFT+5\r\nshowgui=CTRL+SHIFT+1\r\n");
    AppSettings s = LoadAppSettings(p);
    CHECK(s.strobe.timing.mode == TimingMode::Frames);
    CHECK(s.strobe.timing.onFrames == 100);
    CHECK(s.strobe.timing.refreshHz == 400.0);
    CHECK(!s.strobe.refreshAuto);
    CHECK(s.strobe.fill == FillKind::Color);
    CHECK(s.strobe.colorRgb == 0);
    CHECK(!s.strobe.maskEnabled);
    CHECK(s.monitor.empty());
    CHECK(s.toggle.vk == '3');
}

TEST(missing_config_gives_auto_refresh_defaults) {
    AppSettings s = LoadAppSettings(MakeTempDir() + L"nope.ini");
    CHECK(s.strobe.refreshAuto);
    CHECK(s.strobe.timing.onFrames == 100);
    CHECK(s.showGui.vk == '1');
    CHECK(s.showGui.mods == (MOD_CONTROL | MOD_SHIFT));
}

TEST(refresh_auto_key) {
    std::wstring p = MakeTempDir() + L"config.ini";
    WriteTextFile(p, "[overlay]\r\nrefresh_hz=auto\r\n");
    CHECK(LoadAppSettings(p).strobe.refreshAuto);
}

TEST(units_time_selects_ms) {
    std::wstring p = MakeTempDir() + L"config.ini";
    WriteTextFile(p, "[overlay]\r\nunits=time\r\non_ms=12.5\r\noff_ms=7\r\n");
    AppSettings s = LoadAppSettings(p);
    CHECK(s.strobe.timing.mode == TimingMode::Ms);
    CHECK(s.strobe.timing.onMs == 12.5);
    CHECK(s.strobe.timing.offMs == 7.0);
}

TEST(settings_round_trip_all_fields) {
    std::wstring p = MakeTempDir() + L"config.ini";
    AppSettings a;
    a.strobe.timing.mode = TimingMode::Ms;
    a.strobe.timing.onMs = 12.25;
    a.strobe.timing.offMs = 33.5;
    a.strobe.timing.onFrames = 7;
    a.strobe.timing.offFrames = 9;
    a.strobe.timing.refreshHz = 399.5;
    a.strobe.refreshAuto = false;
    a.strobe.fill = FillKind::Image;
    a.strobe.colorRgb = 0x12AB34;
    a.strobe.imageFile = L"images\\a b.png";
    a.strobe.maskEnabled = true;
    a.strobe.maskFile = L"masks\\m.png";
    a.strobe.maskInvert = true;
    a.monitor = L"\\\\.\\DISPLAY3";
    a.toggle = {MOD_ALT, VK_F9};
    SaveAppSettings(p, a);

    AppSettings b = LoadAppSettings(p);
    CHECK(b.strobe.timing.mode == TimingMode::Ms);
    CHECK(b.strobe.timing.onMs == 12.25);
    CHECK(b.strobe.timing.offMs == 33.5);
    CHECK(b.strobe.timing.onFrames == 7);
    CHECK(b.strobe.timing.offFrames == 9);
    CHECK(b.strobe.timing.refreshHz == 399.5);
    CHECK(!b.strobe.refreshAuto);
    CHECK(b.strobe.fill == FillKind::Image);
    CHECK(b.strobe.colorRgb == 0x12AB34);
    CHECK(b.strobe.imageFile == L"images\\a b.png");
    CHECK(b.strobe.maskEnabled);
    CHECK(b.strobe.maskFile == L"masks\\m.png");
    CHECK(b.strobe.maskInvert);
    CHECK(b.monitor == L"\\\\.\\DISPLAY3");
}

TEST(save_keeps_comments) {
    std::wstring p = MakeTempDir() + L"config.ini";
    WriteTextFile(p, "[overlay]\r\n; keep me\r\non_frames=5\r\n");
    SaveAppSettings(p, LoadAppSettings(p));
    FILE* f = nullptr;
    _wfopen_s(&f, p.c_str(), L"rb");
    char buf[2048] = {0};
    if (f) {
        fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
    }
    CHECK(strstr(buf, "; keep me") != nullptr);
}

TEST(save_keeps_existing_hotkey_text) {
    std::wstring p = MakeTempDir() + L"config.ini";
    WriteTextFile(p, "[hotkeys]\r\ntoggle=CTRL+ALT+F2\r\n");
    SaveAppSettings(p, LoadAppSettings(p));
    wchar_t buf[64] = {0};
    GetPrivateProfileStringW(L"hotkeys", L"toggle", L"", buf, 64, p.c_str());
    CHECK(std::wstring(buf) == L"CTRL+ALT+F2");
}

TEST(bad_color_falls_back_to_black) {
    std::wstring p = MakeTempDir() + L"config.ini";
    WriteTextFile(p, "[appearance]\r\ncolor=zzz\r\n");
    CHECK(LoadAppSettings(p).strobe.colorRgb == 0);
}

TEST(out_of_range_values_use_defaults) {
    std::wstring p = MakeTempDir() + L"config.ini";
    WriteTextFile(p, "[overlay]\r\non_frames=99999\r\noff_frames=-3\r\nrefresh_hz=-1\r\non_ms=-1\r\n");
    AppSettings s = LoadAppSettings(p);
    CHECK(s.strobe.timing.onFrames == 100);
    CHECK(s.strobe.timing.offFrames == 100);
    CHECK(s.strobe.timing.refreshHz == 400.0);
    CHECK(s.strobe.timing.onMs == 100.0);
}

TEST(profile_excludes_monitor) {
    std::wstring p = MakeTempDir() + L"prof.ini";
    StrobeSettings s;
    s.colorRgb = 0xFF0000;
    SaveProfile(p, s);
    wchar_t buf[64];
    CHECK(GetPrivateProfileStringW(L"display", nullptr, L"", buf, 64, p.c_str()) == 0);
    CHECK(LoadProfile(p, StrobeSettings{}).colorRgb == 0xFF0000);
}

TEST(profile_load_keeps_base_for_missing_keys) {
    std::wstring p = MakeTempDir() + L"prof.ini";
    WriteTextFile(p, "[appearance]\r\ncolor=00FF00\r\n");
    StrobeSettings base;
    base.timing.onFrames = 42;
    StrobeSettings s = LoadProfile(p, base);
    CHECK(s.colorRgb == 0x00FF00);
    CHECK(s.timing.onFrames == 42);
}

TEST(list_profiles_sorted_without_extension) {
    std::wstring d = MakeTempDir();
    WriteTextFile(d + L"zeta.ini", "");
    WriteTextFile(d + L"alpha one.ini", "");
    WriteTextFile(d + L"notes.txt", "");
    std::vector<std::wstring> names = ListProfiles(d);
    CHECK(names.size() == 2);
    if (names.size() == 2) {
        CHECK(names[0] == L"alpha one");
        CHECK(names[1] == L"zeta");
    }
    CHECK(ListProfiles(d + L"missing\\").empty());
}

TEST(profile_name_rules) {
    CHECK(IsValidProfileName(L"aim warmup"));
    CHECK(!IsValidProfileName(L""));
    CHECK(!IsValidProfileName(L"a/b"));
    CHECK(!IsValidProfileName(L"a\\b"));
    CHECK(!IsValidProfileName(L"a:b"));
    CHECK(!IsValidProfileName(L"CON"));
    CHECK(!IsValidProfileName(L"com1"));
    CHECK(!IsValidProfileName(L"trail."));
    CHECK(!IsValidProfileName(L"trail "));
}

TEST(hotkey_parse) {
    Hotkey h{};
    CHECK(ParseHotkey(L"CTRL+SHIFT+3", h));
    CHECK(h.mods == (MOD_CONTROL | MOD_SHIFT));
    CHECK(h.vk == '3');
    CHECK(ParseHotkey(L"alt + f12", h));
    CHECK(h.mods == MOD_ALT);
    CHECK(h.vk == VK_F12);
    CHECK(ParseHotkey(L"CTRL+UP", h));
    CHECK(h.vk == VK_UP);
    CHECK(!ParseHotkey(L"CTRL+", h));
    CHECK(!ParseHotkey(L"CTRL+F99", h));
}

TEST(bad_hotkey_in_config_uses_default) {
    std::wstring p = MakeTempDir() + L"config.ini";
    WriteTextFile(p, "[hotkeys]\r\ntoggle=CTRL+\r\n");
    AppSettings s = LoadAppSettings(p);
    CHECK(s.toggle.vk == '3');
    CHECK(s.toggle.mods == (MOD_CONTROL | MOD_SHIFT));
}

TEST(missing_asset_falls_back_and_keeps_path) {
    std::wstring dir = MakeTempDir();
    StrobeSettings s;
    s.fill = FillKind::Image;
    s.imageFile = L"images\\gone.png";
    s.maskEnabled = true;
    s.maskFile = L"masks\\gone.png";
    std::wstring warn = ProbeMissingAssets(dir, s);
    CHECK(!warn.empty());
    CHECK(warn.find(L"gone.png") != std::wstring::npos);
    CHECK(s.fill == FillKind::Color);
    CHECK(!s.maskEnabled);
    CHECK(s.imageFile == L"images\\gone.png");
    CHECK(s.maskFile == L"masks\\gone.png");
}

TEST(present_assets_stay_enabled) {
    std::wstring dir = MakeTempDir();
    WriteTextFile(dir + L"a.png", "x");
    StrobeSettings s;
    s.fill = FillKind::Image;
    s.imageFile = L"a.png";
    CHECK(ProbeMissingAssets(dir, s).empty());
    CHECK(s.fill == FillKind::Image);
}

TEST(recompose_only_on_look_changes) {
    StrobeSettings a, b = a;
    b.timing.onMs = 5;
    CHECK(!NeedsRecompose(a, b));
    b = a;
    b.colorRgb = 0xFFFFFF;
    CHECK(NeedsRecompose(a, b));
    b = a;
    b.fill = FillKind::Image;
    CHECK(NeedsRecompose(a, b));
    b = a;
    b.imageFile = L"images\a.png";
    CHECK(NeedsRecompose(a, b));
    b = a;
    b.maskFile = L"masks\a.png";
    CHECK(NeedsRecompose(a, b));
    b = a;
    b.maskEnabled = true;
    CHECK(NeedsRecompose(a, b));
    b = a;
    b.maskInvert = true;
    CHECK(NeedsRecompose(a, b));
}

TEST(no_file_chosen_falls_back_without_a_warning) {
    std::wstring dir = MakeTempDir();
    StrobeSettings s;
    s.fill = FillKind::Image;
    s.maskEnabled = true;
    CHECK(ProbeMissingAssets(dir, s).empty());
    CHECK(s.fill == FillKind::Color);
    CHECK(!s.maskEnabled);
}
