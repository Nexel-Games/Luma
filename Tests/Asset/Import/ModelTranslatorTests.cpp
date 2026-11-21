#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "Luma/Asset/Import/Translators/AxisConversion.h"
#include "Luma/Asset/Import/Translators/ModelTranslator.h"
#include "Luma/Asset/LumaMesh.h"
#include "Luma/Math/Math.h"

using Catch::Approx;
using namespace Luma;
using namespace Luma::Import;

namespace {

Math::Vec4 Mul(const Math::Mat4& m, const Math::Vec4& v) {
    Math::Vec4 r;
    r.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w;
    r.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w;
    r.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w;
    r.w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w;
    return r;
}

}  // namespace

// Luma is right-handed, +Y up, camera looking down -Z (viewer sits on +Z).
// Perspective flips clip Y because Vulkan NDC +Y is down, so a world point
// above the camera projects to NDC y < 0 (the top of the screen).
TEST_CASE("Luma is Y-up right-handed", "[import][model]") {
    const Math::Mat4 view = Math::LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Math::Mat4 proj = Math::Perspective(Math::Radians(90.0f), 1.0f, 0.1f, 100.0f);
    const Math::Mat4 clip = proj * view;

    const auto project = [&](Math::Vec3 p) {
        const Math::Vec4 c = Mul(clip, {p.x, p.y, p.z, 1.0f});
        REQUIRE(c.w > 0.0f);
        return Math::Vec3{c.x / c.w, c.y / c.w, c.z / c.w};
    };

    const Math::Vec3 centre = project({0, 0, 0});
    const Math::Vec3 above = project({0, 1, 0});
    const Math::Vec3 right = project({1, 0, 0});

    REQUIRE(centre.x == Approx(0.0f).margin(1e-4));
    REQUIRE(centre.y == Approx(0.0f).margin(1e-4));
    REQUIRE(above.y < centre.y);
    REQUIRE(right.x > centre.x);
}

namespace {

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(LUMA_SOURCE_DIR) / "Tests" / "Fixtures" / "Import" / name;
}

const ImportNode* FindNamed(const ImportScene& scene, NodeKind kind, const std::string& name) {
    for (const ImportNode& node : scene.nodes) {
        if (node.kind == kind && node.name == name) {
            return &node;
        }
    }
    return nullptr;
}

NodeId IdOf(const ImportScene& scene, const ImportNode* node) {
    return static_cast<NodeId>(node - scene.nodes.data());
}

void RequireRobotNodes(const ImportScene& scene, bool embeddedPng) {
    REQUIRE(scene.OfKind(NodeKind::File).size() == 1);
    REQUIRE(scene.OfKind(NodeKind::Mesh).size() == 2);
    REQUIRE(scene.OfKind(NodeKind::Material).size() == 2);
    REQUIRE(scene.OfKind(NodeKind::Texture).size() == 2);

    const ImportNode* body = FindNamed(scene, NodeKind::Mesh, "Body");
    const ImportNode* arm = FindNamed(scene, NodeKind::Mesh, "Arm");
    const ImportNode* paint = FindNamed(scene, NodeKind::Material, "Paint");
    const ImportNode* metal = FindNamed(scene, NodeKind::Material, "Metal");
    const ImportNode* diffuse = FindNamed(scene, NodeKind::Texture, "T_Paint_D");
    const ImportNode* normal = FindNamed(scene, NodeKind::Texture, "T_Paint_N");
    REQUIRE(body);
    REQUIRE(arm);
    REQUIRE(paint);
    REQUIRE(metal);
    REQUIRE(diffuse);
    REQUIRE(normal);

    REQUIRE(body->meshVertexCount == 24);
    REQUIRE(body->meshTriangleCount == 12);
    REQUIRE(arm->meshVertexCount == 24);
    REQUIRE(arm->meshTriangleCount == 12);
    REQUIRE(body->meshMaterials.size() == 1);
    REQUIRE(body->meshMaterials[0] == IdOf(scene, paint));

    REQUIRE(paint->material.metallic == Approx(0.0f));
    REQUIRE(paint->material.roughness == Approx(0.6f));
    REQUIRE(paint->material.baseColorTex.texture == IdOf(scene, diffuse));
    REQUIRE(paint->material.normalTex.texture == IdOf(scene, normal));

    REQUIRE(metal->material.baseColor.x == Approx(0.8f));
    REQUIRE(metal->material.baseColor.y == Approx(0.8f));
    REQUIRE(metal->material.baseColor.z == Approx(0.8f));
    REQUIRE(metal->material.metallic == Approx(1.0f));
    REQUIRE(metal->material.roughness == Approx(0.3f));
    REQUIRE(metal->material.baseColorTex.texture == kNone);
    REQUIRE(metal->material.normalTex.texture == kNone);

    REQUIRE(normal->textureIsNormalMap);
    REQUIRE(normal->textureIsLinear);
    REQUIRE_FALSE(diffuse->textureIsNormalMap);
    if (embeddedPng) {
        REQUIRE(diffuse->textureEmbeddedExt == "png");
        REQUIRE(normal->textureEmbeddedExt == "png");
        REQUIRE_FALSE(diffuse->textureEmbedded.empty());
        REQUIRE_FALSE(normal->textureEmbedded.empty());
    }
}

Math::Vec3 TransformPoint(const Math::Mat4& m, Math::Vec3 p) {
    const Math::Vec4 r = Mul(m, {p.x, p.y, p.z, 1.0f});
    return {r.x, r.y, r.z};
}

const LumaMeshData* MeshNamed(const std::vector<LumaMeshData>& meshes, const std::string& name) {
    for (const LumaMeshData& mesh : meshes) {
        if (mesh.name == name) {
            return &mesh;
        }
    }
    return nullptr;
}

void RequireBodyBounds(const LumaBounds& bounds) {
    REQUIRE(bounds.min.x == Approx(-0.5f).margin(1e-3));
    REQUIRE(bounds.min.y == Approx(0.0f).margin(1e-3));
    REQUIRE(bounds.min.z == Approx(-0.5f).margin(1e-3));
    REQUIRE(bounds.max.x == Approx(0.5f).margin(1e-3));
    REQUIRE(bounds.max.y == Approx(1.0f).margin(1e-3));
    REQUIRE(bounds.max.z == Approx(0.5f).margin(1e-3));
}

}  // namespace

TEST_CASE("SourceToLuma is identity for Y-up metres and converts Z-up centimetres",
          "[import][model]") {
    const Math::Mat4 identity = SourceToLuma(SourceAxes{});
    const Math::Vec3 kept = TransformPoint(identity, {1.0f, 2.0f, 3.0f});
    REQUIRE(kept.x == Approx(1.0f).margin(1e-5));
    REQUIRE(kept.y == Approx(2.0f).margin(1e-5));
    REQUIRE(kept.z == Approx(3.0f).margin(1e-5));

    SourceAxes zUp;
    zUp.up = 2;
    zUp.upSign = 1;
    zUp.front = 1;
    zUp.frontSign = -1;
    zUp.coord = 0;
    zUp.coordSign = 1;
    zUp.unitScaleCm = 100.0;
    const Math::Vec3 up = TransformPoint(SourceToLuma(zUp), {0.0f, 0.0f, 1.0f});
    REQUIRE(up.x == Approx(0.0f).margin(1e-5));
    REQUIRE(up.y == Approx(1.0f).margin(1e-5));
    REQUIRE(up.z == Approx(0.0f).margin(1e-5));

    SourceAxes centimetres = SourceAxes{};
    centimetres.unitScaleCm = 1.0;
    const Math::Vec3 scaled = TransformPoint(SourceToLuma(centimetres), {1.0f, 0.0f, 0.0f});
    REQUIRE(scaled.x == Approx(0.01f).margin(1e-6));
    REQUIRE(scaled.y == Approx(0.0f).margin(1e-6));
    REQUIRE(scaled.z == Approx(0.0f).margin(1e-6));
}

TEST_CASE("Model translator reads the robot glTF and GLB", "[import][model]") {
    ModelTranslator translator;
    REQUIRE(translator.Name() == "Luma.Import.Model");
    REQUIRE(translator.CanTranslate(Fixture("robot.gltf")));
    REQUIRE(translator.CanTranslate(Fixture("robot.glb")));
    REQUIRE(translator.CanTranslate(Fixture("robot.fbx")));
    REQUIRE(translator.CanTranslate(Fixture("robot.obj")));
    REQUIRE_FALSE(translator.CanTranslate(Fixture("img_rgba.png")));

    ImportScene gltf;
    std::string error;
    REQUIRE(translator.Translate(Fixture("robot.gltf"), gltf, &error));
    REQUIRE(error.empty());
    RequireRobotNodes(gltf, false);

    ImportScene glb;
    REQUIRE(translator.Translate(Fixture("robot.glb"), glb, &error));
    RequireRobotNodes(glb, true);
}

TEST_CASE("Model translator reads the robot OBJ", "[import][model]") {
    ModelTranslator translator;
    ImportScene scene;
    std::string error;
    REQUIRE(translator.Translate(Fixture("robot.obj"), scene, &error));
    REQUIRE(scene.OfKind(NodeKind::Mesh).size() == 2);
    REQUIRE(scene.OfKind(NodeKind::Material).size() == 2);
    REQUIRE(scene.OfKind(NodeKind::Texture).size() == 2);
    REQUIRE(FindNamed(scene, NodeKind::Mesh, "Body"));
    REQUIRE(FindNamed(scene, NodeKind::Mesh, "Arm"));
    const ImportNode* paint = FindNamed(scene, NodeKind::Material, "Paint");
    const ImportNode* diffuse = FindNamed(scene, NodeKind::Texture, "T_Paint_D");
    const ImportNode* normal = FindNamed(scene, NodeKind::Texture, "T_Paint_N");
    REQUIRE(paint);
    REQUIRE(diffuse);
    REQUIRE(normal);
    REQUIRE(paint->material.baseColorTex.texture == IdOf(scene, diffuse));
    REQUIRE(paint->material.normalTex.texture == IdOf(scene, normal));
    REQUIRE(normal->textureIsNormalMap);
}

TEST_CASE("FBX centimetres Z-up matches the glTF body and UVs agree", "[import][model]") {
    ModelTranslator translator;
    ImportScene fbx;
    std::string error;
    REQUIRE(translator.Translate(Fixture("robot.fbx"), fbx, &error));
    REQUIRE(FindNamed(fbx, NodeKind::Mesh, "Body"));
    REQUIRE(FindNamed(fbx, NodeKind::Mesh, "Arm"));
    REQUIRE(FindNamed(fbx, NodeKind::Material, "Paint"));
    REQUIRE(FindNamed(fbx, NodeKind::Material, "Metal"));

    const std::vector<std::string> names{"Body", "Arm"};
    ModelPayloadOptions options;
    const std::vector<LumaMeshData> gltfMeshes =
        translator.BuildMeshes(Fixture("robot.gltf"), names, options, &error);
    REQUIRE(error.empty());
    const std::vector<LumaMeshData> fbxMeshes =
        translator.BuildMeshes(Fixture("robot.fbx"), names, options, &error);
    REQUIRE(error.empty());
    const std::vector<LumaMeshData> objMeshes =
        translator.BuildMeshes(Fixture("robot.obj"), names, options, &error);
    REQUIRE(error.empty());

    REQUIRE(gltfMeshes.size() == 2);
    REQUIRE(fbxMeshes.size() == 2);
    REQUIRE(objMeshes.size() == 2);
    const LumaMeshData* gltfBody = MeshNamed(gltfMeshes, "Body");
    const LumaMeshData* fbxBody = MeshNamed(fbxMeshes, "Body");
    const LumaMeshData* objBody = MeshNamed(objMeshes, "Body");
    REQUIRE(gltfBody);
    REQUIRE(fbxBody);
    REQUIRE(objBody);
    RequireBodyBounds(gltfBody->bounds);
    RequireBodyBounds(fbxBody->bounds);

    // Vertex 0 of the fixture box is the top-left UV (0, 1).
    REQUIRE(gltfBody->vertices[0].texCoord.x == Approx(0.0f).margin(1e-4));
    REQUIRE(gltfBody->vertices[0].texCoord.y == Approx(1.0f).margin(1e-4));
    REQUIRE(fbxBody->vertices[0].texCoord.x == Approx(gltfBody->vertices[0].texCoord.x).margin(1e-4));
    REQUIRE(fbxBody->vertices[0].texCoord.y == Approx(gltfBody->vertices[0].texCoord.y).margin(1e-4));
    REQUIRE(objBody->vertices[0].texCoord.x == Approx(gltfBody->vertices[0].texCoord.x).margin(1e-4));
    REQUIRE(objBody->vertices[0].texCoord.y == Approx(gltfBody->vertices[0].texCoord.y).margin(1e-4));

    options.combineMeshes = true;
    const std::vector<LumaMeshData> combined =
        translator.BuildMeshes(Fixture("robot.gltf"), names, options, &error);
    REQUIRE(combined.size() == 1);
    REQUIRE(combined[0].submeshes.size() == 2);
    REQUIRE(combined[0].vertices.size() == 48);

    options.combineMeshes = false;
    options.pivot = ModelPayloadOptions::Pivot::BoundsBottom;
    const std::vector<LumaMeshData> bottom =
        translator.BuildMeshes(Fixture("robot.gltf"), names, options, &error);
    for (const LumaMeshData& mesh : bottom) {
        float minY = mesh.vertices[0].position.y;
        for (const LumaVertex& vertex : mesh.vertices) {
            minY = std::min(minY, vertex.position.y);
        }
        REQUIRE(minY == Approx(0.0f).margin(1e-4));
    }
}

TEST_CASE("A missing external texture is reported and translation still succeeds",
          "[import][model]") {
    const auto temp = std::filesystem::temp_directory_path() / "luma-import-missing-tex";
    std::filesystem::remove_all(temp);
    std::filesystem::create_directories(temp);
    std::filesystem::copy_file(Fixture("robot.gltf"), temp / "robot.gltf");
    std::filesystem::copy_file(Fixture("robot.bin"), temp / "robot.bin");

    ModelTranslator translator;
    ImportScene scene;
    std::string error;
    REQUIRE(translator.Translate(temp / "robot.gltf", scene, &error));
    const ImportNode* diffuse = FindNamed(scene, NodeKind::Texture, "T_Paint_D");
    REQUIRE(diffuse);
    REQUIRE_FALSE(diffuse->error.empty());
    REQUIRE(diffuse->error.find("not found") != std::string::npos);

    std::filesystem::remove_all(temp);
}
