#pragma once

#include <unordered_map>
#include <vector>

#include "Luma/RHI/MaterialGpuDesc.h"
#include "Vulkan/VulkanCommon.h"

namespace Luma {

// GPU residency for material textures, exposed to shaders as one bindless
// `sampler2D[]` (update-after-bind, partially bound).
//
// Every texture owns a slot s with two consecutive array elements: 2*s is
// its linear (UNORM) view and 2*s+1 its sRGB view, so a material picks the
// color space per input without duplicating the image. A handle is the slot
// plus a generation (bits 16..31): once a texture is released and its slot
// reused, the old handle stops resolving. Handle 0 is a reserved 1x1 white
// texture; invalid or stale handles resolve to it. Images get a full mip chain (vkCmdBlitImage)
// and are sampled through one 16x anisotropic repeat sampler.
//
// Released textures stay alive until CollectGarbage(), which the owner calls
// once the GPU has finished every frame that may still sample them; their
// slots are reused afterwards.
class VulkanTextureCache {
public:
    VulkanTextureCache(VkPhysicalDevice physical, VkDevice device, VkQueue queue,
                       u32 queueFamily, u32 maxTextures = 4096);
    ~VulkanTextureCache();

    VulkanTextureCache(const VulkanTextureCache&) = delete;
    VulkanTextureCache& operator=(const VulkanTextureCache&) = delete;

    bool IsValid() const { return m_set != VK_NULL_HANDLE; }

    // Uploads (or returns the existing texture for `key`). Returns
    // kInvalidHandle for an empty/invalid description or when full.
    GpuTextureHandle Upload(u64 key, const TextureUploadDesc& desc);
    void Release(GpuTextureHandle handle);
    void CollectGarbage();

    // Shader array index of `handle` sampled through `view`.
    u32 BindlessIndex(GpuTextureHandle handle, GpuTextureView view) const;
    u32 MipCount(GpuTextureHandle handle) const;

    VkDescriptorSetLayout Layout() const { return m_layout; }
    VkDescriptorSet Set() const { return m_set; }

    static u32 MipCountFor(u32 width, u32 height);

private:
    struct Entry {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView linearView = VK_NULL_HANDLE;
        VkImageView srgbView = VK_NULL_HANDLE;
        u32 mipCount = 0;
        u64 key = 0;
        u16 generation = 0;
        bool alive = false;
    };

    static constexpr u32 kMaxSlots = 0xFFFF;
    // Slot of a live texture, or kInvalidHandle for invalid/stale handles.
    u32 SlotOf(GpuTextureHandle handle) const;
    GpuTextureHandle MakeHandle(u32 slot) const;

    // CPU-side pixels converted to what the GPU image stores.
    struct Prepared {
        std::vector<u8> pixels;
        VkFormat storageFormat = VK_FORMAT_UNDEFINED;  // image format
        VkFormat linearFormat = VK_FORMAT_UNDEFINED;   // linear view format
        VkFormat srgbFormat = VK_FORMAT_UNDEFINED;     // sRGB view format
        VkComponentMapping swizzle{};
        bool mutableFormat = false;
    };

    bool CreateDescriptors(u32 maxTextures);
    bool CreateSampler();
    bool CanBlitLinear(VkFormat format) const;
    Prepared Prepare(const TextureUploadDesc& desc) const;
    bool CreateImage(const TextureUploadDesc& desc, const Prepared& prepared, Entry& out);
    void WriteDescriptors(u32 slot, VkImageView linearView, VkImageView srgbView);
    void DestroyEntry(Entry& entry);
    u32 AcquireSlot();

    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    u32 m_slotCapacity = 0;  // textures (two descriptors each)

    std::vector<Entry> m_entries;  // indexed by slot
    std::vector<u32> m_freeSlots;
    std::vector<u32> m_pendingRelease;
    std::unordered_map<u64, GpuTextureHandle> m_byKey;
};

}  // namespace Luma
