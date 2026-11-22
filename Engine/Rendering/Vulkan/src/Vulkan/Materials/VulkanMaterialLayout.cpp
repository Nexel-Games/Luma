#include "Vulkan/Materials/VulkanMaterialLayout.h"

#include <algorithm>
#include <cstring>

#include "Luma/Core/Log.h"

namespace Luma {
namespace {

constexpr VkDeviceSize kInitialObjects = 256;
constexpr VkDeviceSize kInitialParams = 1024;  // vec4s
constexpr VkBufferUsageFlags kStorage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
constexpr VkMemoryPropertyFlags kHostVisible =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

}  // namespace

VulkanMaterialLayout::VulkanMaterialLayout(VkPhysicalDevice physical, VkDevice device,
                                           VkDescriptorSetLayout textureSetLayout)
    : m_physical(physical), m_device(device) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    const VkDeviceSize align = std::max<VkDeviceSize>(props.limits.minUniformBufferOffsetAlignment, 16);
    m_viewStride = (sizeof(FrameData) + align - 1) / align * align;
    if (!CreateLayouts(textureSetLayout) || !CreateFrames()) {
        LUMA_LOG_ERROR("MaterialLayout", "Failed to create the shared material pipeline layout");
        if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
}

VulkanMaterialLayout::~VulkanMaterialLayout() {
    for (Frame& f : m_frames) {
        DestroyBuffer(m_device, f.views);
        DestroyBuffer(m_device, f.objects);
        DestroyBuffer(m_device, f.params);
    }
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_frameLayout) vkDestroyDescriptorSetLayout(m_device, m_frameLayout, nullptr);
    if (m_paramLayout) vkDestroyDescriptorSetLayout(m_device, m_paramLayout, nullptr);
}

bool VulkanMaterialLayout::CreateLayouts(VkDescriptorSetLayout textureSetLayout) {
    const VkShaderStageFlags all = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDescriptorType types[8] = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
    VkDescriptorSetLayoutBinding frame[8]{};
    for (u32 i = 0; i < 8; ++i) {
        frame[i].binding = i;
        frame[i].descriptorType = types[i];
        frame[i].descriptorCount = i == 2 ? kSceneTextureCount : 1;
        frame[i].stageFlags = all;
    }
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = 8;
    info.pBindings = frame;
    if (vkCreateDescriptorSetLayout(m_device, &info, nullptr, &m_frameLayout) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding params{};
    params.binding = 0;
    params.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    params.descriptorCount = 1;
    params.stageFlags = all;
    info.bindingCount = 1;
    info.pBindings = &params;
    if (vkCreateDescriptorSetLayout(m_device, &info, nullptr, &m_paramLayout) != VK_SUCCESS) return false;

    const VkDescriptorSetLayout sets[3] = {m_frameLayout, textureSetLayout, m_paramLayout};
    VkPushConstantRange push{all, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 3;
    layout.pSetLayouts = sets;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    return vkCreatePipelineLayout(m_device, &layout, nullptr, &m_pipelineLayout) == VK_SUCCESS;
}

bool VulkanMaterialLayout::CreateFrames() {
    const VkDescriptorPoolSize sizes[4] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight * 3},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight * (kSceneTextureCount + 3)}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = kFramesInFlight * 2;
    pool.poolSizeCount = 4;
    pool.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(m_device, &pool, nullptr, &m_pool) != VK_SUCCESS) return false;

    for (Frame& f : m_frames) {
        const VkDescriptorSetLayout layouts[2] = {m_frameLayout, m_paramLayout};
        VkDescriptorSet sets[2];
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = m_pool;
        alloc.descriptorSetCount = 2;
        alloc.pSetLayouts = layouts;
        if (vkAllocateDescriptorSets(m_device, &alloc, sets) != VK_SUCCESS) return false;
        f.frameSet = sets[0];
        f.paramSet = sets[1];
        f.views = CreateBuffer(m_physical, m_device, m_viewStride * kViewsPerFrame, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               kHostVisible, true);
        if (!f.views.buffer || !Grow(f.objects, kInitialObjects * sizeof(ObjectData), kStorage) ||
            !Grow(f.params, kInitialParams * sizeof(Math::Vec4), kStorage)) {
            return false;
        }
        WriteBuffers(f);
    }
    return true;
}

bool VulkanMaterialLayout::Grow(GpuBuffer& buffer, VkDeviceSize needed, VkBufferUsageFlags usage) {
    if (buffer.buffer && buffer.size >= needed) return true;
    VkDeviceSize size = buffer.size ? buffer.size : needed;
    while (size < needed) size *= 2;
    GpuBuffer grown = CreateBuffer(m_physical, m_device, size, usage, kHostVisible, true);
    if (!grown.buffer) return false;
    // Only this slot's buffer is replaced; the GPU finished with it (see the
    // class comment), so it can go now.
    DestroyBuffer(m_device, buffer);
    buffer = grown;
    return true;
}

void VulkanMaterialLayout::WriteBuffers(Frame& f) {
    const VkDescriptorBufferInfo view{f.views.buffer, 0, sizeof(FrameData)};
    const VkDescriptorBufferInfo objects{f.objects.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo params{f.params.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet writes[3]{};
    for (VkWriteDescriptorSet& w : writes) {
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.descriptorCount = 1;
    }
    writes[0].dstSet = f.frameSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[0].pBufferInfo = &view;
    writes[1].dstSet = f.frameSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &objects;
    writes[2].dstSet = f.paramSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &params;
    vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
}

void VulkanMaterialLayout::BeginFrame(u32 slot) {
    m_slot = slot % kFramesInFlight;
    Frame& f = m_frames[m_slot];
    f.viewCount = 0;
    f.objectData.clear();
    f.paramData.clear();
}

u32 VulkanMaterialLayout::AddView(const FrameData& view) {
    Frame& f = m_frames[m_slot];
    if (f.viewCount >= kViewsPerFrame) {
        LUMA_LOG_ERROR("MaterialLayout", "More than %u views in one frame", kViewsPerFrame);
        return 0;
    }
    const VkDeviceSize offset = m_viewStride * f.viewCount++;
    std::memcpy(static_cast<u8*>(f.views.mapped) + offset, &view, sizeof(FrameData));
    return static_cast<u32>(offset);
}

u32 VulkanMaterialLayout::AddObject(const ObjectData& object) {
    Frame& f = m_frames[m_slot];
    f.objectData.push_back(object);
    return static_cast<u32>(f.objectData.size() - 1);
}

u32 VulkanMaterialLayout::AddParams(std::span<const Math::Vec4> block) {
    Frame& f = m_frames[m_slot];
    const u32 offset = static_cast<u32>(f.paramData.size());
    f.paramData.insert(f.paramData.end(), block.begin(), block.end());
    return offset;
}

bool VulkanMaterialLayout::EndFrame() {
    Frame& f = m_frames[m_slot];
    const VkBuffer objectsBefore = f.objects.buffer, paramsBefore = f.params.buffer;
    const VkDeviceSize objectBytes = std::max<VkDeviceSize>(f.objectData.size(), 1) * sizeof(ObjectData);
    const VkDeviceSize paramBytes = std::max<VkDeviceSize>(f.paramData.size(), 1) * sizeof(Math::Vec4);
    if (!Grow(f.objects, objectBytes, kStorage) || !Grow(f.params, paramBytes, kStorage)) {
        LUMA_LOG_ERROR("MaterialLayout", "Out of memory for per-frame material data");
        return false;
    }
    if (!f.objectData.empty()) std::memcpy(f.objects.mapped, f.objectData.data(), f.objectData.size() * sizeof(ObjectData));
    if (!f.paramData.empty()) std::memcpy(f.params.mapped, f.paramData.data(), f.paramData.size() * sizeof(Math::Vec4));
    if (f.objects.buffer != objectsBefore || f.params.buffer != paramsBefore) WriteBuffers(f);
    return true;
}

void VulkanMaterialLayout::SetSceneTextures(u32 slot, const std::array<VkImageView, kSceneTextureCount>& views,
                                            VkSampler sampler) {
    std::array<VkDescriptorImageInfo, kSceneTextureCount> images{};
    for (u32 i = 0; i < kSceneTextureCount; ++i) {
        // Scene depth (index 1) is sampled while it stays a read-only depth attachment.
        images[i] = {sampler, views[i],
                     i == 1 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = FrameSet(slot);
    w.dstBinding = 2;
    w.descriptorCount = kSceneTextureCount;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = images.data();
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
}

void VulkanMaterialLayout::SetLighting(u32 slot, const Lighting& l) {
    const VkDescriptorBufferInfo ubo{l.ubo, 0, l.uboSize};
    const VkDescriptorBufferInfo sh{l.skySH, 0, l.skySHSize};
    const VkDescriptorImageInfo images[3] = {
        {l.shadowSampler, l.shadowMap, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {l.environmentSampler, l.environment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {l.skySampler, l.skyCube, l.skyLayout}};
    VkWriteDescriptorSet w[5]{};
    for (u32 i = 0; i < 5; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = FrameSet(slot);
        w[i].dstBinding = 3 + i;
        w[i].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &ubo;
    for (u32 i = 0; i < 3; ++i) {
        w[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1 + i].pImageInfo = &images[i];
    }
    w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[4].pBufferInfo = &sh;
    vkUpdateDescriptorSets(m_device, 5, w, 0, nullptr);
}

}  // namespace Luma
