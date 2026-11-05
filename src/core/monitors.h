#pragma once
#include <windows.h>

#include <string>
#include <vector>

struct MonitorInfo {
    std::wstring device;  // \\.\DISPLAY1
    std::wstring label;   // Monitor name, or the device name if Windows has none.
    RECT rect;
    double refreshHz;
    bool primary;
};

std::vector<MonitorInfo> EnumerateMonitors();

// Returns the named monitor, or the primary if it isn't connected.
const MonitorInfo& PickMonitor(const std::vector<MonitorInfo>& monitors,
                               const std::wstring& device);
