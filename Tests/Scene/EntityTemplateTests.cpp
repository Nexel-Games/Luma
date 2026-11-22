#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/EntityTemplates.h"
#include "Luma/Scene/Scene.h"

using namespace Luma;

namespace {

std::vector<std::string> TemplateIds(const EntityTemplateRegistry& reg) {
    std::vector<std::string> ids;
    for (const EntityTemplate* t : reg.All()) ids.push_back(t->id);
    return ids;
}

EntityTemplate SimpleTemplate(std::string id) {
    EntityTemplate t;
    t.id = std::move(id);
    t.displayName = "Simple";
    t.category = "Test";
    t.owner = "Test.Plugin";
    t.create = [](Scene& scene, const EntityTemplateContext&) {
        return scene.CreateEntity(UniqueEntityName(scene, "Simple"));
    };
    return t;
}

}  // namespace

TEST_CASE("Built-in entity templates are registered in menu order", "[scene][templates]") {
    EntityTemplateRegistry reg;
    RegistrationSet set = RegisterBuiltinEntityTemplates(reg);
    REQUIRE(TemplateIds(reg) ==
            std::vector<std::string>{"Empty", "Cube", "Plane", "Sphere", "Cylinder",
                                     "DirectionalLight", "PointLight", "SpotLight", "TubeLight",
                                     "Environment", "PostProcessVolume"});
    REQUIRE(reg.Find("Cube")->category == "Geometry");
    REQUIRE(reg.Find("PointLight")->category == "Lights");
    REQUIRE(reg.Find("Environment")->singletonComponent == "Environment");
    REQUIRE(reg.Find("Environment")->autoCreate);
    for (const EntityTemplate* t : reg.All()) REQUIRE(t->owner == "Engine");
    set.Reset();
    REQUIRE(reg.All().empty());
}

TEST_CASE("Instantiating templates builds configured entities with unique names",
          "[scene][templates]") {
    Scene scene;
    const EntityTemplate* cube = EntityTemplates().Find("Cube");
    Entity a = Instantiate(scene, *cube, {1});
    Entity b = Instantiate(scene, *cube, {2});
    REQUIRE(scene.Registry().get<NameComponent>(a).name == "Cube");
    REQUIRE(scene.Registry().get<NameComponent>(b).name == "Cube 1");
    REQUIRE(scene.Registry().get<MeshRendererComponent>(a).primitive == MeshPrimitive::Cube);
    REQUIRE(scene.Registry().get<TransformComponent>(b).position.x == 2.0f);

    Entity sun = Instantiate(scene, *EntityTemplates().Find("DirectionalLight"), {1});
    REQUIRE(scene.Registry().get<LightComponent>(sun).type == LightType::Directional);
    REQUIRE(scene.Registry().get<TransformComponent>(sun).rotationEuler.x == -50.0f);
    Entity spot = Instantiate(scene, *EntityTemplates().Find("SpotLight"), {1});
    REQUIRE(scene.Registry().get<LightComponent>(spot).type == LightType::Spot);

    Entity empty = Instantiate(scene, *EntityTemplates().Find("Empty"), {1});
    REQUIRE(scene.Registry().get<NameComponent>(empty).name == "Empty Actor");
    REQUIRE(ComponentsOf(scene, empty).size() == 2);  // Name + Transform
}

TEST_CASE("A singleton template selects the existing holder", "[scene][templates]") {
    Scene scene;
    const EntityTemplate* env = EntityTemplates().Find("Environment");
    Entity first = Instantiate(scene, *env, {1});
    REQUIRE(scene.Registry().all_of<EnvironmentComponent>(first));
    Entity second = Instantiate(scene, *env, {2});
    REQUIRE(second == first);
    REQUIRE(scene.EntityCount() == 1);
}

TEST_CASE("Auto-created templates fill scenes that lack them", "[scene][templates]") {
    Scene scene;
    REQUIRE(EnsureAutoCreatedEntities(scene) == 1);
    REQUIRE(scene.Registry().view<EnvironmentComponent>().size() == 1);
    REQUIRE(EnsureAutoCreatedEntities(scene) == 0);
}

TEST_CASE("Entity template registry validates and unregisters", "[scene][templates]") {
    EntityTemplateRegistry reg;
    RegistrationToken a = reg.Register(SimpleTemplate("Simple"));
    REQUIRE(a.IsActive());
    std::string error;
    REQUIRE_FALSE(reg.Register(SimpleTemplate("Simple"), &error).IsActive());
    REQUIRE(error.find("already registered") != std::string::npos);

    EntityTemplate bad = SimpleTemplate("Auto");
    bad.autoCreate = true;  // needs a singleton component to know when it exists
    REQUIRE_FALSE(reg.Register(bad, &error).IsActive());
    REQUIRE(error.find("singletonComponent") != std::string::npos);

    EntityTemplate noCreate = SimpleTemplate("NoCreate");
    noCreate.create = nullptr;
    REQUIRE_FALSE(reg.Register(noCreate, &error).IsActive());

    a.Reset();
    REQUIRE(reg.Find("Simple") == nullptr);
}

TEST_CASE("Outliner icons come from the component registry", "[scene][templates]") {
    Scene scene;
    Entity cube = Instantiate(scene, *EntityTemplates().Find("Cube"), {1});
    Entity sphere = Instantiate(scene, *EntityTemplates().Find("Sphere"), {1});
    Entity point = Instantiate(scene, *EntityTemplates().Find("PointLight"), {1});
    Entity env = Instantiate(scene, *EntityTemplates().Find("Environment"), {1});
    Entity empty = Instantiate(scene, *EntityTemplates().Find("Empty"), {1});
    REQUIRE(OutlinerIconFor(scene, cube) == "ActorCube");
    REQUIRE(OutlinerIconFor(scene, sphere) == "ActorSphere");
    REQUIRE(OutlinerIconFor(scene, point) == "ActorPointLight");
    REQUIRE(OutlinerIconFor(scene, env) == "CategoryEnvironment");
    REQUIRE(OutlinerIconFor(scene, empty).empty());

    // The highest-priority component wins: a mesh with a light shows the mesh.
    Components().Find("Light")->variants[0].apply(scene, cube);
    REQUIRE(OutlinerIconFor(scene, cube) == "ActorCube");
}

TEST_CASE("One-per-scene components are found on an entity", "[scene][templates]") {
    Scene scene;
    Entity env = Instantiate(scene, *EntityTemplates().Find("Environment"), {1});
    Entity cube = Instantiate(scene, *EntityTemplates().Find("Cube"), {1});
    const ComponentDesc* single = OnePerSceneComponentOf(scene, env);
    REQUIRE(single != nullptr);
    REQUIRE(single->id == "Environment");
    REQUIRE(OnePerSceneComponentOf(scene, cube) == nullptr);
}
