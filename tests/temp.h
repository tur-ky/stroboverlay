#pragma once
#include <windows.h>

#include <cstdio>
#include <string>

// Creates a fresh folder under %TEMP% and returns its path with a trailing slash.
inline std::wstring MakeTempDir() {
    static int counter = 0;
    wchar_t base[MAX_PATH];
    GetTempPathW(MAX_PATH, base);
    std::wstring dir = std::wstring(base) + L"strobo_test_" +
                       std::to_wstring(GetCurrentProcessId()) + L"_" +
                       std::to_wstring(++counter) + L"\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

inline void WriteTextFile(const std::wstring& path, const char* text) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}
