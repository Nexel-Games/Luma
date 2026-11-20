#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Luma/Asset/AssetType.h"
#include "Luma/Material/MaterialInstanceSerializer.h"
#include "Luma/Material/MaterialSerializer.h"
#include "Luma/Serialization/Json.h"

using namespace Luma::Material;
namespace fs = std::filesystem;

namespace {

// A material with every field set away from its default.
Material MakeRichMaterial() {
    Material m = Material::MakeDefault();
    m.name = "M_Barrel";
    m.settings.blendMode = BlendMode::Masked;
    m.settings.shadingModel = ShadingModel::Unlit;
    m.settings.twoSided = true;
    m.settings.opacityMaskClip = 0.42f;
    auto& base = m.Input(MaterialParameter::BaseColor);
    base.constant = {0.1f, 0.2f, 0.3f, 1.0f};
    base.texture = "/Game/Barrel/textures/barrel_02_diff_2k";
    base.tint = {0.9f, 0.8f, 0.7f, 1.0f};
    base.uvTiling = {2.0f, 3.0f};
    base.uvOffset = {0.25f, 0.5f};
    auto& normal = m.Input(MaterialParameter::Normal);
    normal.texture = "/Game/Barrel/textures/barrel_02_nor_dx_2k";
    normal.normalStrength = 0.6f;
    normal.flipGreen = true;
    auto& rough = m.Input(MaterialParameter::Roughness);
    rough.texture = "/Game/Barrel/textures/barrel_02_arm_2k";
    rough.channel = TextureChannel::G;
    auto& emissive = m.Input(MaterialParameter::EmissiveColor);
    emissive.constant = {1.0f, 0.5f, 0.0f, 0.0f};
    emissive.emissiveIntensity = 8.0f;
    return m;
}

void RequireInputsEqual(const MaterialInput& a, const MaterialInput& b) {
    CHECK(a.constant.x == b.constant.x);
    CHECK(a.constant.y == b.constant.y);
    CHECK(a.constant.z == b.constant.z);
    CHECK(a.constant.w == b.constant.w);
    CHECK(a.texture == b.texture);
    CHECK(a.channel == b.channel);
    CHECK(a.tint.x == b.tint.x);
    CHECK(a.tint.w == b.tint.w);
    CHECK(a.uvTiling.x == b.uvTiling.x);
    CHECK(a.uvTiling.y == b.uvTiling.y);
    CHECK(a.uvOffset.x == b.uvOffset.x);
    CHECK(a.uvOffset.y == b.uvOffset.y);
    CHECK(a.normalStrength == b.normalStrength);
    CHECK(a.flipGreen == b.flipGreen);
    CHECK(a.emissiveIntensity == b.emissiveIntensity);
}

}  // namespace

TEST_CASE("Material round-trips every field through JSON",
          "[material][serialization]") {
    const Material m = MakeRichMaterial();
    Material back;
    std::string err;
    REQUIRE(MaterialSerializer::FromJson(back, MaterialSerializer::ToJson(m), &err));
    CHECK(back.name == "M_Barrel");
    CHECK(back.settings.blendMode == BlendMode::Masked);
    CHECK(back.settings.shadingModel == ShadingModel::Unlit);
    CHECK(back.settings.twoSided);
    CHECK(back.settings.opacityMaskClip == 0.42f);
    for (Luma::usize i = 0; i < kMaterialParameterCount; ++i) {
        RequireInputsEqual(back.inputs[i], m.inputs[i]);
    }
}

TEST_CASE("Material round-trips through a file", "[material][serialization]") {
    const fs::path path = fs::temp_directory_path() / "luma_m_roundtrip.lmat";
    const Material m = MakeRichMaterial();
    std::string err;
    REQUIRE(MaterialSerializer::SaveToFile(m, path, &err));
    Material back;
    REQUIRE(MaterialSerializer::LoadFromFile(back, path, &err));
    CHECK(back.Input(MaterialParameter::Normal).flipGreen);
    CHECK(back.Input(MaterialParameter::BaseColor).texture ==
          "/Game/Barrel/textures/barrel_02_diff_2k");
    fs::remove(path);
}

TEST_CASE("Legacy .lmat (constants layout) loads its constants",
          "[material][serialization]") {
    // Shape of TestProject/Assets/Materials/TestMaterial.lmat (version 1).
    const char* legacy = R"({
        "version": 1, "name": "TestMaterial", "blendMode": 0,
        "opacityThreshold": 0.5, "maps": {},
        "constants": {
            "baseColor": [0.5, 0.5, 0.5], "metallic": 0.0, "specular": 0.5,
            "roughness": 0.5, "normal": [0.0, 0.0, 1.0],
            "emissive": [0.0, 0.0, 0.0], "opacity": 1.0, "opacityMask": 1.0,
            "ambientOcclusion": 1.0
        },
        "properties": {"baseColor": {"expr": 1, "out": 0}},
        "graph": {"nextId": 5, "nodes": []}
    })";
    auto parsed = Luma::ParseJson(legacy);
    REQUIRE(parsed);
    Material m;
    std::string err;
    REQUIRE(MaterialSerializer::FromJson(m, *parsed, &err));
    CHECK(m.name == "TestMaterial");
    CHECK(m.Input(MaterialParameter::BaseColor).constant.x == 0.5f);
    CHECK(m.Input(MaterialParameter::Roughness).constant.x == 0.5f);
    CHECK(m.Input(MaterialParameter::Specular).constant.x == 0.5f);
    CHECK(m.Input(MaterialParameter::BaseColor).texture.empty());
    CHECK(m.settings.blendMode == BlendMode::Opaque);
}

TEST_CASE("Unknown keys are ignored and malformed input is rejected",
          "[material][serialization]") {
    auto parsed = Luma::ParseJson(R"({"version": 3, "name": "X", "futureKey": 7,
        "inputs": {"roughness": {"constant": [0.8, 0, 0, 0], "futureField": 1}}})");
    REQUIRE(parsed);
    Material m;
    REQUIRE(MaterialSerializer::FromJson(m, *parsed));
    CHECK(m.Input(MaterialParameter::Roughness).constant.x == 0.8f);
    CHECK(m.Input(MaterialParameter::BaseColor).constant.x == 0.82f);  // default kept

    const fs::path bad = fs::temp_directory_path() / "luma_m_bad.lmat";
    { std::FILE* f = std::fopen(bad.string().c_str(), "wb"); std::fputs("{ not json", f); std::fclose(f); }
    std::string err;
    CHECK_FALSE(MaterialSerializer::LoadFromFile(m, bad, &err));
    CHECK_FALSE(err.empty());
    fs::remove(bad);
}

TEST_CASE("Material instance round-trips overrides and optional settings",
          "[material][serialization]") {
    MaterialInstance mi;
    mi.name = "MI_Barrel_Red";
    mi.parent = "/Game/Materials/M_Barrel";
    MaterialInput red = Material::DefaultInputs()[0];
    red.constant = {1.0f, 0.0f, 0.0f, 1.0f};
    mi.Override(MaterialParameter::BaseColor, red);
    mi.twoSided = true;

    MaterialInstance back;
    std::string err;
    REQUIRE(MaterialInstanceSerializer::FromJson(
        back, MaterialInstanceSerializer::ToJson(mi), &err));
    CHECK(back.name == "MI_Barrel_Red");
    CHECK(back.parent == "/Game/Materials/M_Barrel");
    CHECK(back.IsOverridden(MaterialParameter::BaseColor));
    CHECK_FALSE(back.IsOverridden(MaterialParameter::Roughness));
    CHECK(back.inputs[0].constant.x == 1.0f);
    REQUIRE(back.twoSided.has_value());
    CHECK(*back.twoSided);
    CHECK_FALSE(back.opacityMaskClip.has_value());
    CHECK_FALSE(back.blendMode.has_value());
}

TEST_CASE(".lmatinst is the Material Instance asset type", "[material][serialization]") {
    CHECK(Luma::AssetTypeFromExtension("lmatinst") == Luma::AssetType::MaterialInstance);
    CHECK(Luma::AssetTypeFromExtension("lmat") == Luma::AssetType::Material);
    CHECK(std::string(Luma::AssetTypeName(Luma::AssetType::MaterialInstance)) ==
          "MaterialInstance");
}
