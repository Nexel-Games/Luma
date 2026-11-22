// Material functions (compiled inline per call) and parameter collections
// (preshaded uniform reads) in the material compiler.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Material/GraphAssets.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialCompiler/Functions.h"
#include "Luma/MaterialCompiler/Preshader.h"
#include "Luma/MaterialCompiler/Translator.h"

namespace fs = std::filesystem;
using Catch::Approx;
using namespace Luma;
using namespace Luma::MaterialCompiler;
using Luma::Material::GraphMaterial;
using Luma::Material::MaterialFunction;
using Luma::Material::MaterialLibrary;
using Luma::Material::ParameterCollection;
using Luma::MaterialGraph::Graph;
using Luma::MaterialGraph::NodeId;
using Luma::MaterialGraph::PinRef;
using Luma::MaterialGraph::PropertyValue;

namespace {

constexpr u32 kMetallic = 1;
constexpr u32 kRoughness = 3;

struct Library {
    fs::path root = fs::temp_directory_path() / "luma_function_compile_test";
    AssetPathResolver paths;
    std::unique_ptr<MaterialLibrary> lib;
    Library() {
        fs::remove_all(root);
        fs::create_directories(root / "Assets");
        paths.SetGameRoot(root / "Assets");
        paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
        lib = std::make_unique<MaterialLibrary>(paths);
    }
    ~Library() {
        lib.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

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
NodeId Input(Graph& g, const char* name, i64 inputType = 0) {
    const NodeId n = Node(g, "FunctionInput");
    Set(g, n, "name", PropertyValue(name));
    Set(g, n, "inputType", PropertyValue(inputType));
    return n;
}
NodeId Output(Graph& g, const char* name) {
    const NodeId n = Node(g, "FunctionOutput");
    Set(g, n, "name", PropertyValue(name));
    return n;
}
NodeId Call(Graph& g, const char* path) {
    const NodeId n = Node(g, "MaterialFunctionCall");
    Set(g, n, "function", PropertyValue(path));
    return n;
}

// MF_Double: Result = X * 2.
MaterialFunction Double() {
    MaterialFunction f;
    f.name = "MF_Double";
    const NodeId x = Input(f.graph, "X");
    const NodeId mul = Node(f.graph, "Multiply");
    Link(f.graph, x, mul, 0);
    Link(f.graph, Constant(f.graph, 2.0), mul, 1);
    Link(f.graph, mul, Output(f.graph, "Result"), 0);
    return f;
}

const MaterialOutputValue* OutputFor(const CompiledMaterial& c, const char* pin) {
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

}  // namespace

TEST_CASE("A function used twice compiles once per call and folds each", "[compiler][function]") {
    Library L;
    L.lib->SetFunction("/Game/MF_Double", Double());
    GraphMaterial m;
    Graph& g = m.graph;
    const NodeId a = Call(g, "/Game/MF_Double");
    const NodeId b = Call(g, "/Game/MF_Double");
    Link(g, Constant(g, 3.0), a, 0);
    Link(g, Constant(g, 4.0), b, 0);
    Link(g, a, g.OutputNode(), kMetallic);
    Link(g, b, g.OutputNode(), kRoughness);

    const CompiledMaterial c = Translator(m, L.lib.get(), {}).Finish();
    REQUIRE(c.Ok());
    REQUIRE(OutputFor(c, "Metallic"));
    REQUIRE(OutputFor(c, "Roughness"));
    CHECK(OutputFor(c, "Metallic")->code == "6.0");
    CHECK(OutputFor(c, "Roughness")->code == "8.0");
}

TEST_CASE("A function can call another function", "[compiler][function]") {
    Library L;
    L.lib->SetFunction("/Game/MF_Double", Double());
    MaterialFunction quad;
    quad.name = "MF_Quad";
    const NodeId x = Input(quad.graph, "X");
    const NodeId first = Call(quad.graph, "/Game/MF_Double");
    const NodeId second = Call(quad.graph, "/Game/MF_Double");
    Link(quad.graph, x, first, 0);
    Link(quad.graph, first, second, 0);
    Link(quad.graph, second, Output(quad.graph, "Result"), 0);
    L.lib->SetFunction("/Game/MF_Quad", quad);

    GraphMaterial m;
    const NodeId call = Call(m.graph, "/Game/MF_Quad");
    Link(m.graph, Constant(m.graph, 3.0), call, 0);
    Link(m.graph, call, m.graph.OutputNode(), kMetallic);

    const CompiledMaterial c = Translator(m, L.lib.get(), {}).Finish();
    REQUIRE(c.Ok());
    CHECK(OutputFor(c, "Metallic")->code == "12.0");
}

TEST_CASE("A function that calls itself is an error at the material's call node", "[compiler][function]") {
    Library L;
    MaterialFunction loop;
    loop.name = "MF_Loop";
    const NodeId x = Input(loop.graph, "X");
    const NodeId inner = Call(loop.graph, "/Game/MF_Loop");
    Link(loop.graph, x, inner, 0);
    Link(loop.graph, inner, Output(loop.graph, "Result"), 0);
    L.lib->SetFunction("/Game/MF_Loop", loop);

    GraphMaterial m;
    const NodeId call = Call(m.graph, "/Game/MF_Loop");
    Link(m.graph, call, m.graph.OutputNode(), kMetallic);

    const CompiledMaterial c = Translator(m, L.lib.get(), {}).Finish();
    CHECK_FALSE(c.Ok());
    CHECK(HasErrorAt(c, call));
}

TEST_CASE("An unconnected function input uses its preview value as the default", "[compiler][function]") {
    Library L;
    MaterialFunction f = Double();
    for (const auto& n : f.graph.Nodes()) {
        if (n.type == "FunctionInput") f.graph.Find(n.id)->properties["in.Preview"] = PropertyValue(1.0);
    }
    L.lib->SetFunction("/Game/MF_Double", f);
    GraphMaterial m;
    const NodeId call = Call(m.graph, "/Game/MF_Double");
    Link(m.graph, call, m.graph.OutputNode(), kMetallic);

    const CompiledMaterial c = Translator(m, L.lib.get(), {}).Finish();
    REQUIRE(c.Ok());
    CHECK(OutputFor(c, "Metallic")->code == "2.0");
}

TEST_CASE("An unconnected input without a preview default is an error at the call", "[compiler][function]") {
    Library L;
    MaterialFunction f = Double();
    for (const auto& n : f.graph.Nodes()) {
        if (n.type == "FunctionInput") f.graph.Find(n.id)->properties["usePreviewAsDefault"] = PropertyValue(false);
    }
    L.lib->SetFunction("/Game/MF_Double", f);
    GraphMaterial m;
    const NodeId call = Call(m.graph, "/Game/MF_Double");
    Link(m.graph, call, m.graph.OutputNode(), kMetallic);

    const CompiledMaterial c = Translator(m, L.lib.get(), {}).Finish();
    CHECK(HasErrorAt(c, call));
}

TEST_CASE("A missing function asset is an error at the call", "[compiler][function]") {
    Library L;
    GraphMaterial m;
    const NodeId call = Call(m.graph, "/Game/MF_Nowhere");
    Link(m.graph, call, m.graph.OutputNode(), kMetallic);
    const CompiledMaterial c = Translator(m, L.lib.get(), {}).Finish();
    CHECK(HasErrorAt(c, call));
}

TEST_CASE("Function pins are ordered by sort priority, then creation", "[compiler][function]") {
    MaterialFunction f;
    const NodeId b = Input(f.graph, "B", 2);
    const NodeId a = Input(f.graph, "A", 0);
    f.graph.Find(a)->properties["sortPriority"] = PropertyValue(i64{-1});
    Output(f.graph, "Out");
    const FunctionSignature s = SignatureOf(f);
    REQUIRE(s.inputs.size() == 2);
    CHECK(s.inputs[0].name == "A");
    CHECK(s.inputs[0].type == MaterialGraph::ValueType::Float1);
    CHECK(s.inputs[1].name == "B");
    CHECK(s.inputs[1].node == b);
    CHECK(s.inputs[1].type == MaterialGraph::ValueType::Float3);
    REQUIRE(s.outputs.size() == 1);
    CHECK(s.outputs[0].name == "Out");
}

TEST_CASE("A collection parameter is a uniform the collection value drives without recompiling",
          "[compiler][function][collection]") {
    Library L;
    ParameterCollection pc;
    pc.name = "MPC_Wind";
    pc.scalars.push_back({"Strength", 0.5f});
    L.lib->SetCollection("/Game/MPC_Wind", pc);

    GraphMaterial m;
    const NodeId n = Node(m.graph, "CollectionParameter");
    Set(m.graph, n, "collection", PropertyValue("/Game/MPC_Wind"));
    Set(m.graph, n, "parameter", PropertyValue("Strength"));
    Link(m.graph, n, m.graph.OutputNode(), kMetallic);

    const CompiledMaterial c = Translator(m, L.lib.get(), {}).Finish();
    REQUIRE(c.Ok());
    CHECK(OutputFor(c, "Metallic")->code == "MP(0).x");
    REQUIRE(c.preshader.collectionValues.size() == 1);
    CHECK(c.preshader.collectionValues[0].first == "/Game/MPC_Wind");

    const ParameterValueSource source = CollectionValues(*L.lib);
    CHECK(EvaluatePreshader(c.preshader, c.layout, {}, source, {})[0].x == Approx(0.5f));
    REQUIRE(L.lib->SetCollectionValue("/Game/MPC_Wind", "Strength", {0.9f, 0, 0, 0}));
    CHECK(EvaluatePreshader(c.preshader, c.layout, {}, source, {})[0].x == Approx(0.9f));
    // The GLSL does not depend on the value.
    CHECK(Translator(m, L.lib.get(), {}).Finish().Fingerprint() == c.Fingerprint());
}

TEST_CASE("A collection parameter naming an unknown parameter is an error", "[compiler][function][collection]") {
    Library L;
    ParameterCollection pc;
    pc.vectors.push_back({"Tint", {1, 0, 0, 1}});
    L.lib->SetCollection("/Game/MPC_Look", pc);
    GraphMaterial m;
    const NodeId n = Node(m.graph, "CollectionParameter");
    Set(m.graph, n, "collection", PropertyValue("/Game/MPC_Look"));
    Set(m.graph, n, "parameter", PropertyValue("Missing"));
    Link(m.graph, n, m.graph.OutputNode(), kMetallic);
    const CompiledMaterial c = Translator(m, L.lib.get(), {}).Finish();
    CHECK(HasErrorAt(c, n));
}
