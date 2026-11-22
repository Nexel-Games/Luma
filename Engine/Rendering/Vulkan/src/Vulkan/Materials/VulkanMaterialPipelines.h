#pragma once

#include <array>
#include <map>
#include <tuple>
#include <vector>

#include "Luma/RHI/MaterialGpuDesc.h"
#include "Vulkan/VulkanCommon.h"

// Per-material graphics pipelines (Unreal: the material shader map's PSOs).
// Every pipeline uses the shared VulkanMaterialLayout pipeline layout; what
// varies is the material's SPIR-V for the pass and the fixed-function state
// that pass + material settings imply. Pipelines are cached by
// (shader key, pass, state, target) and freed once no material references
// them for kRetireFrames frames.

namespace Luma {

class VulkanMaterialPipelines {
public:
    static constexpr u32 kRetireFrames = 3;

    // Where a pass draws: a render pass (subpass 0) or, when null, dynamic
    // rendering into the listed formats.
    struct Target {
        VkRenderPass renderPass = VK_NULL_HANDLE;
        std::vector<VkFormat> colorFormats;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        bool operator==(const Target&) const = default;
    };

    VulkanMaterialPipelines(VkDevice device, VkPipelineLayout layout);
    ~VulkanMaterialPipelines();
    VulkanMaterialPipelines(const VulkanMaterialPipelines&) = delete;
    VulkanMaterialPipelines& operator=(const VulkanMaterialPipelines&) = delete;

    // Registers (or replaces, e.g. after a resize) the target of a pass.
    void SetTarget(GpuPass pass, const Target& target);
    bool HasTarget(GpuPass pass) const;

    // The pipeline drawing `material`'s `pass`; created on first use. Null
    // when the material has no such pass, the pass has no target yet, or
    // creation failed (logged once per shader key).
    VkPipeline Get(const CompiledMaterialGpu& material, GpuPass pass);
    // Creates every pipeline `material` needs for the registered targets.
    // False if any failed (the caller keeps drawing the previous shaders).
    bool Prepare(const CompiledMaterialGpu& material);

    // Marks the shader key as referenced this frame / ages the rest.
    void Touch(u64 shaderKey);
    void EndFrame();
    usize PipelineCount() const { return m_pipelines.size(); }

private:
    struct Key {
        u64 shaderKey;
        GpuPass pass;
        u8 cull;
        u8 blend;
        u32 target;  // target generation
        auto operator<=>(const Key&) const = default;
    };
    struct Entry {
        VkPipeline pipeline = VK_NULL_HANDLE;
        u64 shaderKey = 0;
    };
    VkShaderModule Module(const std::vector<u32>& spirv);
    VkPipeline Create(const CompiledMaterialGpu& material, GpuPass pass, const GpuShaderPair& shaders);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    std::array<Target, kGpuPassCount> m_targets{};
    std::array<u32, kGpuPassCount> m_targetGeneration{};
    std::map<Key, Entry> m_pipelines;
    std::map<u64, u32> m_idleFrames;  // shader key -> frames since last Touch
    std::map<u64, bool> m_failed;     // shader keys whose pipelines failed (logged once)
    u32 m_nextGeneration = 1;
};

}  // namespace Luma
