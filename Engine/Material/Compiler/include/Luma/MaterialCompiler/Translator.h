#pragma once

#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "Luma/Material/GraphAssets.h"
#include "Luma/MaterialCompiler/CompiledMaterial.h"
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

class Translator {
public:
    Translator(const Material::GraphMaterial& material, const Material::MaterialLibrary* library,
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

    void Error(MaterialGraph::NodeId node, i32 pin, std::string message);
    void Warning(MaterialGraph::NodeId node, i32 pin, std::string message);

    const Chunk& ChunkAt(i32 index) const { return m_chunks[static_cast<usize>(index)]; }
    Stage CurrentStage() const { return m_stage; }
    const Material::GraphMaterial& MaterialAsset() const { return m_material; }
    const Material::MaterialLibrary* Library() const { return m_library; }
    const StaticSwitchSet& StaticSwitches() const { return m_switches; }

    // Compiles every Material Output input the material's settings use.
    CompiledMaterial Finish();

private:
    i32 AddChunk(std::string code, MaterialGraph::ValueType type, Frequency freq,
                 std::optional<Math::Vec4> constant);

    const Material::GraphMaterial& m_material;
    const Material::MaterialLibrary* m_library;
    StaticSwitchSet m_switches;
    Stage m_stage = Stage::Pixel;
    std::vector<Chunk> m_chunks;
    std::string m_body[2];  // per stage
    u32 m_nextLocal = 0;
    std::map<std::tuple<MaterialGraph::NodeId, u32, Stage>, i32> m_cache;
    std::set<std::pair<MaterialGraph::NodeId, Stage>> m_inProgress;
    std::vector<CompileError> m_errors, m_warnings;
};

}  // namespace Luma::MaterialCompiler
