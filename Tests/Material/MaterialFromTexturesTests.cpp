#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Luma/Material/MaterialFromTextures.h"

using Luma::Material::BlendMode;
using Luma::Material::BuildMaterialFromTextures;
using Luma::Material::MaterialParameter;
using Luma::Material::TextureChannel;

namespace {
const Luma::Material::MaterialInput& In(const Luma::Material::Material& m, MaterialParameter p) {
    return m.inputs[static_cast<size_t>(p)];
}
}  // namespace

TEST_CASE("Texture set maps by name tokens", "[material][fromtextures]") {
    const auto r = BuildMaterialFromTextures({"/Game/Textures/barrel_02_diff_2k",
                                              "/Game/Textures/barrel_02_nor_gl_2k",
                                              "/Game/Textures/barrel_02_rough_2k",
                                              "/Game/Textures/barrel_02_metal_2k"});
    CHECK(r.ignored.empty());
    CHECK(In(r.material, MaterialParameter::BaseColor).texture == "/Game/Textures/barrel_02_diff_2k");
    CHECK(In(r.material, MaterialParameter::Normal).texture == "/Game/Textures/barrel_02_nor_gl_2k");
    CHECK_FALSE(In(r.material, MaterialParameter::Normal).flipGreen);
    CHECK(In(r.material, MaterialParameter::Roughness).texture == "/Game/Textures/barrel_02_rough_2k");
    CHECK(In(r.material, MaterialParameter::Roughness).channel == TextureChannel::R);
    CHECK(In(r.material, MaterialParameter::Metallic).texture == "/Game/Textures/barrel_02_metal_2k");
    // Texture-driven inputs: the constant no longer scales the map.
    CHECK(In(r.material, MaterialParameter::Metallic).tint.x == 1.0f);
    CHECK(r.suggestedName == "M_barrel_02");
}

TEST_CASE("DirectX normal maps flip green", "[material][fromtextures]") {
    const auto r = BuildMaterialFromTextures({"/Game/T/Brick_Normal_DX"});
    CHECK(In(r.material, MaterialParameter::Normal).flipGreen);
}

TEST_CASE("Packed ARM maps feed AO, roughness and metallic channels", "[material][fromtextures]") {
    const auto r = BuildMaterialFromTextures({"/Game/T/T_Rock_ARM", "/Game/T/T_Rock_BaseColor"});
    CHECK(In(r.material, MaterialParameter::AmbientOcclusion).channel == TextureChannel::R);
    CHECK(In(r.material, MaterialParameter::Roughness).channel == TextureChannel::G);
    CHECK(In(r.material, MaterialParameter::Metallic).channel == TextureChannel::B);
    CHECK(In(r.material, MaterialParameter::Metallic).texture == "/Game/T/T_Rock_ARM");
    CHECK(In(r.material, MaterialParameter::BaseColor).texture == "/Game/T/T_Rock_BaseColor");
    CHECK(r.suggestedName == "M_Rock");
}

TEST_CASE("Opacity maps make the material masked", "[material][fromtextures]") {
    const auto r = BuildMaterialFromTextures({"/Game/T/leaf_alpha", "/Game/T/leaf_color"});
    CHECK(r.material.settings.blendMode == BlendMode::Masked);
    CHECK(In(r.material, MaterialParameter::OpacityMask).texture == "/Game/T/leaf_alpha");
    CHECK(In(r.material, MaterialParameter::BaseColor).texture == "/Game/T/leaf_color");
}

TEST_CASE("Unknown texture names are ignored and reported", "[material][fromtextures]") {
    const auto r = BuildMaterialFromTextures({"/Game/T/wood_diff", "/Game/T/wood_height",
                                              "/Game/T/random_thing"});
    CHECK(r.ignored == std::vector<std::string>{"/Game/T/wood_height", "/Game/T/random_thing"});
    CHECK(In(r.material, MaterialParameter::BaseColor).texture == "/Game/T/wood_diff");
}
