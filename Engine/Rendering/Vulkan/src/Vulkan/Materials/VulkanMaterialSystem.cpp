#include "Vulkan/Materials/VulkanMaterialSystem.h"

#include <algorithm>
#include <cstring>

#include "Vulkan/Textures/VulkanTextureCache.h"

namespace Luma {
namespace {

void Copy4(f32 dst[4], const Math::Vec4& v) {
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
    dst[3] = v.w;
}

// Index of the reserved white texture for a view (see VulkanTextureCache).
u32 WhiteIndex(GpuTextureView view) { return view == GpuTextureView::SRGB ? 1u : 0u; }

}  // namespace

VulkanMaterialSystem::VulkanMaterialSystem(VkPhysicalDevice physical, VkDevice device,
                                           u32 maxMaterials)
    : m_device(device), m_capacity(std::max(maxMaterials, 1u)) {
    const VkDeviceSize size = sizeof(GpuMaterial) * m_capacity;
    for (GpuBuffer& buffer : m_buffers) {
        buffer = CreateBuffer(physical, device, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              true);
        if (!buffer.mapped) {
            LUMA_LOG_ERROR("MaterialSystem", "Failed to allocate the material table");
            return;
        }
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = kFramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS ||
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        LUMA_LOG_ERROR("MaterialSystem", "Failed to create the material table descriptors");
        return;
    }
    std::array<VkDescriptorSetLayout, kFramesInFlight> layouts;
    layouts.fill(m_layout);
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = m_pool;
    alloc.descriptorSetCount = kFramesInFlight;
    alloc.pSetLayouts = layouts.data();
    std::array<VkDescriptorSet, kFramesInFlight> sets{};
    if (vkAllocateDescriptorSets(device, &alloc, sets.data()) != VK_SUCCESS) {
        LUMA_LOG_ERROR("MaterialSystem", "Failed to allocate the material table descriptors");
        return;
    }
    for (u32 i = 0; i < kFramesInFlight; ++i) {
        VkDescriptorBufferInfo info{m_buffers[i].buffer, 0, size};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = sets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // Handle 0: the Default Material until the editor uploads the asset.
    const MaterialGpuDesc fallback = MakeDefaultMaterialGpuDesc();
    m_descs.push_back(fallback);
    m_table.push_back(Build(fallback, nullptr));
    m_alive.push_back(true);
    m_keys.push_back(kDefaultMaterialKey);
    m_byKey[kDefaultMaterialKey] = kDefaultMaterialHandle;
    for (u32 i = 0; i < kFramesInFlight; ++i) {
        std::memcpy(m_buffers[i].mapped, m_table.data(), sizeof(GpuMaterial));
    }
    m_sets = sets;
}

VulkanMaterialSystem::~VulkanMaterialSystem() {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    if (m_layout) vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
    for (GpuBuffer& buffer : m_buffers) DestroyBuffer(m_device, buffer);
}

GpuMaterial VulkanMaterialSystem::Build(const MaterialGpuDesc& desc,
                                        const VulkanTextureCache* textures) const {
    GpuMaterial m{};
    m.blendMode = static_cast<u32>(desc.blend);
    m.shadingModel = static_cast<u32>(desc.shading);
    m.twoSided = desc.twoSided ? 1u : 0u;
    m.opacityMaskClip = desc.opacityMaskClip;
    for (usize i = 0; i < kMaterialGpuInputCount; ++i) {
        const MaterialGpuDesc::Input& in = desc.inputs[i];
        GpuMaterialInputData& out = m.inputs[i];
        Copy4(out.constant, in.constant);
        Copy4(out.tint, in.tint);
        Copy4(out.tilingOffset, {in.tiling.x, in.tiling.y, in.offset.x, in.offset.y});
        out.channel = static_cast<u32>(in.channel);
        out.scalar = static_cast<GpuMaterialInput>(i) == GpuMaterialInput::EmissiveColor
                         ? in.emissiveIntensity
                         : in.normalStrength;
        const bool resident =
            textures && in.texture != kInvalidHandle && textures->MipCount(in.texture) > 0;
        out.texIndex = resident ? textures->BindlessIndex(in.texture, in.view)
                                : WhiteIndex(in.view);
        out.flags = (resident ? GpuMaterial::kHasTexture : 0u) |
                    (in.flipGreen ? GpuMaterial::kFlipGreen : 0u) |
                    (in.invert ? GpuMaterial::kInvert : 0u);
    }
    return m;
}

MaterialHandle VulkanMaterialSystem::Update(u64 key, const MaterialGpuDesc& desc,
                                            const VulkanTextureCache& textures) {
    if (!IsValid()) return kDefaultMaterialHandle;
    MaterialHandle handle;
    if (auto it = m_byKey.find(key); it != m_byKey.end()) {
        handle = it->second;
    } else if (!m_free.empty()) {
        handle = m_free.back();
        m_free.pop_back();
    } else if (m_table.size() < m_capacity) {
        handle = static_cast<MaterialHandle>(m_table.size());
        m_descs.emplace_back();
        m_table.emplace_back();
        m_alive.push_back(false);
        m_keys.push_back(0);
    } else {
        LUMA_LOG_ERROR("MaterialSystem", "Material table full ({} materials)", m_capacity);
        return kDefaultMaterialHandle;
    }
    m_descs[handle] = desc;
    m_table[handle] = Build(desc, &textures);
    m_alive[handle] = true;
    m_keys[handle] = key;
    m_byKey[key] = handle;
    return handle;
}

void VulkanMaterialSystem::Release(MaterialHandle handle) {
    if (handle == kDefaultMaterialHandle || handle >= m_table.size() || !m_alive[handle]) return;
    m_alive[handle] = false;
    m_byKey.erase(m_keys[handle]);
    // Draws still referencing the handle fall back to the Default Material.
    m_descs[handle] = m_descs[kDefaultMaterialHandle];
    m_table[handle] = m_table[kDefaultMaterialHandle];
    m_free.push_back(handle);
}

u32 VulkanMaterialSystem::Index(MaterialHandle handle) const {
    if (handle >= m_table.size() || !m_alive[handle]) return kDefaultMaterialHandle;
    return handle;
}

const GpuMaterial& VulkanMaterialSystem::Entry(MaterialHandle handle) const {
    return m_table[Index(handle)];
}

void VulkanMaterialSystem::Flush(u32 frameSlot, const VulkanTextureCache& textures) {
    if (!IsValid()) return;
    for (usize h = 0; h < m_table.size(); ++h) {
        if (m_alive[h]) m_table[h] = Build(m_descs[h], &textures);
    }
    std::memcpy(m_buffers[frameSlot % kFramesInFlight].mapped, m_table.data(),
                sizeof(GpuMaterial) * m_table.size());
}

}  // namespace Luma
