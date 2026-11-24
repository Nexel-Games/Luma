#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "Luma/Scene/ComponentReflection.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Scene.h"

using Luma::ComponentDesc;
using Luma::ComponentRegistry;
using Luma::Entity;
using Luma::RegistrationToken;
using Luma::Scene;
using Luma::SerialValue;

namespace {

// A plugin-style component used only by these tests.
struct SpinComponent {
    float degreesPerSecond = 45.0f;
    bool enabled = true;
};

std::vector<std::string> Ids(const ComponentRegistry& reg) {
    std::vector<std::string> ids;
    for (const ComponentDesc* d : reg.All()) ids.push_back(d->id);
    return ids;
}

// A hand-written (non-template) descriptor, the way a plugin without reflection
// would register: stores a flag in a side table keyed by entity.
ComponentDesc ManualDesc(std::string id, std::vector<Entity>* storage) {
    ComponentDesc d;
    d.id = std::move(id);
    d.displayName = "Manual";
    d.owner = "Test.Plugin";
    d.add = [storage](Scene&, Entity e) { storage->push_back(e); };
    d.remove = [storage](Scene&, Entity e) { std::erase(*storage, e); };
    d.has = [storage](const Scene&, Entity e) {
        return std::find(storage->begin(), storage->end(), e) != storage->end();
    };
    d.write = [](const Scene&, Entity, SerialValue& out) { out = SerialValue::MakeObject(); };
    d.read = [storage](Scene&, Entity e, const SerialValue&) { storage->push_back(e); };
    d.reset = [](Scene&, Entity) {};
    return d;
}

}  // namespace

namespace Luma {
template <>
const TypeInfo<SpinComponent>& GetTypeInfo<SpinComponent>() {
    static const TypeInfo<SpinComponent> info = [] {
        TypeBuilder<SpinComponent> b("Spin");
        b.Property("degreesPerSecond", &SpinComponent::degreesPerSecond).Range(-720.0, 720.0);
        b.Property("enabled", &SpinComponent::enabled);
        return b.Build();
    }();
    return info;
}
}  // namespace Luma

TEST_CASE("Built-in components are registered in order with their scene keys",
          "[scene][components]") {
    ComponentRegistry reg;
    Luma::RegistrationSet builtins = Luma::RegisterBuiltinComponents(reg);
    REQUIRE(builtins.Size() == 8);
    REQUIRE(Ids(reg) == std::vector<std::string>{"Name", "Transform", "MeshRenderer", "Camera",
                                                 "Light", "Environment", "PostProcess", "PrefabLink"});
    for (const ComponentDesc* d : reg.All()) {
        INFO(d->id);
        REQUIRE(d->owner == "Engine");
        REQUIRE_FALSE(d->displayName.empty());
        // The prefab link is bookkeeping (a serialized baseline, not editable
        // fields): it reads and writes itself and has no reflection.
        if (d->id == "PrefabLink") continue;
        REQUIRE(d->reflection != nullptr);
        REQUIRE(d->reflection->Name() == d->id);
    }
    REQUIRE_FALSE(reg.Find("PrefabLink")->userAddable);
    // Name and Transform are structural: every scene object has them.
    REQUIRE_FALSE(reg.Find("Name")->userAddable);
    REQUIRE_FALSE(reg.Find("Transform")->userAddable);
    REQUIRE(reg.Find("Environment")->onePerScene);
    REQUIRE_FALSE(reg.Find("Light")->onePerScene);
    REQUIRE(reg.Find("Light")->variants.size() == 4);

    builtins.Reset();
    REQUIRE(reg.All().empty());
}

TEST_CASE("The process-wide registry carries the built-ins", "[scene][components]") {
    ComponentRegistry& reg = Luma::Components();
    for (const char* id :
         {"Name", "Transform", "MeshRenderer", "Camera", "Light", "Environment", "PostProcess"}) {
        INFO(id);
        REQUIRE(reg.Find(id) != nullptr);
    }
}

TEST_CASE("Registering a duplicate id is an error", "[scene][components]") {
    ComponentRegistry reg;
    std::vector<Entity> storage;
    RegistrationToken a = reg.Register(ManualDesc("Manual", &storage));
    REQUIRE(a.IsActive());
    std::string error;
    RegistrationToken b = reg.Register(ManualDesc("Manual", &storage), &error);
    REQUIRE_FALSE(b.IsActive());
    REQUIRE(error.find("Manual") != std::string::npos);
    REQUIRE(error.find("already registered") != std::string::npos);
    REQUIRE(reg.All().size() == 1);
}

TEST_CASE("Invalid component descriptors are rejected with a reason", "[scene][components]") {
    ComponentRegistry reg;
    std::vector<Entity> storage;
    auto expectRejected = [&](ComponentDesc d, const char* needle) {
        std::string error;
        RegistrationToken t = reg.Register(std::move(d), &error);
        INFO(error);
        REQUIRE_FALSE(t.IsActive());
        REQUIRE(error.find(needle) != std::string::npos);
    };
    expectRejected(ManualDesc("", &storage), "id");
    expectRejected(ManualDesc("Bad Id", &storage), "Bad Id");
    expectRejected(ManualDesc("Bad/Id", &storage), "Bad/Id");
    {
        ComponentDesc d = ManualDesc("NoOwner", &storage);
        d.owner.clear();
        expectRejected(std::move(d), "owner");
    }
    {
        ComponentDesc d = ManualDesc("NoRead", &storage);
        d.read = nullptr;
        expectRejected(std::move(d), "read");
    }
    {
        ComponentDesc d = ManualDesc("NoHas", &storage);
        d.has = nullptr;
        expectRejected(std::move(d), "has");
    }
    REQUIRE(reg.All().empty());
}

TEST_CASE("Unregistering removes the entry and keeps the order of the rest",
          "[scene][components]") {
    ComponentRegistry reg;
    std::vector<Entity> storage;
    RegistrationToken a = reg.Register(ManualDesc("A", &storage));
    RegistrationToken b = reg.Register(ManualDesc("B", &storage));
    RegistrationToken c = reg.Register(ManualDesc("C", &storage));
    const auto rev = reg.Revision();
    b.Reset();
    REQUIRE(reg.Find("B") == nullptr);
    REQUIRE(Ids(reg) == std::vector<std::string>{"A", "C"});
    REQUIRE(reg.Revision() != rev);

    // The id is free again; a stale token never removes the new registration.
    RegistrationToken b2 = reg.Register(ManualDesc("B", &storage));
    REQUIRE(b2.IsActive());
    b.Reset();
    REQUIRE(reg.Find("B") != nullptr);
    REQUIRE(Ids(reg) == std::vector<std::string>{"A", "C", "B"});
}

TEST_CASE("Register<T> fills add/has/remove/write/read from reflection",
          "[scene][components]") {
    ComponentRegistry reg;
    ComponentDesc base;
    base.id = "Spin";
    base.displayName = "Spinner";
    base.category = "Gameplay";
    base.owner = "Test.Plugin";
    RegistrationToken t = reg.Register<SpinComponent>(std::move(base));
    REQUIRE(t.IsActive());
    const ComponentDesc* d = reg.Find("Spin");
    REQUIRE(d != nullptr);
    REQUIRE(d->reflection == &Luma::GetTypeInfo<SpinComponent>());
    REQUIRE(d->reflection->PropertyCount() == 2);
    REQUIRE(d->reflection->PropertyName(0) == "degreesPerSecond");
    REQUIRE(d->reflection->PropertyMetaAt(0).hasRange);

    Scene scene;
    Entity e = scene.CreateEntity("Spinner");
    REQUIRE_FALSE(d->has(scene, e));
    d->add(scene, e);
    REQUIRE(d->has(scene, e));
    scene.Registry().get<SpinComponent>(e).degreesPerSecond = 90.0f;
    scene.Registry().get<SpinComponent>(e).enabled = false;
    // add on an entity that already has it keeps the existing value
    d->add(scene, e);
    REQUIRE(scene.Registry().get<SpinComponent>(e).degreesPerSecond == 90.0f);

    SerialValue payload;
    d->write(scene, e, payload);
    REQUIRE(payload.IsObject());
    REQUIRE(payload.Find("degreesPerSecond")->AsFloat() == 90.0);

    Entity other = scene.CreateEntity("Other");
    d->read(scene, other, payload);
    REQUIRE(d->has(scene, other));
    REQUIRE(scene.Registry().get<SpinComponent>(other).degreesPerSecond == 90.0f);
    REQUIRE_FALSE(scene.Registry().get<SpinComponent>(other).enabled);

    d->remove(scene, e);
    REQUIRE_FALSE(d->has(scene, e));
}

TEST_CASE("Built-in descriptors round-trip their component through write/read",
          "[scene][components]") {
    const ComponentDesc* light = Luma::Components().Find("Light");
    REQUIRE(light != nullptr);
    Scene scene;
    Entity e = scene.CreateEntity("Lamp");
    light->add(scene, e);
    auto& lc = scene.Registry().get<Luma::LightComponent>(e);
    lc.type = Luma::LightType::Spot;
    lc.intensity = 7.5f;
    SerialValue v;
    light->write(scene, e, v);
    Entity copy = scene.CreateEntity("Copy");
    light->read(scene, copy, v);
    REQUIRE(scene.Registry().get<Luma::LightComponent>(copy).type == Luma::LightType::Spot);
    REQUIRE(scene.Registry().get<Luma::LightComponent>(copy).intensity == 7.5f);

    // A variant adds the component and applies its preset.
    Entity v2 = scene.CreateEntity("Point");
    const auto& variants = light->variants;
    auto it = std::find_if(variants.begin(), variants.end(),
                           [](const auto& x) { return x.displayName == "Point Light"; });
    REQUIRE(it != variants.end());
    it->apply(scene, v2);
    REQUIRE(scene.Registry().get<Luma::LightComponent>(v2).type == Luma::LightType::Point);
}

TEST_CASE("Reset restores defaults; Light keeps its type", "[scene][components]") {
    Scene scene;
    Entity e = scene.CreateEntity("Lamp");
    const ComponentDesc* light = Luma::Components().Find("Light");
    light->variants[1].apply(scene, e);  // Point Light
    auto& lc = scene.Registry().get<Luma::LightComponent>(e);
    lc.intensity = 42.0f;
    REQUIRE(light->reset);
    light->reset(scene, e);
    const auto& after = scene.Registry().get<Luma::LightComponent>(e);
    REQUIRE(after.type == Luma::LightType::Point);
    REQUIRE(after.intensity == Luma::LightComponent{}.intensity);

    const ComponentDesc* cam = Luma::Components().Find("Camera");
    cam->add(scene, e);
    scene.Registry().get<Luma::CameraComponent>(e).fovYDegrees = 12.0f;
    cam->reset(scene, e);
    REQUIRE(scene.Registry().get<Luma::CameraComponent>(e).fovYDegrees ==
            Luma::CameraComponent{}.fovYDegrees);
}
