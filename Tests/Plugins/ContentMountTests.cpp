#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <string>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Asset/AssetRegistry.h"
#include "Luma/Core/Version.h"
#include "Luma/Plugins/PluginManager.h"
#include "Luma/Serialization/Json.h"

namespace fs = std::filesystem;
using Luma::AssetPathResolver;
using Luma::AssetRegistry;
using Luma::ContentRootKind;
using Luma::EngineVersion;
using Luma::SerialValue;
using Luma::Plugins::CompiledPluginTable;
using Luma::Plugins::HostPlatform;
using Luma::Plugins::PluginManager;
using Luma::Plugins::PluginState;

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

std::string ContentPluginJson(const std::string& name, bool canContainContent) {
    return std::format(
        "{{\n"
        "  \"FileVersion\": 1,\n"
        "  \"Version\": 1,\n"
        "  \"VersionName\": \"1.0.0\",\n"
        "  \"FriendlyName\": \"{}\",\n"
        "  \"Description\": \"Content fixture.\",\n"
        "  \"Category\": \"Tests\",\n"
        "  \"CreatedBy\": \"Luma\",\n"
        "  \"EngineVersion\": \"{}\",\n"
        "  \"EnabledByDefault\": true,\n"
        "  \"CanContainContent\": {},\n"
        "  \"SupportedTargetPlatforms\": [\"{}\"]\n"
        "}}\n",
        name, CompatibleRange(), canContainContent ? "true" : "false", HostPlatform());
}

void WritePlugin(const fs::path& pluginsRoot, const std::string& name, bool canContainContent) {
    const fs::path dir = pluginsRoot / name;
    fs::create_directories(dir / "Content" / "Materials");
    fs::create_directories(dir / "Config");
    std::ofstream(dir / (name + ".lplugin")) << ContentPluginJson(name, canContainContent);
    std::ofstream(dir / "Content" / "Materials" / "M_Pack.lmat") << "{}";
    std::ofstream(dir / "Config" / ("Default" + name + ".json"))
        << "{\n  \"fileVersion\": 1,\n  \"Plugins." << name
        << "\": { \"speed\": 12 }\n}\n";
}

}  // namespace

TEST_CASE("Active plugin content mounts at /Name/ and is scanned", "[plugins][content]") {
    TempDir dir("luma_plugin_content_on");
    const fs::path plugins = dir.root / "Plugins";
    WritePlugin(plugins, "Pack", true);
    CompiledPluginTable table;
    PluginManager mgr(table);
    const fs::path roots[] = {plugins};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);

    AssetPathResolver paths;
    paths.SetGameRoot(dir.root / "Assets");
    fs::create_directories(dir.root / "Assets" / "Pack" / "Materials");
    std::ofstream(dir.root / "Assets" / "Pack" / "Materials" / "M_Pack.lmat") << "game";

    AssetRegistry registry;
    mgr.MountContentAndConfig(&paths, &registry);
    registry.Scan();

    REQUIRE(mgr.Find("Pack"));
    REQUIRE(mgr.Find("Pack")->state == PluginState::Resolved);
    REQUIRE(paths.IsPluginMounted("Pack"));
    auto file = paths.ToFile("/Pack/Materials/M_Pack");
    REQUIRE(file.has_value());
    CHECK(fs::equivalent(*file, plugins / "Pack" / "Content" / "Materials" / "M_Pack.lmat"));
    CHECK(paths.Classify("/Pack/Materials/M_Pack") == AssetPathResolver::Status::Ok);
    auto gameFile = paths.ToFile("/Game/Pack/Materials/M_Pack");
    REQUIRE(gameFile.has_value());
    CHECK_FALSE(fs::equivalent(*file, *gameFile));
    REQUIRE(registry.LookupByPath(*file) != nullptr);
    bool sawPluginRoot = false;
    for (const auto& info : registry.ContentRoots()) {
        if (info.kind == ContentRootKind::Plugin && info.label == "Pack") sawPluginRoot = true;
    }
    REQUIRE(sawPluginRoot);
    const SerialValue* defaults = mgr.PluginDefaultConfig("Pack");
    REQUIRE(defaults);
    REQUIRE(defaults->Find("Plugins.Pack"));
    CHECK(defaults->Find("Plugins.Pack")->Find("speed")->AsInt() == 12);
}

TEST_CASE("Disabled plugin content is a missing-plugin error and does not alias /Game",
          "[plugins][content]") {
    TempDir dir("luma_plugin_content_off");
    const fs::path plugins = dir.root / "Plugins";
    WritePlugin(plugins, "Pack", true);
    CompiledPluginTable table;
    PluginManager mgr(table);
    const fs::path roots[] = {plugins};
    mgr.Discover(roots);
    mgr.RestrictEnabled(std::span<const std::string>{});

    AssetPathResolver paths;
    const fs::path game = dir.root / "Assets";
    fs::create_directories(game / "Pack" / "Materials");
    std::ofstream(game / "Pack" / "Materials" / "M_Pack.lmat") << "game";
    paths.SetGameRoot(game);

    AssetRegistry registry;
    registry.AddRoot(game);
    mgr.MountContentAndConfig(&paths, &registry);

    REQUIRE(mgr.Find("Pack")->state == PluginState::Disabled);
    REQUIRE_FALSE(paths.IsPluginMounted("Pack"));
    REQUIRE(paths.IsKnownPlugin("Pack"));
    CHECK_FALSE(paths.ToFile("/Pack/Materials/M_Pack").has_value());
    CHECK(paths.Classify("/Pack/Materials/M_Pack") == AssetPathResolver::Status::MissingPlugin);
    auto gameFile = paths.ToFile("/Game/Pack/Materials/M_Pack");
    REQUIRE(gameFile.has_value());
    CHECK(fs::equivalent(*gameFile, game / "Pack" / "Materials" / "M_Pack.lmat"));
    CHECK_FALSE(registry.IsPluginPath(*gameFile));
}

TEST_CASE("CanContainContent false does not mount Content/", "[plugins][content]") {
    TempDir dir("luma_plugin_content_flag");
    const fs::path plugins = dir.root / "Plugins";
    WritePlugin(plugins, "Pack", false);
    CompiledPluginTable table;
    PluginManager mgr(table);
    const fs::path roots[] = {plugins};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    AssetPathResolver paths;
    mgr.MountContentAndConfig(&paths, nullptr);
    REQUIRE_FALSE(paths.IsPluginMounted("Pack"));
    CHECK(paths.Classify("/Pack/Materials/M_Pack") == AssetPathResolver::Status::MissingPlugin);
}
