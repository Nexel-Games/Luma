#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Luma/Editor/Panels/ProjectSettings.h"
#include "Luma/Editor/Panels/ProjectSettingsStore.h"
#include "Luma/Editor/SettingsPageRegistry.h"
#include "Luma/Project/Project.h"
#include "Luma/Serialization/Json.h"

namespace fs = std::filesystem;
using namespace Luma;
using namespace Luma::Editor;
using Luma::Editor::Panels::MapsAndModesSettings;
using Luma::Editor::Panels::ProjectSettingsPanel;
using Luma::Editor::Panels::ProjectSettingsStore;

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

std::string ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void WriteFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

// A page holding one integer, persisted as {"value": n}.
struct IntPage {
    int value = 0;
    int loads = 0;
    bool loadedNull = false;
    SettingsPageDesc Desc(std::string id, std::string file) {
        SettingsPageDesc p;
        p.id = std::move(id);
        p.category = "Test";
        p.title = p.id;
        p.owner = "Test";
        p.configFile = std::move(file);
        p.load = [this](const SerialValue& s) {
            ++loads;
            loadedNull = s.IsNull();
            value = s.IsObject() && s.Find("value") ? static_cast<int>(s.Find("value")->AsInt()) : 0;
        };
        p.save = [this] {
            SerialValue s = SerialValue::MakeObject();
            s["value"] = value;
            return s;
        };
        p.draw = [this](Slate::Context&, const Slate::Rect&) { return false; };
        return p;
    }
};

}  // namespace

TEST_CASE("Settings pages save their section and load it back", "[settings]") {
    TempDir dir("luma_settings_roundtrip");
    SettingsPageRegistry reg;
    IntPage a, b;
    RegistrationSet set;
    set.Add(reg.Register(a.Desc("Test.A", "Game.json")));
    set.Add(reg.Register(b.Desc("Test.B", "Game.json")));
    WriteFile(dir.root / "Config" / "Game.json",
              R"({"fileVersion": 1, "Other.Section": {"keep": [1, 2]}})");

    ProjectSettingsStore store(reg, dir.root / "Config");
    REQUIRE(store.LoadAll());
    REQUIRE(a.loadedNull);  // never saved yet
    a.value = 7;
    b.value = 9;
    store.MarkDirty("Test.A");
    store.MarkDirty("Test.B");
    REQUIRE(store.AnyDirty());
    std::vector<std::string> errors;
    REQUIRE(store.SaveAll(&errors));
    REQUIRE_FALSE(store.AnyDirty());

    const auto doc = ParseJson(ReadFile(dir.root / "Config" / "Game.json"));
    REQUIRE(doc.has_value());
    REQUIRE(doc->Find("Other.Section")->Find("keep")->Size() == 2);  // untouched
    REQUIRE(doc->Find("Test.A")->Find("value")->AsInt() == 7);
    REQUIRE(doc->Find("Test.B")->Find("value")->AsInt() == 9);

    a.value = b.value = 0;
    ProjectSettingsStore again(reg, dir.root / "Config");
    REQUIRE(again.LoadAll());
    REQUIRE(a.value == 7);
    REQUIRE(b.value == 9);
}

TEST_CASE("A corrupt config file is reported and never overwritten", "[settings]") {
    TempDir dir("luma_settings_corrupt");
    SettingsPageRegistry reg;
    IntPage a;
    RegistrationToken t = reg.Register(a.Desc("Test.A", "Broken.json"));
    const fs::path file = dir.root / "Config" / "Broken.json";
    WriteFile(file, R"({"Test.A": {"value": )");
    ProjectSettingsStore store(reg, dir.root / "Config");
    std::vector<std::string> errors;
    REQUIRE_FALSE(store.LoadAll(&errors));
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].find("Broken.json") != std::string::npos);

    a.value = 3;
    store.MarkDirty("Test.A");
    std::string error;
    REQUIRE_FALSE(store.Save("Test.A", &error));
    REQUIRE(error.find("not overwritten") != std::string::npos);
    REQUIRE(ReadFile(file) == R"({"Test.A": {"value": )");
}

TEST_CASE("A read-only config file fails to save with the reason", "[settings]") {
    TempDir dir("luma_settings_ro");
    SettingsPageRegistry reg;
    IntPage a;
    RegistrationToken t = reg.Register(a.Desc("Test.A", "Game.json"));
    const fs::path file = dir.root / "Config" / "Game.json";
    WriteFile(file, "{}");
    constexpr fs::perms kWrite =
        fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
    fs::permissions(file, kWrite, fs::perm_options::remove);
    ProjectSettingsStore store(reg, dir.root / "Config");
    REQUIRE(store.LoadAll());
    store.MarkDirty("Test.A");
    std::string error;
    REQUIRE_FALSE(store.Save("Test.A", &error));
    REQUIRE(error.find("read-only") != std::string::npos);
    REQUIRE(store.IsDirty("Test.A"));
    fs::permissions(file, kWrite, fs::perm_options::add);
}

TEST_CASE("Pages registered later are loaded when they appear", "[settings]") {
    TempDir dir("luma_settings_late");
    SettingsPageRegistry reg;
    WriteFile(dir.root / "Config" / "Plugins.json", R"({"Late.Page": {"value": 5}})");
    ProjectSettingsStore store(reg, dir.root / "Config");
    REQUIRE(store.LoadAll());
    IntPage late;
    RegistrationToken t = reg.Register(late.Desc("Late.Page", "Plugins.json"));
    store.LoadNewPages();
    REQUIRE(late.value == 5);
    store.LoadNewPages();
    REQUIRE(late.loads == 1);
}

TEST_CASE("Plugin settings load Default overlay then project Config", "[settings][plugins]") {
    TempDir dir("luma_settings_plugin_layer");
    SettingsPageRegistry reg;
    IntPage page;
    SettingsPageDesc desc = page.Desc("Plugins.Pack", "Pack.json");
    desc.owner = "Pack";
    RegistrationToken t = reg.Register(std::move(desc));
    SerialValue defaults = SerialValue::MakeObject();
    SerialValue section = SerialValue::MakeObject();
    section["value"] = 3;
    defaults["Plugins.Pack"] = section;
    ProjectSettingsStore store(reg, dir.root / "Config");
    store.SetPluginDefaultConfig("Pack", defaults);
    REQUIRE(store.LoadAll());
    REQUIRE(page.value == 3);
    REQUIRE_FALSE(page.loadedNull);
    page.value = 9;
    store.MarkDirty("Plugins.Pack");
    REQUIRE(store.SaveAll());
    page.value = 0;
    REQUIRE(store.LoadAll());
    REQUIRE(page.value == 9);
    const std::string saved = ReadFile(dir.root / "Config" / "Pack.json");
    REQUIRE(saved.find("\"value\"") != std::string::npos);
    REQUIRE(fs::exists(dir.root / "Config" / "Pack.json"));
}

TEST_CASE("The Project Settings window lists the registered pages only", "[settings]") {
    TempDir dir("luma_settings_window");
    SettingsPageRegistry reg;
    IntPage a;
    RegistrationToken t = reg.Register(a.Desc("Test.A", "Game.json"));
    ProjectSettingsStore store(reg, dir.root / "Config");
    ProjectSettingsPanel panel(reg);
    panel.SetStore(&store);
    REQUIRE_FALSE(panel.IsOpen());
    panel.Open();
    REQUIRE(panel.IsOpen());
    REQUIRE(panel.Categories() == std::vector<std::string>{"Test"});
    REQUIRE(panel.SelectedPage() == "Test.A");
    panel.Close();
    REQUIRE_FALSE(panel.IsOpen());
}

TEST_CASE("Maps & Modes chooses the editor startup scene", "[settings]") {
    TempDir dir("luma_settings_maps");
    ProjectDesc desc;
    desc.name = "Maps";
    desc.parentDirectory = dir.root;
    auto project = Project::Create(desc);
    REQUIRE(project.has_value());

    MapsAndModesSettings maps;
    REQUIRE(Editor::Panels::EditorStartupScenePath(*project, maps) == project->StartupScenePath());
    maps.editorStartupScene = "Assets/Scenes/Lab.lscene";
    WriteFile(project->RootDir() / "Assets" / "Scenes" / "Lab.lscene", "{}");
    REQUIRE(Editor::Panels::EditorStartupScenePath(*project, maps) ==
            project->RootDir() / "Assets" / "Scenes" / "Lab.lscene");
    maps.editorStartupScene = "Assets/Scenes/Missing.lscene";  // falls back
    REQUIRE(Editor::Panels::EditorStartupScenePath(*project, maps) == project->StartupScenePath());

    SettingsPageRegistry reg;
    RegistrationToken t = Editor::Panels::RegisterMapsAndModesPage(reg, maps, [] {
        return std::vector<std::string>{"Assets/Scenes/Lab.lscene"};
    });
    REQUIRE(t.IsActive());
    maps.editorStartupScene = "Assets/Scenes/Lab.lscene";
    ProjectSettingsStore store(reg, project->ConfigDir());
    store.MarkDirty(Editor::Panels::kMapsAndModesPageId);
    REQUIRE(store.SaveAll());
    maps.editorStartupScene.clear();
    REQUIRE(store.LoadAll());
    REQUIRE(maps.editorStartupScene == "Assets/Scenes/Lab.lscene");
}

TEST_CASE("Simulation settings round trip through project Config and initialize play", "[settings][play]") {
    TempDir dir("luma_simulation_settings");
    SettingsPageRegistry pages;
    SimulationSettings settings;
    auto token = Panels::RegisterSimulationPage(pages, settings);
    REQUIRE(token);
    ProjectSettingsStore store(pages, dir.root / "Config");
    REQUIRE(store.LoadAll());
    settings = {0.025, 4};
    store.MarkDirty("Project.Simulation");
    REQUIRE(store.SaveAll());
    settings = {};
    REQUIRE(store.LoadAll());
    CHECK(settings.fixedTimeStep == 0.025);
    CHECK(settings.maxSubsteps == 4);
    Scene authoring;
    SceneRunner runner;
    REQUIRE(runner.Begin(authoring, settings));
}
