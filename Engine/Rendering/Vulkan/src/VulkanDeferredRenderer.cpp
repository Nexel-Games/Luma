#include "Luma/Rendering/Vulkan/VulkanDeferredRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4505)
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "Luma/Core/Log.h"
#include "Luma/Math/MathUtils.h"
#include "Luma/Mesh/Mesh.h"
#include "Luma/Renderer/DeferredSceneView.h"
#include "Vulkan/Grid/VulkanGridPass.h"
#include "Vulkan/Materials/VulkanMaterialLayout.h"
#include "Vulkan/Materials/VulkanMaterialPipelines.h"
#include "Vulkan/Materials/VulkanMaterialSystem.h"
#include "Vulkan/Meshes/VulkanMeshCache.h"
#include "Vulkan/Selection/VulkanSelectionOutlinePass.h"
#include "Vulkan/Textures/VulkanTextureCache.h"
#include "Vulkan/Overlay/VulkanOverlayLinePass.h"
#include "Vulkan/EyeAdaptation/VulkanEyeAdaptation.h"
#include "Vulkan/SkyLight/VulkanSkyLight.h"
#include "Vulkan/UI/VulkanUIPass.h"
#include "Vulkan/VulkanShader.h"
#include "Vulkan/VulkanMemory.h"

namespace Luma {
namespace Rendering {

namespace {

void SetVec3(f32* dst, const Math::Vec3& v, f32 w = 1.0f) {
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
    dst[3] = w;
}

// Transforms a world point by a column-major matrix (w = 1, no perspective).
Math::Vec3 TransformPoint(const Math::Mat4& m, const Math::Vec3& p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

u16 FloatToHalf(f32 value) {
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const u32 sign = (bits >> 16) & 0x8000u;
    const u32 exponent = (bits >> 23) & 0xffu;
    const u32 mantissa = bits & 0x7fffffu;
    if (exponent == 0xffu) {
        return static_cast<u16>(sign | 0x7c00u |
                                (mantissa ? 0x0200u : 0u));
    }
    const i32 halfExponent = static_cast<i32>(exponent) - 127 + 15;
    if (halfExponent <= 0) return static_cast<u16>(sign);
    if (halfExponent >= 31) return static_cast<u16>(sign | 0x7c00u);
    return static_cast<u16>(sign |
                            (static_cast<u32>(halfExponent) << 10) |
                            ((mantissa + 0x1000u) >> 13));
}

f32 HalfToFloat(u16 h) {
    const u32 sign = static_cast<u32>(h & 0x8000u) << 16;
    u32 exponent = (h >> 10) & 0x1fu;
    u32 mantissa = h & 0x3ffu;
    u32 bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {  // subnormal: renormalize
            exponent = 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3ffu;
            bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    f32 value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

}  // namespace

VulkanDeferredRenderer::VulkanDeferredRenderer(
    VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue,
    uint32_t graphicsQueueFamilyIndex, VulkanUIPass& uiPass,
    const std::string& shaderDir, bool useEnvironment)
    : m_physicalDevice(physicalDevice)
    , m_device(device)
    , m_graphicsQueue(graphicsQueue)
    , m_graphicsQueueFamilyIndex(graphicsQueueFamilyIndex)
    , m_uiPass(uiPass)
    , m_shaderDir(shaderDir)
    , m_useEnvironment(useEnvironment)
    , m_initialized(false)
    , m_lightingPipeline(VK_NULL_HANDLE)
    , m_lightingLayout(VK_NULL_HANDLE)
    , m_lightingSetLayout(VK_NULL_HANDLE)
    , m_lightingPool(VK_NULL_HANDLE)
    , m_lightingSet(VK_NULL_HANDLE)
    , m_commandPool(VK_NULL_HANDLE)
    , m_commandBuffer(VK_NULL_HANDLE)
    , m_fence(VK_NULL_HANDLE)
{
    m_gBuffer.width = 0;
    m_gBuffer.height = 0;
    m_gBuffer.sampler = VK_NULL_HANDLE;
    m_gBuffer.albedo = {};
    m_gBuffer.normal = {};
    m_gBuffer.material = {};
    m_gBuffer.depth = {};
    m_gBuffer.lightAccum = {};

    m_textures = std::make_unique<VulkanTextureCache>(physicalDevice, device, graphicsQueue,
                                                      graphicsQueueFamilyIndex);
    m_meshes = std::make_unique<VulkanMeshCache>(physicalDevice, device, graphicsQueue,
                                                 graphicsQueueFamilyIndex);
    m_materialLayout = std::make_unique<VulkanMaterialLayout>(physicalDevice, device, m_textures->Layout());
    m_materialPipelines = std::make_unique<VulkanMaterialPipelines>(device, m_materialLayout->PipelineLayout());
    m_materials = std::make_unique<VulkanMaterialSystem>(*m_materialPipelines);
    if (!m_textures->IsValid() || !m_materialLayout->IsValid()) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Scene content caches failed to initialize");
    }
    const MeshPrimitive kinds[kPrimitiveCount] = {MeshPrimitive::Cube, MeshPrimitive::Plane,
                                                  MeshPrimitive::Sphere, MeshPrimitive::Cylinder};
    for (u32 i = 0; i < kPrimitiveCount; ++i) {
        // Reserved keys: the top bits mark built-in content.
        m_primitiveMeshes[i] = m_meshes->Upload(0xB1170000'00000000ull | i, BuildPrimitive(kinds[i]));
    }
}

VulkanDeferredRenderer::~VulkanDeferredRenderer() {
    Cleanup();
    // Caches are released after the frame resources (members, reverse order).
}

MeshHandle VulkanDeferredRenderer::UploadMesh(u64 key, const MeshData& mesh) {
    return m_meshes->Upload(key, mesh);
}

void VulkanDeferredRenderer::ReleaseMesh(MeshHandle mesh) {
    for (MeshHandle primitive : m_primitiveMeshes) {
        if (primitive == mesh) return;  // built-in shapes stay resident
    }
    m_meshes->Release(mesh);
}

GpuTextureHandle VulkanDeferredRenderer::UploadTexture(u64 key, const TextureUploadDesc& texture) {
    return m_textures->Upload(key, texture);
}

void VulkanDeferredRenderer::ReleaseTexture(GpuTextureHandle texture) {
    m_textures->Release(texture);
}

MaterialHandle VulkanDeferredRenderer::UpdateMaterial(u64 key, const CompiledMaterialGpu& material,
                                                     const MaterialParameterBlock& parameters) {
    return m_materials->Update(key, material, parameters);
}

void VulkanDeferredRenderer::UpdateMaterialParameters(MaterialHandle material,
                                                      const MaterialParameterBlock& parameters) {
    m_materials->UpdateParameters(material, parameters);
}

usize VulkanDeferredRenderer::MaterialPipelineCount() const { return m_materialPipelines->PipelineCount(); }

void VulkanDeferredRenderer::RegisterMaterialTargets() {
    VulkanMaterialPipelines::Target gbuffer;
    gbuffer.renderPass = m_gBuffer.renderPass;
    gbuffer.colorFormats = {kAlbedoFormat, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, kEmissiveFormat,
                            VK_FORMAT_R8G8B8A8_UNORM};
    gbuffer.depthFormat = kDepthFormat;
    m_materialPipelines->SetTarget(GpuPass::GBuffer, gbuffer);
    VulkanMaterialPipelines::Target shadow;
    shadow.depthFormat = kShadowFormat;
    m_materialPipelines->SetTarget(GpuPass::DepthOnly, shadow);
    VulkanMaterialPipelines::Target selection;
    selection.colorFormats = {VulkanSelectionOutlinePass::kMaskFormat};
    m_materialPipelines->SetTarget(GpuPass::SelectionMask, selection);
    VulkanMaterialPipelines::Target forward;
    forward.colorFormats = {kLightFormat};
    forward.depthFormat = kDepthFormat;
    m_materialPipelines->SetTarget(GpuPass::Forward, forward);
}

void VulkanDeferredRenderer::RecordTranslucency(const SceneView& source, const Math::Vec3& cameraPosition) {
    struct Draw {
        u32 instance;
        f32 distance;
    };
    std::vector<Draw> draws;
    bool refraction = false;
    for (u32 i = 0; i < source.instanceCount; ++i) {
        const SceneInstance& inst = source.instances[i];
        const GpuMesh* mesh = ResolveMesh(inst);
        if (!mesh) continue;
        bool translucent = false;
        for (u32 s = 0; s < mesh->sections.size(); ++s) {
            const CompiledMaterialGpu* m = m_materials->Drawable(SectionMaterial(inst, s));
            if (!m || !m->HasPass(GpuPass::Forward)) continue;
            translucent = true;
            refraction = refraction || m->usesRefraction;
        }
        if (!translucent) continue;
        const Math::Vec3 d = m_instanceCenters[i] - cameraPosition;
        draws.push_back({i, Math::Dot(d, d)});
    }
    if (draws.empty()) return;
    // Back to front (per object, as Unreal sorts translucency).
    std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) { return a.distance > b.distance; });

    auto imageBarrier = [&](VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
                            VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = srcAccess;
        b.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(m_commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    VkImageLayout lightLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (refraction) {
        // The scene behind the translucent surfaces, as refraction samples it.
        imageBarrier(m_gBuffer.lightAccum.image, lightLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
        imageBarrier(m_gBuffer.sceneColor.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.dstSubresource = copy.srcSubresource;
        copy.extent = {static_cast<u32>(m_gBuffer.width), static_cast<u32>(m_gBuffer.height), 1};
        vkCmdCopyImage(m_commandBuffer, m_gBuffer.lightAccum.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_gBuffer.sceneColor.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        imageBarrier(m_gBuffer.sceneColor.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        lightLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    imageBarrier(m_gBuffer.lightAccum.image, lightLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
                 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    // Depth is tested, never written: read-only, so the shaders can sample it.
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_gBuffer.lightAccum.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = m_gBuffer.depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {static_cast<u32>(m_gBuffer.width), static_cast<u32>(m_gBuffer.height)};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(m_commandBuffer, &rendering);
    const VkViewport viewport{0, 0, static_cast<f32>(m_gBuffer.width), static_cast<f32>(m_gBuffer.height), 0, 1};
    const VkRect2D scissor{{0, 0}, {static_cast<u32>(m_gBuffer.width), static_cast<u32>(m_gBuffer.height)}};
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
    BindMaterialSets(m_mainViewOffset);
    for (const Draw& d : draws) {
        DrawMaterialSections(source.instances[d.instance], m_instanceObjects[d.instance], GpuPass::Forward);
    }
    vkCmdEndRendering(m_commandBuffer);
    imageBarrier(m_gBuffer.lightAccum.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void VulkanDeferredRenderer::BindMaterialSets(u32 viewOffset) {
    const VkDescriptorSet sets[3] = {m_materialLayout->FrameSet(m_frameSlot), m_textures->Set(),
                                     m_materialLayout->ParamSet(m_frameSlot)};
    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_materialLayout->PipelineLayout(), 0, 3,
                            sets, 1, &viewOffset);
}

Math::Mat4 VulkanDeferredRenderer::NormalMatrix(const Math::Mat4& model) const {
    // Inverse-transpose of the upper 3x3: correct normals under non-uniform scale.
    return Math::Inverse(model);
}

void VulkanDeferredRenderer::DrawMaterialSections(const SceneInstance& inst, u32 objectId, GpuPass pass) {
    const GpuMesh* mesh = ResolveMesh(inst);
    if (!mesh) return;
    VulkanMaterialLayout::Push push{};
    push.model = inst.model;
    const Math::Mat4 inverse = NormalMatrix(inst.model);
    for (int col = 0; col < 3; ++col) {
        // column `col` of transpose(inverse) = row `col` of inverse
        for (int row = 0; row < 3; ++row) push.normalMatrix[col][row] = inverse.m[row * 4 + col];
        push.normalMatrix[col][3] = 0.0f;
    }
    push.objectId = objectId;
    const VkDeviceSize offset = 0;
    bool bound = false;
    for (u32 s = 0; s < mesh->sections.size(); ++s) {
        const MaterialHandle handle = SectionMaterial(inst, s);
        const CompiledMaterialGpu* material = m_materials->Drawable(handle);
        if (!material) continue;
        const VkPipeline pipeline = m_materialPipelines->Get(*material, pass);
        if (!pipeline) continue;  // the material doesn't draw in this pass
        if (!bound) {
            vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, &mesh->vertices.buffer, &offset);
            vkCmdBindIndexBuffer(m_commandBuffer, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            bound = true;
        }
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        push.paramOffset = m_materials->ParamOffset(handle);
        push.opacityMaskClip = material->opacityMaskClip;
        vkCmdPushConstants(m_commandBuffer, m_materialLayout->PipelineLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        const MeshSection& section = mesh->sections[s];
        vkCmdDrawIndexed(m_commandBuffer, section.indexCount, 1, section.indexOffset, 0, 0);
    }
}

void VulkanDeferredRenderer::ReleaseMaterial(MaterialHandle material) {
    m_materials->Release(material);
}

MeshHandle VulkanDeferredRenderer::PrimitiveMesh(MeshPrimitive primitive) const {
    const u32 i = static_cast<u32>(primitive);
    return i < kPrimitiveCount ? m_primitiveMeshes[i] : m_primitiveMeshes[0];
}

MaterialHandle VulkanDeferredRenderer::SectionMaterial(const SceneInstance& instance,
                                                       u32 section) {
    // Section s uses slot s; sections past the slot array reuse the last slot;
    // sections past the instance's count use the Default Material.
    if (section >= std::max(instance.sectionCount, 1u)) return kDefaultMaterialHandle;
    return instance.sectionMaterials[std::min(section, kMaxSectionMaterials - 1)];
}

const GpuMesh* VulkanDeferredRenderer::ResolveMesh(const SceneInstance& instance) const {
    if (instance.mesh != kInvalidHandle) {
        if (const GpuMesh* mesh = m_meshes->Get(instance.mesh)) return mesh;
    }
    return m_meshes->Get(PrimitiveMesh(instance.primitive));
}

bool VulkanDeferredRenderer::Initialize(int32_t width, int32_t height) {
    if (m_initialized) {
        LUMA_LOG_WARN("VulkanDeferredRenderer", "Already initialized");
        return true;
    }

    if (width <= 0 || height <= 0) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Invalid dimensions {}x{}",
                       width, height);
        return false;
    }

    LUMA_LOG_INFO("VulkanDeferredRenderer",
                  "Initializing deferred renderer ({}x{})", width, height);

    if (!CreateCommandPool()) return false;
    
    // Create fence for command buffer synchronization
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_fence) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create fence");
        return false;
    }
    
    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to allocate command buffer");
        return false;
    }
    
    if (!CreateGBuffer(width, height)) return false;
    if (!CreateShadowResources()) return false;
    // Sky light must exist before the lighting descriptor set references it.
    m_skyLight = std::make_unique<VulkanSkyLight>(m_physicalDevice, m_device, m_shaderDir);
    if (!m_skyLight->IsValid()) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Sky light capture failed to initialize");
        return false;
    }
    // Eye adaptation meters the HDR light buffer; must exist before the bloom
    // composite descriptor (which binds its exposure buffer) is written.
    m_eyeAdaptation = std::make_unique<VulkanEyeAdaptation>(
        m_physicalDevice, m_device, m_shaderDir, m_gBuffer.lightAccum.view,
        m_gBuffer.sampler);
    if (!m_eyeAdaptation->IsValid()) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Eye adaptation failed to initialize");
        return false;
    }
    if (!CreateEnvironmentTexture(m_useEnvironment
            ? std::string(LUMA_CONTENT_DIR) + "/skybox/braustuble_alley_2k.hdr"
            : std::string{})) {
        LUMA_LOG_WARN("VulkanDeferredRenderer",
                      "HDR environment unavailable; using fallback sky");
    }
    if (!CreateUBOs()) return false;
    if (!CreateLayouts()) return false;
    if (!CreateDescriptorSets()) return false;
    if (!CreatePipelines(m_shaderDir)) return false;
    if (!CreateFXAAPass()) return false;
    // The bloom chain also hosts the final tone-mapping composite. Don't fail
    // the renderer without it (the viewport stays usable, just untonemapped),
    // but report it loudly: the image will be wrong.
    if (!CreateBloomResources()) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer",
                       "Post-process (bloom/tone-map) init failed; viewport "
                       "will show untonemapped HDR");
        DestroyBloomResources();
        m_bloomEnabled = false;
    }

    // Editor ground grid: 1-sample targets matching the deferred pass formats.
    m_gridPass = std::make_unique<VulkanGridPass>(
        m_physicalDevice, m_device, m_shaderDir, VK_SAMPLE_COUNT_1_BIT,
        kLightFormat, kDepthFormat);
    // Editor overlay (transform gizmo), drawn onto the final display image.
    m_overlayPass = std::make_unique<VulkanOverlayLinePass>(
        m_physicalDevice, m_device, m_shaderDir, kLightFormat);
    // Selection outline over the final image (sized to the viewport).
    m_selectionPass = std::make_unique<VulkanSelectionOutlinePass>(
        m_physicalDevice, m_device, m_shaderDir, static_cast<u32>(width),
        static_cast<u32>(height), kLightFormat);
    RegisterMaterialTargets();

    // Register the FXAA-resolved texture with the UI pass (the viewport
    // displays this instead of the raw light accumulation buffer).
    if (m_textureHandle == 0) {
        m_textureHandle = m_uiPass.RegisterExternalTexture(
            m_fxaaResolved.view);
    } else {
        m_uiPass.UpdateExternalTexture(m_textureHandle,
                                       m_fxaaResolved.view);
    }
    m_registeredLightView = m_fxaaResolved.view;

    m_initialized = true;
    LUMA_LOG_INFO("VulkanDeferredRenderer",
                  "Initialized successfully (GBuffer: {}x{})", width, height);
    return true;
}

void VulkanDeferredRenderer::Cleanup() {
    if (!m_initialized && m_commandPool == VK_NULL_HANDLE) return;

    if (m_fence != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    m_gridPass.reset();
    m_overlayPass.reset();
    m_selectionPass.reset();
    m_skyLight.reset();
    DestroyBloomResources();
    m_eyeAdaptation.reset();
    DestroyFXAAPass();
    DestroyEnvironmentTexture();
    DestroyBuffer(m_device, m_lightingUBO);
    DestroyShadowResources();

    if (m_lightingPipeline)
        vkDestroyPipeline(m_device, m_lightingPipeline, nullptr);
    if (m_lightingLayout)
        vkDestroyPipelineLayout(m_device, m_lightingLayout, nullptr);
    m_lightingPipeline = VK_NULL_HANDLE;
    m_lightingLayout = VK_NULL_HANDLE;

    if (m_lightingPool)
        vkDestroyDescriptorPool(m_device, m_lightingPool, nullptr);
    if (m_lightingSetLayout)
        vkDestroyDescriptorSetLayout(m_device, m_lightingSetLayout, nullptr);
    m_lightingPool = VK_NULL_HANDLE;
    m_lightingSetLayout = VK_NULL_HANDLE;

    if (m_commandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &m_commandBuffer);
        m_commandBuffer = VK_NULL_HANDLE;
    }
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device, m_fence, nullptr);
        m_fence = VK_NULL_HANDLE;
    }

    if (m_gBuffer.framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_gBuffer.framebuffer, nullptr);
        m_gBuffer.framebuffer = VK_NULL_HANDLE;
    }
    if (m_gBuffer.renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_gBuffer.renderPass, nullptr);
        m_gBuffer.renderPass = VK_NULL_HANDLE;
    }
    if (m_gBuffer.lightingFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_gBuffer.lightingFramebuffer, nullptr);
        m_gBuffer.lightingFramebuffer = VK_NULL_HANDLE;
    }
    if (m_gBuffer.lightingRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_gBuffer.lightingRenderPass, nullptr);
        m_gBuffer.lightingRenderPass = VK_NULL_HANDLE;
    }

    DestroyAttachment(m_gBuffer.lightAccum);
    DestroyAttachment(m_gBuffer.depth);
    DestroyAttachment(m_gBuffer.material);
    DestroyAttachment(m_gBuffer.normal);
    DestroyAttachment(m_gBuffer.albedo);
    DestroyAttachment(m_gBuffer.emissive);
    DestroyAttachment(m_gBuffer.custom);
    DestroyAttachment(m_gBuffer.sceneColor);

    if (m_gBuffer.sampler)
        vkDestroySampler(m_device, m_gBuffer.sampler, nullptr);
    m_gBuffer.sampler = VK_NULL_HANDLE;

    if (m_commandPool)
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    m_commandBuffer = VK_NULL_HANDLE;
    m_fence = VK_NULL_HANDLE;
    m_commandPool = VK_NULL_HANDLE;

    m_initialized = false;
    m_gBufferInitialized = false;
}

bool VulkanDeferredRenderer::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create command pool");
        return false;
    }

    return true;
}

bool VulkanDeferredRenderer::CreateGBuffer(int32_t width, int32_t height) {
    m_gBuffer.width = width;
    m_gBuffer.height = height;

    // Create MRT attachments
    if (!CreateAttachment(kAlbedoFormat,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          &m_gBuffer.albedo)) {
        return false;
    }
    if (!CreateAttachment(kEmissiveFormat,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          &m_gBuffer.emissive)) {
        return false;
    }
    if (!CreateAttachment(VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          &m_gBuffer.normal)) {
        return false;
    }
    if (!CreateAttachment(VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          &m_gBuffer.material)) {
        return false;
    }
    if (!CreateAttachment(VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          &m_gBuffer.custom)) {
        return false;
    }
    if (!CreateAttachment(VK_FORMAT_D32_SFLOAT,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT,
                          &m_gBuffer.depth)) {
        return false;
    }
    // HDR light accumulation (scene-referred linear radiance).
    if (!CreateAttachment(kLightFormat,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                          &m_gBuffer.lightAccum)) {
        return false;
    }
    // Its copy for refraction (translucent materials read what is behind them).
    if (!CreateAttachment(kLightFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          &m_gBuffer.sceneColor)) {
        return false;
    }

    if (!CreateRenderPass()) return false;
    if (!CreateFramebuffer()) return false;
    if (!CreateLightingRenderPass()) return false;
    if (!CreateLightingFramebuffer()) return false;
    if (!CreateSampler()) return false;

    return true;
}

bool VulkanDeferredRenderer::CreateRenderPass() {
    // MRT attachments: base color, normal, material, emissive, custom data, depth
    std::array<VkAttachmentDescription, 6> attachments{};

    // Base color + shading model
    attachments[0].format = kAlbedoFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Normal
    attachments[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Material
    attachments[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Emissive radiance
    attachments[3].format = kEmissiveFormat;
    attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[3].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Shading-model custom data (RT4)
    attachments[4] = attachments[2];

    // Depth
    attachments[5].format = VK_FORMAT_D32_SFLOAT;
    attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[5].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[5].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[5].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[5].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentReference, 5> colorReferences{};
    colorReferences[0].attachment = 0; // albedo
    colorReferences[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorReferences[1].attachment = 1; // normal
    colorReferences[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorReferences[2].attachment = 2; // material
    colorReferences[2].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorReferences[3].attachment = 3; // emissive
    colorReferences[3].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorReferences[4].attachment = 4; // custom data
    colorReferences[4].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 5;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pColorAttachments = colorReferences.data();
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
    subpass.pDepthStencilAttachment = &depthReference;

    // Subpass dependencies
    std::array<VkSubpassDependency, 2> dependencies{};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_gBuffer.renderPass) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create render pass");
        return false;
    }

    return true;
}

bool VulkanDeferredRenderer::CreateLightingRenderPass() {
    VkAttachmentDescription attachment{};
    attachment.format = kLightFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    return vkCreateRenderPass(m_device, &info, nullptr,
                              &m_gBuffer.lightingRenderPass) == VK_SUCCESS;
}

bool VulkanDeferredRenderer::CreateLightingFramebuffer() {
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = m_gBuffer.lightingRenderPass;
    info.attachmentCount = 1;
    info.pAttachments = &m_gBuffer.lightAccum.view;
    info.width = static_cast<u32>(m_gBuffer.width);
    info.height = static_cast<u32>(m_gBuffer.height);
    info.layers = 1;
    return vkCreateFramebuffer(m_device, &info, nullptr,
                               &m_gBuffer.lightingFramebuffer) == VK_SUCCESS;
}

bool VulkanDeferredRenderer::CreateAttachment(VkFormat format, VkImageUsageFlags usage,
                          Attachment* attachment, i32 width, i32 height) {
    // Non-positive extent = full viewport (G-Buffer) size.
    const i32 w = width > 0 ? width : m_gBuffer.width;
    const i32 h = height > 0 ? height : m_gBuffer.height;
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<u32>(w);
    imageInfo.extent.height = static_cast<u32>(h);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &attachment->image) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create image");
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_device, attachment->image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(m_physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &attachment->memory) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to allocate image memory");
        vkDestroyImage(m_device, attachment->image, nullptr);
        return false;
    }

    vkBindImageMemory(m_device, attachment->image, attachment->memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = attachment->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (format == VK_FORMAT_D32_SFLOAT) {
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &attachment->view) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create image view");
        return false;
    }

    attachment->format = format;
    return true;
}

void VulkanDeferredRenderer::DestroyAttachment(Attachment& attachment) {
    if (attachment.view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, attachment.view, nullptr);
        attachment.view = VK_NULL_HANDLE;
    }
    if (attachment.image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, attachment.image, nullptr);
        attachment.image = VK_NULL_HANDLE;
    }
    if (attachment.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, attachment.memory, nullptr);
        attachment.memory = VK_NULL_HANDLE;
    }
}

bool VulkanDeferredRenderer::CreateFramebuffer() {
    std::array<VkImageView, 6> attachments{};
    attachments[0] = m_gBuffer.albedo.view;
    attachments[1] = m_gBuffer.normal.view;
    attachments[2] = m_gBuffer.material.view;
    attachments[3] = m_gBuffer.emissive.view;
    attachments[4] = m_gBuffer.custom.view;
    attachments[5] = m_gBuffer.depth.view;

    VkFramebufferCreateInfo fbufInfo{};
    fbufInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbufInfo.renderPass = m_gBuffer.renderPass;
    fbufInfo.pAttachments = attachments.data();
    fbufInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbufInfo.width = m_gBuffer.width;
    fbufInfo.height = m_gBuffer.height;
    fbufInfo.layers = 1;

    if (vkCreateFramebuffer(m_device, &fbufInfo, nullptr, &m_gBuffer.framebuffer) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create framebuffer");
        return false;
    }

    return true;
}

bool VulkanDeferredRenderer::CreateSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_gBuffer.sampler) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create sampler");
        return false;
    }

    return true;
}

bool VulkanDeferredRenderer::CreateEnvironmentTexture(const std::string& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    float* hdr = path.empty()
        ? nullptr
        : stbi_loadf(path.c_str(), &width, &height, &channels, STBI_rgb);
    if (path.empty() || !hdr || width <= 0 || height <= 0) {
        if (hdr) stbi_image_free(hdr);
        width = 1;
        height = 1;
    }

    std::vector<u16> pixels(static_cast<size_t>(width) * height * 4);
    if (hdr) {
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
            // Preserve scene-linear HDR values for material illumination.
            pixels[i * 4 + 0] = FloatToHalf(std::max(hdr[i * 3 + 0], 0.0f));
            pixels[i * 4 + 1] = FloatToHalf(std::max(hdr[i * 3 + 1], 0.0f));
            pixels[i * 4 + 2] = FloatToHalf(std::max(hdr[i * 3 + 2], 0.0f));
            pixels[i * 4 + 3] = FloatToHalf(1.0f);
        }
        stbi_image_free(hdr);
    } else {
        pixels = {FloatToHalf(0.12f), FloatToHalf(0.15f),
                  FloatToHalf(0.20f), FloatToHalf(1.0f)};
        if (!path.empty()) {
            LUMA_LOG_WARN("VulkanDeferredRenderer",
                          "Failed to load HDR environment: {}", path);
        }
    }

    const VkDeviceSize imageSize = pixels.size() * sizeof(u16);
    GpuBuffer staging = CreateBuffer(
        m_physicalDevice, m_device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
    if (staging.buffer == VK_NULL_HANDLE) return false;
    std::memcpy(staging.mapped, pixels.data(), pixels.size());

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {static_cast<u32>(width), static_cast<u32>(height), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_environmentImage) != VK_SUCCESS) {
        DestroyBuffer(m_device, staging);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, m_environmentImage, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = FindMemoryType(
        m_physicalDevice, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &allocation, nullptr,
                         &m_environmentMemory) != VK_SUCCESS) {
        DestroyBuffer(m_device, staging);
        DestroyEnvironmentTexture();
        return false;
    }
    vkBindImageMemory(m_device, m_environmentImage, m_environmentMemory, 0);

    VkCommandBuffer cmd = BeginOneTimeCommands(m_device, m_commandPool);
    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = m_environmentImage;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toTransfer);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<u32>(width), static_cast<u32>(height), 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, m_environmentImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    VkImageMemoryBarrier toShader = toTransfer;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &toShader);
    EndOneTimeCommands(m_device, m_commandPool, m_graphicsQueue, cmd);
    DestroyBuffer(m_device, staging);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_environmentImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(m_device, &viewInfo, nullptr,
                          &m_environmentView) != VK_SUCCESS) {
        DestroyEnvironmentTexture();
        return false;
    }

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = 0.0f;
    if (vkCreateSampler(m_device, &sampler, nullptr,
                        &m_environmentSampler) != VK_SUCCESS) {
        DestroyEnvironmentTexture();
        return false;
    }
    return true;
}

void VulkanDeferredRenderer::DestroyEnvironmentTexture() {
    if (m_environmentSampler) vkDestroySampler(m_device, m_environmentSampler, nullptr);
    if (m_environmentView) vkDestroyImageView(m_device, m_environmentView, nullptr);
    if (m_environmentImage) vkDestroyImage(m_device, m_environmentImage, nullptr);
    if (m_environmentMemory) vkFreeMemory(m_device, m_environmentMemory, nullptr);
    m_environmentSampler = VK_NULL_HANDLE;
    m_environmentView = VK_NULL_HANDLE;
    m_environmentImage = VK_NULL_HANDLE;
    m_environmentMemory = VK_NULL_HANDLE;
}

// --- FXAA Anti-Aliasing Pass ------------------------------------------------
// Reads from lightAccum (the lit, tone-mapped scene) and writes to a resolved
// texture that is registered with the UI pass. FXAA smooths geometry edges
// and texture seams without the cost of multi-sample rendering.

bool VulkanDeferredRenderer::CreateFXAAPass() {
    // 1. Create the resolved output texture (same format/size as lightAccum)
    // Transfer source: ReadFinalImage copies it back (thumbnails, captures).
    if (!CreateAttachment(kLightFormat,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                          &m_fxaaResolved)) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create FXAA resolved texture");
        return false;
    }

    // 2. Create a simple single-attachment render pass (color output only)
    //    lightAccum is transitioned to SHADER_READ before this pass, then
    //    sampled as a combined image sampler (not a subpass input attachment).
    VkAttachmentDescription colorAtt{};
    colorAtt.format = kLightFormat;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAtt;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;
    if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_fxaaRenderPass) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create FXAA render pass");
        return false;
    }

    // 3. Create the FXAA framebuffer (single output attachment)
    VkFramebufferCreateInfo fboInfo{};
    fboInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fboInfo.renderPass = m_fxaaRenderPass;
    fboInfo.attachmentCount = 1;
    fboInfo.pAttachments = &m_fxaaResolved.view;
    fboInfo.width = static_cast<u32>(m_gBuffer.width);
    fboInfo.height = static_cast<u32>(m_gBuffer.height);
    fboInfo.layers = 1;
    if (vkCreateFramebuffer(m_device, &fboInfo, nullptr, &m_fxaaFramebuffer) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create FXAA framebuffer");
        return false;
    }

    // 4. Descriptor set layout (combined image sampler + push constants)
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 1;
    dslInfo.pBindings = &binding;
    vkCreateDescriptorSetLayout(m_device, &dslInfo, nullptr, &m_fxaaSetLayout);

    // 5. Create pipeline layout
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(Math::Vec2);
    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_fxaaSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_fxaaLayout);

    // 7. Create descriptor pool + set
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_fxaaPool);

    VkDescriptorSetAllocateInfo setAlloc{};
    setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAlloc.descriptorPool = m_fxaaPool;
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts = &m_fxaaSetLayout;
    vkAllocateDescriptorSets(m_device, &setAlloc, &m_fxaaSet);

    PointFXAAAt(m_gBuffer.lightAccum.view);

    // 7. Create FXAA pipeline (fullscreen triangle, no vertex input)
    VkShaderModule vertMod = LoadShaderModule(m_device, m_shaderDir + "/deferred_lighting.vert.spv");
    VkShaderModule fragMod = LoadShaderModule(m_device, m_shaderDir + "/fxaa.frag.spv");
    if (!vertMod || !fragMod) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to load FXAA shaders");
        return false;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
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
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkFormat colorFmt = kLightFormat;
    VkPipelineRenderingCreateInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &colorFmt;

    VkGraphicsPipelineCreateInfo gpInfo{};
    gpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpInfo.pNext = &ri;
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &vin;
    gpInfo.pInputAssemblyState = &ia;
    gpInfo.pViewportState = &vp;
    gpInfo.pRasterizationState = &rs;
    gpInfo.pMultisampleState = &ms;
    gpInfo.pDepthStencilState = &ds;
    gpInfo.pColorBlendState = &cb;
    gpInfo.pDynamicState = &dyn;
    gpInfo.layout = m_fxaaLayout;
    gpInfo.renderPass = m_fxaaRenderPass;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &m_fxaaPipeline) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create FXAA pipeline");
        m_fxaaPipeline = VK_NULL_HANDLE;
    }
    vkDestroyShaderModule(m_device, vertMod, nullptr);
    vkDestroyShaderModule(m_device, fragMod, nullptr);

    LUMA_LOG_INFO("VulkanDeferredRenderer", "FXAA pass created");
    return true;
}

void VulkanDeferredRenderer::DestroyFXAAPass() {
    if (m_fxaaPipeline) vkDestroyPipeline(m_device, m_fxaaPipeline, nullptr);
    if (m_fxaaLayout) vkDestroyPipelineLayout(m_device, m_fxaaLayout, nullptr);
    if (m_fxaaPool) vkDestroyDescriptorPool(m_device, m_fxaaPool, nullptr);
    if (m_fxaaSetLayout) vkDestroyDescriptorSetLayout(m_device, m_fxaaSetLayout, nullptr);
    if (m_fxaaFramebuffer) vkDestroyFramebuffer(m_device, m_fxaaFramebuffer, nullptr);
    if (m_fxaaRenderPass) vkDestroyRenderPass(m_device, m_fxaaRenderPass, nullptr);
    DestroyAttachment(m_fxaaResolved);
    m_fxaaPipeline = VK_NULL_HANDLE;
    m_fxaaLayout = VK_NULL_HANDLE;
    m_fxaaPool = VK_NULL_HANDLE;
    m_fxaaSetLayout = VK_NULL_HANDLE;
    m_fxaaFramebuffer = VK_NULL_HANDLE;
    m_fxaaRenderPass = VK_NULL_HANDLE;
    m_fxaaSet = VK_NULL_HANDLE;
}

void VulkanDeferredRenderer::PointFXAAAt(VkImageView sourceView) {
    if (m_fxaaSet == VK_NULL_HANDLE || sourceView == VK_NULL_HANDLE ||
        m_gBuffer.sampler == VK_NULL_HANDLE) {
        return;
    }
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = sourceView;
    imageInfo.sampler = m_gBuffer.sampler;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_fxaaSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

bool VulkanDeferredRenderer::CreateUBOs() {
    m_lightingUBO = CreateBuffer(m_physicalDevice, m_device, sizeof(LightingUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    return m_lightingUBO.buffer != VK_NULL_HANDLE;
}

bool VulkanDeferredRenderer::CreateLayouts() {
    VkDescriptorSetLayoutBinding lb[11]{};
    for (u32 i = 0; i < 4; ++i) { lb[i].binding = i; lb[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; lb[i].descriptorCount = 1; lb[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; }
    lb[4].binding = 4; lb[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; lb[4].descriptorCount = 1; lb[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lb[5].binding = 5; lb[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; lb[5].descriptorCount = 1; lb[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lb[6].binding = 6; lb[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; lb[6].descriptorCount = 1; lb[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // Sky light: 7 = GGX-prefiltered sky cube, 8 = diffuse SH buffer.
    lb[7].binding = 7; lb[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; lb[7].descriptorCount = 1; lb[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lb[8].binding = 8; lb[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; lb[8].descriptorCount = 1; lb[8].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // 9 = emissive G-buffer target (RT3).
    lb[9].binding = 9; lb[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; lb[9].descriptorCount = 1; lb[9].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // 10 = shading-model data G-buffer target (RT4).
    lb[10].binding = 10; lb[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; lb[10].descriptorCount = 1; lb[10].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 11; li.pBindings = lb;
    if (vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_lightingSetLayout) != VK_SUCCESS) return false;

    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &m_lightingSetLayout;
    return vkCreatePipelineLayout(m_device, &pl, nullptr, &m_lightingLayout) == VK_SUCCESS;
}

bool VulkanDeferredRenderer::CreateDescriptorSets() {
    VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pi.maxSets = 1; pi.poolSizeCount = 3; pi.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_lightingPool) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ai.descriptorPool = m_lightingPool; ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_lightingSetLayout;
    if (vkAllocateDescriptorSets(m_device, &ai, &m_lightingSet) != VK_SUCCESS) return false;
    VkDescriptorBufferInfo lb{m_lightingUBO.buffer, 0, sizeof(LightingUBO)};
    VkDescriptorImageInfo shadow{m_shadowSampler, m_shadowArrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo environment{m_environmentSampler, m_environmentView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo g[4] = {{m_gBuffer.sampler, m_gBuffer.albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {m_gBuffer.sampler, m_gBuffer.normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {m_gBuffer.sampler, m_gBuffer.material.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {m_gBuffer.sampler, m_gBuffer.depth.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet writes[7]{};
    for (u32 i = 0; i < 4; ++i) { writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[i].dstSet = m_lightingSet; writes[i].dstBinding = i; writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[i].descriptorCount = 1; writes[i].pImageInfo = &g[i]; }
    writes[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[4].dstSet = m_lightingSet; writes[4].dstBinding = 4; writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[4].descriptorCount = 1; writes[4].pBufferInfo = &lb;
    writes[5] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[5].dstSet = m_lightingSet; writes[5].dstBinding = 5; writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[5].descriptorCount = 1; writes[5].pImageInfo = &shadow;
    writes[6] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[6].dstSet = m_lightingSet; writes[6].dstBinding = 6; writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[6].descriptorCount = 1; writes[6].pImageInfo = &environment;
    VkDescriptorImageInfo skyCube{m_skyLight->Sampler(), m_skyLight->FilteredCubeView(),
                                  VulkanSkyLight::kCubeLayout};
    VkDescriptorBufferInfo skySH{m_skyLight->SHBuffer(), 0, m_skyLight->SHBufferSize()};
    VkWriteDescriptorSet skyWrites[2]{};
    skyWrites[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; skyWrites[0].dstSet = m_lightingSet; skyWrites[0].dstBinding = 7; skyWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; skyWrites[0].descriptorCount = 1; skyWrites[0].pImageInfo = &skyCube;
    skyWrites[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; skyWrites[1].dstSet = m_lightingSet; skyWrites[1].dstBinding = 8; skyWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; skyWrites[1].descriptorCount = 1; skyWrites[1].pBufferInfo = &skySH;
    vkUpdateDescriptorSets(m_device, 7, writes, 0, nullptr);
    VkDescriptorImageInfo emissiveInfo{m_gBuffer.sampler, m_gBuffer.emissive.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet emissiveWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; emissiveWrite.dstSet = m_lightingSet; emissiveWrite.dstBinding = 9; emissiveWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; emissiveWrite.descriptorCount = 1; emissiveWrite.pImageInfo = &emissiveInfo;
    vkUpdateDescriptorSets(m_device, 1, &emissiveWrite, 0, nullptr);
    VkDescriptorImageInfo customInfo{m_gBuffer.sampler, m_gBuffer.custom.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet customWrite = emissiveWrite;
    customWrite.dstBinding = 10;
    customWrite.pImageInfo = &customInfo;
    vkUpdateDescriptorSets(m_device, 1, &customWrite, 0, nullptr);
    vkUpdateDescriptorSets(m_device, 2, skyWrites, 0, nullptr);
    return true;
}

bool VulkanDeferredRenderer::CreatePipelines(const std::string& shaderDir) {
    VkShaderModule lv = LoadShaderModule(m_device, shaderDir + "/deferred_lighting.vert.spv");
    VkShaderModule lf = LoadShaderModule(m_device, shaderDir + "/deferred_lighting.frag.spv");
    if (!lv || !lf) {
        if (lv) vkDestroyShaderModule(m_device, lv, nullptr);
        if (lf) vkDestroyShaderModule(m_device, lf, nullptr);
        return false;
    }
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dyn;
    VkPipelineShaderStageCreateInfo ls[2]{};
    ls[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, lv, "main", nullptr}; ls[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, lf, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo noVin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo lrs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; lrs.cullMode = VK_CULL_MODE_NONE; lrs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; lrs.polygonMode = VK_POLYGON_MODE_FILL; lrs.lineWidth = 1.0f;
    VkPipelineDepthStencilStateCreateInfo lds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState lb{}; lb.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo lcb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; lcb.attachmentCount = 1; lcb.pAttachments = &lb;
    VkGraphicsPipelineCreateInfo li{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; li.stageCount = 2; li.pStages = ls; li.pVertexInputState = &noVin; li.pInputAssemblyState = &ia; li.pViewportState = &vp; li.pRasterizationState = &lrs; li.pMultisampleState = &ms; li.pDepthStencilState = &lds; li.pColorBlendState = &lcb; li.pDynamicState = &dynamic; li.layout = m_lightingLayout; li.renderPass = m_gBuffer.lightingRenderPass;
    bool ok = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &li, nullptr, &m_lightingPipeline) == VK_SUCCESS;
    vkDestroyShaderModule(m_device, lv, nullptr); vkDestroyShaderModule(m_device, lf, nullptr);
    return ok;
}

bool VulkanDeferredRenderer::CreateShadowResources() {
    // Cascaded sun shadow map: one kShadowSize^2 depth layer per cascade.
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D; info.format = kShadowFormat;
    info.extent = {kShadowSize, kShadowSize, 1}; info.mipLevels = 1; info.arrayLayers = kCascades;
    info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(m_device, &info, nullptr, &m_shadowImage) != VK_SUCCESS) return false;
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(m_device, m_shadowImage, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize = req.size; alloc.memoryTypeIndex = FindMemoryType(m_physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &alloc, nullptr, &m_shadowMem) != VK_SUCCESS) return false;
    vkBindImageMemory(m_device, m_shadowImage, m_shadowMem, 0);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = m_shadowImage; view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; view.format = kShadowFormat; view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kCascades};
    if (vkCreateImageView(m_device, &view, nullptr, &m_shadowArrayView) != VK_SUCCESS) return false;
    // Single-layer views: render targets for each cascade's depth pass.
    for (u32 i = 0; i < kCascades; ++i) {
        VkImageViewCreateInfo layer{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        layer.image = m_shadowImage;
        layer.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layer.format = kShadowFormat;
        layer.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, i, 1};
        if (vkCreateImageView(m_device, &layer, nullptr, &m_shadowLayerViews[i]) != VK_SUCCESS) return false;
    }
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sampler.magFilter = VK_FILTER_LINEAR; sampler.minFilter = VK_FILTER_LINEAR; sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    return vkCreateSampler(m_device, &sampler, nullptr, &m_shadowSampler) == VK_SUCCESS;
}

void VulkanDeferredRenderer::DestroyShadowResources() {
    for (VkImageView& layer : m_shadowLayerViews) {
        if (layer) vkDestroyImageView(m_device, layer, nullptr);
        layer = VK_NULL_HANDLE;
    }
    if (m_shadowSampler) vkDestroySampler(m_device, m_shadowSampler, nullptr);
    if (m_shadowArrayView) vkDestroyImageView(m_device, m_shadowArrayView, nullptr);
    if (m_shadowImage) vkDestroyImage(m_device, m_shadowImage, nullptr);
    if (m_shadowMem) vkFreeMemory(m_device, m_shadowMem, nullptr);
    m_shadowSampler = VK_NULL_HANDLE; m_shadowArrayView = VK_NULL_HANDLE; m_shadowImage = VK_NULL_HANDLE; m_shadowMem = VK_NULL_HANDLE;
    m_shadowImageInitialized = false;
}

void VulkanDeferredRenderer::RecordSunShadows(const SceneView& source,
                                              const Renderer2::SunCascades& cascades,
                                              bool enabled) {
    auto layerBarrier = [&](VkImageLayout oldL, VkImageLayout newL,
                            VkAccessFlags src, VkAccessFlags dst,
                            VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = oldL; b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_shadowImage;
        b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kCascades};
        b.srcAccessMask = src; b.dstAccessMask = dst;
        vkCmdPipelineBarrier(m_commandBuffer, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // Contents are fully rewritten every frame, so the old layout is discarded.
    layerBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

    const VkViewport viewport{0, 0, static_cast<f32>(kShadowSize),
                              static_cast<f32>(kShadowSize), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {kShadowSize, kShadowSize}};
    for (u32 c = 0; c < kCascades; ++c) {
        // Every layer is cleared (far = no occluder) even when shadows are off
        // or the cascade is unused, so sampling is always well-defined.
        VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depth.imageView = m_shadowLayerViews[c];
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = {kShadowSize, kShadowSize};
        rendering.layerCount = 1;
        rendering.pDepthAttachment = &depth;
        vkCmdBeginRendering(m_commandBuffer, &rendering);

        if (enabled && c < cascades.count) {
            vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
            BindMaterialSets(m_cascadeViewOffsets[c]);
            for (u32 i = 0; i < source.instanceCount; ++i) {
                DrawMaterialSections(source.instances[i], m_instanceObjects[i], GpuPass::DepthOnly);
            }
        }
        vkCmdEndRendering(m_commandBuffer);
    }

    layerBarrier(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void VulkanDeferredRenderer::SetViewportDimensions(int32_t width, int32_t height) {
    if (m_gBuffer.width != width || m_gBuffer.height != height) {
        LUMA_LOG_INFO("VulkanDeferredRenderer", "Resizing G-buffer from {}x{} to {}x{}", 
                     m_gBuffer.width, m_gBuffer.height, width, height);
        Cleanup();
        Initialize(width, height);
    }
}

void VulkanDeferredRenderer::PrepareScene() {
    // Prepare scene data for rendering
}

void VulkanDeferredRenderer::RenderScene(const Renderer2::DeferredSceneView& scene) {
    if (!m_initialized || !scene.sceneData) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Cannot render - not initialized");
        return;
    }
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, std::numeric_limits<u64>::max());
    vkResetFences(m_device, 1, &m_fence);
    vkResetCommandBuffer(m_commandBuffer, 0);

    // The previous frame has finished: content released since is unused now,
    // and this frame's material table copy can be published.
    m_meshes->CollectGarbage();
    m_textures->CollectGarbage();
    m_frameSlot = (m_frameSlot + 1) % VulkanMaterialLayout::kFramesInFlight;
    m_materialPipelines->EndFrame();
    m_materialLayout->BeginFrame(m_frameSlot);
    m_materials->Flush(*m_materialLayout, *m_textures);

    const SceneView& source = *scene.sceneData;
    // Per-object data (bounds, world-to-local) for every instance.
    m_instanceObjects.assign(source.instanceCount, 0u);
    m_instanceCenters.assign(source.instanceCount, Math::Vec3{0.0f, 0.0f, 0.0f});
    for (u32 i = 0; i < source.instanceCount; ++i) {
        const SceneInstance& inst = source.instances[i];
        VulkanMaterialLayout::ObjectData object{};
        object.worldToLocal = Math::Inverse(inst.model);
        if (const GpuMesh* mesh = ResolveMesh(inst)) {
            const Math::Vec3 center = (mesh->boundsMin + mesh->boundsMax) * 0.5f;
            const Math::Vec3 extent = (mesh->boundsMax - mesh->boundsMin) * 0.5f;
            const f32* m = inst.model.m;  // column-major
            const Math::Vec3 worldCenter{m[0] * center.x + m[4] * center.y + m[8] * center.z + m[12],
                                         m[1] * center.x + m[5] * center.y + m[9] * center.z + m[13],
                                         m[2] * center.x + m[6] * center.y + m[10] * center.z + m[14]};
            const f32 scale = std::max({Math::Length(Math::Vec3{inst.model.m[0], inst.model.m[1], inst.model.m[2]}),
                                        Math::Length(Math::Vec3{inst.model.m[4], inst.model.m[5], inst.model.m[6]}),
                                        Math::Length(Math::Vec3{inst.model.m[8], inst.model.m[9], inst.model.m[10]})});
            SetVec3(object.boundsCenterRadius, worldCenter, Math::Length(extent) * scale);
            m_instanceCenters[i] = worldCenter;
            SetVec3(object.boundsExtent, extent, 0.0f);
        }
        m_instanceObjects[i] = m_materialLayout->AddObject(object);
    }
    const f32 width = static_cast<f32>(std::max(m_gBuffer.width, 1));
    const f32 height = static_cast<f32>(std::max(m_gBuffer.height, 1));
    VulkanMaterialLayout::FrameData view{};
    view.view = scene.viewMatrix;
    view.proj = scene.projectionMatrix;
    view.viewProj = scene.viewProjectionMatrix;
    view.invViewProj = Math::Inverse(scene.viewProjectionMatrix);
    view.prevViewProj = scene.viewProjectionMatrix;
    SetVec3(view.camPos, scene.cameraPosition);
    view.time[0] = scene.time;
    view.time[1] = scene.deltaTime;
    view.time[2] = static_cast<f32>(m_frameIndex++);
    view.viewport[0] = width;
    view.viewport[1] = height;
    view.viewport[2] = 1.0f / width;
    view.viewport[3] = 1.0f / height;
    m_mainViewOffset = m_materialLayout->AddView(view);

    LightingUBO lighting{};
    lighting.invViewProj = Math::Inverse(scene.viewProjectionMatrix);
    SetVec3(lighting.camPos, scene.cameraPosition);
    SetVec3(lighting.camForward, scene.cameraDirection);
    const LightingParams fallback{};
    const LightingParams& params = scene.lightingParams ? *scene.lightingParams : fallback;
    SetVec3(lighting.sunDir, params.sunDirection);
    SetVec3(lighting.sunColor, params.sunColor, params.sunIntensity);
    SetVec3(lighting.skyZenith, params.skyZenith);
    SetVec3(lighting.skyHorizon, params.skyHorizon, params.iblIntensity);
    SetVec3(lighting.groundColor, params.groundColor);
    FillAtmosphereParams(lighting.atmo, source.sky);
    lighting.params[0] = static_cast<f32>(
        std::min(source.lightCount, kMaxLights));

    // Cascaded sun shadows: fit stable cascades to the camera frustum (capped
    // at the shadow distance). The depth passes are recorded below.
    Renderer2::SunCascadeInput cascadeIn;
    cascadeIn.view = scene.viewMatrix;
    cascadeIn.fovYRadians = scene.fov;
    cascadeIn.aspect = static_cast<f32>(std::max(m_gBuffer.width, 1)) /
                       static_cast<f32>(std::max(m_gBuffer.height, 1));
    cascadeIn.nearZ = scene.nearPlane;
    cascadeIn.farZ = std::min(scene.farPlane, params.shadowDistance);
    cascadeIn.sunDirection = params.sunDirection;
    cascadeIn.cascadeCount = std::min(params.cascadeCount, kCascades);
    cascadeIn.splitLambda = params.cascadeSplitLambda;
    cascadeIn.shadowMapSize = kShadowSize;
    cascadeIn.casterDistance = std::max(params.shadowDistance, 50.0f);
    const Renderer2::SunCascades cascades = Renderer2::FitSunCascades(cascadeIn);
    // No shadows once the sun is below the horizon (the sky is dark anyway).
    const bool sunShadows = params.sunShadows && cascades.count > 0 &&
                            Math::Normalize(params.sunDirection).y > -0.05f;

    for (u32 c = 0; c < kCascades; ++c) {
        // A shadow view: the sun's matrix; camera data stays the main view's
        // so World Position Offset matches what the camera sees.
        VulkanMaterialLayout::FrameData cascadeView = view;
        cascadeView.viewProj = c < cascades.count ? cascades.viewProj[c] : Math::Mat4::Identity();
        cascadeView.invViewProj = Math::Inverse(cascadeView.viewProj);
        m_cascadeViewOffsets[c] = m_materialLayout->AddView(cascadeView);
    }
    m_materialLayout->EndFrame();

    lighting.params[1] = sunShadows ? 1.0f : 0.0f;
    lighting.params[2] = 1.0f / static_cast<f32>(kShadowSize);
    lighting.params[3] = m_useEnvironment ? 1.0f : 0.0f;
    lighting.shadowParams[0] = params.shadowSoftness;
    lighting.shadowParams[1] = static_cast<f32>(cascades.count);
    lighting.shadowParams[2] = params.shadowBias;
    lighting.shadowParams[3] = params.normalBias;
    for (u32 c = 0; c < kCascadesUBO; ++c) {
        const bool used = c < cascades.count;
        lighting.cascadeViewProj[c] = used ? cascades.viewProj[c] : Math::Mat4::Identity();
        lighting.cascadeSplits[c] = used ? cascades.splitFar[c] : 0.0f;
        lighting.cascadeTexel[c] = used ? cascades.texelWorldSize[c] : 0.0f;
    }
    // Sky light ambient comes from the captured atmosphere whenever the sky is
    // on (the HDR-environment thumbnail path keeps its own environment map).
    const bool skyLightOn = source.sky.enabled && !m_useEnvironment;
    lighting.skyLight[0] = skyLightOn ? 1.0f : 0.0f;
    lighting.skyLight[1] = static_cast<f32>(VulkanSkyLight::kMipCount - 1);
    // z = viewport view mode (deferred_lighting.frag); the composite pass
    // bypasses exposure / tone mapping / bloom for the visualizations.
    lighting.skyLight[2] = static_cast<f32>(static_cast<u32>(source.viewMode));
    m_viewMode = source.viewMode;
    for (u32 i = 0; i < std::min(source.lightCount, kMaxLights); ++i) {
        const SceneLight& src = source.lights[i];
        GpuLight& dst = lighting.lights[i];
        SetVec3(dst.posType, src.position, static_cast<f32>(src.type));
        SetVec3(dst.dirRange, Math::Normalize(src.direction), src.range);
        SetVec3(dst.color, src.color, src.intensity);
        dst.spot[0] = src.cosInner;
        dst.spot[1] = src.cosOuter;
        dst.spot[2] = std::max(src.falloffExponent, 0.0f);
    }
    std::memcpy(m_lightingUBO.mapped, &lighting, sizeof(lighting));
    {
        // Scene textures: depth for selection; the rest get real buffers with
        // translucency / post-process (white until then).
        const VkImageView white = m_textures->WhiteView();
        m_materialLayout->SetSceneTextures(m_frameSlot,
                                           {m_gBuffer.sceneColor.view, m_gBuffer.depth.view, m_gBuffer.albedo.view, m_gBuffer.normal.view,
                                            m_gBuffer.material.view, m_gBuffer.custom.view, white},
                                           m_gBuffer.sampler);
        VulkanMaterialLayout::Lighting l;
        l.ubo = m_lightingUBO.buffer;
        l.uboSize = sizeof(LightingUBO);
        l.shadowMap = m_shadowArrayView;
        l.shadowSampler = m_shadowSampler;
        l.environment = m_environmentView;
        l.environmentSampler = m_environmentSampler;
        l.skyCube = m_skyLight->FilteredCubeView();
        l.skySampler = m_skyLight->Sampler();
        l.skyLayout = VulkanSkyLight::kCubeLayout;
        l.skySH = m_skyLight->SHBuffer();
        l.skySHSize = m_skyLight->SHBufferSize();
        m_materialLayout->SetLighting(m_frameSlot, l);
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to begin command buffer");
        return;
    }
    // Sky light capture (compute; records nothing unless the sky changed).
    // Always called so the first frame initializes the cubes before lighting
    // samples them.
    m_skyLight->Record(m_commandBuffer, lighting.atmo, scene.cameraPosition);
    auto barrier = [&](VkImage image, VkImageAspectFlags aspect, VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = oldLayout; b.newLayout = newLayout; b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = image; b.subresourceRange = {aspect, 0, 1, 0, 1}; b.srcAccessMask = srcAccess; b.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(m_commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    {
        const VkImageLayout oldColorLayout = m_gBufferInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        const VkAccessFlags oldColorAccess = m_gBufferInitialized ? VK_ACCESS_SHADER_READ_BIT : 0;
        const VkPipelineStageFlags oldColorStage = m_gBufferInitialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        barrier(m_gBuffer.albedo.image, VK_IMAGE_ASPECT_COLOR_BIT, oldColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, oldColorAccess, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, oldColorStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        barrier(m_gBuffer.normal.image, VK_IMAGE_ASPECT_COLOR_BIT, oldColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, oldColorAccess, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, oldColorStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        barrier(m_gBuffer.material.image, VK_IMAGE_ASPECT_COLOR_BIT, oldColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, oldColorAccess, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, oldColorStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        barrier(m_gBuffer.emissive.image, VK_IMAGE_ASPECT_COLOR_BIT, oldColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, oldColorAccess, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, oldColorStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        barrier(m_gBuffer.custom.image, VK_IMAGE_ASPECT_COLOR_BIT, oldColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, oldColorAccess, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, oldColorStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }
    barrier(m_gBuffer.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
            m_gBufferInitialized ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            m_gBufferInitialized ? VK_ACCESS_SHADER_READ_BIT : 0,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            m_gBufferInitialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    VkClearValue clears[6]{}; clears[0].color = {{0, 0, 0, 1}}; clears[1].color = {{0.5f, 0.5f, 1, 1}}; clears[2].color = {{0, 0.5f, 1, 0}}; clears[3].color = {{0, 0, 0, 0}}; clears[4].color = {{0, 0, 0, 0}}; clears[5].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rp.renderPass = m_gBuffer.renderPass; rp.framebuffer = m_gBuffer.framebuffer; rp.renderArea.extent = {static_cast<u32>(m_gBuffer.width), static_cast<u32>(m_gBuffer.height)}; rp.clearValueCount = 6; rp.pClearValues = clears;
    vkCmdBeginRenderPass(m_commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0, 0, static_cast<f32>(m_gBuffer.width), static_cast<f32>(m_gBuffer.height), 0, 1}; VkRect2D scissor{{0, 0}, {static_cast<u32>(m_gBuffer.width), static_cast<u32>(m_gBuffer.height)}}; vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport); vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
    BindMaterialSets(m_mainViewOffset);
    for (u32 i = 0; i < source.instanceCount; ++i) {
        DrawMaterialSections(source.instances[i], m_instanceObjects[i], GpuPass::GBuffer);
    }
    vkCmdEndRenderPass(m_commandBuffer);
    barrier(m_gBuffer.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    // The lighting pass clears the light buffer: its previous contents (last
    // frame's, or none on the first frame) are discarded.
    barrier(m_gBuffer.lightAccum.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    // Sun cascade depth passes (always recorded so every layer is cleared and
    // in SHADER_READ_ONLY before the lighting pass samples it).
    RecordSunShadows(source, cascades, sunShadows);
    m_shadowImageInitialized = true;
    VkClearValue lightClear{}; lightClear.color = {{0, 0, 0, 1}}; VkRenderPassBeginInfo lightRp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; lightRp.renderPass = m_gBuffer.lightingRenderPass; lightRp.framebuffer = m_gBuffer.lightingFramebuffer; lightRp.renderArea.extent = {static_cast<u32>(m_gBuffer.width), static_cast<u32>(m_gBuffer.height)}; lightRp.clearValueCount = 1; lightRp.pClearValues = &lightClear; vkCmdBeginRenderPass(m_commandBuffer, &lightRp, VK_SUBPASS_CONTENTS_INLINE); vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport); vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor); vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingPipeline); vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingLayout, 0, 1, &m_lightingSet, 0, nullptr); vkCmdDraw(m_commandBuffer, 3, 1, 0, 0); vkCmdEndRenderPass(m_commandBuffer);
    if (m_gridPass && source.grid.enabled) {
        barrier(m_gBuffer.lightAccum.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        barrier(m_gBuffer.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

        VkRenderingAttachmentInfo gridColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        gridColor.imageView = m_gBuffer.lightAccum.view;
        gridColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        gridColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        gridColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingAttachmentInfo gridDepth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        gridDepth.imageView = m_gBuffer.depth.view;
        gridDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        gridDepth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        gridDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo gridRendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        gridRendering.renderArea.extent = {static_cast<u32>(m_gBuffer.width), static_cast<u32>(m_gBuffer.height)};
        gridRendering.layerCount = 1;
        gridRendering.colorAttachmentCount = 1;
        gridRendering.pColorAttachments = &gridColor;
        gridRendering.pDepthAttachment = &gridDepth;
        vkCmdBeginRendering(m_commandBuffer, &gridRendering);
        vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
        m_gridPass->Record(m_commandBuffer, source.grid, scene.viewMatrix,
                           scene.viewProjectionMatrix);
        vkCmdEndRendering(m_commandBuffer);
        barrier(m_gBuffer.lightAccum.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        barrier(m_gBuffer.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }

    // --- Translucency: forward-lit, sorted, over the opaque scene (+ grid).
    RecordTranslucency(source, scene.cameraPosition);

    // --- HDR post: bloom + exposure + tone map (composite), then FXAA on the
    // display-encoded result. Without the composite (resource failure) FXAA
    // falls back to the raw HDR buffer, which displays clamped/untonemapped.
    if (m_bloomEnabled && m_bloomCompositeFBO) {
        // Meter the final HDR scene (after lighting + grid) for auto exposure.
        if (source.postProcess.autoExposure) {
            m_eyeAdaptation->Record(m_commandBuffer, static_cast<u32>(m_gBuffer.width),
                                    static_cast<u32>(m_gBuffer.height),
                                    source.postProcess, scene.deltaTime);
        }
        RecordBloom(source.postProcess);
        PointFXAAAt(m_bloomCompositeOutput.view);
    } else {
        PointFXAAAt(m_gBuffer.lightAccum.view);
    }

    if (m_fxaaRenderPass && m_fxaaPipeline) {
        VkClearValue fxaaClear{}; fxaaClear.color = {{0, 0, 0, 1}};
        VkRenderPassBeginInfo fxaaRp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        fxaaRp.renderPass = m_fxaaRenderPass;
        fxaaRp.framebuffer = m_fxaaFramebuffer;
        fxaaRp.renderArea.extent = {static_cast<u32>(m_gBuffer.width), static_cast<u32>(m_gBuffer.height)};
        fxaaRp.clearValueCount = 1;
        fxaaRp.pClearValues = &fxaaClear;
        vkCmdBeginRenderPass(m_commandBuffer, &fxaaRp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fxaaPipeline);
        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_fxaaLayout, 0, 1, &m_fxaaSet, 0, nullptr);
        Math::Vec2 res{static_cast<f32>(m_gBuffer.width), static_cast<f32>(m_gBuffer.height)};
        vkCmdPushConstants(m_commandBuffer, m_fxaaLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(res), &res);
        vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(m_commandBuffer);

        // Editor decorations on the final, display-encoded image: the
        // selection outline (Unreal-style), then the gizmo lines on top (no
        // depth test, so handles stay visible through geometry).
        bool anySelected = false;
        for (u32 i = 0; i < source.instanceCount; ++i) {
            anySelected = anySelected || (source.instances[i].selected && ResolveMesh(source.instances[i]));
        }
        const bool outline = m_selectionPass && m_selectionPass->Valid() && anySelected;
        const bool overlay = m_overlayPass && m_overlayPass->Valid() && source.overlayLines &&
                             source.overlayLineVertexCount >= 2;
        if (outline) {
            m_selectionPass->RecordMask(m_commandBuffer, [&](VkCommandBuffer) {
                BindMaterialSets(m_mainViewOffset);
                for (u32 i = 0; i < source.instanceCount; ++i) {
                    if (!source.instances[i].selected) continue;
                    DrawMaterialSections(source.instances[i], m_instanceObjects[i], GpuPass::SelectionMask);
                }
            });
        }
        if (outline || overlay) {
            VkImageMemoryBarrier toColor{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toColor.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            toColor.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toColor.srcQueueFamilyIndex = toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toColor.image = m_fxaaResolved.image;
            toColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr,
                                 0, nullptr, 1, &toColor);
            VkRenderingAttachmentInfo finalColor{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            finalColor.imageView = m_fxaaResolved.view;
            finalColor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            finalColor.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            finalColor.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfo finalRendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
            finalRendering.renderArea.extent = {static_cast<u32>(m_gBuffer.width),
                                                static_cast<u32>(m_gBuffer.height)};
            finalRendering.layerCount = 1;
            finalRendering.colorAttachmentCount = 1;
            finalRendering.pColorAttachments = &finalColor;
            vkCmdBeginRendering(m_commandBuffer, &finalRendering);
            if (outline) m_selectionPass->RecordOutline(m_commandBuffer);
            if (overlay) {
                m_overlayPass->Record(m_commandBuffer, source.overlayLines,
                                      source.overlayLineVertexCount, scene.viewProjectionMatrix,
                                      static_cast<u32>(m_gBuffer.width),
                                      static_cast<u32>(m_gBuffer.height));
            }
            vkCmdEndRendering(m_commandBuffer);
            VkImageMemoryBarrier toRead = toColor;
            toRead.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                                 nullptr, 1, &toRead);
        }
    }

    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) return;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &m_commandBuffer; vkQueueSubmit(m_graphicsQueue, 1, &submit, m_fence);
    m_gBufferInitialized = true;
}

bool VulkanDeferredRenderer::ReadFinalImage(std::vector<u8>& outRgba, u32& outWidth,
                                            u32& outHeight) {
    if (!m_initialized || !m_gBufferInitialized || m_fxaaResolved.image == VK_NULL_HANDLE) {
        return false;
    }
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, std::numeric_limits<u64>::max());
    const u32 width = static_cast<u32>(m_gBuffer.width);
    const u32 height = static_cast<u32>(m_gBuffer.height);
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 8;  // RGBA16F
    GpuBuffer staging = CreateBuffer(m_physicalDevice, m_device, bytes,
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     true);
    if (!staging.mapped) {
        DestroyBuffer(m_device, staging);
        return false;
    }
    VkCommandBuffer cmd = BeginOneTimeCommands(m_device, m_commandPool);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = m_fxaaResolved.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd, m_fxaaResolved.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.buffer, 1, &region);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    EndOneTimeCommands(m_device, m_commandPool, m_graphicsQueue, cmd);

    // The image holds display-encoded values in [0, 1].
    const u16* half = static_cast<const u16*>(staging.mapped);
    outRgba.resize(static_cast<usize>(width) * height * 4);
    for (usize i = 0; i < outRgba.size(); ++i) {
        const f32 v = i % 4 == 3 ? 1.0f : std::clamp(HalfToFloat(half[i]), 0.0f, 1.0f);
        outRgba[i] = static_cast<u8>(v * 255.0f + 0.5f);
    }
    DestroyBuffer(m_device, staging);
    outWidth = width;
    outHeight = height;
    return true;
}

const VulkanDeferredRenderer::Attachment* VulkanDeferredRenderer::GetLightAccumulationBuffer() const {
    return &m_gBuffer.lightAccum;
}

// --- Bloom Resources -----------------------------------------------------------

bool VulkanDeferredRenderer::CreateBloomResources() {
    // Create bloom sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = 1.0f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_bloomSampler) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create bloom sampler");
        return false;
    }

    // Create bloom render pass (single color attachment, sampled input)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = kLightFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // In: earlier passes' color writes / shader reads must finish before this
    // pass writes. Out: this pass's color writes must be visible to the next
    // pass, which samples them in its fragment shader.
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 2;
    rpInfo.pDependencies = deps;
    if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_bloomRenderPass) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create bloom render pass");
        return false;
    }
    // Upsample variant: LOAD the target (it holds that mip's downsample) so the
    // additively blended upsample accumulates onto it. Differs only in load op
    // and initial layout, so it is render-pass compatible with the one above
    // (same pipelines and framebuffers work with both).
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_bloomUpRenderPass) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create bloom upsample render pass");
        return false;
    }
    m_bloomChainPrimed = false;  // new mips: run the chain at least once

    // Create descriptor set layout for bloom (single sampled texture)
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_bloomSetLayout) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create bloom descriptor set layout");
        return false;
    }

    // Create bloom pipeline layout (push constant: texelSize, threshold, intensity, filterRadius)
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(f32) * 5; // texelSize(2) + threshold + intensity + filterRadius
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_bloomSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_bloomDownsampleLayout) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create bloom downsample pipeline layout");
        return false;
    }
    // Reuse same layout for upsample and composite
    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_bloomUpsampleLayout) != VK_SUCCESS) return false;

    // Composite layout: scene + bloom + eye-adaptation exposure buffer
    VkDescriptorSetLayoutBinding compositeBindings[3]{};
    compositeBindings[0].binding = 0;
    compositeBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compositeBindings[0].descriptorCount = 1;
    compositeBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    compositeBindings[1].binding = 1;
    compositeBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    compositeBindings[1].descriptorCount = 1;
    compositeBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    compositeBindings[2].binding = 2;
    compositeBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    compositeBindings[2].descriptorCount = 1;
    compositeBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo compositeLayoutInfo{};
    compositeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    compositeLayoutInfo.bindingCount = 3;
    compositeLayoutInfo.pBindings = compositeBindings;
    if (vkCreateDescriptorSetLayout(m_device, &compositeLayoutInfo, nullptr,
                                   &m_bloomCompositeSetLayout) != VK_SUCCESS) {
        return false;
    }
    VkPushConstantRange compositePush{};
    compositePush.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    compositePush.offset = 0;
    compositePush.size = sizeof(f32) * 8; // bloomTint(3)+intensity, exposure, toneMapper, pad(2)
    VkPipelineLayoutCreateInfo compositeLayoutInfo2{};
    compositeLayoutInfo2.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    compositeLayoutInfo2.setLayoutCount = 1;
    compositeLayoutInfo2.pSetLayouts = &m_bloomCompositeSetLayout;
    compositeLayoutInfo2.pushConstantRangeCount = 1;
    compositeLayoutInfo2.pPushConstantRanges = &compositePush;
    if (vkCreatePipelineLayout(m_device, &compositeLayoutInfo2, nullptr, &m_bloomCompositeLayout) != VK_SUCCESS) return false;

    // Create bloom pipelines
    // `additive`: blend ONE+ONE onto the target (upsample accumulation).
    auto createBloomPipeline = [&](VkPipelineLayout layout, const std::string& fragShader,
                                   VkPipeline& pipeline, bool additive) -> bool {
        // glslc emits "<name>.frag.spv" (see the shader list in CMakeLists).
        VkShaderModule vertModule = LoadShaderModule(m_device, m_shaderDir + "/deferred_lighting.vert.spv");
        VkShaderModule fragModule = LoadShaderModule(m_device, m_shaderDir + "/" + fragShader + ".frag.spv");
        if (!vertModule || !fragModule) {
            if (vertModule) vkDestroyShaderModule(m_device, vertModule, nullptr);
            if (fragModule) vkDestroyShaderModule(m_device, fragModule, nullptr);
            LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to load bloom shader: {}", fragShader);
            return false;
        }
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vertInput{};
        vertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = additive ? VK_TRUE : VK_FALSE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;
        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = layout;
        pipelineInfo.renderPass = m_bloomRenderPass;
        bool ok = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS;
        vkDestroyShaderModule(m_device, vertModule, nullptr);
        vkDestroyShaderModule(m_device, fragModule, nullptr);
        return ok;
    };

    if (!createBloomPipeline(m_bloomDownsampleLayout, "bloom_downsample", m_bloomDownsamplePipeline, false)) return false;
    if (!createBloomPipeline(m_bloomUpsampleLayout, "bloom_upsample", m_bloomUpsamplePipeline, true)) return false;
    if (!createBloomPipeline(m_bloomCompositeLayout, "bloom_composite", m_bloomCompositePipeline, false)) return false;

    // Calculate mip chain sizes (half each level, min 1x1)
    i32 mipW = m_gBuffer.width / 2;
    i32 mipH = m_gBuffer.height / 2;
    for (u32 i = 0; i < kBloomMips; ++i) {
        mipW = std::max(mipW, 1);
        mipH = std::max(mipH, 1);
        m_bloomMips[i].width = mipW;
        m_bloomMips[i].height = mipH;

        // Each mip's image must be exactly its own size: the passes sample it
        // across UV 0..1, so a larger image would expose stale texels.
        if (!CreateAttachment(kLightFormat,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              &m_bloomMips[i].color, mipW, mipH)) {
            LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create bloom mip {} texture", i);
            return false;
        }

        // Create framebuffer
        VkFramebufferCreateInfo fboInfo{};
        fboInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fboInfo.renderPass = m_bloomRenderPass;
        fboInfo.attachmentCount = 1;
        fboInfo.pAttachments = &m_bloomMips[i].color.view;
        fboInfo.width = static_cast<u32>(mipW);
        fboInfo.height = static_cast<u32>(mipH);
        fboInfo.layers = 1;
        if (vkCreateFramebuffer(m_device, &fboInfo, nullptr, &m_bloomMips[i].downFBO) != VK_SUCCESS) {
            LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create bloom mip {} framebuffer", i);
            return false;
        }
        // Upsample FBO reads from this mip's color as input, writes to next higher mip
        // (for the upsample pass, the FBO of the *next* mip is used; this is allocated below)

        mipW /= 2;
        mipH /= 2;
    }

    // Create upsample FBOs (writes to the same mip level, uses the downsampled version as input)
    for (u32 i = kBloomMips - 1; i > 0; --i) {
        // Upsample writes to mip[i-1] (the higher-res level)
        // We reuse mip[i-1]'s downFBO since it targets the same attachment
        m_bloomMips[i].upFBO = m_bloomMips[i - 1].downFBO;
    }
    if (kBloomMips > 0) {
        m_bloomMips[0].upFBO = m_bloomMips[0].downFBO; // Final upsample writes back to mip 0
    }

    // Create descriptor pool
    u32 descriptorCount = kBloomMips * 2 + 1; // sets: down + up per mip + composite
    // One sampler per down/up set, plus two for the composite (scene + bloom),
    // plus the composite's eye-adaptation exposure buffer.
    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kBloomMips * 2 + 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = descriptorCount;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_bloomPool) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create bloom descriptor pool");
        return false;
    }

    // Allocate descriptor sets for each mip
    std::vector<VkDescriptorSetLayout> layouts(descriptorCount, m_bloomSetLayout);
    // Last one uses composite layout
    layouts.back() = m_bloomCompositeSetLayout;
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_bloomPool;
    allocInfo.descriptorSetCount = descriptorCount;
    allocInfo.pSetLayouts = layouts.data();
    std::vector<VkDescriptorSet> descSets(descriptorCount);
    if (vkAllocateDescriptorSets(m_device, &allocInfo, descSets.data()) != VK_SUCCESS) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to allocate bloom descriptor sets");
        return false;
    }

    // Assign descriptor sets to mips
    for (u32 i = 0; i < kBloomMips; ++i) {
        m_bloomMips[i].downSet = descSets[i];
        m_bloomMips[i].upSet = descSets[kBloomMips + i];
    }
    m_bloomCompositeSet = descSets[kBloomMips * 2];

    // Update descriptor sets
    auto updateDescriptor = [&](VkDescriptorSet set, VkImageView imageView) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = imageView;
        imageInfo.sampler = m_bloomSampler;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    };

    // Downsample descriptors: read from source (lightAccum for first mip, previous mip for rest)
    updateDescriptor(m_bloomMips[0].downSet, m_gBuffer.lightAccum.view);
    for (u32 i = 1; i < kBloomMips; ++i) {
        updateDescriptor(m_bloomMips[i].downSet, m_bloomMips[i - 1].color.view);
    }

    // Upsample descriptors: read from current mip (which was just downsampled)
    for (u32 i = kBloomMips - 1; i > 0; --i) {
        updateDescriptor(m_bloomMips[i].upSet, m_bloomMips[i].color.view);
    }
    if (kBloomMips > 0) {
        updateDescriptor(m_bloomMips[0].upSet, m_bloomMips[0].color.view);
    }

    // Composite descriptor: scene + bloom
    {
        VkDescriptorImageInfo sceneInfo{};
        sceneInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneInfo.imageView = m_gBuffer.lightAccum.view;
        sceneInfo.sampler = m_bloomSampler;
        VkDescriptorImageInfo bloomInfo{};
        bloomInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bloomInfo.imageView = m_bloomMips[0].color.view; // Use mip 0 for final bloom
        bloomInfo.sampler = m_bloomSampler;
        VkDescriptorBufferInfo exposureInfo{m_eyeAdaptation->ExposureBuffer(), 0,
                                            m_eyeAdaptation->ExposureBufferSize()};
        VkWriteDescriptorSet writes[3]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_bloomCompositeSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &sceneInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_bloomCompositeSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &bloomInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = m_bloomCompositeSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo = &exposureInfo;
        vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
    }

    // Create composite output texture
    if (!CreateAttachment(kLightFormat,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          &m_bloomCompositeOutput)) {
        LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create bloom composite output");
        return false;
    }
    {
        VkFramebufferCreateInfo fboInfo{};
        fboInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fboInfo.renderPass = m_bloomRenderPass;
        fboInfo.attachmentCount = 1;
        fboInfo.pAttachments = &m_bloomCompositeOutput.view;
        fboInfo.width = static_cast<u32>(m_gBuffer.width);
        fboInfo.height = static_cast<u32>(m_gBuffer.height);
        fboInfo.layers = 1;
        if (vkCreateFramebuffer(m_device, &fboInfo, nullptr, &m_bloomCompositeFBO) != VK_SUCCESS) {
            LUMA_LOG_ERROR("VulkanDeferredRenderer", "Failed to create bloom composite framebuffer");
            return false;
        }
    }

    m_bloomEnabled = true;
    LUMA_LOG_INFO("VulkanDeferredRenderer", "Bloom initialized ({} mips, {}x{} final)",
                  kBloomMips, m_gBuffer.width, m_gBuffer.height);
    return true;
}

void VulkanDeferredRenderer::RecordBloom(const PostProcessParams& pp) {
    if (!m_bloomEnabled || !m_bloomCompositeFBO || !m_commandBuffer) return;

    auto beginBloomPass = [&](VkRenderPass pass, VkFramebuffer fbo, i32 width, i32 height) {
        VkClearValue clear{};
        clear.color = {{0, 0, 0, 1}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = pass;
        rp.framebuffer = fbo;
        rp.renderArea.extent = {static_cast<u32>(width), static_cast<u32>(height)};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(m_commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, static_cast<f32>(width), static_cast<f32>(height), 0, 1};
        VkRect2D sc{{0, 0}, {static_cast<u32>(width), static_cast<u32>(height)}};
        vkCmdSetViewport(m_commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(m_commandBuffer, 0, 1, &sc);
    };

    // Push layouts match bloom_downsample/upsample/composite.frag.
    struct DownPush {
        f32 texelSize[2];
        f32 threshold;
        f32 knee;
        f32 prefilter;
    };
    struct UpPush {
        f32 texelSize[2];
        f32 unused0;
        f32 unused1;
        f32 filterRadius;
    };
    struct CompositePush {
        f32 bloomTint[4];  // rgb tint, w intensity
        f32 exposure;      // manual exposure, or compensation when auto
        f32 toneMapper;
        f32 autoExposure;  // 1 = also multiply by the eye adaptation exposure
        f32 pad;
    };

    // The down/up chain only needs to run when bloom contributes, but must run
    // at least once after (re)creation so mip 0 is valid for the composite.
    const bool runChain = pp.bloomEnabled || !m_bloomChainPrimed;
    if (runChain) {
        // Downsample: mip 0 reads the HDR scene (thresholded + Karis-averaged),
        // each later mip reads the previous one.
        for (u32 i = 0; i < kBloomMips; ++i) {
            beginBloomPass(m_bloomRenderPass, m_bloomMips[i].downFBO,
                           m_bloomMips[i].width, m_bloomMips[i].height);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_bloomDownsamplePipeline);
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_bloomDownsampleLayout, 0, 1,
                                    &m_bloomMips[i].downSet, 0, nullptr);
            const i32 srcW = (i == 0) ? m_gBuffer.width : m_bloomMips[i - 1].width;
            const i32 srcH = (i == 0) ? m_gBuffer.height : m_bloomMips[i - 1].height;
            DownPush push{};
            push.texelSize[0] = 1.0f / static_cast<f32>(std::max(srcW, 1));
            push.texelSize[1] = 1.0f / static_cast<f32>(std::max(srcH, 1));
            push.threshold = std::max(pp.bloomThreshold, 0.0f);
            push.knee = std::clamp(pp.bloomSoftThreshold, 0.0f, 1.0f);
            push.prefilter = (i == 0) ? 1.0f : 0.0f;
            vkCmdPushConstants(m_commandBuffer, m_bloomDownsampleLayout,
                               VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(m_commandBuffer);
        }

        // Upsample from the smallest mip back toward mip 0, additively blending
        // each blurred level onto the next-larger mip (LOAD pass keeps that
        // mip's own downsample), so every scale contributes to the final glow.
        for (u32 i = kBloomMips - 1; i > 0; --i) {
            beginBloomPass(m_bloomUpRenderPass, m_bloomMips[i].upFBO,
                           m_bloomMips[i - 1].width, m_bloomMips[i - 1].height);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              m_bloomUpsamplePipeline);
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_bloomUpsampleLayout, 0, 1,
                                    &m_bloomMips[i].upSet, 0, nullptr);
            UpPush push{};
            push.texelSize[0] = 1.0f / static_cast<f32>(std::max(m_bloomMips[i].width, 1));
            push.texelSize[1] = 1.0f / static_cast<f32>(std::max(m_bloomMips[i].height, 1));
            push.filterRadius = 1.0f;
            vkCmdPushConstants(m_commandBuffer, m_bloomUpsampleLayout,
                               VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
            vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(m_commandBuffer);
        }
        m_bloomChainPrimed = true;
    }

    // Composite = final HDR -> display pass: bloom mix, exposure, tone map, sRGB.
    beginBloomPass(m_bloomRenderPass, m_bloomCompositeFBO, m_gBuffer.width, m_gBuffer.height);
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_bloomCompositePipeline);
    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_bloomCompositeLayout, 0, 1, &m_bloomCompositeSet,
                            0, nullptr);
    CompositePush composite{};
    composite.bloomTint[0] = pp.bloomTint.x;
    composite.bloomTint[1] = pp.bloomTint.y;
    composite.bloomTint[2] = pp.bloomTint.z;
    // Mip 0 accumulates every level (~kBloomMips x the thresholded energy), so
    // normalize: `bloomIntensity` stays a stable fraction of the scene glow.
    composite.bloomTint[3] = pp.bloomEnabled
        ? std::max(pp.bloomIntensity, 0.0f) / static_cast<f32>(kBloomMips)
        : 0.0f;
    // Auto: exposure = compensation x adapted scale (average -> 18% grey, +1 EV
    // like UE5); `pp.exposure` stays a user compensation on top. Manual: the
    // calibrated base exposure x `pp.exposure`.
    const bool autoExposure = pp.autoExposure && m_eyeAdaptation;
    composite.exposure = (autoExposure ? kAutoExposureCompensation : kBaseExposure) *
                         std::max(pp.exposure, 0.0f);
    composite.toneMapper = static_cast<f32>(static_cast<u32>(pp.toneMapper));
    composite.autoExposure = autoExposure ? 1.0f : 0.0f;
    if (BypassesPostProcess(m_viewMode)) {
        composite.bloomTint[3] = 0.0f;
        composite.pad = 1.0f;  // display the lighting output as-is
    }
    vkCmdPushConstants(m_commandBuffer, m_bloomCompositeLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(composite),
                       &composite);
    vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(m_commandBuffer);
}

void VulkanDeferredRenderer::DestroyBloomResources() {
    m_bloomEnabled = false;

    for (u32 i = 0; i < kBloomMips; ++i) {
        // Every upFBO aliases some mip's downFBO (mip 0's aliases its own), so
        // each mip destroys exactly its downFBO and never its upFBO.
        if (m_bloomMips[i].downFBO)
            vkDestroyFramebuffer(m_device, m_bloomMips[i].downFBO, nullptr);
        m_bloomMips[i].downFBO = VK_NULL_HANDLE;
        m_bloomMips[i].upFBO = VK_NULL_HANDLE;
        DestroyAttachment(m_bloomMips[i].color);
    }

    if (m_bloomCompositeFBO) vkDestroyFramebuffer(m_device, m_bloomCompositeFBO, nullptr);
    m_bloomCompositeFBO = VK_NULL_HANDLE;
    DestroyAttachment(m_bloomCompositeOutput);

    if (m_bloomPool) vkDestroyDescriptorPool(m_device, m_bloomPool, nullptr);
    m_bloomPool = VK_NULL_HANDLE;

    if (m_bloomDownsamplePipeline) vkDestroyPipeline(m_device, m_bloomDownsamplePipeline, nullptr);
    if (m_bloomUpsamplePipeline) vkDestroyPipeline(m_device, m_bloomUpsamplePipeline, nullptr);
    if (m_bloomCompositePipeline) vkDestroyPipeline(m_device, m_bloomCompositePipeline, nullptr);
    m_bloomDownsamplePipeline = VK_NULL_HANDLE;
    m_bloomUpsamplePipeline = VK_NULL_HANDLE;
    m_bloomCompositePipeline = VK_NULL_HANDLE;

    if (m_bloomDownsampleLayout) vkDestroyPipelineLayout(m_device, m_bloomDownsampleLayout, nullptr);
    if (m_bloomUpsampleLayout) vkDestroyPipelineLayout(m_device, m_bloomUpsampleLayout, nullptr);
    if (m_bloomCompositeLayout) vkDestroyPipelineLayout(m_device, m_bloomCompositeLayout, nullptr);
    m_bloomDownsampleLayout = VK_NULL_HANDLE;
    m_bloomUpsampleLayout = VK_NULL_HANDLE;
    m_bloomCompositeLayout = VK_NULL_HANDLE;

    if (m_bloomSetLayout) vkDestroyDescriptorSetLayout(m_device, m_bloomSetLayout, nullptr);
    m_bloomSetLayout = VK_NULL_HANDLE;
    if (m_bloomCompositeSetLayout)
        vkDestroyDescriptorSetLayout(m_device, m_bloomCompositeSetLayout, nullptr);
    m_bloomCompositeSetLayout = VK_NULL_HANDLE;

    if (m_bloomRenderPass) vkDestroyRenderPass(m_device, m_bloomRenderPass, nullptr);
    m_bloomRenderPass = VK_NULL_HANDLE;
    if (m_bloomUpRenderPass) vkDestroyRenderPass(m_device, m_bloomUpRenderPass, nullptr);
    m_bloomUpRenderPass = VK_NULL_HANDLE;
    m_bloomChainPrimed = false;

    if (m_bloomSampler) vkDestroySampler(m_device, m_bloomSampler, nullptr);
    m_bloomSampler = VK_NULL_HANDLE;
}

} // namespace Rendering
} // namespace Luma
