#include "core/monitors.h"

#include <cwchar>

namespace {

struct PathDetails {
    std::wstring device;
    std::wstring friendlyName;
    double refreshHz;
};

std::vector<PathDetails> QueryPaths() {
    std::vector<PathDetails> out;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return out;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                           modes.data(), nullptr) != ERROR_SUCCESS) {
        return out;
    }
    paths.resize(pathCount);

    for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        std::wstring friendly;
        if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) {
            friendly = target.monitorFriendlyDeviceName;
        }

        const DISPLAYCONFIG_RATIONAL& rate = path.targetInfo.refreshRate;
        double hz = rate.Denominator ? static_cast<double>(rate.Numerator) / rate.Denominator : 0.0;
        out.push_back({source.viewGdiDeviceName, friendly, hz});
    }
    return out;
}

}  // namespace

std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM param) -> BOOL {
            auto* out = reinterpret_cast<std::vector<MonitorInfo>*>(param);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(mon, &mi)) {
                out->push_back({mi.szDevice, mi.szDevice, mi.rcMonitor, 0.0,
                                (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));

    std::vector<PathDetails> paths = QueryPaths();
    for (MonitorInfo& m : monitors) {
        for (const PathDetails& p : paths) {
            if (_wcsicmp(p.device.c_str(), m.device.c_str()) != 0) continue;
            m.refreshHz = p.refreshHz;
            if (!p.friendlyName.empty()) m.label = p.friendlyName;
            break;
        }
        if (m.refreshHz <= 0.0) {
            DEVMODEW dm{};
            dm.dmSize = sizeof(dm);
            bool ok = EnumDisplaySettingsW(m.device.c_str(), ENUM_CURRENT_SETTINGS, &dm) != 0;
            m.refreshHz = (ok && dm.dmDisplayFrequency > 1) ? dm.dmDisplayFrequency : 60.0;
        }
    }
    return monitors;
}

const MonitorInfo& PickMonitor(const std::vector<MonitorInfo>& monitors,
                               const std::wstring& device) {
    const MonitorInfo* primary = &monitors.front();
    for (const MonitorInfo& m : monitors) {
        if (!device.empty() && _wcsicmp(m.device.c_str(), device.c_str()) == 0) return m;
        if (m.primary) primary = &m;
    }
    return *primary;
}
