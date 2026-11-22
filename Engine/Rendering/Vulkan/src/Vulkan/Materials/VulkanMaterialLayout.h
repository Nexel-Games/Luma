#pragma once

#include <array>
#include <span>
#include <vector>

#include "Luma/Math/Math.h"
#include "Vulkan/VulkanMemory.h"

// The one pipeline layout every material pipeline shares (material_frame.glsl
// / material_params.glsl), and the per-frame data behind its sets:
//
//   set 0  per view   0 frame UBO (dynamic offset per view), 1 per-object
//                       buffer, 2 scene textures [7], 3 lighting UBO,
//                       4 shadow map, 5 environment map, 6 sky light cube,
//                       7 sky light SH
//   set 1  textures   VulkanTextureCache (sampled images, sampler table, cubes)
//   set 2  params     one storage buffer of vec4s; each draw pushes its offset
//   push   128 bytes  model, normal matrix, param offset, object id, flags, clip
//
// Every per-frame resource exists once per frame slot. A slot is rewritten
// only after the GPU finished the frame that last used it (the renderer waits
// on its fence first), so growing a buffer never invalidates offsets a frame
// in flight still reads.

namespace Luma {

class VulkanMaterialLayout {
public:
    static constexpr u32 kFramesInFlight = 2;
    static constexpr u32 kViewsPerFrame = 8;  // main view, shadow cascades, previews
    static constexpr u32 kSceneTextureCount = 7;

    // std140, mirrors LumaFrameUBO.
    struct FrameData {
        Math::Mat4 view;
        Math::Mat4 proj;
        Math::Mat4 viewProj;
        Math::Mat4 invViewProj;
        Math::Mat4 prevViewProj;
        f32 camPos[4];
        f32 time[4];      // seconds, delta seconds, frame index
        f32 viewport[4];  // width, height, 1/width, 1/height
    };
    static_assert(sizeof(FrameData) == 5 * 64 + 48);

    // std430, mirrors LumaObject.
    struct ObjectData {
        Math::Mat4 worldToLocal;
        f32 boundsCenterRadius[4];
        f32 boundsExtent[4];
    };
    static_assert(sizeof(ObjectData) == 96);

    // Mirrors LumaPush.
    struct Push {
        Math::Mat4 model;
        f32 normalMatrix[3][4];
        u32 paramOffset;
        u32 objectId;
        u32 flags;
        f32 opacityMaskClip;
    };
    static_assert(sizeof(Push) == 128, "the whole push-constant budget");

    struct Lighting {
        VkBuffer ubo = VK_NULL_HANDLE;
        VkDeviceSize uboSize = 0;
        VkImageView shadowMap = VK_NULL_HANDLE;
        VkSampler shadowSampler = VK_NULL_HANDLE;
        VkImageView environment = VK_NULL_HANDLE;
        VkSampler environmentSampler = VK_NULL_HANDLE;
        VkImageView skyCube = VK_NULL_HANDLE;
        VkSampler skySampler = VK_NULL_HANDLE;
        VkBuffer skySH = VK_NULL_HANDLE;
        VkDeviceSize skySHSize = 0;
    };

    VulkanMaterialLayout(VkPhysicalDevice physical, VkDevice device, VkDescriptorSetLayout textureSetLayout);
    ~VulkanMaterialLayout();
    VulkanMaterialLayout(const VulkanMaterialLayout&) = delete;
    VulkanMaterialLayout& operator=(const VulkanMaterialLayout&) = delete;

    bool IsValid() const { return m_pipelineLayout != VK_NULL_HANDLE; }
    VkPipelineLayout PipelineLayout() const { return m_pipelineLayout; }
    VkDescriptorSetLayout FrameSetLayout() const { return m_frameLayout; }
    VkDescriptorSetLayout ParamSetLayout() const { return m_paramLayout; }
    u32 PushConstantBytes() const { return sizeof(Push); }

    // Starts recording frame slot `slot`: clears its views, objects and blocks.
    void BeginFrame(u32 slot);
    // Adds a view; returns the dynamic offset to bind set 0 with.
    u32 AddView(const FrameData& view);
    // Adds a draw's object data; returns its object id.
    u32 AddObject(const ObjectData& object);
    // Appends a parameter block; returns its first vec4 (the draw's paramOffset).
    u32 AddParams(std::span<const Math::Vec4> block);
    // Copies the slot's objects and blocks to the GPU (growing buffers as
    // needed) and updates the slot's descriptor sets. Call after the last
    // Add* and before binding the sets.
    bool EndFrame();

    void SetSceneTextures(u32 slot, const std::array<VkImageView, kSceneTextureCount>& views, VkSampler sampler);
    void SetLighting(u32 slot, const Lighting& lighting);

    VkDescriptorSet FrameSet(u32 slot) const { return m_frames[slot % kFramesInFlight].frameSet; }
    VkDescriptorSet ParamSet(u32 slot) const { return m_frames[slot % kFramesInFlight].paramSet; }
    u32 CurrentSlot() const { return m_slot; }
    // The current slot's packed parameter blocks (what EndFrame uploads).
    std::span<const Math::Vec4> PendingParams() const { return m_frames[m_slot].paramData; }
    // Sizes in bytes of the slot's GPU buffers (tests: growth).
    VkDeviceSize ParamBufferSize(u32 slot) const { return m_frames[slot % kFramesInFlight].params.size; }
    VkDeviceSize ObjectBufferSize(u32 slot) const { return m_frames[slot % kFramesInFlight].objects.size; }

private:
    struct Frame {
        GpuBuffer views;    // kViewsPerFrame aligned FrameData
        GpuBuffer objects;  // ObjectData[]
        GpuBuffer params;   // vec4[]
        VkDescriptorSet frameSet = VK_NULL_HANDLE;
        VkDescriptorSet paramSet = VK_NULL_HANDLE;
        u32 viewCount = 0;
        std::vector<ObjectData> objectData;
        std::vector<Math::Vec4> paramData;
    };

    bool CreateLayouts(VkDescriptorSetLayout textureSetLayout);
    bool CreateFrames();
    bool Grow(GpuBuffer& buffer, VkDeviceSize needed, VkBufferUsageFlags usage);
    void WriteBuffers(Frame& frame);

    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_frameLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_paramLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDeviceSize m_viewStride = 0;
    std::array<Frame, kFramesInFlight> m_frames{};
    u32 m_slot = 0;
};

}  // namespace Luma
