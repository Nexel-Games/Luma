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
    u32 vertexCount = 0;
    std::vector<MeshSection> sections;
    // LOD1.. (LOD0 is `indices` / `sections`): own index buffers over the
    // same vertices, the same section count, decreasing screen sizes.
    struct Lod {
        GpuBuffer indices;
        std::vector<MeshSection> sections;
        f32 screenSize = 0.0f;
    };
    std::vector<Lod> lods;
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
    // Replaces `handle`'s LOD chain (empty = none). False (nothing changed)
    // when an index is out of range or the section count differs. Old LOD
    // buffers are freed once no frame in flight can still use them.
    bool SetLods(MeshHandle handle, const std::vector<MeshLodData>& lods);
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
    // Replaced LOD buffers: freed after kRetireFrames garbage collections.
    static constexpr u32 kRetireFrames = 3;
    struct RetiredBuffer {
        GpuBuffer buffer;
        u32 framesLeft = kRetireFrames;
    };
    std::vector<RetiredBuffer> m_retired;
    std::unordered_map<u64, MeshHandle> m_byKey;
};

}  // namespace Luma
