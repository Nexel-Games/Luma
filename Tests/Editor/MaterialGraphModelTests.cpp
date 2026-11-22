#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "Luma/GraphEditor/GraphController.h"
#include "Luma/MaterialCompiler/Translator.h"
#include "Luma/MaterialEditors/MaterialGraphModel.h"

using namespace Luma;
using GraphEditor::ConnectResponse;
using GraphEditor::NodeId;
using GraphEditor::PinDir;
using GraphEditor::PinKey;
using MaterialEditors::MaterialGraphModel;
using MaterialGraph::ValueType;

namespace {
PinKey Out(NodeId n, u32 i = 0) { return {n, PinDir::Output, i}; }
PinKey In(NodeId n, u32 i = 0) { return {n, PinDir::Input, i}; }
}  // namespace

TEST_CASE("Material link rules follow the compiler's conversions", "[materialeditors][graphmodel]") {
    using MaterialEditors::TypesCompatible;
    CHECK(TypesCompatible(ValueType::Float1, ValueType::Float3));
    CHECK(TypesCompatible(ValueType::Float4, ValueType::Float3));
    CHECK_FALSE(TypesCompatible(ValueType::Float2, ValueType::Float3));
    CHECK(TypesCompatible(ValueType::Any, ValueType::Float2));
    CHECK(TypesCompatible(ValueType::Float2, ValueType::Any));
    CHECK_FALSE(TypesCompatible(ValueType::Texture2D, ValueType::Float3));
    CHECK_FALSE(TypesCompatible(ValueType::Texture2D, ValueType::TextureCube));
    CHECK_FALSE(TypesCompatible(ValueType::Bool, ValueType::Float1));
    std::string why;
    CHECK_FALSE(TypesCompatible(ValueType::Float2, ValueType::Float3, &why));
    CHECK(why.find("Float2") != std::string::npos);
}

TEST_CASE("Material graph model exposes registry nodes and checks links", "[materialeditors][graphmodel]") {
    MaterialGraphModel m(Material::GraphMaterial::MakeDefault());
    const NodeId output = m.Material().graph.OutputNode();
    const NodeId uv = m.Spawn("TextureCoordinate", {-400, 0});
    const NodeId c3 = m.Spawn("Constant3Vector", {-400, 200});
    REQUIRE(uv != 0);
    REQUIRE(c3 != 0);

    const auto nodes = m.Nodes();
    const auto out = std::find_if(nodes.begin(), nodes.end(), [&](auto& n) { return n.id == output; });
    REQUIRE(out != nodes.end());
    CHECK_FALSE(out->removable);
    CHECK(out->inputs.size() > 5);

    // UV (Float2) into Base Color (Float3) is refused; a Float3 replaces the existing link.
    CHECK(m.CanConnect(Out(uv), In(output, 0)).response == ConnectResponse::Disallow);
    CHECK(m.CanConnect(Out(c3), In(output, 0)).response == ConnectResponse::Replace);
    CHECK(m.Connect(Out(c3), In(output, 0)));
    CHECK(m.Material().graph.Source({output, 0})->node == c3);
    CHECK_FALSE(m.RemoveNode(output));
}

TEST_CASE("Cycles are refused", "[materialeditors][graphmodel]") {
    MaterialGraphModel m(Material::GraphMaterial{});
    const NodeId a = m.Spawn("Add", {0, 0});
    const NodeId b = m.Spawn("Add", {200, 0});
    REQUIRE(m.Connect(Out(a), In(b)));
    const auto check = m.CanConnect(Out(b), In(a));
    CHECK(check.response == ConnectResponse::Disallow);
    CHECK(check.message.find("cycle") != std::string::npos);
}

TEST_CASE("Reroute knots pass values through and compile", "[materialeditors][graphmodel]") {
    MaterialGraphModel m(Material::GraphMaterial{});
    const NodeId output = m.Material().graph.OutputNode();
    const NodeId c3 = m.Spawn("Constant3Vector", {-400, 0});
    GraphEditor::GraphController controller(m);
    REQUIRE(controller.TryLink(Out(c3), In(output, 0)).Allowed());
    const NodeId knot = controller.InsertReroute(m.Links()[0], {-200, 0});
    REQUIRE(knot != 0);
    CHECK(m.PinType(Out(knot)) == ValueType::Float3);  // takes its source's type
    CHECK(m.CanConnect(Out(knot), In(output, 3)).Allowed());  // Float3 -> Roughness truncates (compiler warns)

    MaterialCompiler::Translator t(m.Material(), nullptr, {});
    const auto compiled = t.Finish();
    CHECK(compiled.Ok());
}

TEST_CASE("Parameters spawn with unique names", "[materialeditors][graphmodel]") {
    MaterialGraphModel m(Material::GraphMaterial{});
    const NodeId a = m.Spawn("ScalarParameter", {0, 0});
    const NodeId b = m.Spawn("ScalarParameter", {0, 100});
    const auto* na = m.Material().graph.Find(a);
    const auto* nb = m.Material().graph.Find(b);
    REQUIRE(na);
    REQUIRE(nb);
    CHECK(na->properties.at("name").AsString() != nb->properties.at("name").AsString());
}

TEST_CASE("Copy/paste and comments round-trip through the clipboard", "[materialeditors][graphmodel]") {
    MaterialGraphModel m(Material::GraphMaterial{});
    const NodeId a = m.Spawn("Constant", {100, 100});
    const NodeId b = m.Spawn("Multiply", {300, 100});
    REQUIRE(m.Connect(Out(a), In(b)));
    REQUIRE(m.SetProperty(a, "value", MaterialGraph::PropertyValue(0.75)));
    const NodeId comment = m.AddComment({50, 50}, {400, 200}, "Block");
    REQUIRE((comment & MaterialEditors::kCommentBit) != 0);

    const std::string clip = m.Copy({a, b, comment, m.Material().graph.OutputNode()});
    const auto added = m.Paste(clip, {1000, 500});
    REQUIRE(added.size() == 3);  // the output node is never pasted
    CHECK(m.Material().graph.Links().size() == 2);
    CHECK(m.Material().graph.Comments().size() == 2);
    const auto pastedConst = std::find_if(added.begin(), added.end(), [&](NodeId id) {
        const auto* n = m.Material().graph.Find(id);
        return n && n->type == "Constant";
    });
    REQUIRE(pastedConst != added.end());
    const auto* pc = m.Material().graph.Find(*pastedConst);
    CHECK(pc->properties.at("value").AsFloat() == 0.75);
    CHECK(pc->position.x == 1050);  // comment at 50 was the block's left edge
    CHECK(pc->position.y == 550);

    const std::any snap = m.Snapshot();
    m.RemoveNode(comment);
    CHECK(m.Material().graph.Comments().size() == 1);
    m.Restore(snap);
    CHECK(m.Material().graph.Comments().size() == 2);
}

TEST_CASE("Drag-off actions are filtered by pin type", "[materialeditors][graphmodel]") {
    MaterialGraphModel m(Material::GraphMaterial{});
    const NodeId tex = m.Spawn("TextureObjectParameter", {0, 0});
    REQUIRE(tex != 0);
    const PinKey from = Out(tex);
    const auto filtered = m.Actions(&from);
    const auto all = m.Actions(nullptr);
    CHECK(filtered.size() < all.size());
    CHECK(std::any_of(filtered.begin(), filtered.end(), [](auto& a) { return a.id == "TextureSample"; }));
    CHECK(std::none_of(filtered.begin(), filtered.end(), [](auto& a) { return a.id == "Fresnel"; }));  // float-only inputs
}
