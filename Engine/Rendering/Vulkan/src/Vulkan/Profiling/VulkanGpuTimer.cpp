#include "Vulkan/Profiling/VulkanGpuTimer.h"

#include "Luma/Core/Log.h"

namespace Luma {

VulkanGpuTimer::VulkanGpuTimer(VkPhysicalDevice physical, VkDevice device, u32 queueFamily, u32 slots,
                               std::string source)
    : m_device(device), m_source(std::move(source)) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    u32 familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
    const u32 validBits = queueFamily < familyCount ? families[queueFamily].timestampValidBits : 0;
    if (validBits == 0 || props.limits.timestampPeriod <= 0.0f) {
        LUMA_LOG_WARN("Profiling", "GPU timestamps unsupported on this queue; GPU timings off");
        return;
    }
    m_periodNs = props.limits.timestampPeriod;
    m_validMask = validBits >= 64 ? ~0ull : ((1ull << validBits) - 1ull);
    m_slots.resize(slots > 0 ? slots : 1);
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = kMaxScopes * 2 * static_cast<u32>(m_slots.size());
    if (vkCreateQueryPool(device, &info, nullptr, &m_pool) != VK_SUCCESS) {
        m_pool = VK_NULL_HANDLE;
        LUMA_LOG_WARN("Profiling", "could not create the GPU timestamp query pool");
    }
}

VulkanGpuTimer::~VulkanGpuTimer() {
    if (m_pool) vkDestroyQueryPool(m_device, m_pool, nullptr);
}

void VulkanGpuTimer::Collect(u32 slot) {
    Slot& s = m_slots[slot];
    if (!s.pending || s.scopes.empty()) return;
    s.pending = false;
    const u32 count = static_cast<u32>(s.scopes.size()) * 2;
    // (value, availability) pairs. The slot's fence was waited by the caller,
    // so this never blocks (no WAIT flag); a query that was never written
    // reports unavailable and its scope is skipped.
    std::vector<u64> stamps(static_cast<usize>(count) * 2);
    const VkResult r = vkGetQueryPoolResults(m_device, m_pool, slot * kMaxScopes * 2, count,
                                             stamps.size() * sizeof(u64), stamps.data(), 2 * sizeof(u64),
                                             VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (r != VK_SUCCESS && r != VK_NOT_READY) return;
    m_latest.clear();
    for (usize i = 0; i < s.scopes.size(); ++i) {
        const usize b = i * 4;  // begin value, begin avail, end value, end avail
        if (!s.scopes[i].closed || stamps[b + 1] == 0 || stamps[b + 3] == 0) continue;
        const u64 begin = stamps[b] & m_validMask;
        const u64 end = stamps[b + 2] & m_validMask;
        const f64 ms = end >= begin ? static_cast<f64>(end - begin) * m_periodNs * 1e-6 : 0.0;
        m_latest.push_back({s.scopes[i].name, ms, s.scopes[i].depth});
    }
    Profiling::Global().SubmitGpuTimings(m_source, m_latest);
}

void VulkanGpuTimer::BeginFrame(VkCommandBuffer cmd, u32 slot) {
    if (!Valid()) return;
    m_current = slot % static_cast<u32>(m_slots.size());
    Collect(m_current);
    Slot& s = m_slots[m_current];
    s.scopes.clear();
    vkCmdResetQueryPool(cmd, m_pool, m_current * kMaxScopes * 2, kMaxScopes * 2);
    m_recording = true;
    m_depth = 0;
    m_root = Begin(cmd, m_source);
}

u32 VulkanGpuTimer::Begin(VkCommandBuffer cmd, std::string_view name) {
    if (!Valid() || !m_recording) return ~0u;
    Slot& s = m_slots[m_current];
    if (s.scopes.size() >= kMaxScopes) return ~0u;
    const u32 index = static_cast<u32>(s.scopes.size());
    s.scopes.push_back({std::string(name), m_depth, false});
    ++m_depth;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_pool, m_current * kMaxScopes * 2 + index * 2);
    return index;
}

void VulkanGpuTimer::End(VkCommandBuffer cmd, u32 scope) {
    if (!Valid() || !m_recording || scope == ~0u) return;
    Slot& s = m_slots[m_current];
    if (scope >= s.scopes.size() || s.scopes[scope].closed) return;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_pool,
                        m_current * kMaxScopes * 2 + scope * 2 + 1);
    s.scopes[scope].closed = true;
    if (m_depth > 0) --m_depth;
}

void VulkanGpuTimer::EndFrame(VkCommandBuffer cmd) {
    if (!Valid() || !m_recording) return;
    End(cmd, m_root);
    // Scopes left open have no end stamp; Collect skips them.
    m_slots[m_current].pending = true;
    m_recording = false;
}

}  // namespace Luma
