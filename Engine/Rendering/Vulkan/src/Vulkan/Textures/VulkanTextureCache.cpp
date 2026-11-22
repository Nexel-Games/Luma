#include "Vulkan/Textures/VulkanTextureCache.h"

#include <algorithm>
#include <cstring>

#include "Vulkan/VulkanMemory.h"

namespace Luma {
namespace {

constexpr VkComponentMapping kIdentity = {
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
constexpr VkComponentMapping kGray = {
    VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R,
    VK_COMPONENT_SWIZZLE_ONE};

u8 To8(u16 v) { return static_cast<u8>((static_cast<u32>(v) + 128u) / 257u); }

// Expands any accepted layout to tightly packed RGBA8.
std::vector<u8> ToRGBA8(const TextureUploadDesc& d) {
    const usize count = static_cast<usize>(d.width) * d.height;
    std::vector<u8> out(count * 4);
    const u8* src8 = static_cast<const u8*>(d.pixels);
    const u16* src16 = static_cast<const u16*>(d.pixels);
    for (usize i = 0; i < count; ++i) {
        u8* o = &out[i * 4];
        switch (d.format) {
            case TexturePixelFormat::R8:
                o[0] = o[1] = o[2] = src8[i];
                o[3] = 255;
                break;
            case TexturePixelFormat::RG8:
                o[0] = o[1] = o[2] = src8[i * 2];
                o[3] = src8[i * 2 + 1];
                break;
            case TexturePixelFormat::RGBA8:
                std::memcpy(o, src8 + i * 4, 4);
                break;
            case TexturePixelFormat::R16:
                o[0] = o[1] = o[2] = To8(src16[i]);
                o[3] = 255;
                break;
            case TexturePixelFormat::RGBA16:
                for (u32 c = 0; c < 4; ++c) o[c] = To8(src16[i * 4 + c]);
                break;
        }
    }
    return out;
}

void ImageBarrier(VkCommandBuffer cmd, VkImage image, u32 mip, u32 mipCount,
                  VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
                  VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                  VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, mipCount, 0, 1};
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

}  // namespace

u32 VulkanTextureCache::MipCountFor(u32 width, u32 height) {
    u32 size = std::max(width, height);
    u32 mips = 1;
    while (size > 1) {
        size >>= 1;
        ++mips;
    }
    return mips;
}

VulkanTextureCache::VulkanTextureCache(VkPhysicalDevice physical, VkDevice device,
                                       VkQueue queue, u32 queueFamily, u32 maxTextures, u32 anisotropy, f32 mipBias)
    : m_physical(physical), m_device(device), m_queue(queue) {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        LUMA_LOG_ERROR("TextureCache", "Failed to create the upload command pool");
        return;
    }
    if (!CreateSamplers(anisotropy, mipBias) || !CreateDescriptors(maxTextures) || !CreateWhiteCube()) {
        LUMA_LOG_ERROR("TextureCache", "Failed to create the bindless texture table");
        return;
    }

    // Slot 0: the 1x1 white texture every missing texture resolves to.
    const u8 white[4] = {255, 255, 255, 255};
    TextureUploadDesc desc;
    desc.width = desc.height = 1;
    desc.format = TexturePixelFormat::RGBA8;
    desc.pixels = white;
    desc.srgb = false;
    Entry entry;
    const u32 slot = AcquireSlot();
    if (slot != 0 || !CreateImage(desc, Prepare(desc), entry)) {
        LUMA_LOG_ERROR("TextureCache", "Failed to create the default white texture");
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
        m_set = VK_NULL_HANDLE;
        return;
    }
    entry.alive = true;
    m_entries[0] = entry;
    WriteDescriptors(0, entry.linearView, entry.srgbView);
}

VulkanTextureCache::~VulkanTextureCache() {
    if (m_device == VK_NULL_HANDLE) return;
    vkQueueWaitIdle(m_queue);
    for (Entry& e : m_entries) DestroyEntry(e);
    if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_layout) vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
    if (m_whiteCubeView) vkDestroyImageView(m_device, m_whiteCubeView, nullptr);
    if (m_whiteCube) vkDestroyImage(m_device, m_whiteCube, nullptr);
    if (m_whiteCubeMemory) vkFreeMemory(m_device, m_whiteCubeMemory, nullptr);
    for (VkSampler s : m_samplers) {
        if (s) vkDestroySampler(m_device, s, nullptr);
    }
    if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
}

bool VulkanTextureCache::CreateSamplers(u32 anisotropy, f32 mipBias) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physical, &props);
    const VkSamplerAddressMode addresses[3] = {VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                               VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT};
    for (u32 f = 0; f < 4; ++f) {
        for (u32 a = 0; a < 3; ++a) {
            const auto filter = static_cast<GpuSamplerFilter>(f);
            VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            const bool nearest = filter == GpuSamplerFilter::Nearest;
            info.magFilter = info.minFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            info.mipmapMode = nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.addressModeU = info.addressModeV = info.addressModeW = addresses[a];
            info.anisotropyEnable = filter == GpuSamplerFilter::Anisotropic ? VK_TRUE : VK_FALSE;
            info.mipLodBias = std::clamp(mipBias, -props.limits.maxSamplerLodBias, props.limits.maxSamplerLodBias);
            info.maxAnisotropy = info.anisotropyEnable ? std::clamp(static_cast<f32>(anisotropy), 1.0f, props.limits.maxSamplerAnisotropy) : 1.0f;
            info.minLod = 0.0f;
            info.maxLod = filter == GpuSamplerFilter::LinearNoMips ? 0.0f : VK_LOD_CLAMP_NONE;
            const u32 index = GpuSamplerIndex(filter, static_cast<GpuSamplerAddress>(a));
            m_samplerInfos[index] = info;
            if (vkCreateSampler(m_device, &info, nullptr, &m_samplers[index]) != VK_SUCCESS) return false;
        }
    }
    return true;
}

bool VulkanTextureCache::CreateWhiteCube() {
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {1, 1, 1};
    image.mipLevels = 1;
    image.arrayLayers = 6;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &image, nullptr, &m_whiteCube) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_device, m_whiteCube, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryType(m_physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc.memoryTypeIndex == ~0u || vkAllocateMemory(m_device, &alloc, nullptr, &m_whiteCubeMemory) != VK_SUCCESS) {
        return false;
    }
    vkBindImageMemory(m_device, m_whiteCube, m_whiteCubeMemory, 0);

    VkCommandBufferAllocateInfo cbAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbAlloc.commandPool = m_pool;
    cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &cbAlloc, &cmd) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
    VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = m_whiteCube;
    toDst.subresourceRange = range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toDst);
    const VkClearColorValue white{{1.0f, 1.0f, 1.0f, 1.0f}};
    vkCmdClearColorImage(cmd, m_whiteCube, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &range);
    VkImageMemoryBarrier toRead = toDst;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toRead);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    const bool ok = vkQueueSubmit(m_queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(m_queue) == VK_SUCCESS;
    vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
    if (!ok) return false;

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_whiteCube;
    view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange = range;
    if (vkCreateImageView(m_device, &view, nullptr, &m_whiteCubeView) != VK_SUCCESS) return false;

    VkDescriptorImageInfo info{VK_NULL_HANDLE, m_whiteCubeView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_set;
    write.dstBinding = 2;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return true;
}

bool VulkanTextureCache::CreateDescriptors(u32 maxTextures) {
    VkPhysicalDeviceVulkan12Properties p12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &p12;
    vkGetPhysicalDeviceProperties2(m_physical, &props2);
    u32 count = std::max(maxTextures, 2u);
    count = std::min({count, p12.maxPerStageDescriptorUpdateAfterBindSampledImages - kMaxCubes,
                      p12.maxDescriptorSetUpdateAfterBindSampledImages - kMaxCubes});
    count = std::min(count, kMaxSlots * 2);
    count &= ~1u;  // two views per texture
    if (count < 2) return false;
    m_slotCapacity = count / 2;

    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = count;
    bindings[0].stageFlags = stages;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[1].descriptorCount = kGpuSamplerCount;
    bindings[1].stageFlags = stages;
    bindings[1].pImmutableSamplers = m_samplers.data();
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[2].descriptorCount = kMaxCubes;
    bindings[2].stageFlags = stages;
    const VkDescriptorBindingFlags bindless =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    const VkDescriptorBindingFlags flags[3] = {bindless, 0, bindless};
    VkDescriptorSetLayoutBindingFlagsCreateInfo flagInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    flagInfo.bindingCount = 3;
    flagInfo.pBindingFlags = flags;
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.pNext = &flagInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS)
        return false;

    const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, count + kMaxCubes},
                                           {VK_DESCRIPTOR_TYPE_SAMPLER, kGpuSamplerCount}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = m_descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_layout;
    return vkAllocateDescriptorSets(m_device, &alloc, &m_set) == VK_SUCCESS;
}

bool VulkanTextureCache::CanBlitLinear(VkFormat format) const {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(m_physical, format, &props);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                      VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                      VK_FORMAT_FEATURE_BLIT_DST_BIT;
    return (props.optimalTilingFeatures & need) == need;
}

VulkanTextureCache::Prepared VulkanTextureCache::Prepare(const TextureUploadDesc& d) const {
    Prepared p;
    const usize bytes = static_cast<usize>(d.width) * d.height * TexturePixelBytes(d.format);
    // 16-bit data keeps its precision when the device can filter and blit it;
    // 16-bit color is stored as 8-bit sRGB (hardware decode + linear-space mips).
    if (!d.srgb && d.format == TexturePixelFormat::R16 && CanBlitLinear(VK_FORMAT_R16_UNORM)) {
        p.pixels.assign(static_cast<const u8*>(d.pixels), static_cast<const u8*>(d.pixels) + bytes);
        p.storageFormat = p.linearFormat = p.srgbFormat = VK_FORMAT_R16_UNORM;
        p.swizzle = kGray;
        return p;
    }
    if (!d.srgb && d.format == TexturePixelFormat::RGBA16 &&
        CanBlitLinear(VK_FORMAT_R16G16B16A16_UNORM)) {
        p.pixels.assign(static_cast<const u8*>(d.pixels), static_cast<const u8*>(d.pixels) + bytes);
        p.storageFormat = p.linearFormat = p.srgbFormat = VK_FORMAT_R16G16B16A16_UNORM;
        p.swizzle = kIdentity;
        return p;
    }
    p.pixels = ToRGBA8(d);
    // The image is stored in its own color space so mip blits filter linear
    // light; the other view reinterprets the same texels.
    p.storageFormat = d.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    p.linearFormat = VK_FORMAT_R8G8B8A8_UNORM;
    p.srgbFormat = VK_FORMAT_R8G8B8A8_SRGB;
    p.swizzle = kIdentity;
    p.mutableFormat = true;
    return p;
}

bool VulkanTextureCache::CreateImage(const TextureUploadDesc& desc, const Prepared& p,
                                     Entry& out) {
    const u32 mips = MipCountFor(desc.width, desc.height);

    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = p.mutableFormat ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = p.storageFormat;
    info.extent = {desc.width, desc.height, 1};
    info.mipLevels = mips;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &info, nullptr, &out.image) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_device, out.image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex =
        FindMemoryType(m_physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &alloc, nullptr, &out.memory) != VK_SUCCESS) {
        DestroyEntry(out);
        return false;
    }
    vkBindImageMemory(m_device, out.image, out.memory, 0);

    GpuBuffer staging = CreateBuffer(
        m_physical, m_device, p.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    if (!staging.mapped) {
        DestroyBuffer(m_device, staging);
        DestroyEntry(out);
        return false;
    }
    std::memcpy(staging.mapped, p.pixels.data(), p.pixels.size());

    VkCommandBuffer cmd = BeginOneTimeCommands(m_device, m_pool);
    ImageBarrier(cmd, out.image, 0, mips, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {desc.width, desc.height, 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copy);

    i32 w = static_cast<i32>(desc.width);
    i32 h = static_cast<i32>(desc.height);
    for (u32 mip = 1; mip < mips; ++mip) {
        ImageBarrier(cmd, out.image, mip - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
        const i32 nw = std::max(w / 2, 1);
        const i32 nh = std::max(h / 2, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
        blit.srcOffsets[1] = {w, h, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
        blit.dstOffsets[1] = {nw, nh, 1};
        vkCmdBlitImage(cmd, out.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        ImageBarrier(cmd, out.image, mip - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        w = nw;
        h = nh;
    }
    ImageBarrier(cmd, out.image, mips - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    EndOneTimeCommands(m_device, m_pool, m_queue, cmd);
    DestroyBuffer(m_device, staging);

    auto makeView = [&](VkFormat format, VkImageView* view) {
        VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        v.image = out.image;
        v.viewType = VK_IMAGE_VIEW_TYPE_2D;
        v.format = format;
        v.components = p.swizzle;
        v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
        return vkCreateImageView(m_device, &v, nullptr, view) == VK_SUCCESS;
    };
    if (!makeView(p.linearFormat, &out.linearView) || !makeView(p.srgbFormat, &out.srgbView)) {
        DestroyEntry(out);
        return false;
    }
    out.mipCount = mips;
    return true;
}

void VulkanTextureCache::WriteDescriptors(u32 slot, VkImageView linearView,
                                          VkImageView srgbView) {
    VkDescriptorImageInfo images[2] = {
        {VK_NULL_HANDLE, linearView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, srgbView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_set;
    write.dstBinding = 0;
    write.dstArrayElement = slot * 2;
    write.descriptorCount = 2;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = images;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VulkanTextureCache::DestroyEntry(Entry& e) {
    if (e.linearView) vkDestroyImageView(m_device, e.linearView, nullptr);
    if (e.srgbView) vkDestroyImageView(m_device, e.srgbView, nullptr);
    if (e.image) vkDestroyImage(m_device, e.image, nullptr);
    if (e.memory) vkFreeMemory(m_device, e.memory, nullptr);
    const u16 generation = e.generation;
    e = Entry{};
    e.generation = generation;
}

u32 VulkanTextureCache::SlotOf(GpuTextureHandle handle) const {
    if (handle == kInvalidHandle) return kInvalidHandle;
    const u32 slot = handle & 0xFFFFu;
    const u32 generation = handle >> 16;
    if (slot >= m_entries.size()) return kInvalidHandle;
    const Entry& e = m_entries[slot];
    return e.alive && e.generation == generation ? slot : kInvalidHandle;
}

GpuTextureHandle VulkanTextureCache::MakeHandle(u32 slot) const {
    return slot | (static_cast<u32>(m_entries[slot].generation) << 16);
}

u32 VulkanTextureCache::AcquireSlot() {
    if (!m_freeSlots.empty()) {
        const u32 slot = m_freeSlots.back();
        m_freeSlots.pop_back();
        return slot;
    }
    if (m_entries.size() >= m_slotCapacity) return kInvalidHandle;
    m_entries.emplace_back();
    return static_cast<u32>(m_entries.size() - 1);
}

GpuTextureHandle VulkanTextureCache::Upload(u64 key, const TextureUploadDesc& desc) {
    if (!IsValid()) return kInvalidHandle;
    if (auto it = m_byKey.find(key); it != m_byKey.end()) return it->second;
    if (desc.width == 0 || desc.height == 0 || desc.pixels == nullptr) {
        LUMA_LOG_ERROR("TextureCache", "Rejected empty texture upload");
        return kInvalidHandle;
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physical, &props);
    if (desc.width > props.limits.maxImageDimension2D ||
        desc.height > props.limits.maxImageDimension2D) {
        LUMA_LOG_ERROR("TextureCache", "Texture {}x{} exceeds the GPU limit of {}",
                       desc.width, desc.height, props.limits.maxImageDimension2D);
        return kInvalidHandle;
    }
    const u32 slot = AcquireSlot();
    if (slot == kInvalidHandle) {
        LUMA_LOG_ERROR("TextureCache", "Texture table full ({} textures)", m_slotCapacity);
        return kInvalidHandle;
    }
    Entry entry;
    if (!CreateImage(desc, Prepare(desc), entry)) {
        LUMA_LOG_ERROR("TextureCache", "Failed to create a {}x{} texture", desc.width,
                       desc.height);
        m_freeSlots.push_back(slot);
        return kInvalidHandle;
    }
    entry.key = key;
    entry.alive = true;
    entry.generation = m_entries[slot].generation;
    m_entries[slot] = entry;
    WriteDescriptors(slot, entry.linearView, entry.srgbView);
    const GpuTextureHandle handle = MakeHandle(slot);
    m_byKey[key] = handle;
    return handle;
}

void VulkanTextureCache::Release(GpuTextureHandle handle) {
    const u32 slot = SlotOf(handle);
    if (slot == kInvalidHandle || slot == 0) return;  // the white texture stays
    Entry& e = m_entries[slot];
    e.alive = false;
    m_byKey.erase(e.key);
    m_pendingRelease.push_back(slot);
}

void VulkanTextureCache::CollectGarbage() {
    if (m_pendingRelease.empty()) return;
    const Entry& white = m_entries[0];
    for (u32 slot : m_pendingRelease) {
        WriteDescriptors(slot, white.linearView, white.srgbView);
        DestroyEntry(m_entries[slot]);
        ++m_entries[slot].generation;  // stale handles stop resolving
        m_freeSlots.push_back(slot);
    }
    m_pendingRelease.clear();
}

u32 VulkanTextureCache::BindlessIndex(GpuTextureHandle handle, GpuTextureView view) const {
    u32 slot = SlotOf(handle);
    if (slot == kInvalidHandle) slot = 0;
    return slot * 2 + (view == GpuTextureView::SRGB ? 1u : 0u);
}

u32 VulkanTextureCache::MipCount(GpuTextureHandle handle) const {
    const u32 slot = SlotOf(handle);
    return slot == kInvalidHandle ? 0 : m_entries[slot].mipCount;
}

}  // namespace Luma
