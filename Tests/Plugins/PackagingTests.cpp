#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Luma/Plugins/Module.h"
#include "Luma/Plugins/ModuleManager.h"
#include "Luma/Plugins/PluginDescriptor.h"
#include "Luma/Plugins/PluginDiscovery.h"
#include "Luma/Plugins/PluginManager.h"
#include "Luma/Plugins/PluginManifest.h"
#include "Luma/Plugins/PluginPackaging.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/SceneSystems.h"

namespace fs = std::filesystem;
using Luma::Plugins::DllModuleBackend;
using Luma::Plugins::GamePackagePlan;
using Luma::Plugins::ModuleManager;
using Luma::Plugins::PackagingItem;
using Luma::Plugins::PackagingRole;
using Luma::Plugins::ParseDescriptor;
using Luma::Plugins::PlanGamePackage;
using Luma::Plugins::PluginDescriptor;
using Luma::Plugins::PluginHostServices;
using Luma::Plugins::PluginTarget;

namespace {

const PackagingItem* FindItem(const GamePackagePlan& plan, PackagingRole role) {
    for (const PackagingItem& item : plan.items) {
        if (item.role == role) return &item;
    }
    return nullptr;
}

PluginDescriptor LoadSampleSpinner() {
    const fs::path file = fs::path(LUMA_SOURCE_DIR) / "Plugins" / "SampleSpinner" /
                          "SampleSpinner.lplugin";
    std::ifstream in(file, std::ios::binary);
    std::string json((std::istreambuf_iterator<char>(in)), {});
    std::vector<Luma::Plugins::ManifestError> errors;
    auto desc = ParseDescriptor(json, file, errors);
    REQUIRE(desc);
    REQUIRE(errors.empty());
    return *desc;
}

}  // namespace

TEST_CASE("PlanGamePackage is blocked and splits Runtime from Editor and Source",
          "[plugins][packaging]") {
    const PluginDescriptor spinner = LoadSampleSpinner();
    const GamePackagePlan plan = PlanGamePackage(std::span<const PluginDescriptor>(&spinner, 1));
    REQUIRE(plan.blocked);
    REQUIRE(plan.blockedReason.find("no packaging pipeline") != std::string::npos);

    const PackagingItem* runtime = nullptr;
    const PackagingItem* editor = nullptr;
    for (const PackagingItem& item : plan.items) {
        if (item.role == PackagingRole::RuntimeModule) runtime = &item;
        if (item.role == PackagingRole::EditorModule) editor = &item;
    }
    REQUIRE(runtime);
    REQUIRE(runtime->inPackagedGame);
    REQUIRE(runtime->relative == fs::path("Source") / "SampleSpinner");
    REQUIRE(editor);
    REQUIRE_FALSE(editor->inPackagedGame);

    const PackagingItem* content = FindItem(plan, PackagingRole::Content);
    const PackagingItem* config = FindItem(plan, PackagingRole::Config);
    const PackagingItem* source = FindItem(plan, PackagingRole::Source);
    REQUIRE(content);
    REQUIRE(content->inPackagedGame);
    REQUIRE(config);
    REQUIRE(config->inPackagedGame);
    REQUIRE(source);
    REQUIRE_FALSE(source->inPackagedGame);
}

TEST_CASE("LumaEngine does not static-link Editor plugin modules", "[plugins][packaging][sandbox]") {
    std::ifstream in(LUMA_ENGINE_LINK_DUMP);
    REQUIRE(in);
    std::string dump((std::istreambuf_iterator<char>(in)), {});
    REQUIRE(dump.find("SampleSpinner") != std::string::npos);
    REQUIRE(dump.find("SampleSpinnerEditor") == std::string::npos);
}

TEST_CASE("SampleSpinner start/stop ten times leaves registries unchanged",
          "[plugins][stability]") {
    const Luma::usize components = Luma::Components().Size();
    const Luma::usize systems = Luma::SceneSystems().Size();
    for (int i = 0; i < 10; ++i) {
        Luma::Plugins::PluginManager mgr(Luma::Plugins::CompiledPluginTable::Global());
        Luma::Plugins::ModuleManager modules;
        mgr.SetRegistries(&Luma::Components(), &Luma::SceneSystems());
        PluginHostServices host;
        host.components = &Luma::Components();
        host.sceneSystems = &Luma::SceneSystems();
        mgr.SetHostServices(host);
        const fs::path roots[] = {fs::path(LUMA_SOURCE_DIR) / "Plugins"};
        mgr.Discover(roots);
        mgr.RestrictEnabled(std::vector<std::string>{"SampleSpinner"});
        REQUIRE(mgr.StartAll(Luma::Plugins::PluginHostKind::Runtime));
        modules.SetHostServices(host);
        modules.SetTarget(PluginTarget::Game);
        modules.SetActivePlugins(mgr.LoadableDescriptors());
        modules.LoadAllPhases();
        REQUIRE(modules.IsLoaded("SampleSpinner"));
        REQUIRE_FALSE(modules.IsLoaded("SampleSpinnerEditor"));
        modules.ShutdownAll();
        mgr.ShutdownAll();
        REQUIRE(Luma::Components().Size() == components);
        REQUIRE(Luma::SceneSystems().Size() == systems);
    }
}

#if defined(LUMA_DLL_FIXTURE_ROOT)
TEST_CASE("DllModuleBackend reports a missing LumaModuleApiVersion export", "[plugins][dll]") {
    DllModuleBackend backend;
    const fs::path roots[] = {fs::path(LUMA_DLL_FIXTURE_ROOT)};
    backend.SetSearchRoots(roots);
    REQUIRE(backend.IsAvailable("DllProbeNoExport"));
    std::string error;
    auto module = backend.Create("DllProbeNoExport", &error);
    REQUIRE_FALSE(module);
    REQUIRE(error.find("LumaModuleApiVersion") != std::string::npos);
}

TEST_CASE("DllProbe load/unload ten times does not grow the component registry",
          "[plugins][dll][stability]") {
    DllModuleBackend backend;
    const fs::path roots[] = {fs::path(LUMA_DLL_FIXTURE_ROOT)};
    backend.SetSearchRoots(roots);
    PluginHostServices host;
    host.components = &Luma::Components();
    host.sceneSystems = &Luma::SceneSystems();
    const Luma::usize before = Luma::Components().Size();
    for (int i = 0; i < 10; ++i) {
        ModuleManager mgr(backend);
        mgr.SetHostServices(host);
        mgr.SetTarget(PluginTarget::Editor);
        PluginDescriptor plugin;
        plugin.name = "DllProbe";
        Luma::Plugins::ModuleDescriptor mod;
        mod.name = "DllProbe";
        plugin.modules.push_back(mod);
        mgr.SetActivePlugins(std::span<const PluginDescriptor>(&plugin, 1));
        mgr.LoadAllPhases();
        REQUIRE(mgr.IsLoaded("DllProbe"));
        REQUIRE(Luma::Components().Find("DllProbe"));
        mgr.ShutdownAll();
        REQUIRE_FALSE(Luma::Components().Find("DllProbe"));
        REQUIRE(Luma::Components().Size() == before);
    }
}
#endif
