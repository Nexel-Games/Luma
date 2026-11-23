#include "Vulkan/Meshes/VulkanMeshCache.h"

#include <algorithm>
#include <cstring>

namespace Luma {

VulkanMeshCache::VulkanMeshCache(VkPhysicalDevice physical, VkDevice device, VkQueue queue,
                                 u32 queueFamily)
    : m_physical(physical), m_device(device), m_queue(queue) {
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    info.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(m_device, &info, nullptr, &m_pool) != VK_SUCCESS) {
        LUMA_LOG_ERROR("MeshCache", "Failed to create the upload command pool");
        m_pool = VK_NULL_HANDLE;
    }
}

VulkanMeshCache::~VulkanMeshCache() {
    if (m_device == VK_NULL_HANDLE) return;
    vkQueueWaitIdle(m_queue);
    for (Entry& e : m_entries) Destroy(e);
    if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
}

GpuBuffer VulkanMeshCache::UploadBuffer(const void* data, VkDeviceSize size,
                                        VkBufferUsageFlags usage) {
    GpuBuffer staging = CreateBuffer(
        m_physical, m_device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    GpuBuffer target = CreateBuffer(m_physical, m_device, size,
                                    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
    if (!staging.mapped || target.buffer == VK_NULL_HANDLE) {
        DestroyBuffer(m_device, staging);
        DestroyBuffer(m_device, target);
        return {};
    }
    std::memcpy(staging.mapped, data, static_cast<usize>(size));
    VkCommandBuffer cmd = BeginOneTimeCommands(m_device, m_pool);
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging.buffer, target.buffer, 1, &region);
    EndOneTimeCommands(m_device, m_pool, m_queue, cmd);
    DestroyBuffer(m_device, staging);
    return target;
}

MeshHandle VulkanMeshCache::Upload(u64 key, const MeshData& mesh) {
    if (auto it = m_byKey.find(key); it != m_byKey.end()) return it->second;
    if (m_pool == VK_NULL_HANDLE) return kInvalidHandle;
    if (mesh.vertices.empty() || mesh.indices.size() < 3) {
        LUMA_LOG_ERROR("MeshCache", "Rejected an empty mesh upload");
        return kInvalidHandle;
    }
    const u32 vertexCount = static_cast<u32>(mesh.vertices.size());
    for (u32 index : mesh.indices) {
        if (index >= vertexCount) {
            LUMA_LOG_ERROR("MeshCache", "Rejected a mesh with index {} >= {} vertices", index,
                           vertexCount);
            return kInvalidHandle;
        }
    }
    const u32 indexCount = static_cast<u32>(mesh.indices.size());
    std::vector<MeshSection> sections;
    for (const MeshSection& s : mesh.sections) {
        if (s.indexCount == 0 || s.indexOffset >= indexCount) continue;
        MeshSection clamped = s;
        clamped.indexCount = std::min(s.indexCount, indexCount - s.indexOffset);
        sections.push_back(std::move(clamped));
    }
    if (sections.empty()) sections.push_back({0, indexCount, {}});

    Entry entry;
    entry.mesh.vertices = UploadBuffer(mesh.vertices.data(),
                                       sizeof(MeshVertex) * mesh.vertices.size(),
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    entry.mesh.indices = UploadBuffer(mesh.indices.data(), sizeof(u32) * mesh.indices.size(),
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!entry.mesh.vertices.buffer || !entry.mesh.indices.buffer) {
        LUMA_LOG_ERROR("MeshCache", "Failed to allocate GPU buffers for a mesh");
        Destroy(entry);
        return kInvalidHandle;
    }
    entry.mesh.indexCount = indexCount;
    entry.mesh.vertexCount = vertexCount;
    entry.mesh.boundsMin = entry.mesh.boundsMax = mesh.vertices.front().position;
    for (const MeshVertex& v : mesh.vertices) {
        entry.mesh.boundsMin = {std::min(entry.mesh.boundsMin.x, v.position.x), std::min(entry.mesh.boundsMin.y, v.position.y),
                                std::min(entry.mesh.boundsMin.z, v.position.z)};
        entry.mesh.boundsMax = {std::max(entry.mesh.boundsMax.x, v.position.x), std::max(entry.mesh.boundsMax.y, v.position.y),
                                std::max(entry.mesh.boundsMax.z, v.position.z)};
    }
    entry.mesh.sections = std::move(sections);
    entry.key = key;
    entry.alive = true;

    MeshHandle handle;
    if (!m_free.empty()) {
        handle = m_free.back();
        m_free.pop_back();
        m_entries[handle] = std::move(entry);
    } else {
        handle = static_cast<MeshHandle>(m_entries.size());
        m_entries.push_back(std::move(entry));
    }
    m_byKey[key] = handle;
    return handle;
}

void VulkanMeshCache::Release(MeshHandle handle) {
    if (handle >= m_entries.size() || !m_entries[handle].alive) return;
    m_entries[handle].alive = false;
    m_byKey.erase(m_entries[handle].key);
    m_pendingRelease.push_back(handle);
}

void VulkanMeshCache::CollectGarbage() {
    for (MeshHandle h : m_pendingRelease) {
        Destroy(m_entries[h]);
        m_free.push_back(h);
    }
    m_pendingRelease.clear();
    std::erase_if(m_retired, [this](RetiredBuffer& r) {
        if (--r.framesLeft > 0) return false;
        DestroyBuffer(m_device, r.buffer);
        return true;
    });
}

bool VulkanMeshCache::SetLods(MeshHandle handle, const std::vector<MeshLodData>& lods) {
    if (handle >= m_entries.size() || !m_entries[handle].alive) return false;
    GpuMesh& mesh = m_entries[handle].mesh;
    for (const MeshLodData& lod : lods) {
        if (lod.sections.size() != mesh.sections.size()) {
            LUMA_LOG_ERROR("MeshCache", "Rejected a LOD with {} sections (mesh has {})", lod.sections.size(),
                           mesh.sections.size());
            return false;
        }
        for (u32 index : lod.indices) {
            if (index >= mesh.vertexCount) {
                LUMA_LOG_ERROR("MeshCache", "Rejected a LOD with index {} >= {} vertices", index, mesh.vertexCount);
                return false;
            }
        }
        for (const MeshSection& s : lod.sections) {
            if (usize(s.indexOffset) + s.indexCount > lod.indices.size()) {
                LUMA_LOG_ERROR("MeshCache", "Rejected a LOD section past its index list");
                return false;
            }
        }
    }
    std::vector<GpuMesh::Lod> uploaded;
    for (const MeshLodData& lod : lods) {
        GpuMesh::Lod gpu;
        gpu.sections = lod.sections;
        gpu.screenSize = lod.screenSize;
        if (!lod.indices.empty()) {
            gpu.indices = UploadBuffer(lod.indices.data(), sizeof(u32) * lod.indices.size(),
                                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            if (!gpu.indices.buffer) {
                LUMA_LOG_ERROR("MeshCache", "Failed to allocate a LOD index buffer");
                for (GpuMesh::Lod& done : uploaded) DestroyBuffer(m_device, done.indices);
                return false;
            }
        }
        uploaded.push_back(std::move(gpu));
    }
    for (GpuMesh::Lod& old : mesh.lods) {
        if (old.indices.buffer) m_retired.push_back({old.indices});
    }
    mesh.lods = std::move(uploaded);
    return true;
}

const GpuMesh* VulkanMeshCache::Get(MeshHandle handle) const {
    if (handle >= m_entries.size() || !m_entries[handle].alive) return nullptr;
    return &m_entries[handle].mesh;
}

void VulkanMeshCache::Destroy(Entry& entry) {
    DestroyBuffer(m_device, entry.mesh.vertices);
    DestroyBuffer(m_device, entry.mesh.indices);
    for (GpuMesh::Lod& lod : entry.mesh.lods) DestroyBuffer(m_device, lod.indices);
    entry = Entry{};
}

}  // namespace Luma
