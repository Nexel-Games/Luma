#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "Luma/Core/Version.h"
#include "Luma/Plugins/PluginManager.h"
#include "Luma/Project/Project.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Serialization/Json.h"

namespace fs = std::filesystem;
using Luma::ComponentDesc;
using Luma::ComponentRegistry;
using Luma::EngineVersion;
using Luma::Entity;
using Luma::Project;
using Luma::ProjectDesc;
using Luma::Scene;
using Luma::SerialValue;
using Luma::Plugins::CompiledPluginTable;
using Luma::Plugins::HostPlatform;
using Luma::Plugins::IPlugin;
using Luma::Plugins::PluginContext;
using Luma::Plugins::PluginHostKind;
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
        fs::permissions(root, fs::perms::all, fs::perm_options::add, ec);
        fs::remove_all(root, ec);
    }
};

std::string CompatibleRange() {
    const Luma::SemVer v = EngineVersion();
    return ">=" + v.ToString() + " <" + std::to_string(v.major + 1) + ".0.0";
}

std::string Manifest(const std::string& id, const std::string& depsJson,
                     bool enabledByDefault = false, const std::string& version = "1.0.0") {
    return std::format(
        "{{\n"
        "  \"schema\": 1,\n"
        "  \"id\": \"{}\",\n"
        "  \"displayName\": \"{}\",\n"
        "  \"category\": \"Tools\",\n"
        "  \"author\": \"Luma\",\n"
        "  \"version\": \"{}\",\n"
        "  \"engineVersionRange\": \"{}\",\n"
        "  \"kind\": \"Runtime\",\n"
        "  \"platforms\": [\"{}\"],\n"
        "  \"enabledByDefault\": {},\n"
        "  \"dependencies\": {}\n"
        "}}\n",
        id, id, version, CompatibleRange(), HostPlatform(), enabledByDefault ? "true" : "false",
        depsJson);
}

void WritePlugin(const fs::path& root, const std::string& id, const std::string& json) {
    const fs::path dir = root / id;
    fs::create_directories(dir);
    std::ofstream out(dir / (id + ".lplugin"), std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out << json;
}

class EmptyPlugin final : public IPlugin {
public:
    bool Startup(PluginContext&, std::string&) override { return true; }
    void Shutdown(PluginContext&) override {}
};

void RegisterEmpty(CompiledPluginTable& table, const std::string& id) {
    REQUIRE(table.Register(id, [] { return std::make_unique<EmptyPlugin>(); }));
}

Project MakeProject(const fs::path& parent, const std::string& name) {
    ProjectDesc desc;
    desc.name = name;
    desc.parentDirectory = parent;
    auto project = Project::Create(desc);
    REQUIRE(project.has_value());
    return std::move(*project);
}

std::vector<std::string> ArrayIds(const SerialValue* arr) {
    std::vector<std::string> out;
    if (!arr || !arr->IsArray()) return out;
    for (const SerialValue& v : arr->Elements()) {
        if (v.IsString()) out.push_back(v.AsString());
    }
    return out;
}

bool ContainsId(const std::vector<std::string>& ids, std::string_view id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

constexpr auto kWrite =
    fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;

}  // namespace

TEST_CASE("Project plugins section round-trips schema 1", "[plugins][persist]") {
    TempDir dir("luma_p3_roundtrip");
    const fs::path pluginsRoot = dir.root / "discovered";
    WritePlugin(pluginsRoot, "Luma.KeepOff", Manifest("Luma.KeepOff", "[]", false));
    WritePlugin(pluginsRoot, "Luma.TurnOn", Manifest("Luma.TurnOn", "[]", false));

    CompiledPluginTable table;
    RegisterEmpty(table, "Luma.KeepOff");
    RegisterEmpty(table, "Luma.TurnOn");
    PluginManager mgr(table);
    const fs::path roots[] = {pluginsRoot};
    mgr.Discover(roots);

    Project project = MakeProject(dir.root, "PersistGame");
    mgr.ApplyProject(&project);
    REQUIRE_FALSE(mgr.Find("Luma.TurnOn")->enabledInProject);

    auto plan = mgr.PlanEnable("Luma.TurnOn");
    REQUIRE(plan.blockedBy.empty());
    REQUIRE(plan.alsoEnable.empty());
    std::string error;
    REQUIRE(mgr.Commit(project, plan, &error));
    REQUIRE(error.empty());

    const SerialValue* plugins = project.Find("plugins");
    REQUIRE(plugins);
    REQUIRE(plugins->Find("schema")->AsInt() == 1);
    REQUIRE(ContainsId(ArrayIds(plugins->Find("enabled")), "Luma.TurnOn"));

    auto reloaded = Project::Load(project.ProjectFile());
    REQUIRE(reloaded.has_value());
    PluginManager mgr2(table);
    mgr2.Discover(roots);
    mgr2.ApplyProject(&*reloaded);
    REQUIRE(mgr2.Find("Luma.TurnOn")->enabledInProject);
    REQUIRE_FALSE(mgr2.Find("Luma.KeepOff")->enabledInProject);
}

TEST_CASE("Unknown plugin ids in the project are kept and shown as not installed",
          "[plugins][persist]") {
    TempDir dir("luma_p3_unknown");
    const fs::path pluginsRoot = dir.root / "discovered";
    WritePlugin(pluginsRoot, "Luma.Known", Manifest("Luma.Known", "[]", false));

    CompiledPluginTable table;
    RegisterEmpty(table, "Luma.Known");
    PluginManager mgr(table);
    const fs::path roots[] = {pluginsRoot};
    mgr.Discover(roots);

    Project project = MakeProject(dir.root, "UnknownGame");
    SerialValue plugins = SerialValue::MakeObject();
    plugins["schema"] = 1;
    SerialValue enabled = SerialValue::MakeArray();
    enabled.PushBack("Luma.Known");
    enabled.PushBack("Studio.Missing.Plugin");
    plugins["enabled"] = enabled;
    SerialValue disabled = SerialValue::MakeArray();
    disabled.PushBack("Studio.Old.Plugin");
    plugins["disabled"] = disabled;
    plugins["note"] = "keep extra keys";
    REQUIRE(project.Set("plugins", plugins));
    REQUIRE(project.Save());

    mgr.ApplyProject(&project);
    REQUIRE(mgr.Find("Luma.Known"));
    REQUIRE(mgr.Find("Studio.Missing.Plugin"));
    REQUIRE(mgr.Find("Studio.Missing.Plugin")->state == PluginState::NotInstalled);
    REQUIRE(mgr.Find("Studio.Missing.Plugin")->enabledInProject);
    REQUIRE(mgr.Find("Studio.Old.Plugin")->state == PluginState::NotInstalled);
    REQUIRE_FALSE(mgr.Find("Studio.Old.Plugin")->enabledInProject);

    auto plan = mgr.PlanEnable("Luma.Known");
    REQUIRE(mgr.Commit(project, plan, nullptr));

    auto reloaded = Project::Load(project.ProjectFile());
    REQUIRE(reloaded.has_value());
    const SerialValue* saved = reloaded->Find("plugins");
    REQUIRE(saved);
    REQUIRE(saved->Find("note")->AsString() == "keep extra keys");
    REQUIRE(ContainsId(ArrayIds(saved->Find("enabled")), "Studio.Missing.Plugin"));
    REQUIRE(ContainsId(ArrayIds(saved->Find("disabled")), "Studio.Old.Plugin"));
}

TEST_CASE("PlanEnable pulls required dependencies only", "[plugins][persist]") {
    TempDir dir("luma_p3_plan_enable");
    const fs::path pluginsRoot = dir.root / "discovered";
    WritePlugin(pluginsRoot, "Luma.CoreLib", Manifest("Luma.CoreLib", "[]", false));
    WritePlugin(pluginsRoot, "Luma.OptionalLib", Manifest("Luma.OptionalLib", "[]", false));
    WritePlugin(pluginsRoot, "Luma.Unrelated", Manifest("Luma.Unrelated", "[]", false));
    WritePlugin(pluginsRoot, "Luma.Feature",
                Manifest("Luma.Feature",
                         R"([{"id":"Luma.CoreLib","version":">=1.0.0"},)"
                         R"({"id":"Luma.OptionalLib","version":">=1.0.0","optional":true}])",
                         false));

    CompiledPluginTable table;
    RegisterEmpty(table, "Luma.CoreLib");
    RegisterEmpty(table, "Luma.OptionalLib");
    RegisterEmpty(table, "Luma.Unrelated");
    RegisterEmpty(table, "Luma.Feature");
    PluginManager mgr(table);
    const fs::path roots[] = {pluginsRoot};
    mgr.Discover(roots);
    Project project = MakeProject(dir.root, "PlanEnableGame");
    mgr.ApplyProject(&project);

    auto plan = mgr.PlanEnable("Luma.Feature");
    REQUIRE(plan.blockedBy.empty());
    REQUIRE(plan.alsoEnable == std::vector<std::string>{"Luma.CoreLib"});
    REQUIRE_FALSE(ContainsId(plan.alsoEnable, "Luma.OptionalLib"));
    REQUIRE_FALSE(ContainsId(plan.alsoEnable, "Luma.Unrelated"));
    REQUIRE(plan.restart);

    REQUIRE(mgr.Commit(project, plan, nullptr));
    REQUIRE(mgr.Find("Luma.Feature")->enabledInProject);
    REQUIRE(mgr.Find("Luma.CoreLib")->enabledInProject);
    REQUIRE_FALSE(mgr.Find("Luma.OptionalLib")->enabledInProject);
    REQUIRE_FALSE(mgr.Find("Luma.Unrelated")->enabledInProject);
}

TEST_CASE("PlanDisable lists dependents and components in the open scene",
          "[plugins][persist]") {
    TempDir dir("luma_p3_plan_disable");
    const fs::path pluginsRoot = dir.root / "discovered";
    WritePlugin(pluginsRoot, "Luma.Base", Manifest("Luma.Base", "[]", true));
    WritePlugin(pluginsRoot, "Luma.Extra",
                Manifest("Luma.Extra", R"([{"id":"Luma.Base","version":">=1.0.0"}])", true));

    CompiledPluginTable table;
    RegisterEmpty(table, "Luma.Base");
    RegisterEmpty(table, "Luma.Extra");

    ComponentRegistry components;
    ComponentDesc desc;
    desc.id = "Spinner";
    desc.displayName = "Spinner";
    desc.category = "Tools";
    desc.owner = "Luma.Base";
    desc.has = [](const Scene& scene, Entity entity) {
        return scene.Registry().all_of<Luma::NameComponent>(entity) &&
               scene.Registry().get<Luma::NameComponent>(entity).name == "SpinActor";
    };
    desc.add = [](Scene&, Entity) {};
    desc.remove = [](Scene&, Entity) {};
    desc.write = [](const Scene&, Entity, SerialValue&) {};
    desc.read = [](Scene&, Entity, const SerialValue&) {};
    desc.reset = [](Scene&, Entity) {};
    auto spinnerToken = components.Register(std::move(desc));
    REQUIRE(spinnerToken);

    PluginManager mgr(table);
    mgr.SetRegistries(&components);
    const fs::path roots[] = {pluginsRoot};
    mgr.Discover(roots);
    Project project = MakeProject(dir.root, "PlanDisableGame");
    mgr.ApplyProject(&project);
    REQUIRE(mgr.StartAll(PluginHostKind::Runtime));

    Scene scene;
    scene.CreateEntity("SpinActor");
    scene.CreateEntity("Other");

    auto plan = mgr.PlanDisable("Luma.Base", &scene);
    REQUIRE(ContainsId(plan.dependents, "Luma.Extra"));
    REQUIRE_FALSE(plan.affectedComponents.empty());
    REQUIRE(plan.affectedComponents.front().find("Spinner") != std::string::npos);
    REQUIRE(plan.affectedComponents.front().find("SpinActor") != std::string::npos);
    REQUIRE(plan.restart);
}

TEST_CASE("Commit reports a read-only project file", "[plugins][persist]") {
    TempDir dir("luma_p3_readonly");
    const fs::path pluginsRoot = dir.root / "discovered";
    WritePlugin(pluginsRoot, "Luma.Tool", Manifest("Luma.Tool", "[]", false));

    CompiledPluginTable table;
    RegisterEmpty(table, "Luma.Tool");
    PluginManager mgr(table);
    const fs::path roots[] = {pluginsRoot};
    mgr.Discover(roots);
    Project project = MakeProject(dir.root, "ReadOnlyGame");
    mgr.ApplyProject(&project);

    fs::permissions(project.ProjectFile(), kWrite, fs::perm_options::remove);
    REQUIRE_FALSE(project.IsWritable());
    auto plan = mgr.PlanEnable("Luma.Tool");
    std::string error;
    REQUIRE_FALSE(mgr.Commit(project, plan, &error));
    REQUIRE(error.find("read-only") != std::string::npos);
    fs::permissions(project.ProjectFile(), kWrite, fs::perm_options::add);
}
