#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Luma/AssetImport/ImportMetadata.h"
#include "Luma/AssetImport/ImportScene.h"
#include "Luma/AssetImport/ImportSettings.h"

using namespace Luma;
using namespace Luma::Import;

namespace {

ImportNode MakeNode(NodeKind kind, std::string name, NodeId parent = kNone) {
    ImportNode n;
    n.kind = kind;
    n.name = std::move(name);
    n.parent = parent;
    return n;
}

}  // namespace

TEST_CASE("ImportScene indexes children and kinds", "[import][scene]") {
    ImportScene scene;
    const NodeId file = scene.AddNode(MakeNode(NodeKind::File, "robot.gltf"));
    const NodeId tex = scene.AddNode(MakeNode(NodeKind::Texture, "T_Paint_D", file));
    const NodeId mat = scene.AddNode(MakeNode(NodeKind::Material, "Paint", file));
    ImportNode mesh = MakeNode(NodeKind::Mesh, "Body", file);
    mesh.meshMaterials = {mat};
    const NodeId meshId = scene.AddNode(std::move(mesh));

    REQUIRE(file == 0);
    REQUIRE(scene.nodes.size() == 4);
    const auto children = scene.Children(file);
    REQUIRE(children == std::vector<NodeId>{tex, mat, meshId});
    REQUIRE(scene.OfKind(NodeKind::Mesh) == std::vector<NodeId>{meshId});
    REQUIRE(scene.OfKind(NodeKind::File) == std::vector<NodeId>{file});
}

TEST_CASE("ImportScene Append remaps mesh and texture ids", "[import][scene]") {
    ImportScene batch;
    const NodeId file = batch.AddNode(MakeNode(NodeKind::File, "a.gltf"));
    const NodeId tex = batch.AddNode(MakeNode(NodeKind::Texture, "T", file));
    ImportNode mat = MakeNode(NodeKind::Material, "Paint", file);
    mat.material.baseColorTex.texture = tex;
    mat.material.normalTex.texture = kNone;
    const NodeId matId = batch.AddNode(std::move(mat));
    ImportNode mesh = MakeNode(NodeKind::Mesh, "Body", file);
    mesh.meshMaterials = {matId};
    batch.AddNode(std::move(mesh));

    ImportScene extra;
    const NodeId file2 = extra.AddNode(MakeNode(NodeKind::File, "b.gltf"));
    const NodeId tex2 = extra.AddNode(MakeNode(NodeKind::Texture, "T2", file2));
    ImportNode mat2 = MakeNode(NodeKind::Material, "Metal", file2);
    mat2.material.baseColorTex.texture = tex2;
    const NodeId mat2Id = extra.AddNode(std::move(mat2));
    ImportNode mesh2 = MakeNode(NodeKind::Mesh, "Arm", file2);
    mesh2.meshMaterials = {mat2Id};
    extra.AddNode(std::move(mesh2));

    const NodeId base = static_cast<NodeId>(batch.nodes.size());
    batch.Append(extra);

    REQUIRE(batch.nodes.size() == base + extra.nodes.size());
    const NodeId newMat = base + mat2Id;
    const NodeId newTex = base + tex2;
    const NodeId newMesh = base + mat2Id + 1;
    REQUIRE(batch.nodes[newMat].parent == base + file2);
    REQUIRE(batch.nodes[newMat].material.baseColorTex.texture == newTex);
    REQUIRE(batch.nodes[newMesh].meshMaterials.size() == 1);
    REQUIRE(batch.nodes[newMesh].meshMaterials[0] == newMat);
    REQUIRE(batch.nodes[matId].material.baseColorTex.texture == tex);
    REQUIRE(batch.nodes[matId].material.normalTex.texture == kNone);
}

TEST_CASE("SettingsBlock defaults, clamp, type check, and JSON", "[import][scene]") {
    SettingsBlock block;
    SettingField scale;
    scale.key = "scale";
    scale.label = "Scale";
    scale.category = "Meshes";
    scale.type = FieldType::Float;
    scale.min = 0.0;
    scale.max = 2.0;
    block.Define(scale, 1.0);

    SettingField name;
    name.key = "name";
    name.type = FieldType::String;
    block.Define(name, "Body");

    SettingField offset;
    offset.key = "offset";
    offset.type = FieldType::Vec3;
    SerialValue offsetDefault = SerialValue::MakeArray();
    offsetDefault.PushBack(0.0);
    offsetDefault.PushBack(1.0);
    offsetDefault.PushBack(0.0);
    block.Define(offset, offsetDefault);

    REQUIRE(block.GetFloat("scale") == 1.0);
    REQUIRE(block.GetString("name") == "Body");
    REQUIRE(block.GetVec3("offset").y == 1.0f);

    block.Set("scale", 5.0);
    REQUIRE(block.GetFloat("scale") == 2.0);
    block.Set("scale", -1.0);
    REQUIRE(block.GetFloat("scale") == 0.0);

    block.Set("scale", "nope");
    REQUIRE(block.GetFloat("scale") == 0.0);

    block.Set("name", "Arm");
    block.Set("offset", offsetDefault);
    const SerialValue saved = block.ToJson();

    block.ResetToDefaults();
    REQUIRE(block.GetFloat("scale") == 1.0);
    REQUIRE(block.GetString("name") == "Body");

    SerialValue withUnknown = saved;
    withUnknown["notAField"] = 42;
    block.FromJson(withUnknown);
    REQUIRE(block.Fields().size() == 3);
    REQUIRE(block.GetFloat("scale") == 0.0);
    REQUIRE(block.GetString("name") == "Arm");
    REQUIRE(block.GetVec3("offset").y == 1.0f);
}

TEST_CASE("Import metadata round-trips and hashes file bytes", "[import][scene]") {
    const auto dir = std::filesystem::temp_directory_path() / "luma-import-meta";
    std::filesystem::create_directories(dir);
    const auto asset = dir / "T_X.png";
    const auto bytesPath = dir / "bytes.bin";
    {
        std::ofstream out(asset, std::ios::binary);
        out << "png";
        std::ofstream bytes(bytesPath, std::ios::binary);
        bytes << "abc";
    }

    REQUIRE(MetadataPathFor(asset) == asset.string() + ".meta");
    REQUIRE(MetadataPathFor("a/T_X.png") == std::filesystem::path("a/T_X.png.meta"));

    const std::string hash = HashFile(bytesPath);
    REQUIRE(hash.size() == 16);
    REQUIRE(HashFile(bytesPath) == hash);
    {
        std::ofstream bytes(bytesPath, std::ios::binary | std::ios::trunc);
        bytes << "abd";
    }
    REQUIRE(HashFile(bytesPath) != hash);

    ImportMetadata meta;
    meta.guid = MakeAssetIdFromKey("T_X.png");
    meta.type = AssetType::Texture;
    meta.sourceFile = "Assets/T_X.png";
    meta.sourceHash = hash;
    meta.sourceMtime = 42;
    meta.importer = "Luma.Import.Texture";
    meta.importerVersion = 1;
    meta.sourceNodeName = "T_X";
    meta.settings = SerialValue::MakeObject();
    meta.settings["srgb"] = true;

    std::string err;
    REQUIRE(WriteMetadata(asset, meta, &err));
    REQUIRE(err.empty());
    const auto loaded = ReadMetadata(asset);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->guid == meta.guid);
    REQUIRE(loaded->type == AssetType::Texture);
    REQUIRE(loaded->sourceFile == meta.sourceFile);
    REQUIRE(loaded->sourceHash == hash);
    REQUIRE(loaded->sourceMtime == 42);
    REQUIRE(loaded->importer == meta.importer);
    REQUIRE(loaded->importerVersion == 1);
    REQUIRE(loaded->sourceNodeName == "T_X");
    REQUIRE(loaded->settings.Find("srgb")->AsBool());

    std::filesystem::remove_all(dir);
}
