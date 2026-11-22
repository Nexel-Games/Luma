// The shared material pipeline layout (set 0/1/2 + push constants) and the
// texture cache's set 1: sampler table, view pairs, per-frame buffer growth.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "VulkanTestFixture.h"
#include "Vulkan/Materials/VulkanMaterialLayout.h"
#include "Vulkan/Textures/VulkanTextureCache.h"

using namespace Luma;

TEST_CASE("The sampler table has 12 entries in filter x address order", "[vulkan][material]") {
    VulkanTestFixture f;
    VulkanTextureCache cache(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    REQUIRE(cache.IsValid());
    const VkSamplerAddressMode modes[3] = {VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                           VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT};
    for (u32 i = 0; i < kGpuSamplerCount; ++i) {
        const VkSamplerCreateInfo& s = cache.SamplerInfo(i);
        CHECK(s.addressModeU == modes[i % 3]);
        const u32 filter = i / 3;
        CHECK(s.magFilter == (filter == 1 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR));
        CHECK((s.anisotropyEnable == VK_TRUE) == (filter == 2));
        CHECK((s.maxLod == 0.0f) == (filter == 3));
    }
    CHECK(f.ValidationErrors().empty());
}

TEST_CASE("Texture views stay paired linear / sRGB at 2i / 2i+1", "[vulkan][material]") {
    VulkanTestFixture f;
    VulkanTextureCache cache(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    std::vector<u8> px(16, 200);
    TextureUploadDesc d;
    d.width = d.height = 2;
    d.pixels = px.data();
    const GpuTextureHandle h = cache.Upload(1, d);
    REQUIRE(h != kInvalidHandle);
    const u32 linear = cache.BindlessIndex(h, GpuTextureView::Linear);
    CHECK(linear % 2 == 0);
    CHECK(cache.BindlessIndex(h, GpuTextureView::SRGB) == linear + 1);
    CHECK(cache.WhiteView() != VK_NULL_HANDLE);
    CHECK(f.ValidationErrors().empty());
}

TEST_CASE("The material pipeline layout pushes exactly 128 bytes", "[vulkan][material]") {
    VulkanTestFixture f;
    VulkanTextureCache cache(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    VulkanMaterialLayout layout(f.Physical(), f.Logical(), cache.Layout());
    REQUIRE(layout.IsValid());
    CHECK(layout.PushConstantBytes() == 128);
    CHECK(f.ValidationErrors().empty());
}

TEST_CASE("Parameter blocks don't overlap and survive buffer growth", "[vulkan][material]") {
    VulkanTestFixture f;
    VulkanTextureCache cache(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    VulkanMaterialLayout layout(f.Physical(), f.Logical(), cache.Layout());
    layout.BeginFrame(0);
    const VkDeviceSize before = layout.ParamBufferSize(0);
    std::vector<u32> offsets;
    std::vector<Math::Vec4> block(100);
    for (u32 i = 0; i < 40; ++i) {  // 4000 vec4s: more than the initial buffer
        for (Math::Vec4& v : block) v = {static_cast<f32>(i), 0, 0, 0};
        offsets.push_back(layout.AddParams(block));
    }
    REQUIRE(layout.EndFrame());
    CHECK(layout.ParamBufferSize(0) > before);
    for (usize i = 1; i < offsets.size(); ++i) CHECK(offsets[i] == offsets[i - 1] + 100);
    for (usize i = 0; i < offsets.size(); ++i) CHECK(layout.PendingParams()[offsets[i] + 99].x == static_cast<f32>(i));
    // The other frame slot keeps its own buffer: offsets a frame in flight
    // reads stay valid.
    CHECK(layout.ParamBufferSize(1) == before);
    CHECK(f.ValidationErrors().empty());
}
