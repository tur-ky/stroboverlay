#include "core/strobe_clock.h"

#include <cmath>

static double Sanitize(double ms) {
    return (std::isfinite(ms) && ms > 0.0) ? ms : 0.0;
}

Durations ResolveDurations(const TimingSettings& t) {
    if (t.mode == TimingMode::Ms) {
        return {Sanitize(t.onMs), Sanitize(t.offMs)};
    }
    const double frameMs = (t.refreshHz > 0.0) ? 1000.0 / t.refreshHz : 2.5;
    return {Sanitize(t.onFrames * frameMs), Sanitize(t.offFrames * frameMs)};
}

bool HasPhases(const Durations& d) {
    return d.strobeMs > 0.0 || d.visibleMs > 0.0;
}
