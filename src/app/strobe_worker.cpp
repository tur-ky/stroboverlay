#include "app/strobe_worker.h"

#include <dwmapi.h>

#include "version.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static const double kWatchdogMs = 250.0;

bool StrobeWorker::Start(Flip flip, std::wstring logDir) {
    flip_ = std::move(flip);
    logDir_ = std::move(logDir);
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    qpcToMs_ = 1000.0 / static_cast<double>(freq.QuadPart);

    InitializeCriticalSection(&lock_);
    lockReady_ = true;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    updateEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stopEvent_ || !updateEvent_) return false;
    thread_ = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
    return thread_ != nullptr;
}

void StrobeWorker::SetDurations(Durations durations) {
    EnterCriticalSection(&lock_);
    durations_ = durations;
    LeaveCriticalSection(&lock_);
    SetEvent(updateEvent_);
}

void StrobeWorker::SetRunning(bool running) {
    EnterCriticalSection(&lock_);
    running_ = running;
    LeaveCriticalSection(&lock_);
    SetEvent(updateEvent_);
}

bool StrobeWorker::Running() const {
    EnterCriticalSection(&lock_);
    bool running = running_;
    LeaveCriticalSection(&lock_);
    return running;
}

void StrobeWorker::Stop() {
    if (thread_) {
        SetEvent(stopEvent_);
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stopEvent_) CloseHandle(stopEvent_);
    if (updateEvent_) CloseHandle(updateEvent_);
    stopEvent_ = updateEvent_ = nullptr;
    if (lockReady_) DeleteCriticalSection(&lock_);
    lockReady_ = false;
}

DWORD WINAPI StrobeWorker::ThreadMain(LPVOID self) {
    static_cast<StrobeWorker*>(self)->Run();
    return 0;
}

void StrobeWorker::Snapshot(bool& running, Durations& durations) {
    EnterCriticalSection(&lock_);
    running = running_;
    durations = durations_;
    LeaveCriticalSection(&lock_);
}

bool StrobeWorker::WaitPhase(HANDLE timer, double ms) {
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(ms * 10000.0);
    SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0);

    HANDLE handles[3] = {stopEvent_, updateEvent_, timer};
    while (true) {
        DWORD r = WaitForMultipleObjects(3, handles, FALSE, INFINITE);
        if (r == WAIT_OBJECT_0 + 2) return true;
        if (r != WAIT_OBJECT_0 + 1) return false;
        // Dragging a slider must not restart the phase, so new durations wait for the next one.
        bool running;
        Durations ignored;
        Snapshot(running, ignored);
        if (!running) return true;
    }
}

void StrobeWorker::Run() {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    if (!timer) return;

    bool shown = false;
    const auto show = [&](bool want) {
        if (want == shown) return;
        flip_(want);
        DwmFlush();
        shown = want;
    };
    const auto runPhase = [&](bool strobing, double ms, const wchar_t* name) -> bool {
        show(strobing);
        LARGE_INTEGER start;
        QueryPerformanceCounter(&start);
        if (!WaitPhase(timer, ms)) return false;
        bool stillRunning;
        Durations ignored;
        Snapshot(stillRunning, ignored);
        if (!stillRunning) return true;  // Cut short, so its length means nothing.
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        double actual = (end.QuadPart - start.QuadPart) * qpcToMs_;
        bool stalled = (actual - ms) > kWatchdogMs;
        if (stalled) show(false);
        LogPhase(name, ms, actual, end.QuadPart, stalled);
        return true;
    };

    while (true) {
        bool running;
        Durations d;
        Snapshot(running, d);

        if (!running || !HasPhases(d)) {
            show(false);
            CancelWaitableTimer(timer);
            HANDLE idle[2] = {stopEvent_, updateEvent_};
            if (WaitForMultipleObjects(2, idle, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
            continue;
        }

        OpenLog();
        if (d.strobeMs > 0.0 && !runPhase(true, d.strobeMs, L"strobe")) break;
        Snapshot(running, d);
        if (running && d.visibleMs > 0.0 && !runPhase(false, d.visibleMs, L"visible")) break;
    }

    show(false);
    CancelWaitableTimer(timer);
    CloseHandle(timer);
    CloseLog();
}

void StrobeWorker::OpenLog() {
    if (log_) return;
    CreateDirectoryW(logDir_.c_str(), nullptr);
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf_s(name, L"drift_%04u%02u%02u_%02u%02u%02u.csv", st.wYear, st.wMonth, st.wDay, st.wHour,
               st.wMinute, st.wSecond);
    _wfopen_s(&log_, (logDir_ + name).c_str(), L"w, ccs=UTF-8");
    if (!log_) return;
    fwprintf(log_, L"build_id,%s\n", BUILD_ID_W);
    fwprintf(log_, L"seq,phase,expected_ms,actual_ms,drift_ms,qpc,notes\n");
}

void StrobeWorker::CloseLog() {
    if (log_) fclose(log_);
    log_ = nullptr;
}

void StrobeWorker::LogPhase(const wchar_t* phase, double expectedMs, double actualMs, long long qpc,
                            bool stalled) {
    if (!log_) return;
    ++seq_;
    fwprintf(log_, L"%lld,%s,%.3f,%.3f,%.3f,%lld,%s\n", seq_, phase, expectedMs, actualMs,
             actualMs - expectedMs, qpc, stalled ? L"STALL" : L"");
    if (seq_ % 64 == 0) fflush(log_);
}
