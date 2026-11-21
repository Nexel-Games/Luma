#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Asset/LumaMesh.h"
#include "Luma/Editor/MaterialSceneBridge.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Scene/Scene.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#include "stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace fs = std::filesystem;
using namespace Luma;
using Luma::Editor::MaterialSceneBridge;
using Luma::Material::MaterialInput;
using Luma::Material::MaterialInstance;
using Luma::Material::MaterialLibrary;
using Luma::Material::MaterialParameter;

namespace {

// Records what the bridge uploads; hands out sequential handles.
class RecordingRenderer final : public SceneRenderer {
public:
    MeshHandle UploadMesh(u64 key, const MeshData& mesh) override {
        ++meshUploads;
        sections[key] = mesh.sections.size();
        return nextMesh++;
    }
    void ReleaseMesh(MeshHandle) override {}
    GpuTextureHandle UploadTexture(u64, const TextureUploadDesc& texture) override {
        textures.push_back(texture.srgb);
        return nextTexture++;
    }
    void ReleaseTexture(GpuTextureHandle) override { ++textureReleases; }
    MaterialHandle UpdateMaterial(u64 key, const MaterialGpuDesc& material) override {
        ++materialUpdates;
        descs[key] = material;
        if (key == kDefaultMaterialKey) return kDefaultMaterialHandle;
        auto [it, inserted] = handles.try_emplace(key, nextMaterial);
        if (inserted) ++nextMaterial;
        return it->second;
    }
    void ReleaseMaterial(MaterialHandle) override {}
    MeshHandle PrimitiveMesh(MeshPrimitive p) const override {
        return 1000 + static_cast<u32>(p);
    }

    int meshUploads = 0;
    int materialUpdates = 0;
    int textureReleases = 0;
    std::map<u64, usize> sections;
    std::map<u64, MaterialGpuDesc> descs;
    std::map<u64, MaterialHandle> handles;
    std::vector<bool> textures;  // srgb flag per upload
    MeshHandle nextMesh = 1;
    GpuTextureHandle nextTexture = 1;
    MaterialHandle nextMaterial = 1;
};

struct BridgeFixture {
    fs::path root = fs::temp_directory_path() / "luma_bridge_test";
    AssetPathResolver paths;
    AssetRegistry registry;
    RecordingRenderer renderer;
    MaterialSceneBridge bridge;
    Scene scene;

    BridgeFixture() {
        fs::remove_all(root);
        fs::create_directories(root / "Assets" / "Meshes");
        fs::create_directories(root / "Assets" / "Textures");
        paths.SetGameRoot(root / "Assets");
        paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
    }
    ~BridgeFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // A quad split into `sectionCount` submeshes, written as an .lmesh.
    // Returns the mesh's virtual path. `sectionMaterials[i]` (if given) is what
    // the importer writes as section i's material.
    std::string WriteMesh(const std::string& name, u32 sectionCount,
                          std::vector<std::string> sectionMaterials = {}) {
        LumaMeshData mesh;
        mesh.name = name;
        for (u32 s = 0; s < sectionCount; ++s) {
            const f32 x = static_cast<f32>(s);
            const u32 base = static_cast<u32>(mesh.vertices.size());
            for (int v = 0; v < 3; ++v) {
                LumaVertex lv{};
                lv.position = {x + (v == 1 ? 1.0f : 0.0f), v == 2 ? 1.0f : 0.0f, 0.0f};
                lv.normal = {0.0f, 0.0f, 1.0f};
                lv.texCoord = {lv.position.x, lv.position.y};
                mesh.vertices.push_back(lv);
            }
            LumaSubmesh sm{};
            sm.indexOffset = static_cast<u32>(mesh.indices.size());
            sm.indexCount = 3;
            sm.vertexOffset = base;
            sm.vertexCount = 3;
            sm.materialName = s < sectionMaterials.size() ? sectionMaterials[s]
                                                          : "Section" + std::to_string(s);
            mesh.submeshes.push_back(sm);
            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
        }
        mesh.ComputeBounds();
        const fs::path file = root / "Assets" / "Meshes" / (name + ".lmesh");
        REQUIRE(LumaMeshIO::WriteMesh(file, mesh));
        registry.AddRoot(root / "Assets");
        registry.Scan();
        return "/Game/Meshes/" + name;
    }

    void WritePng(const std::string& name) {
        const unsigned char px[4 * 4 * 4] = {};
        const fs::path file = root / "Assets" / "Textures" / (name + ".png");
        REQUIRE(stbi_write_png(file.string().c_str(), 4, 4, 4, px, 16) != 0);
    }

    Entity Spawn(MeshRendererComponent mr) {
        const Entity e = scene.CreateEntity("Actor");
        scene.Registry().emplace<MeshRendererComponent>(e, std::move(mr));
        return e;
    }

    std::vector<SceneInstance> Build(MaterialLibrary& lib, Entity selected = kNullEntity) {
        bridge.Sync(scene, renderer, lib);
        std::vector<SceneInstance> out;
        bridge.BuildInstances(scene, selected, out);
        return out;
    }
};

MaterialInstance RedInstance() {
    MaterialInstance mi;
    mi.parent = std::string(Luma::Material::kDefaultMaterialPath);
    MaterialInput base;
    base.constant = {0.8f, 0.1f, 0.1f, 1.0f};
    mi.Override(MaterialParameter::BaseColor, base);
    return mi;
}

}  // namespace

TEST_CASE("Bridge gives sections without a slot the Default Material", "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    const std::string red = lib.CreateInMemoryInstance("/Game/Materials/MI_Red", RedInstance());

    MeshRendererComponent mr;
    mr.mesh = f.WriteMesh("ThreeParts", 3);
    mr.materials = {red};
    const Entity e = f.Spawn(mr);

    const auto instances = f.Build(lib, e);
    REQUIRE(instances.size() == 1);
    const SceneInstance& inst = instances[0];
    CHECK(inst.mesh != kInvalidHandle);
    CHECK(inst.sectionCount == 3);
    CHECK(inst.sectionMaterials[0] != kDefaultMaterialHandle);
    CHECK(inst.sectionMaterials[1] == kDefaultMaterialHandle);
    CHECK(inst.sectionMaterials[2] == kDefaultMaterialHandle);
    CHECK(inst.selected);
    // The slot's material reached the renderer with its resolved values.
    const auto& base = f.renderer.descs.at(f.renderer.handles.begin()->first)
                           .inputs[static_cast<usize>(GpuMaterialInput::BaseColor)].constant;
    CHECK((base.x == 0.8f && base.y == 0.1f && base.z == 0.1f));
}

TEST_CASE("Bridge keeps extra slots on the component", "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    const std::string red = lib.CreateInMemoryInstance("/Game/Materials/MI_Red", RedInstance());
    MeshRendererComponent mr;
    mr.primitive = MeshPrimitive::Sphere;
    mr.materials = {red, red, "", red, red};
    const Entity e = f.Spawn(mr);

    const auto instances = f.Build(lib);
    REQUIRE(instances.size() == 1);
    CHECK(instances[0].mesh == kInvalidHandle);
    CHECK(instances[0].primitive == MeshPrimitive::Sphere);
    CHECK(instances[0].sectionCount == 1);
    CHECK(instances[0].sectionMaterials[0] != kDefaultMaterialHandle);
    CHECK_FALSE(instances[0].selected);
    CHECK(f.scene.Registry().get<MeshRendererComponent>(e).materials.size() == 5);
}

TEST_CASE("Bridge uploads a material once per version", "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    const std::string red = lib.CreateInMemoryInstance("/Game/Materials/MI_Red", RedInstance());
    MeshRendererComponent mr;
    mr.materials = {red};
    f.Spawn(mr);
    f.Spawn(mr);

    f.Build(lib);
    const int afterFirst = f.renderer.materialUpdates;
    CHECK(afterFirst == 2);  // the Default Material + MI_Red
    f.Build(lib);
    CHECK(f.renderer.materialUpdates == afterFirst);

    MaterialInstance edited = RedInstance();
    edited.inputs[static_cast<usize>(MaterialParameter::BaseColor)].constant = {0, 1, 0, 1};
    lib.SetInstance(red, edited);
    f.Build(lib);
    CHECK(f.renderer.materialUpdates == afterFirst + 1);
    const auto& base = f.renderer.descs.at(f.renderer.handles.begin()->first)
                           .inputs[static_cast<usize>(GpuMaterialInput::BaseColor)].constant;
    CHECK(base.y == 1.0f);
}

TEST_CASE("Bridge uploads each mesh asset once", "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    MeshRendererComponent mr;
    mr.mesh = f.WriteMesh("Rock", 1);
    f.Spawn(mr);
    f.Spawn(mr);
    f.Build(lib);
    f.Build(lib);
    CHECK(f.renderer.meshUploads == 1);
    const auto* bounds = f.bridge.Bounds(mr.mesh);
    REQUIRE(bounds != nullptr);
    CHECK(bounds->max.x == 1.0f);
}

TEST_CASE("Bridge samples color textures as sRGB and data textures as linear", "[bridge]") {
    BridgeFixture f;
    f.WritePng("T_Wood_diff");
    f.WritePng("T_Wood_nor");
    MaterialLibrary lib(f.paths);
    MaterialInstance mi = RedInstance();
    MaterialInput base;
    base.texture = "/Game/Textures/T_Wood_diff";
    mi.Override(MaterialParameter::BaseColor, base);
    MaterialInput normal;
    normal.texture = "/Game/Textures/T_Wood_nor";
    mi.Override(MaterialParameter::Normal, normal);
    const std::string wood = lib.CreateInMemoryInstance("/Game/Materials/MI_Wood", mi);
    MeshRendererComponent mr;
    mr.materials = {wood};
    f.Spawn(mr);
    f.Build(lib);

    REQUIRE(f.renderer.textures.size() == 2);
    const MaterialGpuDesc* desc = nullptr;
    for (const auto& [key, d] : f.renderer.descs) {
        if (key != kDefaultMaterialKey) desc = &d;
    }
    REQUIRE(desc != nullptr);
    const auto& gb = desc->inputs[static_cast<usize>(GpuMaterialInput::BaseColor)];
    const auto& gn = desc->inputs[static_cast<usize>(GpuMaterialInput::Normal)];
    CHECK(gb.texture != kInvalidHandle);
    CHECK(gb.view == GpuTextureView::SRGB);
    CHECK(gn.texture != kInvalidHandle);
    CHECK(gn.view == GpuTextureView::Linear);

    // A changed texture file is re-uploaded and the material refreshed.
    const int updates = f.renderer.materialUpdates;
    f.bridge.OnTextureFileChanged("/Game/Textures/T_Wood_diff");
    f.Build(lib);
    CHECK(f.renderer.textureReleases == 1);
    CHECK(f.renderer.textures.size() == 3);
    CHECK(f.renderer.materialUpdates == updates + 1);
}

TEST_CASE("Bridge reports a mesh's section count", "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    MeshRendererComponent mesh;
    mesh.mesh = f.WriteMesh("FourParts", 4);
    MeshRendererComponent primitive;
    f.Spawn(mesh);
    f.Build(lib);
    CHECK(f.bridge.SectionCount(mesh) == 4);
    CHECK(f.bridge.SectionCount(primitive) == 1);
}

TEST_CASE("Empty slots draw the mesh's section materials", "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    const std::string a = lib.CreateInMemoryInstance("/Game/Materials/MI_A", RedInstance());
    const std::string b = lib.CreateInMemoryInstance("/Game/Materials/MI_B", RedInstance());
    const std::string c = lib.CreateInMemoryInstance("/Game/Materials/MI_C", RedInstance());
    MeshRendererComponent mr;
    mr.mesh = f.WriteMesh("TwoParts", 2, {a, b});
    const Entity e = f.Spawn(mr);

    auto instances = f.Build(lib);
    REQUIRE(instances.size() == 1);
    CHECK(instances[0].sectionMaterials[0] == f.bridge.HandleFor(a));
    CHECK(instances[0].sectionMaterials[1] == f.bridge.HandleFor(b));
    CHECK(instances[0].sectionMaterials[0] != kDefaultMaterialHandle);
    CHECK(instances[0].sectionMaterials[0] != instances[0].sectionMaterials[1]);

    // An override wins over the mesh's material.
    f.scene.Registry().get<MeshRendererComponent>(e).materials = {"", c};
    instances = f.Build(lib);
    CHECK(instances[0].sectionMaterials[0] == f.bridge.HandleFor(a));
    CHECK(instances[0].sectionMaterials[1] == f.bridge.HandleFor(c));

    const auto* sections = f.bridge.Sections(mr.mesh);
    REQUIRE(sections != nullptr);
    REQUIRE(sections->size() == 2);
    CHECK((*sections)[0].defaultMaterial == a);
    CHECK((*sections)[0].slotName == "A");
    CHECK((*sections)[1].slotName == "B");
}

TEST_CASE("A section material that is not an asset path uses the Default Material",
          "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    MeshRendererComponent mr;
    mr.mesh = f.WriteMesh("Lambert", 1, {"lambert1"});
    f.Spawn(mr);
    const auto instances = f.Build(lib);
    CHECK(instances[0].sectionMaterials[0] == kDefaultMaterialHandle);
    const auto* sections = f.bridge.Sections(mr.mesh);
    REQUIRE(sections != nullptr);
    CHECK((*sections)[0].defaultMaterial.empty());
    CHECK((*sections)[0].slotName == "lambert1");
}

TEST_CASE("A missing mesh draws its primitive and has no sections", "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    MeshRendererComponent mr;
    mr.mesh = "/Game/Meshes/DoesNotExist";
    mr.primitive = MeshPrimitive::Sphere;
    f.Spawn(mr);
    const auto instances = f.Build(lib);
    REQUIRE(instances.size() == 1);
    CHECK(instances[0].mesh == kInvalidHandle);
    CHECK(instances[0].primitive == MeshPrimitive::Sphere);
    CHECK(f.bridge.Sections(mr.mesh) == nullptr);
    CHECK(f.bridge.Bounds(mr.mesh) == nullptr);
}
