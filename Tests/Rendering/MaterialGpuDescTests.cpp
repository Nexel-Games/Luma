#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "Luma/RHI/MaterialGpuDesc.h"
#include "Luma/RHI/Renderer.h"
#include "Luma/RHI/SceneRenderer.h"

using namespace Luma;

TEST_CASE("Compiled material GPU enums cover every pass, blend mode and shading model", "[rhi][material]") {
    STATIC_REQUIRE(static_cast<usize>(GpuPass::Count) == 5);
    STATIC_REQUIRE(kGpuPassCount == 5);
    STATIC_REQUIRE(static_cast<u8>(GpuPass::GBuffer) == 0);
    STATIC_REQUIRE(static_cast<u8>(GpuPass::PostProcess) == 4);
    STATIC_REQUIRE(static_cast<u8>(GpuBlendMode::Modulate) == 4);
    STATIC_REQUIRE(static_cast<u8>(GpuShadingModel::TwoSidedFoliage) == 4);
    STATIC_REQUIRE(static_cast<u8>(GpuPostProcessLocation::AfterTonemapping) == 1);
}

TEST_CASE("A default compiled material has no passes", "[rhi][material]") {
    const CompiledMaterialGpu m;
    CHECK(m.shaderKey == 0);
    for (const auto& pass : m.passes) CHECK_FALSE(pass.has_value());
    CHECK_FALSE(m.HasPass(GpuPass::GBuffer));
    CHECK(m.blend == GpuBlendMode::Opaque);
    CHECK(m.shading == GpuShadingModel::DefaultLit);
    CHECK(m.textures.empty());
    CHECK(m.opacityMaskClip == 0.3333f);
}

TEST_CASE("Parameter blocks compare by value", "[rhi][material]") {
    MaterialParameterBlock a;
    a.values = {{1, 2, 3, 4}, {5, 6, 7, 8}};
    MaterialParameterBlock b = a;
    CHECK(a == b);
    b.values[1].w = 9;
    CHECK_FALSE(a == b);
    b.values.pop_back();
    CHECK_FALSE(a == b);
}

TEST_CASE("Sampler table indices follow filter x address, then the unmipped rows", "[rhi][material]") {
    CHECK(GpuSamplerIndex(GpuSamplerFilter::Linear, GpuSamplerAddress::Wrap) == 0);
    CHECK(GpuSamplerIndex(GpuSamplerFilter::Linear, GpuSamplerAddress::Mirror) == 2);
    CHECK(GpuSamplerIndex(GpuSamplerFilter::Nearest, GpuSamplerAddress::Clamp) == 4);
    CHECK(GpuSamplerIndex(GpuSamplerFilter::Anisotropic, GpuSamplerAddress::Wrap) == 6);
    CHECK(GpuSamplerIndex(GpuSamplerFilter::LinearNoMips, GpuSamplerAddress::Clamp) == 10);
    STATIC_REQUIRE(kGpuSamplerCount == 12);
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
