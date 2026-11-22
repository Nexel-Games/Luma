#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialEditors/InstanceParameters.h"
#include "Luma/MaterialEditors/MaterialInstanceEditor.h"
#include "../Slate/SlateTestHarness.h"

namespace fs = std::filesystem;
using namespace Luma;
using Material::GraphMaterial;
using Material::GraphMaterialInstance;
using Material::ParameterOverride;
using MaterialEditors::InstanceParameterModel;
using MaterialGraph::NodeId;
using MaterialGraph::PinRef;
using MaterialGraph::PropertyValue;

namespace {

struct Fixture {
    fs::path root = fs::temp_directory_path() / "luma_instance_params_test";
    AssetPathResolver paths;
    std::unique_ptr<Material::MaterialLibrary> lib;

    Fixture() {
        fs::remove_all(root);
        fs::create_directories(root / "Assets" / "Materials");
        paths.SetGameRoot(root / "Assets");
        paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
        lib = std::make_unique<Material::MaterialLibrary>(paths);

        GraphMaterial m;
        m.name = "M_Base";
        std::string err;
        const NodeId rough = m.graph.AddNode("ScalarParameter", {0, 0});
        m.graph.Find(rough)->properties["name"] = PropertyValue("Roughness");
        m.graph.Find(rough)->properties["group"] = PropertyValue("Surface");
        m.graph.Find(rough)->properties["default"] = PropertyValue(0.25);
        const NodeId tint = m.graph.AddNode("VectorParameter", {0, 0});
        m.graph.Find(tint)->properties["name"] = PropertyValue("Tint");
        m.graph.Find(tint)->properties["default"] = PropertyValue(Math::Vec4{1, 0, 0, 1});
        REQUIRE(m.graph.Connect(PinRef{tint, 0}, PinRef{m.graph.OutputNode(), 0}, &err));
        REQUIRE(m.graph.Connect(PinRef{rough, 0}, PinRef{m.graph.OutputNode(), 3}, &err));
        lib->SetGraphMaterial("/Game/Materials/M_Base", m);

        GraphMaterialInstance a;
        a.name = "MI_A";
        a.parent = "/Game/Materials/M_Base";
        ParameterOverride o;
        o.name = "Roughness";
        o.scalar = 0.8f;
        a.Set(o);
        lib->SetGraphInstance("/Game/Materials/MI_A", a);

        GraphMaterialInstance b;
        b.name = "MI_B";
        b.parent = "/Game/Materials/MI_A";
        lib->SetGraphInstance("/Game/Materials/MI_B", b);
    }
    ~Fixture() {
        lib.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

std::optional<MaterialEditors::InstanceParameter> FindParam(
    const std::vector<MaterialEditors::ParameterGroup>& groups, std::string_view name) {
    for (const auto& g : groups)
        for (const auto& p : g.parameters)
            if (p.info.name == name) return p;
    return std::nullopt;
}

}  // namespace

TEST_CASE("Instance parameters are grouped from the root material", "[materialeditors][instance]") {
    Fixture f;
    InstanceParameterModel model("/Game/Materials/MI_B", *f.lib);
    REQUIRE(model.Valid());
    CHECK(model.RootMaterial() == "/Game/Materials/M_Base");
    const auto groups = model.Groups();
    REQUIRE(groups.size() == 2);
    CHECK(groups[0].name == "None");  // ungrouped first, like UE
    CHECK(groups[1].name == "Surface");
    REQUIRE(FindParam(groups, "Tint"));
    CHECK(FindParam(groups, "Tint")->value.vector.x == 1.0f);
}

TEST_CASE("Inherited values come through the instance chain", "[materialeditors][instance]") {
    Fixture f;
    InstanceParameterModel model("/Game/Materials/MI_B", *f.lib);
    const auto rough = FindParam(model.Groups(), "Roughness");
    REQUIRE(rough);
    CHECK_FALSE(rough->overridden);
    CHECK(rough->value.scalar == 0.8f);        // from MI_A
    CHECK(rough->parentValue.scalar == 0.8f);
}

TEST_CASE("Overriding starts from the inherited value; reset falls back", "[materialeditors][instance]") {
    Fixture f;
    InstanceParameterModel model("/Game/Materials/MI_B", *f.lib);
    model.SetOverride("Roughness", true);
    auto rough = FindParam(model.Groups(), "Roughness");
    REQUIRE(rough->overridden);
    CHECK(rough->value.scalar == 0.8f);
    CHECK(model.IsDirty());

    ParameterOverride v = rough->value;
    v.scalar = 0.1f;
    model.SetValue(v);
    CHECK(FindParam(model.Groups(), "Roughness")->value.scalar == 0.1f);
    CHECK(f.lib->GetGraphInstance("/Game/Materials/MI_B")->Find("Roughness")->scalar == 0.1f);

    model.Reset("Roughness");
    rough = FindParam(model.Groups(), "Roughness");
    CHECK_FALSE(rough->overridden);
    CHECK(rough->value.scalar == 0.8f);
}

TEST_CASE("Setting a value turns its override on", "[materialeditors][instance]") {
    Fixture f;
    InstanceParameterModel model("/Game/Materials/MI_B", *f.lib);
    ParameterOverride v;
    v.name = "Tint";
    v.kind = ParameterOverride::Kind::Vector;
    v.vector = {0, 1, 0, 1};
    model.SetValue(v);
    const auto tint = FindParam(model.Groups(), "Tint");
    CHECK(tint->overridden);
    CHECK(tint->value.vector.y == 1.0f);
    CHECK(tint->parentValue.vector.x == 1.0f);
}

TEST_CASE("Parent changes refuse cycles and unknown assets", "[materialeditors][instance]") {
    Fixture f;
    InstanceParameterModel model("/Game/Materials/MI_A", *f.lib);
    std::string err;
    CHECK_FALSE(model.SetParent("/Game/Materials/MI_B", &err));  // MI_B -> MI_A
    CHECK_FALSE(err.empty());
    CHECK_FALSE(model.SetParent("/Game/Materials/Nope", &err));
    CHECK(model.Parent() == "/Game/Materials/M_Base");
    CHECK(model.SetParent(std::string(Material::kDefaultMaterialPath), &err));
    CHECK(model.RootMaterial() == Material::kDefaultMaterialPath);
    // The Default Material exposes BaseColor / Roughness.
    CHECK(FindParam(model.Groups(), "BaseColor"));
}

TEST_CASE("Instance editor draws a 70% preview and the details", "[materialeditors][instance]") {
    Fixture f;
    MaterialEditors::MaterialEditorServices services;
    MaterialEditors::PreviewRequest last;
    int renders = 0;
    services.renderPreview = [&](const MaterialEditors::PreviewRequest& r) {
        last = r;
        ++renders;
        return TextureHandle{7};
    };
    services.listMaterials = [] { return std::vector<std::string>{"/Game/Materials/M_Base"}; };
    MaterialEditors::MaterialInstanceEditor editor("/Game/Materials/MI_B", *f.lib, services);
    Slate::Test::Harness h;
    for (int i = 0; i < 2; ++i) h.Frame([&] { editor.Draw(h.ctx, {0, 0, 800, 600}); });
    CHECK(renders == 2);
    CHECK(last.materialPath == "/Game/Materials/MI_B");
    CHECK(last.width > 500);
    CHECK(last.width < 600);
    CHECK(editor.Title() == "MI_B*");  // in-memory fixture asset = unsaved
}
