#include "Vulkan/EyeAdaptation/VulkanEyeAdaptation.h"

#include <algorithm>
#include <cstring>

#include "Luma/Core/Log.h"
#include "Vulkan/VulkanShader.h"

namespace Luma {
namespace {

struct HistogramPush {
    i32 size[2];
    f32 histScale;
    f32 histBias;
};

struct AdaptPush {
    f32 deltaTime;
    f32 speedUp;
    f32 speedDown;
    f32 minLuminance;
    f32 maxLuminance;
    f32 lowPercent;
    f32 highPercent;
    f32 histScale;
    f32 histBias;
};

constexpr u32 kHistogramGroup = 16;  // eye_histogram.comp local size

}  // namespace

VulkanEyeAdaptation::VulkanEyeAdaptation(VkPhysicalDevice physical, VkDevice device,
                                         const std::string& shaderDir,
                                         VkImageView sceneHDR, VkSampler sampler)
    : m_physical(physical), m_device(device) {
    const VkMemoryPropertyFlags hostMem =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    m_histogram = CreateBuffer(m_physical, m_device, kHistogramBytes,
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hostMem, true);
    m_state = CreateBuffer(m_physical, m_device, kStateBytes,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, hostMem, true);
    if (!m_histogram.buffer || !m_state.buffer) return;
    std::memset(m_histogram.mapped, 0, kHistogramBytes);
    // exposureScale = 1, not yet initialized (first adaptation snaps).
    const f32 initState[4] = {1.0f, 0.18f, 0.18f, 0.0f};
    std::memcpy(m_state.mapped, initState, kStateBytes);

    VkDescriptorSetLayoutBinding b[3]{};
    b[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 3;
    li.pBindings = b;
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_setLayout) != VK_SUCCESS) return;

    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0,
                             static_cast<u32>(std::max(sizeof(HistogramPush), sizeof(AdaptPush)))};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(m_device, &pl, nullptr, &m_layout) != VK_SUCCESS) return;

    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 1;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_pool) != VK_SUCCESS) return;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = m_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_setLayout;
    if (vkAllocateDescriptorSets(m_device, &ai, &m_set) != VK_SUCCESS) return;

    SetSource(sceneHDR, sampler);

    m_histogramPipeline = CreatePipeline(shaderDir + "/eye_histogram.comp.spv");
    m_adaptPipeline = CreatePipeline(shaderDir + "/eye_adapt.comp.spv");
    m_valid = m_histogramPipeline && m_adaptPipeline;
    if (!m_valid) LUMA_LOG_ERROR("VulkanEyeAdaptation", "Failed to create eye adaptation pipelines");
}

void VulkanEyeAdaptation::SetSource(VkImageView sceneHDR, VkSampler sampler) {
    VkDescriptorImageInfo scene{sampler, sceneHDR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo histInfo{m_histogram.buffer, 0, kHistogramBytes};
    VkDescriptorBufferInfo stateInfo{m_state.buffer, 0, kStateBytes};
    VkWriteDescriptorSet w[3]{};
    for (u32 i = 0; i < 3; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = m_set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &scene;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         w[1].pBufferInfo = &histInfo;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         w[2].pBufferInfo = &stateInfo;
    vkUpdateDescriptorSets(m_device, 3, w, 0, nullptr);
}

VulkanEyeAdaptation::~VulkanEyeAdaptation() {
    if (m_histogramPipeline) vkDestroyPipeline(m_device, m_histogramPipeline, nullptr);
    if (m_adaptPipeline) vkDestroyPipeline(m_device, m_adaptPipeline, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    DestroyBuffer(m_device, m_histogram);
    DestroyBuffer(m_device, m_state);
}

VkPipeline VulkanEyeAdaptation::CreatePipeline(const std::string& path) {
    VkShaderModule module = LoadShaderModule(m_device, path);
    if (!module) {
        LUMA_LOG_ERROR("VulkanEyeAdaptation", "Failed to load {}", path);
        return VK_NULL_HANDLE;
    }
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
    ci.layout = m_layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline) != VK_SUCCESS) {
        pipeline = VK_NULL_HANDLE;
    }
    vkDestroyShaderModule(m_device, module, nullptr);
    return pipeline;
}

void VulkanEyeAdaptation::Record(VkCommandBuffer cmd, u32 width, u32 height,
                                 const PostProcessParams& pp, f32 deltaTime) {
    if (!m_valid || width == 0 || height == 0) return;

    // Histogram position = log2(L) * scale + bias maps [LogMin, LogMax] to [0,1].
    const f32 histScale = 1.0f / (kHistogramLogMax - kHistogramLogMin);
    const f32 histBias = -kHistogramLogMin * histScale;

    // Scene color was written as a color attachment; make it visible to compute.
    VkMemoryBarrier toCompute{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    toCompute.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &toCompute,
                         0, nullptr, 0, nullptr);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &m_set, 0, nullptr);

    // 1. Histogram (each invocation meters one pixel per 2x2 block).
    HistogramPush hp{{static_cast<i32>(width), static_cast<i32>(height)}, histScale, histBias};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_histogramPipeline);
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(hp), &hp);
    const u32 gx = ((width + 1) / 2 + kHistogramGroup - 1) / kHistogramGroup;
    const u32 gy = ((height + 1) / 2 + kHistogramGroup - 1) / kHistogramGroup;
    vkCmdDispatch(cmd, gx, gy, 1);

    VkMemoryBarrier histDone{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    histDone.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    histDone.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &histDone,
                         0, nullptr, 0, nullptr);

    // 2. Adapt. One speed control on the component: brightening uses it as-is
    // and darkening a third of it -- Unreal's default up/down ratio (3 : 1).
    const f32 speed = std::max(pp.autoExposureSpeed, 0.0f);
    const f32 minLum = std::max(pp.autoExposureMinLuminance, 1e-4f);
    const f32 maxLum = std::max(pp.autoExposureMaxLuminance, minLum);
    AdaptPush ap{std::max(deltaTime, 0.0f), speed, speed / 3.0f, minLum, maxLum,
                 kLowPercent, kHighPercent, histScale, histBias};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_adaptPipeline);
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ap), &ap);
    vkCmdDispatch(cmd, 1, 1, 1);

    // Exposure -> the composite's fragment shader.
    VkMemoryBarrier toFragment{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    toFragment.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toFragment.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &toFragment,
                         0, nullptr, 0, nullptr);
}

}  // namespace Luma
