#include "core/strobe_clock.h"
#include "test.h"

TEST(frames_use_refresh_rate) {
    TimingSettings t;
    t.mode = TimingMode::Frames;
    t.onFrames = 100;
    t.offFrames = 50;
    t.refreshHz = 400;
    Durations d = ResolveDurations(t);
    CHECK_NEAR(d.strobeMs, 250.0, 1e-9);
    CHECK_NEAR(d.visibleMs, 125.0, 1e-9);
}

TEST(ms_mode_uses_values_directly) {
    TimingSettings t;
    t.mode = TimingMode::Ms;
    t.onMs = 33;
    t.offMs = 7.5;
    Durations d = ResolveDurations(t);
    CHECK_NEAR(d.strobeMs, 33.0, 1e-9);
    CHECK_NEAR(d.visibleMs, 7.5, 1e-9);
}

TEST(non_positive_refresh_falls_back_to_2_5ms_frame) {
    TimingSettings t;
    t.mode = TimingMode::Frames;
    t.onFrames = 2;
    t.offFrames = 2;
    t.refreshHz = 0;
    CHECK_NEAR(ResolveDurations(t).strobeMs, 5.0, 1e-9);
}

TEST(negative_and_nan_clamp_to_zero) {
    TimingSettings t;
    t.mode = TimingMode::Ms;
    t.onMs = -5;
    t.offMs = NAN;
    Durations d = ResolveDurations(t);
    CHECK(d.strobeMs == 0.0);
    CHECK(d.visibleMs == 0.0);
}

TEST(both_zero_has_no_phases) {
    CHECK(!HasPhases({0.0, 0.0}));
    CHECK(HasPhases({0.0, 5.0}));
    CHECK(HasPhases({5.0, 0.0}));
}

TEST(tiny_durations_get_a_floor) {
    TimingSettings t;
    t.mode = TimingMode::Ms;
    t.onMs = 0.00001;
    t.offMs = 0.0;
    Durations d = ResolveDurations(t);
    CHECK_NEAR(d.strobeMs, 0.1, 1e-9);
    CHECK(d.visibleMs == 0.0);
}
