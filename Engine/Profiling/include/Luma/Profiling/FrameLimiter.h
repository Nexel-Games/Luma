#pragma once

#include <chrono>

#include "Luma/Core/Types.h"

// Frame-rate limiter: paces the main loop to a target frame rate without
// relying on VSync. Waits with a coarse OS sleep and finishes with a short spin
// so the frame time is accurate to a few microseconds (Windows timer
// resolution is ~1 ms, too coarse for 240+ FPS caps on its own).
//
//   FrameLimiter limiter;
//   while (running) {
//       limiter.Wait(settings.frameRateLimit);   // 0 = uncapped (returns at once)
//       ... frame ...
//   }

namespace Luma::Profiling {

class FrameLimiter {
public:
    using Clock = std::chrono::steady_clock;

    // Time to wait so a frame that started at `lastFrameStart` lasts 1/fps,
    // given the current time. Zero when uncapped (fps <= 0) or already late.
    static Clock::duration WaitTime(Clock::time_point lastFrameStart, Clock::time_point now, f64 fps);
    // The next frame start after waiting. A frame that ran late restarts the
    // schedule from `now` instead of trying to catch up with short frames.
    static Clock::time_point NextFrameStart(Clock::time_point lastFrameStart, Clock::time_point now, f64 fps);

    // Blocks until the frame budget for `fps` has elapsed since the previous
    // call returned (no-op for fps <= 0). Returns the time spent waiting.
    Clock::duration Wait(f64 fps);

private:
    Clock::time_point m_frameStart{};
    bool m_started = false;
};

}  // namespace Luma::Profiling
