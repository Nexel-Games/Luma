#include <catch2/catch_test_macros.hpp>

#include "Luma/Scene/AssetReferences.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/Scene.h"

using namespace Luma;

namespace {

Entity Actor(Scene& scene, const char* name, std::string mesh, std::vector<std::string> materials) {
    const Entity e = scene.CreateEntity(name);
    auto& mr = scene.Registry().emplace<MeshRendererComponent>(e);
    mr.mesh = std::move(mesh);
    mr.materials = std::move(materials);
    return e;
}

const MeshRendererComponent& Mesh(Scene& scene, Entity e) {
    return scene.Registry().get<MeshRendererComponent>(e);
}

}  // namespace

TEST_CASE("Renaming an asset retargets meshes and material slots", "[scene][references]") {
    Scene scene;
    const Entity a = Actor(scene, "A", "/Game/Props/SM_Rock", {"/Game/M_Moss", ""});
    const Entity b = Actor(scene, "B", "/Game/Props/SM_Tree", {"/Game/M_Moss"});

    CHECK(RetargetAssetReferences(scene, "/Game/Props/SM_Rock", "/Game/Props/SM_Boulder") == 1);
    CHECK(Mesh(scene, a).mesh == "/Game/Props/SM_Boulder");
    CHECK(Mesh(scene, b).mesh == "/Game/Props/SM_Tree");

    CHECK(RetargetAssetReferences(scene, "/Game/M_Moss", "/Game/M_Lichen") == 2);
    CHECK(Mesh(scene, a).materials[0] == "/Game/M_Lichen");
    CHECK(Mesh(scene, a).materials[1].empty());
    CHECK(Mesh(scene, b).materials[0] == "/Game/M_Lichen");
}

TEST_CASE("Moving a folder retargets everything under it", "[scene][references]") {
    Scene scene;
    const Entity a = Actor(scene, "A", "/Game/Props/SM_Rock", {"/Game/Props/Mats/M_Rock"});
    const Entity b = Actor(scene, "B", "/Game/Prop/SM_Other", {"/Game/PropsExtra/M_X"});

    CHECK(RetargetAssetReferences(scene, "/Game/Props", "/Game/Rocks") == 2);
    CHECK(Mesh(scene, a).mesh == "/Game/Rocks/SM_Rock");
    CHECK(Mesh(scene, a).materials[0] == "/Game/Rocks/Mats/M_Rock");
    // Whole path segments only.
    CHECK(Mesh(scene, b).mesh == "/Game/Prop/SM_Other");
    CHECK(Mesh(scene, b).materials[0] == "/Game/PropsExtra/M_X");

    CHECK(RetargetAssetReferences(scene, "/Game/Nothing", "/Game/Else") == 0);
    CHECK(RetargetAssetReferences(scene, "", "/Game/Else") == 0);
}
