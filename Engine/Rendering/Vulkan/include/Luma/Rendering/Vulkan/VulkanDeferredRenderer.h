#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Math/Math.h"
#include "Luma/Profiling/Profiler.h"
#include "Luma/RHI/SceneRenderer.h"
#include "Luma/Renderer/CascadeShadows.h"
#include "Luma/Renderer/FrustumCulling.h"
#include "Luma/Renderer/RenderQuality.h"
#include "Luma/Renderer/DeferredSceneView.h"
#include "Vulkan/Sky/AtmosphereParams.h"
#include "Vulkan/VulkanMemory.h"

namespace Luma {
class VulkanUIPass;
class VulkanGridPass;
class VulkanOverlayLinePass;
class VulkanSkyLight;
class VulkanEyeAdaptation;
class VulkanTextureCache;
class VulkanMeshCache;
class VulkanMaterialSystem;
class VulkanMaterialLayout;
class VulkanMaterialPipelines;
class VulkanSelectionOutlinePass;
class VulkanGpuTimer;
struct GpuMesh;
namespace Rendering {

/**
 * Vulkan deferred renderer for the editor viewport (and asset thumbnails).
 *
 * Pipeline (all offscreen, into the light-accumulation buffer):
 *   1. G-Buffer pass  - renders every mesh section with its compiled
 *      material's G-buffer pipeline (VulkanMaterialPipelines, shared
 *      VulkanMaterialLayout) into MRT attachments: base color + shading
 *      model R8G8B8A8_SRGB, world-normal+roughness R16G16B16A16_SFLOAT,
 *      metallic/specular/AO R8G8B8A8_UNORM, emissive R16G16B16A16_SFLOAT,
 *      shading-model data R8G8B8A8_UNORM, plus a D32 depth attachment.
 *      Sun shadows and the selection mask draw with the same materials'
 *      depth-only / selection pipelines.
 *   2. Lighting pass  - fullscreen triangle (deferred_lighting shaders)
 *      reads the G-Buffer + lights UBO and writes the lit, tone-mapped,
 *      gamma-encoded color into the light-accumulation buffer.
 *   3. Grid pass      - the editor ground grid, depth-tested against the
 *      G-Buffer depth so scene geometry occludes it (optional; skipped when
 *      the scene's grid is disabled).
 *
 * The light-accumulation image is registered with the UI pass as an external
 * texture so the viewport panel can display it (see GetViewportTextureHandle).
 *
 * Scene content (meshes, textures, materials) is resident in caches owned for
 * the renderer's whole lifetime, so it survives viewport resizes; the
 * SceneRenderer interface is how the editor uploads it.
 */
class VulkanDeferredRenderer final : public SceneRenderer {
public:
    struct Attachment {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    struct GBuffer {
        int32_t width = 0;
        int32_t height = 0;
        Attachment albedo;     // R8G8B8A8_SRGB (base color + shading model)
        Attachment normal;     // R16G16B16A16_SFLOAT (+ roughness in alpha)
        Attachment material;   // R8G8B8A8_UNORM (metallic/specular/AO)
        Attachment emissive;   // R16G16B16A16_SFLOAT (emissive radiance)
        Attachment custom;     // R8G8B8A8_UNORM (shading-model data, RT4)
        Attachment sceneColor; // copy of the lit HDR scene read by refracting translucency
        Attachment ppInput;    // Post Process Input 0: copy of the post chain's current image
        Attachment depth;      // D32_SFLOAT
        Attachment lightAccum; // final lit result shown in the viewport
        VkSampler sampler = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass lightingRenderPass = VK_NULL_HANDLE;
        VkFramebuffer lightingFramebuffer = VK_NULL_HANDLE;
    };

    VulkanDeferredRenderer(VkPhysicalDevice physicalDevice, VkDevice device,
                           VkQueue graphicsQueue, uint32_t graphicsQueueFamilyIndex,
                           VulkanUIPass& uiPass, const std::string& shaderDir,
                           bool useEnvironment = false,
                           std::string environmentPath = {}, u32 anisotropy = 16, f32 mipBias = 0);  // "" = the default HDRI
    ~VulkanDeferredRenderer();

    VulkanDeferredRenderer(const VulkanDeferredRenderer&) = delete;
    VulkanDeferredRenderer& operator=(const VulkanDeferredRenderer&) = delete;

    // Initialization
    bool Initialize(int32_t width, int32_t height);
    // Background blur of the HDR environment as a mip level (lighting always
    // uses the sharp image); negative = automatic (~128 px wide mip).
    void SetEnvironmentBlur(f32 mipLevel) { m_environmentBlurLod = mipLevel; }
    void Cleanup();

    // Public API for editor integration
    void SetViewportDimensions(int32_t width, int32_t height);
    void SetQuality(const Renderer2::RenderQuality& quality);
    void PrepareScene();
    void RenderScene(const Renderer2::DeferredSceneView& scene);
    const Attachment* GetLightAccumulationBuffer() const;

    // UI texture handle for the light-accumulation buffer (0 until first render).
    // Owned by this renderer; stays valid across resizes.
    uint64_t GetViewportTextureHandle() const { return m_textureHandle; }

    // --- SceneRenderer -----------------------------------------------------------
    MeshHandle UploadMesh(u64 key, const MeshData& mesh) override;
    void ReleaseMesh(MeshHandle mesh) override;
    GpuTextureHandle UploadTexture(u64 key, const TextureUploadDesc& texture) override;
    void ReleaseTexture(GpuTextureHandle texture) override;
    MaterialHandle UpdateMaterial(u64 key, const CompiledMaterialGpu& material,
                                  const MaterialParameterBlock& parameters) override;
    void UpdateMaterialParameters(MaterialHandle material, const MaterialParameterBlock& parameters) override;
    // Pipelines currently alive (tests: parameter-only edits create none).
    usize MaterialPipelineCount() const;
    void ReleaseMaterial(MaterialHandle material) override;
    MeshHandle PrimitiveMesh(MeshPrimitive primitive) const override;

    // Copies the last rendered display image (post chain, outline and overlay
    // included) into tightly packed RGBA8, top row first. Waits for the frame
    // to finish. False before the first RenderScene.
    bool ReadFinalImage(std::vector<u8>& outRgba, u32& outWidth, u32& outHeight);
    // Current render size (0 before Initialize).
    i32 ViewportWidth() const { return m_gBuffer.width; }
    i32 ViewportHeight() const { return m_gBuffer.height; }

    // True once Initialize() succeeded (resources exist: command pool, GBuffer
    // descriptor set, samplers).
    bool IsInitialized() const { return m_initialized; }

    // Name GPU timings and CPU scopes are reported under ("Viewport",
    // "Preview"). Set before the first frame.
    void SetProfileName(std::string name);
    // Counters of the last RenderScene (draws, triangles, culled instances).
    const Profiling::FrameCounters& LastFrameStats() const { return m_stats; }

private:
    struct GpuLight {
        f32 posType[4];   // xyz position, w type (0 dir, 1 point, 2 spot)
        f32 dirRange[4];  // xyz direction, w range
        f32 color[4];     // rgb color, w intensity
        f32 spot[4];      // x cosInner, y cosOuter, z falloff exponent
    };
    static constexpr u32 kMaxLights = 16;
    static constexpr u32 kCascadesUBO = 4;
    Renderer2::RenderQuality m_quality;
    std::vector<Renderer2::Aabb> m_worldBounds;
    std::vector<bool> m_staticBounds;
    std::vector<bool> m_visible;
    static constexpr u32 kCascades = 4;
    static constexpr VkFormat kShadowFormat = VK_FORMAT_D32_SFLOAT;

    // Lighting UBO (matches LightingUBO in deferred_lighting.frag).
    ViewMode m_viewMode = ViewMode::Lit;  // last scene's view mode (composite)

    struct LightingUBO {
        Math::Mat4 invViewProj;
        f32 camPos[4];
        f32 camForward[4];   // xyz camera forward (for cascade selection)
        f32 sunDir[4];       // xyz direction TO sun, w = below-horizon sky fade
        f32 sunColor[4];     // rgb, w intensity
        f32 skyZenith[4];    // rgb IBL up
        f32 skyHorizon[4];   // rgb IBL horizon, w = ibl intensity
        f32 groundColor[4];  // rgb IBL below horizon
        // x = lightCount, y = sunShadows, z = 1/shadowSize,
        // w = material environment enabled
        f32 params[4];
        // x = softness, y = cascadeCount, z = shadowBias, w = normalBias
        f32 shadowParams[4];
        f32 cascadeSplits[4];
        f32 cascadeTexel[4];  // world units per shadow texel, per cascade
        // Sky light (real-time capture): x = enabled, y = max mip of the
        // prefiltered cube (for roughness -> mip), zw unused.
        f32 skyLight[4];
        Math::Mat4 cascadeViewProj[kCascadesUBO];
        Rendering::AtmosphereParams atmo;  // physical sky params (sky_atmosphere.glsl)
        GpuLight lights[kMaxLights];
    };

    // Resource creation
    bool CreateCommandPool();
    bool CreateGBuffer(int32_t width, int32_t height);
    void DestroyGBufferTargets();
    // `width`/`height` <= 0 use the viewport (G-Buffer) size.
    bool CreateAttachment(VkFormat format, VkImageUsageFlags usage,
                          Attachment* attachment, i32 width = 0, i32 height = 0);
    void DestroyAttachment(Attachment& attachment);
    bool CreateRenderPass();
    bool CreateLightingRenderPass();
    bool CreateFramebuffer();
    bool CreateLightingFramebuffer();
    bool CreateSampler();
    bool CreateEnvironmentTexture(const std::string& path);
    void DestroyEnvironmentTexture();
    bool CreateUBOs();
    bool CreateLayouts();
    bool CreateDescriptorSets();
    bool CreatePipelines(const std::string& shaderDir);
    // The instance's uploaded mesh, else its built-in primitive shape.
    const GpuMesh* ResolveMesh(const SceneInstance& instance) const;
    // The material a mesh section draws with (shared by every pass).
    static MaterialHandle SectionMaterial(const SceneInstance& instance, u32 section);

    // Cascaded shadow maps (sun / directional light)
    bool CreateShadowResources();
    void DestroyShadowResources();
    // Shared material pipeline layout: targets per pass, per-frame sets.
    void RegisterMaterialTargets();
    void BindMaterialSets(u32 viewOffset);
    Math::Mat4 NormalMatrix(const Math::Mat4& model) const;
    struct DrawItem {
        VkPipeline pipeline;
        MaterialHandle handle;
        const GpuMesh* mesh;
        const CompiledMaterialGpu* material;
        u32 instance, section;
    };
    std::vector<DrawItem> m_opaqueDraws, m_depthDraws;
    const GpuMesh* m_boundMesh = nullptr;
    VkPipeline m_boundPipeline = VK_NULL_HANDLE;
    void BuildDrawList(const SceneView& source, GpuPass pass, std::vector<DrawItem>& list);
    void DrawSection(const SceneInstance& inst, u32 objectId, GpuPass pass, const GpuMesh* mesh,
                     u32 sectionIndex, MaterialHandle handle, const CompiledMaterialGpu& material, VkPipeline pipeline);
    void DrawMaterialSections(const SceneInstance& inst, u32 objectId, GpuPass pass);
    // Forward pass: translucent sections back to front over the lit scene.
    void RecordTranslucency(const SceneView& source, const Math::Vec3& cameraPosition);
    // Post-process materials at `location`, rendered in place over `target`
    // (in SHADER_READ_ONLY before and after).
    void RecordPostProcessMaterials(const PostProcessParams& pp, GpuPostProcessLocation location, VkImage target,
                                    VkImageView targetView);
    // Records the depth-only cascade passes (clears every layer; draws only
    // when `enabled`) and leaves the array in SHADER_READ_ONLY for lighting.
    void RecordSunShadows(const SceneView& source,
                          const Renderer2::SunCascades& cascades, bool enabled);

    std::string m_profileName = "Viewport";
    std::unique_ptr<VulkanGpuTimer> m_gpuTimer;
    Profiling::FrameCounters m_stats;

    // Scene content residency (lives as long as the renderer).
    std::unique_ptr<VulkanTextureCache> m_textures;
    std::unique_ptr<VulkanMeshCache> m_meshes;
    std::unique_ptr<VulkanMaterialLayout> m_materialLayout;
    std::unique_ptr<VulkanMaterialPipelines> m_materialPipelines;
    std::unique_ptr<VulkanMaterialSystem> m_materials;  // references the two above
    static constexpr u32 kPrimitiveCount = 4;
    std::array<MeshHandle, kPrimitiveCount> m_primitiveMeshes{};
    u32 m_frameSlot = 0;  // per-frame material data slot used by the current frame
    std::vector<u32> m_instanceObjects;              // object id per scene instance this frame
    std::vector<Math::Vec3> m_instanceCenters;       // world bounds centre per instance (sorting)
    u32 m_mainViewOffset = 0;
    u32 m_frameIndex = 0;                        // frame-UBO dynamic offsets this frame
    std::array<u32, 4> m_cascadeViewOffsets{};

    // Equirectangular HDR environment used by the deferred background pass.
    VkImage m_environmentImage = VK_NULL_HANDLE;
    VkDeviceMemory m_environmentMemory = VK_NULL_HANDLE;
    VkImageView m_environmentView = VK_NULL_HANDLE;
    VkSampler m_environmentSampler = VK_NULL_HANDLE;

    // Vulkan handles
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamilyIndex = 0;
    VulkanUIPass& m_uiPass;
    std::string m_shaderDir;
    bool m_useEnvironment = false;
    std::string m_environmentPath;
    u32 m_environmentMips = 1;
    f32 m_environmentBlurLod = -1.0f;
    bool m_initialized = false;
    bool m_gBufferInitialized = false;

    // GBuffer (MRT attachments + light accumulation)
    GBuffer m_gBuffer;

    // Pipelines
    // G-buffer permutations: [masked][twoSided].
    VkPipelineLayout m_lightingLayout = VK_NULL_HANDLE;
    VkPipeline m_lightingPipeline = VK_NULL_HANDLE;

    // Descriptor resources
    VkDescriptorSetLayout m_lightingSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_lightingPool = VK_NULL_HANDLE;
    static constexpr u32 kFrameSlots = 2;
    std::array<VkDescriptorSet, kFrameSlots> m_lightingSets{};
    void UpdateLightingSet(u32 slot);

    // Uniform buffers
    std::array<GpuBuffer, kFrameSlots> m_lightingUBOs{};

    // Command resources
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    std::array<VkFence, kFrameSlots> m_fences{};
    std::array<VkCommandBuffer, kFrameSlots> m_commands{};
    bool m_garbagePending = false;

    // Cascaded shadow map resources (sun / directional light)
    VkImage m_shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_shadowMem = VK_NULL_HANDLE;
    VkImageView m_shadowArrayView = VK_NULL_HANDLE;
    VkImageView m_shadowLayerViews[kCascades] = {};
    VkSampler m_shadowSampler = VK_NULL_HANDLE;
    bool m_shadowImageInitialized = false;

    // Editor ground grid (created with 1-sample targets matching this pass)
    std::unique_ptr<VulkanGridPass> m_gridPass;
    std::unique_ptr<VulkanOverlayLinePass> m_overlayPass;  // editor gizmo lines
    std::unique_ptr<VulkanOverlayLinePass> m_depthLinePass;  // depth-tested world lines (DebugDraw)
    // Selected-actor outline (its own module, Vulkan/Selection).
    std::unique_ptr<VulkanSelectionOutlinePass> m_selectionPass;

    // Sky light: atmosphere captured into a GGX-prefiltered cube + diffuse SH
    // (its own module, Vulkan/SkyLight). Feeds the lighting pass's ambient.
    std::unique_ptr<VulkanSkyLight> m_skyLight;

    // Auto exposure (histogram eye adaptation; its own module,
    // Vulkan/EyeAdaptation). Meters the HDR light buffer; the composite reads
    // its exposure buffer.
    std::unique_ptr<VulkanEyeAdaptation> m_eyeAdaptation;

    // FXAA anti-aliasing pass resources
    Attachment m_fxaaResolved;        // FXAA output texture
    VkRenderPass m_fxaaRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_fxaaFramebuffer = VK_NULL_HANDLE;
    VkPipelineLayout m_fxaaLayout = VK_NULL_HANDLE;
    VkPipeline m_fxaaPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_fxaaSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_fxaaPool = VK_NULL_HANDLE;
    VkDescriptorSet m_fxaaSet = VK_NULL_HANDLE;
    const Attachment& DisplayAttachment() const;
    bool CreateFXAAPass();
    bool CreateFXAATarget();
    void DestroyFXAAPass();
    void PointFXAAAt(VkImageView sourceView);

    // Multi-pass bloom resources
    static constexpr u32 kBloomMips = 5;
    struct BloomMip {
        Attachment color;              // downsampled / upsampled texture
        VkFramebuffer downFBO = VK_NULL_HANDLE;   // for downsample pass
        VkFramebuffer upFBO = VK_NULL_HANDLE;     // for upsample pass
        VkDescriptorSet downSet = VK_NULL_HANDLE;  // descriptor for downsample
        VkDescriptorSet upSet = VK_NULL_HANDLE;    // descriptor for upsample
        i32 width = 0;
        i32 height = 0;
    };
    BloomMip m_bloomMips[kBloomMips];
    VkRenderPass m_bloomRenderPass = VK_NULL_HANDLE;    // CLEAR (downsample/composite)
    VkRenderPass m_bloomUpRenderPass = VK_NULL_HANDLE;  // LOAD (additive upsample)
    VkPipelineLayout m_bloomDownsampleLayout = VK_NULL_HANDLE;
    VkPipeline m_bloomDownsamplePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_bloomUpsampleLayout = VK_NULL_HANDLE;
    VkPipeline m_bloomUpsamplePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_bloomCompositeLayout = VK_NULL_HANDLE;
    VkPipeline m_bloomCompositePipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_bloomSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_bloomPool = VK_NULL_HANDLE;
    VkSampler m_bloomSampler = VK_NULL_HANDLE;
    // Composite resources
    Attachment m_bloomCompositeOutput; // final composited result
    VkFramebuffer m_bloomCompositeFBO = VK_NULL_HANDLE;
    VkDescriptorSet m_bloomCompositeSet = VK_NULL_HANDLE;
    bool m_bloomEnabled = false;      // bloom/composite resources exist
    bool m_bloomChainPrimed = false;  // down/up chain has run since creation
    VkDescriptorSetLayout m_bloomCompositeSetLayout = VK_NULL_HANDLE;
    bool CreateBloomResources();
    bool CreateBloomTargets();
    void DestroyBloomTargets();
    void DestroyBloomResources();
    // Bloom chain (when enabled) + the final composite: exposure, tone map and
    // sRGB encode of the HDR light buffer into m_bloomCompositeOutput.
    void RecordBloom(const PostProcessParams& pp);

    // Calibrates scene-referred radiance to the display: PostProcessParams::
    // exposure multiplies this (1.0 = the engine's default look).
    static constexpr f32 kBaseExposure = 1.0f;
    // With auto exposure, the adapted average maps to 18% grey; this extra
    // compensation is Unreal 5's default Exposure Compensation (+1 EV = x2).
    static constexpr f32 kAutoExposureCompensation = 2.0f;

    // UI texture handle registered for the light-accumulation buffer
    uint64_t m_textureHandle = 0;

    // Image view the UI texture currently points at (re-pointed on resize)
    VkImageView m_registeredLightView = VK_NULL_HANDLE;

    static constexpr VkFormat kAlbedoFormat = VK_FORMAT_R8G8B8A8_SRGB;
    static constexpr VkFormat kEmissiveFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kNormalFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kMaterialFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
    // HDR: lighting writes scene-referred linear radiance; bloom mips and the
    // post chain share this format. The final composite writes display-encoded
    // values into the same format, which FXAA and the UI then sample.
    static constexpr VkFormat kLightFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
};

} // namespace Rendering
} // namespace Luma
