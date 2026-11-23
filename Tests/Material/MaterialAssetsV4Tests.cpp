// v4 graph assets: graph materials (.lmat v4), instances v2 (.lmatinst),
// material functions (.lmatfunc), parameter collections (.lmpc) — round
// trips through files and the MaterialLibrary graph-asset API.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Material/GraphAssets.h"
#include "Luma/Material/MaterialLibrary.h"

namespace fs = std::filesystem;
using namespace Luma;
using namespace Luma::Material;
using Luma::MaterialGraph::Graph;
using Luma::MaterialGraph::NodeId;
using Luma::MaterialGraph::PropertyValue;

namespace {
struct TempProject {
    fs::path root = fs::temp_directory_path() / "luma_v4_assets_test";
    AssetPathResolver paths;
    TempProject() {
        fs::remove_all(root);
        fs::create_directories(root / "Assets" / "Materials");
        paths.SetGameRoot(root / "Assets");
        paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
    }
    ~TempProject() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path File(const char* rel) const { return root / "Assets" / rel; }
};

GraphMaterial SampleMaterial() {
    GraphMaterial m;
    m.name = "M_Sample";
    m.settings.blendMode = BlendMode::Translucent;
    m.settings.shadingModel = ShadingModel::ClearCoat;
    m.settings.twoSided = true;
    m.settings.refraction = RefractionMethod::IndexOfRefraction;
    m.settings.translucencyLighting = TranslucencyLighting::SurfacePerPixel;
    m.settings.ppPriority = 3;
    const NodeId c = m.graph.AddNode("Constant", {-300, 10});
    m.graph.Find(c)->properties["value"] = PropertyValue(2.0);
    const NodeId mul = m.graph.AddNode("Multiply", {-150, 10});
    m.graph.Find(mul)->properties["in.B"] = PropertyValue(3.0);
    m.graph.Find(mul)->comment = "x3";
    const NodeId v = m.graph.AddNode("VectorParameter", {-300, 120});
    m.graph.Find(v)->properties["name"] = PropertyValue(std::string("Tint"));
    m.graph.Find(v)->properties["default"] = PropertyValue(Math::Vec4{0.1f, 0.2f, 0.3f, 1.0f});
    m.graph.Find(v)->properties["useSlider"] = PropertyValue(true);
    m.graph.Find(v)->properties["sortPriority"] = PropertyValue(i64{7});
    std::string err;
    REQUIRE(m.graph.Connect({c, 0}, {mul, 0}, &err));
    REQUIRE(m.graph.Connect({mul, 0}, {m.graph.OutputNode(), 3}, &err));
    REQUIRE(m.graph.Connect({v, 0}, {m.graph.OutputNode(), 0}, &err));
    m.graph.AddComment({-400, -50}, {300, 250}, "Inputs");
    return m;
}

void RequireSameGraph(const Graph& a, const Graph& b) {
    REQUIRE(a.Nodes().size() == b.Nodes().size());
    for (const auto& n : a.Nodes()) {
        const auto* m = b.Find(n.id);
        REQUIRE(m);
        CHECK(m->type == n.type);
        CHECK(m->position.x == n.position.x);
        CHECK(m->position.y == n.position.y);
        CHECK(m->comment == n.comment);
        REQUIRE(m->properties.size() == n.properties.size());
        for (const auto& [k, val] : n.properties) {
            INFO(k);
            REQUIRE(m->properties.count(k));
            CHECK(m->properties.at(k) == val);
        }
    }
    REQUIRE(a.Links().size() == b.Links().size());
    for (const auto& l : a.Links()) {
        auto src = b.Source(l.to);
        REQUIRE(src);
        CHECK(*src == l.from);
    }
    REQUIRE(a.Comments().size() == b.Comments().size());
    CHECK(b.Comments()[0].text == a.Comments()[0].text);
    CHECK(b.OutputNode() == a.OutputNode());
    CHECK(b.NextId() == a.NextId());
}
}  // namespace

TEST_CASE("Graph material round-trips through a v4 file", "[material][v4]") {
    TempProject p;
    const GraphMaterial m = SampleMaterial();
    std::string err;
    REQUIRE(GraphAssetSerializer::Save(m, p.File("Materials/M_Sample.lmat"), &err));
    CHECK(GraphAssetSerializer::ProbeVersion(p.File("Materials/M_Sample.lmat")) == 4);
    GraphMaterial loaded;
    REQUIRE(GraphAssetSerializer::Load(loaded, p.File("Materials/M_Sample.lmat"), &err));
    CHECK(loaded.name == "M_Sample");
    CHECK(loaded.settings.blendMode == BlendMode::Translucent);
    CHECK(loaded.settings.shadingModel == ShadingModel::ClearCoat);
    CHECK(loaded.settings.twoSided);
    CHECK(loaded.settings.refraction == RefractionMethod::IndexOfRefraction);
    CHECK(loaded.settings.ppPriority == 3);
    RequireSameGraph(m.graph, loaded.graph);
}

TEST_CASE("Instance v2, function and collection round-trip", "[material][v4]") {
    TempProject p;
    std::string err;

    GraphMaterialInstance inst;
    inst.name = "MI_Blue";
    inst.parent = "/Game/Materials/M_Sample";
    inst.Set({.name = "Tint", .kind = ParameterOverride::Kind::Vector, .vector = {0, 0, 1, 1}});
    inst.Set({.name = "Roughness", .kind = ParameterOverride::Kind::Scalar, .scalar = 0.2f});
    inst.Set({.name = "Albedo", .kind = ParameterOverride::Kind::Texture, .texture = "/Game/T_Blue"});
    inst.Set({.name = "UseDetail", .kind = ParameterOverride::Kind::StaticSwitch, .staticSwitch = true});
    inst.twoSided = false;
    REQUIRE(GraphAssetSerializer::Save(inst, p.File("Materials/MI_Blue.lmatinst"), &err));
    GraphMaterialInstance inst2;
    REQUIRE(GraphAssetSerializer::Load(inst2, p.File("Materials/MI_Blue.lmatinst"), &err));
    CHECK(inst2.parent == inst.parent);
    REQUIRE(inst2.overrides.size() == 4);
    CHECK(inst2.Find("Tint")->vector.z == 1.0f);
    CHECK(inst2.Find("Roughness")->scalar == 0.2f);
    CHECK(inst2.Find("Albedo")->texture == "/Game/T_Blue");
    CHECK(inst2.Find("UseDetail")->staticSwitch);
    CHECK(inst2.twoSided == std::optional<bool>(false));
    CHECK_FALSE(inst2.opacityMaskClip.has_value());
    CHECK(inst2.Remove("Roughness"));
    CHECK(inst2.Find("Roughness") == nullptr);

    MaterialFunction fn;
    fn.name = "MF_Double";
    fn.description = "Doubles its input";
    fn.category = "Math";
    const NodeId in = fn.graph.AddNode("FunctionInput", {-200, 0});
    fn.graph.Find(in)->properties["name"] = PropertyValue(std::string("X"));
    const NodeId out = fn.graph.AddNode("FunctionOutput", {0, 0});
    REQUIRE(fn.graph.Connect({in, 0}, {out, 0}, &err));
    REQUIRE(GraphAssetSerializer::Save(fn, p.File("Materials/MF_Double.lmatfunc"), &err));
    MaterialFunction fn2;
    REQUIRE(GraphAssetSerializer::Load(fn2, p.File("Materials/MF_Double.lmatfunc"), &err));
    CHECK(fn2.description == "Doubles its input");
    CHECK(fn2.category == "Math");
    CHECK(fn2.exposeToLibrary);
    REQUIRE(fn2.graph.Nodes().size() == fn.graph.Nodes().size());
    CHECK(fn2.graph.Source({out, 0})->node == in);

    ParameterCollection pc;
    pc.name = "MPC_Wind";
    pc.scalars.push_back({"Strength", 0.7f});
    pc.vectors.push_back({"Direction", {1, 0, 0, 0}});
    REQUIRE(GraphAssetSerializer::Save(pc, p.File("Materials/MPC_Wind.lmpc"), &err));
    ParameterCollection pc2;
    REQUIRE(GraphAssetSerializer::Load(pc2, p.File("Materials/MPC_Wind.lmpc"), &err));
    REQUIRE(pc2.scalars.size() == 1);
    CHECK(pc2.scalars[0].name == "Strength");
    CHECK(pc2.scalars[0].value == 0.7f);
    CHECK(pc2.vectors[0].value.x == 1.0f);
}

TEST_CASE("A v3 .lmat file is detected as version 3", "[material][v4]") {
    TempProject p;
    std::ofstream(p.File("Materials/M_Old.lmat"))
        << R"({"version": 3, "type": "Material", "name": "M_Old", "settings": {}, "inputs": {}})";
    CHECK(GraphAssetSerializer::ProbeVersion(p.File("Materials/M_Old.lmat")) == 3);
    CHECK(GraphAssetSerializer::ProbeVersion(p.File("Materials/Missing.lmat")) == 0);
}

TEST_CASE("Malformed graph files report errors", "[material][v4]") {
    TempProject p;
    std::ofstream(p.File("Materials/M_Bad.lmat"))
        << R"({"version": 4, "type": "Material", "name": "M_Bad", "graph": {"nodes": [{"id": 0, "type": "Add"}]}})";
    GraphMaterial m;
    std::string err;
    CHECK_FALSE(GraphAssetSerializer::Load(m, p.File("Materials/M_Bad.lmat"), &err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("Library serves graph assets, collection values and dependents", "[material][v4]") {
    TempProject p;
    MaterialLibrary lib(p.paths);
    std::string err;

    MaterialFunction fn;
    fn.name = "MF_Wind";
    fn.graph.AddNode("FunctionOutput", {0, 0});
    lib.SetFunction("/Game/Materials/MF_Wind", fn);
    REQUIRE(lib.SaveGraphAsset("/Game/Materials/MF_Wind", &err));
    CHECK(fs::exists(p.File("Materials/MF_Wind.lmatfunc")));

    ParameterCollection pc;
    pc.name = "MPC_Wind";
    pc.scalars.push_back({"Strength", 0.5f});
    lib.SetCollection("/Game/Materials/MPC_Wind", pc);
    REQUIRE(lib.SaveGraphAsset("/Game/Materials/MPC_Wind", &err));

    GraphMaterial m = GraphMaterial::MakeDefault();
    m.name = "M_Foliage";
    const NodeId call = m.graph.AddNode("MaterialFunctionCall", {-200, 0});
    m.graph.Find(call)->properties["function"] =
        PropertyValue(std::string("/Game/Materials/MF_Wind"));
    lib.SetGraphMaterial("/Game/Materials/M_Foliage", m);
    REQUIRE(lib.SaveGraphAsset("/Game/Materials/M_Foliage", &err));

    // A fresh library loads everything from disk.
    MaterialLibrary lib2(p.paths);
    REQUIRE(lib2.GetFunction("/Game/Materials/MF_Wind"));
    REQUIRE(lib2.GetGraphMaterial("/Game/Materials/M_Foliage"));
    const ParameterCollection* loaded = lib2.GetCollection("/Game/Materials/MPC_Wind");
    REQUIRE(loaded);
    const u32 rev = loaded->revision;
    CHECK(lib2.SetCollectionValue("/Game/Materials/MPC_Wind", "Strength", {0.9f, 0, 0, 0}));
    CHECK(lib2.GetCollection("/Game/Materials/MPC_Wind")->scalars[0].value == 0.9f);
    CHECK(lib2.GetCollection("/Game/Materials/MPC_Wind")->revision == rev + 1);
    CHECK_FALSE(lib2.SetCollectionValue("/Game/Materials/MPC_Wind", "Nope", {}));
    const auto deps = lib2.Dependents("/Game/Materials/MF_Wind");
    REQUIRE(deps.size() == 1);
    CHECK(deps[0] == "/Game/Materials/M_Foliage");

    // Transitive: function -> material -> instance -> child instance.
    GraphMaterialInstance mi;
    mi.parent = "/Game/Materials/M_Foliage";
    lib2.SetGraphInstance("/Game/Materials/MI_Foliage", mi);
    GraphMaterialInstance child;
    child.parent = "/Game/Materials/MI_Foliage";
    lib2.SetGraphInstance("/Game/Materials/MI_Foliage_Red", child);
    GraphMaterialInstance unrelated;
    unrelated.parent = "/Game/Materials/M_Other";
    lib2.SetGraphInstance("/Game/Materials/MI_Other", unrelated);
    const auto all = lib2.AllDependents("/Game/Materials/MF_Wind");
    REQUIRE(all.size() == 3);
    CHECK(all[0] == "/Game/Materials/MI_Foliage");
    CHECK(all[1] == "/Game/Materials/MI_Foliage_Red");
    CHECK(all[2] == "/Game/Materials/M_Foliage");
    CHECK(lib2.AllDependents("/Game/Materials/MI_Foliage_Red").empty());
    // A cycle (instance chains are validated, but a bad file can contain one)
    // terminates and never lists the root itself.
    GraphMaterialInstance loopA;
    loopA.parent = "/Game/Materials/MI_LoopB";
    GraphMaterialInstance loopB;
    loopB.parent = "/Game/Materials/MI_LoopA";
    lib2.SetGraphInstance("/Game/Materials/MI_LoopA", loopA);
    lib2.SetGraphInstance("/Game/Materials/MI_LoopB", loopB);
    const auto loop = lib2.AllDependents("/Game/Materials/MI_LoopA");
    REQUIRE(loop.size() == 1);
    CHECK(loop[0] == "/Game/Materials/MI_LoopB");
}

TEST_CASE("The default graph material exposes BaseColor and Roughness parameters", "[material][v4]") {
    const GraphMaterial m = GraphMaterial::MakeDefault();
    const auto& g = m.graph;
    auto base = g.Source({g.OutputNode(), 0});
    REQUIRE(base);
    const auto* bc = g.Find(base->node);
    REQUIRE(bc);
    CHECK(bc->type == "VectorParameter");
    CHECK(bc->properties.at("name").AsString() == "BaseColor");
    CHECK(bc->properties.at("default").AsVec4().x == 0.82f);
    auto rough = g.Source({g.OutputNode(), 3});
    REQUIRE(rough);
    CHECK(g.Find(rough->node)->type == "ScalarParameter");
    CHECK(g.Find(rough->node)->properties.at("default").AsFloat() == 0.5);
}
