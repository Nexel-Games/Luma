#include "Vulkan/SkyLight/VulkanSkyLight.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Luma/Core/Log.h"
#include "Vulkan/VulkanShader.h"

namespace Luma {
namespace {

constexpr u32 kGroupSize = 8;  // local_size_x/y of every sky light shader

u32 MipSize(u32 mip) { return std::max(VulkanSkyLight::kCubeSize >> mip, 1u); }

// Unreal's ComputeReflectionCaptureRoughnessFromMip (REFLECTION_CAPTURE_
// ROUGHEST_MIP = 1, ROUGHNESS_MIP_SCALE = 1.2): a given mip always means the
// same roughness. Must stay the inverse of the lighting shader's mapping.
f32 RoughnessFromMip(u32 mip, u32 mipCount) {
    const f32 levelFrom1x1 = static_cast<f32>(mipCount) - 1.0f - static_cast<f32>(mip);
    return std::clamp(std::exp2((1.0f - levelFrom1x1) / 1.2f), 0.0f, 1.0f);
}

}  // namespace

VulkanSkyLight::VulkanSkyLight(VkPhysicalDevice physical, VkDevice device,
                               const std::string& shaderDir)
    : m_physical(physical), m_device(device) {
    if (!CreateCube(m_source) || !CreateCube(m_filtered)) {
        LUMA_LOG_ERROR("VulkanSkyLight", "Failed to create sky light cubemaps");
        return;
    }

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = static_cast<f32>(kMipCount);
    if (vkCreateSampler(m_device, &si, nullptr, &m_sampler) != VK_SUCCESS) return;

    m_shBuffer = CreateBuffer(m_physical, m_device, kSHBytes,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              true);
    m_ubo = CreateBuffer(m_physical, m_device, sizeof(CaptureUBO),
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         true);
    if (!m_shBuffer.buffer || !m_ubo.buffer) return;
    // Until the first capture completes, ambient is black rather than garbage.
    std::memset(m_shBuffer.mapped, 0, kSHBytes);

    if (!CreateDescriptors()) return;
    m_capturePipeline = CreatePipeline(shaderDir + "/skylight_capture.comp.spv");
    m_downsamplePipeline = CreatePipeline(shaderDir + "/skylight_downsample.comp.spv");
    m_filterPipeline = CreatePipeline(shaderDir + "/skylight_filter.comp.spv");
    m_shPipeline = CreatePipeline(shaderDir + "/skylight_sh.comp.spv");
    m_valid = m_capturePipeline && m_downsamplePipeline && m_filterPipeline && m_shPipeline;
    if (!m_valid) LUMA_LOG_ERROR("VulkanSkyLight", "Failed to create sky light pipelines");
}

VulkanSkyLight::~VulkanSkyLight() {
    for (VkPipeline p : {m_capturePipeline, m_downsamplePipeline, m_filterPipeline, m_shPipeline}) {
        if (p) vkDestroyPipeline(m_device, p, nullptr);
    }
    if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
    DestroyBuffer(m_device, m_shBuffer);
    DestroyBuffer(m_device, m_ubo);
    DestroyCube(m_source);
    DestroyCube(m_filtered);
}

bool VulkanSkyLight::CreateCube(Cube& cube) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = kFormat;
    ii.extent = {kCubeSize, kCubeSize, 1};
    ii.mipLevels = kMipCount;
    ii.arrayLayers = 6;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &ii, nullptr, &cube.image) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_device, cube.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = FindMemoryType(m_physical, req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &ai, nullptr, &cube.memory) != VK_SUCCESS) return false;
    vkBindImageMemory(m_device, cube.image, cube.memory, 0);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = cube.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    vi.format = kFormat;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, kMipCount, 0, 6};
    if (vkCreateImageView(m_device, &vi, nullptr, &cube.cubeView) != VK_SUCCESS) return false;
    for (u32 m = 0; m < kMipCount; ++m) {
        VkImageViewCreateInfo mv = vi;
        mv.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        mv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, m, 1, 0, 6};
        if (vkCreateImageView(m_device, &mv, nullptr, &cube.mipViews[m]) != VK_SUCCESS) return false;
    }
    return true;
}

void VulkanSkyLight::DestroyCube(Cube& cube) {
    for (VkImageView& v : cube.mipViews) {
        if (v) vkDestroyImageView(m_device, v, nullptr);
        v = VK_NULL_HANDLE;
    }
    if (cube.cubeView) vkDestroyImageView(m_device, cube.cubeView, nullptr);
    if (cube.image) vkDestroyImage(m_device, cube.image, nullptr);
    if (cube.memory) vkFreeMemory(m_device, cube.memory, nullptr);
    cube = Cube{};
}

bool VulkanSkyLight::CreateDescriptors() {
    // One layout for all passes: 0 = capture UBO, 1 = source cube (sampled),
    // 2 = output mip (storage), 3 = SH buffer. Every set writes all four so
    // each binding is always valid, whichever subset a shader uses.
    VkDescriptorSetLayoutBinding b[4]{};
    const VkDescriptorType types[4] = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
    for (u32 i = 0; i < 4; ++i) {
        b[i].binding = i;
        b[i].descriptorType = types[i];
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 4;
    li.pBindings = b;
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_setLayout) != VK_SUCCESS) return false;

    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(m_device, &pl, nullptr, &m_layout) != VK_SUCCESS) return false;

    VkDescriptorPoolSize sizes[4];
    for (u32 i = 0; i < 4; ++i) sizes[i] = {types[i], kSetCount};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = kSetCount;
    pi.poolSizeCount = 4;
    pi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_pool) != VK_SUCCESS) return false;

    VkDescriptorSetLayout layouts[kSetCount];
    for (VkDescriptorSetLayout& l : layouts) l = m_setLayout;
    VkDescriptorSet sets[kSetCount]{};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = m_pool;
    ai.descriptorSetCount = kSetCount;
    ai.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(m_device, &ai, sets) != VK_SUCCESS) return false;

    u32 s = 0;
    m_captureSet = sets[s++];
    WriteSet(m_captureSet, m_source.mipViews[0]);
    for (u32 m = 1; m < kMipCount; ++m) {
        m_downSets[m] = sets[s++];
        WriteSet(m_downSets[m], m_source.mipViews[m]);
    }
    for (u32 m = 0; m < kMipCount; ++m) {
        m_filterSets[m] = sets[s++];
        WriteSet(m_filterSets[m], m_filtered.mipViews[m]);
    }
    m_shSet = sets[s++];
    WriteSet(m_shSet, m_filtered.mipViews[0]);  // storage binding unused by SH
    return true;
}

void VulkanSkyLight::WriteSet(VkDescriptorSet set, VkImageView storageView) {
    VkDescriptorBufferInfo ubo{m_ubo.buffer, 0, sizeof(CaptureUBO)};
    VkDescriptorImageInfo src{m_sampler, m_source.cubeView, kCubeLayout};
    VkDescriptorImageInfo dst{VK_NULL_HANDLE, storageView, kCubeLayout};
    VkDescriptorBufferInfo sh{m_shBuffer.buffer, 0, kSHBytes};
    VkWriteDescriptorSet w[4]{};
    for (u32 i = 0; i < 4; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;         w[0].pBufferInfo = &ubo;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[1].pImageInfo = &src;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          w[2].pImageInfo = &dst;
    w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         w[3].pBufferInfo = &sh;
    vkUpdateDescriptorSets(m_device, 4, w, 0, nullptr);
}

VkPipeline VulkanSkyLight::CreatePipeline(const std::string& path) {
    VkShaderModule module = LoadShaderModule(m_device, path);
    if (!module) {
        LUMA_LOG_ERROR("VulkanSkyLight", "Failed to load {}", path);
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

void VulkanSkyLight::Dispatch(VkCommandBuffer cmd, VkPipeline pipeline,
                              VkDescriptorSet set, const Push& push) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    const u32 groups = (static_cast<u32>(push.size) + kGroupSize - 1) / kGroupSize;
    vkCmdDispatch(cmd, groups, groups, 6);  // z = cube face
}

void VulkanSkyLight::Record(VkCommandBuffer cmd, const Rendering::AtmosphereParams& atmo,
                            const Math::Vec3& cameraPosition) {
    if (!m_valid) return;

    // The sky only depends on the atmosphere inputs and the viewer's altitude
    // (not x/z), so quantize altitude to avoid re-capturing while orbiting.
    const f32 altitude = std::round(std::max(cameraPosition.y, 0.0f) / 50.0f) * 50.0f;
    const bool changed = !m_hasCapture ||
                         std::memcmp(&atmo, &m_lastAtmo, sizeof(atmo)) != 0 ||
                         altitude != m_lastAltitude;
    if (!changed) return;

    CaptureUBO ubo{};
    ubo.atmo = atmo;
    ubo.camPos[0] = 0.0f;
    ubo.camPos[1] = altitude;
    ubo.camPos[2] = 0.0f;
    ubo.camPos[3] = 1.0f;
    vkCmdUpdateBuffer(cmd, m_ubo.buffer, 0, sizeof(ubo), &ubo);
    VkMemoryBarrier uploaded{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    uploaded.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    uploaded.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &uploaded, 0, nullptr, 0, nullptr);

    auto computeBarrier = [&]() {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    };

    // Earlier frames' lighting reads must finish before we overwrite; on first
    // use also move both cubes (all mips/faces) into GENERAL.
    {
        VkImageMemoryBarrier ib[2]{};
        for (u32 i = 0; i < 2; ++i) {
            ib[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ib[i].oldLayout = m_layoutsInitialized ? kCubeLayout : VK_IMAGE_LAYOUT_UNDEFINED;
            ib[i].newLayout = kCubeLayout;
            ib[i].srcQueueFamilyIndex = ib[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            ib[i].image = (i == 0) ? m_source.image : m_filtered.image;
            ib[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, kMipCount, 0, 6};
            ib[i].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            ib[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, ib);
        m_layoutsInitialized = true;
    }

    const i32 mips = static_cast<i32>(kMipCount);
    // 1. Capture the sky into source mip 0.
    Dispatch(cmd, m_capturePipeline, m_captureSet, {0, mips, 0.0f, static_cast<i32>(kCubeSize)});
    computeBarrier();
    // 2. Source mip chain.
    for (u32 m = 1; m < kMipCount; ++m) {
        Dispatch(cmd, m_downsamplePipeline, m_downSets[m],
                 {static_cast<i32>(m), mips, 0.0f, static_cast<i32>(MipSize(m))});
        computeBarrier();
    }
    // 3. GGX-prefiltered specular cube (reads all source mips) and
    // 4. diffuse SH (reads a source mip) -- independent, no barrier between.
    for (u32 m = 0; m < kMipCount; ++m) {
        Dispatch(cmd, m_filterPipeline, m_filterSets[m],
                 {static_cast<i32>(m), mips, RoughnessFromMip(m, kMipCount),
                  static_cast<i32>(MipSize(m))});
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_shPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &m_shSet, 0, nullptr);
    const Push shPush{0, mips, 0.0f, 0};
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(shPush), &shPush);
    vkCmdDispatch(cmd, 1, 1, 1);

    // Results -> fragment shaders (lighting pass).
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);

    m_lastAtmo = atmo;
    m_lastAltitude = altitude;
    m_hasCapture = true;
}

}  // namespace Luma
