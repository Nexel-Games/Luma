#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Serialization/Json.h"

using Luma::ComponentDesc;
using Luma::Entity;
using Luma::RegistrationToken;
using Luma::Scene;
using Luma::SceneSerializer;
using Luma::UnresolvedComponents;

namespace {

// Stands in for a component provided by a plugin that may be missing.
struct OrbitComponent {
    float radius = 2.0f;
    float speed = 0.25f;
    std::string target = "/Game/Planet";
};

RegistrationToken RegisterOrbit() {
    ComponentDesc d;
    d.id = "Orbit";
    d.displayName = "Orbit";
    d.category = "Gameplay";
    d.owner = "Test.Orbits";
    return Luma::Components().Register<OrbitComponent>(std::move(d));
}

Entity FindByName(Scene& scene, const std::string& name) {
    for (auto e : scene.Registry().view<Luma::NameComponent>()) {
        if (scene.Registry().get<Luma::NameComponent>(e).name == name) return e;
    }
    return Luma::kNullEntity;
}

// A scene saved while the Orbit plugin was loaded.
std::string SaveSceneWithOrbit(bool pretty) {
    RegistrationToken orbit = RegisterOrbit();
    REQUIRE(orbit.IsActive());
    Scene scene;
    Entity moon = scene.CreateEntity("Moon");
    auto& o = scene.Registry().emplace<OrbitComponent>(moon);
    o.radius = 7.25f;
    o.speed = 0.1f;
    o.target = "/Game/Earth";
    scene.Registry().emplace<Luma::LightComponent>(moon).intensity = 0.3f;
    scene.CreateEntity("Plain");
    return SceneSerializer::SaveToString(scene, pretty);
}

}  // namespace

namespace Luma {
template <>
const TypeInfo<OrbitComponent>& GetTypeInfo<OrbitComponent>() {
    static const TypeInfo<OrbitComponent> info = [] {
        TypeBuilder<OrbitComponent> b("Orbit");
        b.Property("radius", &OrbitComponent::radius);
        b.Property("speed", &OrbitComponent::speed);
        b.Property("target", &OrbitComponent::target);
        return b.Build();
    }();
    return info;
}
}  // namespace Luma

TEST_CASE("Scene files are version 3", "[scene][serialize][unresolved]") {
    Scene scene;
    scene.CreateEntity("A");
    const auto doc = Luma::ParseJson(SceneSerializer::SaveToString(scene));
    REQUIRE(doc.has_value());
    REQUIRE(doc->Find("version")->AsInt() == 3);
    REQUIRE(SceneSerializer::kVersion == 3);
    // Built-in-only scenes record no component owners.
    REQUIRE(doc->Find("componentOwners") == nullptr);
}

TEST_CASE("A plugin component records its owner in the scene", "[scene][serialize][unresolved]") {
    const auto doc = Luma::ParseJson(SaveSceneWithOrbit(true));
    REQUIRE(doc.has_value());
    const Luma::SerialValue* owners = doc->Find("componentOwners");
    REQUIRE(owners != nullptr);
    REQUIRE(owners->Find("Orbit")->AsString() == "Test.Orbits");
}

TEST_CASE("An unknown component survives load and save byte-identical",
          "[scene][serialize][unresolved]") {
    for (bool pretty : {true, false}) {
        INFO("pretty=" << pretty);
        const std::string original = SaveSceneWithOrbit(pretty);
        REQUIRE(Luma::Components().Find("Orbit") == nullptr);  // plugin gone

        Scene scene;
        std::string error;
        REQUIRE(SceneSerializer::LoadFromString(scene, original, &error));
        Entity moon = FindByName(scene, "Moon");
        REQUIRE(moon != Luma::kNullEntity);
        const auto* unresolved = scene.Registry().try_get<UnresolvedComponents>(moon);
        REQUIRE(unresolved != nullptr);
        REQUIRE(unresolved->payloads.size() == 1);
        REQUIRE(unresolved->payloads[0].id == "Orbit");
        REQUIRE(unresolved->payloads[0].owner == "Test.Orbits");
        REQUIRE(unresolved->Find("Orbit") != nullptr);
        // The known components on the same entity still load.
        REQUIRE(scene.Registry().get<Luma::LightComponent>(moon).intensity == 0.3f);
        REQUIRE_FALSE(scene.Registry().all_of<UnresolvedComponents>(FindByName(scene, "Plain")));

        REQUIRE(SceneSerializer::SaveToString(scene, pretty) == original);

        // ...and again (load -> save -> load -> save).
        Scene again;
        REQUIRE(SceneSerializer::LoadFromString(again, SceneSerializer::SaveToString(scene, pretty)));
        REQUIRE(SceneSerializer::SaveToString(again, pretty) == original);
    }
}

TEST_CASE("Unknown components keep their key order among known ones",
          "[scene][serialize][unresolved]") {
    const std::string input =
        R"({"version":3,"componentOwners":{"Alpha":"P.A","Zeta":"P.Z"},"entities":[)"
        R"({"components":{"Name":{"name":"X"},"Zeta":{"b":1,"a":2.5,"s":"t"},)"
        R"("Alpha":[1,"two",null,{"k":true},-0.125e-7]}}]})";
    Scene scene;
    REQUIRE(SceneSerializer::LoadFromString(scene, input));
    const auto canonical = Luma::ParseJson(input);
    REQUIRE(canonical.has_value());
    REQUIRE(SceneSerializer::SaveToString(scene, false) == Luma::WriteJson(*canonical, false));
}

TEST_CASE("Registering the type later resolves preserved components",
          "[scene][serialize][unresolved]") {
    const std::string original = SaveSceneWithOrbit(true);
    Scene scene;
    REQUIRE(SceneSerializer::LoadFromString(scene, original));
    Entity moon = FindByName(scene, "Moon");

    RegistrationToken orbit = RegisterOrbit();
    REQUIRE(Luma::ResolveUnresolvedComponents(scene) == 1);
    REQUIRE_FALSE(scene.Registry().all_of<UnresolvedComponents>(moon));
    const auto& o = scene.Registry().get<OrbitComponent>(moon);
    REQUIRE(o.radius == 7.25f);
    REQUIRE(o.target == "/Game/Earth");
    REQUIRE(SceneSerializer::SaveToString(scene, true) == original);
    REQUIRE(Luma::ResolveUnresolvedComponents(scene) == 0);
}

TEST_CASE("Loading with the plugin present resolves components directly",
          "[scene][serialize][unresolved]") {
    const std::string original = SaveSceneWithOrbit(false);
    RegistrationToken orbit = RegisterOrbit();
    Scene scene;
    REQUIRE(SceneSerializer::LoadFromString(scene, original));
    Entity moon = FindByName(scene, "Moon");
    REQUIRE(scene.Registry().all_of<OrbitComponent>(moon));
    REQUIRE_FALSE(scene.Registry().all_of<UnresolvedComponents>(moon));
    REQUIRE(SceneSerializer::SaveToString(scene, false) == original);
}

TEST_CASE("Live components can be demoted to preserved payloads",
          "[scene][serialize][unresolved]") {
    RegistrationToken orbit = RegisterOrbit();
    Scene scene;
    Entity moon = scene.CreateEntity("Moon");
    scene.Registry().emplace<OrbitComponent>(moon).radius = 3.0f;
    const std::string before = SceneSerializer::SaveToString(scene);

    const ComponentDesc* desc = Luma::Components().Find("Orbit");
    REQUIRE(Luma::UnresolveComponents(scene, *desc) == 1);
    REQUIRE_FALSE(scene.Registry().all_of<OrbitComponent>(moon));
    REQUIRE(scene.Registry().get<UnresolvedComponents>(moon).payloads.size() == 1);
    orbit.Reset();
    REQUIRE(SceneSerializer::SaveToString(scene) == before);
}

TEST_CASE("Load then save keeps the entity order", "[scene][serialize]") {
    Scene scene;
    for (const char* n : {"First", "Second", "Third", "Fourth"}) scene.CreateEntity(n);
    const std::string once = SceneSerializer::SaveToString(scene);
    REQUIRE(once.find("First") < once.find("Second"));
    REQUIRE(once.find("Third") < once.find("Fourth"));
    Scene loaded;
    REQUIRE(SceneSerializer::LoadFromString(loaded, once));
    REQUIRE(SceneSerializer::SaveToString(loaded) == once);
}

TEST_CASE("Duplicating an entity keeps its preserved payloads", "[scene][unresolved]") {
    const std::string original = SaveSceneWithOrbit(true);
    Scene scene;
    REQUIRE(SceneSerializer::LoadFromString(scene, original));
    Entity copy = scene.DuplicateEntity(FindByName(scene, "Moon"));
    REQUIRE(scene.Registry().get<UnresolvedComponents>(copy).Find("Orbit") != nullptr);
}

TEST_CASE("Scene files from a newer engine are rejected", "[scene][serialize]") {
    Scene scene;
    scene.CreateEntity("Keep");
    std::string error;
    REQUIRE_FALSE(SceneSerializer::LoadFromString(scene, R"({"version":4,"entities":[]})", &error));
    REQUIRE(error.find("newer") != std::string::npos);
    REQUIRE(scene.EntityCount() == 1);  // untouched on failure
    REQUIRE_FALSE(SceneSerializer::LoadFromString(scene, R"({"version":"x","entities":[]})", &error));
    REQUIRE_FALSE(SceneSerializer::LoadFromString(scene, R"({"version":0,"entities":[]})", &error));
}

TEST_CASE("Version 2 scenes load unchanged", "[scene][serialize]") {
    const std::string v2 =
        R"({"version":2,"entities":[{"components":{"Name":{"name":"Old"},)"
        R"("Light":{"type":1,"intensity":4.0}}}]})";
    Scene scene;
    REQUIRE(SceneSerializer::LoadFromString(scene, v2));
    Entity e = FindByName(scene, "Old");
    REQUIRE(scene.Registry().get<Luma::LightComponent>(e).type == Luma::LightType::Point);
    REQUIRE_FALSE(scene.Registry().all_of<UnresolvedComponents>(e));
    const auto saved = Luma::ParseJson(SceneSerializer::SaveToString(scene));
    REQUIRE(saved->Find("version")->AsInt() == 3);
}
