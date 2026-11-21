#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <string>

#include "Luma/Material/GraphAssets.h"
#include "Luma/MaterialCompiler/Preshader.h"
#include "Luma/MaterialCompiler/Translator.h"

using Catch::Approx;
using namespace Luma;
using namespace Luma::MaterialCompiler;
using Luma::Material::GraphMaterial;
using Luma::Material::ParameterOverride;
using Luma::MaterialGraph::Graph;
using Luma::MaterialGraph::NodeId;
using Luma::MaterialGraph::PinRef;
using Luma::MaterialGraph::PropertyValue;

namespace {

constexpr u32 kBaseColor = 0;
constexpr u32 kMetallic = 1;

NodeId Node(Graph& g, const char* type) { return g.AddNode(type, {0.0f, 0.0f}); }
void Set(Graph& g, NodeId n, const char* key, PropertyValue v) { g.Find(n)->properties[key] = std::move(v); }
void Link(Graph& g, NodeId from, NodeId to, u32 toPin, u32 fromPin = 0) {
    std::string err;
    REQUIRE(g.Connect(PinRef{from, fromPin}, PinRef{to, toPin}, &err));
}

NodeId Scalar(Graph& g, const char* name, f64 def) {
    const NodeId n = Node(g, "ScalarParameter");
    Set(g, n, "name", PropertyValue(name));
    Set(g, n, "default", PropertyValue(def));
    return n;
}

NodeId VectorParam(Graph& g, const char* name, Math::Vec4 def) {
    const NodeId n = Node(g, "VectorParameter");
    Set(g, n, "name", PropertyValue(name));
    Set(g, n, "default", PropertyValue(def));
    return n;
}

NodeId Constant(Graph& g, f64 v) {
    const NodeId n = Node(g, "Constant");
    Set(g, n, "value", PropertyValue(v));
    return n;
}

ParameterOverride ScalarOverride(const char* name, f32 v) {
    ParameterOverride o;
    o.name = name;
    o.kind = ParameterOverride::Kind::Scalar;
    o.scalar = v;
    return o;
}

std::vector<Math::Vec4> Evaluate(const CompiledMaterial& c, std::span<const ParameterOverride> overrides = {},
                                 const TextureResolver& textures = {}) {
    return EvaluatePreshader(c.preshader, c.layout, overrides, {}, textures);
}

const MaterialOutputValue* Output(const CompiledMaterial& c, const char* pin) {
    for (const MaterialOutputValue& o : c.outputs) {
        if (o.pin == pin) return &o;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("A parameter expression becomes one uniform slot evaluated on the CPU", "[compiler][preshader]") {
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId mul = Node(g, "Multiply");
    Link(g, Scalar(g, "A", 2.0), mul, 0);
    Link(g, Constant(g, 3.0), mul, 1);
    Link(g, mul, g.OutputNode(), kMetallic);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    REQUIRE(c.errors.empty());
    CHECK(c.layout.vec4Count == 1);
    CHECK(c.pixelBody.empty());  // no per-pixel code for a uniform value
    CHECK(Output(c, "Metallic")->code == "MP(0).x");
    CHECK(Evaluate(c)[0].x == Approx(6.0f));
    const ParameterOverride a = ScalarOverride("A", 4.0f);
    CHECK(Evaluate(c, std::span(&a, 1))[0].x == Approx(12.0f));
}

TEST_CASE("Swizzles and appends of a vector parameter evaluate correctly", "[compiler][preshader]") {
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId v = VectorParam(g, "Tint", {1, 2, 3, 4});
    const NodeId zx = Node(g, "AppendVector");
    Link(g, v, zx, 0, 3);  // B (z)
    Link(g, v, zx, 1, 1);  // R (x)
    const NodeId rgb = Node(g, "AppendVector");
    Link(g, zx, rgb, 0);
    Link(g, Constant(g, 0.5), rgb, 1);
    Link(g, rgb, g.OutputNode(), kBaseColor);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    REQUIRE(c.errors.empty());
    CHECK(c.pixelBody.empty());
    const Math::Vec4 slot = Evaluate(c)[0];
    CHECK(slot.x == Approx(3.0f));
    CHECK(slot.y == Approx(1.0f));
    CHECK(slot.z == Approx(0.5f));
}

TEST_CASE("Lerp of two vector parameters by a scalar parameter", "[compiler][preshader]") {
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId lerp = Node(g, "Lerp");
    Link(g, VectorParam(g, "From", {0, 0, 0, 1}), lerp, 0, 0);
    Link(g, VectorParam(g, "To", {10, 20, 40, 1}), lerp, 1, 0);
    Link(g, Scalar(g, "Blend", 0.5), lerp, 2);
    Link(g, lerp, g.OutputNode(), kBaseColor);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    REQUIRE(c.errors.empty());
    CHECK(c.layout.vec4Count == 1);
    const ParameterOverride blend = ScalarOverride("Blend", 0.25f);
    const Math::Vec4 slot = Evaluate(c, std::span(&blend, 1))[0];
    CHECK(slot.x == Approx(2.5f));
    CHECK(slot.y == Approx(5.0f));
    CHECK(slot.z == Approx(10.0f));
}

TEST_CASE("Texture slots pack the resolver's image and sampler", "[compiler][preshader]") {
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId tex = Node(g, "TextureSampleParameter2D");
    Set(g, tex, "name", PropertyValue("Albedo"));
    Set(g, tex, "texture", PropertyValue("/Game/T_A"));
    Link(g, tex, g.OutputNode(), kBaseColor);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    REQUIRE(c.errors.empty());
    REQUIRE(c.layout.textures.size() == 1);
    const TextureSlot& slot = c.layout.textures[0];
    CHECK(slot.parameter == "Albedo");
    CHECK(c.pixelBody.find("MSample2D(" + std::to_string(slot.slot) + ",") != std::string::npos);

    std::string asked;
    const TextureResolver resolver = [&asked](const std::string& path, const TextureSlot&) {
        asked = path;
        return TextureBinding{path == "/Game/T_B" ? 9u : 7u, 2u, true};
    };
    std::vector<Math::Vec4> block = Evaluate(c, {}, resolver);
    CHECK(asked == "/Game/T_A");
    CHECK(std::bit_cast<u32>(block[slot.slot].x) == 7u);
    CHECK(std::bit_cast<u32>(block[slot.slot].y) == 2u);
    CHECK(std::bit_cast<u32>(block[slot.slot].z) == 1u);

    ParameterOverride swap;
    swap.name = "Albedo";
    swap.kind = ParameterOverride::Kind::Texture;
    swap.texture = "/Game/T_B";
    block = Evaluate(c, std::span(&swap, 1), resolver);
    CHECK(asked == "/Game/T_B");
    CHECK(std::bit_cast<u32>(block[slot.slot].x) == 9u);
}

TEST_CASE("Static switch values form the permutation key", "[compiler][preshader]") {
    CHECK(StaticSwitchKey({{"Use Detail", true}}) != StaticSwitchKey({{"Use Detail", false}}));
    CHECK(StaticSwitchKey({{"A", true}, {"B", false}}) == StaticSwitchKey({{"B", false}, {"A", true}}));
    CHECK(StaticSwitchKey({}) == StaticSwitchKey({}));
}

TEST_CASE("Parameter metadata is collected", "[compiler][preshader]") {
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId s = Scalar(g, "Roughness Scale", 1.0);
    Set(g, s, "group", PropertyValue("Surface"));
    Set(g, s, "sortPriority", PropertyValue(i64{5}));
    Set(g, s, "sliderMin", PropertyValue(0.0));
    Set(g, s, "sliderMax", PropertyValue(2.0));
    Link(g, s, g.OutputNode(), kMetallic);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    REQUIRE(c.parameters.size() == 1);
    CHECK(c.parameters[0].name == "Roughness Scale");
    CHECK(c.parameters[0].group == "Surface");
    CHECK(c.parameters[0].sortPriority == 5);
    CHECK(c.parameters[0].sliderMax == Approx(2.0f));
    CHECK(c.parameters[0].defaultValue.x == Approx(1.0f));
}

TEST_CASE("A parameter name used with two types is an error on the second node", "[compiler][preshader]") {
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId a = Scalar(g, "Tint", 1.0);
    const NodeId b = VectorParam(g, "Tint", {1, 1, 1, 1});
    const NodeId add = Node(g, "Add");
    Link(g, a, add, 0);
    Link(g, b, add, 1, 0);
    Link(g, add, g.OutputNode(), kBaseColor);

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    bool onSecond = false, onFirst = false;
    for (const CompileError& e : c.errors) {
        onSecond = onSecond || e.node == b;
        onFirst = onFirst || e.node == a;
    }
    CHECK(onSecond);
    CHECK_FALSE(onFirst);
}
