#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Luma/Material/Material.h"
#include "Luma/Material/MaterialInstance.h"

using namespace Luma::Material;

TEST_CASE("Default material matches legacy component defaults", "[material]") {
    const Material m = Material::MakeDefault();
    const auto& base = m.inputs[static_cast<size_t>(MaterialParameter::BaseColor)];
    CHECK(base.constant.x == 0.82f);
    CHECK(base.constant.y == 0.82f);
    CHECK(base.constant.z == 0.85f);
    CHECK(m.inputs[static_cast<size_t>(MaterialParameter::Metallic)].constant.x == 0.0f);
    CHECK(m.inputs[static_cast<size_t>(MaterialParameter::Roughness)].constant.x == 0.5f);
    CHECK(m.inputs[static_cast<size_t>(MaterialParameter::Specular)].constant.x == 0.5f);
    CHECK(m.inputs[static_cast<size_t>(MaterialParameter::AmbientOcclusion)].constant.x == 1.0f);
    CHECK(m.inputs[static_cast<size_t>(MaterialParameter::OpacityMask)].constant.x == 1.0f);
    const auto& n = m.inputs[static_cast<size_t>(MaterialParameter::Normal)].constant;
    CHECK((n.x == 0.0f && n.y == 0.0f && n.z == 1.0f));
    const auto& e = m.inputs[static_cast<size_t>(MaterialParameter::EmissiveColor)].constant;
    CHECK((e.x == 0.0f && e.y == 0.0f && e.z == 0.0f));
    CHECK(m.settings.opacityMaskClip == 0.3333f);
    CHECK(m.settings.blendMode == BlendMode::Opaque);
    CHECK(m.settings.shadingModel == ShadingModel::DefaultLit);
    CHECK_FALSE(m.settings.twoSided);
    for (const auto& input : m.inputs) CHECK(input.texture.empty());
}

TEST_CASE("Material parameters have stable keys and color classification",
          "[material]") {
    CHECK(std::string(ParameterKey(MaterialParameter::BaseColor)) == "baseColor");
    CHECK(std::string(ParameterKey(MaterialParameter::EmissiveColor)) == "emissiveColor");
    CHECK(std::string(ParameterKey(MaterialParameter::AmbientOcclusion)) == "ambientOcclusion");
    CHECK(std::string(ParameterKey(MaterialParameter::OpacityMask)) == "opacityMask");
    CHECK(IsColorParameter(MaterialParameter::BaseColor));
    CHECK(IsColorParameter(MaterialParameter::EmissiveColor));
    CHECK_FALSE(IsColorParameter(MaterialParameter::Normal));
    CHECK_FALSE(IsColorParameter(MaterialParameter::Roughness));
}

TEST_CASE("Material instance starts with nothing overridden", "[material]") {
    MaterialInstance mi;
    for (bool o : mi.overridden) CHECK_FALSE(o);
    CHECK_FALSE(mi.twoSided.has_value());
    CHECK_FALSE(mi.opacityMaskClip.has_value());
    CHECK_FALSE(mi.blendMode.has_value());
    CHECK(mi.parent.empty());
}
