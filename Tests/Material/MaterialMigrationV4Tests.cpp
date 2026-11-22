// Phase-1 (.lmat v3 / .lmatinst v1) -> graph material (v4) / instance (v2).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "Luma/Material/MaterialMigrationV4.h"
#include "Luma/MaterialCompiler/Preshader.h"
#include "Luma/MaterialCompiler/Translator.h"
#include "Luma/MaterialGraph/ExpressionRegistry.h"

using namespace Luma;
using namespace Luma::Material;
using Luma::MaterialGraph::Graph;
using Luma::MaterialGraph::Node;
using Luma::MaterialGraph::NodeId;

namespace {

u32 OutputPin(const char* name) {
    const auto* d = MaterialGraph::ExpressionRegistry::Get().Find("MaterialOutput");
    for (u32 i = 0; i < d->inputs.size(); ++i) {
        if (d->inputs[i].name == name) return i;
    }
    FAIL("no output pin " << name);
    return 0;
}

const Node* Feed(const Graph& g, NodeId node, u32 pin) {
    auto src = g.Source({node, pin});
    return src ? g.Find(src->node) : nullptr;
}

const Node* Param(const Graph& g, const char* type, const char* name) {
    for (const Node& n : g.Nodes()) {
        if (n.type == type && n.properties.count("name") && n.properties.at("name").AsString() == name) {
            return &n;
        }
    }
    return nullptr;
}

usize Count(const Graph& g, const char* type) {
    return static_cast<usize>(std::count_if(g.Nodes().begin(), g.Nodes().end(),
                                            [type](const Node& n) { return n.type == type; }));
}

// All nodes reachable (upstream) from `start`.
bool Upstream(const Graph& g, NodeId start, const char* type) {
    std::vector<NodeId> stack{start};
    std::vector<NodeId> seen;
    while (!stack.empty()) {
        const NodeId n = stack.back();
        stack.pop_back();
        if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
        seen.push_back(n);
        if (g.Find(n)->type == type) return true;
        for (const auto& l : g.Links()) {
            if (l.to.node == n) stack.push_back(l.from.node);
        }
    }
    return false;
}

}  // namespace

TEST_CASE("Constant inputs become switchable parameters", "[material][migration]") {
    Luma::Material::Material old = Luma::Material::Material::MakeDefault();
    old.Input(MaterialParameter::Roughness).constant = {0.3f, 0, 0, 0};
    std::vector<std::string> notes;
    const GraphMaterial m = MigrateV3(old, &notes);
    const Graph& g = m.graph;
    const Node* sw = Feed(g, g.OutputNode(), OutputPin("Roughness"));
    REQUIRE(sw);
    CHECK(sw->type == "StaticSwitchParameter");
    CHECK(sw->properties.at("name").AsString() == "Use Roughness Texture");
    CHECK_FALSE(sw->properties.at("default").AsBool());
    const Node* constant = Feed(g, sw->id, 1);  // False
    REQUIRE(constant);
    CHECK(constant->type == "ScalarParameter");
    CHECK(constant->properties.at("name").AsString() == "Roughness");
    CHECK(constant->properties.at("default").AsFloat() == Catch::Approx(0.3));
    REQUIRE(Param(g, "TextureSampleParameter2D", "Roughness Texture"));
    CHECK(Param(g, "TextureSampleParameter2D", "Roughness Texture")->properties.at("texture").AsString().empty());
    CHECK(Upstream(g, sw->id, "TextureSampleParameter2D"));
    CHECK_FALSE(notes.empty());
}

TEST_CASE("A textured input keeps its texture, tint and tiling as parameters", "[material][migration]") {
    Luma::Material::Material old = Luma::Material::Material::MakeDefault();
    MaterialInput& bc = old.Input(MaterialParameter::BaseColor);
    bc.texture = "/Game/Textures/T_Wood_D";
    bc.tint = {0.9f, 0.8f, 0.7f, 1.0f};
    bc.uvTiling = {4.0f, 4.0f};
    bc.uvOffset = {0.25f, 0.0f};
    const GraphMaterial m = MigrateV3(old, nullptr);
    const Graph& g = m.graph;
    const Node* sw = Feed(g, g.OutputNode(), OutputPin("BaseColor"));
    REQUIRE(sw);
    CHECK(sw->properties.at("default").AsBool());
    const Node* sample = Param(g, "TextureSampleParameter2D", "Base Color Texture");
    REQUIRE(sample);
    CHECK(sample->properties.at("texture").AsString() == "/Game/Textures/T_Wood_D");
    CHECK(sample->properties.at("sampler").AsInt() == 0);  // Color
    const Node* tint = Param(g, "VectorParameter", "Base Color Tint");
    REQUIRE(tint);
    CHECK(tint->properties.at("default").AsVec4().y == 0.8f);
    const Node* tiling = Param(g, "VectorParameter", "Base Color Tiling");
    REQUIRE(tiling);
    CHECK(tiling->properties.at("default").AsVec4().x == 4.0f);
    const Node* offset = Param(g, "VectorParameter", "Base Color Offset");
    REQUIRE(offset);
    CHECK(offset->properties.at("default").AsVec4().x == 0.25f);
    CHECK(Upstream(g, sample->id, "TextureCoordinate"));
    CHECK(Upstream(g, sample->id, "VectorParameter"));  // tiling/offset feed the UVs
}

TEST_CASE("Channel selection and invert are kept", "[material][migration]") {
    Luma::Material::Material old = Luma::Material::Material::MakeDefault();
    MaterialInput& r = old.Input(MaterialParameter::Roughness);
    r.texture = "/Game/Textures/T_ORM";
    r.channel = TextureChannel::G;
    r.invert = true;
    const GraphMaterial m = MigrateV3(old, nullptr);
    const Graph& g = m.graph;
    const Node* sw = Feed(g, g.OutputNode(), OutputPin("Roughness"));
    REQUIRE(sw);
    CHECK(Upstream(g, sw->id, "OneMinus"));
    const Node* sample = Param(g, "TextureSampleParameter2D", "Roughness Texture");
    REQUIRE(sample);
    CHECK(sample->properties.at("sampler").AsInt() == 1);  // Linear (data)
    // The G channel is read from the sample's G output pin (index 2).
    bool fromG = false;
    for (const auto& l : g.Links()) {
        if (l.from.node == sample->id && l.from.pin == 2) fromG = true;
    }
    CHECK(fromG);
}

TEST_CASE("Normal, emissive, height, bump and cavity convert to their node chains", "[material][migration]") {
    Luma::Material::Material old = Luma::Material::Material::MakeDefault();
    old.Input(MaterialParameter::Normal).texture = "/Game/T_N";
    old.Input(MaterialParameter::Normal).normalStrength = 2.0f;
    old.Input(MaterialParameter::Normal).flipGreen = true;
    old.Input(MaterialParameter::EmissiveColor).constant = {1, 0.5f, 0, 0};
    old.Input(MaterialParameter::EmissiveColor).emissiveIntensity = 8.0f;
    old.Input(MaterialParameter::Height).texture = "/Game/T_H";
    old.Input(MaterialParameter::Height).normalStrength = 0.06f;
    old.Input(MaterialParameter::Bump).texture = "/Game/T_B";
    old.Input(MaterialParameter::Bump).normalStrength = 3.0f;
    old.Input(MaterialParameter::Cavity).texture = "/Game/T_C";
    const GraphMaterial m = MigrateV3(old, nullptr);
    const Graph& g = m.graph;
    CHECK(Count(g, "ParallaxOcclusionMapping") == 1);
    CHECK(Count(g, "NormalFromHeight") == 1);
    CHECK(Count(g, "BlendAngleCorrectedNormals") == 1);
    REQUIRE(Param(g, "ScalarParameter", "Normal Strength"));
    CHECK(Param(g, "ScalarParameter", "Normal Strength")->properties.at("default").AsFloat() == 2.0);
    REQUIRE(Param(g, "ScalarParameter", "Height Scale"));
    CHECK(Param(g, "ScalarParameter", "Height Scale")->properties.at("default").AsFloat() ==
          Catch::Approx(0.06));
    REQUIRE(Param(g, "ScalarParameter", "Bump Strength"));
    REQUIRE(Param(g, "ScalarParameter", "Emissive Intensity"));
    CHECK(Param(g, "ScalarParameter", "Emissive Intensity")->properties.at("default").AsFloat() == 8.0);
    CHECK(Param(g, "StaticSwitchParameter", "Use Height Texture")->properties.at("default").AsBool());
    // The single POM node feeds every texture sample's UVs.
    const Node* pom = nullptr;
    for (const Node& n : g.Nodes()) {
        if (n.type == "ParallaxOcclusionMapping") pom = &n;
    }
    REQUIRE(pom);
    const Node* bcSample = Param(g, "TextureSampleParameter2D", "Base Color Texture");
    REQUIRE(bcSample);
    CHECK(Upstream(g, bcSample->id, "ParallaxOcclusionMapping"));
    // Cavity multiplies ambient occlusion.
    const Node* ao = Feed(g, g.OutputNode(), OutputPin("AmbientOcclusion"));
    REQUIRE(ao);
    CHECK(ao->type == "Multiply");
    CHECK(Upstream(g, ao->id, "TextureSampleParameter2D"));
    CHECK(g.FindCycle().empty());
}

TEST_CASE("Settings are copied", "[material][migration]") {
    Luma::Material::Material old = Luma::Material::Material::MakeDefault();
    old.name = "M_Leaves";
    old.settings.blendMode = BlendMode::Masked;
    old.settings.twoSided = true;
    old.settings.opacityMaskClip = 0.4f;
    old.settings.shadingModel = ShadingModel::Unlit;
    const GraphMaterial m = MigrateV3(old, nullptr);
    CHECK(m.name == "M_Leaves");
    CHECK(m.settings.blendMode == BlendMode::Masked);
    CHECK(m.settings.twoSided);
    CHECK(m.settings.opacityMaskClip == 0.4f);
    CHECK(m.settings.shadingModel == ShadingModel::Unlit);
}

TEST_CASE("A v1 instance becomes named parameter overrides", "[material][migration]") {
    Luma::Material::Material parentV3 = Luma::Material::Material::MakeDefault();
    parentV3.Input(MaterialParameter::BaseColor).texture = "/Game/Textures/T_Checker_diff";
    const GraphMaterial parent = MigrateV3(parentV3, nullptr);

    MaterialInstance inst;
    inst.name = "MI_Checker_Blue";
    inst.parent = "/Game/Materials/M_Checker";
    MaterialInput bc = parentV3.Input(MaterialParameter::BaseColor);
    bc.tint = {0.35f, 0.55f, 1.0f, 1.0f};
    bc.uvTiling = {2.0f, 2.0f};
    inst.Override(MaterialParameter::BaseColor, bc);
    MaterialInput rough = parentV3.Input(MaterialParameter::Roughness);
    rough.constant = {0.2f, 0, 0, 0};
    inst.Override(MaterialParameter::Roughness, rough);
    inst.twoSided = true;
    std::vector<std::string> notes;
    const GraphMaterialInstance out = MigrateInstanceV1(inst, parentV3, &notes);
    CHECK(out.parent == inst.parent);
    REQUIRE(out.Find("Base Color Tint"));
    CHECK(out.Find("Base Color Tint")->vector.x == 0.35f);
    REQUIRE(out.Find("Base Color Tiling"));
    CHECK(out.Find("Base Color Tiling")->vector.x == 2.0f);
    REQUIRE(out.Find("Base Color Texture"));
    CHECK(out.Find("Base Color Texture")->texture == "/Game/Textures/T_Checker_diff");
    REQUIRE(out.Find("Use Base Color Texture"));
    CHECK(out.Find("Use Base Color Texture")->staticSwitch);
    REQUIRE(out.Find("Roughness"));
    CHECK(out.Find("Roughness")->scalar == 0.2f);
    CHECK(out.twoSided == std::optional<bool>(true));
    // Overrides that equal the parent are not written (keeps instances lean).
    CHECK(out.Find("Metallic") == nullptr);
}

TEST_CASE("Instance overrides of static structure are reported", "[material][migration]") {
    Luma::Material::Material parentV3 = Luma::Material::Material::MakeDefault();
    parentV3.Input(MaterialParameter::Roughness).texture = "/Game/T_ORM";
    MaterialInstance inst;
    MaterialInput r = parentV3.Input(MaterialParameter::Roughness);
    r.channel = TextureChannel::B;  // channel is graph structure, not a parameter
    inst.Override(MaterialParameter::Roughness, r);
    std::vector<std::string> notes;
    MigrateInstanceV1(inst, parentV3, &notes);
    const bool reported = std::any_of(notes.begin(), notes.end(), [](const std::string& n) {
        return n.find("Roughness") != std::string::npos && n.find("channel") != std::string::npos;
    });
    CHECK(reported);
}

#include <filesystem>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/Material/MaterialSerializer.h"
#include "Luma/Material/MaterialInstanceSerializer.h"

TEST_CASE("The library converts Phase-1 files when loaded as graph assets", "[material][migration]") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "luma_migration_lib_test";
    fs::remove_all(root);
    fs::create_directories(root / "Assets" / "Materials");
    AssetPathResolver paths;
    paths.SetGameRoot(root / "Assets");
    paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");

    Luma::Material::Material v3 = Luma::Material::Material::MakeDefault();
    v3.name = "M_Old";
    v3.Input(MaterialParameter::Roughness).constant = {0.25f, 0, 0, 0};
    std::string err;
    REQUIRE(MaterialSerializer::SaveToFile(v3, root / "Assets/Materials/M_Old.lmat", &err));
    MaterialInstance mi;
    mi.name = "MI_Old";
    mi.parent = "/Game/Materials/M_Old";
    MaterialInput r = v3.Input(MaterialParameter::Roughness);
    r.constant = {0.75f, 0, 0, 0};
    mi.Override(MaterialParameter::Roughness, r);
    REQUIRE(MaterialInstanceSerializer::SaveToFile(mi, root / "Assets/Materials/MI_Old.lmatinst", &err));

    {
        MaterialLibrary lib(paths);
        const GraphMaterial* m = lib.GetGraphMaterial("/Game/Materials/M_Old");
        REQUIRE(m);
        CHECK(m->name == "M_Old");
        CHECK(Param(m->graph, "ScalarParameter", "Roughness")->properties.at("default").AsFloat() ==
              Catch::Approx(0.25));
        CHECK_FALSE(lib.IsGraphAssetDirty("/Game/Materials/M_Old"));  // converted, not edited
        const GraphMaterialInstance* inst = lib.GetGraphInstance("/Game/Materials/MI_Old");
        REQUIRE(inst);
        CHECK(inst->parent == "/Game/Materials/M_Old");
        REQUIRE(inst->Find("Roughness"));
        CHECK(inst->Find("Roughness")->scalar == 0.75f);
        // Saving writes the v4 / v2 formats.
        REQUIRE(lib.SaveGraphAsset("/Game/Materials/M_Old", &err));
        CHECK(GraphAssetSerializer::ProbeVersion(root / "Assets/Materials/M_Old.lmat") == 4);
    }
    fs::remove_all(root);
}

TEST_CASE("A migrated Unlit material shows its base colour plus emissive", "[material][migration]") {
    // Phase-1 Unlit drew base colour + emissive; graph Unlit materials output
    // Emissive only (Unreal), so migration feeds both into Emissive Color.
    Luma::Material::Material old = Luma::Material::Material::MakeDefault();
    old.settings.shadingModel = ShadingModel::Unlit;
    old.Input(MaterialParameter::BaseColor).constant = {0.6f, 0.3f, 0.1f, 1.0f};
    old.Input(MaterialParameter::EmissiveColor).constant = {0.2f, 0.1f, 0.0f, 0.0f};
    const GraphMaterial m = MigrateV3(old, nullptr);
    const Luma::MaterialCompiler::CompiledMaterial c = Luma::MaterialCompiler::Translator(m, nullptr, {}).Finish();
    REQUIRE(c.Ok());
    const auto block = Luma::MaterialCompiler::EvaluatePreshader(c.preshader, c.layout, {}, {}, {});
    std::string emissive;
    for (const auto& o : c.outputs) {
        if (o.pin == "EmissiveColor") emissive = o.code;
    }
    REQUIRE_FALSE(emissive.empty());
    // The whole sum is uniform: one block slot holds (0.8, 0.4, 0.1).
    bool found = false;
    for (const auto& v : block) {
        found = found || (std::abs(v.x - 0.8f) < 1e-4f && std::abs(v.y - 0.4f) < 1e-4f && std::abs(v.z - 0.1f) < 1e-4f);
    }
    CHECK(found);
}
