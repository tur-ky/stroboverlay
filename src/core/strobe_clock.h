#pragma once

enum class TimingMode { Ms, Frames };

struct TimingSettings {
    TimingMode mode = TimingMode::Frames;
    double onMs = 100, offMs = 100;
    int onFrames = 100, offFrames = 100;
    double refreshHz = 400;
};

// Strobe is the time the overlay is drawn, visible the time the game shows through.
struct Durations {
    double strobeMs;
    double visibleMs;
};

Durations ResolveDurations(const TimingSettings& t);
bool HasPhases(const Durations& d);
