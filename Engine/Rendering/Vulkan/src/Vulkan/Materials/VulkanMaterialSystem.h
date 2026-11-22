#pragma once

#include <unordered_map>
#include <vector>

#include "Luma/RHI/MaterialGpuDesc.h"
#include "Vulkan/VulkanCommon.h"

namespace Luma {

class VulkanTextureCache;
class VulkanMaterialLayout;
class VulkanMaterialPipelines;

// Every material the scene draws with: its compiled shader set (per-pass
// SPIR-V + state) and its parameter block. Handle 0 is the Default Material
// (it draws nothing until the editor uploads the compiled asset).
//
// Updating a material swaps its shader set only once every pipeline of the
// new set exists for the registered pass targets; until then (or when one
// fails) the handle keeps drawing the previous set. Parameter-only updates
// touch no pipelines. Each frame, Flush packs every live block into the
// frame's parameter buffer, writing texture slots as (image index, sampler
// index, bound, 0) bits, and records each handle's offset.
class VulkanMaterialSystem {
public:
    VulkanMaterialSystem(VulkanMaterialPipelines& pipelines);

    VulkanMaterialSystem(const VulkanMaterialSystem&) = delete;
    VulkanMaterialSystem& operator=(const VulkanMaterialSystem&) = delete;

    MaterialHandle Update(u64 key, const CompiledMaterialGpu& material, const MaterialParameterBlock& parameters);
    void UpdateParameters(MaterialHandle handle, const MaterialParameterBlock& parameters);
    void Release(MaterialHandle handle);

    // The shader set `handle` draws with (unknown handles: the Default
    // Material). Null when it has none yet.
    const CompiledMaterialGpu* Drawable(MaterialHandle handle) const;
    // First vec4 of `handle`'s block in the current frame (after Flush).
    u32 ParamOffset(MaterialHandle handle) const;

    // Packs every live block into the layout's current frame.
    void Flush(VulkanMaterialLayout& layout, const VulkanTextureCache& textures);

    usize LiveCount() const;

private:
    struct Entry {
        CompiledMaterialGpu material;  // the set drawing now (no passes = nothing yet)
        MaterialParameterBlock block;
        u64 key = 0;
        u32 paramOffset = 0;
        bool alive = false;
    };
    MaterialHandle Resolve(MaterialHandle handle) const;

    VulkanMaterialPipelines& m_pipelines;
    std::vector<Entry> m_entries;  // indexed by handle
    std::vector<MaterialHandle> m_free;
    std::unordered_map<u64, MaterialHandle> m_byKey;
};

}  // namespace Luma
