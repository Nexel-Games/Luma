#include "Luma/Profiling/FrameLimiter.h"

#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Luma::Profiling {

namespace {

FrameLimiter::Clock::duration Budget(f64 fps) {
    return std::chrono::duration_cast<FrameLimiter::Clock::duration>(std::chrono::duration<f64>(1.0 / fps));
}

#ifdef _WIN32
// A high-resolution waitable timer (Windows 10 1803+) sleeps with ~0.5 ms
// accuracy instead of the default 15.6 ms scheduler tick.
class PreciseTimer {
public:
    PreciseTimer() {
        m_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    ~PreciseTimer() {
        if (m_timer) CloseHandle(m_timer);
    }
    bool Sleep(std::chrono::nanoseconds d) {
        if (!m_timer) return false;
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(d.count() / 100);  // relative, 100 ns units
        if (due.QuadPart >= 0) return true;
        if (!SetWaitableTimerEx(m_timer, &due, 0, nullptr, nullptr, nullptr, 0)) return false;
        WaitForSingleObject(m_timer, INFINITE);
        return true;
    }

private:
    HANDLE m_timer = nullptr;
};
#endif

void SleepFor(std::chrono::nanoseconds d) {
#ifdef _WIN32
    static thread_local PreciseTimer timer;
    if (timer.Sleep(d)) return;
#endif
    std::this_thread::sleep_for(d);
}

}  // namespace

FrameLimiter::Clock::duration FrameLimiter::WaitTime(Clock::time_point lastFrameStart, Clock::time_point now,
                                                     f64 fps) {
    if (fps <= 0.0) return Clock::duration::zero();
    const Clock::time_point due = lastFrameStart + Budget(fps);
    return due > now ? due - now : Clock::duration::zero();
}

FrameLimiter::Clock::time_point FrameLimiter::NextFrameStart(Clock::time_point lastFrameStart,
                                                             Clock::time_point now, f64 fps) {
    if (fps <= 0.0) return now;
    const Clock::time_point due = lastFrameStart + Budget(fps);
    // On time: keep the exact cadence (no drift). Late by more than a frame:
    // restart from now rather than bursting to catch up.
    if (now <= due) return due;
    if (now - due < Budget(fps)) return due;
    return now;
}

FrameLimiter::Clock::duration FrameLimiter::Wait(f64 fps) {
    const Clock::time_point now = Clock::now();
    if (fps <= 0.0 || !m_started) {
        m_frameStart = now;
        m_started = true;
        return Clock::duration::zero();
    }
    const Clock::duration wait = WaitTime(m_frameStart, now, fps);
    const Clock::time_point target = NextFrameStart(m_frameStart, now, fps);
    if (wait > Clock::duration::zero()) {
        // Sleep most of it, then spin the last ~1 ms for accuracy.
        constexpr auto kSpin = std::chrono::microseconds(1000);
        if (wait > kSpin) SleepFor(std::chrono::duration_cast<std::chrono::nanoseconds>(wait - kSpin));
        while (Clock::now() < target) std::this_thread::yield();
    }
    m_frameStart = target > now ? target : now;
    return wait;
}

}  // namespace Luma::Profiling
