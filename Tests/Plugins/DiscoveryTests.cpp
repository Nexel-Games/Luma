#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Luma/Core/Version.h"
#include "Luma/Plugins/PluginDiscovery.h"

namespace fs = std::filesystem;
using Luma::EngineVersion;
using Luma::Plugins::CompiledPluginTable;
using Luma::Plugins::DiscoverPlugins;
using Luma::Plugins::HostPlatform;

namespace {

struct TempDir {
    fs::path root;
    explicit TempDir(const char* name) : root(fs::temp_directory_path() / name) {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

std::string CompatibleRange() {
    const Luma::SemVer v = EngineVersion();
    return ">=" + v.ToString() + " <" + std::to_string(v.major + 1) + ".0.0";
}

std::string IncompatibleRange() {
    const Luma::SemVer v = EngineVersion();
    return ">=" + std::to_string(v.major + 1) + ".0.0";
}

const char* OtherPlatform() {
    return std::string_view(HostPlatform()) == "Linux" ? "Windows" : "Linux";
}

std::string ManifestJson(const std::string& id, const std::string& engineRange,
                         const std::string& platform) {
    return std::format(
        "{{\n"
        "  \"schema\": 1,\n"
        "  \"id\": \"{}\",\n"
        "  \"displayName\": \"{}\",\n"
        "  \"description\": \"Discovered sample.\",\n"
        "  \"category\": \"Tools\",\n"
        "  \"author\": \"Luma\",\n"
        "  \"version\": \"1.0.0\",\n"
        "  \"engineVersionRange\": \"{}\",\n"
        "  \"kind\": \"Runtime\",\n"
        "  \"platforms\": [\"{}\"]\n"
        "}}\n",
        id, id, engineRange, platform);
}

void WritePlugin(const fs::path& root, const std::string& id, const std::string& json) {
    const fs::path dir = root / id;
    fs::create_directories(dir);
    std::ofstream out(dir / (id + ".lplugin"), std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out << json;
}

bool HasMessage(const auto& errors, std::string_view needle) {
    for (const auto& error : errors) {
        if (error.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("DiscoverPlugins reads both roots and does not execute factories",
          "[plugins][discovery]") {
    TempDir engine("luma_plugins_engine");
    TempDir project("luma_plugins_project");
    const std::string jsonA =
        ManifestJson("Luma.Alpha", CompatibleRange(), HostPlatform());
    const std::string jsonB =
        ManifestJson("Luma.Beta", CompatibleRange(), HostPlatform());
    WritePlugin(engine.root, "Luma.Alpha", jsonA);
    WritePlugin(project.root, "Luma.Beta", jsonB);

    int calls = 0;
    CompiledPluginTable table;
    std::string regError;
    REQUIRE(table.Register("Luma.Alpha", [&] { ++calls; }, &regError));
    REQUIRE(table.Register("Luma.Beta", [&] { ++calls; }, &regError));
    REQUIRE(table.InstantiateCount() == 0);

    const fs::path roots[] = {engine.root, project.root};
    const auto found = DiscoverPlugins(roots, &table);
    REQUIRE(found.errors.empty());
    REQUIRE(found.plugins.size() == 2);
    REQUIRE(found.plugins[0].manifest.id == "Luma.Alpha");
    REQUIRE(found.plugins[1].manifest.id == "Luma.Beta");
    REQUIRE(found.plugins[0].compiledIn);
    REQUIRE(found.plugins[1].compiledIn);
    REQUIRE(found.plugins[0].engineCompatible);
    REQUIRE(found.plugins[0].platformCompatible);
    REQUIRE(calls == 0);
    REQUIRE(table.InstantiateCount() == 0);

    REQUIRE(table.Instantiate("Luma.Alpha"));
    REQUIRE(calls == 1);
    REQUIRE(table.InstantiateCount() == 1);

    const auto again = DiscoverPlugins(roots, &table);
    REQUIRE(again.errors.empty());
    REQUIRE(calls == 1);
    REQUIRE(table.InstantiateCount() == 1);
}

TEST_CASE("DiscoverPlugins reports a duplicate id with both paths", "[plugins][discovery]") {
    TempDir a("luma_plugins_dup_a");
    TempDir b("luma_plugins_dup_b");
    const std::string json = ManifestJson("Luma.Sample", CompatibleRange(), HostPlatform());
    WritePlugin(a.root, "Luma.Sample", json);
    WritePlugin(b.root, "Luma.Sample", json);
    const fs::path roots[] = {a.root, b.root};
    const auto found = DiscoverPlugins(roots);
    REQUIRE(found.plugins.empty());
    REQUIRE(HasMessage(found.errors, "Luma.Sample"));
    REQUIRE(HasMessage(found.errors, "luma_plugins_dup_a"));
    REQUIRE(HasMessage(found.errors, "luma_plugins_dup_b"));
}

TEST_CASE("DiscoverPlugins reports folder and id mismatches", "[plugins][discovery]") {
    TempDir root("luma_plugins_mismatch");
    const fs::path dir = root.root / "Luma.Sample";
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "Luma.Sample.lplugin", std::ios::binary);
        out << ManifestJson("Luma.Other", CompatibleRange(), HostPlatform());
    }
    const fs::path roots[] = {root.root};
    const auto found = DiscoverPlugins(roots);
    REQUIRE(found.plugins.empty());
    REQUIRE(HasMessage(found.errors, "Luma.Other"));
    REQUIRE(HasMessage(found.errors, "Luma.Sample"));
}

TEST_CASE("DiscoverPlugins reports a manifest file whose name does not match the folder",
          "[plugins][discovery]") {
    TempDir root("luma_plugins_filename");
    const fs::path dir = root.root / "Luma.Sample";
    fs::create_directories(dir);
    {
        std::ofstream out(dir / "Other.lplugin", std::ios::binary);
        out << ManifestJson("Luma.Sample", CompatibleRange(), HostPlatform());
    }
    const fs::path roots[] = {root.root};
    const auto found = DiscoverPlugins(roots);
    REQUIRE(found.plugins.empty());
    REQUIRE(HasMessage(found.errors, "Other.lplugin"));
    REQUIRE(HasMessage(found.errors, "Luma.Sample.lplugin"));
}

TEST_CASE("DiscoverPlugins reports an unreadable manifest", "[plugins][discovery]") {
    TempDir root("luma_plugins_unreadable");
    fs::create_directories(root.root / "Luma.Broken" / "Luma.Broken.lplugin");
    const fs::path roots[] = {root.root};
    const auto found = DiscoverPlugins(roots);
    REQUIRE(found.plugins.empty());
    REQUIRE(HasMessage(found.errors, "could not read"));
    REQUIRE(HasMessage(found.errors, "Luma.Broken.lplugin"));
}

TEST_CASE("DiscoverPlugins keeps incompatible engine and platform plugins with diagnostics",
          "[plugins][discovery]") {
    TempDir root("luma_plugins_compat");
    WritePlugin(root.root, "Luma.Future",
                ManifestJson("Luma.Future", IncompatibleRange(), HostPlatform()));
    WritePlugin(root.root, "Luma.OtherOs",
                ManifestJson("Luma.OtherOs", CompatibleRange(), OtherPlatform()));
    const fs::path roots[] = {root.root};
    const auto found = DiscoverPlugins(roots);
    REQUIRE(found.errors.empty());
    REQUIRE(found.plugins.size() == 2);

    const auto& future = found.plugins[0];
    REQUIRE(future.manifest.id == "Luma.Future");
    REQUIRE_FALSE(future.engineCompatible);
    REQUIRE(future.platformCompatible);
    REQUIRE_FALSE(future.diagnostics.empty());
    REQUIRE(future.diagnostics[0].find(EngineVersion().ToString()) != std::string::npos);

    const auto& other = found.plugins[1];
    REQUIRE(other.manifest.id == "Luma.OtherOs");
    REQUIRE(other.engineCompatible);
    REQUIRE_FALSE(other.platformCompatible);
    REQUIRE(other.diagnostics[0].find(HostPlatform()) != std::string::npos);
    REQUIRE(other.diagnostics[0].find(OtherPlatform()) != std::string::npos);
}

TEST_CASE("DiscoverPlugins skips a missing root and rejects a manifest dropped in the root",
          "[plugins][discovery]") {
    TempDir root("luma_plugins_layout");
    {
        std::ofstream out(root.root / "Luma.Nope.lplugin", std::ios::binary);
        out << ManifestJson("Luma.Nope", CompatibleRange(), HostPlatform());
    }
    const fs::path missing = fs::temp_directory_path() / "luma_plugins_missing_root_absent";
    std::error_code ec;
    fs::remove_all(missing, ec);
    const fs::path roots[] = {missing, root.root};
    const auto found = DiscoverPlugins(roots);
    REQUIRE(found.plugins.empty());
    REQUIRE(HasMessage(found.errors, "Luma.Nope.lplugin"));
    REQUIRE(HasMessage(found.errors, "Plugins/<Id>/<Id>.lplugin"));
}

TEST_CASE("DiscoverPlugins reports invalid JSON without instantiating a factory",
          "[plugins][discovery]") {
    TempDir root("luma_plugins_badjson");
    WritePlugin(root.root, "Luma.Bad", "{ not json");
    int calls = 0;
    CompiledPluginTable table;
    REQUIRE(table.Register("Luma.Bad", [&] { ++calls; }));
    const fs::path roots[] = {root.root};
    const auto found = DiscoverPlugins(roots, &table);
    REQUIRE(found.plugins.empty());
    REQUIRE_FALSE(found.errors.empty());
    REQUIRE(calls == 0);
    REQUIRE(table.InstantiateCount() == 0);
}
