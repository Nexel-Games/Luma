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
using Luma::Material::GraphMaterial;
using Luma::Material::GraphMaterialInstance;
using Luma::Material::MaterialLibrary;
using Luma::Material::ParameterOverride;
using Luma::MaterialGraph::NodeId;
using Luma::MaterialGraph::PinRef;
using Luma::MaterialGraph::PropertyValue;

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
    MaterialHandle UpdateMaterial(u64 key, const CompiledMaterialGpu& material,
                                  const MaterialParameterBlock& parameters) override {
        ++materialUpdates;
        compiled[key] = material;
        blocks[key] = parameters;
        if (key == kDefaultMaterialKey) return kDefaultMaterialHandle;
        auto [it, inserted] = handles.try_emplace(key, nextMaterial);
        if (inserted) ++nextMaterial;
        return it->second;
    }
    void UpdateMaterialParameters(MaterialHandle h, const MaterialParameterBlock& b) override {
        ++parameterUpdates;
        parameterBlocks[h] = b;
    }
    void ReleaseMaterial(MaterialHandle) override {}
    MeshHandle PrimitiveMesh(MeshPrimitive p) const override {
        return 1000 + static_cast<u32>(p);
    }

    int meshUploads = 0;
    int materialUpdates = 0;
    int textureReleases = 0;
    std::map<u64, usize> sections;
    int parameterUpdates = 0;
    std::map<MaterialHandle, MaterialParameterBlock> parameterBlocks;
    std::map<u64, CompiledMaterialGpu> compiled;
    std::map<u64, MaterialParameterBlock> blocks;

    // Whether `key`'s block holds `rgb` in some slot (the overridden colour).
    bool BlockHas(u64 key, Math::Vec3 rgb) const {
        for (const Math::Vec4& v : blocks.at(key).values) {
            if (v.x == rgb.x && v.y == rgb.y && v.z == rgb.z) return true;
        }
        return false;
    }
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

// A graph instance of the Default Material with BaseColor overridden.
GraphMaterialInstance ColorInstance(Math::Vec4 color) {
    GraphMaterialInstance mi;
    mi.parent = std::string(Luma::Material::kDefaultMaterialPath);
    ParameterOverride o;
    o.name = "BaseColor";
    o.kind = ParameterOverride::Kind::Vector;
    o.vector = color;
    mi.Set(o);
    return mi;
}

std::string RedInstance(MaterialLibrary& lib, const std::string& path) {
    lib.SetGraphInstance(path, ColorInstance({0.8f, 0.1f, 0.1f, 1.0f}));
    return path;
}

}  // namespace

TEST_CASE("Bridge gives sections without a slot the Default Material", "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    const std::string red = RedInstance(lib, "/Game/Materials/MI_Red");

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
    // The slot's material reached the renderer compiled, with its override in
    // the parameter block.
    const u64 key = f.renderer.handles.begin()->first;
    CHECK(f.renderer.compiled.at(key).HasPass(GpuPass::GBuffer));
    CHECK(f.renderer.BlockHas(key, {0.8f, 0.1f, 0.1f}));
}

TEST_CASE("Bridge keeps extra slots on the component", "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    const std::string red = RedInstance(lib, "/Game/Materials/MI_Red");
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
    const std::string red = RedInstance(lib, "/Game/Materials/MI_Red");
    MeshRendererComponent mr;
    mr.materials = {red};
    f.Spawn(mr);
    f.Spawn(mr);

    f.Build(lib);
    const int afterFirst = f.renderer.materialUpdates;
    CHECK(afterFirst == 2);  // the Default Material + MI_Red
    f.Build(lib);
    CHECK(f.renderer.materialUpdates == afterFirst);

    lib.SetGraphInstance(red, ColorInstance({0, 1, 0, 1}));
    f.Build(lib);
    CHECK(f.renderer.materialUpdates == afterFirst + 1);
    CHECK(f.renderer.BlockHas(f.renderer.handles.begin()->first, {0, 1, 0}));
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
    // BaseColor from a colour texture, Normal from a normal map.
    GraphMaterial m;
    auto sample = [&](const char* texture, i64 samplerType, u32 outputPin) {
        const NodeId n = m.graph.AddNode("TextureSample", {0.0f, 0.0f});
        m.graph.Find(n)->properties["texture"] = PropertyValue(std::string(texture));
        m.graph.Find(n)->properties["sampler"] = PropertyValue(samplerType);
        std::string err;
        REQUIRE(m.graph.Connect(PinRef{n, 0}, PinRef{m.graph.OutputNode(), outputPin}, &err));
    };
    sample("/Game/Textures/T_Wood_diff", 0, 0);  // Color -> BaseColor
    sample("/Game/Textures/T_Wood_nor", 2, 8);   // Normal -> Normal
    lib.SetGraphMaterial("/Game/Materials/M_Wood", m);
    MeshRendererComponent mr;
    mr.materials = {"/Game/Materials/M_Wood"};
    f.Spawn(mr);
    f.Build(lib);

    REQUIRE(f.renderer.textures.size() == 2);
    const CompiledMaterialGpu* gpu = nullptr;
    for (const auto& [key, c] : f.renderer.compiled) {
        if (key != kDefaultMaterialKey) gpu = &c;
    }
    REQUIRE(gpu != nullptr);
    REQUIRE(gpu->textures.size() == 2);
    u32 srgb = 0, linear = 0;
    for (const GpuTextureBinding& t : gpu->textures) {
        CHECK(t.texture != kInvalidHandle);
        (t.srgb ? srgb : linear) += 1;
    }
    CHECK(srgb == 1);
    CHECK(linear == 1);

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
    const std::string a = RedInstance(lib, "/Game/Materials/MI_A");
    const std::string b = RedInstance(lib, "/Game/Materials/MI_B");
    const std::string c = RedInstance(lib, "/Game/Materials/MI_C");
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

TEST_CASE("A placement preview is drawn but is not part of the scene", "[bridge]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    const std::string a = RedInstance(lib, "/Game/Materials/MI_A");
    const std::string rock = f.WriteMesh("Rock", 1, {a});
    MeshRendererComponent cube;
    f.Spawn(cube);

    f.bridge.SetPlacementPreview(rock, Math::Translate(Math::Vec3(3.0f, 0.0f, 0.0f)));
    auto instances = f.Build(lib);
    REQUIRE(instances.size() == 2);
    const SceneInstance& preview = instances.back();
    CHECK(preview.mesh != kInvalidHandle);
    CHECK(preview.selected);
    CHECK(preview.model.m[12] == 3.0f);
    CHECK(preview.sectionMaterials[0] == f.bridge.HandleFor(a));  // its own material, uploaded
    CHECK(f.scene.Registry().view<const MeshRendererComponent>().size() == 1);

    f.bridge.ClearPlacementPreview();
    instances = f.Build(lib);
    CHECK(instances.size() == 1);
}

namespace {

// BaseColor = /Game/MPC_Wind.Strength.
GraphMaterial WindMaterial() {
    GraphMaterial m;
    const NodeId n = m.graph.AddNode("CollectionParameter", {0.0f, 0.0f});
    m.graph.Find(n)->properties["collection"] = PropertyValue(std::string("/Game/MPC_Wind"));
    m.graph.Find(n)->properties["parameter"] = PropertyValue(std::string("Strength"));
    std::string err;
    REQUIRE(m.graph.Connect(PinRef{n, 0}, PinRef{m.graph.OutputNode(), 0}, &err));
    return m;
}

Luma::Material::ParameterCollection Wind(f32 strength) {
    Luma::Material::ParameterCollection pc;
    pc.name = "MPC_Wind";
    pc.scalars.push_back({"Strength", strength});
    return pc;
}

bool BlockHasX(const MaterialParameterBlock& b, f32 x) {
    for (const Math::Vec4& v : b.values) {
        if (v.x == x) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("Materials reading a collection share its value", "[bridge][collection]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    lib.SetCollection("/Game/MPC_Wind", Wind(0.5f));
    lib.SetGraphMaterial("/Game/Materials/M_A", WindMaterial());
    lib.SetGraphMaterial("/Game/Materials/M_B", WindMaterial());
    MeshRendererComponent mr;
    mr.materials = {"/Game/Materials/M_A", "/Game/Materials/M_B"};
    f.Spawn(mr);
    f.Build(lib);
    int found = 0;
    for (const auto& [key, block] : f.renderer.blocks) {
        if (key != kDefaultMaterialKey && BlockHasX(block, 0.5f)) ++found;
    }
    CHECK(found == 2);
}

TEST_CASE("A collection value change updates parameters without recompiling", "[bridge][collection]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    lib.SetCollection("/Game/MPC_Wind", Wind(0.5f));
    lib.SetGraphMaterial("/Game/Materials/M_A", WindMaterial());
    MeshRendererComponent mr;
    mr.materials = {"/Game/Materials/M_A"};
    f.Spawn(mr);
    f.Build(lib);
    const int compiles = f.renderer.materialUpdates;
    REQUIRE(lib.SetCollectionValue("/Game/MPC_Wind", "Strength", {0.9f, 0, 0, 0}));
    f.Build(lib);
    CHECK(f.renderer.materialUpdates == compiles);
    REQUIRE(f.renderer.parameterUpdates == 1);
    CHECK(BlockHasX(f.renderer.parameterBlocks.begin()->second, 0.9f));
    f.Build(lib);  // unchanged revision: nothing to do
    CHECK(f.renderer.parameterUpdates == 1);
}

TEST_CASE("Deleting a collection keeps the last good material", "[bridge][collection]") {
    BridgeFixture f;
    MaterialLibrary lib(f.paths);
    lib.SetCollection("/Game/MPC_Wind", Wind(0.5f));
    REQUIRE(lib.SaveGraphAsset("/Game/MPC_Wind"));
    lib.SetGraphMaterial("/Game/Materials/M_A", WindMaterial());
    MeshRendererComponent mr;
    mr.materials = {"/Game/Materials/M_A"};
    const Entity e = f.Spawn(mr);
    f.Build(lib, e);
    const MaterialHandle good = f.bridge.HandleFor("/Game/Materials/M_A");
    const int uploads = f.renderer.materialUpdates;

    std::filesystem::remove(f.root / "Assets" / "MPC_Wind.lmpc");
    lib.Reload("/Game/MPC_Wind");
    REQUIRE(lib.GetCollection("/Game/MPC_Wind") == nullptr);
    f.Build(lib, e);
    CHECK(f.renderer.materialUpdates == uploads);  // the compile failed: nothing new uploaded
    CHECK(f.bridge.HandleFor("/Game/Materials/M_A") == good);
}
