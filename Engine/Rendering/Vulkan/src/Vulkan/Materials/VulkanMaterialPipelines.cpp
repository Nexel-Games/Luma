#include "Vulkan/Materials/VulkanMaterialPipelines.h"

#include "Luma/Core/Log.h"
#include "Luma/RHI/MeshData.h"

namespace Luma {
namespace {

bool Translucent(GpuBlendMode b) {
    return b == GpuBlendMode::Translucent || b == GpuBlendMode::Additive || b == GpuBlendMode::Modulate;
}

// Forward-pass colour blending per blend mode (premultiplied translucency).
VkPipelineColorBlendAttachmentState ForwardBlend(const CompiledMaterialGpu& m) {
    VkPipelineColorBlendAttachmentState b{};
    b.colorWriteMask = 0xF;
    if (m.usesRefraction) return b;  // the shader composites over the scene-colour copy itself
    b.blendEnable = VK_TRUE;
    b.colorBlendOp = b.alphaBlendOp = VK_BLEND_OP_ADD;
    switch (m.blend) {
        case GpuBlendMode::Additive:
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            break;
        case GpuBlendMode::Modulate:
            b.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            break;
        default:
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            break;
    }
    b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // keep the target's alpha
    return b;
}

}  // namespace

VulkanMaterialPipelines::VulkanMaterialPipelines(VkDevice device, VkPipelineLayout layout)
    : m_device(device), m_layout(layout) {}

VulkanMaterialPipelines::~VulkanMaterialPipelines() {
    for (auto& [key, entry] : m_pipelines) {
        if (entry.pipeline) vkDestroyPipeline(m_device, entry.pipeline, nullptr);
    }
}

void VulkanMaterialPipelines::SetTarget(GpuPass pass, const Target& target) {
    const usize i = static_cast<usize>(pass);
    if (m_targetGeneration[i] != 0 && m_targets[i] == target) return;
    m_targets[i] = target;
    m_targetGeneration[i] = m_nextGeneration++;
    // Pipelines for the old target are unusable now.
    for (auto it = m_pipelines.begin(); it != m_pipelines.end();) {
        if (it->first.pass == pass) {
            if (it->second.pipeline) vkDestroyPipeline(m_device, it->second.pipeline, nullptr);
            it = m_pipelines.erase(it);
        } else {
            ++it;
        }
    }
}

bool VulkanMaterialPipelines::HasTarget(GpuPass pass) const {
    return m_targetGeneration[static_cast<usize>(pass)] != 0;
}

VkShaderModule VulkanMaterialPipelines::Module(const std::vector<u32>& spirv) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = spirv.size() * sizeof(u32);
    info.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &info, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}

VkPipeline VulkanMaterialPipelines::Create(const CompiledMaterialGpu& m, GpuPass pass, const GpuShaderPair& shaders) {
    const Target& target = m_targets[static_cast<usize>(pass)];
    VkShaderModule vs = Module(shaders.vertex);
    VkShaderModule fs = Module(shaders.fragment);
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vs && fs) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main",
                     nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs,
                     "main", nullptr};

        const bool fullscreen = pass == GpuPass::PostProcess;
        const VkVertexInputBindingDescription binding{0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attrs[4] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, tangent)},
            {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv0)}};
        VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        if (!fullscreen) {
            vin.vertexBindingDescriptionCount = 1;
            vin.pVertexBindingDescriptions = &binding;
            vin.vertexAttributeDescriptionCount = 4;
            vin.pVertexAttributeDescriptions = attrs;
        }
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        rs.cullMode = m.twoSided || fullscreen ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        if (pass == GpuPass::DepthOnly) {
            // Shadow casters draw both faces: open meshes still cast, and
            // slope-scaled bias keeps acne off lit surfaces.
            rs.cullMode = VK_CULL_MODE_NONE;
            rs.depthBiasEnable = VK_TRUE;
            rs.depthBiasConstantFactor = 1.25f;
            rs.depthBiasSlopeFactor = 1.75f;
        }
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        switch (pass) {
            case GpuPass::GBuffer:
            case GpuPass::DepthOnly:
                ds.depthTestEnable = VK_TRUE;
                ds.depthWriteEnable = VK_TRUE;
                ds.depthCompareOp = VK_COMPARE_OP_LESS;
                break;
            case GpuPass::Forward:
                ds.depthTestEnable = VK_TRUE;  // against opaque depth, no writes
                ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
                break;
            default: break;  // selection compares in the shader; post-process is fullscreen
        }

        std::vector<VkPipelineColorBlendAttachmentState> blends(target.colorFormats.size());
        for (auto& b : blends) b.colorWriteMask = 0xF;
        if (pass == GpuPass::SelectionMask) {
            // MAX: a pixel is visible when any covering fragment is.
            for (auto& b : blends) {
                b.blendEnable = VK_TRUE;
                b.srcColorBlendFactor = b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                b.srcAlphaBlendFactor = b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                b.colorBlendOp = b.alphaBlendOp = VK_BLEND_OP_MAX;
            }
        } else if (pass == GpuPass::Forward && !blends.empty()) {
            blends[0] = ForwardBlend(m);
        }
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = static_cast<u32>(blends.size());
        cb.pAttachments = blends.data();

        const VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = static_cast<u32>(target.colorFormats.size());
        rendering.pColorAttachmentFormats = target.colorFormats.data();
        rendering.depthAttachmentFormat = target.depthFormat;

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = target.renderPass ? nullptr : &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vin;
        info.pInputAssemblyState = &ia;
        info.pViewportState = &vp;
        info.pRasterizationState = &rs;
        info.pMultisampleState = &ms;
        info.pDepthStencilState = &ds;
        info.pColorBlendState = &cb;
        info.pDynamicState = &dynamic;
        info.layout = m_layout;
        info.renderPass = target.renderPass;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
            pipeline = VK_NULL_HANDLE;
        }
    }
    if (vs) vkDestroyShaderModule(m_device, vs, nullptr);
    if (fs) vkDestroyShaderModule(m_device, fs, nullptr);
    return pipeline;
}

VkPipeline VulkanMaterialPipelines::Get(const CompiledMaterialGpu& m, GpuPass pass) {
    const usize p = static_cast<usize>(pass);
    if (!m.passes[p] || m_targetGeneration[p] == 0) return VK_NULL_HANDLE;
    const Key key{m.shaderKey, pass, static_cast<u8>(m.twoSided), static_cast<u8>(m.blend), m_targetGeneration[p]};
    if (const auto it = m_pipelines.find(key); it != m_pipelines.end()) return it->second.pipeline;
    VkPipeline pipeline = Create(m, pass, *m.passes[p]);
    if (!pipeline && !m_failed[m.shaderKey]) {
        m_failed[m.shaderKey] = true;
        LUMA_LOG_ERROR("MaterialPipelines", "Failed to create the pipeline of pass %u for shader set %016llx",
                       static_cast<u32>(p), static_cast<unsigned long long>(m.shaderKey));
    }
    m_pipelines[key] = {pipeline, m.shaderKey};
    m_idleFrames[m.shaderKey] = 0;
    return pipeline;
}

bool VulkanMaterialPipelines::Prepare(const CompiledMaterialGpu& m) {
    bool ok = true;
    for (usize p = 0; p < kGpuPassCount; ++p) {
        if (!m.passes[p] || m_targetGeneration[p] == 0) continue;
        ok = Get(m, static_cast<GpuPass>(p)) != VK_NULL_HANDLE && ok;
    }
    return ok;
}

void VulkanMaterialPipelines::Touch(u64 shaderKey) { m_idleFrames[shaderKey] = 0; }

void VulkanMaterialPipelines::EndFrame() {
    for (auto it = m_idleFrames.begin(); it != m_idleFrames.end();) {
        if (++it->second <= kRetireFrames) {
            ++it;
            continue;
        }
        const u64 key = it->first;
        for (auto p = m_pipelines.begin(); p != m_pipelines.end();) {
            if (p->second.shaderKey == key) {
                if (p->second.pipeline) vkDestroyPipeline(m_device, p->second.pipeline, nullptr);
                p = m_pipelines.erase(p);
            } else {
                ++p;
            }
        }
        m_failed.erase(key);
        it = m_idleFrames.erase(it);
    }
}

}  // namespace Luma
