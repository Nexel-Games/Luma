#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include "Luma/Scene/SceneSystems.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Scene/Hierarchy.h"
#include "Luma/Scene/ComponentRegistry.h"

using namespace Luma;

namespace {
struct Counter final : ISceneSystem {
    Counter(std::string id, std::vector<std::string>& log) : id(std::move(id)), log(log) {}
    std::string_view Id() const override { return id; }
    void OnPlayBegin(Scene&) override { log.push_back("begin " + id); }
    void FixedUpdate(Scene& scene, f64) override { ++ticks; scene.CreateEntity("spawned"); }
    void Update(Scene&, f64) override { ++updates; }
    void OnPlayEnd(Scene&) override { log.push_back("end " + id); }
    std::string id;
    std::vector<std::string>& log;
    int ticks = 0, updates = 0;
};
}

TEST_CASE("Play owns its scene and runs sorted systems with bounded fixed steps", "[scene][play]") {
    Scene scene;
    // Leave a hole so a sequentially-created copy would assign different ids.
    const Entity unused = scene.CreateEntity("unused");
    auto parent = scene.CreateEntity("parent");
    SetParent(scene, scene.CreateEntity("child"), parent);
    scene.DestroyEntity(unused);
    auto& unresolved = scene.Registry().emplace<UnresolvedComponents>(parent);
    unresolved.payloads.push_back({"Curve", "Test.Curves", SerialValue(17)});
    unresolved.keyOrder = {"Name", "Transform", "Curve"};
    const auto before = SceneSerializer::SaveToString(scene);
    SceneSystemRegistry registry;
    std::vector<std::string> events;
    RegistrationSet registrations;
    registrations.Add(registry.Register(std::make_unique<Counter>("C", events), 1));
    registrations.Add(registry.Register(std::make_unique<Counter>("B", events), 0));
    auto a = std::make_unique<Counter>("A", events);
    auto* counter = a.get();
    registrations.Add(registry.Register(std::move(a), 0));
    std::string error;
    CHECK_FALSE(registry.Register(std::make_unique<Counter>("A", events), 99, &error));
    CHECK_FALSE(error.empty());
    SceneRunner runner(registry);
    REQUIRE(runner.Begin(scene, {0.125, 3}, &error));
    CHECK(SceneSerializer::SaveToString(*runner.PlayScene()) == before);
    REQUIRE(runner.PlayScene()->IsValid(parent));
    CHECK(runner.PlayScene()->Registry().get<NameComponent>(parent).name == "parent");
    REQUIRE(runner.PlayScene()->Registry().get<UnresolvedComponents>(parent).Find("Curve"));
    CHECK(events == std::vector<std::string>{"begin A", "begin B", "begin C"});
    runner.Tick(0.375);
    CHECK(counter->ticks == 3);
    runner.Tick(2.0);
    CHECK(counter->ticks == 6);
    runner.Tick(0.0625);
    CHECK(counter->ticks == 6);
    runner.SetPaused(true);
    runner.Tick(1);
    CHECK(counter->updates == 3);
    runner.Step();
    CHECK(counter->ticks == 7);
    CHECK(counter->updates == 4);
    REQUIRE(runner.PlayScene()->EntityCount() > scene.EntityCount());
    runner.End();
    CHECK(events == std::vector<std::string>{"begin A","begin B","begin C","end C","end B","end A"});
    CHECK(SceneSerializer::SaveToString(scene) == before);
    CHECK_FALSE(runner.PlayScene());
}

TEST_CASE("Removing a playing system ends it once before its token returns", "[scene][play]") {
    std::vector<std::string> events;
    SceneSystemRegistry registry;
    Scene scene;
    auto token = registry.Register(std::make_unique<Counter>("Test", events), 0);
    SceneRunner runner(registry);
    REQUIRE(runner.Begin(scene));
    token.Reset();
    runner.Tick(1.0);
    runner.End();
    CHECK(events == std::vector<std::string>{"begin Test", "end Test"});
}

TEST_CASE("Failed play startup unwinds initialized systems", "[scene][play]") {
    struct Failing final : ISceneSystem {
        std::string_view Id() const override { return "Failing"; }
        void OnPlayBegin(Scene&) override { throw std::runtime_error("setup failed"); }
    };
    std::vector<std::string> events;
    Scene scene;
    SceneSystemRegistry registry;
    auto first = registry.Register(std::make_unique<Counter>("First", events), 0);
    auto failure = registry.Register(std::make_unique<Failing>(), 1);
    auto last = registry.Register(std::make_unique<Counter>("Last", events), 2);
    SceneRunner runner(registry);
    std::string error;
    CHECK_FALSE(runner.Begin(scene, {}, &error));
    CHECK(error.find("Failing") != std::string::npos);
    CHECK_FALSE(runner.PlayScene());
    CHECK(events == std::vector<std::string>{"begin First", "end First"});
}
