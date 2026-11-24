#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Luma/Core/Version.h"
#include "Luma/Plugins/PluginManager.h"
#include "Luma/Scene/ComponentRegistry.h"
#include "Luma/Scene/Scene.h"

namespace fs = std::filesystem;
using Luma::ComponentDesc;
using Luma::ComponentRegistry;
using Luma::EngineVersion;
using Luma::Entity;
using Luma::Scene;
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
        fs::remove_all(root, ec);
    }
};

std::string CompatibleRange() {
    const Luma::SemVer v = EngineVersion();
    return ">=" + v.ToString() + " <" + std::to_string(v.major + 1) + ".0.0";
}

ComponentDesc DummyComponent(const std::string& id, const std::string& owner) {
    ComponentDesc d;
    d.id = id;
    d.displayName = id;
    d.category = "Tools";
    d.owner = owner;
    d.add = [](Scene&, Entity) {};
    d.remove = [](Scene&, Entity) {};
    d.has = [](const Scene&, Entity) { return false; };
    d.write = [](const Scene&, Entity, Luma::SerialValue&) {};
    d.read = [](Scene&, Entity, const Luma::SerialValue&) {};
    d.reset = [](Scene&, Entity) {};
    return d;
}

std::vector<std::string> ComponentIds(const ComponentRegistry& registry) {
    std::vector<std::string> ids;
    for (const Luma::ComponentDesc* d : registry.All()) ids.push_back(d->id);
    return ids;
}

struct Recorder {
    std::vector<std::string> started;
    std::vector<std::string> stopped;
};

class TestPlugin final : public IPlugin {
public:
    TestPlugin(std::string id, Recorder& rec, bool fail, bool throwsEx)
        : m_id(std::move(id)), m_rec(rec), m_fail(fail), m_throws(throwsEx) {}

    bool Startup(PluginContext& ctx, std::string& error) override {
        m_rec.started.push_back(m_id);
        if (ctx.Components()) {
            std::string err;
            ctx.Track(ctx.Components()->Register(
                DummyComponent("Comp." + m_id, m_id), &err));
            if (!err.empty()) {
                error = err;
                return false;
            }
        }
        if (m_throws) throw std::runtime_error(m_id + " exploded");
        if (m_fail) {
            error = m_id + " refused to start";
            return false;
        }
        return true;
    }

    void Shutdown(PluginContext& ctx) override {
        LUMA_UNUSED(ctx);
        m_rec.stopped.push_back(m_id);
    }

private:
    std::string m_id;
    Recorder& m_rec;
    bool m_fail = false;
    bool m_throws = false;
};

std::string Manifest(const std::string& id, const std::string& depsJson,
                     const std::string& version = "1.0.0",
                     const std::string& kind = "Runtime") {
    return std::format(
        "{{\n"
        "  \"schema\": 1,\n"
        "  \"id\": \"{}\",\n"
        "  \"displayName\": \"{}\",\n"
        "  \"category\": \"Tools\",\n"
        "  \"author\": \"Luma\",\n"
        "  \"version\": \"{}\",\n"
        "  \"engineVersionRange\": \"{}\",\n"
        "  \"kind\": \"{}\",\n"
        "  \"platforms\": [\"{}\"],\n"
        "  \"dependencies\": {}\n"
        "}}\n",
        id, id, version, CompatibleRange(), kind, HostPlatform(), depsJson);
}

void WritePlugin(const fs::path& root, const std::string& id, const std::string& json) {
    const fs::path dir = root / id;
    fs::create_directories(dir);
    std::ofstream out(dir / (id + ".lplugin"), std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out << json;
}

void Register(CompiledPluginTable& table, const std::string& id, Recorder& rec,
              bool fail = false, bool throwsEx = false) {
    REQUIRE(table.Register(id, [&rec, id, fail, throwsEx] {
        return std::make_unique<TestPlugin>(id, rec, fail, throwsEx);
    }));
}

const Luma::Plugins::PluginStatus* Need(PluginManager& mgr, const char* id) {
    const auto* s = mgr.Find(id);
    REQUIRE(s);
    return s;
}

}  // namespace

TEST_CASE("StartAll loads A then B then C for a required chain", "[plugins][lifecycle]") {
    TempDir root("luma_p2_chain");
    WritePlugin(root.root, "Luma.A", Manifest("Luma.A", "[]"));
    WritePlugin(root.root, "Luma.B",
                Manifest("Luma.B", R"([{"id":"Luma.A","version":">=1.0.0"}])"));
    WritePlugin(root.root, "Luma.C",
                Manifest("Luma.C", R"([{"id":"Luma.B","version":">=1.0.0"}])"));

    Recorder rec;
    CompiledPluginTable table;
    Register(table, "Luma.A", rec);
    Register(table, "Luma.B", rec);
    Register(table, "Luma.C", rec);

    ComponentRegistry components;
    const auto before = ComponentIds(components);
    PluginManager mgr(table);
    mgr.SetRegistries(&components);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    REQUIRE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(rec.started == std::vector<std::string>{"Luma.A", "Luma.B", "Luma.C"});
    REQUIRE(mgr.LastStartOrder() == rec.started);
    REQUIRE(Need(mgr, "Luma.A")->state == PluginState::Started);
    REQUIRE(Need(mgr, "Luma.C")->state == PluginState::Started);
    REQUIRE(components.Find("Comp.Luma.A"));
    REQUIRE(components.Find("Comp.Luma.C"));

    mgr.ShutdownAll();
    REQUIRE(rec.stopped == std::vector<std::string>{"Luma.C", "Luma.B", "Luma.A"});
    REQUIRE(mgr.LastShutdownOrder() == rec.stopped);
    REQUIRE(ComponentIds(components) == before);
    REQUIRE(Need(mgr, "Luma.A")->state == PluginState::Stopped);
}

TEST_CASE("Diamond dependencies start the shared root first, then B, C, D",
          "[plugins][lifecycle]") {
    TempDir root("luma_p2_diamond");
    WritePlugin(root.root, "Luma.A", Manifest("Luma.A", "[]"));
    WritePlugin(root.root, "Luma.B",
                Manifest("Luma.B", R"([{"id":"Luma.A","version":"1.0.0"}])"));
    WritePlugin(root.root, "Luma.C",
                Manifest("Luma.C", R"([{"id":"Luma.A","version":"1.0.0"}])"));
    WritePlugin(root.root, "Luma.D",
                Manifest("Luma.D",
                         R"([{"id":"Luma.B","version":"1.0.0"},{"id":"Luma.C","version":"1.0.0"}])"));

    Recorder rec;
    CompiledPluginTable table;
    Register(table, "Luma.A", rec);
    Register(table, "Luma.B", rec);
    Register(table, "Luma.C", rec);
    Register(table, "Luma.D", rec);

    ComponentRegistry components;
    PluginManager mgr(table);
    mgr.SetRegistries(&components);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    REQUIRE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(rec.started ==
            std::vector<std::string>{"Luma.A", "Luma.B", "Luma.C", "Luma.D"});
}

TEST_CASE("Optional dependency missing still starts the plugin", "[plugins][lifecycle]") {
    TempDir root("luma_p2_opt_ok");
    WritePlugin(root.root, "Luma.A",
                Manifest("Luma.A",
                         R"([{"id":"Luma.B","version":">=1.0.0","optional":true}])"));

    Recorder rec;
    CompiledPluginTable table;
    Register(table, "Luma.A", rec);

    ComponentRegistry components;
    PluginManager mgr(table);
    mgr.SetRegistries(&components);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    REQUIRE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(Need(mgr, "Luma.A")->state == PluginState::Started);
    REQUIRE_FALSE(mgr.Find("Luma.B"));
}

TEST_CASE("Required missing dependency is Unavailable", "[plugins][lifecycle]") {
    TempDir root("luma_p2_req_missing");
    WritePlugin(root.root, "Luma.A",
                Manifest("Luma.A", R"([{"id":"Luma.B","version":">=1.0.0"}])"));

    Recorder rec;
    CompiledPluginTable table;
    Register(table, "Luma.A", rec);

    PluginManager mgr(table);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    REQUIRE_FALSE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(Need(mgr, "Luma.A")->state == PluginState::Unavailable);
    REQUIRE_FALSE(Need(mgr, "Luma.A")->diagnostics.empty());
    REQUIRE(Need(mgr, "Luma.A")->diagnostics.back().find("not installed") !=
            std::string::npos);
    REQUIRE(rec.started.empty());
}

TEST_CASE("Required version mismatch is Unavailable", "[plugins][lifecycle]") {
    TempDir root("luma_p2_ver");
    WritePlugin(root.root, "Luma.B", Manifest("Luma.B", "[]", "1.0.0"));
    WritePlugin(root.root, "Luma.A",
                Manifest("Luma.A", R"([{"id":"Luma.B","version":">=2.0.0"}])"));

    Recorder rec;
    CompiledPluginTable table;
    Register(table, "Luma.A", rec);
    Register(table, "Luma.B", rec);

    PluginManager mgr(table);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    REQUIRE_FALSE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(Need(mgr, "Luma.A")->state == PluginState::Unavailable);
    REQUIRE(Need(mgr, "Luma.A")->diagnostics.back().find("1.0.0") != std::string::npos);
    REQUIRE(Need(mgr, "Luma.B")->state == PluginState::Started);
}

TEST_CASE("Two-node and three-node dependency cycles are Unavailable",
          "[plugins][lifecycle]") {
    {
        TempDir root("luma_p2_cyc2");
        WritePlugin(root.root, "Luma.A",
                    Manifest("Luma.A", R"([{"id":"Luma.B","version":"1.0.0"}])"));
        WritePlugin(root.root, "Luma.B",
                    Manifest("Luma.B", R"([{"id":"Luma.A","version":"1.0.0"}])"));
        Recorder rec;
        CompiledPluginTable table;
        Register(table, "Luma.A", rec);
        Register(table, "Luma.B", rec);
        PluginManager mgr(table);
        const fs::path roots[] = {root.root};
        mgr.Discover(roots);
        mgr.ApplyProject(nullptr);
        REQUIRE_FALSE(mgr.StartAll(PluginHostKind::Runtime));
        REQUIRE(Need(mgr, "Luma.A")->state == PluginState::Unavailable);
        REQUIRE(Need(mgr, "Luma.B")->state == PluginState::Unavailable);
        REQUIRE(Need(mgr, "Luma.A")->diagnostics.back().find("cycle") != std::string::npos);
        REQUIRE(Need(mgr, "Luma.A")->diagnostics.back().find("Luma.A") != std::string::npos);
        REQUIRE(Need(mgr, "Luma.A")->diagnostics.back().find("Luma.B") != std::string::npos);
        REQUIRE(rec.started.empty());
    }
    {
        TempDir root("luma_p2_cyc3");
        WritePlugin(root.root, "Luma.A",
                    Manifest("Luma.A", R"([{"id":"Luma.B","version":"1.0.0"}])"));
        WritePlugin(root.root, "Luma.B",
                    Manifest("Luma.B", R"([{"id":"Luma.C","version":"1.0.0"}])"));
        WritePlugin(root.root, "Luma.C",
                    Manifest("Luma.C", R"([{"id":"Luma.A","version":"1.0.0"}])"));
        Recorder rec;
        CompiledPluginTable table;
        Register(table, "Luma.A", rec);
        Register(table, "Luma.B", rec);
        Register(table, "Luma.C", rec);
        PluginManager mgr(table);
        const fs::path roots[] = {root.root};
        mgr.Discover(roots);
        mgr.ApplyProject(nullptr);
        REQUIRE_FALSE(mgr.StartAll(PluginHostKind::Runtime));
        REQUIRE(Need(mgr, "Luma.A")->diagnostics.back().find("Luma.C") != std::string::npos);
        REQUIRE(rec.started.empty());
    }
}

TEST_CASE("Failed Startup rolls back only that plugin and skips dependents",
          "[plugins][lifecycle]") {
    TempDir root("luma_p2_fail");
    WritePlugin(root.root, "Luma.A", Manifest("Luma.A", "[]"));
    WritePlugin(root.root, "Luma.B",
                Manifest("Luma.B", R"([{"id":"Luma.A","version":"1.0.0"}])"));
    WritePlugin(root.root, "Luma.C",
                Manifest("Luma.C", R"([{"id":"Luma.B","version":"1.0.0"}])"));
    WritePlugin(root.root, "Luma.Q", Manifest("Luma.Q", "[]"));

    Recorder rec;
    CompiledPluginTable table;
    Register(table, "Luma.A", rec);
    Register(table, "Luma.B", rec, /*fail=*/true);
    Register(table, "Luma.C", rec);
    Register(table, "Luma.Q", rec);

    ComponentRegistry components;
    const auto before = ComponentIds(components);
    PluginManager mgr(table);
    mgr.SetRegistries(&components);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    REQUIRE_FALSE(mgr.StartAll(PluginHostKind::Runtime));

    REQUIRE(Need(mgr, "Luma.A")->state == PluginState::Started);
    REQUIRE(Need(mgr, "Luma.B")->state == PluginState::Failed);
    REQUIRE(Need(mgr, "Luma.C")->state == PluginState::Unavailable);
    REQUIRE(Need(mgr, "Luma.Q")->state == PluginState::Started);
    REQUIRE_FALSE(components.Find("Comp.Luma.B"));
    REQUIRE(components.Find("Comp.Luma.A"));
    REQUIRE(components.Find("Comp.Luma.Q"));
    REQUIRE_FALSE(std::find(rec.started.begin(), rec.started.end(), "Luma.C") !=
                  rec.started.end());

    mgr.ShutdownAll();
    REQUIRE(ComponentIds(components) == before);
}

TEST_CASE("Thrown Startup is Failed and rolls back registrations", "[plugins][lifecycle]") {
    TempDir root("luma_p2_throw");
    WritePlugin(root.root, "Luma.A", Manifest("Luma.A", "[]"));

    Recorder rec;
    CompiledPluginTable table;
    Register(table, "Luma.A", rec, /*fail=*/false, /*throwsEx=*/true);

    ComponentRegistry components;
    const auto before = ComponentIds(components);
    PluginManager mgr(table);
    mgr.SetRegistries(&components);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    REQUIRE_FALSE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(Need(mgr, "Luma.A")->state == PluginState::Failed);
    REQUIRE(Need(mgr, "Luma.A")->diagnostics.back().find("exploded") != std::string::npos);
    REQUIRE(ComponentIds(components) == before);
}

TEST_CASE("Start, shutdown, start again does not duplicate component registrations",
          "[plugins][lifecycle]") {
    TempDir root("luma_p2_restart");
    WritePlugin(root.root, "Luma.A", Manifest("Luma.A", "[]"));

    Recorder rec;
    CompiledPluginTable table;
    Register(table, "Luma.A", rec);

    ComponentRegistry components;
    PluginManager mgr(table);
    mgr.SetRegistries(&components);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    REQUIRE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(components.Find("Comp.Luma.A"));
    const Luma::u64 firstRev = components.Revision();
    mgr.ShutdownAll();
    REQUIRE_FALSE(components.Find("Comp.Luma.A"));
    REQUIRE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(components.Find("Comp.Luma.A"));
    REQUIRE(rec.started.size() == 2);
    REQUIRE(table.InstantiateCount() == 2);
    Luma::usize count = 0;
    for (const auto* d : components.All()) {
        if (d->id == "Comp.Luma.A") ++count;
    }
    REQUIRE(count == 1);
    REQUIRE(components.Revision() != firstRev);
}

TEST_CASE("Compiled-out plugin is Unavailable", "[plugins][lifecycle]") {
    TempDir root("luma_p2_missing");
    WritePlugin(root.root, "Luma.Missing", Manifest("Luma.Missing", "[]"));
    CompiledPluginTable table;
    PluginManager mgr(table);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    REQUIRE_FALSE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(Need(mgr, "Luma.Missing")->state == PluginState::Unavailable);
    REQUIRE(Need(mgr, "Luma.Missing")->diagnostics.back().find("not built") !=
            std::string::npos);
}

TEST_CASE("Editor-only plugin is Unavailable on Runtime and starts in Editor",
          "[plugins][lifecycle]") {
    TempDir root("luma_p2_edit");
    WritePlugin(root.root, "Luma.Edit", Manifest("Luma.Edit", "[]", "1.0.0", "Editor"));
    Recorder rec;
    CompiledPluginTable table;
    Register(table, "Luma.Edit", rec);
    PluginManager mgr(table);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    mgr.ApplyProject(nullptr);
    REQUIRE_FALSE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(Need(mgr, "Luma.Edit")->state == PluginState::Unavailable);
    REQUIRE(Need(mgr, "Luma.Edit")->diagnostics.back().find("Editor-only") !=
            std::string::npos);
    REQUIRE(rec.started.empty());
    REQUIRE(mgr.StartAll(PluginHostKind::Editor));
    REQUIRE(Need(mgr, "Luma.Edit")->state == PluginState::Started);
}

TEST_CASE("Discovery does not instantiate while StartAll does", "[plugins][lifecycle]") {
    TempDir root("luma_p2_noexec");
    WritePlugin(root.root, "Luma.A", Manifest("Luma.A", "[]"));
    Recorder rec;
    CompiledPluginTable table;
    Register(table, "Luma.A", rec);
    PluginManager mgr(table);
    const fs::path roots[] = {root.root};
    mgr.Discover(roots);
    REQUIRE(table.InstantiateCount() == 0);
    mgr.ApplyProject(nullptr);
    REQUIRE(mgr.StartAll(PluginHostKind::Runtime));
    REQUIRE(table.InstantiateCount() == 1);
}
