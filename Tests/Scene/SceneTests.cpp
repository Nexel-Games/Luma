#include <catch2/catch_test_macros.hpp>

#include "Luma/Scene/Scene.h"

using namespace Luma;

TEST_CASE("DuplicateEntity copies every component independently", "[scene]") {
    Scene scene;
    Entity src = scene.CreateEntity("Lamp");
    scene.Registry().get<TransformComponent>(src).position = Math::Vec3(1.0f, 2.0f, 3.0f);
    auto& light = scene.Registry().emplace<LightComponent>(src);
    light.type = LightType::Spot;
    light.intensity = 7.5f;

    Entity copy = scene.DuplicateEntity(src);
    REQUIRE(scene.IsValid(copy));
    REQUIRE(copy != src);
    REQUIRE(scene.Registry().all_of<NameComponent, TransformComponent, LightComponent>(copy));
    CHECK_FALSE(scene.Registry().all_of<MeshRendererComponent>(copy));
    CHECK(scene.Registry().get<NameComponent>(copy).name == "Lamp");
    CHECK(scene.Registry().get<TransformComponent>(copy).position.y == 2.0f);
    CHECK(scene.Registry().get<LightComponent>(copy).type == LightType::Spot);
    CHECK(scene.Registry().get<LightComponent>(copy).intensity == 7.5f);

    // The copy is independent of the source.
    scene.Registry().get<LightComponent>(copy).intensity = 1.0f;
    CHECK(scene.Registry().get<LightComponent>(src).intensity == 7.5f);
    CHECK(scene.EntityCount() == 2);
}

TEST_CASE("DuplicateEntity of an invalid entity returns null", "[scene]") {
    Scene scene;
    Entity e = scene.CreateEntity("Gone");
    scene.DestroyEntity(e);
    CHECK(scene.DuplicateEntity(e) == kNullEntity);
    CHECK(scene.EntityCount() == 0);
}
