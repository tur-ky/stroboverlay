#pragma once
#include <windows.h>

#include <cstdio>
#include <functional>
#include <string>

#include "core/strobe_clock.h"

// Runs the strobe and visible phases on its own thread, with a waitable timer, a
// stall watchdog and a drift CSV in logDir.
class StrobeWorker {
public:
    using Flip = std::function<void(bool show)>;

    bool Start(Flip flip, std::wstring logDir);
    void SetDurations(Durations durations);
    void SetRunning(bool running);
    bool Running() const;
    void Stop();

private:
    static DWORD WINAPI ThreadMain(LPVOID self);
    void Run();
    // Waits out one phase, picking up new durations for the next one. Returns false to quit.
    bool WaitPhase(HANDLE timer, double ms);
    void Snapshot(bool& running, Durations& durations);
    void OpenLog();
    void CloseLog();
    void LogPhase(const wchar_t* phase, double expectedMs, double actualMs, long long qpc,
                  bool stalled);

    Flip flip_;
    std::wstring logDir_;
    HANDLE thread_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE updateEvent_ = nullptr;
    mutable CRITICAL_SECTION lock_;
    bool lockReady_ = false;
    bool running_ = false;
    Durations durations_{0.0, 0.0};
    FILE* log_ = nullptr;
    long long seq_ = 0;
    double qpcToMs_ = 0.0;
};
