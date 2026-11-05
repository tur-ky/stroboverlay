# Stroboverlay overlay rework: design

Date: 2026-11-05. Status: awaiting Patri's review.

## Goal

Turn stroboverlay from a separate config window plus a flashing layer into one overlay that behaves like a game overlay. The menu is a second layer, hidden until a hotkey opens it. Add a monitor picker, saved profiles, mask images, strobe colors and strobe images, clear labels, and a new look. Pick the lowest-latency way of drawing for each game and display mode.

## Success criteria

1. With strobe stopped and menu closed, nothing from stroboverlay is on screen and no window of ours sits over the game.
2. The hotkey opens the menu over KovaaK's and CS2 in borderless, and the strobe keeps running behind it.
3. The strobe runs on any of the three monitors.
4. A mask image limits the strobe to the areas it marks.
5. A saved profile restores timing, color or image, and mask.
6. `Auto` uses the method measured fastest for that game and display mode, and refuses injection when a guard fails.
7. Flash timing in window mode is no worse than the current build, measured.
8. Panic hotkey ends all strobing in every mode.

## Facts this design rests on

Checked on Patri's PC on 2026-11-05.

- Monitors: `DISPLAY1` 1280x1024 primary at a reported 399 Hz, `DISPLAY2` 1440x2560, `DISPLAY3` 1080x1920.
- GPU: RTX 5070. Toolchain: Visual Studio 2022 Community, v143, Windows SDK 10.0.26100.
- KovaaK's: UE4, DX11 (`LogD3D11RHI`), `FullscreenMode=1` (borderless). Executable `FPSAimTrainer-Win64-Shipping.exe`. Ships and starts `anybrainSDK.dll` with the Steam auth ticket, a behavioral anticheat.
- CS2: `cs2.exe` in `S:\SteamLibrary`. No launch options set, so DX11. Ships `rendersystemvulkan.dll` as an alternative renderer. The saved video config for account 436231218 has `setting.fullscreen 1`; account 1605134072 has borderless.
- FACEIT: `FACEIT_AC.sys` is a `SYSTEM_START` kernel driver, loaded from boot and running. Client processes are `faceitclient.exe` and `faceitservice.exe`.
- Patri uses stroboverlay only on KovaaK's and on CS2 VAC-less servers, in borderless. Not on Fortnite or FACEIT.
- DirectComposition is documented as thread-safe from any thread. A `Commit` submits commands pending from all threads.

## Decisions already made

- Both draw methods exist: a window, and an injected DLL. `Auto` chooses per game and display mode on measured latency. A tie goes to window, because it doesn't touch the game.
- KovaaK's is on the injection allowlist despite Anybrain. Patri has weighed this.
- Injection never bypasses or evades anything. If a game or anticheat blocks it, stroboverlay stays in window mode. No anticheat code, service or driver is touched.
- Exclusive fullscreen is served by injection only. A window can't appear there.
- Menu look: Rail layout, in black, white and grey only.
- The on/off fields are relabelled `Strobe` (overlay drawn) and `Game visible` (overlay clear).

## Out of scope

Image sequences, per-monitor profiles, strobe opacity control, Vulkan or DX12 hooks, code signing, any game beyond the two on the allowlist, and multiple simultaneous monitors.

## Architecture

```
src/core/    static lib, no window or graphics API dependency
  settings      config.ini and profile load/save
  strobe_clock  strobe/visible phase logic, time or frame driven
  image_compose WIC decode, stretch, mask into premultiplied BGRA
  method_select pure decision: inputs to method plus reason
  menu          ImGui panels and theme, written against a small host interface
src/app/     stroboverlay.exe
  overlay_window, dcomp_renderer, strobe_worker, hotkeys,
  game_probe, attach, shared_state
src/hook/    stroboverlay_hook.dll (phase 2)
  present_hook, input_filter, shared_state
third_party/ imgui, minhook (phase 2)
tests/       core_tests.exe
```

`main.cpp` is split along these lines. `stroboverlay.sln` gains projects for core, tests and later hook. `build.bat` still produces `bin\Release\stroboverlay.exe`.

### Shared state of a strobe

`image_compose` produces one premultiplied BGRA bitmap the size of the target: fill color or fill image, multiplied by the mask. Both methods show that bitmap. It is rebuilt only when settings change, so a flash costs the same regardless of mask detail.

### Window method

One topmost window on the chosen monitor, created with `WS_EX_NOREDIRECTIONBITMAP`. DirectComposition holds two visuals:

- Strobe visual: a composition swapchain presented once per recompose.
- Menu visual, above it: a composition swapchain drawn by Dear ImGui's D3D11 backend, only while the menu is open.

`strobe_worker` keeps today's timing thread, waitable timer, watchdog and drift CSV. A flip toggles the strobe visual, commits, and calls `DwmFlush` as today.

If phase 1 measurement shows this is slower than `SetLayeredWindowAttributes`, the strobe moves to a layered window with per-pixel alpha, set up once by `UpdateLayeredWindow` and flipped by constant alpha. The menu stays as designed.

Input and focus:

- Menu closed: window has `WS_EX_TRANSPARENT` and `WS_EX_NOACTIVATE`, so clicks reach the game.
- Menu open: those styles are cleared, the previous foreground window is recorded, and the overlay takes focus. The hotkey delivery grants that right.
- Menu closed again: styles return and focus goes back to the recorded window.
- Strobe stopped and menu closed: the window is hidden.

### Injected method (phase 2)

`stroboverlay_hook.dll` is loaded into the target by `CreateRemoteThread` on `LoadLibraryW`. Inside the game it:

- Finds the swapchain vtable with a throwaway device, and uses MinHook on `Present` and `ResizeBuffers`, plus `Present1` if the game uses it.
- Draws the strobe bitmap as a fullscreen quad, then the same ImGui menu, using `SwapDeviceContextState` so the game's pipeline state stays intact.
- Advances `strobe_clock` once per `Present`. In frames mode, frames are game frames. In time mode, phases change on the first `Present` after the deadline.
- Subclasses the game window to feed ImGui and, while the menu is open, drops input messages so the game doesn't react. If a game reads raw input another way, `GetRawInputData` and `GetRawInputBuffer` get hooked too.

The exe stays sole owner of global hotkeys and forwards them through a named shared memory block, `Local\stroboverlay_v1`, which carries commands (menu toggle, strobe toggle, unload) and a settings generation counter. Both sides read the same files. Window strobing pauses while attached.

The DLL unloads itself on panic, on exe exit, or on any init failure: it disables hooks, waits for in-flight `Present` calls, restores the window procedure, then calls `FreeLibraryAndExitThread`. A failed attach falls back to window mode with a message in the menu.

### Method selection

`method_select` is a pure function. Inputs: override, executable name, `-insecure` present, FACEIT client running, exclusive or borderless, and the measured table. Output: method plus a reason string the menu shows.

It runs when strobe starts, never in the background. `game_probe` gathers the inputs:

- Foreground window, process id, and executable name by `QueryFullProcessImageNameW`.
- Command line by `NtQueryInformationProcess` class 60, needing only limited query access.
- FACEIT client by a process snapshot for `faceitclient.exe` and `faceitservice.exe`.
- Exclusive fullscreen by `SHQueryUserNotificationState`.

These checks stay outside the game and never open it for memory access. Renderer isn't probed from outside: a DLL that finds no DX11 device unloads itself through the normal failure path.

Injection is allowed only when all of these hold:

1. Executable is exactly `cs2.exe` or `FPSAimTrainer-Win64-Shipping.exe`.
2. For `cs2.exe`, the command line contains `-insecure`.
3. No FACEIT client process is running.

The override offers `Auto`, `Window` and `Injected`. `Injected` can't skip the guards.

| Situation | Result |
| --- | --- |
| Borderless, injection allowed | Faster method from measured table; tie goes to window |
| Borderless, injection refused | Window, reason shown |
| Exclusive, injection allowed | Injected |
| Exclusive, injection refused | No strobe; menu can't appear over the game either, so a desktop notification states the reason |
| Any other game | Window |

## Menu

Rail layout: a narrow icon rail on the left, one section at a time on the right. Panel stays open beside the running strobe.

- Timing: `Strobe` and `Game visible` as slider plus typed number, a `ms` / `frames` switch, refresh rate.
- Appearance: color or image fill, color picker, image list, import.
- Mask: on/off, invert, thumbnails of imported masks, import.
- Profiles: list, load, save, save as, delete with confirmation.
- Display: monitor list with resolution and refresh rate, method override, last method chosen and why.

Black, white and grey only. Background `#000000`, raised surfaces `#141414` and `#222222`, borders `#3A3A3A`, secondary text `#8C8C8C`, primary text and active state `#FFFFFF`. The only color on screen is the strobe color swatch and image thumbnails, which show real content. Text uses Segoe UI and rail icons use Segoe MDL2 Assets from `C:\Windows\Fonts`, falling back to ImGui's default font with text labels if missing.

The app opens with the menu showing. `Ctrl+Shift+1` toggles it, `Esc` closes it.

## Data

`config.ini` stays, with existing keys and meanings, so `on_ms`, `off_ms`, `on_frames` and `off_frames` are unchanged in the file. New keys:

```ini
[overlay]
refresh_hz=auto        ; or a number

[appearance]
fill=color             ; color or image
color=000000
image=

[mask]
enabled=0
file=
invert=0

[display]
monitor=\\.\DISPLAY1
method=auto
```

- Profiles are `profiles/<name>.ini` beside the exe, holding `[overlay]`, `[appearance]` and `[mask]`. Monitor and method stay global.
- Imports are copied into `images/` or `masks/` beside the exe. A name clash gets a numeric suffix. Paths in settings are relative to the exe.
- PNG, JPG and BMP through WIC. Stretched to the target, no aspect correction.
- Mask rule: white draws the strobe, black leaves the game visible, grey is partial. Alpha in the mask multiplies in. `invert` flips it. Luminance uses Rec. 709 weights.
- Refresh `auto` reads the chosen monitor through `QueryDisplayConfig`.

## Errors

- Missing or unreadable image: fall back to plain color and mask off, warn in the menu, keep the setting so restoring the file works.
- Saved monitor gone: use primary, warn, keep the saved value.
- Profile name with characters illegal in a filename: refuse with a message.
- Invalid numbers in `config.ini`: use defaults, as today.
- Display change: rebuild swapchains and recompose.
- Device lost, in window mode: rebuild once, then stop the strobe and warn.
- Hotkey registration failure: warn in the menu instead of only `OutputDebugString`.

## Testing

Automated, in `core_tests.exe`: `strobe_clock` phases in both time and frame modes, settings and profile round trips including old `config.ini` files, profile name validation, mask composition including invert and grey, and every row of `method_select`.

Latency, phase 1: new window build against the current build, same timing, using drift CSVs and flip-to-`DwmFlush` time.

Latency, phase 2: PresentMon across KovaaK's and CS2, borderless and exclusive, window and injected, strobe running, recording present mode and display latency. PresentMon download needs Patri's OK. Results go in `docs/latency.md` and set the table in `method_select`.

Manual: Claude builds and runs everything that doesn't need a game, warning before anything flashes a monitor. Patri confirms in-game behavior in KovaaK's and CS2.

## Delivery

1. Baseline commit of the existing source, unchanged, so later diffs are readable. `.gitignore` adds `bin/`, `x64/`, `stroboverlay/x64/`, `repomix-output.xml`, `latency_profile.csv` and `desktop.ini`.
2. Phase 1 PR: core, window method, menu, all features in window mode.
3. Phase 2 PR: hook, attach, guards, `Auto`, measurement.

Phase 2 opens with a throwaway check of whether `-insecure` alone lets the DLL load in CS2 Trusted mode. If it doesn't, `-allow_third_party_software` is added to the launch requirement. It stays a launch option Patri sets, never something stroboverlay changes.

## Open risks

- Whether FACEIT's always-loaded driver records injection into CS2 while its client is closed is unknown. Raised with Patri. The client guard stays; the driver being loaded doesn't block.
- Whether VAC is active on a client launched without `-insecure` that joins a VAC-less server is unconfirmed, which is why the guard reads the client's command line and not the server.
- What Anybrain does about a loaded DLL, and what a flag costs, is unknown. Accepted by Patri.
- Blocking mouse look in CS2 while the injected menu is open is untested.
- `WS_EX_NOREDIRECTIONBITMAP` with click-through styles is confirmed only by building it. If it fails, the menu keeps its behavior through a second window owned by the same process.
- Any game update can break the hook. The failure path is unload and window mode.
