# Stroboverlay Phase 1: Core and Window Method Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the separate config window and whole-window flash with one DirectComposition overlay holding a strobe layer and an ImGui Rail menu, with monitor choice, profiles, masks, and strobe color or image.

**Architecture:** A UI-free static library (`src/core`) owns settings, timing maths, image composition and menu panels. `src/app` hosts them in a topmost window with two DirectComposition visuals. A bench gate early in the plan decides whether the strobe flips through DirectComposition or a layered window before anything is built on top.

**Tech Stack:** C++17, Win32, D3D11, DXGI, DirectComposition, WIC, Dear ImGui v1.93.0, MSBuild v143, Windows SDK 10.0.26100.

**Spec:** `docs/superpowers/specs/2026-11-05-stroboverlay-overlay-rework-design.md`

Phase 2 (hook, attach, `Auto`, PresentMon) gets its own plan after this ships, because it depends on this phase's bench result and on the Trusted mode check.

## Global Constraints

- Windows 11, x64 only. Config `Release|x64` is the shipping build.
- Output stays `bin\Release\stroboverlay.exe`, with `config.ini` read from beside the exe.
- `config.ini` keys `on_ms`, `off_ms`, `on_frames`, `off_frames`, `units`, `refresh_hz` and `[hotkeys]` keep their meaning.
- UI text: `Strobe` means overlay drawn, `Game visible` means overlay clear. Never show "on" or "off" for durations.
- Menu palette: background `#000000`, surfaces `#141414` and `#222222`, borders `#3A3A3A`, secondary text `#8C8C8C`, primary and active `#FFFFFF`. No other colors except real image or swatch content.
- Hotkeys: `Ctrl+Shift+1` menu, `Ctrl+Shift+3` strobe, `Ctrl+Shift+5` panic quit. `Esc` closes the menu.
- No code touches any game process in this phase.
- Warn Patri before running anything that flashes a monitor. Prefer `DISPLAY3` for visual checks. Bench on `DISPLAY1` needs her OK.
- Never `git add -A`, `git stash`, `git checkout` or `git restore` on her files. Run builds at below-normal priority.
- Comments in code stay plain and sparse. Commit bodies end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

- Portrait monitors (`DISPLAY2`, `DISPLAY3`): strobe, mask and menu must fill and position correctly on a tall target, not a stretched landscape one. Pinned in Task 4 and Task 10.
- Both durations zero, or a duration typed as empty text: no busy loop and no stuck blackout. Pinned in Task 2.
- A `config.ini` from the old build, or none at all: loads with working defaults and keeps `refresh_hz=400` as a manual value. Pinned in Task 3.
- A profile pointing at a deleted image or mask: loads, falls back to color and no mask, warns, keeps the path. Pinned in Task 3 and Task 9.
- PNG masks with transparency, or masks and images of a different aspect than the monitor. Pinned in Task 4.

---

### Task 1: Baseline commit and shared build layout

**Files:**
- Modify: `.gitignore`, `build.bat`, `scripts/build_release.ps1`
- Create: `Directory.Build.props`
- Add unchanged to git: `main.cpp`, `build.bat`, `config.ini`, `flash.ico`, `version.h`, `version.rc`, `scripts/`, `stroboverlay.sln`, `stroboverlay.vcxproj`

**Interfaces:**
- Produces: `bin\baseline\stroboverlay.exe`, the unmodified build for comparison. All projects output to `bin\$(Configuration)\`.

- [ ] **Step 1: Extend `.gitignore`**

Add `bin/`, `x64/`, `stroboverlay/x64/`, `repomix-output.xml`, `latency_profile.csv`, `desktop.ini`, `*.user`, `.vs/`.

- [ ] **Step 2: Commit existing source unchanged**

```bash
git add .gitignore main.cpp build.bat config.ini flash.ico version.h version.rc scripts stroboverlay.sln stroboverlay.vcxproj
git commit -m "chore: add existing source as baseline"
```

- [ ] **Step 3: Copy the current exe aside**

Copy `bin\Release\stroboverlay.exe` to `bin\baseline\stroboverlay.exe`.

- [ ] **Step 4: Centralise output**

`Directory.Build.props` at repo root sets `OutDir` to `$(MSBuildThisFileDirectory)bin\$(Configuration)\`. Remove `/p:OutDir=bin\Release\` from `build.bat`, and change its two `pause` lines to `if not defined CI pause` so it can run unattended.

- [ ] **Step 5: Verify the build**

Run with `CI=1`: `build.bat`
Expected: `Build succeeded` and a fresh `bin\Release\stroboverlay.exe`.

- [ ] **Step 6: Commit**

```bash
git add Directory.Build.props build.bat scripts/build_release.ps1
git commit -m "build: output all projects to bin"
```

### Task 2: Core library, test harness, timing maths

**Files:**
- Create: `src/core/core.vcxproj`, `src/core/strobe_clock.h`, `src/core/strobe_clock.cpp`
- Create: `tests/core_tests.vcxproj`, `tests/test.h`, `tests/main.cpp`, `tests/strobe_clock_test.cpp`
- Modify: `stroboverlay.sln`

**Interfaces:**
- Produces, in `core/strobe_clock.h`:

```cpp
enum class TimingMode { Ms, Frames };
struct TimingSettings {
    TimingMode mode = TimingMode::Frames;
    double onMs = 100, offMs = 100;
    int onFrames = 100, offFrames = 100;
    double refreshHz = 400;
};
struct Durations { double strobeMs; double visibleMs; };
Durations ResolveDurations(const TimingSettings& t);
bool HasPhases(const Durations& d); // false when both are zero
```

- Produces, in `tests/test.h`: `TEST(name)` registration and `CHECK(expr)`, `CHECK_NEAR(a, b, eps)`. `core_tests.exe` prints each failure with file and line, and exits non-zero on any failure.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(frames_use_refresh_rate) {
    TimingSettings t; t.mode = TimingMode::Frames; t.onFrames = 100; t.offFrames = 50; t.refreshHz = 400;
    auto d = ResolveDurations(t);
    CHECK_NEAR(d.strobeMs, 250.0, 1e-9); CHECK_NEAR(d.visibleMs, 125.0, 1e-9);
}
TEST(non_positive_refresh_falls_back_to_2_5ms_frame) {
    TimingSettings t; t.onFrames = 2; t.offFrames = 2; t.refreshHz = 0;
    CHECK_NEAR(ResolveDurations(t).strobeMs, 5.0, 1e-9);
}
TEST(negative_and_nan_clamp_to_zero) {
    TimingSettings t; t.mode = TimingMode::Ms; t.onMs = -5; t.offMs = NAN;
    auto d = ResolveDurations(t);
    CHECK(d.strobeMs == 0.0); CHECK(d.visibleMs == 0.0);
}
TEST(both_zero_has_no_phases) { CHECK(!HasPhases({0.0, 0.0})); CHECK(HasPhases({0.0, 5.0})); }
```

- [ ] **Step 2: Run to verify failure**

Run: `build.bat` with `CI=1`
Expected: FAIL, `ResolveDurations` undefined.

- [ ] **Step 3: Implement `ResolveDurations` and `HasPhases`**

Matches today's worker maths in `main.cpp:352-360`. Add both new projects to the solution, `core_tests` referencing `core`.

- [ ] **Step 4: Verify**

Run: `bin\Release\core_tests.exe`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/core tests stroboverlay.sln
git commit -m "feat(core): add timing maths and test harness"
```

### Task 3: Settings, profiles, hotkey parsing

**Files:**
- Create: `src/core/settings.h`, `src/core/settings.cpp`, `tests/settings_test.cpp`

**Interfaces:**
- Consumes: `TimingSettings`.
- Produces:

```cpp
enum class FillKind { Color, Image };
struct StrobeSettings {
    TimingSettings timing;
    bool refreshAuto = false;
    FillKind fill = FillKind::Color;
    uint32_t colorRgb = 0x000000;
    std::wstring imageFile;   // relative to exe
    bool maskEnabled = false;
    std::wstring maskFile;    // relative to exe
    bool maskInvert = false;
};
struct Hotkey { UINT mods; UINT vk; };
struct AppSettings {
    StrobeSettings strobe;
    std::wstring monitor;     // e.g. \\.\DISPLAY1, empty means primary
    Hotkey toggle, panic, showGui;
};
AppSettings LoadAppSettings(const std::wstring& iniPath);
void SaveAppSettings(const std::wstring& iniPath, const AppSettings&);
StrobeSettings LoadProfile(const std::wstring& path, const StrobeSettings& base);
void SaveProfile(const std::wstring& path, const StrobeSettings&);
std::vector<std::wstring> ListProfiles(const std::wstring& dir); // sorted names, no extension
bool IsValidProfileName(const std::wstring& name);
bool ParseHotkey(const std::wstring& text, Hotkey& out);
std::wstring ProbeMissingAssets(const std::wstring& exeDir, StrobeSettings& s);
```

`ProbeMissingAssets` sets `fill` to `Color` and `maskEnabled` to false in the returned-by-reference copy when a file is missing, leaves the paths intact, and returns a warning naming the file, or empty.

- [ ] **Step 1: Write the failing tests**

Using an ini written to a temp path:

```cpp
TEST(old_config_keeps_manual_refresh) {
    // on_frames=100, off_frames=100, refresh_hz=400, no units key, no new sections
    auto s = LoadAppSettings(p);
    CHECK(s.strobe.timing.mode == TimingMode::Frames);
    CHECK(s.strobe.timing.refreshHz == 400.0);
    CHECK(!s.strobe.refreshAuto);
    CHECK(s.strobe.fill == FillKind::Color); CHECK(s.strobe.colorRgb == 0);
}
TEST(missing_config_gives_auto_refresh_defaults) { CHECK(LoadAppSettings(L"nope.ini").strobe.refreshAuto); }
TEST(refresh_auto_key) { /* refresh_hz=auto */ CHECK(s.strobe.refreshAuto); }
TEST(settings_round_trip_all_fields) { /* save then load, compare every field */ }
TEST(bad_color_falls_back_to_black) { /* color=zzz */ CHECK(s.strobe.colorRgb == 0); }
TEST(out_of_range_values_use_defaults) { /* on_frames=99999 */ CHECK(s.strobe.timing.onFrames == 100); }
TEST(profile_excludes_monitor) { /* saved file has no [display] section */ }
TEST(profile_name_rules) {
    CHECK(IsValidProfileName(L"aim warmup"));
    CHECK(!IsValidProfileName(L"")); CHECK(!IsValidProfileName(L"a/b"));
    CHECK(!IsValidProfileName(L"CON")); CHECK(!IsValidProfileName(L"trail."));
}
TEST(hotkey_parse) {
    Hotkey h; CHECK(ParseHotkey(L"CTRL+SHIFT+3", h));
    CHECK(h.mods == (MOD_CONTROL|MOD_SHIFT)); CHECK(h.vk == '3');
    CHECK(ParseHotkey(L"alt + f12", h)); CHECK(h.vk == VK_F12);
    CHECK(!ParseHotkey(L"CTRL+", h));
}
TEST(missing_asset_falls_back_and_keeps_path) {
    StrobeSettings s; s.fill = FillKind::Image; s.imageFile = L"images\\gone.png";
    auto warn = ProbeMissingAssets(dir, s);
    CHECK(!warn.empty()); CHECK(s.fill == FillKind::Color); CHECK(s.imageFile == L"images\\gone.png");
}
```

- [ ] **Step 2: Run to verify failure**

Run: build, then `bin\Release\core_tests.exe`
Expected: compile failure on `settings.h`.

- [ ] **Step 3: Implement**

Use `GetPrivateProfile*` and `WritePrivateProfileStringW`, as `main.cpp:97-140` does, so comments in `config.ini` survive. Move the hotkey lambda from `main.cpp:527-548`.

- [ ] **Step 4: Verify**

Run: `bin\Release\core_tests.exe`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/core tests
git commit -m "feat(core): add settings, profiles and hotkey parsing"
```

### Task 4: Image loading and strobe composition

**Files:**
- Create: `src/core/image_compose.h`, `src/core/image_compose.cpp`, `tests/image_compose_test.cpp`

**Interfaces:**
- Produces:

```cpp
struct Bitmap { int w = 0, h = 0; std::vector<uint8_t> bgra; };
bool LoadImageStretched(const std::wstring& path, int w, int h, Bitmap& out); // straight alpha
Bitmap ComposeStrobe(uint32_t colorRgb, const Bitmap* fillImage, const Bitmap* mask,
                     bool invert, int w, int h); // premultiplied BGRA
bool ImportAsset(const std::wstring& src, const std::wstring& destDir, std::wstring& outName);
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(no_mask_is_opaque_color) { auto b = ComposeStrobe(0xFF0000, nullptr, nullptr, false, 2, 2);
    CHECK(px(b,0,0) == BGRA(0,0,255,255)); }
TEST(white_mask_draws_black_mask_clears) { /* mask row: white, black */
    CHECK(alpha(b,0,0) == 255); CHECK(alpha(b,1,0) == 0); }
TEST(grey_is_partial_and_premultiplied) { /* 50% grey, red fill */
    CHECK(near(alpha(b,0,0), 128, 1)); CHECK(near(red(b,0,0), 128, 1)); }
TEST(invert_flips_mask) {}
TEST(mask_alpha_multiplies_in) { /* white with alpha 0 */ CHECK(alpha(b,0,0) == 0); }
TEST(rec709_luminance) { /* pure green mask */ CHECK(near(alpha(b,0,0), 182, 1)); }
TEST(image_fill_keeps_its_transparency) {}
TEST(portrait_target_keeps_dimensions) { auto b = ComposeStrobe(0, nullptr, nullptr, false, 1080, 1920);
    CHECK(b.w == 1080 && b.h == 1920 && b.bgra.size() == 1080*1920*4); }
TEST(load_stretches_to_requested_size) { /* WIC-encode 4x4 PNG to temp, load at 8x2 */ }
TEST(corrupt_file_returns_false) {}
TEST(import_suffixes_on_clash) { /* second import of a.png gives a_1.png */ }
```

`near` here takes integers; luminance stays in sRGB values with no linearisation.

- [ ] **Step 2: Run to verify failure**

Expected: compile failure on `image_compose.h`.

- [ ] **Step 3: Implement**

WIC decode to `GUID_WICPixelFormat32bppBGRA`, scaled with `IWICBitmapScaler` Fant. Reject anything where decode fails or either source dimension exceeds 16384. Luminance weights 0.2126, 0.7152, 0.0722.

- [ ] **Step 4: Verify**

Run: `bin\Release\core_tests.exe`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/core tests
git commit -m "feat(core): add image loading and strobe composition"
```

### Task 5: Flip bench gate

**Files:**
- Create: `tools/flip_bench/flip_bench.vcxproj`, `tools/flip_bench/main.cpp`, `docs/latency.md`
- Modify: `stroboverlay.sln`

**Interfaces:**
- Produces: a recorded decision, `DirectComposition` or `Layered`, at the top of `docs/latency.md`. Task 6 builds whichever wins.

- [ ] **Step 1: Write the bench**

`flip_bench.exe <monitor-device> <method> <flips>` creates a fullscreen click-through window on that monitor and alternates opaque black and clear with `<flips>` transitions, 20 ms apart. Methods: `slwa` (today's `SetLayeredWindowAttributes`), `ulw` (`UpdateLayeredWindow` constant alpha), `dcomp` (one composition swapchain, visual `SetOpacity` then `Commit`). Each flip times from before the call to return of `DwmFlush` with QPC. Prints median, p95, p99 and max in ms.

- [ ] **Step 2: Get Patri's OK, then run it on `DISPLAY1`**

This flashes her primary monitor black for about 4 seconds per method. Say so and wait for yes.

Run each method: `bin\Release\flip_bench.exe \\.\DISPLAY1 <method> 200`
Expected: three result rows.

- [ ] **Step 3: Record and decide**

Write the numbers, marked measured, to `docs/latency.md`. Pick `dcomp` unless its median or p99 is more than 0.25 ms worse than `slwa`. If so, pick `ulw`, and in Task 6 the strobe becomes a separate layered window while the menu visual stays DirectComposition.

- [ ] **Step 4: Commit**

```bash
git add tools docs/latency.md stroboverlay.sln
git commit -m "test: add flip bench and record result"
```

### Task 6: Overlay window, strobe layer, worker

**Files:**
- Create: `src/app/overlay_window.h/.cpp`, `src/app/dcomp_renderer.h/.cpp`, `src/app/strobe_worker.h/.cpp`, `src/app/main.cpp`
- Create: `src/core/monitors.h`, `src/core/monitors.cpp`, `tests/monitors_test.cpp`
- Move: `version.h`, `version.rc`, `flash.ico` stay at root; `main.cpp` at root is deleted once `src/app/main.cpp` replaces it
- Modify: `stroboverlay.vcxproj`

**Interfaces:**
- Consumes: `Durations`, `ResolveDurations`, `HasPhases`, `Bitmap`, `ComposeStrobe`, `AppSettings`.
- Produces:

```cpp
struct MonitorInfo { std::wstring device; std::wstring label; RECT rect; double refreshHz; bool primary; };
std::vector<MonitorInfo> EnumerateMonitors();
const MonitorInfo& PickMonitor(const std::vector<MonitorInfo>&, const std::wstring& device); // primary if absent

class OverlayRenderer {
public:
    bool Init(HWND hwnd, int w, int h);
    void SetStrobeBitmap(const Bitmap&);
    void ShowStrobe(bool);         // thread-safe, commits
    void ShowMenuLayer(bool);      // commits
    bool Resize(int w, int h);
    ID3D11Device* Device();
    ID3D11DeviceContext* Context();
    IDXGISwapChain1* MenuSwapChain();
};

class StrobeWorker {
public:
    using Flip = std::function<void(bool show)>;
    bool Start(Flip flip);
    void SetDurations(Durations);
    void SetRunning(bool);
    bool Running() const;
    void Stop();
};
```

`overlay_window` exposes `SetMenuInteractive(bool)` and `SetVisible(bool)`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST(enumerates_a_primary_monitor) { auto m = EnumerateMonitors(); CHECK(!m.empty());
    CHECK(std::count_if(m.begin(), m.end(), [](auto& x){ return x.primary; }) == 1); }
TEST(refresh_is_positive) { for (auto& x : EnumerateMonitors()) CHECK(x.refreshHz > 0); }
TEST(unknown_device_picks_primary) { auto m = EnumerateMonitors(); CHECK(PickMonitor(m, L"\\\\.\\GONE").primary); }
```

- [ ] **Step 2: Run to verify failure**

Expected: compile failure on `monitors.h`.

- [ ] **Step 3: Implement `monitors`**

`EnumDisplayMonitors` plus `QueryDisplayConfig` for the rational `vSyncFreq` of each path, matched by source device name.

- [ ] **Step 4: Implement renderer, window and worker**

Window styles when idle: `WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`. Move the worker from `main.cpp:319-417`, keeping waitable timer, watchdog and drift CSV. When `HasPhases` is false, the worker idles as if stopped. Window is hidden whenever strobe is stopped and menu closed. A topmost `SetWindowPos` per flip is dropped; call it once on show. App starts from `AppSettings`, no menu yet, strobe on `Ctrl+Shift+3`.

If `WS_EX_NOREDIRECTIONBITMAP` with `WS_EX_TRANSPARENT` eats clicks, take the spec's fallback: menu in a second window owned by the same process, keeping the same `OverlayRenderer` interface.

- [ ] **Step 5: Verify tests**

Run: `bin\Release\core_tests.exe`
Expected: exit 0.

- [ ] **Step 6: Verify on `DISPLAY3`, warning Patri first**

Set `monitor=\\.\DISPLAY3`, `units=time`, `on_ms=1000`, `off_ms=1000` in `bin\Release\config.ini`. Start the exe, toggle with the hotkey, capture `DISPLAY3` during each phase, and capture once with strobe stopped.
Expected: black on `DISPLAY3` during strobe, nothing on it otherwise, `DISPLAY1` untouched, clicks pass through. Panic hotkey exits with no leftover window.

- [ ] **Step 7: Commit**

```bash
git add src tests stroboverlay.vcxproj main.cpp
git commit -m "feat(app): draw strobe through composition overlay on chosen monitor"
```

### Task 7: Menu host, Rail shell, Timing section

**Files:**
- Create: `third_party/imgui/` (v1.93.0: core files plus `backends/imgui_impl_dx11.*`, `backends/imgui_impl_win32.*`, `LICENSE.txt`)
- Create: `src/core/menu.h`, `src/core/menu.cpp`, `src/core/menu_theme.cpp`, `src/app/menu_host.h/.cpp`, `tests/menu_test.cpp`
- Modify: `src/core/core.vcxproj`, `stroboverlay.vcxproj`, `src/app/main.cpp`

**Interfaces:**
- Consumes: `OverlayRenderer`, `StrobeWorker`, `SetMenuInteractive`, `AppSettings`.
- Produces:

```cpp
enum class MenuSection { Timing, Appearance, Mask, Profiles, Display };
struct MenuContext {
    AppSettings* settings;
    bool running;
    std::vector<MonitorInfo> monitors;
    std::vector<std::wstring> images, masks, profiles;
    std::wstring warning;
    float dpiScale;
};
struct MenuActions {
    bool strobeSettingsChanged = false, toggleStrobe = false, closeMenu = false;
    bool importImage = false, importMask = false;
    std::wstring loadProfile, saveProfile, deleteProfile;
    std::wstring selectMonitor;
};
void ApplyMenuTheme(float dpiScale);
MenuActions DrawMenu(MenuContext&, MenuSection& section);
```

`core` builds ImGui's core files. `menu_host` owns backends, focus handling and the render loop: while open, the message loop uses `PeekMessage` and presents with sync interval 1; while closed, it blocks in `GetMessage`.

- [ ] **Step 1: Write the failing tests**

Using a headless ImGui context with a font atlas built and a 1024x768 display:

```cpp
TEST(theme_uses_only_greys) { ApplyMenuTheme(1.0f);
    for (auto& c : ImGui::GetStyle().Colors) CHECK(c.x == c.y && c.y == c.z); }
TEST(timing_labels_say_strobe_and_game_visible) {
    auto labels = DrawAndCollectLabels(MenuSection::Timing);
    CHECK(contains(labels, "Strobe")); CHECK(contains(labels, "Game visible"));
    CHECK(!contains(labels, "On")); CHECK(!contains(labels, "Off"));
}
```

`DrawAndCollectLabels` runs one ImGui frame. `menu.cpp` routes section labels through a small `Label(const char*)` helper, which also appends to `std::vector<std::string>* g_menuTextLog` when that pointer is set.

- [ ] **Step 2: Run to verify failure**

Expected: compile failure on `menu.h`.

- [ ] **Step 3: Implement theme and Rail shell**

Palette from Global Constraints. Rail icons from Segoe MDL2 Assets codepoints: Timing `E916`, Appearance `E790`, Mask `E8B2`, Profiles `E8F1`, Display `E7F4`. Text in Segoe UI at 15 px times `dpiScale`. If either font is absent, use ImGui's default font and text labels in the rail. Panel 420 by 460 px times `dpiScale`, pinned 24 px from the monitor's top left, not movable.

- [ ] **Step 4: Implement Timing section**

`Strobe` and `Game visible` each as a slider with a typed input. `ms` / `frames` switch. Refresh rate: `Auto` checkbox showing detected Hz, otherwise a typed value. Start/stop button. Changes apply live.

- [ ] **Step 5: Implement `menu_host` focus flow**

Open: record `GetForegroundWindow()`, `SetMenuInteractive(true)`, `SetVisible(true)`, `ShowMenuLayer(true)`, `SetForegroundWindow(overlay)`. Close, by `Ctrl+Shift+1` or `Esc`: reverse, then `SetForegroundWindow(previous)` if it still exists. Launch opens the menu. Settings save on menu close and on quit.

- [ ] **Step 6: Verify tests**

Run: `bin\Release\core_tests.exe`
Expected: exit 0.

- [ ] **Step 7: Verify on `DISPLAY3`**

Open with `Ctrl+Shift+1`, screenshot, change `Strobe` while running, close with `Esc`.
Expected: Rail menu renders, strobe keeps flashing under it and the menu itself never flashes, previous window regains focus.

- [ ] **Step 8: Commit**

```bash
git add third_party src tests stroboverlay.vcxproj
git commit -m "feat(menu): add Rail menu with Timing section"
```

### Task 8: Appearance and Mask sections

**Files:**
- Modify: `src/core/menu.cpp`, `src/app/menu_host.cpp`, `src/app/main.cpp`, `tests/menu_test.cpp`
- Create: `src/app/asset_store.h/.cpp`

**Interfaces:**
- Consumes: `ComposeStrobe`, `LoadImageStretched`, `ImportAsset`, `ProbeMissingAssets`, `SetStrobeBitmap`.
- Produces:

```cpp
bool NeedsRecompose(const StrobeSettings& before, const StrobeSettings& after); // in core/settings.h
class AssetStore {
public:
    explicit AssetStore(std::wstring exeDir);
    std::vector<std::wstring> List(const wchar_t* subdir);
    bool Import(HWND owner, const wchar_t* subdir, std::wstring& outRelative); // IFileOpenDialog
    Bitmap BuildStrobe(const StrobeSettings&, int w, int h, std::wstring& warning);
    ID3D11ShaderResourceView* Thumbnail(const std::wstring& relative); // grey placeholder if unreadable
};
```

- [ ] **Step 1: Write the failing test**

```cpp
TEST(recompose_only_on_look_changes) {
    StrobeSettings a, b = a;
    b.timing.onMs = 5; CHECK(!NeedsRecompose(a, b));
    b = a; b.colorRgb = 0xFFFFFF; CHECK(NeedsRecompose(a, b));
    b = a; b.fill = FillKind::Image; CHECK(NeedsRecompose(a, b));
    b = a; b.maskFile = L"masks\\a.png"; CHECK(NeedsRecompose(a, b));
    b = a; b.maskEnabled = true; CHECK(NeedsRecompose(a, b));
    b = a; b.maskInvert = true; CHECK(NeedsRecompose(a, b));
}
```

- [ ] **Step 2: Run to verify failure**

Expected: compile failure on `NeedsRecompose`.

- [ ] **Step 3: Implement sections**

Appearance: `Color` / `Image` switch, `ImGui::ColorPicker3` on the color, image thumbnails with `Import image`. Mask: enable checkbox, `Invert`, thumbnails with `Import mask`, and a line of help text: `White draws the strobe, black leaves the game visible`. Filters: `*.png;*.jpg;*.jpeg;*.bmp`.

- [ ] **Step 4: Wire recompose**

`main.cpp` compares settings before and after `DrawMenu` with `NeedsRecompose`, then runs `AssetStore::BuildStrobe` and `SetStrobeBitmap` on the UI thread. Warnings go to `MenuContext::warning`.

- [ ] **Step 5: Verify tests**

Run: `bin\Release\core_tests.exe`
Expected: exit 0.

- [ ] **Step 6: Verify on `DISPLAY3`**

Import a half-black, half-white PNG as mask, set a color and then an image fill, toggle invert. Capture each.
Expected: strobe covers only the white half, color and image both show, invert swaps sides, files appear under `bin\Release\masks` and `images`.

- [ ] **Step 7: Commit**

```bash
git add src tests
git commit -m "feat(menu): add Appearance and Mask sections"
```

### Task 9: Profiles section

**Files:**
- Modify: `src/core/menu.cpp`, `src/app/main.cpp`, `tests/menu_test.cpp`

**Interfaces:**
- Consumes: `LoadProfile`, `SaveProfile`, `ListProfiles`, `IsValidProfileName`, `ProbeMissingAssets`, `MenuActions::{loadProfile,saveProfile,deleteProfile}`.
- Produces, in `core/menu.h`:

```cpp
class ConfirmState {
public:
    bool Request(const std::wstring& name); // true only on a second request for the same name
    std::wstring pending;
};
```

- [ ] **Step 1: Write the failing test**

```cpp
TEST(delete_needs_second_request_for_same_name) {
    ConfirmState c;
    CHECK(!c.Request(L"a")); CHECK(c.Request(L"a"));
    CHECK(!c.Request(L"a")); CHECK(!c.Request(L"b")); CHECK(c.Request(L"b"));
}
```

- [ ] **Step 2: Run to verify failure**

Expected: compile failure on `ConfirmState`.

- [ ] **Step 3: Implement**

List with `Load`, `Save` over the selected profile, `Save as` with a name field, `Delete` through `ConfirmState`, whose button reads `Confirm delete` while pending. An invalid name shows `Use a name without \ / : * ? " < > |` and doesn't save. Loading a profile with a missing image or mask goes through `ProbeMissingAssets` and shows its warning, then recomposes. Profile files live in `profiles\` beside the exe; the directory is created on first save.

- [ ] **Step 4: Verify tests and in the app**

Run: `bin\Release\core_tests.exe`, then save two profiles on `DISPLAY3`, switch between them, delete one, delete the mask file behind the other and load it.
Expected: exit 0; settings restore; missing mask shows a warning and strobe still works.

- [ ] **Step 5: Commit**

```bash
git add src tests
git commit -m "feat(menu): add Profiles section"
```

### Task 10: Display section and failure notices

**Files:**
- Modify: `src/core/menu.cpp`, `src/app/main.cpp`, `src/app/overlay_window.cpp`, `src/app/menu_host.cpp`
- Create: `src/app/notify.h/.cpp`, `src/core/process_probe.h/.cpp`, `tests/process_probe_test.cpp`

**Interfaces:**
- Consumes: `EnumerateMonitors`, `PickMonitor`, `OverlayRenderer::Resize`, `MenuActions::selectMonitor`.
- Produces:

```cpp
bool IsProcessRunning(const std::vector<std::wstring>& exeNames); // process snapshot, case-insensitive
void ShowDesktopNotice(HWND owner, const wchar_t* title, const wchar_t* text);
float MenuScaleForDpi(UINT dpi); // dpi / 96.0f
```

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(finds_own_process) { CHECK(IsProcessRunning({L"CORE_TESTS.EXE"})); }
TEST(missing_process) { CHECK(!IsProcessRunning({L"definitely-not-running.exe"})); }
TEST(dpi_scale) { CHECK(MenuScaleForDpi(96) == 1.0f); CHECK(MenuScaleForDpi(144) == 1.5f); }
```

- [ ] **Step 2: Run to verify failure**

Expected: compile failure on `process_probe.h`.

- [ ] **Step 3: Implement Display section**

Monitor list showing label, resolution and refresh. Selecting one moves the window, rebuilds swapchains, recomposes the strobe at the new size, reloads the refresh value when `refreshAuto` is on, and rescales the menu by `MenuScaleForDpi` of that monitor. `WM_DISPLAYCHANGE` and `WM_DPICHANGED` do the same. A saved monitor that's gone uses primary and warns, keeping the saved value. On `DXGI_ERROR_DEVICE_REMOVED` or `DXGI_ERROR_DEVICE_RESET`, rebuild the renderer once; if that fails, stop the strobe and warn.

- [ ] **Step 4: Implement notices**

`ShowDesktopNotice` uses `Shell_NotifyIcon` with `NIF_INFO` on a tray icon from `flash.ico`, added for the notice and removed when dismissed. Use it when a hotkey fails to register, and when strobe starts while `faceitclient.exe` or `faceitservice.exe` runs, with text `FACEIT is running, so the overlay may not show.` The start still proceeds.

- [ ] **Step 5: Verify tests**

Run: `bin\Release\core_tests.exe`
Expected: exit 0.

- [ ] **Step 6: Verify on all three monitors, warning Patri first**

Switch to `DISPLAY2` and `DISPLAY3` from the menu, with a mask active, and back. For `DISPLAY1`, wait for her OK.
Expected: strobe fills each monitor at its own orientation, mask stretches to it, menu stays readable.

- [ ] **Step 7: Commit**

```bash
git add src tests
git commit -m "feat(menu): add Display section and desktop notices"
```

### Task 11: Finish, measure, ship

**Files:**
- Modify: `version.h`, `config.ini`, `README.md`, `docs/latency.md`, `scripts/run_overlay.ps1` if paths changed

- [ ] **Step 1: Update docs and version**

Version `0.10.0.0`, `BUILD_ID` `Build-0.10.0-revA`. `config.ini` comment reflects `Strobe` and `Game visible` meaning of `on_*` and `off_*`, and lists new sections with spec defaults. `README.md` is UTF-16 today; rewrite it as UTF-8 with hotkeys, monitor choice, mask rule, profiles.

- [ ] **Step 2: Grep for leftovers**

Run: `git grep -n -E "CreateGuiWindow|StroboGuiClass|IDC_EDIT_|\"On |\"Off " -- src`
Expected: no matches.

- [ ] **Step 3: Compare timing with the baseline, Patri's OK first**

Run `bin\baseline\stroboverlay.exe` then `bin\Release\stroboverlay.exe` on `DISPLAY1` with the same `on_frames=100`, `off_frames=100`, `refresh_hz=400`, for 30 seconds each. Read drift from each newest `logs\drift_*.csv`.
Expected: median and p99 drift no worse than baseline by more than 0.25 ms. Append to `docs/latency.md`, marked measured.

- [ ] **Step 4: Run full build and tests**

Run with `CI=1`: `build.bat`, then `bin\Release\core_tests.exe`
Expected: build succeeds, exit 0.

- [ ] **Step 5: Hand to Patri for in-game confirmation**

Ask her to try KovaaK's and CS2 in borderless: menu opens with `Ctrl+Shift+1`, strobe runs behind it, closing returns control to the game, panic quits. Success criteria 1 to 5 and 7 and 8 from the spec must hold before calling this done. Criterion 6 belongs to phase 2.

- [ ] **Step 6: Commit, push, PR, merge**

```bash
git add version.h config.ini README.md docs/latency.md
git commit -m "chore: release 0.10.0"
git push -u origin feat/overlay-rework
```

Open the PR with `gh pr create`, ending the body with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`, then merge it under the standing PR rule unless CI fails or a review comment is open. Update `Claude Memory/stroboverlay/- MEMORY.md` to the new state.
