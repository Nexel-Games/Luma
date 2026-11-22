#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Luma/Math/Math.h"
#include "Vulkan/VulkanCommon.h"

namespace Luma {

// Unreal-style selection outline: an orange ring around the selected actor
// drawn over the final image, fainter where the actor is behind other
// geometry. Its own module:
//   1. RecordMask draws the selected actor's sections without depth testing
//      into an R8G8 mask (r = covered, g = visible against the scene depth).
//   2. RecordOutline composites the ring from the mask into an open rendering
//      scope on the display image (after FXAA, before the gizmo overlay).
// Created per viewport size (the renderer rebuilds it on resize).
class VulkanSelectionOutlinePass {
public:
    static constexpr VkFormat kMaskFormat = VK_FORMAT_R8G8_UNORM;

    // Theme accent (255, 140, 0), 2 px, 35 % through occluders.
    struct Style {
        Math::Vec4 color{1.0f, 140.0f / 255.0f, 0.0f, 1.0f};
        f32 widthPx = 2.0f;
        f32 occludedAlpha = 0.35f;
    };

    // `targetFormat` is the display image the outline is blended onto.
    VulkanSelectionOutlinePass(VkPhysicalDevice physical, VkDevice device,
                               const std::string& shaderDir, u32 width, u32 height,
                               VkFormat targetFormat);
    ~VulkanSelectionOutlinePass();

    VulkanSelectionOutlinePass(const VulkanSelectionOutlinePass&) = delete;
    VulkanSelectionOutlinePass& operator=(const VulkanSelectionOutlinePass&) = delete;

    bool Valid() const { return m_outlinePipeline != VK_NULL_HANDLE; }

    // Own rendering scope; leaves the mask ready for sampling.
    // Clears the mask and runs `draw` inside its rendering scope (one R8G8
    // colour target, no depth): the caller draws the selected sections with
    // their materials' SelectionMask pipelines.
    void RecordMask(VkCommandBuffer cmd, const std::function<void(VkCommandBuffer)>& draw);
    // Inside an open rendering scope whose color target is the display image.
    void RecordOutline(VkCommandBuffer cmd, const Style& style = {});

private:
    bool CreateMask(VkPhysicalDevice physical);
    bool CreateDescriptors();
    bool CreatePipelines(const std::string& shaderDir, VkFormat targetFormat);

    VkDevice m_device = VK_NULL_HANDLE;
    u32 m_width = 0;
    u32 m_height = 0;

    VkImage m_mask = VK_NULL_HANDLE;
    VkDeviceMemory m_maskMemory = VK_NULL_HANDLE;
    VkImageView m_maskView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;  // nearest, clamp (texelFetch)

    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_outlineSet = VK_NULL_HANDLE;  // selection mask

    VkPipelineLayout m_outlineLayout = VK_NULL_HANDLE;
    VkPipeline m_outlinePipeline = VK_NULL_HANDLE;

};

}  // namespace Luma
