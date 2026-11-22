#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

#include "Luma/Core/Types.h"
#include "Luma/Profiling/Profiler.h"

namespace Luma {

// GPU pass timing with timestamp queries, one query range per frame slot.
//
//   timer.BeginFrame(cmd, slot);      // after the slot's fence was waited:
//                                     // reads the slot's previous results
//                                     // (no stall) and resets its queries
//   u32 s = timer.Begin(cmd, "GBuffer");
//   ... record ...
//   timer.End(cmd, s);
//   timer.EndFrame(cmd);              // closes the frame's root scope
//
// Results reach Profiling::Global() under `source` with a depth-0 root named
// after the source and the passes nested below it. Scopes may nest. Silently
// inactive on queues without timestamp support.
class VulkanGpuTimer {
public:
    static constexpr u32 kMaxScopes = 64;

    VulkanGpuTimer(VkPhysicalDevice physical, VkDevice device, u32 queueFamily, u32 slots, std::string source);
    ~VulkanGpuTimer();
    VulkanGpuTimer(const VulkanGpuTimer&) = delete;
    VulkanGpuTimer& operator=(const VulkanGpuTimer&) = delete;

    bool Valid() const { return m_pool != VK_NULL_HANDLE; }

    // Must be recorded outside a render pass.
    void BeginFrame(VkCommandBuffer cmd, u32 slot);
    u32 Begin(VkCommandBuffer cmd, std::string_view name);
    void End(VkCommandBuffer cmd, u32 scope);
    void EndFrame(VkCommandBuffer cmd);

    // Last resolved frame (also published to the profiler).
    const std::vector<Profiling::Timing>& Latest() const { return m_latest; }

private:
    struct Scope {
        std::string name;
        u32 depth = 0;
        bool closed = false;
    };
    struct Slot {
        std::vector<Scope> scopes;  // scope i uses queries 2i (begin), 2i+1 (end)
        bool pending = false;       // recorded, results not read yet
    };
    void Collect(u32 slot);

    VkDevice m_device = VK_NULL_HANDLE;
    VkQueryPool m_pool = VK_NULL_HANDLE;
    f64 m_periodNs = 1.0;
    u64 m_validMask = ~0ull;
    std::string m_source;
    std::vector<Slot> m_slots;
    u32 m_current = 0;
    u32 m_root = ~0u;
    u32 m_depth = 0;
    bool m_recording = false;
    std::vector<Profiling::Timing> m_latest;
};

// RAII scope (no-op when `timer` is null or invalid).
class GpuScope {
public:
    GpuScope(VulkanGpuTimer* timer, VkCommandBuffer cmd, std::string_view name)
        : m_timer(timer && timer->Valid() ? timer : nullptr), m_cmd(cmd) {
        if (m_timer) m_scope = m_timer->Begin(cmd, name);
    }
    ~GpuScope() {
        if (m_timer) m_timer->End(m_cmd, m_scope);
    }
    GpuScope(const GpuScope&) = delete;
    GpuScope& operator=(const GpuScope&) = delete;

private:
    VulkanGpuTimer* m_timer;
    VkCommandBuffer m_cmd;
    u32 m_scope = ~0u;
};

}  // namespace Luma
