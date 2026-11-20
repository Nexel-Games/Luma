#pragma once

#include <string>

#include "Luma/Math/Math.h"
#include "Vulkan/Sky/AtmosphereParams.h"
#include "Vulkan/VulkanCommon.h"
#include "Vulkan/VulkanMemory.h"

namespace Luma {

// Real-time sky light capture (Unreal's SkyLight "Real Time Capture" model,
// see ReflectionEnvironmentRealTimeCapture.cpp). Turns the physical atmosphere
// into image-based ambient lighting:
//
//   1. capture    - render the sky (without the sun disk) into a cube, mip 0
//   2. downsample - build the source cube's mip chain
//   3. filter     - GGX-prefilter a second cube, one roughness per mip
//                   (filtered importance sampling from the source mips)
//   4. SH         - project a low mip onto L2 spherical harmonics with the
//                   cosine lobe baked in (diffuse irradiance)
//
// Lighting then reads the prefiltered cube for specular (mip chosen from
// roughness, Unreal's mapping) and evaluates the SH for diffuse. All work is
// compute; it is only re-recorded when the sky inputs change.
//
// Its own module: owns every resource and pipeline. Both cubes stay in
// VK_IMAGE_LAYOUT_GENERAL (sampled and written as storage images).
class VulkanSkyLight {
public:
    static constexpr u32 kCubeSize = 128;
    static constexpr u32 kMipCount = 8;  // 128 -> 1
    static constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkImageLayout kCubeLayout = VK_IMAGE_LAYOUT_GENERAL;

    VulkanSkyLight(VkPhysicalDevice physical, VkDevice device,
                   const std::string& shaderDir);
    ~VulkanSkyLight();

    VulkanSkyLight(const VulkanSkyLight&) = delete;
    VulkanSkyLight& operator=(const VulkanSkyLight&) = delete;

    bool IsValid() const { return m_valid; }

    // Records the capture + filtering when the sky changed since the last
    // capture (or on first use), followed by a barrier that makes the results
    // visible to fragment shaders. Otherwise records nothing.
    void Record(VkCommandBuffer cmd, const Rendering::AtmosphereParams& atmo,
                const Math::Vec3& cameraPosition);

    // Prefiltered specular cube (sample with kCubeLayout) and the diffuse SH
    // buffer (9 x vec4, rgb = convolved L2 coefficients).
    VkImageView FilteredCubeView() const { return m_filtered.cubeView; }
    VkSampler Sampler() const { return m_sampler; }
    VkBuffer SHBuffer() const { return m_shBuffer.buffer; }
    VkDeviceSize SHBufferSize() const { return kSHBytes; }

private:
    struct Cube {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView cubeView = VK_NULL_HANDLE;           // all mips (sampling)
        VkImageView mipViews[kMipCount] = {};             // 2D array (storage)
    };
    struct CaptureUBO {
        Rendering::AtmosphereParams atmo;
        f32 camPos[4];
    };
    struct Push {
        i32 mip;
        i32 numMips;
        f32 roughness;
        i32 size;
    };
    static constexpr VkDeviceSize kSHBytes = sizeof(f32) * 4 * 9;
    // Sets: capture(1) + downsample(mips-1) + filter(mips) + SH(1).
    static constexpr u32 kSetCount = 1 + (kMipCount - 1) + kMipCount + 1;

    bool CreateCube(Cube& cube);
    void DestroyCube(Cube& cube);
    bool CreateDescriptors();
    VkPipeline CreatePipeline(const std::string& path);
    void WriteSet(VkDescriptorSet set, VkImageView storageView);
    void Dispatch(VkCommandBuffer cmd, VkPipeline pipeline, VkDescriptorSet set,
                  const Push& push);

    VkPhysicalDevice m_physical;
    VkDevice m_device;
    bool m_valid = false;

    Cube m_source;    // raw sky + box-filtered mips
    Cube m_filtered;  // GGX-prefiltered (one roughness per mip)
    GpuBuffer m_shBuffer;
    GpuBuffer m_ubo;
    VkSampler m_sampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_captureSet = VK_NULL_HANDLE;
    VkDescriptorSet m_downSets[kMipCount] = {};    // index = output mip (1..)
    VkDescriptorSet m_filterSets[kMipCount] = {};  // index = output mip
    VkDescriptorSet m_shSet = VK_NULL_HANDLE;

    VkPipeline m_capturePipeline = VK_NULL_HANDLE;
    VkPipeline m_downsamplePipeline = VK_NULL_HANDLE;
    VkPipeline m_filterPipeline = VK_NULL_HANDLE;
    VkPipeline m_shPipeline = VK_NULL_HANDLE;

    // Change detection: re-capture only when the sky inputs differ.
    bool m_hasCapture = false;
    bool m_layoutsInitialized = false;
    Rendering::AtmosphereParams m_lastAtmo{};
    f32 m_lastAltitude = 0.0f;
};

}  // namespace Luma
