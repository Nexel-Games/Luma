#include "Vulkan/Materials/VulkanMaterialSystem.h"

#include <bit>

#include "Luma/Core/Log.h"
#include "Vulkan/Materials/VulkanMaterialLayout.h"
#include "Vulkan/Materials/VulkanMaterialPipelines.h"
#include "Vulkan/Textures/VulkanTextureCache.h"

namespace Luma {

VulkanMaterialSystem::VulkanMaterialSystem(VulkanMaterialPipelines& pipelines) : m_pipelines(pipelines) {
    Entry fallback;
    fallback.key = kDefaultMaterialKey;
    fallback.alive = true;
    m_entries.push_back(std::move(fallback));
    m_byKey[kDefaultMaterialKey] = kDefaultMaterialHandle;
}

MaterialHandle VulkanMaterialSystem::Update(u64 key, const CompiledMaterialGpu& material,
                                            const MaterialParameterBlock& parameters) {
    MaterialHandle handle = kInvalidHandle;
    if (const auto it = m_byKey.find(key); it != m_byKey.end()) {
        handle = it->second;
    } else if (!m_free.empty()) {
        handle = m_free.back();
        m_free.pop_back();
    } else {
        handle = static_cast<MaterialHandle>(m_entries.size());
        m_entries.emplace_back();
    }
    Entry& e = m_entries[handle];
    if (!e.alive) {
        e = Entry{};
        e.key = key;
        e.alive = true;
        m_byKey[key] = handle;
    }
    e.block = parameters;
    const bool hasShaders = Drawable(handle) != nullptr && Resolve(handle) == handle;
    if (material.shaderKey != e.material.shaderKey || !hasShaders) {
        if (m_pipelines.Prepare(material)) {
            e.material = material;
        } else {
            LUMA_LOG_ERROR("MaterialSystem",
                           "Material %016llx keeps its previous shaders: a pipeline of the new set failed",
                           static_cast<unsigned long long>(key));
        }
    } else {
        // Same shaders: settings that need no new pipelines (clip, textures).
        e.material.opacityMaskClip = material.opacityMaskClip;
        e.material.textures = material.textures;
    }
    return handle;
}

void VulkanMaterialSystem::UpdateParameters(MaterialHandle handle, const MaterialParameterBlock& parameters) {
    if (handle >= m_entries.size() || !m_entries[handle].alive) return;
    m_entries[handle].block = parameters;
}

void VulkanMaterialSystem::Release(MaterialHandle handle) {
    if (handle == kDefaultMaterialHandle || handle >= m_entries.size() || !m_entries[handle].alive) return;
    m_byKey.erase(m_entries[handle].key);
    m_entries[handle] = Entry{};
    m_free.push_back(handle);
}

MaterialHandle VulkanMaterialSystem::Resolve(MaterialHandle handle) const {
    return handle < m_entries.size() && m_entries[handle].alive ? handle : kDefaultMaterialHandle;
}

const CompiledMaterialGpu* VulkanMaterialSystem::Drawable(MaterialHandle handle) const {
    const Entry& e = m_entries[Resolve(handle)];
    for (const auto& pass : e.material.passes) {
        if (pass) return &e.material;
    }
    return nullptr;
}

u32 VulkanMaterialSystem::ParamOffset(MaterialHandle handle) const { return m_entries[Resolve(handle)].paramOffset; }

usize VulkanMaterialSystem::LiveCount() const {
    usize n = 0;
    for (const Entry& e : m_entries) n += e.alive ? 1 : 0;
    return n;
}

void VulkanMaterialSystem::Flush(VulkanMaterialLayout& layout, const VulkanTextureCache& textures) {
    std::vector<Math::Vec4> block;
    for (Entry& e : m_entries) {
        if (!e.alive) continue;
        block = e.block.values;
        for (const GpuTextureBinding& t : e.material.textures) {
            if (t.slot >= block.size()) block.resize(t.slot + 1, Math::Vec4{0, 0, 0, 0});
            // Cubes: the cache has no cube uploads yet, so element 0 (white).
            const u32 image =
                t.cube ? 0u : textures.BindlessIndex(t.texture, t.srgb ? GpuTextureView::SRGB : GpuTextureView::Linear);
            block[t.slot] = {std::bit_cast<f32>(image), std::bit_cast<f32>(t.sampler), std::bit_cast<f32>(1u), 0.0f};
        }
        if (block.empty()) block.push_back(Math::Vec4{0, 0, 0, 0});  // a valid offset for empty blocks
        e.paramOffset = layout.AddParams(block);
        if (Drawable(static_cast<MaterialHandle>(&e - m_entries.data()))) m_pipelines.Touch(e.material.shaderKey);
    }
}

}  // namespace Luma
