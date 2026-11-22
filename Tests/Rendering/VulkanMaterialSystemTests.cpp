#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <vector>

#include "VulkanTestFixture.h"
#include "Luma/Mesh/Mesh.h"
#include "Vulkan/Materials/VulkanMaterialLayout.h"
#include "Vulkan/Materials/VulkanMaterialPipelines.h"
#include "Vulkan/Materials/VulkanMaterialSystem.h"
#include "Vulkan/Meshes/VulkanMeshCache.h"
#include "Vulkan/Textures/VulkanTextureCache.h"

using namespace Luma;

namespace {

GpuTextureHandle UploadWhite(VulkanTextureCache& cache, u64 key) {
    static const std::vector<u8> px(4 * 4 * 4, 255);
    TextureUploadDesc d;
    d.width = d.height = 4;
    d.format = TexturePixelFormat::RGBA8;
    d.pixels = px.data();
    return cache.Upload(key, d);
}

struct MaterialRig {
    VulkanTestFixture f;
    VulkanTextureCache textures{f.Physical(), f.Logical(), f.Queue(), f.QueueFamily()};
    VulkanMaterialLayout layout{f.Physical(), f.Logical(), textures.Layout()};
    VulkanMaterialPipelines pipelines{f.Logical(), layout.PipelineLayout()};
    VulkanMaterialSystem materials{pipelines};
};

MaterialParameterBlock Block(std::initializer_list<Math::Vec4> values) {
    MaterialParameterBlock b;
    b.values = values;
    return b;
}

}  // namespace

TEST_CASE("Material system keeps a handle per key", "[vulkan][material]") {
    MaterialRig r;
    REQUIRE(r.layout.IsValid());
    CompiledMaterialGpu m;
    m.shaderKey = 7;
    const MaterialHandle a = r.materials.Update(100, m, Block({{1, 2, 3, 4}}));
    CHECK(r.materials.Update(100, m, Block({{5, 6, 7, 8}})) == a);
    const MaterialHandle b = r.materials.Update(200, m, Block({}));
    CHECK(b != a);
    CHECK(a != kDefaultMaterialHandle);
    CHECK(r.materials.Update(kDefaultMaterialKey, m, Block({})) == kDefaultMaterialHandle);
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Material system reuses released entries", "[vulkan][material]") {
    MaterialRig r;
    const CompiledMaterialGpu m;
    const MaterialHandle a = r.materials.Update(1, m, Block({}));
    r.materials.Release(a);
    CHECK(r.materials.Update(2, m, Block({})) == a);
    // The Default Material can never be released.
    r.materials.Release(kDefaultMaterialHandle);
    CHECK(r.materials.Update(3, m, Block({})) != kDefaultMaterialHandle);
}

TEST_CASE("Flush packs every block at its own offset", "[vulkan][material]") {
    MaterialRig r;
    const CompiledMaterialGpu m;
    const MaterialHandle a = r.materials.Update(1, m, Block({{1, 1, 1, 1}, {2, 2, 2, 2}}));
    const MaterialHandle b = r.materials.Update(2, m, Block({{3, 3, 3, 3}}));
    r.layout.BeginFrame(0);
    r.materials.Flush(r.layout, r.textures);
    const auto params = r.layout.PendingParams();
    const u32 oa = r.materials.ParamOffset(a), ob = r.materials.ParamOffset(b);
    CHECK(oa != ob);
    CHECK(params[oa].x == 1.0f);
    CHECK(params[oa + 1].x == 2.0f);
    CHECK(params[ob].x == 3.0f);
    // Parameter-only edits land on the next flush.
    r.materials.UpdateParameters(b, Block({{9, 9, 9, 9}}));
    r.layout.BeginFrame(1);
    r.materials.Flush(r.layout, r.textures);
    CHECK(r.layout.PendingParams()[r.materials.ParamOffset(b)].x == 9.0f);
    CHECK(r.layout.EndFrame());
    CHECK(r.f.ValidationErrors().empty());
}

TEST_CASE("Texture slots receive image, sampler and bound bits", "[vulkan][material]") {
    MaterialRig r;
    const GpuTextureHandle tex = UploadWhite(r.textures, 5);
    CompiledMaterialGpu m;
    GpuTextureBinding binding;
    binding.slot = 1;
    binding.texture = tex;
    binding.srgb = true;
    binding.sampler = GpuSamplerIndex(GpuSamplerFilter::Nearest, GpuSamplerAddress::Clamp);
    m.textures.push_back(binding);
    const MaterialHandle h = r.materials.Update(9, m, Block({{0.5f, 0, 0, 0}, {0, 0, 0, 0}}));
    r.layout.BeginFrame(0);
    r.materials.Flush(r.layout, r.textures);
    const Math::Vec4 slot = r.layout.PendingParams()[r.materials.ParamOffset(h) + 1];
    CHECK(std::bit_cast<u32>(slot.x) == r.textures.BindlessIndex(tex, GpuTextureView::SRGB));
    CHECK(std::bit_cast<u32>(slot.y) == 4u);
    CHECK(std::bit_cast<u32>(slot.z) == 1u);
    CHECK(r.layout.PendingParams()[r.materials.ParamOffset(h)].x == 0.5f);
}

TEST_CASE("Mesh cache uploads sections", "[vulkan][material]") {
    VulkanTestFixture f;
    VulkanMeshCache meshes(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    MeshData data = BuildPrimitive(MeshPrimitive::Cube);
    const u32 half = static_cast<u32>(data.indices.size() / 2);
    data.sections = {{0, half, "A"}, {half, static_cast<u32>(data.indices.size()) - half, "B"}};

    const MeshHandle h = meshes.Upload(42, data);
    REQUIRE(h != kInvalidHandle);
    const GpuMesh* mesh = meshes.Get(h);
    REQUIRE(mesh != nullptr);
    CHECK(mesh->sections.size() == 2);
    CHECK(mesh->indexCount == data.indices.size());
    CHECK(meshes.Upload(42, data) == h);

    meshes.Release(h);
    meshes.CollectGarbage();
    CHECK(meshes.Get(h) == nullptr);
    CHECK(f.ValidationErrors().empty());
}

TEST_CASE("Mesh cache rejects out-of-range indices", "[vulkan][material]") {
    VulkanTestFixture f;
    VulkanMeshCache meshes(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    MeshData data = BuildPrimitive(MeshPrimitive::Plane);
    data.indices.push_back(static_cast<u32>(data.vertices.size()));
    data.indices.push_back(0);
    data.indices.push_back(1);
    CHECK(meshes.Upload(1, data) == kInvalidHandle);
    CHECK(meshes.Upload(2, MeshData{}) == kInvalidHandle);
}

