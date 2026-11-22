#include "Vulkan/Selection/VulkanSelectionOutlinePass.h"

#include <cstddef>

#include "Vulkan/VulkanMemory.h"
#include "Vulkan/VulkanShader.h"

namespace Luma {
namespace {

struct OutlinePush {
    f32 color[4];
    f32 widthPx;
    f32 occludedAlpha;
};

}  // namespace

VulkanSelectionOutlinePass::VulkanSelectionOutlinePass(
    VkPhysicalDevice physical, VkDevice device, const std::string& shaderDir, u32 width,
    u32 height, VkFormat targetFormat)
    : m_device(device), m_width(width), m_height(height) {
    if (width == 0 || height == 0) return;
    if (!CreateMask(physical) || !CreateDescriptors() ||
        !CreatePipelines(shaderDir, targetFormat)) {
        LUMA_LOG_ERROR("SelectionOutline", "Failed to create the selection outline pass");
    }
}

VulkanSelectionOutlinePass::~VulkanSelectionOutlinePass() {
    if (m_outlinePipeline) vkDestroyPipeline(m_device, m_outlinePipeline, nullptr);
    if (m_outlineLayout) vkDestroyPipelineLayout(m_device, m_outlineLayout, nullptr);
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
    if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_maskView) vkDestroyImageView(m_device, m_maskView, nullptr);
    if (m_mask) vkDestroyImage(m_device, m_mask, nullptr);
    if (m_maskMemory) vkFreeMemory(m_device, m_maskMemory, nullptr);
}

bool VulkanSelectionOutlinePass::CreateMask(VkPhysicalDevice physical) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = kMaskFormat;
    info.extent = {m_width, m_height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &info, nullptr, &m_mask) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_device, m_mask, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex =
        FindMemoryType(physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &alloc, nullptr, &m_maskMemory) != VK_SUCCESS) return false;
    vkBindImageMemory(m_device, m_mask, m_maskMemory, 0);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_mask;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = kMaskFormat;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(m_device, &view, nullptr, &m_maskView) == VK_SUCCESS;
}

bool VulkanSelectionOutlinePass::CreateDescriptors() {
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_device, &sampler, nullptr, &m_sampler) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(m_device, &layout, nullptr, &m_setLayout) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    if (vkCreateDescriptorPool(m_device, &pool, nullptr, &m_pool) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = m_pool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_setLayout;
    if (vkAllocateDescriptorSets(m_device, &alloc, &m_outlineSet) != VK_SUCCESS) return false;

    VkDescriptorImageInfo maskInfo{m_sampler, m_maskView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_outlineSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &maskInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return true;
}

bool VulkanSelectionOutlinePass::CreatePipelines(const std::string& shaderDir,
                                                 VkFormat targetFormat) {
    VkShaderModule outVert = LoadShaderModule(m_device, shaderDir + "/selection_outline.vert.spv");
    VkShaderModule outFrag = LoadShaderModule(m_device, shaderDir + "/selection_outline.frag.spv");
    auto cleanup = [&] {
        for (VkShaderModule m : {outVert, outFrag}) {
            if (m) vkDestroyShaderModule(m_device, m, nullptr);
        }
    };
    if (!outVert || !outFrag) {
        cleanup();
        return false;
    }

    VkPushConstantRange outlinePush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutlinePush)};
    VkPipelineLayoutCreateInfo outlineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    outlineLayout.setLayoutCount = 1;
    outlineLayout.pSetLayouts = &m_setLayout;
    outlineLayout.pushConstantRangeCount = 1;
    outlineLayout.pPushConstantRanges = &outlinePush;
    if (vkCreatePipelineLayout(m_device, &outlineLayout, nullptr, &m_outlineLayout) != VK_SUCCESS) {
        cleanup();
        return false;
    }

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dyn;

    // Outline: fullscreen triangle, alpha blended onto the display image.
    VkPipelineVertexInputStateCreateInfo noInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState alphaBlend{};
    alphaBlend.blendEnable = VK_TRUE;
    alphaBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    alphaBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    alphaBlend.colorBlendOp = VK_BLEND_OP_ADD;
    alphaBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    alphaBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    alphaBlend.alphaBlendOp = VK_BLEND_OP_ADD;
    alphaBlend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo outlineBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    outlineBlend.attachmentCount = 1;
    outlineBlend.pAttachments = &alphaBlend;
    VkPipelineRenderingCreateInfo outlineRendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    outlineRendering.colorAttachmentCount = 1;
    outlineRendering.pColorAttachmentFormats = &targetFormat;
    VkPipelineShaderStageCreateInfo outlineStages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, outVert, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, outFrag, "main", nullptr}};
    VkGraphicsPipelineCreateInfo outlineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    outlineInfo.pNext = &outlineRendering;
    outlineInfo.stageCount = 2;
    outlineInfo.pInputAssemblyState = &ia;
    outlineInfo.pViewportState = &vp;
    outlineInfo.pRasterizationState = &rs;
    outlineInfo.pMultisampleState = &ms;
    outlineInfo.pDepthStencilState = &ds;
    outlineInfo.pDynamicState = &dynamic;
    outlineInfo.pStages = outlineStages;
    outlineInfo.pVertexInputState = &noInput;
    outlineInfo.pColorBlendState = &outlineBlend;
    outlineInfo.layout = m_outlineLayout;

    const bool ok =
        vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &outlineInfo, nullptr, &m_outlinePipeline) == VK_SUCCESS;
    cleanup();
    if (!ok) m_outlinePipeline = VK_NULL_HANDLE;
    return ok;
}

void VulkanSelectionOutlinePass::RecordMask(VkCommandBuffer cmd,
                                            const std::function<void(VkCommandBuffer)>& draw) {
    if (!Valid()) return;
    VkImageMemoryBarrier toColor{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // fully rewritten
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.srcQueueFamilyIndex = toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.image = m_mask;
    toColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &toColor);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_maskView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {m_width, m_height};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &rendering);
    const VkViewport viewport{0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0, 1};
    const VkRect2D scissor{{0, 0}, {m_width, m_height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    draw(cmd);
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier toRead = toColor;
    toRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toRead);
}

void VulkanSelectionOutlinePass::RecordOutline(VkCommandBuffer cmd, const Style& style) {
    if (!Valid()) return;
    const VkViewport viewport{0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0, 1};
    const VkRect2D scissor{{0, 0}, {m_width, m_height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_outlinePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_outlineLayout, 0, 1,
                            &m_outlineSet, 0, nullptr);
    OutlinePush push{{style.color.x, style.color.y, style.color.z, style.color.w},
                     style.widthPx, style.occludedAlpha};
    vkCmdPushConstants(cmd, m_outlineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

}  // namespace Luma
