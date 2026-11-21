#pragma once

#include <array>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "Luma/Material/GraphAssets.h"
#include "Luma/MaterialCompiler/CompiledMaterial.h"
#include "Luma/MaterialCompiler/Preshader.h"
#include "Luma/MaterialGraph/ExpressionRegistry.h"

namespace Luma::Material {
class MaterialLibrary;
}

// Graph -> typed code chunks -> GLSL (Unreal's FHLSLMaterialTranslator, in
// Luma's own form). One translator compiles one material for one set of
// static-switch values (a permutation).

namespace Luma::MaterialCompiler {

// Static Switch / Static Bool parameter values of a permutation, by name.
// Parameters not listed use their default.
using StaticSwitchSet = std::map<std::string, bool>;
// Permutation key of a static-switch set (order-independent).
u64 StaticSwitchKey(const StaticSwitchSet& set);

class Translator {
public:
    // `library` resolves Material Function calls and Parameter Collections
    // (null: those nodes are errors).
    Translator(const Material::GraphMaterial& material, Material::MaterialLibrary* library,
               StaticSwitchSet staticSwitches);

    // Follows the link into `inputPin` of `node`, or uses the pin's default
    // (or its "in.<Pin>" override). -1 on error.
    i32 CompileInput(MaterialGraph::NodeId node, u32 inputPin, Stage stage);
    // Compiles an output pin of a node; cached per (node, pin, stage).
    i32 CompileOutput(MaterialGraph::NodeId node, u32 outputPin, Stage stage);

    // --- Emission helpers used by expressions ---------------------------
    i32 Constant(const Math::Vec4& v, MaterialGraph::ValueType type);
    // + - * / with Unreal's rule: a Float1 broadcasts, otherwise widths match.
    i32 Binary(std::string_view op, i32 a, i32 b, MaterialGraph::NodeId at);
    // A GLSL call; frequency is the highest of the arguments' (at least
    // `minFreq`, e.g. Varying for derivatives).
    i32 Call(std::string_view fn, std::span<const i32> args, MaterialGraph::ValueType result,
             MaterialGraph::NodeId at, Frequency minFreq = Frequency::Constant);
    // Float1 broadcasts to any width; wider -> narrower truncates with a
    // warning; anything else is an error. -1 on error.
    i32 Cast(i32 chunk, MaterialGraph::ValueType to, MaterialGraph::NodeId at);
    // A new chunk of arbitrary code (no folding).
    i32 Emit(std::string code, MaterialGraph::ValueType type, Frequency freq);

    // --- Uniform expressions (preshader) ------------------------------------
    // True for Constant chunks and Uniform chunks the preshader computes.
    bool Preshadable(i32 chunk) const;
    // A Uniform chunk computed on the CPU by `op` over the operands; -1 when
    // an operand isn't preshadable (the caller falls back to GLSL). `extra`
    // holds the op's inline operands (Dot width, Swizzle count + lanes,
    // Append widths).
    i32 Preshade(PreOp op, std::initializer_list<i32> operands, MaterialGraph::ValueType result,
                 std::array<u8, 5> extra = {});
    // A collection parameter's current value (Uniform, preshaded).
    i32 CollectionValue(const std::string& collection, const std::string& parameter,
                        MaterialGraph::ValueType type);

    // A chunk for code that needs no local variable (an existing local,
    // interpolant or uniform read).
    i32 Reference(std::string code, MaterialGraph::ValueType type, Frequency freq);
    // Appends a raw statement to the current stage body.
    void Statement(std::string code);
    // Runs `make` once per (node, key, stage) and reuses its chunk (shared
    // work of multi-output nodes such as a texture sample).
    i32 Memo(MaterialGraph::NodeId node, std::string_view key, const std::function<i32()>& make);
    // Adds a generated function definition; returns its sequence number.
    u32 AddFunction(std::string definition);
    u32 FunctionCount() const { return m_functionCount; }
    void AddInclude(std::string include);
    // Registers a vertex -> pixel interpolant; returns its index.
    u32 AddInterpolator(MaterialGraph::ValueType type, std::string vertexCode);
    // The permutation's value of a Static Switch / Static Bool parameter.
    bool StaticSwitch(const std::string& name, bool defaultValue) const;

    // --- Parameters and textures (parameter-block slots) --------------------
    // Declares (or re-uses) a parameter and returns a Uniform chunk reading it.
    i32 ScalarParameter(ParameterInfo info);
    i32 VectorParameter(ParameterInfo info);  // Float4
    // Declares a parameter that has no block slot (Static Switch / Bool).
    // False (and an error at `info.node`) when the name is taken by another kind.
    bool DeclareParameter(ParameterInfo info);
    // Adds a texture slot; returns a Texture2D / TextureCube chunk whose code
    // is the slot index. `param` names a Texture parameter ("" = fixed).
    i32 Texture(TextureSlot slot, const ParameterInfo* param);
    const ParameterBlockLayout& Layout() const { return m_layout; }

    // --- Material functions -------------------------------------------------
    // Compiles `outputNode` (a Function Output of `function`) inline for the
    // call node `call` of the current graph. Each call gets its own scope, so
    // two calls never share chunks. Errors (self-recursion, depth > 16) are
    // reported at the call.
    i32 CompileFunctionOutput(MaterialGraph::NodeId call, const std::string& path,
                              const Material::MaterialFunction& function, MaterialGraph::NodeId outputNode);
    // Inside a function: compiles the call's input `pin` in the caller's
    // scope. `linked` is false (and nothing compiles) when it is unconnected.
    i32 CompileCallerInput(u32 pin, bool& linked);
    // The function being compiled (null at material level).
    const Material::MaterialFunction* CurrentFunction() const;
    // The graph nodes resolve against: the material's or the current function's.
    const MaterialGraph::Graph& CurrentGraph() const;
    // 0 at material level; a distinct id per function call instance (for
    // generated names that must not collide between calls).
    u32 ScopeId() const { return m_scope; }

    // Errors and warnings raised inside a function are reported at the
    // material-level call node, prefixed with the function's name.
    void Error(MaterialGraph::NodeId node, i32 pin, std::string message);
    void Warning(MaterialGraph::NodeId node, i32 pin, std::string message);
    const std::vector<CompileError>& Errors() const { return m_errors; }

    const Chunk& ChunkAt(i32 index) const { return m_chunks[static_cast<usize>(index)]; }
    Stage CurrentStage() const { return m_stage; }
    const Material::GraphMaterial& MaterialAsset() const { return m_material; }
    Material::MaterialLibrary* Library() const { return m_library; }
    const StaticSwitchSet& StaticSwitches() const { return m_switches; }

    // Compiles every Material Output input the material's settings use.
    CompiledMaterial Finish();

private:
    i32 AddChunk(std::string code, MaterialGraph::ValueType type, Frequency freq,
                 std::optional<Math::Vec4> constant);
    i32 PreNodeOf(i32 chunk);
    i32 UniformFromPre(i32 node, MaterialGraph::ValueType type);
    u16 PreParam(const std::string& name, bool vector, const Math::Vec4& def);
    // Replaces uniform / texture placeholders in generated text with their
    // slots, assigning slots (and preshader programs) in first-use order.
    std::string Resolve(const std::string& text, CompiledMaterial& out);
    void EmitPreshader(i32 node, PreshaderProgram& program);

    struct PreNode {
        PreOp op = PreOp::PushConst;
        std::vector<i32> args;
        u16 index = 0;  // parameter / collection-value index
        Math::Vec4 constant{0.0f, 0.0f, 0.0f, 0.0f};
        std::array<u8, 5> extra{};
    };

    struct Scope {
        const MaterialGraph::Graph* graph = nullptr;
        const Material::MaterialFunction* function = nullptr;  // null for the material
        std::string path;
        u32 parent = 0;
        MaterialGraph::NodeId call = 0;  // call node in the parent scope
        u32 depth = 0;
    };
    // Maps a node of the current scope to what the user sees (the
    // material-level call node) and prefixes the message with the call path.
    std::pair<MaterialGraph::NodeId, std::string> Attribute(MaterialGraph::NodeId node, std::string message) const;

    const Material::GraphMaterial& m_material;
    Material::MaterialLibrary* m_library;
    std::vector<Scope> m_scopes;  // [0] = the material
    u32 m_scope = 0;
    std::map<std::pair<u32, MaterialGraph::NodeId>, u32> m_callScopes;  // (scope, call node) -> scope
    StaticSwitchSet m_switches;
    Stage m_stage = Stage::Pixel;
    std::vector<Chunk> m_chunks;
    std::string m_body[2];  // per stage
    u32 m_nextLocal = 0;
    std::map<std::tuple<u32, MaterialGraph::NodeId, u32, Stage>, i32> m_cache;  // (scope, node, pin, stage)
    std::set<std::tuple<u32, MaterialGraph::NodeId, Stage>> m_inProgress;
    std::vector<CompileError> m_errors, m_warnings;
    std::map<std::tuple<u32, MaterialGraph::NodeId, std::string, Stage>, i32> m_memo;
    std::string m_functions;
    u32 m_functionCount = 0;
    std::vector<std::string> m_includes;
    std::vector<Interpolator> m_interpolators;
    std::vector<ParameterInfo> m_parameters;
    ParameterBlockLayout m_layout;          // filled by Finish
    std::vector<PreNode> m_pre;
    std::vector<PreshaderParam> m_preParams;
    std::vector<std::pair<std::string, std::string>> m_collectionValues;
    std::vector<TextureSlot> m_textures;    // distinct bindings; slots assigned by Finish
    std::map<i32, u32> m_uniformSlots;      // uniform chunk -> block slot
    std::map<u32, u32> m_textureSlots;      // m_textures index -> block slot
};

}  // namespace Luma::MaterialCompiler
