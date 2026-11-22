#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>

#include "Luma/Scene/Components.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SceneSerializer.h"

using Luma::EnvironmentComponent;
using Luma::LightComponent;
using Luma::LightType;
using Luma::MeshRendererComponent;
using Luma::NameComponent;
using Luma::Scene;
using Luma::SceneSerializer;
using Luma::TransformComponent;

namespace {

// Builds a small scene: a named cube with a transform + mesh, and a point light.
Scene MakeSampleScene() {
    Scene scene;

    Luma::Entity cube = scene.CreateEntity("Cube");
    auto& t = scene.Registry().get<TransformComponent>(cube);
    t.position = {1.0f, 2.0f, 3.0f};
    t.rotationEuler = {0.0f, 90.0f, 0.0f};
    t.scale = {2.0f, 2.0f, 2.0f};
    auto& mesh = scene.Registry().emplace<MeshRendererComponent>(cube);
    mesh.primitive = Luma::MeshPrimitive::Sphere;
    mesh.materials = {"/Game/Materials/MI_Blue", ""};

    Luma::Entity light = scene.CreateEntity("Key Light");
    auto& lc = scene.Registry().emplace<LightComponent>(light);
    lc.type = LightType::Spot;
    lc.intensity = 12.0f;
    lc.outerAngleDeg = 45.0f;

    return scene;
}

}  // namespace

TEST_CASE("Scene serializes to non-empty JSON with a version and entities",
          "[scene][serialize]") {
    Scene scene = MakeSampleScene();
    const std::string json = SceneSerializer::SaveToString(scene);
    REQUIRE_FALSE(json.empty());
    REQUIRE(json.find("\"version\"") != std::string::npos);
    REQUIRE(json.find("Cube") != std::string::npos);
    REQUIRE(json.find("Key Light") != std::string::npos);
}

TEST_CASE("Scene round-trips through save/load preserving components",
          "[scene][serialize]") {
    Scene original = MakeSampleScene();
    const std::string json = SceneSerializer::SaveToString(original);

    Scene loaded;
    std::string err;
    REQUIRE(SceneSerializer::LoadFromString(loaded, json, &err));
    REQUIRE(err.empty());

    REQUIRE(loaded.EntityCount() == 2);

    // Find the cube by name and check its component values survived.
    bool foundCube = false;
    bool foundLight = false;
    auto view = loaded.Registry().view<NameComponent>();
    for (auto entity : view) {
        const auto& name = view.get<NameComponent>(entity).name;
        if (name == "Cube") {
            foundCube = true;
            const auto& t = loaded.Registry().get<TransformComponent>(entity);
            REQUIRE(t.position.x == 1.0f);
            REQUIRE(t.position.y == 2.0f);
            REQUIRE(t.position.z == 3.0f);
            REQUIRE(t.rotationEuler.y == 90.0f);
            REQUIRE(t.scale.x == 2.0f);
            REQUIRE(loaded.Registry().all_of<MeshRendererComponent>(entity));
            const auto& m = loaded.Registry().get<MeshRendererComponent>(entity);
            REQUIRE(m.primitive == Luma::MeshPrimitive::Sphere);
            REQUIRE(m.materials ==
                    std::vector<std::string>{"/Game/Materials/MI_Blue", ""});
        } else if (name == "Key Light") {
            foundLight = true;
            REQUIRE(loaded.Registry().all_of<LightComponent>(entity));
            const auto& lc = loaded.Registry().get<LightComponent>(entity);
            REQUIRE(lc.type == LightType::Spot);
            REQUIRE(lc.intensity == 12.0f);
            REQUIRE(lc.outerAngleDeg == 45.0f);
        }
    }
    REQUIRE(foundCube);
    REQUIRE(foundLight);
}

TEST_CASE("Post-process volumes survive a save/load round-trip",
          "[scene][serialize]") {
    // Regression: PostProcessComponent had reflection + an Inspector but was
    // missing from the serializer, so volumes were silently dropped on save.
    Scene original;
    Luma::Entity e = original.CreateEntity("Post Process Volume");
    auto& pp = original.Registry().emplace<Luma::PostProcessComponent>(e);
    pp.priority = 3;
    pp.bloomIntensity = 0.75f;
    pp.bloomThreshold = 1.5f;
    pp.toneMapper = Luma::PostProcessComponent::ToneMapper::AgX;
    pp.exposure = 1.5f;

    const std::string json = SceneSerializer::SaveToString(original);
    REQUIRE(json.find("\"PostProcess\"") != std::string::npos);

    Scene loaded;
    std::string err;
    REQUIRE(SceneSerializer::LoadFromString(loaded, json, &err));
    REQUIRE(err.empty());
    auto view = loaded.Registry().view<const Luma::PostProcessComponent>();
    REQUIRE(view.size() == 1);
    const auto& out = view.get<const Luma::PostProcessComponent>(view.front());
    REQUIRE(out.priority == 3);
    REQUIRE(out.bloomIntensity == 0.75f);
    REQUIRE(out.bloomThreshold == 1.5f);
    REQUIRE(out.toneMapper == Luma::PostProcessComponent::ToneMapper::AgX);
    REQUIRE(out.exposure == 1.5f);
}

TEST_CASE("Loading a scene clears any previous contents", "[scene][serialize]") {
    Scene scene = MakeSampleScene();      // 2 entities
    const std::string json = SceneSerializer::SaveToString(scene);

    Scene target;
    target.CreateEntity("Stale");         // should be gone after load
    REQUIRE(SceneSerializer::LoadFromString(target, json));
    REQUIRE(target.EntityCount() == 2);
}

TEST_CASE("Loading malformed JSON fails with an error", "[scene][serialize]") {
    Scene scene;
    std::string err;
    REQUIRE_FALSE(SceneSerializer::LoadFromString(scene, "{ not json", &err));
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("Scene round-trips through a file on disk", "[scene][serialize]") {
    namespace fs = std::filesystem;
    static std::atomic<int> counter{0};
    fs::path path = fs::temp_directory_path() /
                    ("luma_scene_" + std::to_string(counter.fetch_add(1)) +
                     ".lscene");

    Scene original = MakeSampleScene();
    std::string err;
    REQUIRE(SceneSerializer::SaveToFile(original, path, &err));
    REQUIRE(err.empty());
    REQUIRE(fs::exists(path));

    Scene loaded;
    REQUIRE(SceneSerializer::LoadFromFile(loaded, path, &err));
    REQUIRE(loaded.EntityCount() == 2);

    std::error_code ec;
    fs::remove(path, ec);
}

TEST_CASE("Loading a missing scene file fails with an error",
          "[scene][serialize]") {
    Scene scene;
    std::string err;
    REQUIRE_FALSE(SceneSerializer::LoadFromFile(
        scene, "Z:/luma/does/not/exist.lscene", &err));
    REQUIRE_FALSE(err.empty());
}

TEST_CASE("Post-process materials survive a save/load round-trip", "[scene][serialize]") {
    Scene original;
    Luma::Entity e = original.CreateEntity("Post Process Volume");
    auto& pp = original.Registry().emplace<Luma::PostProcessComponent>(e);
    pp.materials.push_back({"/Game/Materials/PP_Invert", 0.5f, true});
    pp.materials.push_back({"/Game/Materials/PP_Outline", 1.0f, false});

    const std::string json = SceneSerializer::SaveToString(original);
    Scene loaded;
    std::string err;
    REQUIRE(SceneSerializer::LoadFromString(loaded, json, &err));
    auto view = loaded.Registry().view<const Luma::PostProcessComponent>();
    REQUIRE(view.size() == 1);
    const auto& out = view.get<const Luma::PostProcessComponent>(view.front());
    REQUIRE(out.materials.size() == 2);
    CHECK(out.materials[0].material == "/Game/Materials/PP_Invert");
    CHECK(out.materials[0].weight == 0.5f);
    CHECK(out.materials[0].enabled);
    CHECK(out.materials[1].material == "/Game/Materials/PP_Outline");
    CHECK_FALSE(out.materials[1].enabled);
}
