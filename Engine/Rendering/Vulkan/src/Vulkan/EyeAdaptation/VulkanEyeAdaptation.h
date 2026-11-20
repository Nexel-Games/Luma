#pragma once

#include <string>

#include "Luma/RHI/Renderer.h"
#include "Vulkan/VulkanCommon.h"
#include "Vulkan/VulkanMemory.h"

namespace Luma {

// Auto exposure / eye adaptation (Unreal's histogram model, see
// PostProcessEyeAdaptation + PostProcessHistogramCommon):
//
//   1. histogram - 64-bucket log2-luminance histogram of the HDR scene
//   2. adapt     - outlier-trimmed average -> 18%-grey target exposure ->
//                  log-space adaptation over time; persists on the GPU
//
// The tone-mapping composite multiplies scene color by the resulting
// exposure scale, read from ExposureBuffer(). Its own module: owns its
// pipelines, buffers and descriptors.
class VulkanEyeAdaptation {
public:
    // Fraction of metered pixels discarded at each end of the histogram
    // (Unreal's AutoExposureLowPercent/HighPercent defaults: 10% / 90%).
    static constexpr f32 kLowPercent = 0.10f;
    static constexpr f32 kHighPercent = 0.90f;
    // Metered log2-luminance range (Unreal's extended range defaults).
    static constexpr f32 kHistogramLogMin = -10.0f;
    static constexpr f32 kHistogramLogMax = 20.0f;

    // `sceneHDR` is the HDR scene color (sampled in SHADER_READ_ONLY_OPTIMAL).
    VulkanEyeAdaptation(VkPhysicalDevice physical, VkDevice device,
                        const std::string& shaderDir, VkImageView sceneHDR,
                        VkSampler sampler);
    ~VulkanEyeAdaptation();

    VulkanEyeAdaptation(const VulkanEyeAdaptation&) = delete;
    VulkanEyeAdaptation& operator=(const VulkanEyeAdaptation&) = delete;

    bool IsValid() const { return m_valid; }

    // Meters the scene (which must be fully written by color-attachment
    // writes) and updates the exposure. `deltaTime` <= 0 snaps to the target.
    void Record(VkCommandBuffer cmd, u32 width, u32 height,
                const PostProcessParams& pp, f32 deltaTime);

    // std430 { float exposureScale; float adapted; float target; float init; }
    VkBuffer ExposureBuffer() const { return m_state.buffer; }
    VkDeviceSize ExposureBufferSize() const { return kStateBytes; }

private:
    static constexpr u32 kBuckets = 64;
    static constexpr VkDeviceSize kHistogramBytes = sizeof(u32) * kBuckets;
    static constexpr VkDeviceSize kStateBytes = sizeof(f32) * 4;

    VkPipeline CreatePipeline(const std::string& path);

    VkPhysicalDevice m_physical;
    VkDevice m_device;
    bool m_valid = false;

    GpuBuffer m_histogram;
    GpuBuffer m_state;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    VkPipeline m_histogramPipeline = VK_NULL_HANDLE;
    VkPipeline m_adaptPipeline = VK_NULL_HANDLE;
};

}  // namespace Luma
