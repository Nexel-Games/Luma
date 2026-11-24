#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Luma/Core/Registration.h"
#include "Luma/Core/Version.h"
#include "Luma/Editor/MenuRegistry.h"
#include "Luma/Editor/PanelRegistry.h"
#include "Luma/EditorPlugins/PluginsWindow.h"
#include "Luma/Plugins/PluginDiscovery.h"
#include "Luma/Plugins/PluginManager.h"

using Luma::EngineVersion;
using Luma::Editor::MenuRegistry;
using Luma::Editor::PanelRegistry;
using Luma::EditorPlugins::PluginCategory;
using Luma::EditorPlugins::PluginMatchesQuery;
using Luma::EditorPlugins::PluginStatusLine;
using Luma::EditorPlugins::PluginsWindow;
using Luma::EditorPlugins::RegisterPluginsWindow;
using Luma::EditorPlugins::kPluginsPanelId;
using Luma::Plugins::CompiledPluginTable;
using Luma::Plugins::PluginManager;
using Luma::Plugins::PluginOrigin;
using Luma::Plugins::PluginState;
using Luma::Plugins::PluginStatus;
using Luma::RegistrationSet;

namespace fs = std::filesystem;

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

void WritePlugin(const fs::path& root, const std::string& name, const std::string& category) {
    const fs::path dir = root / name;
    fs::create_directories(dir);
    std::ofstream out(dir / (name + ".lplugin"), std::ios::binary);
    out << "{\n"
           "  \"FileVersion\": 1,\n"
           "  \"Version\": 1,\n"
           "  \"VersionName\": \"1.0.0\",\n"
           "  \"FriendlyName\": \""
        << name << "\",\n"
                   "  \"Description\": \"A " << category << " plugin.\",\n"
                   "  \"Category\": \""
        << category
        << "\",\n"
           "  \"CreatedBy\": \"Luma\",\n"
           "  \"EngineVersion\": \""
        << CompatibleRange()
        << "\",\n"
           "  \"SupportedTargetPlatforms\": [\"Windows\", \"Linux\", \"macOS\"],\n"
           "  \"Modules\": [ { \"Name\": \""
        << name << "Runtime\", \"Type\": \"Runtime\" } ]\n"
                   "}\n";
}

PluginStatus MakeStatus() {
    PluginStatus s;
    s.manifest.id = "SampleSpinner";
    s.manifest.displayName = "Sample Spinner";
    s.manifest.description = "Rotates actors.";
    s.manifest.author = "Luma";
    s.manifest.category = "Simulation";
    s.descriptor.name = "SampleSpinner";
    s.descriptor.friendlyName = "Sample Spinner";
    s.descriptor.description = "Rotates actors.";
    s.descriptor.createdBy = "Luma";
    s.descriptor.category = "Simulation";
    Luma::Plugins::ModuleDescriptor mod;
    mod.name = "SampleSpinnerEditor";
    s.descriptor.modules.push_back(mod);
    s.state = PluginState::Started;
    s.enabledInProject = true;
    return s;
}

}  // namespace

TEST_CASE("PluginMatchesQuery searches name, description, author, and modules",
          "[plugins][browser]") {
    const PluginStatus s = MakeStatus();
    REQUIRE(PluginMatchesQuery(s, ""));
    REQUIRE(PluginMatchesQuery(s, "spinner"));
    REQUIRE(PluginMatchesQuery(s, "ROTATES"));
    REQUIRE(PluginMatchesQuery(s, "luma"));
    REQUIRE(PluginMatchesQuery(s, "SampleSpinnerEditor"));
    REQUIRE_FALSE(PluginMatchesQuery(s, "physx"));
}

TEST_CASE("PluginStatusLine names Active, Disabled, Unavailable, Failed, Not installed",
          "[plugins][browser]") {
    PluginStatus s = MakeStatus();
    REQUIRE(PluginStatusLine(s) == "Active");
    s.enabledInProject = false;
    REQUIRE(PluginStatusLine(s) == "Disabled");
    s.enabledInProject = true;
    s.state = PluginState::Unavailable;
    s.diagnostics.push_back("needs PhysX");
    REQUIRE(PluginStatusLine(s).find("Unavailable") != std::string::npos);
    REQUIRE(PluginStatusLine(s).find("PhysX") != std::string::npos);
    s.state = PluginState::Failed;
    s.diagnostics = {"threw"};
    REQUIRE(PluginStatusLine(s).find("Failed") != std::string::npos);
    s.state = PluginState::NotInstalled;
    REQUIRE(PluginStatusLine(s) == "Not installed");
    REQUIRE(PluginCategory(s) == "Simulation");
}

TEST_CASE("Discover tags Built-In vs Project plugin folders", "[plugins][browser]") {
    TempDir engine("luma_plugins_browser_engine");
    TempDir project("luma_plugins_browser_project");
    WritePlugin(engine.root, "EngineSim", "Simulation");
    WritePlugin(project.root, "GameTool", "Tools");
    CompiledPluginTable table;
    PluginManager mgr(table);
    const fs::path roots[] = {engine.root, project.root};
    mgr.Discover(roots);
    REQUIRE(mgr.Find("EngineSim"));
    REQUIRE(mgr.Find("EngineSim")->origin == PluginOrigin::BuiltIn);
    REQUIRE(mgr.Find("GameTool"));
    REQUIRE(mgr.Find("GameTool")->origin == PluginOrigin::Project);
}

TEST_CASE("RegisterPluginsWindow adds Edit → Plugins and a dockable panel",
          "[plugins][browser]") {
    MenuRegistry menus;
    PanelRegistry panels;
    RegistrationSet set;
    set.Add(menus.RegisterMenu({"Edit", "Edit", 100, "Engine"}));
    PluginsWindow window;
    CompiledPluginTable table;
    PluginManager mgr(table);
    bool opened = false;
    RegisterPluginsWindow(set, menus, panels, window, mgr, [] { return nullptr; },
                          [] { return nullptr; }, [&opened] { opened = true; });
    REQUIRE(panels.Find(kPluginsPanelId));
    REQUIRE(menus.Execute("Edit.Plugins"));
    REQUIRE(opened);
}

TEST_CASE("PlanEnable records beta and experimental flags", "[plugins][browser]") {
    TempDir root("luma_plugins_browser_beta");
    WritePlugin(root.root, "BetaThing", "Tools");
    {
        std::ifstream in(root.root / "BetaThing" / "BetaThing.lplugin");
        std::string json((std::istreambuf_iterator<char>(in)), {});
        const auto pos = json.find("\"Category\"");
        json.insert(pos, "\"IsBetaVersion\": true,\n  ");
        std::ofstream out(root.root / "BetaThing" / "BetaThing.lplugin", std::ios::binary);
        out << json;
    }
    CompiledPluginTable table;
    PluginManager mgr(table);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    auto plan = mgr.PlanEnable("BetaThing");
    REQUIRE(plan.beta);
    REQUIRE_FALSE(plan.experimental);
}
