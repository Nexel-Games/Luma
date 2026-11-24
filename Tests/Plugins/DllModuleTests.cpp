#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Fixtures/DllProbeComponent.h"
#include "Luma/Plugins/Module.h"
#include "Luma/Plugins/ModuleManager.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Components.h"
#include "Luma/Scene/Scene.h"

namespace fs = std::filesystem;
using Luma::DllProbeComponent;
using Luma::Entity;
using Luma::Scene;
using Luma::Plugins::DllModuleBackend;
using Luma::Plugins::IModuleInterface;
using Luma::Plugins::kModuleApiVersion;
using Luma::Plugins::ModuleContext;
using Luma::Plugins::ModuleManager;
using Luma::Plugins::PluginDescriptor;
using Luma::Plugins::PluginHostServices;
using Luma::Plugins::PluginTarget;

namespace {

fs::path FixtureRoot() { return fs::path(LUMA_DLL_FIXTURE_ROOT); }

PluginDescriptor ProbePlugin(const char* moduleName) {
    PluginDescriptor d;
    d.name = "DllProbe";
    d.friendlyName = "DLL Probe";
    Luma::Plugins::ModuleDescriptor m;
    m.name = moduleName;
    m.loadingPhase = Luma::Plugins::LoadingPhase::Default;
    d.modules.push_back(m);
    return d;
}

}  // namespace

TEST_CASE("DllModuleBackend loads a plugin DLL and unloads it", "[plugins][dll]") {
    DllModuleBackend backend;
    const fs::path roots[] = {FixtureRoot()};
    backend.SetSearchRoots(roots);
    REQUIRE(backend.IsAvailable("DllProbe"));
    std::string error;
    auto module = backend.Create("DllProbe", &error);
    REQUIRE(module);
    REQUIRE(error.empty());
    PluginHostServices host;
    host.components = &Luma::Components();
    host.sceneSystems = &Luma::SceneSystems();
    PluginDescriptor plugin = ProbePlugin("DllProbe");
    ModuleContext ctx("DllProbe", &plugin, host);
    const Luma::usize before = Luma::Components().Size();
    module->StartupModule(ctx);
    REQUIRE_FALSE(ctx.Failed());
    REQUIRE(Luma::Components().Size() == before + 1);
    module->ShutdownModule(ctx);
    ctx.ReleaseAll();
    module.reset();
    backend.Unload("DllProbe");
    REQUIRE(Luma::Components().Size() == before);
}

TEST_CASE("DllModuleBackend rejects an API version mismatch", "[plugins][dll]") {
    DllModuleBackend backend;
    const fs::path roots[] = {FixtureRoot()};
    backend.SetSearchRoots(roots);
    REQUIRE(backend.IsAvailable("DllProbeBadApi"));
    std::string error;
    auto module = backend.Create("DllProbeBadApi", &error);
    REQUIRE_FALSE(module);
    REQUIRE(error.find("Rebuild the plugin") != std::string::npos);
    REQUIRE(error.find(std::to_string(kModuleApiVersion)) != std::string::npos);
}

TEST_CASE("A component defined in a plugin DLL is visible to the engine registry",
          "[plugins][dll][entt]") {
    DllModuleBackend backend;
    ModuleManager mgr(backend);
    const fs::path roots[] = {FixtureRoot()};
    backend.SetSearchRoots(roots);
    PluginHostServices host;
    host.components = &Luma::Components();
    host.sceneSystems = &Luma::SceneSystems();
    mgr.SetHostServices(host);
    mgr.SetTarget(PluginTarget::Editor);
    const PluginDescriptor plugin = ProbePlugin("DllProbe");
    mgr.SetActivePlugins(std::span<const PluginDescriptor>(&plugin, 1));
    mgr.LoadAllPhases();
    REQUIRE(mgr.IsLoaded("DllProbe"));

    {
        Scene scene;
        Entity e = scene.CreateEntity("Probe");
        const auto* desc = Luma::Components().Find("DllProbe");
        REQUIRE(desc);
        desc->add(scene, e);
        REQUIRE(desc->has(scene, e));
        REQUIRE(scene.Registry().all_of<DllProbeComponent>(e));
        REQUIRE(scene.Registry().get<DllProbeComponent>(e).value == 7);
    }

    mgr.ShutdownAll();
    REQUIRE_FALSE(Luma::Components().Find("DllProbe"));
}

#if defined(LUMA_MODULAR)
TEST_CASE("SampleSpinner editor modules ship as LumaEditor-*.dll", "[plugins][dll]") {
    const fs::path bin = fs::path(LUMA_SOURCE_DIR) / "Plugins" / "SampleSpinner" / "Binaries" /
                         "Win64" / DllModuleBackend::HostConfigName();
    REQUIRE(fs::exists(bin / "LumaEditor-SampleSpinner.dll"));
    REQUIRE(fs::exists(bin / "LumaEditor-SampleSpinnerEditor.dll"));
    DllModuleBackend& dll = DllModuleBackend::Global();
    const fs::path roots[] = {fs::path(LUMA_SOURCE_DIR) / "Plugins"};
    dll.SetSearchRoots(roots);
    REQUIRE(dll.IsAvailable("SampleSpinner"));
    REQUIRE(dll.IsAvailable("SampleSpinnerEditor"));
}
#endif
