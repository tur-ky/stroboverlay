#include "core/strobe_clock.h"

#include <cmath>

// A shorter phase would round to a zero timer wait and spin the worker thread.
static const double kMinPhaseMs = 0.1;

static double Sanitize(double ms) {
    if (!std::isfinite(ms) || ms <= 0.0) return 0.0;
    return ms < kMinPhaseMs ? kMinPhaseMs : ms;
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
