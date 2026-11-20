#pragma once

#include <string>
#include <vector>

#include "Luma/Core/Types.h"
#include "Luma/Math/Math.h"
#include "Luma/RHI/RHIResources.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/Renderer/Lighting.h"
#include "Luma/Renderer/GBufferInfo.h"

// Shadow system for deferred shading. Inspired by UE5's shadow system
// but adapted for Luma's simpler architecture. Provides shadow map
// rendering and cascaded shadow maps for directional lights.

namespace Luma {
namespace Renderer2 {

using std::string;
using std::vector;
using namespace Luma::Math;

// Forward declarations
class GBuffer;

// ============================================================================
// Shadow Map
// ============================================================================

// Shadow map type
enum class EShadowMapType : u32 {
    Simple,         // Single shadow map
    Cascaded,       // Cascaded shadow maps for directional lights
    Cube,           // Cube shadow map for point lights
};

// Shadow map description
struct ShadowMapDesc {
    u32 width = 2048;
    u32 height = 2048;
    RHI::ETextureFormat format = RHI::ETextureFormat::D24_UNORM_S8_UINT;
    EShadowMapType type = EShadowMapType::Simple;
    u32 cascadeCount = 4;  // For cascaded shadow maps
    f32 cascadeSplitLambda = 0.5f;  // Cascade split parameter
};

// Shadow map class
class ShadowMap {
public:
    ShadowMap();
    ~ShadowMap();
    
    // Get shadow map description
    const ShadowMapDesc& GetDesc() const { return m_desc; }
    
    // Set shadow map description
    void SetDesc(const ShadowMapDesc& desc) { m_desc = desc; }
    
    // Get shadow map texture
    RHI::RHITexture* GetShadowMapTexture() const { return m_shadowMapTexture; }
    
    // Get cascade shadow map textures (for cascaded shadows)
    const vector<RHI::RHITexture*>& GetCascadeTextures() const { return m_cascadeTextures; }
    
    // Create shadow map
    bool Create(const ShadowMapDesc& desc, RHI::RHIDevice* device = nullptr);
    
    // Resize shadow map
    bool Resize(u32 width, u32 height, RHI::RHIDevice* device = nullptr);
    
    // Destroy shadow map
    void Destroy();
    
    // Check if valid
    bool IsValid() const { return m_created; }
    
    // Get shadow map type
    EShadowMapType GetType() const { return m_desc.type; }
    
private:
    ShadowMapDesc m_desc;
    RHI::RHITexture* m_shadowMapTexture = nullptr;
    vector<RHI::RHITexture*> m_cascadeTextures;
    RHI::RHIDevice* m_device = nullptr;
    bool m_created = false;
};

// ============================================================================
// Shadow Renderer
// ============================================================================

// Shadow renderer for rendering shadow maps
class ShadowRenderer {
public:
    ShadowRenderer();
    ~ShadowRenderer();
    
    // Get shadow map
    ShadowMap* GetShadowMap() const { return m_shadowMap; }
    
    // Set shadow map
    void SetShadowMap(ShadowMap* shadowMap) { m_shadowMap = shadowMap; }
    
    // Set shadow map description
    void SetShadowMapDesc(const ShadowMapDesc& desc) { m_shadowMapDesc = desc; }
    
    // Initialize with device
    bool Initialize(RHI::RHIDevice* device);
    
    // Shutdown
    void Shutdown();
    
    // Render shadow map for a light
    void RenderShadowMap(RHI::RHICommandList* cmdList, const LightData& light, const Luma::SceneView& sceneView);
    
    // Render cascaded shadow maps for directional light
    void RenderCascadedShadowMaps(RHI::RHICommandList* cmdList, const LightData& light, const Luma::SceneView& sceneView);
    
    // Draw scene instances into the current shadow pass using the bound pipeline
    void DrawInstances(RHI::RHICommandList* cmdList, const Luma::SceneView& sceneView, const Mat4& cascadeViewProj);
    
    // Render cube shadow map for point light
    void RenderCubeShadowMap(RHI::RHICommandList* cmdList, const LightData& light, const Luma::SceneView& sceneView);
    
    // Prepare shadow rendering
    void Prepare(RHI::RHICommandList* cmdList);
    
    // Calculate cascade split distances
    vector<f32> CalculateCascadeSplits(f32 nearPlane, f32 farPlane, f32 splitLambda, u32 cascadeCount);
    
    // Get cascade matrices
    const vector<Mat4>& GetCascadeViewProjections() const { return m_cascadeViewProjections; }
    
    // Get cascade split distances
    const vector<f32>& GetCascadeSplits() const { return m_cascadeSplits; }
    
    // Get shadow map DSV views for each cascade
    const vector<RHI::RHIDepthStencilView*>& GetCascadeDSVs() const { return m_cascadeDSVs; }
    
    // Set primitive mesh data from GBufferRenderer (shared, not owned)
    void SetPrimitiveMeshes(const GBufferRenderer::RhiMesh* primitives) { m_primitives = primitives; }
    
private:
    ShadowMap* m_shadowMap = nullptr;
    ShadowMapDesc m_shadowMapDesc;
    RHI::RHIDevice* m_device = nullptr;
    vector<Mat4> m_cascadeViewProjections;
    vector<f32> m_cascadeSplits;
    vector<RHI::RHIDepthStencilView*> m_cascadeDSVs;
    bool m_initialized = false;
    
    // Pipeline state
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    
    // Shared primitive mesh data (from GBufferRenderer, not owned)
    const GBufferRenderer::RhiMesh* m_primitives = nullptr;
    
    // Shadow UBO
    struct ShadowUBO {
        Mat4 viewProj;
    };
    RHI::RHIBuffer* m_shadowUBO = nullptr;
    
    // Initialize pipeline
    bool InitPipeline();
    void CleanupPipeline();
    
    // Update shadow UBO for a cascade
    void UpdateShadowUBO(RHI::RHICommandList* cmdList, const Mat4& viewProj);
    
    // Calculate light view matrix
    Mat4 CalculateLightViewMatrix(const LightData& light);
    
    // Calculate light projection matrix
    Mat4 CalculateLightProjectionMatrix(const LightData& light);
    
    // Calculate cascade view-projection matrix
    Mat4 CalculateCascadeViewProjection(const Vec3& lightPos, const Vec3& lightDir, f32 nearPlane, f32 farPlane, const Luma::SceneView& sceneView);
    
    // Create DSV views for cascade textures
    bool CreateCascadeViews();
};

// ============================================================================
// Shadow Settings
// ============================================================================

// Shadow quality settings
struct ShadowSettings {
    u32 shadowMapSize = 2048;
    u32 cascadeCount = 4;
    f32 cascadeSplitLambda = 0.5f;
    f32 shadowBias = 0.005f;
    f32 shadowSlopeBias = 0.002f;
    f32 shadowNormalBias = 0.001f;
    u32 pcfSamples = 4;  // Percentage-closer filtering samples
    bool enablePCSS = false;  // Percentage-closer soft shadows
    f32 pcssLightSize = 1.6f;
};

// ============================================================================
// Shadow Manager
// ============================================================================

// Global shadow manager
class ShadowManager {
public:
    static ShadowManager& GetInstance();
    
    // Get shadow settings
    const ShadowSettings& GetSettings() const { return m_settings; }
    
    // Set shadow settings
    void SetSettings(const ShadowSettings& settings) { m_settings = settings; }
    
    // Create shadow map for a light
    ShadowMap* CreateShadowMap(const ShadowMapDesc& desc, RHI::RHIDevice* device = nullptr);
    
    // Destroy shadow map
    void DestroyShadowMap(ShadowMap* shadowMap);
    
    // Get shadow renderer
    ShadowRenderer* GetShadowRenderer() const { return m_shadowRenderer; }
    
    // Clear all shadow maps
    void Clear();
    
private:
    ShadowManager() = default;
    ~ShadowManager();
    
    ShadowSettings m_settings;
    ShadowRenderer* m_shadowRenderer = nullptr;
    vector<ShadowMap*> m_shadowMaps;
};

// ============================================================================
// Convenience Functions
// ============================================================================

// Create shadow map
inline ShadowMap* CreateShadowMap(u32 width, u32 height, EShadowMapType type = EShadowMapType::Simple, RHI::RHIDevice* device = nullptr) {
    ShadowMapDesc desc;
    desc.width = width;
    desc.height = height;
    desc.type = type;
    
    auto* shadowMap = new ShadowMap();
    shadowMap->Create(desc, device);
    return shadowMap;
}

// Destroy shadow map
inline void DestroyShadowMap(ShadowMap* shadowMap) {
    if (shadowMap) {
        shadowMap->Destroy();
        delete shadowMap;
    }
}

} // namespace Renderer2
} // namespace Luma