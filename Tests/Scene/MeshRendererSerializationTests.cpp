#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Luma/Scene/Components.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SceneSerializer.h"

using namespace Luma;

TEST_CASE("A mesh renderer's static mesh is saved with the scene", "[scene][mesh]") {
    Scene scene;
    const Entity rock = scene.CreateEntity("Rock");
    auto& mr = scene.Registry().emplace<MeshRendererComponent>(rock);
    mr.mesh = "/Game/Props/SM_Rock";
    mr.materials = {"", "/Game/M_Moss"};

    const std::string json = SceneSerializer::SaveToString(scene);
    Scene loaded;
    std::string err;
    REQUIRE(SceneSerializer::LoadFromString(loaded, json, &err));
    bool found = false;
    for (Entity e : loaded.Registry().view<const MeshRendererComponent>()) {
        const auto& m = loaded.Registry().get<const MeshRendererComponent>(e);
        CHECK(m.mesh == "/Game/Props/SM_Rock");
        CHECK(m.materials == std::vector<std::string>{"", "/Game/M_Moss"});
        CHECK(m.UsesMeshAsset());
        found = true;
    }
    CHECK(found);
}

TEST_CASE("Scenes saved before static meshes load with the primitive", "[scene][mesh]") {
    Scene scene;
    const Entity cube = scene.CreateEntity("Cube");
    auto& mr = scene.Registry().emplace<MeshRendererComponent>(cube);
    mr.primitive = MeshPrimitive::Cylinder;
    std::string json = SceneSerializer::SaveToString(scene);
    // Strip the "mesh" field as an older editor would have written it.
    const auto at = json.find("\"mesh\"");
    REQUIRE(at != std::string::npos);
    const auto lineStart = json.rfind('\n', at);
    const auto lineEnd = json.find('\n', at);
    json.erase(lineStart, lineEnd - lineStart);

    Scene loaded;
    std::string err;
    REQUIRE(SceneSerializer::LoadFromString(loaded, json, &err));
    for (Entity e : loaded.Registry().view<const MeshRendererComponent>()) {
        const auto& m = loaded.Registry().get<const MeshRendererComponent>(e);
        CHECK(m.mesh.empty());
        CHECK_FALSE(m.UsesMeshAsset());
        CHECK(m.primitive == MeshPrimitive::Cylinder);
    }
}
