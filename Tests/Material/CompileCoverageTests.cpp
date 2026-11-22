// Every catalogue expression, in every pass its domain draws in, produces GLSL
// that shaderc accepts with the engine's pass templates. Catches template /
// code-generation breakage for expressions no other test renders.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "Luma/Asset/AssetPath.h"
#include "Luma/Material/GraphAssets.h"
#include "Luma/Material/MaterialLibrary.h"
#include "Luma/MaterialCompiler/Translator.h"
#include "Luma/MaterialGraph/ExpressionRegistry.h"
#include "Luma/ShaderCompiler/ShaderCompiler.h"

namespace fs = std::filesystem;
using namespace Luma;
using namespace Luma::MaterialCompiler;
using Luma::Material::GraphMaterial;
using Luma::MaterialGraph::ExpressionDesc;
using Luma::MaterialGraph::ExpressionRegistry;
using Luma::MaterialGraph::Graph;
using Luma::MaterialGraph::NodeId;
using Luma::MaterialGraph::PinRef;
using Luma::MaterialGraph::PropertyValue;
using Luma::MaterialGraph::ValueType;

namespace {

constexpr u32 kBaseColor = 0;
constexpr u32 kEmissive = 5;
constexpr u32 kWorldPositionOffset = 10;

NodeId Node(Graph& g, const char* type) { return g.AddNode(type, {0.0f, 0.0f}); }
void Set(Graph& g, NodeId n, const char* key, PropertyValue v) { g.Find(n)->properties[key] = std::move(v); }
void Link(Graph& g, NodeId from, NodeId to, u32 toPin, u32 fromPin = 0) {
    std::string err;
    REQUIRE(g.Connect(PinRef{from, fromPin}, PinRef{to, toPin}, &err));
}

struct Domain {
    const char* name;
    Material::MaterialDomain domain;
    Material::BlendMode blend;
};
constexpr Domain kDomains[] = {
    {"Surface/Opaque", Material::MaterialDomain::Surface, Material::BlendMode::Opaque},
    {"Surface/Masked", Material::MaterialDomain::Surface, Material::BlendMode::Masked},
    {"Surface/Translucent", Material::MaterialDomain::Surface, Material::BlendMode::Translucent},
    {"PostProcess", Material::MaterialDomain::PostProcess, Material::BlendMode::Opaque},
};

struct Library {
    fs::path root = fs::temp_directory_path() / "luma_compile_coverage_test";
    AssetPathResolver paths;
    std::unique_ptr<Material::MaterialLibrary> lib;
    Library() {
        fs::remove_all(root);
        fs::create_directories(root / "Assets");
        paths.SetGameRoot(root / "Assets");
        paths.SetEngineRoot(fs::path(LUMA_SOURCE_DIR) / "Content" / "Engine");
        lib = std::make_unique<Material::MaterialLibrary>(paths);
        Material::MaterialFunction f;
        f.name = "MF_Half";
        const NodeId in = Node(f.graph, "FunctionInput");
        Set(f.graph, in, "inputType", PropertyValue(i64{0}));
        const NodeId mul = Node(f.graph, "Multiply");
        Link(f.graph, in, mul, 0);
        const NodeId out = Node(f.graph, "FunctionOutput");
        Link(f.graph, mul, out, 0);
        lib->SetFunction("/Game/MF_Half", f);
        Material::ParameterCollection pc;
        pc.vectors.push_back({"Tint", {1, 0.5f, 0.25f, 1}});
        lib->SetCollection("/Game/MPC_Look", pc);
    }
    ~Library() {
        lib.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

// A varying scalar (UV.x), so expressions emit GLSL instead of folding.
NodeId VaryingScalar(Graph& g) {
    const NodeId uv = Node(g, "TextureCoordinate");
    const NodeId mask = Node(g, "ComponentMask");
    Set(g, mask, "g", PropertyValue(false));
    Link(g, uv, mask, 0);
    return mask;
}

// Builds `type` with its inputs fed (varying floats, textures, static bools)
// and returns the node whose output 0 is a float of `width` (0 = none).
NodeId Build(GraphMaterial& m, const ExpressionDesc& d, bool feedVarying) {
    Graph& g = m.graph;
    const NodeId n = Node(g, d.type.c_str());
    if (d.type == "MaterialFunctionCall") Set(g, n, "function", PropertyValue("/Game/MF_Half"));
    if (d.type == "CollectionParameter") {
        Set(g, n, "collection", PropertyValue("/Game/MPC_Look"));
        Set(g, n, "parameter", PropertyValue("Tint"));
    }
    if (d.type == "NamedRerouteUsage") {
        const NodeId decl = Node(g, "NamedRerouteDeclaration");
        if (feedVarying) Link(g, VaryingScalar(g), decl, 0);
    }
    for (u32 pin = 0; pin < d.inputs.size(); ++pin) {
        const MaterialGraph::PinDesc& p = d.inputs[pin];
        const bool texturePin = p.type == ValueType::Texture2D || p.type == ValueType::TextureCube ||
                                p.name == "Tex" || p.name == "Texture";
        if (texturePin) {
            const NodeId tex = Node(g, "TextureObject");
            Set(g, tex, "texture", PropertyValue("/Game/T_Coverage"));
            if (p.type != ValueType::TextureCube) Link(g, tex, n, pin);
            continue;
        }
        if (p.type == ValueType::Bool) {
            Link(g, Node(g, "StaticBool"), n, pin);
            continue;
        }
        if (!feedVarying) continue;
        // Component Mask selects R and G by default: it needs a Float2.
        Link(g, d.type == "ComponentMask" ? Node(g, "TextureCoordinate") : VaryingScalar(g), n, pin);
    }
    if (d.type == "MaterialFunctionCall" && feedVarying) Link(g, VaryingScalar(g), n, 0);
    return n;
}

// Adapts output 0 of `n` (of chunk type `type`) to a Float3.
std::optional<NodeId> AsFloat3(Graph& g, NodeId n, ValueType type) {
    switch (type) {
        case ValueType::Float1:
        case ValueType::Float3: return n;
        case ValueType::Float2: {
            const NodeId append = Node(g, "AppendVector");
            Link(g, n, append, 0);
            return append;
        }
        case ValueType::Float4: {
            const NodeId mask = Node(g, "ComponentMask");
            Set(g, mask, "b", PropertyValue(true));
            Link(g, n, mask, 0);
            return mask;
        }
        case ValueType::Texture2D: {
            const NodeId sample = Node(g, "TextureSample");
            Link(g, n, sample, 1);
            return sample;
        }
        default: return std::nullopt;
    }
}

struct Job {
    std::string expression, where;
};

}  // namespace

TEST_CASE("Every expression compiles to valid GLSL in every pass of its domains", "[compiler][coverage]") {
    Library L;
    const fs::path shaders = fs::path(LUMA_SOURCE_DIR) / "Engine" / "Rendering" / "Vulkan" / "shaders";
    const fs::path cache = fs::temp_directory_path() / "luma_compile_coverage_cache";
    fs::remove_all(cache);
    ShaderCompiler::Compiler compiler(shaders, cache, std::max(2u, std::thread::hardware_concurrency()));

    std::vector<std::string> failures;
    std::map<u64, Job> jobs;  // ticket -> what it compiles
    std::map<std::string, u64> submitted;  // source -> ticket (identical sources compile once)
    u32 sources = 0;

    for (const ExpressionDesc* d : ExpressionRegistry::Get().All()) {
        if (d->type == "MaterialOutput" || d->type == "FunctionInput" || d->type == "FunctionOutput") continue;
        bool compiledSomewhere = false;
        std::string lastError;
        for (const Domain& domain : kDomains) {
            for (const bool feedVarying : {true, false}) {
                GraphMaterial m;
                m.settings.domain = domain.domain;
                m.settings.blendMode = domain.blend;
                if (domain.blend == Material::BlendMode::Translucent) {
                    m.settings.refraction = Material::RefractionMethod::IndexOfRefraction;
                }
                const NodeId n = Build(m, *d, feedVarying);
                // Probe output 0's type to adapt it.
                Translator probe(m, L.lib.get(), {});
                const i32 chunk = d->outputs.empty() ? -1 : probe.CompileOutput(n, 0, Stage::Pixel);
                if (chunk < 0) {
                    lastError = probe.Errors().empty() ? "no output" : probe.Errors().front().message;
                    continue;
                }
                const std::optional<NodeId> value = AsFloat3(m.graph, n, probe.ChunkAt(chunk).type);
                if (value) {
                    const bool post = domain.domain == Material::MaterialDomain::PostProcess;
                    Link(m.graph, *value, m.graph.OutputNode(), post ? kEmissive : kBaseColor);
                    if (!post && !d->pixelOnly) {
                        Link(m.graph, *value, m.graph.OutputNode(), kWorldPositionOffset);
                    }
                    if (domain.blend == Material::BlendMode::Masked) {
                        Link(m.graph, *value, m.graph.OutputNode(), 7);  // Opacity Mask
                    }
                }
                const CompiledMaterial c = Translator(m, L.lib.get(), {}).Finish();
                if (!c.Ok()) {
                    lastError = std::string(domain.name) + ": " + c.errors.front().message;
                    continue;
                }
                compiledSomewhere = true;
                for (usize pass = 0; pass < c.passes.size(); ++pass) {
                    if (!c.passes[pass]) continue;
                    for (const bool vertex : {true, false}) {
                        ShaderCompiler::CompileRequest r;
                        r.source = vertex ? c.passes[pass]->vertex : c.passes[pass]->fragment;
                        r.stage = vertex ? ShaderCompiler::ShaderStage::Vertex : ShaderCompiler::ShaderStage::Fragment;
                        r.name = d->type + (vertex ? ".vert" : ".frag");
                        ++sources;
                        if (submitted.contains(r.source)) continue;
                        const std::string where = std::string(domain.name) + (feedVarying ? " varying" : " defaults") +
                                                  " pass " + std::to_string(pass) + (vertex ? " vertex" : " pixel");
                        const std::string source = r.source;
                        const u64 ticket = compiler.Submit(std::move(r));
                        submitted[source] = ticket;
                        jobs[ticket] = {d->type, where};
                    }
                }
                break;  // fed variant compiled; the defaults variant adds nothing
            }
        }
        if (!compiledSomewhere) failures.push_back(d->type + " compiles in no domain: " + lastError);
    }

    usize done = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (done < jobs.size() && std::chrono::steady_clock::now() < deadline) {
        for (const auto& [ticket, result] : compiler.Poll()) {
            ++done;
            if (result.ok) continue;
            const Job& job = jobs.at(ticket);
            failures.push_back(job.expression + " [" + job.where + "]: " +
                               (result.errors.empty() ? result.log : result.errors.front().second));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(done == jobs.size());

    std::string report;
    for (const std::string& f : failures) report += f + "\n";
    INFO(sources << " sources, " << jobs.size() << " distinct\n" << report);
    CHECK(failures.empty());
    CHECK(jobs.size() > 100);
    std::error_code ec;
    fs::remove_all(cache, ec);
}

