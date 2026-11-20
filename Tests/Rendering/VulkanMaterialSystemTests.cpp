#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "VulkanTestFixture.h"
#include "Luma/Mesh/Mesh.h"
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

usize Slot(GpuMaterialInput p) { return static_cast<usize>(p); }

}  // namespace

TEST_CASE("Material system keeps a handle per key", "[vulkan][material]") {
    VulkanTestFixture f;
    VulkanTextureCache textures(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    VulkanMaterialSystem materials(f.Physical(), f.Logical());
    REQUIRE(materials.IsValid());

    MaterialGpuDesc desc = MakeDefaultMaterialGpuDesc();
    const MaterialHandle a = materials.Update(100, desc, textures);
    desc.inputs[Slot(GpuMaterialInput::Roughness)].constant = {0.9f, 0.9f, 0.9f, 0.9f};
    CHECK(materials.Update(100, desc, textures) == a);
    CHECK(materials.Entry(a).inputs[Slot(GpuMaterialInput::Roughness)].constant[0] == 0.9f);

    const MaterialHandle b = materials.Update(200, desc, textures);
    CHECK(b != a);
    CHECK(materials.Index(a) != materials.Index(b));
    CHECK(a != kDefaultMaterialHandle);
    CHECK(materials.Update(kDefaultMaterialKey, desc, textures) == kDefaultMaterialHandle);
}

TEST_CASE("Material system reuses released entries", "[vulkan][material]") {
    VulkanTestFixture f;
    VulkanTextureCache textures(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    VulkanMaterialSystem materials(f.Physical(), f.Logical());
    const MaterialGpuDesc desc = MakeDefaultMaterialGpuDesc();
    const MaterialHandle a = materials.Update(1, desc, textures);
    const u32 index = materials.Index(a);
    materials.Release(a);
    const MaterialHandle b = materials.Update(2, desc, textures);
    CHECK(materials.Index(b) == index);
    // The Default Material can never be released.
    materials.Release(kDefaultMaterialHandle);
    CHECK(materials.Update(3, desc, textures) != kDefaultMaterialHandle);
}

TEST_CASE("Material texture indices follow the selected view", "[vulkan][material]") {
    VulkanTestFixture f;
    VulkanTextureCache textures(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    VulkanMaterialSystem materials(f.Physical(), f.Logical());
    const GpuTextureHandle tex = UploadWhite(textures, 9);
    REQUIRE(tex != kInvalidHandle);

    MaterialGpuDesc desc = MakeDefaultMaterialGpuDesc();
    auto& base = desc.inputs[Slot(GpuMaterialInput::BaseColor)];
    base.texture = tex;
    base.view = GpuTextureView::SRGB;
    auto& normal = desc.inputs[Slot(GpuMaterialInput::Normal)];
    normal.texture = tex;
    normal.view = GpuTextureView::Linear;
    normal.flipGreen = true;
    const MaterialHandle h = materials.Update(5, desc, textures);

    const GpuMaterial& gpu = materials.Entry(h);
    const auto& gb = gpu.inputs[Slot(GpuMaterialInput::BaseColor)];
    const auto& gn = gpu.inputs[Slot(GpuMaterialInput::Normal)];
    CHECK(gb.texIndex == textures.BindlessIndex(tex, GpuTextureView::SRGB));
    CHECK(gn.texIndex == textures.BindlessIndex(tex, GpuTextureView::Linear));
    CHECK((gb.flags & GpuMaterial::kHasTexture) != 0);
    CHECK((gn.flags & GpuMaterial::kFlipGreen) != 0);
    CHECK((gpu.inputs[Slot(GpuMaterialInput::Roughness)].flags & GpuMaterial::kHasTexture) == 0);

    // A released texture's slot is reused, but the stale handle no longer
    // resolves to it: the material falls back to white on the next flush.
    textures.Release(tex);
    textures.CollectGarbage();
    const GpuTextureHandle other = UploadWhite(textures, 10);
    CHECK(other != tex);
    materials.Flush(0, textures);
    CHECK(materials.Entry(h).inputs[Slot(GpuMaterialInput::BaseColor)].texIndex ==
          textures.BindlessIndex(kInvalidHandle, GpuTextureView::SRGB));
    CHECK(f.ValidationErrors().empty());
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

TEST_CASE("Material table carries invert and the detail-map strengths", "[vulkan][material]") {
    VulkanTestFixture f;
    VulkanTextureCache textures(f.Physical(), f.Logical(), f.Queue(), f.QueueFamily());
    VulkanMaterialSystem materials(f.Physical(), f.Logical());
    const GpuTextureHandle tex = UploadWhite(textures, 3);
    MaterialGpuDesc desc = MakeDefaultMaterialGpuDesc();
    auto& rough = desc.inputs[Slot(GpuMaterialInput::Roughness)];
    rough.texture = tex;
    rough.invert = true;
    auto& height = desc.inputs[Slot(GpuMaterialInput::Height)];
    height.texture = tex;
    height.normalStrength = 0.07f;
    const MaterialHandle h = materials.Update(11, desc, textures);
    const GpuMaterial& gpu = materials.Entry(h);
    CHECK((gpu.inputs[Slot(GpuMaterialInput::Roughness)].flags & GpuMaterial::kInvert) != 0);
    CHECK(gpu.inputs[Slot(GpuMaterialInput::Height)].scalar == 0.07f);
    CHECK(gpu.inputs[Slot(GpuMaterialInput::Cavity)].constant[0] == 1.0f);
    CHECK(kMaterialGpuInputCount == 11);
}
