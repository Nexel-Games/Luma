#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Material/GraphAssets.h"
#include "Luma/MaterialCompiler/Preshader.h"
#include "Luma/RHI/MaterialGpuDesc.h"

namespace Luma::Material {
class MaterialLibrary;
}
namespace Luma::ShaderCompiler {
class Compiler;
}

// Graph material -> what a SceneRenderer draws: translate the graph
// (MaterialCompiler), compile every pass's GLSL to SPIR-V (ShaderCompiler,
// synchronously, through its disk cache) and evaluate the parameter block.

namespace Luma::Editor {

// One shader stage of a pass, as generated GLSL.
struct GpuShaderSource {
    GpuPass pass = GpuPass::GBuffer;
    bool vertex = true;
    std::string source;
};

struct GpuMaterialBuild {
    bool ok = false;
    std::string errors;  // translator / shader errors, "; "-separated (empty when ok)
    CompiledMaterialGpu gpu;
    MaterialParameterBlock block;
    // Texture asset (virtual path, "" = none) for each gpu.textures entry; the
    // caller uploads them and fills GpuTextureBinding::texture.
    std::vector<std::string> texturePaths;
    // To re-evaluate the block when parameters or collections change.
    MaterialCompiler::PreshaderProgram preshader;
    MaterialCompiler::ParameterBlockLayout layout;
    // Every stage to compile, in shader-key order, and a fingerprint of all
    // of them (equal fingerprints = the same shader set).
    std::vector<GpuShaderSource> sources;
    std::string fingerprint;
};

// Translation only (fast, main thread): everything but SPIR-V. `ok` and
// `errors` report translator errors as "<node type> - <message>"; gpu.passes
// stay empty for CompileShaderSet / the caller to fill.
GpuMaterialBuild PrepareGpuMaterial(const Material::GraphMaterial& material, Material::MaterialLibrary* library,
                                    std::span<const Material::ParameterOverride> overrides,
                                    std::optional<f32> clip, std::optional<bool> twoSided);

// Folds one stage's cache key into a shader-set key (stage order matters).
u64 FoldShaderKey(u64 setKey, u64 stageKey);
constexpr u64 kShaderKeySeed = 1469598103934665603ull;

// `overrides` are an instance chain's parameter values (static switches pick
// the permutation); `clip` / `twoSided` its setting overrides. `library`
// resolves functions and collections (may be null).
GpuMaterialBuild BuildGpuMaterial(const Material::GraphMaterial& material, Material::MaterialLibrary* library,
                                  std::span<const Material::ParameterOverride> overrides,
                                  std::optional<f32> clip, std::optional<bool> twoSided,
                                  ShaderCompiler::Compiler& shaders, std::string_view label);

}  // namespace Luma::Editor
