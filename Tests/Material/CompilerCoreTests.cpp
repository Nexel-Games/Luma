#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Luma/Material/GraphAssets.h"
#include "Luma/MaterialCompiler/Translator.h"

using namespace Luma;
using namespace Luma::MaterialCompiler;
using Luma::Material::GraphMaterial;
using Luma::MaterialGraph::Graph;
using Luma::MaterialGraph::NodeId;
using Luma::MaterialGraph::PinRef;
using Luma::MaterialGraph::PropertyValue;
using Luma::MaterialGraph::ValueType;

namespace {

// Material Output input pins (catalogue order).
constexpr u32 kBaseColor = 0;
constexpr u32 kMetallic = 1;
constexpr u32 kRoughness = 3;
constexpr u32 kEmissive = 5;
constexpr u32 kWorldPositionOffset = 10;

GraphMaterial Empty() {
    GraphMaterial m;
    m.name = "M_Test";
    return m;
}

NodeId Add(Graph& g, const char* type) { return g.AddNode(type, {0.0f, 0.0f}); }

NodeId Constant(Graph& g, f64 v) {
    const NodeId id = Add(g, "Constant");
    g.Find(id)->properties["value"] = PropertyValue(v);
    return id;
}

NodeId Vector(Graph& g, const char* type, Math::Vec4 v) {
    const NodeId id = Add(g, type);
    g.Find(id)->properties["value"] = PropertyValue(v);
    return id;
}

void Link(Graph& g, NodeId from, NodeId to, u32 toPin, u32 fromPin = 0) {
    std::string err;
    REQUIRE(g.Connect(PinRef{from, fromPin}, PinRef{to, toPin}, &err));
}

const MaterialOutputValue* Output(const CompiledMaterial& c, const char* pin) {
    for (const MaterialOutputValue& o : c.outputs) {
        if (o.pin == pin) return &o;
    }
    return nullptr;
}

bool HasErrorAt(const CompiledMaterial& c, NodeId node) {
    for (const CompileError& e : c.errors) {
        if (e.node == node) return true;
    }
    return false;
}

usize Count(const std::string& hay, const std::string& needle) {
    usize n = 0;
    for (usize at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
    return n;
}

}  // namespace

TEST_CASE("Constant operands fold at compile time", "[compiler]") {
    GraphMaterial m = Empty();
    Graph& g = m.graph;
    const NodeId add = Add(g, "Add");
    Link(g, Constant(g, 2.0), add, 0);
    Link(g, Constant(g, 3.0), add, 1);
    Link(g, add, g.OutputNode(), kMetallic);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    REQUIRE(c.errors.empty());
    const MaterialOutputValue* metallic = Output(c, "Metallic");
    REQUIRE(metallic != nullptr);
    CHECK(metallic->code == "5.0");
    CHECK(c.pixelBody.empty());  // no GLSL variable for a folded constant
}

TEST_CASE("A Float1 broadcasts to the other operand's width", "[compiler]") {
    GraphMaterial m = Empty();
    Graph& g = m.graph;
    const NodeId add = Add(g, "Add");
    Link(g, Vector(g, "Constant3Vector", {1, 2, 3, 0}), add, 0);
    Link(g, Constant(g, 1.0), add, 1);

    Translator t(m, nullptr, {});
    const i32 chunk = t.CompileOutput(add, 0, Stage::Pixel);
    REQUIRE(chunk >= 0);
    CHECK(t.ChunkAt(chunk).type == ValueType::Float3);
    CHECK(t.ChunkAt(chunk).code == "vec3(2.0, 3.0, 4.0)");
}

TEST_CASE("Mismatched widths are an error at the node", "[compiler]") {
    GraphMaterial m = Empty();
    Graph& g = m.graph;
    const NodeId add = Add(g, "Add");
    Link(g, Vector(g, "Constant2Vector", {1, 2, 0, 0}), add, 0);
    Link(g, Vector(g, "Constant3Vector", {1, 2, 3, 0}), add, 1);
    Link(g, add, g.OutputNode(), kBaseColor);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    CHECK(HasErrorAt(c, add));
}

TEST_CASE("A sub-graph shared by two outputs is emitted once", "[compiler]") {
    GraphMaterial m = Empty();
    Graph& g = m.graph;
    const NodeId ddx = Add(g, "DDX");
    Link(g, Vector(g, "Constant3Vector", {1, 2, 3, 0}), ddx, 0);
    const NodeId mul = Add(g, "Multiply");
    Link(g, ddx, mul, 0);
    Link(g, Constant(g, 2.0), mul, 1);
    Link(g, mul, g.OutputNode(), kBaseColor);
    Link(g, mul, g.OutputNode(), kEmissive);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    REQUIRE(c.errors.empty());
    CHECK(Count(c.pixelBody, "dFdx(") == 1);
    CHECK(Count(c.pixelBody, " * ") == 1);
    CHECK(Output(c, "BaseColor")->code == Output(c, "EmissiveColor")->code);
}

TEST_CASE("A cycle is an error naming every node on it", "[compiler]") {
    GraphMaterial m = Empty();
    Graph& g = m.graph;
    const NodeId a = Add(g, "Add");
    const NodeId b = Add(g, "Add");
    Link(g, a, b, 0);
    Link(g, b, a, 0);
    Link(g, a, g.OutputNode(), kMetallic);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    CHECK(HasErrorAt(c, a));
    CHECK(HasErrorAt(c, b));
}

TEST_CASE("Pixel-only expressions can't feed the vertex shader", "[compiler]") {
    GraphMaterial m = Empty();
    Graph& g = m.graph;
    const NodeId ddx = Add(g, "DDX");
    Link(g, Vector(g, "Constant3Vector", {1, 2, 3, 0}), ddx, 0);
    Link(g, ddx, g.OutputNode(), kWorldPositionOffset);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    REQUIRE(HasErrorAt(c, ddx));
    bool mentionsVertex = false;
    for (const CompileError& e : c.errors) {
        if (e.node == ddx && e.message.find("vertex shader") != std::string::npos) mentionsVertex = true;
    }
    CHECK(mentionsVertex);
}

TEST_CASE("A linked pin the shading model ignores is a warning", "[compiler]") {
    GraphMaterial m = Empty();
    m.settings.shadingModel = Material::ShadingModel::Unlit;
    Graph& g = m.graph;
    Link(g, Constant(g, 0.7), g.OutputNode(), kRoughness);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    CHECK(c.errors.empty());
    bool warned = false;
    for (const CompileError& w : c.warnings) {
        if (w.warning && w.message.find("Roughness") != std::string::npos) warned = true;
    }
    CHECK(warned);
    CHECK(Output(c, "Roughness") == nullptr);
}

TEST_CASE("Identical graphs generate identical code", "[compiler]") {
    auto build = [] {
        GraphMaterial m = Empty();
        Graph& g = m.graph;
        const NodeId ddx = Add(g, "DDX");
        Link(g, Vector(g, "Constant3Vector", {0.25, 0.5, 1, 0}), ddx, 0);
        const NodeId mul = Add(g, "Multiply");
        Link(g, ddx, mul, 0);
        Link(g, Constant(g, 3.0), mul, 1);
        Link(g, mul, g.OutputNode(), kBaseColor);
        return Translator(m, nullptr, {}).Finish();
    };
    const CompiledMaterial a = build();
    const CompiledMaterial b = build();
    CHECK(a.pixelBody == b.pixelBody);
    CHECK(a.Fingerprint() == b.Fingerprint());
    REQUIRE(Output(a, "BaseColor") != nullptr);
    CHECK(Output(a, "BaseColor")->code == Output(b, "BaseColor")->code);
}
