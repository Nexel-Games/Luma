#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "VulkanTestFixture.h"

// The Content Browser creates one UI texture per thumbnail; a folder of a few
// hundred imported assets must not run the UI descriptor pool dry.
TEST_CASE("The UI pass hands out hundreds of textures", "[rendering][vulkan][ui]") {
    VulkanTestFixture f;
    const std::uint32_t pixel = 0xFF8040FFu;
    std::vector<Luma::TextureHandle> handles;
    for (int i = 0; i < 400; ++i) {
        const Luma::TextureHandle handle = f.renderer->CreateTexture(1, 1, &pixel);
        REQUIRE(handle != 0);
        handles.push_back(handle);
    }
    for (Luma::TextureHandle handle : handles) f.renderer->DestroyTexture(handle);
    // Freed sets are reused.
    for (int i = 0; i < 50; ++i) REQUIRE(f.renderer->CreateTexture(1, 1, &pixel) != 0);
    REQUIRE(f.ValidationErrors().empty());
}
