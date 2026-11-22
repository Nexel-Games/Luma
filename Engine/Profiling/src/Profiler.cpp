#include "Luma/Profiling/Profiler.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace Luma::Profiling {

namespace {

f64 Ms(Clock::duration d) { return std::chrono::duration<f64, std::milli>(d).count(); }

// Adds `from` into `into`, merging by name (first-seen order is kept).
void MergeTimings(std::vector<Timing>& into, const std::vector<Timing>& from) {
    for (const Timing& t : from) {
        auto it = std::find_if(into.begin(), into.end(), [&](const Timing& e) { return e.name == t.name; });
        if (it == into.end()) {
            into.push_back(t);
        } else {
            it->ms += t.ms;
        }
    }
}

std::string JsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

}  // namespace

FrameCounters& FrameCounters::operator+=(const FrameCounters& o) {
    drawCalls += o.drawCalls;
    triangles += o.triangles;
    instances += o.instances;
    culledInstances += o.culledInstances;
    shadowDraws += o.shadowDraws;
    pipelineBinds += o.pipelineBinds;
    descriptorBinds += o.descriptorBinds;
    return *this;
}

void Profiler::BeginFrame() {
    if (m_inFrame) EndFrame();  // unbalanced caller: close the open frame
    m_frameStart = Clock::now();
    m_current = FrameStats{};
    m_stack.clear();
    m_inFrame = true;
}

void Profiler::PushScope(std::string_view name) {
    if (!m_inFrame) return;
    const u32 depth = static_cast<u32>(m_stack.size());
    // Repeated scopes with the same name and depth accumulate into one entry.
    usize index = m_current.cpu.size();
    for (usize i = 0; i < m_current.cpu.size(); ++i) {
        if (m_current.cpu[i].depth == depth && m_current.cpu[i].name == name) {
            index = i;
            break;
        }
    }
    if (index == m_current.cpu.size()) m_current.cpu.push_back({std::string(name), 0.0, depth});
    m_stack.push_back({index, Clock::now()});
}

void Profiler::PopScope() {
    if (!m_inFrame || m_stack.empty()) return;
    const OpenScope open = m_stack.back();
    m_stack.pop_back();
    m_current.cpu[open.index].ms += Ms(Clock::now() - open.start);
}

void Profiler::SubmitGpuTimings(std::string_view source, std::vector<Timing> passes) {
    for (GpuSource& s : m_gpuSources) {
        if (s.name == source) {
            s.passes = std::move(passes);
            s.frame = m_frameIndex;
            return;
        }
    }
    m_gpuSources.push_back({std::string(source), std::move(passes), m_frameIndex});
}

void Profiler::EndFrame() {
    if (!m_inFrame) return;
    const Clock::time_point now = Clock::now();
    while (!m_stack.empty()) PopScope();
    m_current.cpuMs = Ms(now - m_frameStart);
    m_current.frameMs = m_hasPrevFrame ? Ms(m_frameStart - m_prevFrameStart) : m_current.cpuMs;
    m_current.fps = m_current.frameMs > 0.0 ? 1000.0 / m_current.frameMs : 0.0;
    // GPU results arrive a frame or two late; a source silent for longer
    // than that has stopped rendering.
    constexpr u64 kStaleFrames = 8;
    std::erase_if(m_gpuSources, [&](const GpuSource& s) { return m_frameIndex - s.frame > kStaleFrames; });
    m_current.gpu.clear();
    m_current.gpuMs = 0.0;
    for (const GpuSource& s : m_gpuSources) {
        for (const Timing& t : s.passes) {
            if (t.depth == 0) m_current.gpuMs += t.ms;
            m_current.gpu.push_back(t);
        }
    }
    m_current.frames = 1;
    m_prevFrameStart = m_frameStart;
    // The first frame has no measured interval: keep it out of the averages.
    const bool measured = m_hasPrevFrame;
    m_hasPrevFrame = true;
    m_inFrame = false;
    ++m_frameIndex;
    m_last = m_current;
    if (!measured) return;

    Accumulate(m_windowSum, m_current);
    m_windowElapsed += m_current.frameMs / 1000.0;
    if (m_windowElapsed >= m_window) {
        m_average = Divide(m_windowSum);
        m_windowSum = FrameStats{};
        m_windowElapsed = 0.0;
    }
    if (m_capturing) Accumulate(m_captureSum, m_current);
}

void Profiler::Accumulate(FrameStats& into, const FrameStats& frame) {
    into.frameMs += frame.frameMs;
    into.cpuMs += frame.cpuMs;
    into.gpuMs += frame.gpuMs;
    into.counters += frame.counters;
    MergeTimings(into.cpu, frame.cpu);
    MergeTimings(into.gpu, frame.gpu);
    into.frames += frame.frames;
}

FrameStats Profiler::Divide(const FrameStats& sum) {
    FrameStats avg = sum;
    if (sum.frames == 0) return avg;
    const f64 n = static_cast<f64>(sum.frames);
    avg.frameMs = sum.frameMs / n;
    avg.cpuMs = sum.cpuMs / n;
    avg.gpuMs = sum.gpuMs / n;
    avg.fps = avg.frameMs > 0.0 ? 1000.0 / avg.frameMs : 0.0;
    auto div = [&](u64 v) { return static_cast<u64>(static_cast<f64>(v) / n + 0.5); };
    avg.counters.drawCalls = div(sum.counters.drawCalls);
    avg.counters.triangles = div(sum.counters.triangles);
    avg.counters.instances = div(sum.counters.instances);
    avg.counters.culledInstances = div(sum.counters.culledInstances);
    avg.counters.shadowDraws = div(sum.counters.shadowDraws);
    avg.counters.pipelineBinds = div(sum.counters.pipelineBinds);
    avg.counters.descriptorBinds = div(sum.counters.descriptorBinds);
    for (Timing& t : avg.cpu) t.ms /= n;
    for (Timing& t : avg.gpu) t.ms /= n;
    return avg;
}

void Profiler::StartCapture() {
    m_captureSum = FrameStats{};
    m_capturing = true;
}

FrameStats Profiler::TotalAverage() const { return Divide(m_captureSum); }

void Profiler::Reset() { *this = Profiler{}; }

Profiler& Global() {
    static Profiler profiler;
    return profiler;
}

std::string FormatReport(const FrameStats& s) {
    std::string out;
    char line[256];
    std::snprintf(line, sizeof(line), "%.1f FPS | frame %.3f ms | CPU %.3f ms | GPU %.3f ms | %u frames\n",
                  s.fps, s.frameMs, s.cpuMs, s.gpuMs, s.frames);
    out += line;
    std::snprintf(line, sizeof(line),
                  "draws %llu (shadow %llu) | triangles %llu | instances %llu (culled %llu) | "
                  "pipeline binds %llu | descriptor binds %llu\n",
                  static_cast<unsigned long long>(s.counters.drawCalls),
                  static_cast<unsigned long long>(s.counters.shadowDraws),
                  static_cast<unsigned long long>(s.counters.triangles),
                  static_cast<unsigned long long>(s.counters.instances),
                  static_cast<unsigned long long>(s.counters.culledInstances),
                  static_cast<unsigned long long>(s.counters.pipelineBinds),
                  static_cast<unsigned long long>(s.counters.descriptorBinds));
    out += line;
    out += "CPU:\n";
    for (const Timing& t : s.cpu) {
        std::snprintf(line, sizeof(line), "  %*s%-32s %8.3f ms\n", static_cast<int>(t.depth * 2), "",
                      t.name.c_str(), t.ms);
        out += line;
    }
    out += "GPU:\n";
    for (const Timing& t : s.gpu) {
        std::snprintf(line, sizeof(line), "  %*s%-32s %8.3f ms\n", static_cast<int>(t.depth * 2), "",
                      t.name.c_str(), t.ms);
        out += line;
    }
    return out;
}

bool WriteReportJson(const FrameStats& s, const std::string& path, std::string_view label) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    char buf[512];
    out << "{\n  \"label\": \"" << JsonEscape(label) << "\",\n";
    std::snprintf(buf, sizeof(buf),
                  "  \"frames\": %u,\n  \"fps\": %.3f,\n  \"frameMs\": %.4f,\n  \"cpuMs\": %.4f,\n"
                  "  \"gpuMs\": %.4f,\n",
                  s.frames, s.fps, s.frameMs, s.cpuMs, s.gpuMs);
    out << buf;
    std::snprintf(buf, sizeof(buf),
                  "  \"drawCalls\": %llu,\n  \"shadowDraws\": %llu,\n  \"triangles\": %llu,\n"
                  "  \"instances\": %llu,\n  \"culledInstances\": %llu,\n  \"pipelineBinds\": %llu,\n"
                  "  \"descriptorBinds\": %llu,\n",
                  static_cast<unsigned long long>(s.counters.drawCalls),
                  static_cast<unsigned long long>(s.counters.shadowDraws),
                  static_cast<unsigned long long>(s.counters.triangles),
                  static_cast<unsigned long long>(s.counters.instances),
                  static_cast<unsigned long long>(s.counters.culledInstances),
                  static_cast<unsigned long long>(s.counters.pipelineBinds),
                  static_cast<unsigned long long>(s.counters.descriptorBinds));
    out << buf;
    auto list = [&](const char* key, const std::vector<Timing>& timings, bool last) {
        out << "  \"" << key << "\": [";
        for (usize i = 0; i < timings.size(); ++i) {
            std::snprintf(buf, sizeof(buf), "%s\n    {\"name\": \"%s\", \"ms\": %.4f, \"depth\": %u}",
                          i ? "," : "", JsonEscape(timings[i].name).c_str(), timings[i].ms, timings[i].depth);
            out << buf;
        }
        out << (timings.empty() ? "]" : "\n  ]") << (last ? "\n" : ",\n");
    };
    list("cpu", s.cpu, false);
    list("gpu", s.gpu, true);
    out << "}\n";
    return static_cast<bool>(out);
}

}  // namespace Luma::Profiling
