#include <catch2/catch_test_macros.hpp>

#include "Luma/Asset/TextureColorSpace.h"
#include "Luma/Material/MaterialTypes.h"

TEST_CASE("Texture color space is inferred from token-delimited names", "[texture]") {
    struct Row { const char* name; bool srgb; };
    const Row rows[] = {
        {"Barrel_02_diff_2k.png", true},
        {"barrel_02_nor_gl_2k.png", false},
        {"T_Rock_ORM.png", false},
        {"Snormal.png", true},  // "normal" is not token-delimited here
        {"rock-Roughness.jpg", false},
        {"metal plate.metal.png", false},
        {"wall_AO.tga", false},
        {"leaf_opacity.png", false},
        {"T_Brick_BaseColor.png", true},
        // A color token wins over data words that are part of the asset name.
        {"rusty_metal_02_diff_4k.png", true},
        {"rough_plaster_diff.png", true},
        {"metal_plate_albedo.jpg", true},
        {"rough_plaster_rough.png", false},
        {"Rocky_Ground_2K_Cavity.jpg", false},
        {"Rocky_Ground_2K_Displacement.exr", false},
        {"Rocky_Ground_2K_Bump.jpg", false},
        {"Rocky_Ground_2K_Gloss.jpg", false},
        {"Rocky_Ground_2K_Specular.jpg", false},
    };
    for (const Row& r : rows) {
        INFO(r.name);
        CHECK(Luma::IsTextureSRGB(r.name, nullptr) == r.srgb);
    }
}

TEST_CASE("Texture metadata sRGB flag overrides the name", "[texture]") {
    Luma::LumaTextureMetadata meta;
    meta.isSRGB = false;
    CHECK_FALSE(Luma::IsTextureSRGB("Barrel_02_diff_2k.png", &meta));
    meta.isSRGB = true;
    CHECK(Luma::IsTextureSRGB("barrel_02_nor_gl_2k.png", &meta));
}

TEST_CASE("Texture view is sRGB only for color parameters", "[texture]") {
    using namespace Luma::Material;
    CHECK(SelectTextureView(MaterialParameter::Normal, true) == TextureView::Linear);
    CHECK(SelectTextureView(MaterialParameter::BaseColor, true) == TextureView::SRGB);
    CHECK(SelectTextureView(MaterialParameter::BaseColor, false) == TextureView::Linear);
    CHECK(SelectTextureView(MaterialParameter::Roughness, true) == TextureView::Linear);
}

