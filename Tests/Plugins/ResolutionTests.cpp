#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Luma/Core/Version.h"
#include "Luma/Plugins/PluginDescriptor.h"
#include "Luma/Plugins/PluginManifest.h"

using Luma::EngineVersion;
using Luma::Plugins::HostPlatform;
using Luma::Plugins::ModuleDescriptor;
using Luma::Plugins::ModuleLoadsOn;
using Luma::Plugins::ModuleType;
using Luma::Plugins::PluginDescriptor;
using Luma::Plugins::PluginReference;
using Luma::Plugins::PluginResolveState;
using Luma::Plugins::PluginTarget;
using Luma::Plugins::ProjectPluginEntry;
using Luma::Plugins::ResolveConfig;
using Luma::Plugins::ResolvedPlugin;
using Luma::Plugins::ResolvePlugins;
using Luma::SemVer;

namespace {

PluginDescriptor Make(const char* name, const char* version = "1.0.0") {
    PluginDescriptor d;
    d.name = name;
    d.friendlyName = name;
    d.versionName = *SemVer::Parse(version);
    d.engineVersionRange = ">=" + EngineVersion().ToString() + " <99.0.0";
    d.supportedTargetPlatforms = {std::string(HostPlatform())};
    d.enabledByDefault = false;
    ModuleDescriptor mod;
    mod.name = std::string(name) + "Runtime";
    d.modules.push_back(std::move(mod));
    return d;
}

void RequireDep(PluginDescriptor& d, const char* other, const char* range = ">=1.0.0",
                bool optional = false) {
    PluginReference ref;
    ref.name = other;
    ref.versionRange = range;
    ref.enabled = true;
    ref.optional = optional;
    d.plugins.push_back(std::move(ref));
}

const ResolvedPlugin* Find(const std::vector<ResolvedPlugin>& list, std::string_view name) {
    for (const ResolvedPlugin& r : list) {
        if (r.descriptor.name == name) return &r;
    }
    return nullptr;
}

std::vector<std::string> ActiveOrder(const std::vector<ResolvedPlugin>& list) {
    std::vector<std::string> names;
    for (const ResolvedPlugin& r : list) {
        if (r.state == PluginResolveState::Active) names.push_back(r.descriptor.name);
    }
    return names;
}

bool HasDiag(const ResolvedPlugin& r, std::string_view needle) {
    for (const std::string& d : r.diagnostics) {
        if (d.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("ModuleLoadsOn matches the architecture type table", "[plugins][resolution]") {
    REQUIRE(ModuleLoadsOn(ModuleType::Runtime, PluginTarget::Editor, false));
    REQUIRE(ModuleLoadsOn(ModuleType::Runtime, PluginTarget::Game, true));
    REQUIRE(ModuleLoadsOn(ModuleType::Developer, PluginTarget::Editor, false));
    REQUIRE(ModuleLoadsOn(ModuleType::Developer, PluginTarget::Game, false));
    REQUIRE_FALSE(ModuleLoadsOn(ModuleType::Developer, PluginTarget::Game, true));
    REQUIRE(ModuleLoadsOn(ModuleType::Editor, PluginTarget::Editor, false));
    REQUIRE_FALSE(ModuleLoadsOn(ModuleType::Editor, PluginTarget::Game, false));
    REQUIRE(ModuleLoadsOn(ModuleType::UncookedOnly, PluginTarget::Editor, false));
    REQUIRE(ModuleLoadsOn(ModuleType::UncookedOnly, PluginTarget::Program, false));
    REQUIRE_FALSE(ModuleLoadsOn(ModuleType::UncookedOnly, PluginTarget::Game, false));
}

TEST_CASE("ResolvePlugins walks a required dependency chain in order", "[plugins][resolution]") {
    auto a = Make("A");
    auto b = Make("B");
    auto c = Make("C");
    RequireDep(c, "B");
    RequireDep(b, "A");
    const PluginDescriptor valid[] = {c, a, b};
    const ProjectPluginEntry project[] = {{"A", true}, {"B", true}, {"C", true}};
    const auto out = ResolvePlugins(valid, {}, project);
    REQUIRE(ActiveOrder(out) == std::vector<std::string>{"A", "B", "C"});
    REQUIRE(Find(out, "A")->state == PluginResolveState::Active);
}

TEST_CASE("ResolvePlugins orders a diamond with name ties after dependencies",
          "[plugins][resolution]") {
    auto a = Make("A");
    auto b = Make("B");
    auto c = Make("C");
    auto d = Make("D");
    RequireDep(a, "B");
    RequireDep(a, "C");
    RequireDep(b, "D");
    RequireDep(c, "D");
    const PluginDescriptor valid[] = {a, b, c, d};
    const ProjectPluginEntry project[] = {{"A", true}, {"B", true}, {"C", true}, {"D", true}};
    const auto out = ResolvePlugins(valid, {}, project);
    REQUIRE(ActiveOrder(out) == std::vector<std::string>{"D", "B", "C", "A"});
}

TEST_CASE("ResolvePlugins ignores optional missing dependencies", "[plugins][resolution]") {
    auto a = Make("A");
    RequireDep(a, "Ghost", ">=1.0.0", true);
    const PluginDescriptor valid[] = {a};
    const ProjectPluginEntry project[] = {{"A", true}};
    const auto out = ResolvePlugins(valid, {}, project);
    REQUIRE(Find(out, "A")->state == PluginResolveState::Active);
}

TEST_CASE("ResolvePlugins marks missing required dependencies Unavailable", "[plugins][resolution]") {
    auto a = Make("A");
    RequireDep(a, "Missing");
    const PluginDescriptor valid[] = {a};
    const ProjectPluginEntry project[] = {{"A", true}};
    const auto out = ResolvePlugins(valid, {}, project);
    const auto* r = Find(out, "A");
    REQUIRE(r->state == PluginResolveState::Unavailable);
    REQUIRE(HasDiag(*r, "not installed"));
}

TEST_CASE("ResolvePlugins rejects a dependency whose VersionName is out of range",
          "[plugins][resolution]") {
    auto a = Make("A");
    auto b = Make("B", "1.0.0");
    RequireDep(a, "B", ">=2.0.0");
    const PluginDescriptor valid[] = {a, b};
    const ProjectPluginEntry project[] = {{"A", true}, {"B", true}};
    const auto out = ResolvePlugins(valid, {}, project);
    REQUIRE(Find(out, "B")->state == PluginResolveState::Active);
    const auto* r = Find(out, "A");
    REQUIRE(r->state == PluginResolveState::Unavailable);
    REQUIRE(HasDiag(*r, "1.0.0"));
}

TEST_CASE("ResolvePlugins reports a cycle with the full path", "[plugins][resolution]") {
    auto a = Make("A");
    auto b = Make("B");
    auto c = Make("C");
    RequireDep(a, "B");
    RequireDep(b, "C");
    RequireDep(c, "A");
    const PluginDescriptor valid[] = {a, b, c};
    const ProjectPluginEntry project[] = {{"A", true}, {"B", true}, {"C", true}};
    const auto out = ResolvePlugins(valid, {}, project);
    for (const char* name : {"A", "B", "C"}) {
        const auto* r = Find(out, name);
        REQUIRE(r->state == PluginResolveState::Unavailable);
        REQUIRE(HasDiag(*r, "cycle"));
        REQUIRE(HasDiag(*r, "A"));
        REQUIRE(HasDiag(*r, "B"));
        REQUIRE(HasDiag(*r, "C"));
    }
}

TEST_CASE("ResolvePlugins respects platform, engine range, and EnabledByDefault",
          "[plugins][resolution]") {
    auto off = Make("OffByDefault");
    auto platform = Make("WrongOs");
    platform.supportedTargetPlatforms = {
        std::string_view(HostPlatform()) == "Linux" ? "Windows" : "Linux"};
    auto future = Make("Future");
    future.engineVersionRange = ">=" + std::to_string(EngineVersion().major + 1) + ".0.0";
    const PluginDescriptor valid[] = {off, platform, future};
    const auto out = ResolvePlugins(valid, {}, {});
    REQUIRE(Find(out, "OffByDefault")->state == PluginResolveState::Disabled);
    REQUIRE(Find(out, "WrongOs")->state == PluginResolveState::Disabled);
    REQUIRE(Find(out, "Future")->state == PluginResolveState::Disabled);

    const ProjectPluginEntry project[] = {{"WrongOs", true}, {"Future", true}};
    const auto enabled = ResolvePlugins(valid, {}, project);
    REQUIRE(Find(enabled, "WrongOs")->state == PluginResolveState::Unavailable);
    REQUIRE(HasDiag(*Find(enabled, "WrongOs"), HostPlatform()));
    REQUIRE(Find(enabled, "Future")->state == PluginResolveState::Unavailable);
    REQUIRE(HasDiag(*Find(enabled, "Future"), EngineVersion().ToString()));
}

TEST_CASE("ResolvePlugins propagates unavailability through dependents", "[plugins][resolution]") {
    auto a = Make("A");
    auto b = Make("B");
    auto c = Make("C");
    RequireDep(b, "A");
    RequireDep(c, "B");
    a.engineVersionRange = ">=99.0.0";
    const PluginDescriptor valid[] = {a, b, c};
    const ProjectPluginEntry project[] = {{"A", true}, {"B", true}, {"C", true}};
    const auto out = ResolvePlugins(valid, {}, project);
    REQUIRE(Find(out, "A")->state == PluginResolveState::Unavailable);
    REQUIRE(Find(out, "B")->state == PluginResolveState::Unavailable);
    REQUIRE(Find(out, "C")->state == PluginResolveState::Unavailable);
    REQUIRE(HasDiag(*Find(out, "C"), "unavailable"));
}

TEST_CASE("ResolvePlugins lists invalid plugins and unknown project entries",
          "[plugins][resolution]") {
    PluginDescriptor bad;
    bad.name = "Broken";
    bad.descriptorPath = "Broken.lplugin";
    auto ok = Make("Ok");
    ok.enabledByDefault = true;
    const PluginDescriptor valid[] = {ok};
    const PluginDescriptor invalid[] = {bad};
    const ProjectPluginEntry project[] = {{"Missing", true}};
    const auto out = ResolvePlugins(valid, invalid, project);
    REQUIRE(Find(out, "Broken")->state == PluginResolveState::Invalid);
    REQUIRE(Find(out, "Missing")->state == PluginResolveState::Unavailable);
    REQUIRE(HasDiag(*Find(out, "Missing"), "not installed"));
    REQUIRE(Find(out, "Ok")->state == PluginResolveState::Active);
}

TEST_CASE("ResolvePlugins treats a missing built module as Unavailable", "[plugins][resolution]") {
    auto a = Make("A");
    const PluginDescriptor valid[] = {a};
    const ProjectPluginEntry project[] = {{"A", true}};
    ResolveConfig config;
    config.checkBuiltModules = true;
    const auto missing = ResolvePlugins(valid, {}, project, config);
    REQUIRE(Find(missing, "A")->state == PluginResolveState::Unavailable);
    REQUIRE(HasDiag(*Find(missing, "A"), "not built"));

    config.builtModules.insert("ARuntime");
    const auto present = ResolvePlugins(valid, {}, project, config);
    REQUIRE(Find(present, "A")->state == PluginResolveState::Active);
}

TEST_CASE("ResolvePlugins order is deterministic across input permutations",
          "[plugins][resolution]") {
    auto x = Make("X");
    auto y = Make("Y");
    auto z = Make("Z");
    x.enabledByDefault = true;
    y.enabledByDefault = true;
    z.enabledByDefault = true;
    const PluginDescriptor a[] = {z, y, x};
    const PluginDescriptor b[] = {x, z, y};
    REQUIRE(ActiveOrder(ResolvePlugins(a, {}, {})) ==
            ActiveOrder(ResolvePlugins(b, {}, {})));
    REQUIRE(ActiveOrder(ResolvePlugins(a, {}, {})) == std::vector<std::string>{"X", "Y", "Z"});
}
