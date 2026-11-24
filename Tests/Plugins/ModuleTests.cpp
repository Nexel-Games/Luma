#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Luma/Core/Version.h"
#include "Luma/Plugins/ModuleManager.h"
#include "Luma/Plugins/PluginDescriptor.h"
#include "Luma/Plugins/PluginManifest.h"
#include <map>

#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SceneSystems.h"

using Luma::EngineVersion;
using Luma::Plugins::HostPlatform;
using Luma::Plugins::IModuleInterface;
using Luma::Plugins::LoadingPhase;
using Luma::Plugins::ModuleContext;
using Luma::Plugins::ModuleDescriptor;
using Luma::Plugins::ModuleManager;
using Luma::Plugins::ModuleRunState;
using Luma::Plugins::ModuleType;
using Luma::Plugins::PluginDescriptor;
using Luma::Plugins::PluginHostServices;
using Luma::Plugins::PluginReference;
using Luma::Plugins::PluginTarget;
using Luma::Plugins::StaticModuleBackend;
using Luma::SemVer;

namespace {

PluginDescriptor MakePlugin(const char* name) {
    PluginDescriptor d;
    d.name = name;
    d.friendlyName = name;
    d.versionName = *SemVer::Parse("1.0.0");
    d.engineVersionRange = ">=" + EngineVersion().ToString() + " <99.0.0";
    d.supportedTargetPlatforms = {std::string(HostPlatform())};
    return d;
}

ModuleDescriptor MakeMod(const char* name, LoadingPhase phase,
                         ModuleType type = ModuleType::Runtime) {
    ModuleDescriptor m;
    m.name = name;
    m.loadingPhase = phase;
    m.type = type;
    return m;
}

struct Recorder : IModuleInterface {
    static std::vector<std::string> starts;
    static std::vector<std::string> stops;
    std::string name;
    bool fail = false;
    bool seePeer = false;
    std::string peer;
    explicit Recorder(std::string n, bool shouldFail = false, std::string other = {})
        : name(std::move(n)), fail(shouldFail), peer(std::move(other)) {}
    void StartupModule(ModuleContext& ctx) override {
        starts.push_back(name);
        if (!peer.empty()) seePeer = ModuleManagerPeekLoaded(peer);
        if (fail) ctx.Fail(name + " asked to fail");
    }
    void ShutdownModule(ModuleContext&) override { stops.push_back(name); }

    static bool ModuleManagerPeekLoaded(const std::string&) { return false; }
};

std::vector<std::string> Recorder::starts;
std::vector<std::string> Recorder::stops;

class RecordingBackend final : public Luma::Plugins::IModuleBackend {
public:
    std::map<std::string, StaticModuleBackend::Factory, std::less<>> factories;
    bool IsAvailable(std::string_view name) const override {
        return factories.find(name) != factories.end();
    }
    std::unique_ptr<IModuleInterface> Create(std::string_view name, std::string* error) override {
        auto it = factories.find(name);
        if (it == factories.end()) {
            if (error) *error = "missing";
            return nullptr;
        }
        return it->second();
    }
};

struct Session {
    RecordingBackend backend;
    ModuleManager mgr;
    PluginHostServices host;
    Session() : mgr(backend) {
        host.components = &Luma::Components();
        host.sceneSystems = &Luma::SceneSystems();
        mgr.SetHostServices(host);
        mgr.SetTarget(PluginTarget::Editor);
        Recorder::starts.clear();
        Recorder::stops.clear();
    }
};

}  // namespace

TEST_CASE("ModuleManager loads phases in order and shuts down in reverse",
          "[plugins][modules]") {
    Session s;
    s.backend.factories["Early"] = [] { return std::make_unique<Recorder>("Early"); };
    s.backend.factories["Default"] = [] { return std::make_unique<Recorder>("Default"); };
    s.backend.factories["Late"] = [] { return std::make_unique<Recorder>("Late"); };
    auto plugin = MakePlugin("P");
    plugin.modules = {MakeMod("Early", LoadingPhase::EarliestPossible),
                      MakeMod("Default", LoadingPhase::Default),
                      MakeMod("Late", LoadingPhase::PostEngineInit)};
    s.mgr.SetActivePlugins(std::vector<PluginDescriptor>{plugin});
    s.mgr.LoadAllPhases();
    REQUIRE(s.mgr.LoadOrder() == std::vector<std::string>{"Early", "Default", "Late"});
    REQUIRE(Recorder::starts == s.mgr.LoadOrder());
    REQUIRE(s.mgr.IsLoaded("Default"));
    s.mgr.ShutdownAll();
    REQUIRE(s.mgr.ShutdownOrder() == std::vector<std::string>{"Late", "Default", "Early"});
    REQUIRE(Recorder::stops == s.mgr.ShutdownOrder());
    REQUIRE_FALSE(s.mgr.IsLoaded("Default"));
}

TEST_CASE("ModuleManager rolls back a failed plugin and its dependents", "[plugins][modules]") {
    const Luma::usize componentsBefore = Luma::Components().Size();
    Session s;
    s.backend.factories["A1"] = [] { return std::make_unique<Recorder>("A1"); };
    s.backend.factories["A2"] = [] { return std::make_unique<Recorder>("A2", true); };
    s.backend.factories["B1"] = [] { return std::make_unique<Recorder>("B1"); };
    s.backend.factories["C1"] = [] { return std::make_unique<Recorder>("C1"); };

    auto a = MakePlugin("A");
    a.modules = {MakeMod("A1", LoadingPhase::PreDefault), MakeMod("A2", LoadingPhase::Default)};
    auto b = MakePlugin("B");
    PluginReference ref;
    ref.name = "A";
    ref.versionRange = ">=1.0.0";
    b.plugins.push_back(ref);
    b.modules = {MakeMod("B1", LoadingPhase::PreDefault)};
    auto c = MakePlugin("C");
    c.modules = {MakeMod("C1", LoadingPhase::PreDefault)};

    s.mgr.SetActivePlugins(std::vector<PluginDescriptor>{a, b, c});
    s.mgr.LoadPhase(LoadingPhase::PreDefault);
    REQUIRE(s.mgr.IsLoaded("A1"));
    REQUIRE(s.mgr.IsLoaded("B1"));
    REQUIRE(s.mgr.IsLoaded("C1"));

    s.mgr.LoadPhase(LoadingPhase::Default);
    REQUIRE_FALSE(s.mgr.IsLoaded("A1"));
    REQUIRE_FALSE(s.mgr.IsLoaded("A2"));
    REQUIRE_FALSE(s.mgr.IsLoaded("B1"));
    REQUIRE(s.mgr.IsLoaded("C1"));
    REQUIRE(Luma::Components().Size() == componentsBefore);
    bool aFailed = false;
    for (const auto& st : s.mgr.Statuses()) {
        if (st.name == "A2") {
            REQUIRE(st.state == ModuleRunState::Failed);
            aFailed = true;
        }
    }
    REQUIRE(aFailed);
}

TEST_CASE("ModuleManager restores registries after shutdown", "[plugins][modules]") {
    const Luma::usize systemsBefore = Luma::SceneSystems().Size();
    {
        Session s;
        s.backend.factories["Reg"] = [] {
            class TempSystem final : public Luma::ISceneSystem {
            public:
                std::string_view Id() const override { return "ModuleTest.Temp"; }
                void Update(Luma::Scene&, Luma::f64) override {}
            };
            class RegMod final : public IModuleInterface {
            public:
                void StartupModule(ModuleContext& ctx) override {
                    std::string error;
                    auto token =
                        ctx.SceneSystems().Register(std::make_unique<TempSystem>(), 50, &error);
                    if (!token) {
                        ctx.Fail(error);
                        return;
                    }
                    ctx.Track(std::move(token));
                }
            };
            return std::make_unique<RegMod>();
        };
        auto plugin = MakePlugin("P");
        plugin.modules = {MakeMod("Reg", LoadingPhase::Default)};
        s.mgr.SetActivePlugins(std::vector<PluginDescriptor>{plugin});
        s.mgr.LoadAllPhases();
        REQUIRE(Luma::SceneSystems().Contains("ModuleTest.Temp"));
        s.mgr.ShutdownAll();
    }
    REQUIRE_FALSE(Luma::SceneSystems().Contains("ModuleTest.Temp"));
    REQUIRE(Luma::SceneSystems().Size() == systemsBefore);
}

TEST_CASE("A module can GetChecked a dependency loaded in the same phase", "[plugins][modules]") {
    Session s;
    class Provider final : public IModuleInterface {
    public:
        void StartupModule(ModuleContext&) override {}
    };
    class Consumer final : public IModuleInterface {
    public:
        ModuleManager* mgr = nullptr;
        bool saw = false;
        void StartupModule(ModuleContext&) override {
            saw = mgr && mgr->IsLoaded("Provider");
            mgr->GetChecked<Provider>("Provider");
        }
    };
    auto* consumer = new Consumer();
    s.backend.factories["Provider"] = [] { return std::make_unique<Provider>(); };
    s.backend.factories["Consumer"] = [consumer]() -> std::unique_ptr<IModuleInterface> {
        return std::unique_ptr<IModuleInterface>(consumer);
    };
    auto a = MakePlugin("A");
    a.modules = {MakeMod("Provider", LoadingPhase::Default)};
    auto b = MakePlugin("B");
    PluginReference ref;
    ref.name = "A";
    ref.versionRange = ">=1.0.0";
    b.plugins.push_back(ref);
    b.modules = {MakeMod("Consumer", LoadingPhase::Default)};
    s.mgr.SetActivePlugins(std::vector<PluginDescriptor>{a, b});
    consumer->mgr = &s.mgr;
    s.mgr.LoadPhase(LoadingPhase::Default);
    REQUIRE(s.mgr.IsLoaded("Provider"));
    REQUIRE(s.mgr.IsLoaded("Consumer"));
    REQUIRE(consumer->saw);
}

TEST_CASE("Editor modules are skipped on the Game target", "[plugins][modules]") {
    Session s;
    s.mgr.SetTarget(PluginTarget::Game);
    s.backend.factories["Run"] = [] { return std::make_unique<Recorder>("Run"); };
    s.backend.factories["Ed"] = [] { return std::make_unique<Recorder>("Ed"); };
    auto plugin = MakePlugin("P");
    plugin.modules = {MakeMod("Run", LoadingPhase::Default, ModuleType::Runtime),
                      MakeMod("Ed", LoadingPhase::PostEngineInit, ModuleType::Editor)};
    s.mgr.SetActivePlugins(std::vector<PluginDescriptor>{plugin});
    s.mgr.LoadAllPhases();
    REQUIRE(s.mgr.IsLoaded("Run"));
    REQUIRE_FALSE(s.mgr.IsLoaded("Ed"));
    bool skipped = false;
    for (const auto& st : s.mgr.Statuses()) {
        if (st.name == "Ed") {
            REQUIRE(st.state == ModuleRunState::Skipped);
            skipped = true;
        }
    }
    REQUIRE(skipped);
}
