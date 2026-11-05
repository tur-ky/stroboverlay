#include <algorithm>

#include "core/monitors.h"
#include "core/settings.h"
#include "test.h"

TEST(enumerates_a_primary_monitor) {
    std::vector<MonitorInfo> m = EnumerateMonitors();
    CHECK(!m.empty());
    CHECK(std::count_if(m.begin(), m.end(), [](const MonitorInfo& x) { return x.primary; }) == 1);
}

TEST(refresh_is_positive) {
    for (const MonitorInfo& x : EnumerateMonitors()) CHECK(x.refreshHz > 0);
}

TEST(monitor_rect_is_not_empty) {
    for (const MonitorInfo& x : EnumerateMonitors()) {
        CHECK(x.rect.right > x.rect.left);
        CHECK(x.rect.bottom > x.rect.top);
        CHECK(!x.label.empty());
    }
}

TEST(unknown_device_picks_primary) {
    std::vector<MonitorInfo> m = EnumerateMonitors();
    CHECK(PickMonitor(m, L"\\\\.\\GONE").primary);
    CHECK(PickMonitor(m, L"").primary);
}

TEST(known_device_is_picked) {
    std::vector<MonitorInfo> m = EnumerateMonitors();
    for (const MonitorInfo& x : m) CHECK(PickMonitor(m, x.device).device == x.device);
}

TEST(auto_refresh_uses_the_monitor_rate) {
    StrobeSettings s;
    s.refreshAuto = true;
    s.timing.mode = TimingMode::Frames;
    s.timing.onFrames = 60;
    s.timing.offFrames = 60;
    s.timing.refreshHz = 400;
    CHECK_NEAR(StrobeDurations(s, 60.0).strobeMs, 1000.0, 1e-9);
}

TEST(manual_refresh_ignores_the_monitor_rate) {
    StrobeSettings s;
    s.refreshAuto = false;
    s.timing.mode = TimingMode::Frames;
    s.timing.onFrames = 40;
    s.timing.refreshHz = 400;
    CHECK_NEAR(StrobeDurations(s, 60.0).strobeMs, 100.0, 1e-9);
}
