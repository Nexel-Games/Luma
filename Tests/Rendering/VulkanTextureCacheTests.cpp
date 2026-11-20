#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "VulkanTestFixture.h"
#include "Vulkan/Textures/VulkanTextureCache.h"

using namespace Luma;

namespace {

std::vector<u8> Pixels(u32 width, u32 height, u32 bytesPerPixel, u8 value = 200) {
    return std::vector<u8>(static_cast<usize>(width) * height * bytesPerPixel, value);
}

TextureUploadDesc Desc(u32 w, u32 h, TexturePixelFormat f, const std::vector<u8>& px,
                       bool srgb = true) {
    TextureUploadDesc d;
    d.width = w;
    d.height = h;
    d.format = f;
    d.pixels = px.data();
    d.srgb = srgb;
    return d;
}

}  // namespace

TEST_CASE("Texture cache builds a full mip chain", "[vulkan][texture]") {
    VulkanTestFixture f;
    VulkanTextureCache cache(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    REQUIRE(cache.IsValid());

    const auto px = Pixels(300, 200, 4);
    const GpuTextureHandle h = cache.Upload(1, Desc(300, 200, TexturePixelFormat::RGBA8, px));
    REQUIRE(h != kInvalidHandle);
    CHECK(cache.MipCount(h) == 9);
    CHECK(VulkanTextureCache::MipCountFor(8192, 8192) == 14);
    CHECK(VulkanTextureCache::MipCountFor(1, 1) == 1);
    CHECK(f.ValidationErrors().empty());
}

TEST_CASE("Texture cache uploads an 8192 square texture", "[vulkan][texture]") {
    VulkanTestFixture f;
    VulkanTextureCache cache(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    const auto px = Pixels(8192, 8192, 4);
    const GpuTextureHandle h = cache.Upload(7, Desc(8192, 8192, TexturePixelFormat::RGBA8, px));
    REQUIRE(h != kInvalidHandle);
    CHECK(cache.MipCount(h) == 14);
    CHECK(f.ValidationErrors().empty());
}

TEST_CASE("Texture cache accepts gray, gray-alpha and 16-bit images", "[vulkan][texture]") {
    VulkanTestFixture f;
    VulkanTextureCache cache(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    const auto r8 = Pixels(33, 17, 1);
    const auto rg8 = Pixels(64, 64, 2);
    const auto r16 = Pixels(20, 40, 2);
    const auto rgba16 = Pixels(128, 32, 8);
    CHECK(cache.Upload(1, Desc(33, 17, TexturePixelFormat::R8, r8)) != kInvalidHandle);
    CHECK(cache.Upload(2, Desc(64, 64, TexturePixelFormat::RG8, rg8)) != kInvalidHandle);
    CHECK(cache.Upload(3, Desc(20, 40, TexturePixelFormat::R16, r16, false)) != kInvalidHandle);
    CHECK(cache.Upload(4, Desc(128, 32, TexturePixelFormat::RGBA16, rgba16, false)) != kInvalidHandle);
    CHECK(cache.Upload(5, Desc(128, 32, TexturePixelFormat::RGBA16, rgba16, true)) != kInvalidHandle);
    CHECK(cache.MipCount(cache.Upload(3, Desc(20, 40, TexturePixelFormat::R16, r16, false))) == 6);
    CHECK(f.ValidationErrors().empty());
}

TEST_CASE("Texture cache exposes distinct sRGB and linear views", "[vulkan][texture]") {
    VulkanTestFixture f;
    VulkanTextureCache cache(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    const auto px = Pixels(16, 16, 4);
    const GpuTextureHandle h = cache.Upload(1, Desc(16, 16, TexturePixelFormat::RGBA8, px));
    const u32 srgb = cache.BindlessIndex(h, GpuTextureView::SRGB);
    const u32 linear = cache.BindlessIndex(h, GpuTextureView::Linear);
    CHECK(srgb != linear);
    CHECK(srgb > 1);
    CHECK(linear > 1);
    // Invalid handles resolve to the reserved 1x1 white texture.
    CHECK(cache.BindlessIndex(kInvalidHandle, GpuTextureView::Linear) == 0);
    CHECK(cache.BindlessIndex(kInvalidHandle, GpuTextureView::SRGB) == 1);
}

TEST_CASE("Texture cache dedupes keys and reuses released slots", "[vulkan][texture]") {
    VulkanTestFixture f;
    VulkanTextureCache cache(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    const auto px = Pixels(8, 8, 4);
    const GpuTextureHandle a = cache.Upload(10, Desc(8, 8, TexturePixelFormat::RGBA8, px));
    CHECK(cache.Upload(10, Desc(8, 8, TexturePixelFormat::RGBA8, px)) == a);

    const u32 index = cache.BindlessIndex(a, GpuTextureView::Linear);
    cache.Release(a);
    cache.CollectGarbage();
    const GpuTextureHandle b = cache.Upload(11, Desc(8, 8, TexturePixelFormat::RGBA8, px));
    CHECK(cache.BindlessIndex(b, GpuTextureView::Linear) == index);
    CHECK(f.ValidationErrors().empty());
}

TEST_CASE("Texture cache rejects empty uploads", "[vulkan][texture]") {
    VulkanTestFixture f;
    VulkanTextureCache cache(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    TextureUploadDesc d;
    CHECK(cache.Upload(1, d) == kInvalidHandle);
}
