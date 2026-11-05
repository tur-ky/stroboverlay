#pragma once
#include <windows.h>

// Sent to the owner window when the tray icon's notice is dismissed or times out.
const UINT kNoticeMessage = WM_APP + 1;

// Sent by a second launch, which then exits, so the running copy shows its menu.
const UINT kOpenMenuMessage = WM_APP + 2;

// A Windows notification, for things the menu may not be able to show.
void ShowDesktopNotice(HWND owner, const wchar_t* title, const wchar_t* text);
void RemoveDesktopNotice(HWND owner);
