#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "Luma/RHI/MaterialGpuDesc.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/RHI/SceneRenderer.h"

using namespace Luma;

TEST_CASE("MaterialGpuDesc defaults reference no textures", "[render][material]") {
    const MaterialGpuDesc desc;
    for (const MaterialGpuDesc::Input& input : desc.inputs) {
        CHECK(input.texture == kInvalidHandle);
    }
    CHECK(desc.inputs.size() == kMaterialGpuInputCount);
    CHECK(desc.opacityMaskClip == 0.3333f);
    CHECK(desc.blend == GpuBlendMode::Opaque);
    CHECK(desc.shading == GpuShadingModel::DefaultLit);
}

TEST_CASE("Default GPU material carries the Default Material values", "[render][material]") {
    const MaterialGpuDesc d = MakeDefaultMaterialGpuDesc();
    const auto& base = d.inputs[static_cast<usize>(GpuMaterialInput::BaseColor)].constant;
    CHECK((base.x == 0.82f && base.y == 0.82f && base.z == 0.85f));
    CHECK(d.inputs[static_cast<usize>(GpuMaterialInput::Roughness)].constant.x == 0.5f);
    CHECK(d.inputs[static_cast<usize>(GpuMaterialInput::Specular)].constant.x == 0.5f);
    CHECK(d.inputs[static_cast<usize>(GpuMaterialInput::Metallic)].constant.x == 0.0f);
    const auto& n = d.inputs[static_cast<usize>(GpuMaterialInput::Normal)].constant;
    CHECK((n.x == 0.0f && n.y == 0.0f && n.z == 1.0f));
}

TEST_CASE("SceneInstance references a mesh and per-section materials", "[render][material]") {
    const SceneInstance inst;
    CHECK(inst.mesh == kInvalidHandle);
    CHECK(inst.sectionCount == 0);
    CHECK_FALSE(inst.selected);
    CHECK(inst.sectionMaterials.size() == kMaxSectionMaterials);
    for (MaterialHandle h : inst.sectionMaterials) CHECK(h == kDefaultMaterialHandle);
    STATIC_REQUIRE(std::is_abstract_v<SceneRenderer>);
    STATIC_REQUIRE(sizeof(MeshVertex) == 48);
}
