#pragma once

#include <unordered_map>
#include <vector>

#include "Luma/RHI/MaterialGpuDesc.h"
#include "Luma/RHI/MeshData.h"
#include "Vulkan/VulkanMemory.h"

namespace Luma {

// One uploaded mesh: device-local vertex (MeshVertex) and index (u32)
// buffers plus the sections each drawn with its own material.
struct GpuMesh {
    GpuBuffer vertices;
    GpuBuffer indices;
    u32 indexCount = 0;
    std::vector<MeshSection> sections;
    Math::Vec3 boundsMin{0.0f, 0.0f, 0.0f};  // local space
    Math::Vec3 boundsMax{0.0f, 0.0f, 0.0f};
};

// GPU residency for meshes. Uploading a key twice returns the first handle.
// Released meshes stay alive until CollectGarbage(), which the owner calls
// once no in-flight frame can still draw them.
class VulkanMeshCache {
public:
    VulkanMeshCache(VkPhysicalDevice physical, VkDevice device, VkQueue queue, u32 queueFamily);
    ~VulkanMeshCache();

    VulkanMeshCache(const VulkanMeshCache&) = delete;
    VulkanMeshCache& operator=(const VulkanMeshCache&) = delete;

    // Returns kInvalidHandle for an empty mesh or one with out-of-range
    // indices. A mesh without sections gets one covering every index.
    MeshHandle Upload(u64 key, const MeshData& mesh);
    void Release(MeshHandle handle);
    void CollectGarbage();

    const GpuMesh* Get(MeshHandle handle) const;

private:
    struct Entry {
        GpuMesh mesh;
        u64 key = 0;
        bool alive = false;
    };

    GpuBuffer UploadBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
    void Destroy(Entry& entry);

    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;

    std::vector<Entry> m_entries;  // indexed by handle
    std::vector<MeshHandle> m_free;
    std::vector<MeshHandle> m_pendingRelease;
    std::unordered_map<u64, MeshHandle> m_byKey;
};

}  // namespace Luma
