#include "Vulkan/Overlay/VulkanOverlayLinePass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Luma/Core/Log.h"
#include "Vulkan/VulkanShader.h"

namespace Luma {
namespace {

struct Clip {
    f32 x, y, z, w;
};

Clip ToClip(const Math::Mat4& m, const Math::Vec3& p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14],
            m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15]};
}

Clip Lerp(const Clip& a, const Clip& b, f32 t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t};
}

}  // namespace

VulkanOverlayLinePass::VulkanOverlayLinePass(VkPhysicalDevice physical, VkDevice device,
                                             const std::string& shaderDir,
                                             VkFormat colorFormat, VkFormat depthFormat)
    : m_device(device) {
    m_vertices = CreateBuffer(physical, device, sizeof(Vertex) * kMaxVertices,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              true);
    if (!m_vertices.buffer) return;

    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(f32) * 4};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) return;

    VkShaderModule vert = LoadShaderModule(device, shaderDir + "/overlay_line.vert.spv");
    VkShaderModule frag = LoadShaderModule(device, shaderDir + "/overlay_line.frag.spv");
    if (!vert || !frag) {
        LUMA_LOG_ERROR("VulkanOverlayLinePass", "Failed to load overlay line shaders");
        if (vert) vkDestroyShaderModule(device, vert, nullptr);
        if (frag) vkDestroyShaderModule(device, frag, nullptr);
        return;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i) {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;

    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, clip)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)}};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = 2;
    vin.pVertexAttributeDescriptions = attrs;

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
    if (depthFormat != VK_FORMAT_UNDEFINED) {
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_FALSE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    }
    VkPipelineColorBlendAttachmentState ba{};
    ba.blendEnable = VK_TRUE;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.colorBlendOp = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.alphaBlendOp = VK_BLEND_OP_ADD;
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &colorFormat;
    ri.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &ri;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vin;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dynState;
    info.layout = m_layout;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline) !=
        VK_SUCCESS) {
        m_pipeline = VK_NULL_HANDLE;
        LUMA_LOG_ERROR("VulkanOverlayLinePass", "Failed to create overlay line pipeline");
    }
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
}

VulkanOverlayLinePass::~VulkanOverlayLinePass() {
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    DestroyBuffer(m_device, m_vertices);
}

void VulkanOverlayLinePass::Record(VkCommandBuffer cmd, const LineVertex* lines,
                                   u32 vertexCount, const Math::Mat4& viewProj, u32 width,
                                   u32 height, f32 thicknessPx) {
    if (!Valid() || !lines || vertexCount < 2 || width == 0 || height == 0) return;
    m_scratch.clear();
    const f32 halfW = static_cast<f32>(width) * 0.5f;
    const f32 halfH = static_cast<f32>(height) * 0.5f;
    constexpr f32 kFeatherPx = 1.0f;
    const f32 halfThick = thicknessPx * 0.5f + kFeatherPx;  // core + AA feather
    constexpr f32 kNearW = 1e-3f;

    for (u32 i = 0; i + 1 < vertexCount; i += 2) {
        Clip a = ToClip(viewProj, lines[i].position);
        Clip b = ToClip(viewProj, lines[i + 1].position);
        // Clip the segment to the region in front of the camera.
        if (a.w < kNearW && b.w < kNearW) continue;
        if (a.w < kNearW) a = Lerp(a, b, (kNearW - a.w) / (b.w - a.w));
        if (b.w < kNearW) b = Lerp(b, a, (kNearW - b.w) / (a.w - b.w));

        // Pixel-space direction -> perpendicular offset of constant width.
        const f32 ax = a.x / a.w * halfW, ay = a.y / a.w * halfH;
        const f32 bx = b.x / b.w * halfW, by = b.y / b.w * halfH;
        f32 dx = bx - ax, dy = by - ay;
        const f32 len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        dx /= len;
        dy /= len;
        // Offset in NDC, then scaled by each end's w to stay in clip space.
        const f32 ox = -dy * halfThick / halfW;
        const f32 oy = dx * halfThick / halfH;

        const Math::Vec3& ca = lines[i].color;
        const Math::Vec3& cbc = lines[i + 1].color;
        auto vert = [&](const Clip& c, f32 sign, const Math::Vec3& col) {
            Vertex v;
            v.clip[0] = c.x + ox * sign * c.w;
            v.clip[1] = c.y + oy * sign * c.w;
            v.clip[2] = std::clamp(c.z, 0.0f, c.w);  // never depth-clipped
            v.clip[3] = c.w;
            v.color[0] = col.x;
            v.color[1] = col.y;
            v.color[2] = col.z;
            // w = signed distance from the line center in pixels (the
            // fragment shader fades the outer kFeatherPx).
            v.color[3] = sign * halfThick;
            return v;
        };
        const Vertex a0 = vert(a, -1.0f, ca), a1 = vert(a, 1.0f, ca);
        const Vertex b0 = vert(b, -1.0f, cbc), b1 = vert(b, 1.0f, cbc);
        m_scratch.insert(m_scratch.end(), {a0, a1, b1, a0, b1, b0});
        if (m_scratch.size() + 6 > kMaxVertices) break;
    }
    if (m_scratch.empty()) return;

    std::memcpy(m_vertices.mapped, m_scratch.data(), m_scratch.size() * sizeof(Vertex));
    VkViewport viewport{0.0f, 0.0f, static_cast<f32>(width), static_cast<f32>(height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    const f32 pushData[4] = {halfThick, 1.0f, 0.0f, 0.0f};  // half width, feather
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushData),
                       pushData);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertices.buffer, &offset);
    vkCmdDraw(cmd, static_cast<u32>(m_scratch.size()), 1, 0, 0);
}

}  // namespace Luma
