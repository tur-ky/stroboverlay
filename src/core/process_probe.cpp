#include "core/process_probe.h"

#include <windows.h>

#include <tlhelp32.h>

bool IsProcessRunning(const std::vector<std::wstring>& exeNames) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !found;
         more = Process32NextW(snapshot, &entry)) {
        for (const std::wstring& name : exeNames) {
            if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) found = true;
        }
    }
    CloseHandle(snapshot);
    return found;
}
