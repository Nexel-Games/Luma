#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "Luma/Plugins/ModuleManager.h"
#include "Luma/Plugins/PluginManager.h"
#include "Luma/Sample/SampleSpinnerComponent.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Scene/SceneSystems.h"

namespace fs = std::filesystem;
using Luma::Entity;
using Luma::Scene;
using Luma::SceneRunner;
using Luma::SceneSerializer;
using Luma::UnresolvedComponents;
using Luma::Plugins::CompiledPluginTable;
using Luma::Plugins::ModuleManager;
using Luma::Plugins::PluginHostKind;
using Luma::Plugins::PluginManager;
using Luma::Plugins::PluginState;
using Luma::Plugins::PluginTarget;

namespace {

fs::path EnginePluginsRoot() { return fs::path(LUMA_SOURCE_DIR) / "Plugins"; }

Entity FindByName(Scene& scene, const std::string& name) {
    for (auto e : scene.Registry().view<Luma::NameComponent>()) {
        if (scene.Registry().get<Luma::NameComponent>(e).name == name) return e;
    }
    return Luma::kNullEntity;
}

struct SampleRuntimeSession {
    PluginManager mgr;
    ModuleManager modules;
    SampleRuntimeSession() : mgr(CompiledPluginTable::Global()) {
        mgr.SetRegistries(&Luma::Components(), &Luma::SceneSystems());
        Luma::Plugins::PluginHostServices host;
        host.components = &Luma::Components();
        host.sceneSystems = &Luma::SceneSystems();
        mgr.SetHostServices(host);
        const fs::path roots[] = {EnginePluginsRoot()};
        mgr.Discover(roots);
        mgr.RestrictEnabled(std::vector<std::string>{"SampleSpinner"});
        REQUIRE(mgr.StartAll(PluginHostKind::Runtime));
        modules.SetHostServices(host);
        modules.SetTarget(PluginTarget::Game);
        modules.SetActivePlugins(mgr.LoadableDescriptors());
        modules.LoadAllPhases();
        mgr.ApplyModuleStatuses(modules);
        REQUIRE(mgr.Find("SampleSpinner")->state == PluginState::Started);
    }
    ~SampleRuntimeSession() {
        modules.ShutdownAll();
        mgr.ShutdownAll();
    }
};

}  // namespace

TEST_CASE("SampleSpinner registers exactly its component and system", "[plugins][sample]") {
    const Luma::usize componentsBefore = Luma::Components().Size();
    const Luma::usize systemsBefore = Luma::SceneSystems().Size();
    {
        SampleRuntimeSession session;
        REQUIRE(Luma::Components().Size() == componentsBefore + 1);
        REQUIRE(Luma::SceneSystems().Size() == systemsBefore + 1);
        const auto* spinner = Luma::Components().Find("SampleSpinner");
        REQUIRE(spinner);
        REQUIRE(spinner->owner == "SampleSpinner");
        REQUIRE(spinner->displayName == "Spinner");
        REQUIRE(Luma::SceneSystems().Contains("SampleSpinner.Spinner"));
        REQUIRE(session.modules.IsLoaded("SampleSpinner"));
        REQUIRE_FALSE(session.modules.IsLoaded("SampleSpinnerEditor"));
    }
    REQUIRE(Luma::Components().Find("SampleSpinner") == nullptr);
    REQUIRE_FALSE(Luma::SceneSystems().Contains("SampleSpinner.Spinner"));
}

TEST_CASE("SampleSpinner serializes and the system rotates during Play", "[plugins][sample]") {
    SampleRuntimeSession session;
    Scene scene;
    Entity actor = scene.CreateEntity("SpinMe");
    auto& spinner = scene.Registry().emplace<Luma::SampleSpinnerComponent>(actor);
    spinner.degreesPerSecond = 90.0f;
    spinner.axis = {0.0f, 1.0f, 0.0f};

    const std::string saved = SceneSerializer::SaveToString(scene, true);
    REQUIRE(saved.find("SampleSpinner") != std::string::npos);
    Scene loaded;
    REQUIRE(SceneSerializer::LoadFromString(loaded, saved));
    Entity again = FindByName(loaded, "SpinMe");
    REQUIRE(again != Luma::kNullEntity);
    REQUIRE(loaded.Registry().all_of<Luma::SampleSpinnerComponent>(again));
    REQUIRE(loaded.Registry().get<Luma::SampleSpinnerComponent>(again).degreesPerSecond == 90.0f);

    SceneRunner runner;
    REQUIRE(runner.Begin(loaded));
    Scene* play = runner.PlayScene();
    REQUIRE(play);
    Entity playing = FindByName(*play, "SpinMe");
    const Luma::f32 yaw0 = play->Registry().get<Luma::TransformComponent>(playing).rotationEuler.y;
    runner.Tick(1.0);
    const Luma::f32 yaw1 = play->Registry().get<Luma::TransformComponent>(playing).rotationEuler.y;
    REQUIRE(yaw1 == Catch::Approx(yaw0 + 90.0f));
    runner.End();
}

TEST_CASE("Disabling SampleSpinner keeps the spinner payload and re-enable restores it",
          "[plugins][sample]") {
    std::string withPlugin;
    {
        SampleRuntimeSession session;
        Scene scene;
        Entity actor = scene.CreateEntity("Kept");
        auto& spinner = scene.Registry().emplace<Luma::SampleSpinnerComponent>(actor);
        spinner.degreesPerSecond = 12.5f;
        spinner.axis = {1.0f, 0.0f, 0.0f};
        withPlugin = SceneSerializer::SaveToString(scene, true);
        REQUIRE(withPlugin.find("SampleSpinner") != std::string::npos);
    }
    REQUIRE(Luma::Components().Find("SampleSpinner") == nullptr);

    Scene missing;
    REQUIRE(SceneSerializer::LoadFromString(missing, withPlugin));
    Entity kept = FindByName(missing, "Kept");
    REQUIRE(kept != Luma::kNullEntity);
    REQUIRE_FALSE(missing.Registry().all_of<Luma::SampleSpinnerComponent>(kept));
    const auto* unresolved = missing.Registry().try_get<UnresolvedComponents>(kept);
    REQUIRE(unresolved);
    REQUIRE(unresolved->Find("SampleSpinner"));
    REQUIRE(unresolved->Find("SampleSpinner")->owner == "SampleSpinner");
    const std::string stillKept = SceneSerializer::SaveToString(missing, true);
    REQUIRE(stillKept.find("\"SampleSpinner\"") != std::string::npos);
    REQUIRE(stillKept.find("12.5") != std::string::npos);

    SampleRuntimeSession session;
    REQUIRE(Luma::ResolveUnresolvedComponents(missing) == 1);
    REQUIRE(missing.Registry().all_of<Luma::SampleSpinnerComponent>(kept));
    REQUIRE(missing.Registry().get<Luma::SampleSpinnerComponent>(kept).degreesPerSecond ==
            Catch::Approx(12.5f));
}

TEST_CASE("SampleSpinner Editor module is compiled into tests and skipped on Game",
          "[plugins][sample]") {
    REQUIRE(Luma::Plugins::StaticModuleBackend::Global().IsAvailable("SampleSpinner"));
    REQUIRE(Luma::Plugins::StaticModuleBackend::Global().IsAvailable("SampleSpinnerEditor"));
    SampleRuntimeSession session;
    REQUIRE(session.modules.IsLoaded("SampleSpinner"));
    REQUIRE_FALSE(session.modules.IsLoaded("SampleSpinnerEditor"));
}
