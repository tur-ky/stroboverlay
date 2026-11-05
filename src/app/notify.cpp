#include "app/notify.h"

#include <shellapi.h>
#include <strsafe.h>

static const UINT kIconId = 1;

void ShowDesktopNotice(HWND owner, const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = kIconId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = kNoticeMessage;
    data.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    if (!data.hIcon) data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    StringCchCopyW(data.szTip, ARRAYSIZE(data.szTip), L"Stroboverlay");
    Shell_NotifyIconW(NIM_ADD, &data);

    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_WARNING;
    StringCchCopyW(data.szInfoTitle, ARRAYSIZE(data.szInfoTitle), title);
    StringCchCopyW(data.szInfo, ARRAYSIZE(data.szInfo), text);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void RemoveDesktopNotice(HWND owner) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = kIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
}
