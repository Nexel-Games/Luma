#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <set>
#include <string>

#include "Luma/Material/GraphAssets.h"
#include "Luma/MaterialCompiler/Translator.h"

using Catch::Approx;
using namespace Luma;
using namespace Luma::MaterialCompiler;
using Luma::Material::GraphMaterial;
using Luma::MaterialGraph::ExpressionRegistry;
using Luma::MaterialGraph::Graph;
using Luma::MaterialGraph::NodeId;
using Luma::MaterialGraph::PinRef;
using Luma::MaterialGraph::PropertyValue;
using Luma::MaterialGraph::ValueType;

namespace {

NodeId Node(Graph& g, const char* type) { return g.AddNode(type, {0.0f, 0.0f}); }

void Set(Graph& g, NodeId n, const char* key, PropertyValue v) { g.Find(n)->properties[key] = std::move(v); }

void Link(Graph& g, NodeId from, NodeId to, u32 toPin, u32 fromPin = 0) {
    std::string err;
    REQUIRE(g.Connect(PinRef{from, fromPin}, PinRef{to, toPin}, &err));
}

NodeId Constant(Graph& g, f64 v) {
    const NodeId n = Node(g, "Constant");
    Set(g, n, "value", PropertyValue(v));
    return n;
}

NodeId Vector(Graph& g, const char* type, Math::Vec4 v) {
    const NodeId n = Node(g, type);
    Set(g, n, "value", PropertyValue(v));
    return n;
}

NodeId TextureObject(Graph& g) {
    const NodeId n = Node(g, "TextureObject");
    Set(g, n, "texture", PropertyValue("/Game/Textures/T_Height"));
    return n;
}

struct Expectation {
    const char* type;
    ValueType result;
    Frequency freq;
    std::function<void(GraphMaterial&, NodeId)> setup;  // links / properties / settings
};

std::string Describe(const Translator& t) {
    std::string text;
    for (const CompileError& e : t.Errors()) text += e.message + "\n";
    return text;
}

// Compiles output 0 of a lone `type` node; returns the chunk (or -1).
i32 CompileLone(GraphMaterial& m, Translator& t, NodeId node) {
    (void)m;
    return t.CompileOutput(node, 0, Stage::Pixel);
}

f32 Folded(const char* type, std::function<void(Graph&, NodeId)> wire) {
    GraphMaterial m;
    const NodeId n = Node(m.graph, type);
    wire(m.graph, n);
    Translator t(m, nullptr, {});
    const i32 c = t.CompileOutput(n, 0, Stage::Pixel);
    REQUIRE(c >= 0);
    REQUIRE(t.ChunkAt(c).constant.has_value());
    return t.ChunkAt(c).constant->x;
}

}  // namespace

TEST_CASE("Every catalogue expression compiles to its expected type and frequency", "[compiler][expr]") {
    const Frequency C = Frequency::Constant, U = Frequency::Uniform, V = Frequency::Varying;
    auto texAsset = [](GraphMaterial& m, NodeId n) { Set(m.graph, n, "texture", PropertyValue("/Game/T_Rock")); };
    auto heightTex = [](u32 pin) {
        return [pin](GraphMaterial& m, NodeId n) { Link(m.graph, TextureObject(m.graph), n, pin); };
    };
    auto postProcess = [](GraphMaterial& m, NodeId) { m.settings.domain = Material::MaterialDomain::PostProcess; };
    const std::vector<Expectation> table = {
        {"Constant", ValueType::Float1, C, {}},
        {"Constant2Vector", ValueType::Float2, C, {}},
        {"Constant3Vector", ValueType::Float3, C, {}},
        {"Constant4Vector", ValueType::Float4, C, {}},
        {"StaticBool", ValueType::Bool, C, {}},
        {"ScalarParameter", ValueType::Float1, U, {}},
        {"VectorParameter", ValueType::Float3, U, {}},
        {"TextureSampleParameter2D", ValueType::Float3, V, texAsset},
        {"TextureSampleParameterCube", ValueType::Float3, V, texAsset},
        {"TextureObjectParameter", ValueType::Texture2D, U, {}},
        {"StaticSwitchParameter", ValueType::Float1, C, {}},
        {"StaticBoolParameter", ValueType::Bool, C, {}},
        {"Add", ValueType::Float1, C, {}},
        {"Subtract", ValueType::Float1, C, {}},
        {"Multiply", ValueType::Float1, C, {}},
        {"Divide", ValueType::Float1, C, {}},
        {"Power", ValueType::Float1, C, {}},
        {"Abs", ValueType::Float1, C, {}},
        {"Min", ValueType::Float1, C, {}},
        {"Max", ValueType::Float1, C, {}},
        {"Clamp", ValueType::Float1, C, {}},
        {"Saturate", ValueType::Float1, C, {}},
        {"OneMinus", ValueType::Float1, C, {}},
        {"Lerp", ValueType::Float1, C, {}},
        {"Dot", ValueType::Float1, C, {}},
        {"Cross", ValueType::Float3, C, {}},
        {"Normalize", ValueType::Float3, C,
         [](GraphMaterial& m, NodeId n) { Link(m.graph, Vector(m.graph, "Constant3Vector", {3, 0, 4, 0}), n, 0); }},
        {"Length", ValueType::Float1, C, {}},
        {"Distance", ValueType::Float1, C, {}},
        {"SquareRoot", ValueType::Float1, C, {}},
        {"Exp", ValueType::Float1, C, {}},
        {"Log2", ValueType::Float1, C,
         [](GraphMaterial& m, NodeId n) { Link(m.graph, Constant(m.graph, 8.0), n, 0); }},
        {"Sine", ValueType::Float1, C, {}},
        {"Cosine", ValueType::Float1, C, {}},
        {"Tangent", ValueType::Float1, C, {}},
        {"Arcsine", ValueType::Float1, C, {}},
        {"Arccosine", ValueType::Float1, C, {}},
        {"Arctangent2", ValueType::Float1, C, {}},
        {"Floor", ValueType::Float1, C, {}},
        {"Ceil", ValueType::Float1, C, {}},
        {"Round", ValueType::Float1, C, {}},
        {"Frac", ValueType::Float1, C, {}},
        {"Fmod", ValueType::Float1, C, {}},
        {"Sign", ValueType::Float1, C, {}},
        {"Step", ValueType::Float1, C, {}},
        {"SmoothStep", ValueType::Float1, C, {}},
        {"If", ValueType::Float1, C, {}},
        {"AppendVector", ValueType::Float2, C, {}},
        {"ComponentMask", ValueType::Float2, C,
         [](GraphMaterial& m, NodeId n) { Link(m.graph, Vector(m.graph, "Constant4Vector", {1, 2, 3, 4}), n, 0); }},
        {"BreakOutFloat2Components", ValueType::Float1, C, {}},
        {"BreakOutFloat3Components", ValueType::Float1, C, {}},
        {"BreakOutFloat4Components", ValueType::Float1, C, {}},
        {"MakeFloat2", ValueType::Float2, C, {}},
        {"MakeFloat3", ValueType::Float3, C, {}},
        {"MakeFloat4", ValueType::Float4, C, {}},
        {"Desaturation", ValueType::Float3, C, {}},
        {"RemapValueRange", ValueType::Float1, C, {}},
        {"TextureSample", ValueType::Float3, V, texAsset},
        {"TextureObject", ValueType::Texture2D, U, texAsset},
        {"TextureProperty", ValueType::Float2, U, heightTex(0)},
        {"TextureCoordinate", ValueType::Float2, V, {}},
        {"Panner", ValueType::Float2, V, {}},
        {"Rotator", ValueType::Float2, V, {}},
        {"WorldPosition", ValueType::Float3, V, {}},
        {"ObjectPosition", ValueType::Float3, V, {}},
        {"ObjectRadius", ValueType::Float1, V, {}},
        {"ObjectBounds", ValueType::Float3, V, {}},
        {"ActorPosition", ValueType::Float3, V, {}},
        {"CameraPositionWS", ValueType::Float3, V, {}},
        {"CameraVectorWS", ValueType::Float3, V, {}},
        {"VertexNormalWS", ValueType::Float3, V, {}},
        {"PixelNormalWS", ValueType::Float3, V, {}},
        {"VertexTangentWS", ValueType::Float3, V, {}},
        {"ReflectionVectorWS", ValueType::Float3, V, {}},
        {"ScreenPosition", ValueType::Float2, V, {}},
        {"ViewSize", ValueType::Float2, V, {}},
        {"PixelDepth", ValueType::Float1, V, {}},
        {"SceneDepth", ValueType::Float1, V, postProcess},
        {"Time", ValueType::Float1, V, {}},
        {"DeltaTime", ValueType::Float1, V, {}},
        {"VertexColor", ValueType::Float3, V, {}},
        {"Fresnel", ValueType::Float1, V, {}},
        {"BumpOffset", ValueType::Float2, V,
         [](GraphMaterial& m, NodeId n) { Link(m.graph, Constant(m.graph, 0.5), n, 1); }},
        {"ParallaxOcclusionMapping", ValueType::Float2, V, heightTex(0)},
        {"NormalFromHeight", ValueType::Float3, V, heightTex(0)},
        {"BlendAngleCorrectedNormals", ValueType::Float3, C, {}},
        {"Transform", ValueType::Float3, V,
         [](GraphMaterial& m, NodeId n) { Link(m.graph, Vector(m.graph, "Constant3Vector", {0, 0, 1, 0}), n, 0); }},
        {"TransformPosition", ValueType::Float3, V,
         [](GraphMaterial& m, NodeId n) { Link(m.graph, Vector(m.graph, "Constant3Vector", {0, 0, 1, 0}), n, 0); }},
        {"DDX", ValueType::Float1, V, {}},
        {"DDY", ValueType::Float1, V, {}},
        {"Custom", ValueType::Float3, V, {}},
        {"VertexInterpolator", ValueType::Float1, C,
         [](GraphMaterial& m, NodeId n) { Link(m.graph, Constant(m.graph, 2.0), n, 0); }},
        {"NamedRerouteDeclaration", ValueType::Float1, C, {}},
        {"NamedRerouteUsage", ValueType::Float1, C,
         [](GraphMaterial& m, NodeId) { Node(m.graph, "NamedRerouteDeclaration"); }},
        {"Reroute", ValueType::Float1, C, {}},
        {"SceneTexture", ValueType::Float4, V, postProcess},
        {"SceneColor", ValueType::Float3, V,
         [](GraphMaterial& m, NodeId) { m.settings.blendMode = Material::BlendMode::Translucent; }},
    };
    // Covered elsewhere: Task 8 (functions/collections) and the output node.
    const std::set<std::string> skipped = {"CollectionParameter", "MaterialFunctionCall", "FunctionInput",
                                           "FunctionOutput", "MaterialOutput"};

    std::set<std::string> covered;
    for (const Expectation& e : table) {
        INFO(e.type);
        covered.insert(e.type);
        GraphMaterial m;
        const NodeId n = Node(m.graph, e.type);
        if (e.setup) e.setup(m, n);
        Translator t(m, nullptr, {});
        const i32 c = CompileLone(m, t, n);
        INFO(Describe(t));
        REQUIRE(c >= 0);
        CHECK(t.Errors().empty());
        CHECK(t.ChunkAt(c).type == e.result);
        CHECK(t.ChunkAt(c).freq == e.freq);
    }
    for (const auto* desc : ExpressionRegistry::Get().All()) {
        INFO(desc->type);
        CHECK((covered.contains(desc->type) || skipped.contains(desc->type)));
    }
}

TEST_CASE("Expressions fold constant inputs", "[compiler][expr]") {
    CHECK(Folded("Lerp", [](Graph& g, NodeId n) {
              Link(g, Constant(g, 0.0), n, 0);
              Link(g, Constant(g, 10.0), n, 1);
              Link(g, Constant(g, 0.25), n, 2);
          }) == Approx(2.5f));
    CHECK(Folded("Clamp", [](Graph& g, NodeId n) { Link(g, Constant(g, 5.0), n, 0); }) == Approx(1.0f));
    CHECK(Folded("OneMinus", [](Graph& g, NodeId n) { Link(g, Constant(g, 0.25), n, 0); }) == Approx(0.75f));
    CHECK(Folded("Power", [](Graph& g, NodeId n) {
              Link(g, Constant(g, 2.0), n, 0);
              Link(g, Constant(g, 3.0), n, 1);
          }) == Approx(8.0f));
    CHECK(Folded("Sine", [](Graph& g, NodeId n) { Link(g, Constant(g, 0.0), n, 0); }) == Approx(0.0f).margin(1e-6));
    CHECK(Folded("RemapValueRange", [](Graph& g, NodeId n) {
              Link(g, Constant(g, 0.5), n, 0);
              Link(g, Constant(g, 0.0), n, 1);
              Link(g, Constant(g, 1.0), n, 2);
              Link(g, Constant(g, 10.0), n, 3);
              Link(g, Constant(g, 20.0), n, 4);
          }) == Approx(15.0f));
}

TEST_CASE("Vector shaping expressions produce the right widths", "[compiler][expr]") {
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId dot = Node(g, "Dot");
    Link(g, Vector(g, "Constant3Vector", {1, 2, 3, 0}), dot, 0);
    Link(g, Vector(g, "Constant3Vector", {4, 5, 6, 0}), dot, 1);
    const NodeId append = Node(g, "AppendVector");
    Link(g, Vector(g, "Constant2Vector", {1, 2, 0, 0}), append, 0);
    Link(g, Constant(g, 3.0), append, 1);
    const NodeId mask = Node(g, "ComponentMask");
    Link(g, Vector(g, "Constant4Vector", {1, 2, 3, 4}), mask, 0);
    const NodeId iff = Node(g, "If");
    Link(g, Constant(g, 1.0), iff, 0);
    Link(g, Vector(g, "Constant3Vector", {7, 8, 9, 0}), iff, 2);
    const NodeId panner = Node(g, "Panner");
    Link(g, Node(g, "Time"), panner, 1);

    Translator t(m, nullptr, {});
    const Chunk d = t.ChunkAt(t.CompileOutput(dot, 0, Stage::Pixel));
    CHECK(d.type == ValueType::Float1);
    CHECK(d.constant->x == Approx(32.0f));
    const Chunk a = t.ChunkAt(t.CompileOutput(append, 0, Stage::Pixel));
    CHECK(a.type == ValueType::Float3);
    CHECK(a.code == "vec3(1.0, 2.0, 3.0)");
    const Chunk k = t.ChunkAt(t.CompileOutput(mask, 0, Stage::Pixel));
    CHECK(k.type == ValueType::Float2);
    CHECK(k.code == "vec2(1.0, 2.0)");
    const Chunk i = t.ChunkAt(t.CompileOutput(iff, 0, Stage::Pixel));
    CHECK(i.type == ValueType::Float3);
    CHECK(i.code == "vec3(7.0, 8.0, 9.0)");  // A > B picks AGreaterThanB
    const Chunk p = t.ChunkAt(t.CompileOutput(panner, 0, Stage::Pixel));
    CHECK(p.type == ValueType::Float2);
    CHECK(p.freq == Frequency::Varying);
    CHECK(t.Errors().empty());
}

TEST_CASE("A Custom node becomes a generated function", "[compiler][expr]") {
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId custom = Node(g, "Custom");
    Set(g, custom, "code", PropertyValue("Mask = A * 0.5;\nreturn vec2(A, B);"));
    Set(g, custom, "outputType", PropertyValue(i64{1}));
    Set(g, custom, "inputs", PropertyValue("A, B"));
    Set(g, custom, "outputs", PropertyValue("Mask:Float1"));
    Link(g, Constant(g, 0.25), custom, 0);
    Link(g, Node(g, "Time"), custom, 1);
    const NodeId add = Node(g, "Add");
    Link(g, custom, add, 0, 0);
    Link(g, custom, add, 1, 1);
    Link(g, add, g.OutputNode(), 1);  // Metallic (the Float2 truncates, with a warning)

    const CompiledMaterial c = Translator(m, nullptr, {}).Finish();
    INFO(c.functions);
    REQUIRE(c.errors.empty());
    CHECK(c.functions.find("vec2 CustomExpression0(float A, float B, inout float Mask)") != std::string::npos);
    CHECK(c.functions.find("Mask = A * 0.5;\nreturn vec2(A, B);") != std::string::npos);
    CHECK(c.pixelBody.find("CustomExpression0(") != std::string::npos);
}

TEST_CASE("Functions outside a function graph and unknown function assets are errors", "[compiler][expr]") {
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId call = Node(g, "MaterialFunctionCall");
    Set(g, call, "function", PropertyValue("/Game/Functions/MF_Missing"));
    const NodeId input = Node(g, "FunctionInput");

    Translator t(m, nullptr, {});
    CHECK(t.CompileOutput(call, 0, Stage::Pixel) < 0);
    CHECK(t.CompileOutput(input, 0, Stage::Pixel) < 0);
    std::set<NodeId> failed;
    for (const CompileError& e : t.Errors()) failed.insert(e.node);
    CHECK(failed.contains(call));
    CHECK(failed.contains(input));
}

TEST_CASE("Static switches pick their branch at compile time", "[compiler][expr]") {
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId sw = Node(g, "StaticSwitchParameter");
    Set(g, sw, "name", PropertyValue("Use Detail"));
    Link(g, Vector(g, "Constant3Vector", {1, 0, 0, 0}), sw, 0);
    Link(g, Constant(g, 0.5), sw, 1);

    Translator off(m, nullptr, {});
    CHECK(off.ChunkAt(off.CompileOutput(sw, 0, Stage::Pixel)).type == ValueType::Float1);
    Translator on(m, nullptr, {{"Use Detail", true}});
    CHECK(on.ChunkAt(on.CompileOutput(sw, 0, Stage::Pixel)).type == ValueType::Float3);
}
