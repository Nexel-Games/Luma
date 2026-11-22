#pragma once

#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Types.h"

// Frame profiler: CPU scopes, GPU pass timings and per-frame render counters,
// averaged over a short window for display (stats overlay, benchmark reports).
//
//   Profiling::Profiler& p = Profiling::Global();
//   p.BeginFrame();
//   { LUMA_PROFILE_SCOPE("Scene.Build"); ... }
//   p.Counters().drawCalls += n;            // renderers add their counts
//   p.SubmitGpuTimings("Viewport", passes); // backends report resolved queries
//   p.EndFrame();
//   const Profiling::FrameStats& s = p.Average();
//
// CPU scopes are recorded on the thread that owns the profiler (the main
// thread); nesting is allowed and kept as a depth for indented display. GPU
// timings arrive a few frames late (queries are read back without stalling);
// the average simply uses the latest results that arrived. Main-thread only.

namespace Luma::Profiling {

using Clock = std::chrono::steady_clock;

// One named timing (CPU scope or GPU pass), in milliseconds.
struct Timing {
    std::string name;
    f64 ms = 0.0;
    u32 depth = 0;  // nesting level (0 = top level)
};

// Render work counted during a frame.
struct FrameCounters {
    u64 drawCalls = 0;
    u64 triangles = 0;
    u64 instances = 0;        // scene instances submitted
    u64 culledInstances = 0;  // instances rejected by frustum culling (main view)
    u64 shadowDraws = 0;      // draw calls of the shadow passes (part of drawCalls)
    u64 pipelineBinds = 0;
    u64 descriptorBinds = 0;

    FrameCounters& operator+=(const FrameCounters& o);
};

// One frame (or an average of frames).
struct FrameStats {
    f64 frameMs = 0.0;  // wall time between frame starts
    f64 cpuMs = 0.0;    // BeginFrame -> EndFrame
    f64 gpuMs = 0.0;    // sum of top-level GPU passes
    f64 fps = 0.0;      // 1000 / frameMs
    FrameCounters counters;
    std::vector<Timing> cpu;  // in first-seen order
    std::vector<Timing> gpu;
    u32 frames = 0;  // frames averaged
};

class Profiler {
public:
    // Average window (seconds). The average is rebuilt when a window closes.
    void SetAverageWindow(f64 seconds) { m_window = seconds > 0.0 ? seconds : 0.5; }

    void BeginFrame();
    void EndFrame();
    bool InFrame() const { return m_inFrame; }

    // Scope bookkeeping (use LUMA_PROFILE_SCOPE / CpuScope).
    void PushScope(std::string_view name);
    void PopScope();

    FrameCounters& Counters() { return m_current.counters; }
    // Latest resolved GPU passes of one source (a renderer / command stream),
    // replacing that source's previous set. Sources that stop submitting are
    // dropped after a few frames (e.g. an occasional thumbnail render).
    void SubmitGpuTimings(std::string_view source, std::vector<Timing> passes);

    const FrameStats& LastFrame() const { return m_last; }
    // Average over the last completed window (empty until one completes).
    const FrameStats& Average() const { return m_average; }
    u64 FrameIndex() const { return m_frameIndex; }

    // Accumulates every frame from now on into a running total (benchmarks);
    // TotalAverage() is the mean since StartCapture.
    void StartCapture();
    void StopCapture() { m_capturing = false; }
    bool Capturing() const { return m_capturing; }
    FrameStats TotalAverage() const;

    // Clears everything (tests).
    void Reset();

private:
    struct OpenScope {
        usize index;
        Clock::time_point start;
    };
    static void Accumulate(FrameStats& into, const FrameStats& frame);
    static FrameStats Divide(const FrameStats& sum);

    FrameStats m_current;
    FrameStats m_last;
    FrameStats m_average;
    FrameStats m_windowSum;
    FrameStats m_captureSum;
    struct GpuSource {
        std::string name;
        std::vector<Timing> passes;
        u64 frame = 0;  // frame index of the last submission
    };
    std::vector<OpenScope> m_stack;
    std::vector<GpuSource> m_gpuSources;
    Clock::time_point m_frameStart{};
    Clock::time_point m_prevFrameStart{};
    bool m_hasPrevFrame = false;
    f64 m_windowElapsed = 0.0;
    f64 m_window = 0.5;
    bool m_inFrame = false;
    bool m_capturing = false;
    u64 m_frameIndex = 0;
};

// The process-wide profiler.
Profiler& Global();

// RAII CPU scope on the global profiler (a no-op outside BeginFrame/EndFrame).
class CpuScope {
public:
    explicit CpuScope(std::string_view name) : m_active(Global().InFrame()) {
        if (m_active) Global().PushScope(name);
    }
    ~CpuScope() {
        if (m_active) Global().PopScope();
    }
    CpuScope(const CpuScope&) = delete;
    CpuScope& operator=(const CpuScope&) = delete;

private:
    bool m_active;
};

// Formats the stats as a multi-line text report (benchmark log / clipboard).
std::string FormatReport(const FrameStats& stats);
// Writes the stats as a JSON object to `path`; false on I/O failure.
bool WriteReportJson(const FrameStats& stats, const std::string& path, std::string_view label);

}  // namespace Luma::Profiling

#define LUMA_PROFILE_CONCAT_INNER(a, b) a##b
#define LUMA_PROFILE_CONCAT(a, b) LUMA_PROFILE_CONCAT_INNER(a, b)
#define LUMA_PROFILE_SCOPE(name) \
    ::Luma::Profiling::CpuScope LUMA_PROFILE_CONCAT(lumaProfileScope_, __LINE__)(name)
