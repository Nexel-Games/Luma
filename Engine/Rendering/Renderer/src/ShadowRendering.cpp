#include "Luma/Renderer/Shadows.h"

#include <vulkan/vulkan.h>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <vector>
#include "Luma/Core/Log.h"
#include "Luma/RHI/RHIContext.h"
#include "Luma/RHI/RHIResources.h"
#include "Luma/Mesh/Mesh.h"
#include "Vulkan/RHI/VulkanRHIDevice.h"
#include "Vulkan/RHI/VulkanRHICommandList.h"
#include "Vulkan/RHI/VulkanRHIResources.h"

namespace Luma {
namespace Renderer2 {

using std::vector;
using namespace Luma::Math;

namespace {

VkShaderModule LoadShaderModule(VkDevice device, const std::string& spirvPath) {
    std::ifstream file(spirvPath, std::ios::binary | std::ios::ate);
    if (!file) {
        LUMA_LOG_ERROR("Deferred", "Shadow shader not found: {}", spirvPath);
        return VK_NULL_HANDLE;
    }
    std::streamsize size = file.tellg();
    if (size <= 0 || (size % 4) != 0) {
        LUMA_LOG_ERROR("Deferred", "Invalid SPIR-V size ({}): {}", size, spirvPath);
        return VK_NULL_HANDLE;
    }
    file.seekg(0);
    std::vector<uint32_t> code(static_cast<size_t>(size) / 4);
    file.read(reinterpret_cast<char*>(code.data()), size);

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = static_cast<size_t>(size);
    info.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        LUMA_LOG_ERROR("Deferred", "Failed to create shadow shader module for {}", spirvPath);
        return VK_NULL_HANDLE;
    }
    return module;
}

} // namespace

// ============================================================================
// Shadow Map
// ============================================================================

ShadowMap::ShadowMap()
    : m_created(false) {
}

ShadowMap::~ShadowMap() {
    Destroy();
}

bool ShadowMap::Create(const ShadowMapDesc& desc, RHI::RHIDevice* device) {
    m_desc = desc;
    m_device = device;
    
    if (!device) {
        m_created = true;
        return true;
    }

    auto* factory = device->GetContext()->GetResourceFactory();
    if (!factory) {
        return false;
    }
    
    auto makeTexture = [&](RHI::ETextureFormat format) -> RHI::RHITexture* {
        RHI::TextureDesc td;
        td.width = m_desc.width;
        td.height = m_desc.height;
        td.mipLevels = 1;
        td.arraySize = 1;
        td.format = format;
        td.usage = RHI::ETextureUsage::DepthStencil | RHI::ETextureUsage::ShaderResource;
        td.flags = RHI::ETextureFlags::DepthStencilTargetable | RHI::ETextureFlags::ShaderResource;
        return factory->CreateTexture(td);
    };

    if (desc.type == EShadowMapType::Cascaded) {
        m_cascadeTextures.resize(desc.cascadeCount);
        for (u32 i = 0; i < desc.cascadeCount; ++i) {
            m_cascadeTextures[i] = makeTexture(desc.format);
            if (!m_cascadeTextures[i]) return false;
        }
    } else {
        m_shadowMapTexture = makeTexture(desc.format);
        if (!m_shadowMapTexture) return false;
    }
    
    m_created = true;
    return true;
}

bool ShadowMap::Resize(u32 width, u32 height, RHI::RHIDevice* device) {
    Destroy();
    ShadowMapDesc next = m_desc;
    next.width = width;
    next.height = height;
    return Create(next, device);
}

void ShadowMap::Destroy() {
    if (m_device) {
        auto* factory = m_device->GetContext()->GetResourceFactory();
        if (factory) {
            if (m_shadowMapTexture) {
                factory->DestroyTexture(m_shadowMapTexture);
            }
            for (auto* tex : m_cascadeTextures) {
                if (tex) {
                    factory->DestroyTexture(tex);
                }
            }
        }
    }
    m_shadowMapTexture = nullptr;
    m_cascadeTextures.clear();
    m_device = nullptr;
    m_created = false;
}

// ============================================================================
// Shadow Renderer
// ============================================================================

ShadowRenderer::ShadowRenderer()
    : m_shadowMap(nullptr), m_device(nullptr), m_initialized(false) {
}

ShadowRenderer::~ShadowRenderer() {
    Shutdown();
}

bool ShadowRenderer::Initialize(RHI::RHIDevice* device) {
    m_device = device;
    m_initialized = InitPipeline();
    if (!m_initialized) {
        LUMA_LOG_ERROR("Deferred", "ShadowRenderer: Failed to initialize pipeline");
    } else {
        LUMA_LOG_INFO("Deferred", "ShadowRenderer initialized with {} cascades", m_shadowMapDesc.cascadeCount);
    }
    return m_initialized;
}

void ShadowRenderer::Shutdown() {
    if (m_initialized) {
        CleanupPipeline();
    }
    
    // Destroy cascade DSV views
    if (m_device) {
        auto* factory = m_device->GetContext()->GetResourceFactory();
        if (factory) {
            for (auto* dsv : m_cascadeDSVs) {
                if (dsv) {
                    factory->DestroyDepthStencilView(dsv);
                }
            }
        }
    }
    m_cascadeDSVs.clear();
    
    // Destroy shadow map
    if (m_shadowMap) {
        m_shadowMap->Destroy();
        delete m_shadowMap;
        m_shadowMap = nullptr;
    }
    
    m_device = nullptr;
    m_initialized = false;
}

bool ShadowRenderer::InitPipeline() {
    if (!m_device || !m_shadowMap || !m_shadowMap->IsValid()) return false;

    auto* vkDevice = dynamic_cast<RHI::VulkanRHIDevice*>(m_device);
    if (!vkDevice) return false;

    VkDevice device = vkDevice->LogicalHandle();
    std::string shaderDir = vkDevice->GetShaderDir();

    VkShaderModule vert = LoadShaderModule(device, shaderDir + "/shadow.vert.spv");
    VkShaderModule frag = LoadShaderModule(device, shaderDir + "/shadow.frag.spv");
    if (!vert || !frag) {
        LUMA_LOG_ERROR("Deferred", "Failed to load shadow shaders from {}", shaderDir);
        if (vert) vkDestroyShaderModule(device, vert, nullptr);
        if (frag) vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // Push constant range (MVP matrix)
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(Mat4);

    // Pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        LUMA_LOG_ERROR("Deferred", "Failed to create shadow pipeline layout");
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    // Vertex input state (position only)
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(MeshVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrDesc{};
    attrDesc.binding = 0;
    attrDesc.location = 0;
    attrDesc.format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDesc.offset = offsetof(MeshVertex, position);

    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &bindingDesc;
    vin.vertexAttributeDescriptionCount = 1;
    vin.pVertexAttributeDescriptions = &attrDesc;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    rs.depthBiasEnable = VK_TRUE;
    rs.depthBiasConstantFactor = 1.25f;
    rs.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 0;

    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 3;
    dynState.pDynamicStates = dyn;

    VkPipelineRenderingCreateInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    ri.colorAttachmentCount = 0;
    ri.depthAttachmentFormat = VK_FORMAT_D24_UNORM_S8_UINT;

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
    info.layout = m_pipelineLayout;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline) != VK_SUCCESS) {
        LUMA_LOG_ERROR("Deferred", "Failed to create shadow pipeline");
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
        return false;
    }

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);

    // Create shadow UBO
    RHI::BufferDesc bufDesc;
    bufDesc.size = sizeof(Mat4);
    bufDesc.usage = RHI::EBufferUsage::Uniform;
    bufDesc.cpuAccess = RHI::EBufferCPUAccess::Write;
    m_shadowUBO = m_device->GetContext()->GetResourceFactory()->CreateBuffer(bufDesc);
    if (!m_shadowUBO) {
        LUMA_LOG_ERROR("Deferred", "Failed to create shadow UBO");
        return false;
    }

    // Create DSV views for cascade textures
    if (!CreateCascadeViews()) {
        LUMA_LOG_ERROR("Deferred", "Failed to create cascade DSV views");
        return false;
    }

    return true;
}

void ShadowRenderer::CleanupPipeline() {
    if (!m_device) return;
    auto* vkDevice = dynamic_cast<RHI::VulkanRHIDevice*>(m_device);
    if (vkDevice) {
        VkDevice device = vkDevice->LogicalHandle();
        if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
        if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    }
    if (m_shadowUBO) {
        m_device->GetContext()->GetResourceFactory()->DestroyBuffer(m_shadowUBO);
        m_shadowUBO = nullptr;
    }
    
    // Destroy cascade DSV views
    auto* factory = m_device->GetContext()->GetResourceFactory();
    if (factory) {
        for (auto* dsv : m_cascadeDSVs) {
            if (dsv) {
                factory->DestroyDepthStencilView(dsv);
            }
        }
    }
    m_cascadeDSVs.clear();
    
    m_pipeline = VK_NULL_HANDLE;
    m_pipelineLayout = VK_NULL_HANDLE;
    m_initialized = false;
}

bool ShadowRenderer::CreateCascadeViews() {
    if (!m_shadowMap || !m_shadowMap->IsValid()) return false;
    if (!m_device) return false;

    auto* factory = m_device->GetContext()->GetResourceFactory();
    if (!factory) return false;

    m_cascadeDSVs.clear();
    const auto& cascadeTextures = m_shadowMap->GetCascadeTextures();
    
    RHI::TextureSubresourceRange range{};
    range.baseMipLevel = 0;
    range.mipLevels = 1;
    range.baseArrayLayer = 0;
    range.arrayLayers = 1;

    for (auto* tex : cascadeTextures) {
        if (tex) {
            auto* dsv = factory->CreateDepthStencilView(tex, m_shadowMapDesc.format, range);
            if (!dsv) {
                LUMA_LOG_ERROR("Deferred", "Failed to create cascade DSV");
                return false;
            }
            m_cascadeDSVs.push_back(dsv);
        }
    }
    return true;
}

void ShadowRenderer::Prepare(RHI::RHICommandList* cmdList) {
    if (!m_shadowMap || !m_shadowMap->IsValid()) return;

    (void)cmdList;
    // Shadow maps are cleared at the start of RenderCascadedShadowMaps;
    // Prepare is kept as a hook for future per-pass setup.
}

void ShadowRenderer::UpdateShadowUBO(RHI::RHICommandList* /*cmdList*/, const Mat4& viewProj) {
    if (m_shadowUBO) {
        m_shadowUBO->UpdateData(&viewProj, sizeof(Mat4), 0);
    }
}

void ShadowRenderer::RenderCascadedShadowMaps(RHI::RHICommandList* cmdList, const LightData& light, const Luma::SceneView& sceneView) {
    if (!m_shadowMap || !m_shadowMap->IsValid() || !m_initialized) {
        return;
    }

    if (light.type != static_cast<u32>(ELightType::Directional)) {
        return;
    }

    auto* vkList = dynamic_cast<RHI::VulkanRHICommandList*>(cmdList);
    if (!vkList) {
        LUMA_LOG_ERROR("Deferred", "ShadowRenderer: cannot cast cmdList to VulkanRHICommandList");
        return;
    }
    VkCommandBuffer cmd = vkList->Handle();

    m_cascadeSplits = CalculateCascadeSplits(sceneView.nearZ, sceneView.farZ, m_shadowMapDesc.cascadeSplitLambda, m_shadowMapDesc.cascadeCount);
    m_cascadeViewProjections.resize(m_shadowMapDesc.cascadeCount);

    Vec3 lightDir = Luma::Math::Normalize(light.direction);
    if (Dot(lightDir, lightDir) < 0.001f * 0.001f) {
        lightDir = Vec3(0.0f, -1.0f, 0.0f);
    }
    lightDir = Luma::Math::Normalize(lightDir);

    Vec3 camPos(0.0f, 0.0f, 0.0f);
    if (sceneView.instanceCount > 0 || true) {
        Mat4 invView = Inverse(sceneView.view);
        camPos = Vec3(invView.m[12], invView.m[13], invView.m[14]);
    }

    LUMA_LOG_INFO("Deferred", "Rendering {} cascaded shadow maps", m_shadowMapDesc.cascadeCount);

    for (u32 i = 0; i < m_shadowMapDesc.cascadeCount; ++i) {
        f32 cascadeNear = (i == 0) ? sceneView.nearZ : m_cascadeSplits[i - 1];
        f32 cascadeFar  = m_cascadeSplits[i];

        Mat4 cascadeViewProj = CalculateCascadeViewProjection(camPos, lightDir, cascadeNear, cascadeFar, sceneView);
        m_cascadeViewProjections[i] = cascadeViewProj;

        RHI::RHIDepthStencilView* cascadeDSV = (i < m_cascadeDSVs.size()) ? m_cascadeDSVs[i] : nullptr;
        if (!cascadeDSV) continue;

        cmdList->Begin();

        m_cascadeDSVs[i] = cascadeDSV; // ensure DSV is current
        cmdList->SetRenderTargets(0, nullptr, cascadeDSV);

        VkViewport viewport{0, 0, static_cast<f32>(m_shadowMapDesc.width), static_cast<f32>(m_shadowMapDesc.height), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, {m_shadowMapDesc.width, m_shadowMapDesc.height}};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        f32 depthBiasConstant = 1.25f + static_cast<f32>(i) * 0.5f;
        f32 depthBiasSlope = 1.75f + static_cast<f32>(i) * 0.5f;
        vkCmdSetDepthBias(cmd, depthBiasConstant, 0.0f, depthBiasSlope);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &cascadeViewProj);

        DrawInstances(cmdList, sceneView, cascadeViewProj);

        cmdList->End();

        RHI::RHICommandList* lists[] = {cmdList};
        m_device->GetContext()->GetGraphicsQueue()->Submit(1, lists);
    }

    m_device->WaitIdle();

    LUMA_LOG_INFO("Deferred", "Completed cascaded shadow map rendering");
}

void ShadowRenderer::DrawInstances(RHI::RHICommandList* cmdList, const Luma::SceneView& sceneView, const Mat4& cascadeViewProj) {
    if (!cmdList || !sceneView.instances || sceneView.instanceCount == 0) return;
    if (!m_primitives) return;

    auto* vkList = dynamic_cast<RHI::VulkanRHICommandList*>(cmdList);
    if (!vkList) return;
    VkCommandBuffer cmd = vkList->Handle();

    u32 drawn = 0;
    for (u32 i = 0; i < sceneView.instanceCount; ++i) {
        const auto& inst = sceneView.instances[i];

        const GBufferRenderer::RhiMesh* mesh = nullptr;
        if (inst.customMeshValid && inst.customVertices && inst.customVertexCount > 0) {
            continue;
        } else {
            u32 primIdx = static_cast<u32>(inst.primitive);
            if (primIdx >= 4) continue;
            mesh = &m_primitives[primIdx];
        }

        if (!mesh || !mesh->vertexBuffer) continue;

        Mat4 mvp = cascadeViewProj * inst.model;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &mvp);

        VkDeviceSize offset = 0;
        VkBuffer vkVert = static_cast<RHI::VulkanRHIBuffer*>(mesh->vertexBuffer)->Handle();
        vkCmdBindVertexBuffers(cmd, 0, 1, &vkVert, &offset);

        if (mesh->indexBuffer && mesh->indexCount > 0) {
            VkBuffer vkIdx = static_cast<RHI::VulkanRHIBuffer*>(mesh->indexBuffer)->Handle();
            vkCmdBindIndexBuffer(cmd, vkIdx, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0u);
        } else {
            vkCmdDraw(cmd, mesh->vertexCount, 1, 0, 0u);
        }
        ++drawn;
    }

    LUMA_LOG_TRACE("Deferred", "Shadow pass drew {} / {} instances", drawn, sceneView.instanceCount);
}

void ShadowRenderer::RenderShadowMap(RHI::RHICommandList* cmdList, const LightData& light, const Luma::SceneView& sceneView) {
    if (light.type == static_cast<u32>(ELightType::Directional)) {
        RenderCascadedShadowMaps(cmdList, light, sceneView);
    }
    (void)cmdList;
    (void)light;
    (void)sceneView;
}

void ShadowRenderer::RenderCubeShadowMap(RHI::RHICommandList* cmdList, const LightData& light, const Luma::SceneView& sceneView) {
    (void)cmdList;
    (void)light;
    (void)sceneView;
}

vector<f32> ShadowRenderer::CalculateCascadeSplits(f32 nearPlane, f32 farPlane, f32 splitLambda, u32 cascadeCount) {
    vector<f32> splits;
    splits.reserve(cascadeCount);

    // Practical split scheme (logarithmic + uniform blend)
    for (u32 i = 1; i <= cascadeCount; ++i) {
        f32 fraction = static_cast<f32>(i) / static_cast<f32>(cascadeCount);
        
        // Logarithmic split
        f32 logSplit = nearPlane * std::pow(farPlane / nearPlane, fraction);
        
        // Uniform split
        f32 uniformSplit = nearPlane + (farPlane - nearPlane) * fraction;
        
        // Blend between logarithmic and uniform
        f32 split = splitLambda * (logSplit - uniformSplit) + uniformSplit;
        splits.push_back(split);
    }

    return splits;
}

Mat4 ShadowRenderer::CalculateLightViewMatrix(const LightData& light) {
    Vec3 lightPos = light.position;
    Vec3 lightDir = light.direction;
    if (Dot(lightDir, lightDir) < 0.001f * 0.001f) {
        lightDir = Vec3(0.0f, -1.0f, 0.0f);
    }
    lightDir = Normalize(lightDir);
    Vec3 lightTarget = lightPos + lightDir;
    Vec3 lightUp = Vec3(0.0f, 1.0f, 0.0f);
    
    // Avoid gimbal lock when looking straight down
    if (std::abs(lightDir.y) > 0.99f) {
        lightUp = Vec3(0.0f, 0.0f, 1.0f);
    }
    
    return LookAt(lightPos, lightTarget, lightUp);
}

Mat4 ShadowRenderer::CalculateLightProjectionMatrix(const LightData& light) {
    if (static_cast<ELightType>(light.type) == ELightType::Directional) {
        f32 orthoSize = light.range * 2.0f;
        return Ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, light.range);
    } else {
        f32 aspect = 1.0f;
        f32 fov = Radians(90.0f);
        return Perspective(fov, aspect, 0.1f, light.range);
    }
}

Mat4 ShadowRenderer::CalculateCascadeViewProjection(const Vec3& cameraPos, const Vec3& lightDir, f32 nearPlane, f32 farPlane, const Luma::SceneView& sceneView) {
    // Calculate cascade view-projection for directional light
    // We position the light camera to cover the cascade's view frustum
    
    // Light view matrix - position the light camera to look at the cascade center
    Vec3 cascadeCenter = cameraPos - lightDir * ((nearPlane + farPlane) * 0.5f);
    Vec3 lightPos = cascadeCenter - lightDir * (farPlane * 0.5f);
    
    Mat4 lightView = LookAt(lightPos, cascadeCenter, Vec3(0.0f, 1.0f, 0.0f));
    
    // Orthographic projection sized to cover the cascade
    f32 fovY = sceneView.fovYRadians;
    f32 halfHeight = std::tan(fovY * 0.5f) * farPlane * 1.2f;
    f32 halfWidth = halfHeight; // Shadow map is square (2048x2048)
    
    Mat4 lightProj = Ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, 0.1f, farPlane * 2.0f);
    
    return lightProj * lightView;
}

// ============================================================================
// Shadow Manager
// ============================================================================

ShadowManager& ShadowManager::GetInstance() {
    static ShadowManager instance;
    return instance;
}

ShadowManager::~ShadowManager() {
    Clear();
}

ShadowMap* ShadowManager::CreateShadowMap(const ShadowMapDesc& desc, RHI::RHIDevice* device) {
    auto* shadowMap = new ShadowMap();
    shadowMap->Create(desc, device);
    m_shadowMaps.push_back(shadowMap);
    return shadowMap;
}

void ShadowManager::DestroyShadowMap(ShadowMap* shadowMap) {
    auto it = std::find(m_shadowMaps.begin(), m_shadowMaps.end(), shadowMap);
    if (it != m_shadowMaps.end()) {
        m_shadowMaps.erase(it);
        shadowMap->Destroy();
        delete shadowMap;
    }
}

void ShadowManager::Clear() {
    for (auto* shadowMap : m_shadowMaps) {
        shadowMap->Destroy();
        delete shadowMap;
    }
    m_shadowMaps.clear();
}

} // namespace Renderer2
} // namespace Luma