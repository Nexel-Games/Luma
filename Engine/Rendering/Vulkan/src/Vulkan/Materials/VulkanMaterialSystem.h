#pragma once

#include <array>
#include <unordered_map>
#include <vector>

#include "Luma/RHI/MaterialGpuDesc.h"
#include "Vulkan/VulkanMemory.h"

namespace Luma {

class VulkanTextureCache;

// One material input as the shaders read it (std430; mirrored exactly by
// `MaterialInput` in material_common.glsl).
struct GpuMaterialInputData {
    f32 constant[4];
    f32 tint[4];
    f32 tilingOffset[4];  // xy tiling, zw offset
    u32 texIndex;         // bindless sampler2D index (white when no texture)
    u32 channel;          // GpuTextureChannel
    f32 scalar;           // normal strength (Normal) / intensity (Emissive)
    u32 flags;            // GpuMaterial::kHasTexture | kFlipGreen
};
static_assert(sizeof(GpuMaterialInputData) == 64);

// One material table entry (std430; mirrored by `Material` in
// material_common.glsl).
struct GpuMaterial {
    static constexpr u32 kHasTexture = 1u << 0;
    static constexpr u32 kFlipGreen = 1u << 1;

    u32 blendMode;     // GpuBlendMode
    u32 shadingModel;  // GpuShadingModel
    u32 twoSided;
    f32 opacityMaskClip;
    GpuMaterialInputData inputs[kMaterialGpuInputCount];
};
static_assert(sizeof(GpuMaterial) == 16 + 64 * kMaterialGpuInputCount);
static_assert(sizeof(GpuMaterial) % 16 == 0);

// The material table: every material the scene uses, one storage-buffer entry
// each, indexed per draw. Handle == table index; handle 0 is the Default
// Material. Updates land in a CPU copy; Flush() publishes it into the buffer
// of one frame in flight, so a frame the GPU is still reading never sees a
// half-written entry.
class VulkanMaterialSystem {
public:
    static constexpr u32 kFramesInFlight = 2;

    VulkanMaterialSystem(VkPhysicalDevice physical, VkDevice device, u32 maxMaterials = 4096);
    ~VulkanMaterialSystem();

    VulkanMaterialSystem(const VulkanMaterialSystem&) = delete;
    VulkanMaterialSystem& operator=(const VulkanMaterialSystem&) = delete;

    bool IsValid() const { return m_sets[0] != VK_NULL_HANDLE; }

    // Creates or rewrites the entry for `key` (kDefaultMaterialKey always
    // targets handle 0). Returns the Default Material when the table is full.
    MaterialHandle Update(u64 key, const MaterialGpuDesc& desc, const VulkanTextureCache& textures);
    void Release(MaterialHandle handle);

    // Table index for a handle (unknown handles draw the Default Material).
    u32 Index(MaterialHandle handle) const;
    const GpuMaterial& Entry(MaterialHandle handle) const;

    // Re-resolves texture indices (a released texture falls back to white)
    // and copies the table into frame `frameSlot`'s buffer.
    void Flush(u32 frameSlot, const VulkanTextureCache& textures);

    VkDescriptorSetLayout Layout() const { return m_layout; }
    VkDescriptorSet Set(u32 frameSlot) const { return m_sets[frameSlot % kFramesInFlight]; }

private:
    GpuMaterial Build(const MaterialGpuDesc& desc, const VulkanTextureCache* textures) const;

    VkDevice m_device = VK_NULL_HANDLE;
    u32 m_capacity = 0;
    std::array<GpuBuffer, kFramesInFlight> m_buffers{};
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> m_sets{};

    std::vector<MaterialGpuDesc> m_descs;  // indexed by handle
    std::vector<GpuMaterial> m_table;      // indexed by handle
    std::vector<bool> m_alive;
    std::vector<u64> m_keys;
    std::vector<MaterialHandle> m_free;
    std::unordered_map<u64, MaterialHandle> m_byKey;
};

}  // namespace Luma
